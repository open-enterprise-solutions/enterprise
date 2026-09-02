#ifndef __IB_SESSION_H__
#define __IB_SESSION_H__

// ibSession — per-session record owned by ibSessionRegistry. Identity
// (guid, user, computer, app mode, started) + runtime state machine
// (lifecycle / auth) + script bindings (module manager, ProcUnit map).
//
// Renamed from ibSessionContext as part of the session-registry
// refactor. ibSessionScope / Current() stay available as legacy shims
// during migration — direct ibSession pointer passing (via ibProcUnit
// etc.) is the target, thread_local Current() is deprecated.

#include "backend/backend.h"
#include "backend/userInfo.h"
#include "backend/appData.h"      // ibRunMode
#include "backend/backend_exception.h"
#include "backend/compiler/value.h" // ibValue (base for ibValuePtr)
#include "backend/compiler/procUnitState.h"   // ibProcUnitState — per-session interpreter swap target
#include "backend/value_ptr.h"    // ibValuePtr for m_root
#include "backend/databaseLayer/connectionHolder.h"   // ibDatabaseConnectionHolder for m_dbHolder
#include "backend/databaseLayer/connectionScope.h"    // ibConnectionScope for OpenScope return type

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

class ibValueModuleManager;
class ibValueModuleManagerRuntimeConfiguration;
class ibRuntimeModuleDataObject;
class ibProcUnit;

#include "backend/databaseLayer/connectionHolder.h"
struct ibRunContext;
class ibMetaData;
class ibValueMetaObjectConfiguration;
class ibAccessPolicy;   // RLS — the L3 door pulls it opaquely; concrete impl is session-side
class BACKEND_API ibBackendDocFrame;

// ------------------------------------------------------------------
// ibSessionState — lifecycle axis. Advances strictly forward through
// Created → Added → Stopping → Gone, with Rejected as a terminal
// branch out of Created. Auth axis lives in ibAuthState separately.
// ------------------------------------------------------------------
enum class ibSessionState : int {
	Created   = 0,  // object constructed, not yet submitted to registry
	Added     = 1,  // policies passed, row in sys_session, lock held
	Rejected  = 2,  // terminal: policy veto / DbError / duplicate guid
	Stopping  = 3,  // ~ticket or Kick in progress
	Gone      = 4,  // row DELETED, lock released, safe to destroy
};

// ------------------------------------------------------------------
// ibAuthState — auth axis, independent of lifecycle. A session can be
// Anonymous (anonymous phase while the login dialog / cookie is open,
// or technical session that never attaches a user) and switch to
// Authenticated via Attach. AuthFailed is non-terminal — retry is
// legal within the same session.
// ------------------------------------------------------------------
enum class ibAuthState : int {
	Anonymous     = 0,
	Authenticated = 1,
	AuthFailed    = 2,  // retry allowed; session still Added
};

// ------------------------------------------------------------------
// ibSessionKind — sessions-layer enum. Shares numeric values with
// ibRunMode for the 1:1 cases (Launcher/Designer/Enterprise/Service)
// so casts round-trip; splits the web case into two distinct session
// roles that both share ibRunMode::eWEB_RUNTIME_MODE as the host
// process's run mode. Physically only the wes process runs — inside
// it sessions come in two flavours:
//   WebServer  — the process's own technical sys_session row
//   WebClient  — per-tab / per-API-caller connections
// Desktop binaries populate their corresponding session kind directly;
// SessionKindFromRunMode is the default for unambiguous cases and
// returns WebClient for eWEB_RUNTIME_MODE (the common per-tab case).
// ------------------------------------------------------------------
enum class ibSessionKind : int {
	Launcher   = eLAUNCHER_MODE,       // 1
	Designer   = eDESIGNER_MODE,       // 2
	Enterprise = eRUNTIME_MODE,     // 3
	Service    = eSERVICE_MODE,        // 4
	WebServer  = eWEB_RUNTIME_MODE, // 5 — wes process technical row
	WebClient  = 100,                  // per-tab / API caller
	// A job's own session. Like WebClient these live OUTSIDE the run-mode range,
	// because a job is not a way of running the process: any host can hold one
	// alongside its normal sessions, so "what kind of session is this" stops
	// being answerable from how the process was started.
	//
	// THREE kinds rather than one, because an administrator looking at Active
	// Users has a different decision for each. A stuck BackgroundJob has a user
	// waiting on it and a form to tell. A stuck ScheduledJob belongs to the
	// configuration — someone wrote it, and it will come back on its interval
	// whether or not this run is killed. A stuck SystemJob is the engine's own
	// housekeeping, which is safe to kill precisely because it is housekeeping
	// (a skipped fold costs a slightly wider read and nothing else). Collapsing
	// them into one row type would hide exactly the distinction that decides
	// whether to wait or to kick.
	BackgroundJob = 101,   // started by hand from script, under the caller's identity
	ScheduledJob  = 102,   // declared by the configuration, runs on its interval
	SystemJob     = 103,   // the platform's own (totals fold, maintenance)

};

// IS THIS SESSION A JOB — one of the three above, whatever host it lives in.
//
// Worth one question because a job's session carries the APP MODE of whoever
// started it: a run inside designer.exe says eDESIGNER_MODE, and anything reading
// the app mode to decide "is this a designer" counts it as one. It is not — it is a
// job that happens to live there. The KIND is what answers, and this is the
// shorthand for asking.
inline bool IsJobSessionKind(ibSessionKind k) {
	return k == ibSessionKind::BackgroundJob
	    || k == ibSessionKind::ScheduledJob
	    || k == ibSessionKind::SystemJob;
}

inline ibSessionKind SessionKindFromRunMode(ibRunMode m) {
	// Web run mode is ambiguous at this layer — default to WebClient
	// (the per-tab common case). Callers that need WebServer set the
	// kind explicitly (see ibSessionRegistry::CreateSessionWithFactory).
	if (m == eWEB_RUNTIME_MODE) return ibSessionKind::WebClient;
	return static_cast<ibSessionKind>(m);
}

// ------------------------------------------------------------------
// ibPriority — request priority for ibSessionRegistry's single-consumer
// queue. Strict descending: all Urgent drained before any Normal, etc.
// Default for Submit is Normal; callers escalate explicitly when
// needed (Remove/Kick/Stop get Urgent; SetActivity gets Low).
// ------------------------------------------------------------------
enum class ibPriority : int {
	Urgent     = 0,
	Normal     = 1,
	Low        = 2,
	Background = 3,
};

// ------------------------------------------------------------------
// ibSessionIdentity — immutable (after Add) descriptor of who/what
// this session represents. Populated by the producer at Connect time;
// the registry never mutates it except through explicit Attach (which
// fills user fields).
// ------------------------------------------------------------------
struct BACKEND_API ibSessionIdentity {
	ibGuid       m_guid;              // PK in sys_session.session
	wxString     m_userName;          // empty = anonymous phase / technical
	wxString     m_userGuid;           // sys_user row, empty before Attach
	wxString     m_computer;          // hostname
	wxString     m_address;            // "host:port" for web; "" for desktop
	ibRunMode    m_appMode;            // eENTERPRISE / eDESIGNER / eWEB_ENTERPRISE / ...
	wxDateTime   m_started;
	int          m_pid = 0;            // OS pid — for kick / attach debugger
	bool         m_expectsAnonPhase = true;  // true: INSERT on Add; false: INSERT deferred to Attach success
};

