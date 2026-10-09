// =============================================================================
// A question parked on a person is a fiber, not an OS thread.
//
// ibWorkerPoolHeadless::Await suspends the session's stack and returns the
// worker to the scheduler. Wake resumes that session's fiber on the thread
// that parked it. The script API is unchanged — these tests call the pool
// door directly.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/session/fiberLocals.h"
#include "backend/session/session.h"
#include "backend/session/workerPoolHeadless.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
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

std::shared_ptr<ibSession> MakeSession(const wxString& id)
{
	return std::make_shared<ibSession>(id, ibSessionKind::Designer);
}

void MarkRunning(ibSession& session)
{
	// Cancel on a host session only sticks when a run is in progress.
	// Designer sessions start Idle; the test is the run.
	const_cast<std::atomic<ibRunState>*>(session.RunState())->store(ibRunState::Running);
}

// A value set inside one session and read by the next one on the same
// thread. Registered before any pool exists (static init); the first
// fiber snapshot is taken later, from a worker.
thread_local int g_marker = 0;

struct ibRegisterMarker {
	ibRegisterMarker()
	{
		ibFiberLocals::RegisterTrivial<int>(
			[](void* dst) { *static_cast<int*>(dst) = g_marker; },
			[](const void* src) { g_marker = *static_cast<const int*>(src); });
	}
};

const ibRegisterMarker s_registerMarker;

bool WaitUntil(const std::function<bool()>& pred, std::chrono::milliseconds budget)
{
	const auto deadline = std::chrono::steady_clock::now() + budget;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return pred();
}

#if defined(__linux__)

struct ibProcStatus {
	long threads = -1;
	long vmRssKb = -1;
	long vmSizeKb = -1;
};

ibProcStatus ReadProcStatus()
{
	ibProcStatus status;
	std::ifstream in("/proc/self/status");
	std::string key;
	while (in >> key) {
		if (key == "Threads:")
			in >> status.threads;
		else if (key == "VmRSS:")
			in >> status.vmRssKb;
		else if (key == "VmSize:")
			in >> status.vmSizeKb;
		else {
			std::string rest;
			std::getline(in, rest);
		}
	}
	return status;
}

#endif

} // namespace

TEST(WorkerPoolFiber, AwaitOffAFiberIsRefused)
{
	ibWorkerPoolHeadless pool(1);
	EXPECT_THROW(pool.Await(nullptr, [] { return true; }), std::logic_error);
	EXPECT_NO_THROW(pool.Wake(nullptr));
	pool.Stop();
}

TEST(WorkerPoolFiber, ParkedFiberDoesNotLeakItsLocals)
{
	ibWorkerPoolHeadless pool(1);
	auto first = MakeSession(wxT("local-a"));
	auto second = MakeSession(wxT("local-b"));

	ibLatch firstParked;
	ibLatch secondSaw;
	std::atomic<bool> releaseFirst{ false };
	std::atomic<int> secondObserved{ -1 };
	std::atomic<int> firstObserved{ -1 };

	std::future<void> fa = pool.Submit(first.get(), [&] {
		g_marker = 11;
		pool.Await(first.get(), [&] {
			firstParked.Signal();
			return releaseFirst.load();
		});
		firstObserved.store(g_marker);
	});
	ASSERT_TRUE(firstParked.Wait());

	std::future<void> fb = pool.Submit(second.get(), [&] {
		secondObserved.store(g_marker);
		g_marker = 22;
		secondSaw.Signal();
	});
	ASSERT_TRUE(secondSaw.Wait()) << "the second session never ran on the freed thread";
	EXPECT_EQ(secondObserved.load(), 0)
		<< "the parked session's marker leaked into the session that reused its thread";

	releaseFirst.store(true);
	pool.Wake(first.get());
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	EXPECT_EQ(firstObserved.load(), 11);
	EXPECT_NO_THROW(fb.get());
	pool.Stop();
}

