#include "workerPoolHeadless.h"

#include "backend/session/session.h"   // ibSessionScope
#include "backend/backend_exception.h" // ibBackendException

#include <wx/log.h>

#include <chrono>
#include <stdexcept>

namespace {

// Tracks which session the current worker thread is leasing. Used by
// Submit to detect reentrant submission on the same session and run
// the task inline (avoids the worker-blocking-on-itself deadlock).
thread_local ibSession* tl_currentLease = nullptr;

// Idle worker self-exits after this much inactivity, unless it's one
// of the last kMinIdle survivors which stay alive for fast response
// on the next Submit.
constexpr auto       kIdleTimeout = std::chrono::seconds(30);
constexpr std::size_t kMinIdle    = 1;

// How often Stop() says out loud that it is still waiting. Not a
// deadline — the wait is unbounded by design; see Stop().
constexpr auto       kStopWaitReport = std::chrono::seconds(5);

void LogWorkerException(const wxString& location)
{
	// Reraise to identify the type without losing the original exception_ptr —
	// outer catch(...) keeps current_exception() valid across this nested
	// rethrow, so set_exception below still gets the same one.
	try { throw; }
	catch (const ibBackendException& e) {
		ibJournalWarning(wxT("session.worker"),wxT("%s: ibBackendException: %s"),
		             location, e.GetErrorDescription());
	}
	catch (const std::exception& e) {
		ibJournalWarning(wxT("session.worker"),wxT("%s: std::exception: %s"),
		             location, wxString::FromUTF8(e.what()));
	}
	catch (...) {
		ibJournalWarning(wxT("session.worker"),wxT("%s: unknown exception"), location);
	}
}

} // namespace

ibWorkerPoolHeadless::ibWorkerPoolHeadless(std::size_t maxWorkers)
	: m_maxWorkers(maxWorkers > 0 ? maxWorkers : 1)
{
	// Lazy spawn — no workers at construction. The first Submit kicks
	// the first worker into existence; load growth spawns more up to
	// m_maxWorkers; idle ones eventually self-exit via timeout.
}

ibWorkerPoolHeadless::~ibWorkerPoolHeadless()
{
	Stop();
}

void ibWorkerPoolHeadless::TrySpawnWorker()
{
	std::lock_guard<std::mutex> lk(m_workersMtx);
	// Re-check inside the lock so two concurrent Submits don't both
	// spawn past the cap.
	if (m_aliveWorkers.load(std::memory_order_acquire) >= m_maxWorkers)
		return;
	if (m_stop.load(std::memory_order_acquire))
		return;
	m_aliveWorkers.fetch_add(1, std::memory_order_acq_rel);
	// Detached: the thread takes care of its own lifetime; Stop() waits
	// on m_stopCv until m_aliveWorkers reaches 0. Avoids tracking handles
	// in a vector that has to be cleaned up when workers self-exit.
	std::thread(&ibWorkerPoolHeadless::WorkerLoop, this).detach();
}

std::future<void> ibWorkerPoolHeadless::Submit(ibSession* session, Task task)
{
	auto promise = std::make_shared<std::promise<void>>();
	auto future  = promise->get_future();

	// Pool already stopped — reject submission with a ready exception
	// future rather than enqueuing a task no one will ever run.
	if (m_stop.load(std::memory_order_acquire)) {
		promise->set_exception(std::make_exception_ptr(
			std::runtime_error("worker pool is stopped")));
		return future;
	}

	// Reentrant Submit on the same session running on this worker —
	// run inline. The worker is already leasing this session and would
	// otherwise wait forever for itself to release.
	if (tl_currentLease != nullptr && tl_currentLease == session) {
		try {
			task();
			promise->set_value();
		}
		catch (...) {
			LogWorkerException(wxT("worker pool reentrant task"));
			promise->set_exception(std::current_exception());
		}
		return future;
	}

	{
		std::unique_lock<std::mutex> lk(m_mtx);
		auto& slot = m_sessions[session];
		if (!slot) slot = std::make_unique<ibSessionQueue>();
		slot->tasks.push_back({ std::move(task), std::move(promise) });
	}
	m_cv.notify_one();

	// Lazy spawn. If no worker is currently idle and we're below the
	// cap, kick a new one into existence. m_idleWorkers is incremented
	// in WorkerLoop right before the CV wait and decremented on wake-up
	// — so "idle == 0" means every alive worker is busy on a session.
	if (m_idleWorkers.load(std::memory_order_acquire) == 0
	 && m_aliveWorkers.load(std::memory_order_acquire) < m_maxWorkers) {
		TrySpawnWorker();
	}

	return future;
}

