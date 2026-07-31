// =============================================================================
// Rented background runs — ibJobTenancy::Tenant (integration scope).
//
// One background-job concept on the outside, two behaviours inside. A STANDALONE
// run is somebody's own work: its own identity, its own runtime, a row in Active
// Users, and it outlives whoever started it. A TENANT is not: no parent means no
// existence. It is a read performed on the parent's behalf — no identity, no
// runtime, no row — and it creates a session for exactly one reason: a session
// owns a connection, the parent's is busy, and a connection is the one thing
// that cannot be borrowed.
//
// Three things about it are load-bearing for every list in the product, and all
// three are asserted here:
//
//   1. it BORROWS — the parent is reachable from the run, which is what lets
//      ibSession::GetAccessPolicy find the policy nobody threaded through;
//   2. it LEAVES NOTHING — no row in the registry, so scrolling a list does not
//      fill Active Users with a line per portion;
//   3. it GIVES THE CONNECTION BACK — the one that mattered in practice. A
//      tenant that leaks its pooled connection exhausts the pool after N
//      scrolls, and the window stops answering with no error anywhere.
//
// Brings up the appData env the way codeRunner does, plus a SQLite pool, and
// SKIPS (not fails) if the headless environment cannot come up. Its own target,
// so that bring-up cannot perturb the main suite.
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include <wx/init.h>                                    // wxInitializer — wxBase before appData

#include "backend/appData.h"
#include "backend/appDataCtorToken.h"                   // ib::AppDataCtorToken — OES_TESTING opens it
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/job/jobManager.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"

namespace {

// ONE connection in the pool, deliberately. A tenant takes its connection on the
// calling thread and gives it back when its session dies, so with a pool of one
// the very second run proves the first gave it back — and the leak that used to
// hide behind a 32-slot pool becomes a first-iteration failure instead of a
// thirty-scroll mystery.
constexpr std::size_t kPoolMax = 1;

struct JobTenancyFix : ::testing::Test {
	wxInitializer                          m_wxInit;   // constructed before SetUp
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	std::shared_ptr<ibSession>             parent;
	std::unique_ptr<ibSessionScope>        bound;      // makes `parent` the Current()
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		if (ibApplicationData::GetSessionRegistry() == nullptr)
			GTEST_SKIP() << "no session registry after CreateAppDataEnv";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, kPoolMax, /*minIdle=*/0);

		// The landlord. Held by shared_ptr because the tenant path takes a
		// shared_from_this on whatever is Current(), and bound for the same reason
		// a form's session is bound when it asks for a portion.
		parent = std::make_shared<ibSession>(wxString(wxT("tenancy-parent")),
		                                     ibSessionKind::Enterprise);
		bound  = std::make_unique<ibSessionScope>(parent.get());
		ready  = true;
	}

	void TearDown() override {
		bound.reset();
		parent.reset();
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}
};

} // namespace

// ---------------------------------------------------------------------------
// No parent, no tenant
// ---------------------------------------------------------------------------

TEST_F(JobTenancyFix, RentedRunRefusesWithoutTheSessionThatStartsIt)
{
	if (!ready) GTEST_SKIP();

	bound.reset();   // nothing is Current() any more
	ASSERT_EQ(ibSession::Current(), nullptr);

	ibJobManager manager(ib::AppDataCtorToken{});
	std::atomic<bool> ran { false };

	EXPECT_THROW(manager.StartBackground(
		[&ran](ibSession*) { ran.store(true); return ibValue(); },
		wxT("orphan read"), ibJobTenancy::Tenant), ibBackendException)
		<< "a run with nothing to rent from is not a tenant — it must refuse "
		   "rather than quietly run unrented, with no policy and nobody's rights";
	EXPECT_FALSE(ran.load());

	manager.Stop();
}

// ---------------------------------------------------------------------------
// What a tenant IS: the parent's session, one step away
// ---------------------------------------------------------------------------

