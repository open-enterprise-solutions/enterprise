// Calculation engine — a schedule summed over a record's days (the kernel, ibScheduleSeries), and the description a
// calculation register binds its schedule by (ibCalcScheduleDescription). The schedule data itself — the records,
// their pieces and the schedule read from a base — is the kernel applied to rows; this holds the two halves it
// stands on.
#include <gtest/gtest.h>

#include "backend/calcScheduleDescription.h"
#include "backend/calculation/calculation.h"
#include "backend/serialize/dataBuilder.h"

// =============================================================================
// The running total
// =============================================================================

// A run of days is summed with both of its ends: a record in force from the 3rd to the 5th of an 8-hour week
// reads 24 hours.
TEST(CalcScheduleSeries, ARunIsSummedWithBothEnds)
{
	ibScheduleSeries hours(1);
	for (int64_t day = 1; day <= 10; ++day)
		hours.Add(day, 0, ibNumber(8));
	EXPECT_EQ(ibNumber(24), hours.Sum(3, 5, 0));
	EXPECT_EQ(ibNumber(8), hours.Sum(7, 7, 0));
	EXPECT_EQ(ibNumber(80), hours.Sum(1, 10, 0));
}

// A day the schedule carries no row for adds nothing: a weekend left out of the register reads as zero, and a run
// reaching past the schedule reads what falls inside it.
TEST(CalcScheduleSeries, DaysWithoutARowAddNothing)
{
	ibScheduleSeries hours(1);
	hours.Add(1, 0, ibNumber(1));
	hours.Add(5, 0, ibNumber(1));
	hours.Add(9, 0, ibNumber(1));
	EXPECT_EQ(ibNumber(0), hours.Sum(2, 4, 0));
	EXPECT_EQ(ibNumber(2), hours.Sum(5, 9, 0));
	EXPECT_EQ(ibNumber(3), hours.Sum(-100, 100, 0));
}

// Nothing to sum is zero, and never a failure: a run past the last day, a run that ends before it starts (a record
// with no days), a schedule with no rows at all, a figure the schedule does not carry.
TEST(CalcScheduleSeries, NothingToSumIsZero)
{
	ibScheduleSeries hours(1);
	hours.Add(1, 0, ibNumber(8));
	hours.Add(2, 0, ibNumber(8));
	EXPECT_EQ(ibNumber(0), hours.Sum(20, 30, 0));
	EXPECT_EQ(ibNumber(0), hours.Sum(2, 1, 0));
	EXPECT_EQ(ibNumber(0), hours.Sum(1, 2, 5));
	const ibScheduleSeries none;
	EXPECT_EQ(ibNumber(0), none.Sum(1, 2, 0));
}

// A day given again adds to itself — two rows of one date reach the reading one after the other (a date carrying a
// time, a dimension of the schedule nothing links).
TEST(CalcScheduleSeries, ADayGivenAgainAddsUp)
{
	ibScheduleSeries hours(1);
	hours.Add(4, 0, ibNumber(3));
	hours.Add(4, 0, ibNumber(5));
	hours.Add(5, 0, ibNumber(1));
	EXPECT_EQ(ibNumber(8), hours.Sum(4, 4, 0));
	EXPECT_EQ(ibNumber(9), hours.Sum(4, 5, 0));
	EXPECT_EQ(ibNumber(1), hours.Sum(5, 5, 0));
}

// Every figure of a day keeps its own total: the hours and the work days of one calendar, read over one run.
TEST(CalcScheduleSeries, EachFigureKeepsItsOwnTotal)
{
	ibScheduleSeries calendar(2);
	for (int64_t day = 0; day < 7; ++day) {
		const bool workDay = day < 5;
		calendar.Add(day, 0, ibNumber(workDay ? 8 : 0));
		calendar.Add(day, 1, ibNumber(workDay ? 1 : 0));
	}
	EXPECT_EQ(ibNumber(40), calendar.Sum(0, 6, 0));
	EXPECT_EQ(ibNumber(5), calendar.Sum(0, 6, 1));
	EXPECT_EQ(ibNumber(0), calendar.Sum(5, 6, 1));
}

