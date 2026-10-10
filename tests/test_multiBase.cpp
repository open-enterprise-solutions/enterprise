// =============================================================================
// Several bases in one process — ibApplicationHost (integration scope).
//
// One chain answers "which base does this thread work for" (ibApplicationInstance::
// Get, docs/private/multi-base-process.md § 3), and a process holding two bases
// is where it is tested:
//
//   1. a thread bound to a base gets THAT base — its pool, its data;
//   2. a thread with no session and no base is REFUSED — there is no global
//      current base, not even "the only one"; the hot path (ibSession::Current)
//      answers "no session" instead of throwing;
//   3. two threads, two bases, at once — neither sees the other's table;
//   4. the thread that OPENED a base works for it — every host of today opens its
//      base and goes on to log in from the same thread.
//
// Each base is opened the way codeRunner opens one, plus its own in-memory SQLite
// pool, and the fixture SKIPS (not fails) if the headless environment cannot come
// up. Its own target, like oes_job_tenancy_test: an appData bring-up must not
// perturb the main suite.
// =============================================================================

#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include <wx/filename.h>                                // a file the pool's clones all open
#include <wx/init.h>                                    // wxInitializer — wxBase before appData

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"

namespace {

// A base with no database, then a database of its own — an in-memory SQLite in its own pool, set up while
// this thread works for it. Null when the environment cannot come up.
// poolSize 1 is the whole base: nothing else borrows a connection. A session
// registry keeps one, so a login beside it needs a wider pool, and every
// connection has to be the same database — a clone of :memory: is empty.
ibApplicationInstance* OpenBase(ibRunMode mode = ibRunMode::eFILE_MODE,
                                std::size_t poolSize = 1,
                                const wxString& database = wxT(":memory:"))
{
	if (!ibApplicationInstance::CreateAppDataEnv(mode))
		return nullptr;
	ibApplicationInstance* const applicationInstance = ibApplicationHost::GetInstances().back();

	ibApplicationInstanceScope working(applicationInstance);
	ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool();
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	if (pool == nullptr || !db->Open(database))
		return nullptr;
	pool->Init(db, poolSize, /*minIdle=*/0);
	return applicationInstance;
}

// Does the table exist in the base this thread works for?
bool HasTable(const wxString& table)
{
	return db_query != nullptr && db_query->TableExists(table);
}

struct MultiBaseFix : ::testing::Test {
	wxInitializer      m_wxInit;   // constructed before SetUp
	ibApplicationInstance* first  = nullptr;
	ibApplicationInstance* second = nullptr;
	bool               ready  = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		first  = OpenBase();
		second = OpenBase();
		if (first == nullptr || second == nullptr)
			GTEST_SKIP() << "appData env unavailable headless";
		ready = true;
	}

	void TearDown() override {
		ibApplicationInstance::DestroyAppDataEnv();
	}
};

struct OneBaseFix : ::testing::Test {
	wxInitializer      m_wxInit;
	ibApplicationInstance* only  = nullptr;
	bool               ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		only = OpenBase();
		if (only == nullptr)
			GTEST_SKIP() << "appData env unavailable headless";
		ready = true;
	}

	void TearDown() override {
		ibApplicationInstance::DestroyAppDataEnv();
	}
};

} // namespace

TEST_F(MultiBaseFix, OpeningAddsABaseAndDoesNotReplaceTheOneThereWas)
{
	if (!ready) GTEST_SKIP();

	ASSERT_EQ(ibApplicationHost::GetInstances().size(), 2u)
		<< "opening used to destroy the base there was; a process of several must keep both";
	EXPECT_NE(first, second);
}

TEST_F(MultiBaseFix, ABoundThreadGetsItsOwnBase)
{
	if (!ready) GTEST_SKIP();

	ibConnectionPool* poolOfFirst  = nullptr;
	ibConnectionPool* poolOfSecond = nullptr;
	{
		ibApplicationInstanceScope working(first);
		EXPECT_EQ(ibApplicationInstance::Get(), first);
		poolOfFirst = ibApplicationInstance::GetConnectionPool();
	}
	{
		ibApplicationInstanceScope working(second);
		EXPECT_EQ(ibApplicationInstance::Get(), second);
		poolOfSecond = ibApplicationInstance::GetConnectionPool();
	}
	ASSERT_NE(poolOfFirst, nullptr);
	EXPECT_NE(poolOfFirst, poolOfSecond) << "each base owns its pool — the accessor must answer the bound one";
}

TEST_F(MultiBaseFix, AThreadWithNoSessionAndNoBaseIsRefused)
{
	if (!ready) GTEST_SKIP();

	// A FRESH thread — this one opened the bases and is bound to the last of them.
	bool refused = false, refusedDoor = false, noSession = false;
	std::thread fresh([&] {
		try { (void)ibApplicationInstance::Get(); } catch (const ibBackendException&) { refused = true; }
		try { (void)db_query; } catch (const ibBackendException&) { refusedDoor = true; }
		// …while the hot path that runs from error handlers and teardown listeners answers without throwing.
		try { noSession = ibSession::Current() == nullptr; } catch (...) {}
	});
	fresh.join();

	EXPECT_TRUE(refused) << "with nothing bound there is no right answer — it must be said, not guessed";
	EXPECT_TRUE(refusedDoor);
	EXPECT_TRUE(noSession) << "ibSession::Current answers \"no session\" and does not throw";
}

