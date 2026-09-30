# sentinel

describe sentinel


## Interface

These are the methods to access Sentinel's functionality:
* `sentinelDefaultHostExecutor` - the built-in default host executor auto-registered as base on `sentinelServerInitialize`
* `sentinelDefaultDeviceExecutor` - the built-in default device executor auto-registered as base on `sentinelServerInitialize`
* `sentinelServerInitialize` - initializes the server side Sentinel creating its assets and registering the `sentinelDefaultExecutor` and the `executor` if provided.
* `sentinelServerShutdown` - shutsdown the server side Sentinel and its assets
* `sentinelDeviceSend` - used in the message constructor to send message(s) on the device bus
* `sentinelClientInitialize` - initializes the client side Sentinel establishing a connection to the server
* `sentinelClientShutdown` - shutsdown the client side Sentinel
* `sentinelClientRedir` - used in the client side to create a redir pipeline
* `sentinelClientSend` - used in the message constructor to send message(s) on the host bus
* `sentinelFindExecutor` - finds the `sentinelExecutor` with the given `name` on the host or device
* `sentinelRegisterExecutor` - registers the `sentinelExecutor` on the host or device
* `sentinelUnregisterExecutor` - un-registers the `sentinelExecutor` on the host or device
* `sentinelRegisterFileUtils` - registers the `sentinelRegisterFileUtils` for use with file-system utilities
```
extern bool sentinelDefaultHostExecutor(void *tag, sentinelMessage *data, int length, char *(**hostPrepare)(void*,char*,char*,intptr_t));
extern bool sentinelDefaultDeviceExecutor(void *tag, sentinelMessage *data, int length, char *(**hostPrepare)(void*,char*,char*,intptr_t));
extern void sentinelServerInitialize(sentinelExecutor *deviceExecutor = nullptr, const char *mapHostName = SENTINEL_NAME, bool hostSentinel = true, bool deviceSentinel = true);
extern void sentinelServerShutdown();
#if HAS_DEVICESENTINEL
	extern __device__ void sentinelDeviceSend(sentinelMessage *msg, int msgLength, sentinelInPtr *ptrsIn = nullptr, sentinelOutPtr *ptrsOut = nullptr);
#endif
#if HAS_HOSTSENTINEL
	extern void sentinelClientInitialize(const char *mapHostName = SENTINEL_NAME);
	extern void sentinelClientShutdown();
	extern void sentinelClientRedir(pipelineRedir *redir);
	extern void sentinelClientSend(sentinelMessage *msg, int msgLength, sentinelInPtr *ptrsIn = nullptr, sentinelOutPtr *ptrsOut = nullptr);
#endif
extern sentinelExecutor *sentinelFindExecutor(const char *name, bool forDevice = true);
extern void sentinelRegisterExecutor(sentinelExecutor *exec, bool makeDefault = false, bool forDevice = true);
extern void sentinelUnregisterExecutor(sentinelExecutor *exec, bool forDevice = true);

// file-utils
extern void sentinelRegisterFileUtils();
```

## Structure
These defines are used for Sentinel:
* `SENTINEL_NAME` - default name of the host bus: a file mapping on Windows, a POSIX shared-memory segment (`shm_open`) elsewhere
* `SENTINEL_MAGIC` - a magic value written into every slot when a map is created, checked on every access
* `SENTINEL_DEVICEMAPS` - the number of device to host maps, and threads to create
* `SENTINEL_MSGSIZE` - the bytes of payload in a slot: the `sentinelMessage` plus the values it embeds
* `SENTINEL_MSGCOUNT` - the number of slots in a map
* `SENTINEL_CHUNK` - the default `size` budget for embedded values; larger values go through the chunked transfer
```
#if __OS_WIN
#define SENTINEL_NAME "Sentinel"
#else
#define SENTINEL_NAME "/sentinel"
#endif
#define SENTINEL_MAGIC 0xC811
#define SENTINEL_DEVICEMAPS 1
#define SENTINEL_MSGSIZE 5120
#define SENTINEL_MSGCOUNT 1
#define SENTINEL_CHUNK 4096
```

### Transport
Slot state, tickets and lock counts are shared between a GPU and host threads, or between two host processes, so they are only ever touched through the system-scope atomics in `ext/mutex.h` (`cuda::atomic_ref` with `cuda::thread_scope_system`): `mutexGet`, `mutexSet`, `mutexAdd` and `mutexSpinLock`. Waiting backs off exponentially in microseconds, yielding first on the host and using `__nanosleep` on the device; nothing in the bus is a block-wide barrier, so any thread of any warp may send. The host threads are `std::thread`s with a cancel token and are joined by `sentinelServerShutdown`, which is also registered with `atexit`.

### SentinelContext
SentinelContext is a singleton which represents the state of Sentinel. Sentinel provides two distinct message buses for device to host, and host to host communication respectivly. The later is used for IPC using named pipes, named `SENTINEL_NAME`, and is extensivly used by the file-system utilities.

