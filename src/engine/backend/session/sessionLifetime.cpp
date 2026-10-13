#include "sessionLifetime.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <wx/string.h>
#include <wx/utils.h>

namespace {

// Whole units from the environment. Zero, junk, and a product that does not
// fit in the millisecond count leave `fallback` alone.
long long ReadScaled(const wxString& name, long long fallback, long long scale)
{
	wxString raw;
	if (!wxGetEnv(name, &raw))
		return fallback;
	raw.Trim(true).Trim(false);
	if (raw.empty())
		return fallback;

	const wxScopedCharBuffer utf8 = raw.utf8_str();
	if (utf8.data() == nullptr)
		return fallback;
	char* end = nullptr;
	errno = 0;
	const unsigned long long units = std::strtoull(utf8.data(), &end, 10);
	if (end == utf8.data() || (end != nullptr && *end != '\0') || errno == ERANGE || units == 0)
		return fallback;
	if (units > static_cast<unsigned long long>(INT64_MAX / scale))
		return fallback;
	return static_cast<long long>(units) * scale;
}

} // namespace

ibSessionLifetime ibSessionLifetimePolicy()
{
	ibSessionLifetime policy;
	policy.m_idleMs     = ReadScaled(wxT("OES_SESSION_IDLE_MINUTES"),   policy.m_idleMs,     60LL * 1000LL);
	policy.m_absoluteMs = ReadScaled(wxT("OES_SESSION_ABSOLUTE_HOURS"), policy.m_absoluteMs, 60LL * 60LL * 1000LL);
	return policy;
}

long long ibWallUnixMs(const ibDateTime& reading)
{
	if (reading.IsEmpty())
		return 0;
	ibDateTimeParts parts;
	reading.ToParts(parts);
	std::tm local{};
	local.tm_year = parts.m_year - 1900;
	local.tm_mon = static_cast<int>(parts.m_month) - 1;
	local.tm_mday = static_cast<int>(parts.m_day);
	local.tm_hour = static_cast<int>(parts.m_hour);
	local.tm_min = static_cast<int>(parts.m_minute);
	local.tm_sec = static_cast<int>(parts.m_second);
	local.tm_isdst = -1;
	const std::time_t placed = std::mktime(&local);
	if (placed == static_cast<std::time_t>(-1))
		return 0;
	return static_cast<long long>(placed) * 1000LL + static_cast<long long>(parts.m_millisecond);
}

ibSessionEnd ibJudgeSessionLifetime(long long nowMs,
	long long bornMs,
	long long authenticatedAtMs,
	long long lastActiveMs,
	const ibSessionLifetime& policy)
{
	const long long anchor = authenticatedAtMs > 0 ? authenticatedAtMs : bornMs;
	if (anchor > 0 && policy.m_absoluteMs > 0 && nowMs - anchor > policy.m_absoluteMs)
		return ibSessionEnd::Absolute;
	if (lastActiveMs > 0 && policy.m_idleMs > 0 && nowMs - lastActiveMs > policy.m_idleMs)
		return ibSessionEnd::Idle;
	return ibSessionEnd::Keep;
}

bool ibSessionPasswordStillCurrent(long long authenticatedAtMs,
	const ibDateTime& passwordChangedAt)
{
	if (passwordChangedAt.IsEmpty())
		return true;
	const long long changedMs = ibWallUnixMs(passwordChangedAt);
	if (changedMs == 0)
		return true;
	if (authenticatedAtMs <= 0)
		return false;
	return authenticatedAtMs >= changedMs;
}
