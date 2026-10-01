#ifndef __IB_WORKER_POOL_HEADLESS_H__
#define __IB_WORKER_POOL_HEADLESS_H__

// Headless worker pool — N OS threads serving M sessions with
// per-session sequential dispatch (single in-flight task per session).
//
// Lease semantics: each session has a queue + an atomic "leased" flag.
// A worker claiming a session's queue CAS-flips leased→true, drains
// every task in FIFO order under the lease, then releases. Other
// workers see leased sessions and skip them; cross-session work
// proceeds in parallel.
//
// Reentrant Submit (a task running on session S calls Submit on the
// same session) runs inline rather than enqueuing — avoids the
// self-deadlock where a worker would queue work behind itself.
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

class BACKEND_API ibWorkerPoolHeadless : public ibWorkerPool {
public:
	// maxWorkers — hard cap on the number of OS threads the pool will
	// ever spawn concurrently. Workers spawn lazily on demand: ctor
	// creates none; the first Submit launches the first worker;
	// subsequent Submits spawn more (up to the cap) when no idle
	// worker is available. Idle workers self-exit after kIdleTimeout
	// of inactivity, except the last kMinIdle which stay alive for
	// low-latency response to the next Submit.
	explicit ibWorkerPoolHeadless(std::size_t maxWorkers);
	~ibWorkerPoolHeadless() override;

	std::future<void> Submit(ibSession* session, Task task) override;
	void              DropSession(ibSession* session) override;
	void              Stop() override;

	// Diagnostics — current worker counts. Useful for /admin endpoints
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
		// tasks — the task's closure can own the session holder — so DropSession can
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

	std::size_t              m_maxWorkers;
	std::atomic<bool>        m_stop { false };

	// Worker spawn coordination + join replacement (detached threads).
	std::mutex               m_workersMtx;
	std::atomic<std::size_t> m_aliveWorkers { 0 };
	// Idle-count drives lazy growth: zero idle + below cap = spawn.
	std::atomic<std::size_t> m_idleWorkers  { 0 };
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
