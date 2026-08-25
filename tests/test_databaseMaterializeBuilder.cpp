// =============================================================================
// OES Enterprise — L2-2 materialization renderer
//
// RenderMaterialization is a pure function of (spec, dialect): it turns a
// declarative ibMaterializeSpec into the trigger / view statements that keep a
// register's totals current. It is the second half of level 2 — L2-1 renders a
// query IR into SELECT text, L2-2 renders a materialize spec into trigger text,
// and both are dialect-driven renderers that know nothing about metadata.
//
// Because it touches no connection and no metadata, every engine-shaped
// decision it makes is checkable by reading a string.
//
// What these tests guard is the class of bug that does not announce itself. A
// totals trigger that replaces instead of accumulates, that drops the sign on a
// delete, or that keys rows by an untruncated period still runs, still commits,
// and produces numbers that look plausible until someone reconciles a year of
// them. None of that surfaces in a smoke test; all of it surfaces here.
//
// (docs/register-totals-strategy.md §4/§4a)
// =============================================================================

#include <gtest/gtest.h>

#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/queryException.h"   // the READ half refuses a running form with no grain

namespace {

// The register shape these tests render: movements keyed by month + warehouse, with a
// receipt / expense pair per resource. Deliberately the BALANCE flavour — the signed one,
// where the delta's value branches on the record type — because it exercises the parts a
// turnover-only register would not.
//
// Note what the fixture does NOT need: no queryable, no schema snapshot, no metadata. The spec
// is plain names and SQL fragments because L2-2 is metadata-blind — the expansion of a logical
// column into its physical fields happened a floor above, in L3-2.
struct Fixture
{
    ibMaterializeSpec spec;

