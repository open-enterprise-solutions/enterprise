#include "webApplication.h"

#include <iostream>
#include <iomanip>
#include <wx/log.h>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <dbghelp.h>
#	include <psapi.h>
#	pragma comment(lib, "dbghelp.lib")
#	pragma comment(lib, "psapi.lib")
#endif

#include "backend/backend_exception.h"

#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"
#include "backend/metaCollection/metaObject.h"

#include "webClientSession.h"
#include "webFrame.h"
#include "webChildFrame.h"
#include "webWindow.h"

#include "visualView/ctrl/form.h"
#include "visualView/ctrl/widgets.h"
#include "visualView/visualHostClient.h"

namespace {

#if defined(_WIN32)

// Resolve fault address → "module.dll+0xRVA (function)" via dbghelp. Called
// from the SEH __except filter; kept out of the __try block because SymXxx
// uses C++ internally and we don't want it tangled with SEH unwinding.
// Best-effort: if SymInitialize / SymFromAddr fails we fall back to the
// raw "module+RVA" form.
static void ReportFaultAddress(const char* label, DWORD code, void* addr)
{
	HANDLE proc = GetCurrentProcess();

	// Which module owns this address?
	HMODULE mod = nullptr;
	char    modName[MAX_PATH] = "?";
	MODULEINFO modInfo = {};
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
						 | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						 static_cast<LPCSTR>(addr), &mod) && mod != nullptr)
	{
		GetModuleBaseNameA(proc, mod, modName, sizeof(modName));
		GetModuleInformation(proc, mod, &modInfo, sizeof(modInfo));
	}

	const DWORD_PTR base = reinterpret_cast<DWORD_PTR>(modInfo.lpBaseOfDll);
	const DWORD_PTR rva  = base != 0 ? (reinterpret_cast<DWORD_PTR>(addr) - base) : 0;

	// Try symbol lookup. SymInitialize is idempotent-ish when invoked with
	// fInvadeProcess=TRUE (loads all module symbols once) — first call per
	// process does the work, subsequent calls are cheap.
	static bool s_symInit = false;
	if (!s_symInit) {
		SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
		s_symInit = SymInitialize(proc, nullptr, TRUE) != FALSE;
	}

	char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
	SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen   = 511;

	DWORD64 displacement = 0;
	const bool haveSym = s_symInit && SymFromAddr(proc,
		reinterpret_cast<DWORD64>(addr), &displacement, sym) != FALSE;

	IMAGEHLP_LINE64 line = {};
	line.SizeOfStruct = sizeof(line);
	DWORD lineDisp = 0;
	const bool haveLine = s_symInit && SymGetLineFromAddr64(proc,
		reinterpret_cast<DWORD64>(addr), &lineDisp, &line) != FALSE;

	std::cerr << "[app] " << label << " SEH: code=0x"
		<< std::hex << code
		<< " at 0x" << addr
		<< " (" << modName << "+0x" << rva << ")";
	if (haveSym) {
		std::cerr << " " << sym->Name << "+0x" << displacement;
	}
	std::cerr << std::dec;
	if (haveLine) {
		std::cerr << " [" << line.FileName << ":" << line.LineNumber << "]";
	}
	std::cerr << std::endl;
}

#endif // _WIN32

// SEH wrapper. The interior C++ exceptions (CallAsProc catches (...) itself)
// are handled by the backend; what escapes is structured — access violations
// when the script call path dereferences a desktop-only singleton. /EHsc
// does NOT translate SEH into C++ exceptions, so a plain try/catch upstream
// never sees them. We grab the code + fault address here and log them so
// the crash can be mapped to a function via the PDB / .map file.
//
// The helper has no C++ locals with destructors — SEH handlers can't unwind
// C++ objects cleanly unless compiled with /EHa, and we rely on /EHsc.
static bool StartMainModuleSafe(ibValueModuleManagerConfiguration* mgr)
{
#if defined(_WIN32)
	DWORD  code = 0;
	void*  addr = nullptr;
	__try {
		return mgr->StartMainModule();
	}
	__except (code = GetExceptionCode(),
			  addr = GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
			  EXCEPTION_EXECUTE_HANDLER)
	{
		ReportFaultAddress("StartMainModule", code, addr);
		return false;
	}
#else
	return mgr->StartMainModule();
#endif
}

static bool ExitMainModuleSafe(ibValueModuleManagerConfiguration* mgr)
{
#if defined(_WIN32)
	DWORD  code = 0;
	void*  addr = nullptr;
	__try {
		return mgr->ExitMainModule();
	}
	__except (code = GetExceptionCode(),
			  addr = GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
			  EXCEPTION_EXECUTE_HANDLER)
	{
		ReportFaultAddress("ExitMainModule", code, addr);
		return false;
	}
#else
	return mgr->ExitMainModule();
#endif
}

} // anonymous namespace

ibSession* ibWebApplication::GetSessionContext() const
{
	// Implicit upcast ibWebClientSession* -> ibSession*. Out-of-line
	// because the header only forward-declares ibWebClientSession.
	return m_sessionContext;
}

ibWebApplication::ibWebApplication() = default;

