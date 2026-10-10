// The pool finds a session's queue by the session's address, and a fiber
// parked on a question keeps that address on its stack. The session is not
// the pool's. Its holder can let go while the question is still open —
// teardown's barrier runs under Await, and then the last shared_ptr the
// holder had is dropped. The lease holds one of its own until the fiber
// has unwound, so the address stays live for that whole stack.
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

TEST(FiberLifetime, LastHolderDroppedWhileParkedDoesNotFreeTheSession)
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

	session.reset();
	EXPECT_FALSE(died.load())
		<< "the session was freed while its fiber was still parked";

	answered.store(true);
	pool.Wake(raw);
	ASSERT_EQ(waiting.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(waiting.get());
	EXPECT_TRUE(WaitUntil(died))
		<< "the fiber unwound and the session was still alive";

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
