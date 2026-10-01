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
