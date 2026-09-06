#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <wx/thread.h>
#include <atomic>   // std::atomic<short> m_number_connection_attempts (MSVC pulled it in transitively)
#include <functional>   // std::function — the deferred call the pack below is wrapped in
#include <memory>   // the adapter owns its bridges
#include <vector>

// Lifecycle: owned by ibMetaDataConfigurationStorage as a unique_ptr
// field (private ctor + friend). Same cache-pointer pattern as
// ibDebuggerServer — designer / codeEditor hot paths read the static
// slot directly through the macro.
#define debugClient           (ibDebuggerClient::Get())

#include "debugClientBridge.h"

class BACKEND_API ibDebuggerClient {

	// ⭐⭐ NOT A wxEvtHandler ANY MORE. It was one for a single reason — to be something
	// `wxQueueEvent` could deliver a deferred call to — which made the DEBUG TRANSPORT know about a
	// GUI, and put every reply, a whole report's tables included, onto the window thread whether or
	// not anybody there wanted it (Max, 2026-09-06: *"untie the adapter from wxEvtHandler, and the
	// deferred call goes to the worker"*).
	//
	// ⭐ THE WORKER ALREADY ANSWERS THE THREAD QUESTION, and it is the only one who can: the desktop
	// session's pool IS `wxTheApp::CallAfter` made into an object, the web server's is that session's
	// FIFO worker, and a headless host has none and runs the task where it stands. *"One question,
	// one door, three honest answers"* (guiSession.h). So nothing here asks which thread it is on,
	// and nothing here should: the hop belongs to whoever knows, and that is not the transport.
	class BACKEND_API ibDebuggerClientAdapter {
	public:

		// ⭐ THE SESSION IS TAKEN HERE, at birth, like a bridge's id — and this is the moment it is
		// knowable. The debug client is built from the registry's first-connect notification, so a
		// session exists and is the current one; the socket thread, where replies arrive, has
		// nothing bound and could never be asked. Out-of-line: the complete ibSession is not wanted
		// in this header.
		ibDebuggerClientAdapter();

		// 🛑 NOT COPYABLE, AND NOW IT HAS TO SAY SO. It never was — it owns its bridges through
		// unique_ptr — but the prohibition was BORROWED from wxEvtHandler, whose own copy is deleted.
		// With that base gone the compiler set about defining these, and `BACKEND_API` is what forced
		// it to: dllexport instantiates every implicitly-declared member whether or not anybody calls
		// it, so an assignment nobody wanted became an error in <vector>.
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

		// ⭐ HAND A CALL TO THE WORKER — the one place a reply changes threads, and the only thing
		// this class knows about threads. How many listeners are attached does not enter into it:
		// one hop, then the fan-out.
		void Defer(std::function<void()> call);

		// The first one installed — the IDE's, in the process that has one. Kept
		// because callers ask "is anybody bridged", not "which".
		ibDebuggerClientBridge* GetBridge() const {
			return m_debugBridges.empty() ? nullptr : m_debugBridges.front().get();
		}

		virtual ~ibDebuggerClientAdapter() {}

		//commands 
		void OnSessionStart(wxSocketClient* sock);
		void OnSessionEnd(wxSocketClient* sock);

