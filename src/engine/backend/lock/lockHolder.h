/////////////////////////////////////////////////////////////////////////////
// ibLockHolder — identity marker for who owns a set of pessimistic
// (sys_lock) locks. Mirrors the ibDatabaseConnectionHolder pattern from
// the connection pool (see connection-pool.md).
//
// Today's primary holder kind = the active ibSession (one row in
// sys_session). Future / custom kinds, intentionally enabled by this
// abstraction:
//
//   - Long-running background job not tied to a user session
//     (Designer apply that needs to hold locks across worker hops,
//      scheduled maintenance that locks several namespaces).
//   - System-level coordinator (cluster-wide singleton that pre-acquires
//     locks before a deploy / migration).
//   - Test fixtures (deterministic identity, no session needed).
//
// Acquire takes an optional `ibLockHolder*`; when null, the manager
// falls back to ibSession::Current() so existing call sites just work.
//
// Identity = a GUID. Two holders with the same Identity() are
// indistinguishable from sys_lock's perspective (same row owner). For
// the default session-derived holder, identity = sys_session.session
// — keeps lock cleanup naturally aligned with session teardown.
//
// See docs/record-locks.md.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_HOLDER_H_
#define _IB_LOCK_HOLDER_H_

#include "backend/backend.h"
#include "backend/guid.h"

class BACKEND_API ibLockHolder {
public:
	virtual ~ibLockHolder() = default;

	// Owner-identity GUID. Stored as sys_lock.sessionGuid (the column
	// keeps its historical name — semantically it's "owner-guid",
	// session is one kind of owner). Two acquires with the same
	// Identity() are same-owner re-entry; different Identity = cross-
	// owner conflict check applies.
	virtual ibGuid Identity() const = 0;

	// Human-readable owner name surfaced in conflict messages
	// ("locked by user X") and admin UI rows. For session holders =
	// the user name; for custom holders = whatever the caller picks
	// (e.g. "Designer apply", "Maintenance job 42").
	virtual wxString DisplayName() const = 0;

	// Machine hint surfaced in admin UI alongside DisplayName. For
	// session holders = the session's computer field; for custom
	// holders = caller-chosen (typically wxGetHostName()).
	virtual wxString Computer() const = 0;
};

// Standalone holder — generates its own GUID on construction and
// keeps caller-supplied display strings. Use for custom long-running
// owners that don't fit the session model (background jobs, test
// fixtures). Owns its own identity for its full lifetime.
class BACKEND_API ibSingleLockHolder : public ibLockHolder {
public:
	// displayName surfaces in conflict messages / admin UI. computer
	// defaults to wxGetHostName() when empty so callers don't have to
	// figure out the right hostname themselves.
	explicit ibSingleLockHolder(const wxString& displayName,
	                             const wxString& computer = wxEmptyString);
	~ibSingleLockHolder() override = default;

	ibGuid   Identity()    const override { return m_identity; }
	wxString DisplayName() const override { return m_displayName; }
	wxString Computer()    const override { return m_computer; }

private:
	ibGuid   m_identity;
	wxString m_displayName;
	wxString m_computer;
};

#endif // _IB_LOCK_HOLDER_H_