ibWebApplication::~ibWebApplication()
{
	if (m_initialized)
		OnExit();
}

ibValueModuleManagerConfiguration* ibWebApplication::GetManagerModule() const
{
	// Per-session — runtime mm now lives on ibSession (formerly on
	// metadata; that virtual is gone). Pull the current session's mm,
	// which is already typed as ibValueModuleManagerConfiguration*.
	ibSession* session = ibSession::Current();
	return session ? session->GetManagerModule() : nullptr;
}

ibWebApplication* currentWebApp()
{
	// Current session's frame on web is its ibWebFrame — reach directly
	// through the session, no process/thread-local singleton.
	ibSession* session = ibSession::Current();
	if (session == nullptr) return nullptr;
	auto* frame = dynamic_cast<ibWebFrame*>(session->GetFrame());
	return frame != nullptr ? frame->GetApp() : nullptr;
}

bool ibWebApplication::OnInit()
{
	std::cerr << "[app] OnInit begin" << std::endl;
	if (activeMetaData == nullptr)
		return false;

	ibValueMetaObjectConfiguration* common = activeMetaData->GetCommonMetaObject();
	if (common == nullptr)
		return false;

	// Frame first — scripts fired from StartMainModule (OnStart
	// handlers, early OpenForm calls, page-load code) can already
	// reach backend_mainFrame and register open documents.
	(void)common;  // pulled from metadata inside the shared moduleManager
	m_frame = new ibWebFrame(this);
	std::cerr << "[app] frame created" << std::endl;

	// Publish the frame on the session so script-side calls into
	// ibBackendValueForm::CreateNewForm / FindFormByUniqueKey resolve
	// through ibSession::CurrentFrame() instead of throwing
	// "Context functions are not available!".
	if (m_sessionContext != nullptr)
		m_sessionContext->SetFrame(m_frame);

	// No per-session moduleManager any more — the shared Configuration
	// lives in metadata (compiled once at wfrontendInit). AttachRuntime
	// ran in ibWebSession::Login right before OnInit, so session's
	// m_procUnitMap is already populated.
	ibValueModuleManagerConfiguration* sharedMgr = GetManagerModule();
	if (sharedMgr == nullptr) {
		wxLogWarning(wxT("ibWebApplication::OnInit: no shared moduleManager"));
		delete m_frame;
		m_frame = nullptr;
		return false;
	}

	// Fire the main module's OnStart — that's where the configuration's
	// startup script runs and typically calls OpenForm(…) one or more
	// times to land the user on the default form(s). StartMainModule
	// also serves as the veto point for the session: a BeforeStart
	// script handler can reject the start, in which case we tear the
	// runtime down and report a failed login.
	//
	// Historically this segfaulted on web inside the script call path
	// with a raw access violation that bypasses /EHsc try/catch. The
	// StartMainModuleSafe helper wraps it in SEH so the server survives
	// and logs the fault address, which lets us narrow the desktop-only
	// singleton still being dereferenced in the web build.
	// OnStart/BeforeStart resolve the session's ProcUnit through
	// ibValueModuleManager::GetProcUnit() — that delegate consults
	// ibSession::Current(), which the worker thread has installed
	// via ibSessionScope (and the bootstrap thread did the same before
	// calling OnInit). No runtime passed explicitly.
	std::cerr << "[app] StartMainModule: begin" << std::endl;
	const bool startOk = StartMainModuleSafe(sharedMgr);
	std::cerr << "[app] StartMainModule: " << (startOk ? "ok" : "failed/vetoed") << std::endl;
	if (!startOk) {
		wxLogWarning(wxT("ibWebApplication::OnInit: StartMainModule returned false"));
		delete m_frame;
		m_frame = nullptr;
		return false;
	}

	// Worker dispatch goes through the process-wide ibWorkerPool
	// (appData->GetWorkerPool()) — no per-session thread to start.
	// Per-session FIFO + lease inside the pool preserves the script
	// invariant that one task at a time runs on m_sessionContext.

	m_initialized = true;
	return true;
}

ibVisualHostClient* ibWebApplication::GetActiveHost() const
{
	if (m_frame == nullptr || m_frame->TabCount() == 0)
		return nullptr;
	ibWebDocChildFrame* tab = m_frame->Tab(m_frame->ActiveTab());
	return tab != nullptr ? tab->GetHost() : nullptr;
}

