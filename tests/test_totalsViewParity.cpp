// =============================================================================
// OES Enterprise — totals views: shape parity against the live aggregation
//
// Two roads lead to the same numbers. The LIVE path scans the movements and
// aggregates them on every read; the MATERIALISED path reads a trigger-kept
// totals table through a view. The live one is the oracle — it is correct by
// construction, being a direct reading of the source of truth — so anywhere the
// two disagree, the materialised one is wrong.
//
// This file checks the half of that parity a unit test can reach without a
// database: that the two paths agree on the SHAPE of the answer — the columns
// they name, the granularity they key on, the rows they are willing to emit.
// Shape divergence is the failure mode that hides, because both sides still
// return plausible numbers; only the row COUNT or a column NAME differs, and a
// report silently loses a column or lists every item ever traded.
//
// The value half — same movements in, same figures out — needs a live engine and
// belongs in the SQLite integration target alongside test_queryParity.
//
// (docs/register-totals-strategy.md)
// =============================================================================

#include <gtest/gtest.h>

#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

const ibMaterializationDialect& Mat() { return ibDatabaseLayerSQLite::MaterializationDialect(); }
const ibDialectDictionary&      Dia() { return ibDatabaseLayerSQLite::Dialect(); }

// The shape an accumulation register declares: totals keyed by (day, warehouse), one resource
// stored as a received / spent pair. Mirrors accumulationRegisterSchema.cpp — if that file's
// composition changes, this fixture should change with it, and the tests below say why.
ibMaterializeSpec MakeSpec()
{
	ibMaterializeSpec s;
	s.m_table            = wxT("Reg7_T");
	s.m_source           = wxT("Reg7");
	s.m_keyColumns       = { wxT("wh") };
	s.m_periodColumn     = wxT("period_");
	s.m_periodSourceExpr = wxT("{row}.period_");
	s.m_periodUnit       = ibTotalsPeriod::Day;
	s.m_deltas = {
		{ wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 0 THEN {row}.qty ELSE 0 END") },
		{ wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 0 THEN 0 ELSE {row}.qty END") },
	};
	return s;
}

ibMaterializeView TurnoverView()
{
	ibMaterializeView v;
	v.m_name       = wxT("Reg7_Turnovers");
	v.m_withPeriod = true;
	v.m_columns = {
		{ wxT("Qty_Receipt"),  wxT("qty_in"),  wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Expense"),  wxT("qty_out"), wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Turnover"), wxT("qty_in"),  wxT("qty_out"), ibMaterializeAgg::Difference },
	};
	return v;
}

ibMaterializeView BalanceView()
{
	ibMaterializeView v;
	v.m_name         = wxT("Reg7_Balance");
	v.m_withPeriod   = false;
	v.m_dropZeroRows = true;
	v.m_columns = { { wxT("Qty_Balance"), wxT("qty_in"), wxT("qty_out"), ibMaterializeAgg::Difference } };
	return v;
}

ibMaterializeView BalanceAndTurnoverView()
{
	ibMaterializeView v;
	v.m_name       = wxT("Reg7_BalanceAndTurnovers");
	v.m_withPeriod = true;
	v.m_columns = {
		{ wxT("Qty_OpeningBalance"), wxT("qty_in"), wxT("qty_out"), ibMaterializeAgg::RunningSumExcludingCurrent },
		{ wxT("Qty_Receipt"),        wxT("qty_in"), wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Expense"),        wxT("qty_out"),wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Turnover"),       wxT("qty_in"), wxT("qty_out"), ibMaterializeAgg::Difference },
		{ wxT("Qty_ClosingBalance"), wxT("qty_in"), wxT("qty_out"), ibMaterializeAgg::RunningSum },
	};
	return v;
}

wxString RenderOne(const ibMaterializeView& view, unsigned int shards = 1)
{
	ibMaterializeSpec s = MakeSpec();
	s.m_shards = shards;
	s.m_views  = { view };
	return RenderMaterialization(s, &Mat(), Dia()).LastCreate();
}

} // namespace

// ---------------------------------------------------------------------------
// Column names — the contract between a view and the queryable that reads it
// ---------------------------------------------------------------------------

// The live path reports _Receipt / _Expense / _Turnover (ComputeTurnover builds exactly those
// aliases). The view must name them identically, because the register's view-source describes its
// columns by these names: a mismatch is not an error anywhere — the reader simply finds no column
// and hands back an empty value, so a report loses a column silently.
TEST(TotalsViewParity, TurnoverColumnsMatchTheLiveNames) {
	const wxString sql = RenderOne(TurnoverView());
	EXPECT_TRUE(sql.Contains(wxT("AS Qty_Receipt")));
	EXPECT_TRUE(sql.Contains(wxT("AS Qty_Expense")));
	EXPECT_TRUE(sql.Contains(wxT("AS Qty_Turnover")));
}

// Same contract for the five columns of balance-and-turnovers.
TEST(TotalsViewParity, BalanceAndTurnoverReportsAllFiveColumns) {
	const wxString sql = RenderOne(BalanceAndTurnoverView());
	for (const wxChar* c : { wxT("AS Qty_OpeningBalance"), wxT("AS Qty_Receipt"), wxT("AS Qty_Expense"),
	                         wxT("AS Qty_Turnover"), wxT("AS Qty_ClosingBalance") })
		EXPECT_TRUE(sql.Contains(c)) << "missing " << wxString(c).ToStdString();
}

