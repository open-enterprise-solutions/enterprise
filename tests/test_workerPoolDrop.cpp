// =============================================================================
// The worker pool's lease/drop interlock — ibWorkerPoolHeadless.
//
// A session owns a queue in the pool, keyed on its own pointer, and a worker
// LEASES that queue while it drains it. Dropping a session (its teardown) while
// a worker stands on its queue is not an edge case: the teardown usually runs
// from INSIDE one of that session's own tasks, which is the caller that would
// otherwise erase the thing it is standing on. So the drop is recorded and the
// worker performs the erase when it releases the lease.
//
// This is the shape that stalled a window for five seconds and then read freed
// memory. It is deterministic to reproduce with a couple of latches, so it is
// pinned here.
//
// A REJECTED SUBMIT is the other half. A stopped pool must refuse out loud — a
// resolved future carrying an exception — because the caller has by then raised
// its own "a read is out" state and would otherwise wait forever for a delivery
// nobody will make (the spinner that never stops, the list that refuses every
// later portion).
//
// AWAIT is the third. A script waiting for its client (a question on the web)
// waits on the thread that holds its session, and that thread goes on running
// the session's tasks meanwhile — otherwise the very request that shows the
// question to the client queues behind it and never comes. Pinned here: the
// order, the place a waiting worker does not take under the cap, the cancel
// that wins over the next task, the stop, and who may wait at all.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"   // ibBackendInterruptException — how a cancelled wait ends
#include "backend/session/session.h"
#include "backend/session/workerPoolHeadless.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

// A one-shot latch. Not a bare atomic + sleep: the whole point of these tests is
// to hit an exact interleaving, and a sleep either makes them slow or makes them
// lie.
class ibLatch {
public:
	void Signal() {
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_set = true;
		}
		m_cv.notify_all();
	}
	bool Wait(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
		std::unique_lock<std::mutex> lk(m_mtx);
		return m_cv.wait_for(lk, timeout, [this] { return m_set; });
	}
private:
	std::mutex              m_mtx;
	std::condition_variable m_cv;
	bool                    m_set = false;
};

// Sessions are held by shared_ptr because ibSessionScope — which the worker
// takes around every task — binds through weak_from_this.
std::shared_ptr<ibSession> MakeSession(const wxString& id) {
	return std::make_shared<ibSession>(id, ibSessionKind::Designer);
}

} // namespace

// ---------------------------------------------------------------------------
// Dropping a session whose queue a worker is standing on
// ---------------------------------------------------------------------------

TEST(WorkerPoolDrop, DropWhileLeasedDoesNotBlockTheDropper)
{
	ibWorkerPoolHeadless pool(2);
	auto session = MakeSession(wxT("drop-while-leased"));

	ibLatch started, release;
	std::future<void> task = pool.Submit(session.get(), [&started, &release]() {
		started.Signal();
		release.Wait();
	});

	ASSERT_TRUE(started.Wait()) << "the task never reached the worker";

	// The queue is leased right now. The dropper must record the drop and return
	// — erasing here would pull the queue out from under a running worker, and
	// waiting here would deadlock every teardown that runs from inside a task.
	const auto before = std::chrono::steady_clock::now();
	pool.Drop(session.get());
	const auto spent = std::chrono::steady_clock::now() - before;
	EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(spent).count(), 500)
		<< "Drop waited for the lease instead of deferring the erase";

	release.Signal();
	ASSERT_EQ(task.wait_for(std::chrono::seconds(5)), std::future_status::ready)
		<< "the in-flight task never completed after its session was dropped";
	EXPECT_NO_THROW(task.get());

	pool.Stop();
}

