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

#include <memory>
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
	spec.m_periodUnit       = ibTotalsPeriod::Day;
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
		{ wxT("Qty_Receipt"),  wxT("qty_in"),  wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Expense"),  wxT("qty_out"), wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Turnover"), wxT("qty_in"),  wxT("qty_out"), ibMaterializeAgg::Difference },
	};
	dressed.m_withMovements = true;
	dressed.m_movementColumns = { { wxT("rec_"), ibTypeString(36) } };

	spec.m_views = { dressed };
	return spec;
}

const ibDialectDictionary& SqliteQ() { return ibDatabaseLayerSQLite::Dialect(); }

wxString SqlOf(const ibQueryRelPtr& rel)
{
	return ibQueryRenderer(SqliteQ()).Render(ibQueryIR(ibProject(rel))).m_sql;
}

// A balance as of `moment`, cut at the start of its day — off the rows as they stand, or (rows = false) off
// the dressed view the way every reading went before.
ibMaterializeReadSpec BalanceRead(bool rows, const wxDateTime& moment, bool excluding)
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
	r.m_floor        = ibValue(ibTruncateToPeriod(moment, ibTotalsPeriod::Day));
	r.m_dropZeroRows = true;
	r.m_columns = {
		{ wxT("Qty_Balance"), wxT("Qty_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true },
	};
	return r;
}

wxString ReadSql(const ibMaterializeReadSpec& spec)
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
	const wxString sql = SqlOf(RenderStoredRows(spec, spec.m_views.front()));

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
	const wxString sql = SqlOf(RenderMovementRows(spec, spec.m_views.front()));

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
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	const wxString sql = ReadSql(BalanceRead(/*rows*/ true, moment, /*excluding*/ true));

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
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	const wxString sql = ReadSql(BalanceRead(/*rows*/ false, moment, true));
	EXPECT_FALSE(sql.Contains(wxT("UNION ALL"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("_cut"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("FROM Reg9_Turnovers"))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NULL"))) << sql;        // the cut is still there, as the one condition it was
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NOT NULL"))) << sql;
}

// A reading that stops AT the grain needs no movements: no floor, no union — the stored rows alone.
TEST(MaterializeCutRead, AtTheGrainOnlyTheStoredRowsAreRead) {
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, wxDateTime(5, wxDateTime::Mar, 2026), false);
	r.m_floor = ibValue();   // what ibRegFillArmCut leaves when the bound does not reach inside a grain
	const wxString sql = ReadSql(r);
	EXPECT_FALSE(sql.Contains(wxT("UNION ALL"))) << sql;
	EXPECT_FALSE(sql.Contains(wxT("FROM Reg9 "))) << sql;
	EXPECT_TRUE(sql.Contains(wxT("Reg9_T"))) << sql;
}

// 🛑 Stored rows cannot be cut without the movements: the partial grain would be read from nowhere, a balance
// short of today's postings. Refused, not answered.
TEST(MaterializeCutRead, StoredRowsWithoutTheMovementsAreNotCut) {
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, wxDateTime(5, wxDateTime::Mar, 2026, 14, 0, 0), true);
	r.m_movedRows = nullptr;
	EXPECT_ANY_THROW(RenderMaterializedRead(r, wxT("b")));
}

// The caller's filters narrow EACH half — that is what lets "this warehouse" reach an index in both — and are
// NOT said a third time around them: the relation the halves publish carries only the columns the reading
// names, and a filter is free to name another.
TEST(MaterializeCutRead, FiltersNarrowEachHalf) {
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("b_rows"), wxT("wh")), ibConst(ibValue(wxString(wxT("kitchen"))))) };
	const wxString sql = ReadSql(r);

	int seen = 0;
	for (size_t at = 0; (at = sql.find(wxT("b_rows.wh = "), at)) != wxString::npos; at += 5)
		++seen;
	EXPECT_EQ(seen, 2) << sql;   // once in each half, and not around them
}

