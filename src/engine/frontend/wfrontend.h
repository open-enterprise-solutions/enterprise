#ifndef __WFRONTEND_H__
#define __WFRONTEND_H__

// Public surface of the web-runtime variant of frontend.dll.
//
// wfrontend.dll is compiled from the same source tree as frontend.dll
// but with OES_USE_WEB defined, so UI-specific code paths take the web
// branch (HTML / JSON over HTTP) instead of the native wx branch.
//
// Lifecycle (host process = wenterprise-server.exe / mod_oes / ...):
//
//   wfrontendInit(...)                          // once per process
//     │
//     ├─ wfrontendCreateSession(user, pw) ─┐    // per connected user
//     ├─ wfrontendFindSession(id)          │
//     ├─ wfrontendDestroySession(id)       ┘
//     │
//   wfrontendShutdown()                         // at process exit
//
// Real per-session state (appData-scoped module manager, main frame,
// ibProcUnit, etc.) will be plugged in in follow-up steps. For now
// the session id is just a random token and the manager keeps
// bookkeeping in memory.

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#	ifdef WFRONTEND_EXPORTS
#		define WFRONTEND_API __declspec(dllexport)
#	else
#		define WFRONTEND_API __declspec(dllimport)
#	endif
#else
#	define WFRONTEND_API
#endif

// Short identifier for probing that the DLL linked, e.g. "wfrontend 0.1 (web)".
extern "C" WFRONTEND_API const char* wfrontendVersion();

// Single-page HTML/CSS/JS client. Returned as a static C string so every
// entry-point (wenterprise-server, future Apache/IIS/CGI modules) can serve
// the same bytes from a single source of truth in wfrontend.dll — no
// duplicated blobs in each host.
extern "C" WFRONTEND_API const char* wfrontendClientHTML();

// Process-level init / shutdown. Call once at startup / shutdown of the
// host process.
//
// Two flavours:
//
//   * wfrontendInitFile()   — appDataCreateFile  (single-file DB path)
//   * wfrontendInitServer() — appDataCreateServer (host/port/db)
//
// Both then bring up the wes process's own system session via
// CreateSession + session->Open(ibUser, ibPassword) — that's the
// service-account authentication. Per-HTTP-session auth (the per-tab
// ibWebSession::Login flow) is layered on top. The last error (if
// any) is available via wfrontendLastError().
WFRONTEND_API bool wfrontendInitFile(
	const std::string& filePath,
	const std::string& ibUser,
	const std::string& ibPassword,
	const std::string& locale = "en",
	bool               debugEnable = false);

WFRONTEND_API bool wfrontendInitServer(
	const std::string& server,
	const std::string& port,
	const std::string& user,
	const std::string& password,
	const std::string& database,
	const std::string& ibUser,
	const std::string& ibPassword,
	const std::string& locale = "en",
	bool               debugEnable = false);

WFRONTEND_API void        wfrontendShutdown();
WFRONTEND_API std::string wfrontendLastError();

// Record the HTTP host:port the container is about to bind on. Host
// process (wenterprise-server main) calls this right after arg parse,
// before `wfrontendInit*` if possible. Stored so per-cookie sessions
// that spawn from ibWebSession::Login can populate
// `sys_session.address`, letting admin / Active-Users listings
// distinguish which server instance a session lives on when multiple
// wenterprise-server processes share a DB.
WFRONTEND_API void        wfrontendSetServerAddress(const std::string& host, int port);
WFRONTEND_API std::string wfrontendServerAddress();   // "host:port"; "" if unset

// Human-readable name of the configuration loaded at init time
// (ibMetaDataConfiguration::GetConfigName). Empty if not initialised
// or the metadata has no name yet.
WFRONTEND_API std::string wfrontendConfigName();

// Install a process-exit hook on backend's ForceExit path. wes's main
// hands in a lambda that calls `g_svr->stop()` so any backend code that
// requests exit (debug-Destroy, fatal error) lands in the same orderly
// httplib-stop → wfrontendShutdown sequence as Ctrl+C.
WFRONTEND_API void        wfrontendSetProcessExitHook(void (*hook)());

