// =============================================================================
// OES Enterprise — register totals: NUMERIC parity against the live aggregation
//
// register-totals-strategy.md § "What remains" names this as the open question:
// the trigger-maintained totals are live, but nobody has proven they hold the
// SAME NUMBERS the movements do. test_totalsViewParity covers the SHAPE half
// (columns, granularity, which rows may be emitted) and says outright that the
// value half "needs a live engine and belongs in the SQLite integration target".
// This is that file.
//
// The method is the same one ibDerivedState::VerifyLastPeriod uses in production:
// re-aggregate the movements and compare key by key with what the totals hold.
// Here the whole thing runs on an in-memory SQLite with the REAL rendered bundle
// (RenderMaterialization -> Apply), so the triggers under test are the ones that
// ship — not a re-implementation of what they ought to do.
//
// The live aggregation is the ORACLE: it is a direct reading of the source of
// truth, so wherever the two disagree, the materialised side is wrong.
//
// What this catches is the class of bug that commits happily and reconciles to
// nothing a year later: a delta that replaces instead of accumulating, an update
// that adds the new value without backing out the old, a delete that forgets the
// sign, a key that moves between periods and leaves its old bucket dirty.
// =============================================================================

#include <gtest/gtest.h>

#include <map>
#include <utility>

#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

// (period, warehouse) -> (received, spent)
using Totals = std::map<std::pair<wxString, wxString>, std::pair<double, double>>;

