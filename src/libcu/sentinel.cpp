#include <sentinel.h>
#include <ext/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <atomic>
#include <thread>
#include <cuda_runtimecu.h>
#if __OS_WIN
#include <windows.h>
#include <io.h>
#elif __OS_UNIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif

static sentinelContext _ctx;
static sentinelExecutor _baseHostExecutor = { nullptr, "base", sentinelDefaultHostExecutor, nullptr };
static sentinelExecutor _baseDeviceExecutor = { nullptr, "base", sentinelDefaultDeviceExecutor, nullptr };

static bool executeTrans(void **tag);

// Where the values a message embeds must end: the message's declared budget, capped at the slot.
// Senders place them at ROUND8_(length), so the host must look there too.
static char *sentinelDataStart(sentinelCommand *cmd) { return cmd->data + ROUND8_(cmd->length); }
static char *sentinelDataEnd(sentinelCommand *cmd, const sentinelMessage *msg) {
	char *end = sentinelDataStart(cmd) + msg->size, *slotEnd = cmd->data + SENTINEL_MSGSIZE;
	return end > slotEnd ? slotEnd : end;
}

// One thread per map. The cancel token is a pointer the shutdown clears; mutexSpinLock gives
// up as soon as it does, so the thread can be joined instead of cancelled.
static void sentinelMapThread(sentinelMap *map, bool forDevice, int threadId, void **token) {
	void *funcTag[3]{ (void *)(intptr_t)threadId, nullptr, nullptr };
	while (*(void *volatile *)token) {
		unsigned int id = mutexAdd(&map->getId, 1);
		sentinelCommand *cmd = &map->cmds[id % SENTINEL_MSGCOUNT];
		if (cmd->magic != SENTINEL_MAGIC) {
			fprintf(stderr, "sentinel: bad magic in slot %u\n", id % SENTINEL_MSGCOUNT);
			abort();
		}
		funcTag[1] = cmd;
		int *control = &cmd->control;
		mutexSpinLock(token, control, SENTINELCONTROL_DEVICERDY, SENTINELCONTROL_HOST, MUTEXPRED_GTE, SENTINELCONTROL_TRAN, executeTrans, funcTag);
		if (!*(void *volatile *)token) break;
		sentinelMessage *msg = (sentinelMessage *)cmd->data;

		// EXECUTE
		char *(*hostPrepare)(void*, char*, char*, intptr_t) = nullptr;
		sentinelExecutor *list = forDevice ? _ctx.deviceList : _ctx.hostList;
		for (sentinelExecutor *exec = list; exec && exec->executor && !exec->executor(exec->tag, msg, cmd->length, &hostPrepare); exec = exec->next) {}

		// host-prepare: results the executor points at are copied into the slot
		if (hostPrepare && !hostPrepare(msg, sentinelDataStart(cmd), sentinelDataEnd(cmd, msg), map->offset)) {
			fprintf(stderr, "sentinel: reply too long for op %d\n", msg->op);
			abort();
		}

		// FLOW-WAIT
		if (msg->flow & SENTINELFLOW_WAIT) {
			mutexSet(control, SENTINELCONTROL_HOSTRDY);
			if (msg->flow & SENTINELFLOW_TRAN) {
				mutexSpinLock(token, control, SENTINELCONTROL_DEVICERDY, SENTINELCONTROL_HOSTWAIT, MUTEXPRED_GTE, SENTINELCONTROL_TRAN, executeTrans, funcTag);
				mutexSet(control, SENTINELCONTROL_NORMAL);
			}
		}
		else mutexSet(control, SENTINELCONTROL_NORMAL);
	}
	if (funcTag[2]) free(funcTag[2]);
}

