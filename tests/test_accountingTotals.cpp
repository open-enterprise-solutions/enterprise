// =============================================================================
// OES Enterprise — the ACCOUNTING register's totals bundle: the rules that decide
// whether a movement moves a figure at all.
//
// The accounting register was rebuilt on 2026-08-13 and NOTHING covered it; the
// only check it ever had was a person applying a configuration and looking at the
// numbers. This is the half that can be pinned down without a configuration: the
// declaration this register produces, rendered by the real L2-2 renderer and run
// against a live engine, with movements posted through the triggers that ship.
//
// ⚠ WHAT THIS DOES NOT COVER, stated so the green is not read wider than it is. The
// spec below is written BY HAND in the shape ContributeTables produces
// (accountingRegisterMetadataSchema.cpp) — it does not call that function, which
// needs a whole metadata image with a chart of accounts behind it. So a change in
// what the REGISTER declares is not caught here; a change in what the DECLARATION
// then does is. The same boundary test_totalsNumericParity.cpp stands on for the
// accumulation register.
//
// The rules under test are the accounting-specific ones, each of which is a figure
// that would otherwise be silently wrong rather than an error:
//
//   * a movement that is not IN FORCE moves nothing (the Active guard);
//   * a correspondence line names two accounts, so it raises the debit turnover of
//     one and the credit turnover of the other — two keys, two tables;
//   * a line that legitimately names ONE side (an off-balance entry) contributes to
//     that side only, and posts NOTHING keyed by an empty account;
//   * one-sided: the record type splits one movement into the two stored figures,
//     and DEBIT is the enum's first member — the neighbouring register once filed
//     every receipt as an expense because a literal disagreed with that order.
//
// Added 2026-08-14, the cases the accumulation register's parity test earned its
// keep on and this one had none of: a REVERSAL lowers its own side rather than
// raising the other, a delete and an amount edit run BOTH arms of the accumulate, a
// backdated entry lands in its own day, and a fraction survives into the stored
// figure (the live NUMERIC(18,0) defect, pinned by consequence rather than spelling).
//
// The last section reads the SOURCES instead of running them, and says why: the
// guard-shape defect it pins is a Firebird refusal that SQLite accepts, so no live
// check on this fixture could ever see it.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/textfile.h>

#include <vector>

#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/schemaSnapshot.h"

namespace {

// The movements of a CORRESPONDENCE register: one line, both accounts, one amount,
// and the flag that says whether it is in force. A reference is spelled as the pair
// the column layout produces — the _RTRef half is what "is there an account here"
// is asked of, since an empty reference is an all-zero guid and not NULL.
const wxChar* kCreateMovements = wxT(
	"CREATE TABLE Acc5 ("
	"  id INTEGER PRIMARY KEY, period_ TEXT, active_ INTEGER,"
	"  accdr_RTRef INTEGER, accdr_RRRef TEXT,"
	"  acccr_RTRef INTEGER, acccr_RRRef TEXT,"
	"  amount NUMERIC)");

// One side's totals table: keyed by the period and the account it is ABOUT, holding
// that side's turnover. Two of them, because one movement is about two accounts and
// two different keys cannot be one upsert.
wxString CreateTotals(const wxString& table, const wxString& side)
{
	return wxT("CREATE TABLE ") + table + wxT(" ("
		"  period_ TEXT NOT NULL, ") + side + wxT("_RTRef INTEGER NOT NULL, ") + side + wxT("_RRRef TEXT NOT NULL,"
		"  amount_Dr NUMERIC NOT NULL DEFAULT 0, amount_Cr NUMERIC NOT NULL DEFAULT 0,"
		"  PRIMARY KEY (period_, ") + side + wxT("_RTRef, ") + side + wxT("_RRRef))");
}

// The declaration for ONE side of a correspondence register, in the shape
// ContributeTables builds: the guards are declared separately and must COMPOSE —
// "in force" AND "this side names an account".
ibMaterializeSpec CorrespondenceSide(bool creditSide)
{
	const wxString side = creditSide ? wxT("acccr") : wxT("accdr");

	ibSchemaMaterialize m;
	m.Guard(wxT("{row}.active_ <> 0"));
	m.Guard(wxT("{row}.") + side + wxT("_RTRef <> 0"));

	ibMaterializeSpec spec;
	spec.m_table            = creditSide ? wxT("Acc5_TtCr") : wxT("Acc5_TtDr");
	spec.m_source           = wxT("Acc5");
	spec.m_keyColumns       = { side + wxT("_RTRef"), side + wxT("_RRRef") };
	spec.m_periodColumn     = wxT("period_");
	spec.m_periodSourceExpr = wxT("{row}.period_");
	spec.m_periodUnit       = ibTotalsPeriod::Day;
	spec.m_guard            = m.m_guard;
	// The whole amount, on the side this table is keyed by — in correspondence mode
	// the row IS a posting, so which side it lands on was decided by which account
	// keyed the table.
	spec.m_deltas = { { creditSide ? wxT("amount_Cr") : wxT("amount_Dr"), wxT("{row}.amount") } };
	return spec;
}

// A ONE-SIDED register: one table, one account, and the record type splitting one
// movement into the two stored figures. The accumulate stays unconditional — only
// the VALUE branches — so the trigger needs no procedural IF.
ibMaterializeSpec OneSided()
{
	ibSchemaMaterialize m;
	m.Guard(wxT("{row}.active_ <> 0"));

	ibMaterializeSpec spec;
	spec.m_table            = wxT("Acc6_Tt");
	spec.m_source           = wxT("Acc6");
	spec.m_keyColumns       = { wxT("acc_RTRef"), wxT("acc_RRRef") };
	spec.m_periodColumn     = wxT("period_");
	spec.m_periodSourceExpr = wxT("{row}.period_");
	spec.m_periodUnit       = ibTotalsPeriod::Day;
	spec.m_guard            = m.m_guard;
	// ⚠ THE TAG IS THE ENUM'S ORDINAL: ibAccountingRecordType declares Debit FIRST,
	// so a debit movement stores 0. Spelled here as the literal the register renders
	// from the enum — if the enum's order ever moves, this test is the thing that
	// says the two disagree.
	spec.m_deltas = {
		{ wxT("amount_Dr"), wxT("CASE WHEN {row}.rectype_ = 0 THEN {row}.amount ELSE 0 END") },
		{ wxT("amount_Cr"), wxT("CASE WHEN {row}.rectype_ = 0 THEN 0 ELSE {row}.amount END") },
	};
	return spec;
}

struct AcctFix : ::testing::Test
{
	ibDatabaseLayerSQLite db;
	bool ready = false;

