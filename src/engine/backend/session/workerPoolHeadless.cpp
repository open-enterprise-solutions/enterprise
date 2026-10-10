#include "workerPoolHeadless.h"

#include "core/fiber/fiber.h"

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

// (No polling slice for a parked fiber. Its wake comes by name: an answer and a withdrawn question Wake, a cancel is
//  ibSession::Cancel, which Wakes as its own step, and Stop notifies everyone. The flag a wake sets is read in the
//  wait's predicate, so a worker holding parked fibers waits as long as an idle one.)

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

void JoinWorkers(std::vector<std::thread> workers)
{
	for (std::thread& worker : workers) {
		if (worker.joinable())
			worker.join();
	}
}

} // namespace

std::vector<std::thread> ibWorkerPoolHeadless::TakeWorkersLocked(bool exitedOnly)
{
	std::vector<std::thread> out;
	const auto self = std::this_thread::get_id();
	auto it = m_workers.begin();
	while (it != m_workers.end()) {
		const bool exited = it->exited && it->exited->load(std::memory_order_acquire);
		// Joining the thread we are on deadlocks. An idle worker whose
		// thread_local destructor re-enters the pool stays in the list;
		// the next other thread, or Stop, joins it.
		if ((exitedOnly && !exited) || (it->thread.joinable() && it->thread.get_id() == self)) {
			++it;
			continue;
		}
		if (it->thread.joinable())
			out.push_back(std::move(it->thread));
		it = m_workers.erase(it);
	}
	return out;
}

std::vector<ibWorkerPoolHeadless::ibParked>& ibWorkerPoolHeadless::ParkedFibers()
{
	thread_local std::vector<ibParked> parked;
	return parked;
}

void ibWorkerPoolHeadless::RegisterFiberLocals()
{
	// Once per process, and before any fiber snapshot. The other slots
	// register themselves at static init, which has already run by the
	// time a pool exists.
	static std::once_flag once;
	std::call_once(once, []() {
		ibFiberLocals::RegisterTrivial<ibSession*>(
			[](void* dst) { *static_cast<ibSession**>(dst) = tl_currentLease; },
			[](const void* src) { tl_currentLease = *static_cast<ibSession* const*>(src); });
	});
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
	// Joined by the new worker, before WorkerLoop, so Submit does not wait
	// out another thread's thread_local destructors. Shared so a failed
	// constructor can put the handles back: destroying a joinable
	// std::thread is std::terminate.
	std::shared_ptr<std::vector<std::thread>> toJoin;
	struct PutBack {
		ibWorkerPoolHeadless* self = nullptr;
		std::shared_ptr<std::vector<std::thread>>* held = nullptr;
		bool armed = true;
		~PutBack()
		{
			if (!armed || self == nullptr || held == nullptr || *held == nullptr)
				return;
			std::lock_guard<std::mutex> lk(self->m_workersMtx);
			for (std::thread& worker : **held) {
				if (worker.joinable())
					self->m_workers.push_back(ibWorker{ nullptr, std::move(worker) });
			}
		}
	} putBack{ this, &toJoin };

	std::lock_guard<std::mutex> lk(m_workersMtx);
	// At the cap, or while stopping, idle self-exits stay in m_workers
	// for the next spawn or for Stop. Taking them here would make this
	// Submit join them.
	if (m_stop.load(std::memory_order_acquire))
		return;
	if (m_maxWorkers != 0 && m_aliveWorkers.load(std::memory_order_acquire) >= m_maxWorkers)
		return;

	auto finished = TakeWorkersLocked(true);
	if (!finished.empty())
		toJoin = std::make_shared<std::vector<std::thread>>(std::move(finished));

	// No allocation under the push: a throw after the thread exists would
	// destroy a still-joinable std::thread. PutBack returns any handles
	// already taken if reserve, make_shared or the thread constructor throws.
	m_workers.reserve(m_workers.size() + 1);
	auto exited = std::make_shared<std::atomic<bool>>(false);
	m_aliveWorkers.fetch_add(1, std::memory_order_acq_rel);
	try {
		m_workers.push_back(ibWorker{
			exited,
			std::thread([this, exited, toJoin]() {
				if (toJoin)
					JoinWorkers(std::move(*toJoin));
				WorkerLoop();
				// Still before thread_local destructors. The next spawn
				// joins an idle self-exit that has stored this; Stop joins
				// every handle, and that join is what waits the destructors out.
				exited->store(true, std::memory_order_release);
			})
		});
	}
	catch (...) {
		m_aliveWorkers.fetch_sub(1, std::memory_order_acq_rel);
		throw;
	}
	putBack.armed = false;
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
		if (session != nullptr) {
			slot->owner = session->weak_from_this();
			// Every submit, not once. A flag left true would interrupt a later
			// session that reused the address before that session had a hold.
			slot->owned = !slot->owner.expired();
		}
		slot->tasks.push_back({ std::move(task), std::move(promise) });
		// A script of this session waits in Await on the fiber that holds it. That fiber runs this
		// task, no other worker may. Its home thread is in m_cv, so the flag and a broadcast are what
		// resume it: notify_one can be taken by a worker that does not hold the fiber.
		if (slot->waiting > 0) {
			slot->woken = true;
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
		// The lease's own hold keeps the session alive, so "the owner expired"
		// never becomes true while the fiber exists. The drop is the interrupt,
		// and the parked fiber has to be resumed to hear it.
		if (it->second->waiting > 0)
			it->second->woken = true;
		lk.unlock();
		m_cv.notify_all();
		return;
	}
	m_sessions.erase(it);
}

