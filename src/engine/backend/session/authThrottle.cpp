#include "backend/session/authThrottle.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace {

wxString AccountKey(wxString user)
{
	user.Trim(true).Trim(false);
	return user.Lower();
}

// An address that is missing, or that is this machine, is not a shared bucket.
// "127.0.0.1:8080" and "::1" are the same case as an empty string.
bool AddressIsThisMachine(const wxString& address)
{
	wxString host = address;
	host.Trim(true).Trim(false);
	host.MakeLower();
	if (host.IsEmpty())
		return true;
	return host.StartsWith(wxT("127."))
		|| host.StartsWith(wxT("0.0.0.0"))
		|| host.StartsWith(wxT("localhost"))
		|| host.StartsWith(wxT("::1"))
		|| host.StartsWith(wxT("[::1]"));
}

int CeilSeconds(ibAuthThrottle::Clock::duration d)
{
	if (d <= ibAuthThrottle::Clock::duration::zero())
		return 0;
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
	const auto sec = (ms + 999) / 1000;
	return sec > 0 ? static_cast<int>(sec) : 1;
}

int DelayAfterFailures(int failures)
{
	if (failures >= 7)
		return ibAuthThrottle::kDelayCapSec;
	const int shifted = 1 << (failures - 1);
	return shifted > ibAuthThrottle::kDelayCapSec ? ibAuthThrottle::kDelayCapSec : shifted;
}

} // namespace

struct ibAuthThrottle::State {
	struct Account {
		int               m_failures = 0;
		Clock::time_point m_next{};
		Clock::time_point m_lastFailure{};
	};
	struct Bucket {
		double            m_tokens = kSourceBurst;
		Clock::time_point m_updated{};
		bool              m_seen = false;
	};

	std::mutex                  m_mutex;
	std::map<wxString, Account> m_accounts;
	std::map<wxString, Bucket>  m_addresses;

	static void Refill(Bucket& bucket, Clock::time_point now);
	void BoundAccounts(Clock::time_point now);
	void BoundAddresses();
	Decision Decide(const wxString& account, const wxString& address, Clock::time_point now);
};

void ibAuthThrottle::State::Refill(Bucket& bucket, Clock::time_point now)
{
	if (!bucket.m_seen) {
		bucket.m_tokens  = kSourceBurst;
		bucket.m_updated = now;
		bucket.m_seen    = true;
		return;
	}
	const double elapsed = std::chrono::duration<double>(now - bucket.m_updated).count();
	if (elapsed <= 0)
		return;
	const double filled = bucket.m_tokens + elapsed * kSourceRefillPerSec;
	bucket.m_tokens  = std::min(static_cast<double>(kSourceBurst), filled);
	bucket.m_updated = now;
}

void ibAuthThrottle::State::BoundAccounts(Clock::time_point now)
{
	while (m_accounts.size() > kMaxTracked) {
		auto drop = m_accounts.end();
		for (auto it = m_accounts.begin(); it != m_accounts.end(); ++it) {
			if (drop == m_accounts.end()) {
				drop = it;
				continue;
			}
			const bool expired = it->second.m_next <= now;
			const bool dropExpired = drop->second.m_next <= now;
			if (expired != dropExpired) {
				if (expired)
					drop = it;
				continue;
			}
			if (it->second.m_lastFailure < drop->second.m_lastFailure)
				drop = it;
		}
		if (drop == m_accounts.end())
			break;
		m_accounts.erase(drop);
	}
}

void ibAuthThrottle::State::BoundAddresses()
{
	while (m_addresses.size() > kMaxTracked) {
		auto drop = m_addresses.end();
		for (auto it = m_addresses.begin(); it != m_addresses.end(); ++it) {
			if (drop == m_addresses.end() || it->second.m_updated < drop->second.m_updated)
				drop = it;
		}
		if (drop == m_addresses.end())
			break;
		m_addresses.erase(drop);
	}
}

ibAuthThrottle::Decision ibAuthThrottle::State::Decide(const wxString& account, const wxString& address,
	Clock::time_point now)
{
	Decision decision;
	decision.m_allowed = true;

	const auto found = m_accounts.find(account);
	if (found != m_accounts.end() && found->second.m_next > now) {
		decision.m_allowed = false;
		decision.m_retryAfterSec = CeilSeconds(found->second.m_next - now);
		decision.m_reason = Reason::Account;
	}

	if (AddressIsThisMachine(address))
		return decision;

	Bucket& bucket = m_addresses[address];
	Refill(bucket, now);
	if (bucket.m_tokens < 1.0) {
		const double need = (1.0 - bucket.m_tokens) / kSourceRefillPerSec;
		const int bucketWait = static_cast<int>(std::ceil(need));
		const int wait = bucketWait > 0 ? bucketWait : 1;
		if (wait > decision.m_retryAfterSec) {
			decision.m_allowed = false;
			decision.m_retryAfterSec = wait;
			decision.m_reason = Reason::Address;
		}
	}
	return decision;
}

ibAuthThrottle::ibAuthThrottle()
	: m_state(std::make_unique<State>())
{
}

ibAuthThrottle::~ibAuthThrottle() = default;

ibAuthThrottle::Decision ibAuthThrottle::Begin(const wxString& user, const wxString& address, Clock::time_point now)
{
	const wxString account = AccountKey(user);
	std::lock_guard<std::mutex> lock(m_state->m_mutex);
	return m_state->Decide(account, address, now);
}

void ibAuthThrottle::Finish(const wxString& user, const wxString& address, bool success, Clock::time_point now)
{
	const wxString account = AccountKey(user);
	std::lock_guard<std::mutex> lock(m_state->m_mutex);

	if (success) {
		m_state->m_accounts.erase(account);
		return;
	}

	State::Account& row = m_state->m_accounts[account];
	const auto window = std::chrono::seconds(kFailureWindowSec);
	if (row.m_failures > 0 && now - row.m_lastFailure >= window)
		row.m_failures = 0;
	++row.m_failures;
	row.m_lastFailure = now;
	row.m_next = now + std::chrono::seconds(DelayAfterFailures(row.m_failures));
	m_state->BoundAccounts(now);

	if (AddressIsThisMachine(address))
		return;

	State::Bucket& bucket = m_state->m_addresses[address];
	State::Refill(bucket, now);
	if (bucket.m_tokens >= 1.0)
		bucket.m_tokens -= 1.0;
	else
		bucket.m_tokens = 0.0;
	m_state->BoundAddresses();
}
