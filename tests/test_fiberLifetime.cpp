// The pool finds a session's queue by the session's address, and a fiber
// parked on a question keeps that address on its stack. Drop, or a session
// that has reached Stopping, is an interrupt: the fiber unwinds while the
// base is still open, and the future throws. The lease's hold keeps the
// address live for that unwind, then FinishFiber releases it on the
// scheduler — ~ibSession does not run on the fiber.
//
// Stop asks the same question through the queue's weak hold. A second
// Stop, the one the pool's destructor makes after the sessions are
// already gone, locks that hold and finds it expired. It does not call
// through the map key.

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/workerPool.h"
#include "backend/session/workerPoolHeadless.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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

// Destroyed when the worker thread exits, which is after WorkerLoop and
// after thread_local destructors start — later than m_aliveWorkers hitting
// zero. The sleep keeps a Stop that returns on the counter alone from
// observing the increment by luck.
struct ibWorkerExitProbe {
	std::shared_ptr<std::atomic<int>> destroyed;
	~ibWorkerExitProbe()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		if (destroyed != nullptr)
			destroyed->fetch_add(1, std::memory_order_release);
	}
};

void TouchWorkerExitProbe(const std::shared_ptr<std::atomic<int>>& destroyed)
{
	thread_local ibWorkerExitProbe probe{ destroyed };
	(void)probe;
}

bool WaitUntil(const std::atomic<bool>& flag, std::chrono::milliseconds budget = std::chrono::seconds(5))
{
	const auto deadline = std::chrono::steady_clock::now() + budget;
	while (!flag.load() && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	return flag.load();
}

// Destruction is the signal that the last shared_ptr let go. GetWorkerPool
// is the pool the test built, so teardown's barrier is submitted there and
// not run inline on the thread that is closing.
class ibWatchedSession : public ibSession {
public:
	ibWatchedSession(const wxString& id, std::atomic<bool>* died, ibWorkerPool* pool)
		: ibSession(id, ibSessionKind::Designer)
		, m_died(died)
		, m_pool(pool)
	{
	}
	~ibWatchedSession() override
	{
		if (m_died != nullptr)
			m_died->store(true);
	}
	ibWorkerPool* GetWorkerPool() const override { return m_pool; }

private:
	std::atomic<bool>* m_died;
	ibWorkerPool*      m_pool;
};

} // namespace

