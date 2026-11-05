////////////////////////////////////////////////////////////////////////////
//	Description : background runs — one-off work on a session of its own
////////////////////////////////////////////////////////////////////////////

#include "jobManager.h"

#include "backend/appData.h"
#include "core/fiber/fiberLocals.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/moduleManager/moduleManager.h"   // root module manager -> GetProcUnit
#include "backend/metaData.h"                      // ibMetaData::FindAnyObjectByFilter — a common module by its name
#include "backend/compiler/procUnit.h"             // CallAsFunc by name
#include "backend/backend_exception.h"
#include "backend/system/systemManager.h"          // WriteJournalEvent - the run's own record

#include <wx/log.h>

#include <algorithm>

namespace {

// THE RUN'S OWN VOICE. A background session is tied to nobody, so this is not one channel
// among several — it is the only one. Everything a run has to say about itself goes through
// here, stamped with the session id by the logger, which is what makes `journal_read
// {session}` the account of a single run rather than of the whole installation.
void Journal(const ibString& text, ibStatusMessage status)
{
	ibValueSystemFunction::WriteJournalEvent(text, status, ibT("job.background"));
}

// HOW LONG A TENANT WAITS — for the registry to answer its Add, and for the pool to
// hand it a connection. Short, and short for one reason: a rented run is serving
// somebody who is already waiting, and who has something to show meanwhile (the rows
// already on screen). "Not now" is an answer they can act on; half a minute of
// patience is not. A standalone run keeps the generous defaults — nobody is watching
// it, and failing it early only means asking again.
constexpr std::chrono::seconds kTenantWait { 5 };

// WHAT A BACKGROUND RUN IS HANDED — one parcel rather than a capture list.
//
// The task needs eight things and outlives the call that built it, so they travel
// together and by shared_ptr: the closure copies one pointer, and what the run is
// given stays readable as a list of fields instead of a line of captures nobody can
// take in. Everything here is filled on the CALLER's thread, where the answers are
// still to hand.
struct ibBackgroundLaunch {
	std::shared_ptr<ibBackgroundRun>  m_run;
	ibSession*                        m_session  = nullptr;   // owned by m_run's holder
	ibSessionRegistry*                m_registry = nullptr;
	ibUserInfo                        m_initiator;            // empty for a tenant — it installs none
	ibJobManager::ibBackgroundBody    m_body;
	ibString                          m_activity;
	bool                              m_tenant   = false;
	// The session this run RENTS. Held for the run's lifetime rather than for its
	// use: a tenant reads through its parent's access policy, so the object that
	// policy lives on must still be there while the read is out. Empty otherwise.
	std::shared_ptr<ibSession>        m_parent;
};

// THE RUN THIS THREAD IS DOING — set around its body (ibCurrentRun), read by ibBackgroundRun::Current.
thread_local ibBackgroundRun* t_currentRun = nullptr;

struct ibRegisterCurrentRunLocal {
	ibRegisterCurrentRunLocal()
	{
		ibFiberLocals::RegisterTrivial<ibBackgroundRun*>(
			[](void* dst) { *static_cast<ibBackgroundRun**>(dst) = t_currentRun; },
			[](const void* src) { t_currentRun = *static_cast<ibBackgroundRun* const*>(src); });
	}
} s_registerCurrentRunLocal;

struct ibCurrentRun {
	ibBackgroundRun* const m_previous;
	explicit ibCurrentRun(ibBackgroundRun* run) : m_previous(t_currentRun) { t_currentRun = run; }
	~ibCurrentRun() { t_currentRun = m_previous; }
};

// A run that talks in a loop keeps its last lines, not all of them.
constexpr size_t kKeptLines = 200;

} // namespace

// ---------------------------------------------------------------------------
// ibBackgroundRun — the handle on one background run
// ---------------------------------------------------------------------------

bool ibBackgroundRun::IsComplete() const
{
	return m_done.load(std::memory_order_acquire);
}