// A turnover looks at nothing below its interval, and the stored half is told so: bounded on both sides, the
// period can ride the index instead of walking the warehouse's whole history to add zeros.
TEST(MaterializeCutRead, ATurnoverBoundsItsStoredHalfBelow) {
	const wxDateTime from(3, wxDateTime::Mar, 2026);
	const wxDateTime to(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, to, false);
	r.m_from = ibValue(from);
	r.m_columns = {
		{ wxT("Qty_Moved"), wxT("Qty_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
	};
	// The stored half: from the totals table up to the UNION — its rows' subquery, then its own WHERE.
	const auto storedHalf = [](const wxString& sql) {
		const size_t fromAt = sql.find(wxT("Reg9_T"));
		const size_t unionAt = sql.find(wxT("UNION ALL"));
		return fromAt == wxString::npos || unionAt == wxString::npos || unionAt < fromAt
			? wxString() : sql.Mid(fromAt, unionAt - fromAt);
	};
	const wxString turnover = storedHalf(ReadSql(r));
	ASSERT_FALSE(turnover.IsEmpty());
	EXPECT_TRUE(turnover.Contains(wxT("period_ >= "))) << turnover;

	// A balance reads the whole history — its stored half has no lower bound to be given.
	const wxString balance = storedHalf(ReadSql(BalanceRead(/*rows*/ true, to, false)));
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
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

		db->RunQuery(wxT("CREATE TABLE Reg9 (rec_ TEXT NOT NULL, line_ INTEGER NOT NULL, period_ TEXT NOT NULL, ")
			wxT("active_ INTEGER NOT NULL, wh TEXT NOT NULL, rectype_ INTEGER NOT NULL, qty NUMERIC NOT NULL, ")
			wxT("PRIMARY KEY (rec_, line_))"));
		db->RunQuery(wxT("CREATE TABLE Reg9_T (period_ TEXT NOT NULL, wh TEXT NOT NULL, ")
			wxT("qty_in NUMERIC NOT NULL DEFAULT 0, qty_out NUMERIC NOT NULL DEFAULT 0, PRIMARY KEY (period_, wh))"));

		// The maintenance — the triggers that keep Reg9_T, and the dressed view the old road reads.
		const ibMaterializeSql sql = RenderMaterialization(CutSpec(), &ibDatabaseLayerSQLite::MaterializationDialect(), SqliteQ());
		ASSERT_TRUE(sql.Apply(*db));
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}

	void Move(const wxString& rec, int line, const wxString& when, const wxString& wh, bool receipt, double qty, bool active = true) {
		db->RunQuery(wxT("INSERT INTO Reg9 VALUES ('%s', %d, '%s', %d, '%s', %d, %s)"),
			rec, line, when, active ? 1 : 0, wh, receipt ? 1 : 0, wxString::FromCDouble(qty));
	}

	// The balance of one warehouse through a rendered read; -1e9 when the warehouse has no row at all.
	double Balance(const ibMaterializeReadSpec& spec, const wxString& wh) {
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
	double ByHand(const wxString& wh, const wxString& upTo, bool excluding) {
		double sum = 0.0;
		ibDatabaseResultSet* rs = db->RunQueryWithResults(wxT("%s"), wxString::Format(
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
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);

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
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ true, moment, true), wxT("kitchen")), 100.0);
}

// The other warehouse is untouched by the day being cut — its answer comes off the stored half alone.
TEST_F(CutReadFix, AKeyWithNoMovementsInTheGrainReadsItsStoredRows) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ true, moment, true), wxT("bar")), 40.0);
}

// A filter narrows both halves and must not lose the key it names.
TEST_F(CutReadFix, AFilterOnTheKeyKeepsThatKeyWhole) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("b_rows"), wxT("wh")), ibConst(ibValue(wxString(wxT("kitchen"))))) };

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
	kinds.From(ibScan(wxT("Kinds"), wxT("k"))).Project({ ibQueryProjItem{ ibCol(wxT("k"), wxT("wh")), wxString() } });
	kinds.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("k"), wxT("wh")), ibCol(wxT("b_rows"), wxT("wh"))));
	kinds.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("k"), wxT("kind")), ibConst(ibValue(wxString(wxT("hot"))))));

	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	for (const bool inside : { true, false }) {   // a cut (two halves) and a reading at the grain (one)
		ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, inside ? moment : wxDateTime(5, wxDateTime::Mar, 2026), true);
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
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);

	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ false, moment, false), wxT("kitchen")), -1e9);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(/*rows*/ true, moment, false), wxT("kitchen")), -1e9);
}

// A filter may name a column the reading does not carry. Said around the halves as well as inside them, it
// named a column their relation does not publish, and the read failed where the single view had answered.
TEST_F(CutReadFix, AFilterOnAColumnTheReadingDoesNotCarryStillReads) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(/*rows*/ true, moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Ge, ibCol(wxT("b_rows"), wxT("Qty_Receipt")), ibConst(ibValue(ibNumber(0)))) };   // every row passes

	EXPECT_DOUBLE_EQ(Balance(r, wxT("kitchen")), 100.0);
}

// ⭐ THE TURNOVER OF AN INTERVAL, THREE ROADS AGAIN — with the stored half bounded below. 04.03 00:00 through
// 05.03 14:00 inclusive: +30 -25 +7 -9; the day before and the evening after belong to nobody.
TEST_F(CutReadFix, ATurnoverBoundedBelowAnswersWhatTheMovementsSay) {
	Fill();
	const wxDateTime from(4, wxDateTime::Mar, 2026);
	const wxDateTime to(5, wxDateTime::Mar, 2026, 14, 0, 0);

	const auto read = [&](bool rows) {
		ibMaterializeReadSpec r = BalanceRead(rows, to, /*excluding*/ false);
		r.m_from = ibValue(from);
		r.m_columns = {
			{ wxT("Qty_Balance"), wxT("Qty_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
		};
		return Balance(r, wxT("kitchen"));
	};
	const double hand = ByHand(wxT("kitchen"), wxT("2026-03-05 14:00:00"), false) - ByHand(wxT("kitchen"), wxT("2026-03-04 00:00:00"), true);

	EXPECT_DOUBLE_EQ(hand, 3.0);
	EXPECT_DOUBLE_EQ(read(/*rows*/ false), hand);
	EXPECT_DOUBLE_EQ(read(/*rows*/ true), hand);
}
