// =============================================================================
// OES Enterprise — ibMaterializationDialect tests
//
// The materialization dictionary (backend/databaseLayer/databaseLayer.h) is the
// SECOND per-driver dictionary: the facts needed to keep a register's totals
// table current by trigger. It exists separately from ibDialectDictionary
// because a trigger body diverges in STRUCTURE, not spelling — Firebird must
// MERGE where PostgreSQL and SQLite ON CONFLICT, and MySQL names the incoming
// value inline with no alias at all.
//
// The failure this file guards is the quiet one. A totals delta that REPLACES
// instead of ACCUMULATES still runs, still commits, and produces wrong balances
// that look plausible until someone reconciles a year. Firebird makes that
// mistake especially easy to make: its UPDATE OR INSERT .. MATCHING (the
// replace-upsert already in ibDialectDictionary) is syntactically valid in a
// trigger and silently wrong here. So the pins below are structural — they run
// against ANY dialect, which means a driver added later is checked the moment
// it is vended, without anyone remembering to extend this file.
//
// (docs/register-totals-strategy.md § Engine integration)
// =============================================================================

#include <gtest/gtest.h>

#include "backend/databaseLayer/databaseLayer.h"                 // ibMaterializationDialect
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"   // always built (embedded)

#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#endif
#ifdef OES_USE_MYSQL
#include "backend/databaseLayer/mysql/mysqlDatabaseLayer.h"
#endif

// ---------------------------------------------------------------------------
// Structural invariants — hold for EVERY dialect, present and future.
// ---------------------------------------------------------------------------

// A delta ACCUMULATES. Whatever the spelling — "t.c = t.c + s.c",
// "c = c + VALUES(c)" — the item must add. An item without a '+' is a replace,
// which is the silent-wrong-totals bug.
static void ExpectAccumulates(const ibMaterializationDialect& m, const char* who)
{
    SCOPED_TRACE(who);
    EXPECT_TRUE(m.m_deltaUpdateItem.Contains(wxT("+")))
        << "delta item must accumulate, not replace: " << m.m_deltaUpdateItem.ToStdString();
    EXPECT_TRUE(m.m_deltaUpdateItem.Contains(wxT("{col}")))
        << "delta item must be per-column";
}

// Every unit, on every vended dialect. Read granularity is a QUERY parameter —
// a consumer asks for weekly or ten-day or half-year rollups at will — so a
// dialect that covers only some units would make the answer depend on which
// engine the deployment happens to run. Partial coverage is worse than none:
// it turns a portable query into a portable-except-here query. An engine that
// cannot express one of these has no business maintaining totals.
//
// This also makes the enum self-enforcing: adding a unit to ibTotalsPeriod
// fails every driver's test until each closes it, which is the intended cost.
static void ExpectRendersEveryUnit(const ibDialectDictionary& q, const char* who)
{
    SCOPED_TRACE(who);
    static const ibTotalsPeriod kAll[] = {
        ibTotalsPeriod::Second,  ibTotalsPeriod::Minute,  ibTotalsPeriod::Hour,
        ibTotalsPeriod::Day,     ibTotalsPeriod::Week,    ibTotalsPeriod::TenDays,
        ibTotalsPeriod::Month,   ibTotalsPeriod::Quarter, ibTotalsPeriod::HalfYear,
        ibTotalsPeriod::Year,
    };
    for (const ibTotalsPeriod unit : kAll) {
        const auto it = q.m_periodTrunc.find(unit);
        EXPECT_NE(it, q.m_periodTrunc.end())
            << "unit " << static_cast<int>(unit) << " unsupported — coverage must be total";
    }
}

// Every truncation template, whichever units the engine supports, must consume
// {expr} — one that does not would silently key every totals row identically.
static void ExpectTruncationsConsumeExpr(const ibDialectDictionary& q, const char* who)
{
    SCOPED_TRACE(who);
    for (const auto& entry : q.m_periodTrunc) {
        EXPECT_TRUE(entry.second.Contains(wxT("{expr}")))
            << "truncation ignores its input: " << entry.second.ToStdString();
    }
}

// A dialect that needs a separate function object (PostgreSQL) must have a
// shell that actually calls it; otherwise the generator emits a function nobody
// invokes and a trigger with no body.
static void ExpectShellMatchesFunction(const ibMaterializationDialect& m, const char* who)
{
    SCOPED_TRACE(who);
    if (m.m_functionShellTemplate.IsEmpty()) {
        EXPECT_TRUE(m.m_triggerShellTemplate.Contains(wxT("{body}")))
            << "an inline dialect must place the body in its trigger shell";
    }
    else {
        EXPECT_TRUE(m.m_functionShellTemplate.Contains(wxT("{body}")))
            << "the function shell carries the body";
        EXPECT_TRUE(m.m_triggerShellTemplate.Contains(wxT("{function}")))
            << "the trigger must call the function it emitted";
    }
}

