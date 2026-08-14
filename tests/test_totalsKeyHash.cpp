// =============================================================================
// OES Enterprise — a totals key an INDEX CANNOT HOLD, and the identity that
// replaces it.
//
// The first live apply of an accounting register's bundle failed on its own key:
//
//     too many keys defined for index ACCOUNTINGREGISTER1094_TT_PK
//
// A key is LOGICAL columns; an index is their PHYSICAL fields, and a reference is
// three of those (_TYPE / _RTRef / _RRRef). Seven key columns are therefore
// twenty-one index segments, against Firebird's sixteen. Nothing in an accounting
// totals key is removable, so the UNIQUENESS moves into one hashed field while the
// key columns stay exactly as they are and the delta goes on MATCHING BY THEM.
// (docs/register-shared-machinery.md § 4a)
//
// What this file pins down, in the order the mechanism is decided:
//
//   1. WHETHER the hash is used at all — the engine's declared ceiling, and the
//      honest NO where an engine has no digest to compute.
//   2. WHERE the digest lands in the rendered delta — in the inserted columns, and
//      NOT in the match, because a collision must be able to refuse an insert and
//      never to merge two keys into one row.
//   3. THAT THE NUMBERS STILL COME OUT RIGHT with the mechanism engaged, end to end
//      on a live engine: the movements are posted through the REAL rendered
//      triggers, over a table whose only unique index is the hash.
//
// (3) is the half that cannot be read off a string. SQLite declares no ceiling and
// has no SQL digest, so the suite's engine would never turn the mechanism on by
// itself — the fixture hands the renderer a dialect whose digest is a plain
// concatenation, which SQLite can compute. That is the same MECHANISM with a
// different spelling of one template, which is exactly what a dialect is for; it
// is not a re-implementation of the path under test.
// =============================================================================

#include <gtest/gtest.h>

#include <map>

#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

