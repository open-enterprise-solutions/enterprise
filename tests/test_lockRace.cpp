// Two connections acquiring the same sys_lock key.
//
// A SELECT ... FOR UPDATE of a key that has no row locks nothing, so two
// connections used to both insert an Exclusive row. The header row
// (sys_lock_key) is the mutex: one transaction holds it, the other waits
// or, with noWait, is refused at once. The header stays after release.
// The sweep deletes it once nobody holds the key.
//
// PostgreSQL is compiled into oes_pg_dialect_test (OES_LOCK_RACE_POSTGRES_ONLY)
// and skips without OES_PG_USER. Firebird and SQLite are compiled into
// oes_tests. Firebird skips when the client cannot create a file database.
// SQLite runs the protocol and the guid fix on a file database. It does not
// run noWait or the raced rounds: the driver ignores noWait, and a busy
// file is a timeout, so five immediate retries can give up before the peer
// commits.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/lock/lockHolder.h"
#include "backend/lock/lockManager.h"

#ifdef OES_LOCK_RACE_POSTGRES_ONLY
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#else
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#endif

struct LockSchemaAccess {
	static void Create() { ibApplicationInstance::CreateTableLock(); }
};

namespace {

wxString EnvOr(const char* name, const wxString& fallback)
{
	const char* value = std::getenv(name);
	return (value != nullptr && *value != '\0') ? wxString::FromUTF8(value) : fallback;
}

bool EnsureWx()
{
	static wxInitializer wx;
	return wx.IsOk();
}

ibLockItem Item(const wxString& key, ibLockMode mode)
{
	return ibLockItem::ForRef(wxT("LockRace.Key"), ibGuid(key), mode);
}

int CountRows(const wxString& table, const wxString& ns, const wxString& hash)
{
	ibDatabaseQueryBuilder q;
	// A real column, not a constant: Firebird refuses a parameter whose type
	// it cannot see ("Data type unknown" on SELECT ?).
	ibQueryIR ir(ibProject(
		ibFilter(ibScan(table),
			ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("namespace")), ibConst(ibValue(ns))),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("keyHash")),   ibConst(ibValue(hash))))),
		{ { ibCol(wxT("namespace")), wxT("n") } }));
	ibQueryResult rs = q.ExecuteIR(ir);
	int n = 0;
	while (rs.Next())
		++n;
	return n;
}

struct HolderRow {
	int      count = 0;
	wxString guid;
	int      mode  = -1;
};

HolderRow ReadHolder(const wxString& ns, const wxString& hash)
{
	ibDatabaseQueryBuilder q;
	ibQueryIR ir(ibProject(
		ibFilter(ibScan(lock_table),
			ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("namespace")), ibConst(ibValue(ns))),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("keyHash")),   ibConst(ibValue(hash))))),
		{ { ibCol(wxT("lockGuid")), wxEmptyString },
		  { ibCol(wxT("lockMode")), wxEmptyString } }));
	ibQueryResult rs = q.ExecuteIR(ir);
	HolderRow row;
	while (rs.Next()) {
		++row.count;
		row.guid = rs.GetResultString(wxT("lockGuid"));
		row.mode = rs.GetResultInt(wxT("lockMode"));
	}
	return row;
}

enum class OutcomeKind { Granted, Conflict, Other };

struct Outcome {
	OutcomeKind  kind = OutcomeKind::Other;
	wxString     error;
	ibLockHandle handle;
};

// The worker has no session. Binding the application is what lets Acquire
// borrow a connection, and what lets the handle release on this thread.
Outcome TryAcquire(ibApplicationInstance* app, ibLockManager* manager, ibLockHolder* holder,
                   const ibLockItem& item, const ibLockOptions& opts)
{
	ibApplicationInstanceScope bound(app);
	Outcome out;
	try {
		out.handle = manager->Acquire({ item }, opts, holder);
		out.kind = OutcomeKind::Granted;
	}
	catch (const ibBackendLockException&) {
		out.kind = OutcomeKind::Conflict;
	}
	catch (const ibCoreException& err) {
		out.kind = OutcomeKind::Other;
		out.error = err.GetErrorDescription();
	}
	catch (...) {
		out.kind = OutcomeKind::Other;
		out.error = wxT("acquire failed");
	}
	return out;
}

