// =============================================================================
// The calculation register's fact (calculationRegister.h, "The fact — a reading of the records") — run on a live
// in-memory SQLite and held to the rules.
//
// The fact is ibCalcFactRelation, executed over the records alone, with the Displacing section joined as it is
// read: a displacer is in force while its active records outnumber its active stornos. The oracle is the rules
// said directly: positions folded arithmetically, then ibComputeActionPeriodDisplacementByRelation over the
// positions in force — the rule cases of test_calcDisplacement.cpp first, then random registers written, rewritten
// and erased, compared record by record.
//
// (Firebird 5.0.3 ran the walk against the same rule while it was written, 2026-09-12; it is where the
// narrowing had to move inside — calculationRegister.h.)
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <tuple>
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

// The tables in plain names: the registration period, one dimension (emp), the type as a plain string, dates as
// the platform keeps them on SQLite, one resource; the Displacing section as (owner, named).
ibCalcViewSpec Spec()
{
	ibCalcViewSpec v;
	v.m_period        = wxT("reg_");
	v.m_dimensions    = { wxT("emp") };
	v.m_type          = { wxT("kind") };
	v.m_actionPeriod  = { wxT("ap_") };
	v.m_start         = wxT("aps");
	v.m_end           = wxT("ape");
	v.m_resources     = { wxT("result_") };
	v.m_typeId        = wxT("kind");
	v.m_displacing    = wxT("Disp");
	v.m_owner         = wxT("owner");
	v.m_named         = { { wxT("kind"), wxT("named") } };
	v.m_namedId       = wxT("named");
	v.m_records       = wxT("Reg");
	v.m_recordKey     = { wxT("recorder_"), wxT("line_") };
	v.m_active        = wxT("active_");
	v.m_storno        = wxT("storno");
	v.m_recordFields  = { v.m_active, v.m_storno };
	return v;
}

// Day n of the calendar these tests use, as a stored date.
wxString DateOf(int day)
{
	wxDateTime d(1, wxDateTime::Jan, 2026);
	d += wxDateSpan::Days(day);
	return d.Format(wxT("%Y-%m-%d 00:00:00"));
}

// By the Julian day: a difference of local times across a daylight-saving change is a day short.
int DayOf(const wxString& stored)
{
	wxDateTime d;
	d.ParseFormat(stored.Left(10), wxT("%Y-%m-%d"));
	return static_cast<int>(std::lround(d.GetJulianDayNumber() - wxDateTime(1, wxDateTime::Jan, 2026).GetJulianDayNumber()));
}

struct Record {
	wxString recorder;
	int      line = 0;
	wxString emp = wxT("E");
	wxString kind;
	int      reg = 0;           // the registration period, as a day
	int      month = 0;         // the month-for, as a day
	int      first = 0, last = 0;
	bool     storno = false;
	bool     active = true;
	int      result = 0;
};

// A position: what a displacer is in force by. A record: its recorder and line — what the fact's rows are of.
using Position = std::tuple<wxString, wxString, int, int, int>;   // emp, kind, month, first, last
using RecordId = std::pair<wxString, int>;
using Days = std::vector<std::pair<int, int>>;
using Pieces = std::map<RecordId, Days>;

Position PositionOf(const Record& r) { return Position{ r.emp, r.kind, r.month, r.first, r.last }; }
RecordId IdOf(const Record& r) { return RecordId{ r.recorder, r.line }; }

struct Standing { int count = 0; int result = 0; };

// The rules, asked directly. Positions folded ARITHMETICALLY — a record adds one and its result, a storno
// takes one away and adds its result (turned round already), an inactive record adds nothing — and in force
// while the count is above zero; then displacement per employee over the positions in force with days.
std::map<Position, Standing> Fold(const std::vector<Record>& records)
{
	std::map<Position, Standing> out;
	for (const Record& r : records) {
		if (!r.active)
			continue;
		Standing& s = out[PositionOf(r)];
		s.count += r.storno ? -1 : 1;
		s.result += r.result;
	}
	return out;
}

