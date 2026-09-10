// Calculation engine — displacement by the relation a chart of calculation types declares (kernel).
#include <gtest/gtest.h>
#include "backend/calculation/calculation.h"

#include <utility>
#include <vector>

// A record is cut only by records whose calculation type is DECLARED to displace its own. Types are
// dense ordinals; edges are {displaced, displacer}. (The priority-rank kernel these tests once stood
// beside is gone, with its own suite: a rank is not the relation — see calculation.h.)

namespace {
using TR = ibActionPeriodTypedRecord;
using E2 = std::pair<int, int>;
enum { kSalary = 0, kAbsence = 1 };
}

TEST(CalcDisplacementByRelation, DisplacerCutsTheDisplaced) {
	// Absence displaces Salary. Salary for the month, Absence 10..20.
	const auto out = ibComputeActionPeriodDisplacementByRelation(2, { E2{ kSalary, kAbsence } }, {
		TR{ kSalary,  0, 31 },
		TR{ kAbsence, 10, 20 },
	});
	ASSERT_EQ(out.size(), 2u);
	ASSERT_EQ(out[0].size(), 2u);             // Salary is left in two pieces
	EXPECT_EQ(out[0][0].start, 0);  EXPECT_EQ(out[0][0].end, 10);
	EXPECT_EQ(out[0][1].start, 20); EXPECT_EQ(out[0][1].end, 31);
	ASSERT_EQ(out[1].size(), 1u);             // Absence is not cut by anything
	EXPECT_EQ(out[1][0].start, 10); EXPECT_EQ(out[1][0].end, 20);
}

TEST(CalcDisplacementByRelation, OrderOfTheRecordsDoesNotDecide) {
	// The same two movements added the other way round must give the same answer per record — the
	// line-order "priority" this replaced let Salary wipe Absence out when Absence came first.
	const auto out = ibComputeActionPeriodDisplacementByRelation(2, { E2{ kSalary, kAbsence } }, {
		TR{ kAbsence, 10, 20 },
		TR{ kSalary,  0, 31 },
	});
	ASSERT_EQ(out[0].size(), 1u);
	EXPECT_EQ(out[0][0].start, 10); EXPECT_EQ(out[0][0].end, 20);   // Absence intact
	ASSERT_EQ(out[1].size(), 2u);                                    // Salary cut
	EXPECT_EQ(out[1][0].end, 10);
	EXPECT_EQ(out[1][1].start, 20);
}

TEST(CalcDisplacementByRelation, NoDeclaredRelationCutsNothing) {
	const auto out = ibComputeActionPeriodDisplacementByRelation(2, {}, {
		TR{ kSalary,  0, 31 },
		TR{ kAbsence, 10, 20 },
	});
	ASSERT_EQ(out[0].size(), 1u); EXPECT_EQ(out[0][0].end, 31);
	ASSERT_EQ(out[1].size(), 1u); EXPECT_EQ(out[1][0].end, 20);
}

TEST(CalcDisplacementByRelation, APartialOrderIsNotMadeTotal) {
	// C displaces B, C displaces A, B displaces Z. A rank by longest chain puts B above A, and a kernel
	// that lets any higher rank win would have B cut A — an edge nobody declared. Here it must not.
	enum { A = 0, B = 1, C = 2, Z = 3 };
	const auto out = ibComputeActionPeriodDisplacementByRelation(4,
		{ E2{ B, C }, E2{ A, C }, E2{ Z, B } }, {
		TR{ A, 0, 10 },
		TR{ B, 0, 10 },
	});
	ASSERT_EQ(out[0].size(), 1u); EXPECT_EQ(out[0][0].start, 0); EXPECT_EQ(out[0][0].end, 10);   // A intact
	ASSERT_EQ(out[1].size(), 1u); EXPECT_EQ(out[1][0].end, 10);                                  // B intact
}

TEST(CalcDisplacementByRelation, FullyCoveredRecordIsEmpty) {
	const auto out = ibComputeActionPeriodDisplacementByRelation(2, { E2{ kSalary, kAbsence } }, {
		TR{ kSalary,  10, 20 },
		TR{ kAbsence, 0, 31 },
	});
	EXPECT_TRUE(out[0].empty());
}

