#include "backend/session/authThrottle.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace {

thread_local wxString tl_source;
thread_local int      tl_refusalSec = 0;

wxString AccountKey(wxString user)
{
	user.Trim(true).Trim(false);
	return user.Lower();
}

wxString SourceKey(wxString source)
{
	source.Trim(true).Trim(false);
	if (source.IsEmpty())
		return wxT("local");
	return source;
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
		int            m_failures = 0;
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
	std::map<wxString, Bucket>  m_sources;
	int                         m_inFlight = 0;

	static void Refill(Bucket& bucket, Clock::time_point now);
	// Under the lock. Refills the source. 0 means an attempt may start.
	int RefusalSeconds(const wxString& account, const wxString& source, Clock::time_point now);
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

int ibAuthThrottle::State::RefusalSeconds(const wxString& account, const wxString& source, Clock::time_point now)
{
	int wait = 0;
	const auto found = m_accounts.find(account);
	if (found != m_accounts.end() && found->second.m_next > now)
		wait = CeilSeconds(found->second.m_next - now);

	Bucket& bucket = m_sources[source];
	Refill(bucket, now);
	if (bucket.m_tokens < 1.0) {
		const double need = (1.0 - bucket.m_tokens) / kSourceRefillPerSec;
		const int bucketWait = static_cast<int>(std::ceil(need));
		wait = std::max(wait, bucketWait > 0 ? bucketWait : 1);
	}
	if (m_inFlight >= kMaxInFlightVerifies)
		wait = std::max(wait, 1);
	return wait;
}

ibAuthThrottle::ibAuthThrottle()
	: m_state(std::make_unique<State>())
{
}

ibAuthThrottle::~ibAuthThrottle() = default;

ibAuthThrottle& ibAuthThrottle::Shared()
{
	static ibAuthThrottle gate;
	return gate;
}

wxString ibAuthThrottle::CurrentSource()
{
	return tl_source.IsEmpty() ? wxString(wxT("local")) : tl_source;
}

void ibAuthThrottle::NoteRefusal(int retryAfterSec)
{
	tl_refusalSec = retryAfterSec > 0 ? retryAfterSec : 0;
}

int ibAuthThrottle::TakeRefusal()
{
	const int seconds = tl_refusalSec;
	tl_refusalSec = 0;
	return seconds;
}

ibAuthThrottle::Decision ibAuthThrottle::Begin(const wxString& user, const wxString& source)
{
	return Begin(user, source, Clock::now());
}

ibAuthThrottle::Decision ibAuthThrottle::Begin(const wxString& user, const wxString& source, Clock::time_point now)
{
	const wxString account = AccountKey(user);
	const wxString from    = SourceKey(source);
	std::lock_guard<std::mutex> lock(m_state->m_mutex);
	const int wait = m_state->RefusalSeconds(account, from, now);
	Decision decision;
	if (wait > 0) {
		decision.m_allowed = false;
		decision.m_retryAfterSec = wait;
		return decision;
	}
	++m_state->m_inFlight;
	decision.m_allowed = true;
	decision.m_retryAfterSec = 0;
	return decision;
}

void ibAuthThrottle::Finish(const wxString& user, const wxString& source, bool success)
{
	Finish(user, source, success, Clock::now());
}

void ibAuthThrottle::Finish(const wxString& user, const wxString& source, bool success, Clock::time_point now)
{
	const wxString account = AccountKey(user);
	const wxString from    = SourceKey(source);
	std::lock_guard<std::mutex> lock(m_state->m_mutex);
	if (m_state->m_inFlight > 0)
		--m_state->m_inFlight;

	if (success) {
		m_state->m_accounts.erase(account);
		return;
	}

	State::Account& row = m_state->m_accounts[account];
	const auto window = std::chrono::seconds(kFailureWindowSec);
	if (row.m_failures > 0 && now - row.m_lastFailure > window)
		row.m_failures = 0;
	++row.m_failures;
	row.m_lastFailure = now;
	row.m_next = now + std::chrono::seconds(DelayAfterFailures(row.m_failures));

	State::Bucket& bucket = m_state->m_sources[from];
	State::Refill(bucket, now);
	if (bucket.m_tokens >= 1.0)
		bucket.m_tokens -= 1.0;
	else
		bucket.m_tokens = 0.0;
}

void ibAuthThrottle::ReleaseSlot()
{
	std::lock_guard<std::mutex> lock(m_state->m_mutex);
	if (m_state->m_inFlight > 0)
		--m_state->m_inFlight;
}

int ibAuthThrottle::SecondsUntil(const wxString& user, const wxString& source)
{
	return SecondsUntil(user, source, Clock::now());
}

int ibAuthThrottle::SecondsUntil(const wxString& user, const wxString& source, Clock::time_point now)
{
	const wxString account = AccountKey(user);
	const wxString from    = SourceKey(source);
	std::lock_guard<std::mutex> lock(m_state->m_mutex);
	return m_state->RefusalSeconds(account, from, now);
}

ibAuthSourceScope::ibAuthSourceScope(const wxString& source)
	: m_previous(tl_source)
{
	tl_source = source;
}

ibAuthSourceScope::~ibAuthSourceScope()
{
	tl_source = m_previous;
}
