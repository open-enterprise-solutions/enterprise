// A park must not hand the thread's exception state or a held mutex to the
// next fiber, and a breakpoint must park the fiber rather than the thread.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/workerPoolHeadless.h"
#include "core/fiber/fiber.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

class ibLatch {
public:
	void Signal()
	{
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_set = true;
		}
		m_cv.notify_all();
	}
	bool Wait(std::chrono::milliseconds timeout = std::chrono::seconds(5))
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		return m_cv.wait_for(lk, timeout, [this] { return m_set; });
	}
private:
	std::mutex              m_mtx;
	std::condition_variable m_cv;
	bool                    m_set = false;
};

std::shared_ptr<ibSession> MakeSession(const wxString& id)
{
	return std::make_shared<ibSession>(id, ibSessionKind::Designer);
}

class ibBaseForTest {
public:
	ibBaseForTest()
	{
		if (m_wxInit.IsOk() && ibApplicationHost::IsEmpty()) {
			ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE);
			m_owns = !ibApplicationHost::IsEmpty();
		}
	}
	~ibBaseForTest()
	{
		if (m_owns)
			ibApplicationInstance::DestroyAppDataEnv();
	}
	bool IsOpen() const { return !ibApplicationHost::IsEmpty(); }
private:
	wxInitializer m_wxInit;
	bool          m_owns = false;
};

void ExpectRefusal(ibWorkerPoolHeadless& pool, ibSession* session, const char* because)
{
	try {
		pool.Await(session, [] { return false; });
		FAIL() << "the fiber parked";
	}
	catch (const ibBackendException& err) {
		EXPECT_NE(std::string(err.what()).find(because), std::string::npos) << err.what();
	}
}

} // namespace

TEST(FiberAwaitGuards, AnUnwindingFiberDoesNotThrowFromTheDestructor)
{
	// A destructor is noexcept. The park returns without throwing, and
	// the exception that is already in flight goes on.
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("unwind"));
	std::atomic<bool> returned{ false };

	std::future<void> f = pool.Submit(session.get(), [&] {
		struct DuringUnwind {
			ibWorkerPoolHeadless* pool;
			ibSession*            session;
			std::atomic<bool>*    returned;
			~DuringUnwind()
			{
				pool->Await(session, [] { return false; });
				returned->store(true);
			}
		};
		try {
			DuringUnwind guard{ &pool, session.get(), &returned };
			throw 1;
		}
		catch (int) {
		}
	});

	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(f.get());
	EXPECT_TRUE(returned.load());
	pool.Stop();
}

TEST(FiberAwaitGuards, ACatchHandlerRefusesToPark)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("catch"));

	std::future<void> f = pool.Submit(session.get(), [&] {
		try {
			throw 1;
		}
		catch (int) {
			ExpectRefusal(pool, session.get(), "catch handler");
		}
	});

	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(f.get());
	pool.Stop();
}

TEST(FiberAwaitGuards, AHeldMutexRefusesToPark)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("mutex"));

	std::future<void> f = pool.Submit(session.get(), [&] {
		std::mutex gate;
		ibFiberMutexLock<std::mutex> held(gate);
		ExpectRefusal(pool, session.get(), "mutex");
	});

	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(f.get());
	pool.Stop();
}

TEST(FiberAwaitGuards, ABreakpointDoesNotFreezeTheSibling)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen()) << "no base, so the session cannot attach a debugger";
	ibSessionRegistry* const reg = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(reg, nullptr);

	ibWorkerPoolHeadless pool(1);
	auto stopped = MakeSession(wxT("break-a"));
	auto sibling = MakeSession(wxT("break-b"));
	reg->EnableDebugForSession(stopped.get());
	ASSERT_NE(stopped->Debug(), nullptr);

	ibLatch entered;
	std::future<void> fa = pool.Submit(stopped.get(), [&] {
		stopped->Debug()->m_debugLoop = true;
		entered.Signal();
		EXPECT_TRUE(stopped->ParkDebugLoop());
	});
	ASSERT_TRUE(entered.Wait());

	ibLatch siblingDone;
	std::future<void> fb = pool.Submit(sibling.get(), [&] {
		siblingDone.Signal();
	});
	// Before the breakpoint is released. A wait on the OS thread never
	// reaches this; a parked fiber leaves the thread free to run it.
	const bool ran = siblingDone.Wait(std::chrono::seconds(2));

	stopped->Debug()->m_debugLoop = false;
	pool.Wake(stopped.get());
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	ASSERT_EQ(fb.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fb.get());
	EXPECT_TRUE(ran) << "the breakpoint parked the thread, so the sibling never ran";
	pool.Stop();
}

TEST(FiberAwaitGuards, DetachReleasesAStoppedFiberAndDoesNotRunTheCallInsideIt)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());
	ibSessionRegistry* const reg = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(reg, nullptr);
	reg->Start();

	ibSessionHolder holder = reg->CreateSessionOfKind(
		ibRunMode::eFILE_MODE, wxT("fiber-await"), ibSessionKind::Designer, {});
	ASSERT_TRUE(static_cast<bool>(holder));
	ibSession* const stopped = holder.Get();
	reg->EnableDebugForSession(stopped);
	ibWorkerPool* const pool = stopped->GetWorkerPool();
	ASSERT_NE(pool, nullptr);

	ibLatch entered;
	std::atomic<bool> stopReturned{ false };
	std::future<void> fa = pool->Submit(stopped, [&] {
		stopped->Debug()->m_debugLoop = true;
		reg->EnterDebugLoop(stopped);
		entered.Signal();
		EXPECT_TRUE(stopped->ParkDebugLoop());
		stopReturned.store(true);
	});
	ASSERT_TRUE(entered.Wait());

	std::atomic<bool> callRanInside{ false };
	std::future<void> call = pool->Submit(stopped, [&] {
		callRanInside.store(!stopReturned.load());
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	EXPECT_FALSE(callRanInside.load());
	EXPECT_EQ(call.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

	// The road Detach and ResetDebugger take.
	reg->ReleaseDebugParks();
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	ASSERT_EQ(call.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(call.get());
	EXPECT_FALSE(callRanInside.load());
}

TEST(FiberAwaitGuards, ASecondCloseWhileBeforeExitAsksDoesNotLockAgain)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("second-close"));
	std::mutex gate;
	ibLatch inside;
	std::atomic<bool> release{ false };
	std::atomic<bool> secondRan{ false };

	std::future<void> first = pool.Submit(session.get(), [&] {
		ibDeferInlineTasks defer(session.get());
		std::lock_guard<std::mutex> lock(gate);
		pool.Await(session.get(), [&] {
			inside.Signal();
			return release.load();
		});
	});
	ASSERT_TRUE(inside.Wait());

	std::future<void> second = pool.Submit(session.get(), [&] {
		std::lock_guard<std::mutex> lock(gate);
		secondRan.store(true);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	EXPECT_FALSE(secondRan.load());

	release.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(first.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(first.get());
	ASSERT_EQ(second.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(second.get());
	EXPECT_TRUE(secondRan.load());
	pool.Stop();
}
