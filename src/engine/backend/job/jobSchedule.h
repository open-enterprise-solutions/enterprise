#ifndef __IB_JOB_SCHEDULE_H__
#define __IB_JOB_SCHEDULE_H__

// ibJobScheduleDescription — WHEN a job is due.
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

#include <wx/buffer.h>
#include <wx/datetime.h>

class ibDataValue;   // storage door — see ibJobScheduleDescriptionMemory at the bottom

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

// Which occurrence of a weekday inside its month — "the second Tuesday", "the last Friday". Counted
// from the start except for Last, which is counted from the end; a month has four or five of any
// given weekday, so "the fifth" simply does not occur in most months, the same honest reading the
// day-of-month mask gives "the 31st". 0 = the ordinal is not used at all.
enum ibJobOrdinal : std::uint8_t {
	ibJobOrdinal_None   = 0,
	ibJobOrdinal_First  = 1,
	ibJobOrdinal_Second = 2,
	ibJobOrdinal_Third  = 3,
	ibJobOrdinal_Fourth = 4,
	ibJobOrdinal_Fifth  = 5,
	ibJobOrdinal_Last   = 255,
};

struct BACKEND_API ibJobScheduleDescription {

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

	// ---- counted from the END of the month -------------------------------
	// The same day-of-month idea read backwards: bit 0 = the LAST day, bit 1 = the day before it.
	// A separate mask rather than a direction flag on the one above, because "the 1st and the last"
	// is an ordinary wish and a single field with a direction cannot say it. Zero = not used; the
	// two masks are OR-ed, so naming both means either matches.
	std::uint32_t m_daysOfMonthFromEnd = 0;

	// ---- the Nth weekday of the month ------------------------------------
	// "The second Tuesday", "the last Friday". m_weekdayOrdinal counts occurrences of the weekday
	// WITHIN the month: 1..5 from the start, or ibJobOrdinal_Last for the final one; 0 = not used.
	// Which weekday is m_daysOfWeek — it must then name exactly one day, or the ordinal has nothing
	// to count. Direction is carried by the value itself, so there is no second field to contradict.
	std::uint8_t m_weekdayOrdinal = 0;

	// ---- how OFTEN, when the masks say WHICH ------------------------------
	// A mask answers "which days", never "how often": every Monday and every other Monday have the
	// same mask. These are the period, in whole weeks / months, counted from m_periodAnchor (or from
	// m_activeFrom when the anchor is unset, or the epoch when neither is). 0 or 1 = every one.
	//
	// Counted from a FIXED anchor rather than from the last run on purpose: a run that happens late
	// must not shift every later run with it, which is exactly what "count from last time" does.
	std::uint16_t m_everyNWeeks  = 0;
	std::uint16_t m_everyNMonths = 0;
	wxDateTime    m_periodAnchor;

	// ---- stop after ------------------------------------------------------
	// Minutes from midnight after which a pass must not START, even though the window still allows
	// it. Distinct from m_endMinute: the window says when work may happen, this says when it is too
	// late to begin something that will run long. -1 = not used.
	//
	// It gates the START only. Cutting a pass off mid-flight is the cancel token's job, and belongs
	// to whoever knows what half-done means for that work — a backup does not survive being halved.
	int m_stopAfterMinute = -1;

	// ---- the shape, not the meaning --------------------------------------

	// Is this description WELL FORMED? A non-positive interval, a half-declared window, an ordinal
	// with no single weekday to count. Deliberately the only judgement kept here: it is a statement
	// about the fields themselves, the kind an editor makes while the user types. WHEN the job runs
	// is not a property of the data — see ibJobScheduleRules.
	bool IsValid() const;

	// Value semantics — two schedules are the same when every field is, which is what an editor
	// needs to know whether the user actually changed anything.
	bool operator==(const ibJobScheduleDescription& other) const;
	bool operator!=(const ibJobScheduleDescription& other) const { return !(*this == other); }

	// Convenience for the common shapes, so a caller does not assemble bits.
	static ibJobScheduleDescription EverySeconds(int seconds);
	static ibJobScheduleDescription Nightly(int startHour, int endHour);

	// Minutes-from-midnight helper, so call sites read as clock time.
	static int AtTime(int hour, int minute = 0) { return hour * 60 + minute; }