bool ibWorkerPoolHeadless::ShouldInterrupt(ibSession* session) const
{
	// m_mtx is held by the only caller (Await). The queue is found here so a
	// drop is seen under that same hold as the pop of the next task.
	if (m_stop.load(std::memory_order_acquire))
		return true;
	if (session == nullptr)
		return false;
	const auto it = m_sessions.find(session);
	if (it != m_sessions.end() && it->second != nullptr && it->second->dropped)
		return true;
	if (static_cast<int>(session->State()) >= static_cast<int>(ibSessionState::Stopping))
		return true;
	return ibRunCancelled(session->RunState());
}

bool ibWorkerPoolHeadless::ParkedShouldRunLocked(ibSessionQueue* q) const
{
	if (q == nullptr)
		return false;
	if (q->woken || !q->tasks.empty() || q->dropped)
		return true;
	if (m_stop.load(std::memory_order_acquire))
		return true;
	// The session, if it is still there. lock() on an expired hold does not
	// touch the object. A hold that used to lock and no longer does is a
	// session that ended while its fiber was parked — resume it so the
	// stack unwinds, instead of calling RunState through the map key.
	if (std::shared_ptr<ibSession> alive = q->owner.lock()) {
		if (static_cast<int>(alive->State()) >= static_cast<int>(ibSessionState::Stopping))
			return true;
		return ibRunCancelled(alive->RunState());
	}
	return q->owned;
}

bool ibWorkerPoolHeadless::HasRunnableParkedLocked() const
{
	for (const ibParked& parked : ParkedFibers()) {
		if (ParkedShouldRunLocked(parked.queue))
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

	// Counted on the queue only. The OS thread is about to be free; counting this wait against the cap
	// (or lifting the cap because of it) would put a thread back under every open question.
	++q->waiting;

	for (;;) {
		// The cancel BEFORE the next task, and done() under this lock: a flag read, nothing that calls
		// back into the pool. A teardown cancels, then submits its barrier, and the barrier must run
		// after this script is out, not under it. The check and the pop stay in one hold of the lock,
		// so a barrier submitted after the cancel cannot slip between them.
		// A session that ended under the park is the same outcome as a cancel, and it is asked
		// of the weak hold: `session` is the map key, and calling through it here is the use-after-free.
		if (q->owned && q->owner.expired()) {
			--q->waiting;
			lk.unlock();
			ibBackendInterruptException::Error();
		}
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
			q->woken = false;
			lk.unlock();

			ParkedFibers().push_back(ibParked{ session, q, self });
			self->SwitchTo(ibFiber::Scheduler());

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
			it->second->woken = true;
			notify = true;
		}
	}
	// The home thread is the one that can switch to the fiber, and it waits on m_cv with the rest.
	// Every worker wakes; the one that parked it notices.
	if (notify)
		m_cv.notify_all();
}

