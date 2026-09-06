////////////////////////////////////////////////////////////////////////////
//	Description : background runs — one-off work on a session of its own
////////////////////////////////////////////////////////////////////////////

#include "jobManager.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/workerPool.h"            // CancelSession for ibBackgroundRun::Cancel
#include "backend/moduleManager/moduleManager.h"   // root module manager -> GetProcUnit
#include "backend/compiler/procUnit.h"             // CallAsFunc by name
#include "backend/backend_exception.h"

#include <wx/log.h>

namespace {

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
	wxString                          m_activity;
	bool                              m_tenant   = false;
	// The session this run RENTS. Held for the run's lifetime rather than for its
	// use: a tenant reads through its parent's access policy, so the object that
	// policy lives on must still be there while the read is out. Empty otherwise.
	std::shared_ptr<ibSession>        m_parent;
};

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
	// The future is written once, before the handle is published, so reading it
	// here needs no lock; waiting on it under one would block every other reader.
	if (!m_future.valid())
		return IsComplete();

	if (milliseconds <= 0) {
		m_future.wait();
		return true;
	}
	return m_future.wait_for(std::chrono::milliseconds(milliseconds))
		== std::future_status::ready;
}

ibValue ibBackgroundRun::Result() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_result;
}

wxString ibBackgroundRun::Error() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_error;
}

wxString ibBackgroundRun::Activity() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	return m_activity;
}

wxString ibBackgroundRun::SessionGuid() const
{
	std::lock_guard<std::mutex> lk(m_mtx);
	// ⚠ THE HOLDER MAY BE EMPTY — a run that has finished and let its session go. Answering with
	// nothing is right: the row is gone, and inventing a name for it would send the caller looking
	// in Active Users for something that is no longer there.
	const ibSession* const session = m_holder.Get();
	return session != nullptr ? session->Identity().m_guid.str() : wxString();
}

void ibBackgroundRun::Cancel()
{
	// Cooperative — raises the flag and wakes the worker; the interpreter throws
	// ibBackendInterruptException at its next loop boundary and the task unwinds.
	// Under the lock because the manager's tick takes the session away from a run
	// that has finished — cancelling one at that exact moment must read the holder
	// or the empty slot, never a half-moved one. A finished run reads empty here
	// and there is nothing left to cancel, which is the right answer anyway.
	std::lock_guard<std::mutex> lk(m_mtx);
	ibSession* const session = m_holder.Get();
	if (session == nullptr)
		return;
	if (ibWorkerPool* const pool = session->GetWorkerPool())
		pool->CancelSession(session);
}

// ---------------------------------------------------------------------------

std::shared_ptr<ibBackgroundRun> ibJobManager::StartBackground(const wxString& procedureName,
                                                               const std::vector<ibValue>& args)
{
	// Gate first, before anything is created: a bad argument is the caller's
	// mistake and they are still on the stack to hear about it.
	CheckTransferable(args);

	if (procedureName.IsEmpty())
		ibBackendCoreException::Error(_("Background job: no procedure name given"));

	// Everything a background run IS — its own session, the initiator's identity,
	// the error capture, the handle — belongs to the general form below. What is
	// specific to a call BY NAME is only this: resolve ModuleName.MethodName
	// against the session's runtime and call it. So that is all this body does.
	const std::vector<ibValue> argsCopy = args;
	return StartBackground(
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
			const wxString moduleName = procedureName.Left(dot);
			const wxString methodName = procedureName.Mid(dot + 1);

			ibValue moduleValue;
			if (!unit->GetPropVal(moduleName, moduleValue) || moduleValue.IsEmpty())
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
		wxString::Format(wxT("background: %s"), procedureName));
}

