#ifndef __SESSION_EXCEPTION_H__
#define __SESSION_EXCEPTION_H__

// THE SESSION'S OWN REFUSALS — who may work in this base right now, and on whose connection.
//
// Same division as the query engine's (query/queryException.h, docs/exceptions.md §3): the TYPE says
// WHO refused, the Kind says what about it. A session refusal is not a database fault and not a
// query fault — nothing malfunctioned. Somebody else holds the base, or nobody holds it at all, and
// the caller can usually act on that: wait, ask the other user to leave, open a session first.
//
// Why it is its own family rather than a Kind on an existing one:
//
//   * as a DATABASE exception it would inherit retry semantics — and retrying "another user is
//     connected" is a spin, not a recovery;
//   * as a QUERY exception it would claim our machinery failed, which is false and sends whoever
//     reads it looking in the wrong place;
//   * as a bare ibBackendException it carries no answer to "so what do I do", which is precisely
//     what a session refusal is FOR — every Kind below maps to a different next step for the user.

#include "backend/backend_exception.h"

class BACKEND_API ibBackendSessionException : public ibBackendException
{
public:
	enum class Kind {
		// Somebody holds the exclusive lock. The caller waits or asks them to release it.
		ExclusiveHeld,

		// Others are connected, and the operation needs the base to itself (a restructuring that
		// executes DDL). Distinct from ExclusiveHeld: nobody has TAKEN anything, there are simply
		// other people in the base — a different sentence to the user and a different remedy.
		OthersActive,

		// There is no session on this thread at all. A programming fault at the boundary rather than
		// a condition of the base: something asked for session-scoped state outside a session.
		NoSession,

		// The session could not obtain a connection to work on. NOT a database fault — the engine was
		// never reached; the holder or the pool refused. Kept here so it stops arriving as a DBMS
		// error with a message about the driver.
		NoConnection,
	};

	Kind GetKind() const { return m_kind; }

	[[noreturn]] static void Throw(Kind kind, const wxString& message);

protected:
	ibBackendSessionException(Kind kind, const wxString& msg)
		: ibBackendException(msg), m_kind(kind) {}

private:
	Kind m_kind;
};

#endif