// Both calls run together. Separate managers: one manager's mutex would
// hide the race the header exists to close.
struct PairResult {
	Outcome first;
	Outcome second;
};

PairResult Race(ibApplicationInstance* app,
                ibLockManager* a, ibLockHolder* holderA, const ibLockItem& itemA, const ibLockOptions& optsA,
                ibLockManager* b, ibLockHolder* holderB, const ibLockItem& itemB, const ibLockOptions& optsB)
{
	std::atomic<int> arrived{ 0 };
	std::atomic<bool> go{ false };
	PairResult result;

	auto run = [&](ibLockManager* manager, ibLockHolder* holder, const ibLockItem& item,
	               const ibLockOptions& opts, Outcome* slot) {
		arrived.fetch_add(1);
		while (!go.load())
			std::this_thread::yield();
		*slot = TryAcquire(app, manager, holder, item, opts);
	};

	std::thread ta(run, a, holderA, itemA, optsA, &result.first);
	std::thread tb(run, b, holderB, itemB, optsB, &result.second);
	while (arrived.load() < 2)
		std::this_thread::yield();
	go.store(true);
	ta.join();
	tb.join();
	return result;
}

// One static set per driver. The suites run in one process and must not
// share the application or the database.
template <typename Driver>
class LockRace : public ::testing::Test {
protected:
	using DriverType = Driver;

	static ibApplicationInstance* s_app;
	static std::shared_ptr<ibDatabaseLayer> s_db;
	static std::unique_ptr<ibLockManager> s_peer;

	static void SetUpTestSuite()
	{
		if (!EnsureWx())
			return;
		try {
			if (!Driver::Open(s_db))
				return;
		}
		catch (...) {
			return;   // no client, or the file database would not come up — the test skips
		}
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			return;
		s_app = ibApplicationInstance::Get();
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (s_app == nullptr || pool == nullptr) {
			if (ibApplicationInstance::Get() != nullptr)
				ibApplicationInstance::DestroyAppDataEnv();
			s_app = nullptr;
			return;
		}
		pool->Init(s_db, /*maxSize=*/4, /*minIdle=*/0);
		LockSchemaAccess::Create();
		s_peer = std::make_unique<ibLockManager>(ib::AppDataCtorToken{ s_app });
	}

