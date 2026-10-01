#ifndef __IB_SERVICE_EXCLUSIVE_POLICY_H__
#define __IB_SERVICE_EXCLUSIVE_POLICY_H__

// A BASE SERVED BY APPLICATION SERVERS IS THEIRS (docs/private/multi-base-process.md § 5.2).
//
// Nobody attaches to a served base from the side — a designer opening a file base's directory directly, a
// client started against it; and several application servers may serve one base together: what they share
// lives in the base already (sys_session, sys_lock, a job's claim and its clock in sys_job). NOT the
// per-session exclusive mode: that one wants the SOLE live session, the process's own included, and would
// lock the server out of its own jobs. This is a rule over the PROCESSES in the cluster snapshot:
//
//   - a Service session (a server's row in the base) is refused while a session of another process that is
//     NOT a server is alive — servers share a base with servers;
//   - any other session is refused while a Service session of another process is alive.
//
// Sessions of the process itself — the server's jobs now, its clients later — pass: the registry knows them
// as its own. A row that stopped beating (a server killed) is settled first (SettleSilentPeers), as the
// designer rule does, so a dead server does not keep its base.
//
// The twin of ibDesignerExclusivePolicy, and registered beside it — in EVERY process, because the refusal
// has to happen in the one that tries to come in.

#include "backend/backend.h"
#include "backend/appData.h"   // ibRunMode, ibSessionKind
#include "sessionPolicy.h"

class ibSessionRegistry;

class BACKEND_API ibServiceExclusivePolicy : public ibSessionPolicy {
public:
	explicit ibServiceExclusivePolicy(ibSessionRegistry* registry);

	bool CanAdd(const ibSession& session, wxString& reason) override;

	// ⭐ ASKED AT OPEN — before the opening process writes anything into the base: its tables, its jobs, its
	// sweep of sys_session. The same rule CanAdd applies to a session, over the rows alive now (their beat
	// within the registry's silence); this process has none in a base it is only opening. False, with the
	// refusal, when the base is held. A base whose sessions cannot be read is not proven held — the
	// session-time check still stands behind this one.
	static bool CanOpen(ibRunMode runMode, wxString& reason);

private:
	// THE RULE, once: a row of another process is in the way when exactly one of the two is a Service session —
	// servers share a base with servers, clients with clients, the two do not mix.
	static bool IsInTheWay(bool arrivingService, ibSessionKind rowKind);

	ibSessionRegistry* m_registry;
};

#endif
