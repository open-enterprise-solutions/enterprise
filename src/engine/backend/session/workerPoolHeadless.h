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

		// How many Await of this session are on the stack — a question asked from a task run under another
		// question nests; only the innermost is awake. `wake` is what a submit, an answer or a cancel rings.
		// Both under m_mtx. The fiber does not block on `wake`: the home thread is back in the pool's own
		// wait, and `m_wake` is the flag that wait looks at. The condition variable stays so a caller that
		// already notified it keeps doing so.
		int                       waiting { 0 };
		std::condition_variable   wake;

		// The lease fiber, pinned to m_home. Non-null from the moment the
		// fiber is created until it has unwound and the home thread has
		// destroyed it. m_parked and m_wake are under m_mtx. m_fiber and
		// m_home are touched only on the home thread.
		class ibFiber*            m_fiber = nullptr;
		std::thread::id           m_home{};
		bool                      m_parked = false;
		bool                      m_wake = false;
	};

	void WorkerLoop();

	// Spawn a new detached worker thread. Re-checks alive-vs-cap under
	// m_workersMtx to handle the race between two threads racing to
	// spawn; bumps m_aliveWorkers atomically before std::thread::detach.
	void TrySpawnWorker();

	// Find a session with pending tasks not currently leased and CAS
	// the lease in. Returns the session pointer + queue, or {nullptr,
	// nullptr} if no work is available. Must be called with m_mtx held.
	std::pair<ibSession*, ibSessionQueue*> ClaimSessionLocked();

	// A fiber parked on this worker. The vector is per OS thread: fibers
	// never migrate, so the scheduler on the home thread is the only
	// reader and the only writer.
	struct ibParked {
		ibSession*       session = nullptr;
		ibSessionQueue*  queue = nullptr;
		class ibFiber*   fiber = nullptr;
	};
	static thread_local std::vector<ibParked> tl_parked;
	static thread_local ibSessionQueue* tl_currentQueue;

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
	void        FinishFiber(ibSession* session, ibSessionQueue* q, class ibFiber* fiber);
	bool        ShouldInterrupt(ibSession* session) const;
	// m_mtx must be held. True when a fiber parked on THIS thread should
	// be resumed: it was woken, it has queued tasks, the pool is
	// stopping, or its session was cancelled.
	bool        HasRunnableParkedLocked() const;

	std::size_t              m_maxWorkers;
	std::atomic<bool>        m_stop { false };

	// Worker spawn coordination + join replacement (detached threads).
	std::mutex               m_workersMtx;
	std::atomic<std::size_t> m_aliveWorkers { 0 };
	// Idle-count drives lazy growth: zero idle + below cap = spawn.
	std::atomic<std::size_t> m_idleWorkers  { 0 };
	// ⭐ LEFT AT ZERO. A question used to block the OS thread, so a worker in Await was subtracted from the
	// cap and another thread was started in its place — otherwise two open questions stopped a pool of two.
	// The question is now a fiber: the thread goes back to the scheduler and serves other sessions itself,
	// and raising the cap would spend a thread per question, which is the thing this exists to stop. The
	// term stays in the formula so the old arithmetic is still visible, and Await does not touch it.
	std::atomic<std::size_t> m_waitingWorkers { 0 };
	// Stop() waits on this until m_aliveWorkers reaches 0 (every
	// detached worker has exited).
	std::mutex               m_stopMtx;
	std::condition_variable  m_stopCv;

	// Per-session queue + dispatch.
	mutable std::mutex                                                m_mtx;
	std::condition_variable                                           m_cv;
	std::unordered_map<ibSession*, std::unique_ptr<ibSessionQueue>>   m_sessions;

};

#endif
