/*
mutex.h - spin locks over memory shared between a GPU and the host, or two host processes
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

#ifndef _EXT_MUTEX_H
#define _EXT_MUTEX_H
#include <crtdefscu.h>
// libcu++: the same atomics compile for the device (nvcc) and for plain host compilers, and
// thread_scope_system is what a word shared with a GPU or another process needs.
#include <cuda/atomic>
#ifndef __CUDA_ARCH__
#include <chrono>
#include <thread>
#endif

#define MUTEXPRED_EQ 1
#define MUTEXPRED_NE 2
#define MUTEXPRED_LT 3
#define MUTEXPRED_GT 4
#define MUTEXPRED_LTE 5
#define MUTEXPRED_GTE 6
#define MUTEXPRED_AND 7
#define MUTEXPRED_ANE 8

/* Back-off between polls, in microseconds: start at usmin, multiply by factor, cap at usmax. */
typedef struct mutexSleep_t {
	unsigned int usmin;
	unsigned int usmax;
	unsigned int factor;
} mutexSleep_t;

typedef cuda::atomic_ref<int, cuda::thread_scope_system> mutexRef_t;

/* Waits a little. Every thread of a warp that reaches a bus waits on its own word, so this
** must never be a block-wide barrier. Zero yields on the host. */
static __host_device__ __forceinline__ void mutexPause(unsigned int us) {
#if defined(__CUDA_ARCH__)
#if __CUDA_ARCH__ >= 700
	__nanosleep(us ? us * 1000 : 100);
#endif
#else
	if (!us) std::this_thread::yield();
	else std::this_thread::sleep_for(std::chrono::microseconds(us));
#endif
}

/* Atomic read and write of a shared word. */
static __host_device__ __forceinline__ int mutexGet(int *mutex) { return mutexRef_t(*mutex).load(cuda::std::memory_order_acquire); }
static __host_device__ __forceinline__ void mutexSet(int *mutex, int val = 0) { mutexRef_t(*mutex).store(val, cuda::std::memory_order_release); }

/* Atomic post-increment of a shared counter (tickets). */
static __host_device__ __forceinline__ unsigned int mutexAdd(unsigned int *counter, unsigned int val = 1) {
	return cuda::atomic_ref<unsigned int, cuda::thread_scope_system>(*counter).fetch_add(val, cuda::std::memory_order_acq_rel);
}

/* Spins until *mutex equals cmp and then sets it to val, with exponential back-off.
**
** While waiting, the value seen is tested against pred/predVal; if the predicate holds, func is
** called with funcTag and the wait continues when it returns true, or the wait returns without
** acquiring when there is no func or it returns false. cancelToken, when given, ends the wait
** as soon as the pointer it addresses becomes null. */
static __host_device__ inline void mutexSpinLock(void **cancelToken, int *mutex, int cmp = 0, int val = 1, char pred = 0, int predVal = 0, bool(*func)(void **) = nullptr, void **funcTag = nullptr, const mutexSleep_t *sleep = nullptr) {
	unsigned int us = sleep ? sleep->usmin : 0, usmax = sleep ? sleep->usmax : 256, factor = sleep ? sleep->factor : 2;
	mutexRef_t m(*mutex);
	int v = cmp;
	while ((!cancelToken || *(void *volatile *)cancelToken) && !m.compare_exchange_strong(v, val, cuda::std::memory_order_acq_rel, cuda::std::memory_order_acquire)) {
		bool condition = false;
		switch (pred) {
		case MUTEXPRED_EQ: condition = v == predVal; break;
		case MUTEXPRED_NE: condition = v != predVal; break;
		case MUTEXPRED_LT: condition = v < predVal; break;
		case MUTEXPRED_GT: condition = v > predVal; break;
		case MUTEXPRED_LTE: condition = v <= predVal; break;
		case MUTEXPRED_GTE: condition = v >= predVal; break;
		case MUTEXPRED_AND: condition = (v & predVal) != 0; break;
		case MUTEXPRED_ANE: condition = (v & predVal) == predVal; break;
		}
		if (condition && (!func || !func(funcTag))) return;
		mutexPause(us);
		us = us >= usmax ? usmax : us ? us * factor : 1;
		v = cmp;
	}
}

/* Mutex held. */
#define mutexHeld(mutex) (mutexGet(mutex) == 1)

#endif  /* _EXT_MUTEX_H */