void ibWorkerPoolHeadless::LeaseEntry(void* raw)
{
	std::unique_ptr<ibLeaseArgs> args(static_cast<ibLeaseArgs*>(raw));
	ibWorkerPoolHeadless* const pool = args->pool;
	ibSession* const session = args->session;
	ibSessionQueue* const q = args->queue;

	// The hold was taken from the queue's weak owner under m_mtx, in
	// ClaimSessionLocked. It keeps the raw pointer live for this stack.
	// A session that was never shared has an empty hold — its caller
	// keeps it, as before.
	//
	// The last reference is not released here. Worker tasks do not keep
	// a session alive, and ~ibSession must not run on this fiber, outside
	// the session scope. HandOff moves the pointer onto the queue; 
	// FinishFiber releases it on the scheduler after this stack is gone.
	struct HandOff {
		ibWorkerPoolHeadless*      pool = nullptr;
		ibSessionQueue*            queue = nullptr;
		std::shared_ptr<ibSession> hold;
		~HandOff()
		{
			if (pool == nullptr || queue == nullptr || !hold)
				return;
			std::lock_guard<std::mutex> lk(pool->m_mtx);
			queue->releaseOnScheduler = std::move(hold);
		}
	} handOff{ pool, q, std::move(args->hold) };
	args.reset();

	ibSessionScope scope(session);
	struct ClearLease {
		~ClearLease() { tl_currentLease = nullptr; }
	} clear;
	tl_currentLease = session;
	pool->DrainLease(q);
}

void ibWorkerPoolHeadless::StartLease(ibSession* session, ibSessionQueue* q)
{
	auto* args = new ibLeaseArgs();
	args->pool = this;
	args->session = session;
	args->queue = q;
	args->hold = std::move(q->leaseHold);
	ibFiber* fiber = nullptr;
	try {
		fiber = ibFiber::Create(&ibWorkerPoolHeadless::LeaseEntry, args, ibFiber::kStackReserve);
	}
	catch (...) {
		delete args;
		throw;
	}
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
	std::shared_ptr<ibSession> release;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		release = std::move(q->releaseOnScheduler);
		q->woken = false;
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
	// Only now is the stack free of live objects. The session, if this
	// was its last reference, is released here — on the scheduler, not
	// on the fiber.
	ibFiber::Destroy(fiber);
	release.reset();
}

std::pair<ibSession*, ibWorkerPoolHeadless::ibSessionQueue*>
ibWorkerPoolHeadless::ClaimSessionLocked()
{
	for (auto& kv : m_sessions) {
		ibSessionQueue* q = kv.second.get();
		if (q->tasks.empty()) continue;
		// The owner ended after the task was queued and before a fiber
		// took it. Fail the tasks. Do not call through the map key.
		if (q->owned && q->owner.expired()) {
			std::deque<ibSessionTask> dead;
			dead.swap(q->tasks);
			for (ibSessionTask& item : dead) {
				try {
					item.promise->set_exception(std::make_exception_ptr(
						std::runtime_error("worker pool: the session ended before its task ran")));
				}
				catch (...) {
				}
			}
			continue;
		}
		bool expected = false;
		if (q->leased.compare_exchange_strong(expected, true)) {
			q->leaseHold = q->owner.lock();
			return { kv.first, q };
		}
	}
	return { nullptr, nullptr };
}

