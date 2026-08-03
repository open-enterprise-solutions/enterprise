////////////////////////////////////////////////////////////////////////////
//	Description : ibJobScheduleDescription — the calendar half of a job's timing.
//
// Everything here is pure: a schedule plus a moment in, an answer out. No
// database, no session, no manager — which is exactly why these rules are worth
// pinning down here rather than by watching a job at 02:00.
//
// The two questions the schedule answers (see jobSchedule.h): does the CALENDAR
// allow this moment (IsAllowed), and what is the first allowed moment at or
// after a given one (NextAllowedAfter). The manager composes them with the
// interval to get a due moment — that composition is tested through the pieces.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/job/jobSchedule.h"
#include "backend/serialize/dataBuilder.h"

namespace {

// A local moment, spelled the way a schedule reads it.
wxDateTime At(int year, wxDateTime::Month month, int day, int hour = 0, int minute = 0)
{
	return wxDateTime(day, month, year, hour, minute, 0);
}

} // namespace

// ---------------------------------------------------------------------------
// Time-of-day window
// ---------------------------------------------------------------------------

TEST(JobSchedule, Window_AllowsInsideAndRejectsOutside)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 5);   // 02:00–05:00

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 2, 0)));    // start is inclusive
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 4, 59)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 5, 0)));    // end is exclusive
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 1, 59)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 14, 0)));
}

TEST(JobSchedule, Window_WrapsMidnight)
{
	// 22:00–05:00 — read as a plain range this matches nothing; as a night it is
	// the ordinary case, so the wrap is the meaning.
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(24 * 3600);
	s.m_startMinute = ibJobScheduleDescription::AtTime(22);
	s.m_endMinute   = ibJobScheduleDescription::AtTime(5);

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 23, 30)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 4,  1,  0)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 4, 12,  0)));
}

// ---------------------------------------------------------------------------
// Stop-after — too late to BEGIN, even though the window still allows it
// ---------------------------------------------------------------------------

TEST(JobSchedule, StopAfter_GatesTheStartOnly)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 6);
	s.m_stopAfterMinute = ibJobScheduleDescription::AtTime(4);   // do not START after 04:00

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 3, 59)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 4, 0)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 5, 30)));   // still inside the window
}

TEST(JobSchedule, StopAfter_BeforeWindowStartIsRefusedAtValidation)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 6);
	s.m_stopAfterMinute = ibJobScheduleDescription::AtTime(1);   // would gate the job forever
	EXPECT_FALSE(s.IsValid());
}

// ---------------------------------------------------------------------------
// Day of month, counted from both ends
// ---------------------------------------------------------------------------

TEST(JobSchedule, DayOfMonth_FromStart)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(24 * 3600);
	s.m_daysOfMonth = (1u << 0) | (1u << 14);   // the 1st and the 15th

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug,  1)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 15)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug,  2)));
}

TEST(JobSchedule, DayOfMonth_FromEnd_KnowsMonthLength)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(24 * 3600);
	s.m_daysOfMonthFromEnd = (1u << 0);   // the LAST day, whatever it is

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 31)));   // 31-day month
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 30)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Sep, 30)));   // 30-day month
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Feb, 28)));   // 2026 is not a leap year
}

TEST(JobSchedule, DayOfMonth_BothEndsAreOrEd)
{
	// "The 1st and the last day" — the case a single field with a direction flag
	// cannot express, which is why there are two masks.
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(24 * 3600);
	s.m_daysOfMonth        = (1u << 0);
	s.m_daysOfMonthFromEnd = (1u << 0);

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug,  1)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 31)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 10)));
}

// ---------------------------------------------------------------------------
// Nth weekday of the month
// ---------------------------------------------------------------------------

TEST(JobSchedule, WeekdayOrdinal_SecondTuesday)
{
	// August 2026: Tuesdays fall on the 4th, 11th, 18th and 25th.
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(7 * 24 * 3600);
	s.m_daysOfWeek     = ibJobWeekDay_Tuesday;
	s.m_weekdayOrdinal = ibJobOrdinal_Second;

	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug,  4)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 11)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 18)));
}

TEST(JobSchedule, WeekdayOrdinal_LastFriday)
{
	// August 2026: Fridays on the 7th, 14th, 21st and 28th — the 28th is the last.
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(7 * 24 * 3600);
	s.m_daysOfWeek     = ibJobWeekDay_Friday;
	s.m_weekdayOrdinal = ibJobOrdinal_Last;

	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 21)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 28)));
}

