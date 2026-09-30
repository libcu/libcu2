// The client side of the Sentinel host bus. This is a source file meant to be #included by a
// host program (the d* tools do), so those programs need only the libcu headers.
#include <sentinel.h>
#include <sentinel-hostmsg.h>
#include <ext/mutex.h>
#if __OS_WIN
#include <windows.h>
#elif __OS_UNIX
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif
#include <stdio.h>

#if HAS_HOSTSENTINEL

static void executeTrans(char id, sentinelCommand *cmd, int size, sentinelInPtr *listIn, sentinelOutPtr *listOut, intptr_t offset, char *&trans);

static char *preparePtrs(sentinelInPtr *ptrsIn, sentinelOutPtr *ptrsOut, sentinelCommand *cmd, char *data, char *dataEnd, intptr_t offset, sentinelOutPtr *&listOut_, char *&trans) {
	sentinelInPtr *i; sentinelOutPtr *o; char **field; char *ptr = data, *next;
	// PREPARE & TRANSFER
	int transSize = 0;
	sentinelInPtr *listIn = nullptr;
	if (ptrsIn)
		for (i = ptrsIn, field = (char **)i->field; field; i++, field = (char **)i->field) {
			if (!*field) continue;
			int size = i->size != -1 ? i->size : (i->size = (int)strlen(*field) + 1);
			next = ptr + size;
			if (!size) *field = nullptr;
			else if (next <= dataEnd) { i->unknown = ptr; ptr = next; }
			else { i->unknown = listIn; listIn = i; transSize += size; }
		}
	sentinelOutPtr *listOut = nullptr;
	if (ptrsOut) {
		if (ptrsOut[0].field != (char *)-1) ptr = data;
		else ptrsOut++; // { -1 } = append
		for (o = ptrsOut, field = (char **)o->field; field; o++, field = (char **)o->field) {
			int size = o->size != -1 ? o->size : (o->size = (int)(dataEnd - ptr));
			next = ptr + size;
			if (!size) *field = nullptr;
			else if (next <= dataEnd) { *field = ptr + offset; o->unknown = (void *)-1; ptr = next; }
			else { o->unknown = listOut; listOut = o; transSize += size; continue; }
		}
	}
	listOut_ = listOut;

	// TRANSFER & PACK
	if (transSize)
		executeTrans(0, cmd, transSize, listIn, listOut, offset, trans); // size & transfer-in
	if (ptrsIn)
		for (i = ptrsIn, field = (char **)i->field; field; i++, field = (char **)i->field) {
			if (!*field || !(ptr = (char *)i->unknown)) continue;
			memcpy(ptr, *field, i->size);
			*field = ptr + offset;
		}
	return data;
}

static bool postfixPtrs(sentinelOutPtr *ptrsOut, sentinelCommand *cmd, intptr_t offset, sentinelOutPtr *listOut, char *&trans) {
	sentinelOutPtr *o; char **field, **buf;
	// UNPACK & TRANSFER
	if (ptrsOut) {
		if (ptrsOut[0].field == (char *)-1) ptrsOut++; // { -1 } = append
		for (o = ptrsOut, field = (char **)o->field; field; o++, field = (char **)o->field) {
			if (o->unknown != (void *)-1) continue;
			if (!*field || !(buf = (char **)o->buf)) continue;
			int size = !o->sizeField ? o->size : *(int *)o->sizeField;
			if (size > 0) memcpy(*buf, *field - offset, size);
		}
	}
	if (listOut)
		executeTrans(1, cmd, 0, nullptr, listOut, offset, trans);
	return true;
}

static sentinelMap *_sentinelClientMap = nullptr;
static intptr_t _sentinelClientMapOffset = 0;
void sentinelClientSend(sentinelMessage *msg, int msgLength, sentinelInPtr *ptrsIn, sentinelOutPtr *ptrsOut) {
	sentinelMap *map = _sentinelClientMap;
	if (!map)
		panic("sentinel: client map not defined. did you call sentinelClientInitialize?");
	if (msgLength <= 0 || ROUND8_(msgLength) > SENTINEL_MSGSIZE)
		panic("sentinel: msg too long (%d)", msgLength);

	// ATTACH: take a ticket and own its slot once the previous occupant has released it
	unsigned int id = mutexAdd(&map->setId, 1);
	sentinelCommand *cmd = &map->cmds[id % SENTINEL_MSGCOUNT];
	if (cmd->magic != SENTINEL_MAGIC)
		panic("sentinel: bad magic");
	mutexAdd((unsigned int *)&cmd->locks, 1);
	int *control = &cmd->control; intptr_t offset = _sentinelClientMapOffset; char *trans = nullptr;
	mutexSpinLock(nullptr, control, SENTINELCONTROL_NORMAL, SENTINELCONTROL_DEVICE);

	// PREPARE
	char *data = cmd->data + ROUND8_(msgLength), *dataEnd = data + msg->size, *slotEnd = cmd->data + SENTINEL_MSGSIZE;
	if (dataEnd > slotEnd) dataEnd = slotEnd;
	sentinelOutPtr *listOut = nullptr;
	if (((ptrsIn || ptrsOut) && !(data = preparePtrs(ptrsIn, ptrsOut, cmd, data, dataEnd, offset, listOut, trans))) ||
		(msg->prepare && !msg->prepare(msg, data, dataEnd, offset)))
		panic("sentinel: msg too long (op %d)", msg->op);
	if (listOut)
		msg->flow |= SENTINELFLOW_TRAN;
	cmd->length = msgLength; memcpy(cmd->data, msg, msgLength);
	mutexSet(control, SENTINELCONTROL_DEVICERDY);

	// FLOW-WAIT
	if (msg->flow & SENTINELFLOW_WAIT) {
		mutexSpinLock(nullptr, control, SENTINELCONTROL_HOSTRDY, SENTINELCONTROL_DEVICEWAIT);
		cmd->length = msgLength; memcpy(msg, cmd->data, msgLength);
		if ((ptrsOut && !postfixPtrs(ptrsOut, cmd, offset, listOut, trans)) ||
			(msg->postfix && !msg->postfix(msg, offset)))
			panic("sentinel: postfix error (op %d)", msg->op);
		mutexSet(control, !listOut ? SENTINELCONTROL_NORMAL : SENTINELCONTROL_DEVICERDY);
	}
	mutexAdd((unsigned int *)&cmd->locks, (unsigned int)-1);
}

