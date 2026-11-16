// =============================================================================
// The rows as they stand, and the cut read half by half.
//
// A balance "as of a moment inside a day" is the stored totals below the day's start plus the
// movements of the day up to the moment. Until 2026-09-19 that was read as ONE selection over ONE
// dressed view — `(stored AND period < floor) OR (moved AND period >= floor)` over a union that
// projected six calendar units per row and folded the shards — and on a real base every posting paid
// for a walk of the whole register (measured on Firebird, 193 000 stored rows: 0.6 s a balance,
// 0.04 s after). What changed is the ROAD, not the answer, and that is what these tests pin:
//
//   * the rows — the two arms of the view's own declaration rendered as RELATIONS over the two tables
//     (RenderStoredRows / RenderMovementRows): nothing computed per row, nothing grouped, and nothing in
//     the database but the tables — no view a base built before them would lack;
//   * the reader — a cut becomes a UNION ALL of two selections, each narrowed on its own;
//   * and the NUMBERS — the same balance through the old single view, through the two halves, and
//     summed by hand from the movements, on a live SQLite.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

// One register: a warehouse key, one quantity, a recorder mark that only a movement carries. The
// deltas and the guard in BOTH forms the declaration carries — text for the trigger, IR for a reading.
ibMaterializeSpec CutSpec()
{
	ibMaterializeSpec spec;
	spec.m_table            = wxT("Reg9_T");
	spec.m_source           = wxT("Reg9");
	spec.m_keyColumns       = { wxT("wh") };
	spec.m_periodColumn     = wxT("period_");
	spec.m_periodSourceExpr = wxT("{row}.period_");
	spec.m_periodUnits      = { ibTotalsPeriod::Day };
	spec.m_guard            = wxT("{row}.active_ <> 0");

	const ibQueryExprPtr receipt = ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("Reg9"), wxT("rectype_")), ibConst(ibValue(ibNumber(1))));
	const ibQueryExprPtr qty     = ibCol(wxT("Reg9"), wxT("qty"));
	const ibQueryExprPtr zero    = ibConst(ibValue(ibNumber(0)));
	spec.m_deltas = {
		{ wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 1 THEN {row}.qty ELSE 0 END"), ibCase({ { receipt, qty } }, zero) },
		{ wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 1 THEN 0 ELSE {row}.qty END"), ibCase({ { receipt, zero } }, qty) },
	};
	spec.m_guardIR        = ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("Reg9"), wxT("active_")), zero);
	spec.m_periodSourceIR = ibCol(wxT("Reg9"), wxT("period_"));
	spec.m_periodIsDateIR = ibIsNull(ibCol(wxT("Reg9"), wxT("period_")), /*negated*/ true);

	ibMaterializeView dressed;
	dressed.m_name = wxT("Reg9_Turnovers");
	dressed.m_columns = {
		{ wxT("Qty_Receipt"),  wxT("qty_in"),  ibString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Expense"),  wxT("qty_out"), ibString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Turnover"), wxT("qty_in"),  wxT("qty_out"), ibMaterializeAgg::Difference },
	};
	dressed.m_withMovements = true;
	dressed.m_movementColumns = { { wxT("rec_"), ibTypeString(36) } };

	spec.m_views = { dressed };
	return spec;
}

const ibDialectDictionary& SqliteQ() { return ibDatabaseLayerSQLite::Dialect(); }

ibString SqlOf(const ibQueryRelPtr& rel)
{
	return ibQueryRenderer(SqliteQ()).Render(ibQueryIR(ibProject(rel))).m_sql;
}

// A balance as of `moment`, cut at the start of its day — off the rows as they stand, or (rows = false) off
// the dressed view the way every reading went before.
ibMaterializeReadSpec BalanceRead(bool rows, const ibDateTime& moment, bool excluding)
{
	ibMaterializeReadSpec r;
	if (rows) {
		const ibMaterializeSpec spec = CutSpec();
		r.m_storedRows = RenderStoredRows(spec, spec.m_views.front());
		r.m_movedRows  = RenderMovementRows(spec, spec.m_views.front());
		r.m_rowsAlias  = wxT("b_rows");
	}
	else
		r.m_view = wxT("Reg9_Turnovers");
	r.m_periodColumn = wxT("period_");
	r.m_keyColumns   = { wxT("wh") };
	r.m_to           = ibValue(moment);
	r.m_toExcluding  = excluding;
	r.m_markColumn   = wxT("rec_");
	r.m_floor        = ibValue(moment.BeginOfPeriod(ibTotalsPeriod::Day));
	r.m_dropZeroRows = true;
	r.m_columns = {
		{ wxT("Qty_Balance"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true },
	};
	return r;
}

ibString ReadSql(const ibMaterializeReadSpec& spec)
{
	return SqlOf(RenderMaterializedRead(spec, wxT("b")));
}

} // namespace

// =============================================================================
// The rows
// =============================================================================

