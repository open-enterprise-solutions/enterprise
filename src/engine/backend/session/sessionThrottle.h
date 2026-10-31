#ifndef __SESSION_THROTTLE_H__
#define __SESSION_THROTTLE_H__

// HOW OFTEN A LOGIN MAY TRY A PASSWORD: against guessing, not against load — what holds a storm of connections back is
// MemoryLimit and ClientConnections (backend.conf). One per base (ibSessionRegistry::Attach, the door every login comes
// through). A refused attempt does not look the account up and does not hash the password.
//
//   * A NAME FROM AN ADDRESS. A failed attempt refuses the next one from there for 1 second, then 2, 4, 8, 16, 32, and
//     60. A success clears it; so does a quarter of an hour with no failure. From another address the same name goes
//     on, so a stranger who keeps getting a password wrong holds up nobody else.
//   * AN ADDRESS, whatever names it tries: 30 attempts, then one more a second. The attempt is SPENT WHEN IT BEGINS and
//     given back when it succeeds, or a hundred guesses sent together would all pass before the first had failed. So
//     a morning when everyone arrives spends nothing in the end; only past 30 checks at once from one address is the
//     next told to wait.
//   * AFTER A FAILURE, ONE CHECK AT A TIME for that name from that address, for the same reason. Before any failure
//     checks run side by side: refused, the correct passwords of a shared name would fail, and a client does not retry.
//   * THE ADDRESS OF THIS MACHINE, OR NONE, IS NOT A BUCKET: a desktop, and whatever a proxy brings in, must not lock
//     every other login. Those are told apart by the name alone - so behind a proxy that does not say who is calling,
//     one stranger can still hold up a name, and an address has no limit at all.
//   * NO NAME IS NO GUESS: every client's start logs in with none, to learn whether the base asks for one. Counted, it
//     would hold up the next client's start from that address. It only holds its attempt while it is checked.
//
// The name is as it is written: names are case-sensitive here, and folding them would merge two accounts.

#include <chrono>
#include <map>
#include <mutex>
#include <utility>

#include "backend/backend.h"
#include "core/fstring.h"

class BACKEND_API ibAuthThrottle {
public:
	using Clock = std::chrono::steady_clock;

	static constexpr int         kFailureWindowSec = 15 * 60;
	static constexpr int         kMostDelaySec     = 60;
	static constexpr int         kAddressAttempts  = 30;
	static constexpr std::size_t kMostTracked      = 1024;

	enum class Reason { None, Account, Address };

	struct Decision {
		bool   m_allowed = false;
		int    m_retryAfterSec = 0;
		Reason m_reason = Reason::None;
	};

	// May this attempt check its password? Allowed, it ends in exactly one of the three below.
	Decision Begin(const ibString& name, const ibString& address, Clock::time_point now);
	void     Succeed(const ibString& name, const ibString& address);
	void     Fail(const ibString& name, const ibString& address, Clock::time_point now);
	// …nothing was learnt of the password: a database that could not be read is not a wrong one.
	void     Cancel(const ibString& name, const ibString& address);

private:
	struct ibAccount {
		int               m_failures = 0;
		bool              m_checking = false;
		Clock::time_point m_next{};
		Clock::time_point m_lastFailure{};
	};
	struct ibBucket {
		double            m_attempts = kAddressAttempts;
		Clock::time_point m_updated{};
	};
	using ibAccountKey = std::pair<ibString, ibString>;

	void GiveBack(const ibString& address);
	ibBucket& BucketOf(const ibString& address, Clock::time_point now);

	std::mutex                        m_mutex;
	std::map<ibAccountKey, ibAccount> m_accounts;
	std::map<ibString, ibBucket>      m_buckets;
};

#endif
