// A fiber stopped at a breakpoint, and the next session, share an OS thread.
// A role handler may not ask the user. A scope that is open on the parked
// fiber has to come back as it was, and a scope that cannot be open at a
// park has to be refused before the switch.

#include <gtest/gtest.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/query/queryLowering.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryable.h"   // ibSourceMetaDataScope — a pointer scope a query holds
#include "backend/restructureInfo.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/workerPoolHeadless.h"
#include "backend/system/value/valueTable.h"
#include "core/exception.h"
#include "core/fiber/fiberLocals.h"
#include "core/guid.h"

#include <wx/init.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
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

// A flag the waiter sets and an inline task must not see. Registered
// before any fiber snapshot, the same way the engine's own slots are.
thread_local bool g_waiterFlag = false;

struct ibRegisterWaiterFlag {
	ibRegisterWaiterFlag()
	{
		ibFiberLocals::RegisterTrivial<bool>(
			[](void* dst) { *static_cast<bool*>(dst) = g_waiterFlag; },
			[](const void* src) { g_waiterFlag = *static_cast<const bool*>(src); });
	}
} s_registerWaiterFlag;

bool WaitUntil(const std::function<bool()>& pred, std::chrono::milliseconds budget = std::chrono::seconds(5))
{
	const auto deadline = std::chrono::steady_clock::now() + budget;
	while (std::chrono::steady_clock::now() < deadline) {
		if (pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return pred();
}

ibValue TableWith(const wxString& column)
{
	ibValueModelTable* const table = new ibValueModelTable();
	const ibValue held(table);
	table->GetColumnCollection()->AddColumn(column, ibTypeDescription(), column);
	return held;
}

// True when CheckNames accepts the column on `&Goods`. The reason is kept
// so a failure that is not "unknown attribute" is visible.
bool ColumnResolves(const wxString& column, const ibValue& table, wxString* why)
{
	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT ") + column + wxT(" FROM &Goods"));
	std::map<wxString, ibValue> params;
	params.emplace(wxT("Goods"), table);
	try {
		ibQueryLowering::CheckNames(package, params);
		return true;
	}
	catch (const ibBackendException& err) {
		if (why != nullptr)
			*why = err.GetErrorDescription();
		return false;
	}
}

// ibSession::Current() answers "no session" while the process holds no
// base, so the exclusive-mode test opens one.
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

} // namespace

// The reference-read stack and the bound-table wrap are private. These
// drive them from the test; they are exported from the backend.
std::size_t ibRefReadDepthForTest();
void ibRefReadPushForTest(const ibMetaID& id, const ibGuid& guid);
void ibRefReadPopForTest();
bool ibRefReadContainsForTest(const ibMetaID& id, const ibGuid& guid);
const void* ibBoundWrapForTest(const wxString& name);

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
		EXPECT_THROW(pool.Await(session.get(), [] { return false; }), ibCoreException);
	});
	ASSERT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(ran.get());
	pool.Stop();
}

TEST(FiberTls, TwoFibersBindTheSameNameToDifferentTables)
{
	wxInitializer wx;
	ibWorkerPoolHeadless pool(1);
	auto first = MakeSession(wxT("tls-table-a"));
	auto second = MakeSession(wxT("tls-table-b"));
	const ibValue alpha = TableWith(wxT("Alpha"));
	const ibValue beta = TableWith(wxT("Beta"));

	ibLatch firstParked;
	ibLatch secondDone;
	std::atomic<bool> releaseFirst{ false };
	std::atomic<bool> firstKeptAlpha{ false };
	std::atomic<const void*> firstWrap{ nullptr };
	std::atomic<const void*> firstWrapAfter{ nullptr };
	std::atomic<const void*> secondWrap{ nullptr };
	wxString firstWhy;

	std::future<void> fa = pool.Submit(first.get(), [&] {
		wxString why;
		EXPECT_TRUE(ColumnResolves(wxT("Alpha"), alpha, &why)) << why;
		firstWrap.store(ibBoundWrapForTest(wxT("Goods")));
		pool.Await(first.get(), [&] {
			firstParked.Signal();
			return releaseFirst.load();
		});
		// The wrap from before the park, not a new one built because the
		// other fiber replaced the map entry.
		firstWrapAfter.store(ibBoundWrapForTest(wxT("Goods")));
		firstKeptAlpha.store(ColumnResolves(wxT("Alpha"), alpha, &firstWhy));
	});
	ASSERT_TRUE(firstParked.Wait());
	ASSERT_NE(firstWrap.load(), nullptr);

	std::future<void> fb = pool.Submit(second.get(), [&] {
		wxString why;
		EXPECT_TRUE(ColumnResolves(wxT("Beta"), beta, &why)) << why;
		secondWrap.store(ibBoundWrapForTest(wxT("Goods")));
		secondDone.Signal();
	});
	ASSERT_TRUE(secondDone.Wait());
	EXPECT_NE(secondWrap.load(), nullptr);
	EXPECT_NE(secondWrap.load(), firstWrap.load());

	releaseFirst.store(true);
	pool.Wake(first.get());
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	EXPECT_EQ(firstWrapAfter.load(), firstWrap.load());
	EXPECT_TRUE(firstKeptAlpha.load()) << firstWhy;
	EXPECT_NO_THROW(fb.get());
	pool.Stop();
}