// The dressed view projects the coarser units — that is what it is FOR. The rows as they stand do not: each
// unit is a date expression evaluated on every stored row of every read, asked for or not. Nor do they fold
// the shards: a GROUP BY is a line the engine will not push "this warehouse" beneath.
TEST(MaterializeRows, TheStoredRowsAreTheTotalsTableAsItStands) {
	ibMaterializeSpec spec = CutSpec();
	spec.m_shards = 4;   // split: the rows are still the rows, one per shard, for the reader to fold
	const ibString sql = SqlOf(RenderStoredRows(spec, spec.m_views.front()));

	EXPECT_TRUE(sql.Contains(wxT("Reg9_T"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("period_"))) << sql;          // the period itself, as stored
	EXPECT_FALSE(sql.Contains(wxT("period__Month"))) << sql;   // and no unit computed from it
	EXPECT_TRUE(sql.Contains(wxT("Qty_Turnover"))) << sql;     // the figures under the view's own names
	EXPECT_FALSE(sql.Contains(wxT("GROUP BY"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("SUM("))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("CAST("))) << sql;            // the mark, published as a typed null
	EXPECT_FALSE(sql.Contains(wxT("Reg9_Turnovers"))) << sql;  // and no view anywhere
}

// The movement rows read the movements — never the totals — under the same guard the trigger accumulates by,
// and the condition that lets a floor ride the period index joins it rather than replacing it.
TEST(MaterializeRows, TheMovementRowsAreTheMovementsUnderTheGuard) {
	const ibMaterializeSpec spec = CutSpec();
	const ibString sql = SqlOf(RenderMovementRows(spec, spec.m_views.front()));

	EXPECT_TRUE(sql.Contains(wxT("Reg9.active_"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("Reg9.period_ IS NOT NULL"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("Reg9.rec_"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("Reg9_T"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("UNION"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("{row}"))) << sql;
}

// Without its read forms a spec cannot say which movements count, and rows read past the guard would be
// figures the totals never held — so there are no rows rather than rows without the guard.
TEST(MaterializeRows, WithoutTheReadFormsThereAreNoMovementRows) {
	ibMaterializeSpec spec = CutSpec();
	spec.m_guardIR = nullptr;
	EXPECT_FALSE(RenderMovementRows(spec, spec.m_views.front()));

	spec = CutSpec();
	spec.m_deltas.front().m_valueIR = nullptr;
	EXPECT_FALSE(RenderMovementRows(spec, spec.m_views.front()));
}

// =============================================================================
// The reader
// =============================================================================

// A cut inside the grain is read as two selections, each of its own table. The bound of each stands in ITS OWN
// WHERE, which is the whole point: under an OR neither could ride an index.
TEST(MaterializeCutRead, ACutIsAUnionOfTwoNarrowedSelections) {
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	const ibString sql = ReadSql(BalanceRead(/*rows*/ true, moment, /*excluding*/ true));

	EXPECT_TRUE(sql.Contains(wxT("UNION ALL"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("Reg9_T"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("FROM Reg9 "))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("Reg9_Turnovers"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NULL"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NOT NULL"))) << sql;
}

// Off a named view the reading stays ONE selection — the road the accounting register takes. Both halves would
// select from the same dressed union: four arms expanded where there were two, and neither half any nearer an
// index than the OR it replaced.
TEST(MaterializeCutRead, OffANamedViewTheReadingStaysOneSelection) {
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	const ibString sql = ReadSql(BalanceRead(/*rows*/ false, moment, true));
	EXPECT_FALSE(sql.Contains(wxT("UNION ALL"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("_cut"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("FROM Reg9_Turnovers"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NULL"))) << sql;        // the cut is still there, as the one condition it was
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NOT NULL"))) << sql;
}

// A reading that stops AT the grain needs no movements: no floor, no union — the stored rows alone.
TEST(MaterializeCutRead, AtTheGrainOnlyTheStoredRowsAreRead) {
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, ibDateTime(2026, 3, 5), false);
	r.m_floor = ibValue();   // what ibRegFillArmCut leaves when the bound does not reach inside a grain
	const ibString sql = ReadSql(r);
	EXPECT_FALSE(sql.Contains(wxT("UNION ALL"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("FROM Reg9 "))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("Reg9_T"))) << sql;
}

// 🛑 Stored rows cannot be cut without the movements: the partial grain would be read from nowhere, a balance
// short of today's postings. Refused, not answered.
TEST(MaterializeCutRead, StoredRowsWithoutTheMovementsAreNotCut) {
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, ibDateTime(2026, 3, 5, 14, 0, 0), true);
	r.m_movedRows = nullptr;
	EXPECT_ANY_THROW(RenderMaterializedRead(r, wxT("b")));
}

// The caller's filters narrow EACH half — that is what lets "this warehouse" reach an index in both — and are
// NOT said a third time around them: the relation the halves publish carries only the columns the reading
// names, and a filter is free to name another.
TEST(MaterializeCutRead, FiltersNarrowEachHalf) {
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("b_rows"), wxT("wh")), ibConst(ibValue(ibString(wxT("kitchen"))))) };
	const ibString sql = ReadSql(r);

	int seen = 0;
	for (size_t at = 0; (at = sql.find(wxT("b_rows.wh = "), at)) != ibString::npos; at += 5)
		++seen;
	EXPECT_EQ(seen, 2) << sql;   // once in each half, and not around them
}

// A turnover looks at nothing below its interval, and the stored half is told so: bounded on both sides, the
// period can ride the index instead of walking the warehouse's whole history to add zeros.
TEST(MaterializeCutRead, ATurnoverBoundsItsStoredHalfBelow) {
	const ibDateTime from(2026, 3, 3);
	const ibDateTime to(2026, 3, 5, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, to, false);
	r.m_from = ibValue(from);
	r.m_columns = {
		{ wxT("Qty_Moved"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
	};
	// The stored half: from the totals table up to the UNION — its rows' subquery, then its own WHERE.
	const auto storedHalf = [](const ibString& sql) {
		const size_t fromAt = sql.find(wxT("Reg9_T"));
		const size_t unionAt = sql.find(wxT("UNION ALL"));
		return fromAt == ibString::npos || unionAt == ibString::npos || unionAt < fromAt
			? ibString() : sql.Mid(fromAt, unionAt - fromAt);
	};
	const ibString turnover = storedHalf(ReadSql(r));
	ASSERT_FALSE(turnover.IsEmpty());
	EXPECT_TRUE(turnover.Contains(wxT("period_ >= "))) << turnover;

	// A balance reads the whole history — its stored half has no lower bound to be given.
	const ibString balance = storedHalf(ReadSql(BalanceRead(/*rows*/ true, to, false)));
	ASSERT_FALSE(balance.IsEmpty());
	EXPECT_FALSE(balance.Contains(wxT("period_ >= "))) << balance;
}

// =============================================================================
// The numbers — live, on SQLite
// =============================================================================

namespace {

struct CutReadFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

		db->RunQuery(wxT("CREATE TABLE Reg9 (rec_ TEXT NOT NULL, line_ INTEGER NOT NULL, period_ TEXT NOT NULL, ")
			wxT("active_ INTEGER NOT NULL, wh TEXT NOT NULL, rectype_ INTEGER NOT NULL, qty NUMERIC NOT NULL, ")
			wxT("PRIMARY KEY (rec_, line_))"));
		db->RunQuery(wxT("%s"), TotalsTable());

		// The maintenance — the triggers that keep Reg9_T, and the dressed view the old road reads.
		const ibMaterializeSql sql = RenderMaterialization(Declared(), &ibDatabaseLayerSQLite::MaterializationDialect(), SqliteQ());
		ASSERT_TRUE(sql.Apply(*db));
	}

	// The totals as this fixture keeps them: the declaration, and the table it maintains.
	virtual ibMaterializeSpec Declared() const { return CutSpec(); }
	virtual ibString TotalsTable() const {
		return wxT("CREATE TABLE Reg9_T (period_ TEXT NOT NULL, wh TEXT NOT NULL, ")
			wxT("qty_in NUMERIC NOT NULL DEFAULT 0, qty_out NUMERIC NOT NULL DEFAULT 0, PRIMARY KEY (period_, wh))");
	}
	void TearDown() override {
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	void Move(const ibString& rec, int line, const ibString& when, const ibString& wh, bool receipt, double qty, bool active = true) {
		db->RunQuery(wxT("INSERT INTO Reg9 VALUES ('%s', %d, '%s', %d, '%s', %d, %s)"),
			rec, line, when, active ? 1 : 0, wh, receipt ? 1 : 0, wxString::FromCDouble(qty));
	}

	// The balance of one warehouse through a rendered read; -1e9 when the warehouse has no row at all.
	double Balance(const ibMaterializeReadSpec& spec, const ibString& wh) {
		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(RenderMaterializedRead(spec, wxT("b")));
		q.Project({ ibQueryProjItem{ ibCol(wxT("b"), wxT("wh")), wxT("wh") },
		            ibQueryProjItem{ ibCol(wxT("b"), wxT("Qty_Balance")), wxT("Qty_Balance") } });
		ibQueryResult rs = q.Execute();
		while (rs.Next())
			if (rs.GetResultString(wxT("wh")) == wh)
				return rs.GetResultDouble(wxT("Qty_Balance"));
		return -1e9;
	}

	// The same figure summed straight off the movements — the oracle nothing above can disagree with.
	double ByHand(const ibString& wh, const ibString& upTo, bool excluding) {
		double sum = 0.0;
		ibDatabaseResultSet* rs = db->RunQueryWithResults(wxT("%s"), ibString::Format(
			wxT("SELECT COALESCE(SUM(CASE WHEN rectype_ = 1 THEN qty ELSE -qty END), 0) FROM Reg9 ")
			wxT("WHERE active_ <> 0 AND wh = '%s' AND period_ %s '%s'"), wh, excluding ? wxT("<") : wxT("<="), upTo));
		if (rs != nullptr) {
			if (rs->Next()) sum = rs->GetResultDouble(1);
			db->CloseResultSet(rs);
		}
		return sum;
	}

	void Fill() {
		Move(wxT("r1"), 1, wxT("2026-03-03 09:00:00"), wxT("kitchen"), true, 100);
		Move(wxT("r1"), 2, wxT("2026-03-03 09:00:00"), wxT("bar"),     true, 40);
		Move(wxT("w1"), 1, wxT("2026-03-03 23:00:00"), wxT("kitchen"), false, 12);
		Move(wxT("r2"), 1, wxT("2026-03-04 09:00:00"), wxT("kitchen"), true, 30);
		Move(wxT("w2"), 1, wxT("2026-03-04 23:00:00"), wxT("kitchen"), false, 25);
		Move(wxT("r3"), 1, wxT("2026-03-05 09:00:00"), wxT("kitchen"), true, 7);       // the day being cut
		Move(wxT("x3"), 1, wxT("2026-03-05 10:00:00"), wxT("kitchen"), true, 500, /*active*/ false);   // written, not in force
		Move(wxT("w3"), 1, wxT("2026-03-05 14:00:00"), wxT("kitchen"), false, 9);      // AT the moment asked about
		Move(wxT("w4"), 1, wxT("2026-03-05 23:00:00"), wxT("kitchen"), false, 50);     // after it
		Move(wxT("r4"), 1, wxT("2026-03-06 09:00:00"), wxT("kitchen"), true, 1000);    // another day entirely
	}
};

} // namespace

// ⭐ ONE BALANCE, THREE ROADS. The dressed view read both halves at once (the old road), the rows as they stand
// read half by half (the new one), the movements summed by hand. Excluding the moment and including it — the
// movement standing exactly AT the moment is the row the two differ by.
TEST_F(CutReadFix, TheRowsAnswerWhatTheSingleViewAnswered) {
	Fill();
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);

	for (const bool excluding : { true, false }) {
		const double hand   = ByHand(wxT("kitchen"), wxT("2026-03-05 14:00:00"), excluding);
		const double oldWay = Balance(BalanceRead(/*rows*/ false, moment, excluding), wxT("kitchen"));
		const double newWay = Balance(BalanceRead(/*rows*/ true, moment, excluding), wxT("kitchen"));

		EXPECT_DOUBLE_EQ(hand, excluding ? 100.0 : 91.0);   // 100 - 12 + 30 - 25 + 7 [- 9]
		EXPECT_DOUBLE_EQ(oldWay, hand) << "the dressed view, excluding = " << excluding;
		EXPECT_DOUBLE_EQ(newWay, hand) << "the rows as they stand, excluding = " << excluding;
	}
}

// ⭐⭐ NOTHING BUT THE TABLES. The rows need no view in the base: a base built before this reading — or one whose
// views are gone — answers it all the same. (The reading the rows replaced named a second view, and a base
// without it failed every balance, the one inside every posting included.)
TEST_F(CutReadFix, TheRowsNeedNoViewInTheBase) {
	Fill();
	db->RunQuery(wxT("DROP VIEW Reg9_Turnovers"));
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ true, moment, true), wxT("kitchen")), 100.0);
}

// The other warehouse is untouched by the day being cut — its answer comes off the stored half alone.
TEST_F(CutReadFix, AKeyWithNoMovementsInTheGrainReadsItsStoredRows) {
	Fill();
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ true, moment, true), wxT("bar")), 40.0);
}

// A filter narrows both halves and must not lose the key it names.
TEST_F(CutReadFix, AFilterOnTheKeyKeepsThatKeyWhole) {
	Fill();
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("b_rows"), wxT("wh")), ibConst(ibValue(ibString(wxT("kitchen"))))) };

	EXPECT_DOUBLE_EQ(Balance(r, wxT("kitchen")), 100.0);
	EXPECT_DOUBLE_EQ(Balance(r, wxT("bar")), -1e9);   // filtered out — no row at all
}

