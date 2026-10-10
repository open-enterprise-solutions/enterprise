// A park must not hand the thread's exception state or a held mutex to the
// next fiber, and a breakpoint must park the fiber rather than the thread.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/session/session.h"
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
	catch (const std::logic_error& err) {
		EXPECT_NE(std::string(err.what()).find(because), std::string::npos) << err.what();
	}
}

} // namespace

TEST(FiberAwaitGuards, AnUnwindingFiberRefusesToPark)
{
#ifndef NDEBUG
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("unwind"));
	std::atomic<bool> refused{ false };

	std::future<void> f = pool.Submit(session.get(), [&] {
		struct DuringUnwind {
			ibWorkerPoolHeadless* pool;
			ibSession*            session;
			std::atomic<bool>*    refused;
			~DuringUnwind()
			{
				try {
					pool->Await(session, [] { return false; });
				}
				catch (const std::logic_error& err) {
					if (std::string(err.what()).find("unwinding") != std::string::npos)
						refused->store(true);
				}
			}
		};
		try {
			DuringUnwind guard{ &pool, session.get(), &refused };
			throw 1;
		}
		catch (int) {
		}
	});

	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(f.get());
	EXPECT_TRUE(refused.load());
	pool.Stop();
#else
	GTEST_SKIP() << "the park guards are compiled into the debug build";
#endif
}

TEST(FiberAwaitGuards, ACatchHandlerRefusesToPark)
{
#ifndef NDEBUG
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("catch"));

	std::future<void> f = pool.Submit(session.get(), [&] {
		try {
			throw 1;
		}
		catch (int) {
			ibFiberHandlerScope handler;
			ExpectRefusal(pool, session.get(), "catch handler");
		}
	});

	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(f.get());
	pool.Stop();
#else
	GTEST_SKIP() << "the park guards are compiled into the debug build";
#endif
}

TEST(FiberAwaitGuards, AHeldMutexRefusesToPark)
{
#ifndef NDEBUG
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
#else
	GTEST_SKIP() << "the park guards are compiled into the debug build";
#endif
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