// EXECUTETRANS: the host side of the chunked transfer. The sender drives the state; this
// allocates the transfer buffer on TRANSIZE and copies chunks in or out until the sender
// leaves the TRAN states.
static bool executeTrans(void **tag) {
	sentinelCommand *cmd = (sentinelCommand *)tag[1];
	int *control = &cmd->control;
	char *data = cmd->data, *ptr = (char *)tag[2];
	while (mutexGet(control) >= SENTINELCONTROL_TRAN) {
		int length = cmd->length;
		switch (mutexGet(control)) {
		case SENTINELCONTROL_TRANSIZE:
			ptr = (char *)tag[2];
			if (ptr) free(ptr);
			if (!(ptr = (char *)malloc(length))) {
				fprintf(stderr, "sentinel: transfer out of memory\n");
				abort();
			}
			tag[2] = *(char **)data = ptr;
			break;
		case SENTINELCONTROL_TRANIN:
			memcpy(ptr, data, length); ptr += length; break;
		case SENTINELCONTROL_TRANOUT:
			memcpy(data, ptr, length); ptr += length; break;
		default:
			mutexPause(1); continue;
		}
		// The sender acknowledges TRANRDY by setting TRAN and may move straight on to the next
		// chunk state, so wait for anything but TRANRDY rather than for TRAN exactly; waiting for
		// TRAN alone deadlocked whenever the acknowledgement was overwritten before it was seen.
		mutexSet(control, SENTINELCONTROL_TRANRDY);
		mutexSpinLock(nullptr, control, SENTINELCONTROL_TRAN, SENTINELCONTROL_TRAN, MUTEXPRED_NE, SENTINELCONTROL_TRANRDY);
	}
	return true;
}

static void sentinelMapInitialize(sentinelMap *map, intptr_t offset) {
	memset(map, 0, sizeof(sentinelMap));
	map->offset = offset;
	for (int j = 0; j < SENTINEL_MSGCOUNT; j++)
		map->cmds[j].magic = SENTINEL_MAGIC;
}

// HOSTSENTINEL: the map lives in a named shared-memory segment so other processes can open it.
#if HAS_HOSTSENTINEL
static std::thread _hostThread;
static void *_hostToken = nullptr;
static void *_hostMap = nullptr;
#if __OS_WIN
static HANDLE _hostMapHandle = NULL;
#elif __OS_UNIX
static char _hostMapName[256];
#endif
#endif

// DEVICESENTINEL: pinned, device-mapped host memory; the device sees it at the same address
// under unified addressing, so offset is normally zero.
#if HAS_DEVICESENTINEL
static std::thread _deviceThreads[SENTINEL_DEVICEMAPS];
static void *_deviceTokens[SENTINEL_DEVICEMAPS];
static sentinelMap *_deviceMapHost[SENTINEL_DEVICEMAPS];
#endif

void sentinelServerInitialize(sentinelExecutor *deviceExecutor, const char *mapHostName, bool hostSentinel, bool deviceSentinel) {
	// an exit() anywhere, including from a sender's error path, must not leave a joinable
	// std::thread behind for the static destructors to terminate on
	static bool atExitRegistered = false;
	if (!atExitRegistered) { atexit(sentinelServerShutdown); atExitRegistered = true; }

#if HAS_HOSTSENTINEL
	if (hostSentinel) {
		// create host map
#if __OS_WIN
		_hostMapHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)sizeof(sentinelMap), mapHostName);
		if (!_hostMapHandle) {
			fprintf(stderr, "sentinel: could not create file mapping %s (%lu)\n", mapHostName, (unsigned long)GetLastError()); exit(1);
		}
		_hostMap = MapViewOfFile(_hostMapHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(sentinelMap));
		if (!_hostMap) {
			fprintf(stderr, "sentinel: could not map view of %s (%lu)\n", mapHostName, (unsigned long)GetLastError()); CloseHandle(_hostMapHandle); _hostMapHandle = NULL; exit(1);
		}
#elif __OS_UNIX
		int fd = shm_open(mapHostName, O_CREAT | O_RDWR, 0666);
		if (fd < 0) { fprintf(stderr, "sentinel: could not create shared memory %s: %s\n", mapHostName, strerror(errno)); exit(1); }
		if (ftruncate(fd, sizeof(sentinelMap)) < 0) { fprintf(stderr, "sentinel: could not size shared memory %s: %s\n", mapHostName, strerror(errno)); close(fd); shm_unlink(mapHostName); exit(1); }
		_hostMap = mmap(nullptr, sizeof(sentinelMap), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
		if (_hostMap == MAP_FAILED) { _hostMap = nullptr; fprintf(stderr, "sentinel: could not map shared memory %s: %s\n", mapHostName, strerror(errno)); shm_unlink(mapHostName); exit(1); }
		snprintf(_hostMapName, sizeof(_hostMapName), "%s", mapHostName);
#endif
		_ctx.hostMap = (sentinelMap *)_hostMap;
		sentinelMapInitialize(_ctx.hostMap, (intptr_t)_ctx.hostMap); // clients subtract the address they map it at

		// register executor
		sentinelRegisterExecutor(&_baseHostExecutor, true, false);

		// launch thread
		_hostToken = _ctx.hostMap;
		_hostThread = std::thread(sentinelMapThread, _ctx.hostMap, false, -1, &_hostToken);
	}