bool ibBackgroundRun::Wait(int milliseconds)
{
	// The future is written once, after the run is already held (the manager's Stop may be waiting by
	// then), so a copy is taken under the lock and waited on outside it: waiting under the lock would
	// block every other reader.
	std::shared_future<void> future;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		future = m_future;
	}
	if (!future.valid())
		return IsComplete();

	if (milliseconds <= 0) {
		future.wait();
		return true;
	}
	return future.wait_for(std::chrono::milliseconds(milliseconds))
		== std::future_status::ready;
}

ibValue ibBackgroundRun::Result() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_result;
}

ibString ibBackgroundRun::Error() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_error;
}

ibString ibBackgroundRun::Activity() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_activity;
}

void ibBackgroundRun::Activity(const ibString& activity)
{
	// Only into the row that exists. A tenant has none (it is unlisted), so this would be a registry request per
	// scrolled page for a line nobody can read; its handle carries the text instead. The session is asked after the
	// lock is let go, as Cancel asks it.
	std::shared_ptr<ibSession> session;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		m_activity = activity;
		if (m_tenancy != ibJobTenancy::Tenant)
			if (ibSession* const held = m_holder.Get())
				session = held->shared_from_this();
	}
	if (session)
		session->SetActivity(activity);
}

ibGuid ibBackgroundRun::UUID() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_uuid;
}

ibBackgroundRequest ibBackgroundRun::Request() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_request;
}

ibDateTime ibBackgroundRun::Begin() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_begin;
}

ibDateTime ibBackgroundRun::End() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_end;
}

ibBackgroundJobState ibBackgroundRun::State() const
{
	// Completion is published LAST (ibRunEnded), after the error and the cancel flag are stored, so a run
	// read as complete already says how it ended.
	if (!IsComplete())
		return ibBackgroundJobState_Active;
	if (m_cancelled.load(std::memory_order_acquire))
		return ibBackgroundJobState_Cancelled;
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_error.IsEmpty() ? ibBackgroundJobState_Completed : ibBackgroundJobState_Failed;
}

ibBackgroundRun* ibBackgroundRun::Current()
{
	return t_currentRun;
}

void ibBackgroundRun::Say(const ibString& text, ibStatusMessage status)
{
	std::lock_guard<std::mutex> lk(m_mtx);
	m_said.push_back({ text, status });
	if (m_said.size() > kKeptLines)
		m_said.pop_front();
}

std::vector<ibBackgroundRun::ibSaid> ibBackgroundRun::Said() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return std::vector<ibSaid>(m_said.begin(), m_said.end());
}

ibString ibBackgroundRun::SessionGuid() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	// ⚠ THE HOLDER MAY BE EMPTY — a run that has finished and let its session go. Answering with
	// nothing is right: the row is gone, and inventing a name for it would send the caller looking
	// in Active Users for something that is no longer there.
	const ibSession* const session = m_holder.Get();
	return session != nullptr ? session->Identity().m_guid.str() : ibString();
}

void ibBackgroundRun::Cancel()
{
	// The run's session is told (ibSession::Cancel) and it passes the cancel on.
	// The holder is read under the lock because the manager's tick takes the session
	// away from a run that has finished — cancelling one at that exact moment must
	// read the holder or the empty slot, never a half-moved one. A finished run reads
	// empty here and there is nothing left to cancel, which is the right answer anyway.
	// The cancel itself goes out after the lock is let go: it asks the manager for
	// tenants, and the manager takes its own lock before this one.
	std::shared_ptr<ibSession> session;
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (ibSession* const held = m_holder.Get())
			session = held->shared_from_this();
	}
	if (!session) {
		// Said, because a cancel that does not arrive looks exactly like one that arrived and was ignored.
		ibJournalInfo(ibT("cancel"), ibT("background run '%s': nothing to cancel - it holds no session"), m_activity);
		return;
	}
	session->Cancel();
}

// ---------------------------------------------------------------------------