bool ibWebApplication::Dispatch(int controlId, const wxString& kind, const wxString& value)
{
	ibVisualHostClient* host = GetActiveHost();
	if (host == nullptr)
		return false;
	ibValueForm* form = host->GetValueForm();
	if (form == nullptr)
		return false;
	ibValueFrame* ctrl = form->FindControlByID(controlId);
	if (ctrl == nullptr)
		return false;

	// Host maintains the (ibValueFrame → wxObject) map in m_baseObjects
	// — same storage desktop uses, populated by the walker on web and
	// by GenerateControl on desktop. The web walker only inserts
	// ibWebWindow* entries (sizers skip the map; they don't receive
	// HTTP events), so the lookup result is guaranteed to be an
	// ibWebWindow when non-null — static_cast is safe.
	wxObject* obj = host->GetWxObject(ctrl);
	if (obj == nullptr)
		return false;
	ibWebWindow* web = static_cast<ibWebWindow*>(obj);

	if (!web->HandleRequest(kind, value))
		return false;

	// Handler chain unwound — safe to actually destroy any tabs the
	// script closed while we were deep in ProcessPendingEvents. Drain
	// happens HERE, not inside CloseForm, so the toolbar/control that
	// bubbled the event no longer sits on the stack when its parent
	// tab's host gets torn down.
	if (m_frame != nullptr)
		m_frame->DrainPendingCloses();

	// No explicit rebuild here: unified control Update methods push
	// property changes through setters on existing ibWebWindow nodes
	// (mirror of how desktop wx propagates through its window tree),
	// so the tree already reflects the post-script state. Next ToJSON
	// emits the fresh JSON from the same tree — no Clear+Create pass
	// needed. CreateAndUpdateVisualHost belongs only in
	// ibFormVisualEditView::OnCreate (first build after form open).
	// MarkDirty wakes any SSE subscriber so the new JSON ships out.
	MarkDirty();
	return true;
}

void ibWebApplication::MarkDirty()
{
	m_seq.fetch_add(1, std::memory_order_acq_rel);
	std::lock_guard<std::mutex> lk(m_seqMtx);
	m_seqCv.notify_all();
}

uint64_t ibWebApplication::WaitForChange(uint64_t lastSeen, int timeoutMs)
{
	std::unique_lock<std::mutex> lk(m_seqMtx);
	if (timeoutMs <= 0) {
		m_seqCv.wait(lk, [this, lastSeen]{ return m_seq.load() != lastSeen; });
	} else {
		m_seqCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
			[this, lastSeen]{ return m_seq.load() != lastSeen; });
	}
	return m_seq.load();
}

void ibWebApplication::PostWork(std::function<void()> fn)
{
	// Hand off to the session — it forwards through the registry's
	// worker pool. Per-session FIFO + lease inside the pool preserves
	// the "single in-flight task per session" invariant that the old
	// per-session worker thread provided. The pool worker binds the
	// session via ibSessionScope at lease start, so script-side
	// Current()/GetPUState() see this session.
	if (m_sessionContext == nullptr) return;
	// Discard the future — PostWork is fire-and-forget; exceptions are
	// captured in the promise state, which we ignore here.
	(void)m_sessionContext->Submit(std::move(fn));
}

void ibWebApplication::OnExit()
{
	if (!m_initialized)
		return;

	// Timers now live on the form (ibValueForm::m_idleHandlerArray) —
	// same ownership model as desktop; the form's dtor / CloseDocForm
	// path stops and deletes them. No separate app-level teardown.

	// Close every still-open tab on the WORKER thread. DeleteAllViews
	// runs ibView::Close → ibFormVisualEditView::OnClose → potentially
	// ibValueForm::CloseDocForm, which fires `beforeClose`/`onClose`
	// scripts through ibProcUnit. procUnit is session-thread-only; doing
	// it on the HTTP thread (the one that reached OnExit via
	// ibSessionRegistry::Destroy → ~ibWebSession) would hand procUnit a
	// thread_local state set up for the session's worker and crash on
	// deref. Drain via RunOnWorker(...).get() before DropSession below.
	if (m_frame != nullptr) {
		RunOnWorker([this]() {
			if (m_frame == nullptr) return true;
			const std::size_t n = m_frame->TabCount();
			for (std::size_t i = 0; i < n; ++i) {
				ibWebDocChildFrame* tab = m_frame->Tab(i);
				if (tab == nullptr) continue;
				if (auto* doc = dynamic_cast<ibFormVisualDocument*>(tab->GetDocument()))
					doc->DeleteAllViews();
			}
			return true;
		}).get();
	}

	// Pool's per-session queue entry is dropped automatically by the
	// session registry when ProcessRemove fires (downstream of
	// ibWebSession::Close). No explicit teardown needed here.

	// Shared moduleManager lives in metadata — don't Destroy it, just
	// fire per-session Exit events. GetProcUnit() in the event handlers
	// delegates through ibSession::Current(), which the worker
	// thread's ibSessionScope has set up.
	if (auto* sharedMgr = GetManagerModule()) {
		std::cerr << "[app] ExitMainModule: begin" << std::endl;
		ExitMainModuleSafe(sharedMgr);
		std::cerr << "[app] ExitMainModule: done" << std::endl;
	}

	if (m_frame != nullptr) {
		std::cerr << "[app] delete m_frame: begin" << std::endl;
		// Tabs already closed above; m_frame's m_tabs should be empty.
		// ~ibWebFrame's DeleteAllViews loop is an idempotent no-op here.
		// Detach from session first so any late ibSession::CurrentFrame()
		// lookup doesn't see a freed pointer.
		if (m_sessionContext != nullptr)
			m_sessionContext->SetFrame(nullptr);
		delete m_frame;
		m_frame = nullptr;
		std::cerr << "[app] delete m_frame: done" << std::endl;
	}

	std::cerr << "[app] OnExit: complete" << std::endl;
	m_initialized = false;
}
