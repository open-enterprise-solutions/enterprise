////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - server part
////////////////////////////////////////////////////////////////////////////

#include "debugServer.h"

// Socket-option constants for the SetOption calls below — winsock supplies them through wx on
// Windows; POSIX keeps them in its own headers. Same reason as debugClient.cpp.
#ifndef __WXMSW__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <chrono>                             // steady_clock — how long a sandbox run actually took

#include "backend/logger/logger.h"            // the screenshot is written down before it is handed over

#include "backend/compiler/procUnit.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/workerPool.h"

#include "backend/fileSystem/fs.h"
#include "backend/system/systemManager.h"     // Message — the person is told before code runs
#include "backend/databaseLayer/databaseLayer.h"   // …and the transaction that undoes it
#if _USE_NET_COMPRESSOR == 1
#include "utils/fs/lz/lzhuf.h"
#endif

#include "backend/appData.h"

///////////////////////////////////////////////////////////////////////
ibDebuggerServer* ibDebuggerServer::ms_debugServer = nullptr;
///////////////////////////////////////////////////////////////////////

namespace {
// Run a watch / tooltip / expand eval against the run context of the session
// currently stopped at a breakpoint, holding that session's debug mutex for
// the whole compile+run.
//
// Replaces the old `IsDebugLooped() + ibSession::CurrentRunContext()` pattern
// at every debug-server-thread eval site. That pattern gated on the
// *server-global* loop flag but dereferenced the *per-session* run-context
// pointer — a TOCTOU. A script worker resuming (Continue / Step / Cancel /
// destroy) unwinds its ibRunContext (a worker-stack object) while a
// concurrent Expand / ToolTip eval on the debug-server thread was still
// reading it → use-after-free; the freed frame reads back as 0xdddddddd.
// Hit 2026-06-05 expanding `thisForm` in the watch (ibProcUnit::Evaluate →
// pRunContext->m_listEval on a dead frame).
//
// The fix makes "is anyone parked", "which frame", and "use the frame" one
// atomic step under ibDebugSession::m_mutex. The worker's resume teardown
// (DoDebugLoop leave block) takes the same mutex and stamps m_debugLoop=false
// alongside the pointer clear, so an eval either:
//   - acquires first: the worker blocks on the teardown lock until the eval
//     finishes against a still-live frame, then unwinds; or
//   - acquires after: sees m_debugLoop == false and skips.
// `evalMode` separates the two callers this has: a TOOLTIP, which must change nothing, and the
// SANDBOX, whose purpose is to change things and have them rolled back. They used to share one
// answer through eval mode, and the sandbox's writes were refused in silence (2026-09-02).
bool EvalInParkedSession(const wxString& expr, ibValue& vResult, bool compileBlock,
	ibEvalMode evalMode = eval_watch)
{
	ibSession* sess = ibSession::Current();
	auto* dbg = sess ? sess->Debug() : nullptr;
	if (dbg == nullptr)
		return false;

	std::lock_guard<std::mutex> lock(dbg->m_mutex);
	if (!dbg->m_debugLoop.load(std::memory_order_acquire) || dbg->m_runContext == nullptr)
		return false;

	return ibProcUnit::Evaluate(expr, dbg->m_runContext, vResult, compileBlock, evalMode);
}
} // namespace

ibDebuggerServer::ibDebuggerServer() :
	m_bUseDebug(false), m_bDebugStopLine(false),
	m_numCurrentNumberStopContext(0),
	m_socketConnectionThread(nullptr)
{
	// One per process — owned by ibMetaDataConfiguration. The static
	// `ms_debugServer` is a hot-path cache (interpreter steps read it
	// every opcode through the `debugServer` macro); ctor publishes,
	// dtor retires. If somebody constructs a second one before the
	// first dies, last-writer wins and the first will null the slot
	// in its dtor only if it still owns it.
	ms_debugServer = this;
}

ibDebuggerServer::~ibDebuggerServer()
{
	// Hard guarantee: worker thread is joined BEFORE the CV/mutex fields disappear.
	// Safe to call twice — ShutdownServer already handles nullptr m_socketConnectionThread.
	ShutdownServer();

	if (ms_debugServer == this)
		ms_debugServer = nullptr;
}

bool ibDebuggerServer::CreateServer(const wxString& hostName, unsigned short startPort, bool wait)
{
	ShutdownServer();

	m_socketConnectionThread = new ibDebuggerServerConnection(hostName, startPort);
	// must be set BEFORE Run() — EntryClient reads m_waitConnection in the worker thread
	m_socketConnectionThread->m_waitConnection = wait;

	if (m_socketConnectionThread->Run() != wxTHREAD_NO_ERROR) {
		ShutdownServer();
		return false;
	}

	if (wait) {

		while (m_socketConnectionThread != nullptr) {

			if (m_bUseDebug || m_socketConnectionThread->m_acceptConnection)
				break;

			wxMilliSleep(5);
		}

		wxASSERT_MSG(m_socketConnectionThread != nullptr
			&& m_socketConnectionThread->m_socket != nullptr, _("Client not connected!"));
	}
	else {
		// Non-blocking server (wes / designer auto-debug): wait for the
		// worker thread to finish binding a port before returning.
		// Caller (e.g. wes's manifest write + listen_after_bind) can now
		// rely on the debug listener already being live, removing the
		// race that left designer's SearchServer scanning empty ports.
		// 500 ms cap is enough for a successful bind in practice — if
		// every port in the diapason is taken the loop exits with the
		// flag still set (worker stores it after the loop terminates,
		// successful or not, see EntryClient).
		const int kBindTimeoutMs = 500;
		for (int waited = 0; waited < kBindTimeoutMs; waited += 5) {
			if (m_socketConnectionThread == nullptr) break;
			if (m_socketConnectionThread->m_bindReady.load(std::memory_order_acquire))
				break;
			wxMilliSleep(5);
		}
	}

	return m_socketConnectionThread != nullptr;
}

void ibDebuggerServer::ShutdownServer()
{
	// Take a local copy + null the member FIRST so other threads observing
	// m_socketConnectionThread (e.g. bytecode thread in DoDebugLoop) see null
	// and stop touching it while we join.
	ibDebuggerServerConnection* thread = m_socketConnectionThread;
	if (thread == nullptr)
		return;
	m_socketConnectionThread = nullptr;

	// Wake any parked DoDebugLoop so the script worker thread(s) unblock.
	// Drains every per-session m_debugLoop + CV — the server-global loop
	// flag this used to clear is gone, so this is the only wake path.
	m_bUseDebug = false;
	WakeAllDebugSessions();

	// Self-join guard: if ShutdownServer is reached from the worker thread itself
	// (e.g. CommandId_Destroy → ForceExit → ~ibDebuggerServer), Wait() would deadlock.
	const bool isSelf = (wxThread::GetCurrentId() == thread->GetId());

	thread->Delete();          // sets TestDestroy() flag

	// Tear the listening socket down BEFORE Wait(). Without this the
	// worker thread sleeps inside wxSocketServer::Accept(true) (the
	// blocking-accept branch when m_waitConnection is true) or waits
	// out a full waitDebuggerTimeout in WaitForAccept(0, ...) on every
	// loop tick — neither path polls TestDestroy fast enough to make
	// shutdown timely. Destroying the server socket aborts the in-
	// flight accept so the next TestDestroy check immediately exits
	// the loop. Worker's own OnKill/dtor will null the pointer it
	// holds; the duplicate Destroy is a no-op.
	if (auto* srv = thread->m_socketServer) {
		srv->Destroy();
		thread->m_socketServer = nullptr;
	}

	if (!isSelf) {
		thread->Wait();        // block until worker actually exited
		delete thread;
	}
	// When called from self, the thread will clean itself up when Entry() returns;
	// we intentionally leak the wxThread object here to avoid UB on self-destruction.
}

#include "backend/system/value/valueOLE.h"
#include "backend/backend_mainFrame.h"

void ibDebuggerServer::ClearCollectionBreakpoint()
{
	// anytime we access the m_pThread pointer we must ensure that it won't
	// be modified in the meanwhile; since only a single thread may be
	// inside a given critical section at a given time, the following code
	// is safe:
	wxCriticalSectionLocker enter(m_clearBreakpointsCS);

	m_listBreakpoint.clear();
}

void ibDebuggerServer::WakeDebugSession(const wxString& sessionGuid)
{
	// Empty guid -> wake everyone (legacy / pre-multi-session designer
	// command). Otherwise route only to the matching session so siblings
	// stay parked at their own breakpoints.
	if (sessionGuid.IsEmpty()) { WakeAllDebugSessions(); return; }

	auto* reg = ibApplicationData::GetSessionRegistry();
	if (reg == nullptr) return;

	// Resolve by sid (GetId(), echoed back by the designer from EnterLoop).
	// Find() now searches the live m_own map; it used to hit the dead legacy
	// m_sessions map and return null, so this wake was a no-op masked by the
	// old server-global m_bDebugLoop flag.
	// Fallback: the session actually parked at the breakpoint (front of the
	// debug queue). Guards against sid drift so Continue / Step never strand
	// the parked worker — the failure mode that surfaced when the global flag
	// was removed.
	ibSessionWatch target = reg->Find(sessionGuid);
	if (!target) target = reg->GetActiveDebugTarget();

	// Held for the whole wake — the session cannot be torn down between
	// resolving it and notifying its CV.
	auto sess = target.Share();
	if (!sess) return;

	auto* d = sess->Debug();
	if (d == nullptr) return;
	d->m_debugLoop = false;
	std::lock_guard<std::mutex> lk(d->m_mutex);
	d->m_cv.notify_all();
}

