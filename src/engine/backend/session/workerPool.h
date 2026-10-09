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
//     wxTheApp's CallAfter so tasks run on the wx main thread. Installed by
//     ibGUISession; a task submitted on the main thread runs inline, so the
//     desktop's script stays where it always was.
//
// See docs/private/worker-pool-tls-audit.md (TLS migration prerequisite — done)
// and docs/private/compute-server-tiering.md (architectural roadmap).

#include "backend/backend.h"

#include <functional>
#include <future>
#include <memory>

class ibSession;

class BACKEND_API ibWorkerPool {
public:
	using Task = std::function<void()>;

	virtual ~ibWorkerPool() = default;

	// Convenience: submit + wait. Throws if the task threw.
	void Execute(ibSession* session, Task task) {
		Submit(session, std::move(task)).get();
	}

	// Schedule a task to run with `session` bound on the worker thread.
	// Returns a future that fulfils after the task runs or holds the
	// exception thrown by the task — and nobody else says it: a caller
	// that does not wait for its task catches inside the task. Per-session
	// order is FIFO; tasks for distinct sessions run in parallel up to the
	// worker count.
	virtual std::future<void> Submit(ibSession* session, Task task) = 0;

	// ⭐ AWAIT `done`, RUNNING THE SESSION'S WORK MEANWHILE — how a script waits for its client: a question, later
	// a modal form or a client service. Called from a task of `session`, on the thread that holds it — the only
	// thread that may run the session's work, so the one that goes on running it while the script waits: the
	// tasks that arrive are run here, inline, in order, the way wx's nested loop runs events under wxMessageBox.
	// Returns once `done` says yes; throws the interruption (ibBackendInterruptException) when the session is
	// cancelled or the pool stops — the cancel is asked BEFORE each task, so a teardown's barrier, submitted
	// after its cancel, never runs under the script it waits out.
	//
	// Anywhere else — a thread that does not hold the session, a task of ANOTHER session — it refuses
	// (std::logic_error): the waiting thread is the one that runs the session's work, or nobody does.
	//
	// `done` is asked under the pool's lock: a flag read, nothing that calls back into the pool. Whoever
	// makes it true calls Wake.
	virtual void Await(ibSession* session, const std::function<bool()>& done) = 0;

	// What an Await of `session` waits for may have changed — its answer arrived, or the session was
	// cancelled. Any thread. Nobody waiting — nothing to do.
	virtual void Wake(ibSession* session) = 0;

	// Remove a session's queue and any internal bookkeeping the pool
	// holds for it. Caller must guarantee no in-flight tasks remain
	// (typical pattern: drain via Execute, then Drop). Used
	// at session teardown so the pool's per-session map doesn't leak
	// stale entries pointing at destroyed sessions.
	virtual void Drop(ibSession* session) = 0;

	// (No cancel here. Stopping what a session is doing is the session's own command — ibSession::Cancel —
	//  because the session is what knows everything that is doing it: its thread, its connection, its tenants.
	//  A pool only runs tasks.)

	// Drain queues, signal workers to stop, join. Idempotent. Pending
	// tasks at the time of Stop run to completion before workers exit
	// — the pool acts as an actor-system shutdown, not a force-kill.
	virtual void Stop() = 0;
};

#endif
