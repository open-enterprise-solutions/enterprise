////////////////////////////////////////////////////////////////////////////
//	Description : RUNNING SENT CODE - the application-side half. See
//	              jobRunByteCode.h for why it lives here and not in the tool.
////////////////////////////////////////////////////////////////////////////

#include "backend/job/jobRunByteCode.h"

#include "backend/appData.h"
#include "backend/job/jobManager.h"
#include "backend/session/session.h"
#include "backend/compiler/procUnit.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/backend_exception.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message - telling the person
#include "backend/compiler/compileCode.h"   // the code is COMPILED here, against this application
#include "backend/compiler/compileModule.h" // ...parented to the root, which is what it must see
#include "backend/moduleInfo.h"             // GetCompileModule - which is how the root is asked for
#include "backend/fileSystem/fs.h"          // ibWriterMemory / ibReaderMemory - the wire format below

#include <wx/app.h>                         // wxTheApp - the thread that may touch a window

#include <map>
#include <mutex>

namespace {

// ⭐⭐ THE RUNS THIS PROCESS STARTED, KEYED BY THEIR SESSION. A background run is a handle and the
// caller is on the other side of a socket, so something has to cross the wire and name it — and that
// something is the id the run ALREADY HAS: sys_session.session, the row a person sees in Active
// Users and the column every journal line is stamped with.
//
// 🛑 IT USED TO MINT A TOKEN OF ITS OWN ("fill1", "fill2"), so one run had TWO names: ours and the
// platform's. Two names for one thing is two things to keep in step, and the second one bought
// nothing — the session id is what journal_read filters by and what a person can stop a run by, so
// a caller holding only a token had to be handed the session anyway (Max, 2026-09-06: *"it returns
// you the id of the background job that started, and you track it"* — one id).
//
// ⚠ CAPTURED AT START, NOT ASKED OF THE LIVE RUN. That is the one thing the token did do: outlive
// the session. A finished run has let its session go, so `SessionGuid()` would answer nothing and
// status-after-completion would break; the guid taken here stays valid as long as the entry does.
//
// ⚠ THE REGISTRY IS HERE, not at the tool. The run belongs to the process doing the work: a designer
// that reconnects, or a second assistant on the same application, can still ask after a run somebody
// else started. Keeping the map at the caller would have made an id mean "a run I personally
// launched during this connection", which is not what it is for.
std::mutex                                           g_runsMutex;
std::map<wxString, std::shared_ptr<ibBackgroundRun>> g_runs;

wxString Remember(const std::shared_ptr<ibBackgroundRun>& run)
{
	const wxString session = run->SessionGuid();
	std::lock_guard<std::mutex> lock(g_runsMutex);
	g_runs[session] = run;
	return session;
}

std::shared_ptr<ibBackgroundRun> Recall(const wxString& session)
{
	std::lock_guard<std::mutex> lock(g_runsMutex);
	const auto it = g_runs.find(session);
	return it != g_runs.end() ? it->second : nullptr;
}

// What a run has to say about itself, in one shape, so all three requests answer alike.
//
// ⚠ THE SESSION COMES FROM THE REGISTRY, not from the live run: a finished run has let its session
// go and would answer nothing, and "the run you asked about has no name" is not a true sentence.
void Describe(const ibBackgroundRun& run, const wxString& session, ibJobRunByteCodeState& state)
{
	state.m_session  = session;
	state.m_known    = true;
	state.m_complete = run.IsComplete();
	// ⭐ THE SAME LINE THE PERSON READS. A run says where it has got to in ONE place - its activity,
	// which is also written to sys_session.currentActivity and shows in Active Users. So what a
	// caller reads and what they see cannot drift apart, because there is nothing to drift from.
	state.m_activity = run.Activity();
	if (run.IsComplete()) {
		state.m_error = run.Error();
		if (state.m_error.IsEmpty())
			state.m_result = run.Result().GetString();
	}
}

// 🛑⭐ TOLD FROM THE APPLICATION'S OWN THREAD. This may be called from the debug thread; `Message`
// reaches the frame, and a desktop frame draws — touching a window from a thread that does not own
// it hangs where it does not crash. Handed over and forgotten: the person is being told something,
// not asked.
void Tell(const wxString& text, ibStatusMessage status)
{
	if (wxTheApp != nullptr)
		wxTheApp->CallAfter([text, status]() { ibValueSystemFunction::Message(text, status); });
}

}   // namespace

