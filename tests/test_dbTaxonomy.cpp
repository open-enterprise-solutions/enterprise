// =============================================================================
// OES Enterprise — DB exception taxonomy regression tests
//
// Pins the per-driver native code -> Kind tables (the static ClassifyDatabaseError
// of the SQLite and Firebird layers, ClassifySqlState of PostgreSQL) so a driver bump
// doesn't silently flip a constraint violation into Kind::Unknown — at which
// point IsRetryable() == false stops holding and the apply-flow / form save /
// lock manager lose their recoverability signal.
//
// Two layers:
//   1. Unit — call each driver's table directly with well-known native codes
//      (no DB needed). Fast, deterministic, runs in every CI build.
//   2. Integration — a duplicate INSERT on SQLite, through the layer AND
//      through a prepared statement (the road ibDatabaseQueryBuilder and the
//      lock manager take): the caught exception carries Kind::Constraint.
//      The statement road threw Unknown until the Kind was recorded with the
//      code where the driver reads it (#247).
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

#include <wx/init.h>
#include "backend/databaseLayer/databaseResultSet.h"

#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#endif

using Kind = ibBackendDatabaseException::Kind;

// ---------------------------------------------------------------------------
// SQLite — known native codes
// ---------------------------------------------------------------------------

TEST(DbTaxonomy_SQLite, ConstraintViolation_19) {
	EXPECT_EQ(ibDatabaseLayerSQLite::ClassifyDatabaseError(19), Kind::Constraint)
		<< "SQLITE_CONSTRAINT (19) must classify as Constraint — IsRetryable=false";
	// An extended code is the same constraint: SQLITE_CONSTRAINT_PRIMARYKEY.
	EXPECT_EQ(ibDatabaseLayerSQLite::ClassifyDatabaseError(19 | (6 << 8)), Kind::Constraint);
}

TEST(DbTaxonomy_SQLite, BusyOrLocked_5_6) {
	// SQLITE_BUSY / SQLITE_LOCKED — caller can retry after a backoff.
	EXPECT_EQ(ibDatabaseLayerSQLite::ClassifyDatabaseError(5), Kind::Timeout);
	EXPECT_EQ(ibDatabaseLayerSQLite::ClassifyDatabaseError(6), Kind::Timeout);
}

TEST(DbTaxonomy_SQLite, GenericError_NotConnectionLost) {
	// Code 1 (SQLITE_ERROR) is the catch-all for SQL syntax / missing
	// table / etc. Critical NOT to classify it as ConnectionLost —
	// recovery code that triggers reconnect on that would loop.
	EXPECT_NE(ibDatabaseLayerSQLite::ClassifyDatabaseError(1), Kind::ConnectionLost);
}

// ---------------------------------------------------------------------------
// Cross-driver invariant — Constraint is Constraint
// ---------------------------------------------------------------------------
//
// The actual native codes differ (SQLite 19, FB isc_unique_key_violation,
// PG SQLSTATE 23505 -> int, MySQL errno 1062, ODBC SQLSTATE 23000) but the
// classification result is the same Kind::Constraint. If a driver flips
// this, code that branches on Kind (apply-flow, form save) silently
// misroutes the recovery path.

#ifdef OES_USE_FIREBIRD
TEST(DbTaxonomy_Firebird, UniqueKeyViolationIsConstraint) {
	// isc_unique_key_violation = 335544665 — primary "duplicate key" gds
	// code FB raises for the canonical PK / unique-index conflict.
	EXPECT_EQ(ibDatabaseLayerFirebird::ClassifyDatabaseError(335544665), Kind::Constraint);
	EXPECT_EQ(ibDatabaseLayerFirebird::ClassifyDatabaseError(335544349), Kind::Constraint);   // isc_no_dup
}

TEST(DbTaxonomy_Firebird, AWaiterOnAChangedRowIsALockWait) {
	EXPECT_EQ(ibDatabaseLayerFirebird::ClassifyDatabaseError(335544451), Kind::Deadlock);   // isc_update_conflict
	EXPECT_EQ(ibDatabaseLayerFirebird::ClassifyDatabaseError(335544336), Kind::Deadlock);   // isc_deadlock
}
#endif

#ifdef OES_USE_POSTGRESQL
TEST(DbTaxonomy_Postgres, SqlStateClasses) {
	EXPECT_EQ(ibDatabaseLayerPostgres::ClassifySqlState(wxT("23505")), Kind::Constraint);
	EXPECT_EQ(ibDatabaseLayerPostgres::ClassifySqlState(wxT("40P01")), Kind::Deadlock);
	EXPECT_EQ(ibDatabaseLayerPostgres::ClassifySqlState(wxT("55P03")), Kind::Timeout);
}
#endif

// ---------------------------------------------------------------------------
// IsRetryable — semantic contract on the exception class itself.
//
// Constructors are protected/private (only mintable via Throw); we round-
// trip through Throw → catch to read the resulting flag. That's also the
// realistic path — every production throw site uses Throw.
// ---------------------------------------------------------------------------

namespace {
bool RetryableForKind(Kind k) {
	try { ibBackendDatabaseException::Throw(k, wxT("probe")); }
	catch (const ibBackendDatabaseException& e) { return e.IsRetryable(); }
	return false;   // unreachable — Throw is [[noreturn]]
}
} // namespace

TEST(DbTaxonomy_Kind, ConstraintIsNotRetryable) {
	EXPECT_FALSE(RetryableForKind(Kind::Constraint));
}

TEST(DbTaxonomy_Kind, ConnectionLostIsRetryable) {
	EXPECT_TRUE(RetryableForKind(Kind::ConnectionLost));
}

