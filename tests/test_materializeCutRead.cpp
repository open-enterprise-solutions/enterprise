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
//   * the renderer — a view of the rows as they stand computes nothing per row and groups nothing,
//     and a movements-only view has no stored arm at all;
//   * the reader — a cut becomes a UNION ALL of two selections, each narrowed on its own, each read
//     from its own relation when the surface keeps them apart;
//   * and the NUMBERS — the same balance through the old single view, through the two halves, and
//     summed by hand from the movements, on a live SQLite with split totals on and off.
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
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"   // a dialect that really splits totals

namespace {

// One register: a warehouse key, one quantity, a recorder mark that only a movement carries.
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
	spec.m_deltas = {
		{ wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 1 THEN {row}.qty ELSE 0 END") },
		{ wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 1 THEN 0 ELSE {row}.qty END") },
	};

	const std::vector<ibMaterializeViewColumn> figures = {
		{ wxT("Qty_Receipt"),  wxT("qty_in"),  wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Expense"),  wxT("qty_out"), wxString(),     ibMaterializeAgg::Value },
		{ wxT("Qty_Turnover"), wxT("qty_in"),  wxT("qty_out"), ibMaterializeAgg::Difference },
	};
	const std::vector<std::pair<wxString, ibColumnType>> marks = { { wxT("rec_"), ibTypeString(36) } };

	ibMaterializeView dressed;
	dressed.m_name = wxT("Reg9_Turnovers");
	dressed.m_columns = figures;
	dressed.m_withMovements = true;
	dressed.m_movementColumns = marks;

	ibMaterializeView stored;
	stored.m_name = wxT("Reg9_Flow");
	stored.m_columns = figures;
	stored.m_rawRows = true;
	stored.m_movementColumns = marks;      // published as typed nulls — one column list for both halves

	ibMaterializeView moved;
	moved.m_name = wxT("Reg9_FlowMoved");
	moved.m_columns = figures;
	moved.m_rawRows = true;
	moved.m_movementsOnly = true;
	moved.m_movementColumns = marks;
	moved.m_movementWhere = wxT("{row}.period_ IS NOT NULL");

	spec.m_views = { dressed, stored, moved };
	return spec;
}

const ibMaterializationDialect& Sqlite()  { return ibDatabaseLayerSQLite::MaterializationDialect(); }
const ibDialectDictionary&      SqliteQ() { return ibDatabaseLayerSQLite::Dialect(); }

// The CREATE VIEW of one named view out of the bundle.
wxString ViewText(const ibMaterializeSpec& spec, const wxString& name)
{
	const ibMaterializeSql sql = RenderMaterialization(spec, &Sqlite(), SqliteQ());
	const wxString all = sql.CreateText();
	const size_t at = all.find(wxT("VIEW ") + name + wxT(" "));
	if (at == wxString::npos)
		return wxString();
	const size_t next = all.find(wxT("CREATE "), at);
	return all.Mid(at, next == wxString::npos ? wxString::npos : next - at);
}

// A balance as of `moment`, cut at the start of its day. `movedView` empty = both halves of the dressed view.
ibMaterializeReadSpec BalanceRead(const wxString& view, const wxString& movedView, const wxDateTime& moment, bool excluding)
{
	ibMaterializeReadSpec r;
	r.m_view         = view;
	r.m_viewMoved    = movedView;
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
	const ibQueryRelPtr rel = RenderMaterializedRead(spec, wxT("src"));
	return ibQueryRenderer(SqliteQ()).Render(ibQueryIR(ibProject(rel))).m_sql;
}

} // namespace

// =============================================================================
// The renderer
// =============================================================================

// The dressed view projects the coarser units — that is what it is FOR. The rows as they stand do not:
// each unit is a date expression evaluated on every stored row of every read, asked for or not.
TEST(MaterializeRawRows, ProjectsThePeriodAloneAndNothingComputedFromIt) {
	const ibMaterializeSpec spec = CutSpec();
	const wxString dressed = ViewText(spec, wxT("Reg9_Turnovers"));
	const wxString raw     = ViewText(spec, wxT("Reg9_Flow"));

	ASSERT_FALSE(dressed.IsEmpty());
	ASSERT_FALSE(raw.IsEmpty());
	EXPECT_TRUE(dressed.Contains(wxT("period__Month")));
	EXPECT_FALSE(raw.Contains(wxT("period__Month")));
	EXPECT_FALSE(raw.Contains(wxT("period__Year")));
	EXPECT_TRUE(raw.Contains(wxT("period_")));           // the period itself is still there
	EXPECT_TRUE(raw.Contains(wxT("Qty_Turnover")));      // and the figures under the same names
}

// ⭐ NOTHING IS GROUPED, EVEN SPLIT. A GROUP BY in a view is a line the engine will not push a condition
// under, so "this warehouse" was decided after every warehouse had been summed. The reader of these rows
// folds them itself — once, above a condition that was free to reach an index.
TEST(MaterializeRawRows, NeverGroupsEvenWhenTheTotalsAreSplit) {
	ibMaterializeSpec spec = CutSpec();
	spec.m_shards = 4;
	const ibMaterializationDialect& pg = ibDatabaseLayerPostgres::MaterializationDialect();
	const ibMaterializeSql sql = RenderMaterialization(spec, &pg, ibDatabaseLayerPostgres::Dialect());
	const wxString all = sql.CreateText();

	const size_t rawAt = all.find(wxT("VIEW Reg9_Flow "));
	ASSERT_NE(rawAt, wxString::npos);
	const size_t rawEnd = all.find(wxT("CREATE "), rawAt);
	const wxString raw = all.Mid(rawAt, rawEnd == wxString::npos ? wxString::npos : rawEnd - rawAt);
	EXPECT_FALSE(raw.Contains(wxT("GROUP BY")));
	EXPECT_FALSE(raw.Contains(wxT("SUM(")));

	const size_t dressedAt = all.find(wxT("VIEW Reg9_Turnovers "));
	ASSERT_NE(dressedAt, wxString::npos);
	const size_t dressedEnd = all.find(wxT("CREATE "), dressedAt);
	const wxString dressed = all.Mid(dressedAt, dressedEnd == wxString::npos ? wxString::npos : dressedEnd - dressedAt);
	EXPECT_TRUE(dressed.Contains(wxT("GROUP BY")));      // the dressed view still absorbs the split
}

// The stored half reads the totals and no movements; the movement half reads the movements and no totals.
TEST(MaterializeRawRows, TheTwoHalvesAreTwoRelations) {
	const ibMaterializeSpec spec = CutSpec();
	const wxString stored = ViewText(spec, wxT("Reg9_Flow"));
	const wxString moved  = ViewText(spec, wxT("Reg9_FlowMoved"));

	EXPECT_TRUE(stored.Contains(wxT(" FROM Reg9_T")));
	EXPECT_FALSE(stored.Contains(wxT("UNION ALL")));
	EXPECT_TRUE(stored.Contains(wxT("CAST(NULL AS")));   // the mark column, published as a typed null

	EXPECT_TRUE(moved.Contains(wxT(" FROM Reg9 ")) || moved.EndsWith(wxT(" FROM Reg9")) || moved.Contains(wxT(" FROM Reg9\n")));
	EXPECT_FALSE(moved.Contains(wxT("Reg9_T")));
	EXPECT_FALSE(moved.Contains(wxT("UNION ALL")));
}

// The arm's own condition is ANDed to the guard — it narrows, and never replaces, what is in force.
TEST(MaterializeRawRows, TheMovementConditionJoinsTheGuard) {
	const wxString moved = ViewText(CutSpec(), wxT("Reg9_FlowMoved"));
	EXPECT_TRUE(moved.Contains(wxT("Reg9.active_ <> 0")));
	EXPECT_TRUE(moved.Contains(wxT("Reg9.period_ IS NOT NULL")));
	EXPECT_FALSE(moved.Contains(wxT("{row}")));
}

// …and a guard with an OR of its own lends the AND all of itself, not its last term.
TEST(MaterializeRawRows, AGuardWithAnOrIsTakenWhole) {
	ibMaterializeSpec spec = CutSpec();
	spec.m_guard = wxT("{row}.active_ = 1 OR {row}.active_ = 2");
	const wxString moved = ViewText(spec, wxT("Reg9_FlowMoved"));
	EXPECT_TRUE(moved.Contains(wxT("(Reg9.active_ = 1 OR Reg9.active_ = 2) AND (Reg9.period_ IS NOT NULL)"))) << moved;
}

// =============================================================================
// The reader
// =============================================================================

// A cut inside the grain is read as two selections. The bound of each stands in ITS OWN WHERE, which is
// the whole point: under an OR neither could ride an index.
TEST(MaterializeCutRead, ACutIsAUnionOfTwoNarrowedSelections) {
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	const wxString sql = ReadSql(BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, /*excluding*/ true));

	EXPECT_TRUE(sql.Contains(wxT("UNION ALL")));
	EXPECT_TRUE(sql.Contains(wxT("FROM Reg9_Flow")));
	EXPECT_TRUE(sql.Contains(wxT("FROM Reg9_FlowMoved")));
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NULL")));
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NOT NULL")));
}

// With no separate relation for the movements both halves read the one view — the road every other
// register still takes — and they are still read apart.
TEST(MaterializeCutRead, WithoutASecondRelationBothHalvesReadTheView) {
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	const wxString sql = ReadSql(BalanceRead(wxT("Reg9_Turnovers"), wxString(), moment, true));
	EXPECT_TRUE(sql.Contains(wxT("UNION ALL")));
	EXPECT_FALSE(sql.Contains(wxT("Reg9_FlowMoved")));
}

// A reading that stops AT the grain needs no movements: no floor, no union — the stored rows alone.
TEST(MaterializeCutRead, AtTheGrainNothingIsUnioned) {
	ibMaterializeReadSpec r = BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), wxDateTime(5, wxDateTime::Mar, 2026), false);
	r.m_floor = ibValue();   // what ibRegFillArmCut leaves when the bound does not reach inside a grain
	const wxString sql = ReadSql(r);
	EXPECT_FALSE(sql.Contains(wxT("UNION ALL")));
	EXPECT_TRUE(sql.Contains(wxT("rec_ IS NULL")));
}