// True if the wes process was launched with --debug (debug server up,
// per-session Debug context auto-enabled at authenticate time).
WFRONTEND_API bool        wfrontendDebugMode();

// True iff the per-tab session identified by sessionId is currently
// parked at a breakpoint (its ibSession::Debug()->m_debugLoop is true).
// Returns false for unknown / non-debugged / running sessions.
WFRONTEND_API bool        wfrontendSessionPaused(const std::string& sessionId);

// True when the sys_user table has at least one row. Clients use this
// to decide whether to show a login form (true) or auto-login the
// anonymous/default user (false).
WFRONTEND_API bool wfrontendHasUsers();

// Serialise the configuration's top-level navigation (catalogs,
// documents, reports, etc.) as a JSON string. Returns "{}" if the
// backend is not initialised. The client reads this to draw its
// side panel / menu bar.
WFRONTEND_API std::string wfrontendMenuJSON();

// Session management.
//
//   CreateSession()      — allocate an anonymous session (no user yet).
//                          The returned id is what the host puts into
//                          the oes_session cookie.
//   Login(id, user, pw)  — authenticate the given session. On success
//                          the session spins up its ibWebApplication
//                          (main frame + CreateMainModule) and is ready
//                          to serve form requests.
//   SessionExists/Destroy/Count — bookkeeping.
WFRONTEND_API std::string wfrontendCreateSession();

// Create (or reuse) a session with a caller-supplied id. Used by the
// web client: each browser tab mints its own random id in
// sessionStorage and passes it via the `X-OES-Session` header, so
// every tab has a distinct ibWebSession instead of sharing one
// session across all tabs via the cookie jar.
WFRONTEND_API std::string wfrontendCreateSessionWithId(const std::string& id);
WFRONTEND_API bool        wfrontendLogin(const std::string& sessionId,
	const std::string& user, const std::string& password);
WFRONTEND_API bool        wfrontendSessionExists(const std::string& sessionId);
WFRONTEND_API void        wfrontendDestroySession(const std::string& sessionId);
WFRONTEND_API std::size_t wfrontendSessionCount();

// Write "kick" into sys_session.signal for the given session guid.
// Any wes process owning that row picks it up on its next
// JobCheckSignal tick (~3s) and submits Remove@Urgent, tearing the
// session down. Returns true if the UPDATE ran (not whether a row
// was affected — we don't need the distinction for the admin endpoint).
// Used by POST /admin/sessions/<guid>/kick; the session may belong to
// a different wes instance in a multi-process deployment.
WFRONTEND_API bool wfrontendKickSessionByGuid(const std::string& sessionGuid);

// Write "reload" into sys_session.signal for the given guid. Unlike
// "kick" (which targets one session row), "reload" latches a process-
// wide flag on the owning wes — when JobCheckSignal picks it up, that
// wes evicts every web-client session it owns and forces re-login. The
// guid argument just identifies WHICH process to poke; any row owned
// by that process triggers the same process-wide eviction. Used by
// POST /admin/sessions/<guid>/reload.
WFRONTEND_API bool wfrontendReloadSessionByGuid(const std::string& sessionGuid);

// Diagnostic snapshot — JSON object exposing pool/worker/session state
// for /admin/diag and the designer's Active Users dialog. Shape:
//   {
//     "workerPool":     { "alive": N, "idle": N, "max": N },
//     "connectionPool": { "live": N, "idle": N, "max": N, "minIdle": N },
//     "sessions":       { "count": N, "active": N,
//                         "byKind": { "<kindLabel>": N, ... } }
//   }
// Cheap to call — just reads atomic counters and the registry's
// last-refreshed snapshot. Returns "{}" on any internal failure.
WFRONTEND_API std::string wfrontendDiagJSON();

