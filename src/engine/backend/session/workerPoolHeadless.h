#ifndef __IB_WORKER_POOL_HEADLESS_H__
#define __IB_WORKER_POOL_HEADLESS_H__

// Headless worker pool — N OS threads serving M sessions with
// per-session sequential dispatch (single in-flight task per session).
//
// Lease semantics: each session has a queue + an atomic "leased" flag.
// A worker claiming a session's queue CAS-flips leased→true and runs
// the lease on a fiber pinned to that worker. Tasks drain in FIFO
// order on that fiber. Await suspends the fiber and returns the OS
// thread to the scheduler; the lease stays held, so no other worker
// picks the session up. Wake, a queued task, or cancel resumes the
// fiber on its home thread. Other sessions proceed in parallel.
//
// Reentrant Submit (a task running on session S calls Submit on the
// same session) runs inline rather than enqueuing — avoids the
// self-deadlock where a worker would queue work behind itself. A
// parked fiber has saved its lease, so a submit from another session
// on the same OS thread does not look reentrant.
//
// Used by wenterprise-server.exe (replaces today's per-session worker
// thread in ibWebApplication) and the future oes-server.exe compute
// server. See docs/private/compute-server-tiering.md Phase 2 for the bigger
// picture.

#include "workerPool.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>   // weak_ptr — a queue holds its session the way a stranger does
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

class ibFiber;

class BACKEND_API ibWorkerPoolHeadless : public ibWorkerPool {
public:
	// maxWorkers — hard cap on the number of OS threads the pool will
	// ever spawn concurrently; 0 — no cap. Workers spawn lazily on demand: ctor
	// creates none; the first Submit launches the first worker;
	// subsequent Submits spawn more (up to the cap) when no idle
	// worker is available. Idle workers self-exit after kIdleTimeout
	// of inactivity, except the last kMinIdle which stay alive for
	// low-latency response to the next Submit.
	explicit ibWorkerPoolHeadless(std::size_t maxWorkers);
	~ibWorkerPoolHeadless() override;

	std::future<void> Submit(ibSession* session, Task task) override;
	void              Await(ibSession* session, const std::function<bool()>& done) override;
	void              Wake(ibSession* session) override;
	void              Drop(ibSession* session) override;
	void              Stop() override;

	// Diagnostics — current worker counts (the cap 0 — none). Useful for /admin endpoints
	// and load tests.
	std::size_t MaxWorkers()   const { return m_maxWorkers; }
	std::size_t AliveWorkers() const { return m_aliveWorkers.load(std::memory_order_acquire); }
	std::size_t IdleWorkers()  const { return m_idleWorkers.load(std::memory_order_acquire); }

private:
	struct ibSessionTask {
		Task                                task;
		std::shared_ptr<std::promise<void>> promise;
	};

	struct ibSessionQueue {
		std::deque<ibSessionTask> tasks;
		std::atomic<bool>         leased { false };
		// DROPPED WHILE LEASED. A session's teardown runs from inside one of its own
		// tasks — the task's closure can own the session holder — so Drop can
		// arrive while a worker is standing on this very object. Erasing it there is
		// a use-after-free under the pool's own mutex. So the drop is RECORDED here
		// and the worker erases the queue itself when it lets the lease go.
		bool                      dropped { false };

		// ⭐⭐ WHO THIS QUEUE BELONGS TO, ASKED RATHER THAN ASSUMED. The map's KEY is a bare
		// ibSession* and it must stay one — a queue is found by address — but a key says nothing
		// about whether the thing at that address is still there. A session is not ours: it can end
		// without telling us (a pool declared BEFORE its sessions outlives them, which is the order
		// every scope gives by default), and then the map names freed memory. Legal to look up,
		// a use-after-free to call through — AddressSanitizer in ibWorkerPoolHeadless::Stop, from
		// the pool's own destructor, 2026-09-24, and once before that on 2026-09-22, when it was
		// answered with `dropped` alone. `dropped` says the session LET GO; this says it is ALIVE,
		// and only the second one can be asked of a session that told us nothing.
		std::weak_ptr<ibSession>  owner;
		// Set once a submit's weak hold actually locked. An empty hold is a session
		// that was never shared — its caller keeps it alive, and cancel reaches the
		// fiber through Wake. An owned hold that no longer locks means the session
		// has ended, and the fiber must be resumed so it can unwind.
		bool                      owned { false };