Pieces RulesFact(const std::vector<Record>& records, const std::vector<std::pair<wxString, wxString>>& displaces)
{
	std::map<wxString, int> typeIndex;
	const auto ordinal = [&typeIndex](const wxString& t) {
		const auto it = typeIndex.find(t);
		if (it != typeIndex.end()) return it->second;
		const int next = static_cast<int>(typeIndex.size());
		typeIndex.emplace(t, next);
		return next;
	};
	std::vector<std::pair<int, int>> edges;
	for (const auto& d : displaces)
		edges.push_back({ ordinal(d.first), ordinal(d.second) });

	const auto typeOf = [&typeIndex](const wxString& kind) {
		const auto it = typeIndex.find(kind);
		return it != typeIndex.end() ? it->second : -1;
	};

	// The displacers: the positions in force with days, by employee.
	std::map<wxString, std::vector<Position>> byEmp;
	for (const auto& entry : Fold(records))
		if (entry.second.count > 0 && std::get<3>(entry.first) <= std::get<4>(entry.first))
			byEmp[std::get<0>(entry.first)].push_back(entry.first);

	// Every active record with days, stornos included: its days minus those of the displacers of its type.
	Pieces out;
	for (const Record& r : records) {
		if (!r.active || r.first > r.last)
			continue;
		std::vector<ibActionPeriodTypedRecord> typed{ { typeOf(r.kind), r.first, r.last + 1 } };
		for (const Position& p : byEmp[r.emp])
			typed.push_back({ typeOf(std::get<1>(p)), std::get<3>(p), std::get<4>(p) + 1 });
		const auto actual = ibComputeActionPeriodDisplacementByRelation(typeIndex.size(), edges, typed);
		for (const ibActionInterval& piece : actual[0])
			if (piece.end > piece.start)
				out[IdOf(r)].push_back({ static_cast<int>(piece.start), static_cast<int>(piece.end - 1) });
	}
	for (auto& entry : out)
		std::sort(entry.second.begin(), entry.second.end());
	return out;
}

