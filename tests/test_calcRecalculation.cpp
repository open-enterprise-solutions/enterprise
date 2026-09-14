// Calculation engine — which already-computed records a change makes stale: the rule (kernel), and the
// recalculation's reading of the records held to it.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/calculation/calculation.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/partial/calculationRegister.h"

namespace {

// Days as ticks: enough to read the scenarios, and the kernel does not care about the unit.
constexpr int64_t kNov1 = 0, kDec1 = 30, kJan1 = 61, kFeb1 = 92;
constexpr int64_t kNone = -1;   // start > end: no such period

enum : int { kSalary = 0, kBonus = 1, kAbsence = 2 };

ibRecalcFact Fact(int type, int key, int64_t as, int64_t ae, int64_t bs, int64_t be, int64_t reg) {
	return ibRecalcFact{ type, key, as, ae, bs, be, reg };
}
ibRecalcFact NoPeriods(int type, int key, int64_t reg) {
	return ibRecalcFact{ type, key, 0, kNone, 0, kNone, reg };
}

} // namespace

// The case the whole mechanism exists for: a December salary is corrected after the January bonus was
// computed over it. The bonus names salary as leading, its base period is December — it is stale.
TEST(CalcRecalculation, BonusOverDecemberIsLedByDecemberSalary) {
	const std::vector<ibRecalcFact> changed = { Fact(kSalary, 7, kDec1, kJan1, kNov1, kDec1, kDec1) };
	const std::vector<ibRecalcFact> candidates = { Fact(kBonus, 7, kJan1, kFeb1, kDec1, kJan1, kJan1) };
	const std::vector<std::pair<int, int>> leads = { { kBonus, kSalary } };
	EXPECT_EQ(std::vector<size_t>({ 0 }), ibFindLedRecords(changed, candidates, leads));
}

// No edge, no dependency — whatever the periods say. A type nobody declared dependent stays computed.
TEST(CalcRecalculation, WithoutALeadingEdgeNothingIsStale) {
	const std::vector<ibRecalcFact> changed = { Fact(kSalary, 7, kDec1, kJan1, kNov1, kDec1, kDec1) };
	const std::vector<ibRecalcFact> candidates = { Fact(kBonus, 7, kJan1, kFeb1, kDec1, kJan1, kJan1) };
	EXPECT_TRUE(ibFindLedRecords(changed, candidates, {}).empty());
	// …and the edge is directed: salary leading bonus is not bonus leading salary.
	EXPECT_TRUE(ibFindLedRecords(changed, candidates, { { kSalary, kBonus } }).empty());
}

// The recalculation's dimensions decide WHOSE records: another employee's bonus is not touched.
TEST(CalcRecalculation, AnotherKeyIsNotLed) {
	const std::vector<ibRecalcFact> changed = { Fact(kSalary, 7, kDec1, kJan1, kNov1, kDec1, kDec1) };
	const std::vector<ibRecalcFact> candidates = { Fact(kBonus, 8, kJan1, kFeb1, kDec1, kJan1, kJan1) };
	EXPECT_TRUE(ibFindLedRecords(changed, candidates, { { kBonus, kSalary } }).empty());
}

// A bonus whose base is NOVEMBER does not care about a December correction.
TEST(CalcRecalculation, BasePeriodElsewhereIsNotLed) {
	const std::vector<ibRecalcFact> changed = { Fact(kSalary, 7, kDec1, kJan1, kNov1, kDec1, kDec1) };
	const std::vector<ibRecalcFact> candidates = { Fact(kBonus, 7, kJan1, kFeb1, kNov1, kDec1, kJan1) };
	EXPECT_TRUE(ibFindLedRecords(changed, candidates, { { kBonus, kSalary } }).empty());
}