static void ExpectWellFormed(const ibMaterializationDialect& m, const ibDialectDictionary& q, const char* who)
{
    ExpectAccumulates(m, who);
    ExpectRendersEveryUnit(q, who);
    ExpectTruncationsConsumeExpr(q, who);
    ExpectShellMatchesFunction(m, who);
}

// ---------------------------------------------------------------------------
// The baseline (default-constructed) — the shape a new driver inherits.
// ---------------------------------------------------------------------------

TEST(MaterializationDialect, BaselineIsPerRowAndAccumulates) {
    ibMaterializationDialect m;
    EXPECT_EQ(m.m_family, ibTriggerFamily::PerRow);
    ExpectAccumulates(m, "baseline");
}

// The baseline QUERY dictionary carries no truncation entries: period granularity
// is engine arithmetic, and a default that silently applied one engine's syntax
// elsewhere is worse than none. A driver opts in explicitly.
//
// Note where the map lives — on the QUERY dictionary, not this one. Truncating a
// period is an ordinary declarative expression that any SELECT may want (a report
// grouping by month long before totals exist), so locking it inside the
// materialization dictionary would have shut a general fact in a private room.
TEST(MaterializationDialect, BaselineDeclaresNoPeriodUnits) {
    ibDialectDictionary q;
    EXPECT_TRUE(q.m_periodTrunc.empty());
}

// The reason this dictionary is separate from ibDialectDictionary, pinned: the
// query dictionary's upsert REPLACES, this one ADDS. If these ever converge,
// someone has folded an accumulate into a replace.
TEST(MaterializationDialect, DeltaUpsertDiffersFromTheQueryUpsert) {
    const ibDialectDictionary       q;
    const ibMaterializationDialect  m;
    EXPECT_NE(q.m_upsertTemplate, m.m_deltaUpsertTemplate);
    EXPECT_NE(q.m_upsertUpdateItem, m.m_deltaUpdateItem);
}

// ---------------------------------------------------------------------------
// SQLite — the per-row FLOOR (statements only, no procedural block). Always built.
// ---------------------------------------------------------------------------

TEST(MaterializationDialect, SqliteVendsAWellFormedDialect) {
    ibDatabaseLayerSQLite db;
    const ibMaterializationDialect* m = db.GetMaterializationDialect();
    ASSERT_NE(m, nullptr) << "SQLite is a totals target — presence IS the capability";
    ExpectWellFormed(*m, ibDatabaseLayerSQLite::Dialect(), "sqlite");
}

// The floor's defining property: the body inlines, so no engine in the family
// needs a procedural block to express a delta.
TEST(MaterializationDialect, SqliteInlinesTheBody) {
    const ibMaterializationDialect& m = ibDatabaseLayerSQLite::MaterializationDialect();
    EXPECT_TRUE(m.m_functionShellTemplate.IsEmpty());
    EXPECT_TRUE(m.m_triggerShellTemplate.Contains(wxT("{body}")));
}

// The units strftime cannot mask directly (Week / TenDays / Quarter / HalfYear)
// are date() arithmetic, not format strings. Pinned because the temptation is to
// drop them as "unsupported" — and partial coverage would make a query's answer
// depend on which engine the deployment runs.
TEST(MaterializationDialect, SqliteBuildsTheArithmeticUnits) {
    const ibDialectDictionary& m = ibDatabaseLayerSQLite::Dialect();
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::Quarter).Contains(wxT("months")));
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::HalfYear).Contains(wxT("months")));
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::TenDays).Contains(wxT("days")));
    // Monday-start, expressed as "back 6 days, then forward to the next Monday".
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::Week).Contains(wxT("weekday 1")));
}

// The ten-day offset must be CAPPED. Uncapped, day 31 floors to 3 and opens a
// fourth, one-day bucket — a silent extra totals row that no consumer expects.
// The last ten-day period being 8-11 days long is the definition, not a defect.
TEST(MaterializationDialect, SqliteCapsTheTenDayOffset) {
    const ibDialectDictionary& m = ibDatabaseLayerSQLite::Dialect();
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::TenDays).Contains(wxT("min(")));
}