TEST(JobSchedule, WeekdayOrdinal_NeedsExactlyOneWeekday)
{
	// An ordinal counts occurrences of ONE weekday; with several named there is
	// nothing to count, and the schedule is refused rather than silently odd.
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(7 * 24 * 3600);
	s.m_daysOfWeek     = ibJobWeekDay_Tuesday | ibJobWeekDay_Friday;
	s.m_weekdayOrdinal = ibJobOrdinal_Second;
	EXPECT_FALSE(s.IsValid());
}

// ---------------------------------------------------------------------------
// "Every N" — the phase belongs to the calendar, not to our history
// ---------------------------------------------------------------------------

TEST(JobSchedule, EveryNWeeks_CountsFromTheAnchor)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(7 * 24 * 3600);
	s.m_daysOfWeek   = ibJobWeekDay_Monday;
	s.m_everyNWeeks  = 2;
	s.m_periodAnchor = At(2026, wxDateTime::Aug, 3);   // a Monday

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug,  3)));   // anchor week
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 10)));   // the week between
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 17)));   // two weeks on
}

TEST(JobSchedule, EveryNMonths_CountsCalendarMonths)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(30 * 24 * 3600);
	s.m_daysOfMonth  = (1u << 0);   // the 1st
	s.m_everyNMonths = 3;           // quarterly
	s.m_periodAnchor = At(2026, wxDateTime::Jan, 1);

	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Jan, 1)));
	EXPECT_FALSE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Feb, 1)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Apr, 1)));
	EXPECT_TRUE (ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Jul, 1)));
}

// ---------------------------------------------------------------------------
// NextAllowedAfter — the moment a due time is computed from
// ---------------------------------------------------------------------------

TEST(JobSchedule, NextAllowedAfter_ReturnsTheMomentItselfWhenAlreadyAllowed)
{
	// "Not before", never "only at": a moment that already qualifies IS the next
	// allowed one. This is what lets a missed night run late instead of vanishing.
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 5);
	const wxDateTime inside = At(2026, wxDateTime::Aug, 3, 3, 0);
	EXPECT_EQ(ibJobScheduleRules::NextAllowedAfter(s, inside), inside);
}

TEST(JobSchedule, NextAllowedAfter_KeepsSecondsWhenTheCalendarAllows)
{
	// SUB-MINUTE intervals. An allowed moment comes back untouched — seconds included — because
	// "at or after" means exactly that. Rounding up to the next whole minute (which the search does
	// once the calendar refuses) turned "every 4 seconds" into one run a minute, on the :00.
	ibJobScheduleDescription s = ibJobScheduleDescription::EverySeconds(4);
	wxDateTime moment = At(2026, wxDateTime::Aug, 3, 10, 0);
	moment.SetSecond(37);
	EXPECT_EQ(ibJobScheduleRules::NextAllowedAfter(s, moment), moment);
}

TEST(JobSchedule, NextAllowedAfter_StillRoundsToTheMinuteWhenItHasToSearch)
{
	// The other half of the same rule: a REFUSED moment starts the walk at the next whole minute,
	// because minutes are all the calendar's own fields can name.
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 5);
	wxDateTime outside = At(2026, wxDateTime::Aug, 3, 9, 0);
	outside.SetSecond(37);
	EXPECT_EQ(ibJobScheduleRules::NextAllowedAfter(s, outside), At(2026, wxDateTime::Aug, 4, 2, 0));
}

TEST(JobSchedule, NextAllowedAfter_MovesForwardToTheWindow)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 5);
	const wxDateTime next = ibJobScheduleRules::NextAllowedAfter(s, At(2026, wxDateTime::Aug, 3, 9, 0));
	ASSERT_TRUE(next.IsValid());
	EXPECT_EQ(next, At(2026, wxDateTime::Aug, 4, 2, 0));   // the following night
}

TEST(JobSchedule, NextAllowedAfter_HonoursTheWeekdayMask)
{
	ibJobScheduleDescription s = ibJobScheduleDescription::Nightly(2, 5);
	s.m_daysOfWeek = ibJobWeekDay_Monday;

	// From Tuesday morning the next allowed moment is the following Monday 02:00.
	const wxDateTime next = ibJobScheduleRules::NextAllowedAfter(s, At(2026, wxDateTime::Aug, 4, 9, 0));
	ASSERT_TRUE(next.IsValid());
	EXPECT_EQ(next, At(2026, wxDateTime::Aug, 10, 2, 0));
}