	// Window test, exposed for tests: [start, end) in minutes, wrapping when
	// start > end. Either bound negative means "no window", always inside.
	static bool IsInsideWindow(int startMinute, int endMinute, int nowMinute);
};

////////////////////////////////////////////////////////////////////////////
// THE RULES — what a schedule MEANS, kept away from what it IS.
//
// The description above is data: fields a form edits and a serialiser writes down. Reading those
// fields as "may this job run now" is a decision, and decisions belong with whoever makes them —
// the manager, which composes these answers with the last run to get a due moment (ibJobManager::
// IsDue). Static and pure, so they stay testable without standing a manager up: a schedule and a
// moment in, an answer out.
////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibJobScheduleRules {
public:
	// Does the calendar allow `moment` (local time)? Windows, weekdays, month days from either end,
	// the Nth weekday, the every-N phase and the validity range — all of it, one answer.
	static bool IsAllowed(const ibJobScheduleDescription& schedule, const wxDateTime& moment);

	// The first moment the calendar allows AT OR AFTER `notBefore`. At or after, not strictly after:
	// a moment that already qualifies IS the answer, which is what makes a time of day read as "not
	// before" and lets a missed night run late instead of vanishing. Invalid when nothing matches
	// within a year (a schedule naming, say, February 30th).
	//
	// Derived on demand and never stored: a "next run" kept in a field is one more thing that can
	// disagree with reality after a restart or a clock change.
	static wxDateTime NextAllowedAfter(const ibJobScheduleDescription& schedule, const wxDateTime& notBefore);

	// A human sentence — "Every 10 minutes, 10:00-15:00, Tue, in March". Localised, built from the
	// same fields an editor shows, so a settings list and a log line say the same thing without
	// either restating the rules. Omits everything left at "any", because naming defaults is how a
	// description stops being read.
	static wxString Describe(const ibJobScheduleDescription& schedule);
};

////////////////////////////////////////////////////////////////////////////
// The serialiser, split from the data the way every other description in the tree is split from
// its storage (see docs/descriptions.md): the struct above says what a schedule IS, this says how
// it is written down. Keeping them apart is what lets the struct stay a plain value — copyable,
// comparable, usable in a form's state — while the node shape evolves on its own.
//
// NODE SHAPE — one child node, fields addressed BY NAME:
//   IntervalSeconds, StartMinute, EndMinute, StopAfterMinute : s32 (minutes from midnight; -1 = unset)
//   DaysOfWeek, DaysOfMonth, DaysOfMonthFromEnd, Months      : s32 bit masks (0 = any)
//   WeekdayOrdinal, EveryNWeeks, EveryNMonths                : s32 (0 = unused)
//   PeriodAnchor, ActiveFrom, ActiveTo                       : Date (0 = invalid / unbounded)
//
// Names rather than positions, so adding or dropping a field keeps old data readable: an absent
// name leaves the member at its default — and every default here means "not restricted", never
// "restricted to nothing", which would silently produce a job that can never run.
////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibJobScheduleDescriptionMemory {
public:
	static bool ReadNode(const ibDataValue& value, ibJobScheduleDescription& schedule);
	static bool WriteNode(ibDataValue& value, const ibJobScheduleDescription& schedule);

	// THE COLUMN form of the same schedule — a flat little-endian blob, because a schedule that
	// lives in a ROW (a parameterized job's own Schedule requisite) is written by the column codec,
	// which binds a blob, not a node tree. Same door, second spelling: the node form stays the
	// metadata format, this is the data one, and both are here so neither can be written twice.
	//
	// Layout: version u8, then the fields in declaration order (ints as s32, dates as s64 ms since
	// the wxDateTime epoch; 0 = invalid / unbounded). VERSIONED because a row outlives a release:
	// a reader older than the blob stops at the fields it knows, and every field it did not read
	// keeps its default — which here always means "not restricted".
	static void WriteBuffer(wxMemoryBuffer& out, const ibJobScheduleDescription& schedule);
	static bool ReadBuffer(const void* data, size_t length, ibJobScheduleDescription& schedule);
};

#endif // !__IB_JOB_SCHEDULE_H__