// ---------------------------------------------------------------------------
// Firebird — the default embedded engine, and the structural outlier.
// ---------------------------------------------------------------------------

#ifdef OES_USE_FIREBIRD

TEST(MaterializationDialect, FirebirdVendsAWellFormedDialect) {
    const ibMaterializationDialect& m = ibDatabaseLayerFirebird::MaterializationDialect();
    ExpectWellFormed(m, ibDatabaseLayerFirebird::Dialect(), "firebird");
}

// THE pin of this file. Firebird's accumulate must be a MERGE: UPDATE OR INSERT
// .. MATCHING cannot read the target's current value, so it can only replace —
// valid SQL, silently wrong totals. ON CONFLICT is not Firebird syntax at all,
// so its appearance here would mean a PostgreSQL template was copied across.
TEST(MaterializationDialect, FirebirdAccumulatesViaMerge) {
    const ibMaterializationDialect& m = ibDatabaseLayerFirebird::MaterializationDialect();
    EXPECT_TRUE(m.m_deltaUpsertTemplate.Contains(wxT("MERGE")));
    EXPECT_FALSE(m.m_deltaUpsertTemplate.Contains(wxT("ON CONFLICT")));
    EXPECT_FALSE(m.m_deltaUpsertTemplate.Contains(wxT("UPDATE OR INSERT")));
    // MERGE is the form that spends the placeholders ON CONFLICT leaves unused.
    EXPECT_TRUE(m.m_deltaUpsertTemplate.Contains(wxT("{sourceRel}")));
    EXPECT_TRUE(m.m_deltaUpsertTemplate.Contains(wxT("{keyMatch}")));
    EXPECT_FALSE(m.m_deltaSourceTemplate.IsEmpty());
}

// Firebird names the table before the timing — the reshuffle a token
// substitution could not have expressed, which is why the shell is a template.
TEST(MaterializationDialect, FirebirdShellPutsTableBeforeTiming) {
    const ibMaterializationDialect& m = ibDatabaseLayerFirebird::MaterializationDialect();
    const int table  = m.m_triggerShellTemplate.Find(wxT("{table}"));
    const int timing = m.m_triggerShellTemplate.Find(wxT("{timing}"));
    ASSERT_NE(table,  wxNOT_FOUND);
    ASSERT_NE(timing, wxNOT_FOUND);
    EXPECT_LT(table, timing);
}

// Sub-day truncation on Firebird goes through SUBSTRING of the textual form, NOT
// EXTRACT + DATEADD. EXTRACT(SECOND) / EXTRACT(MILLISECOND) return fractional
// values there (100-microsecond resolution), so subtracting them back out
// invites rounding — and a period key that rounds occasionally lands in the
// wrong bucket. Cutting fixed character offsets is exact and total.
TEST(MaterializationDialect, FirebirdTruncatesSubDayTextually) {
    const ibDialectDictionary& m = ibDatabaseLayerFirebird::Dialect();
    for (const ibTotalsPeriod unit : { ibTotalsPeriod::Second, ibTotalsPeriod::Minute, ibTotalsPeriod::Hour }) {
        const wxString& tpl = m.m_periodTrunc.at(unit);
        EXPECT_TRUE(tpl.Contains(wxT("SUBSTRING")));
        EXPECT_FALSE(tpl.Contains(wxT("EXTRACT(SECOND")));
        EXPECT_FALSE(tpl.Contains(wxT("EXTRACT(MILLISECOND")));
    }
}

// Week must start Monday on every engine, or the same query answers differently
// per deployment. Firebird's EXTRACT(WEEKDAY) is 0=Sunday, so it needs the
// (wd + 6) mod 7 shift the other three get for free.
TEST(MaterializationDialect, FirebirdWeekStartsMonday) {
    const ibDialectDictionary& m = ibDatabaseLayerFirebird::Dialect();
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::Week).Contains(wxT("MOD(EXTRACT(WEEKDAY")));
}

// Same cap as SQLite, different spelling — MINVALUE. Day 31 must fold into the
// third ten-day period, not open a fourth.
TEST(MaterializationDialect, FirebirdCapsTheTenDayOffset) {
    const ibDialectDictionary& m = ibDatabaseLayerFirebird::Dialect();
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::TenDays).Contains(wxT("MINVALUE")));
}

#endif  // OES_USE_FIREBIRD

// ---------------------------------------------------------------------------
// PostgreSQL — production target; the one per-row engine needing two objects.
// ---------------------------------------------------------------------------

#ifdef OES_USE_POSTGRESQL