std::shared_ptr<ibBackgroundRun> ibJobManager::StartBackground(const ibBackgroundRequest& request,
                                                               const std::vector<ibValue>& args)
{
	// Gate first, before anything is created: a bad argument is the caller's
	// mistake and they are still on the stack to hear about it.
	CheckTransferable(args);

	const ibString procedureName = request.m_method;
	if (procedureName.IsEmpty())
		ibBackendCoreException::Error(_("Background job: no procedure name given"));

	// Everything a background run IS — its own session, the initiator's identity,
	// the error capture, the handle — belongs to the general form below. What is
	// specific to a call BY NAME is only this: resolve ModuleName.MethodName
	// against the session's runtime and call it. So that is all this body does.
	const std::vector<ibValue> argsCopy = args;
	return Launch(
		[procedureName, argsCopy](ibSession* session) -> ibValue {
			// Resolve ModuleName.MethodName against the session's root module. The
			// name is REQUIRED to carry its module: a background call is made by
			// name from another session, so "which module did the author mean" has
			// to be in the name rather than in whatever happened to be in scope
			// where it was written.
			ibValueModuleManagerRuntimeConfiguration* const mm = session->GetManagerModule();
			if (mm == nullptr)
				ibBackendCoreException::Error(_("Background job: the session has no runtime"));

			std::shared_ptr<ibProcUnit> unit = mm->GetProcUnit();
			if (!unit)
				ibBackendCoreException::Error(_("Background job: the session has no runtime"));

			const int dot = procedureName.Find('.', /*fromEnd*/ true);
			if (dot == wxNOT_FOUND) {
				ibBackendCoreException::Error(
					_("Background job: '%s' must name the module - ModuleName.MethodName"),
					procedureName);
			}
			const ibString moduleName = procedureName.Left(dot);
			const ibString methodName = procedureName.Mid(dot + 1);

			// ⭐ A COMMON MODULE IS FOUND AMONG THE SESSION'S COMMON MODULES, not among the root's properties.
			// `GetPropVal(name)` answers only for EXPORT variables of the root module (ibProcUnit::FindProp
			// skips External / Context on purpose — jobRunByteCode.cpp spells out why), and a common module
			// is bound into a session as exactly such an ambient name. So the one thing this door exists
			// for — `RunBackground("StockManagement.RecalculateTotals", …)` — answered "common module not
			// found" for every common module there is (measured 2026-09-19; no test called it by name).
			// The root's own export of that name still wins, which keeps whatever relied on it.
			//
			// Asked as the scheduled job asks it (metaScheduledJobObject.cpp): the COMMON MODULE by its name in
			// this session's own configuration, then its unit from the session — FindCommonModule, the one door
			// that answers "which unit runs this module here". A name compared along the list of units would be
			// a second answer to it, and that list holds every object's manager module too.
			ibValue moduleValue;
			if (!unit->GetPropVal(moduleName, moduleValue) || moduleValue.IsEmpty()) {
				const ibMetaData* const metaData = mm->GetMetaManager() != nullptr ? mm->GetMetaManager()->GetMetaData() : nullptr;
				const ibValueMetaObjectCommonModule* const commonModule = metaData != nullptr
					? metaData->FindAnyObjectByFilter<ibValueMetaObjectCommonModule>(moduleName, g_metaCommonModuleCLSID)
					: nullptr;
				if (commonModule != nullptr)
					if (ibValueModuleManager::ibValueModuleUnit* const found = mm->FindCommonModule(commonModule))
						moduleValue = ibValue(found);
			}
			if (moduleValue.IsEmpty())
				ibBackendCoreException::Error(_("Background job: common module '%s' not found"), moduleName);

			// Only PUBLIC methods are on a module value's surface at all — that is
			// what a module value exposes ("exports are the whole surface",
			// moduleManager.h, where Export is the bytecode-side name for the
			// Public modifier). So this lookup enforces the visibility rule
			// without a second check: a Private method is simply not there.
			const long methodNum = moduleValue.FindMethod(methodName);
			if (methodNum == wxNOT_FOUND) {
				ibBackendCoreException::Error(
					_("Background job: '%s' has no public method '%s'"), moduleName, methodName);
			}

			std::vector<ibValue> callArgs = argsCopy;
			std::vector<ibValue*> ptrs;
			ptrs.reserve(callArgs.size() + 1);
			for (auto& v : callArgs) ptrs.push_back(&v);
			ptrs.push_back(nullptr);   // trailing null keeps a 0-arg array valid

			// Called as a FUNCTION: a procedure simply leaves the result empty, so
			// one path serves both and the caller's Result() answers honestly
			// either way.
			ibValue result;
			moduleValue.CallAsFunc(methodNum, result, ptrs.data(),
			                       static_cast<long>(callArgs.size()));
			return result;
		},
		request,
		ibString::Format(ibT("background: %s"), request.m_description.IsEmpty() ? procedureName : request.m_description),
		ibJobTenancy::Standalone);
}