// ------------------------------------------------------------------
// ibSession — the record itself.
// Fields split into three groups:
//   - identity: immutable post-Add (except userInfo via Attach)
//   - state: atomic axes + activity string under mutex
//   - runtime: per-session ProcUnit map + module manager + raw password
//
// Single-consumer registry thread reads/writes identity+runtime
// freely; producers interact via the registry queue. State transitions
// are notified via m_cv so WaitState() can block producer threads
// safely.
//
// Inherits std::enable_shared_from_this so Authenticate() can submit
// Attach requests to the registry without an ibSessionTicket in the
// call path — the request's shared_ptr<ibSession> is minted via
// shared_from_this(), safe because every ibSession instance is created
// via std::make_shared (registry path) or the typed factory.
// ------------------------------------------------------------------
class BACKEND_API ibSession
	: public std::enable_shared_from_this<ibSession> {
public:
	// kind = what this session does in the running process. The process-
	// level run mode (how the exe was launched) lives on appData and is
	// the same for every session inside a process — not duplicated here.
	ibSession(wxString id, ibSessionKind kind);

	// Virtual — ibGUISession (desktop) and ibWebClientSession (per web
	// tab) hang off this. The holder is the only strong reference (the
	// registry indexes weakly); the concrete dtor runs through the
	// virtual chain when that holder is released.
	virtual ~ibSession();

	ibSession(const ibSession&)            = delete;
	ibSession& operator=(const ibSession&) = delete;

	// The window driving this session, for backend callers that need to
	// reach the UI (CurrentFrame, script-side CreateNewForm). Each kind
	// answers from wherever its window already is — the desktop pair from
	// its main-window singleton, a web client from its tab — so nothing
	// is stored here and there is no registration step to get wrong.
	// Default null: daemon, codeRunner, the wes technical row have no UI.
	virtual ibBackendDocFrame* GetFrame() const { return nullptr; }

	// Session-owned auth orchestration. Submits Attach to the registry
	// directly (via shared_from_this so no ticket is required in the call
	// path). If CLI creds already work, returns true silently — no prompt
	// event. On Attach failure raises OnShowAuthenticate — GUI override
	// shows the login dialog, writes singleton userInfo / rawPassword,
	// returns true — then re-submits Attach with the stored session creds.
	// Returns true iff the auth axis transitioned to Authenticated.
	// Non-GUI sessions inherit the default OnShowAuthenticate (false) so
	// the fallback no-ops and Authenticate reports the original failure;
	// GUI app OnInit terminates the process in that case.
	//
	// Tri-state result. Callers used to treat `bool == false` as "show
	// error", which surfaced "Authentication failed" even when the user
	// just clicked Cancel on the login dialog. Distinguishing the two
	// lets the GUI app exit silently on cancel and only message on a
	// real auth failure.
	enum class OpenResult {
		Authenticated,   // creds accepted (silent or via dialog)
		Failed,          // creds rejected — show "Authentication failed"
		Cancelled,       // user cancelled the interactive dialog — silent exit
	};
	OpenResult Open(const wxString& user, const wxString& password);

	// Interactive prompt event — fires only when silent Attach fails.
	// Overridden by ibGUISession (shared for designer + enterprise; shows
	// the wx login dialog) and future ibWebClientSession (HTTP login
	// form). Base no-op returns false so non-GUI sessions fail hard on
	// wrong creds without hanging on a non-existent prompt.
	virtual bool OnShowAuthenticate(const wxString& /*user*/, const wxString& /*password*/) { return false; }

	// Legacy id (string representation of m_guid or external cookie).
	// New code should prefer m_identity.m_guid directly.
	const wxString&    GetId()   const { return m_id; }
	ibSessionKind      GetKind() const { return m_kind; }

	// Session-wide lambda executor — pure accessor; the runtime itself
	// is allocated inside CompileRoot() right after AttachRuntime,
	// once m_root's procUnit is live. ibValueFunction::Execute swaps
	// the runtime's m_pByteCode to info->parentBc for the dispatch
	// and restores after; ibProcUnit::Execute snapshots m_pByteCode
	// at entry so re-entrant lambdas don't clobber the outer view.
	ibProcUnit* GetLambdaRuntime() { return m_lambdaRuntime.get(); }

	// ⭐⭐ STATE THAT LIVES ONCE PER SESSION — ASKED FOR BY ITS TYPE.
	//
	// A subsystem that needs something per session (the live reference table is the first: one
	// reference object per identity, so a row is read once however many cells name it) asks here for
	// its own type and gets the one belonging to this session, made on first use and destroyed with
	// it. Nothing is declared in advance and nothing is registered by name.
	//
	// ⭐ THE SESSION DOES NOT KNOW WHAT IT IS HOLDING, deliberately. A named member per subsystem
	// would make this header the list of everything that happens to want per-session state — and a
	// list like that is only ever right on the day it is written; the next subsystem adds a second
	// member, then a third, and the session gains a dependency on each of their headers. The type is
	// the key, so a new one costs nothing here and is impossible to collide with.
	//
	// ⚠ NOT LOCKED, and it does not need to be: a session is leased to one worker at a time (see
	// workerPoolHeadless.h), so there is never a second thread inside it.
	template <class T>
	std::shared_ptr<T> Local(bool createIfMissing = true)
	{
		const std::type_index key(typeid(T));
		std::shared_ptr<void> slot = FindLocal(key);
		if (!slot && createIfMissing) {
			slot = std::make_shared<T>();
			SetLocal(key, slot);
		}
		return std::static_pointer_cast<T>(slot);
	}

	// Root runtime of this session. Populated by CreateRoot() driven from
	// the registry's NotifyAuthenticated phase right after Open() succeeds;
	// stays nullptr for sessions that never run scripts (Designer,
	// WebServer technical session, Launcher).
	ibValueModuleManagerRuntimeConfiguration* GetManagerModule() const;

	// The RLS access policy for this session. The L3 door (ibDataQueryBuilder)
	// pulls it opaquely in its ctor and applies it to every read/write. Null on
	// Designer / technical sessions (no enforcement). Created at authentication
	// (EnsureRoot) for runtime sessions; the concrete impl lives in session.cpp.
	// Returns null inside an ibAccessTrustScope (a role module runs privileged).
	const ibAccessPolicy* GetAccessPolicy() const;

	// The module manager whose context (Manager / Catalogs / Documents / globals)
	// an object/record/module compiled against `metaData` should parent to. One
	// seam, two roads: Designer returns the lightweight designer manager held in
	// `metaData`'s compile cache (no runtime root exists in the Designer); runtime
	// returns this session's root mm. Callers that previously wrote
	// `session->GetManagerModule()` + a DesignerMode branch use this instead.
	// Out-of-line (session.cpp) — needs the complete designer type.
	class ibValueModuleManager* GetEditModuleManager(const class ibMetaData* metaData) const;

	// Convenience: resolve against ibSession::Current() (the common call shape at
	// InitializeObject sites). Null-safe when there's no current session.
	static class ibValueModuleManager* EditModuleManagerFor(const class ibMetaData* metaData);

	// Create the session's root module manager. The configuration's
	// commonMetaObject is taken directly from metaData (typed accessor —
	// no dynamic_cast needed). Returns a pointer for immediate use;
	// nullptr on failure. Calling twice replaces the old root — previous
	// ibValuePtr releases its ref (delete-if-last) after running
	// DestroyMainModule on it. CompileRoot is separate so callers can
	// register common modules in metadata's storage between the two.
	ibValueModuleManagerRuntimeConfiguration* CreateRoot(class ibMetaDataConfigurationBase* metaData);

	// End this session — by asking whatever holds it to go. Close does
	// NOT dismantle anything itself; it is the backend's equivalent of
	// the user pressing [X].
	//
	//   Close()      — try. The owner runs its own close path and may
	//                  refuse (unsaved document, BeforeExit script);
	//                  then nothing happened and this returns false.
	//   Close(true)  — force. Nobody is asked.
	//
	// The teardown follows on its own: the owner dies, its holder is
	// released, and that release is what ends the session. So closing a
	// window never has to notify the session — the release says it.
	bool Close(bool force = false);

	// Drop the user identity bound to this session. Auth axis transitions
	// back to Anonymous; the session itself stays Added so the caller can
	// retry Authenticate. Best-effort with a soft timeout.
	void Detach(std::chrono::milliseconds timeout = std::chrono::seconds(5));

	// Fire-and-forget activity label update — submits SetActivity@Low so
	// it never preempts real Add / Remove work. Registry handler UPDATEs
	// `sys_session.currentActivity`; admin UI sees it on the next snapshot.
	void SetActivity(const wxString& activity);

	// Server session — back-link from a server-spawned client to the
	// session that hosts it. Set by the holder right after the client is
	// registered in the registry (e.g. wes's WebClient session points to
	// wes's WebServer system session). Empty for standalone sessions
	// (desktop main, wes system itself, codeRunner) — Server() returns
	// nullptr.
	//
	// Used by:
	//   - shutdown logic: a server checks "do I still have clients?"
	//     before declining process exit (replaces the pre-2026-04-26
	//     Count() > 2 magic number on wes's keep-alive hook);
	//   - cluster topology: walking up the chain identifies which node
	//     of the cluster a session is currently homed on;
	//   - admin UI: discriminates "client of server X" vs "standalone"
	//     in Active Users / sys_session listings.
	//
	// weak_ptr so a server's premature death doesn't dangle clients;
	// children's Server().lock() returns nullptr after the server is gone.
	std::shared_ptr<ibSession> Server() const { return m_server.lock(); }
	void SetServer(ibSession* server) {
		if (server != nullptr) m_server = server->shared_from_this();
		else                   m_server.reset();
	}

	// Exclusive (monopoly) mode — at most one session in the registry
	// holds it at a time. While held, every other Connect parks in
	// ibSessionRegistry::m_pendingExclusive and only resumes when this
	// session releases or closes.
	//
	// SetExclusive(true)  — submits SetExclusive@Normal, waits for the
	//                       registry handler. Throws ibBackendCoreException
	//                       on rejection: another session already holds
	//                       exclusive, or other live sessions are still
	//                       attached to the registry (acquisition needs
	//                       this session to be the sole live one).
	// SetExclusive(false) — release. No-op when not currently exclusive.
	//                       Drains parked Adds.
	bool IsExclusive() const { return m_exclusive.load(std::memory_order_acquire); }
	void SetExclusive(bool on);

	// Compile the root mm — runs CreateMainModule on the allocated
	// m_root. Called after metadata->RunDatabase() has populated common-
	// module descriptors in metadata's ibModuleStorage. Returns false
	// if root isn't allocated or compile fails.
	// Runs the configuration's session module (SetSessionParameters) inside a trusted
	// window, before the access policy is built — the policy filters by what it sets.
	// Every kind of session passes through here, including jobs, which never see
	// beforeStart / onStart.
	void SetSessionParameters();
	bool CompileRoot();

	// Symmetric teardown — DestroyMainModule on the root mm without
	// dropping it. Used by callers that want to tear down compile state
	// before metadata-side close cascade. ClearRoot also runs this
	// internally before resetting m_root.
	bool DestroyRoot();

	// Drop the session's root explicitly. Must be called before the
	// process-level metadata tree is torn down (ibMetaData::CloseDatabase),
	// otherwise the root's refs to metadata descriptors dangle. No-op
	// when the session never had a root.
	void ClearRoot();

	// Idempotent CreateRoot driven by the active process-level metadata.
	// Called by ibSessionRegistry::NotifyAuthenticated between the
	// OnFirstConnect phase (which may run metadataCreate, populating
	// activeMetaData) and the OnAuthenticated phase (whose listeners —
	// e.g. RunDatabase → OnBeforeRunMetaObject — need session->mm to
	// already exist). No-op when m_root already set or activeMetaData null.
	void EnsureRoot();

	// Read access — single overload, const only. The non-const reference
	// overload was removed to enforce "userInfo is mutated only via
	// SetUserInfo on the registry thread"; external callers (script-side
	// AppUser readers, GUI title-bar updaters) must not mutate fields
	// directly because that would race with concurrent SetUserInfo calls
	// on registry thread without any synchronization.
	//
	// Thread safety. Writes (SetUserInfo) run only on the registry thread
	// during ProcessAttach (Authenticated transition) or ProcessDetach
	// (Authenticated -> Anonymous transition). Readers from any thread
	// observe Auth() == Authenticated before relying on the contents:
	// pre-Auth the struct is empty by ctor; post-Detach the struct is
	// reset to empty and the script worker is supposed to stop. The
	// gap window during which a script could see a partially-assigned
	// wxString is narrow and bounded by the auth state machine; if a
	// stricter guarantee is needed later, switch m_userInfo to
	// shared_ptr<const ...> with atomic_load/store.
	const ibUserInfo& GetUserInfo() const { return m_userInfo; }

	// Plain-text password cached for the Designer "Start debugging" path —
	// the spawned child re-authenticates without prompting. Public read
	// because the writer (SetSessionRawPassword in registry-driven
	// InstallUser) is single-threaded; readers observe Auth() ==
	// Authenticated before relying on the value.
	const wxString& GetSessionRawPassword() const { return m_sessionRawPassword; }

	// Cancellation flag — async hint to interrupt a long-running script
	// on this session. Pool's CancelSession (or admin Kick on a busy
	// session) sets it; the interpreter checks it at loop boundaries
	// inside ibProcUnit::Execute and throws ibBackendInterruptException
	// when set. Atomic so the cancel request can come from any thread
	// while the script thread reads on its hot loop. Cleared at the
	// start of every Execute so a stale set from a prior task doesn't
	// interrupt the next one.
	void RequestCancel()           { m_cancelRequested.store(true,  std::memory_order_release); }
	void ClearCancel()             { m_cancelRequested.store(false, std::memory_order_release); }
	bool IsCancelRequested() const { return m_cancelRequested.load(std::memory_order_acquire); }

	// The flag ITSELF, for work that polls instead of running bytecode.
	// The Firebird Services API is the reason this exists: a sweep or a
	// backup/restore cycle sits in its own poll loop for up to 30 minutes
	// and never reaches an interpreter loop boundary, so the one signal
	// it can watch is this address. The flag lives in the session, which
	// outlives the task running on it — the pool raises it in Stop()
	// before waiting for the workers, and the poll bails within one tick.
	const std::atomic<bool>* CancelFlag() const { return &m_cancelRequested; }

	// Force-exit flag — "voluntary kick" of this session. The interpreter
	// breaks out of its loop at the next iteration and the window is told
	// hears OnClose(true) — no questions asked. Atomic +
	// cooperative (the script thread checks the flag); blocking I/O won't
	// notice. Kept distinct from Cancel because cancel says "interrupt
	// this task" while force-exit says "stop running on this session for
	// the rest of its life". Normally you call Close(true) instead, which
	// does both in the right order.
	void RequestForceExit();
	bool IsForceExit() const { return m_forceExit.load(std::memory_order_acquire); }

	// Eval-mode flag — set during debug-watch / Eval evaluation so
	// side-effecting calls (UpdateForm, dialogs, OLE calls) self-suppress.
	// Per-session because two concurrent web sessions can each be in/out
	// of eval independently — a debug-watch on tab 1 must not silence
	// tab 2's regular OnWrite. Replaces the thread_local gs_evalMode in
	// backend_exception.cpp.
	// ⭐ ONE ANSWER, NOT A PAIR. This returns the KIND (backend_core.h) and `eval_none` is zero, so
	// the old `if (IsEvalMode())` reads exactly as before while a caller that cares WHICH kind can
	// compare. A separate Get/Is pair would be two names for one fact, and they drift.
	ibEvalMode IsEvalMode()  const { return m_evalMode.load(std::memory_order_acquire); }
	void SetEvalMode(ibEvalMode m) { m_evalMode.store(m, std::memory_order_release); }

	// ⭐⭐ …AND WHETHER THIS EVALUATION MAY CHANGE ANYTHING — the question the WRITE gates ask.
	//
	// 🛑 THEY USED TO ASK IsEvalMode, and that answered for two different things at once: a watch,
	// which must never write or fire a handler, and the sandbox, whose entire purpose is to write
	// and be undone. BeginWriteScope and its record-set twin returned false under eval mode, so a
	// document Write() from the sandbox answered nothing, left the Ref empty, and reported no error
	// at all (measured 2026-09-02, trying to post a receipt).
	//
	// The safety that remains is the real one: a sandbox runs inside a transaction that is always
	// rolled back.
	bool IsEvalSandbox() const { return IsEvalMode() == eval_sandbox; }

	// Processing-backend-error flag — re-entrancy guard for
	// ibBackendException::ProcessError so a logging path can't re-throw
	// into itself. Same per-session rationale as eval-mode.
	bool IsProcessingBackendError()       const { return m_processingBackendError.load(std::memory_order_acquire); }
	void SetProcessingBackendError(bool m)      { m_processingBackendError.store(m, std::memory_order_release); }

	// Configuration-language code for this session — selects which
	// metadata synonym / form-label translation is shown. Distinct from
	// the platform's wxLocale (UI gettext, process-wide via --locale=).
	// Set after auth from the user's preferred language (or the config's
	// default when the user has none); script can override via the
	// CurrentLanguage() builtin. Empty => fall back to the process-wide
	// default (ibBackendLocalization::GetUserLanguage), which is what
	// pre-auth and headless contexts use.
	//
	// Hot path: this getter is hit per metadata-synonym lookup, hundreds
	// of times during a single form open. m_resolvedLanguageCode is the
	// pre-computed answer — refreshed only when the override or the user
	// record changes (SetLanguageCode / SetUserInfo). Inline + by-const-ref
	// keeps the read at one field load with no logic.
	const wxString& GetLanguageCode() const { return m_resolvedLanguageCode; }
	void            SetLanguageCode(const wxString& code) {
		m_languageCode = code;
		m_resolvedLanguageCode = code.IsEmpty() ? m_userInfo.m_strLanguageCode : code;
	}

protected:
	// "This session is closing" — the one event, and it is about the
	// SESSION, not about a window. Not every session has a window: a
	// background worker running scheduled jobs is a perfectly good
	// session with no UI at all, and it hears this the same way.
	//
	// Each kind does what closing means for it: the desktop pair closes
	// its main frame, a web client closes its tab, a job runner stops
	// taking work, a plain headless session does nothing and inherits
	// the default. Whatever it does, the holder release that follows is
	// what actually ends the session.
	//
	// Start the close of whatever owns this session. Kinds that HAVE
	// something to close (window, tab) do exactly that and no more —
	// their teardown arrives with the holder release that follows, so the
	// owner is never left holding a session that has already been
	// dismantled.
	//
	// The default is the other case: nothing to close. Then there is no
	// owner whose death would release a holder — the holder sits in plain
	// code (daemon's scope, a job runner, wes's technical global) — so
	// this IS the end and the session ends here.
	//
	// Returning false means "not now": nothing happened and the caller
	// may try again. Under force the answer is not asked for.
	// A FORCED CLOSE PUTS OUT THE WORK FIRST. Whoever has no window to close still has a worker that
	// may be draining a task, and tearing the session down around a running body is how a job's
	// Job.<name> claim ends up held by nobody. Cancelling first means the body unwinds (the
	// interpreter checks between opcodes, a native pass through the session's cancel flag) and the
	// teardown below then waits behind an idle queue instead of a live one.
	//
	// This is what makes an admin kick sensible on a session that is not a seat: the kick calls
	// Close(true) on whatever the session is, and each kind answers for itself — a desktop session
	// closes its frame, a web client destroys its tab, and one with neither stops its work and ends.
	// Nothing above has to know which is which.
	virtual bool OnClose(bool force);

public:

	// Which pool runs THIS session's tasks. Virtual because the answer
	// belongs to the session kind, not to the process: one process can
	// hold an interactive session that must stay on the UI thread and
	// background / scheduled sessions that must not. Branching on kind
	// inside Submit would put that knowledge in the wrong place.
	//
	// Base answer is the registry's pool. Returning nullptr is a valid
	// answer and means "run inline on the calling thread" — which is what
	// the desktop GUI session does, keeping script on the wx main thread.
	virtual class ibWorkerPool* GetWorkerPool() const;

	// Submit a task to run on the session's worker. Routed through
	// GetWorkerPool() above, so pool ownership stays on the registry
	// while the CHOICE of pool stays with the session. When that
	// resolves to no pool the task runs inline on the calling thread
	// and the returned future is fulfilled before Submit returns.
	std::future<void> Submit(std::function<void()> task);

	// (A read that must leave this thread does NOT get a door here. It is a RENTED
	//  background run — ibJobManager::StartBackground with ibJobTenancy::Tenant —
	//  which gives a read the one thing it cannot borrow (a connection, since this
	//  session owns exactly one and it is busy) and borrows everything else: no
	//  identity, no runtime, no row, and THIS session's access policy, so it sees
	//  exactly what this session sees. Run once, gone.
	//
	//  An earlier attempt kept a per-window reader session alive between portions
	//  to save the start-up cost. It was removed: a session held open is one that
	//  can sit in Active Users holding a connection with nobody able to tell
	//  working from stuck, and a run that ends by construction cannot.)

	// Per-session "working date" — the conceptual business-date used by
	// script's WorkingDate() helper (reports, document registration,
	// etc.). Initialized to the session-creation wall-clock; scripts
	// that want a pinned past/future date call SetWorkDate. Replaces
	// the legacy static ibValueSystemFunction::ms_workDate so two web
	// sessions in the same process don't step on each other's value.
	//
	// Returned by value (not const ref) so a concurrent SetWorkDate can
	// never race with a long-lived caller-side reference. wxDateTime is
	// a small POD-like value, copy is cheap. Both Get and Set are
	// expected to be called from the per-session script thread (single
	// in-flight per session), so the copy itself is also race-free in
	// practice — value semantics document the invariant.
	wxDateTime GetWorkDate()         const { return m_workDate; }
	void       SetWorkDate(const wxDateTime& d) { m_workDate = d; }

	// Per-session interpreter state slot — currentRunModule, runContext
	// stack, errorPlace, recCount. Single source of truth for the script
	// interpreter; ibProcUnit forwarders go through this method which
	// resolves to Current()'s session each call. The session-binding
	// primitives (ibSessionScope, BindSessionToThread) update the
	// thread→session map; GetPUState() then sees the new state visible
	// transparently — no separate cache or activation step.
	//
	// On debug-server worker threads Current() redirects to whichever
	// session is parked at a breakpoint (see m_debugQueue on
	// ibSessionRegistry); GetPUState() therefore yields the parked
	// session's stack/locals to debug eval handlers without an explicit
	// sid threaded through.
	//
	// Returns nullptr when no session is bound on this thread.
	static ibProcUnitState* GetPUState() { return PUStateOf(Current()); }

	// THE SAME ANSWER, WHEN THE CALLER ALREADY HOLDS THE SESSION.
	//
	// GetPUState() is Current() plus a member address, and Current() is a
	// shared_lock on a shared_mutex, a thread-id hash into an unordered_map and a
	// weak_ptr::lock — two atomic read-modify-writes at least. A caller that has
	// just called Current() for its own reasons should not pay for it twice.
	//
	// `ibProcUnit::Execute` did exactly that: it resolved Current() for the cancel
	// flag and then GetPUState() for the state, and the ibProcStackGuard built one
	// line earlier resolved it a third time — three lookups per call for one
	// answer that cannot change while the call runs.
	static ibProcUnitState* PUStateOf(ibSession* session);

	// State accessors — lock-free reads.
	ibSessionState State() const { return m_state.load(std::memory_order_acquire); }
	ibAuthState    Auth()  const { return m_auth.load(std::memory_order_acquire); }

	const ibSessionIdentity& Identity() const { return m_identity; }

	// Diagnostic string set by the registry thread on Rejected / AuthFailed
	// transitions. Read after State() / Auth() changes to report the
	// reason to producers. Returned by value to avoid exposing the mutex.
	wxString Reason() const;

	// Same string, set WITHOUT a state change — for a close that owes the
	// user an explanation. An admin kick writes it here before Close(true);
	// the frontend's force-exit listener shows it and stays silent when it
	// is empty (an ordinary process shutdown force-closes too, and that one
	// explains itself by the user having asked for it).
	void SetReason(const wxString& reason);

	// Access mode — set once by the application at startup, before any
	// session is created.
	//
	//   Single — the process runs exactly one session for its entire life
	//            (designer.exe, enterprise.exe, daemon.exe, codeRunner.exe). Current() returns the lone session
	//            regardless of the calling thread; bindings are recorded
	//            for diagnostics but lookup ignores them.
	//
	//   Shared — per-thread lookup with a process-wide fallback.
	//            wenterprise-server.exe — workers serving a tab register
	//            their session under their thread id; the wes process's
	//            own system session is registered via SetFallback and
	//            served to any thread that isn't a tab worker (registry
	//            consumer, signal handlers, etc.).
	enum class AccessMode { Single, Shared };

	static void       SetAccessMode(AccessMode mode);
	static AccessMode GetAccessMode();

	// Canonical "session this code is currently working on". Lookup
	// strategy depends on AccessMode (see above).
	static ibSession* Current();

	// Shared-mode fallback — session returned by Current() when the
	// calling thread isn't bound. Effective only when AccessMode == Shared.
	static void SetFallback(ibSession* s);
	static void ClearFallback();

	// Diagnostic — given a thread id, what session is currently scoped on
	// that thread? In Single mode returns the lone session; in Multi mode
	// returns the bound session or the fallback.
	static ibSession* GetByThread(std::thread::id tid);

	// Explicit bind — pin a session under an arbitrary thread id without
	// going through ibSessionScope. Use cases: eval / parallel-execute
	// scenarios where one session is shared across worker threads, or
	// pre-binding a session for a thread that hasn't started yet.
	// Pairs with UnbindThread; not RAII — caller owns lifetime.
	static void BindSessionToThread(ibSession* s, std::thread::id tid);
	static void UnbindThread(std::thread::id tid);

	// Erase every thread-binding pointing to this session, regardless of
	// which thread put it there. Used by registry-thread teardown
	// (OnDisconnect listener) where the original binding-thread isn't
	// available — find by session pointer instead.
	static void UnbindSession(ibSession* s);

	// Diagnostic — atomic snapshot of the full thread→session map. The
	// k_singletonKey entry (default-constructed thread::id) appears in
	// the snapshot when singleton mode is active. Snapshot is by-value;
	// safe to iterate after returning.
	static std::vector<std::pair<std::thread::id, ibSession*>> SnapshotByThread();

	// Convenience: frame of the currently-scoped session. Single
	// canonical entry point for backend code reaching the process's
	// main UI window. Equivalent to:
	//   ibSession* s = Current(); return s ? s->GetFrame() : nullptr;
	// Null when no scope is active or when the scoped session is
	// frameless (web-server, headless, codeRunner).
	static ibBackendDocFrame* CurrentFrame();

	// Convenience: whether the currently-scoped session has been
	// force-exited. Returns false when no session is bound. Drop-in
	// replacement for the legacy process-level ibApplicationData::
	// IsForceExit() at frontend / GUI startup checks.
	static bool IsCurrentForceExit() {
		auto* s = Current();
		return s != nullptr && s->IsForceExit();
	}

private:
	friend class ibSessionScope;
	friend class ibSessionRegistry;
	friend class ibSessionHolder;   // releasing the holder tears us down
	friend class ibJobManager;      // mints an unlisted session for a rented read

	// WAS THIS SESSION EVER TAKEN IN BY THE REGISTRY?
	//
	// A rented read is minted straight (ibJobManager::StartBackground with
	// ibJobTenancy::Tenant): it takes no row, passes no policy and answers no
	// lookup, so Add has nothing to do for it — and Add is not free. It is a
	// handshake with the consumer thread plus, inside ProcessAdd, a cluster-snapshot
	// refresh (a SELECT over sys_session), paid on the thread that asked, per
	// scrolled page.
	//
	// Nothing taken in means nothing to give back: Teardown skips the Remove, which
	// would otherwise fire the disconnect listeners — an audit row per page for a
	// session nobody was ever told about.
	bool m_listed = true;
	void SetUnlisted() { m_listed = false; }

	// The teardown: quiesce the worker, then Remove@Urgent so the
	// registry drops us, DELETEs the sys_session row and fires
	// OnDisconnect (where DetachRuntime + DestroyRoot live). Reached ONLY
	// by releasing the owning holder — that is what makes "the owner
	// died, so the session died" true by construction rather than by
	// convention, and why nothing else needs to report a close.
	void Teardown();
	friend class ibAccessTrustScope;   // toggles m_accessTrusted (RLS privileged window)

	wxString       m_id;
	ibSessionKind  m_kind;

	// Registry-only API. Takes m_mtx, mutates the state machine, and
	// cv.notify_all so producers waiting on WaitForState* wake up.
	// Call sites outside ibSessionRegistry's single-consumer thread are
	// bugs by construction — that's why the friend declaration above is
	// the only access path.
	void Transition(ibSessionState next, const wxString& reason = wxEmptyString);
	void TransitionAuth(ibAuthState   next, const wxString& reason = wxEmptyString);

	// Block the calling thread until the lifecycle state changes away
	// from `from` (or timeout). Returns the new state on success, `from`
	// on timeout. Producers (registry-internal only) use this to wait for
	// Add/Attach to settle.
	ibSessionState WaitForState(ibSessionState from, std::chrono::milliseconds timeout);
	ibAuthState    WaitForAuth (ibAuthState    from, std::chrono::milliseconds timeout);

	// Identity / sys_session-row tracking. Identity is filled in by the
	// registry as the session moves through Add → Attach; the inserted
	// flag tracks whether a sys_session row has actually been INSERTed
	// (relevant when creds-at-Connect defers INSERT to Attach success).
	void SetIdentity(const ibSessionIdentity& id) { m_identity = id; }
	bool Inserted()      const { return m_inserted; }
	void SetInserted(bool v)   { m_inserted = v; }

	// Auth-flow mutators. Driven only through ibSessionRegistry façades
	// (InstallUser, EnableDebugForSession) — registry is the single
	// mutator of session state, callers from appData / login dialogs
	// route through it. SetSessionRawPassword writes the plain-text
	// cache used by the Designer "Start debugging" child spawn; the
	// matching read accessor stays public (registry-thread is the sole
	// writer, reads from any thread are race-free against atomic-flag
	// observation of Auth() == Authenticated).
	void SetUserInfo(const ibUserInfo& info) {
		m_userInfo = info;
		// Refresh cached language: explicit SetLanguageCode override
		// wins; otherwise the new user's preferred language.
		if (m_languageCode.IsEmpty())
			m_resolvedLanguageCode = info.m_strLanguageCode;
	}
	void SetSessionRawPassword(const wxString& pwd) { m_sessionRawPassword = pwd; }
	void ClearSessionRawPassword() { m_sessionRawPassword.clear(); }
	void EnableDebug()  { if (!m_debug) m_debug = std::make_unique<ibDebugSession>(); }
	void DisableDebug() { m_debug.reset(); }

public:
	// Per-session debug state. nullptr -> session is not being debugged
	// (ibProcUnit::Execute skips breakpoint checks fast). Allocated by
	// EnableDebug() when --debug starts the session or designer attaches;
	// destroyed in DisableDebug() / ~ibSession. Migrated here from
	// process-level ibDebuggerServer fields so concurrent web sessions
	// in wenterprise-server can each enter their own debug loop without
	// blocking the others.
	struct ibDebugSession {
		// True while ibProcUnit::Execute is parked in DoDebugLoop's CV
		// wait. Designer's Continue/Step/Detach commands clear this and
		// notify the CV for the matching session.
		std::atomic<bool>       m_debugLoop{false};

		// Run context of the script frame currently stopped at the
		// breakpoint. Eval / locals / stack on the debugger side resolve
		// through this.
		ibRunContext*           m_runContext{nullptr};

		// CV + mutex pair: producer (script thread inside DoDebugLoop)
		// waits on m_cv; consumer (designer command handler in the
		// debug-server worker) flips m_debugLoop and notifies.
		std::condition_variable m_cv;
		std::mutex              m_mutex;

		// Per-session watch expressions (id -> source). Breakpoints
		// stay process-level on ibDebuggerServer because module bytecode
		// is shared across sessions.
		std::map<unsigned long long, wxString> m_expressions;
	};

	// Thread safety for the four accessors below.
	//
	// EnableDebug runs exactly once per session, on the registry thread,
	// inside the OnAuthenticated listener BEFORE TransitionAuth flips
	// m_auth to Authenticated. IsDebug / Debug are read by debug-server
	// connection threads, web HTTP handlers, and ibProcUnit on script
	// threads — all of which only run after the session reaches
	// Authenticated. Happens-before via the auth state machine
	// (release-store on m_auth followed by acquire-load) makes the
	// non-atomic m_debug write visible to readers; no explicit mutex.
	//
	// DisableDebug is currently unused — the unique_ptr is released by
	// ~ibSession. Kept on the API for future "detach debugger mid-
	// session" scenarios; if it gains a real caller, the timing
	// argument above no longer holds and m_debug needs atomic
	// shared_ptr or a mutex.
	// Public reads — used by debug-server worker threads and HTTP handlers
	// to discover whether a session is attached for debugging and to
	// access its watch list / debug-loop CV. Mutators (EnableDebug /
	// DisableDebug) are restricted to the auth flow — see private block
	// further down with friend ibApplicationData.
	bool IsDebug() const     { return m_debug != nullptr; }
	ibDebugSession* Debug()  { return m_debug.get(); }

	// Drop the debug-park flag and notify the per-session CV so any
	// script worker stopped at a breakpoint inside DoDebugLoop wakes
	// up immediately and unwinds. Used by session-destroy paths (web
	// F5 → ibWebSession::OnExit → worker join) so the parked thread
	// doesn't hold the worker indefinitely while the destroy waits to
	// release the registry slot. No-op if the session isn't being
	// debugged.
	void WakeDebugLoop();

	// --- Database connection façade ----------------------------------
	// Composition over inheritance: session OWNS a connection holder
	// rather than BEING one. Each session has ONE connection; runtime
	// descriptor work all funnels through EnsureConnection. The raw
	// holder pointer is exposed for pool's CurrentHolder cast and for
	// scope-binding from ibConnectionScope.
	//
	// AcquireFreeConnection is intentionally NOT mirrored on session —
	// "session has one conn" invariant. Background subsystems that
	// genuinely need a side-channel conn (meta-watcher polling,
	// registry's separate write/probe holders) declare their own
	// holders and call AcquireFreeConnection on those directly.
	std::shared_ptr<class ibDatabaseLayer> EnsureConnection() {
		return m_dbHolder.EnsureConnection();
	}
	ibConnectionScope OpenConnectionScope() {
		return m_dbHolder.OpenConnectionScope();
	}
	ibDatabaseConnectionHolder*       Holder()       { return &m_dbHolder; }
	const ibDatabaseConnectionHolder* Holder() const { return &m_dbHolder; }

	// Static — backs the ses_query macro. Resolves to
	// Current()->EnsureConnection() with explicit-failure throws if
	// no session is bound or the conn isn't open.
	static std::shared_ptr<class ibDatabaseLayer> DatabaseLayer();

private:
	// Owned holder identity — session uses its own holder for pool
	// reservations (TX pin / scope binding). Const-mutable not needed:
	// every method that touches m_dbHolder is non-const.
	//
	// SELF-CLEANING ON PURPOSE. ibSingleConnectionHolder's dtor releases every
	// reservation this holder took out of the pool; the plain base's dtor is
	// trivial and releases nothing. EnsureConnection BINDS the checked-out entry to
	// this holder and nothing else ever unbinds it — no scope object is involved —
	// so with the base type a session that had ever touched the database left its
	// entry marked "bound", pointing at a holder that no longer exists. Checkout
	// skips a bound entry and the idle reaper never reclaims one, so the connection
	// was lost to the pool for the life of the process.
	//
	// Invisible while a session was one-per-window. Fatal the moment a session is
	// created per scrolled page (a rented read): thirty-odd portions drain a
	// 32-connection pool, and everything after that waits out its checkout timeout
	// before failing. The registry's own write holder is an ibSingleConnectionHolder
	// for exactly this reason.
	ibSingleConnectionHolder m_dbHolder;
	// Root runtime — intrusive-refcounted owner (ibValuePtr is the
	// project convention for ibValue-derived types). Nested descriptors
	// (common modules, object instances, forms) parent up through
	// m_parent chain. See project_runtime_facade_plan.md.
	ibValuePtr<ibValueModuleManagerRuntimeConfiguration> m_root;
	
	// nullptr unless the session was created with debug attached.
	std::unique_ptr<ibDebugSession> m_debug;

	// RLS — the concrete session-side policy (created at auth)
	std::unique_ptr<ibAccessPolicy> m_accessPolicy;

	// RLS trusted window. While set, GetAccessPolicy() returns null (bypass)
	// EVEN THOUGH m_accessPolicy is real: a role module runs privileged, so any
	// query its body spins up (reading the very source it restricts) does not
	// re-enter RLS. Toggled ONLY through ibAccessTrustScope (RAII save/restore —
	// survives a handler throw). Per-session, never process-global: a trusted
	// window on one web session must not lift enforcement on another.
	bool m_accessTrusted = false;

	// SESSION PARAMETERS — declared in metadata, filled once by the session module,
	// read everywhere. Keyed by the parameter's NAME, which is what a script writes.
	//
	// They live on the SESSION and nowhere else: two users signed in at the same
	// moment work under different organisations, and a process-wide store would let
	// one of them answer for the other. Isolation here is structural, not a rule
	// anybody has to keep.
	std::map<wxString, ibValue> m_sessionParameters;

	// WRITABLE ONLY WHILE THE SESSION MODULE RUNS. Not "frozen afterwards" — closed
	// by default, opened for the length of that one call and closed again:
	//
	//     read       — always, from anywhere
	//     write      — only inside SetSessionParameters
	//     write else — raises, before and after alike
	//
	// This is the whole protection, and it needs no rights to enforce. Row access is
	// filtered by these values, so a later assignment — from a report a user wrote
	// themselves, say — would be a way around the policy. "Nobody may write them"
	// cannot be got around by running under a different role, while "only the right
	// code may" would have to be checked, and every check has a way past it.
	//
	// It RAISES rather than ignoring the write: a silently dropped assignment leaves
	// a configuration author certain the value was set, and the row filter says
	// otherwise somewhere far away.
	bool m_sessionParametersOpen = false;

public:

	// Read one, by the name a script used. Answers an empty value for a name that
	// was never declared — the caller sees Undefined, which is what an unset
	// parameter is.
	ibValue GetSessionParameter(const wxString& name) const;
	// Write one. Refused (raises) once the session module has returned — see the
	// freeze note above.
	void SetSessionParameter(const wxString& name, const ibValue& value);

private:

	// Lambda executor — see GetLambdaRuntime() for semantics. Allocated
	// in CreateRoot; SetParent(m_root's procUnit) is wired lazily on
	// first GetLambdaRuntime() call once m_root's procUnit exists.
	std::unique_ptr<ibProcUnit> m_lambdaRuntime;

	// Per-session state, keyed by the asking type — see Local() above. The map holds the only owning
	// pointer, so everything parked here is released when the session goes.
	std::shared_ptr<void> FindLocal(const std::type_index& key) const;
	void SetLocal(const std::type_index& key, const std::shared_ptr<void>& value);

	std::unordered_map<std::type_index, std::shared_ptr<void>> m_locals;

	// Identity fields — populated progressively as the session moves
	// through Add → Attach. Registry thread is the sole writer.
	ibSessionIdentity m_identity;
	bool              m_inserted = false;  // sys_session row present

	// State machine. atomic for lock-free reads by observers
	// (IsActive / Auth checks from snapshot readers, admin UI).
	std::atomic<ibSessionState> m_state { ibSessionState::Created };
	std::atomic<ibAuthState>    m_auth  { ibAuthState::Anonymous };

	// cv + mutex — producer waits for state transitions here. Registry
	// thread notifies after each Transition(). NotifyAll because
	// multiple producers may be waiting on different predicates
	// (e.g. WaitAdded, WaitAttachSettled).
	mutable std::mutex              m_mtx;
	mutable std::condition_variable m_cv;

	wxString m_activity;   // guarded by m_mtx; last-reported activity string
	wxString m_reason;     // guarded by m_mtx; reject / auth-fail diagnostic

	// Authoritative user identity for this session. Populated by InstallUser
	// (registry's ProcessAttach for headless paths, the GUI login dialog
	// under a ibSessionScope on the main thread). m_identity holds the row
	// fields written to sys_session; m_userInfo carries the full user record
	// (roles, language) used by script-side AppUser() readers and access
	// checks. m_sessionRawPassword caches the plain-text for Designer
	// "Start debugging" — handed to spawned child processes so they can
	// re-authenticate without prompting.
	wxString                  m_sessionRawPassword;
	ibUserInfo 				  m_userInfo;

	// Script-visible "working date" — see GetWorkDate/SetWorkDate.
	// Initialized to the session-creation wall-clock in the ctor.
	wxDateTime                m_workDate;

	// Per-session active configuration-language code.
	// m_languageCode = explicit override from SetLanguageCode (empty =
	// no override, use the user's preferred language).
	// m_resolvedLanguageCode = pre-computed answer for GetLanguageCode —
	// either m_languageCode if non-empty, or m_userInfo.m_strLanguageCode.
	// Refreshed on every SetLanguageCode / SetUserInfo call so the hot
	// read path is a single field load, no fallback logic per call.
	wxString                  m_languageCode;
	wxString                  m_resolvedLanguageCode;

	// Cancellation request flag — see RequestCancel / IsCancelRequested.
	// atomic so set/clear from any thread is safe against the script
	// thread's check loop in ibProcUnit::Execute.
	std::atomic<bool>         m_cancelRequested { false };

	// Force-exit request flag — see RequestForceExit / IsForceExit.
	// One-shot: set once, never cleared. The script thread observes it
	// and exits its loop; OnForceExit dispatches the per-kind action.
	std::atomic<bool>         m_forceExit       { false };

	// Eval / processing-backend-error flags — see Get/Set above.
	std::atomic<ibEvalMode>   m_evalMode                { eval_none };
	std::atomic<bool>         m_processingBackendError  { false };

	// Per-session interpreter state (currentRunModule, runContext stack,
	// errorPlace, recCount). Today the interpreter still reads/writes its
	// thread_local mirrors in procUnit.cpp; this slot is the staging
	// ground for the worker pool refactor (docs/worker-pool-tls-audit.md).
	// Step 1 of that refactor only allocates the slot — the swap helpers
	// at the worker boundary land in step 2. Default-constructed empty;
	// no reads from here yet.
	ibProcUnitState           m_procUnitState;

	// Exclusive mode — see SetExclusive(). True only on the session that
	// currently holds monopoly. Atomic for lock-free IsExclusive() reads
	// from any thread (script-side, listeners). Mutated by the registry
	// thread inside ProcessSetExclusive, with a notify_all on m_cv after
	// the result lands so SetExclusive() callers can resume.
	std::atomic<bool>          m_exclusive { false };

	// SetExclusive() handshake — Pending while a SetExclusive request is
	// in the queue, set by ProcessSetExclusive to the final outcome.
	// SetExclusive() reads it under m_mtx after WaitForState wakes up.
public:
	enum class ibExclusiveResult : int {
		Pending     = 0,
		Granted     = 1,   // either acquire ok or release ok
		HeldByOther = 2,   // another session is currently exclusive
		NotSole     = 3,   // other live sessions present — can't acquire
	};
private:
	ibExclusiveResult         m_exclusiveResult { ibExclusiveResult::Pending };

	// Server (parent) session — non-owning back-link populated by the
	// holder of the client after the server spawns it. See Server() /
	// SetServer above.
	std::weak_ptr<ibSession>   m_server;
};

