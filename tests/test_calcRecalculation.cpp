// Calculation engine — which already-computed records a change makes stale (kernel).
#include <gtest/gtest.h>
#include "backend/calculation/calculation.h"

#include <utility>
#include <vector>

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
