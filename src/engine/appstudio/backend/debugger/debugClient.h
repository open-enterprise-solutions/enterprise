#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <algorithm>
#include <concepts>   // std::invocable — what ForEachConnection calls on each connection
#include <map>
#include <memory>   // the adapter owns its bridges
#include <vector>

#include "core/guid.h"
#include "core/stringUtils.h"

// THE DESIGNER'S DEBUGGER IS THE SERVER'S (protocol 6) — the engine's client of the designer's session
// (backend/debugger/debugClient.h) is the one connected to the runtimes, and this is its facade, in its words, so the
// windows ask what they always asked: a command goes as debug {Command} on the debugger's own connection
// (ibCommunicator::Debug), and what the engine says comes back as news (ibCommunicator::ListenDebug), told the
// listeners here on the window's loop. A web designer listens the same way. The breakpoints are that client's too, at
// the editor's lines: one map, as the desktop's one process had it, moved by the edits the editor's text carries
// (ibModuleTextView, Change), and asked of the store, schema Debugger (debugClientQuery.cpp) — none kept here.
//
// Lifecycle: owned by the designer's main frame as a unique_ptr field (private ctor + friend), made once it is logged
// in. Designer / codeEditor hot paths read the static slot directly through the macro.
#define debugClient           (ibDebuggerClient::Get())

#include "debugClientBridge.h"

class ibProtocolNode;

class ibDebuggerClient {

	// The listeners — the designer's windows. What the server's debugger says is told them here, already on the window's
	// loop (Listen): nothing here changes threads.
	class ibDebuggerClientAdapter {
	public:

		ibDebuggerClientAdapter() = default;

		// 🛑 NOT COPYABLE, AND NOW IT HAS TO SAY SO. It never was — it owns its bridges through
		// unique_ptr — but the prohibition was BORROWED from wxEvtHandler, whose own copy is deleted.
		ibDebuggerClientAdapter(const ibDebuggerClientAdapter&) = delete;
		ibDebuggerClientAdapter& operator=(const ibDebuggerClientAdapter&) = delete;

		// THE BRIDGE IS A LIST. There was one, and one is why the IDE's windows
		// and anything else watching the same session were mutually exclusive:
		// installing a second put out the first. A debugging session has as many
		// listeners as there are things looking at it — the IDE's windows, the
		// assistant — and none of them is the privileged one.
		//
		// The adapter OWNS every bridge in the list: they are handed over with
		// `new` and destroyed here, which is what the single-bridge version did
		// and the only arrangement in which nobody has to outlive anybody.
		void SetBridge(ibDebuggerClientBridge* bridge) {
			m_debugBridges.clear();
			AddBridge(bridge);
		}
		void AddBridge(ibDebuggerClientBridge* bridge);
		void RemoveBridge(ibDebuggerClientBridge* bridge);

		// The first one installed — the IDE's, in the process that has one. Kept
		// because callers ask "is anybody bridged", not "which".
		ibDebuggerClientBridge* GetBridge() const {
			return m_debugBridges.empty() ? nullptr : m_debugBridges.front().get();
		}

		virtual ~ibDebuggerClientAdapter() {}

		//commands
		void OnSessionStart();
		void OnSessionEnd();

		void OnEnterLoop(const ibDebugLineData& data);
		void OnLeaveLoop(const ibDebugLineData& data);

		void OnMessageFromServer(const ibDebugLineData& data, const wxString& message);
		void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr);
		void OnSetStack(const ibStackData& data);

		void OnSetLocalVariable(const ibLocalWindowData& data);

		void OnSetVariable(const ibWatchWindowData& data);
		void OnSetExpanded(const ibWatchWindowData& data);

	private:
		std::vector<std::unique_ptr<ibDebuggerClientBridge> > m_debugBridges;
	};

	// THE CONFIGURATION IS THE SERVER'S: its guid, asked of schema Debugger; and the debugger's news listened to from then
	// on (Listen).
	ibDebuggerClient();
	// …or of its store as the login read it (the designer's GetStartSchemas): nothing asked.
	explicit ibDebuggerClient(const class ibProtocolNode& store);
	friend class ibFrontendMainFrameDesigner;

public:

	// A RUNTIME THE SERVER'S DEBUGGER KNOWS — a row of debug {Connections}, as the server listed it: what the Debug items
	// window shows and attaches by.
	class ibDebuggerClientConnection {
	public:

		bool IsVerifiedConnection() const {
			return m_verifiedConnection && IsConnected();
		}

		// A socket of the server's to it, and not gone.
		bool IsConnected() const { return m_connected; }

		wxString GetHostName() const { return m_hostName; }
		unsigned short GetPort() const { return m_port; }
		wxString GetComputerName() const { return m_compName; }
		wxString GetUserName() const { return m_userName; }

		ConnectionType GetConnectionType() const { return m_connectionType; }

	private:

		bool			m_verifiedConnection = false;
		bool			m_connected = false;

		wxString		m_hostName;
		unsigned short	m_port = 0;

		wxString		m_userName;
		wxString		m_compName;

		ConnectionType	m_connectionType = ConnectionType::ConnectionType_Scanner;
		friend class ibDebuggerClient;
	};

	// The configuration the runtimes over there are checked against — the engine's GetMetaData()->GetConfigGuid().
	const ibGuid& GetConfigGuid() const { return m_configGuid; }

	void SetBridge(ibDebuggerClientBridge* bridge) { m_adapter->SetBridge(bridge); }

	// The identity a listener stamps on the questions it asks, so its answers come back to it and
	// to nobody else. Empty when nothing is bridged, which is also when nothing can be asked.
	wxString BridgeId() const {
		const ibDebuggerClientBridge* bridge = m_adapter != nullptr ? m_adapter->GetBridge() : nullptr;
		return bridge != nullptr ? bridge->GetBridgeId() : wxString();
	}

	// Ride along on a session somebody else is driving — see the note on the
	// adapter. Not owned: whoever adds one removes it before it dies.
	void AddBridge(ibDebuggerClientBridge* bridge) { m_adapter->AddBridge(bridge); }
	void RemoveBridge(ibDebuggerClientBridge* bridge) { m_adapter->RemoveBridge(bridge); }

	virtual ~ibDebuggerClient();

	// Process-wide cache. Hot-path readers (codeEditor, watchWindow,
	// stackWindow, etc.) go through this static slot via the
	// `debugClient` macro. Ctor/dtor maintain ms_debugClient.
	static ibDebuggerClient* Get() { return ms_debugClient; }