TEST(WorkerPoolDrop, ThePoolStaysUsableAfterADeferredErase)
{
	ibWorkerPoolHeadless pool(2);
	auto dropped = MakeSession(wxT("dropped"));
	auto other   = MakeSession(wxT("other"));

	ibLatch started, release;
	std::future<void> held = pool.Submit(dropped.get(), [&started, &release]() {
		started.Signal();
		release.Wait();
	});
	ASSERT_TRUE(started.Wait());

	pool.Drop(dropped.get());
	release.Signal();
	ASSERT_EQ(held.wait_for(std::chrono::seconds(5)), std::future_status::ready);

	// The erase happened on the worker, under the pool's own lock. Everything the
	// pool holds must still be consistent afterwards: another session runs...
	std::atomic<bool> ranOther { false };
	std::future<void> f1 = pool.Submit(other.get(), [&ranOther]() { ranOther.store(true); });
	ASSERT_EQ(f1.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_TRUE(ranOther.load());

	// ...and so does the dropped session, whose queue is simply created again. A
	// dropped session is not a banned one — the drop retires a queue, not an
	// identity.
	std::atomic<bool> ranAgain { false };
	std::future<void> f2 = pool.Submit(dropped.get(), [&ranAgain]() { ranAgain.store(true); });
	ASSERT_EQ(f2.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_TRUE(ranAgain.load());

	pool.Stop();
}

// Dropping something the pool never saw is a no-op, not a fault: a session that
// never submitted anything still tears itself down through the same path.
TEST(WorkerPoolDrop, DroppingAnUnknownSessionIsHarmless)
{
	ibWorkerPoolHeadless pool(1);
	auto stranger = MakeSession(wxT("never-submitted"));
	EXPECT_NO_THROW(pool.Drop(stranger.get()));
	EXPECT_NO_THROW(pool.Drop(nullptr));
	pool.Stop();
}

// ---------------------------------------------------------------------------
// A stopped pool refuses OUT LOUD
// ---------------------------------------------------------------------------

TEST(WorkerPoolDrop, SubmitAfterStopFailsTheFutureInsteadOfGoingQuiet)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("late-submit"));
	pool.Stop();

	std::atomic<bool> ran { false };
	std::future<void> f = pool.Submit(session.get(), [&ran]() { ran.store(true); });

	ASSERT_TRUE(f.valid()) << "a refusal still owes the caller a future to read it from";
	ASSERT_EQ(f.wait_for(std::chrono::seconds(0)), std::future_status::ready)
		<< "a refused submit must come back resolved — a caller that waits on it "
		   "waits forever, and that is a wedged form, not a slow one";
	EXPECT_THROW(f.get(), std::exception)
		<< "the refusal must be visible to whoever asked";
	EXPECT_FALSE(ran.load()) << "a stopped pool must not run the work";
}

// ---------------------------------------------------------------------------
// Await — a script waiting for its client, on the thread that holds its session
// ---------------------------------------------------------------------------

namespace {

// What happened, in the order it happened, from whichever thread.
class ibOrder {
public:
	void Note(const char* what) {
		std::lock_guard<std::mutex> lk(m_mtx);
		m_seen.push_back(what);
	}
	std::vector<std::string> Seen() {
		std::lock_guard<std::mutex> lk(m_mtx);
		return m_seen;
	}
private:
	std::mutex               m_mtx;
	std::vector<std::string> m_seen;
};

// A session whose cancel always stands: a job's. A host session's cancel is for a running script only
// (ibSession::Cancel), and these tasks are not scripts.
std::shared_ptr<ibSession> MakeJobSession(const wxString& id) {
	return std::make_shared<ibSession>(id, ibSessionKind::BackgroundJob);
}

} // namespace

TEST(WorkerPoolAwait, TheWaitingThreadRunsItsSessionsTasksInOrderUntilDone)
{
	ibWorkerPoolHeadless pool(2);
	auto session = MakeSession(wxT("await-runs"));

	ibOrder order;
	std::atomic<bool> answered { false };
	std::thread::id waiter, readOn;
	ibLatch waiting;
	std::future<void> script = pool.Submit(session.get(), [&]() {
		waiter = std::this_thread::get_id();
		order.Note("script");
		waiting.Signal();
		pool.Await(session.get(), [&answered]() { return answered.load(); });
		order.Note("script-out");
	});
	ASSERT_TRUE(waiting.Wait());

	// Two tasks of the same session arrive while the script waits: a read (the request that shows the question),
	// then the answer.
	std::future<void> read = pool.Submit(session.get(), [&]() {
		readOn = std::this_thread::get_id();
		order.Note("read");
	});
	std::future<void> answer = pool.Submit(session.get(), [&]() {
		order.Note("answer");
		answered.store(true);
	});

	ASSERT_EQ(script.wait_for(std::chrono::seconds(5)), std::future_status::ready)
		<< "the wait never ended — the tasks it waited through were not run";
	EXPECT_NO_THROW(script.get());
	EXPECT_EQ(readOn, waiter) << "the session's task ran on another thread — two threads in one session";
	EXPECT_EQ(order.Seen(), (std::vector<std::string>{ "script", "read", "answer", "script-out" }));
	pool.Stop();
}

