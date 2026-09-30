#include <crtdefscu.h>
#include <stringcu.h>
#include <sentinel.h>
#include <ext/mutex.h>

__BEGIN_DECLS;

#if HAS_DEVICESENTINEL

static __device__ void executeTrans(char id, sentinelCommand *cmd, int size, sentinelInPtr *listIn, sentinelOutPtr *listOut, intptr_t offset, char *&trans);

static __device__ char *preparePtrs(sentinelInPtr *ptrsIn, sentinelOutPtr *ptrsOut, sentinelCommand *cmd, char *data, char *dataEnd, intptr_t offset, sentinelOutPtr *&listOut_, char *&trans) {
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

static __device__ bool postfixPtrs(sentinelOutPtr *ptrsOut, sentinelCommand *cmd, intptr_t offset, sentinelOutPtr *listOut, char *&trans) {
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

__device__ unsigned int _sentinelMapId;
__constant__ sentinelMap *_sentinelDeviceMap[SENTINEL_DEVICEMAPS];
__device__ void sentinelDeviceSend(sentinelMessage *msg, int msgLength, sentinelInPtr *ptrsIn, sentinelOutPtr *ptrsOut) {
	sentinelMap *map = _sentinelDeviceMap[atomicAdd(&_sentinelMapId, 1) % SENTINEL_DEVICEMAPS];
	if (!map)
		panic("sentinel: device map not defined. did you start sentinel?");
	if (msgLength <= 0 || ROUND8_(msgLength) > SENTINEL_MSGSIZE)
		panic("sentinel: msg too long (%d)", msgLength);

	// ATTACH: take a ticket and own its slot once the previous occupant has released it
	unsigned int id = mutexAdd(&map->setId, 1);
	sentinelCommand *cmd = &map->cmds[id % SENTINEL_MSGCOUNT];
	if (cmd->magic != SENTINEL_MAGIC)
		panic("sentinel: bad magic");
	atomicAdd(&cmd->locks, 1);
	int *control = &cmd->control; intptr_t offset = map->offset; char *trans = nullptr;
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

	// FLOW-WAIT: the host answers with HOSTRDY; postfix reads results out of the slot before
	// it is released, then the slot goes back to NORMAL (or on to the transfer-out states)
	if (msg->flow & SENTINELFLOW_WAIT) {
		mutexSpinLock(nullptr, control, SENTINELCONTROL_HOSTRDY, SENTINELCONTROL_DEVICEWAIT);
		cmd->length = msgLength; memcpy(msg, cmd->data, msgLength);
		if ((ptrsOut && !postfixPtrs(ptrsOut, cmd, offset, listOut, trans)) ||
			(msg->postfix && !msg->postfix(msg, offset)))
			panic("sentinel: postfix error (op %d)", msg->op);
		mutexSet(control, !listOut ? SENTINELCONTROL_NORMAL : SENTINELCONTROL_DEVICERDY);
	}
	atomicSub(&cmd->locks, 1);
}

static __device__ void executeTrans(char id, sentinelCommand *cmd, int size, sentinelInPtr *listIn, sentinelOutPtr *listOut, intptr_t offset, char *&trans) {
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

#endif

__END_DECLS;