// The caller's filters narrow EACH half — that is what lets "this warehouse" reach an index in both — and
// are NOT said a third time around them: the relation the halves publish carries only the columns the reading
// names, and a filter is free to name another (an accounting register's correspondence, say).
TEST(MaterializeCutRead, FiltersNarrowEachHalf) {
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("wh")), ibConst(ibValue(wxString(wxT("kitchen"))))) };
	const wxString sql = ReadSql(r);

	int seen = 0;
	for (size_t at = 0; (at = sql.find(wxT("wh = "), at)) != wxString::npos; at += 5)
		++seen;
	EXPECT_EQ(seen, 2);   // once in each half, and not around them
}

// A turnover looks at nothing below its interval, and the stored half is told so: bounded on both sides, the
// period can ride the index instead of walking the warehouse's whole history to add zeros.
TEST(MaterializeCutRead, ATurnoverBoundsItsStoredHalfBelow) {
	const wxDateTime from(3, wxDateTime::Mar, 2026);
	const wxDateTime to(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), to, false);
	r.m_from = ibValue(from);
	r.m_columns = {
		{ wxT("Qty_Moved"), wxT("Qty_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
	};
	// The stored half's own WHERE: from its FROM up to the UNION (the projection before it names the
	// interval too, inside each figure's CASE — that is not the bound being looked for).
	const auto storedHalf = [](const wxString& sql) {
		const size_t fromAt = sql.find(wxT("FROM Reg9_Flow "));
		const size_t unionAt = sql.find(wxT("UNION ALL"));
		return fromAt == wxString::npos || unionAt == wxString::npos || unionAt < fromAt
			? wxString() : sql.Mid(fromAt, unionAt - fromAt);
	};
	const wxString turnover = storedHalf(ReadSql(r));
	ASSERT_FALSE(turnover.IsEmpty());
	EXPECT_TRUE(turnover.Contains(wxT("period_ >= "))) << turnover;

	// A balance reads the whole history — its stored half has no lower bound to be given.
	const wxString balance = storedHalf(ReadSql(BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), to, false)));
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

		const ibMaterializeSql sql = RenderMaterialization(CutSpec(), &Sqlite(), SqliteQ());
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