TEST(MaterializationDialect, PostgresVendsAWellFormedDialect) {
    const ibMaterializationDialect& m = ibDatabaseLayerPostgres::MaterializationDialect();
    ExpectWellFormed(m, ibDatabaseLayerPostgres::Dialect(), "postgres");
}

// PG cannot inline a trigger body — it needs a FUNCTION the trigger executes.
// ExpectShellMatchesFunction already ties the two together; this pins WHICH
// side of that fork PG is on, so a refactor cannot quietly inline it.
TEST(MaterializationDialect, PostgresNeedsASeparateFunction) {
    const ibMaterializationDialect& m = ibDatabaseLayerPostgres::MaterializationDialect();
    EXPECT_FALSE(m.m_functionShellTemplate.IsEmpty());
    EXPECT_TRUE(m.m_functionShellTemplate.Contains(wxT("plpgsql")));
}

// fillfactor is not a micro-tune here: the totals row is UPDATEd on nearly
// every movement, and free space on the page is what keeps that update HOT
// (same page, no index write) instead of making totals the write bottleneck.
TEST(MaterializationDialect, PostgresLeavesPageSpaceForHotUpdates) {
    const ibMaterializationDialect& m = ibDatabaseLayerPostgres::MaterializationDialect();
    EXPECT_TRUE(m.m_totalsTableSuffix.Contains(wxT("fillfactor")));
}

// Same ten-day cap as everywhere, spelled LEAST. Also guards against reaching
// for PG's own 'decade', which means ten YEARS and is a different thing entirely.
TEST(MaterializationDialect, PostgresCapsTheTenDayOffset) {
    const ibDialectDictionary& m = ibDatabaseLayerPostgres::Dialect();
    const wxString& tpl = m.m_periodTrunc.at(ibTotalsPeriod::TenDays);
    EXPECT_TRUE(tpl.Contains(wxT("LEAST")));
    EXPECT_FALSE(tpl.Contains(wxT("decade")));
}

#endif  // OES_USE_POSTGRESQL

// ---------------------------------------------------------------------------
// MySQL — the third accumulate spelling: no source alias anywhere.
// ---------------------------------------------------------------------------

#ifdef OES_USE_MYSQL

TEST(MaterializationDialect, MysqlVendsAWellFormedDialect) {
    const ibMaterializationDialect& m = ibDatabaseLayerMySQL::MaterializationDialect();
    ExpectWellFormed(m, ibDatabaseLayerMySQL::Dialect(), "mysql");
}

// The case that justifies m_deltaUpdateItem being a template rather than a
// pair of alias slots: ON DUPLICATE KEY UPDATE spells the incoming value
// inline as VALUES(col) and has no alias to name.
TEST(MaterializationDialect, MysqlAccumulatesWithoutASourceAlias) {
    const ibMaterializationDialect& m = ibDatabaseLayerMySQL::MaterializationDialect();
    EXPECT_TRUE(m.m_deltaUpdateItem.Contains(wxT("VALUES(")));
    EXPECT_FALSE(m.m_deltaUpdateItem.Contains(wxT("{source}")));
    EXPECT_FALSE(m.m_deltaUpdateItem.Contains(wxT("{target}")));
}

// A FROM-less SELECT cannot carry a WHERE in MySQL, and the conditional delta
// (an accounting register's absent side) is exactly a SELECT with a WHERE — so
// the dummy relation is load-bearing, not decoration.
TEST(MaterializationDialect, MysqlHasADummyRelationForGuardedDeltas) {
    EXPECT_EQ(ibDatabaseLayerMySQL::Dialect().m_selectFromDual, wxT("DUAL"));
}

// WEEKDAY() is 0=Monday (DAYOFWEEK() is 1=Sunday — the wrong one, and the easy
// mistake). Picking the wrong function shifts every weekly bucket by a day.
TEST(MaterializationDialect, MysqlWeekStartsMonday) {
    const ibDialectDictionary& m = ibDatabaseLayerMySQL::Dialect();
    const wxString& tpl = m.m_periodTrunc.at(ibTotalsPeriod::Week);
    EXPECT_TRUE(tpl.Contains(wxT("WEEKDAY(")));
    EXPECT_FALSE(tpl.Contains(wxT("DAYOFWEEK(")));
}

TEST(MaterializationDialect, MysqlCapsTheTenDayOffset) {
    const ibDialectDictionary& m = ibDatabaseLayerMySQL::Dialect();
    EXPECT_TRUE(m.m_periodTrunc.at(ibTotalsPeriod::TenDays).Contains(wxT("LEAST")));
}

#endif  // OES_USE_MYSQL