// sys_lock snapshot — JSON array of cluster-wide held locks for
// /admin/locks and the designer's Active Users → Locks sub-pane. Shape:
//   [ { "lockGuid":"<uuid>", "sessionGuid":"<uuid>",
//       "namespace":"Catalog.Goods", "key":"Ref=<uuid>",
//       "mode":"Exclusive", "acquiredAt":"2026-05-24T10:11:12",
//       "user":"ivanov", "computer":"WS-42" }, ... ]
// Reads the same snapshot ibLockManager::GetSnapshot returns; one row
// per held sys_lock entry. Returns "[]" on internal failure.
WFRONTEND_API std::string wfrontendLocksJSON();

// Force-release one sys_lock row by its lockGuid. Owner is not asked —
// admin override (e.g. session went zombie, user can't release through
// the UI). Returns true iff the DELETE ran without error (row may
// already have been released by the natural path; that's still "ok"
// from the admin's view). Used by DELETE /admin/locks/<lockGuid>.
WFRONTEND_API bool wfrontendForceReleaseLockByGuid(const std::string& lockGuid);

// Current tab count for the session (0 if the session doesn't exist or
// isn't authenticated yet). Used by GET / to decide whether F5 should
// swap an empty session for a fresh one (re-running OnStart).
WFRONTEND_API std::size_t wfrontendSessionTabCount(const std::string& sessionId);

// PNG bytes of the tab's icon (empty string if the session / tab /
// icon is missing). Served through GET /tab/<i>/icon so browsers can
// use the same caching machinery as any static image.
WFRONTEND_API std::string wfrontendTabIconPNG(const std::string& sessionId, int tabIndex);

// Open a form by its metadata id (as returned in /menu) inside the
// given session. Returns the JSON tree of the form's ibWebWindow
// subtree — control types, labels, nested containers — ready for the
// browser to render. On failure (no session / unknown metaID / form
// creation error) returns "{}".
WFRONTEND_API std::string wfrontendOpenForm(const std::string& sessionId,
	int metaID);

// Flat list of every ibValueMetaObjectFormBase in the loaded
// configuration, as JSON array of {id, name, owner_id, owner_name}.
// Useful for discovery: /menu lists catalogs/documents by their
// object metaID, but forms live one level deeper. Empty array if
// the backend isn't initialised.
WFRONTEND_API std::string wfrontendFormsJSON();

// Diagnostic: list every registered ctor currently visible to
// ibValue::GetAvailableCtor from wfrontend.dll's address space, as
// JSON array of {className, clsid, objectType}. Empty/short means
// the CONTROL_TYPE_REGISTER statics in ctrl/*.cpp weren't pulled in
// by the linker (MSVC can drop internal-linkage globals with no
// external references).
WFRONTEND_API std::string wfrontendCtorsJSON();

// Trigger a control-side event on the active form in the given session.
// controlID is the ibValueFrame::GetControlID value emitted as "id" in
// the form JSON. Currently supports buttons (calls FireClick); other
// control events land behind the same hook in follow-ups. Returns the
// updated form JSON (the ibVisualHostClient is re-walked so script
// side-effects surface immediately). "{}" on invalid session / control.
WFRONTEND_API std::string wfrontendFireAction(const std::string& sessionId,
	int controlID);

// Generic kind-aware dispatcher. Routes the `kind` string into
// ibWebWindow::HandleRequest on the target control. Textctrl side
// buttons use "buttonSelect"/"buttonOpen"/"buttonClear"; callers can
// pick any kind a control understands. Returns the rebuilt form JSON
// (same shape as wfrontendFireAction).
WFRONTEND_API std::string wfrontendFireKind(const std::string& sessionId,
	int controlID, const std::string& kind);

// Commit a textctrl value edit from the browser. newValue is the raw
// UTF-8 string the user typed. Server coerces through the backing
// ibValue's type (numeric / date / string / reference) and fires the
// OnChange script. Returns the updated form JSON with the committed
// value re-emitted, or "{}" on invalid session / control.
WFRONTEND_API std::string wfrontendFireTextChange(const std::string& sessionId,
	int controlID, const std::string& newValue);

// Toggle a checkbox from the browser. `checked` is the new state sent
// from the client (after the user click). Returns the updated form
// JSON with the new state and any downstream script mutations.
WFRONTEND_API std::string wfrontendFireToggle(const std::string& sessionId,
	int controlID, bool checked);

