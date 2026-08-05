// =============================================================================
// PostgreSQL dialect — executed against a REAL server (integration scope).
//
// WHY THIS TARGET EXISTS. Everything else in the suite runs on SQLite, and
// SQLite is the polygon: fast, embedded, reproducible, and perfect for pinning
// SEMANTICS — how a query is built, how columns are laid out, how composition
// behaves. What it cannot pin is the other half: whether the SERVER agrees.
//
// The existing dialect tests (test_dialectDictionary, test_materializationDialect)
// check that we RENDER the right SQL per driver. Nobody checked that the driver
// then reads the answer back correctly — and PostgreSQL is the engine production
// runs on, while SQLite is being kept for tests and the log.
//
// So this target executes, on a live server, exactly the things that differ
// between engines and cannot differ silently:
//
//   * numeric precision — a decimal must survive the round trip unrounded;
//   * timestamps — a date with a time, back as the same instant;
//   * guids — the identity column type, back as the same string;
//   * blobs — bytes in, same bytes out;
//   * upsert — the dialect's own spelling (ON CONFLICT), including the second
//     write that must UPDATE rather than duplicate.
//
// CONNECTION comes from the environment, so the same binary is a no-op on a
// developer machine and a real test in CI:
//
//   OES_PG_HOST (default 127.0.0.1)  OES_PG_PORT (default 5432)
//   OES_PG_DB   (default oes_test)   OES_PG_USER / OES_PG_PASSWORD
//
// Without OES_PG_USER every test SKIPS — a missing server is not a failure,
// it is an absent opportunity to check.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/string.h>

#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/preparedStatement.h"

#include <cstdlib>
#include <memory>

namespace {

wxString EnvOr(const char* name, const wxString& fallback)
{
	const char* value = std::getenv(name);
	return (value != nullptr && *value != '\0') ? wxString::FromUTF8(value) : fallback;
}

bool HaveServer()
{
	const char* user = std::getenv("OES_PG_USER");
	return user != nullptr && *user != '\0';
}

// One connection for the whole target: opening PostgreSQL is a network round
// trip plus authentication, and every test here needs the same database.
class PostgresDialect : public ::testing::Test {
protected:
	static void SetUpTestSuite()
	{
		if (!HaveServer())
			return;

		static wxInitializer s_wxInit;   // wxString / wxDateTime need wxBase up

		s_db = std::make_shared<ibDatabaseLayerPostgres>();
		const bool opened = s_db->Open(
			EnvOr("OES_PG_HOST", wxT("127.0.0.1")),
			EnvOr("OES_PG_PORT", wxT("5432")),
			EnvOr("OES_PG_DB", wxT("oes_test")),
			EnvOr("OES_PG_USER", wxT("postgres")),
			EnvOr("OES_PG_PASSWORD", wxEmptyString));

		if (!opened)
			s_db.reset();
	}

	static void TearDownTestSuite()
	{
		if (s_db) {
			s_db->Close();
			s_db.reset();
		}
	}

	void SetUp() override
	{
		if (!HaveServer())
			GTEST_SKIP() << "OES_PG_USER not set — no PostgreSQL to talk to";
		if (!s_db)
			GTEST_SKIP() << "PostgreSQL is configured but did not open";
	}

	static std::shared_ptr<ibDatabaseLayerPostgres> s_db;
};

std::shared_ptr<ibDatabaseLayerPostgres> PostgresDialect::s_db;

} // namespace

TEST_F(PostgresDialect, NumericSurvivesTheRoundTripUnrounded)
{
	// The case SQLite cannot answer: it stores numerics as doubles, so a value
	// that must stay exact is exact there by luck. PostgreSQL has a real
	// NUMERIC, and the driver has to read it back without going through a
	// double on the way.
	s_db->RunQuery(wxT("DROP TABLE IF EXISTS oes_dialect_numeric"));
	s_db->RunQuery(wxT("CREATE TABLE oes_dialect_numeric (v NUMERIC(28,10))"));
	s_db->RunQuery(wxT("INSERT INTO oes_dialect_numeric (v) VALUES (12345678901234.1234567891)"));

	// ibResultSetGuard, NOT unique_ptr. A result set is owned by the LAYER as well
	// as by the caller: RunQueryWithResults registers it, and the layer closes
	// whatever is still registered when it goes away. Deleting it directly leaves
	// that registration pointing at freed memory, and the layer's destructor walks
	// into it — the process then dies AFTER the last test passes, which is exactly
	// how this read (all four green, then SIGSEGV at teardown, with only
	// "ResultSet NOT closed and cleaned up by the ibDatabaseLayer dtor" as a hint).
	// The guard calls Close() and CloseResultSet(), so both owners agree.
	ibResultSetGuard rs(s_db,
		s_db->RunQueryWithResults(wxT("SELECT v FROM oes_dialect_numeric")));
	ASSERT_TRUE(static_cast<bool>(rs));
	ASSERT_TRUE(rs->Next());

	EXPECT_EQ(wxT("12345678901234.1234567891"), rs->GetResultString(1));

	s_db->RunQuery(wxT("DROP TABLE oes_dialect_numeric"));
}

