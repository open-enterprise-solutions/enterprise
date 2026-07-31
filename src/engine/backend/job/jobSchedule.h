#ifndef __IB_JOB_SCHEDULE_H__
#define __IB_JOB_SCHEDULE_H__

// ibJobSchedule — WHEN a job is due.
//
// Split out of ibJobDescription because it is the part a user edits and the part
// that is worth testing on its own: everything here is a pure function of (last
// run, now), with no manager, no session and no clock of its own.
//
// TWO INDEPENDENT QUESTIONS, and keeping them separate is what makes the common
// cases expressible without a cron grammar:
//
//   1. Does the CALENDAR allow this moment? — day of week, day of month, month,
//      time of day, and an optional validity range. All defaults mean "any".
//   2. Has enough time passed since the last run? — m_intervalSeconds.
//
// A job is due when BOTH say yes. That composition is what "every Monday at
// 02:00" is made of: allow Mondays 02:00–05:00, repeat no more than once a day.
// Without the interval the job would re-run on every tick for three hours;
// without the calendar it would run at any hour.
//
//   every 10 minutes             interval 600
//   nightly, off-hours           interval 24h + window 02:00–05:00
//   Mondays only, off-hours      the above + daysOfWeek = Monday
//   1st of the month, at night   interval 24h + window + daysOfMonth = {1}
//   every 5 min during business  interval 300 + window 09:00–18:00 + Mon–Fri
//
// WHAT THIS DELIBERATELY IS NOT: a cron expression. A schedule that has to be
// parsed cannot be shown in an editor without also being unparsed, and every
// field here is a control a designer can render. If a case ever genuinely needs
// cron, it is a second kind of schedule, not a rewrite of this one.
//
// Clocks: the calendar reads local wall-clock time (a user says "02:00" and
// means their 02:00). The interval is measured on a STEADY clock by the manager,
// so moving the system clock cannot make a job fire twice or stall for hours —
// see ibJobEntry::m_lastRun.

#include "backend/backend.h"

#include <cstdint>

#include <wx/datetime.h>

// Bit per weekday, Monday = bit 0 (matching how a week is written, not how
// wxDateTime numbers it — the conversion lives in the .cpp).
enum ibJobWeekDay : std::uint8_t {
	ibJobWeekDay_Monday    = 1 << 0,
	ibJobWeekDay_Tuesday   = 1 << 1,
	ibJobWeekDay_Wednesday = 1 << 2,
	ibJobWeekDay_Thursday  = 1 << 3,
	ibJobWeekDay_Friday    = 1 << 4,
	ibJobWeekDay_Saturday  = 1 << 5,
	ibJobWeekDay_Sunday    = 1 << 6,

	ibJobWeekDay_Weekdays  = 0x1F,   // Mon–Fri
	ibJobWeekDay_Weekend   = 0x60,   // Sat–Sun
	ibJobWeekDay_Any       = 0x7F,
};

struct BACKEND_API ibJobSchedule {

	// ---- repetition ------------------------------------------------------
	// Minimum seconds between two runs. Must be positive: a job with no
	// interval would re-run on every tick for as long as its calendar allows,
	// which is never what anyone means.
	int      m_intervalSeconds = 3600;

	// ---- calendar (all defaults = "any") ---------------------------------
	// Time-of-day window [start, end), in MINUTES from midnight, so 02:30 is
	// expressible. -1 on either disables the window. A window whose start is
	// greater than its end WRAPS midnight (22:00–05:00), which is what a night
	// window means; read as a plain range it would match nothing at all.
	int      m_startMinute = -1;
	int      m_endMinute   = -1;

	// Days of the week, ibJobWeekDay bits. 0 or Any = every day.
	std::uint8_t  m_daysOfWeek = ibJobWeekDay_Any;

	// Days of the month, bit 0 = the 1st … bit 30 = the 31st. 0 = every day.
	// A month shorter than a requested day simply has no such day that month —
	// "the 31st" runs seven times a year, which is the honest reading.
	std::uint32_t m_daysOfMonth = 0;

	// Months, bit 0 = January … bit 11 = December. 0 = every month.
	std::uint16_t m_months = 0;

	// Validity range — the job does not run before / after these. Invalid
	// (default-constructed) means unbounded. Used for a job that should start
	// next quarter, or stop after a migration window closes.
	wxDateTime m_activeFrom;
	wxDateTime m_activeTo;

	// ---- the two questions ----------------------------------------------

	// Does the calendar allow `moment` (local time)? Pure — pass the moment in
	// so this is testable without waiting for Tuesday.
	bool IsAllowed(const wxDateTime& moment) const;

	// Is the schedule usable at all? A non-positive interval, an out-of-range
	// window or an empty day mask would make a job that can never run — worth
	// refusing at registration rather than debugging as silence.
	bool IsValid() const;

	// A human sentence for this schedule — "Every 10 minutes, 10:00-15:00, Tue,
	// in March". Localised, built from the same fields an editor shows, so a
	// settings list and a log line say the same thing without either restating
	// the rules. Omits everything left at "any", because naming defaults is how
	// a description stops being read.
	wxString ToString() const;

	// The next moment this schedule allows, at or after `notBefore` — the
	// CALENDAR's answer, so it accounts for windows, weekdays, month days and
	// months, not just the interval. Invalid when nothing matches within a year
	// (a schedule naming, say, February 30th).
	//
	// Derived on demand and never stored: a "next run" kept in a field is one
	// more thing that can disagree with reality after a restart or a clock change.
	wxDateTime NextAllowedAfter(const wxDateTime& notBefore) const;

	// Convenience for the common shapes, so a caller does not assemble bits.
	static ibJobSchedule EverySeconds(int seconds);
	static ibJobSchedule Nightly(int startHour, int endHour);

	// Minutes-from-midnight helper, so call sites read as clock time.
	static int AtTime(int hour, int minute = 0) { return hour * 60 + minute; }

	// Window test, exposed for tests: [start, end) in minutes, wrapping when
	// start > end. Either bound negative means "no window", always inside.
	static bool IsInsideWindow(int startMinute, int endMinute, int nowMinute);
};

#endif // !__IB_JOB_SCHEDULE_H__