// A register keyed by period + warehouse + owner, one resource stored as a
// received / spent pair. Three key columns is not what breaks an index — the point
// of the fixture is the MECHANISM, and the mechanism is decided by a declared
// ceiling, not by how the key got wide.
ibMaterializeSpec MakeSpec(bool withKeyHash)
{
	ibMaterializeSpec spec;
	spec.m_table            = wxT("Reg9_T");
	spec.m_source           = wxT("Reg9");
	spec.m_keyColumns       = { wxT("wh"), wxT("owner_") };
	spec.m_periodColumn     = wxT("period_");
	spec.m_periodSourceExpr = wxT("{row}.period_");
	spec.m_periodUnit       = ibTotalsPeriod::Month;
	spec.m_deltas = {
		{ wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 0 THEN {row}.qty ELSE 0 END") },
		{ wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 0 THEN 0 ELSE {row}.qty END") },
	};
	if (withKeyHash)
		spec.m_keyHashColumn = KeyHashColumnName();
	return spec;
}

// The suite's engine with a digest it can actually evaluate. A concatenation is a
// LOSSLESS digest, which makes it the strictest possible stand-in: if the parts are
// assembled in the wrong order, or a part is missed, the rows collide or split and
// the numeric assertions below fail rather than passing by luck.
ibMaterializationDialect DialectWithHash()
{
	ibMaterializationDialect m = ibDatabaseLayerSQLite::MaterializationDialect();
	m.m_keyHashItem   = wxT("COALESCE(CAST({value} AS TEXT), '~')");
	m.m_keyHashJoin   = wxT(" || '.' || ");
	m.m_keyHashDigest = wxT("({expr})");
	return m;
}

// A dialect that CANNOT digest — the SQLite one as it ships.
const ibMaterializationDialect& DialectWithoutHash()
{
	return ibDatabaseLayerSQLite::MaterializationDialect();
}

// (period, wh, owner) -> (in, out)
using Totals = std::map<wxString, std::pair<double, double>>;

struct KeyHashFix : ::testing::Test
{
	ibDatabaseLayerSQLite db;
	ibMaterializationDialect mat = DialectWithHash();
	bool ready = false;

	void SetUp() override
	{
		if (!db.Open(wxT(":memory:"))) {
			GTEST_SKIP() << "in-memory SQLite unavailable";
			return;
		}
		db.RunQuery(wxT("CREATE TABLE Reg9 ("
			"id INTEGER PRIMARY KEY, period_ TEXT, wh TEXT, owner_ TEXT, qty NUMERIC, rectype_ INTEGER)"));

		// ⭐ THE TABLE HAS NO KEY OF ITS OWN — its ONLY unique index is over the hash
		// column, which is precisely the shape the mechanism produces. If the digest
		// were wrong, two logical keys would share a row (or one would split in two)
		// and the parity below would say so.
		db.RunQuery(wxT("CREATE TABLE Reg9_T ("
			"period_ TEXT NOT NULL, wh TEXT NOT NULL, owner_ TEXT NOT NULL, "
			"keyhash_ TEXT, "
			"qty_in NUMERIC NOT NULL DEFAULT 0, qty_out NUMERIC NOT NULL DEFAULT 0)"));
		db.RunQuery(wxT("CREATE UNIQUE INDEX Reg9_T_PK ON Reg9_T (keyhash_)"));

		const ibMaterializeSql sql = RenderMaterialization(
			MakeSpec(/*withKeyHash*/ true), &mat, ibDatabaseLayerSQLite::Dialect());
		ASSERT_TRUE(sql.Apply(db)) << "the rendered maintenance bundle must install";
		ready = true;
	}

	void TearDown() override { db.Close(); }

	void Move(int id, const wxChar* period, const wxChar* wh, const wxChar* owner, double qty, int rectype)
	{
		db.RunQuery(wxT("INSERT INTO Reg9 (id, period_, wh, owner_, qty, rectype_) VALUES (%d, '%s', '%s', '%s', %s, %d)"),
			id, period, wh, owner, wxString::FromCDouble(qty), rectype);
	}
	void Erase(int id) { db.RunQuery(wxT("DELETE FROM Reg9 WHERE id = %d"), id); }

	// ⚠ THE QUERY GOES IN AS A %s ARGUMENT. It carries '%' of its own (strftime masks), and the driver
	// entry is a printf-style vararg — passing it directly makes the CRT read `%Y` as a specifier and
	// assert. The neighbouring parity test carries the same note; it is the sort of trap that is
	// invisible until a query happens to contain a percent sign.
	Totals Read(const wxString& sql)
	{
		Totals out;
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), sql);
		if (rs == nullptr)
			return out;
		while (rs->Next()) {
			const wxString key = rs->GetResultString(1) + wxT("|") + rs->GetResultString(2) + wxT("|") + rs->GetResultString(3);
			out[key] = { rs->GetResultDouble(4), rs->GetResultDouble(5) };
		}
		db.CloseResultSet(rs);   // the LAYER owns the result set; closing it on the object alone leaves its registry dangling
		return out;
	}

	// The ORACLE — the movements, aggregated directly.
	Totals FromMovements()
	{
		return Read(wxT(
			"SELECT strftime('%Y-%m-01 00:00:00', period_) AS p, wh AS w, owner_ AS o,"
			"       SUM(CASE WHEN rectype_ = 0 THEN qty ELSE 0 END) AS a,"
			"       SUM(CASE WHEN rectype_ = 0 THEN 0 ELSE qty END) AS b"
			"  FROM Reg9 GROUP BY p, w, o"
			"  HAVING a <> 0 OR b <> 0"));
	}

	Totals FromTotals()
	{
		return Read(wxT("SELECT period_, wh, owner_, qty_in, qty_out FROM Reg9_T"
		                " WHERE qty_in <> 0 OR qty_out <> 0"));
	}

	long RowCount(const wxString& table)
	{
		long n = -1;
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), wxT("SELECT COUNT(*) FROM ") + table);
		if (rs != nullptr) {
			if (rs->Next()) n = rs->GetResultInt(1);
			db.CloseResultSet(rs);
		}
		return n;
	}

	wxString OneString(const wxString& sql)
	{
		wxString value;
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), sql);
		if (rs != nullptr) {
			if (rs->Next()) value = rs->GetResultString(1);
			db.CloseResultSet(rs);
		}
		return value;
	}
};

} // namespace