// Displacement is a dependency too: salary names absence as leading; an absence entered into the same
// month (overlapping salary's ACTION period) makes the salary stale even though no base is read.
TEST(CalcRecalculation, OverlappingActionPeriodLeads) {
	const std::vector<ibRecalcFact> changed = { Fact(kAbsence, 7, kDec1 + 9, kDec1 + 20, 0, kNone, kDec1) };
	const std::vector<ibRecalcFact> candidates = { Fact(kSalary, 7, kDec1, kJan1, kNov1, kDec1, kDec1) };
	EXPECT_EQ(std::vector<size_t>({ 0 }), ibFindLedRecords(changed, candidates, { { kSalary, kAbsence } }));
}

// A register with no periods at all: records meet by sharing the registration period.
TEST(CalcRecalculation, WithoutPeriodsTheRegistrationPeriodDecides) {
	const std::vector<ibRecalcFact> changed = { NoPeriods(kSalary, 7, kDec1) };
	const std::vector<ibRecalcFact> sameMonth = { NoPeriods(kBonus, 7, kDec1) };
	const std::vector<ibRecalcFact> nextMonth = { NoPeriods(kBonus, 7, kJan1) };
	const std::vector<std::pair<int, int>> leads = { { kBonus, kSalary } };
	EXPECT_EQ(std::vector<size_t>({ 0 }), ibFindLedRecords(changed, sameMonth, leads));
	EXPECT_TRUE(ibFindLedRecords(changed, nextMonth, leads).empty());
}

// Many changes leading one record list it ONCE; untouched candidates keep their place out of the list.
TEST(CalcRecalculation, ACandidateIsListedOnce) {
	const std::vector<ibRecalcFact> changed = {
		Fact(kSalary, 7, kDec1, kJan1, kNov1, kDec1, kDec1),
		Fact(kSalary, 7, kDec1 + 5, kJan1, kNov1, kDec1, kDec1),
	};
	const std::vector<ibRecalcFact> candidates = {
		Fact(kBonus, 8, kJan1, kFeb1, kDec1, kJan1, kJan1),   // another employee
		Fact(kBonus, 7, kJan1, kFeb1, kDec1, kJan1, kJan1),   // led twice
	};
	EXPECT_EQ(std::vector<size_t>({ 1 }), ibFindLedRecords(changed, candidates, { { kBonus, kSalary } }));
}

// A base read BY REGISTRATION PERIOD is met by what is registered in it, not by the days a record acts
// on. A sick leave for December registered in January (it arrived after December was paid) leads the
// January tax, whose base is January's registrations — and not the December tax, which by action period
// it would have marked for good: nothing would ever answer a mark on a closed month.
TEST(CalcRecalculation, ABaseByRegistrationIsLedByWhatIsRegisteredInIt) {
	enum : int { kTax = 3 };
	const std::vector<ibRecalcFact> changed = { Fact(kAbsence, 7, kDec1 + 9, kDec1 + 14, 0, kNone, kJan1) };
	const std::vector<ibRecalcFact> candidates = {
		Fact(kTax, 7, 0, kNone, kDec1, kJan1, kDec1),   // December's tax
		Fact(kTax, 7, 0, kNone, kJan1, kFeb1, kJan1),   // January's
	};
	const std::vector<std::pair<int, int>> leads = { { kTax, kAbsence } };
	EXPECT_EQ(std::vector<size_t>({ 1 }), ibFindLedRecords(changed, candidates, leads, /*baseByRegistration*/ true));
	// …while a base read by action period is met by the days, whatever month they were registered in.
	EXPECT_EQ(std::vector<size_t>({ 0 }), ibFindLedRecords(changed, candidates, leads));
}

// A type no edge names (-1) is neither leading nor led.
TEST(CalcRecalculation, AnUnnamedTypeTakesNoPart) {
	const std::vector<ibRecalcFact> changed = { Fact(-1, 7, kDec1, kJan1, kNov1, kDec1, kDec1) };
	const std::vector<ibRecalcFact> candidates = { Fact(-1, 7, kJan1, kFeb1, kDec1, kJan1, kJan1) };
	EXPECT_TRUE(ibFindLedRecords(changed, candidates, { { -1, -1 } }).empty());
}

