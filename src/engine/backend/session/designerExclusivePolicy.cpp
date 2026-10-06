#include "designerExclusivePolicy.h"
#include "sessionRegistry.h"
#include "sessionSnapshot.h"

#include "backend/appData.h"
#include "backend/diagnostics/journal.h"   // the veto is written down, not only shown

ibDesignerExclusivePolicy::ibDesignerExclusivePolicy(ibSessionRegistry* registry)
	: m_registry(registry)
{
}

bool ibDesignerExclusivePolicy::CanAdd(const ibSession& session, wxString& reason)
{
	// Only designer Adds are subject to the exclusion; everyone else
	// passes unconditionally.
	//
	// WHAT IS BEING ADDED, not WHERE it runs. A job's session carries the run mode of
	// the process that started it, so a scheduled or background run inside
	// designer.exe announced itself as a designer and was vetoed as "another
	// designer process". One designer per base is about DESIGNERS; the kind is what
	// says whether this is one. (A rented read never reaches here at all — it is
	// minted unlisted and never goes through the registry.)
	if (!IsDesignerSessionKind(session.GetKind()))
		return true;

	if (m_registry == nullptr)
		return true;   // no registry to scan through — be permissive

	// Scan the cluster snapshot for other designer rows. Liveness model
	// is heartbeat-based now (sys_session.lastActive updated each refresh
	// tick by the owner; sweep deletes rows whose lastActive is older
	// than kStaleCutoffSec). The historical TryProbeRowLock probe was
	// dropped from the registry's own bookkeeping and produces false
	// positives here — no row lock is ever held, so the probe always
	// succeeds and policy treats every live designer as a zombie.
	//
	// Rule: any other designer row visible in the snapshot vetoes the new designer.
	const wxString& ownId = session.GetId();
	const auto peerDesigners = [&ownId](const ibSessionSnapshot& snap) {
		std::vector<unsigned int> rows;
		for (unsigned int i = 0; i < snap.GetSessionCount(); ++i) {
			// A DESIGNER BY ITS KIND — a file base's or a thin one in a server alike; a job started BY a designer
			// process is not one, so a peer's scheduled run does not veto a designer starting here. A row whose
			// kind says nothing (legacy schema, back-fill missed) is a designer by the run mode such a row carried,
			// the designer's 2 — the safe side of this particular question.
			constexpr int kLegacyDesignerRunMode = 2;
			const ibSessionKind kind = static_cast<ibSessionKind>(snap.GetSessionKind(i));
			const bool legacyDesigner = kind == ibSessionKind::Unknown
				&& static_cast<int>(snap.GetSessionApplication(i)) == kLegacyDesignerRunMode;
			if (!IsDesignerSessionKind(kind) && !legacyDesigner)
				continue;

			if (snap.GetSession(i) == ownId)
				continue;
			rows.push_back(i);
		}
		return rows;
	};

	ibSessionSnapshot snap = m_registry->GetClusterSnapshot();
	std::vector<unsigned int> peers = peerDesigners(snap);

	// ⭐ …UNLESS IT DOES NOT BREATHE. A designer killed a moment ago leaves its row until the sweep's cutoff
	// passes, and every start in that window was refused by a designer that was not there — the fix was to
	// wait and retry by hand. Its row stops moving the moment it dies, so a few heartbeats settle it
	// (SettleSilentPeers): a row that moved is a designer at work and still vetoes; one that did not is
	// removed, and the start goes on.
	if (!peers.empty()) {
		std::vector<wxString> guids;
		for (const unsigned int i : peers)
			guids.push_back(snap.GetSession(i));
		if (m_registry->SettleSilentPeers(guids) > 0) {
			snap = m_registry->GetClusterSnapshot();
			peers = peerDesigners(snap);
		}
	}

	for (const unsigned int i : peers) {
		// Another designer row visible, and alive — veto.
		reason = wxString::Format(
			_("Another designer process is already running:\n%s, %s, %s"),
			snap.GetStartedDate(i),
			snap.GetComputerName(i),
			snap.GetUserName(i));

		// ⚠ AND WRITTEN DOWN, not only shown. This veto stops the process before there is a window
		// to explain it in, and the only account of it was a modal box — which needs somebody
		// sitting in front of the screen to read and dismiss it. A start driven by anything else —
		// a script, a tool, a service, a rebuild-and-relaunch — sees nothing but "it did not come
		// up", and the previous designer still closing is indistinguishable from a crash. The box
		// stays for the person; the line is for whoever reads afterwards, and it names WHICH peer.
		// ⚠ AT INFO, because a line for the file is what Info is (journal.h): a Warning echoes as a
		// dialog of its own, and it stood beside the box as a SECOND window for the same refusal (2026-09-11).
		ibJournalInfo(wxT("session"), wxT("designer refused to start - another designer is ")
			wxT("running: started %s, computer %s, user %s"),
			snap.GetStartedDate(i), snap.GetComputerName(i), snap.GetUserName(i));

		return false;
	}

	return true;
}
