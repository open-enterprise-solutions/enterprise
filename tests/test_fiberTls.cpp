// A parked question and the next session share an OS thread. A scope that
// is open on the parked fiber has to come back as it was, and a scope that
// cannot be open at a question has to be refused before the switch.

#include <gtest/gtest.h>

#include "backend/query/queryable.h"   // ibSourceMetaDataScope — a pointer scope a query holds
#include "backend/session/session.h"
#include "backend/session/workerPoolHeadless.h"
#include "core/fiber/fiberLocals.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>

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

// A scope the park must refuse. Static init, before any fiber snapshot.
std::atomic<bool> g_forbidPark{ false };

struct ibRegisterForbidPark {
	ibRegisterForbidPark()
	{
		ibFiberLocals::RegisterMustBeClear(
			[]() { return !g_forbidPark.load(); },
			"a test scope");
	}
} s_registerForbidPark;

struct ClearForbid {
	~ClearForbid() { g_forbidPark.store(false); }
};

} // namespace

TEST(FiberTls, AQueryScopeDoesNotLeakIntoTheNextFiber)
{
	ibWorkerPoolHeadless pool(1);
	auto first = MakeSession(wxT("tls-scope-a"));
	auto second = MakeSession(wxT("tls-scope-b"));

	// The scope stores the pointer and never dereferences it.
	static const int kSentinel = 0;
	const ibMetaData* const mine = reinterpret_cast<const ibMetaData*>(&kSentinel);

	ibLatch firstParked;
	ibLatch secondSaw;
	std::atomic<bool> releaseFirst{ false };
	std::atomic<const ibMetaData*> secondObserved{ reinterpret_cast<const ibMetaData*>(1) };
	std::atomic<const ibMetaData*> firstObserved{ nullptr };

	std::future<void> fa = pool.Submit(first.get(), [&] {
		ibSourceMetaDataScope scope(mine);
		pool.Await(first.get(), [&] {
			firstParked.Signal();
			return releaseFirst.load();
		});
		firstObserved.store(ibSourceMetaDataScope::Get());
	});
	ASSERT_TRUE(firstParked.Wait());

	std::future<void> fb = pool.Submit(second.get(), [&] {
		secondObserved.store(ibSourceMetaDataScope::Get());
		secondSaw.Signal();
	});
	ASSERT_TRUE(secondSaw.Wait());
	EXPECT_EQ(secondObserved.load(), nullptr);

	releaseFirst.store(true);
	pool.Wake(first.get());
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	EXPECT_EQ(firstObserved.load(), mine);
	EXPECT_NO_THROW(fb.get());
	pool.Stop();
}

TEST(FiberTls, AScopeThatMustBeClearRefusesThePark)
{
	ClearForbid clear;
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("tls-forbid"));
	g_forbidPark.store(true);

	std::future<void> ran = pool.Submit(session.get(), [&] {
		EXPECT_THROW(pool.Await(session.get(), [] { return false; }), std::logic_error);
	});
	ASSERT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(ran.get());
	pool.Stop();
}