// ---------------------------------------------------------------------------
// Storage — a round trip must not change the meaning
// ---------------------------------------------------------------------------

TEST(JobSchedule, Serialization_RoundTripsEveryField)
{
	ibJobScheduleDescription src = ibJobScheduleDescription::Nightly(2, 5);
	src.m_intervalSeconds      = 7 * 24 * 3600;
	src.m_stopAfterMinute      = ibJobScheduleDescription::AtTime(4, 30);
	src.m_daysOfWeek           = ibJobWeekDay_Friday;
	src.m_daysOfMonth          = (1u << 0) | (1u << 14);
	src.m_daysOfMonthFromEnd   = (1u << 0);
	src.m_months               = (1u << 0) | (1u << 6);
	src.m_weekdayOrdinal       = ibJobOrdinal_Last;
	src.m_everyNWeeks          = 2;
	src.m_everyNMonths         = 3;
	src.m_periodAnchor         = At(2026, wxDateTime::Jan, 5, 12, 0);
	src.m_activeFrom           = At(2026, wxDateTime::Jan, 1);
	src.m_activeTo             = At(2027, wxDateTime::Jan, 1);

	ibDataValue value;
	ASSERT_TRUE(ibJobScheduleDescriptionMemory::WriteNode(value, src));

	ibJobScheduleDescription dst;
	ASSERT_TRUE(ibJobScheduleDescriptionMemory::ReadNode(value, dst));
	EXPECT_TRUE(dst == src);   // value semantics: the whole point of a round trip

	EXPECT_EQ(dst.m_intervalSeconds,    src.m_intervalSeconds);
	EXPECT_EQ(dst.m_startMinute,        src.m_startMinute);
	EXPECT_EQ(dst.m_endMinute,          src.m_endMinute);
	EXPECT_EQ(dst.m_stopAfterMinute,    src.m_stopAfterMinute);
	EXPECT_EQ(dst.m_daysOfWeek,         src.m_daysOfWeek);
	EXPECT_EQ(dst.m_daysOfMonth,        src.m_daysOfMonth);
	EXPECT_EQ(dst.m_daysOfMonthFromEnd, src.m_daysOfMonthFromEnd);
	EXPECT_EQ(dst.m_months,             src.m_months);
	EXPECT_EQ(dst.m_weekdayOrdinal,     src.m_weekdayOrdinal);
	EXPECT_EQ(dst.m_everyNWeeks,        src.m_everyNWeeks);
	EXPECT_EQ(dst.m_everyNMonths,       src.m_everyNMonths);
	EXPECT_EQ(dst.m_periodAnchor,       src.m_periodAnchor);
	EXPECT_EQ(dst.m_activeFrom,         src.m_activeFrom);
	EXPECT_EQ(dst.m_activeTo,           src.m_activeTo);
}

TEST(JobSchedule, Serialization_AbsentFieldsKeepTheirDefaults)
{
	// FORWARD COMPATIBILITY, stated as a test: a blob written before a field
	// existed must read back as "not restricted" — never as "restricted to
	// nothing", which would be a job that silently never runs.
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("IntervalSeconds"), (s32)3600);   // an old blob knowing one field
	const ibDataValue value = ibDataValue::Child(root);

	ibJobScheduleDescription s;
	ASSERT_TRUE(ibJobScheduleDescriptionMemory::ReadNode(value, s));

	EXPECT_EQ(s.m_intervalSeconds,      3600);
	EXPECT_EQ(s.m_startMinute,          -1);
	EXPECT_EQ(s.m_endMinute,            -1);
	EXPECT_EQ(s.m_stopAfterMinute,      -1);
	EXPECT_EQ(s.m_daysOfWeek,           ibJobWeekDay_Any);
	EXPECT_EQ(s.m_daysOfMonth,          0u);
	EXPECT_EQ(s.m_daysOfMonthFromEnd,   0u);
	EXPECT_EQ(s.m_weekdayOrdinal,       ibJobOrdinal_None);
	EXPECT_TRUE(ibJobScheduleRules::IsAllowed(s, At(2026, wxDateTime::Aug, 3, 14, 0)));   // "any" really means any
}
