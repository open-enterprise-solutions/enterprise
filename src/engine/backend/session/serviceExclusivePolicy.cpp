#include "serviceExclusivePolicy.h"
#include "sessionRegistry.h"
#include "sessionSnapshot.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // CanOpen reads the rows the base has now

#include <map>
#include <thread>

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

bool ibServiceExclusivePolicy::IsInTheWay(bool arrivingService, ibRunMode rowRunMode)
{
	// Servers share a base with servers, file bases with file bases — the two do not mix. A row is a server's by
	// how its process holds the base, whatever session it is: a server's own login, its jobs, its clients.
	return arrivingService != (rowRunMode == eSERVER_MODE);
}

bool ibServiceExclusivePolicy::CanAdd(const ibSession& session, wxString& reason)
{
	if (m_registry == nullptr)
		return true;   // no registry to scan through — be permissive

	const bool arrivingService = session.Identity().m_appMode == eSERVER_MODE;
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
			if (IsInTheWay(arrivingService, snap.GetSessionApplication(i)))
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
	const bool arrivingService = runMode == eSERVER_MODE;
	try {
		// The rows in the way, by session, with the beat each was seen at. The base being opened — the opening
		// thread works for it. Every row there is another process's: this one has none yet in a base it is only
		// opening.
		struct ibRowInTheWay { ibDateTime beat; wxString started, computer, user; };
		const auto readRows = [arrivingService](std::map<wxString, ibRowInTheWay>& rows) {
			rows.clear();
			ibDatabaseQueryBuilder q;
			ibQueryResult rs = q.From(session_table)
				.Select({ wxT("session"), wxT("application"), wxT("started"), wxT("computer"), wxT("userName"), wxT("lastActive") })
				.Execute();
			while (rs.Next()) {
				if (!IsInTheWay(arrivingService, static_cast<ibRunMode>(rs.GetResultInt(wxT("application")))))
					continue;
				const ibDateTime started = rs.GetResultDate(wxT("started"));
				rows[rs.GetResultString(wxT("session"))] = ibRowInTheWay{ rs.GetResultDate(wxT("lastActive")),
					started.IsEmpty() ? wxString() : started.ToString().ToWxString(),
					rs.GetResultString(wxT("computer")), rs.GetResultString(wxT("userName")) };
			}
		};

		// ⭐ ALIVE = ITS BEAT MOVES, asked as the session-time check asks it (SettleSilentPeers), not read off one
		// snapshot. A row whose beat is already as old as the registry's one silence has no owner; a fresher one is
		// WATCHED: one that moves has an owner and refuses, one that stands still until it is that old, or goes,
		// has none. Read off one snapshot, a process killed a moment ago kept the base for the rest of the silence
		// (measured 2026-10-06: a designer killed, the server refused 40 ms later).
		const long long silence = ibSessionRegistry::GetSilentSeconds() * 1000ll;
		std::map<wxString, ibRowInTheWay> watched, seen;
		readRows(watched);
		for (;;) {
			const ibDateTime now = ibDateTime::Now();
			for (auto it = watched.begin(); it != watched.end(); ) {
				if (it->second.beat.IsEmpty() || now.ElapsedSince(it->second.beat) >= silence)
					it = watched.erase(it);
				else
					++it;
			}
			if (watched.empty())
				break;

			std::this_thread::sleep_for(ibSessionRegistry::GetHeartbeatInterval() / 2);
			readRows(seen);
			for (auto it = watched.begin(); it != watched.end(); ) {
				const auto found = seen.find(it->first);
				if (found == seen.end()) {
					it = watched.erase(it);   // gone — its owner closed, or a sweep took it
					continue;
				}
				if (found->second.beat != it->second.beat) {
					reason = DescribeRefusal(arrivingService, found->second.started, found->second.computer, found->second.user);
					return false;
				}
				++it;
			}
		}
	}
	catch (...) {
		// Not proven held — a base too old to have the columns, a read that failed. The session-time check
		// (CanAdd) still stands behind this one.
	}
	return true;
}
