// =============================================================================
// The debugger's port — the core's WebSocket (core/net) at /debugger: one designer at a time, each command one
// binary message, handed to the debug thread through the socket's queue (ibDebugCommandQueue, debugDefs.h).
//
// It took the place of ibSocketLock (#155): two threads closing one wxSocketBase is a hazard the debugger no longer
// has — every call on a socket is safe from any thread, and its going is said once.
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <wx/init.h>

#include "backend/debugger/debugServer.h"
#include "core/net/httpClient.h"
#include "core/net/ioThreads.h"

// A debugger server with no configuration around it — for the port tests below, which need the port and nothing
// it would ask a configuration for. In production only ibMetaDataConfiguration makes one.
class ibStandaloneDebuggerServer {
public:
	static std::unique_ptr<ibDebuggerServer> Make() {
		return std::unique_ptr<ibDebuggerServer>(new ibDebuggerServer(nullptr));
	}

	// A designer is on its port.
	static bool IsConnected(const ibDebuggerServer& server) {
		return server.m_socketConnectionThread != nullptr && server.m_socketConnectionThread->IsConnected();
	}

	// The port it listens on — 0 while it is not up yet.
	static std::uint16_t GetHttpPort(const ibDebuggerServer& server) {
		const ibDebuggerServer::ibDebuggerServerConnection* thread = server.m_socketConnectionThread.get();
		if (thread == nullptr || !thread->m_bindReady.load(std::memory_order_acquire) || thread->m_socketServer == nullptr)
			return 0;
		return thread->m_socketServer->GetHttpPort();
	}
};

namespace {

using namespace std::chrono_literals;

struct DebugServerFix : ::testing::Test {
	wxInitializer m_wx;
	void SetUp() override {
		if (!m_wx.IsOk()) GTEST_SKIP() << "wx did not initialise";
	}
};

// Asks `done` until it answers yes, for up to `limit`.
template <class Done>
bool Within(std::chrono::milliseconds limit, Done done) {
	const auto until = std::chrono::steady_clock::now() + limit;
	while (!done()) {
		if (std::chrono::steady_clock::now() >= until)
			return false;
		std::this_thread::sleep_for(5ms);
	}
	return true;
}

// A designer's socket to the port, and whether it has gone — held by its handler too, which may be told after the
// socket was let go.
struct DesignerSocket {
	std::shared_ptr<std::atomic<bool>> m_closed = std::make_shared<std::atomic<bool>>(false);
	std::unique_ptr<ibWebSocketClient> m_socketClient;

	DesignerSocket(ibIoThreads& threads, std::uint16_t port) {
		const std::shared_ptr<std::atomic<bool>> closed = m_closed;
		m_socketClient = std::make_unique<ibWebSocketClient>(threads.GetContext(), nullptr, nullptr, nullptr,
			[closed](const std::string&) { closed->store(true); });
		m_socketClient->Open("127.0.0.1", port, "/debugger");
	}
};

} // namespace

// The port belongs to its connection object, and its own thread takes it down: ShutdownServer asks the thread to
// stop, and the thread, waiting in a bounded interval, stops the port and ends. Repeated real lifecycles keep
// shutdown bounded — on macOS the wx listener's teardown once raced two CFRunLoop source operations here.
TEST_F(DebugServerFix, ServerShutdown_WhileWaitingForClient_IsBounded)
{
	constexpr int kRounds = 100;
	constexpr auto kShutdownLimit = 1s;

	for (int round = 0; round < kRounds; ++round) {
		auto server = ibStandaloneDebuggerServer::Make();
		ASSERT_TRUE(server->CreateServer(wxT("127.0.0.1"), 49170, false))
			<< "round " << round;

		const auto started = std::chrono::steady_clock::now();
		server->ShutdownServer();
		EXPECT_LT(std::chrono::steady_clock::now() - started, kShutdownLimit)
			<< "round " << round;
	}
}

