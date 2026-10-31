#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <algorithm>
#include <map>
#include <memory>   // the adapter owns its bridges
#include <vector>

#include "core/guid.h"
#include "core/stringUtils.h"

// THE DESIGNER'S DEBUGGER IS THE SERVER'S (protocol 6) — the engine's client of the designer's session
// (backend/debugger/debugClient.h) is the one connected to the runtimes, and this is its facade, in its words, so the
// windows ask what they always asked: a command goes as debug {Command} on the debugger's own connection
// (ibCommunicator::Debug), and what the engine says comes back as news (ibCommunicator::ListenDebug), told the
// listeners here on the window's loop. A web designer listens the same way. What is the editor's stays here: the
// breakpoints at the editor's lines and the lines its edits moved — the runtime counts committed lines, and the
// server is told those. The store the breakpoints are kept in is the server's, schema Debugger (debugClientQuery.cpp).
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

	// THE CONFIGURATION IS THE SERVER'S: its guid and its modules, each initialized as the desktop's configuration did as
	// it loaded (ibValueMetaObjectModuleBase::OnBeforeRunMetaObject) — asked of schema Debugger here, once; and the
	// debugger's news listened to from then on (Listen).
	ibDebuggerClient();
	// …or over its store as the login read it (the designer's GetStartSchemas): nothing asked.
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
	template <typename Fn> void ForEachConnection(Fn&& visit) const {
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

	// The module's breakpoints as the editor draws them: editor line (from 0) -> condition (empty stops always).
	std::map<unsigned int, wxString> GetDebugList(const wxString& strModuleName);

	//special functions:
	void Continue();
	void StepOver();
	void StepInto();
	void StepOut();
	void Pause();
	void Stop(bool kill);

	//for breakpoints and offsets
	void InitializeModule(const wxString& strModuleName, unsigned int line_count);
	// atLineStart: the edit was at column 0 of `line` (whole line moved), so an
	// entry sitting ON `line` shifts too — fixes "Enter at line start" not moving a breakpoint.
	void PatchModule(const wxString& strModuleName, unsigned int line, int line_offset, bool atLineStart);
	bool SaveModule(const wxString& strModuleName, unsigned int line_count);
	void RemoveModule(const wxString& strModuleName);

	bool SaveAllBreakpoints();

	// One breakpoint, as the maps below keep it: the offset the edits since the last save have moved it by
	// (its committed line is the map's key - see ShiftLineMap), and the CONDITION it stops on. An empty
	// condition stops every time; any other is an expression of the language, evaluated in the frame that
	// reaches the line, and the run stops only when it is true.
	struct ibBreakpoint {
		int      m_offset = 0;
		wxString m_condition;
	};

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

	// ⭐⭐ WHAT IS SET, AND WHERE — because a breakpoint OUTLIVES the run that needed it, and the
	// next run it stops is somebody else's. Read back as (module guid -> lines), with the editor
	// line each one currently sits on: the map stores a committed line plus the offset edits have
	// moved it by, and only their sum is an address anybody can use.
	//
	// 🛑 Measured on myself, 2026-09-04: a forgotten breakpoint parked a run on its second line, I
	// read "only the first message arrived", and spent several turns building a theory about a lost
	// channel. The state was knowable the whole time and nothing offered it.
	//
	// (module guid -> (editor line -> condition)); the lines count from 0, as the editor's do.
	std::map<wxString, std::map<unsigned int, wxString>> GetBreakpoints() const;

protected:

	static bool TableAlreadyCreated();
	static bool CreateBreakpointDatabase();

	// Resolve the offset-map entry for the original (committed) line that the
	// editor line `line` currently maps to. Shared by Toggle/RemoveBreakpoint
	// (the resolution loop was duplicated verbatim in both). Returns end() when
	// `line` falls in an inserted, not-yet-committed region.
	std::map<unsigned int, int>::iterator ResolveOriginalLine(
		std::map<unsigned int, int>& list_module_offset, unsigned int line) const;

	//db support
	void LoadModuleCollection();
	void LoadModuleCollection(const class ibProtocolNode& store);
	void LoadBreakpointCollection(const wxString& strModuleName);

	bool ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line, const wxString& condition);
	bool RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line);
	bool OffsetBreakpointInDB(const wxString& strModuleName, unsigned int line, int offset, const wxString& condition);
	bool RemoveAllBreakpointInDB();

private:

	// THE SERVER'S DEBUGGER LISTENED TO — its own connection opened and bound (ibCommunicator::ListenDebug), each piece of
	// news handed to the window's loop and received there; a refusal said in the messages, the designer working on
	// without a debugger.
	void Listen();
	// …and one piece of news received: the state it says kept, and the listeners told it, in their words.
	void Receive(const ibProtocolNode& news);

	// A committed line, where the editor's edits since have moved it.
	int GetLineOffset(const wxString& strModuleName, const int current_line) const {

		auto iterator_module_offset = std::find_if(
			m_listOffsetBreakpoint.begin(),
			m_listOffsetBreakpoint.end(),
			[strModuleName](const auto pair) {
				return stringUtils::IsSameAs(pair.first, strModuleName);
			}
		);

		if (iterator_module_offset != m_listOffsetBreakpoint.end()) {
			auto& list_module_offset = iterator_module_offset->second;
			auto iterator_list_module_offset = list_module_offset.find(current_line - 1);
			if (iterator_list_module_offset != list_module_offset.end())
				return current_line + iterator_list_module_offset->second;
		}

		return current_line;
	}

	static ibDebuggerClient* ms_debugClient;

	ibGuid m_configGuid;

	ibDebuggerClientAdapter* m_adapter = nullptr;

	// The runtimes as the server last listed them (SearchServer).
	std::vector<ibDebuggerClientConnection> m_listConnection;

	std::map <wxString, std::map<unsigned int, ibBreakpoint>> m_listBreakpoint; //list of points: committed line -> offset + condition
	std::map <wxString, std::map<unsigned int, int>> m_listOffsetBreakpoint; //list of changed transitions

	// What the server's debugger last said of itself — a runtime attached (SessionStart, SessionEnd), and one stopped
	// (EnterLoop, LeaveLoop).
	bool	m_hasConnections = false;
	bool	m_enterLoop = false;
};

#endif