void ibDebuggerServer::WakeAllDebugSessions()
{
	// Iterate live sessions, flip their per-session m_debugLoop off and
	// kick the CV so the script-thread parked in DoDebugLoop returns.
	// Used on connection loss / server shutdown so sibling tabs in a
	// wes process don't stay frozen after the designer disconnects.
	for (auto& [tid, s] : ibSession::SnapshotByThread()) {
		(void)tid;
		if (s == nullptr) continue;
		auto* d = s->Debug();
		if (d == nullptr) continue;
		d->m_debugLoop = false;
		std::lock_guard<std::mutex> lk(d->m_mutex);
		d->m_cv.notify_all();
	}
}

bool ibDebuggerServer::IsDebugLooped() const
{
	// Per-session park check. On the debug-server thread ibSession::Current()
	// redirects to the front-of-queue parked session (session registry's
	// debug queue), which is the one any incoming Eval/Step command targets.
	// Advisory only — the eval path re-checks under dbg->m_mutex in
	// EvalInParkedSession; the step path re-routes via WakeDebugSession(sid).
	ibSession* sess = ibSession::Current();
	auto* dbg = sess ? sess->Debug() : nullptr;
	return dbg != nullptr && dbg->m_debugLoop.load(std::memory_order_acquire);
}

void ibDebuggerServer::DoDebugLoop(const wxString& strDocPath, const wxString& strModuleName, int numLine, ibRunContext* runContext)
{
	// Own the doc/module path up front. Both params are `const&` into the
	// caller's `byteCode` fields; the LeaveLoop packet below is built AFTER
	// the CV park, by which point the byteCode owner may have been freed
	// (startup-form rebuild) — using the references there would dangle
	// (Face B of the 0xdd debugger UAF, see docs/debugger-per-session.md).
	// Local copies survive a free during the park.
	const wxString docPath    = strDocPath;
	const wxString moduleName = strModuleName;

	// Resolve the script-thread's session — caller is ibProcUnit::Execute
	// which runs under ibSessionScope, so Current() is the session that
	// actually hit the breakpoint. The per-session ibDebugSession is the
	// one we'll park on (so concurrent web sessions don't share a global
	// CV / m_runContext).
	ibSession* sess = ibSession::Current();
	ibSession::ibDebugSession* dbg = sess ? sess->Debug() : nullptr;
	if (dbg == nullptr) {
		// Session not in debug mode — bail without touching server-global
		// state. ResetDebugger here would clear breakpoints for everyone.
		return;
	}

	dbg->m_runContext = runContext;

	// Snapshot the thread pointer once — ShutdownServer nulls m_socketConnectionThread
	// from another thread and we must not deref the member twice with a concurrent null.
	ibDebuggerServerConnection* const thread = m_socketConnectionThread;
	if (thread == nullptr || !thread->IsConnected() || !thread->IsRunning()) {
		ibDebuggerServer::ResetDebugger();
		return;
	}

	m_numCurrentNumberStopContext = 0;

	if (ConnectionType::ConnectionType_Debugger == thread->GetConnectionType()) {

		ibWriterMemory commandChannelEnterLoop;

		commandChannelEnterLoop.w_u16(CommandId_EnterLoop);
		// Session guid travels with every loop-entry packet so the
		// designer side can route Continue/Step/Eval back to the right
		// session in a multi-tab wes process.
		commandChannelEnterLoop.w_stringZ(sess->GetId());
		commandChannelEnterLoop.w_stringZ(docPath);
		commandChannelEnterLoop.w_stringZ(moduleName);
		commandChannelEnterLoop.w_s32(numLine);

		SendCommand(commandChannelEnterLoop.pointer(), commandChannelEnterLoop.size());
	}

	//send expressions from user
	SendExpressions(runContext);

	//send local variable
	SendLocalVariables(runContext);

	//send stack data to designer
	SendStack();

	//start debug loop
	dbg->m_debugLoop = true;
	m_bDebugStopLine = false;

	// Register this session in the registry's debug queue so debug-thread
	// Current() redirects to it (front-of-queue is the active target).
	// Multiple sessions can be parked simultaneously — they rotate as
	// each one resumes and is removed from the queue. Skip when the
	// registry is gone (post-teardown debug step) — the loop below will
	// just exit on dbg->m_debugLoop being false anyway.
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->EnterDebugLoop(sess);

	//create stream for this loop
#ifdef __WXMSW__
	ibValueOLE::CreateStreamForDispatch();
#endif

	// event-driven wait: woken immediately by Continue/StepInto/StepOver/Detach/Destroy.
	// CV/mutex live on the per-session ibDebugSession so a sibling tab's
	// step doesn't unpark this script thread by accident.
	// 250ms wake-up is a safety tick only. Connection-loss / shutdown
	// (ResetDebugger / ShutdownServer / Detach / Destroy) all drain via
	// WakeAllDebugSessions, which flips this session's m_debugLoop and
	// kicks its CV — the single per-session exit condition below.
	while (dbg->m_debugLoop.load(std::memory_order_acquire)) {
		std::unique_lock<std::mutex> lock(dbg->m_mutex);
		dbg->m_cv.wait_for(lock, std::chrono::milliseconds(250), [dbg]() {
			return !dbg->m_debugLoop.load(std::memory_order_acquire);
		});
	}

	// (No store here. The loop above exits only once the flag already reads false, and the ONE
	// authoritative clear is the one below, inside the lock, published together with the run-context
	// pointer — an unsynchronised write beside it could only make the pair disagree.)

	// Symmetric leave — front rotation happens automatically: the next
	// parked session (if any) becomes the new active target for any
	// debug-thread Current() lookup. Same tolerance as EnterDebugLoop
	// above for the post-teardown case.
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->LeaveDebugLoop(sess);

#ifdef __WXMSW__
	ibValueOLE::ReleaseStreamForDispatch();
#endif

	// Re-snapshot — the socket worker may have gone away during the pause.
	if (ibDebuggerServerConnection* const leaveThread = m_socketConnectionThread;
		leaveThread != nullptr && ConnectionType::ConnectionType_Debugger == leaveThread->GetConnectionType()) {

		ibWriterMemory commandChannelLeaveLoop;

		commandChannelLeaveLoop.w_u16(CommandId_LeaveLoop);
		commandChannelLeaveLoop.w_stringZ(sess->GetId());
		commandChannelLeaveLoop.w_stringZ(docPath);
		commandChannelLeaveLoop.w_stringZ(moduleName);
		commandChannelLeaveLoop.w_s32(numLine);

		SendCommand(commandChannelLeaveLoop.pointer(), commandChannelLeaveLoop.size());
	}

	// Activate main frame — pinned by the worker scope of the
	// suspended target's thread. Eval / debug commands route through
	// here while the worker is parked in the CV wait.
	if (auto* frame = ibSession::CurrentFrame())
		frame->RaiseFrame();

	// Drop the eval-visible run context under the per-session debug mutex.
	// A concurrent watch / expand eval (EvalInParkedSession) holds this same
	// mutex across ibProcUnit::Evaluate; acquiring it here blocks our return
	// — and therefore the Execute-side unwind of this frame — until any
	// in-flight eval has finished using it. m_debugLoop is re-stamped false
	// inside the lock so the eval's gate (flag + pointer) observes one
	// consistent state. Prevents the 0xdd use-after-free hit expanding
	// `thisForm` as the worker resumed.
	{
		std::lock_guard<std::mutex> lock(dbg->m_mutex);
		dbg->m_debugLoop = false;
		dbg->m_runContext = nullptr;
	}
}

// Whether an opcode is a "user-stepping" instruction in the debugger
// sense — i.e. the debugger should consider it when deciding to pause
// on step-into / step-over / breakpoint hits. Excluded categories:
//   - Frame-marker opcodes (OPER_FUNC / OPER_END) — synthetic boundaries.
//   - Param-binding opcodes (OPER_SET / OPER_SETCONST / OPER_SET_TYPE)
//     emitted at function entry / call sites — not user-visible lines.
//   - Try/EndTry markers — control-flow brackets.
//   - Tape declarators (OPER_FUNC_PARAM / FUNC_LOCAL / CTX_BEGIN / CTX_END)
//     — pure metadata, NOP at runtime, no source position to stop on.
static bool IsSteppableOpcode(short oper)
{
	switch (oper) {
	case OPER_FUNC:       case OPER_END:
	case OPER_SET:        case OPER_SETCONST:    case OPER_SET_TYPE:
	case OPER_TRY:        case OPER_ENDTRY:
	case OPER_FUNC_PARAM: case OPER_FUNC_LOCAL:
	case OPER_CTX_BEGIN:  case OPER_CTX_END:
		return false;
	default:
		return true;
	}
}