TEST_F(MultiBaseFix, EachBaseKeepsItsOwnData)
{
	if (!ready) GTEST_SKIP();

	{
		ibApplicationInstanceScope working(first);
		ASSERT_NE(db_query, nullptr);
		db_query->RunStatement(wxT("CREATE TABLE only_in_first (x INTEGER)"));
		EXPECT_TRUE(HasTable(wxT("only_in_first")));
	}
	{
		ibApplicationInstanceScope working(second);
		EXPECT_FALSE(HasTable(wxT("only_in_first"))) << "the second base must not see the first one's table";
	}
}

TEST_F(MultiBaseFix, TwoThreadsTwoBasesAtOnce)
{
	if (!ready) GTEST_SKIP();

	ibApplicationInstance* seenByA = nullptr;
	ibApplicationInstance* seenByB = nullptr;
	bool aSeesB = true, bSeesA = true;

	std::thread a([&] {
		ibApplicationInstanceScope working(first);
		db_query->RunStatement(wxT("CREATE TABLE made_by_a (x INTEGER)"));
		seenByA = ibApplicationInstance::Get();
		aSeesB  = HasTable(wxT("made_by_b"));
	});
	std::thread b([&] {
		ibApplicationInstanceScope working(second);
		db_query->RunStatement(wxT("CREATE TABLE made_by_b (x INTEGER)"));
		seenByB = ibApplicationInstance::Get();
		bSeesA  = HasTable(wxT("made_by_a"));
	});
	a.join();
	b.join();

	EXPECT_EQ(seenByA, first);
	EXPECT_EQ(seenByB, second);
	EXPECT_FALSE(aSeesB);
	EXPECT_FALSE(bSeesA);
}

TEST_F(OneBaseFix, TheThreadThatOpenedTheBaseWorksForIt)
{
	if (!ready) GTEST_SKIP();

	EXPECT_EQ(ibApplicationInstance::Get(), only) << "every host of today opens its base and logs in from that thread";
	EXPECT_NE(db_query, nullptr);
}

TEST_F(OneBaseFix, AFreshThreadIsRefusedEvenWithOneBase)
{
	if (!ready) GTEST_SKIP();

	bool refused = false;
	std::thread fresh([&] {
		try { (void)ibApplicationInstance::Get(); } catch (const ibBackendException&) { refused = true; }
	});
	fresh.join();
	EXPECT_TRUE(refused) << "there is no global current base — not even \"the only one\"";
}

// An empty sys_user that was actually read is open access, including the
// application server's own login and the web server's own session. A table
// that cannot be read is not empty, and the login is refused.
TEST_F(OneBaseFix, AnEmptyUserListOpensAndAnUnreadableOneRefuses)
{
	if (!ready) GTEST_SKIP();

	// ListAll reads guid, name and fullName. A table of only name is not an
	// empty list: that read fails, and a failed read is the refusal below.
	const wxString emptyUserList = wxT(
		"CREATE TABLE sys_user (guid TEXT, name TEXT, fullName TEXT)");

	{
		ibApplicationInstanceScope working(only);
		ASSERT_NE(db_query, nullptr);
		ASSERT_NE(db_query->RunQuery(emptyUserList), -1);
		ibUserInfo info;
		EXPECT_TRUE(only->AuthenticateUser(wxEmptyString, wxEmptyString, info));
		EXPECT_TRUE(ibUserInfo::ListAll().empty());

		ASSERT_NE(db_query->RunQuery(wxT("DROP TABLE sys_user")), -1);
		EXPECT_FALSE(only->AuthenticateUser(wxEmptyString, wxEmptyString, info));
		EXPECT_THROW(ibUserInfo::ListAll(), ibCoreException);
	}

	ibApplicationInstance* const server = OpenBase(ibRunMode::eSERVER_MODE);
	if (server == nullptr)
		GTEST_SKIP() << "server env unavailable headless";
	{
		ibApplicationInstanceScope working(server);
		ASSERT_TRUE(server->ServiceMode());
		ASSERT_NE(db_query->RunQuery(emptyUserList), -1);
		ibUserInfo info;
		EXPECT_TRUE(server->AuthenticateUser(wxEmptyString, wxEmptyString, info));
	}

	// The registry started by the web server's own session holds a connection
	// for sys_session. On the one-connection :memory: base above, the login's
	// read then waits until the pool says it is exhausted and the door
	// refuses. A file and a wider pool let that read see the empty list.
	const wxString path = wxFileName::CreateTempFileName(wxT("oes-users"));
	ASSERT_FALSE(path.IsEmpty());
	ibApplicationInstance* const webBase = OpenBase(ibRunMode::eFILE_MODE, /*poolSize=*/4, path);
	ASSERT_NE(webBase, nullptr);
	{
		ibApplicationInstanceScope working(webBase);
		ASSERT_NE(db_query->RunQuery(emptyUserList), -1);
		// The registry writes and re-reads this row on its own connection.
		ASSERT_NE(db_query->RunQuery(wxT(
			"CREATE TABLE sys_session ("
			"session TEXT PRIMARY KEY, userName TEXT, application INTEGER, "
			"started TEXT, lastActive TEXT, computer TEXT)")), -1);
		ibSessionHolder web = webBase->CreateSession(ibSessionKind::WebServer);
		ASSERT_NE(web.Get(), nullptr);
		ibSessionScope scope(web.Get());
		EXPECT_TRUE(webBase->WebEnterpriseMode());
		ibUserInfo info;
		EXPECT_TRUE(webBase->AuthenticateUser(wxEmptyString, wxEmptyString, info));
		EXPECT_TRUE(ibUserInfo::ListAll().empty());
	}
	wxRemoveFile(path);
}
