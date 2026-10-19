#ifndef __IB_JOB_SCHEDULE_H__
#define __IB_JOB_SCHEDULE_H__

// WHAT A SCHEDULE MEANS — read by every side: the server's manager asks when a job is due, an editor (the desktop's,
// the client's) reads its sentence and whether it is well formed as it is typed.
//
// The description itself — the fields and how they are written down — is its schema
// (core/description/jobScheduleDescription.h): a schema keeps structure and decides nothing (Max, 2026-10-12).
// Reading those fields as "may this job run now", "when next", "what does it say" and "is it well formed at all"
// is here: everything a pure function of (schedule, moment), with no manager, no session and no clock of its own.

#include "core/core.h"                                // CORE_API
#include "core/description/jobScheduleDescription.h"  // ibJobScheduleDescription — the data these rules read

////////////////////////////////////////////////////////////////////////////
// THE RULES — what a schedule MEANS, kept away from what it IS.
//
// The description is data: fields a form edits and a serialiser writes down. Reading those
// fields as "may this job run now" is a decision, and decisions belong with whoever makes them —
// the manager, which composes these answers with the last run to get a due moment (ibJobManager::
// IsDue). Static and pure, so they stay testable without standing a manager up: a schedule and a
// moment in, an answer out.
////////////////////////////////////////////////////////////////////////////
class CORE_API ibJobScheduleRules {
public:
	// Is the description WELL FORMED? A non-positive interval, a half-declared window, an ordinal
	// with no single weekday to count. A statement about the fields themselves, the kind an editor
	// makes while the user types.
	static bool IsValid(const ibJobScheduleDescription& schedule);

	// Window test: [start, end) in minutes, wrapping when start > end. Either bound negative means
	// "no window", always inside.
	static bool IsInsideWindow(int startMinute, int endMinute, int nowMinute);

	// Does the calendar allow `moment` (a wall-clock reading)? Windows, weekdays, month days from either
	// end, the Nth weekday, the every-N phase and the validity range — all of it, one answer.
	static bool IsAllowed(const ibJobScheduleDescription& schedule, const ibDateTime& moment);

	// The first moment the calendar allows AT OR AFTER `notBefore`. At or after, not strictly after:
	// a moment that already qualifies IS the answer, which is what makes a time of day read as "not
	// before" and lets a missed night run late instead of vanishing. The empty date when nothing
	// matches within a year (a schedule naming, say, February 30th).
	//
	// Derived on demand and never stored: a "next run" kept in a field is one more thing that can
	// disagree with reality after a restart or a clock change.
	static ibDateTime NextAllowedAfter(const ibJobScheduleDescription& schedule, const ibDateTime& notBefore);

	// A human sentence — "Every 10 minutes, 10:00-15:00, Tue, in March". Localised, built from the
	// same fields an editor shows, so a settings list and a log line say the same thing without
	// either restating the rules. Omits everything left at "any", because naming defaults is how a
	// description stops being read.
	static wxString Describe(const ibJobScheduleDescription& schedule);
};

#endif // !__IB_JOB_SCHEDULE_H__