public:

	// THE RUNTIMES LOOKED FOR — by the server's debugger, on its machine, where a runtime of a server's base runs its code.
	// A debug run just started waits for its child to come up (debug {Search}); otherwise the runtimes are listed as they
	// stand (debug {Connections}), for ForEachConnection.
	void SearchServer(bool run_debug_server = false);

	void AttachConnection(const wxString& hostName, unsigned short port) const;
	void DetachConnection(const wxString& hostName, unsigned short port, bool kill = false) const;

	// Each runtime as the last SearchServer() listed it — the engine's word; here the list is this window's own.
	template <std::invocable<const ibDebuggerClientConnection&> Fn> void ForEachConnection(Fn&& visit) const {
		for (const ibDebuggerClientConnection& connection : m_listConnection)
			visit(connection);
	}

	// ⭐⭐ A WATCH QUESTION CARRIES WHO ASKED IT, IN THE PACKET.
	//
	// The `id` is a handle in the asker's own world (a row of the designer's tree), so an answer
	// says WHICH ROW and, on its own, nothing about WHOSE — and every listener used to receive
	// every answer and guess. The asker's identity therefore travels with the question and comes
	// back with the answer; nothing is remembered on either side, so there is no map to keep in
	// step with rows appearing and going away.
	//
	// The runtime does not read it. It echoes it back exactly as it echoes the id.
	//special public function:
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	void AddExpression(const wxString& strExpression, unsigned long long id, const wxString& asker);
	void ExpandExpression(const wxString& strExpression, unsigned long long id, const wxString& asker);
	void RemoveExpression(unsigned long long id);
#else
	void AddExpression(wxString strExpression, unsigned int id, const wxString& asker);
	void ExpandExpression(wxString strExpression, unsigned int id, const wxString& asker);
	void RemoveExpression(unsigned int id);
#endif

	void SetLevelStack(unsigned int level);

	//evaluate for tooltip
	void EvaluateToolTip(const wxString& strFileName, const wxString& strModuleName, const wxString& strExpression);

	// The module's breakpoints as the editor draws them: editor line (from 0) -> condition (empty stops always) — asked of
	// the store.
	std::map<unsigned int, wxString> GetDebugList(const wxString& strModuleName);

	//special functions:
	void Continue();
	void StepOver();
	void StepInto();
	void StepOut();
	void Pause();
	void Stop(bool kill);

	// The line is the editor's, as the margin has it. A CONDITION is an expression of the language, evaluated in the
	// frame that reaches the line, and the run stops only when it is true; an empty one stops every time.
	// `refusal`, when given, comes back carrying WHY it was refused — the engine states the reason
	// and the caller decides whether that is a dialog, a log line or an answer over a socket.
	// Sets the breakpoint, or gives one already there the condition passed (an empty one stops always).
	bool ToggleBreakpoint(const wxString& strModuleName, unsigned int line,
		wxString* refusal = nullptr, const wxString& condition = wxEmptyString);
	bool RemoveBreakpoint(const wxString& strModuleName, unsigned int line);
	// Same shape as ToggleBreakpoint: the reason travels to whoever asked, or is said through the
	// platform's own Message when nobody did.
	bool RemoveAllBreakpoint(wxString* refusal = nullptr);

	// A runtime attached to the server's debugger — as its last SessionStart or SessionEnd said (Connected).
	bool HasConnections() const { return m_hasConnections; }

	bool IsEnterLoop() const { return m_enterLoop; }

protected:

	static bool TableAlreadyCreated();
	static bool CreateBreakpointDatabase();

	//db support — the store's verbs at the editor's lines; refused with its reason (`said`)
	bool ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line, const wxString& condition, wxString& said);
	bool RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line, wxString& said);
	bool RemoveAllBreakpointInDB();

private:

	// THE SERVER'S DEBUGGER LISTENED TO — its own connection opened and bound (ibCommunicator::ListenDebug), each piece of
	// news handed to the window's loop and received there; a refusal said in the messages, the designer working on
	// without a debugger.
	void Listen();
	// …and one piece of news received: the state it says kept, and the listeners told it, in their words.
	void Receive(const ibProtocolNode& news);

	static ibDebuggerClient* ms_debugClient;

	ibGuid m_configGuid;

	ibDebuggerClientAdapter* m_adapter = nullptr;

	// The runtimes as the server last listed them (SearchServer).
	std::vector<ibDebuggerClientConnection> m_listConnection;

	// What the server's debugger last said of itself — a runtime attached (SessionStart, SessionEnd), and one stopped
	// (EnterLoop, LeaveLoop).
	bool	m_hasConnections = false;
	bool	m_enterLoop = false;
};

#endif