		// How many Await of this session are on the stack — a question asked from a task run under another
		// question nests; only the innermost is awake. `woken` is what a submit, an answer or a cancel sets: the
		// waiting fiber is parked and its home thread is in the pool's own wait, which reads this flag. Both
		// under m_mtx.
		int                       waiting { 0 };
		bool                      woken { false };
	};

	void WorkerLoop();

	// Spawn a worker. Re-checks alive-vs-cap under m_workersMtx so two
	// Submits cannot both pass the cap. m_aliveWorkers is bumped before
	// the thread exists. The handle is kept: Stop joins it, and an idle
	// self-exit is joined on the next spawn. A detached thread would
	// still be inside thread_local destructors after m_aliveWorkers hit
	// zero, which is after Stop used to return.
	void TrySpawnWorker();

	// m_workersMtx must be held. Moves out handles to join outside the
	// lock (a thread_local destructor can call back into the pool).
	// exitedOnly takes workers whose WorkerLoop has already returned.
	std::vector<std::thread> TakeWorkersLocked(bool exitedOnly);

	// Find a session with pending tasks not currently leased and CAS
	// the lease in. Returns the session pointer + queue, or {nullptr,
	// nullptr} if no work is available. Must be called with m_mtx held.
	std::pair<ibSession*, ibSessionQueue*> ClaimSessionLocked();

	// A fiber parked on this worker. `session` is the map key FinishFiber
	// looks the queue up by. It is not asked whether the session is still
	// there: that question goes through the queue's weak hold.
	struct ibParked {
		ibSession*       session = nullptr;
		ibSessionQueue*  queue = nullptr;
		ibFiber*         fiber = nullptr;
	};
	// The fibers parked on the CALLING worker. Per OS thread — fibers never migrate, so the home thread's scheduler is
	// the only reader and the only writer. A function and not a `static thread_local` member: an exported class may not
	// have one (MSVC C2492; procUnit.h says the same of the interpreter's state).
	static std::vector<ibParked>& ParkedFibers();

	static void RunTask(ibSessionTask& item);
	static void RegisterFiberLocals();

	// Passed across the fiber entry. A nested type so the translation
	// unit can name the queue (private) without a friend.
	struct ibLeaseArgs {
		ibWorkerPoolHeadless* pool = nullptr;
		ibSession*            session = nullptr;
		ibSessionQueue*       queue = nullptr;
	};
	bool TakeRunnable(ibParked& out);
	static void LeaseEntry(void* raw);
	void        DrainLease(ibSessionQueue* q);
	void        StartLease(ibSession* session, ibSessionQueue* q);
	void        FinishFiber(ibSession* session, ibSessionQueue* q, ibFiber* fiber);
	bool        ShouldInterrupt(ibSession* session) const;
	// m_mtx must be held. True when this parked queue should be resumed:
	// woken, work queued, the pool stopping, the session cancelled, or
	// the session gone. Cancel and "gone" are asked of the weak hold,
	// never of the raw map key.
	bool        ParkedShouldRunLocked(ibSessionQueue* q) const;
	// m_mtx must be held. True when a fiber parked on THIS thread should
	// be resumed.
	bool        HasRunnableParkedLocked() const;

	std::size_t              m_maxWorkers;
	std::atomic<bool>        m_stop { false };

	// Handles live until joined. m_aliveWorkers hits zero when WorkerLoop
	// returns; the thread itself is still running thread_local
	// destructors until join returns.
	struct ibWorker {
		std::shared_ptr<std::atomic<bool>> exited;
		std::thread                        thread;
	};
	std::mutex               m_workersMtx;
	std::vector<ibWorker>    m_workers;
	std::atomic<std::size_t> m_aliveWorkers { 0 };
	// Idle-count drives lazy growth: zero idle + below cap = spawn.
	std::atomic<std::size_t> m_idleWorkers  { 0 };
	// Stop waits here until every WorkerLoop has returned, then joins
	// the handles so thread_local destructors have run too.
	std::mutex               m_stopMtx;
	std::condition_variable  m_stopCv;

	// Per-session queue + dispatch.
	mutable std::mutex                                                m_mtx;
	std::condition_variable                                           m_cv;
	std::unordered_map<ibSession*, std::unique_ptr<ibSessionQueue>>   m_sessions;

};

#endif