struct CalcViewsFix : ::testing::Test {
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
		db->RunQuery(wxT("CREATE TABLE Reg (recorder_ TEXT NOT NULL, line_ INTEGER NOT NULL, emp TEXT, kind TEXT, ")
			wxT("reg_ TEXT, ap_ TEXT, aps TEXT, ape TEXT, storno INTEGER NOT NULL, active_ INTEGER NOT NULL, result_ NUMERIC, ")
			wxT("PRIMARY KEY (recorder_, line_))"));
		db->RunQuery(wxT("CREATE TABLE Disp (owner TEXT NOT NULL, named TEXT NOT NULL)"));
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}

	void Clear() { db->RunQuery(wxT("DELETE FROM Reg")); db->RunQuery(wxT("DELETE FROM Disp")); }
	void Displaces(const wxString& owner, const wxString& named) {
		db->RunQuery(wxT("INSERT INTO Disp (owner, named) VALUES ('%s', '%s')"), owner, named);
	}
	void NoLongerDisplaces(const wxString& owner, const wxString& named) {
		db->RunQuery(wxT("DELETE FROM Disp WHERE owner = '%s' AND named = '%s'"), owner, named);
	}
	void Add(const Record& r) {
		db->RunStatement(wxString::Format(
			wxT("INSERT INTO Reg (recorder_, line_, emp, kind, reg_, ap_, aps, ape, storno, active_, result_) ")
			wxT("VALUES ('%s', %d, '%s', '%s', '%s', '%s', '%s', '%s', %d, %d, %d)"),
			r.recorder, r.line, r.emp, r.kind, DateOf(r.reg), DateOf(r.month), DateOf(r.first), DateOf(r.last),
			r.storno ? 1 : 0, r.active ? 1 : 0, r.result));
	}
	// A record written again as `r` says now.
	void Rewrite(const Record& r) {
		db->RunStatement(wxString::Format(
			wxT("UPDATE Reg SET emp = '%s', kind = '%s', reg_ = '%s', ap_ = '%s', aps = '%s', ape = '%s', storno = %d, ")
			wxT("active_ = %d, result_ = %d WHERE recorder_ = '%s' AND line_ = %d"),
			r.emp, r.kind, DateOf(r.reg), DateOf(r.month), DateOf(r.first), DateOf(r.last), r.storno ? 1 : 0,
			r.active ? 1 : 0, r.result, r.recorder, r.line));
	}
	void Erase(const Record& r) {
		db->RunStatement(wxString::Format(wxT("DELETE FROM Reg WHERE recorder_ = '%s' AND line_ = %d"), r.recorder, r.line));
	}

	static ibCalcNarrowing ToEmployee(const wxString& emp) {
		if (emp.IsEmpty())
			return nullptr;
		return [emp](const wxString& alias, bool) { return ibBinOp(ibQueryBinOp::Eq, ibCol(alias, wxT("emp")), ibConst(ibValue(emp))); };
	}

	// A record's own field narrows the subject alone — the records the reading produces, never their displacers.
	static ibCalcNarrowing ToKind(const wxString& kind) {
		return [kind](const wxString& alias, bool subject) {
			return subject ? ibBinOp(ibQueryBinOp::Eq, ibCol(alias, wxT("kind")), ibConst(ibValue(kind))) : ibQueryExprPtr();
		};
	}

	// The fact, as the reading computes it — optionally narrowed inside.
	Pieces Fact(const ibCalcNarrowing& narrowing = nullptr) {
		ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
		q.From(ibSubquery(ibCalcFactRelation(Spec(), narrowing), wxT("f")));
		std::vector<ibQueryProjItem> proj;
		for (const wxChar* field : { wxT("recorder_"), wxT("line_"), wxT("aps"), wxT("ape") })
			proj.push_back(ibQueryProjItem{ ibCol(wxT("f"), field), field });
		q.Project(proj);
		Pieces out;
		ibQueryResult rs = q.Execute();
		while (rs.Next())
			out[RecordId{ rs.GetResultString(wxT("recorder_")), rs.GetResultInt(wxT("line_")) }].push_back(
				{ DayOf(rs.GetResultString(wxT("aps"))), DayOf(rs.GetResultString(wxT("ape"))) });
		for (auto& entry : out)
			std::sort(entry.second.begin(), entry.second.end());
		return out;
	}

	Days PiecesOf(const Record& r) {
		const Pieces all = Fact();
		const auto it = all.find(IdOf(r));
		return it != all.end() ? it->second : Days();
	}

	// The periods registered before `next` — what ComputeRows asks of every table for a moment in the period before
	// it (ibCalcViewArg).
	static ibCalcNarrowing Before(const wxDateTime& next) {
		return [next](const wxString& alias, bool) {
			return ibBinOp(ibQueryBinOp::Lt, ibCol(alias, wxT("reg_")), ibConst(ibValue(next)));
		};
	}
};

} // namespace

// ---- the rule cases of test_calcDisplacement.cpp, in whole days --------------------------------------

TEST_F(CalcViewsFix, ADisplacerCutsTheDisplacedAndOrderDoesNotDecide)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	const Record absence{ wxT("A"), 1, wxT("E"), wxT("Absence"), 0, 0, 10, 19 };
	const Record salary{ wxT("A"), 2, wxT("E"), wxT("Salary"), 0, 0, 0, 30 };
	Add(absence);
	Add(salary);
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 9 }, { 20, 30 } }));
	EXPECT_EQ(PiecesOf(absence), (Days{ { 10, 19 } }));
}