void ibDebuggerServer::EnterDebugger(ibRunContext* runContext, const ibByteUnit& byteCode, long& numPrevLine)
{
	if (!m_bUseDebug)
		return;

	if (IsSteppableOpcode(byteCode.m_numOper)) {

		if (byteCode.m_numLine != numPrevLine) {

			// ⭐ A LOCAL, and it always was one. It lived on the server as an atomic member, and
			// five command handlers wrote it — every one of those writes landed on a value this
			// line clears before anybody reads it, so they said nothing. What actually parks and
			// wakes a stopped worker is the per-session ibDebugSession::m_debugLoop and its
			// condition variable; this is only "does THIS opcode stop", asked and answered here.
			bool doLoop = false;

			//step into
			if (m_bDebugStopLine && byteCode.m_numLine >= 0)
			{
				m_bDebugStopLine = false;
				doLoop = true;
			}
			// step through
			else if (auto* st = ibSession::GetPUState();
				st && m_numCurrentNumberStopContext && m_numCurrentNumberStopContext >= st->GetCountRunContext() && byteCode.m_numLine >= 0)
			{
				m_numCurrentNumberStopContext = st->GetCountRunContext();
				doLoop = true;
			}
			else
			{
				//arbitrary breakpoint 
				if (byteCode.m_numLine >= 0) {
					auto list_breakpoint_iterator = m_listBreakpoint.find(byteCode.m_strDocPath);
					if (list_breakpoint_iterator != m_listBreakpoint.end()) {

						const auto& list_current_breakpoint = list_breakpoint_iterator->second;
						auto list_current_breakpoint_iterator = std::find(
							list_current_breakpoint.begin(), list_current_breakpoint.end(), byteCode.m_numLine);

						doLoop = list_current_breakpoint_iterator != list_current_breakpoint.end();
					}
				}
			}

			if (doLoop)
				DoDebugLoop(byteCode.m_strFileName, byteCode.m_strDocPath, byteCode.m_numLine + 1, runContext);
		}

		numPrevLine = byteCode.m_numLine;
	}
}