// The register shape: movements keyed by month + warehouse, one resource stored as a
// received / spent pair, the side chosen by the record type. Mirrors the fixture in
// test_databaseMaterializeBuilder.cpp so the two files describe the same register.
ibMaterializeSpec MakeSpec()
{
	ibMaterializeSpec spec;
	spec.m_table            = wxT("Reg7_T");
	spec.m_source           = wxT("Reg7");
	spec.m_keyColumns       = { wxT("wh") };
	spec.m_periodColumn     = wxT("period_");
	spec.m_periodSourceExpr = wxT("{row}.period_");
	spec.m_periodUnit       = ibTotalsPeriod::Month;
	spec.m_deltas = {
		{ wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 0 THEN {row}.qty ELSE 0 END") },
		{ wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 0 THEN 0 ELSE {row}.qty END") },
	};
	return spec;
}

// A live register: the movement table, the derived table, and the REAL trigger bundle.
// The derived table's PRIMARY KEY is what the upsert's ON CONFLICT targets — without it
// the triggers would insert duplicate rows instead of accumulating into one.
struct TotalsFix : ::testing::Test
{
	ibDatabaseLayerSQLite db;
	bool ready = false;

	void SetUp() override
	{
		if (!db.Open(wxT(":memory:"))) {
			GTEST_SKIP() << "in-memory SQLite unavailable";
			return;
		}
		db.RunQuery(wxT("CREATE TABLE Reg7 ("
			"id INTEGER PRIMARY KEY, period_ TEXT, wh TEXT, qty NUMERIC, rectype_ INTEGER)"));
		db.RunQuery(wxT("CREATE TABLE Reg7_T ("
			"period_ TEXT NOT NULL, wh TEXT NOT NULL, "
			"qty_in NUMERIC NOT NULL DEFAULT 0, qty_out NUMERIC NOT NULL DEFAULT 0, "
			"PRIMARY KEY (period_, wh))"));

		const ibMaterializeSql sql = RenderMaterialization(
			MakeSpec(),
			&ibDatabaseLayerSQLite::MaterializationDialect(),
			ibDatabaseLayerSQLite::Dialect());
		ASSERT_TRUE(sql.Apply(db)) << "the rendered maintenance bundle must install";
		ready = true;
	}

	void TearDown() override { db.Close(); }

	// --- movements ---------------------------------------------------------
	void Move(int id, const wxChar* period, const wxChar* wh, double qty, int rectype)
	{
		db.RunQuery(wxT("INSERT INTO Reg7 (id, period_, wh, qty, rectype_) VALUES (%d, '%s', '%s', %s, %d)"),
			id, period, wh, wxString::FromCDouble(qty), rectype);
	}
	void Retype(int id, int rectype) { db.RunQuery(wxT("UPDATE Reg7 SET rectype_ = %d WHERE id = %d"), rectype, id); }
	void Requantity(int id, double qty) {
		db.RunQuery(wxT("UPDATE Reg7 SET qty = %s WHERE id = %d"), wxString::FromCDouble(qty), id);
	}
	void Rewarehouse(int id, const wxChar* wh) { db.RunQuery(wxT("UPDATE Reg7 SET wh = '%s' WHERE id = %d"), wh, id); }
	void Reperiod(int id, const wxChar* period) { db.RunQuery(wxT("UPDATE Reg7 SET period_ = '%s' WHERE id = %d"), period, id); }
	void Erase(int id) { db.RunQuery(wxT("DELETE FROM Reg7 WHERE id = %d"), id); }

	// --- the two sides -----------------------------------------------------

	// THE ORACLE: aggregate the movements directly, truncating the period exactly as the
	// dialect's Month rule does. Anything the totals disagree with is a totals bug.
	Totals FromMovements()
	{
		return Read(wxT(
			"SELECT strftime('%Y-%m-01 00:00:00', period_) AS p, wh AS w,"
			"       SUM(CASE WHEN rectype_ = 0 THEN qty ELSE 0 END) AS a,"
			"       SUM(CASE WHEN rectype_ = 0 THEN 0 ELSE qty END) AS b"
			"  FROM Reg7 GROUP BY p, w"));
	}

	// The maintained side, with all-zero keys dropped: a key whose movements cancel out
	// leaves a zero ROW behind (the triggers never delete), while the oracle simply has
	// no such group. That is a storage detail, not a numeric disagreement — see the
	// dedicated test below, which asserts the zero row exists on purpose.
	Totals FromTotals()
	{
		return Read(wxT(
			"SELECT period_ AS p, wh AS w, qty_in AS a, qty_out AS b"
			"  FROM Reg7_T WHERE qty_in <> 0 OR qty_out <> 0"));
	}

	Totals Read(const wxString& query)
	{
		Totals out;
		// The query text goes in as a %s ARGUMENT: it contains '%' (strftime masks), which
		// would otherwise be read as printf specifiers.
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), query);
		if (rs == nullptr)
			return out;
		while (rs->Next()) {
			out[{ rs->GetResultString(wxT("p")), rs->GetResultString(wxT("w")) }] =
				{ rs->GetResultDouble(wxT("a")), rs->GetResultDouble(wxT("b")) };
		}
		db.CloseResultSet(rs);
		return out;
	}

	double Cell(const wxChar* period, const wxChar* wh, const wxChar* column)
	{
		ibDatabaseResultSet* rs = db.RunQueryWithResults(
			wxT("SELECT %s AS v FROM Reg7_T WHERE period_ = '%s' AND wh = '%s'"), column, period, wh);
		if (rs == nullptr)
			return -1.0;
		const double v = rs->Next() ? rs->GetResultDouble(wxT("v")) : -1.0;
		db.CloseResultSet(rs);
		return v;
	}

	// The comparison VerifyLastPeriod makes: key by key, in both directions — a key the
	// movements know and the totals do not, and a figure nothing accounts for.
	void ExpectParity(const char* what)
	{
		const Totals oracle = FromMovements();
		const Totals stored = FromTotals();

		for (const auto& kv : oracle) {
			const auto it = stored.find(kv.first);
			ASSERT_TRUE(it != stored.end())
				<< what << ": the movements know (" << kv.first.first.ToStdString() << ", "
				<< kv.first.second.ToStdString() << ") and the totals do not";
			EXPECT_DOUBLE_EQ(it->second.first,  kv.second.first)
				<< what << ": received differs at " << kv.first.second.ToStdString();
			EXPECT_DOUBLE_EQ(it->second.second, kv.second.second)
				<< what << ": spent differs at " << kv.first.second.ToStdString();
		}
		for (const auto& kv : stored) {
			EXPECT_TRUE(oracle.find(kv.first) != oracle.end())
				<< what << ": the totals hold a figure nothing accounts for at ("
				<< kv.first.first.ToStdString() << ", " << kv.first.second.ToStdString() << ")";
		}
	}
};

const wxChar* const JAN = wxT("2026-01-14 10:00:00");
const wxChar* const JAN2 = wxT("2026-01-28 09:30:00");
const wxChar* const FEB = wxT("2026-02-03 12:00:00");
const wxChar* const MJAN = wxT("2026-01-01 00:00:00");
const wxChar* const MFEB = wxT("2026-02-01 00:00:00");

