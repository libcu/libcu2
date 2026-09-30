/*
sentinel.h - lite message bus framework for device to host functions
The MIT License

Copyright (c) 2016 Sky Morey

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef _SENTINEL_H
#define _SENTINEL_H
#include <crtdefscu.h>
#include <driver_types.h>
#include <stdio.h>
#include <ext/pipeline.h>
#if __OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAS_DEVICESENTINEL
#define HAS_DEVICESENTINEL 1
#endif
#ifndef HAS_HOSTSENTINEL
#define HAS_HOSTSENTINEL 1
#endif

/*
** Sentinel is two message buses built on the same mailbox.
**
** Device bus: sentinelServerInitialize allocates SENTINEL_DEVICEMAPS pinned, device-mapped
** host buffers. Device code sends a message with sentinelDeviceSend; a host thread per map
** runs it through the registered device executors and, if the message waits, writes the
** result back. Values a message points at travel inline in the slot when they fit
** (sentinelInPtr / sentinelOutPtr) and through the chunked transfer states otherwise.
**
** Host bus: the same mailbox in a named shared-memory segment (SENTINEL_NAME), so another
** process on the host can send messages with sentinelClientSend. The file utilities use this
** to reach the process that owns the CUDA context.
**
** Slot state lives in sentinelCommand::control and is only touched through the system-scope
** atomics in ext/mutex.h; it is shared between a GPU and host threads, or two host processes.
*/

#if __OS_WIN
#define SENTINEL_NAME "Sentinel" //"Global\\Sentinel"
#else
#define SENTINEL_NAME "/sentinel"
#endif
#define SENTINEL_MAGIC 0xC811
#define SENTINEL_DEVICEMAPS 1
#define SENTINEL_MSGSIZE 5120
#define SENTINEL_MSGCOUNT 1
#define SENTINEL_CHUNK 4096

	typedef struct sentinelInPtr {
		void *field;
		int size;
		void *unknown;
	} sentinelInPtr;

	typedef struct sentinelOutPtr {
		void *field;
		void *buf;
		int size;
		void *sizeField;
		void *unknown;
	} sentinelOutPtr;

#define SENTINELFLOW_NONE 0
#define SENTINELFLOW_WAIT 1
#define SENTINELFLOW_TRAN 2

	typedef struct sentinelMessage {
		unsigned short op;
		unsigned char flow;
		int size;
		char *(*prepare)(void*, char*, char*, intptr_t);
		bool(*postfix)(void*, intptr_t);
		__host__ __device__ sentinelMessage(unsigned short op, unsigned char flow = SENTINELFLOW_WAIT, int size = 0, char *(*prepare)(void*, char*, char*, intptr_t) = nullptr, bool(*postfix)(void*, intptr_t) = nullptr)
			: op(op), flow(flow), size(size), prepare(prepare), postfix(postfix) { }
	} sentinelMessage;
#define SENTINELPREPARE(P) ((char *(*)(void*,char*,char*,intptr_t))&P)
#define SENTINELPOSTFIX(P) ((bool (*)(void*,intptr_t))&P)

	typedef struct sentinelClientMessage {
		sentinelMessage base;
		pipelineRedir redir;
		sentinelClientMessage(pipelineRedir redir, unsigned short op, unsigned char flow = SENTINELFLOW_WAIT, int size = 0, char *(*prepare)(void*, char*, char*, intptr_t) = nullptr, bool(*postfix)(void*, intptr_t) = nullptr)
			: base(op, flow, size, prepare, postfix), redir(redir) { }
	} sentinelClientMessage;

	/* One slot: a fixed header followed by the sentinelMessage and the values it embeds. */
	typedef struct __align__(8) sentinelCommand {
		int magic;      /* SENTINEL_MAGIC, set when the map is created */
		int control;    /* SENTINELCONTROL_*, accessed only through ext/mutex.h */
		int locks;      /* senders attached to the slot, for diagnostics */
		int length;     /* bytes of the sentinelMessage at data */
		char data[SENTINEL_MSGSIZE];
	} sentinelCommand;

	typedef struct __align__(8) sentinelMap {
		unsigned int getId;     /* next ticket the host thread consumes */
		unsigned int setId;     /* next ticket to hand out; senders fetch_add it */
		intptr_t offset;        /* host address of this map minus the sender's address of it */
		sentinelCommand cmds[SENTINEL_MSGCOUNT];
	} sentinelMap;

	typedef struct sentinelExecutor {
		struct sentinelExecutor *next;
		const char *name;
		bool(*executor)(void*, sentinelMessage*, int, char*(**)(void*, char*, char*, intptr_t));
		void *tag;
	} sentinelExecutor;

	typedef struct sentinelContext {
		sentinelMap *deviceMap[SENTINEL_DEVICEMAPS];
		sentinelMap *hostMap;
		sentinelExecutor *hostList;
		sentinelExecutor *deviceList;
	} sentinelContext;

#if HAS_DEVICESENTINEL
	extern __constant__ sentinelMap *_sentinelDeviceMap[SENTINEL_DEVICEMAPS];
#endif

	extern bool sentinelDefaultHostExecutor(void *tag, sentinelMessage *data, int length, char *(**hostPrepare)(void*, char*, char*, intptr_t));
	extern bool sentinelDefaultDeviceExecutor(void *tag, sentinelMessage *data, int length, char *(**hostPrepare)(void*, char*, char*, intptr_t));
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

	/* Slot states. A sender takes the slot at NORMAL, fills it and marks it DEVICERDY; the host
	** takes it to HOST, executes, and marks it HOSTRDY when the sender waits for a reply. The
	** TRAN states move values larger than a slot through it one chunk at a time. */
#define SENTINELCONTROL_NORMAL 0x0
#define SENTINELCONTROL_DEVICE 0x1
#define SENTINELCONTROL_DEVICERDY 0x2
#define SENTINELCONTROL_DEVICEWAIT 0x3
#define SENTINELCONTROL_HOST 0x5
#define SENTINELCONTROL_HOSTRDY 0x6
#define SENTINELCONTROL_HOSTWAIT 0x7
	// transfer
#define SENTINELCONTROL_TRAN 0x10
#define SENTINELCONTROL_TRANRDY 0x11
#define SENTINELCONTROL_TRANDONE 0x12
	// transfer-method
#define SENTINELCONTROL_TRANSIZE 0x13
#define SENTINELCONTROL_TRANIN 0x14
#define SENTINELCONTROL_TRANOUT 0x15

#ifdef __cplusplus
}
#endif

#endif  /* _SENTINEL_H */
