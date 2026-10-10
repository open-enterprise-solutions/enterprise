// Stop used to return when WorkerLoop did. The worker thread was still
// running thread_local destructors — including the parked-fiber vector
// handing its buffer back — while the test destroyed the pool's sessions
// and gtest tore wx down. macOS arm64 reports that fault as a bus error.
//
// Seen twice on the macOS 14 arm64 Debug job, on builds that still
// detached the workers (develop since the fiber pool landed):
//   WorkerPoolFiber.WakeResumesOnlyTheNamedSession — printed OK, then
//   died in "Global test environment tear-down" (run 38011159031,
//   attempt 1; the same commit passed on attempt 2).
//   WorkerPoolAwait.TheWaitingThreadRunsItsSessionsTasksInOrderUntilDone
//   — bus error during the test, before OK (run 38037947017).
//
// The probe's destructor sleeps before it records that it ran, so a Stop
// that returns on the alive-count alone cannot observe the increment by
// luck. Join waits the destructor out.

#include <gtest/gtest.h>

#include "backend/session/session.h"
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

} // namespace

TEST(WorkerPoolFiber, StopJoinsTheWorkerBeforeTheCallerTearsDown)
{
	for (int pass = 0; pass < 4; ++pass) {
		auto tlsDestroyed = std::make_shared<std::atomic<int>>(0);
		std::mutex seenMtx;
		std::vector<std::thread::id> seen;

		ibWorkerPoolHeadless pool(2);
		auto sessionA = std::make_shared<ibSession>(wxT("wake-a"), ibSessionKind::Designer);
		auto sessionB = std::make_shared<ibSession>(wxT("wake-b"), ibSessionKind::Designer);

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
		std::atomic<bool> goA{ false };
		std::atomic<bool> goB{ false };

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
	}
}

// Stop skips the calling thread's handle. Destroying the pool from a
// task used to destroy that joinable std::thread, which is
// std::terminate. The destructor detaches it and the worker leaves
// without touching the pool again.
TEST(WorkerPoolFiber, PoolDestroyedFromATaskDoesNotTerminate)
{
	auto pool = std::make_unique<ibWorkerPoolHeadless>(1);
	std::future<void> done = pool->Submit(nullptr, [&]() {
		pool.reset();
	});
	ASSERT_EQ(done.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(done.get());
	EXPECT_EQ(pool, nullptr);
}