// ONE DESIGNER AT A TIME: a second one is refused while the first is on, the first going takes the next, and the
// port going lets its designer go.
TEST_F(DebugServerFix, AcceptConnection_SecondDesigner_IsRefusedAndTheFirstStays)
{
	// A PORT THE SYSTEM CHOOSES, and the designer knocks where the server listens: a port named here may be held by a
	// socket of the tests before it — on Windows the ports from 49152 are the ones handed to outgoing connections — and
	// the server takes the next one while the designer knocks on the first (CI, 2026-10-21).
	auto server = ibStandaloneDebuggerServer::Make();
	ASSERT_TRUE(server->CreateServer(wxT("127.0.0.1"), 0, false));
	ASSERT_TRUE(Within(5s, [&]() { return ibStandaloneDebuggerServer::GetHttpPort(*server) != 0; }));
	const std::uint16_t port = ibStandaloneDebuggerServer::GetHttpPort(*server);

	ibIoThreads threads;

	DesignerSocket first(threads, port);
	ASSERT_TRUE(Within(5s, [&]() { return ibStandaloneDebuggerServer::IsConnected(*server); }));

	DesignerSocket second(threads, port);
	EXPECT_TRUE(Within(5s, [&]() { return second.m_closed->load(); })) << "a second designer is refused";
	EXPECT_FALSE(first.m_closed->load());
	EXPECT_TRUE(ibStandaloneDebuggerServer::IsConnected(*server));

	first.m_socketClient->Close();
	EXPECT_TRUE(Within(5s, [&]() { return !ibStandaloneDebuggerServer::IsConnected(*server); }))
		<< "the first designer going empties the slot";

	DesignerSocket third(threads, port);
	EXPECT_TRUE(Within(5s, [&]() { return ibStandaloneDebuggerServer::IsConnected(*server); }))
		<< "the next designer is taken";

	server->ShutdownServer();
	EXPECT_TRUE(Within(5s, [&]() { return third.m_closed->load(); })) << "the port going lets its designer go";
}

TEST(DebugCommandQueue, Pop_Commands_ComeInTheOrderTheyCame)
{
	ibDebugCommandQueue commandQueue;
	commandQueue.Push("one");
	commandQueue.Push("two");

	std::string command;
	ASSERT_TRUE(commandQueue.Pop(command, 0));
	EXPECT_EQ(command, "one");
	ASSERT_TRUE(commandQueue.Pop(command, 0));
	EXPECT_EQ(command, "two");
}

// The socket's going does not take what it said before with it.
TEST(DebugCommandQueue, Stop_AfterCommands_TheyAreStillPopped)
{
	ibDebugCommandQueue commandQueue;
	commandQueue.Push("last words");
	commandQueue.Stop("dropped");

	std::string command;
	ASSERT_TRUE(commandQueue.Pop(command, 0));
	EXPECT_EQ(command, "last words");
	EXPECT_FALSE(commandQueue.Pop(command, 0));
	EXPECT_TRUE(commandQueue.IsStopped());
	EXPECT_EQ(commandQueue.GetReason(), "dropped");
}

// Nothing arriving is not the socket going: Pop answers no within its wait, and the queue is not stopped.
TEST(DebugCommandQueue, Pop_NothingCame_AnswersNoWithinItsWait)
{
	ibDebugCommandQueue commandQueue;

	std::string command;
	const auto started = std::chrono::steady_clock::now();
	EXPECT_FALSE(commandQueue.Pop(command, 20));
	EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
	EXPECT_FALSE(commandQueue.IsStopped());
}

// A command pushed from another thread wakes the one waiting for it.
TEST(DebugCommandQueue, Push_FromAnotherThread_WakesTheWaitingPop)
{
	ibDebugCommandQueue commandQueue;

	std::thread pusher([&commandQueue]() {
		std::this_thread::sleep_for(20ms);
		commandQueue.Push("step");
	});

	std::string command;
	EXPECT_TRUE(commandQueue.Pop(command, 5000));
	EXPECT_EQ(command, "step");
	pusher.join();
}