// ⭐⭐ WHO DISPLACES WHOM IS READ WITH THE FACT: the Displacing section changed, the records already written read
// the new way at once — nothing kept had to follow the section.
TEST_F(CalcViewsFix, TheSectionActsAtOnceOnRecordsAlreadyWritten)
{
	if (!ready) return;
	const Record salary{ wxT("A"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 30 };
	Add(salary);
	Add({ wxT("A"), 2, wxT("E"), wxT("Absence"), 0, 0, 10, 19 });
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 30 } }));
	Displaces(wxT("Salary"), wxT("Absence"));
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 9 }, { 20, 30 } }));
	NoLongerDisplaces(wxT("Salary"), wxT("Absence"));
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 30 } }));
}

// A row of the section naming its own owner is left out: a type does not displace itself.
TEST_F(CalcViewsFix, ATypeDoesNotDisplaceItself)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Salary"));
	const Record first{ wxT("A"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 30 };
	const Record second{ wxT("A"), 2, wxT("E"), wxT("Salary"), 0, 0, 10, 19 };
	Add(first);
	Add(second);
	EXPECT_EQ(PiecesOf(first), (Days{ { 0, 30 } }));
	EXPECT_EQ(PiecesOf(second), (Days{ { 10, 19 } }));
}

TEST_F(CalcViewsFix, APartialOrderIsNotMadeTotal)
{
	if (!ready) return;
	Displaces(wxT("B"), wxT("C")); Displaces(wxT("A"), wxT("C")); Displaces(wxT("Z"), wxT("B"));
	const Record a{ wxT("A"), 1, wxT("E"), wxT("A"), 0, 0, 0, 9 };
	const Record b{ wxT("A"), 2, wxT("E"), wxT("B"), 0, 0, 0, 9 };
	Add(a);
	Add(b);
	EXPECT_EQ(PiecesOf(a), (Days{ { 0, 9 } }));
	EXPECT_EQ(PiecesOf(b), (Days{ { 0, 9 } }));
}

TEST_F(CalcViewsFix, AFullyCoveredPositionHasNoPiece)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	const Record salary{ wxT("A"), 1, wxT("E"), wxT("Salary"), 0, 0, 10, 19 };
	Add(salary);
	Add({ wxT("A"), 2, wxT("E"), wxT("Absence"), 0, 0, 0, 30 });
	EXPECT_TRUE(PiecesOf(salary).empty());
}

// ⭐ A STORNO NETS OUT, as an accounting reversal does: June paid, reversed in July, corrected in the same July run
// with a record of its own. The fact is of RECORDS: the paid record, its storno and the correction are rows of
// their own with the same days, the storno's figures turned round, so summed the first two net out.
TEST_F(CalcViewsFix, AStornoNetsOutAndTheCorrectionStands)
{
	if (!ready) return;
	Record june{ wxT("P6"), 1, wxT("E"), wxT("S"), 150, 150, 150, 179 };
	june.result = 3000;
	Record reversal{ wxT("P7"), 1, wxT("E"), wxT("S"), 181, 150, 150, 179, /*storno*/ true };
	reversal.result = -3000;
	Add(june);
	Add(reversal);
	EXPECT_EQ(PiecesOf(june), (Days{ { 150, 179 } }));
	EXPECT_EQ(PiecesOf(reversal), (Days{ { 150, 179 } }));

	Record correction{ wxT("P7"), 2, wxT("E"), wxT("S"), 181, 150, 150, 179 };
	correction.result = 2500;
	Add(correction);
	EXPECT_EQ(PiecesOf(correction), (Days{ { 150, 179 } }));
}