TEST(CalcDisplacementByRelation, AnUnmentionedTypeNeitherCutsNorIsCut) {
	// type -1: the relation never names it (the record set met a type the chart does not list).
	const auto out = ibComputeActionPeriodDisplacementByRelation(2, { E2{ kSalary, kAbsence } }, {
		TR{ -1,       0, 31 },
		TR{ kAbsence, 10, 20 },
	});
	ASSERT_EQ(out[0].size(), 1u); EXPECT_EQ(out[0][0].end, 31);
	ASSERT_EQ(out[1].size(), 1u); EXPECT_EQ(out[1][0].end, 20);
}

// ---- STORNO: a correction is a movement of the current period (calculation.h) ----------------------------

namespace {
ibCalcStornoFact At(const wchar_t* position, int64_t registration, bool storno = false)
{
	ibCalcStornoFact fact;
	fact.position = { ibValue(wxString(position)) };
	fact.registration = registration;
	fact.storno = storno;
	return fact;
}
enum : int64_t { kJune = 6, kJuly = 7, kAugust = 8 };
}

TEST(CalcStorno, AStornoReversesTheEarlierRecordAndDoesNotActItself) {
	// June's salary, reversed in July, with July's correction beside the storno.
	const std::vector<bool> live = ibCalcInForce({
		At(L"Ivanov/Salary/June", kJune),
		At(L"Ivanov/Salary/June", kJuly, /*storno*/ true),
		At(L"Ivanov/Salary/June", kJuly),
	});
	EXPECT_FALSE(live[0]) << "the June record is reversed";
	EXPECT_FALSE(live[1]) << "a storno does not act";
	EXPECT_TRUE(live[2]) << "the correction registered beside the storno is not its target";
}

TEST(CalcStorno, ACorrectionOfACorrectionReversesTheLatest) {
	// June, corrected in July, corrected again in August: August's storno takes July's correction, and
	// June stays reversed by July's.
	const std::vector<bool> live = ibCalcInForce({
		At(L"p", kJune),
		At(L"p", kJuly, true), At(L"p", kJuly),
		At(L"p", kAugust, true), At(L"p", kAugust),
	});
	EXPECT_FALSE(live[0]);
	EXPECT_FALSE(live[2]) << "July's correction is what August reverses";
	EXPECT_TRUE(live[4]) << "only August's correction stands";
}

TEST(CalcStorno, AStornoReachesOnlyItsOwnPosition) {
	const std::vector<bool> live = ibCalcInForce({
		At(L"Ivanov/Salary/June", kJune),
		At(L"Petrov/Salary/June", kJune),
		At(L"Ivanov/Salary/June", kJuly, true),
	});
	EXPECT_FALSE(live[0]);
	EXPECT_TRUE(live[1]) << "another employee's record is not the storno's";
}

TEST(CalcStorno, AStornoWithNothingBeforeItReversesNothing) {
	const std::vector<bool> live = ibCalcInForce({
		At(L"p", kJuly, true),
		At(L"p", kJuly),   // same period: not a target
		At(L"p", kAugust),
	});
	EXPECT_FALSE(live[0]);
	EXPECT_TRUE(live[1]);
	EXPECT_TRUE(live[2]);
}

TEST(CalcStorno, AReversedRecordHasNoPiecesAndCutsNothing) {
	// A sick leave posted in June, then reversed in July (it was entered by mistake): the salary is whole
	// again, and neither the leave nor its storno has an actual action period.
	const std::vector<ibValue> ivanov{ ibValue(wxString(wxT("Ivanov"))) };
	std::vector<ibCalcWindowRecord> records(3);
	records[0].key = ivanov; records[0].type = kSalary;  records[0].kind = ibValue(wxString(wxT("Salary")));
	records[0].start = 0;    records[0].end = 30;        records[0].registration = kJune;
	records[1].key = ivanov; records[1].type = kAbsence; records[1].kind = ibValue(wxString(wxT("SickLeave")));
	records[1].start = 9;    records[1].end = 20;        records[1].registration = kJune;
	records[2] = records[1]; records[2].registration = kJuly; records[2].storno = true;

	const auto out = ibCalcPiecesInWindow(2, { E2{ kSalary, kAbsence } }, records, 0, 30);
	ASSERT_EQ(out[0].size(), 1u) << "the salary is not cut by a leave that stands reversed";
	EXPECT_EQ(out[0][0].start, 0); EXPECT_EQ(out[0][0].end, 30);
	EXPECT_TRUE(out[1].empty()) << "the reversed leave is out of force";
	EXPECT_TRUE(out[2].empty()) << "a storno has no pieces";
}