// Resolve a backend-side ShowModalMessage with the user's chosen button
// code. `modalId` is the id surfaced in /session JSON; `result` is the
// raw wx code (wxOK / wxYES / wxNO / wxCANCEL). Wakes the worker
// thread that was parked on the modal's promise; returns false if the
// modal id is no longer in the queue (already resolved / session gone).
WFRONTEND_API bool wfrontendModalReply(const std::string& sessionId,
	const std::string& modalId, int result);

// "All functions" tree — flat list of metadata objects grouped by kind
// (Constants / Catalogs / Documents / ...). Mirrors desktop's
// ibDialogFunctionAll content. Gated by AccessRight_ModeAllFunction
// — returns {"allowed":false} when the user lacks the role, otherwise
// {"allowed":true, "groups":[{clsid,name,items:[{id,name,synonym}]},…]}.
WFRONTEND_API std::string wfrontendAllFunctionsJSON();

// Open the form for a metadata object by its metaID. `cmdType` is the
// raw ibInterfaceCommandType integer (100=Default, 150=Create,
// 151=List, 152=Select) — passed through to ShowFormByCommandType so
// Create-section clicks land in the new-record flow, List in the list
// flow, etc. Returns the updated active host JSON; "{}" on access
// denied / unknown id / failure.
WFRONTEND_API std::string wfrontendOpenMetaObject(const std::string& sessionId,
	int metaID, int cmdType = 100);

// List metadata Interfaces (subsystems) the current user can use. Each
// interface carries: id, name, synonym, icon (base64 PNG data URI) and
// items[] — the meta objects assigned to it via the interface command
// section. Returns {"allowed":false} when configuration has no
// interfaces / role denies use, otherwise {"interfaces":[…]}.
// Mirrors desktop's ibSubSystemWindow content (enterprise's sidebar).
WFRONTEND_API std::string wfrontendInterfacesJSON();

// Read-only snapshot of the active tab's visual host tree. Unlike
// POST /tab/<i> (which activates + rebuilds), this just serialises
// the current tree — no side effects. Used by the browser's poll
// loop to pick up timer-driven state changes between user actions.
// Returns "{}" when session isn't authenticated or has no active tab.
WFRONTEND_API std::string wfrontendActiveHostJSON(const std::string& sessionId);

// Live-update wait. Block up to `timeoutMs` milliseconds for the
// session's sequence counter to advance past `lastSeen`, then return
// the current seq + active-host JSON. Returns lastSeen unchanged and
// "{}" if the session is gone. On timeout returns the current seq
// (possibly unchanged) so the caller can re-arm. Used by the SSE
// `/stream` endpoint: caller tracks seq, re-enters on every tick to
// keep the connection open and push incremental updates.
struct WfrontendLiveUpdate {
    std::uint64_t seq;
    std::string   json;
};
WFRONTEND_API WfrontendLiveUpdate wfrontendLiveWait(
    const std::string& sessionId, std::uint64_t lastSeen, int timeoutMs);

// Session introspection: returns JSON {tabCount, activeTab, tabs:[{index,
// formName, ...}]} so the browser can render a tab strip and verify
// multiple CreateNewForm invocations accumulate. Empty JSON if session
// not authenticated.
WFRONTEND_API std::string wfrontendSessionInfoJSON(const std::string& sessionId);

// Switch the session's active tab to tabIndex (0-based into the tab
// vector). Returns the JSON of the newly-active tab's ibVisualHostClient
// so the browser can swap body content. "{}" on invalid session / index.
WFRONTEND_API std::string wfrontendActivateTab(const std::string& sessionId,
	int tabIndex);

// Close tab tabIndex. If it was the active tab, the next tab (or the
// last one if it was the last) becomes active. Returns the JSON of the
// new active tab, or "{}" if no tabs remain after the close.
WFRONTEND_API std::string wfrontendCloseTab(const std::string& sessionId,
	int tabIndex);

#endif