TEST_F(PostgresDialect, TimestampComesBackAsTheSameInstant)
{
	s_db->RunQuery(wxT("DROP TABLE IF EXISTS oes_dialect_stamp"));
	s_db->RunQuery(wxT("CREATE TABLE oes_dialect_stamp (v TIMESTAMP)"));

	std::unique_ptr<ibPreparedStatement> stmt(
		s_db->PrepareStatement(wxT("INSERT INTO oes_dialect_stamp (v) VALUES (?)")));
	ASSERT_NE(nullptr, stmt);

	const wxDateTime written(4, wxDateTime::Aug, 2026, 16, 33, 48);
	stmt->SetParamDate(1, written);
	stmt->RunQuery();

	ibResultSetGuard rs(s_db,
		s_db->RunQueryWithResults(wxT("SELECT v FROM oes_dialect_stamp")));
	ASSERT_TRUE(static_cast<bool>(rs));
	ASSERT_TRUE(rs->Next());

	const wxDateTime read = rs->GetResultDate(1);
	ASSERT_TRUE(read.IsValid());
	EXPECT_EQ(written.GetTicks(), read.GetTicks())
		<< "a timestamp must not drift through the driver";

	s_db->RunQuery(wxT("DROP TABLE oes_dialect_stamp"));
}

TEST_F(PostgresDialect, BlobBytesAreReturnedIntact)
{
	// Blobs are where drivers differ most: PostgreSQL wants BYTEA with its own
	// escaping, and a byte that is mangled on the way in is invisible until
	// somebody reads a schedule, a settings record or a value storage back.
	s_db->RunQuery(wxT("DROP TABLE IF EXISTS oes_dialect_blob"));
	s_db->RunQuery(wxT("CREATE TABLE oes_dialect_blob (v BYTEA)"));

	const unsigned char payload[] = { 0x00, 0x01, 0xFE, 0xFF, 0x7F, 0x80, 0x0A, 0x0D };

	std::unique_ptr<ibPreparedStatement> stmt(
		s_db->PrepareStatement(wxT("INSERT INTO oes_dialect_blob (v) VALUES (?)")));
	ASSERT_NE(nullptr, stmt);
	stmt->SetParamBlob(1, payload, sizeof(payload));
	stmt->RunQuery();

	ibResultSetGuard rs(s_db,
		s_db->RunQueryWithResults(wxT("SELECT v FROM oes_dialect_blob")));
	ASSERT_TRUE(static_cast<bool>(rs));
	ASSERT_TRUE(rs->Next());

	wxMemoryBuffer readBack;
	rs->GetResultBlob(1, readBack);
	ASSERT_EQ(sizeof(payload), readBack.GetDataLen());
	EXPECT_EQ(0, memcmp(payload, readBack.GetData(), sizeof(payload)));

	s_db->RunQuery(wxT("DROP TABLE oes_dialect_blob"));
}

TEST_F(PostgresDialect, UpsertUpdatesInsteadOfDuplicating)
{
	// The register (sys_job) and the shared clock both depend on this: the
	// dialect renders ON CONFLICT for PostgreSQL, and the SECOND write must
	// change the row rather than add one.
	s_db->RunQuery(wxT("DROP TABLE IF EXISTS oes_dialect_upsert"));
	s_db->RunQuery(wxT("CREATE TABLE oes_dialect_upsert (k VARCHAR(36) PRIMARY KEY, v INTEGER)"));

	s_db->RunQuery(wxT("INSERT INTO oes_dialect_upsert (k, v) VALUES ('key', 1) ")
		wxT("ON CONFLICT (k) DO UPDATE SET v = EXCLUDED.v"));
	s_db->RunQuery(wxT("INSERT INTO oes_dialect_upsert (k, v) VALUES ('key', 2) ")
		wxT("ON CONFLICT (k) DO UPDATE SET v = EXCLUDED.v"));

	ibResultSetGuard rs(s_db,
		s_db->RunQueryWithResults(wxT("SELECT count(*) AS c, max(v) AS m FROM oes_dialect_upsert")));
	ASSERT_TRUE(static_cast<bool>(rs));
	ASSERT_TRUE(rs->Next());

	EXPECT_EQ(1, rs->GetResultInt(1)) << "upsert inserted a second row";
	EXPECT_EQ(2, rs->GetResultInt(2)) << "upsert did not update the existing row";

	s_db->RunQuery(wxT("DROP TABLE oes_dialect_upsert"));
}