// =============================================================================
// The recalculation's marks (calculationRegister.h, "The recalculation's marks"), run on a live in-memory SQLite:
// ibRecalculationRelation for the records of one recorder, executed, against the rule said directly — a record of
// another recorder is marked when a record of this one, of a type its own names as leading, of the same employee,
// meets it in time (ibFindLedRecords), whenever either was registered. `Acc` is the accruals register, both led and
// leading; `Ev` a register of events that only leads; `Tax` a register with no action period whose base is read by
// registration.
// =============================================================================

namespace {

// Day n of the calendar these tests use, as a stored date.
wxString DateOf(int day)
{
	wxDateTime d(1, wxDateTime::Jan, 2026);
	d += wxDateSpan::Days(day);
	return d.Format(wxT("%Y-%m-%d 00:00:00"));
}

int DayNumber(const wxDateTime& d)
{
	return static_cast<int>(std::lround(d.GetJulianDayNumber() - wxDateTime(1, wxDateTime::Jan, 2026).GetJulianDayNumber()));
}

// Month m of 2026 (and on) as a moment, and its first day as a day.
wxDateTime MonthMoment(int m)
{
	wxDateTime d(1, wxDateTime::Jan, 2026);
	d += wxDateSpan::Months(m);
	return d;
}

int MonthStart(int m) { return DayNumber(MonthMoment(m)); }
int MonthEnd(int m) { return MonthStart(m + 1) - 1; }

// The month a day is in.
int MonthOfDay(int day)
{
	int m = 0;
	while (MonthStart(m + 1) <= day)
		++m;
	return m;
}

struct Rec {
	wxString table = wxT("Acc");
	wxString recorder;
	int      line = 0;
	wxString emp = wxT("E"), kind;
	int      reg = 0;                  // the registration period, as a day — a month's first
	int      month = 0;                // the month-for, as a day
	int      first = 0, last = 0;      // the action period
	int      baseFirst = 0, baseLast = 0;
	bool     storno = false;
	bool     active = true;
};

using Row = std::tuple<wxString, wxString, wxString, int>;   // recorder, type, employee, month
using Rows = std::set<Row>;

// The rule, asked directly of the records, for the records of `recorder`.
Rows RuleRows(const std::vector<Rec>& records, const std::vector<std::pair<wxString, wxString>>& leading,
	const wxString& ledTable, bool acts, bool baseByRegistration, const wxString& recorder)
{
	std::map<wxString, int> typeIndex;
	const auto ordinal = [&typeIndex](const wxString& t) {
		const auto it = typeIndex.find(t);
		if (it != typeIndex.end()) return it->second;
		const int next = static_cast<int>(typeIndex.size());
		typeIndex.emplace(t, next);
		return next;
	};
	std::vector<std::pair<int, int>> leads;
	for (const auto& edge : leading)
		leads.push_back({ ordinal(edge.first), ordinal(edge.second) });
	std::map<wxString, int> keyIndex;
	const auto factOf = [&](const Rec& r) {
		const int key = keyIndex.emplace(r.emp, static_cast<int>(keyIndex.size())).first->second;
		const bool ownActs = r.table == ledTable ? acts : true;
		const bool based = r.table == ledTable;
		return ibRecalcFact{ ordinal(r.kind), key,
			ownActs ? r.first : 0, ownActs ? r.last + 1 : kNone,
			based ? r.baseFirst : 0, based ? r.baseLast + 1 : kNone, r.reg };
	};

	Rows out;
	for (const Rec& d : records) {
		if (d.table != ledTable || !d.active || d.storno || d.recorder == recorder)
			continue;
		for (const Rec& l : records) {
			if (!l.active || l.recorder != recorder)
				continue;
			if (!ibFindLedRecords({ factOf(l) }, { factOf(d) }, leads, baseByRegistration).empty()) {
				out.insert(Row{ d.recorder, d.kind, d.emp, acts ? d.month : 0 });
				break;
			}
		}
	}
	return out;
}

struct CalcRecalculationFix : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

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
		db->RunQuery(wxT("CREATE TABLE Acc (recorder_ TEXT NOT NULL, line_ INTEGER NOT NULL, emp TEXT, kind TEXT, ")
			wxT("reg_ TEXT, ap_ TEXT, aps TEXT, ape TEXT, bps TEXT, bpe TEXT, storno INTEGER NOT NULL, active_ INTEGER NOT NULL, ")
			wxT("PRIMARY KEY (recorder_, line_))"));
		db->RunQuery(wxT("CREATE TABLE Ev (recorder_ TEXT NOT NULL, line_ INTEGER NOT NULL, emp TEXT, kind TEXT, ")
			wxT("reg_ TEXT, aps TEXT, ape TEXT, active_ INTEGER NOT NULL, PRIMARY KEY (recorder_, line_))"));
		db->RunQuery(wxT("CREATE TABLE Tax (recorder_ TEXT NOT NULL, line_ INTEGER NOT NULL, emp TEXT, kind TEXT, ")
			wxT("reg_ TEXT, bps TEXT, bpe TEXT, storno INTEGER NOT NULL, active_ INTEGER NOT NULL, PRIMARY KEY (recorder_, line_))"));
		db->RunQuery(wxT("CREATE TABLE Lead (owner TEXT NOT NULL, named TEXT NOT NULL)"));
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}

	void Leads(const wxString& owner, const wxString& named) {
		db->RunQuery(wxT("INSERT INTO Lead (owner, named) VALUES ('%s', '%s')"), owner, named);
	}
	void Add(const Rec& r) {
		if (r.table == wxT("Ev"))
			db->RunStatement(wxString::Format(
				wxT("INSERT INTO Ev (recorder_, line_, emp, kind, reg_, aps, ape, active_) VALUES ('%s', %d, '%s', '%s', '%s', '%s', '%s', %d)"),
				r.recorder, r.line, r.emp, r.kind, DateOf(r.reg), DateOf(r.first), DateOf(r.last), r.active ? 1 : 0));
		else if (r.table == wxT("Tax"))
			db->RunStatement(wxString::Format(
				wxT("INSERT INTO Tax (recorder_, line_, emp, kind, reg_, bps, bpe, storno, active_) VALUES ('%s', %d, '%s', '%s', '%s', '%s', '%s', %d, %d)"),
				r.recorder, r.line, r.emp, r.kind, DateOf(r.reg), DateOf(r.baseFirst), DateOf(r.baseLast), r.storno ? 1 : 0, r.active ? 1 : 0));
		else
			db->RunStatement(wxString::Format(
				wxT("INSERT INTO Acc (recorder_, line_, emp, kind, reg_, ap_, aps, ape, bps, bpe, storno, active_) ")
				wxT("VALUES ('%s', %d, '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', %d, %d)"),
				r.recorder, r.line, r.emp, r.kind, DateOf(r.reg), DateOf(r.month), DateOf(r.first), DateOf(r.last),
				DateOf(r.baseFirst), DateOf(r.baseLast), r.storno ? 1 : 0, r.active ? 1 : 0));
	}

	static ibRecalculationLeading Accruals() {
		ibRecalculationLeading s;
		s.m_table = wxT("Acc");
		s.m_named = { { wxT("kind"), wxT("named") } };
		s.m_dimensions = { { wxT("emp"), wxT("emp") } };
		s.m_registration = wxT("reg_");
		s.m_active = wxT("active_");
		s.m_start = wxT("aps");
		s.m_end = wxT("ape");
		return s;
	}
	static ibRecalculationLeading Events() {
		ibRecalculationLeading s = Accruals();
		s.m_table = wxT("Ev");
		return s;
	}

	// The accruals' recalculation, keyed by the employee and naming the month.
	static ibRecalculationSpec AccSpec() {
		ibRecalculationSpec v;
		v.m_table = wxT("Acc");
		v.m_mark = { wxT("recorder_"), wxT("kind"), wxT("emp"), wxT("ap_") };
		v.m_typeId = wxT("kind");
		v.m_registration = wxT("reg_");
		v.m_active = wxT("active_");
		v.m_storno = wxT("storno");
		v.m_start = wxT("aps");
		v.m_end = wxT("ape");
		v.m_baseStart = wxT("bps");
		v.m_baseEnd = wxT("bpe");
		v.m_leading = wxT("Lead");
		v.m_owner = wxT("owner");
		v.m_sources = { Accruals(), Events() };
		return v;
	}

	// The tax's: no action period, a base read by registration, led by the accruals.
	static ibRecalculationSpec TaxSpec() {
		ibRecalculationSpec v;
		v.m_table = wxT("Tax");
		v.m_mark = { wxT("recorder_"), wxT("kind"), wxT("emp") };
		v.m_typeId = wxT("kind");
		v.m_registration = wxT("reg_");
		v.m_active = wxT("active_");
		v.m_storno = wxT("storno");
		v.m_baseStart = wxT("bps");
		v.m_baseEnd = wxT("bpe");
		v.m_baseByRegistration = true;
		v.m_leading = wxT("Lead");
		v.m_owner = wxT("owner");
		v.m_sources = { Accruals() };
		return v;
	}

	// The marks the records of `recorder` leave, as the write's statement computes them: its records lead, and no
	// record of its own is led. `emp` narrows both sides to one employee.
	Rows Marks(const ibRecalculationSpec& spec, const wxString& recorder, const wxString& emp = wxString()) {
		Rows out;
		const ibRecalculationNarrowing narrowing = [recorder, emp](const wxString& alias, int source) {
			const ibQueryExprPtr own = ibBinOp(ibQueryBinOp::Eq, ibCol(alias, wxT("recorder_")), ibConst(ibValue(recorder)));
			ibQueryExprPtr all = source < 0 ? ibNot(own) : own;
			if (!emp.IsEmpty())
				all = ibBinOp(ibQueryBinOp::And, all, ibBinOp(ibQueryBinOp::Eq, ibCol(alias, wxT("emp")), ibConst(ibValue(emp))));
			return all;
		};
		const ibQueryRelPtr relation = ibRecalculationRelation(spec, narrowing);
		if (!relation)
			return out;
		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(ibSubquery(relation, wxT("m")));
		std::vector<ibQueryProjItem> proj;
		for (const wxString& field : spec.m_mark)
			proj.push_back(ibQueryProjItem{ ibCol(wxT("m"), field), field });
		q.Project(proj);
		ibQueryResult rs = q.Execute();
		const bool month = spec.m_mark.size() > 3;
		while (rs.Next()) {
			const wxString stored = month ? rs.GetResultString(wxT("ap_")) : wxString();
			int day = 0;
			if (month) {
				wxDateTime d;
				d.ParseFormat(stored.Left(10), wxT("%Y-%m-%d"));
				day = DayNumber(d);
			}
			const bool fresh = out.insert(Row{ rs.GetResultString(wxT("recorder_")), rs.GetResultString(wxT("kind")),
				rs.GetResultString(wxT("emp")), day }).second;
			EXPECT_TRUE(fresh) << "a row is read once";
		}
		return out;
	}

	// A month's salary or bonus as a payroll run writes it: for the month, over the month, based on the month.
	static Rec PayRun(const wxString& recorder, int line, const wxString& emp, const wxString& kind, int m) {
		Rec r;
		r.recorder = recorder; r.line = line; r.emp = emp; r.kind = kind;
		r.reg = MonthStart(m); r.month = MonthStart(m);
		r.first = MonthStart(m); r.last = MonthEnd(m);
		r.baseFirst = MonthStart(m); r.baseLast = MonthEnd(m);
		return r;
	}
	static Rec Event(const wxString& recorder, const wxString& emp, const wxString& kind, int first, int last, int registeredIn) {
		Rec r;
		r.table = wxT("Ev"); r.recorder = recorder; r.line = 1; r.emp = emp; r.kind = kind;
		r.reg = MonthStart(registeredIn); r.first = first; r.last = last;
		return r;
	}
	// The storno and the correction a later run writes for a paid record: its position, registered in month m.
	static std::pair<Rec, Rec> Correction(const Rec& paid, const wxString& recorder, int m) {
		Rec storno = paid, again = paid;
		storno.recorder = again.recorder = recorder;
		storno.line = 100 + paid.line; again.line = 200 + paid.line;
		storno.reg = again.reg = MonthStart(m);
		storno.storno = true;
		return { storno, again };
	}
};

} // namespace

