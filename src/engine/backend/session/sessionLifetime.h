#ifndef __IB_SESSION_LIFETIME_H__
#define __IB_SESSION_LIFETIME_H__

// How long a person's session may live. The registry's sweep is the one
// place that applies these numbers, to every client it owns. The clock is
// UTC milliseconds (system_clock), so a wall-clock change does not age a
// live session by the hour the zone skipped.
//
//   OES_SESSION_IDLE_MINUTES   default 30. No request for this long ends the session.
//   OES_SESSION_ABSOLUTE_HOURS default 8.  From authentication (or from birth, when
//                              the session never authenticated), even if it keeps asking.
//
// An empty, zero, or non-numeric value leaves the default.

#include "core/fdatetime.h"

struct ibSessionLifetime {
	long long m_idleMs     = 30LL * 60LL * 1000LL;
	long long m_absoluteMs = 8LL * 60LL * 60LL * 1000LL;
};

enum class ibSessionEnd {
	Keep,
	Idle,
	Absolute,
};

// The process's current numbers.
ibSessionLifetime ibSessionLifetimePolicy();

// Unix milliseconds of a wall reading, in this machine's zone. 0 when the
// reading is empty or cannot be placed. A password stamp is stored as a
// wall reading; the login's instant is already unix milliseconds.
long long ibWallUnixMs(const ibDateTime& reading);

// All four instants are unix milliseconds. `authenticatedAtMs` 0 means the
// session has not logged in, and the absolute limit is then counted from
// `bornMs`. `lastActiveMs` 0 is not idle.
ibSessionEnd ibJudgeSessionLifetime(long long nowMs,
	long long bornMs,
	long long authenticatedAtMs,
	long long lastActiveMs,
	const ibSessionLifetime& policy);

// True when this login is still the one the password belongs to.
// An empty `passwordChangedAt` is a row written before the stamp existed.
bool ibSessionPasswordStillCurrent(long long authenticatedAtMs,
	const ibDateTime& passwordChangedAt);

#endif