static void executeTrans(char id, sentinelCommand *cmd, int size, sentinelInPtr *listIn, sentinelOutPtr *listOut, intptr_t offset, char *&trans) {
	int *control = &cmd->control;
	sentinelInPtr *i; sentinelOutPtr *o; char **field; char *data = cmd->data, *ptr = trans;
	switch (id) {
	case 0:
		cmd->length = size;
		mutexSet(control, SENTINELCONTROL_TRANSIZE);
		mutexSpinLock(nullptr, control, SENTINELCONTROL_TRANRDY, SENTINELCONTROL_TRAN);
		ptr = trans = *(char **)data;
		if (listIn)
			for (i = listIn; i; i = (sentinelInPtr *)i->unknown) {
				field = (char **)i->field;
				const char *v = (const char *)*field; int remain = i->size, length = 0;
				while (remain > 0) {
					length = cmd->length = remain > SENTINEL_MSGSIZE ? SENTINEL_MSGSIZE : remain;
					memcpy(data, (void *)v, length); remain -= length; v += length;
					mutexSet(control, SENTINELCONTROL_TRANIN);
					mutexSpinLock(nullptr, control, SENTINELCONTROL_TRANRDY, SENTINELCONTROL_TRAN);
				}
				*field = ptr; ptr += i->size;
				i->unknown = nullptr;
			}
		if (listOut) {
			for (o = listOut; o; o = (sentinelOutPtr *)o->unknown) {
				field = (char **)o->field;
				*field = ptr; ptr += o->size;
			}
		}
		break;
	case 1:
		if (listOut)
			for (o = listOut; o; o = (sentinelOutPtr *)o->unknown) {
				field = (char **)o->buf;
				const char *v = (const char *)*field; int remain = o->size, length = 0;
				while (remain > 0) {
					length = cmd->length = remain > SENTINEL_MSGSIZE ? SENTINEL_MSGSIZE : remain;
					mutexSet(control, SENTINELCONTROL_TRANOUT);
					mutexSpinLock(nullptr, control, SENTINELCONTROL_TRANRDY, SENTINELCONTROL_TRAN);
					memcpy((void *)v, data, length); remain -= length; v += length;
				}
				o->unknown = nullptr;
			}
		break;
	}
}

static void *_clientMap = nullptr;
#if __OS_WIN
static HANDLE _clientMapHandle = NULL;
#endif

void sentinelClientInitialize(const char *mapHostName) {
#if __OS_WIN
	_clientMapHandle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mapHostName);
	if (!_clientMapHandle) {
		fprintf(stderr, "sentinel: (%lu) could not connect to the Sentinel host %s. Please ensure the host application is running.\n", (unsigned long)GetLastError(), mapHostName); exit(1);
	}
	_clientMap = MapViewOfFile(_clientMapHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(sentinelMap));
	if (!_clientMap) {
		fprintf(stderr, "sentinel: (%lu) could not map view of %s.\n", (unsigned long)GetLastError(), mapHostName);
		CloseHandle(_clientMapHandle); _clientMapHandle = NULL; exit(1);
	}
#elif __OS_UNIX
	int fd = shm_open(mapHostName, O_RDWR, 0);
	if (fd < 0) {
		fprintf(stderr, "sentinel: could not connect to the Sentinel host %s (%s). Please ensure the host application is running.\n", mapHostName, strerror(errno)); exit(1);
	}
	_clientMap = mmap(nullptr, sizeof(sentinelMap), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (_clientMap == MAP_FAILED) {
		_clientMap = nullptr;
		fprintf(stderr, "sentinel: could not map the Sentinel host %s: %s\n", mapHostName, strerror(errno)); exit(1);
	}
#endif
	_sentinelClientMap = (sentinelMap *)_clientMap;
	_sentinelClientMapOffset = _sentinelClientMap->offset - (intptr_t)_sentinelClientMap; // server address minus ours
}

void sentinelClientShutdown() {
#if __OS_WIN
	if (_clientMap) { UnmapViewOfFile(_clientMap); _clientMap = nullptr; }
	if (_clientMapHandle) { CloseHandle(_clientMapHandle); _clientMapHandle = NULL; }
#elif __OS_UNIX
	if (_clientMap) { munmap(_clientMap, sizeof(sentinelMap)); _clientMap = nullptr; }
#endif
	_sentinelClientMap = nullptr;
	_sentinelClientMapOffset = 0;
}

static __forceinline__ int getprocessid_() { host_getprocessid msg; return msg.rc; }

static char *sentinelClientRedirPipelineArgs[] = { (char *)"^0" };
void sentinelClientRedir(pipelineRedir *redir) {
#if __OS_WIN
	HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, getprocessid_());
	pipelineCreate(1, sentinelClientRedirPipelineArgs, nullptr, &redir[1].input, &redir[1].output, &redir[1].error, process, redir);
#elif __OS_UNIX
	(void)redir; // output redirection through the server process is not implemented on POSIX yet
#endif
}

#endif