	static void TearDownTestSuite()
	{
		s_peer.reset();
		if (s_app != nullptr && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		s_app = nullptr;
		if (s_db) {
			s_db->Close();
			s_db.reset();
		}
		Driver::Close();
	}

	void SetUp() override
	{
		if (s_app == nullptr || s_peer == nullptr)
			GTEST_SKIP() << "lock database did not come up";
	}

	ibApplicationInstance* App() const { return s_app; }
	ibLockManager* Mine() const { return ibApplicationInstance::GetLockManager(); }
	ibLockManager* Peer() const { return s_peer.get(); }

	void RecreateHeaderTable()
	{
		ibDatabaseQueryBuilder q;
		if (q.TableExists(lock_key_table))
			q.Execute(ibDropTable(lock_key_table));
		EXPECT_FALSE(q.TableExists(lock_key_table));
		LockSchemaAccess::Create();
		EXPECT_TRUE(q.TableExists(lock_key_table));
		LockSchemaAccess::Create();
		EXPECT_TRUE(q.TableExists(lock_key_table));
	}

	void ExpectOneExclusive(int round)
	{
		const ibLockItem item = Item(
			wxString::Format(wxT("eeeeeeee-eeee-eeee-eeee-%012d"), round), ibLockMode::Exclusive);
		ibSingleLockHolder left(wxT("left"));
		ibSingleLockHolder right(wxT("right"));
		const PairResult race = Race(App(), Mine(), &left, item, {}, Peer(), &right, item, {});
		const int granted = (race.first.kind == OutcomeKind::Granted ? 1 : 0)
			+ (race.second.kind == OutcomeKind::Granted ? 1 : 0);
		EXPECT_EQ(granted, 1) << race.first.error << " / " << race.second.error;
		EXPECT_TRUE(race.first.kind == OutcomeKind::Conflict || race.second.kind == OutcomeKind::Conflict)
			<< race.first.error << " / " << race.second.error;
	}

	bool SeedHeader(const ibLockItem& item)
	{
		ibSingleLockHolder seed(wxT("seed"));
		ibLockHandle held = Mine()->Acquire({ item }, {}, &seed);
		if (!held.IsValid())
			return false;
		held.Release();
		return CountRows(lock_key_table, item.namespaceName, item.KeyHash()) == 1
			&& CountRows(lock_table, item.namespaceName, item.KeyHash()) == 0;
	}

	// The header already exists and is committed. This transaction locks it
	// and inserts a holder the waiter must not see until Commit.
	void GiveUp(ibDatabaseLayer* raw)
	{
		if (raw == nullptr || !raw->IsActiveTransaction())
			return;
		try { raw->RollBack(); }
		catch (...) {}
	}

	bool Plant(ibDatabaseLayer* raw, const ibLockItem& item, ibLockMode mode)
	{
		try {
			raw->BeginTransaction();
			{
				ibStatementGuard upd(raw, raw->PrepareStatement(
					wxT("UPDATE sys_lock_key SET namespace = ? WHERE namespace = ? AND keyHash = ?")));
				if (!upd) {
					GiveUp(raw);
					return false;
				}
				upd->SetParamString(1, item.namespaceName);
				upd->SetParamString(2, item.namespaceName);
				upd->SetParamString(3, item.KeyHash());
				if (upd->RunQuery() < 1) {
					GiveUp(raw);
					return false;
				}
			}
			{
				ibStatementGuard ins(raw, raw->PrepareStatement(
					wxT("INSERT INTO sys_lock (lockGuid, sessionGuid, namespace, keyHash, keyData, lockMode, acquiredAt, userName, computer) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")));
				if (!ins) {
					GiveUp(raw);
					return false;
				}
				const ibGuid owner = wxNewUniqueGuid;
				ins->SetParamString(1, owner.str());
				ins->SetParamString(2, owner.str());
				ins->SetParamString(3, item.namespaceName);
				ins->SetParamString(4, item.KeyHash());
				ins->SetParamString(5, item.KeyData());
				ins->SetParamInt(6, static_cast<int>(mode));
				ins->SetParamDate(7, ibDateTime::Now());
				ins->SetParamString(8, wxT("peer"));
				ins->SetParamString(9, wxT("test"));
				if (ins->RunQuery() < 1) {
					GiveUp(raw);
					return false;
				}
			}
			return true;
		}
		catch (const ibCoreException&) {
			GiveUp(raw);
			return false;
		}
	}

	Outcome WaitUntilCommit(ibDatabaseLayer* raw, const ibLockItem& item)
	{
		ibSingleLockHolder left(wxT("left"));
		ibLockManager* mine = Mine();
		std::atomic<bool> finished{ false };
		Outcome outcome;
		std::thread waiter([&] {
			outcome = TryAcquire(App(), mine, &left, item, {});
			finished.store(true);
		});

		const auto blockedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
		while (!finished.load() && std::chrono::steady_clock::now() < blockedUntil)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		EXPECT_FALSE(finished.load()) << "the acquire did not wait for the header";

		bool committed = false;
		try {
			raw->Commit();
			committed = true;
		}
		catch (...) {}
		if (!committed) {
			try { raw->RollBack(); }
			catch (...) {}
		}

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
		while (!finished.load() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		if (!finished.load()) {
			try { raw->RollBack(); }
			catch (...) {}
			waiter.join();
			ADD_FAILURE() << "the acquire was still waiting after the header was released";
			return outcome;
		}
		waiter.join();
		return outcome;
	}
};

template <typename Driver>
ibApplicationInstance* LockRace<Driver>::s_app = nullptr;

template <typename Driver>
std::shared_ptr<ibDatabaseLayer> LockRace<Driver>::s_db;

template <typename Driver>
std::unique_ptr<ibLockManager> LockRace<Driver>::s_peer;

#define OES_LOCK_SERVER_TESTS(Fixture)                                                                \
	TEST_F(Fixture, CreateTableLock_ExistingBase_CreatesTheHeader)                                    \
	{                                                                                                  \
		RecreateHeaderTable();                                                                         \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_RacedExclusive_GrantsOneOfTwenty)                                          \
	{                                                                                                  \
		for (int round = 0; round < 20; ++round)                                                       \
			ExpectOneExclusive(round);                                                                 \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_ExclusiveRacedWithShared_GrantsOne)                                        \
	{                                                                                                  \
		const ibLockItem exclusive = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa1"), ibLockMode::Exclusive); \
		const ibLockItem shared    = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa1"), ibLockMode::Shared);    \
		ibSingleLockHolder left(wxT("left"));                                                          \
		ibSingleLockHolder right(wxT("right"));                                                        \
		const PairResult race = Race(App(), Mine(), &left, exclusive, {}, Peer(), &right, shared, {}); \
		const int granted = (race.first.kind == OutcomeKind::Granted ? 1 : 0)                          \
			+ (race.second.kind == OutcomeKind::Granted ? 1 : 0);                                      \
		EXPECT_EQ(granted, 1) << race.first.error << " / " << race.second.error;                       \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_TwoSharedRaced_GrantsBoth)                                                 \
	{                                                                                                  \
		const ibLockItem shared = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa2"), ibLockMode::Shared); \
		ibSingleLockHolder left(wxT("left"));                                                          \
		ibSingleLockHolder right(wxT("right"));                                                        \
		const PairResult race = Race(App(), Mine(), &left, shared, {}, Peer(), &right, shared, {});    \
		EXPECT_EQ(race.first.kind, OutcomeKind::Granted) << race.first.error;                          \
		EXPECT_EQ(race.second.kind, OutcomeKind::Granted) << race.second.error;                        \
		EXPECT_EQ(CountRows(lock_table, shared.namespaceName, shared.KeyHash()), 2);                   \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_UpgradeWhilePeerHoldsShared_Refused)                                       \
	{                                                                                                  \
		const ibLockItem shared = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa3"), ibLockMode::Shared); \
		ibLockItem exclusive = shared;                                                                 \
		exclusive.lockMode = ibLockMode::Exclusive;                                                    \
		ibSingleLockHolder left(wxT("left"));                                                          \
		ibSingleLockHolder right(wxT("right"));                                                        \
		ibLockHandle held = Mine()->Acquire({ shared }, {}, &left);                                    \
		ibLockHandle other = Peer()->Acquire({ shared }, {}, &right);                                  \
		ASSERT_TRUE(held.IsValid());                                                                   \
		ASSERT_TRUE(other.IsValid());                                                                  \
		const Outcome upgrade = TryAcquire(App(), Mine(), &left, exclusive, {});                       \
		EXPECT_EQ(upgrade.kind, OutcomeKind::Conflict) << upgrade.error;                               \
		EXPECT_EQ(CountRows(lock_table, shared.namespaceName, shared.KeyHash()), 2);                   \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_UpgradeOfOwnShared_KeepsGuid)                                              \
	{                                                                                                  \
		const ibLockItem shared = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa4"), ibLockMode::Shared); \
		ibLockItem exclusive = shared;                                                                 \
		exclusive.lockMode = ibLockMode::Exclusive;                                                    \
		ibSingleLockHolder left(wxT("left"));                                                          \
		ibSingleLockHolder right(wxT("right"));                                                        \
		ibLockHandle held = Mine()->Acquire({ shared }, {}, &left);                                    \
		ASSERT_TRUE(held.IsValid());                                                                   \
		const wxString guid = held.LockGuids().front().str();                                          \
		const Outcome upgrade = TryAcquire(App(), Mine(), &left, exclusive, {});                       \
		EXPECT_EQ(upgrade.kind, OutcomeKind::Granted) << upgrade.error;                                \
		const HolderRow row = ReadHolder(shared.namespaceName, shared.KeyHash());                      \
		EXPECT_EQ(row.count, 1);                                                                       \
		EXPECT_EQ(row.guid, guid);                                                                     \
		EXPECT_EQ(row.mode, static_cast<int>(ibLockMode::Exclusive));                                  \
		const Outcome blocked = TryAcquire(App(), Peer(), &right, shared, {});                         \
		EXPECT_EQ(blocked.kind, OutcomeKind::Conflict) << blocked.error;                               \
		held.Release();                                                                                \
		EXPECT_EQ(CountRows(lock_table, shared.namespaceName, shared.KeyHash()), 0);                   \
		EXPECT_EQ(CountRows(lock_key_table, shared.namespaceName, shared.KeyHash()), 1);               \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_NoWaitOnHeldHeader_LockException)                                          \
	{                                                                                                  \
		const ibLockItem exclusive = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa5"), ibLockMode::Exclusive); \
		ASSERT_TRUE(SeedHeader(exclusive));                                                            \
		auto raw = DriverType::Hold();                                                                     \
		ASSERT_TRUE(raw != nullptr);                                                                   \
		raw->BeginTransaction();                                                                       \
		{                                                                                              \
			ibStatementGuard upd(raw.get(), raw->PrepareStatement(                                     \
				wxT("UPDATE sys_lock_key SET namespace = ? WHERE namespace = ? AND keyHash = ?")));    \
			ASSERT_TRUE(static_cast<bool>(upd));                                                       \
			upd->SetParamString(1, exclusive.namespaceName);                                           \
			upd->SetParamString(2, exclusive.namespaceName);                                           \
			upd->SetParamString(3, exclusive.KeyHash());                                               \
			ASSERT_GE(upd->RunQuery(), 1);                                                             \
		}                                                                                              \
		ibSingleLockHolder left(wxT("left"));                                                          \
		ibLockOptions nowait;                                                                          \
		nowait.wait = false;                                                                           \
		ibLockManager* mine = Mine();                                                                  \
		const auto began = std::chrono::steady_clock::now();                                           \
		std::atomic<bool> finished{ false };                                                           \
		Outcome outcome;                                                                               \
		std::thread waiter([&] {                                                                       \
			outcome = TryAcquire(App(), mine, &left, exclusive, nowait);                               \
			finished.store(true);                                                                      \
		});                                                                                            \
		const auto deadline = began + std::chrono::milliseconds(1500);                                 \
		while (!finished.load() && std::chrono::steady_clock::now() < deadline)                        \
			std::this_thread::sleep_for(std::chrono::milliseconds(20));                                \
		if (!finished.load()) {                                                                        \
			try { raw->RollBack(); }                                                                   \
			catch (...) {}                                                                             \
			waiter.join();                                                                             \
			FAIL() << "noWait was still waiting after 1500ms";                                         \
		}                                                                                              \
		waiter.join();                                                                                 \
		const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(                     \
			std::chrono::steady_clock::now() - began);                                                 \
		try { raw->RollBack(); }                                                                       \
		catch (...) {}                                                                                 \
		EXPECT_LT(waited.count(), 1000);                                                               \
		EXPECT_EQ(outcome.kind, OutcomeKind::Conflict) << outcome.error;                               \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_BlockedExclusiveThenCommit_Conflict)                                       \
	{                                                                                                  \
		const ibLockItem exclusive = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa6"), ibLockMode::Exclusive); \
		ASSERT_TRUE(SeedHeader(exclusive));                                                            \
		auto raw = DriverType::Hold();                                                                     \
		ASSERT_TRUE(raw != nullptr);                                                                   \
		ASSERT_TRUE(Plant(raw.get(), exclusive, ibLockMode::Exclusive)) << "the peer did not hold the header"; \
		const Outcome outcome = WaitUntilCommit(raw.get(), exclusive);                                 \
		EXPECT_EQ(outcome.kind, OutcomeKind::Conflict) << outcome.error;                               \
		if (raw->IsActiveTransaction())                                                                \
			raw->RollBack();                                                                           \
	}                                                                                                  \
	TEST_F(Fixture, Acquire_BlockedSharedThenCommit_Granted)                                           \
	{                                                                                                  \
		const ibLockItem shared = Item(wxT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaa7"), ibLockMode::Shared); \
		ASSERT_TRUE(SeedHeader(shared));                                                               \
		auto raw = DriverType::Hold();                                                                     \
		ASSERT_TRUE(raw != nullptr);                                                                   \
		ASSERT_TRUE(Plant(raw.get(), shared, ibLockMode::Shared)) << "the peer did not hold the header"; \
		const Outcome outcome = WaitUntilCommit(raw.get(), shared);                                    \
		EXPECT_EQ(outcome.kind, OutcomeKind::Granted) << outcome.error;                                \
		if (raw->IsActiveTransaction())                                                                \
			raw->RollBack();                                                                           \
	}

