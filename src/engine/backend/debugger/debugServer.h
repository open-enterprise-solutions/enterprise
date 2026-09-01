#ifndef _DEBUGGER_SERVER_H__
#define _DEBUGGER_SERVER_H__

#include <map>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <wx/thread.h>
#include <wx/socket.h>

struct ibRunContext;
class ibSession;
class ibMetaDataConfiguration;

// Lifecycle: owned by ibMetaDataConfiguration as a unique_ptr field
// (private ctor + friend). Ctor/dtor maintain the `ms_debugServer`
// process-wide cache so the interpreter hot path keeps a single
// indirect read, but ownership is strictly metadata's.
#define debugServer           (ibDebuggerServer::Get())

#include "backend/backend_core.h"
#include "debugDefs.h"

class BACKEND_API ibDebuggerServer {

	class BACKEND_API ibDebuggerServerConnection : public wxThread {
	public:

		bool IsConnected() const {
			if (m_socket == nullptr)
				return false;
			if (!m_socket->IsConnected())
				return false;
			if (!m_socket->IsOk())
				return false;
			// Don't include LastError() in the liveness check.
			// wxSocketBase keeps LastError as per-socket state (NOT
			// per-thread), so a transient WOULDBLOCK / TIMEDOUT /
			// IOERR left by any operation on any thread (e.g. a
			// session worker's SendCommand racing the connection
			// thread's WaitForRead) leaks into the connection
			// thread's next IsConnected() check and falsely declares
			// the connection dead. Real disconnects flip IsConnected
			// or IsOk to false above; ReadMsg / WriteMsg operations
			// guard their own short-read / short-write cases via
			// LastCount checks. This brittle filter caused the
			// "designer detaches between F5s" reports under multi-tab
			// breakpoint workloads.
			return true;
		}

		void WaitConnection();
		void Disconnect();

		ConnectionType GetConnectionType() const { return m_connectionType; }

		ibDebuggerServerConnection(const wxString& hostName, unsigned short startPort);
		virtual ~ibDebuggerServerConnection();

		// entry point for the thread - called by Run() and executes in the context
		// of this thread.
		virtual ExitCode Entry() override;

		// This one is called by Kill() before killing the thread and is executed
		// in the context of the thread that called Kill().
		virtual void OnKill() override;

	protected:

		void EntryClient();

		void RecvCommand(void* pointer, unsigned int length);
		void SendCommand(void* pointer, unsigned int length);

	private:

		std::atomic<bool> m_waitConnection;

		ConnectionType m_connectionType;

		wxString m_strHostName;
		unsigned short m_numHostPort;

		// Worker thread sets true after Accept; main thread polls this
		// in CreateServer's wait loop. Plain bool was being kept in a
		// register on the main thread → wait loop never observed the
		// flip and the server hung until something else flushed cache
		// (e.g. designer closing rewrote enough memory).
		std::atomic<bool> m_acceptConnection;

		// Worker thread sets true once a wxSocketServer has successfully
		// bound a port (the listener is now Listening). Used by
		// CreateServer(wait=false) to make the manifest-write path
		// deterministic — without it, the designer's SearchServer can
		// race with wes's bind-loop and the connect refused that
		// follows leaves the loop reaching round-budget exhaustion.
		std::atomic<bool> m_bindReady{false};

		wxSocketServer* m_socketServer;
		wxSocketBase* m_socket;

		// Serialises wire writes. SendCommand does two consecutive
		// WriteMsg calls (length header + payload); without a mutex,
		// concurrent senders interleave their bytes and the designer
		// sees a corrupted frame and drops the connection. Hot path
		// when several web sessions hit breakpoints in parallel and
		// each emits LeaveLoop on F5 destroy. Plain mutex — a write
		// to the socket is already syscall-bound, lock contention is
		// dwarfed by the I/O cost.
		std::mutex m_sendMutex;

		friend class ibDebuggerServer;
	};

	ibDebuggerServer();
	friend class ibMetaDataConfiguration;

public:

	virtual ~ibDebuggerServer();

	// Process-wide cache. ms_debugServer is maintained by ctor/dtor —
	// owner is always the active ibMetaDataConfiguration::m_debugServer
	// unique_ptr. Hot-path readers (procUnit interpreter, exception
	// path) use `debugServer` macro which expands to this Get() —
	// single inline indirect read; identical to pre-refactor cost.
	static ibDebuggerServer* Get() { return ms_debugServer; }

	bool EnableDebugging() const { return m_socketConnectionThread != nullptr; }

	bool AllowDebugging() const {
		return m_socketConnectionThread != nullptr ?
			!m_socketConnectionThread->m_waitConnection : true;
	}

	bool CreateServer(const wxString& hostName = defaultHost, unsigned short startPort = defaultDebuggerPort, bool wait = false);
	void ShutdownServer();

	// Session is read out of runContext → GetProcUnit() → GetSession()
	// at call time — the debug server is a process-level singleton
	// shared across concurrent sessions; they enter sequentially and
	// each knows its session through the run context already held by
	// the dispatch loop.
	void EnterDebugger(ibRunContext* runContext, const struct ibByteUnit& byteCode, long& numPrevLine);
	void SendErrorToClient(const wxString& strFileName, const wxString& strDocPath, unsigned int numLine, const wxString& strErrorMessage);

	// "Is the debug-thread-current session parked in DoDebugLoop?" Resolves
	// the per-session m_debugLoop via ibSession::Current() (body in .cpp —
	// the header only forward-declares ibSession). Advisory fast-path for the
	// command handlers; EvalInParkedSession re-checks authoritatively.
	bool IsDebugLooped() const;

protected:

	void SetConnectSocket(ibDebuggerServerConnection* socketConnectionThread) {
		m_socketConnectionThread = socketConnectionThread;
	}

	void ResetDebugger() {

		m_bUseDebug = false;

		// Drain every per-session debug loop — flips each session's
		// m_debugLoop off and kicks its CV so parked script threads
		// (including sibling wes tabs) return from DoDebugLoop. This is
		// the sole drain path now that the server-global loop flag/CV
		// are gone.
		WakeAllDebugSessions();

		ClearCollectionBreakpoint();
	}

	void WakeAllDebugSessions();
	void WakeDebugSession(const wxString& sessionGuid);

	void ClearCollectionBreakpoint();

	//main loop
	inline void DoDebugLoop(const wxString& strDocPath, const wxString& strModuleName, int numLine, ibRunContext* runContext);

	//special functions:
	// Both take the parked frame explicitly — sourced from the per-session
	// dbg->m_runContext at the call site (worker owns it in DoDebugLoop; the
	// SetStack handler resolves + holds dbg->m_mutex). No server-global mirror.
	inline void SendExpressions(ibRunContext* runContext);
	inline void SendLocalVariables(ibRunContext* runContext);
	inline void SendStack();

	//commands:
	void RecvCommand(void* pointer, unsigned int length);
	void SendCommand(void* pointer, unsigned int length);

private:

	static ibDebuggerServer* ms_debugServer;

	std::atomic<bool> m_bUseDebug;
	std::atomic<bool> m_bDebugStopLine;

	unsigned int m_numCurrentNumberStopContext;

	std::map<wxString, std::vector<unsigned int>> m_listBreakpoint; //list of points

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	std::map <unsigned long long, wxString> m_listExpression;
#else
	std::map <unsigned int, wxString> m_listExpression;
#endif

	wxCriticalSection m_clearBreakpointsCS;

	ibDebuggerServerConnection* m_socketConnectionThread;

	friend class ibBackendException;
};

#endif // __DEBUGGER_CLIENT_H__
