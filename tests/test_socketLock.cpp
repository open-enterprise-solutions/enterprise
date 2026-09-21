// =============================================================================
// ibSocketLock — two threads closing one wxSocketBase (#155).
//
// wxSocketImpl::Close() is a check of the descriptor followed by the work, and nothing between them,
// so two threads can both close. The debugger's client connection closes its socket from two threads
// whenever a session is ended from the designer while the connection's own thread notices the far end
// going away. What these tests establish is that the lock serialises those closes and that Destroy
// frees once; they do NOT establish that an unguarded double close crashes — see the control at the
// bottom, which did not.
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include <wx/init.h>
#include <wx/socket.h>

#include "backend/debugger/debugClient.h"   // ibSocketLock lives beside the one class that holds such a socket

namespace {

// A connected pair on the loopback interface, both ends made on THIS thread — wx initialises its
// socket layer from the first socket created on the main thread, which is where the test runs.
struct LoopbackPair {
	wxSocketServer* server = nullptr;
	wxSocketClient* client = nullptr;
	wxSocketBase*   accepted = nullptr;

	bool Open() {
		wxIPV4address addr;
		addr.LocalHost();
		addr.Service(0);   // the system picks a free port

		server = new wxSocketServer(addr, wxSOCKET_BLOCK | wxSOCKET_WAITALL);
		if (!server->IsOk()) return false;

		wxIPV4address bound;
		if (!server->GetLocal(bound)) return false;

		client = new wxSocketClient(wxSOCKET_BLOCK | wxSOCKET_WAITALL);
		client->Connect(bound, false);
		if (!client->WaitOnConnect(5) || !client->IsConnected()) return false;

		if (!server->WaitForAccept(5)) return false;
		accepted = server->Accept(false);
		return accepted != nullptr;
	}

	// Whatever is still held, let go of — in the order a normal teardown would.
	void Release() {
		if (accepted != nullptr) { accepted->Destroy(); accepted = nullptr; }
		if (client   != nullptr) { client->Destroy();   client = nullptr; }
		if (server   != nullptr) { server->Destroy();   server = nullptr; }
	}
};

// Runs `work` on two threads that are released at the same moment.
template <class Work>
void RunTogether(Work work) {
	std::atomic<bool> go{ false };
	std::atomic<int> ready{ 0 };

	auto body = [&]() {
		++ready;
		while (!go.load(std::memory_order_acquire)) { /* spin until both are here */ }
		work();
	};

	std::thread a(body), b(body);
	while (ready.load() < 2) { std::this_thread::yield(); }
	go.store(true, std::memory_order_release);
	a.join();
	b.join();
}

struct SocketLockFix : ::testing::Test {
	wxInitializer m_wx;
	void SetUp() override {
		if (!m_wx.IsOk()) GTEST_SKIP() << "wx did not initialise";
	}
};

} // namespace

// The case the designer hits: the same socket closed from two threads at once. Repeated, because a
// race that fires one time in a few hundred is still one that fires on somebody's restart.
TEST_F(SocketLockFix, Close_TwoThreadsAtOnce_BothReturnAndTheSocketIsClosed)
{
	constexpr int kRounds = 300;

	for (int round = 0; round < kRounds; ++round) {
		LoopbackPair pair;
		ASSERT_TRUE(pair.Open()) << "round " << round;

		ibSocketLock lock;
		wxSocketClient* slot = pair.client;

		RunTogether([&]() { lock.Close(slot); });

		EXPECT_FALSE(slot->IsConnected()) << "round " << round;
		pair.Release();
	}
}

// Destroy takes the pointer out under the lock, so only one of two concurrent callers frees the
// socket, and the other finds the slot empty. Two frees of one socket is the same crash again.
TEST_F(SocketLockFix, Destroy_TwoThreadsAtOnce_OneFreesTheOtherFindsItEmpty)
{
	constexpr int kRounds = 300;

	for (int round = 0; round < kRounds; ++round) {
		LoopbackPair pair;
		ASSERT_TRUE(pair.Open()) << "round " << round;

		ibSocketLock lock;
		wxSocketClient* slot = pair.client;
		pair.client = nullptr;   // the slot owns it now

		RunTogether([&]() { lock.Destroy(slot); });

		EXPECT_EQ(slot, nullptr) << "round " << round;
		pair.Release();
	}
}

// The mix the connection really has: one thread closes while the other destroys.
TEST_F(SocketLockFix, CloseAndDestroy_Concurrently_NeitherTouchesAFreedSocket)
{
	constexpr int kRounds = 300;

	for (int round = 0; round < kRounds; ++round) {
		LoopbackPair pair;
		ASSERT_TRUE(pair.Open()) << "round " << round;

		ibSocketLock lock;
		wxSocketClient* slot = pair.client;
		pair.client = nullptr;

		std::atomic<int> turn{ 0 };
		RunTogether([&]() {
			if (turn.fetch_add(1) == 0) lock.Close(slot);
			else                        lock.Destroy(slot);
		});

		// Close ran first or after Destroy found the slot empty — either way, nothing crashed and the
		// slot is empty only if Destroy got there.
		lock.Destroy(slot);   // the one that lost the race, or a no-op
		EXPECT_EQ(slot, nullptr) << "round " << round;
		pair.Release();
	}
}

// After Destroy the slot is empty and a late Close is a no-op — the order the connection's thread and
// the caller's thread can arrive in.
TEST_F(SocketLockFix, Close_AfterDestroy_DoesNothing)
{
	LoopbackPair pair;
	ASSERT_TRUE(pair.Open());

	ibSocketLock lock;
	wxSocketClient* slot = pair.client;
	pair.client = nullptr;

	lock.Destroy(slot);
	ASSERT_EQ(slot, nullptr);

	lock.Close(slot);     // must not dereference anything
	lock.Destroy(slot);   // and a second Destroy is the same nothing

	pair.Release();
}

TEST_F(SocketLockFix, Assign_ThenHold_SeesTheNewSocket)
{
	LoopbackPair pair;
	ASSERT_TRUE(pair.Open());

	ibSocketLock lock;
	wxSocketClient* slot = nullptr;

	lock.Assign(slot, pair.client);
	pair.client = nullptr;

	{
		const auto hold = lock.Hold();
		ASSERT_NE(slot, nullptr);
		EXPECT_TRUE(slot->IsConnected());
	}

	lock.Destroy(slot);
	EXPECT_EQ(slot, nullptr);
	pair.Release();
}

// The CONTROL: two threads calling wxSocketBase::Close() on one socket directly, no lock.
//
// It is DISABLED and asserts nothing, because it did NOT reproduce the crash: 3 runs x 2000 rounds on
// macOS 15.6 / arm64 all passed. The window between wx's check of the descriptor and its DoClose is a
// few instructions, and a loopback pair closed twice in a tight loop does not land in it. It is kept as
// the way to look for the race, not as evidence of it:
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*RawConcurrentClose*
TEST_F(SocketLockFix, DISABLED_RawConcurrentClose_ControlThatDidNotReproduce)
{
	for (int round = 0; round < 2000; ++round) {
		LoopbackPair pair;
		ASSERT_TRUE(pair.Open());
		wxSocketClient* slot = pair.client;
		RunTogether([&]() { slot->Close(); });
		pair.Release();
	}
}