#ifdef OES_LOCK_RACE_POSTGRES_ONLY

struct PostgresDriver {
	static bool Open(std::shared_ptr<ibDatabaseLayer>& db)
	{
		if (std::getenv("OES_PG_USER") == nullptr)
			return false;
		auto layer = std::make_shared<ibDatabaseLayerPostgres>();
		if (!layer->Open(
				EnvOr("OES_PG_HOST", wxT("127.0.0.1")),
				EnvOr("OES_PG_PORT", wxT("5432")),
				EnvOr("OES_PG_DB", wxT("oes_test")),
				EnvOr("OES_PG_USER", wxT("postgres")),
				EnvOr("OES_PG_PASSWORD", wxEmptyString)))
			return false;
		db = layer;
		return true;
	}

	static std::shared_ptr<ibDatabaseLayer> Hold()
	{
		auto raw = std::make_shared<ibDatabaseLayerPostgres>();
		if (!raw->Open(
				EnvOr("OES_PG_HOST", wxT("127.0.0.1")),
				EnvOr("OES_PG_PORT", wxT("5432")),
				EnvOr("OES_PG_DB", wxT("oes_test")),
				EnvOr("OES_PG_USER", wxT("postgres")),
				EnvOr("OES_PG_PASSWORD", wxEmptyString)))
			return nullptr;
		return raw;
	}

