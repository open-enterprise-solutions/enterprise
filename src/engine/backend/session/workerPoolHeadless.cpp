#include "workerPoolHeadless.h"

#include "fiber.h"

#include "backend/session/session.h"   // ibSessionScope
#include "backend/backend_exception.h" // ibBackendException

#include <wx/log.h>

#include <chrono>
#include <stdexcept>

namespace {

// Which session this fiber is leasing. Submit uses it to spot a
// reentrant call and run that task inline. It is fiber-scoped: while
// this fiber is parked the thread runs someone else, and the value has
// to come back as it was when the fiber resumes.
thread_local ibSession* tl_currentLease = nullptr;

// Idle worker self-exits after this much inactivity, unless it's one
// of the last kMinIdle survivors which stay alive for fast response
// on the next Submit. A worker with a parked fiber is not idle in the
// sense that matters for shutdown: it must stay to resume that fiber.
constexpr auto       kIdleTimeout = std::chrono::seconds(30);
constexpr std::size_t kMinIdle    = 1;

// How often Stop() says out loud that it is still waiting. Not a
// deadline — the wait is unbounded by design; see Stop().
constexpr auto       kStopWaitReport = std::chrono::seconds(5);

// A cancel lives on the session, not on this condition variable, so a
// worker blocked in the wait does not hear it until something notifies.
// Callers that cancel a question also Wake (the frame destructors). A
// cancel that does not is still observed: while a fiber is parked the
// wait is short enough that the script unwinds. A person thinking is
// not on this timescale.
constexpr auto       kCancelSlice = std::chrono::milliseconds(50);

// What the POOL failed at — its own loop, not a task (a task's exception goes to its future; see RunTask).
void LogWorkerException(const wxString& location)
{
	// Reraise to identify the type — the outer catch(...) keeps the exception alive across this nested rethrow.
	try { throw; }
	catch (const ibCoreException& e) {
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

thread_local std::vector<ibWorkerPoolHeadless::ibParked> ibWorkerPoolHeadless::tl_parked;
thread_local ibWorkerPoolHeadless::ibSessionQueue* ibWorkerPoolHeadless::tl_currentQueue = nullptr;

void ibWorkerPoolHeadless::RegisterFiberLocals()
{
	// Once per process, and before any fiber snapshot. The lambdas sit
	// directly in this member so they can name the private queue type;
	// a lambda nested in another lambda would not. The other slots
	// register themselves at static init, which has already run by the
	// time a pool exists.
	static std::mutex gate;
	static std::atomic<bool> done{ false };
	if (done.load(std::memory_order_acquire))
		return;
	std::lock_guard<std::mutex> lk(gate);
	if (done.load(std::memory_order_relaxed))
		return;
	ibFiberLocals::RegisterTrivial<ibSession*>(
		[](void* dst) { *static_cast<ibSession**>(dst) = tl_currentLease; },
		[](const void* src) { tl_currentLease = *static_cast<ibSession* const*>(src); });
	ibFiberLocals::RegisterTrivial<ibSessionQueue*>(
		[](void* dst) { *static_cast<ibSessionQueue**>(dst) = tl_currentQueue; },
		[](const void* src) { tl_currentQueue = *static_cast<ibSessionQueue* const*>(src); });
	done.store(true, std::memory_order_release);
}

ibWorkerPoolHeadless::ibWorkerPoolHeadless(std::size_t maxWorkers)
	: m_maxWorkers(maxWorkers)
{
	RegisterFiberLocals();
	// Lazy spawn — no workers at construction. The first Submit kicks
	// the first worker into existence; load growth spawns more up to
	// m_maxWorkers (0 — no limit); idle ones eventually self-exit via timeout.
}

ibWorkerPoolHeadless::~ibWorkerPoolHeadless()
{
	Stop();
}

void ibWorkerPoolHeadless::TrySpawnWorker()
{
	std::lock_guard<std::mutex> lk(m_workersMtx);
	// Re-check inside the lock so two concurrent Submits don't both
	// spawn past the cap. m_waitingWorkers stays 0: Await parks a fiber
	// and this thread is free, so the cap is the real cap.
	if (m_maxWorkers != 0 && m_aliveWorkers.load(std::memory_order_acquire)
	    >= m_maxWorkers + m_waitingWorkers.load(std::memory_order_acquire))
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

	// Reentrant Submit on the same session running on this fiber —
	// run inline. The fiber is already leasing this session and would
	// otherwise wait forever for itself to release. A parked fiber has
	// its lease saved away, so a submit from another session on this
	// same OS thread does not look reentrant.
	if (tl_currentLease != nullptr && tl_currentLease == session) {
		try {
			task();
			promise->set_value();
		}
		catch (...) {
			promise->set_exception(std::current_exception());   // the one who holds the future says it (see RunTask)
		}
		return future;
	}

	bool parked = false;
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		auto& slot = m_sessions[session];
		if (!slot) slot = std::make_unique<ibSessionQueue>();
		// Taken on every submit, not only on the first: a queue outlives the drop that retired it
		// (the identity comes back, see ThePoolStaysUsableAfterADeferredErase), and what we want is
		// a hold on the session THIS task belongs to. Empty for a session nobody holds by
		// shared_ptr — a test's own, a stack one — and empty is the right answer there too: such a
		// session cannot be cancelled by us, and must not be reached for.
		if (session != nullptr)
			slot->owner = session->weak_from_this();
		slot->tasks.push_back({ std::move(task), std::move(promise) });
		// A script of this session waits in Await on the fiber that holds it. That fiber runs this
		// task, no other worker may. The fiber is not blocked on slot->wake — its home thread is in
		// m_cv — so the flag and a broadcast are what actually resume it. notify_one can be swallowed
		// by a worker that does not hold the fiber, and the question would sit until the cancel slice.
		if (slot->waiting > 0) {
			slot->m_wake = true;
			slot->wake.notify_all();
			parked = true;
		}
	}
	if (parked)
		m_cv.notify_all();
	else
		m_cv.notify_one();

	// Lazy spawn. If no worker is currently idle, kick a new one into
	// existence — TrySpawnWorker says whether there is room under the cap.
	// m_idleWorkers is incremented in WorkerLoop right before the CV wait
	// and decremented on wake-up — so "idle == 0" means every alive worker
	// is busy on a session. A parked question does not count: its fiber is
	// suspended and the thread is back in this wait, which is idle.
	if (m_idleWorkers.load(std::memory_order_acquire) == 0) {
		TrySpawnWorker();
	}

	return future;
}

void ibWorkerPoolHeadless::Drop(ibSession* session)
{
	std::unique_lock<std::mutex> lk(m_mtx);
	auto it = m_sessions.find(session);
	if (it == m_sessions.end())
		return;
	// A LEASED QUEUE IS NOT OURS TO ERASE — a fiber is standing on it right now,
	// running or parked, and the teardown that called us usually runs from inside
	// one of its tasks. Record the drop; the home thread erases it when the fiber
	// has unwound and the lease is released.
	if (it->second && it->second->leased.load(std::memory_order_acquire)) {
		it->second->dropped = true;
		return;
	}
	m_sessions.erase(it);
}

bool ibWorkerPoolHeadless::ShouldInterrupt(ibSession* session) const
{
	if (m_stop.load(std::memory_order_acquire))
		return true;
	return session != nullptr && ibRunCancelled(session->RunState());
}

bool ibWorkerPoolHeadless::HasRunnableParkedLocked() const
{
	for (const ibParked& parked : tl_parked) {
		if (parked.queue == nullptr)
			continue;
		if (parked.queue->m_wake || !parked.queue->tasks.empty())
			return true;
		if (m_stop.load(std::memory_order_acquire))
			return true;
		if (parked.session != nullptr && ibRunCancelled(parked.session->RunState()))
			return true;
	}
	return false;
}

void ibWorkerPoolHeadless::RunTask(ibSessionTask& item)
{
	try {
		item.task();
		item.promise->set_value();
	}
	catch (...) {
		// ⭐ HANDED TO THE FUTURE, AND SAID BY WHOEVER HOLDS IT — the pool is a door: it runs the task or hands
		// back what the task threw, and does not act for the owner. It used to say every one itself, as a
		// warning, "the double-logging an acceptable cost": a person's refusal («posting cancelled by the
		// handler», «required fields are not filled») the client host had already put in the frame's messages
		// and journalled (ibClientHost::Settle) was told a second time, as a warning, and filled the server's
		// console with them (2026-10-06). A caller that does not wait for its task catches in the task itself.
		item.promise->set_exception(std::current_exception());
	}
}

void ibWorkerPoolHeadless::DrainLease(ibSessionQueue* q)
{
	// Top-level drain. A task that Await threw out of has already left
	// the stack by the time we get back here, so a barrier queued behind
	// it runs now — not under the waiter. Cancel is intentionally not
	// checked: Stop's contract is that queued work still runs, and the
	// cancel belongs to the task that was waiting.
	for (;;) {
		ibSessionTask item;
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			if (q->tasks.empty())
				return;
			item = std::move(q->tasks.front());
			q->tasks.pop_front();
		}
		RunTask(item);
		// item dies HERE, inside the lease — the task's closure may own
		// the session, and teardown must see tl_currentLease still set.
	}
}

void ibWorkerPoolHeadless::Await(ibSession* session, const std::function<bool()>& done)
{
	// Only the fiber that holds the session may run its work, so only it may wait this way — anywhere else
	// the tasks it waits through would be run by nobody (a task of ANOTHER session would also keep that
	// session's fiber on this one).
	if (session == nullptr || tl_currentLease != session)
		throw std::logic_error("ibWorkerPool::Await outside a task of its session");
	ibFiber* const self = ibFiber::Current();
	if (self == nullptr || self->IsScheduler())
		throw std::logic_error("ibWorkerPool::Await outside a task of its session");

	std::unique_lock<std::mutex> lk(m_mtx);
	const auto it = m_sessions.find(session);
	if (it == m_sessions.end() || it->second == nullptr)
		throw std::logic_error("ibWorkerPool::Await: a held session without its queue");
	// Stays: a held queue is never erased (Drop only marks it, see ibSessionQueue::dropped).
	ibSessionQueue* const q = it->second.get();

	// Not m_waitingWorkers. The OS thread is about to be free; counting this wait against the cap
	// (or lifting the cap because of it) would put a thread back under every open question.
	++q->waiting;

	for (;;) {
		// The cancel BEFORE the next task, and done() under this lock: a flag read, nothing that calls
		// back into the pool. A teardown cancels, then submits its barrier, and the barrier must run
		// after this script is out, not under it. The check and the pop stay in one hold of the lock,
		// so a barrier submitted after the cancel cannot slip between them.
		if (ShouldInterrupt(session)) {
			--q->waiting;
			lk.unlock();
			ibBackendInterruptException::Error();
		}
		if (done()) {
			--q->waiting;
			return;
		}
		if (q->tasks.empty()) {
			// A wake that landed in this same hold, with done() still false and nothing queued, is
			// consumed so it cannot spin the scheduler. A wake that lands after the unlock is the
			// flag TakeRunnable sees once we have switched.
			q->m_wake = false;
			q->m_parked = true;
			lk.unlock();

			tl_parked.push_back(ibParked{ session, q, self });
			self->SwitchTo(ibFiber::Scheduler());

			{
				std::lock_guard<std::mutex> parked(m_mtx);
				q->m_parked = false;
			}
			lk.lock();
			continue;
		}

		{
			ibSessionTask item = std::move(q->tasks.front());
			q->tasks.pop_front();
			lk.unlock();
			RunTask(item);
			// item dies HERE, outside the lock — its closure may tear something down that takes it.
		}
		lk.lock();
	}
}

void ibWorkerPoolHeadless::Wake(ibSession* session)
{
	// Looked up by address only — a session we were never told about, or one already gone, finds nothing.
	bool notify = false;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		const auto it = m_sessions.find(session);
		if (it != m_sessions.end() && it->second != nullptr && it->second->waiting > 0) {
			it->second->m_wake = true;
			it->second->wake.notify_all();
			notify = true;
		}
	}
	// The home thread is the one that can switch to the fiber, and it waits on m_cv, not on the
	// session's condition variable. Every worker wakes; the one that parked it notices.
	if (notify)
		m_cv.notify_all();
}

