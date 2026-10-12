#ifndef __AUTH_THROTTLE_H__
#define __AUTH_THROTTLE_H__

// HOW OFTEN A LOGIN MAY TRY A PASSWORD.
//
// Every door (the desktop dialog, the web POST /login, a client of the
// application server) comes through ibApplicationInstance::AuthenticateUser,
// and that is the one place that asks. The numbers, written down because a
// limit that is only in the code is not a limit anyone can operate:
//
//   * One account. A failed attempt refuses the next one for 1 second, then
//     2, 4, 8, 16, 32, and 60 from the seventh failure on. A success clears
//     the count. A failure more than 15 minutes after the previous one
//     starts the count again at 1 second. There is no lock that only an
//     administrator can undo — a lock of that kind is itself a way to shut
//     a person out.
//   * One source (the web client's address, or "local" when the attempt has
//     none). The first 30 failures are accepted; after that, one further
//     failure is accepted per second, up to 30 waiting. A success does not
//     spend one of those: a morning when everyone arrives is not an attack.
//     Guessing a different name each time is, and the account limit above
//     does not see it.
//   * At most 4 password checks in flight in the process. A fifth is told
//     to wait one second and does not start the check.
//
// A refused attempt returns the wait, in seconds, and does not look the
// account up and does not hash the password.

#include <chrono>
#include <memory>

#include <wx/string.h>

#include "backend/backend.h"

class BACKEND_API ibAuthThrottle {
public:
	using Clock = std::chrono::steady_clock;

	static constexpr int kFailureWindowSec     = 15 * 60;
	static constexpr int kDelayCapSec          = 60;
	static constexpr int kSourceBurst          = 30;
	static constexpr int kSourceRefillPerSec   = 1;
	static constexpr int kMaxInFlightVerifies  = 4;

	struct Decision {
		bool m_allowed = false;
		int  m_retryAfterSec = 0;
	};

	ibAuthThrottle();
	~ibAuthThrottle();

	ibAuthThrottle(const ibAuthThrottle&) = delete;
	ibAuthThrottle& operator=(const ibAuthThrottle&) = delete;

	// The process-wide gate AuthenticateUser uses.
	static ibAuthThrottle& Shared();

	// The source named by ibAuthSourceScope on this thread, or "local".
	static wxString CurrentSource();

	// What the last attempt on this thread was told to wait. 0 when the
	// attempt was allowed to run (a wrong password is not a throttle).
	// Reading it clears it.
	static void NoteRefusal(int retryAfterSec);
	static int  TakeRefusal();

	Decision Begin(const wxString& user, const wxString& source);
	Decision Begin(const wxString& user, const wxString& source, Clock::time_point now);

	// The check Begin allowed has finished. A failure starts (or lengthens)
	// the account's wait and spends one source failure. A success clears
	// the account and spends nothing.
	void Finish(const wxString& user, const wxString& source, bool success);
	void Finish(const wxString& user, const wxString& source, bool success, Clock::time_point now);

	// Begin was allowed and the check never finished (it threw). The slot
	// is given back; the account and the source are left as they were.
	void ReleaseSlot();

	// How long, in seconds, until an attempt would be allowed. 0 means now.
	// Does not take a slot and does not record a failure.
	int SecondsUntil(const wxString& user, const wxString& source);
	int SecondsUntil(const wxString& user, const wxString& source, Clock::time_point now);

private:
	struct State;
	std::unique_ptr<State> m_state;
};

// Names the source of the login attempts on this thread, for as long as it
// stands. The web handler sets the client address on the worker that
// actually checks the password; a thread that never sets one is "local".
class BACKEND_API ibAuthSourceScope {
public:
	explicit ibAuthSourceScope(const wxString& source);
	~ibAuthSourceScope();

	ibAuthSourceScope(const ibAuthSourceScope&) = delete;
	ibAuthSourceScope& operator=(const ibAuthSourceScope&) = delete;

private:
	wxString m_previous;
};

#endif