void ibWorkerPoolHeadless::DropSession(ibSession* session)
{
	std::unique_lock<std::mutex> lk(m_mtx);
	auto it = m_sessions.find(session);
	if (it == m_sessions.end())
		return;
	// A LEASED QUEUE IS NOT OURS TO ERASE — a worker is standing on it right now,
	// and the teardown that called us usually runs from inside one of its tasks.
	// Record the drop; the worker erases it when it releases the lease.
	if (it->second && it->second->leased.load(std::memory_order_acquire)) {
		it->second->dropped = true;
		return;
	}
	m_sessions.erase(it);
}

void ibWorkerPoolHeadless::CancelSession(ibSession* session)
{
	if (session == nullptr) return;
	session->RequestCancel();
	// Notify in case a worker is parked on the CV (no work for any
	// session) — wake-up gives the interpreter a chance to observe the
	// flag immediately if a script in this session is running on the
	// hot loop. The flag itself is the actual cancel signal; the notify
	// is just to shorten the latency.
	m_cv.notify_all();
}

std::pair<ibSession*, ibWorkerPoolHeadless::ibSessionQueue*>
ibWorkerPoolHeadless::ClaimSessionLocked()
{
	for (auto& kv : m_sessions) {
		ibSessionQueue* q = kv.second.get();
		if (q->tasks.empty()) continue;
		bool expected = false;
		if (q->leased.compare_exchange_strong(expected, true))
			return { kv.first, q };
	}
	return { nullptr, nullptr };
}