void ibWorkerPoolHeadless::LeaseEntry(void* raw)
{
	std::unique_ptr<ibLeaseArgs> args(static_cast<ibLeaseArgs*>(raw));
	ibWorkerPoolHeadless* const pool = args->pool;
	ibSession* const session = args->session;
	ibSessionQueue* const q = args->queue;
	args.reset();

	// The scope's previous-binding lives on THIS stack. The map slot it
	// writes is per OS thread, so the fiber snapshot (registered from
	// session.cpp) is what puts the binding back when we resume — the
	// scope destructor only runs when the lease actually ends.
	ibSessionScope scope(session);
	struct ClearLease {
		~ClearLease()
		{
			tl_currentLease = nullptr;
			tl_currentQueue = nullptr;
		}
	} clear;
	tl_currentLease = session;
	tl_currentQueue = q;
	pool->DrainLease(q);
}

void ibWorkerPoolHeadless::StartLease(ibSession* session, ibSessionQueue* q)
{
	auto* args = new ibLeaseArgs();
	args->pool = this;
	args->session = session;
	args->queue = q;
	ibFiber* fiber = nullptr;
	try {
		fiber = ibFiber::Create(&ibWorkerPoolHeadless::LeaseEntry, args, ibFiber::kStackReserve);
	}
	catch (...) {
		delete args;
		throw;
	}
	q->m_fiber = fiber;
	q->m_home = std::this_thread::get_id();
	ibFiber::Scheduler()->SwitchTo(fiber);
	if (fiber->Finished())
		FinishFiber(session, q, fiber);
}

