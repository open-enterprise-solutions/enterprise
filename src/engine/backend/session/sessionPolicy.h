#ifndef __IB_SESSION_POLICY_H__
#define __IB_SESSION_POLICY_H__

// ibSessionPolicy — pluggable veto for session Add.
//
// The registry iterates the policy chain on each ProcessAdd; if any
// policy returns false, the session transitions to Rejected with the
// reason string the policy filled in. The chain is strict-order (first
// registered, first consulted); first veto wins and no further policy
// runs.
//
// Typical concrete policies (subclasses):
//   - `ibDesignerExclusivePolicy` — migrates the existing
//     `VerifySessionUpdater` designer-conflict check: at most one
//     designer session across the cluster at a time.
//   - `ibLicenseLimitPolicy` (future) — at most N user sessions,
//     cluster-wide, as a licensing constraint.
//   - `ibPerUserQuotaPolicy` (future) — at most M sessions per user.
//
// The policy sees a fully-constructed ibSession (identity populated).
// It MAY consult the cluster snapshot (ibSessionRegistry::GetClusterSnapshot
// in a future commit) or the local m_own set. It MUST be fast — the
// registry thread is single-consumer, a slow policy blocks every other
// request.

#include "backend/backend.h"
#include "session.h"

#include <wx/string.h>

class BACKEND_API ibSessionPolicy {
public:
	virtual ~ibSessionPolicy() = default;

	// Return true to allow the Add; false to veto. On false, fill
	// `reason` with a user-facing diagnostic — becomes the ibSession's
	// m_reason and propagates to the producer via ibConnectResult.
	virtual bool CanAdd(const ibSession& session, ibString& reason) = 0;
};

#endif