#endif

#if HAS_DEVICESENTINEL
	if (deviceSentinel) {
		// create device maps
		sentinelMap *d_deviceMap[SENTINEL_DEVICEMAPS] = { nullptr };
		for (int i = 0; i < SENTINEL_DEVICEMAPS; i++) {
			cudaErrorCheckF(cudaHostAlloc((void **)&_deviceMapHost[i], sizeof(sentinelMap), cudaHostAllocPortable | cudaHostAllocMapped), goto initialize_error);
			cudaErrorCheckF(cudaHostGetDevicePointer((void **)&d_deviceMap[i], _deviceMapHost[i], 0), goto initialize_error);
			_ctx.deviceMap[i] = _deviceMapHost[i];
			sentinelMapInitialize(_ctx.deviceMap[i], (intptr_t)((char *)_deviceMapHost[i] - (char *)d_deviceMap[i]));
		}
		cudaErrorCheckF(cudaMemcpyToSymbol(_sentinelDeviceMap, d_deviceMap, sizeof(d_deviceMap)), goto initialize_error);

		// register executor
		sentinelRegisterExecutor(&_baseDeviceExecutor, true, true);
		if (deviceExecutor)
			sentinelRegisterExecutor(deviceExecutor, true, true);

		// launch threads
		for (int i = 0; i < SENTINEL_DEVICEMAPS; i++) {
			_deviceTokens[i] = _ctx.deviceMap[i];
			_deviceThreads[i] = std::thread(sentinelMapThread, _ctx.deviceMap[i], true, i, &_deviceTokens[i]);
		}
	}
#endif
	return;
initialize_error:
	fprintf(stderr, "sentinelServerInitialize: error\n");
	sentinelServerShutdown();
	exit(1);
}

void sentinelServerShutdown() {
	// stop the threads first so nothing touches a map while it goes away
#if HAS_HOSTSENTINEL
	_hostToken = nullptr;
	if (_hostThread.joinable()) _hostThread.join();
#endif
#if HAS_DEVICESENTINEL
	for (int i = 0; i < SENTINEL_DEVICEMAPS; i++) {
		_deviceTokens[i] = nullptr;
		if (_deviceThreads[i].joinable()) _deviceThreads[i].join();
	}
#endif
	// close host map
#if HAS_HOSTSENTINEL
#if __OS_WIN
	if (_hostMap) { UnmapViewOfFile(_hostMap); _hostMap = nullptr; }
	if (_hostMapHandle) { CloseHandle(_hostMapHandle); _hostMapHandle = NULL; }
#elif __OS_UNIX
	if (_hostMap) { munmap(_hostMap, sizeof(sentinelMap)); _hostMap = nullptr; shm_unlink(_hostMapName); }
#endif
	_ctx.hostMap = nullptr;
#endif
	// close device maps
#if HAS_DEVICESENTINEL
	for (int i = 0; i < SENTINEL_DEVICEMAPS; i++)
		if (_deviceMapHost[i]) {
			cudaErrorCheckA(cudaFreeHost(_deviceMapHost[i]));
			_deviceMapHost[i] = nullptr;
			_ctx.deviceMap[i] = nullptr;
		}
#endif
}

sentinelExecutor *sentinelFindExecutor(const char *name, bool forDevice) {
	sentinelExecutor *exec = forDevice ? _ctx.deviceList : _ctx.hostList;
	for (; exec && name && strcmp(name, exec->name); exec = exec->next) {}
	return exec;
}

static void sentinelUnlinkExecutor(sentinelExecutor *exec, bool forDevice) {
	sentinelExecutor **head = forDevice ? &_ctx.deviceList : &_ctx.hostList;
	for (sentinelExecutor **p = head; *p; p = &(*p)->next)
		if (*p == exec) { *p = exec->next; break; }
}

void sentinelRegisterExecutor(sentinelExecutor *exec, bool makeDefault, bool forDevice) {
	if (!exec) return;
	sentinelUnlinkExecutor(exec, forDevice);
	sentinelExecutor **head = forDevice ? &_ctx.deviceList : &_ctx.hostList;
	if (makeDefault || !*head) {
		exec->next = *head;
		*head = exec;
	}
	else {
		exec->next = (*head)->next;
		(*head)->next = exec;
	}
}

void sentinelUnregisterExecutor(sentinelExecutor *exec, bool forDevice) {
	if (exec) sentinelUnlinkExecutor(exec, forDevice);
}