TEST_F(JobTenancyFix, RentedRunActsForTheParentOnASessionOfItsOwn)
{
	if (!ready) GTEST_SKIP();

	ibJobManager manager(ib::AppDataCtorToken{});

	ibSession* seen        = nullptr;
	ibSession* seenCurrent = nullptr;
	ibSession* seenServer  = nullptr;

	auto run = manager.StartBackground(
		[&](ibSession* session) {
			seen        = session;
			seenCurrent = ibSession::Current();
			// The landlord, reached the way GetAccessPolicy reaches it.
			const std::shared_ptr<ibSession> server = session->Server();
			seenServer  = server.get();
			return ibValue();
		},
		wxT("rented read"), ibJobTenancy::Tenant);

	ASSERT_TRUE(run != nullptr);
	ASSERT_TRUE(run->Wait(10000)) << "the rented run never completed";
	EXPECT_TRUE(run->Error().IsEmpty()) << run->Error();

	ASSERT_NE(seen, nullptr);
	EXPECT_NE(seen, parent.get())
		<< "a tenant needs a session of its own — that is the one thing it cannot "
		   "borrow, because a session owns exactly one connection";
	EXPECT_EQ(seenCurrent, seen)
		<< "the work must run bound to the run's own session: the query layer reads "
		   "the connection and the access policy off Current()";
	EXPECT_EQ(seenServer, parent.get())
		<< "the parent must be reachable from the tenant — that link IS the "
		   "borrowed access policy";

	manager.Stop();
}

TEST_F(JobTenancyFix, RentedRunLeavesNoRowInTheRegistry)
{
	if (!ready) GTEST_SKIP();

	ibSessionRegistry* const registry = ibApplicationData::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);

	ibJobManager manager(ib::AppDataCtorToken{});

	wxString tenantId;
	auto run = manager.StartBackground(
		[&tenantId](ibSession* session) { tenantId = session->GetId(); return ibValue(); },
		wxT("unlisted read"), ibJobTenancy::Tenant);

	ASSERT_TRUE(run->Wait(10000));
	ASSERT_FALSE(tenantId.IsEmpty());

	EXPECT_FALSE(static_cast<bool>(registry->Find(tenantId)))
		<< "a rented run is UNLISTED on purpose: a row per scrolled portion is a "
		   "line an administrator cannot account for, and Add is not free either";

	manager.Stop();
}

// ---------------------------------------------------------------------------
// THE REGRESSION: the connection comes back
// ---------------------------------------------------------------------------

// Ran with a pool of one, so this fails on the SECOND iteration if a tenant ever
// stops returning its connection. That is the bug this pins: ibSession held its
// pooled connection through a holder that never gave it back, and roughly thirty
// scrolls later the window simply stopped answering — no error, no log, nothing
// to see. The fix is a self-returning holder, and nothing about it is visible
// from the outside except this.
TEST_F(JobTenancyFix, RentedRunsDoNotExhaustTheConnectionPool)
{
	if (!ready) GTEST_SKIP();

	ibConnectionPool* const pool = ibApplicationData::GetConnectionPool();
	ASSERT_NE(pool, nullptr);

	ibJobManager manager(ib::AppDataCtorToken{});

	constexpr int kRuns = 8;   // well past the pool's single slot
	for (int i = 0; i < kRuns; ++i) {
		std::atomic<bool> ran { false };
		std::shared_ptr<ibBackgroundRun> run;

		ASSERT_NO_THROW(
			run = manager.StartBackground(
				[&ran](ibSession*) { ran.store(true); return ibValue(); },
				wxString::Format(wxT("portion %d"), i), ibJobTenancy::Tenant))
			<< "run " << i << " could not take a connection — the previous tenant "
			   "never gave its own back";

		ASSERT_TRUE(run->Wait(10000)) << "run " << i << " never completed";
		EXPECT_TRUE(run->Error().IsEmpty()) << "run " << i << ": " << run->Error();
		EXPECT_TRUE(ran.load())            << "run " << i << " did not execute its body";

		// Drop the handle: the run owns its session, and the session is what gives
		// the connection back. Held handles are exactly how a form keeps a portion,
		// so this is the ordinary path, not a test-only teardown.
		run.reset();
	}

	EXPECT_LE(pool->LiveSize(), kPoolMax)
		<< "the pool grew past its cap — connections were not returned";

	manager.Stop();
}