		void OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data);
		void OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data);

		void OnAutoComplete(const ibDebugAutoCompleteData& data);
		void OnMessageFromServer(const ibDebugLineData& data, const wxString& message);
		void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr);
		void OnSetStack(const ibStackData& data);

		void OnSetLocalVariable(const ibLocalWindowData& data);

		void OnSetVariable(const ibWatchWindowData& data);
		void OnSetExpanded(const ibWatchWindowData& data);

		// A line printed by EVALUATED code — a sandbox, a watch expression. Its own road, read by
		// whoever asked for the evaluation and shown to nobody else.
		void OnEvalMessage(const wxString& message, MessageType type);

		// The picture the running application was asked for. Empty when its user declined, which is
		// an answer and not a failure.
		void OnScreenshot(const wxMemoryBuffer& png, const wxString& focus);

		// What the sandbox did — ran or failed, what it answered with, and the RESULT VALUE as
		// text when it could be written (empty when the value cannot travel, which is not a
		// failure of the run).
		void OnSandboxResult(bool ran, const wxString& answer, const wxString& json, wxLongLong_t microseconds);

		// The state of a filling run — one shape for all three requests, because starting one,
		// asking after one and stopping one are the same question about the same thing. `which`
		// says which of the three is being answered; `accepted` false means the request was
		// refused and `refusal` says why.
		void OnJobState(unsigned int which, const struct ibJobRunByteCodeState& state);

		// What a composition answered, still serialised — the tables, or a refusal saying why there
		// are none. Left as bytes because only the asker knows what to do with them.
		void OnComposed(bool answered, const wxString& refusal, const wxMemoryBuffer& result);

	private:
		std::vector<std::unique_ptr<ibDebuggerClientBridge> > m_debugBridges;

		// ⚠ WHOSE WORKER — one answer for the whole fan-out, not one per listener. Every bridge in a
		// process is installed by the same person in the same session, so a session held per bridge
		// was the same fact written twice.
		class ibSession* m_session = nullptr;
	};

	class BACKEND_API ibDebuggerClientConnection : public wxThread {
	public:

		bool IsVerifiedConnection() const {
			return m_verifiedConnection && IsConnected();
		}

		bool IsConnected() const {
			if (m_socketClient == nullptr)
				return false;
			if (!m_socketClient->IsConnected())
				return false;
			if (!m_socketClient->IsOk())
				return false;
			wxSocketError error =
				m_socketClient->LastError();
			return error == wxSOCKET_NOERROR ||
				error == wxSOCKET_WOULDBLOCK;
		}

		wxString GetHostName() const { return m_hostName; }
		unsigned short GetPort() const { return m_port; }
		wxString GetComputerName() const { return m_compName; }
		wxString GetUserName() const { return m_userName; }

		ConnectionType GetConnectionType() const { return m_connectionType; }

		void AttachConnection();
		void DetachConnection(bool kill = false);

		ibDebuggerClientConnection(ibDebuggerClient* client, const wxString& hostName, unsigned short port) :
			wxThread(wxTHREAD_DETACHED),
			m_verifiedConnection(false),
			m_hostName(hostName),
			m_port(port),
			m_socketClient(nullptr),
			m_number_connection_attempts(-1),
			m_connectionType(ConnectionType::ConnectionType_Scanner) {

			if (debugClient != nullptr)
				debugClient->AppendConnection(this);

			wxThread::SetPriority(wxPRIORITY_MIN);
		}

		~ibDebuggerClientConnection() {

			if (debugClient != nullptr)
				debugClient->DeleteConnection(this);

			if (m_socketClient != nullptr)
				m_socketClient->Destroy();
		}

		// entry point for the thread - called by Run() and executes in the context
		// of this thread.
		virtual ExitCode Entry() override;

		// This one is called by Kill() before killing the thread and is executed
		// in the context of the thread that called Kill().
		virtual void OnKill() override;

	protected:

		bool ResetConnectionCounter() {

			if (m_number_connection_attempts != wxNOT_FOUND)
				m_number_connection_attempts = 0;

			return m_number_connection_attempts != wxNOT_FOUND;
		}

		void EntryClient();

		void RecvCommand(void* pointer, unsigned int length);
		void SendCommand(void* pointer, unsigned int length);

	private:

		bool			m_verifiedConnection;

		wxString		m_hostName;
		unsigned short	m_port;

		wxSocketClient* m_socketClient;

		wxString		m_confGuid;
		wxString		m_md5Hash;
		wxString		m_userName;
		wxString		m_compName;

		std::atomic<short> m_number_connection_attempts;

		ConnectionType	m_connectionType;
		friend class ibDebuggerClient;
	};

	ibDebuggerClient();
	friend class ibMetaDataConfigurationStorage;