const int kIn = 0, kOut = 1;

} // namespace

// Inserts accumulate rather than replace — two movements into one key must SUM.
// A trigger that overwrote would pass every single-row smoke test.
TEST_F(TotalsFix, InsertsAccumulate)
{
	if (!ready) return;
	Move(1, JAN,  wxT("W1"), 10, kIn);
	Move(2, JAN2, wxT("W1"),  4, kIn);
	Move(3, JAN,  wxT("W1"),  3, kOut);
	Move(4, FEB,  wxT("W2"),  7, kIn);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")),  14.0);
	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_out")),  3.0);
	ExpectParity("inserts");
}

// Movements dated within one month collapse to ONE stored key — the period is
// truncated by the dialect's Month rule, not stored verbatim.
TEST_F(TotalsFix, PeriodTruncationGroupsTheMonth)
{
	if (!ready) return;
	Move(1, JAN,  wxT("W1"), 5, kIn);
	Move(2, JAN2, wxT("W1"), 6, kIn);

	EXPECT_EQ(FromTotals().size(), (size_t)1) << "two January movements, one January bucket";
	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")), 11.0);
	ExpectParity("period truncation");
}

// An UPDATE must back the OLD row out and apply the NEW one. Adding the new value
// without reversing the old is the classic maintained-totals bug.
TEST_F(TotalsFix, UpdateReversesTheOldRow)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"), 10, kIn);
	Requantity(1, 25);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")), 25.0) << "not 35 — the old 10 must be reversed";
	ExpectParity("update quantity");
}

// Retyping a movement moves its figure to the OTHER side of the same key.
TEST_F(TotalsFix, UpdateMovesBetweenSides)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"), 8, kIn);
	Retype(1, kOut);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")),  0.0);
	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_out")), 8.0);
	ExpectParity("update record type");
}

// An update that changes the KEY has to unwind the old key and build the new one —
// the case where a stale bucket survives if the reversal keys off the new values.
TEST_F(TotalsFix, UpdateAcrossKeysUnwindsTheOldKey)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"), 12, kIn);
	Rewarehouse(1, wxT("W2"));
	ExpectParity("update across warehouses");

	Reperiod(1, FEB);
	ExpectParity("update across periods");
	EXPECT_DOUBLE_EQ(Cell(MFEB, wxT("W2"), wxT("qty_in")), 12.0);
}

// A DELETE subtracts. Dropping the sign here is invisible until a reconciliation.
TEST_F(TotalsFix, DeleteSubtracts)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"), 10, kIn);
	Move(2, JAN, wxT("W1"),  4, kIn);
	Erase(1);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")), 4.0);
	ExpectParity("delete");
}

// Deleting everything for a key leaves a ZERO ROW, not a missing one — the triggers
// subtract, they never remove. Asserted deliberately so the behaviour is a decision
// on the record: a reader that treats "no row" as "no data" must therefore also
// treat an all-zero row as no data (which is what m_dropZeroRows is for).
TEST_F(TotalsFix, EmptiedKeyLeavesAZeroRowNotAMissingOne)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"), 10, kIn);
	Erase(1);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")), 0.0) << "the row survives, holding zero";
	ExpectParity("emptied key");
}

// The whole traffic mix in one run — inserts, both kinds of update, deletes, several
// keys and two periods. This is the closest a unit test gets to what
// VerifyLastPeriod does against real traffic.
TEST_F(TotalsFix, MixedTrafficStaysInParity)
{
	if (!ready) return;
	Move(1, JAN,  wxT("W1"), 10.5, kIn);
	Move(2, JAN2, wxT("W1"),  2.25, kOut);
	Move(3, JAN,  wxT("W2"),  7, kIn);
	Move(4, FEB,  wxT("W1"),  3, kIn);
	Move(5, FEB,  wxT("W2"),  9, kOut);
	ExpectParity("after inserts");

	Requantity(1, 12.75);
	Retype(3, kOut);
	Rewarehouse(4, wxT("W2"));
	Reperiod(2, FEB);
	ExpectParity("after updates");

	Erase(5);
	Move(6, FEB, wxT("W1"), 1.5, kIn);
	ExpectParity("after delete + late insert");
}

