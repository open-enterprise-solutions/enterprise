#include "sessionLifetime.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>

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

ibSessionEnd ibJudgeSessionLifetime(const ibDateTime& now,
	const ibDateTime& born,
	const ibDateTime& authenticatedAt,
	const ibDateTime& lastActive,
	const ibSessionLifetime& policy)
{
	const ibDateTime& anchor = authenticatedAt.IsEmpty() ? born : authenticatedAt;
	if (!anchor.IsEmpty() && policy.m_absoluteMs > 0
		&& now.ElapsedSince(anchor) > policy.m_absoluteMs)
		return ibSessionEnd::Absolute;
	if (!lastActive.IsEmpty() && policy.m_idleMs > 0
		&& now.ElapsedSince(lastActive) > policy.m_idleMs)
		return ibSessionEnd::Idle;
	return ibSessionEnd::Keep;
}

bool ibSessionPasswordStillCurrent(const ibDateTime& authenticatedAt,
	const ibDateTime& passwordChangedAt)
{
	if (passwordChangedAt.IsEmpty())
		return true;
	if (authenticatedAt.IsEmpty())
		return false;
	return authenticatedAt.GetValue() >= passwordChangedAt.GetValue();
}
