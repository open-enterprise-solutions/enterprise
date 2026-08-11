// =============================================================================
// OES Enterprise — the moment and the boundary
//
// Two values that exist so a reading can say WHERE it stops with the precision the domain actually
// uses. A date cannot separate three documents written the same day; a moment can. A moment cannot
// say whether the document it names is in or out; a boundary can.
//
// What these tests guard is an answer that is wrong by exactly one document — the kind that does
// not raise, does not look odd, and is discovered a quarter later during reconciliation. The
// ordering is the load-bearing part: the engine sorts, cuts and compares through
// ibValue::CompareValueLS, so ONE method decides the order in scripts, in RAM folds and in the
// tuple the SQL cut is built from. If it disagreed with itself anywhere, the server and the memory
// path would answer differently about the same two rows.
//
// ⚠ These values are REFERENCE-COUNTED. They are built with `new` and held through ibValuePtr —
// wrapping a stack object in an ibValue hands its address to the refcount, and the release at the
// end of scope frees a pointer the heap never owned.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/system/value/valuePointInTime.h"
#include "backend/system/value/valueBoundary.h"

namespace {

wxDateTime At(int day, int hour, int minute = 0)
{
	return wxDateTime(day, wxDateTime::Mar, 2026, hour, minute, 0);
}

using MomentPtr = ibValuePtr<ibValuePointInTime>;

MomentPtr Moment(const wxDateTime& when, const ibValue& record = ibValue())
{
	return MomentPtr(new ibValuePointInTime(when, record));
}

// The comparison as the engine performs it: one value against another, through the ordering
// primitive every road shares.
int Order(const MomentPtr& left, const MomentPtr& right)
{
	// The holder IS a value (ibValuePtr derives ibValue), so it is handed over as one.
	return left->CompareValueLS(*right);
}

}  // namespace

// --- the order ---------------------------------------------------------------------------------

TEST(PointInTime, TheDateDecidesFirst) {
	const MomentPtr early = Moment(At(5, 9));
	const MomentPtr late  = Moment(At(5, 17));

	EXPECT_LT(Order(early, late), 0);
	EXPECT_GT(Order(late, early), 0);
}

TEST(PointInTime, TheSameInstantWithNoRecordIsEqual) {
	EXPECT_EQ(0, Order(Moment(At(5, 12)), Moment(At(5, 12))));
}

// ⭐ A MOMENT WITH NO REFERENCE IS THE INSTANT ITSELF — it precedes everything recorded in it.
// That reading is what makes a bare date usable as an OPENING boundary: no record of that instant
// may sort below it, or "from this instant onward" would silently drop the day's first document.
TEST(PointInTime, AnInstantPrecedesWhatIsRecordedInIt) {
	const MomentPtr instant  = Moment(At(5, 12));
	// A reference value is not needed to prove the rule: the same date with SOMETHING in the record
	// slot must sort after the bare instant. The comparison asks whether the slot is empty before it
	// asks what is in it.
	const MomentPtr recorded = Moment(At(5, 12), ibValue(42));

	EXPECT_LT(Order(instant, recorded), 0);
	EXPECT_GT(Order(recorded, instant), 0);
}

// A moment with no date at all is the smallest thing there is — the same rule SQL gives NULL, so a
// value that was never set cannot silently land in the middle of an ordering.
TEST(PointInTime, AnUnsetMomentIsTheSmallest) {
	const MomentPtr nothing   = MomentPtr(new ibValuePointInTime());
	const MomentPtr something = Moment(At(5, 12));

	EXPECT_TRUE(nothing->IsEmpty());
	EXPECT_LT(Order(nothing, something), 0);
	EXPECT_GT(Order(something, nothing), 0);
}

// ⭐ A DATE IS A MOMENT WHOSE REFERENCE IS NOT SET, so the two orders are ONE order. If they were
// two, `Balance(date)` and `Balance(moment)` could disagree about the same row.
TEST(PointInTime, ComparesWithAPlainDateByTheDateAlone) {
	const MomentPtr moment = Moment(At(5, 12));

	EXPECT_LT(moment->CompareValueLS(ibValue(At(5, 18))), 0);
	EXPECT_GT(moment->CompareValueLS(ibValue(At(5, 6))), 0);
	EXPECT_EQ(0, moment->CompareValueLS(ibValue(At(5, 12))));
}

// EQUALITY stays type-strict even though ORDER does not: a date is ordered WITH a moment, and is
// not one. Mixing the two would make a moment equal to a date it merely sorts alongside.
TEST(PointInTime, EqualityIsTypeStrict) {
	const MomentPtr moment = Moment(At(5, 12));
	EXPECT_FALSE(moment->CompareValueEQ(ibValue(At(5, 12))));

	const MomentPtr twin = Moment(At(5, 12));
	EXPECT_TRUE(moment->CompareValueEQ(*twin));
}

// --- construction ------------------------------------------------------------------------------

TEST(PointInTime, TakesADateAndOptionallyAReference) {
	ibValue date(At(7, 10));
	ibValue* one[] = { &date };

	const MomentPtr built = MomentPtr(new ibValuePointInTime());
	EXPECT_TRUE(built->Init(one, 1));
	EXPECT_EQ(At(7, 10), built->m_date);
	EXPECT_TRUE(built->m_reference.IsEmpty());
}

// A moment with no date is not a moment — the constructor refuses rather than inventing one.
TEST(PointInTime, RefusesWithoutADate) {
	ibValue notADate(wxT("yesterday"));
	ibValue* one[] = { &notADate };

	const MomentPtr built = MomentPtr(new ibValuePointInTime());
	EXPECT_FALSE(built->Init(one, 1));
}

// --- the boundary ------------------------------------------------------------------------------

// Including is the default because it is what a bare date has always meant: wrapping a value
// without saying more must change nothing.
TEST(Boundary, DefaultsToIncluding) {
	ibValue date(At(5, 0));
	ibValue* one[] = { &date };

	const ibValuePtr<ibValueBoundary> bound(new ibValueBoundary());
	EXPECT_TRUE(bound->Init(one, 1));
	EXPECT_EQ(ibBoundaryKind_Including, bound->m_kind);
	EXPECT_FALSE(bound->IsEmpty());
}

TEST(Boundary, CarriesTheSideItWasGiven) {
	ibValue date(At(5, 0));
	ibValue kind(static_cast<int>(ibBoundaryKind_Excluding));
	ibValue* two[] = { &date, &kind };

	const ibValuePtr<ibValueBoundary> bound(new ibValueBoundary());
	EXPECT_TRUE(bound->Init(two, 2));
	EXPECT_EQ(ibBoundaryKind_Excluding, bound->m_kind);
}

// ⭐ A BOUNDARY WRAPS A POSITION — it does not replace one. Its value is whatever it was handed,
// unchanged, so everything downstream reads the position exactly as it would a bare one.
TEST(Boundary, KeepsThePositionItWraps) {
	const MomentPtr moment = Moment(At(5, 12));
	ibValue* one[] = { &*moment };

	const ibValuePtr<ibValueBoundary> bound(new ibValueBoundary());
	ASSERT_TRUE(bound->Init(one, 1));

	ibValuePointInTime* inside = nullptr;
	ASSERT_TRUE(bound->m_value.ConvertToValue(inside));
	ASSERT_NE(nullptr, inside);
	EXPECT_EQ(At(5, 12), inside->m_date);
}