// A backdated entry — a movement inserted into a period that already has totals —
// must land in ITS period, not the latest one.
TEST_F(TotalsFix, BackdatedEntryLandsInItsOwnPeriod)
{
	if (!ready) return;
	Move(1, FEB, wxT("W1"), 5, kIn);
	Move(2, JAN, wxT("W1"), 8, kIn);   // backdated, after February already exists

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")), 8.0);
	EXPECT_DOUBLE_EQ(Cell(MFEB, wxT("W1"), wxT("qty_in")), 5.0);
	ExpectParity("backdated entry");
}

// Fractional quantities must not be rounded on the way into the totals — a register
// storing money or weight is the normal case, not an edge one.
TEST_F(TotalsFix, FractionalQuantitiesSurvive)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"), 0.1, kIn);
	Move(2, JAN, wxT("W1"), 0.2, kIn);

	EXPECT_NEAR(Cell(MJAN, wxT("W1"), wxT("qty_in")), 0.3, 1e-9);
	ExpectParity("fractional quantities");
}

// ⭐⭐ A REVERSAL COLLAPSES THE FIGURE INSTEAD OF GROWING IT.
//
// `+10` then `-10` on the same side is a storno. The sign travels with the VALUE, not with the
// record type, so the second movement REDUCES the receipt -- it does not become an expense. Written
// the other way the totals would report received 10 and spent 10: two events where none happened,
// and a turnover of 20 where the answer is nothing.
//
// The oracle is the same one every test here uses: re-aggregate the movements and compare.
TEST_F(TotalsFix, AReversalReducesItsOwnSideRatherThanTheOther)
{
	if (!ready) return;
	Move(1, JAN,  wxT("W1"),  10, kIn);
	Move(2, JAN2, wxT("W1"), -10, kIn);   // the storno

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")),  0.0);
	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_out")), 0.0);   // NOT 10 -- it is not an expense
	ExpectParity("a reversal on the receipt side");
}

// AND THE RULE IS SYMMETRIC: an EXPENSE is reversed exactly as a receipt is, on its own side. There
// is no privileged direction here -- the record type says which figure a movement belongs to, and the
// sign says what it does to that figure. Written asymmetrically (a reversed expense becoming a
// receipt), stock would come back into the warehouse instead of the expense being unsaid.
TEST_F(TotalsFix, AnExpenseIsReversedOnItsOwnSideToo)
{
	if (!ready) return;
	Move(1, JAN,  wxT("W1"),  10, kIn);
	Move(2, JAN2, wxT("W1"),  10, kOut);
	Move(3, FEB,  wxT("W1"), -10, kOut);   // the storno of the expense

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")),   10.0);
	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_out")),  10.0);
	EXPECT_DOUBLE_EQ(Cell(MFEB, wxT("W1"), wxT("qty_out")), -10.0);   // NOT a receipt of 10
	EXPECT_DOUBLE_EQ(Cell(MFEB, wxT("W1"), wxT("qty_in")),    0.0);
	ExpectParity("a reversal on the expense side");
}

// AND IT REALLY IS ONLY THE NET THAT VANISHES. A receipt of 10 against an expense of 10 also nets
// to zero, and that row must NOT disappear: something happened, and both figures say so. This is
// why the drop rule reads "any figure non-zero" rather than "the net is zero".
TEST_F(TotalsFix, ReceiptAgainstExpenseIsNotAReversal)
{
	if (!ready) return;
	Move(1, JAN,  wxT("W1"), 10, kIn);
	Move(2, JAN2, wxT("W1"), 10, kOut);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")),  10.0);
	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_out")), 10.0);
	ExpectParity("a receipt met by an expense");
}

// A reversal that lands in ANOTHER period leaves both periods honest: the first still holds what it
// held, the second carries the negative. Backdating is what makes this worth pinning -- a storno is
// most often written after the fact.
TEST_F(TotalsFix, AReversalInALaterPeriodStaysInThatPeriod)
{
	if (!ready) return;
	Move(1, JAN, wxT("W1"),  10, kIn);
	Move(2, FEB, wxT("W1"), -10, kIn);

	EXPECT_DOUBLE_EQ(Cell(MJAN, wxT("W1"), wxT("qty_in")),  10.0);
	EXPECT_DOUBLE_EQ(Cell(MFEB, wxT("W1"), wxT("qty_in")), -10.0);
	ExpectParity("a reversal a month later");
}
