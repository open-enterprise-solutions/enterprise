#include "backend/session/sessionThrottle.h"

#include <algorithm>
#include <cmath>

namespace {

// The peer as the listeners hand it over: its address alone, no port (core/net/httpServer.cpp).
bool IsThisMachine(const ibString& address)
{
	return address.IsEmpty() || address.StartsWith(ibT("127.")) || address == ibT("::1");
}

std::pair<ibString, ibString> KeyOf(const ibString& name, const ibString& address)
{
	return { name, IsThisMachine(address) ? ibString() : address };
}

int DelayAfter(int failures)
{
	return failures >= 7 ? ibAuthThrottle::kMostDelaySec : 1 << (failures - 1);
}

ibAuthThrottle::Decision Refused(ibAuthThrottle::Reason reason, int retryAfterSec)
{
	ibAuthThrottle::Decision decision;
	decision.m_retryAfterSec = std::max(retryAfterSec, 1);
	decision.m_reason = reason;
	return decision;
}

} // namespace

ibAuthThrottle::ibBucket& ibAuthThrottle::BucketOf(const ibString& address, Clock::time_point now)
{
	auto found = m_buckets.find(address);
	if (found == m_buckets.end()) {
		// Addresses without end (IPv6) must not grow this without end: the one quiet longest makes room.
		if (m_buckets.size() >= kMostTracked) {
			m_buckets.erase(std::min_element(m_buckets.begin(), m_buckets.end(), [](const auto& a, const auto& b) {
				return a.second.m_updated < b.second.m_updated;
			}));
		}
		found = m_buckets.emplace(address, ibBucket()).first;
		found->second.m_updated = now;
		return found->second;
	}
	ibBucket& bucket = found->second;
	const double seconds = std::chrono::duration<double>(now - bucket.m_updated).count();
	if (seconds > 0) {
		bucket.m_attempts = std::min<double>(kAddressAttempts, bucket.m_attempts + seconds);
		bucket.m_updated = now;
	}
	return bucket;
}

void ibAuthThrottle::GiveBack(const ibString& address)
{
	const auto found = m_buckets.find(address);
	if (found != m_buckets.end())
		found->second.m_attempts = std::min<double>(kAddressAttempts, found->second.m_attempts + 1);
}

ibAuthThrottle::Decision ibAuthThrottle::Begin(const ibString& name, const ibString& address, Clock::time_point now)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	ibAccount* account = nullptr;
	const auto found = m_accounts.find(KeyOf(name, address));
	if (found != m_accounts.end()) {
		if (now - found->second.m_lastFailure >= std::chrono::seconds(kFailureWindowSec)) {
			m_accounts.erase(found);
		}
		else {
			account = &found->second;
			if (account->m_next > now) {
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(account->m_next - now).count();
				return Refused(Reason::Account, static_cast<int>((ms + 999) / 1000));
			}
			if (account->m_checking)
				return Refused(Reason::Account, 1);
		}
	}

	if (!IsThisMachine(address)) {
		ibBucket& bucket = BucketOf(address, now);
		if (bucket.m_attempts < 1.0)
			return Refused(Reason::Address, static_cast<int>(std::ceil(1.0 - bucket.m_attempts)));
		bucket.m_attempts -= 1.0;
	}

	if (account != nullptr)
		account->m_checking = true;
	Decision decision;
	decision.m_allowed = true;
	return decision;
}

void ibAuthThrottle::Succeed(const ibString& name, const ibString& address)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_accounts.erase(KeyOf(name, address));
	GiveBack(address);
}

void ibAuthThrottle::Fail(const ibString& name, const ibString& address, Clock::time_point now)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (name.IsEmpty()) {
		GiveBack(address);
		return;
	}
	ibAccount& account = m_accounts[KeyOf(name, address)];
	account.m_checking = false;
	++account.m_failures;
	account.m_lastFailure = now;
	account.m_next = now + std::chrono::seconds(DelayAfter(account.m_failures));
	// Names without end must not grow this without end: the one that failed longest ago makes room.
	if (m_accounts.size() > kMostTracked) {
		m_accounts.erase(std::min_element(m_accounts.begin(), m_accounts.end(), [](const auto& a, const auto& b) {
			return a.second.m_lastFailure < b.second.m_lastFailure;
		}));
	}
}

void ibAuthThrottle::Cancel(const ibString& name, const ibString& address)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto found = m_accounts.find(KeyOf(name, address));
	if (found != m_accounts.end())
		found->second.m_checking = false;
	GiveBack(address);
}
