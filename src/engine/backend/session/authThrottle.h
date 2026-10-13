#ifndef __AUTH_THROTTLE_H__
#define __AUTH_THROTTLE_H__

// HOW OFTEN A LOGIN MAY TRY A PASSWORD.
//
// The gate belongs to one base (ibApplicationInstance). Two bases in one
// process do not share a refusal. Every door comes through
// ibApplicationInstance::AuthenticateUser, and that is the one place that asks.
//
//   * One account. A failed attempt refuses the next one for 1 second, then
//     2, 4, 8, 16, 32, and 60. A success clears the count. A failure a minute
//     or more after the previous one starts again at 1 second, so one mistake
//     a minute cannot climb to a minute and stay there. The folded spelling of
//     the name is the account, the same fold the user table uses.
//   * One address, when the attempt has one and it is not this machine. The
//     first 30 failures are accepted; after that, one further failure is
//     accepted per second. A success does not spend one of those. An empty
//     address, or a loopback one, is not a bucket: a desktop and a proxy that
//     arrived with no address must not lock every other login.
//
// A refused attempt says why (the account, or the address) and how long to
// wait. It does not look the account up and does not hash the password.

#include <chrono>
#include <memory>

#include <wx/string.h>

#include "backend/backend.h"

class BACKEND_API ibAuthThrottle {
public:
	using Clock = std::chrono::steady_clock;

	static constexpr int kFailureWindowSec   = 60;
	static constexpr int kDelayCapSec        = 60;
	static constexpr int kSourceBurst        = 30;
	static constexpr int kSourceRefillPerSec = 1;
	static constexpr std::size_t kMaxTracked = 1024;

	enum class Reason { None, Account, Address };

	struct Decision {
		bool   m_allowed = false;
		int    m_retryAfterSec = 0;
		Reason m_reason = Reason::None;
	};

	ibAuthThrottle();
	~ibAuthThrottle();

	ibAuthThrottle(const ibAuthThrottle&) = delete;
	ibAuthThrottle& operator=(const ibAuthThrottle&) = delete;

	// `now` is the caller's clock. Production passes Clock::now(); a test passes
	// the instant it is talking about.
	Decision Begin(const wxString& user, const wxString& address, Clock::time_point now);

	// The check Begin allowed has finished. A failure starts (or lengthens)
	// the account's wait and, for a real remote address, spends one failure.
	// A success clears the account and spends nothing.
	void Finish(const wxString& user, const wxString& address, bool success, Clock::time_point now);

private:
	struct State;
	std::unique_ptr<State> m_state;
};

#endif