TEST(WorkerPoolFiber, WakeResumesOnlyTheNamedSession)
{
	ibWorkerPoolHeadless pool(2);
	auto sessionA = MakeSession(wxT("wake-a"));
	auto sessionB = MakeSession(wxT("wake-b"));

	ibLatch parkedA;
	ibLatch parkedB;
	std::atomic<bool> goA{ false };
	std::atomic<bool> goB{ false };

	std::future<void> fa = pool.Submit(sessionA.get(), [&] {
		pool.Await(sessionA.get(), [&] {
			parkedA.Signal();
			return goA.load();
		});
	});
	std::future<void> fb = pool.Submit(sessionB.get(), [&] {
		pool.Await(sessionB.get(), [&] {
			parkedB.Signal();
			return goB.load();
		});
	});
	ASSERT_TRUE(parkedA.Wait());
	ASSERT_TRUE(parkedB.Wait());

	goA.store(true);
	pool.Wake(sessionA.get());
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	EXPECT_EQ(fb.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout)
		<< "waking A resumed B";

	goB.store(true);
	pool.Wake(sessionB.get());
	ASSERT_EQ(fb.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fb.get());
	pool.Stop();
}

TEST(WorkerPoolFiber, NestedQuestionOuterDoesNotReturnBeforeInner)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("nested"));

	ibLatch outerParked;
	ibLatch innerParked;
	std::atomic<bool> releaseInner{ false };
	std::atomic<bool> releaseOuter{ false };
	std::atomic<bool> innerReturned{ false };
	std::atomic<bool> outerSawInner{ false };

	std::future<void> outer = pool.Submit(session.get(), [&] {
		pool.Await(session.get(), [&] {
			outerParked.Signal();
			return releaseOuter.load();
		});
		outerSawInner.store(innerReturned.load());
	});
	ASSERT_TRUE(outerParked.Wait());

	// Queued while the outer frame is parked. The lease is still held, so
	// the inner task runs on that same fiber, under the outer Await.
	std::future<void> inner = pool.Submit(session.get(), [&] {
		pool.Await(session.get(), [&] {
			innerParked.Signal();
			return releaseInner.load();
		});
		innerReturned.store(true);
	});
	ASSERT_TRUE(innerParked.Wait());
	EXPECT_EQ(outer.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);

	releaseInner.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(inner.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(inner.get());
	EXPECT_TRUE(innerReturned.load());
	EXPECT_EQ(outer.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout)
		<< "the outer question returned before the inner one";

	releaseOuter.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(outer.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(outer.get());
	EXPECT_TRUE(outerSawInner.load());
	pool.Stop();
}

TEST(WorkerPoolFiber, CancelWhileWaitingThrowsInTheScript)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("cancel-wait"));
	MarkRunning(*session);

	ibLatch parked;
	std::atomic<int> depth{ 0 };
	std::future<void> waiting = pool.Submit(session.get(), [&] {
		struct Depth {
			std::atomic<int>& m_depth;
			explicit Depth(std::atomic<int>& depth) : m_depth(depth) { m_depth.fetch_add(1); }
			~Depth() { m_depth.fetch_sub(1); }
		} guard(depth);
		pool.Await(session.get(), [&] {
			parked.Signal();
			return false;
		});
	});
	ASSERT_TRUE(parked.Wait());

	session->Cancel();

	// Submitted after the cancel. It must run, and it must run after the
	// waiting frame has unwound — not underneath it.
	std::atomic<int> depthSeen{ -1 };
	ibLatch teardownRan;
	std::future<void> teardown = pool.Submit(session.get(), [&] {
		depthSeen.store(depth.load());
		teardownRan.Signal();
	});

	ASSERT_EQ(waiting.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_THROW(waiting.get(), ibBackendInterruptException);
	ASSERT_TRUE(teardownRan.Wait());
	EXPECT_EQ(depthSeen.load(), 0);
	EXPECT_NO_THROW(teardown.get());
	pool.Stop();
}