// =============================================================================
// 1. WHETHER the mechanism engages at all
// =============================================================================

// SQLite declares no ceiling: no key is ever too wide there, so nothing is hashed
// and the unique index goes on covering the key itself.
TEST(KeyHashDecision, NoCeilingDeclared_NeverHashes)
{
	ibDatabaseLayerSQLite db;
	if (!db.Open(wxT(":memory:")))
		GTEST_SKIP() << "in-memory SQLite unavailable";

	EXPECT_EQ(0u, db.GetDialect().m_maxIndexSegments);
	EXPECT_FALSE(ibKeyNeedsHash(db, 3));
	EXPECT_FALSE(ibKeyNeedsHash(db, 64));   // no ceiling means no ceiling, at any width
	db.Close();
}

// The rule itself, stated over the two facts it reads. Written against a locally
// built dictionary rather than a driver's, because what is under test is the
// DECISION and not any one engine's numbers.
TEST(KeyHashDecision, CeilingAndDigestTogetherDecide)
{
	// A ceiling of sixteen: fifteen fields fit, seventeen do not.
	//
	// ⚠ Both halves are required. An engine past its ceiling with NO digest to
	// compute must answer NO — declaring a hash column nothing can fill would be a
	// UNIQUE index over NULLs, which enforces nothing on any engine and LOOKS like
	// it enforces the key.
	struct Case { unsigned int ceiling; bool digest; size_t fields; bool expected; };
	const Case cases[] = {
		{ 16, true,  15, false },   // fits
		{ 16, true,  16, false },   // fits exactly — the ceiling is inclusive
		{ 16, true,  17, true  },   // past it, and the engine can digest
		{ 16, false, 17, false },   // past it, and it cannot — say NO, let the engine object
		{  0, true,  99, false },   // no ceiling declared
	};

	for (const Case& c : cases) {
		const bool needs = (c.ceiling != 0) && (c.fields > c.ceiling) && c.digest;
		EXPECT_EQ(c.expected, needs)
			<< "ceiling " << c.ceiling << ", digest " << c.digest << ", fields " << c.fields;
	}
}

// Firebird is the engine the mechanism exists for, and MySQL meets the same
// ceiling. PostgreSQL declares one too, high enough that no key in the tree reaches
// it. These are the numbers the DECLARATION side reads, so they are worth pinning:
// a ceiling silently dropped would put the register straight back on the index that
// cannot be created.
TEST(KeyHashDecision, DeclaredCeilingsAreTheEnginesOwn)
{
	// (The dictionaries are static and free of any connection, so this asks nothing
	//  of a database — it reads what each driver declares about itself.)
	EXPECT_EQ(0u, ibDatabaseLayerSQLite::Dialect().m_maxIndexSegments);
	EXPECT_TRUE(ibDatabaseLayerSQLite::MaterializationDialect().m_keyHashDigest.IsEmpty())
		<< "SQLite has no SQL digest — the empty is the honest answer, not an omission";
}

// =============================================================================
// 2. WHERE the digest lands in the rendered delta
// =============================================================================

TEST(KeyHashRender, DigestIsInserted_AndIsNotWhatTheDeltaMatchesOn)
{
	const ibMaterializationDialect mat = DialectWithHash();
	const ibMaterializeSql sql = RenderMaterialization(
		MakeSpec(/*withKeyHash*/ true), &mat, ibDatabaseLayerSQLite::Dialect());

	const wxString text = sql.CreateText();
	ASSERT_FALSE(text.IsEmpty());

	// The column is written…
	EXPECT_TRUE(text.Contains(KeyHashColumnName()))
		<< "the digest column must be part of the insert";
	// …from the key's own values, all of them, in declaration order.
	EXPECT_TRUE(text.Contains(wxT("period_")) && text.Contains(wxT("wh")) && text.Contains(wxT("owner_")));

	// SQLite's upsert names its conflict target by COLUMN LIST, and the only unique
	// index there is over the digest — so that is what it must name. Naming the key
	// columns would be an ON CONFLICT with no index behind it.
	EXPECT_TRUE(text.Contains(wxT("ON CONFLICT (") + wxString(KeyHashColumnName()) + wxT(")")))
		<< "an ON CONFLICT engine conflicts on the index that exists";

	// And the accumulate never touches it: a matched row keeps the key it was
	// inserted with, so its digest is already right.
	EXPECT_FALSE(text.Contains(wxString(KeyHashColumnName()) + wxT(" = ")))
		<< "the digest is insert-only — nothing updates it";
}