TEST(DbTaxonomy_Kind, DeadlockIsRetryable) {
	EXPECT_TRUE(RetryableForKind(Kind::Deadlock));
}

TEST(DbTaxonomy_Kind, TimeoutIsRetryable) {
	EXPECT_TRUE(RetryableForKind(Kind::Timeout));
}

TEST(DbTaxonomy_Kind, SyntaxIsNotRetryable) {
	EXPECT_FALSE(RetryableForKind(Kind::Syntax));
}

// ---------------------------------------------------------------------------
// ibDatabaseLayerException::Throw — composes native code + sqlstate +
// message + Kind. Catch-by-base must preserve dynamic type for downstream
// re-catch.
// ---------------------------------------------------------------------------

TEST(DbTaxonomy_Throw, ConstraintRoundTrip) {
	bool caught = false;
	try {
		ibDatabaseLayerException::Throw(
			Kind::Constraint, /*nativeCode=*/19, wxT("23000"),
			wxT("duplicate PK on sys_user(login)"));
	}
	catch (const ibDatabaseLayerException& e) {
		caught = true;
		EXPECT_EQ(e.GetKind(), Kind::Constraint);
		EXPECT_EQ(e.GetDriverErrorCode(), 19);
		EXPECT_EQ(e.GetSqlState(), wxT("23000"));
		EXPECT_FALSE(e.IsRetryable());
	}
	EXPECT_TRUE(caught) << "Throw must throw, not return";
}

TEST(DbTaxonomy_Throw, CatchAsBaseClass) {
	// Re-throw via the base class: the catch site receives
	// ibBackendDatabaseException& but the dynamic type is still
	// ibDatabaseLayerException, so dynamic_cast / typeid resolves.
	// This is the contract debug-log / wes' set_exception_handler
	// rely on when deciding whether to emit driver-specific JSON.
	bool sawDerived = false;
	try {
		ibDatabaseLayerException::Throw(
			Kind::Deadlock, 1213, wxT("40001"),
			wxT("MySQL deadlock victim"));
	}
	catch (const ibBackendException& base) {
		// dynamic_cast through the public base hierarchy:
		// ibBackendException -> ibBackendDatabaseException -> ibDatabaseLayerException
		const auto* dbex = dynamic_cast<const ibDatabaseLayerException*>(&base);
		ASSERT_NE(dbex, nullptr);
		sawDerived = true;
		EXPECT_EQ(dbex->GetKind(), Kind::Deadlock);
		EXPECT_TRUE(dbex->IsRetryable());
	}
	EXPECT_TRUE(sawDerived);
}

// ---------------------------------------------------------------------------
// Integration — live SQLite duplicate insert
// ---------------------------------------------------------------------------
//
// `:memory:` keeps the test self-contained — no temp file litter, no
// per-run cleanup, no cross-test interference. Validates the end-to-end
// path: driver raises native code -> ClassifyDatabaseError -> throw ->
// caller catches typed exception with the right Kind.

TEST(DbTaxonomy_Integration_SQLite, DuplicatePkRaisesConstraint) {
	ibDatabaseLayerSQLite db;
	ASSERT_TRUE(db.Open(wxT(":memory:")))
		<< "in-memory SQLite open should never fail";

	// Throwaway schema — one row with a PK.
	db.RunQuery(wxT("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)"));
	db.RunQuery(wxT("INSERT INTO t (id, name) VALUES (1, 'first')"));

	bool caught = false;
	Kind kind = Kind::Unknown;
	bool retryable = true;

	try {
		// Same id — PK conflict.
		db.RunQuery(wxT("INSERT INTO t (id, name) VALUES (1, 'second')"));
	}
	catch (const ibBackendDatabaseException& e) {
		caught = true;
		kind = e.GetKind();
		retryable = e.IsRetryable();
	}
	catch (const ibBackendException& e) {
		// If the driver throws plain ibBackendException without
		// classification we still want the test to fail loudly with
		// the message rather than time out — log + fail.
		FAIL() << "Expected ibBackendDatabaseException, got plain "
		          "ibBackendException: "
		       << std::string(e.GetErrorDescription().mb_str());
	}

	db.Close();

	EXPECT_TRUE(caught) << "Duplicate PK must throw";
	EXPECT_EQ(kind, Kind::Constraint)
		<< "Duplicate PK must classify as Constraint, got Kind="
		<< static_cast<int>(kind);
	EXPECT_FALSE(retryable)
		<< "Constraint violations are never retryable";
}

// The same duplicate through a prepared statement — the road ibDatabaseQueryBuilder and the lock
// manager take. It throws from the statement's own reporter, which used to answer Unknown.
TEST(DbTaxonomy_Integration_SQLite, DuplicatePkThroughAStatementRaisesConstraint) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(wxT(":memory:")));
	ibApplicationInstance::GetConnectionPool()->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	db->RunQuery(wxT("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)"));

	const auto insert = []() {
		return ibDatabaseQueryBuilder().Execute(ibInsert(wxT("t"), {
			{ wxT("id"),   ibConst(ibValue(1)) },
			{ wxT("name"), ibConst(ibValue(wxString(wxT("first")))) },
		}));
	};
	ASSERT_EQ(insert(), 1);

	Kind kind = Kind::Unknown;
	try {
		insert();
	}
	catch (const ibDatabaseLayerException& e) {
		kind = e.GetKind();
	}
	ibApplicationInstance::DestroyAppDataEnv();
	EXPECT_EQ(kind, Kind::Constraint) << "got Kind=" << static_cast<int>(kind);
}
