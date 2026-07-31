#include "sessionSnapshot.h"
#include "appData.h"   // appData->GetRunModeDescr in GetApplication
#include "session.h"   // ibSessionKind for CountByKind

wxString ibSessionSnapshot::GetUserName(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return m_listSession[idx].m_strUserName;
}

wxString ibSessionSnapshot::GetComputerName(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return m_listSession[idx].m_strComputerName;
}

wxString ibSessionSnapshot::GetSession(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return m_listSession[idx].m_strSession;
}

wxString ibSessionSnapshot::GetStartedDate(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	const wxDateTime& startedDate = m_listSession[idx].m_startedDate;
	return startedDate.Format(wxT("%d.%m.%Y %H:%M:%S"));
}

wxString ibSessionSnapshot::GetApplication(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	const auto& row = m_listSession[idx];
	// ibSessionKind values mirror ibRunMode for 1:1 cases (Launcher /
	// Designer / Enterprise / Service), plus two web roles that share
	// the same ibRunMode::eWEB_RUNTIME_MODE. Pick a web-specific
	// label when the kind disambiguates, otherwise fall back to the
	// run-mode description.
	// A JOB IS NOT AN APPLICATION. Its run mode says which executable happens to
	// host it, which is exactly the wrong answer for the question this column
	// asks — a fold running inside enterprise.exe is not a thick client, and
	// reading it as one is how three rows look like three users. The kind is
	// checked first, because for these it carries the real answer.
	switch (row.m_kind) {
	case static_cast<int>(ibSessionKind::BackgroundJob): return _("Background job");
	case static_cast<int>(ibSessionKind::ScheduledJob):  return _("Scheduled job");
	case static_cast<int>(ibSessionKind::SystemJob):     return _("System job");
	default: break;
	}

	if (row.m_runMode == ibRunMode::eWEB_RUNTIME_MODE) {
		// ibSessionKind::WebServer = 5, WebClient = 100.
		if (row.m_kind == 100) return _("Web client");
		return _("Web server");
	}
	return appData->GetRunModeDescr(row.m_runMode);
}

ibRunMode ibSessionSnapshot::GetSessionApplication(unsigned int idx) const
{
	if (idx > m_listSession.size())
		return ibRunMode::eLAUNCHER_MODE;
	return m_listSession[idx].m_runMode;
}

int ibSessionSnapshot::GetSessionKind(unsigned int idx) const
{
	if (idx > m_listSession.size())
		return 0;
	return m_listSession[idx].m_kind;
}

wxString ibSessionSnapshot::GetSessionKindDescr(unsigned int idx) const
{
	if (idx >= m_listSession.size())
		return wxEmptyString;
	const auto& row = m_listSession[idx];
	// Only web runtime carries the server/client split; desktop modes
	// are always user-facing, reported as "Client" for consistency in
	// the Active Users dialog. Legacy schemas (no `kind` column) read
	// m_kind = 0; interpret that as Server when run-mode is web (the
	// historic behaviour before per-tab sessions landed) and Client
	// otherwise.
	// Jobs are neither: nobody is sitting at them. Saying "Client" here would
	// claim a person is connected, which is the one thing an administrator reads
	// this column to find out.
	switch (row.m_kind) {
	case static_cast<int>(ibSessionKind::BackgroundJob):
	case static_cast<int>(ibSessionKind::ScheduledJob):
	case static_cast<int>(ibSessionKind::SystemJob):
		return _("Job");
	default: break;
	}

	if (row.m_runMode == ibRunMode::eWEB_RUNTIME_MODE) {
		// ibSessionKind::WebClient = 100.
		return (row.m_kind == 100) ? _("Client") : _("Server");
	}
	return _("Client");
}

void ibSessionSnapshot::SetKindsFromMap(
	const std::unordered_map<wxString, int>& kindBySession)
{
	for (auto& u : m_listSession) {
		auto it = kindBySession.find(u.m_strSession);
		if (it != kindBySession.end())
			u.m_kind = it->second;
	}
}

void ibSessionSnapshot::SetExclusiveFromMap(
	const std::unordered_map<wxString, bool>& exclusiveBySession)
{
	for (auto& u : m_listSession) {
		auto it = exclusiveBySession.find(u.m_strSession);
		if (it != exclusiveBySession.end())
			u.m_exclusive = it->second;
	}
}

bool ibSessionSnapshot::IsExclusive(unsigned int idx) const
{
	if (idx >= m_listSession.size()) return false;
	return m_listSession[idx].m_exclusive;
}

wxString ibSessionSnapshot::ExclusiveHolderSession() const
{
	for (const auto& u : m_listSession)
		if (u.m_exclusive)
			return u.m_strSession;
	return wxEmptyString;
}

wxString ibSessionSnapshot::ExclusiveHolderUser() const
{
	for (const auto& u : m_listSession)
		if (u.m_exclusive)
			return u.m_strUserName;
	return wxEmptyString;
}

// IS A PERSON WORKING HERE? Both predicates below answer that, and neither may
// count a session that exists only to serve one — a background job carries the
// user name of whoever started it, so counting it makes one person read as two.
// The consequences are not cosmetic: an exclusive-mode gate built on these would
// block on a job the blocking window itself started, and a licence count would
// double every seat that happens to be reading.
static bool ibIsPersonSession(int kind)
{
	switch (kind) {
	case static_cast<int>(ibSessionKind::BackgroundJob):
	case static_cast<int>(ibSessionKind::ScheduledJob):
	case static_cast<int>(ibSessionKind::SystemJob):
		return false;
	default:
		return true;
	}
}

bool ibSessionSnapshot::HasActiveUsers() const
{
	for (const auto& u : m_listSession)
		if (!u.m_strUserName.IsEmpty() && ibIsPersonSession(u.m_kind))
			return true;
	return false;
}

bool ibSessionSnapshot::IsUserActive(const wxString& userName) const
{
	if (userName.IsEmpty())
		return false;
	for (const auto& u : m_listSession)
		if (u.m_strUserName == userName && ibIsPersonSession(u.m_kind))
			return true;
	return false;
}

unsigned int ibSessionSnapshot::CountByKind(ibSessionKind kind) const
{
	const int wanted = static_cast<int>(kind);
	unsigned int count = 0;
	for (const auto& u : m_listSession)
		if (u.m_kind == wanted)
			++count;
	return count;
}