std::shared_ptr<ibBackgroundRun> ibJobManager::StartBackground(ibBackgroundBody body,
                                                               const wxString& activity,
                                                               ibJobTenancy tenancy)
{
	if (!body)
		ibBackendCoreException::Error(_("Background job: nothing to run"));

	ibSessionRegistry* const registry = ibApplicationData::GetSessionRegistry();
	if (registry == nullptr || appData == nullptr)
		ibBackendCoreException::Error(_("Background job: the application is not running"));

	// A STOPPED MANAGER TAKES NO WORK. Stop() has already cancelled and waited out everything
	// it knew about, and its tick — the one that gives a finished run's session back — is gone
	// for good. A run accepted now would be work on a database being torn down, holding a
	// session nobody is left to close.
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (m_stopped)
			ibBackendCoreException::Error(_("Background job: the schedule is shutting down"));
	}

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
		launch->m_initiator = appData->GetUserInfo();

	auto run = std::make_shared<ibBackgroundRun>();
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
		auto minted = std::make_shared<ibSession>(wxString(wxNewUniqueGuid),
		                                          ibSessionKind::BackgroundJob);
		minted->SetUnlisted();
		run->m_holder = ibSessionHolder(std::move(minted));
	}
	else {
		run->m_holder = registry->CreateSessionOfKind(
			appData->GetAppMode(), appData->GetComputerName(),
			ibSessionKind::BackgroundJob,
			&ib_detail::MakeSessionFactory<ibSession>);
	}

	if (!run->m_holder)
		ibBackendCoreException::Error(_("Background job: session could not be created"));

	ibSession* const session = run->m_holder.Get();
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
	run->m_future = session->Submit([launch]() {
		const ibBackgroundLaunch&              l   = *launch;
		const std::shared_ptr<ibBackgroundRun> run = l.m_run;
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
					l.m_registry->InstallUser(l.m_session, l.m_initiator, wxEmptyString);
				l.m_registry->NotifyAuthenticated(l.m_session);
			}

			{
				std::lock_guard<std::mutex> lk(run->m_mtx);
				run->m_activity = l.m_activity;
			}
			// Only into the row that exists. A tenant has none (it is unlisted), so
			// this would be a registry request per scrolled page for a line nobody
			// can read; its handle carries the text instead.
			if (!l.m_tenant)
				l.m_session->SetActivity(l.m_activity);

			// 3. The work itself. Whatever it is — a configuration's procedure
			//    resolved by name, or an engine read — it is a function of the
			//    session, and this layer neither knows nor needs to know which.
			const ibValue result = l.m_body(l.m_session);

			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_result = result;
		}
		catch (const ibBackendException& err) {
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = err.GetErrorDescription();
		}
		catch (...) {
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_error = _("unknown exception");
		}

		{
			std::lock_guard<std::mutex> lk(run->m_mtx);
			run->m_activity.clear();
		}
		// Published LAST, and only after the result / error are stored: a watcher
		// that sees IsComplete() must find everything already there.
		run->m_done.store(true, std::memory_order_release);
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
	if (run->m_future.valid()
	    && run->m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		try {
			run->m_future.get();
		}
		catch (...) {
			ibBackendCoreException::Error(_("Background job: the worker pool did not take the run"));
		}
	}

	// Held from here on: the tick takes the session back when the run ends (see m_background),
	// and Stop() reaches everything still going.
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		// Checked AGAIN, because Stop() may have swept the list between the gate above and here.
		// Adding after the sweep would leave the run in a list nobody reads again; leaving it out
		// puts it back the way it was before this list owned runs — the task holds it, and it
		// closes its own session when it ends.
		if (!m_stopped)
			m_background.push_back(run);
	}

	// AND THE TICK MUST BE RUNNING, because the tick is now what gives the session back.
	// The schedule normally starts with the platform's own jobs, when a database opens — but a
	// process may run background work without ever getting there, and one that did would keep
	// every finished run's session (and its pooled connection) forever. Idempotent, and a tick
	// with nothing due is a mutex and a clock read.
	Start();

	return run;
}