void ibWorkerPoolHeadless::WorkerLoop()
{
	// RAII-guard for the m_aliveWorkers decrement + Stop-cv notify.
	// Pre-2026-05-26 this bookkeeping lived at function tail; an
	// exception escaping the inner try (set_exception OOM, predicate
	// fault, ibSessionScope ctor throwing) bypassed it, the worker
	// died alive-counted, and Stop() blocked forever on the cv. Tying
	// it to a local dtor closes that hole: any path out of WorkerLoop
	// (clean exit, exception, std::terminate after std::set_terminate
	// transforms it back) walks past this and decrements once.
	struct BookkeepingOnExit {
		ibWorkerPoolHeadless* self;
		~BookkeepingOnExit() {
			self->m_aliveWorkers.fetch_sub(1, std::memory_order_acq_rel);
			std::lock_guard<std::mutex> lk(self->m_stopMtx);
			self->m_stopCv.notify_all();
		}
	} bookkeeping{ this };

	try {
	for (;;) {
		ibSession*       session = nullptr;
		ibSessionQueue*  q       = nullptr;
		bool             gotWork = false;


		{
			std::unique_lock<std::mutex> lk(m_mtx);
			m_idleWorkers.fetch_add(1, std::memory_order_acq_rel);
			gotWork = m_cv.wait_for(lk, kIdleTimeout, [this, &session, &q]() {
				if (m_stop.load(std::memory_order_acquire)) return true;
				auto pair = ClaimSessionLocked();
				session = pair.first;
				q       = pair.second;
				return q != nullptr;
			});
			m_idleWorkers.fetch_sub(1, std::memory_order_acq_rel);
		}

		if (m_stop.load(std::memory_order_acquire))
			break;


		if (!gotWork || q == nullptr) {
			// Idle timeout fired with no work waiting. Self-exit unless
			// we're among the last kMinIdle survivors — keep at least
			// one warm worker so the next Submit isn't paying the
			// thread-creation cost.
			if (m_aliveWorkers.load(std::memory_order_acquire) > kMinIdle)
				break;
			continue;
		}

		// Bind session for the duration of the lease — Current() and
		// GetPUState() resolve to this session inside every task.
		ibSessionScope scope(session);
		tl_currentLease = session;

		// A task's closure can own the very session this worker is leasing (a
		// background run holds its own session holder), so destroying it here tears
		// that session down from inside its own lease. That is DELIBERATE and it is
		// why the task is destroyed while `tl_currentLease` still names this
		// session: Teardown's drain-Submit then takes the reentrant inline path
		// instead of queueing behind itself, and the DropSession it ends with finds
		// this queue leased and defers the erase to us (see ibSessionQueue::dropped).
		while (true) {
			ibSessionTask item;
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				if (q->tasks.empty()) break;
				item = std::move(q->tasks.front());
				q->tasks.pop_front();
			}
			try {
				item.task();
				item.promise->set_value();
			}
			catch (...) {
				// Always log — PostWork drops the future, so without this
				// log the exception is silently swallowed (timer-driven
				// script bugs would mask). RunOnWorker callers will see
				// double-logging when they handle the rethrown exception
				// themselves; that's an acceptable cost for the safety win.
				LogWorkerException(wxT("worker pool task"));
				item.promise->set_exception(std::current_exception());
			}
			// item dies HERE, inside the lease — see above.
		}

		tl_currentLease = nullptr;

		// Release the lease; another worker may claim this session if new tasks
		// arrived while we were draining. And if the session was dropped while we
		// held it, WE are the one that erases the queue — the dropper could not.
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			q->leased.store(false);
			auto it = m_sessions.find(session);
			if (it != m_sessions.end() && it->second.get() == q
			    && it->second->dropped && it->second->tasks.empty())
				m_sessions.erase(it);
		}
		m_cv.notify_one();
	}
	}
	catch (...) {
		// Last-chance log of an unexpected escape from the inner loop —
		// every individual task is already wrapped above, so reaching
		// here implies a fault in the worker scaffolding itself
		// (ibSessionScope ctor, mutex lock, set_exception OOM). Log,
		// then fall through to BookkeepingOnExit. Without this catch,
		// std::terminate would fire and skip the bookkeeping dtor.
		LogWorkerException(wxT("worker pool loop"));
	}

	// m_aliveWorkers decrement + Stop-cv notify happen in BookkeepingOnExit's
	// dtor — guarantees one notify per WorkerLoop entry regardless of how
	// we leave.
}

void ibWorkerPoolHeadless::Stop()
{
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_stop.store(true);
		// RAISE CANCEL ON EVERY KNOWN SESSION. m_stop alone is only read
		// between tasks — a task already running reads nothing, and a task
		// that blocks for minutes (the Firebird maintenance poll) turns
		// this wait into a hang with no way out. The session's cancel flag
		// is the signal such a task can watch, so shutdown raises it here,
		// before waiting for anyone. Safe under m_mtx: RequestCancel is one
		// atomic store and takes no lock of its own, and the queue entry
		// pins nothing beyond the pointer we already hold.
		for (auto& kv : m_sessions)
			if (kv.first != nullptr) kv.first->RequestCancel();
	}
	m_cv.notify_all();

	// Wait for every detached worker to exit. m_aliveWorkers decrements
	// at the end of each WorkerLoop and notifies m_stopCv.
	//
	// Timed, and it keeps waiting — leaving early would let a detached
	// worker run on into session teardown, which is the use-after-free
	// this drain exists to prevent. What the deadline buys is a VOICE:
	// a wait that says nothing is indistinguishable from a deadlock, and
	// reading that difference cost a full-memory dump (2026-08-03: main
	// parked here while a worker sat in the Firebird sweep poll, which
	// was passing nullptr for its cancel token).
	std::unique_lock<std::mutex> lk(m_stopMtx);
	while (!m_stopCv.wait_for(lk, kStopWaitReport, [this]() {
		return m_aliveWorkers.load(std::memory_order_acquire) == 0;
	})) {
		ibJournalWarning(wxT("session.worker"),wxT("worker pool: still waiting on %lu worker(s) after stop"),
		             (unsigned long)m_aliveWorkers.load(std::memory_order_acquire));
	}
}

