#ifndef __IB_DESIGNER_EXCLUSIVE_POLICY_H__
#define __IB_DESIGNER_EXCLUSIVE_POLICY_H__

// Port of the legacy `ibApplicationDataSessionUpdater::VerifySessionUpdater`
// designer-exclusion check into the new ibSessionPolicy framework.
//
// Rule: at most one live designer session per DB cluster. Any second
// designer that tries to Add is Rejected with a message naming the
// living one. "Alive" is decided by row-lock probe — if another
// sys_session row of a designer kind is visible in the
// cluster snapshot AND a `TryProbeRowLock` on it fails, the owner is
// still holding the lock and we reject. If the probe succeeds the row
// is a zombie (owner crashed / was killed); the sweep tick will delete
// it on the next pass, and we let the new designer proceed.
//
// This replaces the pre-2026-04-20 grace-retry loop (sleep for
// timeInterval+2 seconds so a zombie row could age past the cutoff and
// ClearLost could delete it). Row-lock probes are instantaneous —
// aliveness is either true right now or not.

#include "backend/backend.h"
#include "sessionPolicy.h"

class ibSessionRegistry;

class BACKEND_API ibDesignerExclusivePolicy : public ibSessionPolicy {
public:
	explicit ibDesignerExclusivePolicy(ibSessionRegistry* registry);

	bool CanAdd(const ibSession& session, wxString& reason) override;

private:
	ibSessionRegistry* m_registry;
};

#endif