// ⭐ ONE BALANCE, THREE ROADS. The dressed view read both halves at once (the old road), the two relations
// read half by half (the new one), the movements summed by hand. Excluding the moment and including it —
// the movement standing exactly AT the moment is the row the two differ by.
TEST_F(CutReadFix, TheHalvesAnswerWhatTheSingleViewAnswered) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);

	for (const bool excluding : { true, false }) {
		const double hand   = ByHand(wxT("kitchen"), wxT("2026-03-05 14:00:00"), excluding);
		const double oldWay = Balance(BalanceRead(wxT("Reg9_Turnovers"), wxString(), moment, excluding), wxT("kitchen"));
		const double newWay = Balance(BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, excluding), wxT("kitchen"));

		EXPECT_DOUBLE_EQ(hand, excluding ? 100.0 : 91.0);   // 100 - 12 + 30 - 25 + 7 [- 9]
		EXPECT_DOUBLE_EQ(oldWay, hand) << "the dressed view, excluding = " << excluding;
		EXPECT_DOUBLE_EQ(newWay, hand) << "the two halves, excluding = " << excluding;
	}
}

// The other warehouse is untouched by the day being cut — its answer comes off the stored half alone.
TEST_F(CutReadFix, AKeyWithNoMovementsInTheGrainReadsItsStoredRows) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, true), wxT("bar")), 40.0);
}