// ⭐ A FILTER THAT WALKS finds its outer row under the name the rows are read by, in either half. A walk
// through a reference is a correlated EXISTS; lowered against the dressed view's own name it asked for a
// relation the statement no longer read, and failed on every engine. Here the other table is a kind of
// warehouse, and only the kitchen is "hot".
TEST_F(CutReadFix, AFilterThatWalksFindsItsRowInEitherHalf) {
	Fill();
	db->RunQuery(wxT("CREATE TABLE Kinds (wh TEXT NOT NULL, kind TEXT NOT NULL)"));
	db->RunQuery(wxT("INSERT INTO Kinds VALUES ('kitchen', 'hot'), ('bar', 'cold')"));

	ibDatabaseQueryBuilder kinds;
	kinds.From(ibScan(wxT("Kinds"), wxT("k"))).Project({ ibQueryProjItem{ ibCol(wxT("k"), wxT("wh")), ibString() } });
	kinds.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("k"), wxT("wh")), ibCol(wxT("b_rows"), wxT("wh"))));
	kinds.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("k"), wxT("kind")), ibConst(ibValue(ibString(wxT("hot"))))));

	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	for (const bool inside : { true, false }) {   // a cut (two halves) and a reading at the grain (one)
		ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, inside ? moment : ibDateTime(2026, 3, 5), true);
		if (!inside)
			r.m_floor = ibValue();
		r.m_filters = { ibExists(kinds.Build().m_root) };

		EXPECT_DOUBLE_EQ(Balance(r, wxT("kitchen")), inside ? 100.0 : 93.0) << "inside the grain = " << inside;
		EXPECT_DOUBLE_EQ(Balance(r, wxT("bar")), -1e9) << "inside the grain = " << inside;
	}
}