	static void Close() {}
};

class PostgresLockRace : public LockRace<PostgresDriver> {};

OES_LOCK_SERVER_TESTS(PostgresLockRace)

#else

#ifdef OES_USE_FIREBIRD

struct FirebirdDriver {
	static wxString s_path;

	static bool Open(std::shared_ptr<ibDatabaseLayer>& db)
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		s_path = wxFileName(wxStandardPaths::Get().GetTempDir(),
			wxString::Format(wxT("oes_lock_race_%lld.fdb"), (long long)now)).GetFullPath();
		std::shared_ptr<ibDatabaseLayerFirebird> layer;
		try {
			layer = std::make_shared<ibDatabaseLayerFirebird>();
			layer->SetUser(wxT("SYSDBA"));
			layer->SetPassword(wxT("masterkey"));
			if (!layer->Open(s_path))
				return false;
		}
		catch (...) {
			return false;
		}
		db = layer;
		return true;
	}

	static std::shared_ptr<ibDatabaseLayer> Hold()
	{
		try {
			auto raw = std::make_shared<ibDatabaseLayerFirebird>();
			raw->SetUser(wxT("SYSDBA"));
			raw->SetPassword(wxT("masterkey"));
			if (!raw->Open(s_path))
				return nullptr;
			return raw;
		}
		catch (...) {
			return nullptr;
		}
	}

	static void Close()
	{
		if (!s_path.IsEmpty() && wxFileExists(s_path))
			wxRemoveFile(s_path);
		s_path.clear();
	}
};