// RAII binding for the calling thread — calls
// ibSession::BindSessionToThread on construction and ibSession::UnbindThread
// on destruction. Use in app entry points (OnRun) where the binding
// should live for the entire app lifetime: declare on the stack
// alongside the session pointer and let it cover every error-return
// path automatically. Differs from ibSessionScope in that it doesn't
// nest / restore prior values — the calling thread is expected to be
// unbound before construction and after destruction.
class BACKEND_API ibSessionThreadBinding {
public:
	explicit ibSessionThreadBinding(ibSession* s) noexcept;
	~ibSessionThreadBinding();

	ibSessionThreadBinding(const ibSessionThreadBinding&)            = delete;
	ibSessionThreadBinding& operator=(const ibSessionThreadBinding&) = delete;

private:
	std::thread::id m_tid;
};

// RAII guard that makes a session the Current() on the calling
// thread for the duration of the scope. Nested scopes restore the
// prior value on exit. Legacy — will be removed once direct ibSession
// pointer passing reaches all script call sites.
//
// m_prev is a weak_ptr (not raw): if the previously-bound session is
// destroyed while this scope is active, the dtor's restore step
// lock()s and either falls back to "no binding" or re-binds a still-
// live session — never restores a dangling pointer. Critical for
// rapid-F5 / refresh-cycle paths where nested scopes' inner dtor
// could resurrect a freed binding into s_currentByThread.
class BACKEND_API ibSessionScope {
public:
	explicit ibSessionScope(ibSession* s);
	~ibSessionScope();