bool ibWorkerPoolHeadless::TakeRunnable(ibParked& out)
{
	std::vector<ibParked>& parked = ParkedFibers();
	for (auto it = parked.begin(); it != parked.end(); ++it) {
		bool run = false;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			ibSessionQueue* q = it->queue;
			if (q == nullptr)
				continue;
			run = ParkedShouldRunLocked(q);
			if (run)
				q->woken = false;
		}
		if (!run)
			continue;
		out = *it;
		parked.erase(it);
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

	const std::vector<ibParked>& parked = ParkedFibers();
	for (;;) {
		ibParked runnable;
		if (TakeRunnable(runnable)) {
			ibFiber::Scheduler()->SwitchTo(runnable.fiber);
			if (runnable.fiber->Finished())
				FinishFiber(runnable.session, runnable.queue, runnable.fiber);
			continue;
		}

		if (m_stop.load(std::memory_order_acquire) && parked.empty())
			break;

		ibSession*       session = nullptr;
		ibSessionQueue*  q       = nullptr;
		bool             gotWork = false;

		{
			std::unique_lock<std::mutex> lk(m_mtx);
			m_idleWorkers.fetch_add(1, std::memory_order_acq_rel);
			gotWork = m_cv.wait_for(lk, kIdleTimeout, [this, &session, &q]() {
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

		if (m_stop.load(std::memory_order_acquire) && parked.empty() && q == nullptr)
			break;

		if (!gotWork || q == nullptr) {
			// Idle timeout, or we woke because a parked fiber is runnable
			// (handled at the top of the loop). Self-exit only when this
			// thread holds nothing parked — a parked fiber's home thread
			// is the only thread that can resume it.
			if (!parked.empty())
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
			if (kv.second->waiting > 0)
				kv.second->woken = true;
			if (std::shared_ptr<ibSession> session = kv.second->owner.lock())
				alive.push_back(std::move(session));
		}
	}
	m_cv.notify_all();
	for (const std::shared_ptr<ibSession>& session : alive)
		session->Cancel();
	alive.clear();   // let them go before the wait below — this pool holds no session for longer than a cancel

	// Wait until every WorkerLoop has returned. m_aliveWorkers decrements
	// in that function's last destructor and notifies m_stopCv. A parked
	// fiber is resumed by the notify above, throws on its own stack, and
	// only then does its home thread leave — Stop does not return while
	// a fiber is still suspended.
	//
	// That counter is not the thread. The std::thread function returns,
	// then thread_local destructors run, then the thread is actually
	// gone. Returning here used to overlap those destructors with the
	// caller freeing the sessions and, on the way out of the process,
	// with wx teardown. macOS arm64 reports the resulting fault as a
	// bus error (WorkerPoolFiber.WakeResumesOnlyTheNamedSession, fork
	// CI on develop 8dc49fe3: the test printed OK, then died in global
	// tear-down). The join below is the rest of the wait.
	//
	// Timed, and it keeps waiting — leaving early would let a worker run
	// on into session teardown, which is the use-after-free this drain
	// exists to prevent. What the deadline buys is a VOICE: a wait that
	// says nothing is indistinguishable from a deadlock, and reading
	// that difference cost a full-memory dump (2026-08-03: main parked
	// here while a worker sat in the Firebird sweep poll, which was
	// passing nullptr for its cancel token).
	{
		std::unique_lock<std::mutex> lk(m_stopMtx);
		while (!m_stopCv.wait_for(lk, kStopWaitReport, [this]() {
			return m_aliveWorkers.load(std::memory_order_acquire) == 0;
		})) {
			ibJournalWarning(wxT("session.worker"),wxT("worker pool: still waiting on %lu worker(s) after stop"),
			             (unsigned long)m_aliveWorkers.load(std::memory_order_acquire));
		}
	}

	std::vector<std::thread> joining;
	{
		std::lock_guard<std::mutex> lk(m_workersMtx);
		joining = TakeWorkersLocked(false);
	}
	JoinWorkers(std::move(joining));
}