TEST(WorkerPoolFiber, DropWhileParkedDoesNotBlockOrLeak)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("drop-parked"));

	ibLatch parked;
	std::atomic<bool> release{ false };
	std::future<void> task = pool.Submit(session.get(), [&] {
		pool.Await(session.get(), [&] {
			parked.Signal();
			return release.load();
		});
	});
	ASSERT_TRUE(parked.Wait());

	const auto before = std::chrono::steady_clock::now();
	pool.Drop(session.get());
	const auto spent = std::chrono::steady_clock::now() - before;
	EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(spent).count(), 500);

	release.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(task.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(task.get());
	pool.Stop();
}

TEST(WorkerPoolFiber, StopUnwindsParkedFibers)
{
	ibWorkerPoolHeadless pool(2);
	constexpr int n = 4;
	std::vector<std::shared_ptr<ibSession>> sessions;
	std::vector<std::future<void>> futures;
	std::atomic<int> parked{ 0 };
	sessions.reserve(n);
	futures.reserve(n);
	for (int i = 0; i < n; ++i) {
		sessions.push_back(MakeSession(wxString::Format(wxT("stop-%d"), i)));
		ibSession* raw = sessions.back().get();
		futures.push_back(pool.Submit(raw, [raw, &parked] {
			pool.Await(raw, [counted = false, &parked]() mutable {
				if (!counted) {
					counted = true;
					parked.fetch_add(1);
				}
				return false;
			});
		}));
	}
	ASSERT_TRUE(WaitUntil([&] { return parked.load() >= n; }, std::chrono::seconds(10)))
		<< "parked " << parked.load() << " of " << n;

	pool.Stop();
	for (std::future<void>& f : futures) {
		ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
		EXPECT_THROW(f.get(), ibBackendInterruptException);
	}
}

TEST(WorkerPoolFiber, TaskExceptionReachesTheFuture)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("task-throws"));
	std::future<void> f = pool.Submit(session.get(), [] {
		throw std::runtime_error("from the task");
	});
	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_THROW(f.get(), std::runtime_error);

	// The worker is still there afterwards.
	std::atomic<bool> ran{ false };
	std::future<void> next = pool.Submit(session.get(), [&] { ran.store(true); });
	ASSERT_EQ(next.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(next.get());
	EXPECT_TRUE(ran.load());
	pool.Stop();
}

#if defined(__linux__)

TEST(WorkerPoolFiber, OpenQuestionsDoNotGrowOsThreads)
{
	const ibProcStatus baseline = ReadProcStatus();
	ASSERT_GT(baseline.threads, 0);

	constexpr int n = 1000;
	constexpr std::size_t cap = 4;
	ibWorkerPoolHeadless pool(cap);

	std::vector<std::shared_ptr<ibSession>> sessions;
	std::vector<std::future<void>> futures;
	std::atomic<int> waiting{ 0 };
	std::atomic<bool> go{ false };
	sessions.reserve(n);
	futures.reserve(n);
	for (int i = 0; i < n; ++i) {
		sessions.push_back(MakeSession(wxString::Format(wxT("open-%d"), i)));
		ibSession* raw = sessions.back().get();
		futures.push_back(pool.Submit(raw, [raw, &waiting, &go] {
			pool.Await(raw, [counted = false, &waiting, &go]() mutable {
				if (!counted) {
					counted = true;
					waiting.fetch_add(1, std::memory_order_release);
				}
				return go.load(std::memory_order_acquire);
			});
		}));
	}

	ASSERT_TRUE(WaitUntil([&] {
		return waiting.load(std::memory_order_acquire) >= n
			&& pool.AliveWorkers() <= cap
			&& pool.IdleWorkers() == pool.AliveWorkers()
			&& pool.AliveWorkers() > 0;
	}, std::chrono::seconds(30)))
		<< "waiting " << waiting.load()
		<< " alive " << pool.AliveWorkers()
		<< " idle " << pool.IdleWorkers();

	const ibProcStatus parked = ReadProcStatus();
	EXPECT_LE(parked.threads, baseline.threads + static_cast<long>(cap) + 8)
		<< "baseline " << baseline.threads << " parked " << parked.threads
		<< " rss_kb " << parked.vmRssKb << " vmsize_kb " << parked.vmSizeKb;

	go.store(true, std::memory_order_release);
	for (const std::shared_ptr<ibSession>& session : sessions)
		pool.Wake(session.get());
	for (std::future<void>& f : futures) {
		ASSERT_EQ(f.wait_for(std::chrono::seconds(30)), std::future_status::ready);
		EXPECT_NO_THROW(f.get());
	}
	pool.Stop();
}