	ibSessionScope(const ibSessionScope&)            = delete;
	ibSessionScope& operator=(const ibSessionScope&) = delete;

private:
	std::weak_ptr<ibSession> m_prev;
};

// RAII: mark the session's access context TRUSTED for the scope's lifetime, so
// a role module runs PRIVILEGED — every query its body builds sees GetAccessPolicy()
// return null and therefore bypasses RLS, dissolving re-entrancy (the module may
// read the very source it restricts). Saves and restores the PRIOR value, so nested
// trusted scopes compose; the dtor runs on every exit INCLUDING a handler throw, so
// enforcement is always restored. The bypass is CONSTRUCTIVE (only this scope sets
// the flag) — never a failure default, so a genuinely absent policy stays fail-closed.
class BACKEND_API ibAccessTrustScope {
public:
	explicit ibAccessTrustScope(ibSession* s)
		: m_session(s), m_prev(s != nullptr && s->m_accessTrusted)
	{
		if (m_session != nullptr) m_session->m_accessTrusted = true;
	}
	~ibAccessTrustScope() { if (m_session != nullptr) m_session->m_accessTrusted = m_prev; }

	ibAccessTrustScope(const ibAccessTrustScope&)            = delete;
	ibAccessTrustScope& operator=(const ibAccessTrustScope&) = delete;

private:
	ibSession* m_session;
	bool       m_prev;
};

// ibApplicationData::CreateSession<SessionT> template bodies live in
// sessionRegistry.h — they delegate through ibSessionRegistry's factory
// methods, which require the registry's full type at instantiation.
// Callers that use the typed overload include sessionRegistry.h.

// ses_query — symmetric to db_query but routes through the active
// session's holder so the work joins the session's TX/scope-bound
// connection. Throws ibBackendCoreException if no session is
// current or the session has no bound connection — explicit failure
// instead of silent fall-through. Use in descriptor / runtime code
// that must be transactionally cohesive with the outer document
// save; keep db_query for DDL / service / bootstrap paths.
//
// Returns shared_ptr (same as db_query) so callsites can use
// operator-> directly: ses_query->RunQuery(...).
#define ses_query (ibSession::DatabaseLayer())

#endif