wxString FirebirdDriver::s_path;

class FirebirdLockRace : public LockRace<FirebirdDriver> {};

OES_LOCK_SERVER_TESTS(FirebirdLockRace)

#endif

struct SqliteDriver {
	static wxString s_path;

	static bool Open(std::shared_ptr<ibDatabaseLayer>& db)
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		s_path = wxFileName(wxStandardPaths::Get().GetTempDir(),
			wxString::Format(wxT("oes_lock_race_%lld.db"), (long long)now)).GetFullPath();
		auto layer = std::make_shared<ibDatabaseLayerSQLite>();
		if (!layer->Open(s_path))
			return false;
		db = layer;
		return true;
	}

	static void Close()
	{
		if (!s_path.IsEmpty() && wxFileExists(s_path))
			wxRemoveFile(s_path);
		s_path.clear();
	}
};

wxString SqliteDriver::s_path;

class SqliteLockRace : public LockRace<SqliteDriver> {
protected:
	void SetUp() override
	{
		ASSERT_TRUE(s_app != nullptr);
		ASSERT_TRUE(s_peer != nullptr);
	}
};

TEST_F(SqliteLockRace, CreateTableLock_ExistingBase_CreatesTheHeader)
{
	RecreateHeaderTable();
}

TEST_F(SqliteLockRace, Acquire_SecondShared_Granted)
{
	const ibLockItem shared = Item(wxT("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbb1"), ibLockMode::Shared);
	ibSingleLockHolder left(wxT("left"));
	ibSingleLockHolder right(wxT("right"));
	ibLockHandle first = Mine()->Acquire({ shared }, {}, &left);
	ibLockHandle second = Peer()->Acquire({ shared }, {}, &right);
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	EXPECT_EQ(CountRows(lock_table, shared.namespaceName, shared.KeyHash()), 2);
	EXPECT_EQ(CountRows(lock_key_table, shared.namespaceName, shared.KeyHash()), 1);
}