TEST(WorkerPoolFiber, MeasuresThreadsAndMemory)
{
	std::ofstream out("/tmp/oes-fiber-measure.txt", std::ios::trunc);
	ASSERT_TRUE(out.good());

	const ibProcStatus baseline = ReadProcStatus();
	out << "baseline threads=" << baseline.threads
		<< " rss_kb=" << baseline.vmRssKb
		<< " vmsize_kb=" << baseline.vmSizeKb << "\n";

	auto note = [&](const char* kind, int n, const ibProcStatus& status) {
		out << kind << " n=" << n
			<< " threads=" << status.threads
			<< " rss_kb=" << status.vmRssKb
			<< " vmsize_kb=" << status.vmSizeKb << "\n";
		std::cerr << kind << " n=" << n
			<< " threads=" << status.threads
			<< " rss_kb=" << status.vmRssKb
			<< " vmsize_kb=" << status.vmSizeKb << "\n";
	};

	for (int n : { 10, 100, 1000 }) {
		std::mutex mtx;
		std::condition_variable cv;
		bool go = false;
		std::atomic<int> up{ 0 };
		std::vector<std::thread> threads;
		threads.reserve(static_cast<std::size_t>(n));
		for (int i = 0; i < n; ++i) {
			threads.emplace_back([&] {
				up.fetch_add(1, std::memory_order_release);
				std::unique_lock<std::mutex> lk(mtx);
				cv.wait(lk, [&] { return go; });
			});
		}
		ASSERT_TRUE(WaitUntil([&] { return up.load() >= n; }, std::chrono::seconds(30)));
		note("blocked-threads", n, ReadProcStatus());
		{
			std::lock_guard<std::mutex> lk(mtx);
			go = true;
		}
		cv.notify_all();
		for (std::thread& t : threads)
			t.join();
	}

	for (int n : { 10, 100, 1000 }) {
		constexpr std::size_t cap = 4;
		ibWorkerPoolHeadless pool(cap);
		std::vector<std::shared_ptr<ibSession>> sessions;
		std::vector<std::future<void>> futures;
		std::atomic<int> waiting{ 0 };
		std::atomic<bool> go{ false };
		sessions.reserve(static_cast<std::size_t>(n));
		futures.reserve(static_cast<std::size_t>(n));
		for (int i = 0; i < n; ++i) {
			sessions.push_back(MakeSession(wxString::Format(wxT("measure-%d-%d"), n, i)));
			ibSession* raw = sessions.back().get();
			futures.push_back(pool.Submit(raw, [raw, &waiting, &go] {
				pool.Await(raw, [counted = false, &waiting, &go]() mutable {
					if (!counted) {
						counted = true;
						waiting.fetch_add(1, std::memory_order_release);
					}
					return go.load(std::memory_order_acquire);
				});
			}));
		}
		ASSERT_TRUE(WaitUntil([&] {
			return waiting.load() >= n
				&& pool.IdleWorkers() == pool.AliveWorkers()
				&& pool.AliveWorkers() > 0;
		}, std::chrono::seconds(30)))
			<< "n=" << n << " waiting " << waiting.load();
		const ibProcStatus parked = ReadProcStatus();
		note("fibers", n, parked);
		EXPECT_LE(parked.threads, baseline.threads + static_cast<long>(cap) + 8);

		go.store(true);
		for (const std::shared_ptr<ibSession>& session : sessions)
			pool.Wake(session.get());
		for (std::future<void>& f : futures) {
			ASSERT_EQ(f.wait_for(std::chrono::seconds(30)), std::future_status::ready);
			f.get();
		}
		pool.Stop();
	}
}

#endif
