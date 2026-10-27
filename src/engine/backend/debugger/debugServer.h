#ifndef _DEBUGGER_SERVER_H__
#define _DEBUGGER_SERVER_H__

#include <map>
#include <queue>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <thread>

struct ibRunContext;
class ibSession;
class ibMetaDataConfiguration;
class ibStandaloneDebuggerServer;
class ibHttpServer;
class ibWebSocket;

// Lifecycle: owned by ibMetaDataConfiguration as a unique_ptr field
// (private ctor + friend). Ctor/dtor maintain the `ms_debugServer`
// process-wide cache so the interpreter hot path keeps a single
// indirect read, but ownership is strictly metadata's.
#define debugServer           (ibDebuggerServer::Get())

#include "backend/backend_core.h"
#include "debugDefs.h"

class BACKEND_API ibDebuggerServer {

	// ⭐ THE DEBUG THREAD, AND THE PORT BESIDE IT. The port is the core's (core/net): a WebSocket at /debugger, one
	// designer at a time, each command one binary message. Its threads only move bytes — what arrives is handed to
	// THIS thread, in order, and dispatched here, because this is the thread the session registry knows as the
	// debugger's (RegisterDebugThread) and the one COM is initialised on for an OLE value.
	class BACKEND_API ibDebuggerServerConnection {
	public:

		// A designer is on the port.
		bool IsConnected() const;

		void WaitConnection();
		void Disconnect();

		ConnectionType GetConnectionType() const { return m_connectionType; }

		ibDebuggerServerConnection(const ibString& hostName, unsigned short startPort);
		~ibDebuggerServerConnection();

	protected:

		// The thread's own body (m_thread), and the loop it runs.
		void Entry();
		void EntryClient();

		void RecvCommand(void* pointer, unsigned int length);
		void SendCommand(void* pointer, unsigned int length);

	private:

		// From the port's thread: a designer came — its socket in the slot, and the queue what it says goes to; null
		// when another one is on.
		std::shared_ptr<ibDebugCommandQueue> AcceptConnection(const std::shared_ptr<ibWebSocket>& socket);

		// The port, on the first free one from m_numHostPort up.
		bool StartServer();

		// The connection's thread (Entry), and the word that it is to end; it asks between reads. The connection's end
		// says it and waits the thread out (~ibDebuggerServerConnection).
		std::thread       m_thread;
		std::atomic<bool> m_stop{ false };

		std::atomic<bool> m_waitConnection;

		ConnectionType m_connectionType;

		ibString m_strHostName;
		unsigned short m_numHostPort;

		// The port's thread sets true in Accept; main thread polls this
		// in CreateServer's wait loop. Plain bool was being kept in a
		// register on the main thread → wait loop never observed the
		// flip and the server hung until something else flushed cache
		// (e.g. designer closing rewrote enough memory).
		std::atomic<bool> m_acceptConnection;

		// Worker thread sets true once StartServer has answered — the port up,
		// or none free in the diapason. Used by
		// CreateServer(wait=false) to make the manifest-write path
		// deterministic — without it, the designer's SearchServer can
		// race with wes's bind-loop and the connect refused that
		// follows leaves the loop reaching round-budget exhaustion.
		std::atomic<bool> m_bindReady{false};

		std::unique_ptr<ibHttpServer> m_socketServer;

		// ⭐ A MESSAGE IS WHOLE, so the socket needs no lock of its own: the two writes of a frame that interleaved
		// between senders, and the blocking flags a read and a write restored over each other (2026-09-25), were
		// wxSocketBase's. This lock only guards the slot — which socket is the designer's, and its queue.
		mutable std::mutex                   m_socketMutex;
		std::shared_ptr<ibWebSocket>         m_socket;
		std::shared_ptr<ibDebugCommandQueue> m_commandQueue;

		friend class ibDebuggerServer;
		friend class ibStandaloneDebuggerServer;   // the port the system chose for a test (test_debugServer.cpp)
	};

	// metaData — the configuration that made it and owns it; the debugger asks ITS base through it, never
	// "the current one" (docs/private/multi-base-process.md).
	explicit ibDebuggerServer(class ibMetaDataConfigurationBase* metaData);
	friend class ibMetaDataConfiguration;
	friend class ibStandaloneDebuggerServer;   // a test's server with no configuration around it (test_debugServer.cpp)

public:

	virtual ~ibDebuggerServer();

	class ibMetaDataConfigurationBase* GetMetaData() const { return m_metaData; }
	class ibApplicationInstance* GetApplicationInstance() const;

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

	bool CreateServer(const ibString& hostName = defaultHost, unsigned short startPort = ibPort::Debugger.m_first, bool wait = false);
	void ShutdownServer();