TEST_F(SqliteLockRace, Acquire_ExclusiveWhileSharedHeld_Refused)
{
	const ibLockItem shared = Item(wxT("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbb2"), ibLockMode::Shared);
	ibLockItem exclusive = shared;
	exclusive.lockMode = ibLockMode::Exclusive;
	ibSingleLockHolder left(wxT("left"));
	ibSingleLockHolder right(wxT("right"));
	ibLockHandle held = Mine()->Acquire({ shared }, {}, &left);
	ASSERT_TRUE(held.IsValid());
	const Outcome blocked = TryAcquire(App(), Peer(), &right, exclusive, {});
	EXPECT_EQ(blocked.kind, OutcomeKind::Conflict) << blocked.error;
	EXPECT_EQ(CountRows(lock_table, shared.namespaceName, shared.KeyHash()), 1);
}

TEST_F(SqliteLockRace, Acquire_UpgradeOfOwnShared_KeepsGuid)
{
	const ibLockItem shared = Item(wxT("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbb3"), ibLockMode::Shared);
	ibLockItem exclusive = shared;
	exclusive.lockMode = ibLockMode::Exclusive;
	ibSingleLockHolder left(wxT("left"));
	ibLockHandle held = Mine()->Acquire({ shared }, {}, &left);
	ASSERT_TRUE(held.IsValid());
	const wxString guid = held.LockGuids().front().str();
	const Outcome upgrade = TryAcquire(App(), Mine(), &left, exclusive, {});
	EXPECT_EQ(upgrade.kind, OutcomeKind::Granted) << upgrade.error;
	const HolderRow row = ReadHolder(shared.namespaceName, shared.KeyHash());
	EXPECT_EQ(row.count, 1);
	EXPECT_EQ(row.guid, guid);
	EXPECT_EQ(row.mode, static_cast<int>(ibLockMode::Exclusive));
	held.Release();
	EXPECT_EQ(CountRows(lock_table, shared.namespaceName, shared.KeyHash()), 0);
	EXPECT_EQ(CountRows(lock_key_table, shared.namespaceName, shared.KeyHash()), 1);
}

TEST_F(SqliteLockRace, SweepOrphans_NoHolder_RemovesHeader)
{
	const ibLockItem item = Item(wxT("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbb4"), ibLockMode::Exclusive);
	ibSingleLockHolder left(wxT("left"));
	ibLockHandle held = Mine()->Acquire({ item }, {}, &left);
	ASSERT_TRUE(held.IsValid());
	EXPECT_EQ(CountRows(lock_key_table, item.namespaceName, item.KeyHash()), 1);
	held.Release();
	EXPECT_EQ(CountRows(lock_table, item.namespaceName, item.KeyHash()), 0);
	EXPECT_EQ(CountRows(lock_key_table, item.namespaceName, item.KeyHash()), 1);
	Mine()->SweepOrphans({});
	EXPECT_EQ(CountRows(lock_key_table, item.namespaceName, item.KeyHash()), 0);
}

TEST_F(SqliteLockRace, SweepOrphans_HolderPresent_KeepsHeader)
{
	const ibLockItem item = Item(wxT("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbb5"), ibLockMode::Exclusive);
	ibSingleLockHolder left(wxT("left"));
	ibLockHandle held = Mine()->Acquire({ item }, {}, &left);
	ASSERT_TRUE(held.IsValid());
	Mine()->SweepOrphans({ left.Identity() });
	EXPECT_EQ(CountRows(lock_table, item.namespaceName, item.KeyHash()), 1);
	EXPECT_EQ(CountRows(lock_key_table, item.namespaceName, item.KeyHash()), 1);
}

#endif

} // namespace