// ⭐ A MOMENT READS THE PERIODS UP TO IT. June's salary paid in June; in July a sick leave for June arrives and the
// July run reverses the salary and pays it again for the days the leave left — every record registered in July.
// Read as of June, June stands as it was paid: whole, cut by nothing. Read with every period, it stands as
// corrected.
TEST_F(CalcViewsFix, AMomentReadsThePeriodsUpToIt)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("SickLeave"));
	const int june = 151, july = 181;   // 1 June and 1 July 2026
	Record paid{ wxT("P6"), 1, wxT("E"), wxT("Salary"), june, june, june, july - 1 };
	paid.result = 3000;
	Add(paid);
	Add({ wxT("S1"), 1, wxT("E"), wxT("SickLeave"), july, june, june + 9, june + 14 });
	Record reversal = paid;
	reversal.recorder = wxT("P7"); reversal.reg = july; reversal.storno = true; reversal.result = -3000;
	Record again = paid;
	again.recorder = wxT("P7"); again.line = 2; again.reg = july; again.result = 2500;
	Add(reversal);
	Add(again);

	const wxDateTime julyFirst(1, wxDateTime::Jul, 2026);
	const Pieces factThen = Fact(Before(julyFirst));
	ASSERT_EQ(factThen.size(), 1u) << "July's records are not June's";
	EXPECT_EQ(factThen.at(IdOf(paid)), (Days{ { june, july - 1 } }));

	EXPECT_EQ(PiecesOf(paid), (Days{ { june, june + 8 }, { june + 15, july - 1 } }));
	EXPECT_EQ(PiecesOf(again), (Days{ { june, june + 8 }, { june + 15, july - 1 } }));
}

TEST_F(CalcViewsFix, AReversedDisplacerCutsNothing)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("SickLeave"));
	const Record salary{ wxT("J"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 29 };
	const Record sick{ wxT("J"), 2, wxT("E"), wxT("SickLeave"), 0, 0, 9, 19 };
	Add(salary);
	Add(sick);
	Add({ wxT("K"), 1, wxT("E"), wxT("SickLeave"), 31, 0, 9, 19, true });
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 29 } }));
	// The reversed leave keeps its row — its storno beside it turns its figures round — and cuts nothing.
	EXPECT_EQ(PiecesOf(sick), (Days{ { 9, 19 } }));
}

// Active (Max, 2026-09-12): an inactive record exists and counts for nothing — it cuts nothing, stands in
// no position, and an inactive storno reverses nothing. Switched on, it counts; switched off again, it stops.
TEST_F(CalcViewsFix, AnInactiveRecordCountsForNothing)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	const Record salary{ wxT("A"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 30 };
	Add(salary);
	Record absence{ wxT("A"), 2, wxT("E"), wxT("Absence"), 0, 0, 10, 19 };
	absence.active = false;
	Add(absence);
	Record storno{ wxT("B"), 1, wxT("E"), wxT("Salary"), 31, 0, 0, 30, true };
	storno.active = false;
	Add(storno);
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 30 } }));
	EXPECT_TRUE(PiecesOf(absence).empty());

	absence.active = true;
	Rewrite(absence);
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 9 }, { 20, 30 } }));
	absence.active = false;
	Rewrite(absence);
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 30 } }));
}

// A record whose last day is before its first is in force on no day: no piece, and it cuts nothing.
TEST_F(CalcViewsFix, ARecordWithNoDaysHasNoPieceAndCutsNothing)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	const Record salary{ wxT("A"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 30 };
	const Record absence{ wxT("A"), 2, wxT("E"), wxT("Absence"), 0, 0, 20, 10 };
	const Record empty{ wxT("A"), 3, wxT("E"), wxT("Salary"), 0, 0, 12, 5 };
	Add(salary);
	Add(absence);
	Add(empty);
	EXPECT_EQ(PiecesOf(salary), (Days{ { 0, 30 } }));
	EXPECT_TRUE(PiecesOf(absence).empty());
	EXPECT_TRUE(PiecesOf(empty).empty());
}