	void SetUp() override
	{
		if (!db.Open(wxT(":memory:"))) {
			GTEST_SKIP() << "in-memory SQLite unavailable";
			return;
		}
		db.RunQuery(kCreateMovements);
		db.RunQuery(CreateTotals(wxT("Acc5_TtDr"), wxT("accdr")));
		db.RunQuery(CreateTotals(wxT("Acc5_TtCr"), wxT("acccr")));

		for (const bool creditSide : { false, true }) {
			const ibMaterializeSql sql = RenderMaterialization(
				CorrespondenceSide(creditSide),
				&ibDatabaseLayerSQLite::MaterializationDialect(),
				ibDatabaseLayerSQLite::Dialect());
			ASSERT_TRUE(sql.Apply(db)) << "both sides' bundles must install on one source";
		}
		ready = true;
	}

	void TearDown() override { db.Close(); }

	// A posting. An account is (type, key); type 0 means the side names NO account —
	// which is what an off-balance line legitimately looks like.
	void Post(int id, const wxChar* period, int drType, const wxChar* drKey,
	          int crType, const wxChar* crKey, double amount, int active = 1)
	{
		db.RunQuery(wxT("INSERT INTO Acc5 (id, period_, active_, accdr_RTRef, accdr_RRRef,"
		                " acccr_RTRef, acccr_RRRef, amount) VALUES (%d, '%s', %d, %d, '%s', %d, '%s', %s)"),
			id, period, active, drType, drKey, crType, crKey, wxString::FromCDouble(amount));
	}

	void Activate(int id, int active) { db.RunQuery(wxT("UPDATE Acc5 SET active_ = %d WHERE id = %d"), active, id); }

	// The other two ways a movement changes after it was stored. Both fire the delta
	// TWICE — out under the old row, in under the new — so they are the arms an
	// accumulate has to get right, not extra flavours of INSERT.
	void Remove(int id) { db.RunQuery(wxT("DELETE FROM Acc5 WHERE id = %d"), id); }

	void SetAmount(int id, double amount)
	{
		db.RunQuery(wxT("UPDATE Acc5 SET amount = %s WHERE id = %d"), wxString::FromCDouble(amount), id);
	}