// A sick leave for June, written after June was paid: it marks the June salary of its employee, and nothing else.
TEST_F(CalcRecalculationFix, ASickLeaveMarksTheSalaryItCuts)
{
	if (!ready) return;
	Leads(wxT("Salary"), wxT("SickLeave"));
	Add(PayRun(wxT("P6"), 1, wxT("E"), wxT("Salary"), 5));
	Add(PayRun(wxT("P6"), 2, wxT("F"), wxT("Salary"), 5));
	Add(Event(wxT("S1"), wxT("E"), wxT("SickLeave"), MonthStart(5) + 9, MonthStart(5) + 14, 6));
	EXPECT_EQ(Marks(AccSpec(), wxT("S1")), (Rows{ Row{ wxT("P6"), wxT("Salary"), wxT("E"), MonthStart(5) } }));
	EXPECT_TRUE(Marks(AccSpec(), wxT("P6")).empty());   // a salary leads no salary
}

// ⭐ WHEN EITHER WAS REGISTERED DOES NOT DECIDE — the write does. A leave registered in June, the salary's own month,
// written after the salary was computed, marks it all the same. And a recorder never marks its own records: a payroll
// holding a salary and the leave that cuts it computed both together.
TEST_F(CalcRecalculationFix, TheRecorderNeverMarksItsOwnRecords)
{
	if (!ready) return;
	Leads(wxT("Salary"), wxT("SickLeave"));
	Add(PayRun(wxT("P6"), 1, wxT("E"), wxT("Salary"), 5));
	Add(Event(wxT("S1"), wxT("E"), wxT("SickLeave"), MonthStart(5) + 9, MonthStart(5) + 14, 5));
	EXPECT_EQ(Marks(AccSpec(), wxT("S1")), (Rows{ Row{ wxT("P6"), wxT("Salary"), wxT("E"), MonthStart(5) } }));

	Rec ownLeave = PayRun(wxT("P6"), 2, wxT("E"), wxT("SickLeave"), 5);
	ownLeave.first = MonthStart(5) + 3; ownLeave.last = MonthStart(5) + 5;
	Add(ownLeave);
	EXPECT_TRUE(Marks(AccSpec(), wxT("P6")).empty());
}