public:

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

	void SearchServer(bool run_debug_server = false,
		const wxString& hostName = defaultHost,
		unsigned short startPort = defaultDebuggerPort, unsigned short endPort = defaultDebuggerPort + diapasonDebuggerPort)
	{
		if (run_debug_server)
			m_connectionSuccess = false;

		for (unsigned short port = startPort; port < endPort; port++) {

			auto iterator = std::find_if(m_listConnection.begin(), m_listConnection.end(),
				[hostName, port](ibDebuggerClientConnection* client) { return client->m_hostName == hostName && client->m_port == port; }
			);

			if (iterator == m_listConnection.end()) {

				ibDebuggerClientConnection* createdConnection = new ibDebuggerClientConnection(this, hostName, port);
				if (createdConnection->Run() == wxTHREAD_NO_ERROR) {
					//if (run_debug_server)
					//	break;
					continue;
				}
				createdConnection->Delete();
			}
			else {

				bool create_new_connection = !(*iterator)->ResetConnectionCounter();
				if (create_new_connection) {
					ibDebuggerClientConnection* createdConnection = new ibDebuggerClientConnection(this, hostName, port);
					if (createdConnection->Run() == wxTHREAD_NO_ERROR) {
						//if (run_debug_server)
						//	break;
						continue;
					}
					createdConnection->Delete();
				}
			}
		}
	}

	void AttachConnection(const wxString& hostName, unsigned short port) const {

		auto iterator = std::find_if(m_listConnection.begin(), m_listConnection.end(),
			[hostName, port](ibDebuggerClientConnection* client) { return client->m_hostName == hostName && client->m_port == port; }
		);

		if (iterator != m_listConnection.end()) (*iterator)->AttachConnection();
	}

	// Auto-attach every verified Scanner connection. Used by the web
	// debug path (SpawnWebServerWithManifest): wes's debug server runs
	// with wait=false so its connection-type is Scanner, which means
	// designer's RecvCommand does NOT auto-promote to Debugger on verify.
	// Manual UI attach works (ibDialogDebugItem → OnAvailableItemSelected
	// → AttachConnection), this hook does the same for the
	// programmatic spawn-then-attach flow.
	void AttachAllVerified() const {
		for (auto* conn : m_listConnection) {
			if (conn != nullptr) conn->AttachConnection();
		}
	}

	void DetachConnection(const wxString& hostName, unsigned short port, bool kill = false) {

		auto iterator = std::find_if(m_listConnection.begin(), m_listConnection.end(),
			[hostName, port](ibDebuggerClientConnection* client) { return client->m_hostName == hostName && client->m_port == port; }
		);

		if (iterator != m_listConnection.end()) (*iterator)->DetachConnection(kill);
	}

	const std::vector<ibDebuggerClientConnection*>& GetListConnection() {
		wxCriticalSectionLocker enter(ms_criticalSectionConnection1);
		return m_listConnection;
	}

	const unsigned int GetCountConnection() const {
		wxCriticalSectionLocker enter(ms_criticalSectionConnection1);
		return m_listConnection.size();
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

	// Arbitrary code, run in the stopped runtime inside a transaction the far end always rolls
	// back. The answer arrives as OnSandboxResult.
	void RunSandbox(const wxString& code);

	// ⭐ ASK THE RUNNING APPLICATION FOR A PICTURE OF ITS WINDOW. `reason` is shown to the person on
	// the other end, who decides — the answer arrives as OnScreenshot, empty when they declined.
	// Unlike everything above it, this does not need the runtime to be stopped.
	void RequestScreenshot(const wxString& reason, const wxString& area, const wxString& format);

	// ⭐⭐ PUTTING DATA IN — start a configuration procedure over there as a background job, then
	// watch it and stop it by the token that comes back. All three answer as OnFillState.
	//
	// ⚠ LIKE THE SCREENSHOT AND UNLIKE THE SANDBOX, these do not need a stop: what is asked for is a
	// job in a session of its own, which a running application can start at any moment. Which is
	// also why a fill cannot be stepped through — there is no frame to stop in. See debugDefs.h.
	//
	// ⚠ THE CODE GOES OVER AS TEXT and is compiled at the far end, against the configuration that
	// will run it. This side compiles it too, but only to CHECK it — see jobRunByteCode.h.
	void StartJob(const struct ibJobRunRequest& request);
	void AskJobStatus(const wxString& token);
	void CancelJob(const wxString& token);

	// ⭐⭐ RUN A COMPOSITION over there and bring back what it answered. `request` is the whole schema
	// — resolved from a report, or assembled by the caller out of nothing — with its settings and
	// parameters, already serialised. The answer arrives as OnComposed.
	//
	// ⚠ It carries a SCHEMA rather than the name of one, which is what lets a caller run a report
	// that exists nowhere. And, like the fill, it needs no breakpoint: what happens over there is a
	// rented read on a connection of its own.
	void RequestCompose(const wxMemoryBuffer& request);

	//support calc strExpression in debugloop
	void EvaluateAutocomplete(const wxString& strFileName, const wxString& strModuleName, const wxString& strExpression, const wxString& keyWord, int currline);

	//get debug list
	std::vector<unsigned int> GetDebugList(const wxString& strModuleName);

	//special functions:
	void Continue();
	void StepOver();
	void StepInto();
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

	// `refusal`, when given, comes back carrying WHY it was refused — the engine states the reason
	// and the caller decides whether that is a dialog, a log line or an answer over a socket.
	bool ToggleBreakpoint(const wxString& strModuleName, unsigned int line,
		wxString* refusal = nullptr);
	bool RemoveBreakpoint(const wxString& strModuleName, unsigned int line);
	// Same shape as ToggleBreakpoint: the reason travels to whoever asked, or is said through the
	// platform's own Message when nobody did.
	bool RemoveAllBreakpoint(wxString* refusal = nullptr);

	bool HasConnections() const {

		wxCriticalSectionLocker enter(ms_criticalSectionConnection1);

		for (auto connection : m_listConnection) {
			if (ConnectionType::ConnectionType_Debugger == connection->GetConnectionType())
				return connection->IsConnected();
		}

		return false;
	}

	bool IsEnterLoop() const { return m_enterLoop; }

	// ⭐⭐ WHAT IS SET, AND WHERE — because a breakpoint OUTLIVES the run that needed it, and the
	// next run it stops is somebody else's. Read back as (module guid -> lines), with the editor
	// line each one currently sits on: the map stores a committed line plus the offset edits have
	// moved it by, and only their sum is an address anybody can use.
	//
	// 🛑 Measured on myself, 2026-09-04: a forgotten breakpoint parked a run on its second line, I
	// read "only the first message arrived", and spent several turns building a theory about a lost
	// channel. The state was knowable the whole time and nothing offered it.
	std::map<wxString, std::vector<unsigned int>> GetBreakpoints() const;

public:

	// ⭐⭐ ONE OVERLOAD, ANY NUMBER OF ARGUMENTS — a parameter pack instead of a family.
	//
	// This used to be four functions, one per arity: wx supplies event classes for none, one and
	// two arguments and stops there, so a three-argument answer grew a third by hand and a
	// four-argument one grew a fourth. Each was the same six lines with the numbers changed, and
	// each was written the day a callsite needed it — which means the family was always exactly one
	// short of what the protocol was about to ask for.
	//
	// The pack ends that: the next reply to gain a field needs nothing here at all. Two packs, not
	// one, because the METHOD's parameters and the CALLER's arguments are different types on
	// purpose — a `const wxString&` parameter takes a `wxString` argument, and deducing both from
	// one pack would refuse exactly that.
	//
	// ⚠ THE ARGUMENTS ARE COPIED INTO THE CLOSURE, which is the whole contract of a deferred call:
	// this runs later, on another thread, when every stack it was called from is gone.
	// ⚠ THE ARGUMENTS ARE COPIED INTO THE CLOSURE, which is the whole contract of a deferred call:
	// this runs later, on another thread, when every stack it was called from is gone.
	//
	// ⭐⭐ AND THE ONLY THING THAT CHANGED IS WHO CARRIES IT. This used to build the same closure and
	// queue it as a wx event at the adapter; now the adapter hands it to the WORKER. Everything else
	// is where it was — one hop here, the fan-out on the far side of it — because everything else
	// was right: the shape was never the problem, the transport knowing about a GUI was.
	template <typename T, typename... TArgs, typename... PArgs>
	void CallAfter(void (T::* method)(TArgs...), PArgs... args) {
		if (m_adapter != nullptr) {
			T* const target = static_cast<T*>(m_adapter);
			m_adapter->Defer([target, method, args...]() { (target->*method)(args...); });
		}
	}

	void SetConnectionSuccess(bool started) {
		wxCriticalSectionLocker enter(ms_criticalSectionConnection3);
		m_connectionSuccess = started;
	}

	bool GetConnectionSuccess() const {
		wxCriticalSectionLocker enter(ms_criticalSectionConnection3);
		return m_connectionSuccess;
	}

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
	void LoadBreakpointCollection(const wxString& strModuleName);

	bool ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line);
	bool RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line);
	bool OffsetBreakpointInDB(const wxString& strModuleName, unsigned int line, int offset);
	bool RemoveAllBreakpointInDB();

	//commands:
	void AppendConnection(ibDebuggerClientConnection* client) {
		wxCriticalSectionLocker enter(ms_criticalSectionConnection1);
		m_listConnection.push_back(client);
	}

	void DeleteConnection(ibDebuggerClientConnection* client) {
		wxCriticalSectionLocker enter(ms_criticalSectionConnection1);
		if (m_activeSocket == client) m_activeSocket = nullptr;
		m_listConnection.erase(
			std::remove(m_listConnection.begin(), m_listConnection.end(), client), m_listConnection.end()
		);
		if (m_listConnection.size() == 0) m_enterLoop = false;
	}

	void RecvCommand(void* pointer, unsigned int length) {}
	void SendCommand(void* pointer, unsigned int length) {
		if (m_activeSocket != nullptr) {
			m_activeSocket->SendCommand(pointer, length);
		}
		else {
			for (auto connection : m_listConnection) {
				connection->SendCommand(pointer, length);
			}
		}
	}