std::shared_ptr<ibBackgroundRun> ibJobManager::StartBackground(ibBackgroundBody body,
                                                               const ibString& activity,
                                                               ibJobTenancy tenancy)
{
	// An engine body names no method and no key: what it is, is the sentence it shows.
	ibBackgroundRequest request;
	request.m_description = activity;
	return Launch(std::move(body), request, activity, tenancy);
}

std::shared_ptr<ibBackgroundRun> ibJobManager::Launch(ibBackgroundBody body, const ibBackgroundRequest& request,
                                                      const ibString& activity, ibJobTenancy tenancy)
{
	if (!body)
		ibBackendCoreException::Error(_("Background job: nothing to run"));

	// The run works in this manager's base — its registry, its pool — not in the base the calling thread
	// happens to stand in.
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(m_applicationInstance);
	if (registry == nullptr)
		ibBackendCoreException::Error(_("Background job: the application is not running"));

	auto launch = std::make_shared<ibBackgroundLaunch>();
	launch->m_registry = registry;
	launch->m_body     = std::move(body);
	launch->m_activity = activity;
	launch->m_tenant   = (tenancy == ibJobTenancy::Tenant);
	const bool tenant  = launch->m_tenant;

	// THE PARENT — the session this run rents, read here while it is still the
	// current one. Held for the run's whole life, because a tenant borrows its
	// access policy: the object the policy lives on must not be dismantled
	// underneath a read that is using it. A run with no parent is not a tenant at
	// all, so this refuses rather than quietly running unrented.
	if (tenant) {
		ibSession* const current = ibSession::Current();
		if (current == nullptr)
			ibBackendCoreException::Error(_("Background job: a rented run needs the session that starts it"));

		// ANY HOST WILL DO. A tenant is not a property of how the process was
		// started — it is a connection plus a policy, borrowed for the length of one
		// read. A list is a list whether it is opened in the Designer, in the thick
		// client or in a browser tab; the run is used up, brings the data back and
		// ends, and nothing about it needs to know which of those it was.
		launch->m_parent = current->shared_from_this();
	}

	// WHOSE identity this run adopts — read here, on the caller's thread, while
	// the caller's session is the current one. Inside the task it would resolve to
	// the job's own session, which has none yet. A tenant installs none: it does
	// not act as the user, it acts FOR the session that already does.
	if (!tenant)
		launch->m_initiator = m_applicationInstance->GetUserInfo();

	auto run = std::make_shared<ibBackgroundRun>();
	run->m_request = request;
	run->m_tenancy = tenancy;

	// HELD BEFORE ANYTHING IS CREATED, in the step that checks the two refusals — so they cost nothing, and
	// so two starts of one key cannot both pass: the check and the holding are one step under one lock.
	{
		std::lock_guard<std::mutex> lk(m_mtx);

		// A STOPPED MANAGER TAKES NO WORK. Stop() has already cancelled and waited out everything
		// it knew about, and its tick — the one that gives a finished run's session back — is gone
		// for good. A run accepted now would be work on a database being torn down, holding a
		// session nobody is left to close.
		if (m_stopped)
			ibBackendCoreException::Error(_("Background job: the schedule is shutting down"));

		// ONE ACTIVE RUN PER KEY AND METHOD. The key is the caller's name for the work; a second start of it
		// while the first runs is the double click this exists for, and it is named rather than let through.
		if (!request.m_key.IsEmpty()) {
			for (const std::shared_ptr<ibBackgroundRun>& held : m_backgroundJobs) {
				if (held->IsComplete())
					continue;
				std::lock_guard<std::mutex> runLk(held->m_mtx);   // the tick's order: the manager's lock, then the run's
				if (held->m_request.m_key == request.m_key && held->m_request.m_method == request.m_method)
					ibBackendCoreException::Error(_("Background job: '%s' with the key '%s' is already running"),
						request.m_method, request.m_key);
			}
		}
		m_backgroundJobs.push_back(run);
	}

	// …AND LET GO AGAIN IF IT NEVER STARTS — a session that would not come up, a pool that did not take the
	// task. The caller hears why; a run that never was must neither keep its key nor be found.
	struct ibHeldRun {
		ibJobManager&                           m_manager;
		const std::shared_ptr<ibBackgroundRun>& m_run;
		bool                                    m_started = false;

		~ibHeldRun()
		{
			if (m_started)
				return;
			std::lock_guard<std::mutex> lk(m_manager.m_mtx);
			std::vector<std::shared_ptr<ibBackgroundRun>>& runs = m_manager.m_backgroundJobs;
			runs.erase(std::remove(runs.begin(), runs.end(), m_run), runs.end());
		}
	} held{ *this, run };

	ibSessionHolder holder;
	if (tenant) {
		// MINTED, NOT REGISTERED. The registry's Add exists to make a session
		// visible and governable — a row, the policy chain, the lookup index — and a
		// tenant wants none of that: it lives for one portion and answers to nobody
		// but the caller. Add is also not free. It is a handshake with the consumer
		// thread plus, inside ProcessAdd, a cluster-snapshot refresh (a SELECT over
		// sys_session), and paying that per scrolled page ON THE THREAD THAT ASKED
		// is felt as the window hanging.
		//
		// Everything the run actually needs it gets by construction: a worker queue
		// (the pool keys on the session pointer and creates one on first Submit), a
		// connection of its own (taken just below), and a teardown that gives the
		// connection back (~ibSession) without troubling the registry (Teardown
		// reads m_listed).
		auto minted = std::make_shared<ibSession>(ibString(wxNewUniqueGuid),
		                                          ibSessionKind::BackgroundJob);
		// It reads in its parent's configuration — the one the parent works in, not the base's newest: a read for
		// a session is read the way that session reads.
		minted->m_metaData = launch->m_parent->m_metaData;
		// …for its parent in this manager's base: the base's registry is its owner, and the base's pool gives its
		// connection — and takes it back on whatever thread the session dies.
		holder = registry->MintUnlisted(std::move(minted));
	}
	else {
		holder = registry->CreateSessionOfKind(
			m_applicationInstance->GetAppMode(), m_applicationInstance->GetComputerName(),
			ibSessionKind::BackgroundJob,
			&ib_detail::MakeSessionFactory<ibSession>);
	}

	if (!holder)
		ibBackendCoreException::Error(_("Background job: session could not be created"));

	ibSession* const session = holder.Get();
	{
		// Under the run's lock: the run is held already, and the tick and TenantsOf read its holder.
		std::lock_guard<std::mutex> lk(run->m_mtx);
		run->m_holder = std::move(holder);
		// Its session's id: a listed session's is the guid of its identity, and a rented one has no identity at all - it
		// is minted under a guid of its own (#646: Identity() left a rented run's UUID empty).
		run->m_uuid   = ibGuid(session->GetId());
	}
	launch->m_run     = run;
	launch->m_session = session;

	if (tenant) {
		// THE LANDLORD. Server() is already "the session that hosts this one" —
		// what a web client's is to it, this run's parent is to this run — and
		// hanging the tenancy there is what lets GetAccessPolicy find the borrowed
		// policy without anyone threading it through. Set AFTER the session is
		// registered: the registry stamps its own answer during Add (the process's
		// web server, where there is one), and the parent is the truer one here.
		session->SetServer(launch->m_parent.get());

		// THE ONE THING THAT CANNOT BE RENTED. A session owns exactly one
		// connection and the parent's is busy with the parent's own work, so this
		// run takes its own — HERE, on the caller's thread, so a pool with nothing
		// free is an exception the caller can act on (keep the rows it has, read
		// inline, say so) instead of a failure discovered by nobody on a worker.
		// Bounded for the same reason: a form waiting on one portion needs an
		// answer, not a half-minute of patience it never asked for.
		session->Holder()->EnsureConnection(kTenantWait);
	}

	// Everything below runs ON THE WORKER, with the session already bound by the
	// pool's ibSessionScope. That binding is why the identity install, the compile
	// and the call all happen here rather than at submission: ibSession::Current(),
	// GetPUState(), the lambda runtime AND the query layer's connection + access
	// policy (ibDatabaseQueryBuilder reads both off the current session) all
	// resolve through it. That last one is why a background read sees exactly what
	// the initiator sees, RLS included, without this layer arranging anything.
	std::shared_future<void> future = session->Submit([launch]() {
		const ibBackgroundLaunch&              l   = *launch;
		const std::shared_ptr<ibBackgroundRun> run = l.m_run;

		// 🛑⭐⭐ THE ENDING IS PUBLISHED BY A DESTRUCTOR, NOT BY THE LAST STATEMENT — because a run
		// that never REACHES the last statement is the one failure nobody can see. A job that throws
		// is visible (the catches below say so); a job that is cancelled is visible. A job whose
		// thread simply stops being there looks EXACTLY like a job that is working: `complete: false`
		// and an activity line, for as long as anybody cares to watch.
		//
		// WHAT PROMPTED IT, 2026-09-07: a posting run wrote its first document and its movement and
		// was never heard from again — 205 documents asked for, one done, no error anywhere, and the
		// run object still answering "running" twenty minutes later. That particular run turned out
		// to be BLOCKED, not gone (a transaction pin the lock manager took from it, connectionPool.cpp
		// — a stack dump named it in one read, after a debugger attach had said the opposite), and a
		// destructor cannot see a thread that never leaves. It is the shape that matters: the only
		// account of a background run is what the run itself says, so every way OUT of the body has
		// to say something, including the ways nobody wrote.
		//
		// So completion is tied to the SCOPE rather than to reaching its end. Whatever leaves this
		// lambda — a return, an exception the catches missed, an unwind nobody wrote — the guard runs
		// and the run stops claiming to be alive. And if the body did not report an ending itself, the
		// guard says so in the journal, because "it ended and nobody knows how" is a fact worth having
		// rather than a silence to interpret.
		struct ibRunEnded {
			const std::shared_ptr<ibBackgroundRun>& m_run;
			const ibString&                         m_activity;
			bool                                    m_said = false;

			~ibRunEnded()
			{
				if (!m_said) {
					try {
						Journal(ibString::Format(
							_("Background job ENDED WITHOUT SAYING HOW: %s - the run left its body by a "
							  "path that reported nothing. Anything it committed stays."), m_activity),
							ibStatusMessage_Error);
					}
					catch (...) {
						// A destructor says what it can and never throws. Losing the line is bad;
						// losing the completion below would be worse.
					}
					std::lock_guard<std::mutex> lk(m_run->m_mtx);
					if (m_run->m_error.IsEmpty())
						m_run->m_error = _("the run ended without saying how");
				}
				{
					std::lock_guard<std::mutex> lk(m_run->m_mtx);
					m_run->m_activity.clear();
					m_run->m_end = ibDateTime::Now();
				}
				// Published LAST, and only after the result / error are stored: a watcher that sees
				// IsComplete() must find everything already there.
				m_run->m_done.store(true, std::memory_order_release);
			}
		} ended{ run, l.m_activity };
		try {
			// 1. Adopt the initiator's identity. No password: Login is already
			//    split into AuthenticateUser (the check) and InstallUser (the
			//    commit), and only the commit is wanted here — the caller was
			//    authenticated, and this run is theirs.
			//
			// 2. Bring the runtime up. EnsureRoot + CompileRoot hang off the
			//    Authenticated notification, and without them the session has no
			//    root module and nothing to execute.
			//
			// A TENANT DOES NEITHER, and that is the whole saving. It runs no script,
			// so a compile of every common module would buy nothing; it acts for the
			// parent rather than as the user, so an identity of its own would be a
			// second answer to a question already answered next door. What RLS needs
			// — the policy — it borrows through Server() (ibSession::GetAccessPolicy).
			if (!l.m_tenant) {
				if (l.m_initiator.IsOk())
					l.m_registry->InstallUser(l.m_session, l.m_initiator, ibEmptyString);
				l.m_registry->NotifyAuthenticated(l.m_session);
			}

			run->Activity(l.m_activity);

			// 3. The work itself. Whatever it is — a configuration's procedure
			//    resolved by name, or an engine read — it is a function of the
			//    session, and this layer neither knows nor needs to know which.
			//    While it runs, this thread is doing THIS run: what its code says
			//    with `Message` is kept here (ibBackgroundRun::Say).
			const ibCurrentRun doing(run.get());
			const ibValue result = l.m_body(l.m_session);

			// ⭐ AN ENDING IS AN EVENT TOO. Without this the journal shows a run starting and
			// talking, and then nothing — and "still going" reads exactly like "finished a while
			// ago", which is the one question a person asks about work they cannot see.
			//
			// ⚠ NOT FOR A TENANT. It is a rented read serving somebody who is already waiting and
			// already holds the handle; it has no row in Active Users for the same reason, and a
			// line per scrolled page would bury the runs that are actually jobs.
			if (!l.m_tenant)
				Journal(ibString::Format(_("Background job finished: %s"), l.m_activity),
					ibStatusMessage_Information);

			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_result = result;
		}
		// ⭐⭐ A BACKGROUND RUN THAT FAILS WRITES ITS OWN OBITUARY, AND IT BELONGS HERE RATHER THAN IN
		// ANY ONE JOB. This session is tied to nobody: `Message` reaches no window, so an exception
		// that merely unwinds is an event NOBODY CAN EVER SEE - the handle below carries the text,
		// but only for as long as somebody is holding it and only if they think to ask at the right
		// moment. The registration journal is the one durable channel a session like this has, it
		// is stamped with the session id automatically, and it is where the run's own
		// WriteJournalEvent lines already land - so `journal_read {session}` reads the whole story
		// in order: what it was doing, and what it died of (Max, 2026-09-06: *"any background job,
		// when it runs, must handle this inside itself and write the journal - not only the one you
		// have now"*).
		//
		// ⭐ THE KIND IS READ OFF THE TYPE, which is the reason the exception family is split at all.
		// A refusal that names a broken rule - ibBackendFormException, "there are no forms in this
		// process" - arrives intact because nothing on the way up swallowed it, and lands as a line
		// naming that rule instead of a null for somebody to interpret.
		catch (const ibBackendInterruptException&) {
			// ⚠ NOT AN ERROR, AND IT MUST NOT BE FILED AS ONE. Somebody asked for this run to stop
			// and it obeyed; writing it under `level: error` would put a successful cancellation in
			// front of whoever scans the journal for what is broken. It is still recorded, because
			// "why did that run not finish" is a real question with a real answer.
			Journal(ibString::Format(_("Background job cancelled: %s"), l.m_activity),
				ibStatusMessage_Warning);

			run->m_cancelled.store(true, std::memory_order_release);
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = _("the run was cancelled");
		}
		// ⭐ A SCRIPT'S FAILURE SAYS WHERE — the module and the line its run stopped at (ProcessError), so a status
		// read later points at the code rather than only quoting what it said.
		catch (const ibBackendException& err) {
			const ibString said = err.GetErrorModule().IsEmpty() ? err.GetErrorDescription()
				: ibString::Format(_("%s (module %s, line %u)"), err.GetErrorDescription(), err.GetErrorModule(), err.GetErrorLine());
			Journal(ibString::Format(_("Background job FAILED: %s - %s"), l.m_activity, said), ibStatusMessage_Error);

			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = said;
		}
		catch (const ibCoreException& err) {
			Journal(ibString::Format(_("Background job FAILED: %s - %s"),
				l.m_activity, err.GetErrorDescription()), ibStatusMessage_Error);

			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = err.GetErrorDescription();
		}
		catch (...) {
			Journal(ibString::Format(_("Background job FAILED: %s - unknown error"), l.m_activity),
				ibStatusMessage_Error);

			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = _("unknown exception");
		}

		// EVERY ARM ABOVE HAS SAID WHAT HAPPENED, so the guard has nothing to add — clearing the
		// activity and publishing completion are its business now, on the way out.
		ended.m_said = true;
	});

	// A SUBMIT THAT WAS NOT ACCEPTED MUST NOT READ AS "IN FLIGHT". A stopped pool
	// rejects by setting an exception on the future and never running the task. The
	// caller has by then raised its own "a read is out" state, and would wait for a
	// delivery that can never come — the spinner spins forever and the list refuses
	// every later portion, which is a wedged control rather than a slow one. If the
	// future came back already resolved with an exception, say so out loud: the
	// caller reads a refusal as "not now" and does the work itself.
	//
	// A ready future with no exception is the ordinary inline completion (the GUI
	// pool, or a reentrant submit) — consuming it here is harmless, since a watcher
	// asks IsComplete() and the result is already stored.
	if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		try {
			future.get();
		}
		catch (...) {
			ibBackendCoreException::Error(_("Background job: the worker pool did not take the run"));
		}
	}

	// It started: from here on the tick takes its session back when it ends (m_backgroundJobs), and Stop() reaches it
	// while it is still going. If Stop() swept the list since it was held, it is out of it already — the task
	// holds it then, and it closes its own session when it ends.
	{
		std::lock_guard<std::mutex> lk(run->m_mtx);
		run->m_future = future;
	}
	held.m_started = true;

	// AND THE TICK MUST BE RUNNING, because the tick is now what gives the session back.
	// The schedule normally starts with the platform's own jobs, when a database opens — but a
	// process may run background work without ever getting there, and one that did would keep
	// every finished run's session (and its pooled connection) forever. Idempotent, and a tick
	// with nothing due is a mutex and a clock read.
	Start();

	return run;
}