// The narrowing on the dimension is inside, and it changes nothing about the employee it keeps.
TEST_F(CalcViewsFix, NarrowedToOneEmployeeTheFactIsThatEmployeesShare)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	Add({ wxT("A"), 1, wxT("E1"), wxT("Salary"), 0, 0, 0, 30 });
	Add({ wxT("A"), 2, wxT("E1"), wxT("Absence"), 0, 0, 10, 19 });
	const Record other{ wxT("A"), 3, wxT("E2"), wxT("Salary"), 0, 0, 0, 30 };
	Add(other);
	const Pieces all = Fact(), one = Fact(ToEmployee(wxT("E1")));
	EXPECT_EQ(one.size(), 2u);
	for (const auto& entry : one)
		EXPECT_EQ(entry.second, all.at(entry.first));
	EXPECT_TRUE(one.find(IdOf(other)) == one.end());
}

// Narrowed by the calculation type, the subject is narrowed and its displacers are not: the salary still loses
// the days the absence takes, and the absence's own record is no row of the answer.
TEST_F(CalcViewsFix, NarrowedToOneTypeTheDisplacersStillCut)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	const Record salary{ wxT("A"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 30 };
	const Record absence{ wxT("A"), 2, wxT("E"), wxT("Absence"), 0, 0, 10, 19 };
	Add(salary);
	Add(absence);
	const Pieces salaries = Fact(ToKind(wxT("Salary")));
	ASSERT_EQ(salaries.size(), 1u);
	EXPECT_EQ(salaries.at(IdOf(salary)), (Days{ { 0, 9 }, { 20, 30 } }));
}

// FOR WHICH DAYS (ibCalcViewArg) — what ComputeRows asks of the subject for Begin and End: its own days meet them.
// The records outside are no rows; a displacer outside still cuts the ones inside.
TEST_F(CalcViewsFix, AnIntervalChoosesTheSubjectAndNotItsDisplacers)
{
	if (!ready) return;
	Displaces(wxT("Salary"), wxT("Absence"));
	const Record june{ wxT("P"), 1, wxT("E"), wxT("Salary"), 0, 0, 0, 29 };
	const Record july{ wxT("P"), 2, wxT("E"), wxT("Salary"), 0, 30, 30, 60 };
	const Record absence{ wxT("A"), 1, wxT("E"), wxT("Absence"), 0, 0, 25, 34 };
	Add(june);
	Add(july);
	Add(absence);
	const auto from = [](int day) {
		return [day](const wxString& alias, bool subject) {
			return subject ? ibBinOp(ibQueryBinOp::Ge, ibCol(alias, wxT("ape")), ibConst(ibValue(DateOf(day)))) : ibQueryExprPtr();
		};
	};
	const Pieces july_on = Fact(from(30));
	EXPECT_TRUE(july_on.find(IdOf(june)) == july_on.end());
	EXPECT_EQ(july_on.at(IdOf(july)), (Days{ { 35, 60 } }));
	EXPECT_EQ(july_on.at(IdOf(absence)), (Days{ { 25, 34 } }));
}

// ---- random registers, against the rules ----------------------------------------------------------