// ⭐ A CORRECTION IS A RECORD OF ITS PERIOD, AND LEADS LIKE ONE. The July run reverses the June salary and pays it again
// for the days the leave left: the leave marks the June salary, and the correction marks the June bonus computed on it
// — and not the run's own bonus correction beside it.
TEST_F(CalcRecalculationFix, ACorrectionMarksWhatItFeeds)
{
	if (!ready) return;
	Leads(wxT("Salary"), wxT("SickLeave"));
	Leads(wxT("Bonus"), wxT("Salary"));
	const Rec salary = PayRun(wxT("P6"), 1, wxT("E"), wxT("Salary"), 5);
	const Rec bonus = PayRun(wxT("P6"), 2, wxT("E"), wxT("Bonus"), 5);
	Add(salary);
	Add(bonus);
	Add(Event(wxT("S1"), wxT("E"), wxT("SickLeave"), MonthStart(5) + 9, MonthStart(5) + 14, 6));
	EXPECT_EQ(Marks(AccSpec(), wxT("S1")), (Rows{ Row{ wxT("P6"), wxT("Salary"), wxT("E"), MonthStart(5) } }));

	const auto salaryFixed = Correction(salary, wxT("P7"), 6);
	Add(salaryFixed.first);
	Add(salaryFixed.second);
	const Rows bonusOfJune{ Row{ wxT("P6"), wxT("Bonus"), wxT("E"), MonthStart(5) } };
	EXPECT_EQ(Marks(AccSpec(), wxT("P7")), bonusOfJune);

	const auto bonusFixed = Correction(bonus, wxT("P7"), 6);
	Add(bonusFixed.first);
	Add(bonusFixed.second);
	EXPECT_EQ(Marks(AccSpec(), wxT("P7")), bonusOfJune);
}