// A filter narrows both halves and must not lose the key it names.
TEST_F(CutReadFix, AFilterOnTheKeyKeepsThatKeyWhole) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("wh")), ibConst(ibValue(wxString(wxT("kitchen"))))) };

	EXPECT_DOUBLE_EQ(Balance(r, wxT("kitchen")), 100.0);
	EXPECT_DOUBLE_EQ(Balance(r, wxT("bar")), -1e9);   // filtered out — no row at all
}

// A key whose movements net to zero is NO ROW (m_dropZeroRows) — through the halves as through the view.
TEST_F(CutReadFix, AZeroBalanceIsNoRowOnEitherRoad) {
	Move(wxT("r1"), 1, wxT("2026-03-04 09:00:00"), wxT("kitchen"), true, 10);
	Move(wxT("w1"), 1, wxT("2026-03-05 10:00:00"), wxT("kitchen"), false, 10);
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);

	EXPECT_DOUBLE_EQ(Balance(BalanceRead(wxT("Reg9_Turnovers"), wxString(), moment, false), wxT("kitchen")), -1e9);
	EXPECT_DOUBLE_EQ(Balance(BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, false), wxT("kitchen")), -1e9);
}

// A filter may name a column the reading does not carry. Said around the halves as well as inside them, it
// named a column their relation does not publish, and the read failed where the single view had answered.
TEST_F(CutReadFix, AFilterOnAColumnTheReadingDoesNotCarryStillReads) {
	Fill();
	const wxDateTime moment(5, wxDateTime::Mar, 2026, 14, 0, 0);
	ibMaterializeReadSpec r = BalanceRead(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved"), moment, true);
	r.m_filters = { ibBinOp(ibQueryBinOp::Ge, ibCol(wxT("Qty_Receipt")), ibConst(ibValue(ibNumber(0)))) };   // every row passes

	EXPECT_DOUBLE_EQ(Balance(r, wxT("kitchen")), 100.0);
}

// ⭐ THE TURNOVER OF AN INTERVAL, THREE ROADS AGAIN — with the stored half bounded below. 04.03 00:00 through
// 05.03 14:00 inclusive: +30 -25 +7 -9; the day before and the evening after belong to nobody.
TEST_F(CutReadFix, ATurnoverBoundedBelowAnswersWhatTheMovementsSay) {
	Fill();
	const wxDateTime from(4, wxDateTime::Mar, 2026);
	const wxDateTime to(5, wxDateTime::Mar, 2026, 14, 0, 0);

	const auto read = [&](const wxString& view, const wxString& moved) {
		ibMaterializeReadSpec r = BalanceRead(view, moved, to, /*excluding*/ false);
		r.m_from = ibValue(from);
		r.m_columns = {
			{ wxT("Qty_Balance"), wxT("Qty_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true },
		};
		return Balance(r, wxT("kitchen"));
	};
	const double hand = ByHand(wxT("kitchen"), wxT("2026-03-05 14:00:00"), false) - ByHand(wxT("kitchen"), wxT("2026-03-04 00:00:00"), true);

	EXPECT_DOUBLE_EQ(hand, 3.0);
	EXPECT_DOUBLE_EQ(read(wxT("Reg9_Turnovers"), wxString()), hand);
	EXPECT_DOUBLE_EQ(read(wxT("Reg9_Flow"), wxT("Reg9_FlowMoved")), hand);
}