TEST_F(CalcViewsFix, RandomRegistersAgreeWithTheRules)
{
	if (!ready) return;
	// mulberry32: the low bits of a power-of-two LCG cycle with a period of n, and the first prototype of
	// these views drew every record inactive from one phase of such a cycle — checks of nothing.
	uint32_t seed = 12345;
	const auto rnd = [&seed](int n) {
		seed += 0x6D2B79F5u;
		uint32_t t = seed;
		t = (t ^ (t >> 15)) * (1u | t);
		t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
		return static_cast<int>(((t ^ (t >> 14)) % 1000000u) * static_cast<uint64_t>(n) / 1000000u);
	};
	const wxString types[] = { wxT("Salary"), wxT("Absence"), wxT("Vacation"), wxT("Bonus") };

	size_t netted = 0, cut = 0, whole = 0, rewritten = 0, erased = 0;
	for (int round = 0; round < 150; ++round) {
		Clear();
		std::vector<std::pair<wxString, wxString>> displaces;
		for (const wxString& o : types)
			for (const wxString& d : types)
				if (o != d && rnd(3) == 0) { displaces.push_back({ o, d }); Displaces(o, d); }

		std::vector<Record> records;
		const int n = 3 + rnd(12);
		for (int i = 0; i < n; ++i) {
			Record r;
			if (!records.empty() && rnd(3) == 0) {   // a storno or a correction of a position already written
				r = records[rnd(static_cast<int>(records.size()))];
				r.reg += 30 * rnd(3);
				r.storno = rnd(2) == 0;
				r.result = r.storno ? -r.result : 100 * (1 + rnd(9));
			}
			else {
				r.emp = rnd(2) ? wxT("E1") : wxT("E2");
				r.kind = types[rnd(4)];
				r.first = rnd(30);
				r.last = r.first + rnd(20) - (rnd(10) == 0 ? 25 : 0);   // now and then a record with no days
				r.month = 30 * rnd(2);
				r.reg = 30 * rnd(3);
				r.storno = false;
				r.result = 100 * (1 + rnd(9));
			}
			r.recorder = wxString::Format(wxT("R%d"), 1 + rnd(4));
			r.line = i + 1;
			r.active = rnd(8) != 0;
			records.push_back(r);
			Add(r);
		}
		// …then rewritten and erased, as a posting does: the fact must follow both halves of every change.
		for (int k = 0; k < 3 && !records.empty(); ++k) {
			const size_t at = static_cast<size_t>(rnd(static_cast<int>(records.size())));
			if (rnd(3) == 0) {
				Erase(records[at]);
				records.erase(records.begin() + static_cast<std::ptrdiff_t>(at));
				++erased;
				continue;
			}
			Record& r = records[at];
			switch (rnd(4)) {
			case 0: r.active = !r.active; break;
			case 1: r.emp = r.emp == wxT("E1") ? wxT("E2") : wxT("E1"); break;
			case 2: r.first = rnd(30); r.last = r.first + rnd(20); break;
			default: r.result += 50; break;
			}
			Rewrite(r);
			++rewritten;
		}

		// Positions whose stornos netted them out — a displacer the fact must no longer find in force.
		for (const auto& entry : Fold(records))
			if (entry.second.count == 0)
				++netted;

		// The fact against the rules, record by record — whole, and narrowed to one type.
		std::map<RecordId, const Record*> byId;
		for (const Record& r : records)
			byId[IdOf(r)] = &r;
		const Pieces rules = RulesFact(records, displaces), views = Fact();
		EXPECT_EQ(views.size(), rules.size()) << "round " << round;
		const wxString kind = types[rnd(4)];
		const Pieces ofKind = Fact(ToKind(kind));
		size_t rulesOfKind = 0;
		for (const auto& entry : rules) {
			const Record& r = *byId.at(entry.first);
			const auto got = views.find(entry.first);
			EXPECT_EQ(got != views.end() ? got->second : Days(), entry.second) << "round " << round;
			if (r.kind == kind) {
				++rulesOfKind;
				const auto narrowed = ofKind.find(entry.first);
				EXPECT_EQ(narrowed != ofKind.end() ? narrowed->second : Days(), entry.second) << "round " << round << ", narrowed";
			}
			if (entry.second.size() >= 2) ++cut;
			if (entry.second == Days{ { r.first, r.last } }) ++whole;
		}
		EXPECT_EQ(ofKind.size(), rulesOfKind) << "round " << round << ", narrowed";
	}
	// A comparison over cases that never occur compares nothing: the registers must have held each.
	EXPECT_GT(cut, 10u) << "records cut in two or more";
	EXPECT_GT(whole, 10u) << "records left whole";
	EXPECT_GT(netted, 5u) << "positions whose stornos netted the count out";
	EXPECT_GT(rewritten, 100u);
	EXPECT_GT(erased, 50u);
}