// ⭐⭐ THE TWO SHAPES WRITE AND READ THEMSELVES, and the writer sits directly above its reader. What
// was here before was a sequence of `w_stringZ` calls in debugServer.cpp and a matching sequence of
// `r_stringZ` calls in debugClient.cpp, two thousand lines apart in different files, kept in step by
// nothing but whoever edited them last remembering to edit both. Adding one field meant four
// hand-matched edits, and getting the ORDER wrong there does not fail to compile: every field is a
// string, so the run's error would simply arrive in the activity line instead.
void ibJobRunRequest::Write(ibWriterMemory& to) const
{
	to.w_stringZ(m_text);
	to.w_stringZ(m_said);
	to.w_stringZ(m_understood);
	to.w_u8(m_commit ? 1 : 0);
}

void ibJobRunRequest::Read(const ibReaderMemory& from)
{
	from.r_stringZ(m_text);
	from.r_stringZ(m_said);
	from.r_stringZ(m_understood);
	m_commit = from.r_u8() != 0;
}

void ibJobRunByteCodeState::Write(ibWriterMemory& to) const
{
	to.w_u8(m_accepted ? 1 : 0);
	to.w_stringZ(m_refusal);
	to.w_stringZ(m_session);
	to.w_stringZ(m_activity);
	to.w_stringZ(m_error);
	to.w_stringZ(m_result);
	to.w_u8(m_complete ? 1 : 0);
	to.w_u8(m_known ? 1 : 0);
}

void ibJobRunByteCodeState::Read(const ibReaderMemory& from)
{
	m_accepted = from.r_u8() != 0;
	from.r_stringZ(m_refusal);
	from.r_stringZ(m_session);
	from.r_stringZ(m_activity);
	from.r_stringZ(m_error);
	from.r_stringZ(m_result);
	m_complete = from.r_u8() != 0;
	m_known    = from.r_u8() != 0;
}