TEST(WorkerPoolAwait, AWaitingWorkerHoldsNoPlaceUnderTheCap)
{
	ibWorkerPoolHeadless pool(1);
	auto asking = MakeSession(wxT("asking"));
	auto other  = MakeSession(wxT("other"));

	std::atomic<bool> answered { false };
	ibLatch waiting;
	std::future<void> script = pool.Submit(asking.get(), [&]() {
		waiting.Signal();
		pool.Await(asking.get(), [&answered]() { return answered.load(); });
	});
	ASSERT_TRUE(waiting.Wait());

	// A pool of ONE, and its one worker is inside a question. The question is a fiber, so that
	// worker is free to serve another session. The cap stays where it was — a waiting question
	// does not spend a thread, and it does not borrow one either.
	std::future<void> served = pool.Submit(other.get(), []() {});
	EXPECT_EQ(served.wait_for(std::chrono::seconds(5)), std::future_status::ready)
		<< "a question in one session stopped the pool for everybody";

	answered.store(true);
	pool.Wake(asking.get());
	ASSERT_EQ(script.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(script.get());
	pool.Stop();
}

TEST(WorkerPoolAwait, ACancelEndsTheWaitBeforeTheBarrierThatFollowsIt)
{
	ibWorkerPoolHeadless pool(2);
	auto session = MakeJobSession(wxT("await-cancelled"));

	ibOrder order;
	ibLatch waiting;
	std::future<void> script = pool.Submit(session.get(), [&]() {
		waiting.Signal();
		try {
			pool.Await(session.get(), []() { return false; });
		}
		catch (const ibBackendInterruptException&) {
			order.Note("interrupted");
			throw;
		}
	});
	ASSERT_TRUE(waiting.Wait());

	// A teardown's order: the cancel, then the barrier it waits on. The barrier must run after the script is
	// out — run inside the wait, it would tell the teardown "drained" with the script still on the stack.
	session->Cancel();
	pool.Wake(session.get());   // a session without a registry has no pool of its own to ring through Cancel
	std::future<void> barrier = pool.Submit(session.get(), [&order]() { order.Note("barrier"); });

	ASSERT_EQ(script.wait_for(std::chrono::seconds(5)), std::future_status::ready)
		<< "the cancel did not end the wait";
	EXPECT_THROW(script.get(), ibBackendInterruptException);
	ASSERT_EQ(barrier.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_EQ(order.Seen(), (std::vector<std::string>{ "interrupted", "barrier" }));
	pool.Stop();
}

TEST(WorkerPoolAwait, StoppingThePoolEndsAWaitWithTheInterruption)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("await-stopped"));

	ibLatch waiting;
	std::future<void> script = pool.Submit(session.get(), [&]() {
		waiting.Signal();
		pool.Await(session.get(), []() { return false; });
	});
	ASSERT_TRUE(waiting.Wait());

	pool.Stop();   // returns once every worker is out — a wait deaf to the stop would hang it here
	ASSERT_EQ(script.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_THROW(script.get(), ibBackendInterruptException);
}

// A thread that does not hold the session would wait for tasks nobody may run — refused, not hung.
TEST(WorkerPoolAwait, AThreadOffTheLeaseIsRefused)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("await-off-lease"));
	EXPECT_THROW(pool.Await(session.get(), []() { return true; }), std::logic_error);
	pool.Stop();
}

TEST(WorkerPoolAwait, ATaskOfAnotherSessionMayNotWaitForThisOne)
{
	ibWorkerPoolHeadless pool(1);
	auto mine   = MakeSession(wxT("mine"));
	auto theirs = MakeSession(wxT("theirs"));

	std::future<void> f = pool.Submit(mine.get(), [&]() {
		pool.Await(theirs.get(), []() { return true; });
	});
	ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_THROW(f.get(), std::logic_error) << "it would keep this session's worker on another session";
	pool.Stop();
}
