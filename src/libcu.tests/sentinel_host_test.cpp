// Loopback test for the Sentinel host bus. Needs no GPU: the server is started with the device
// bus disabled, and this process opens the shared map as a client the way a d* tool in another
// process would. Exercises the slot state machine, inline embedded values (ptrsIn), values too
// large for a slot (the chunked transfer), replies, fire-and-forget messages and several
// concurrent senders.
#define HAS_DEVICESENTINEL 0
#define HAS_HOSTSENTINEL 1
#include <sentinel.h>
#include <sentinel-client.cpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

enum {
	TEST_ADD = 500,
	TEST_STRLEN,
	TEST_COUNT,
};

struct test_add {
	sentinelMessage base;
	int a, b;
	test_add(int a, int b) : base(TEST_ADD, SENTINELFLOW_WAIT), a(a), b(b) { sentinelClientSend(&base, sizeof(test_add)); }
	int rc;
};

struct test_strlen {
	sentinelMessage base;
	char *str;
	test_strlen(const char *str) : base(TEST_STRLEN, SENTINELFLOW_WAIT, SENTINEL_CHUNK), str((char *)str) { sentinelClientSend(&base, sizeof(test_strlen), ptrsIn); }
	int rc;
	sentinelInPtr ptrsIn[2] = {
		{ &str, -1 },
		{ nullptr }
	};
};

struct test_count {
	sentinelMessage base;
	int delta;
	test_count(int delta) : base(TEST_COUNT, SENTINELFLOW_NONE), delta(delta) { sentinelClientSend(&base, sizeof(test_count)); }
};

static std::atomic<int> _counted{ 0 };

static bool testExecutor(void *tag, sentinelMessage *data, int length, char *(**hostPrepare)(void*, char*, char*, intptr_t)) {
	switch (data->op) {
	case TEST_ADD: { test_add *msg = (test_add *)data; msg->rc = msg->a + msg->b; return true; }
	case TEST_STRLEN: { test_strlen *msg = (test_strlen *)data; msg->rc = (int)strlen(msg->str); return true; }
	case TEST_COUNT: { test_count *msg = (test_count *)data; _counted += msg->delta; return true; }
	}
	return false;
}
static sentinelExecutor _testExecutor = { nullptr, "test", testExecutor, nullptr };

static int _failures = 0;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); _failures++; } } while (0)

int main() {
	// a name of our own so a running libcu server on this machine is left alone
	const char *name = SENTINEL_NAME "-test";
	sentinelServerInitialize(nullptr, (char *)name, true, false);
	sentinelRegisterExecutor(&_testExecutor, true, false);
	CHECK(sentinelFindExecutor("test", false) == &_testExecutor);
	CHECK(sentinelFindExecutor("base", false) != nullptr);
	CHECK(sentinelFindExecutor("test", true) == nullptr);
	sentinelClientInitialize((char *)name);

	// plain request and reply
	{ test_add m(2, 3); CHECK(m.rc == 5); }
	{ test_add m(-7, 7); CHECK(m.rc == 0); }

	// embedded strings: one that fits the slot, one that needs the chunked transfer
	{ test_strlen m("hello"); CHECK(m.rc == 5); }
	{ std::string big(1000, 'x'); test_strlen m(big.c_str()); CHECK(m.rc == 1000); }
	{ std::string huge(3 * SENTINEL_MSGSIZE + 17, 'y'); test_strlen m(huge.c_str()); CHECK(m.rc == 3 * SENTINEL_MSGSIZE + 17); }
	{ test_strlen m(""); CHECK(m.rc == 0); }

	// more messages than slots, in order
	for (int i = 0; i < 64; i++) { test_add m(i, 1); CHECK(m.rc == i + 1); }

	// fire-and-forget messages are all delivered eventually
	for (int i = 0; i < 100; i++) test_count m(1);
	for (int spins = 0; _counted.load() < 100 && spins < 100000; spins++) std::this_thread::sleep_for(std::chrono::microseconds(100));
	CHECK(_counted.load() == 100);

	// concurrent senders contend for the bus without losing or mixing replies
	{
		const int senders = 8, perSender = 200;
		std::atomic<int> bad{ 0 };
		std::vector<std::thread> threads;
		for (int t = 0; t < senders; t++)
			threads.emplace_back([t, &bad]() {
				for (int i = 0; i < perSender; i++) {
					test_add m(t * 100000, i);
					if (m.rc != t * 100000 + i) bad++;
					if ((i & 7) == 0) { char buf[64]; snprintf(buf, sizeof(buf), "%d:%d", t, i); test_strlen s(buf); if (s.rc != (int)strlen(buf)) bad++; }
				}
			});
		for (auto &th : threads) th.join();
		CHECK(bad.load() == 0);
	}

	sentinelClientShutdown();
	sentinelServerShutdown();
	if (_failures) { fprintf(stderr, "sentinel_host_test: %d failure(s)\n", _failures); return 1; }
	printf("sentinel_host_test: ok\n");
	return 0;
}