// A salary running past its own month (registered in June, in force until 10 July) is met by a sick leave of 5 July.
TEST_F(CalcRecalculationFix, ARecordReachingPastItsPeriodIsMetThere)
{
	if (!ready) return;
	Leads(wxT("Salary"), wxT("SickLeave"));
	Rec salary = PayRun(wxT("P6"), 1, wxT("E"), wxT("Salary"), 5);
	salary.last = MonthStart(6) + 9;
	Add(salary);
	Add(Event(wxT("S1"), wxT("E"), wxT("SickLeave"), MonthStart(6) + 4, MonthStart(6) + 6, 6));
	EXPECT_EQ(Marks(AccSpec(), wxT("S1")), (Rows{ Row{ wxT("P6"), wxT("Salary"), wxT("E"), MonthStart(5) } }));
}

// An inactive record counts for nothing — it marks nothing, and nothing marks it.
TEST_F(CalcRecalculationFix, AnInactiveRecordCountsForNothing)
{
	if (!ready) return;
	Leads(wxT("Salary"), wxT("SickLeave"));
	Add(PayRun(wxT("P6"), 1, wxT("E"), wxT("Salary"), 5));
	Rec leave = Event(wxT("S1"), wxT("E"), wxT("SickLeave"), MonthStart(5) + 9, MonthStart(5) + 14, 6);
	leave.active = false;
	Add(leave);
	EXPECT_TRUE(Marks(AccSpec(), wxT("S1")).empty());

	Rec other = PayRun(wxT("P6"), 2, wxT("F"), wxT("Salary"), 5);
	other.active = false;
	Add(other);
	Add(Event(wxT("S2"), wxT("F"), wxT("SickLeave"), MonthStart(5) + 20, MonthStart(5) + 22, 6));
	EXPECT_TRUE(Marks(AccSpec(), wxT("S2")).empty());
}