// ---------------------------------------------------------------------------
// Granularity — the floor on what can be read back
// ---------------------------------------------------------------------------

// Stored by DAY, so the view must expose day-and-coarser projections and nothing finer. The finer
// ones are not merely empty: the information does not exist once movements are folded into daily
// rows, and a column repeating the day under the name "hour" would be worse than its absence.
TEST(TotalsViewParity, ProjectionsStopAtTheStoredGrain) {
	const wxString sql = RenderOne(TurnoverView());
	EXPECT_TRUE(sql.Contains(wxT("AS period__Week")));
	EXPECT_TRUE(sql.Contains(wxT("AS period__Month")));
	EXPECT_TRUE(sql.Contains(wxT("AS period__Quarter")));
	EXPECT_TRUE(sql.Contains(wxT("AS period__Year")));
	EXPECT_FALSE(sql.Contains(wxT("period__Hour")));
	EXPECT_FALSE(sql.Contains(wxT("period__Minute")));
	EXPECT_FALSE(sql.Contains(wxT("period__Second")));
	EXPECT_FALSE(sql.Contains(wxT("period__Day")));   // equal to stored — the column already IS it
}

// ---------------------------------------------------------------------------
// Which rows exist — the divergence that is easiest to miss
// ---------------------------------------------------------------------------

// The live balance path filters with HAVING <any resource> <> 0: no stock means NO ROW. The view
// must do the same, or the two paths return different ROW COUNTS while every number in them
// matches — a balance report that lists every item ever traded instead of the ones on hand.
TEST(TotalsViewParity, BalanceDropsZeroRowsLikeTheLivePath) {
	const wxString sql = RenderOne(BalanceView());
	EXPECT_TRUE(sql.Contains(wxT("<> 0")));
}

// ...but ONLY on the balance. Turnovers report a period that netted to zero — receipt and expense
// that cancelled are real, reportable figures for that period, and dropping the row would lose
// them. This is the same asymmetry that decides a totals row may never be deleted.
TEST(TotalsViewParity, TurnoversKeepCancellingPeriods) {
	const wxString sql = RenderOne(TurnoverView());
	EXPECT_FALSE(sql.Contains(wxT("<> 0")));
}

// A period-less view MERGES every period into one row per key, so it must group and sum. Without
// that it would report whichever single stored row the engine happened to return — a number
// indistinguishable from a correct balance until someone reconciles it.
TEST(TotalsViewParity, BalanceCollapsesEveryPeriod) {
	const wxString sql = RenderOne(BalanceView());
	EXPECT_TRUE(sql.Contains(wxT("SUM(qty_in)")));
	EXPECT_TRUE(sql.Contains(wxT("SUM(qty_out)")));
	EXPECT_TRUE(sql.Contains(wxT("GROUP BY")));
}

// The period-carrying views already hold one row per key and period, so they must NOT group: a
// redundant GROUP BY changes no answer and costs a sort or a hash on every read.
TEST(TotalsViewParity, PeriodViewsDoNotRegroup) {
	EXPECT_FALSE(RenderOne(TurnoverView()).Contains(wxT("GROUP BY")));
	EXPECT_FALSE(RenderOne(BalanceAndTurnoverView()).Contains(wxT("GROUP BY")));
}

// ---------------------------------------------------------------------------
// Splitting must be invisible
// ---------------------------------------------------------------------------

// Whether totals are split is a STORAGE decision. The view absorbs it, so the same query over a
// split and an unsplit register must return the same columns and the same rows — the only
// difference is a fold inside the view body. If the shard column ever reached the projection, a
// consumer could group by it and read partial sums that look like real balances.
TEST(TotalsViewParity, SplitChangesNothingAConsumerCanSee) {
	// SQLite collapses any split to one shard (single writer — nothing to relieve), so the
	// rendered body is identical either way. That collapse IS the guarantee on this engine.
	const wxString plain  = RenderOne(TurnoverView(), 1);
	const wxString asked4 = RenderOne(TurnoverView(), 4);
	EXPECT_EQ(plain, asked4);
	EXPECT_FALSE(asked4.Contains(ShardColumnName()));
}

// ---------------------------------------------------------------------------
// The view reads the totals, never the movements
// ---------------------------------------------------------------------------

// A view that re-aggregated the movements would JOIN just as well and answer just as correctly —
// and leave the scale ceiling exactly where it was, since every read would rescan history. The
// failure would be invisible until volume, which is precisely when it is expensive to discover.
TEST(TotalsViewParity, ViewsReadTheTotalsTable) {
	for (const ibMaterializeView& v : { TurnoverView(), BalanceView(), BalanceAndTurnoverView() }) {
		const wxString sql = RenderOne(v);
		EXPECT_TRUE(sql.Contains(wxT("FROM Reg7_T")));
		EXPECT_FALSE(sql.Contains(wxT("FROM Reg7 ")));
	}
}
