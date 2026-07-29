#ifndef __IB_WORKER_POOL_H__
#define __IB_WORKER_POOL_H__

// ibWorkerPool — task dispatcher with per-session sequential semantics.
//
// Submitting tasks for the same session enforces FIFO order; tasks for
// different sessions execute on different workers in parallel. The
// interpreter state (ibSession::GetPUState) is automatically visible
// to every task because workers bind their session via ibSessionScope
// before draining the queue — no explicit save/restore needed since
// state is owned by the session itself.
//
// Concrete backends:
//   - ibWorkerPoolHeadless (in this directory): N threads, per-session
//     queue + lease, blocking workers wait on a CV. Suitable for
//     wenterprise-server.exe and the future oes-server.exe compute
//     server.
//   - ibWorkerPoolGUI (frontend/session/workerPoolGUI.{h,cpp}): wraps
//     wxTheApp's CallAfter so tasks run on the wx main thread. Implemented,
//     but NOT auto-installed — the desktop still runs script on the wx main
//     thread directly. The session arg is unused (desktop = one session).
//
// See docs/worker-pool-tls-audit.md (TLS migration prerequisite — done)
// and docs/compute-server-tiering.md (architectural roadmap).

#include "backend/backend.h"

#include <functional>
#include <future>
#include <memory>

class ibSession;

class BACKEND_API ibWorkerPool {
public:
	using Task = std::function<void()>;

	virtual ~ibWorkerPool() = default;

	// Schedule a task to run with `session` bound on the worker thread.
	// Returns a future that fulfils after the task runs or holds the
	// exception thrown by the task. Per-session order is FIFO; tasks
	// for distinct sessions run in parallel up to the worker count.
	virtual std::future<void> Submit(ibSession* session, Task task) = 0;

	// Convenience: submit + wait. Throws if the task threw.
	void RunOnSession(ibSession* session, Task task) {
		Submit(session, std::move(task)).get();
	}

	// Remove a session's queue and any internal bookkeeping the pool
	// holds for it. Caller must guarantee no in-flight tasks remain
	// (typical pattern: drain via RunOnSession, then DropSession). Used
	// at session teardown so the pool's per-session map doesn't leak
	// stale entries pointing at destroyed sessions.
	virtual void DropSession(ibSession* session) = 0;

	// Async cancel of whatever task is currently running for `session`.
	// Sets ibSession::RequestCancel and wakes any worker parked on
	// the dispatch CV; the interpreter (ibProcUnit::Execute) sees the
	// flag at its next loop-boundary check and throws
	// ibBackendInterruptException, which unwinds out of the task.
	// Tasks not in the interpreter (e.g. blocking on a socket read)
	// won't notice the flag — cancellation is cooperative, not preemptive.
	virtual void CancelSession(ibSession* session) = 0;

	// Drain queues, signal workers to stop, join. Idempotent. Pending
	// tasks at the time of Stop run to completion before workers exit
	// — the pool acts as an actor-system shutdown, not a force-kill.
	virtual void Stop() = 0;
};

#endif