	// ⚠ The query rides in as a %s ARGUMENT — the driver entry is a printf-style vararg, so a query
	// carrying a '%' of its own would be read as a format string. And the LAYER closes the result set:
	// closing it on the object alone leaves the layer's registry holding a dangling pointer.
	double Figure(const wxString& table, const wxString& column)
	{
		double sum = 0.0;
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"),
			wxT("SELECT COALESCE(SUM(") + column + wxT("), 0) FROM ") + table);
		if (rs != nullptr) {
			if (rs->Next()) sum = rs->GetResultDouble(1);
			db.CloseResultSet(rs);
		}
		return sum;
	}

	long Rows(const wxString& table)
	{
		long n = -1;
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), wxT("SELECT COUNT(*) FROM ") + table);
		if (rs != nullptr) {
			if (rs->Next()) n = rs->GetResultInt(1);
			db.CloseResultSet(rs);
		}
		return n;
	}
};

} // namespace

// =============================================================================
// The guards — what does NOT move a figure
// =============================================================================

// ⭐⭐ THE REGRESSION THIS FILE WAS WRITTEN AROUND. A correspondence register
// declares TWO guards, and the declaration used to ASSIGN rather than compose: the
// second call replaced the first, so "the movement is in force" was silently
// dropped and every inactive posting moved the totals. The numbers all looked like
// numbers.
TEST_F(AcctFix, AnInactiveLineMovesNothing_EvenWithASecondGuardDeclared)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("cash"), 7, wxT("bank"), 100.0, /*active*/ 0);

	EXPECT_EQ(0, Rows(wxT("Acc5_TtDr"))) << "a line that is not in force must reach neither table";
	EXPECT_EQ(0, Rows(wxT("Acc5_TtCr")));
}

// And the flag is a flag: flip it and the same row starts counting, with nothing
// rewritten. (An UPDATE fires the delta twice — out under the old row, in under the
// new — so this also pins that the guard is read on BOTH.)
TEST_F(AcctFix, ActivatingALineStartsCountingIt)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("cash"), 7, wxT("bank"), 100.0, /*active*/ 0);
	ASSERT_EQ(0, Rows(wxT("Acc5_TtDr")));

	Activate(1, 1);
	EXPECT_DOUBLE_EQ(100.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));
	EXPECT_DOUBLE_EQ(100.0, Figure(wxT("Acc5_TtCr"), wxT("amount_Cr")));

	Activate(1, 0);
	EXPECT_DOUBLE_EQ(0.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")))
		<< "taking a line out of force must take its figure back out too";
}

// An off-balance entry has no counterpart BY DEFINITION, and that emptiness is the
// record's meaning rather than an incomplete one. What must not happen is a totals
// row keyed by an EMPTY account — a bucket that is not an account at all, and one
// every reading would then have to learn to ignore.
TEST_F(AcctFix, ALineNamingOneSideContributesToThatSideOnly)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("leased"), 0, wxT(""), 250.0);

	EXPECT_EQ(1, Rows(wxT("Acc5_TtDr")));
	EXPECT_DOUBLE_EQ(250.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));
	EXPECT_EQ(0, Rows(wxT("Acc5_TtCr"))) << "no side, no row — never a row keyed by an empty account";
}

// =============================================================================
// Correspondence — one movement, two accounts, two keys
// =============================================================================

TEST_F(AcctFix, OnePostingRaisesBothSidesUnderTheirOwnAccounts)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("goods"),    7, wxT("supplier"), 1000.0);
	Post(2, wxT("2026-03-04 18:00:00"), 7, wxT("supplier"), 7, wxT("bank"),      600.0);

	// The supplier account stands on BOTH sides across the two postings, and each
	// side is keyed by its own table — which is the whole reason there are two.
	EXPECT_DOUBLE_EQ(1600.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));
	EXPECT_DOUBLE_EQ(1600.0, Figure(wxT("Acc5_TtCr"), wxT("amount_Cr")));
	EXPECT_EQ(2, Rows(wxT("Acc5_TtDr")));
	EXPECT_EQ(2, Rows(wxT("Acc5_TtCr")));
}

// The stored grain is a DAY: two postings on one day against one account are ONE
// stored row, and the next day is another. Anything finer than the grain is a
// question for the movements, which is exactly why the grain can be this coarse.
TEST_F(AcctFix, TheStoredGrainIsADay)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 09:00:00"), 7, wxT("cash"), 7, wxT("bank"), 10.0);
	Post(2, wxT("2026-03-04 20:00:00"), 7, wxT("cash"), 7, wxT("bank"), 15.0);
	Post(3, wxT("2026-03-05 09:00:00"), 7, wxT("cash"), 7, wxT("bank"),  5.0);

	EXPECT_EQ(2, Rows(wxT("Acc5_TtDr"))) << "one row per (day, account), not per movement";
	EXPECT_DOUBLE_EQ(30.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));
}