	// Session is read out of runContext → GetProcUnit() → GetSession()
	// at call time — the debug server is a process-level singleton
	// shared across concurrent sessions; they enter sequentially and
	// each knows its session through the run context already held by
	// the dispatch loop.
	void EnterDebugger(ibRunContext* runContext, const struct ibByteUnit& byteCode, long& numPrevLine);
	// ⭐⭐ THE PERSON'S ROAD, AND IT CARRIES FAILURES. It lands in the DESIGNER'S OUTPUT PANE, where
	// somebody is working: a fault in a module belongs there, with its file, its module and its line,
	// because it is theirs to fix. What a run merely SAYS does not — it goes to whoever asked for the
	// run, by the channel below (Max, 2026-09-04: *"this is an error that reaches me in the designer,
	// and I can see it really is an error; and there are messages that should reach you, not me — I
	// do not need them"*).
	void SendErrorToClient(const ibString& strFileName, const ibString& strDocPath, unsigned int numLine, const ibString& strErrorMessage);

	// ⭐ WHAT EVALUATED CODE PRINTED — up the eval channel, to whoever asked for the evaluation.
	//
	// Separate from the error road above on purpose, and not only to keep the volume down: the
	// person at the designer is debugging their own work and has no reason to read what somebody
	// else's sandbox is printing, while the assistant that ran it has no other way to see it — it
	// is not their window (Max, 2026-09-02: *"I do not need to see those messages; you do, because
	// you write them in the sandbox and I have no access to the sandbox"*).
	//
	// Never opens a connection: unlike an error, an evaluation is not worth a listening socket.
	// ⭐⭐ THE ASSISTANT'S OWN CHANNEL, and what a run SAYS travels on it too. Two roads exist and
	// they answer to different readers: `SendErrorToClient` lands in the DESIGNER'S OUTPUT PANE —
	// the person's window, where a failure belongs — while this one is read by whoever ASKED for
	// the run and is dropped by the designer. Ordinary output down the error road would paint a
	// person's pane with somebody else's narration (Max, 2026-09-04).
	//
	// The level rides along, so a reader can tell "the document did not post" from "posted 12".
	void SendEvalMessage(const ibString& strMessage, MessageType type = MessageType_Normal);

	// "Is the debug-thread-current session parked in DoDebugLoop?" Resolves
	// the per-session m_debugLoop via ibSession::Current() (body in .cpp —
	// the header only forward-declares ibSession). Advisory fast-path for the
	// command handlers; EvalInParkedSession re-checks authoritatively.
	bool IsDebugLooped() const;

	// ⭐ IS ANYBODY WATCHING THIS RUN AT ALL — asked before handing anything to the debug channel.
	//
	// 🛑 IT MATTERS BECAUSE SendErrorToClient CREATES THE SERVER when there is no connection thread
	// yet, and waits while it does. That is right for the one caller it had (an error, where
	// reaching the developer is worth the cost) and wrong for a caller on the ordinary path: every
	// `Message` in an application nobody is debugging would try to open a listening socket.
	bool IsDebugging() const { return m_bUseDebug; }

protected:

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
	void WakeDebugSession(const ibString& sessionGuid);

	void ClearCollectionBreakpoint();

	//main loop
	inline void DoDebugLoop(const ibString& strDocPath, const ibString& strModuleName, int numLine, ibRunContext* runContext);

	//special functions:
	// Both take the parked frame explicitly — sourced from the per-session
	// dbg.m_runContext at the call site (worker owns it in DoDebugLoop; the
	// SetStack handler resolves + holds dbg.m_mutex). No server-global mirror.
	inline void SendExpressions(ibRunContext* runContext);
	inline void SendLocalVariables(ibRunContext* runContext);
	inline void SendStack();

	//commands:
	void RecvCommand(void* pointer, unsigned int length);
	void SendCommand(void* pointer, unsigned int length);

private:

	static ibDebuggerServer* ms_debugServer;

	class ibMetaDataConfigurationBase* const m_metaData;

	std::atomic<bool> m_bUseDebug;
	std::atomic<bool> m_bDebugStopLine;

	unsigned int m_numCurrentNumberStopContext;

	// module -> (line -> condition). An empty condition stops always; any other stops only where it is true
	// in the frame that reaches the line (EnterDebugger).
	//
	// 🛑 UNDER ITS LOCK, every touch: read shared by each script thread on every line it runs while debugged
	// (EnterDebugger), changed exclusively by the debug thread's commands and by a reset, which a script's thread
	// reaches too (ResetDebugger).
	std::map<ibString, std::map<unsigned int, ibString>> m_listBreakpoint;
	std::shared_mutex m_breakpointsMutex;

	// WHAT IS WATCHED HERE, AND FOR WHOM — refreshed at every stop by SendExpressions, which answers each
	// asker in its own name. See ibWatchedExpression (debugDefs.h) for why the name is kept and not
	// recovered.
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	std::map <unsigned long long, ibWatchedExpression> m_listExpression;
#else
	std::map <unsigned int, ibWatchedExpression> m_listExpression;
#endif

	std::unique_ptr<ibDebuggerServerConnection> m_socketConnectionThread;

	friend class ibBackendException;
};

#endif // __DEBUGGER_CLIENT_H__
