////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - server part
////////////////////////////////////////////////////////////////////////////

#include "debugServer.h"

#include "backend/compiler/procUnit.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/workerPool.h"

#include "backend/fileSystem/fs.h"
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
bool EvalInParkedSession(const wxString& expr, ibValue& vResult, bool compileBlock)
{
	ibSession* sess = ibSession::Current();
	auto* dbg = sess ? sess->Debug() : nullptr;
	if (dbg == nullptr)
		return false;

	std::lock_guard<std::mutex> lock(dbg->m_mutex);
	if (!dbg->m_debugLoop.load(std::memory_order_acquire) || dbg->m_runContext == nullptr)
		return false;

	return ibProcUnit::Evaluate(expr, dbg->m_runContext, vResult, compileBlock);
}
} // namespace

ibDebuggerServer::ibDebuggerServer() :
	m_bUseDebug(false), m_bDoLoop(false), m_bDebugStopLine(false),
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
	ibSession* sess = reg->Find(sessionGuid);

	// Fallback: the session actually parked at the breakpoint (front of the
	// debug queue). Guards against sid drift so Continue / Step never strand
	// the parked worker — the failure mode that surfaced when the global flag
	// was removed.
	if (sess == nullptr) {
		if (auto sp = reg->GetActiveDebugTarget()) sess = sp.get();
	}
	if (sess == nullptr) return;

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
		commandChannelEnterLoop.w_stringZ(strDocPath);
		commandChannelEnterLoop.w_stringZ(strModuleName);
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
	dbg->m_debugLoop = false;

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
		commandChannelLeaveLoop.w_stringZ(strDocPath);
		commandChannelLeaveLoop.w_stringZ(strModuleName);
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

			m_bDoLoop = false;

			//step into 
			if (m_bDebugStopLine && byteCode.m_numLine >= 0)
			{
				m_bDebugStopLine = false;
				m_bDoLoop = true;
			}
			// step through
			else if (auto* st = ibSession::GetPUState();
				st && m_numCurrentNumberStopContext && m_numCurrentNumberStopContext >= st->GetCountRunContext() && byteCode.m_numLine >= 0)
			{
				m_numCurrentNumberStopContext = st->GetCountRunContext();
				m_bDoLoop = true;
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

						m_bDoLoop = list_current_breakpoint_iterator != list_current_breakpoint.end();
					}
				}
			}

			if (m_bDoLoop)
				DoDebugLoop(byteCode.m_strFileName, byteCode.m_strDocPath, byteCode.m_numLine + 1, runContext);
		}

		numPrevLine = byteCode.m_numLine;
	}
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
		// using m_strRealName + m_listParamRealName + m_listParam.
		const ibByteCode::ibByteFunction* fn = runContext->m_currentFunction;
		if (fn != nullptr) {
			strFullName += fn->m_strRealName.IsEmpty()
				? wxString(wxT("<fn>"))
				: fn->m_strRealName;
			strFullName += wxT("(");
			const size_t paramCount = fn->m_listParam.size();
			const long frameVarCount = runContext->GetLocalCount();
			for (size_t j = 0; j < paramCount; j++) {
				const wxString& paramName = (j < fn->m_listParamRealName.size())
					? fn->m_listParamRealName[j]
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
	wxThread(wxTHREAD_JOINABLE), m_socket(nullptr), m_socketServer(nullptr),
	m_connectionType(ConnectionType::ConnectionType_Unknown),
	m_strHostName(strHostName), m_numHostPort(numHostPort),
	m_waitConnection(false), m_acceptConnection(false)
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
		wxLogSysError(hr, _("Failed to create an instance in thread!"));
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
		wxString strExpression; commandReader.r_stringZ(strExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		unsigned long long id = commandReader.r_u64();
#else 
		unsigned int id = commandReader.r_u32();
#endif 
		ibWriterMemory commandChannel;

		commandChannel.w_u16(CommandId_SetExpressions);
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
		if (ms_debugServer->IsDebugLooped()) {
			ibValue vResult;
			if (EvalInParkedSession(strExpression, vResult, false)) {
				ibWriterMemory commandChannel;
				commandChannel.w_u16(CommandId_EvalToolTip);
				commandChannel.w_stringZ(strFileName);
				commandChannel.w_stringZ(strModuleName);
				commandChannel.w_stringZ(strExpression);
				commandChannel.w_stringZ(vResult.GetString());
				if (ms_debugServer->IsDebugLooped()) {
					SendCommand(commandChannel.pointer(), commandChannel.size());
				}
			}
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
		ms_debugServer->m_bDoLoop = false;
		ms_debugServer->WakeDebugSession(sid);
	}
	else if (commandFromClient == CommandId_StepInto) {
		wxString sid; commandReader.r_stringZ(sid);
		if (ms_debugServer->IsDebugLooped()) {
			ms_debugServer->m_bDebugStopLine = true;
			ms_debugServer->m_bDoLoop = false;
			ms_debugServer->WakeDebugSession(sid);
		}
	}
	else if (commandFromClient == CommandId_StepOver) {
		wxString sid; commandReader.r_stringZ(sid);
		if (ms_debugServer->IsDebugLooped()) {
			auto* puState = ibSession::GetPUState();
			ms_debugServer->m_numCurrentNumberStopContext = puState ? puState->GetCountRunContext() : 0;
			ms_debugServer->m_bDoLoop = false;
			ms_debugServer->WakeDebugSession(sid);
		}
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
				ibSession* sess = reg->Find(sid);
				if (sess == nullptr) {
					if (auto sp = reg->GetActiveDebugTarget()) sess = sp.get();
				}
				if (sess != nullptr)
					pool->CancelSession(sess);
			}
		}
	}
	else if (commandFromClient == CommandId_Detach) {

		ms_debugServer->m_bUseDebug =
			ms_debugServer->m_bDoLoop = false;
		ms_debugServer->WakeAllDebugSessions();

		ibDebuggerServerConnection::Disconnect();
	}
	else if (commandFromClient == CommandId_Destroy) {

		ms_debugServer->m_bUseDebug =
			ms_debugServer->m_bDoLoop = false;
		ms_debugServer->WakeAllDebugSessions();

		ibDebuggerServerConnection::Disconnect();

		// Destroy = process exit, but hosts can decline. wes registers a
		// keep-alive hook that returns true while user tabs are still
		// connected. Drop the gate and just Close(true) the parked
		// session — its ProcessRemove → NotifyDisconnect cascade is
		// what drives the OnLastDisconnect / wes exit hook chain.
		// Note: CoUninitialize() is already done in Entry() epilogue
		// (line ~472). Doing it again here would give a double-uninit
		// on the worker thread. Per-kind OnForceExit dispatches:
		// GUI desktop session quits wx; web per-tab session just
		// kicks itself.
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