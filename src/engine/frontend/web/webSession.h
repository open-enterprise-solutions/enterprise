#ifndef __WEB_SESSION_H__
#define __WEB_SESSION_H__

// Per-session runtime context for the web frontend.
//
// Owns everything that must be isolated between concurrent web users:
// the module manager's main-module instance, the ibProcUnit bytecode
// interpreter, the logical main frame holding "open" documents, and any
// session-scoped variables. The metadata itself (compiled modules,
// form descriptors, catalog schemas, ...) lives on the singleton
// activeMetaData and is shared across sessions.
//
// Lifecycle mirrors the desktop BeforeRun / RunDatabase / Close flow:
//
//   ctor → OnInit(user, password)  [authorises + CreateMainModule]
//        │
//        │   ... HandleRequest(...) on every HTTP hit ...
//        │
//        └─ OnExit()                [DestroyMainModule + cleanup]
//     dtor

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include <wx/string.h>

class ibWebApplication;
class ibSession;

class ibWebSession {
public:
	ibWebSession(wxString id, wxString user);
	~ibWebSession();

	// Lifecycle:
	//   OnInit()  — lightweight: reads the configuration name, registers
	//               the session in the manager. Runs with no user
	//               identity yet and does NOT start a runtime.
	//   Login()   — called after the user authenticates (POST /login or
	//               auto-login when sys_user is empty). Sets the user
	//               identity, then spins up the ibWebApplication which
	//               builds the main frame and calls CreateMainModule.
	//   OnExit()  — tears the runtime down.
	bool OnInit();
	bool Login(const wxString& user, const wxString& password);
	void OnExit();

	bool IsAuthenticated() const { return m_app != nullptr; }

	const wxString& Id()         const { return m_id; }
	const wxString& User()       const { return m_user; }
	const wxString& ConfigName() const { return m_configName; }
	ibWebApplication*  App()        const { return m_app.get(); }
	// Raw ibSession pointer from the ticket — for HTTP handlers that need
	// to pin ibSessionScope on their worker thread so ibSession::Current()
	// resolves for moduleManager->GetProcUnit() etc. Nullptr before Login.
	class ibSession* Session() const;

	// Milliseconds since epoch at last request — used later for idle
	// timeout eviction in the session manager.
	std::int64_t LastActiveMs() const;
	void         Touch();

private:
	wxString m_id;
	wxString m_user;
	wxString m_configName;

	mutable std::mutex m_mutex;
	std::int64_t       m_lastActiveMs;
	bool               m_initialized = false;

	// Lifecycle serializer — locks Login and OnExit on the same instance
	// against each other. Rapid F5 races SessionManager::Login (holds a
	// keeper shared_ptr) against SessionManager::Destroy (also holds a
	// keeper) on the same ibWebSession; without this the two ran
	// concurrently and Destroy's m_session.reset() freed the ibSession
	// while Login's worker thread was still about to start with
	// m_sessionContext pointing at it. Recursive in case future code
	// calls OnExit re-entrantly from the same thread (dtor → OnExit →
	// internal teardown). Distinct from m_mutex which only guards the
	// activity timestamp accessors.
	mutable std::recursive_mutex m_lifecycleMutex;

	// Per-session "app" — owns the module manager, future frame, etc.
	// Created lazily in OnInit so a failed authentication does not
	// leave a half-initialised app behind.
	std::unique_ptr<ibWebApplication> m_app;

	// Session pointer — registry's m_own owns one shared_ptr; we keep our
	// own strong ref so a concurrent refresh-cycle that overwrites
	// m_own[presetGuid] (same tabSid lands in ProcessAdd while we are
	// mid-OnExit) cannot drop the last reference and free the session
	// while OnExit is still calling methods on it. Set by Login() right
	// after CreateSession via shared_from_this(); cleared on OnExit / dtor
	// AFTER the final ibSession::Close call returns.
	std::shared_ptr<class ibSession> m_session;
};

#endif