TEST(FiberTls, AnInlineTaskSeesTheSchedulerNotTheWaiter)
{
	ibWorkerPoolHeadless pool(1);
	auto session = MakeSession(wxT("tls-inline"));
	ibLatch inside;
	std::atomic<bool> release{ false };
	std::atomic<bool> taskSawFlag{ true };
	std::atomic<bool> taskRan{ false };

	std::future<void> ran = pool.Submit(session.get(), [&] {
		g_waiterFlag = true;
		pool.Await(session.get(), [&] {
			inside.Signal();
			return release.load();
		});
		EXPECT_TRUE(g_waiterFlag);
	});
	ASSERT_TRUE(inside.Wait());

	// Queued from this thread, so it is not the reentrant inline path.
	// Await runs it on the waiter's stack, under the scheduler's locals.
	std::future<void> inner = pool.Submit(session.get(), [&] {
		taskSawFlag.store(g_waiterFlag);
		g_waiterFlag = false;
		taskRan.store(true);
	});
	ASSERT_TRUE(WaitUntil([&] { return taskRan.load(); }));
	EXPECT_FALSE(taskSawFlag.load());

	release.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(ran.get());
	EXPECT_NO_THROW(inner.get());
	pool.Stop();
}

TEST(FiberTls, AReferenceReadStaysWithTheFiberThatPushedIt)
{
	ibWorkerPoolHeadless pool(1);
	auto first = MakeSession(wxT("tls-ref-a"));
	auto second = MakeSession(wxT("tls-ref-b"));
	const ibGuid mine(ibGuid::newGuid());
	const ibGuid theirs(ibGuid::newGuid());
	const ibMetaID id = 7;

	ibLatch firstParked;
	ibLatch secondDone;
	std::atomic<bool> releaseFirst{ false };
	std::atomic<bool> secondSawMine{ true };
	std::atomic<std::size_t> secondDepth{ 99 };
	std::atomic<bool> firstKeptMine{ false };
	std::atomic<bool> firstSawTheirs{ true };
	std::atomic<std::size_t> firstDepth{ 0 };

	std::future<void> fa = pool.Submit(first.get(), [&] {
		ibRefReadPushForTest(id, mine);
		pool.Await(first.get(), [&] {
			firstParked.Signal();
			return releaseFirst.load();
		});
		firstDepth.store(ibRefReadDepthForTest());
		firstKeptMine.store(ibRefReadContainsForTest(id, mine));
		firstSawTheirs.store(ibRefReadContainsForTest(id, theirs));
		ibRefReadPopForTest();
	});
	ASSERT_TRUE(firstParked.Wait());

	std::future<void> fb = pool.Submit(second.get(), [&] {
		secondDepth.store(ibRefReadDepthForTest());
		secondSawMine.store(ibRefReadContainsForTest(id, mine));
		ibRefReadPushForTest(id, theirs);
		EXPECT_TRUE(ibRefReadContainsForTest(id, theirs));
		ibRefReadPopForTest();
		secondDone.Signal();
	});
	ASSERT_TRUE(secondDone.Wait());
	EXPECT_EQ(secondDepth.load(), 0u);
	EXPECT_FALSE(secondSawMine.load());

	releaseFirst.store(true);
	pool.Wake(first.get());
	ASSERT_EQ(fa.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(fa.get());
	EXPECT_EQ(firstDepth.load(), 1u);
	EXPECT_TRUE(firstKeptMine.load());
	EXPECT_FALSE(firstSawTheirs.load());
	EXPECT_NO_THROW(fb.get());
	pool.Stop();
}

TEST(FiberTls, AnInlineTaskDoesNotClearTheExclusiveFlag)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	registry->Start();
	ASSERT_FALSE(appData->ExclusiveMode());

	// A session the registry owns, so the worker resolves the base through
	// it. A bare session has no registry, and the scheduler snapshot the
	// inline task runs under does not carry this thread's base binding.
	ibSessionHolder holder = registry->CreateSessionOfKind(
		ibRunMode::eFILE_MODE, wxT("fiber-tls"), ibSessionKind::Designer, {});
	ASSERT_TRUE(static_cast<bool>(holder));
	ibSession* const session = holder.Get();

	ibWorkerPoolHeadless pool(1);
	ibLatch inside;
	std::atomic<bool> release{ false };
	std::atomic<bool> taskRan{ false };
	std::atomic<bool> stillExclusive{ false };

	std::future<void> ran = pool.Submit(session, [&] {
		ibRestructureInfo::RequireExclusiveForDDL();
		ASSERT_TRUE(appData->ExclusiveMode());
		pool.Await(session, [&] {
			inside.Signal();
			return release.load();
		});
		// The inline task cleared the thread flag on the way in. The
		// waiter's own flag has to be back, or this release is a no-op
		// and exclusive mode stays held.
		ibRestructureInfo::ReleaseAutoExclusive();
		stillExclusive.store(appData->ExclusiveMode());
	});
	ASSERT_TRUE(inside.Wait());

	std::future<void> inner = pool.Submit(session, [&] {
		ibRestructureInfo::RequireExclusiveForDDL();
		taskRan.store(true);
	});
	ASSERT_TRUE(WaitUntil([&] { return taskRan.load(); }));

	release.store(true);
	pool.Wake(session);
	ASSERT_EQ(ran.wait_for(std::chrono::seconds(10)), std::future_status::ready);
	EXPECT_NO_THROW(ran.get());
	EXPECT_NO_THROW(inner.get());
	EXPECT_TRUE(taskRan.load());
	EXPECT_FALSE(stillExclusive.load());
	pool.Stop();
}