TEST(FiberLifetime, DroppedWhileParkedUnwindsAndTheSessionDies)
{
	ibWorkerPoolHeadless pool(1);
	std::atomic<bool> died { false };
	auto session = std::make_shared<ibWatchedSession>(wxT("parked-holder"), &died, &pool);

	ibWatchedSession* const raw = session.get();
	std::atomic<bool> answered { false };
	ibLatch parked;
	std::future<void> waiting = pool.Submit(raw, [&]() {
		parked.Signal();
		pool.Await(raw, [&] { return answered.load(); });
	});
	ASSERT_TRUE(parked.Wait()) << "the question never parked";

	pool.Drop(raw);
	session.reset();
	ASSERT_EQ(waiting.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_THROW(waiting.get(), ibBackendException);
	EXPECT_TRUE(WaitUntil(died))
		<< "the fiber unwound and the session was still alive";
	EXPECT_FALSE(answered.load())
		<< "the drop must not wait for the person to answer";

	pool.Stop();
}

TEST(FiberLifetime, TheOwnerClosingWhileParkedUnwindsTheFiber)
{
	ibWorkerPoolHeadless pool(1);
	std::atomic<bool> died { false };
	auto session = std::make_shared<ibWatchedSession>(wxT("owner-closes"), &died, &pool);
	ibSessionHolder holder(session);

	std::atomic<bool> answered { false };
	ibLatch parked;
	std::future<void> waiting = pool.Submit(session.get(), [&]() {
		parked.Signal();
		pool.Await(session.get(), [&] { return answered.load(); });
	});
	ASSERT_TRUE(parked.Wait());

	// The test lets go. The holder is the owner, as a frame is, and
	// releasing it is what closing the base does to the session.
	session.reset();
	holder.Reset();

	ASSERT_EQ(waiting.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_THROW(waiting.get(), ibBackendException);
	EXPECT_TRUE(WaitUntil(died))
		<< "the owner finished closing and the session was still alive";
	EXPECT_FALSE(answered.load())
		<< "the close must not wait for the person to answer";

	pool.Stop();
}

TEST(FiberLifetime, StopAfterTheSessionsAreGoneDoesNotCallThroughThem)
{
	ibWorkerPoolHeadless pool(1);
	std::atomic<bool> died { false };
	{
		auto session = std::make_shared<ibWatchedSession>(wxT("stopped-twice"), &died, &pool);
		std::future<void> ran = pool.Submit(session.get(), [] {});
		ASSERT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready);
		EXPECT_NO_THROW(ran.get());
		// First Stop, while the session is still alive. The destructor
		// Stops again after the session is gone — the map still has the
		// address, and the weak hold is what says not to call through it.
		pool.Stop();
		session.reset();
	}
	EXPECT_TRUE(died.load());
	EXPECT_NO_THROW(pool.Stop());
}

// The macOS arm64 run of WorkerPoolFiber.WakeResumesOnlyTheNamedSession
// printed OK and then took a bus error in process tear-down. Stop had
// returned as soon as WorkerLoop did, so the worker was still exiting
// while the sessions were destroyed and wx shut down. Repeating the
// same wake, then requiring each worker's thread_local destructor to
// have finished before those sessions are released.
TEST(FiberLifetime, NamedWakeStopFinishesWorkerThreadsBeforeTheSessionsDie)
{
	for (int pass = 0; pass < 8; ++pass) {
		auto tlsDestroyed = std::make_shared<std::atomic<int>>(0);
		std::mutex seenMtx;
		std::vector<std::thread::id> seen;

		ibWorkerPoolHeadless pool(2);
		std::atomic<bool> diedA { false };
		std::atomic<bool> diedB { false };
		auto sessionA = std::make_shared<ibWatchedSession>(wxT("wake-a"), &diedA, &pool);
		auto sessionB = std::make_shared<ibWatchedSession>(wxT("wake-b"), &diedB, &pool);

		auto noteThread = [&]() {
			const auto id = std::this_thread::get_id();
			{
				std::lock_guard<std::mutex> lk(seenMtx);
				for (const std::thread::id known : seen) {
					if (known == id)
						return;
				}
				seen.push_back(id);
			}
			TouchWorkerExitProbe(tlsDestroyed);
		};

		ibLatch parkedA;
		ibLatch parkedB;
		std::atomic<bool> goA { false };
		std::atomic<bool> goB { false };

		std::future<void> fa = pool.Submit(sessionA.get(), [&]() {
			noteThread();
			pool.Await(sessionA.get(), [&]() {
				parkedA.Signal();
				return goA.load();
			});
		});
		std::future<void> fb = pool.Submit(sessionB.get(), [&]() {
			noteThread();
			pool.Await(sessionB.get(), [&]() {
				parkedB.Signal();
				return goB.load();
			});
		});
		ASSERT_TRUE(parkedA.Wait()) << "pass " << pass;
		ASSERT_TRUE(parkedB.Wait()) << "pass " << pass;

		goA.store(true);
		pool.Wake(sessionA.get());
		ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready) << "pass " << pass;
		EXPECT_NO_THROW(fa.get());
		EXPECT_EQ(fb.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout)
			<< "pass " << pass << ": waking A resumed B";

		goB.store(true);
		pool.Wake(sessionB.get());
		ASSERT_EQ(fb.wait_for(std::chrono::seconds(5)), std::future_status::ready) << "pass " << pass;
		EXPECT_NO_THROW(fb.get());

		pool.Stop();

		int touched = 0;
		{
			std::lock_guard<std::mutex> lk(seenMtx);
			touched = static_cast<int>(seen.size());
		}
		EXPECT_GE(touched, 1) << "pass " << pass;
		EXPECT_EQ(tlsDestroyed->load(std::memory_order_acquire), touched)
			<< "pass " << pass << ": Stop returned before the worker thread exited";
		EXPECT_FALSE(diedA.load()) << "pass " << pass;
		EXPECT_FALSE(diedB.load()) << "pass " << pass;
		sessionA.reset();
		sessionB.reset();
		EXPECT_TRUE(diedA.load()) << "pass " << pass;
		EXPECT_TRUE(diedB.load()) << "pass " << pass;
	}
}