// =============================================================================
// A movement that changes after it was stored — the arms of the accumulate
//
// Everything above posts once and reads. These are the cases the neighbouring
// register's parity test earned its keep on (deletes, updates, backdated entries,
// fractional values) and the accounting one had none of: each is a figure that
// stays plausible while being wrong, because a missed arm shows up as a number
// that is merely too big.
// =============================================================================

// ⭐ A REVERSAL LOWERS ITS OWN SIDE — it is NOT an entry on the opposite one.
// Normalising a negative amount into a mirrored posting would report two inflated
// turnovers instead of one unwound movement, and every period report would read
// high on both sides while the balance still came out right. The accumulate is
// algebraic precisely so this needs no special path.
TEST_F(AcctFix, AReversalLowersItsOwnSideRatherThanRaisingTheOther)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("goods"), 7, wxT("supplier"), 100.0);
	Post(2, wxT("2026-03-04 11:00:00"), 7, wxT("goods"), 7, wxT("supplier"), -30.0);

	EXPECT_DOUBLE_EQ(70.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")))
		<< "the storno unwinds the debit turnover it belongs to";
	EXPECT_DOUBLE_EQ(70.0, Figure(wxT("Acc5_TtCr"), wxT("amount_Cr")))
		<< "and the credit side likewise — never 130, which is what a mirrored posting would give";
}

// Deleting a movement takes its figure back out. The row keyed by that day may well
// stay behind holding zero — compacting the stored surface is not the trigger's job,
// and a zero row is a true statement about a day nothing is left on.
TEST_F(AcctFix, DeletingAMovementTakesItsFigureBack)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("cash"), 7, wxT("bank"), 100.0);
	ASSERT_DOUBLE_EQ(100.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));

	Remove(1);
	EXPECT_DOUBLE_EQ(0.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));
	EXPECT_DOUBLE_EQ(0.0, Figure(wxT("Acc5_TtCr"), wxT("amount_Cr")))
		<< "a delete has to reach BOTH sides — one movement, two keyed tables";
}

// An edited amount moves the DIFFERENCE, not the new value on top of the old. The
// out-arm and the in-arm both run, and a missing out-arm is the classic silent
// doubling: the figure grows by the new amount and nothing complains.
TEST_F(AcctFix, ChangingTheAmountMovesTheDifference)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("cash"), 7, wxT("bank"), 100.0);
	SetAmount(1, 250.0);

	EXPECT_DOUBLE_EQ(250.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")))
		<< "250, not 350 — the old amount left before the new one arrived";
}

// A backdated entry belongs to ITS day, not to the day it was entered on. The grain
// is computed from the row's own period, so posting out of order changes nothing —
// which is what makes "the balance as of a date" answerable at all after the fact.
TEST_F(AcctFix, ABackdatedEntryLandsInItsOwnDay)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-05 09:00:00"), 7, wxT("cash"), 7, wxT("bank"), 10.0);
	Post(2, wxT("2026-03-01 09:00:00"), 7, wxT("cash"), 7, wxT("bank"), 40.0);

	EXPECT_EQ(2, Rows(wxT("Acc5_TtDr"))) << "two days, two stored rows — the later posting did not join the earlier day";
	EXPECT_DOUBLE_EQ(50.0, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")));
}