private:

	int GetLineOffset(const wxString& strModuleName, const int current_line) const {

		wxCriticalSectionLocker enter(ms_criticalSectionConnection2);

		auto iterator_module_offset = std::find_if(
			m_listOffsetBreakpoint.begin(),
			m_listOffsetBreakpoint.end(),
			[strModuleName](const auto pair) {
				return stringUtils::CompareString(pair.first, strModuleName);
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

	ibDebuggerClientConnection* m_activeSocket = nullptr;
	ibDebuggerClientAdapter* m_adapter = nullptr;

	static wxCriticalSection ms_criticalSectionConnection1;
	static wxCriticalSection ms_criticalSectionConnection2;
	static wxCriticalSection ms_criticalSectionConnection3;

	std::vector<ibDebuggerClientConnection*>	m_listConnection;

	std::map <wxString, std::map<unsigned int, int>> m_listBreakpoint; //list of points 
	std::map <wxString, std::map<unsigned int, int>> m_listOffsetBreakpoint; //list of changed transitions

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	std::map <unsigned long long, wxString> m_listExpression;
#else 
	std::map <unsigned int, wxString> m_listExpression;
#endif  

	bool	m_enterLoop, m_connectionSuccess;

	// Session guid of the runtime currently parked at a breakpoint.
	// Updated on each CommandId_EnterLoop receive; tagged onto every
	// outgoing Continue/Step/Pause/Detach so the server can route the
	// command to the right ibSession in a multi-tab wes process.
	wxString m_currentSessionGuid;
};

#endif