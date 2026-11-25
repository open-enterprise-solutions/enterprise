#include "studioExclusivePolicy.h"
#include "sessionRegistry.h"
#include "sessionSnapshot.h"

#include "backend/appData.h"

ibStudioExclusivePolicy::ibStudioExclusivePolicy(ibSessionRegistry* registry)
	: m_registry(registry)
{
}

bool ibStudioExclusivePolicy::CanAdd(const ibSession& session, ibString& reason)
{
	// Only studio Adds are subject to the exclusion; everyone else
	// passes unconditionally.
	//
	// WHAT IS BEING ADDED, not WHERE it runs. A job's session carries the run mode of
	// the process that started it, so a scheduled or background run inside
	// designer.exe announced itself as a studio and was vetoed as "another
	// studio process". One studio per base is about STUDIOS; the kind is what
	// says whether this is one. (A rented read never reaches here at all — it is
	// minted unlisted and never goes through the registry.)
	if (session.GetKind() != ibSessionKind::Studio)
		return true;

	if (m_registry == nullptr)
		return true;   // no registry to scan through — be permissive

	// Scan the cluster snapshot for other studio rows. Liveness model
	// is heartbeat-based now (sys_session.lastActive updated each refresh
	// tick by the owner; sweep deletes rows whose lastActive is older
	// than kStaleCutoffSec). The historical TryProbeRowLock probe was
	// dropped from the registry's own bookkeeping and produces false
	// positives here — no row lock is ever held, so the probe always
	// succeeds and policy treats every live studio as a zombie.
	//
	// Rule: any other studio row visible in the snapshot vetoes the new studio.
	const ibString& ownId = session.GetId();
	const auto peerStudios = [&ownId](const ibSessionSnapshot& snap) {
		std::vector<unsigned int> rows;
		for (unsigned int i = 0; i < snap.GetSessionCount(); ++i) {
			// A STUDIO BY ITS KIND — a file base's or a thin one in a server alike; a job started BY a studio
			// process is not one, so a peer's scheduled run does not veto a studio starting here.
			if (static_cast<ibSessionKind>(snap.GetSessionKind(i)) != ibSessionKind::Studio)
				continue;

			if (snap.GetSession(i) == ownId)
				continue;
			rows.push_back(i);
		}
		return rows;
	};

	ibSessionSnapshot snap = m_registry->GetClusterSnapshot();
	std::vector<unsigned int> peers = peerStudios(snap);

	// ⭐ …UNLESS IT DOES NOT BREATHE. A studio killed a moment ago leaves its row until the sweep's cutoff
	// passes, and every start in that window was refused by a studio that was not there — the fix was to
	// wait and retry by hand. Its row stops moving the moment it dies, so a few heartbeats settle it
	// (SettleSilentPeers): a row that moved is a studio at work and still vetoes; one that did not is
	// removed, and the start goes on.
	if (!peers.empty()) {
		std::vector<ibString> guids;
		for (const unsigned int i : peers)
			guids.push_back(snap.GetSession(i));
		if (m_registry->SettleSilentPeers(guids) > 0) {
			snap = m_registry->GetClusterSnapshot();
			peers = peerStudios(snap);
		}
	}

	for (const unsigned int i : peers) {
		// Another studio row visible, and alive — veto.
		reason = ibString::Format(
			_("Another studio process is already running:\n%s, %s, %s"),
			snap.GetStartedDate(i),
			snap.GetComputerName(i),
			snap.GetUserName(i));

		// ⚠ AND WRITTEN DOWN, not only shown. This veto stops the process before there is a window
		// to explain it in, and the only account of it was a modal box — which needs somebody
		// sitting in front of the screen to read and dismiss it. A start driven by anything else —
		// a script, a tool, a service, a rebuild-and-relaunch — sees nothing but "it did not come
		// up", and the previous studio still closing is indistinguishable from a crash. The box
		// stays for the person; the line is for whoever reads afterwards, and it names WHICH peer.
		// ⚠ AT INFO, because a line for the file is what Info is (journal.h): a Warning echoes as a
		// dialog of its own, and it stood beside the box as a SECOND window for the same refusal (2026-09-11).
		ibJournalInfo(ibT("session"), ibT("studio refused to start - another studio is ")
			ibT("running: started %s, computer %s, user %s"),
			snap.GetStartedDate(i), snap.GetComputerName(i), snap.GetUserName(i));

		return false;
	}

	return true;
}