`SentinelServerInitialize` execution:
* Creates `SENTINEL_DEVICEMAPS` instances of `sentinelMap`, each with it's own processing thread, and stores them in DeviceMap.
* Sets a single linked list of `sentinelExecutor(s)` for the processesing of all device to host messages in `deviceList`.
* Creates a single instance of `sentinelMap`, with it's own processing thread, and stores it in HostMap.
* Sets a single linked list of `sentinelExecutor(s)` for the processesing of all host to host messages in `hostList`.
```
sentinelContext
- deviceMap[SENTINEL_DEVICEMAPS] - sentinelMap(s) used for device
- hostMap - sentinelMap used for host IPC
- hostList - linked list of sentinelExecutor(s) for host processing
- deviceList - linked list of sentinelExecutor(s) for device processing
```

### SentinelMap
Each `sentinelMap` has a dedicated processing thread and can hold `SENTINEL_MSGCOUNT` messages of size `SENTINEL_MSGSIZE`, this size must include the `sentinelCommand` size.
* `getId` is the next ticket the map's thread consumes
* `setId` is the next ticket handed out; every sender takes one with `mutexAdd` and owns slot `ticket % SENTINEL_MSGCOUNT` once it is free
* `offset` is the host address of the map minus the sender's address of it: zero for the device under unified addressing, and the difference between the two processes' mappings on the host bus. Pointers embedded in a message have it added so the host can follow them
* `cmds` holds the slots
```
sentinelMap
- getId - next ticket to consume
- setId - next ticket to hand out, atomically incremented
- offset - host address of the map minus the sender's address of it
- cmds[SENTINEL_MSGCOUNT] - the slots
```

### SentinelCommand
Each `sentinelCommand` represents a command being passed across the bus, and has an embeded `sentinelMessage` in it's `Data` property
* `magic` is written when the map is created and checked on every access
* `control` is the slot state, accessed only through `ext/mutex.h`
	* `SENTINELCONTROL_NORMAL` (0) - free
	* `SENTINELCONTROL_DEVICE` (1) - a sender owns the slot and is filling it
	* `SENTINELCONTROL_DEVICERDY` (2) - the message is complete and waits for the host
	* `SENTINELCONTROL_DEVICEWAIT` (3) - the sender is reading the reply
	* `SENTINELCONTROL_HOST` (5) - the host thread is executing the message
	* `SENTINELCONTROL_HOSTRDY` (6) - the reply is in the slot
	* `SENTINELCONTROL_HOSTWAIT` (7) - the host waits for the sender to finish a transfer out
	* `SENTINELCONTROL_TRAN*` (0x10 and up) - the chunked transfer of a value larger than the slot: `TRANSIZE` asks the host for a buffer of `length` bytes, `TRANIN` and `TRANOUT` move one chunk of `length` bytes, `TRANRDY` is the host's acknowledgement and `TRAN` the sender's
* `locks` counts the senders currently attached to the slot, for diagnostics
* `length` and `data` hold the embedded `sentinelMessage`, followed at `ROUND8_(length)` by the values it embeds
```
sentinelCommand
- magic - SENTINEL_MAGIC
- control - slot state
- locks - attached senders
- length - length of the message
- data[SENTINEL_MSGSIZE] - the message and its embedded values
```

### SentinelMessage
Each `sentinelMessage` is a custom message being passed across the bus
```
sentinelMessage
- op - operation
- flow - flow control to asyc or wait
- unknown - internal field
- size - size of message
- prepare() - method to prepare message for transport
- postfix() - method to postfix message after transport
```

### pipelineRedir
The `pipelineRedir` holds stdin, stdout, stderr for redirection
```
pipelineRedir
- ... - defined else where
```

### SentinelClientMessage
The `sentinelClientMessage` holds `sentinelMessage` and `pipelineRedir`
```
sentinelClientMessage
- base - sentinelMessage
- redir - pipelineRedir
```

### SentinelExecutor
The `sentinelExecutor` is responsible for executing message on host.
```
sentinelExecutor
- next - linked list pointer
- name - name of executor
- executor() - attempts to process messages
- tag - optional data for executor
```


## Example

The following is an example of creating a custom message, and using it.

### Enum
* Use an `enum` to auto-number the operations in a module
* The developer is responsible for name collisions
* Device and Host messages have seperate namespaces
* Numbers below `500` are reserved for system use
```
enum {
	MODULE_SIMPLE = 500,
	MODULE_STRING,
	MODULE_RETURN,
	MODULE_CUSTOM,
};
```

### Message - Simple
A simple message with a integer value named `value`, and a integer return code named `rc`.
* `base` must be first
* `base` constructor parameters of `flow`, `size`, `prepare` and `postfix` can be ignored
```
struct module_simple {
	sentinelMessage base;
	int value;
	__device__ module_simple(int value) : base(MODULE_SIMPLE), value(value) { sentinelDeviceSend(&base, sizeof(module_simple)); }
	int rc;
};
```

