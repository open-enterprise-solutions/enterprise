#ifndef __IB_SESSION_LIFETIME_H__
#define __IB_SESSION_LIFETIME_H__

// How long a session may live. ASVS 7.1.1: the numbers are stated, and a
// deployment can replace them.
//
//   OES_SESSION_IDLE_MINUTES   default 30. No request for this long ends the session.
//   OES_SESSION_ABSOLUTE_HOURS default 8.  From authentication (or from birth, when
//                              the session never authenticated), even if it keeps asking.
//
// An empty, zero, or non-numeric value leaves the default. Read from the
// process environment: the sweep runs before it has a reason to open a
// setting stored in the base.

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

// `authenticatedAt` empty means the session has not logged in, and the
// absolute limit is then counted from `born`. `lastActive` empty is not idle.
ibSessionEnd ibJudgeSessionLifetime(const ibDateTime& now,
	const ibDateTime& born,
	const ibDateTime& authenticatedAt,
	const ibDateTime& lastActive,
	const ibSessionLifetime& policy);

// True when this login is still the one the password belongs to.
// An empty `passwordChangedAt` is a row written before the stamp existed.
bool ibSessionPasswordStillCurrent(const ibDateTime& authenticatedAt,
	const ibDateTime& passwordChangedAt);

#endif