// A base by registration period is met by what is REGISTERED in it: a salary registered in January or February marks
// the tax whose base is those two months, one registered in March does not.
TEST_F(CalcRecalculationFix, ABaseByRegistrationIsLedByWhatIsRegisteredInIt)
{
	if (!ready) return;
	Leads(wxT("Tax"), wxT("Salary"));
	Rec tax;
	tax.table = wxT("Tax"); tax.recorder = wxT("T1"); tax.line = 1; tax.emp = wxT("E"); tax.kind = wxT("Tax");
	tax.reg = MonthStart(0); tax.baseFirst = MonthStart(0); tax.baseLast = MonthEnd(1);   // January and February
	Add(tax);
	const Rows taxMarked{ Row{ wxT("T1"), wxT("Tax"), wxT("E"), 0 } };
	Add(PayRun(wxT("P1"), 1, wxT("E"), wxT("Salary"), 0));
	EXPECT_EQ(Marks(TaxSpec(), wxT("P1")), taxMarked);
	Add(PayRun(wxT("P2"), 1, wxT("E"), wxT("Salary"), 1));
	EXPECT_EQ(Marks(TaxSpec(), wxT("P2")), taxMarked);
	Add(PayRun(wxT("P3"), 1, wxT("E"), wxT("Salary"), 2));
	EXPECT_TRUE(Marks(TaxSpec(), wxT("P3")).empty());
}

// Narrowed to one employee, the marks are that employee's share of the whole.
TEST_F(CalcRecalculationFix, NarrowedToOneEmployeeTheMarksAreThatEmployeesShare)
{
	if (!ready) return;
	Leads(wxT("Salary"), wxT("SickLeave"));
	Add(PayRun(wxT("P6"), 1, wxT("E"), wxT("Salary"), 5));
	Add(PayRun(wxT("P6"), 2, wxT("F"), wxT("Salary"), 5));
	Add(Event(wxT("S1"), wxT("E"), wxT("SickLeave"), MonthStart(5) + 9, MonthStart(5) + 14, 6));
	Rec second = Event(wxT("S1"), wxT("F"), wxT("SickLeave"), MonthStart(5) + 2, MonthStart(5) + 3, 6);
	second.line = 2;
	Add(second);
	EXPECT_EQ(Marks(AccSpec(), wxT("S1")).size(), 2u);
	EXPECT_EQ(Marks(AccSpec(), wxT("S1"), wxT("F")), (Rows{ Row{ wxT("P6"), wxT("Salary"), wxT("F"), MonthStart(5) } }));
}