bool ibJobRunByteCode::Start(const ibJobRunRequest& request, ibJobRunByteCodeState& state)
{
	state = ibJobRunByteCodeState();

	if (request.m_text.Strip(wxString::both).IsEmpty()) {
		state.m_refusal = _("Running code: nothing to run - no code came with the request");
		return false;
	}

	// ⚠ THE SAME WALL THE TOOL CHECKS FOR, CHECKED AGAIN WHERE IT IS TRUE. The tool refuses in a
	// designer because it knows it is one; this end refuses because a request may arrive over the
	// wire from anywhere, and a guard that only exists at the caller is a guard the next caller
	// misses.
	if (appData != nullptr && appData->DesignerMode()) {
		state.m_refusal = _("Running code: a designer builds no runtime, so there is nothing here to "
			"run configuration code in");
		return false;
	}

	ibJobManager* const jobs = ibApplicationData::GetJobManager();
	if (jobs == nullptr) {
		state.m_refusal = _("Running code: the application is not running");
		return false;
	}

	// ⭐⭐ THE WHOLE RUN IN ONE TRANSACTION, undone unless it was meant to be kept. That is the two
	// stages, and the difference between them is this one branch - the code is the same, so the
	// first stage is a rehearsal of exactly the second rather than a model of it.
	//
	// ⚠ Batching belongs nowhere: a run that committed as it went could not be thrown away, which
	// is what stage one is for. The price is that a long run holds its locks for its length, so a
	// large load is split across CALLS.
	//
	// ⚠ COPIED OUT OF THE REQUEST, not captured by reference: the job outlives this call by
	// definition — Start answers as soon as the run is accepted, and the run is still going.
	const wxString text = request.m_text;
	const bool     commit = request.m_commit;

	auto body = [text, commit](ibSession* session) -> ibValue
	{
		std::shared_ptr<ibDatabaseLayer> layer = session != nullptr ? session->EnsureConnection() : nullptr;
		if (!layer)
			ibBackendCoreException::Error(_("Running code: the session has no connection to write through"));

		ibValueModuleManagerRuntimeConfiguration* const mm = session->GetManagerModule();
		std::shared_ptr<ibProcUnit> unit = mm != nullptr ? mm->GetProcUnit() : nullptr;
		if (!unit)
			ibBackendCoreException::Error(_("Running code: the session has no runtime"));

		// ⭐⭐ COMPILED HERE, PARENTED ON THIS APPLICATION'S OWN ROOT. That parent is the whole of
		// what makes the code able to do anything: `Catalogs`, `Documents`, the metatype collections,
		// every common module and the platform's own global functions are names on the root, and a
		// compile without it produces a program that resolves nothing. Same recipe as the syntax
		// check uses in the designer (`ibCheckScript`, compiler/scriptCheck.cpp).
		//
		// 🛑 AND THE CODE ARRIVES AS TEXT, not as bytecode, which is the whole reason this compile is
		// here. Sending compiled bytecode WORKS — it was built that way first and measured running,
		// 2026-09-06 — but it makes four separate promises that two processes agree: the AOT format
		// version, the dependency guids, the re-attached parent, the slot numbering. The day the
		// compiler numbers something differently, sent bytecode points at the wrong thing and says
		// nothing about it. Compiling where it will RUN retires all four (Max: *"bytecode can
		// diverge… this way it is guaranteed to pull exactly what is needed"*).
		ibCompileModule* const root = mm->GetCompileModule();
		if (root == nullptr)
			ibBackendCoreException::Error(
				_("Running code: this session has no root module to compile against"));

		// 🛑⭐⭐ THE ROOT UNIT HAS TO BE RUNNING THE ROOT BYTECODE, or the pair below cannot match.
		// A BACKGROUND SESSION DOES NOT RUN ITS MAIN MODULE — it gets a runtime, but nothing has
		// executed on the root ProcUnit, so its `m_pByteCode` is null. `Execute` then compares our
		// program's parent (the root bytecode) against the parent unit's (nothing), decides they
		// disagree, and — before this was fixed one file over — died dereferencing the null while
		// composing the complaint. MEASURED 2026-09-06 from a crash dump: the application went down
		// on `var x = 1 + 1;`, which is as far from the code's own fault as it gets.
		//
		// ⚠ delta FALSE — REGISTER, DO NOT RUN. The root's top level is declarations; running its
		// body here would execute the configuration's start-up code inside somebody's background
		// job. That is exactly what a common module's unit does for the same reason
		// (moduleManager.cpp).
		if (unit->GetByteCode() != &root->m_cByteCode) {
			if (!root->m_cByteCode.m_bCompile)
				ibBackendCoreException::Error(
					_("Running code: this application's root module is not compiled, so there is "
					  "nothing for the code to resolve names against"));
			unit->Execute(root->m_cByteCode, /*delta*/ false);
		}

		// ⚠ PARENTED BEFORE THE COMPILE, because that is what resolves the names, and RE-PARENTED
		// after it — see below.
		ibCompileCode compiler(wxT("JobCode"), wxT("JobCode"));
		compiler.SetParent(root);
		if (!compiler.Compile(text))
			ibBackendCoreException::Error(
				_("Running code: it did not compile here. The application is running a different "
				  "configuration than the one it was written against."));

		// 🛑⭐⭐ AND PARENTED AGAIN, BECAUSE COMPILING FORGETS IT. `Compile` resets the bytecode and
		// `ibByteCode::Reset` clears `m_parent`; only `ibCompileModule` puts it back, because that
		// subclass is the one that keeps a typed parent for exactly this (compileModule.cpp). A
		// plain ibCompileCode does not, and the syntax checker never noticed — it compiles and
		// throws the result away, so a lost parent costs it nothing.
		//
		// MEASURED 2026-09-06: without this the engine refused with "Parent1:(none)" — the program's
		// parent empty while the unit's was the configuration module. Which is the check doing its
		// job, and a good deal better than the crash that preceded it.
		compiler.m_cByteCode.m_parent = &root->m_cByteCode;

		ibValue answer;
		layer->BeginTransaction();
		try {
			// 🛑⭐⭐ THE FRAME HAS TO BE PARENTED THE SAME WAY THE CODE IS. `ibProcUnit::Execute`
			// checks the PAIR before it runs anything: a bytecode with a parent must run on a unit
			// with a parent, and the two parents must be each other's ("check the conformity of
			// modules (compiled and running)", procUnit.cpp). Running this on the session's ROOT unit
			// — which is what the first version did — fails that check as #2: the code has a parent,
			// the unit has none. MEASURED 2026-09-06, and it is the good kind of failure: the engine
			// refused a mismatched pair instead of running one.
			//
			// ⭐ SO THE RUN GETS A UNIT OF ITS OWN, parented on the root — exactly the shape a common
			// module's unit has (`SetParent(moduleManager)` in its ctor, moduleManagerUnit.cpp). It
			// is a local: the program runs once and the frame goes with it.
			ibProcUnit runner;
			runner.SetParent(unit.get());

			// ⚠ delta TRUE — the module BODY is what runs. A common module's top level is only
			// declarations and is registered without running; this is the opposite kind of code, a
			// body written to be executed once.
			runner.Execute(compiler.m_cByteCode, answer, /*delta*/ true);
		}
		catch (...) {
			// ⚠ THE ROLLBACK IS OUTSIDE EVERY EXIT. A throw is a likely end for sent code, and a
			// transaction left open by a failed run would hold locks on a live base.
			layer->RollBack();
			throw;
		}

		if (commit)
			layer->Commit();
		else
			layer->RollBack();   // stage one: it all happened, and none of it is kept
		return answer;
	};

	// ⭐⭐ THE PERSON IS TOLD FIRST, IN THEIR OWN WINDOW, and told BEFORE anything moves - not left to
	// notice the activity line while it happens. The sandbox already does this for a run that keeps
	// NOTHING ("an assistant is running code here in a sandbox"); one that keeps everything cannot
	// say less. What they read is the sentence the caller wrote in `understood`, so the warning and
	// the account of what is about to be done arrive together and in the caller's own words - which
	// is what makes a wrong one catchable.
	Tell(commit
		? wxString::Format(
			_("An assistant is about to WRITE DATA into this base. Existing data can be overwritten "
			  "or deleted and nothing undoes it. What it says it will do: %s"),
			request.m_understood.IsEmpty() ? _("(nothing said)") : request.m_understood)
		: _("An assistant is running code here as a TRIAL - everything it writes will be undone, "
		    "and the base is not changed."),
		commit ? ibStatusMessage_Warning : ibStatusMessage_Information);

	// ⭐⭐ WHAT THE PERSON READS IS WHAT THE CALLER SAID IT WOULD DO. Ad-hoc code has no name to show
	// — there is no module and no method any more — and a line reading "running some code" tells
	// nobody anything. `said` is the caller's own one-line account, the same sentence they wrote in
	// `understood`, and it is the only honest label a nameless run has.
	//
	// 🛑 TWO WRONG WAYS WERE WRITTEN HERE FIRST, and both were the same mistake in different clothes:
	// reaching for `activeMetaData` to look a name up (a global that answers about whichever tree was
	// opened last), and then carrying a name along the wire purely as a caption (Max, 2026-09-06:
	// *"you pulled the active metadata somewhere it has no business being"*). The third answer was to
	// stop needing a name at all.
	const wxString shown = request.m_said.IsEmpty()
		? _("code sent by an assistant") : request.m_said;

	const wxString activity = commit
		? wxString::Format(_("running code (writing): %s"), shown)
		: wxString::Format(_("running code (trial, will be undone): %s"), shown);

	// ⭐ STANDALONE, NOT TENANT, and the reasons stack. A tenant borrows its parent's policy and
	// builds NO runtime - right for reading on somebody's behalf, useless here, since this runs the
	// configuration's own code. And a tenant is UNLISTED: nobody could see this run or stop it,
	// which is acceptable for a read and is not acceptable for a write.
	std::shared_ptr<ibBackgroundRun> run;
	try {
		run = jobs->StartBackground(body, activity, ibJobTenancy::Standalone);
	}
	catch (const ibBackendException& thrown) {
		state.m_refusal = thrown.GetErrorDescription();
		return false;
	}
	if (!run) {
		state.m_refusal = _("Running code: the background run did not start. Nothing was written.");
		return false;
	}

	Describe(*run, Remember(run), state);
	return true;
}

bool ibJobRunByteCode::Status(const wxString& session, ibJobRunByteCodeState& state)
{
	state = ibJobRunByteCodeState();

	const std::shared_ptr<ibBackgroundRun> run = Recall(session);
	if (!run) {
		state.m_refusal = wxString::Format(
			_("Running code: no run called '%s' - it was started by another application, or this one "
			  "has been restarted since"), session);
		return false;
	}

	Describe(*run, session, state);
	return true;
}

bool ibJobRunByteCode::Cancel(const wxString& session, ibJobRunByteCodeState& state)
{
	state = ibJobRunByteCodeState();

	const std::shared_ptr<ibBackgroundRun> run = Recall(session);
	if (!run) {
		state.m_refusal = wxString::Format(
			_("Running code: no run called '%s' - it was started by another application, or this one "
			  "has been restarted since"), session);
		return false;
	}

	// ⚠ ASKED EVEN WHEN IT HAS FINISHED, and answered rather than refused: "it had already ended" is
	// a fact the caller needs, and it is not an error to have been too late.
	if (!run->IsComplete())
		run->Cancel();

	Describe(*run, session, state);
	return true;
}