// A key whose movements net to zero is NO ROW (m_dropZeroRows) — off the rows as off the view.
TEST_F(CutReadFix, AZeroBalanceIsNoRowOnEitherRoad) {
	Move(wxT("r1"), 1, wxT("2026-03-04 09:00:00"), wxT("kitchen"), true, 10);
	Move(wxT("w1"), 1, wxT("2026-03-05 10:00:00"), wxT("kitchen"), false, 10);
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);

	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ false, moment, false), wxT("kitchen")), -1e9);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ true, moment, false), wxT("kitchen")), -1e9);
}

// A filter may name a column the reading does not carry. Said around the halves as well as inside them, it
// named a column their relation does not publish, and the read failed where the single view had answered.
TEST_F(CutReadFix, AFilterOnAColumnTheReadingDoesNotCarryStillReads) {
	Fill();
	const ibDateTime moment(2026, 3, 5, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Ge, ibCol(wxT("b_rows"), wxT("Qty_Receipt")), ibConst(ibValue(ibNumber(0)))) };   // every row passes

	EXPECT_DOUBLE_EQ(Balance(r, wxT("kitchen")), 100.0);
}

// ⭐ THE TURNOVER OF AN INTERVAL, THREE ROADS AGAIN — with the stored half bounded below. 04.03 00:00 through
// 05.03 14:00 inclusive: +30 -25 +7 -9; the day before and the evening after belong to nobody.
TEST_F(CutReadFix, ATurnoverBoundedBelowAnswersWhatTheMovementsSay) {
	Fill();
	const ibDateTime from(2026, 3, 4);
	const ibDateTime to(2026, 3, 5, 14, 0, 0);

	const auto read = [&](bool rows) {
		ibMaterializeReadSpec r = BalanceRead(rows, to, /*excluding*/ false);
		r.m_from = ibValue(from);
		r.m_columns = {
			{ wxT("Qty_Balance"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
		};
		return Balance(r, wxT("kitchen"));
	};
	const double hand = ByHand(wxT("kitchen"), wxT("2026-03-05 14:00:00"), false) - ByHand(wxT("kitchen"), wxT("2026-03-04 00:00:00"), true);

	EXPECT_DOUBLE_EQ(hand, 3.0);
	EXPECT_DOUBLE_EQ(read(/*rows*/ false), hand);
	EXPECT_DOUBLE_EQ(read(/*rows*/ true), hand);
}

// =============================================================================
// The units — the totals kept by the day, the week, the month, the quarter and the year at once
//
// A posting stands once in the row of each unit, and a reading takes every day from ONE of them: the years
// behind a date, then the quarters, the months, the days, and the movements of the day it stops in. What these
// pin is that it is the same NUMBER whichever rows answered — every figure below is compared with the
// movements summed by hand, over boundaries on every kind of edge and inside a day.
// =============================================================================

namespace {

ibMaterializeSpec UnitsSpec()
{
	ibMaterializeSpec spec = CutSpec();
	spec.m_periodUnitColumn = PeriodUnitColumnName();
	spec.m_periodUnits      = { ibTotalsPeriod::Day, ibTotalsPeriod::Week, ibTotalsPeriod::Month,
	                            ibTotalsPeriod::Quarter, ibTotalsPeriod::Year };
	return spec;
}

// A reading off the rows of every unit, cut the way a register cuts it (ibRegFillArmCut, no document at a
// boundary): a boundary inside a day — or an included upper edge, an excluded lower one — is the movements'.
ibMaterializeReadSpec UnitsRead(const ibValue& from, bool fromExcluding, const ibValue& to, bool toExcluding)
{
	const ibMaterializeSpec spec = UnitsSpec();
	ibMaterializeReadSpec r;
	r.m_storedRows       = RenderStoredRows(spec, spec.m_views.front());
	r.m_movedRows        = RenderMovementRows(spec, spec.m_views.front());
	r.m_storedUnits      = spec.m_periodUnits;
	r.m_periodUnitColumn = spec.m_periodUnitColumn;
	r.m_rowsAlias        = wxT("b_rows");
	r.m_periodColumn     = wxT("period_");
	r.m_keyColumns       = { wxT("wh") };
	r.m_markColumn       = wxT("rec_");
	r.m_dropZeroRows     = true;
	r.m_from = from;  r.m_fromExcluding = fromExcluding;
	r.m_to   = to;    r.m_toExcluding   = toExcluding;

	const ibTotalsPeriod day = ibTotalsPeriod::Day;
	const auto reachesInside = [day](const ibValue& at, bool upperEnd, bool excluding) {
		if (at.GetType() != TYPE_DATE)
			return false;
		if (at.GetDate().BeginOfPeriod(day) != at.GetDate())
			return true;
		return upperEnd ? !excluding : excluding;
	};
	const bool cutUpper = reachesInside(to, /*upperEnd*/ true, toExcluding);
	const bool cutLower = reachesInside(from, /*upperEnd*/ false, fromExcluding);
	if (cutUpper || cutLower) {
		// No upper boundary, no floor: the stored rows run to the last of them, the movements answer the head.
		if (to.GetType() == TYPE_DATE)
			r.m_floor = ibValue(to.GetDate().BeginOfPeriod(day));
		if (cutLower) {
			r.m_headSplit = ibValue(from.GetDate().BeginOfNextPeriod(day));
			r.m_headGrain = ibValue(from.GetDate().BeginOfPeriod(day));
		}
	}
	return r;
}

// A moment, and the same moment as the movements' text holds it.
struct Moment { ibDateTime m_at; const wxChar* m_text; };

// Before every posting and after the last, on the edge of a year, a quarter, a month, a week and a day, at a
// posting's own instant and inside days with and without postings.
const std::vector<Moment>& Moments()
{
	static const std::vector<Moment> moments = {
		{ ibDateTime(2024, 1, 1),             wxT("2024-01-01 00:00:00") },
		{ ibDateTime(2024, 11, 20, 10, 0, 0), wxT("2024-11-20 10:00:00") },
		{ ibDateTime(2025, 1, 1),             wxT("2025-01-01 00:00:00") },
		{ ibDateTime(2025, 4, 1),             wxT("2025-04-01 00:00:00") },
		{ ibDateTime(2025, 7, 7),             wxT("2025-07-07 00:00:00") },
		{ ibDateTime(2025, 7, 9, 13, 0, 0),   wxT("2025-07-09 13:00:00") },
		{ ibDateTime(2025, 10, 1),            wxT("2025-10-01 00:00:00") },
		{ ibDateTime(2026, 1, 1),             wxT("2026-01-01 00:00:00") },
		{ ibDateTime(2026, 3, 1),             wxT("2026-03-01 00:00:00") },
		{ ibDateTime(2026, 3, 5),             wxT("2026-03-05 00:00:00") },
		{ ibDateTime(2026, 3, 5, 14, 0, 0),   wxT("2026-03-05 14:00:00") },
		{ ibDateTime(2026, 3, 6),             wxT("2026-03-06 00:00:00") },
		{ ibDateTime(2027, 1, 1),             wxT("2027-01-01 00:00:00") },
	};
	return moments;
}

int CountOf(const ibString& sql, const ibString& part)
{
	int seen = 0;
	for (size_t at = 0; (at = sql.find(part, at)) != ibString::npos; at += part.length())
		++seen;
	return seen;
}

struct UnitReadFix : CutReadFix {
	ibMaterializeSpec Declared() const override { return UnitsSpec(); }
	ibString TotalsTable() const override {
		return wxT("CREATE TABLE Reg9_T (unit_ INTEGER NOT NULL, period_ TEXT NOT NULL, wh TEXT NOT NULL, ")
			wxT("qty_in NUMERIC NOT NULL DEFAULT 0, qty_out NUMERIC NOT NULL DEFAULT 0, PRIMARY KEY (unit_, period_, wh))");
	}

	// One period of a reading cut into periods: where it begins, what moved in it, what it closes with.
	struct Period {
		ibString m_at;
		double   m_moved, m_closing;
	};

	// Two years and a half of postings: on the first instant of a year, a quarter, a month, a week and a day,
	// on the last second of a year, at the moment asked about, one not in force, one warehouse seldom touched.
	void FillYears() {
		int n = 0;
		const auto move = [&](const wxChar* when, const wxChar* wh, double qty, bool active = true) {
			Move(ibString::Format(wxT("m%d"), ++n), 1, when, wh, qty > 0, qty > 0 ? qty : -qty, active);
		};
		move(wxT("2024-11-20 10:00:00"), wxT("kitchen"), 100);
		move(wxT("2024-12-31 23:59:59"), wxT("kitchen"), -10);
		move(wxT("2025-01-01 00:00:00"), wxT("kitchen"), 7);
		move(wxT("2025-01-01 00:00:00"), wxT("bar"),     40);
		move(wxT("2025-02-14 12:00:00"), wxT("kitchen"), 30);
		move(wxT("2025-03-31 18:00:00"), wxT("kitchen"), -5);
		move(wxT("2025-04-01 00:00:00"), wxT("kitchen"), 11);
		move(wxT("2025-06-30 09:00:00"), wxT("kitchen"), 3);
		move(wxT("2025-07-07 00:00:00"), wxT("kitchen"), -2);
		move(wxT("2025-07-09 13:00:00"), wxT("kitchen"), 21);
		move(wxT("2025-07-09 16:00:00"), wxT("kitchen"), 500, /*active*/ false);
		move(wxT("2025-09-15 15:30:00"), wxT("bar"),     -15);
		move(wxT("2025-10-01 08:00:00"), wxT("kitchen"), 50);
		move(wxT("2025-12-31 12:00:00"), wxT("kitchen"), -20);
		move(wxT("2026-01-01 09:00:00"), wxT("kitchen"), 8);
		move(wxT("2026-02-28 23:00:00"), wxT("kitchen"), 13);
		move(wxT("2026-03-01 00:00:00"), wxT("kitchen"), -4);
		move(wxT("2026-03-04 09:00:00"), wxT("kitchen"), 30);
		move(wxT("2026-03-05 00:00:00"), wxT("kitchen"), 6);
		move(wxT("2026-03-05 09:00:00"), wxT("kitchen"), 7);
		move(wxT("2026-03-05 14:00:00"), wxT("kitchen"), -9);
		move(wxT("2026-03-05 23:00:00"), wxT("kitchen"), -50);
		move(wxT("2026-03-06 09:00:00"), wxT("bar"),     1000);
		move(wxT("2026-04-10 10:00:00"), wxT("kitchen"), 17);
	}

	// One figure of one warehouse off a rendered read; a warehouse with no row has nothing to its name.
	double Figure(const ibMaterializeReadSpec& spec, const ibString& wh, const ibString& figure) {
		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(RenderMaterializedRead(spec, wxT("b")));
		q.Project({ ibQueryProjItem{ ibCol(wxT("b"), wxT("wh")), wxT("wh") },
		            ibQueryProjItem{ ibCol(wxT("b"), figure), figure } });
		ibQueryResult rs = q.Execute();
		while (rs.Next())
			if (rs.GetResultString(wxT("wh")) == wh)
				return rs.GetResultDouble(figure);
		return 0.0;
	}

	// The reading cut into periods of `unit`: the turnover inside the interval, and the running balance.
	std::vector<Period> ReadPeriods(const ibString& wh, ibTotalsPeriod unit, const Moment& from, const Moment& to) {
		ibMaterializeReadSpec r = UnitsRead(ibValue(from.m_at), false, ibValue(to.m_at), false);
		r.m_grain      = ibMaterializeGrain::Calendar;
		r.m_periodUnit = unit;
		r.m_fromGrain  = ibValue(from.m_at.BeginOfPeriod(unit));
		r.m_columns = {
			{ wxT("Qty_Moved"),   wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value,      ibMaterializeWhen::InRange, true },
			{ wxT("Qty_Closing"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::RunningSum, ibMaterializeWhen::Always,  true },
		};

		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(RenderMaterializedRead(r, wxT("b")));
		q.Project({ ibQueryProjItem{ ibCol(wxT("b"), wxT("wh")), wxT("wh") },
		            ibQueryProjItem{ ibCol(wxT("b"), wxT("period_")), wxT("period_") },
		            ibQueryProjItem{ ibCol(wxT("b"), wxT("Qty_Moved")), wxT("Qty_Moved") },
		            ibQueryProjItem{ ibCol(wxT("b"), wxT("Qty_Closing")), wxT("Qty_Closing") } });
		std::vector<Period> periods;
		ibQueryResult rs = q.Execute();
		while (rs.Next())
			if (rs.GetResultString(wxT("wh")) == wh)
				periods.push_back({ rs.GetResultString(wxT("period_")), rs.GetResultDouble(wxT("Qty_Moved")),
				                    rs.GetResultDouble(wxT("Qty_Closing")) });
		std::sort(periods.begin(), periods.end(),
			[](const Period& a, const Period& b) { return a.m_at.ToStdString() < b.m_at.ToStdString(); });
		return periods;
	}

	// The same periods off the movements: each period something moved in up to `to`, its turnover inside the
	// interval and the running sum of everything up to its end — from the period `from` falls in, and none of
	// them a row where both are zero.
	std::vector<Period> PeriodsByHand(const ibString& wh, ibTotalsPeriod unit, const Moment& from, const Moment& to) {
		const auto truncated = [unit](const ibString& expr) {
			ibString trunc = SqliteQ().m_periodTrunc.at(unit);
			trunc.Replace(wxT("{expr}"), expr);
			return trunc;
		};
		const ibString signedQty = wxT("CASE WHEN rectype_ = 1 THEN qty ELSE -qty END");
		const ibString sql = ibString::Format(
			wxT("SELECT %s AS p, SUM(CASE WHEN period_ >= '%s' THEN %s ELSE 0 END), SUM(%s), MAX(%s >= %s) ")
			wxT("FROM Reg9 WHERE active_ <> 0 AND wh = '%s' AND period_ <= '%s' GROUP BY 1 ORDER BY 1"),
			truncated(wxT("period_")), ibString(from.m_text), signedQty, signedQty,
			truncated(wxT("period_")), truncated(ibString::Format(wxT("'%s'"), ibString(from.m_text))),
			wh, ibString(to.m_text));

		std::vector<Period> periods;
		double closing = 0.0;
		ibDatabaseResultSet* rs = db->RunQueryWithResults(wxT("%s"), sql);
		if (rs != nullptr) {
			while (rs->Next()) {
				const double moved = rs->GetResultDouble(2);
				closing += rs->GetResultDouble(3);
				if (rs->GetResultInt(4) != 0 && (moved != 0.0 || closing != 0.0))
					periods.push_back({ rs->GetResultString(1), moved, closing });
			}
			db->CloseResultSet(rs);
		}
		return periods;
	}
};

} // namespace

// ⭐ EACH UNIT IS A SELECTION OF ITS OWN. A balance inside 16.11.2026 is the years before 2026, the quarters up
// to October, October itself, the days of November — four ranges of the totals, each with the unit as an
// equality and the caller's filter beside it — and the movements of the day. A week holds days and nothing
// holds it, so before a date no week is read.
TEST(MaterializeUnitRead, ABalanceReadsEachUnitAsASelectionOfItsOwn) {
	ibMaterializeReadSpec r = UnitsRead(ibValue(), false, ibValue(ibDateTime(2026, 11, 16, 14, 30, 0)), true);
	r.m_columns = {
		{ wxT("Qty_Balance"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true },
	};
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("b_rows"), wxT("wh")), ibConst(ibValue(ibString(wxT("kitchen"))))) };
	const ibString sql = ReadSql(r);

	EXPECT_EQ(CountOf(sql, wxT("unit_ = ")), 4) << sql;
	EXPECT_EQ(CountOf(sql, wxT("UNION ALL")), 4) << sql;      // four of the totals, and the movements
	EXPECT_EQ(CountOf(sql, wxT("b_rows.wh = ")), 5) << sql;    // the filter in each, and not around them
	EXPECT_EQ(CountOf(sql, wxT("FROM Reg9 ")), 1) << sql;
}

// With no date at all the balance is the years, and nothing else: one selection, no movements.
TEST(MaterializeUnitRead, WithNoDateTheBalanceIsTheYears) {
	ibMaterializeReadSpec r = UnitsRead(ibValue(), false, ibValue(), false);
	r.m_columns = {
		{ wxT("Qty_Balance"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true },
	};
	const ibString sql = ReadSql(r);
	EXPECT_EQ(CountOf(sql, wxT("unit_ = ")), 1) << sql;
	EXPECT_FALSE(sql.Contains(wxT("UNION ALL"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("FROM Reg9 "))) << sql;
}

// The posting stands once in the row of every unit — which is what a reading of one unit per day counts on.
TEST_F(UnitReadFix, APostingStandsInTheRowOfEveryUnit) {
	Move(wxT("r1"), 1, wxT("2026-03-05 09:00:00"), wxT("kitchen"), true, 7);
	ibDatabaseResultSet* rs = db->RunQueryWithResults(wxT("%s"),
		ibString(wxT("SELECT COUNT(*), COUNT(DISTINCT unit_), SUM(qty_in) FROM Reg9_T")));
	ASSERT_NE(rs, nullptr);
	ASSERT_TRUE(rs->Next());
	EXPECT_EQ(rs->GetResultInt(1), 5);
	EXPECT_EQ(rs->GetResultInt(2), 5);
	EXPECT_DOUBLE_EQ(rs->GetResultDouble(3), 35.0);
	db->CloseResultSet(rs);
}

// ⭐⭐ THE BALANCE AT EVERY MOMENT, INCLUDING IT AND NOT — the rows of whichever units against the movements.
TEST_F(UnitReadFix, ABalanceIsWhatTheMovementsSayAtEveryMoment) {
	FillYears();
	const auto balance = [](const ibValue& to, bool excluding) {
		ibMaterializeReadSpec r = UnitsRead(ibValue(), false, to, excluding);
		r.m_columns = {
			{ wxT("Qty_Balance"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true },
		};
		return r;
	};
	for (const Moment& to : Moments())
		for (const bool excluding : { true, false })
			for (const wxChar* wh : { wxT("kitchen"), wxT("bar") })
				EXPECT_DOUBLE_EQ(Figure(balance(ibValue(to.m_at), excluding), wh, wxT("Qty_Balance")), ByHand(wh, to.m_text, excluding))
					<< wxString(wh).ToStdString() << " up to " << wxString(to.m_text).ToStdString() << ", excluding = " << excluding;

	// …and with no date at all: everything there is.
	for (const wxChar* wh : { wxT("kitchen"), wxT("bar") })
		EXPECT_DOUBLE_EQ(Figure(balance(ibValue(), false), wh, wxT("Qty_Balance")), ByHand(wh, wxT("2999-01-01 00:00:00"), false))
			<< wxString(wh).ToStdString();
}

// ⭐⭐ EVERY INTERVAL BETWEEN THOSE MOMENTS, each end taken in and left out: the turnover read alone (nothing
// below the interval is read), and the opening balance, the turnover and the closing balance read together
// (the history is, and the day the interval begins in is the movements').
TEST_F(UnitReadFix, AnIntervalIsWhatTheMovementsSayBetweenAnyTwoMoments) {
	FillYears();
	const std::vector<Moment>& moments = Moments();
	for (size_t i = 0; i < moments.size(); ++i)
		for (size_t j = i + 1; j < moments.size(); ++j)
			for (const bool fromExcluding : { false, true })
				for (const bool toExcluding : { false, true }) {
					const Moment& from = moments[i];
					const Moment& to = moments[j];
					const std::string where = wxString(from.m_text).ToStdString() + (fromExcluding ? " (out) .. " : " (in) .. ")
						+ wxString(to.m_text).ToStdString() + (toExcluding ? " (out)" : " (in)");

					ibMaterializeReadSpec turnover = UnitsRead(ibValue(from.m_at), fromExcluding, ibValue(to.m_at), toExcluding);
					turnover.m_columns = {
						{ wxT("Qty_Moved"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
					};
					ibMaterializeReadSpec all = UnitsRead(ibValue(from.m_at), fromExcluding, ibValue(to.m_at), toExcluding);
					all.m_columns = {
						{ wxT("Qty_Opening"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, true },
						{ wxT("Qty_Moved"),   wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true },
						{ wxT("Qty_Closing"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo,     true },
					};

					for (const wxChar* wh : { wxT("kitchen"), wxT("bar") }) {
						const double opening = ByHand(wh, from.m_text, /*excluding*/ !fromExcluding);
						const double closing = ByHand(wh, to.m_text, toExcluding);
						EXPECT_DOUBLE_EQ(Figure(turnover, wh, wxT("Qty_Moved")), closing - opening) << wxString(wh).ToStdString() << ", " << where;
						EXPECT_DOUBLE_EQ(Figure(all, wh, wxT("Qty_Opening")), opening) << wxString(wh).ToStdString() << ", " << where;
						EXPECT_DOUBLE_EQ(Figure(all, wh, wxT("Qty_Moved")), closing - opening) << wxString(wh).ToStdString() << ", " << where;
						EXPECT_DOUBLE_EQ(Figure(all, wh, wxT("Qty_Closing")), closing) << wxString(wh).ToStdString() << ", " << where;
					}
				}
}

// ⭐ OPEN ABOVE — from every moment, taken in and left out, to the last row there is: the stored rows answer from the
// first whole day on and the movements the head alone (they answered the whole length — load-million-rows.md, 6(e)).
TEST_F(UnitReadFix, AnIntervalOpenAboveIsWhatTheMovementsSay) {
	FillYears();
	for (const Moment& from : Moments())
		for (const bool fromExcluding : { false, true }) {
			const std::string where = wxString(from.m_text).ToStdString() + (fromExcluding ? " (out) .." : " (in) ..");

			ibMaterializeReadSpec turnover = UnitsRead(ibValue(from.m_at), fromExcluding, ibValue(), false);
			turnover.m_columns = {
				{ wxT("Qty_Moved"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
			};
			ibMaterializeReadSpec all = UnitsRead(ibValue(from.m_at), fromExcluding, ibValue(), false);
			all.m_columns = {
				{ wxT("Qty_Opening"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, true },
				{ wxT("Qty_Moved"),   wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true },
				{ wxT("Qty_Closing"), wxT("Qty_Turnover"), ibString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo,     true },
			};

			for (const wxChar* wh : { wxT("kitchen"), wxT("bar") }) {
				const double opening = ByHand(wh, from.m_text, /*excluding*/ !fromExcluding);
				const double closing = ByHand(wh, wxT("2999-01-01 00:00:00"), false);
				EXPECT_DOUBLE_EQ(Figure(turnover, wh, wxT("Qty_Moved")), closing - opening) << wxString(wh).ToStdString() << ", " << where;
				EXPECT_DOUBLE_EQ(Figure(all, wh, wxT("Qty_Opening")), opening) << wxString(wh).ToStdString() << ", " << where;
				EXPECT_DOUBLE_EQ(Figure(all, wh, wxT("Qty_Moved")), closing - opening) << wxString(wh).ToStdString() << ", " << where;
				EXPECT_DOUBLE_EQ(Figure(all, wh, wxT("Qty_Closing")), closing) << wxString(wh).ToStdString() << ", " << where;
			}
		}
}

// ⭐⭐ CUT INTO PERIODS — by the day, the week, the month, the quarter and the year. From the first period a
// reading reports, only the units that nest in its own answer (a year's row truncated to a month is its
// January); before it the coarsest do, and the running balance still closes each period with what the
// movements say.
TEST_F(UnitReadFix, PeriodsAreWhatTheMovementsSayInEveryUnit) {
	FillYears();
	const std::vector<Moment>& moments = Moments();
	for (const ibTotalsPeriod unit : { ibTotalsPeriod::Day, ibTotalsPeriod::Week, ibTotalsPeriod::Month,
	                                   ibTotalsPeriod::Quarter, ibTotalsPeriod::Year })
		for (size_t i = 0; i < moments.size(); ++i)
			for (size_t j = i + 1; j < moments.size(); ++j)
				for (const wxChar* wh : { wxT("kitchen"), wxT("bar") }) {
					const std::vector<Period> read = ReadPeriods(wh, unit, moments[i], moments[j]);
					const std::vector<Period> hand = PeriodsByHand(wh, unit, moments[i], moments[j]);
					const std::string where = wxString(wh).ToStdString() + " by unit " + std::to_string(static_cast<int>(unit)) + ", "
						+ wxString(moments[i].m_text).ToStdString() + " .. " + wxString(moments[j].m_text).ToStdString();

					ASSERT_EQ(read.size(), hand.size()) << where;
					for (size_t p = 0; p < read.size(); ++p) {
						EXPECT_EQ(read[p].m_at, hand[p].m_at) << where;
						EXPECT_DOUBLE_EQ(read[p].m_moved, hand[p].m_moved) << where << ", " << hand[p].m_at.ToStdString();
						EXPECT_DOUBLE_EQ(read[p].m_closing, hand[p].m_closing) << where << ", " << hand[p].m_at.ToStdString();
					}
				}
}