// ⭐ KOPECKS SURVIVE. This is the live defect class, not a hypothetical: a bare
// `NUMERIC` on Firebird is NUMERIC(9,0), and the totals column used to be declared
// flat — so a resource carrying fractions lost them ON THE WAY INTO the totals
// while the movements held them perfectly. The figure stayed a plausible round
// number. The column now takes its precision and scale from the resource's own
// declaration; this pins the consequence rather than the spelling.
TEST_F(AcctFix, KopecksSurviveTheAccumulation)
{
	if (!ready) GTEST_SKIP();

	Post(1, wxT("2026-03-04 10:00:00"), 7, wxT("cash"), 7, wxT("bank"), 1234.56);

	EXPECT_NEAR(1234.56, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")), 1e-9)
		<< "a truncating column would report 1234 — a plausible number, and wrong by the kopecks";

	// And the fraction survives ACCUMULATION, not just one write: two movements whose
	// fractions carry into the next unit must land on the whole number, never below it.
	Post(2, wxT("2026-03-04 11:00:00"), 7, wxT("cash"), 7, wxT("bank"), 0.44);
	EXPECT_NEAR(1235.00, Figure(wxT("Acc5_TtDr"), wxT("amount_Dr")), 1e-9);
}

// =============================================================================
// One-sided — the record type splits the movement
// =============================================================================

TEST(AcctOneSided, DebitIsTheEnumsFirstMember)
{
	ibDatabaseLayerSQLite db;
	if (!db.Open(wxT(":memory:")))
		GTEST_SKIP() << "in-memory SQLite unavailable";

	db.RunQuery(wxT("CREATE TABLE Acc6 (id INTEGER PRIMARY KEY, period_ TEXT, active_ INTEGER,"
	                " acc_RTRef INTEGER, acc_RRRef TEXT, rectype_ INTEGER, amount NUMERIC)"));
	db.RunQuery(wxT("CREATE TABLE Acc6_Tt (period_ TEXT NOT NULL, acc_RTRef INTEGER NOT NULL,"
	                " acc_RRRef TEXT NOT NULL, amount_Dr NUMERIC NOT NULL DEFAULT 0,"
	                " amount_Cr NUMERIC NOT NULL DEFAULT 0,"
	                " PRIMARY KEY (period_, acc_RTRef, acc_RRRef))"));

	const ibMaterializeSql sql = RenderMaterialization(
		OneSided(), &ibDatabaseLayerSQLite::MaterializationDialect(), ibDatabaseLayerSQLite::Dialect());
	ASSERT_TRUE(sql.Apply(db));

	db.RunQuery(wxT("INSERT INTO Acc6 VALUES (1, '2026-03-04 10:00:00', 1, 7, 'cash', 0, 40)"));   // debit
	db.RunQuery(wxT("INSERT INTO Acc6 VALUES (2, '2026-03-04 11:00:00', 1, 7, 'cash', 1, 25)"));   // credit
	db.RunQuery(wxT("INSERT INTO Acc6 VALUES (3, '2026-03-04 12:00:00', 0, 7, 'cash', 0, 99)"));   // not in force

	double dr = 0.0, cr = 0.0;
	ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), wxT("SELECT amount_Dr, amount_Cr FROM Acc6_Tt"));
	ASSERT_NE(nullptr, rs);
	ASSERT_TRUE(rs->Next());
	dr = rs->GetResultDouble(1);
	cr = rs->GetResultDouble(2);
	db.CloseResultSet(rs);

	EXPECT_DOUBLE_EQ(40.0, dr) << "record type 0 is DEBIT — the enum declares it first";
	EXPECT_DOUBLE_EQ(25.0, cr);
	db.Close();
}

// =============================================================================
// The composition rule itself, read off the declaration
// =============================================================================

// Guards compose with AND and in the order declared; a lone guard is left exactly
// as it was written (no stray parentheses, nothing to unlearn when reading a
// trigger body in a database).
TEST(AcctGuard, GuardsCompose)
{
	ibSchemaMaterialize one;
	one.Guard(wxT("{row}.active_ <> 0"));
	EXPECT_EQ(wxT("{row}.active_ <> 0"), one.m_guard);

	ibSchemaMaterialize two;
	two.Guard(wxT("a"));
	two.Guard(wxT("b"));
	EXPECT_EQ(wxT("(a) AND (b)"), two.m_guard);

	// An empty guard adds nothing — a side that declares no condition does not turn
	// the previous one into "( ) AND ( )".
	ibSchemaMaterialize skip;
	skip.Guard(wxT("a"));
	skip.Guard(wxEmptyString);
	EXPECT_EQ(wxT("a"), skip.m_guard);
}

// =============================================================================
// The rule that must live in ONE place — checked by READING THE SOURCES
//
// ⚠ WHY TEXT AND NOT BEHAVIOUR. The defect these pin is a Firebird refusal: a
// "boolean" attribute is stored as a SMALLINT, so a guard rendered as a bare field
// (`WHERE NEW.fld1095_B`) is a field where a condition is required — FB answers
// "invalid usage of boolean expression" and the whole CREATE TRIGGER fails, taking
// the restructuring that emitted it down with it. SQLite accepts the same text
// happily, so the suite this file otherwise runs on CANNOT see it: every live check
// here would stay green while the apply died on the only driver that ships by
// default. What is checkable without Firebird is that the comparison was WRITTEN —
// the same reasoning test_propertySerialized.cpp stands on.
//
// And the cost is asymmetric, which is why it is worth a test at all: nothing READS
// wrong, the apply does not finish.
// =============================================================================