// ---- random registers, against the rule ---------------------------------------------------------------

TEST_F(CalcRecalculationFix, RandomRegistersAgreeWithTheRule)
{
	if (!ready) return;
	uint32_t seed = 20260914;   // mulberry32, as the views' random test draws
	const auto rnd = [&seed](int n) {
		seed += 0x6D2B79F5u;
		uint32_t t = seed;
		t = (t ^ (t >> 15)) * (1u | t);
		t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
		return static_cast<int>(((t ^ (t >> 14)) % 1000000u) * static_cast<uint64_t>(n) / 1000000u);
	};
	const std::vector<std::pair<wxString, wxString>> leading = {
		{ wxT("Salary"), wxT("SickLeave") }, { wxT("Salary"), wxT("Vacation") }, { wxT("Bonus"), wxT("Salary") } };
	const wxString employees[] = { wxT("E0"), wxT("E1"), wxT("E2"), wxT("E3") };

	for (int round = 0; round < 4; ++round) {
		db->RunQuery(wxT("DELETE FROM Acc"));
		db->RunQuery(wxT("DELETE FROM Ev"));
		db->RunQuery(wxT("DELETE FROM Lead"));
		for (const auto& edge : leading)
			Leads(edge.first, edge.second);

		std::vector<Rec> records;
		for (int m = 0; m < 8; ++m) {
			int line = 0;
			for (const wxString& emp : employees) {
				Rec salary = PayRun(wxString::Format(wxT("P%d"), m), ++line, emp, wxT("Salary"), m);
				if (rnd(6) == 0)
					salary.last = MonthStart(m + 1) + rnd(20);   // in force past its month
				salary.active = rnd(12) != 0;
				records.push_back(salary);
				Rec bonus = PayRun(wxString::Format(wxT("P%d"), m), ++line, emp, wxT("Bonus"), m);
				if (rnd(8) == 0)
					bonus.baseLast = MonthEnd(m + 1);   // a base past its month
				records.push_back(bonus);
			}
		}
		for (int k = 0; k < 24; ++k) {
			const int first = rnd(MonthStart(8));
			Rec event = Event(wxString::Format(wxT("S%d"), k), employees[rnd(4)], rnd(2) == 0 ? wxT("SickLeave") : wxT("Vacation"),
				first, first + rnd(12), MonthOfDay(first) + rnd(3));
			event.active = rnd(10) != 0;
			records.push_back(event);
		}
		// Corrections of paid records, registered one to three months later — some of them corrected again.
		const size_t paid = records.size();
		for (int k = 0; k < 12; ++k) {
			const Rec& target = records[rnd(static_cast<int>(paid))];
			if (target.table != wxT("Acc"))
				continue;
			const auto fixed = Correction(target, wxString::Format(wxT("C%d"), k), MonthOfDay(target.reg) + 1 + rnd(3));
			records.push_back(fixed.first);
			records.push_back(fixed.second);
		}
		for (const Rec& r : records)
			Add(r);

		std::set<wxString> recorders;
		for (const Rec& r : records)
			recorders.insert(r.recorder);
		for (const wxString& recorder : recorders) {
			const Rows rule = RuleRows(records, leading, wxT("Acc"), /*acts*/ true, /*baseByRegistration*/ false, recorder);
			EXPECT_EQ(Marks(AccSpec(), recorder), rule) << "round " << round << ", recorder " << recorder;
			Rows one;
			for (const Row& row : rule)
				if (std::get<2>(row) == wxT("E2"))
					one.insert(row);
			EXPECT_EQ(Marks(AccSpec(), recorder, wxT("E2")), one) << "round " << round << ", recorder " << recorder;
		}
	}
}