void ibWorkerPoolHeadless::FinishFiber(ibSession* session, ibSessionQueue* q, ibFiber* fiber)
{
	if (std::exception_ptr escaped = fiber->TakeException()) {
		try {
			std::rethrow_exception(escaped);
		}
		catch (...) {
			// Logged here, and not rethrown onto the worker. A throw out of
			// the scheduler would kill the thread and skip the unwind of
			// every other fiber parked on it.
			LogWorkerException(wxT("worker pool fiber"));
		}
	}

	bool more = false;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		q->m_fiber = nullptr;
		q->m_parked = false;
		q->m_wake = false;
		q->leased.store(false);
		auto it = m_sessions.find(session);
		if (it != m_sessions.end() && it->second.get() == q
		    && q->dropped && q->tasks.empty()) {
			m_sessions.erase(it);
			more = false;
		}
		else {
			more = !q->tasks.empty();
		}
	}
	if (more)
		m_cv.notify_all();
	// The fiber has unwound (LeaseEntry returned, its scopes destroyed).
	// Only now is the stack free of live objects.
	ibFiber::Destroy(fiber);
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

bool ibWorkerPoolHeadless::TakeRunnable(ibParked& out)
{
	for (auto it = tl_parked.begin(); it != tl_parked.end(); ++it) {
		bool run = false;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			ibSessionQueue* q = it->queue;
			if (q == nullptr)
				continue;
			run = q->m_wake || !q->tasks.empty()
				|| m_stop.load(std::memory_order_acquire)
				|| (it->session != nullptr && ibRunCancelled(it->session->RunState()));
			if (run)
				q->m_wake = false;
		}
		if (!run)
			continue;
		out = *it;
		tl_parked.erase(it);
		return true;
	}
	return false;
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
	ibFiber::ConvertThread();
	struct ReleaseScheduler {
		~ReleaseScheduler() { ibFiber::ReleaseThread(); }
	} releaseScheduler;

	for (;;) {
		ibParked runnable;
		if (TakeRunnable(runnable)) {
			ibFiber::Scheduler()->SwitchTo(runnable.fiber);
			if (runnable.fiber->Finished())
				FinishFiber(runnable.session, runnable.queue, runnable.fiber);
			continue;
		}

		if (m_stop.load(std::memory_order_acquire) && tl_parked.empty())
			break;

		ibSession*       session = nullptr;
		ibSessionQueue*  q       = nullptr;
		bool             gotWork = false;

		{
			std::unique_lock<std::mutex> lk(m_mtx);
			m_idleWorkers.fetch_add(1, std::memory_order_acq_rel);
			const auto slice = tl_parked.empty() ? kIdleTimeout : kCancelSlice;
			gotWork = m_cv.wait_for(lk, slice, [this, &session, &q]() {
				session = nullptr;
				q = nullptr;
				if (HasRunnableParkedLocked())
					return true;
				if (m_stop.load(std::memory_order_acquire))
					return true;
				auto pair = ClaimSessionLocked();
				session = pair.first;
				q       = pair.second;
				return q != nullptr;
			});
			m_idleWorkers.fetch_sub(1, std::memory_order_acq_rel);
		}

		if (m_stop.load(std::memory_order_acquire) && tl_parked.empty() && q == nullptr)
			break;

		if (!gotWork || q == nullptr) {
			// Idle timeout, or we woke because a parked fiber is runnable
			// (handled at the top of the loop). Self-exit only when this
			// thread holds nothing parked — a parked fiber's home thread
			// is the only thread that can resume it.
			if (!tl_parked.empty())
				continue;
			if (m_aliveWorkers.load(std::memory_order_acquire) > kMinIdle)
				break;
			continue;
		}

		// Bind session for the duration of the lease — on the FIBER, not
		// on this thread stack. Current() and GetPUState() resolve to this
		// session inside every task, and the snapshot puts that binding
		// back after the fiber has been parked under somebody else.
		try {
			StartLease(session, q);
		}
		catch (...) {
			LogWorkerException(wxT("worker pool lease"));
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				q->leased.store(false);
				q->m_fiber = nullptr;
			}
			m_cv.notify_all();
		}
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
	std::vector<std::shared_ptr<ibSession>> alive;
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_stop.store(true);
		// CANCEL EVERY KNOWN SESSION. m_stop alone is only read
		// between tasks — a task already running reads nothing, and a task
		// that blocks for minutes (the Firebird maintenance poll) turns
		// this wait into a hang with no way out. The session's cancel
		// is what such a task hears, so shutdown sends it, before
		// waiting for anyone. The sessions are COLLECTED here and cancelled
		// below, outside m_mtx: a cancel rings the session's Await
		// through Wake, which takes this lock. A script waiting in Await
		// is rung here as well — it hears m_stop itself, on its fiber.
		// 🛑 AN ENTRY NAMES A SESSION THAT MAY ALREADY BE GONE, and the key cannot say so. It is
		// legal to look a freed address up (a map compares addresses) and a use-after-free to call
		// through one. Two ways a session leaves without us: it DROPS while leased — `dropped` is
		// set and the worker erases later, the case answered on 2026-09-22 — or it simply ENDS,
		// telling nobody, which is what a pool declared before its sessions meets at scope exit
		// (ThePoolStaysUsableAfterADeferredErase, AddressSanitizer, 2026-09-24).
		//
		// ⭐ So the question is not "was it dropped" but "is it still there", and only the holder
		// can answer: the queue keeps a weak hold on its session and Cancel goes through a lock.
		// A session nobody holds by shared_ptr answers empty, and is left alone — we do not own it
		// and cannot cancel what is already gone.
		for (auto& kv : m_sessions) {
			if (kv.second == nullptr || kv.second->dropped) continue;
			if (kv.second->waiting > 0) {
				kv.second->m_wake = true;
				kv.second->wake.notify_all();
			}
			if (std::shared_ptr<ibSession> session = kv.second->owner.lock())
				alive.push_back(std::move(session));
		}
	}
	m_cv.notify_all();
	for (const std::shared_ptr<ibSession>& session : alive)
		session->Cancel();
	alive.clear();   // let them go before the wait below — this pool holds no session for longer than a cancel

	// Wait for every detached worker to exit. m_aliveWorkers decrements
	// at the end of each WorkerLoop and notifies m_stopCv. A parked fiber
	// is resumed by the notify above, throws on its own stack, and only
	// then does its home thread leave — Stop does not return while a
	// fiber is still suspended.
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