namespace {

wxString BackendPartialsDir()
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.RemoveLastDir();                      // tests -> enterprise
	here.AppendDir(wxT("src"));
	here.AppendDir(wxT("engine"));
	here.AppendDir(wxT("backend"));
	here.AppendDir(wxT("metaCollection"));
	here.AppendDir(wxT("partial"));
	return here.GetPath();
}

std::vector<wxString> ReadLines(const wxString& path)
{
	std::vector<wxString> lines;
	wxTextFile file;
	if (!file.Open(path))
		return lines;
	for (size_t i = 0; i < file.GetLineCount(); ++i)
		lines.push_back(file[i]);
	file.Close();
	return lines;
}

// A guard is an EXPRESSION-condition. Any of these makes it one; a line carrying
// none of them is naming a field and calling it a condition.
bool CarriesAComparison(const wxString& text)
{
	return text.Contains(wxT("<>")) || text.Contains(wxT("="))
	    || text.Contains(wxT(" IS ")) || text.Contains(wxT(">")) || text.Contains(wxT("<"));
}

} // namespace

// Every guard a metatype declares compares something. Checked over the whole
// partial/ directory rather than the two files that have one today, so a metatype
// that gains a totals bundle later is covered by existing here.
TEST(AcctGuardSource, EveryDeclaredGuardComparesRatherThanNamingAField)
{
	wxArrayString files;
	ASSERT_NE(0u, wxDir::GetAllFiles(BackendPartialsDir(), &files, wxT("*.cpp"), wxDIR_FILES))
		<< "the partial sources must be findable from the test's own location";

	size_t guardsSeen = 0;
	for (const wxString& path : files) {
		const std::vector<wxString> lines = ReadLines(path);
		for (size_t i = 0; i < lines.size(); ++i) {
			if (!lines[i].Contains(wxT(".Guard(")))
				continue;
			++guardsSeen;
			// The expression may wrap onto the next line (the predicate half often does).
			const wxString here = lines[i] + (i + 1 < lines.size() ? lines[i + 1] : wxString());
			EXPECT_TRUE(CarriesAComparison(here))
				<< "a guard rendered as a bare field fails CREATE TRIGGER on Firebird: "
				<< wxFileName(path).GetFullName().ToStdString() << ":" << (i + 1);
		}
	}
	EXPECT_GT(guardsSeen, 0u) << "no guard found at all — the search moved, not the rule";
}

// ⭐ THE COLLAPSE, PINNED. "The movement is in force" was written TWICE, and one
// copy shipped without the ` <> 0`. It now lives in ibRegGuardInForce and both
// registers call it — so the check is that nobody declares that guard by hand
// again, which is a stronger statement than "the two copies agree".
TEST(AcctGuardSource, TheInForceRuleIsSpelledInExactlyOnePlace)
{
	wxArrayString files;
	ASSERT_NE(0u, wxDir::GetAllFiles(BackendPartialsDir(), &files, wxT("*MetadataSchema.cpp"), wxDIR_FILES));

	size_t callers = 0;
	for (const wxString& path : files) {
		const std::vector<wxString> lines = ReadLines(path);
		for (const wxString& line : lines) {
			if (!line.Contains(wxT("GetRegisterActive(")))
				continue;
			EXPECT_TRUE(line.Contains(wxT("ibRegGuardInForce")))
				<< "the in-force guard is declared through the shared helper, never spelled again: "
				<< wxFileName(path).GetFullName().ToStdString();
			++callers;
		}
	}
	EXPECT_GE(callers, 2u) << "both registers declare it — accumulation and accounting";

	// …and the helper itself still writes the comparison.
	wxFileName lowering(BackendPartialsDir(), wxT("registerQueryLowering.h"));
	const std::vector<wxString> lines = ReadLines(lowering.GetFullPath());
	ASSERT_FALSE(lines.empty()) << "registerQueryLowering.h must be readable";

	bool found = false;
	for (size_t i = 0; i < lines.size() && !found; ++i) {
		if (!lines[i].Contains(wxT("ibRegGuardInForce")) || !lines[i].Contains(wxT("inline")))
			continue;
		for (size_t j = i; j < lines.size() && j < i + 12; ++j)
			if (lines[j].Contains(wxT("Guard(")) && lines[j].Contains(wxT("<> 0")))
				found = true;
	}
	EXPECT_TRUE(found) << "ibRegGuardInForce must render a comparison, not the field alone";
}