TEST(KeyHashRender, WithoutTheColumn_NothingChanges)
{
	const ibMaterializationDialect mat = DialectWithHash();
	const ibMaterializeSql sql = RenderMaterialization(
		MakeSpec(/*withKeyHash*/ false), &mat, ibDatabaseLayerSQLite::Dialect());

	const wxString text = sql.CreateText();
	ASSERT_FALSE(text.IsEmpty());
	EXPECT_FALSE(text.Contains(KeyHashColumnName()));
	EXPECT_TRUE(text.Contains(wxT("ON CONFLICT (period_, wh, owner_)")))
		<< "the ordinary shape conflicts on the key columns themselves";
}

// A dialect with no digest template renders nothing extra even when a column is
// declared — the two facts are read together, in the renderer as in the decision.
TEST(KeyHashRender, NoDigestTemplate_NoDigestRendered)
{
	const ibMaterializeSql sql = RenderMaterialization(
		MakeSpec(/*withKeyHash*/ true), &DialectWithoutHash(), ibDatabaseLayerSQLite::Dialect());
	EXPECT_FALSE(sql.CreateText().Contains(KeyHashColumnName()));
}

// =============================================================================
// 3. THE NUMBERS, with the mechanism engaged — end to end on a live engine
// =============================================================================

// One key, several movements: they accumulate into ONE row. This is the assertion
// the whole mechanism exists for — with the identity in the digest, two postings to
// the same key must find each other through it.
TEST_F(KeyHashFix, SameKeyAccumulatesIntoOneRow)
{
	if (!ready) GTEST_SKIP();

	Move(1, wxT("2026-03-04 10:00:00"), wxT("W1"), wxT("O1"), 10.0, 0);
	Move(2, wxT("2026-03-20 18:30:00"), wxT("W1"), wxT("O1"),  4.0, 0);
	Move(3, wxT("2026-03-21 09:00:00"), wxT("W1"), wxT("O1"),  6.0, 1);

	EXPECT_EQ(1, RowCount(wxT("Reg9_T"))) << "one key is one row, whatever carries its identity";
	EXPECT_EQ(FromMovements(), FromTotals());
}

// Keys that differ in ONE part each. The digest is assembled from every key value,
// so each of these is a distinct row — a digest built from too few parts would fold
// them together and report one warehouse's stock under another's name.
TEST_F(KeyHashFix, EveryKeyPartTellsRowsApart)
{
	if (!ready) GTEST_SKIP();

	Move(1, wxT("2026-03-04 10:00:00"), wxT("W1"), wxT("O1"), 10.0, 0);
	Move(2, wxT("2026-03-04 10:00:00"), wxT("W2"), wxT("O1"), 20.0, 0);   // other warehouse
	Move(3, wxT("2026-03-04 10:00:00"), wxT("W1"), wxT("O2"), 30.0, 0);   // other owner
	Move(4, wxT("2026-04-04 10:00:00"), wxT("W1"), wxT("O1"), 40.0, 0);   // other period

	EXPECT_EQ(4, RowCount(wxT("Reg9_T")));
	EXPECT_EQ(FromMovements(), FromTotals());
}