    Fixture()
    {
        spec.m_table            = wxT("Reg7_T");
        spec.m_source           = wxT("Reg7");
        // One view, composed from L2-2 PRIMITIVES. Note the vocabulary: the renderer is told
        // "difference" and "running sum", never "turnover" or "balance" — the aliases carry those
        // words because naming is metadata's business, and the layer under test never reads them.
        ibMaterializeView v;
        v.m_name          = wxT("Reg7_T_V");
        v.m_withPeriod    = true;
        v.m_dropZeroRows  = false;
        v.m_columns = {
            { wxT("Qty_Receipt"),        wxT("qty_in"),  wxString(),      ibMaterializeAgg::Value },
            { wxT("Qty_Expense"),        wxT("qty_out"), wxString(),      ibMaterializeAgg::Value },
            { wxT("Qty_Turnover"),       wxT("qty_in"),  wxT("qty_out"),  ibMaterializeAgg::Difference },
            { wxT("Qty_ClosingBalance"), wxT("qty_in"),  wxT("qty_out"),  ibMaterializeAgg::RunningSum },
            { wxT("Qty_OpeningBalance"), wxT("qty_in"),  wxT("qty_out"),  ibMaterializeAgg::RunningSumExcludingCurrent },
        };
        spec.m_views = { v };
        spec.m_keyColumns       = { wxT("wh") };
        spec.m_periodColumn     = wxT("period_");
        spec.m_periodSourceExpr = wxT("{row}.period_");
        spec.m_periodUnit       = ibTotalsPeriod::Month;
        spec.m_deltas = {
            { wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 0 THEN {row}.qty ELSE 0 END") },
            { wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 0 THEN 0 ELSE {row}.qty END") },
        };
    }
};

const ibMaterializationDialect& Sqlite() { return ibDatabaseLayerSQLite::MaterializationDialect(); }
const ibDialectDictionary&      SqliteQ() { return ibDatabaseLayerSQLite::Dialect(); }

ibMaterializeSql RenderSqlite(const ibMaterializeSpec& spec)
{
    return RenderMaterialization(spec, &Sqlite(), SqliteQ());
}

// Concatenate the CREATE statements — most assertions are about what the bundle as a whole
// does or never does, and the split between trigger objects is not the interesting part.
wxString CreateText(const ibMaterializeSql& sql) { return sql.CreateText(); }

int CountOf(const wxString& haystack, const wxString& needle)
{
    int n = 0;
    size_t at = 0;
    while ((at = haystack.find(needle, at)) != wxString::npos) { n++; at += needle.length(); }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// The bundle's shape
// ---------------------------------------------------------------------------

// Three write events, three triggers, one view. An engine that maintained totals on insert
// only would look correct in every test that never deletes a movement.
TEST(MaterializeRenderer, BuildsThreeTriggersAndAView) {
    Fixture f;
    const ibMaterializeSql sql = RenderSqlite(f.spec);
    const wxString text = CreateText(sql);
    EXPECT_EQ(CountOf(text, wxT("CREATE TRIGGER")), 3);
    EXPECT_EQ(CountOf(text, wxT("CREATE VIEW")), 1);
    EXPECT_TRUE(text.Contains(wxT("AFTER INSERT")));
    EXPECT_TRUE(text.Contains(wxT("AFTER UPDATE")));
    EXPECT_TRUE(text.Contains(wxT("AFTER DELETE")));
}

// The bundle is replaced whole on every apply, so the DROPs must cover everything the
// CREATEs make. A trigger left behind from a previous column set keeps writing the old
// columns — and keeps succeeding.
TEST(MaterializeRenderer, DropsEverythingItCreates) {
    Fixture f;
    const ibMaterializeSql sql = RenderSqlite(f.spec);
    const wxString drops = sql.DropText();
    EXPECT_EQ(CountOf(drops, wxT("DROP TRIGGER")), 3);
    EXPECT_TRUE(drops.Contains(wxT("DROP VIEW")));
}

// A register that lost its last resource has nothing left to accumulate — and that is exactly
// when a surviving trigger does the most damage, because the same apply drops the columns it
// writes. So the bundle still DROPS; it merely creates nothing.
TEST(MaterializeRenderer, NothingToAccumulateStillDropsTheOldBundle) {
    Fixture f;
    f.spec.m_deltas.clear();
    const ibMaterializeSql sql = RenderSqlite(f.spec);
    EXPECT_TRUE(sql.CreateText().IsEmpty());
    EXPECT_EQ(CountOf(sql.DropText(), wxT("DROP TRIGGER")), 3);
    EXPECT_TRUE(sql.DropText().Contains(wxT("DROP VIEW")));
}

// The drop names come from the SPEC, not from whatever is currently declared. This is what makes
// removing a VANISHED table's maintenance possible at all: a register whose kind switched keeps
// its totals in a differently-named table, so the old bundle can only be found through the old
// spec — and left behind, its triggers fire on a table that no longer exists.
TEST(MaterializeRenderer, DropsAreNamedAfterTheSpecTable) {
    Fixture f;
    f.spec.m_table = wxT("reg1_Tn");
    f.spec.m_views[0].m_name = wxT("reg1_Tn_Turnovers");
    const wxString drops = RenderSqlite(f.spec).DropText();
    EXPECT_TRUE(drops.Contains(wxT("reg1_Tn_AI")));
    EXPECT_TRUE(drops.Contains(wxT("reg1_Tn_AU")));
    EXPECT_TRUE(drops.Contains(wxT("reg1_Tn_AD")));
    EXPECT_TRUE(drops.Contains(wxT("reg1_Tn_Turnovers")));
}

// A driver with no materialization dialect (ODBC) yields an EMPTY bundle rather than an
// error: the register falls back to live aggregation, which is correct at any scale.
// Emptiness is a supported outcome, and the apply path relies on it being non-fatal.
TEST(MaterializeRenderer, NoDialectYieldsAnEmptyBundle) {
    Fixture f;
    const ibMaterializeSql sql = RenderMaterialization(f.spec, nullptr, SqliteQ());
    EXPECT_TRUE(sql.IsEmpty());
}

// ---------------------------------------------------------------------------
// The delta — where a silent wrong answer would come from
// ---------------------------------------------------------------------------

// Accumulate, never replace. This is the bug that produces plausible-looking totals:
// the last movement's value ends up standing for the whole period.
TEST(MaterializeRenderer, AccumulatesRatherThanReplaces) {
    Fixture f;
    const wxString text = CreateText(RenderSqlite(f.spec));
    EXPECT_TRUE(text.Contains(wxT("qty_in = Reg7_T.qty_in + excluded.qty_in")));
    EXPECT_TRUE(text.Contains(wxT("qty_out = Reg7_T.qty_out + excluded.qty_out")));
}

// A delete must REVERSE the movement. The sign rides in the VALUE (a negated contribution),
// never in the operator — so the accumulate stays '+' in all three triggers and no engine
// needs a second template. Checking the negation is checking that deleting a document
// actually gives the stock back.
TEST(MaterializeRenderer, DeleteContributesTheNegatedValue) {
    Fixture f;
    const ibMaterializeSql sql = RenderSqlite(f.spec);
    // The delete trigger is the third created object (insert, update, delete).
    const wxString del = sql.CreateAt(2);
    EXPECT_TRUE(del.Contains(wxT("AFTER DELETE")));
    EXPECT_TRUE(del.Contains(wxT("-(")));            // negated contribution
    EXPECT_TRUE(del.Contains(wxT("OLD.")));          // read from the row being removed
    EXPECT_FALSE(del.Contains(wxT("NEW.")));         // ...and only from it
}

// An update is a reversal plus an application, in that order. Emitting only the NEW side is
// the subtle version of the delete bug: edits leak value into the totals.
TEST(MaterializeRenderer, UpdateReversesOldThenAppliesNew) {
    Fixture f;
    const wxString upd = RenderSqlite(f.spec).CreateAt(1);
    EXPECT_TRUE(upd.Contains(wxT("AFTER UPDATE")));
    EXPECT_TRUE(upd.Contains(wxT("OLD.")));
    EXPECT_TRUE(upd.Contains(wxT("NEW.")));
    EXPECT_LT(upd.Find(wxT("OLD.")), upd.Find(wxT("NEW.")));   // reversal first
}

// The period must be TRUNCATED into the key, not stored raw. Storing the raw timestamp
// silently turns the totals table into a copy of the movements — one row per movement,
// every read still "correct", and the entire point of the table gone.
TEST(MaterializeRenderer, TruncatesThePeriodIntoTheKey) {
    Fixture f;
    const wxString text = CreateText(RenderSqlite(f.spec));
    EXPECT_TRUE(text.Contains(wxT("strftime('%Y-%m-01 00:00:00', NEW.period_)")));
    EXPECT_FALSE(text.Contains(wxT("(NEW.period_,")));   // never the bare column as the key value
}

// A guard renders as a WHERE on the delta's SELECT — the accounting register's case, where
// a movement's debit and credit sides feed different totals tables and either may be absent.
TEST(MaterializeRenderer, GuardBecomesAWhereOnTheDelta) {
    Fixture f;
    f.spec.m_guard = wxT("{row}.wh IS NOT NULL");
    const wxString text = CreateText(RenderSqlite(f.spec));
    EXPECT_TRUE(text.Contains(wxT("WHERE NEW.wh IS NOT NULL")));
    EXPECT_TRUE(text.Contains(wxT("WHERE OLD.wh IS NOT NULL")));
}

// Without a guard there must be no dangling WHERE — the unconditional case pays nothing.
TEST(MaterializeRenderer, NoGuardEmitsNoWhere) {
    Fixture f;
    EXPECT_FALSE(CreateText(RenderSqlite(f.spec)).Contains(wxT("WHERE")));
}

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------

// The view is a projection of the totals table, NOT a re-aggregation of the movements. A
// fat live-aggregation view would JOIN just as well and recompute on every read, leaving
// the scale ceiling exactly where it was — the failure would be invisible until volume.
TEST(MaterializeRenderer, ViewReadsTheTotalsTableNotTheMovements) {
    Fixture f;
    const wxString view = RenderSqlite(f.spec).LastCreate();
    EXPECT_TRUE(view.Contains(wxT("CREATE VIEW")));
    EXPECT_TRUE(view.Contains(wxT("FROM Reg7_T")));
    EXPECT_FALSE(view.Contains(wxT("FROM Reg7 ")));   // never the movement table
}

// Coarser units are exposed as their own columns, so one query can group by month or
// quarter without a second source. Finer ones must NOT appear: the information does not
// exist below the stored grain, and a column that silently repeated the month under the
// name "day" would be worse than its absence.
TEST(MaterializeRenderer, ViewProjectsCoarserPeriodsOnly) {
    Fixture f;
    const wxString view = RenderSqlite(f.spec).LastCreate();
    EXPECT_TRUE(view.Contains(wxT("AS period__Quarter")));
    EXPECT_TRUE(view.Contains(wxT("AS period__HalfYear")));
    EXPECT_TRUE(view.Contains(wxT("AS period__Year")));
    EXPECT_FALSE(view.Contains(wxT("period__Day")));
    EXPECT_FALSE(view.Contains(wxT("period__Month")));   // equal to stored — the column already IS it
}

// ---------------------------------------------------------------------------
// Split totals
// ---------------------------------------------------------------------------

// Unsplit and period-carrying: nothing MERGES, so there must be no GROUP BY and no shard
// column. The table already holds one row per key, and grouping it would cost the engine a
// sort or a hash on every read while changing no answer.
//
// Window functions ARE present — the running balance is one — and that is not aggregation
// in this sense: a window adds a column without collapsing rows.
TEST(MaterializeRenderer, UnsplitViewDoesNotCollapseRows) {
    Fixture f;
    const wxString view = RenderSqlite(f.spec).LastCreate();
    EXPECT_FALSE(view.Contains(wxT("GROUP BY")));
    EXPECT_FALSE(view.Contains(ShardColumnName()));
    EXPECT_TRUE(view.Contains(wxT("OVER (PARTITION BY")));   // the window still runs
}

// A view WITHOUT the period merges every period into one row per key — so it must group and
// must sum, even with no shards in play. Getting this wrong is subtle and severe: the view
// would report whichever single period the engine happened to return, and the number would
// look like a plausible balance.
TEST(MaterializeRenderer, PeriodlessViewCollapsesAndSums) {
    Fixture f;
    ibMaterializeView v;
    v.m_name         = wxT("Reg7_Balance");
    v.m_withPeriod   = false;
    v.m_dropZeroRows = true;
    v.m_columns = { { wxT("Qty_Balance"), wxT("qty_in"), wxT("qty_out"), ibMaterializeAgg::Difference } };
    f.spec.m_views = { v };

    const wxString view = RenderSqlite(f.spec).LastCreate();
    EXPECT_TRUE(view.Contains(wxT("SUM(qty_in)")));
    EXPECT_TRUE(view.Contains(wxT("SUM(qty_out)")));
    EXPECT_TRUE(view.Contains(wxT("GROUP BY")));
    EXPECT_TRUE(view.Contains(wxT("HAVING")));               // zero balance = NO ROW
    EXPECT_FALSE(view.Contains(wxT("period_")));             // no period anywhere, not even projections
}

// The running balance must be computed BY THE DATABASE. Carrying every period's row out to a
// client to accumulate there turns a report into a transfer, and the cost grows with history.
TEST(MaterializeRenderer, RunningBalanceUsesAWindow) {
    Fixture f;
    const wxString view = RenderSqlite(f.spec).LastCreate();
    EXPECT_TRUE(view.Contains(wxT("AS Qty_ClosingBalance")));
    EXPECT_TRUE(view.Contains(wxT("AS Qty_OpeningBalance")));
    EXPECT_TRUE(view.Contains(wxT("ORDER BY period_")));      // ordered within the partition
    // Opening is the running sum MINUS this row's own contribution — one window, no self-join.
    EXPECT_TRUE(view.Contains(wxT(") - (qty_in - qty_out))")));
}

// SQLite has no connection-id expression, and that is a considered answer rather than a
// gap: a single-writer engine has no write contention, so a split would be pure read tax.
// The request collapses to one shard instead of failing.
TEST(MaterializeRenderer, SplitCollapsesWhereItIsMeaningless) {
    Fixture f;
    f.spec.m_shards = 8;
    EXPECT_EQ(EffectiveShardCount(f.spec, &Sqlite()), 1u);
    const wxString view = RenderSqlite(f.spec).LastCreate();
    EXPECT_FALSE(view.Contains(wxT("GROUP BY")));
}

#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"

// Where splitting IS meaningful, the view absorbs it — sums the shards and groups them
// away — so nothing above L3 can tell a split register from a plain one. If the shard
// column leaked into the projection, a consumer could group by it and read partial sums.
TEST(MaterializeRenderer, SplitIsAbsorbedByTheView) {
    Fixture f;
    f.spec.m_shards = 4;
    const ibMaterializationDialect& pg = ibDatabaseLayerPostgres::MaterializationDialect();
    EXPECT_EQ(EffectiveShardCount(f.spec, &pg), 4u);

    const ibMaterializeSql sql = RenderMaterialization(
        f.spec, &pg, ibDatabaseLayerPostgres::Dialect());
    const wxString view = sql.LastCreate();
    EXPECT_TRUE(view.Contains(wxT("SUM(qty_in) AS Qty_Receipt")));    // shards folded away
    EXPECT_TRUE(view.Contains(wxT("GROUP BY")));
    EXPECT_FALSE(view.Contains(ShardColumnName()));   // never projected — grouping by it would expose partial sums

    // ...while the trigger DOES write it, chosen from the local connection id so concurrent
    // writers land on different rows without any shared counter to contend on.
    EXPECT_TRUE(CreateText(sql).Contains(ShardColumnName()));
    EXPECT_TRUE(CreateText(sql).Contains(wxT("pg_backend_pid()")));
}

// PostgreSQL cannot inline a trigger body: each trigger needs a function, and the bundle
// must drop those too or every restructure orphans one more in the schema.
TEST(MaterializeRenderer, PostgresEmitsAndDropsItsFunctions) {
    Fixture f;
    const ibMaterializeSql sql = RenderMaterialization(
        f.spec, &ibDatabaseLayerPostgres::MaterializationDialect(), ibDatabaseLayerPostgres::Dialect());
    EXPECT_EQ(CountOf(CreateText(sql), wxT("CREATE OR REPLACE FUNCTION")), 3);
    const wxString drops = sql.DropText();
    EXPECT_EQ(CountOf(drops, wxT("DROP FUNCTION")), 3);
}
#endif  // OES_USE_POSTGRESQL

#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"

// Firebird accumulates through MERGE, and the renderer must actually spend the placeholders
// that form requires. Rendering an ON CONFLICT body here would be valid-looking output that
// no Firebird server accepts — and rendering UPDATE OR INSERT would be accepted and wrong.
TEST(MaterializeRenderer, FirebirdRendersAMergeDelta) {
    Fixture f;
    const wxString text = CreateText(RenderMaterialization(
        f.spec, &ibDatabaseLayerFirebird::MaterializationDialect(), ibDatabaseLayerFirebird::Dialect()));
    EXPECT_TRUE(text.Contains(wxT("MERGE INTO Reg7_T t")));
    EXPECT_TRUE(text.Contains(wxT("WHEN MATCHED THEN UPDATE SET")));
    EXPECT_TRUE(text.Contains(wxT("qty_in = t.qty_in + s.qty_in")));   // FB rejects a qualified column LEFT of SET
    EXPECT_FALSE(text.Contains(wxT("ON CONFLICT")));
    EXPECT_FALSE(text.Contains(wxT("UPDATE OR INSERT")));
    // No placeholder may survive into emitted SQL.
    EXPECT_FALSE(text.Contains(wxT("{")));
}
#endif  // OES_USE_FIREBIRD

// No placeholder survives rendering on the always-built engine either — an unreplaced
// {row} / {table} would reach the server verbatim and fail at apply time.
TEST(MaterializeRenderer, LeavesNoPlaceholdersBehind) {
    Fixture f;
    f.spec.m_guard = wxT("{row}.wh IS NOT NULL");
    const ibMaterializeSql sql = RenderSqlite(f.spec);
    const wxString all = CreateText(sql) + sql.DropText();
    EXPECT_FALSE(all.Contains(wxT("{")));
    EXPECT_FALSE(all.Contains(wxT("}")));
}

// =============================================================================
// THE MOVEMENT ARM — a view that carries what the totals do not hold yet.
//
// A maintained total is complete only down to the grain it is stored at. Everything inside the
// current grain lives in the movements, so the view offers both halves as ONE relation and the
// reader cuts between them. What these tests guard is the half that cannot announce itself: an arm
// rendered with a different column list, or a movement whose contribution is computed by a second
// expression that has quietly drifted from the trigger's.
// =============================================================================

TEST(MaterializeRenderer, WithoutTheArmTheViewReadsOnlyTheStoredTable) {
    Fixture f;
    const wxString text = CreateText(RenderSqlite(f.spec));
    EXPECT_FALSE(text.Contains(wxT("UNION ALL")));
}

TEST(MaterializeRenderer, TheMovementArmIsAUnionOverTheSourceTable) {
    Fixture f;
    f.spec.m_views[0].m_withMovements = true;
    const wxString text = CreateText(RenderSqlite(f.spec));

    EXPECT_TRUE(text.Contains(wxT("UNION ALL")));
    EXPECT_TRUE(text.Contains(wxT(" FROM Reg7")));      // the movements
    EXPECT_TRUE(text.Contains(wxT(" FROM Reg7_T")));    // and the totals, still
}

// ⭐ THE CONTRIBUTION IS THE TRIGGER'S OWN EXPRESSION, not a second one that means the same today.
// The arm computes each figure from spec.m_deltas — so a movement counted in the tail and the same
// movement counted into the total are the same arithmetic BY CONSTRUCTION.
TEST(MaterializeRenderer, TheArmComputesFiguresFromTheDeltaExpressions) {
    Fixture f;
    f.spec.m_views[0].m_withMovements = true;
    const wxString text = CreateText(RenderSqlite(f.spec));

    // The delta's own CASE, now over the source table rather than over {row}.
    EXPECT_TRUE(text.Contains(wxT("CASE WHEN Reg7.rectype_ = 0 THEN Reg7.qty ELSE 0 END")));
    EXPECT_TRUE(text.Contains(wxT("CASE WHEN Reg7.rectype_ = 0 THEN 0 ELSE Reg7.qty END")));
    EXPECT_FALSE(text.Contains(wxT("{row}")));
}

// The period on the movement arm is the RAW instant: truncating it there would answer at the very
// grain the arm exists to go below.
TEST(MaterializeRenderer, TheArmKeepsTheRawInstantAsItsPeriod) {
    Fixture f;
    f.spec.m_views[0].m_withMovements = true;
    const wxString text = CreateText(RenderSqlite(f.spec));
    EXPECT_TRUE(text.Contains(wxT("Reg7.period_ AS period_")));
}

// A movement-only column is NULL on the stored arm — which is also how a reader tells the two
// apart, with no flag column invented for it.
TEST(MaterializeRenderer, AMovementOnlyColumnIsNullOnTheStoredArm) {
    Fixture f;
    f.spec.m_views[0].m_withMovements   = true;
    // The type travels with the name: the stored arm stands a CAST null in its place, because a bare
    // NULL has no type for the engine to reconcile the two arms with (see ibMaterializeView).
    f.spec.m_views[0].m_movementColumns = { { wxT("recorder_RRRef"), ibTypeBinary(16) } };
    const wxString text = CreateText(RenderSqlite(f.spec));

    EXPECT_TRUE(text.Contains(wxT("CAST(NULL AS BLOB) AS recorder_RRRef")));
    EXPECT_TRUE(text.Contains(wxT("Reg7.recorder_RRRef")));
}

// =============================================================================
// A ROW WITH NOTHING TO REPORT IS NOT A ROW — and this is what a reversal looks like.
//
// A storno is a movement entered with the sign turned round: receipt +10, then receipt -10. It does
// not ADD to the other side (that would grow the turnover by 20 and report two events); it reduces
// the figure it belongs to, and the key folds back to nothing. What must not survive is a line of
// zeros claiming something happened -- the figure is affected, and to SEE that it happened the
// reader expands to the recorder and the line number.
//
// The rule is "ANY figure non-zero", not "the net is zero": receipt 10 against expense 10 nets to
// zero and MUST stay, because something did happen there.
// =============================================================================

TEST(MaterializeRenderer, ARowSurvivesIfAnyFigureIsNonZero) {
    Fixture f;
    f.spec.m_views[0].m_dropZeroRows = true;
    const wxString text = CreateText(RenderSqlite(f.spec));

    // OR, never AND: an item with quantity 0 and amount 5 still reports.
    EXPECT_TRUE(text.Contains(wxT(" OR ")));
    EXPECT_TRUE(text.Contains(wxT("<> 0")));
    // One test per reportable figure -- the plain sums and the difference.
    EXPECT_EQ(3, CountOf(text, wxT("<> 0")));
}

TEST(MaterializeRenderer, NoZeroRowTestIsEmittedWhenNotAskedFor) {
    Fixture f;   // m_dropZeroRows is false in the fixture
    const wxString text = CreateText(RenderSqlite(f.spec));
    EXPECT_FALSE(text.Contains(wxT("<> 0")));
}

// A WINDOWED FIGURE CANNOT BE TESTED HERE -- a window may not appear in HAVING, and a running
// balance is one. Counting it in would make the whole bundle unloadable rather than merely wrong.
TEST(MaterializeRenderer, TheRunningBalanceIsLeftOutOfTheZeroRowTest) {
    Fixture f;
    f.spec.m_views[0].m_dropZeroRows = true;
    const wxString text = CreateText(RenderSqlite(f.spec));

    const int windows = CountOf(text, wxT("OVER ("));
    EXPECT_GT(windows, 0);                        // the running sums are there...
    EXPECT_FALSE(text.Contains(wxT(") <> 0 OR SUM(qty_in - qty_out) OVER")));   // ...and not in the test
}

// ⭐ THE FRAME IS WRITTEN OUT, not left to the engine (2026-08-20, when the clause moved onto L2-1's
// ibRenderOverClause). All three engines default an ordered window to exactly this, so the SQL means
// what it always meant -- and that is the point: an unwritten default is a decision nobody can find
// later, and a fourth engine defaulting differently would move balances without changing any code.
//
// RANGE, never ROWS, for a stored total: movements sharing one period are one period's worth of
// stock, so the closing figure must not depend on the order they come back in.
TEST(MaterializeRenderer, TheRunningBalanceNamesItsFrame) {
    Fixture f;
    const wxString text = CreateText(RenderSqlite(f.spec));

    EXPECT_TRUE(text.Contains(wxT("RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW")));
    EXPECT_FALSE(text.Contains(wxT("ROWS BETWEEN")));
}

// Grouped, the test is a HAVING; ungrouped, the same test is a WHERE. One rule, and the clause it
// belongs in follows from whether anything actually merged.
TEST(MaterializeRenderer, TheZeroRowTestGoesWhereTheGroupingPutIt) {
    Fixture grouped;
    grouped.spec.m_views[0].m_dropZeroRows = true;
    grouped.spec.m_views[0].m_withPeriod   = false;   // periodless collapses -- see PeriodlessViewCollapsesAndSums
    EXPECT_TRUE(CreateText(RenderSqlite(grouped.spec)).Contains(wxT("HAVING ")));

    Fixture flat;
    flat.spec.m_views[0].m_dropZeroRows = true;       // unsplit + per period: nothing merges
    const wxString text = CreateText(RenderSqlite(flat.spec));
    EXPECT_TRUE (text.Contains(wxT("WHERE ")));
    EXPECT_FALSE(text.Contains(wxT("HAVING ")));
}

// =============================================================================
// ibNextPeriodStart — where a stored row stops covering.
//
// The twin of ibTruncateToPeriod, and the reason a reading whose lower boundary falls INSIDE a
// grain cannot take that grain's stored row. Both walk the calendar rather than adding fixed
// lengths, because months differ in length and the ten-day bucket that ends a month is not ten
// days long. An off-by-one here loses or duplicates a whole grain of movements.
// =============================================================================

TEST(TotalsPeriod, NextDayStartIsTheFollowingMidnight) {
    const wxDateTime noon(15, wxDateTime::Mar, 2026, 12, 30, 5);
    EXPECT_EQ(wxDateTime(16, wxDateTime::Mar, 2026),
              ibNextPeriodStart(noon, ibTotalsPeriod::Day));
}

TEST(TotalsPeriod, NextMonthStartCrossesAMonthOfAnyLength) {
    EXPECT_EQ(wxDateTime(1, wxDateTime::Mar, 2026),
              ibNextPeriodStart(wxDateTime(28, wxDateTime::Feb, 2026, 23, 59, 59), ibTotalsPeriod::Month));
    EXPECT_EQ(wxDateTime(1, wxDateTime::Feb, 2026),
              ibNextPeriodStart(wxDateTime(31, wxDateTime::Jan, 2026), ibTotalsPeriod::Month));
}

// The third ten-day bucket runs to the END of the month, so what follows it is the 1st of the next
// month — not "ten days later", which would open a fourth bucket the truncation never produces.
TEST(TotalsPeriod, TheLastTenDayBucketIsFollowedByTheNextMonth) {
    EXPECT_EQ(wxDateTime(1, wxDateTime::Feb, 2026),
              ibNextPeriodStart(wxDateTime(31, wxDateTime::Jan, 2026), ibTotalsPeriod::TenDays));
    EXPECT_EQ(wxDateTime(11, wxDateTime::Jan, 2026),
              ibNextPeriodStart(wxDateTime(5, wxDateTime::Jan, 2026), ibTotalsPeriod::TenDays));
    EXPECT_EQ(wxDateTime(21, wxDateTime::Jan, 2026),
              ibNextPeriodStart(wxDateTime(20, wxDateTime::Jan, 2026), ibTotalsPeriod::TenDays));
}

// A moment already ON a grain edge still moves to the NEXT one: the reading that asks for it wants
// the grain it starts, not the one it ends.
TEST(TotalsPeriod, AMomentOnTheEdgeStillMovesForward) {
    const wxDateTime midnight(3, wxDateTime::Apr, 2026);
    EXPECT_EQ(midnight, ibTruncateToPeriod(midnight, ibTotalsPeriod::Day));
    EXPECT_EQ(wxDateTime(4, wxDateTime::Apr, 2026), ibNextPeriodStart(midnight, ibTotalsPeriod::Day));
}

TEST(TotalsPeriod, AnInvalidMomentStaysInvalid) {
    EXPECT_FALSE(ibNextPeriodStart(wxDateTime(), ibTotalsPeriod::Day).IsValid());
}

// ---------------------------------------------------------------------------
// The READ half — RenderMaterializedRead
// ---------------------------------------------------------------------------
//
// Until 2026-08-20 this half had no tests at all, and it is the half a report's numbers come out
// of. The periodised reading below is the one that used to be impossible on the server: each period
// opens where the previous one closed, which no combination of conditional sums expresses.

namespace {

// A balance-and-turnovers read over the turnovers surface: one key, one resource, three figures.
ibMaterializeReadSpec ReadFixture()
{
    ibMaterializeReadSpec r;
    r.m_view         = wxT("Reg7_T_V");
    r.m_periodColumn = wxT("period_");
    r.m_keyColumns   = { wxT("wh") };
    r.m_to           = ibValue(2026, 3, 31);
    r.m_from         = ibValue(2026, 1, 1);
    return r;
}

wxString ReadSql(const ibMaterializeReadSpec& spec)
{
    // Wrapped in a Project so the subquery renders where a caller would actually put it — in a FROM.
    const ibQueryRelPtr rel = RenderMaterializedRead(spec, wxT("src"));
    return ibQueryRenderer(SqliteQ()).Render(ibQueryIR(ibProject(rel))).m_sql;
}

} // namespace

// The unperiodised road, unchanged: three conditions over one scan, no window anywhere. Pinned
// because the periodised road was added beside it and the two share every line of the builder.
TEST(MaterializeRead, WholeIntervalStaysConditionalSums) {
    ibMaterializeReadSpec r = ReadFixture();
    r.m_columns = {
        { wxT("Qty_OpeningBalance"), wxT("qty_turn"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, true },
        { wxT("Qty_Turnover"),       wxT("qty_turn"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true },
        { wxT("Qty_ClosingBalance"), wxT("qty_turn"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo,     true },
    };
    const wxString sql = ReadSql(r);

    EXPECT_TRUE(sql.Contains(wxT("CASE")));           // the conditions
    EXPECT_TRUE(sql.Contains(wxT("GROUP BY wh")));    // one row per key
    EXPECT_FALSE(sql.Contains(wxT("OVER (")));        // and nothing accumulates
}

// ⭐ THE READING THAT USED TO FALL INTO RAM. Grouped by the month, the two balances are windows over
// the period sums, and the opening one is the closing one minus this period's own movement.
TEST(MaterializeRead, PeriodisedBalanceAccumulatesOnTheServer) {
    ibMaterializeReadSpec r = ReadFixture();
    r.m_grain      = ibMaterializeGrain::Calendar;
    r.m_periodUnit = ibTotalsPeriod::Month;
    r.m_fromGrain  = ibValue(2026, 1, 1);
    r.m_columns = {
        { wxT("Qty_OpeningBalance"), wxT("qty_turn"), wxString(), ibMaterializeAgg::RunningSumExcludingCurrent, ibMaterializeWhen::Always, true },
        { wxT("Qty_Turnover"),       wxT("qty_turn"), wxString(), ibMaterializeAgg::Value,                      ibMaterializeWhen::Always, true },
        { wxT("Qty_ClosingBalance"), wxT("qty_turn"), wxString(), ibMaterializeAgg::RunningSum,                 ibMaterializeWhen::Always, true },
    };
    const wxString sql = ReadSql(r);

    // The accumulation: a sum of the period sums, partitioned by the key, ordered by the grain.
    EXPECT_TRUE(sql.Contains(wxT("SUM(CAST(SUM(")));
    EXPECT_TRUE(sql.Contains(wxT("OVER (PARTITION BY wh ORDER BY")));
    // RANGE, so two movements stamped with one period cannot close at two different figures.
    EXPECT_TRUE(sql.Contains(wxT("RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW")));
    // The period joined the keys.
    EXPECT_TRUE(sql.Contains(wxT("GROUP BY wh")));
    // Opening = closing minus this period's own movement — one window, not two.
    EXPECT_TRUE(sql.Contains(wxT(") - CAST(SUM(")));
}

// The lower bound is applied AFTER the window and against the GRAIN. Applied before it, the first
// period's opening balance would be zero — the accumulation that produces it lives in the periods
// the answer never shows.
TEST(MaterializeRead, PeriodisedLowerBoundIsAppliedOutsideTheWindow) {
    ibMaterializeReadSpec r = ReadFixture();
    r.m_grain      = ibMaterializeGrain::Calendar;
    r.m_periodUnit = ibTotalsPeriod::Month;
    r.m_fromGrain  = ibValue(2026, 1, 1);
    r.m_columns = {
        { wxT("Qty_ClosingBalance"), wxT("qty_turn"), wxString(), ibMaterializeAgg::RunningSum, ibMaterializeWhen::Always, true },
    };
    const wxString sql = ReadSql(r);

    // An outer layer exists, and the >= test lives in it — after the aggregation, not in its WHERE.
    const size_t window = sql.find(wxT("OVER ("));
    const size_t lower  = sql.rfind(wxT("period_ >="));
    ASSERT_NE(window, wxString::npos);
    ASSERT_NE(lower,  wxString::npos);
    EXPECT_GT(lower, window);
}

// ⭐ THE OTHER PERIODISED READING, AND IT IS THE PLAIN ONE. A turnover looks at no row outside its
// own period, so breaking the interval into months costs a GROUP BY and nothing more — no window,
// no accumulation, nothing an engine might not have. That is why routing this reading by "was a
// periodicity asked for" was the wrong question: it sent the cheap case to RAM and kept the
// expensive one, the running balance above, on the server.
TEST(MaterializeRead, PeriodisedTurnoverIsAGroupByAndNoWindow) {
    ibMaterializeReadSpec r = ReadFixture();
    r.m_grain        = ibMaterializeGrain::Calendar;
    r.m_periodUnit   = ibTotalsPeriod::Month;
    r.m_dropZeroRows = true;
    r.m_columns = {
        { wxT("Qty_Receipt"),  wxT("qty_in"),   wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
        { wxT("Qty_Expense"),  wxT("qty_out"),  wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
        { wxT("Qty_Turnover"), wxT("qty_turn"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
    };
    const wxString sql = ReadSql(r);

    // The SQL travels with every failure: a shape test that only says "false" makes the reader
    // guess which of the four clauses moved.
    const std::string shown = sql.ToStdString();
    EXPECT_FALSE(sql.Contains(wxT("OVER (")))          << shown;   // nothing accumulates across periods
    EXPECT_TRUE(sql.Contains(wxT("GROUP BY wh")))      << shown;   // the key...
    EXPECT_TRUE(sql.Contains(wxT("strftime('%Y-%m-01")))<< shown;  // ...and the month beside it
    // An all-zero period is not a turnover — and the test is written against `<> ?` rather than
    // `<> 0` because a READ binds its constants: the zero travels as a parameter. (The view-side
    // test next door does say `<> 0` — a view's text has nowhere to bind, so it inlines. Two
    // spellings of one rule, and the difference is which tier writes the statement.)
    EXPECT_TRUE(sql.Contains(wxT("Qty_Receipt <> ?")))  << shown;
    EXPECT_TRUE(sql.Contains(wxT(" OR ")))              << shown;   // ANY figure non-zero, never all
    // …and it is tested OUTSIDE the grouping, over the finished rows: the periodised read has an
    // outer layer, and that is where a figure is an ordinary column.
    EXPECT_TRUE(sql.Contains(wxT("_acc")))              << shown;
    // The figures are SUMMED — the fold this whole reading exists for.
    EXPECT_TRUE(sql.Contains(wxT("CAST(SUM(")))         << shown;
}

// ⭐ AND WITH NO PERIODICITY IT IS ONE ROW PER KEY. The surface stores a row per period, so a read
// that does not fold reports the interval spread across however many periods it spans — four months
// of one key as four rows, each holding a quarter of the figure, and nothing raised. The RAM oracle
// (ComputeTurnover) has always grouped and summed; this is the same answer from the other road.
TEST(MaterializeRead, AnUnperiodisedTurnoverFoldsTheWholeInterval) {
    ibMaterializeReadSpec r = ReadFixture();   // m_grain stays Whole
    r.m_columns = {
        { wxT("Qty_Turnover"), wxT("qty_turn"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
    };
    const wxString sql = ReadSql(r);

    EXPECT_TRUE(sql.Contains(wxT("SUM(")));
    EXPECT_TRUE(sql.Contains(wxT("GROUP BY wh")));
    EXPECT_FALSE(sql.Contains(wxT("OVER (")));
    // No period column in the answer either: a reading with no periodicity carries no date, and the
    // field gate (ibRegisterFoldOffersColumn) tells the query's author exactly the same thing.
    EXPECT_FALSE(sql.Contains(wxT("AS period_")));
}

// 🛑 A running form in a reading that reports no periods is refused, not read as a plain sum. Read
// that way it would return the interval's total under the name of an opening balance.
TEST(MaterializeRead, RunningFormWithoutAGrainIsRefused) {
    ibMaterializeReadSpec r = ReadFixture();   // m_grain stays Whole
    r.m_columns = {
        { wxT("Qty_OpeningBalance"), wxT("qty_turn"), wxString(), ibMaterializeAgg::RunningSum, ibMaterializeWhen::Always, true },
    };
    EXPECT_THROW(ReadSql(r), ibBackendQueryException);
}