// The actual action period is several pieces, and its figure is the sum of theirs — the same days read piece by
// piece as in one run.
TEST(CalcScheduleSeries, PiecesAddUpToTheirRun)
{
	ibScheduleSeries hours(1);
	for (int64_t day = 1; day <= 31; ++day)
		hours.Add(day, 0, ibNumber(day % 7 < 5 ? 8 : 0));
	EXPECT_EQ(hours.Sum(1, 31, 0), hours.Sum(1, 16, 0) + hours.Sum(17, 21, 0) + hours.Sum(22, 31, 0));
}

// =============================================================================
// The description
// =============================================================================

// The register chosen is what makes a schedule; the value and the date are parts of it.
TEST(CalcScheduleDescription, TheRegisterIsWhatMakesASchedule)
{
	ibCalcScheduleDescription schedule;
	EXPECT_FALSE(schedule.IsOk());
	schedule.SetSchedule(10, 0, 0);
	EXPECT_TRUE(schedule.IsOk());
	schedule.ClearSchedule();
	EXPECT_FALSE(schedule.IsOk());
	EXPECT_EQ(0, schedule.GetValue());
	EXPECT_EQ(0, schedule.GetDate());
}

// A link names a dimension OF the register it was made for: another register takes the links away, while the same
// register with another value or date keeps them.
TEST(CalcScheduleDescription, AnotherRegisterTakesTheLinksAway)
{
	ibCalcScheduleDescription schedule;
	schedule.SetSchedule(10, 11, 12);
	schedule.SetLink(20, 30);
	schedule.SetSchedule(10, 13, 12);
	EXPECT_EQ(30, schedule.GetLinkedField(20));
	schedule.SetSchedule(40, 11, 12);
	EXPECT_TRUE(schedule.GetLinks().empty());
	EXPECT_EQ(0, schedule.GetLinkedField(20));
}

// One link per dimension of the schedule: set again it is replaced, set to nothing it is gone.
TEST(CalcScheduleDescription, OneLinkPerDimension)
{
	ibCalcScheduleDescription schedule;
	schedule.SetSchedule(10, 11, 12);
	schedule.SetLink(20, 30);
	schedule.SetLink(20, 31);
	ASSERT_EQ(1u, schedule.GetLinks().size());
	EXPECT_EQ(31, schedule.GetLinkedField(20));
	schedule.SetLink(20, 0);
	EXPECT_TRUE(schedule.GetLinks().empty());
	schedule.SetLink(0, 31);   // a link to no dimension is no link
	EXPECT_TRUE(schedule.GetLinks().empty());
}

// Saved and read back, a schedule is the same schedule — its links in their order included.
TEST(CalcScheduleDescription, SurvivesItsNode)
{
	ibCalcScheduleDescription schedule;
	schedule.SetSchedule(10, 11, 12);
	schedule.SetLink(20, 30);
	schedule.SetLink(21, 31);

	ibDataValue node;
	ASSERT_TRUE(ibCalcScheduleDescriptionMemory::WriteNode(node, schedule));

	ibCalcScheduleDescription read;
	read.SetSchedule(99, 98, 97);   // whatever was there before is replaced, not merged
	read.SetLink(96, 95);
	ASSERT_TRUE(ibCalcScheduleDescriptionMemory::ReadNode(node, read));
	EXPECT_EQ(schedule, read);
}

// A register saved before it had a schedule carries no node for it — and reads as having none.
TEST(CalcScheduleDescription, NothingSavedIsNoSchedule)
{
	ibCalcScheduleDescription read;
	read.SetSchedule(10, 11, 12);
	read.SetLink(20, 30);
	ASSERT_TRUE(ibCalcScheduleDescriptionMemory::ReadNode(ibDataValue(), read));
	EXPECT_FALSE(read.IsOk());
	EXPECT_TRUE(read.GetLinks().empty());
}