void ibDebuggerServer::SendEvalMessage(const wxString& strMessage, MessageType type)
{
	// ⚠ NO LAZY CreateServer HERE. The error road below opens one when none is running, because an
	// error is worth reaching a developer for; an evaluation that nobody is watching simply has
	// nowhere to go, and opening a socket to say so would be worse than silence.
	if (m_socketConnectionThread == nullptr || !m_socketConnectionThread->IsConnected() || !m_bUseDebug)
		return;

	ibWriterMemory commandChannel;
	commandChannel.w_u16(CommandId_EvalMessage);
	commandChannel.w_stringZ(strMessage);

	// THE LEVEL, so a reader can tell "the document did not post" from "posted 12" without parsing
	// the sentence. Error here is the APPLICATION's word — a business rule declining — and not a
	// fault of the platform; those travel the other road, with a module and a line.
	commandChannel.w_u16((unsigned short)type);

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void ibDebuggerServer::SendErrorToClient(const wxString& strFileName,
	const wxString& strDocPath, unsigned int numLine, const wxString& strErrorMessage)
{
	if (m_socketConnectionThread == nullptr)
		CreateServer(defaultHost, defaultDebuggerPort, true);

	if (m_socketConnectionThread == nullptr || !m_socketConnectionThread->IsConnected() || !m_bUseDebug)
		return;

	ibWriterMemory commandChannel;

	commandChannel.w_u16(CommandId_MessageFromServer);
	commandChannel.w_stringZ(strFileName); // file name
	commandChannel.w_stringZ(strDocPath); // module name
	commandChannel.w_u32(numLine); // line code
	commandChannel.w_stringZ(strErrorMessage); // error message

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void ibDebuggerServer::SendExpressions(ibRunContext* runContext)
{
	if (!m_listExpression.size())
		return;

	ibWriterMemory commandChannel;

	//header 
	commandChannel.w_u16(CommandId_SetExpressions);
	commandChannel.w_u32(m_listExpression.size());

	ibValue vResult;

	for (const auto& expression : m_listExpression) {
		//header 
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		commandChannel.w_u64(expression.first);
#else 
		commandChannel.w_u32(expression.first);
#endif 
		//variable
		commandChannel.w_stringZ(expression.second);

		if (ibProcUnit::Evaluate(expression.second, runContext, vResult, false)) {
			commandChannel.w_stringZ(vResult.GetString());
			commandChannel.w_stringZ(vResult.GetClassName());
			//array
			commandChannel.w_u32(vResult.GetNProps());
		}
		else {
			commandChannel.w_stringZ(ibBackendException::GetLastError());
			commandChannel.w_stringZ(wxT("<error>"));
			//array
			commandChannel.w_u32(0);
		}
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void ibDebuggerServer::SendLocalVariables(ibRunContext* runContext)
{
	ibWriterMemory commandChannel;
	commandChannel.w_u16(CommandId_SetLocalVariables);

	// Pick the symbol table that matches the running frame:
	//   - inside a function (m_currentFunction != null) → that function's
	//     m_listLocals (slot indices index the function's frame).
	//   - module body (m_currentFunction == null) → bytecode-level m_listVar.
	// Mixing them would index out-of-bounds (module slots > function
	// frame size, or vice-versa) — exactly the AV trap we hit before.
	if (runContext == nullptr) {
		commandChannel.w_u32(0);
		SendCommand(commandChannel.pointer(), commandChannel.size());
		return;
	}

	const long frameVarCount = runContext->GetLocalCount();

	// Show only user-declared frame slots (Local + Export). Excluded:
	//   - ContextProp: m_slotIndex is a prop-index in the parent's
	//     helper, not a frame slot; would mis-render ambient data.
	//   - Context / External: top-level bindings (Manager / ThisForm /
	//     externs) live in a frame slot but they're ambient symbols,
	//     not user locals; keep them out of the locals window.
	auto isLocalsViewable = [](const ibByteCode::ibByteCodeVarInfo& info) {
		return info.IsUserLocal();
	};

	auto emitVar = [&](const ibByteCode::ibByteCodeVarInfo& info, const wxString& renderedName) {
		// Defensive bound check — don't deref past the current frame.
		// Should never fire for well-formed bytecode but if compile
		// stamping ever drifts we send a placeholder rather than crash.
		const bool inRange = info.m_slotIndex >= 0 && info.m_slotIndex < frameVarCount;
		ibValue* locRefValue = inRange
			? runContext->m_pRefLocVars[info.m_slotIndex]
			: nullptr;
		commandChannel.w_stringZ(renderedName);
		commandChannel.w_stringZ(locRefValue ? locRefValue->GetString()    : wxString());
		commandChannel.w_stringZ(locRefValue ? locRefValue->GetClassName() : wxString());
		commandChannel.w_u32(locRefValue ? locRefValue->GetNProps() : 0);
	};

	// Pick the symbol table that matches the running frame:
	//   - inside a function (m_currentFunction != null) → that function's
	//     m_listLocals (slot indices relative to the function frame).
	//   - module body (m_currentFunction == null) → bytecode-level
	//     m_listVar (slot indices relative to the module frame).
	// Both are vector<ibByteCodeVarInfo> after the unification — same
	// iteration shape, single emit loop.
	const std::vector<ibByteCode::ibByteCodeVarInfo>* table = nullptr;
	if (runContext->m_currentFunction != nullptr)
		table = &runContext->m_currentFunction->m_listLocals;
	else if (const ibByteCode* bc = runContext->GetByteCode())
		table = &bc->m_listVar;

	if (table == nullptr) {
		commandChannel.w_u32(0);
		SendCommand(commandChannel.pointer(), commandChannel.size());
		return;
	}

	// Scope-depth filter — block-locals carry compile-time
	// m_scopeDepth > 0; runtime tracks m_currentScopeDepth via
	// OPER_CTX_BEGIN (push, ++) / CTX_END (pop, --). Entry is visible
	// iff its declared depth ≤ current depth. Fn-frame / module-body
	// vars are stamped 0 → always visible.
	auto isInScope = [&](const ibByteCode::ibByteCodeVarInfo& info) -> bool {
		if (info.m_slotIndex < 0 || info.m_slotIndex >= frameVarCount)
			return false;
		return info.m_scopeDepth <= runContext->m_currentScopeDepth;
	};

	// Closure capture (Phase F) — show captured outer frames as
	// additional Locals entries with "<fn>.<var>" labels. Walks
	// m_parentRunContext chain (set in OPER_CALL_LAMBDA to the lexical
	// parent for lambdas); each heap-promoted ancestor
	// (weak_from_this().lock() non-null = was allocated via
	// make_shared = closure-related) contributes its UserLocal
	// entries. Non-heap-promoted parents (regular call callers) are
	// skipped — they belong to the call stack view, not Locals.
	auto emitFromCtx = [&](ibRunContext* ctx, const wxString& prefix) {
		const std::vector<ibByteCode::ibByteCodeVarInfo>* pTable = nullptr;
		if (ctx->m_currentFunction != nullptr)
			pTable = &ctx->m_currentFunction->m_listLocals;
		else if (const ibByteCode* bc = ctx->GetByteCode())
			pTable = &bc->m_listVar;
		if (pTable == nullptr) return;
		const long pVarCount = ctx->GetLocalCount();
		for (const auto& v : *pTable) {
			if (!v.IsUserLocal()) continue;
			const bool inRange = v.m_slotIndex >= 0 && v.m_slotIndex < pVarCount;
			ibValue* locRefValue = inRange ? ctx->m_pRefLocVars[v.m_slotIndex] : nullptr;
			const wxString rendered = prefix.IsEmpty()
				? v.m_strRealName
				: (prefix + wxT(".") + v.m_strRealName);
			commandChannel.w_stringZ(rendered);
			commandChannel.w_stringZ(locRefValue ? locRefValue->GetString()    : wxString());
			commandChannel.w_stringZ(locRefValue ? locRefValue->GetClassName() : wxString());
			commandChannel.w_u32(locRefValue ? locRefValue->GetNProps() : 0);
		}
	};

	// Pass 1 — count own + captured user-locals.
	uint32_t emitCount = 0;
	for (const auto& v : *table)
		if (isLocalsViewable(v) && isInScope(v)) ++emitCount;
	for (ibRunContext* p = runContext->m_parentRunContext; p != nullptr; p = p->m_parentRunContext) {
		if (!p->weak_from_this().lock()) continue;   // skip stack-only frames
		const auto* pTable = (p->m_currentFunction != nullptr)
			? &p->m_currentFunction->m_listLocals
			: (p->GetByteCode() != nullptr ? &p->GetByteCode()->m_listVar : nullptr);
		if (pTable == nullptr) continue;
		for (const auto& v : *pTable) if (v.IsUserLocal()) ++emitCount;
	}
	commandChannel.w_u32(emitCount);

	// Pass 2 — emit own.
	for (const auto& v : *table) {
		if (!isLocalsViewable(v)) continue;
		if (!isInScope(v)) continue;
		emitVar(v, v.m_strRealName);
	}

	// Pass 2b — emit captured frames in chain order. Label =
	// owning fn's m_strRealName (or "<module>" for module bodies).
	for (ibRunContext* p = runContext->m_parentRunContext; p != nullptr; p = p->m_parentRunContext) {
		if (!p->weak_from_this().lock()) continue;
		const wxString fnName = p->m_currentFunction != nullptr
			? p->m_currentFunction->m_strRealName
			: wxString(wxT("<module>"));
		emitFromCtx(p, fnName);
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void ibDebuggerServer::SendStack()
{
	ibWriterMemory commandChannel;

	auto* puState = ibSession::GetPUState();
	const unsigned int frameCount = puState ? puState->GetCountRunContext() : 0;

	commandChannel.w_u16(CommandId_SetStack);
	commandChannel.w_u32(frameCount);

	for (unsigned int i = frameCount; i > 0; i--) { // walk call stack top-down

		ibRunContext* runContext = puState->GetRunContext(i - 1);
		const ibByteCode* byteCode = runContext ? runContext->GetByteCode() : nullptr;
		if (runContext == nullptr || byteCode == nullptr)
			continue;

		// Skip eval/expression-only frames — bytecode-side flag, no
		// compile-context dependency.
		if (byteCode->m_bExpressionOnly)
			continue;

		const long lCurLine = runContext->m_lCurLine;
		if (lCurLine < 0 || lCurLine > (long)byteCode->m_listCode.size())
			continue;

		wxString strFullName = byteCode->m_listCode[lCurLine].m_strModuleName;
		strFullName += wxT(".");

		// Function name + parameters from bytecode-side m_currentFunction.
		// nullptr = module-body (initializer); otherwise render fn signature
		// using m_strRealName + m_listParam (name on each ibByteParam).
		const ibByteCode::ibByteFunction* fn = runContext->m_currentFunction;
		if (fn != nullptr) {
			strFullName += fn->m_strRealName.IsEmpty()
				? wxString(wxT("<fn>"))
				: fn->m_strRealName;
			strFullName += wxT("(");
			const size_t paramCount = fn->m_listParam.size();
			const long frameVarCount = runContext->GetLocalCount();
			for (size_t j = 0; j < paramCount; j++) {
				const wxString& paramName = (j < fn->m_listParam.size())
					? fn->m_listParam[j].m_strName
					: wxString::Format(wxT("p%zu"), j);
				// Params occupy slots [0, paramCount) — defended via
				// frameVarCount in case a half-initialised frame races
				// with debugger probe.
				wxString valStr;
				if ((long)j < frameVarCount) {
					ibValue* slot = runContext->m_pRefLocVars[j];
					if (slot != nullptr) valStr = slot->GetString();
				}
				strFullName += paramName + wxT(" = ") + valStr;
				if (j + 1 < paramCount)
					strFullName += wxT(", ");
			}
			strFullName += wxT(")");
		}
		else {
			strFullName += wxT("<initializer>");
		}
		commandChannel.w_stringZ(byteCode->m_listCode[lCurLine].m_strDocPath);
		commandChannel.w_stringZ(strFullName);
		commandChannel.w_u32(byteCode->m_listCode[lCurLine].m_numLine + 1);
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void ibDebuggerServer::RecvCommand(void* pointer, unsigned int length)
{
	if (m_socketConnectionThread != nullptr)
		m_socketConnectionThread->RecvCommand(pointer, length);
}

void ibDebuggerServer::SendCommand(void* pointer, unsigned int length)
{
	if (m_socketConnectionThread != nullptr)
		m_socketConnectionThread->SendCommand(pointer, length);
}

//////////////////////////////////////////////////////////////////////

void ibDebuggerServer::ibDebuggerServerConnection::WaitConnection()
{
	m_waitConnection = true;
}

void ibDebuggerServer::ibDebuggerServerConnection::Disconnect()
{
	m_connectionType = m_waitConnection ?
		ConnectionType::ConnectionType_Waiter : ConnectionType::ConnectionType_Scanner;

	if (m_socket != nullptr)
		m_socket->Destroy();

	m_socket = nullptr;
}

ibDebuggerServer::ibDebuggerServerConnection::ibDebuggerServerConnection(const wxString& strHostName, unsigned short numHostPort) :
	wxThread(wxTHREAD_JOINABLE), m_waitConnection(false),
	m_connectionType(ConnectionType::ConnectionType_Unknown),
	m_strHostName(strHostName), m_numHostPort(numHostPort),
	m_acceptConnection(false),
	m_socketServer(nullptr), m_socket(nullptr)
{
}

ibDebuggerServer::ibDebuggerServerConnection::~ibDebuggerServerConnection()
{
	if (m_socket != nullptr)
		m_socket->Destroy();

	m_socket = nullptr;

	if (m_socketServer != nullptr)
		m_socketServer->Destroy();

	m_socketServer = nullptr;

	// the thread is being destroyed; make sure not to leave dangling pointers around
	if (ms_debugServer != nullptr)
		ms_debugServer->SetConnectSocket(nullptr);
}

wxThread::ExitCode ibDebuggerServer::ibDebuggerServerConnection::Entry()
{
#ifdef __WXMSW__
	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		ibJournalSysError(wxT("debugger"), hr, _("Failed to initialise COM on the debug thread"));
	}
#endif // !_WXMSW

	// Mark this OS thread as a debug worker so ibSession::Current()
	// redirects to whichever script thread is currently parked at a
	// breakpoint instead of returning nullptr (we never bind an
	// ibSession to this thread directly). Symmetric Unregister at
	// every exit path below.
	const auto debugTid = std::this_thread::get_id();
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->RegisterDebugThread(debugTid);

	ExitCode retCode = 0;

	try {
		EntryClient();
	}
	catch (...) {
		retCode = (ExitCode)1;
	}

#ifdef __WXMSW__
	if (SUCCEEDED(hr)) {
		::CoUninitialize();
	}
#endif // !_WXMSW

	// Symmetric unregister — tolerate a teardown that's already nulled
	// appData (debug listener thread can outlive ibApplicationData on the
	// crash path; we don't want to AV here on the way out).
	if (auto* reg = ibApplicationData::GetSessionRegistry())
		reg->UnregisterDebugThread(debugTid);

	if (ms_debugServer != nullptr)
		ms_debugServer->ResetDebugger();

	return retCode;
}

void ibDebuggerServer::ibDebuggerServerConnection::OnKill()
{
	if (m_socket != nullptr)
		m_socket->Destroy();

	m_socket = nullptr;

	if (m_socketServer != nullptr)
		m_socketServer->Destroy();

	m_socketServer = nullptr;

	// the thread is being destroyed; make sure not to leave dangling pointers around
	if (ms_debugServer != nullptr)
		ms_debugServer->SetConnectSocket(nullptr);
}

void ibDebuggerServer::ibDebuggerServerConnection::EntryClient()
{
	unsigned short numHostPort = m_numHostPort;

	while (true) {

		if (m_socketServer != nullptr) {
			numHostPort++;
			m_socketServer->Destroy();
			m_socketServer = nullptr;
		}

		if (numHostPort > m_numHostPort + diapasonDebuggerPort)
			break;

		wxIPV4address addr;

		addr.Hostname(m_strHostName);
		addr.Service(numHostPort);

		m_socketServer = new wxSocketServer(addr, wxSOCKET_BLOCK | wxSOCKET_WAITALL);
		m_socketServer->SetTimeout(10);

		if (m_socketServer->IsOk())
			break;
	}

	// Signal the spawning thread that the listener is now bound — the
	// wes manifest path uses this to avoid writing the URL before the
	// debug port is actually accepting connections.
	m_bindReady.store(true, std::memory_order_release);

	while (!TestDestroy()) {

		if (m_socketServer == nullptr)
			break;

		while (!TestDestroy()) {

			if (m_socketServer != nullptr && m_waitConnection) {
				m_socket = m_socketServer->Accept(true);
			}
			else if (m_socketServer != nullptr && m_socketServer->WaitForAccept(0, waitDebuggerTimeout)) {
				m_socket = m_socketServer->Accept(false);
			}

			if (m_socket != nullptr) {
				int flag = 1;
				// disable Nagle for small debugger packets — drops step-command latency
				m_socket->SetOption(IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
				// TCP keepalive so a hard-killed client (Task Manager) is detected in seconds,
				// not hours. Without this the server thread sticks on WaitForRead and the whole
				// debugger stays loaded in memory after the client process is gone.
				m_socket->SetOption(SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
			}

			if (m_socket != nullptr || m_waitConnection)
				break;
		}

		m_acceptConnection = true;

		while (!TestDestroy() && ibDebuggerServerConnection::IsConnected()) {
			if (m_socket != nullptr && m_socket->WaitForRead(0, waitDebuggerTimeout)) {
				unsigned int length = 0;
				m_socket->ReadMsg(&length, sizeof(unsigned int));
				// short read on the length header — treat as disconnect, don't parse garbage
				if (m_socket->LastCount() != sizeof(unsigned int))
					break;
				// Reject absurd packet sizes (protects against hostile/garbled client
				// sending 0xFFFFFFFF → 4 GiB wxMemoryBuffer allocation attempt → terminate).
				static const unsigned int kMaxDebugPacket = 16u * 1024u * 1024u; // 16 MiB
				if (length > kMaxDebugPacket)
					break;
				if (m_socket == nullptr)
					break;
				// No second WaitForRead before reading the payload —
				// the socket was created with wxSOCKET_BLOCK |
				// wxSOCKET_WAITALL (see m_socketServer construction
				// above; flags propagate to accepted sockets), so
				// ReadMsg blocks until every requested byte is
				// available. The previous WaitForRead(0, 50ms) gate
				// could time out when the payload was delayed by
				// even a few tens of ms (network jitter, contention
				// from multi-tab debug traffic, designer scheduler
				// hiccups). When that happened the length had already
				// been consumed but the payload was skipped, so the
				// next outer iteration read the payload's leading
				// bytes as a fresh length header — almost always
				// huge, tripping the kMaxDebugPacket guard, breaking
				// out of the loop and detaching the debugger.
				wxMemoryBuffer bufferData(length);
				m_socket->ReadMsg(bufferData.GetData(), length);
				if (m_socket->LastCount() != length)
					break;
				if (length > 0) {
#ifdef __WXMSW__
					ibValueOLE::GetInterfaceAndReleaseStream();
#endif
#if _USE_NET_COMPRESSOR == 1
					BYTE* dest = nullptr; unsigned int dest_sz = 0;
					_decompressLZ(&dest, &dest_sz, bufferData.GetData(), length);
					RecvCommand(dest, dest_sz); free(dest);
#else
					RecvCommand(bufferData.GetData(), length);
#endif
					length = 0;
				}
			}
		}

		// Connection lost (client disconnected, process killed, keepalive timeout,
		// malformed packet, etc.) — release any bytecode thread blocked in
		// DoDebugLoop and disable further breakpoint traps. Without this the
		// debuggee main thread stays parked in the CV wait forever, preventing
		// enterprise.exe from shutting down.
		if (ms_debugServer != nullptr)
			ms_debugServer->ResetDebugger();

		if (m_socket != nullptr)
			m_socket->Destroy();

		m_waitConnection = false;

		m_socket = nullptr;
		m_connectionType = ConnectionType::ConnectionType_Unknown;
	}

	if (m_socket != nullptr)
		m_socket->Destroy();

	m_waitConnection = false;
	m_acceptConnection = false;

	m_socket = nullptr;
	m_connectionType = ConnectionType::ConnectionType_Unknown;
}

void ibDebuggerServer::ibDebuggerServerConnection::RecvCommand(void* pointer, unsigned int length)
{
	ibReaderMemory commandReader(pointer, length);
	wxASSERT(ms_debugServer != nullptr);
	u16 commandFromClient = commandReader.r_u16();

	if (commandFromClient == CommandId_VerifyConnection) {

		m_connectionType = m_waitConnection ?
			ConnectionType::ConnectionType_Waiter : ConnectionType::ConnectionType_Scanner;

		ibWriterMemory commandChannel_SetConnectionType;
		commandChannel_SetConnectionType.w_u16(CommandId_SetConnectionType);
		commandChannel_SetConnectionType.w_u16(m_connectionType);
		SendCommand(commandChannel_SetConnectionType.pointer(), commandChannel_SetConnectionType.size());

		ibWriterMemory commandChannel_VerifyConnection;
		commandChannel_VerifyConnection.w_u16(CommandId_VerifyConnection);
		commandChannel_VerifyConnection.w_stringZ(activeMetaData->GetConfigGuid());
		commandChannel_VerifyConnection.w_stringZ(activeMetaData->GetConfigMD5());
		commandChannel_VerifyConnection.w_stringZ(appData->GetUserName());
		commandChannel_VerifyConnection.w_stringZ(appData->GetComputerName());
		SendCommand(commandChannel_VerifyConnection.pointer(), commandChannel_VerifyConnection.size());
	}
	else if (commandFromClient == CommandId_SetConnectionType) {
		m_connectionType = static_cast<ConnectionType>(commandReader.r_u16());
		if (m_connectionType == ConnectionType::ConnectionType_Unknown)
			ibDebuggerServerConnection::Disconnect();
	}
	else if (commandFromClient == CommandId_StartSession) {
		ibWriterMemory commandChannel;
		commandChannel.w_u16(CommandId_GetArrayBreakpoint);
		SendCommand(commandChannel.pointer(), commandChannel.size());
		m_connectionType = ConnectionType::ConnectionType_Debugger;
	}
	else if (commandFromClient == CommandId_SetArrayBreakpoint) {
		// full-replace semantics: clear stale breakpoints so a reconnect cannot accumulate duplicates
		ms_debugServer->m_listBreakpoint.clear();
		unsigned int countBreakpoints = commandReader.r_u32();
		//parse breakpoints
		for (unsigned int i = 0; i < countBreakpoints; i++) {
			unsigned int countBreakPoints = commandReader.r_u32();
			wxString strModuleName; commandReader.r_stringZ(strModuleName);
			auto& module_breakpoints = ms_debugServer->m_listBreakpoint[strModuleName];
			module_breakpoints.reserve(module_breakpoints.size() + countBreakPoints);
			for (unsigned int j = 0; j < countBreakPoints; j++) {
				module_breakpoints.push_back(commandReader.r_u32());
			}
		}
		ms_debugServer->m_bUseDebug = true;
	}
	else if (commandFromClient == CommandId_ToggleBreakpoint) {
		wxString strModuleName; commandReader.r_stringZ(strModuleName);
		unsigned int line = commandReader.r_u32();
		ms_debugServer->m_listBreakpoint[strModuleName].push_back(line);
	}
	else if (commandFromClient == CommandId_RemoveBreakpoint) {

		wxString strModuleName; commandReader.r_stringZ(strModuleName);
		unsigned int line = commandReader.r_u32();
		auto it = ms_debugServer->m_listBreakpoint.find(strModuleName);
		if (it != ms_debugServer->m_listBreakpoint.end()) {
			auto& module_breakpoint = it->second;
			module_breakpoint.erase(
				std::remove(module_breakpoint.begin(), module_breakpoint.end(), line), module_breakpoint.end());
			if (module_breakpoint.empty())
				ms_debugServer->m_listBreakpoint.erase(it);
		}
	}
	else if (commandFromClient == CommandId_AddExpression) {
		// ⭐ WHO ASKED — carried through untouched. The server does not know what a listener is and
		// has no use for this; it exists so the ANSWER can find its way back to the one window that
		// wanted it, instead of reaching every window and being sorted out there by guesswork.
		wxString strAsker;      commandReader.r_stringZ(strAsker);
		wxString strExpression; commandReader.r_stringZ(strExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		unsigned long long id = commandReader.r_u64();
#else
		unsigned int id = commandReader.r_u32();
#endif
		ibWriterMemory commandChannel;

		commandChannel.w_u16(CommandId_SetExpressions);
		commandChannel.w_stringZ(strAsker);
		commandChannel.w_u32(1); // first elements

		if (ms_debugServer->IsDebugLooped()) {
			ibValue vResult;
			//header 
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			commandChannel.w_u64(id);
#else
			commandChannel.w_u32(id);
#endif 
			//variable
			commandChannel.w_stringZ(strExpression);
			if (EvalInParkedSession(strExpression, vResult, false)) {
				commandChannel.w_stringZ(vResult.GetString());
				commandChannel.w_stringZ(vResult.GetClassName());
				//count of elemetns
				commandChannel.w_u32(vResult.GetNProps());
			}
			else {
				commandChannel.w_stringZ(ibBackendException::GetLastError());
				commandChannel.w_stringZ(wxT("<error>"));
				//count of elemetns
				commandChannel.w_u32(0);
			}
			//send expression
			SendCommand(commandChannel.pointer(), commandChannel.size());
			//set expression in map
			ms_debugServer->m_listExpression.insert_or_assign(id, strExpression);
		}
		else {
			//header 
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			commandChannel.w_u64(id);
#else
			commandChannel.w_u32(id);
#endif 
			//variable
			commandChannel.w_stringZ(strExpression);
			commandChannel.w_stringZ(wxEmptyString);
			commandChannel.w_stringZ(wxEmptyString);
			//count of elemetns 
			commandChannel.w_u32(0);
			//send expression 
			SendCommand(commandChannel.pointer(), commandChannel.size());
			//set expression in map 
			ms_debugServer->m_listExpression.insert_or_assign(id, strExpression);
		}
	}
	else if (commandFromClient == CommandId_ExpandExpression) {
		wxString strAsker;   // carried through — see CommandId_AddExpression
		commandReader.r_stringZ(strAsker);
		wxString strExpression;
		commandReader.r_stringZ(strExpression);
		if (ms_debugServer->IsDebugLooped()) {
			ibValue vResult;
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			unsigned long long id = commandReader.r_u64();
#else
			unsigned int id = commandReader.r_u32();
#endif
			if (EvalInParkedSession(strExpression, vResult, false)) {
				ibWriterMemory commandChannel;
				commandChannel.w_u16(CommandId_ExpandExpression);
				commandChannel.w_stringZ(strAsker);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
				commandChannel.w_u64(id);
#else
				commandChannel.w_u32(id);
#endif
				// Filter out scope-local props (ThisObject / ThisForm /
				// similar). They're bc-internal — must not surface in
				// the watch's expanded view of an object the user
				// reached from outside.
				const long nPropsAll = vResult.GetNProps();
				long nPropsVisible = 0;
				for (long i = 0; i < nPropsAll; i++) {
					if (!vResult.IsPropScoped(i)) ++nPropsVisible;
				}
				//count of attribute
				commandChannel.w_u32((unsigned int)nPropsVisible);

				//send varables
				for (long i = 0; i < vResult.GetNProps(); i++) {
					if (vResult.IsPropScoped(i)) continue;
					const wxString& strPropName = vResult.GetPropName(i); const long lPropNum = vResult.FindProp(strPropName);
					if (lPropNum != wxNOT_FOUND) {

						wxString strPropValue;
						wxString strPropType;

						unsigned int propCount = 0;

						try {
							if (!vResult.IsPropReadable(lPropNum))
								ibBackendCoreException::Error(_("Object field not readable (%s)"), strPropName);
							//send attribute body
							ibValue vAttribute;
							if (vResult.GetPropVal(lPropNum, vAttribute)) {

								strPropValue = vAttribute.GetString();
								strPropType = vAttribute.GetClassName();
							}
							else {
								strPropValue = ibBackendException::GetLastError();
								strPropType = wxT("<error>");
							}
							//count of attribute   
							propCount = vAttribute.GetNProps();
						}
						catch (const ibBackendException& err) {

							wxString strErrorMessage = err.GetErrorDescription();
							strErrorMessage.Replace('\n', ' ');

							//send attribute body
							strPropValue = strErrorMessage;
							strPropType = wxT("<error>");

							//count of attribute
							propCount = 0;
						}

						//send attribute body
						commandChannel.w_stringZ(strPropName);
						commandChannel.w_stringZ(strPropValue);
						commandChannel.w_stringZ(strPropType);

						//count of attribute   
						commandChannel.w_u32(propCount);
					}
					else {
						//send attribute body
						commandChannel.w_stringZ(strPropName);
						commandChannel.w_stringZ(wxT("<not found>"));
						commandChannel.w_stringZ(wxT("<error>"));
						//count of attribute   
						commandChannel.w_u32(0);
					}
				}
				SendCommand(commandChannel.pointer(), commandChannel.size());
			}
		}
	}
	else if (commandFromClient == CommandId_RemoveExpression) {
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		ms_debugServer->m_listExpression.erase(commandReader.r_u64());
#else 
		ms_debugServer->m_listExpression.erase(commandReader.r_u32());
#endif 
	}
	else if (commandFromClient == CommandId_EvalToolTip) {
		wxString strFileName, strModuleName, strExpression;
		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strModuleName);
		commandReader.r_stringZ(strExpression);
		{
			// 🛑⭐⭐ AND THE GATE ITSELF USED TO SWALLOW. "Always answer" was applied INSIDE
			// `if (IsDebugLooped())` and not to the gate around it, so a request arriving when the
			// runtime is not parked — or when the debug thread cannot resolve which session is
			// parked — produced nothing at all. Measured 2026-09-02: stopped in `BeforeStart`,
			// both `debug_evaluate` and the new sandbox timed out identically, and a timeout says
			// "the connection is broken" about a runtime that was answering `continue` perfectly.
			//
			// The same fix, one level up: work it out if it can be worked out, say so if it cannot,
			// and send either way. A gate that decides whether to REPLY is a gate that turns every
			// state it does not like into a broken link.
			//
			// 🛑 THE SEND USED TO SIT INSIDE `if (EvalInParkedSession(...))`, so an expression the
			// runtime could not evaluate produced NOTHING on the wire. For a person hovering in the
			// designer that is a tooltip that never appears — indistinguishable from hovering over
			// something that has no value. For a caller that WAITS for the reply it is a timeout,
			// and "could not be evaluated" arrives as "the runtime did not answer in time", which
			// points at the connection instead of at the expression (2026-09-01, stopped inside
			// Posting: `1 + 2 * 3` timed out exactly like a broken link).
			ibValue vResult;
			const bool parked = ms_debugServer->IsDebugLooped();
			const bool evaluated = parked && EvalInParkedSession(strExpression, vResult, false);

			ibWriterMemory commandChannel;
			commandChannel.w_u16(CommandId_EvalToolTip);
			commandChannel.w_stringZ(strFileName);
			commandChannel.w_stringZ(strModuleName);
			commandChannel.w_stringZ(strExpression);
			commandChannel.w_stringZ(evaluated
				? vResult.GetString()
				: (parked ? _("<cannot be evaluated here>")
				          : _("<the runtime is not parked at a breakpoint>")));

			SendCommand(commandChannel.pointer(), commandChannel.size());
		}
	}
	// 🛑⭐⭐ THE SANDBOX. Arbitrary code, run in the stopped runtime, INSIDE A TRANSACTION THAT IS
	// ALWAYS ROLLED BACK — the point is to change nothing (Max, 2026-09-02: *"the most important
	// thing is not to change the data"*).
	//
	// ⭐ THE ROLLBACK IS NOT A PROMISE, IT IS THE ARITHMETIC OF THE TRANSACTION COUNTER. A script
	// that opens and commits its own transaction inside this one only moves the depth 1→2→1; the
	// real DoCommit fires at the outermost level and the outermost level here is a RollBack. So
	// even code that deliberately commits is undone — which is what makes this safe to point at a
	// base that belongs to somebody.
	//
	// ⚠ AND THE PERSON IS TOLD, in their own window, before it runs. Somebody watching an
	// application they are using is entitled to know that code they did not write is executing in
	// it; an assistant working silently inside somebody's session is the thing this must never be.
	else if (commandFromClient == CommandId_RunSandbox) {

		wxString code;
		commandReader.r_stringZ(code);

		// ⚠ ANSWERED EVEN WHEN IT CANNOT RUN — see the note on the evaluation above. A caller that
		// waits deserves a sentence, and "not parked" is a sentence it can act on; silence is one
		// it can only time out on.
		if (!ms_debugServer->IsDebugLooped()) {

			ibWriterMemory refused;
			refused.w_u16(CommandId_RunSandbox);
			refused.w_u8(0);
			refused.w_stringZ(_("The runtime is not parked at a breakpoint, so there is no session and "
				"no frame for the code to run in. Nothing was run and nothing was changed."));
			refused.w_stringZ(wxEmptyString);   // …and no value, for the same reason
			refused.w_u64(0);                   // …and nothing ran, so nothing took any time

			SendCommand(refused.pointer(), refused.size());
		}
		else {

			// 🛑⭐ TOLD FROM THE APPLICATION'S OWN THREAD, NOT FROM THIS ONE. This runs on the debug
			// thread; `Message` reaches the frame, and a desktop frame draws — touching a window
			// from a thread that does not own it hangs where it does not crash, and the caller
			// waiting for the sandbox's answer sees a timeout with no clue why (measured
			// 2026-09-02: `debug_evaluate` answered `4` from this very stop while the sandbox, one
			// branch along, timed out — the difference was these two lines).
			//
			// Handed to the main thread and forgotten: the person is being told something, not
			// asked, so nothing here has any reason to wait for it.
			const auto tell = [](const wxString& text, ibStatusMessage status) {
				if (wxTheApp != nullptr)
					wxTheApp->CallAfter([text, status]() {
						ibValueSystemFunction::Message(text, status); });
			};

			tell(_("An assistant is running code here in a sandbox - nothing it writes is kept."),
				ibStatusMessage::ibStatusMessage_Information);

			// …AND WHAT IT WAS. A person who is told that "code is running" and not WHICH code has
			// been told the alarming half and none of the useful one; they are sitting in front of
			// the window it runs in, and this is what lets them follow along rather than wonder.
			tell(code, ibStatusMessage::ibStatusMessage_Information);

			wxString answer, json;
			bool ran = false;

			// ⭐⭐ HOW LONG IT TOOK, MEASURED WHERE IT RAN. The platform's own clock is a business
			// clock — a date, to the second, because that is what an accountant needs — so code
			// being tested cannot time itself, and timing it from the other end of a socket measures
			// the socket. Whoever EXECUTES is the only one holding both edges of the run.
			//
			// Monotonic on purpose: a wall clock can step (a correction, daylight saving) and a
			// measurement that can come back negative is worse than none.
			std::chrono::steady_clock::duration elapsed{};

			// 🛑⭐⭐ THE TRANSACTION BELONGS TO THE PARKED SESSION'S OWN CONNECTION, and taking it on
			// `db_query` was wrong twice over. A session owns ONE connection (session.h: *"session
			// has one conn"*), and that is the one the code about to run will write through;
			// `db_query` resolves to the CALLING THREAD's holder, which here is the debug thread —
			// a different connection entirely. So the rollback would have covered nothing the
			// script did, which is the failure mode this whole verb exists to prevent, and it is
			// SILENT: the experiment appears to work and the base keeps the writes.
			//
			// It also explains the hang that found it: asking the pool for a second connection to a
			// file base, while the parked worker holds the first, waits out the checkout timeout —
			// thirty seconds, exactly the length of the caller's wait (2026-09-02).
			std::shared_ptr<ibDatabaseLayer> layer;

			try {
				ibSession* parked = ibSession::Current();
				if (parked != nullptr)
					layer = parked->EnsureConnection();
			}
			catch (...) {
				layer = nullptr;
			}

			if (layer == nullptr) {
				answer = _("The parked session has no open connection, so there is nothing to run the "
					"code against and no transaction to undo it with. Nothing was run.");
			}
			else {
				// ⚠ THE ROLLBACK IS OUTSIDE EVERY EXIT. A throw is the LIKELY end of a piece of code
				// somebody is testing, and a transaction left open by a failed experiment would hold
				// locks on a live base until the application closed.
				layer->BeginTransaction();

				const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();

				try {
					ibValue vResult;

					// (⛔ NOTHING IS COLLECTED HERE. What the code prints goes up the debug channel
					//  as it happens — Message hands every line to SendErrorToClient now — so it
					//  arrives at the caller BEFORE this reply does, in the order it was printed.
					//  Gathering it a second time into this answer would be the same fact on two
					//  roads, and two roads diverge.)
					ran = EvalInParkedSession(code, vResult, /*compileBlock*/ true, eval_sandbox);

					// 🛑 THE REASON IS IN THE VALUE, and it was being thrown away. A failed
					// evaluation writes `<error: …>` into the result slot rather than raising
					// (procUnit.cpp, so the watch row can show it) — so reading the result only on
					// success meant every refusal arrived as an empty answer, and "it did not
					// compile" was indistinguishable from "it ran and produced nothing"
					// (measured 2026-09-02, first live run: `2 + 2;` came back blank).
					if (!ran) {
						answer = vResult.GetString();

						// ⭐ AND THE COMPILER'S OWN WORDS BESIDE IT. The value slot carries a
						// generic `<error: compile failed>` when the compile aborted before it
						// could describe itself; the description is where every other failure in
						// this process leaves it — GetLastError, which is exactly what the watch
						// channel sends (SendExpressions, above). Without it a caller is told THAT
						// their code did not compile and never WHY, which is a round trip they
						// cannot make on their own (measured 2026-09-02: `SerializeValue(42)`
						// compiled in the designer and failed here, with nothing to say why).
						const wxString said = ibBackendException::GetLastError();
						if (!said.IsEmpty())
							answer = answer.IsEmpty() ? said : (answer + wxT(" ") + said);
					}

					if (ran) {
						answer = vResult.GetString();

						// ⭐⭐ AND THE VALUE ITSELF, NOT ONLY HOW IT PRINTS. A printed form is what a
						// person reads; a caller on the other end of this socket wants the thing it
						// is made of — a selection's columns, a structure's fields — and now the
						// language has a verb for exactly that (Max, 2026-09-02: *"instead of
						// Message you just output the JSON value and take it apart on your side"*).
						//
						// ⚠ FAILING TO SERIALIZE IS NOT FAILING TO RUN. Plenty of values cannot
						// travel (IsTransferable says so), and the run they came from was still a
						// success — so this is tried separately and its failure costs the JSON
						// field, nothing else.
						try {
							json = ibValueSystemFunction::SerializeValue(vResult);
						}
						catch (...) {
							json.Clear();
						}
					}
				}
				catch (const ibBackendException& err) {
					answer = err.GetErrorDescription();
				}
				catch (...) {
					answer = _("The code failed with something that carries no description.");
				}

				// ⚠ TAKEN BEFORE THE ROLLBACK AND AFTER EVERY EXIT FROM THE TRY, so code that FAILED
				// is timed too — how long something took before it gave up is exactly the question
				// when the thing being investigated is a timeout.
				elapsed = std::chrono::steady_clock::now() - began;

				try {
					layer->RollBack();
				}
				catch (...) {
					// A rollback that cannot run is not something the caller can act on, and saying
					// nothing about the RESULT because of it would be worse.
				}

				// (⛔ NO LOCALS ARE RE-SENT. The frame's variables travel with the stop and the
				//  watch window keeps them; the code's OWN variables live in the eval's shim frame
				//  and never appear there, so a second send would repeat what the caller has and
				//  still miss what it asked for — Max, 2026-09-02, weighing this exact addition.
				//  What the code wants seen, it prints, and that lands in the window as it runs.)
			}

			// ⭐⭐ AND THE PERSON SEES HOW IT ENDED, not only that it started. Announcing the start
			// and then going quiet is the worst of both: they know something ran in their session
			// and have no idea what came of it (Max, 2026-09-02: *"show the user at least your
			// intermediate results, so they can see something is happening"*).
			//
			// The work in between shows itself: `Message` inside the code lands in this window as
			// it runs, live — which is how a caller narrates a long experiment rather than
			// producing one silent answer at the end.
			{
				const wxString shown = answer.length() > 500
					? answer.Left(497) + wxT("...") : answer;

				tell(ran
					? wxString::Format(
						_("The sandbox finished and everything it wrote was rolled back. %s"), shown)
					: wxString::Format(_("The sandbox stopped: %s"), shown),
					ran ? ibStatusMessage::ibStatusMessage_Information
					    : ibStatusMessage::ibStatusMessage_Error);
			}

			// ⚠ THE OUTCOME IS A NUMBER, NOT THE WORD "ok". A flag spelled as a string is compared as
			// a string at the other end, and a comparison by TEXT is one a rename, a translation or
			// a typo silently inverts — the wire says whether it ran, and one byte says it exactly.
			ibWriterMemory commandChannel;
			commandChannel.w_u16(CommandId_RunSandbox);
			commandChannel.w_u8(ran ? 1 : 0);
			commandChannel.w_stringZ(answer);
			commandChannel.w_stringZ(json);   // the value itself, when it could be written

			// MICROSECONDS, not milliseconds: the things worth measuring here are single queries and
			// single postings, and a millisecond field reports most of them as "0" or "1".
			commandChannel.w_u64((wxLongLong_t)
				std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());

			if (ms_debugServer->IsDebugLooped())
				SendCommand(commandChannel.pointer(), commandChannel.size());
		}
	}
	else if (commandFromClient == CommandId_SetStack) {
		unsigned int stackLevel = commandReader.r_u32();
		auto* puState = ibSession::GetPUState();
		ibRunContext* newRunContext =
			puState ? puState->GetRunContext(stackLevel) : nullptr;
		if (newRunContext) {
			// Repoint the parked session's debug run context to the
			// caller-selected stack frame, under the per-session debug mutex
			// and gated on m_debugLoop — same discipline as
			// EvalInParkedSession and the DoDebugLoop leave block. Without
			// the gate a SetStack racing a force-exit / destroy resume would
			// publish a frame the worker is about to unwind, and the
			// SendExpressions eval below would dereference it (0xdd UAF).
			ibSession* sess = ibSession::Current();
			auto* dbg = sess ? sess->Debug() : nullptr;
			if (dbg != nullptr) {
				std::lock_guard<std::mutex> lock(dbg->m_mutex);
				if (dbg->m_debugLoop.load(std::memory_order_acquire)) {
					dbg->m_runContext = newRunContext;
					// Send under the per-session lock against the just-published
					// frame — the worker is parked, so newRunContext stays live
					// for the duration of these sends.
					ms_debugServer->SendExpressions(newRunContext);
					ms_debugServer->SendLocalVariables(newRunContext);
				}
			}
		}
	}
	else if (commandFromClient == CommandId_EvalAutocomplete) {

		wxString strFileName, strModuleName, strExpression, strKeyWord;

		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strModuleName);
		commandReader.r_stringZ(strExpression);
		commandReader.r_stringZ(strKeyWord);

		s32 currPos = commandReader.r_s32();
		if (ms_debugServer->IsDebugLooped()) {
			ibValue vResult;
			if (EvalInParkedSession(strExpression, vResult, false)) {

				ibWriterMemory commandChannel;
				commandChannel.w_u16(CommandId_EvalAutocomplete);
				commandChannel.w_stringZ(strFileName);
				commandChannel.w_stringZ(strModuleName);
				commandChannel.w_stringZ(strExpression);
				commandChannel.w_stringZ(strKeyWord);
				commandChannel.w_s32(currPos);

				commandChannel.w_u32(vResult.GetNProps());
				//send varables 
				for (long i = 0; i < vResult.GetNProps(); i++) {
					const wxString& strAttributeName = vResult.GetPropName(i);
					commandChannel.w_stringZ(strAttributeName);
				}

				commandChannel.w_u32(vResult.GetNMethods());
				//send functions 
				for (long i = 0; i < vResult.GetNMethods(); i++) {
					const wxString& strMethodName = vResult.GetMethodName(i);
					const wxString& strMethodDescription = vResult.GetMethodHelper(i);
					//send attribute body
					commandChannel.w_stringZ(strMethodName);
					commandChannel.w_stringZ(strMethodDescription);
					commandChannel.w_u8(vResult.HasRetVal(i));
				}

				SendCommand(commandChannel.pointer(), commandChannel.size());
			}
		}
	}
	else if (commandFromClient == CommandId_Continue) {
		wxString sid; commandReader.r_stringZ(sid);
		ms_debugServer->m_bDebugStopLine = false;
		ms_debugServer->WakeDebugSession(sid);
	}
	else if (commandFromClient == CommandId_StepInto) {
		wxString sid; commandReader.r_stringZ(sid);
		if (ms_debugServer->IsDebugLooped()) {
			ms_debugServer->m_bDebugStopLine = true;
			ms_debugServer->WakeDebugSession(sid);
		}
	}
	else if (commandFromClient == CommandId_StepOver) {
		wxString sid; commandReader.r_stringZ(sid);
		if (ms_debugServer->IsDebugLooped()) {
			auto* puState = ibSession::GetPUState();
			ms_debugServer->m_numCurrentNumberStopContext = puState ? puState->GetCountRunContext() : 0;
			ms_debugServer->WakeDebugSession(sid);
		}
	}
	// ⭐⭐ "SHOW ME WHAT YOU ARE SEEING." The one command here that does not speak to a PARKED
	// runtime: it asks a window to draw itself, which a running application can do at any moment —
	// and that is the point, since the person is looking at the wrong list right now, not at a stop.
	//
	// 🛑 THE ANSWER IS THEIR DECISION. CaptureWindow asks the person, showing them the reason this
	// carries, and a refusal comes back as zero bytes — which the caller reports as a refusal, not
	// as a failure. Nothing on this side may photograph somebody's screen by deciding to.
	else if (commandFromClient == CommandId_Screenshot) {

		wxString reason, area, format;
		commandReader.r_stringZ(reason);
		commandReader.r_stringZ(area);
		commandReader.r_stringZ(format);

		wxMemoryBuffer png;
		wxString focus;

		// 🛑⭐⭐ NOT WHILE THE RUNTIME IS PARKED, and this is the whole nature of the thing. Every other
		// command here NEEDS the stop: the stack, the locals and the sandbox exist only because
		// execution is standing still. A picture is the opposite — a window paints itself on the main
		// thread, and while that thread sits in the debug loop there is nobody to paint it. The
		// request would simply wait out its deadline and answer "no reply", which reads as a broken
		// connection rather than as the plain fact it is (measured 2026-09-04: sixty seconds of
		// nothing, with both processes alive and well).
		//
		// So it is refused HERE, immediately, with the reason and the way out. Continue the run and
		// ask again — the window will be drawing by then.
		if (ms_debugServer->IsDebugLooped()) {
			focus = _("The application is stopped at a breakpoint, so its window is not being drawn - "
				"nothing can be captured until it runs on. Continue it and ask again.");
		}
		// 🛑⭐⭐ ON THE APPLICATION'S OWN THREAD, NEVER ON THIS ONE. This runs on the debug socket
		// thread, and a window may only be drawn by the thread that owns it — touching it from here
		// HANGS WHERE IT DOES NOT CRASH, which is exactly what it did: both processes alive, both
		// answering, and the request sitting out its full minute (measured 2026-09-04, twice).
		//
		// The same rule the sandbox already keeps two branches up for its `tell`, and the same door:
		// the session's worker pool, which on the desktop IS wxTheApp::CallAfter and runs the task
		// inline when the caller is already on the main thread.
		else if (ibSession* const session = ibSession::Current()) {

			const auto capture = [&]() {
				if (auto* frame = ibSession::CurrentFrame())
					frame->CaptureWindow(reason, area, format, png, focus);
			};

			if (ibWorkerPool* const pool = session->GetWorkerPool()) {
				try {
					// Waits for it: the answer has to be in hand before the reply is written, and the
					// far end is already waiting on us.
					pool->RunOnSession(session, capture);
				}
				catch (...) {
					focus = _("The window could not be captured.");
				}
			}
			else {
				capture();   // no pool — a host that runs everything on one thread anyway
			}
		}

		// 🛑⭐⭐ WRITTEN DOWN, ALWAYS, AND ON BOTH ANSWERS. A picture of somebody's screen is a picture
		// of their business — customers, sums, whoever they were paying — so consent alone is not
		// enough: there has to be a RECORD of what was permitted, why, and whether it happened. That
		// is what makes this auditable rather than merely polite (Max, 2026-09-04: *"my consent, an
		// entry in the journal, and only then you work with it — you cannot photograph business
		// processes uncontrollably"*).
		//
		// It goes to the ACCOUNTANT'S journal, not the engine's: this is not a technical event, it
		// is something that was done with a person's data, and that is exactly the surface an
		// auditor reads.
		if (appData != nullptr && appData->GetLogger() != nullptr) {

			const bool taken = png.GetDataLen() > 0;

			// ⚠ TWO SENTENCES, NOT ONE WITH A HOLE IN IT. The two outcomes carry different facts —
			// a size only exists for one of them — and a single format string reused for both is
			// how an argument ends up read as the wrong type.
			const wxString said = taken
				? wxString::Format(
					_("The user allowed a picture of their window to be sent to the assistant "
					  "(%u bytes). Reason given: %s"), (unsigned)png.GetDataLen(), reason)
				: wxString::Format(
					_("The user was asked for a picture of their window and DECLINED. Nothing was "
					  "captured. Reason given: %s"), reason);

			appData->GetLogger()->Audit(wxT("assistant"),
				taken ? wxT("screen.captured") : wxT("screen.refused"), said);
		}

		ibWriterMemory commandChannel;
		commandChannel.w_u16(CommandId_Screenshot);
		commandChannel.w_stringZ(focus);   // …and what they were pointing at, which is half the answer
		commandChannel.w_u32((unsigned int)png.GetDataLen());
		if (png.GetDataLen() > 0)
			commandChannel.w(png.GetData(), (u32)png.GetDataLen());

		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
	else if (commandFromClient == CommandId_Pause) {
		wxString sid; commandReader.r_stringZ(sid);
		// Soft pause: m_bDebugStopLine fires DoDebugLoop at the next
		// opcode-with-line, letting the user inspect.
		ms_debugServer->m_bDebugStopLine = true;
		// Hard escape hatch: if the script is in a tight loop without
		// line markers or in a native blocking call, the soft path
		// never fires. CancelSession flips the per-session cancel flag
		// so the interpreter throws ibBackendInterruptException at the
		// next opcode (any kind) and the script unwinds. Trade-off: on
		// a normally-running script Cancel fires before EnterDebugger,
		// so this turns Pause into abort rather than pause-and-inspect.
		// Acceptable for now — users should set breakpoints for
		// inspection; Pause is the "I gave up, stop it" button.
		if (auto* reg = ibApplicationData::GetSessionRegistry()) {
			if (auto* pool = reg->GetWorkerPool()) {
				// Find() now resolves via the live m_own map (see
				// WakeDebugSession). Fall back to the parked session (front of
				// the debug queue) so a sid drift still cancels the right
				// worker instead of silently dropping the hard-abort.
				ibSessionWatch target = reg->Find(sid);
				if (!target) target = reg->GetActiveDebugTarget();
				if (auto sess = target.Share())
					pool->CancelSession(sess.get());
			}
		}
	}
	else if (commandFromClient == CommandId_Detach) {

		ms_debugServer->m_bUseDebug = false;
		ms_debugServer->WakeAllDebugSessions();

		ibDebuggerServerConnection::Disconnect();
	}
	else if (commandFromClient == CommandId_Destroy) {

		ms_debugServer->m_bUseDebug = false;
		ms_debugServer->WakeAllDebugSessions();

		ibDebuggerServerConnection::Disconnect();

		// Destroy = process exit, but hosts can decline. wes registers a
		// keep-alive hook that returns true while user tabs are still
		// connected. Drop the gate and force-exit the parked session —
		// its ProcessRemove → NotifyDisconnect cascade is
		// what drives the OnLastDisconnect / wes exit hook chain.
		// Note: CoUninitialize() is already done in Entry() epilogue
		// (line ~472). Doing it again here would give a double-uninit
		// on the worker thread. The forced close breaks the parked script
		// out of its loop and takes the window down with it — desktop
		// closes its main frame, a web tab kicks itself.
		if (auto* s = ibSession::Current())
			s->Close(true);
	}
	else if (commandFromClient == CommandId_DeleteAllBreakpoints) {
		ms_debugServer->m_listBreakpoint.clear();
	}
}

void ibDebuggerServer::ibDebuggerServerConnection::SendCommand(void* pointer, unsigned int length)
{
	// Serialise the two-step WriteMsg pair — multiple web sessions can
	// emit on the wire concurrently (e.g. parallel breakpoint hits or
	// LeaveLoop emissions when several tabs are F5'd at once). Without
	// the lock, header bytes from one sender mix with payload bytes
	// from another and the designer parser drops the connection on the
	// next garbled frame.
	std::lock_guard<std::mutex> lk(m_sendMutex);
#if _USE_NET_COMPRESSOR == 1
	BYTE* dest = nullptr; unsigned int dest_sz = 0;
	_compressLZ(&dest, &dest_sz, pointer, length);
	if (m_socket && m_socket->IsOk()) {
		m_socket->WriteMsg(&dest_sz, sizeof(unsigned int));
		m_socket->WriteMsg(dest, dest_sz);
	}
	free(dest);
#else
	if (m_socket && ibDebuggerServerConnection::IsConnected()) {
		m_socket->WriteMsg(&length, sizeof(unsigned int));
		m_socket->WriteMsg(pointer, length);
	}
#endif
}