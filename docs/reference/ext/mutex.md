## #include <ext/mutex.h>

Header-only spin locks over a word shared between a GPU and the host, or between two host processes. Built on `cuda::atomic_ref<int, cuda::thread_scope_system>` from libcu++, so the same functions compile for device code, for libcu's host side and for host programs that include the Sentinel client without linking libcu.

## Host and Device Side
Prototype | Description | Tags
--- | --- | :---:
```int mutexGet(int *mutex);``` | Atomic read with acquire ordering.
```void mutexSet(int *mutex, int val = 0);``` | Atomic write with release ordering.
```unsigned int mutexAdd(unsigned int *counter, unsigned int val = 1);``` | Atomic post-increment; returns the previous value (tickets).
```void mutexSpinLock(void **cancelToken, int *mutex, int cmp = 0, int val = 1, char pred = 0, int predVal = 0, bool (*func)(void **) = nullptr, void **funcTag = nullptr, const mutexSleep_t *sleep = nullptr);``` | Spins until `*mutex == cmp` and sets it to `val`. While waiting, if `pred` holds for the value seen, `func` is called with `funcTag`: the wait continues when it returns true and returns without acquiring when there is no `func` or it returns false. `cancelToken`, when given, ends the wait once the pointer it addresses becomes null.
```void mutexPause(unsigned int us);``` | Backs off for about `us` microseconds: yields on the host when zero, `__nanosleep` on the device.
```#define mutexHeld(mutex)``` | True when the word holds 1.

Predicates for `pred`: `MUTEXPRED_EQ`, `MUTEXPRED_NE`, `MUTEXPRED_LT`, `MUTEXPRED_GT`, `MUTEXPRED_LTE`, `MUTEXPRED_GTE`, `MUTEXPRED_AND` (any bit), `MUTEXPRED_ANE` (all bits).

`mutexSleep_t` sets the back-off in microseconds: start at `usmin`, multiply by `factor`, cap at `usmax`. The default is 0, 2, 256.
