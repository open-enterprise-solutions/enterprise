#ifndef __IB_WORKER_POOL_GUI_H__
#define __IB_WORKER_POOL_GUI_H__

// GUI worker pool — wraps wxTheApp::CallAfter so tasks Submitted from
// any thread eventually run on the wx main thread (the only thread
// that may touch wx widgets). One logical "worker" = the wx main
// thread; tasks queue on wx's idle event handler in arrival order.
//
// Use case: a script or background thread on a desktop GUI host wants
// to dispatch heavy work without freezing the UI — Submit lets wx
// drain the task between input events. The headless pool's role
// (N threads serving M sessions) doesn't apply on GUI: there's only
// one session and one UI thread.
//
// Installed: ibGUISession holds one and returns it from GetWorkerPool().
// Desktop script still runs inline on the calling thread, because Submit
// runs a task INLINE when the caller is already on the wx main thread —
// which every existing desktop caller is. The CallAfter path therefore
// serves exactly one case, the one that had no answer before: a result
// arriving from a background thread (a list / report read that went off
// to the registry's pool) being handed back to the session that asked.
//
// The registry's own pool is NOT the place for it: that one serves the
// background and scheduled sessions of the process, and their whole
// point is to stay OFF the UI thread. Two different questions, two
// different answers — which is why the choice lives on the session.

#include "frontend/frontend.h"
#include "backend/session/workerPool.h"

#include <atomic>

class FRONTEND_API ibWorkerPoolGUI : public ibWorkerPool {
public:
	ibWorkerPoolGUI()  = default;
	~ibWorkerPoolGUI() override = default;

	std::future<void> Submit(ibSession* session, Task task) override;
	void              DropSession(ibSession* session) override;
	void              CancelSession(ibSession* session) override;
	void              Stop() override;

private:
	std::atomic<bool> m_stop { false };
};

#endif
