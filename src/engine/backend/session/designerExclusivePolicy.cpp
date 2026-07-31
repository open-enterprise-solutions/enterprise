#include "designerExclusivePolicy.h"
#include "sessionRegistry.h"
#include "sessionSnapshot.h"

#include "backend/appData.h"

ibDesignerExclusivePolicy::ibDesignerExclusivePolicy(ibSessionRegistry* registry)
	: m_registry(registry)
{
}

bool ibDesignerExclusivePolicy::CanAdd(const ibSession& session, wxString& reason)
{
	// Only designer Adds are subject to the exclusion; everyone else
	// passes unconditionally.
	//
	// WHAT IS BEING ADDED, not WHERE it runs. A job's session carries the app mode of
	// the process that started it, so a scheduled or background run inside
	// designer.exe announced itself as eDESIGNER_MODE and was vetoed as "another
	// designer process". One designer per base is about DESIGNERS; the kind is what
	// says whether this is one. (A rented read never reaches here at all — it is
	// minted unlisted and never goes through the registry.)
	if (session.GetKind() != ibSessionKind::Designer)
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
	// New rule: any other designer row visible in the snapshot vetoes
	// the new designer. If the owner is dead, sweep DELETEs its stale
	// row within ~kStaleCutoffSec (≈30s); user can retry after that.
	const auto snap       = m_registry->GetClusterSnapshot();
	const wxString& ownId = session.GetId();
	for (unsigned int i = 0; i < snap.GetSessionCount(); ++i) {
		if (snap.GetSessionApplication(i) != eDESIGNER_MODE)
			continue;

		// Same distinction on the ROW side: a job started BY a designer process
		// carries that process's app mode without being a designer, so a peer's
		// scheduled run must not veto a designer starting here. An unknown kind
		// (legacy schema, back-fill missed) still counts as a designer — the safe
		// side of this particular question.
		if (IsJobSessionKind(static_cast<ibSessionKind>(snap.GetSessionKind(i))))
			continue;

		const wxString otherGuid = snap.GetSession(i);
		if (otherGuid == ownId)
			continue;

		// Another designer row visible — veto.
		reason = wxString::Format(
			_("Another designer process is already running:\n%s, %s, %s"),
			snap.GetStartedDate(i),
			snap.GetComputerName(i),
			snap.GetUserName(i));
		return false;
	}

	return true;
}