### Message - String
Message asset(s) referenced outside of the message payload, like string values, must be coalesced into the message payload. refered values offset(s) must be adjusted to align memory maps.
* `base` must be first
* `base` constructor parameters of `flow` and `size` are required
* `size` should contain enough space to hold the message with it's embeded values, and must be remain under the `SENTINEL_MSGSIZE` plus the `sentinelCommand` overhead size, or sentinel paging will occur.
* `ptrsIn` defines in-references, replacing the original pointers with the emeded one and apply the offset to align memory maps.
```
struct module_string {
	sentinelMessage base;
	const char *str;
	__device__ module_string(const char *str) : base(MODULE_STRING, FLOW_WAIT, SENTINEL_CHUNK), str(str) { sentinelDeviceSend(&base, sizeof(module_string), ptrsIn); }
	int rc;
	sentinelInPtr ptrsIn[2] = {
		{ &str, -1 },
		{ nullptr }
	};
};
```

### Message - Return
Message asset(s) referenced outside of the message payload, like string values, must be coalesced into the message payload. refered values offset(s) must be adjusted to align memory maps.
* `base` must be first
* `base` constructor parameters of `flow` and `size` are required
* `size` should contain enough space to hold the message with it's embeded values, and must be remain under the `SENTINEL_MSGSIZE` plus the `sentinelCommand` overhead size, or sentinel paging will occur.
* `ptrsOut` defines out-references, replacing the original pointers with the emeded one and apply the offset to align memory maps.
```
struct module_return {
	sentinelMessage base;
	const char *buf; size_t size;
	__device__ module_return(const char *buf, size_t size) : base(MODULE_RETURN, SENTINELFLOW_WAIT, SENTINEL_CHUNK), buf(buf), size(size) { ptrsOut[0].size = size; sentinelDeviceSend(&base, sizeof(module_return), nullptr, ptrsOut); }
	size_t rc;
	void *ptr;
	sentinelOutPtr ptrsOut[2] = {
		{ &ptr, &buf, 0 },
		{ nullptr }
	};
};
```

### Message - Custom
Message asset(s) referenced outside of the message payload, like string values, must be coalesced into the message payload. refered values offset(s) must be adjusted to align memory maps.
* `base` must be first
* `base` constructor parameters of `size` and `prepare` are required
* `size` should contain enough space to hold the message with it's embeded values, and must be remain under the `SENTINEL_MSGSIZE` plus the `sentinelCommand` overhead size.
* `prepare` must embed referenced values, replacing the original pointers with the emeded one and apply the offset to align memory maps.
```
struct module_custom {
	static __forceinline __device__ char *prepare(module_custom *t, char *data, char *dataEnd, intptr_t offset) {
		int strLength = t->str ? (int)strlen(t->str) + 1 : 0;
		char *str = data;
		char *end = data += strLength;
		if (end > dataEnd) return nullptr;
		memcpy(str, t->str, strLength);
		t->str = str + offset;
		t->ptr = str + offset;
		return end;
	}
	static __forceinline__ __device__ bool postfix(module_custom *t, intptr_t offset) {
		char *ptr = (char *)t->ptr - offset;
		//if (t->rc > 0) memcpy(t->buf, ptr, t->rc);
		return true;
	}
	sentinelMessage base;
	const char *str; char *buf;
	__device__ module_custom(const char *str, char *buf) : base(MODULE_CUSTOM, FLOW_WAIT, SENTINEL_CHUNK, SENTINELPREPARE(prepare), SENTINELPOSTFIX(postfix)), str(str), buf(buf) { sentinelDeviceSend(&base, sizeof(module_custom)); }
	int rc;
	void *ptr;
};
```

### Executor
```
bool sentinelExecutor(void *tag, sentinelMessage *data, int length, char *(**hostPrepare)(void*,char*,char*,intptr_t)) {
	switch (data->op) {
	case MODULE_SIMPLE: { module_simple *msg = (module_simple *)data; msg->rc = msg->value; return true; }
	case MODULE_STRING: { module_string *msg = (module_string *)data; msg->rc = strlen(msg->str); return true; }
	case MODULE_RETURN: { module_return *msg = (module_return *)data; msg->rc = 5; strcpy((char *)msg->ptr, "test"); return true; }
	case MODULE_CUSTOM: { module_custom *msg = (module_custom *)data; msg->rc = strlen(msg->str); return true; }
	}
	return false;
}
```

### Calling
to call:
```
module_simple msg(123);
int rc = msg.rc;
```

to call:
```
module_string msg("123");
int rc = msg.rc;
```

to call:
```
char buf[100];
module_return msg(buf, sizeof(buf));
int rc = msg.rc;
```

to call:
```
char buf[100];
module_custom msg("123", buf);
int rc = msg.rc;
```