std::vector<std::shared_ptr<ibBackgroundRun>> ibJobManager::GetBackgroundJobs() const
{
	// A RENTED run is not among them: a portion read for a form has no row in Active Users and no identity
	// worth keeping, so it is the form's business, not the application's list. Its tenancy is set before it
	// is held and never changes, so it is read without the run's lock.
	std::vector<std::shared_ptr<ibBackgroundRun>> jobs;
	std::lock_guard<std::mutex> lk(m_mtx);
	for (const std::shared_ptr<ibBackgroundRun>& run : m_backgroundJobs)
		if (run->m_tenancy != ibJobTenancy::Tenant)
			jobs.push_back(run);
	return jobs;
}

std::shared_ptr<ibBackgroundRun> ibJobManager::FindBackgroundJob(const ibGuid& uuid) const
{
	if (!uuid.isValid())
		return nullptr;   // a run whose session never came up has no name to be found by
	std::lock_guard<std::mutex> lk(m_mtx);
	for (const std::shared_ptr<ibBackgroundRun>& run : m_backgroundJobs)
		if (run->m_tenancy != ibJobTenancy::Tenant && run->UUID() == uuid)
			return run;
	return nullptr;
}

std::vector<std::shared_ptr<ibSession>> ibJobManager::TenantsOf(const ibSession* landlord) const
{
	// A TENANT, not any run hosted there: a standalone run's host is whatever the registry stamped (the
	// process's web server, where there is one), and cancelling that server's session is not cancelling
	// every job of the process. A rented read is the unlisted one whose host is the landlord (StartBackground).
	std::vector<std::shared_ptr<ibSession>> tenants;
	if (landlord == nullptr)
		return tenants;
	std::lock_guard<std::mutex> lk(m_mtx);
	for (const std::shared_ptr<ibBackgroundRun>& run : m_backgroundJobs) {
		if (!run || run->m_done.load(std::memory_order_acquire))
			continue;
		std::lock_guard<std::mutex> runLk(run->m_mtx);   // the tick's order: the manager's lock, then the run's
		ibSession* const session = run->m_holder.Get();
		if (session != nullptr && session->IsUnlisted() && session->Server().get() == landlord)
			tenants.push_back(session->shared_from_this());
	}
	return tenants;
}
