#include "serviceExclusivePolicy.h"
#include "sessionRegistry.h"
#include "sessionSnapshot.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // CanOpen reads the rows the base has now
#include "backend/diagnostics/journal.h"                  // the veto is written down, not only shown

namespace {

// The refusal, said once for both doors — what a person is shown and what the journal keeps.
wxString DescribeRefusal(bool arrivingService, const wxString& started, const wxString& computer, const wxString& user)
{
	if (arrivingService) {
		ibJournalInfo(wxT("session"), wxT("the application server refused the base - a client uses it: started %s, computer %s, user %s"),
			started, computer, user);
		return wxString::Format(_("The base cannot be served - a client outside the application servers uses it:\n%s, %s, %s"),
			started, computer, user);
	}
	ibJournalInfo(wxT("session"), wxT("refused - the base is served by the application server on %s since %s"),
		computer, started);
	return wxString::Format(_("The base is served by the application server on %s (since %s) and admits only application servers."),
		computer, started);
}

} // namespace

ibServiceExclusivePolicy::ibServiceExclusivePolicy(ibSessionRegistry* registry)
	: m_registry(registry)
{
}

bool ibServiceExclusivePolicy::IsInTheWay(bool arrivingService, ibSessionKind rowKind)
{
	// Servers share a base with servers, clients with clients — the two do not mix.
	return arrivingService != (rowKind == ibSessionKind::Service);
}

bool ibServiceExclusivePolicy::CanAdd(const ibSession& session, wxString& reason)
{
	if (m_registry == nullptr)
		return true;   // no registry to scan through — be permissive

	const bool arrivingService = session.GetKind() == ibSessionKind::Service;
	const wxString& ownId = session.GetId();

	// THE ROWS IN THE WAY — of another process, which is every row this registry does not know as its own.
	const auto inTheWay = [&](const ibSessionSnapshot& snap) {
		std::vector<unsigned int> rows;
		for (unsigned int i = 0; i < snap.GetSessionCount(); ++i) {
			const wxString id = snap.GetSession(i);
			if (id == ownId)
				continue;
			if (m_registry->Find(id).Share())
				continue;   // this process's own — the server's jobs, and later its clients
			if (IsInTheWay(arrivingService, static_cast<ibSessionKind>(snap.GetSessionKind(i))))
				rows.push_back(i);
		}
		return rows;
	};

	ibSessionSnapshot snap = m_registry->GetClusterSnapshot();
	std::vector<unsigned int> rows = inTheWay(snap);

	// …UNLESS IT DOES NOT BREATHE — a server killed a moment ago must not keep its base until the sweep's
	// cutoff passes. Same settling as the designer rule.
	if (!rows.empty()) {
		std::vector<wxString> guids;
		for (const unsigned int i : rows)
			guids.push_back(snap.GetSession(i));
		if (m_registry->SettleSilentPeers(guids) > 0) {
			snap = m_registry->GetClusterSnapshot();
			rows = inTheWay(snap);
		}
	}

	if (rows.empty())
		return true;

	const unsigned int i = rows.front();
	reason = DescribeRefusal(arrivingService, snap.GetStartedDate(i), snap.GetComputerName(i), snap.GetUserName(i));
	return false;
}

bool ibServiceExclusivePolicy::CanOpen(ibRunMode runMode, wxString& reason)
{
	const bool arrivingService = runMode == eSERVICE_MODE;
	try {
		// The base being opened — the opening thread works for it. Every row there is another process's: this
		// one has none yet in a base it is only opening.
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(session_table)
			.Select({ wxT("kind"), wxT("started"), wxT("computer"), wxT("userName"), wxT("lastActive") })
			.Execute();

		// Alive = its beat within the registry's one silence; an older row is a process that is gone.
		const ibDateTime now = ibDateTime::Now();
		const long long silence = ibSessionRegistry::GetSilentSeconds() * 1000ll;
		while (rs.Next()) {
			const ibDateTime beat = rs.GetResultDate(wxT("lastActive"));
			if (beat.IsEmpty() || now.ElapsedSince(beat) >= silence)
				continue;
			if (!IsInTheWay(arrivingService, static_cast<ibSessionKind>(rs.GetResultInt(wxT("kind")))))
				continue;

			const ibDateTime started = rs.GetResultDate(wxT("started"));
			reason = DescribeRefusal(arrivingService, started.IsEmpty() ? wxString() : started.ToString().ToWxString(),
				rs.GetResultString(wxT("computer")), rs.GetResultString(wxT("userName")));
			return false;
		}
	}
	catch (...) {
		// Not proven held — a base too old to have the columns, a read that failed. The session-time check
		// (CanAdd) still stands behind this one.
	}
	return true;
}