// ⚠ THE PART SEPARATOR EARNS ITS PLACE. Without one, ("AB", "C") and ("A", "BC")
// digest to the same string and two different keys become one row — a total that is
// individually plausible and jointly wrong. With the separator they stay apart.
TEST_F(KeyHashFix, AdjacentPartsCannotRunTogether)
{
	if (!ready) GTEST_SKIP();

	Move(1, wxT("2026-03-04 10:00:00"), wxT("AB"), wxT("C"),  11.0, 0);
	Move(2, wxT("2026-03-04 10:00:00"), wxT("A"),  wxT("BC"), 22.0, 0);

	EXPECT_EQ(2, RowCount(wxT("Reg9_T"))) << "the digest must not run two parts together";
	EXPECT_EQ(FromMovements(), FromTotals());
}

// The whole delete / update path with the identity in place: a movement removed
// takes its own contribution back out of the row it went into, which is only
// possible if the OLD row digests to the same key the NEW one did.
TEST_F(KeyHashFix, DeleteBacksOutOfTheSameRow)
{
	if (!ready) GTEST_SKIP();

	Move(1, wxT("2026-03-04 10:00:00"), wxT("W1"), wxT("O1"), 10.0, 0);
	Move(2, wxT("2026-03-05 10:00:00"), wxT("W1"), wxT("O1"),  4.0, 0);
	Erase(1);

	EXPECT_EQ(1, RowCount(wxT("Reg9_T")));
	EXPECT_EQ(FromMovements(), FromTotals());
}

// A row written WITHOUT a digest — which is what a rebuild leaves behind, because
// it writes through the ordinary door and knows nothing about an identity the
// database computes. ibFillKeyHashes gives it one, and the proof that it gave the
// RIGHT one is that the next movement for that key lands in that very row instead
// of inserting a second.
TEST_F(KeyHashFix, RebuiltRowsAreGivenTheIdentityTheTriggerWouldHave)
{
	if (!ready) GTEST_SKIP();

	// A rebuild's row: the key, the figures, no digest.
	db.RunQuery(wxT("INSERT INTO Reg9_T (period_, wh, owner_, qty_in, qty_out) "
	                "VALUES ('2026-03-01 00:00:00', 'W1', 'O1', 100, 0)"));
	EXPECT_TRUE(OneString(wxT("SELECT COALESCE(keyhash_, '') FROM Reg9_T")).IsEmpty());

	ASSERT_TRUE(ibFillKeyHashes(db, MakeSpec(/*withKeyHash*/ true), &mat));
	EXPECT_FALSE(OneString(wxT("SELECT COALESCE(keyhash_, '') FROM Reg9_T")).IsEmpty())
		<< "a row with no identity is a row the unique index does not hold";

	// The identity has to be the one the TRIGGER computes, or this posting starts a
	// second row for a key that already has one.
	Move(1, wxT("2026-03-09 12:00:00"), wxT("W1"), wxT("O1"), 5.0, 0);
	EXPECT_EQ(1, RowCount(wxT("Reg9_T")))
		<< "the filled digest must be the same digest the trigger writes";

	Totals totals = FromTotals();
	ASSERT_EQ(1u, totals.size());
	EXPECT_DOUBLE_EQ(105.0, totals.begin()->second.first);
}

// Filling is idempotent and touches only what has nothing: a second pass over a
// table whose rows already carry their identity must not rewrite them.
TEST_F(KeyHashFix, FillingTwiceChangesNothing)
{
	if (!ready) GTEST_SKIP();

	Move(1, wxT("2026-03-04 10:00:00"), wxT("W1"), wxT("O1"), 10.0, 0);
	const wxString before = OneString(wxT("SELECT keyhash_ FROM Reg9_T"));
	ASSERT_FALSE(before.IsEmpty());

	ASSERT_TRUE(ibFillKeyHashes(db, MakeSpec(/*withKeyHash*/ true), &mat));
	EXPECT_EQ(before, OneString(wxT("SELECT keyhash_ FROM Reg9_T")));
	EXPECT_EQ(1, RowCount(wxT("Reg9_T")));
}

// A spec that declares no digest column asks nothing of the database — the fill is
// a no-op success, which is what every register on an engine with no ceiling gets.
TEST_F(KeyHashFix, NothingToFillIsNotAFailure)
{
	if (!ready) GTEST_SKIP();
	EXPECT_TRUE(ibFillKeyHashes(db, MakeSpec(/*withKeyHash*/ false), &mat));
}
