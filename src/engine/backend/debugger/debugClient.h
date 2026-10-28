#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <atomic>   // the flags a connection's thread sets and any thread reads
#include <condition_variable>   // the client's end waits for its connections to go
#include <functional>   // std::function — the deferred call the pack below is wrapped in
#include <memory>   // the adapter owns its bridges; the client's adapter, watched by what is queued for it
#include <mutex>    // the connection's socket slot, the client's lists
#include <system_error>   // std::system_error — a thread the system would not start
#include <thread>   // each connection's thread
#include <vector>

// Lifecycle: owned by ibMetaDataConfigurationStorage as a unique_ptr
// field (private ctor + friend). Same cache-pointer pattern as
// ibDebuggerServer — designer / codeEditor hot paths read the static
// slot directly through the macro.
#define debugClient           (ibDebuggerClient::Get())

#include "debugClientBridge.h"
#include "backend/session/sessionHolder.h"   // ibSessionWatch — whose worker a reply goes to, watched
#include "core/net/ioThreads.h"              // the threads every connection's socket goes by

class ibWebSocketClient;

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
			std::lock_guard<std::recursive_mutex> lock(m_bridgesMutex);
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
			std::lock_guard<std::recursive_mutex> lock(m_bridgesMutex);
			return m_debugBridges.empty() ? nullptr : m_debugBridges.front().get();
		}

		virtual ~ibDebuggerClientAdapter() {}

		//commands
		void OnSessionStart();
		void OnSessionEnd();

		void OnEnterLoop(const ibDebugLineData& data);
		void OnLeaveLoop(const ibDebugLineData& data);

		void OnAutoComplete(const ibDebugAutoCompleteData& data);
		void OnMessageFromServer(const ibDebugLineData& data, const ibString& message);
		void OnSetToolTip(const ibDebugExpressionData& data, const ibString& resultStr);
		void OnSetStack(const ibStackData& data);

		void OnSetLocalVariable(const ibLocalWindowData& data);

		void OnSetVariable(const ibWatchWindowData& data);
		void OnSetExpanded(const ibWatchWindowData& data);

		// A line printed by EVALUATED code — a sandbox, a watch expression. Its own road, read by
		// whoever asked for the evaluation and shown to nobody else.
		void OnEvalMessage(const ibString& message, MessageType type);

		// The picture the running application was asked for. Empty when its user declined, which is
		// an answer and not a failure.
		void OnScreenshot(const ibMemoryBuffer& png, const ibString& focus);

		// What the sandbox did — ran or failed, what it answered with, and the RESULT VALUE as
		// text when it could be written (empty when the value cannot travel, which is not a
		// failure of the run).
		void OnSandboxResult(bool ran, const ibString& answer, const ibString& json, long long microseconds);

		// The state of a filling run — one shape for all three requests, because starting one,
		// asking after one and stopping one are the same question about the same thing. `which`
		// says which of the three is being answered; `accepted` false means the request was
		// refused and `refusal` says why.
		void OnJobState(unsigned int which, const struct ibJobRunByteCodeState& state);

		// What a composition answered, still serialised — the tables, or a refusal saying why there
		// are none. Left as bytes because only the asker knows what to do with them.
		void OnComposed(bool answered, const ibString& refusal, const ibMemoryBuffer& result);

	private:
		std::vector<std::unique_ptr<ibDebuggerClientBridge> > m_debugBridges;
		// 🛑 THE LIST UNDER A LOCK — a listener is put in and taken out on its own thread (the assistant's, a thin
		// designer's frame going with its client), while the fan-out walks the list on the worker: a listener taken out
		// mid-walk was destroyed under the walk. Recursive: a listener told something may ask who is bridged (BridgeId),
		// and SetBridge puts one in under it.
		mutable std::recursive_mutex m_bridgesMutex;

		// ⚠ WHOSE WORKER — one answer for the whole fan-out, not one per listener. Every bridge in a
		// process is installed by the same person in the same session, so a session held per bridge
		// was the same fact written twice.
		//
		// 🛑 WATCHED, NOT POINTED AT. It was a raw pointer taken at birth, and the session is gone
		// before the client stops its socket thread: a reply arriving in that window submitted to freed
		// memory, and the designer died on its way out. A watch answers "gone" instead (sessionHolder.h).
		ibSessionWatch m_session;
		bool           m_bound = false;   // there was a session to watch at all — see Defer
	};

	class BACKEND_API ibDebuggerClientConnection {
	public:

		bool IsVerifiedConnection() const {
			return m_verifiedConnection && IsConnected();
		}

		// A socket in the slot, and not gone.
		bool IsConnected() const;

		const ibString& GetHostName() const { return m_hostName; }
		unsigned short GetPort() const { return m_port; }
		const ibString& GetComputerName() const { return m_compName; }
		const ibString& GetUserName() const { return m_userName; }

		// ⭐⭐ WHICH REVISION THE PROCESS OVER THERE IS RUNNING. The handshake has always carried
		// both halves — the configuration's GUID and its digest — and only the GUID was ever asked:
		// `m_verifiedConnection` answers "the same configuration", which is the right question for
		// deciding whether to attach at all, and says nothing about whether it is the same VERSION
		// of it. An application keeps what it read at startup — metadata, bytecode, which registers
		// a document posts to — so a change applied afterwards leaves it correct for a
		// configuration nobody is looking at any more.
		//
		// That is the one failure this surface cannot show as a failure: the code runs, nothing
		// raises, and the change simply does not appear. The digest was on the wire all along
		// (debugClient.cpp, CommandId_VerifyConnection); it only needed asking.
		const ibString& GetConfigMD5() const { return m_md5Hash; }

		ConnectionType GetConnectionType() const { return m_connectionType; }

		void AttachConnection();
		void DetachConnection(bool kill = false);

		ibDebuggerClientConnection(ibDebuggerClient* client, const ibString& hostName, unsigned short port) :
			m_verifiedConnection(false),
			m_hostName(hostName),
			m_port(port),
			m_number_connection_attempts(-1),
			m_connectionType(ConnectionType::ConnectionType_Scanner) {

			if (debugClient != nullptr)
				debugClient->AppendConnection(this);
		}

		~ibDebuggerClientConnection();

	protected:

		bool ResetConnectionCounter() {

			if (m_number_connection_attempts != wxNOT_FOUND)
				m_number_connection_attempts = 0;

			return m_number_connection_attempts != wxNOT_FOUND;
		}

		// The thread's own body — a thread of its own, detached: the connection goes with it — and the loop it runs.
		void Entry();
		void EntryClient();

		void RecvCommand(void* pointer, unsigned int length);
		void SendCommand(void* pointer, unsigned int length);

	private:

		// A socket to the port, in the slot, and the queue what it says goes to — this thread's, for one try.
		std::shared_ptr<ibDebugCommandQueue> OpenConnection();
		// The socket out of the slot, and closed. Any thread: the designer's main thread ends a session
		// (DetachConnection) while this connection's own thread lets the socket go on its way out.
		void CloseConnection();

		bool			m_verifiedConnection;

		ibString		m_hostName;
		unsigned short	m_port;

		// ⭐ A MESSAGE IS WHOLE, so the socket needs no lock of its own: the reads that came back short while a
		// write restored the blocking flags under them (2026-09-25) were wxSocketBase's. This lock only guards
		// the slot — which socket is this connection's, and its queue.
		mutable std::mutex                   m_socketMutex;
		std::shared_ptr<ibWebSocketClient>   m_socketClient;
		std::shared_ptr<ibDebugCommandQueue> m_commandQueue;

		ibString		m_confGuid;
		ibString		m_md5Hash;
		ibString		m_userName;
		ibString		m_compName;

		std::atomic<short> m_number_connection_attempts;

		// The word that the thread is to end (~ibDebuggerClient); it asks between reads.
		std::atomic<bool> m_stop{ false };

		ConnectionType	m_connectionType;
		friend class ibDebuggerClient;
	};

	// metaData — the configuration that made it and owns it; a connection's thread asks it, never "the
	// current one" (docs/private/multi-base-process.md).
	explicit ibDebuggerClient(class ibMetaDataConfigurationBase* metaData);
	friend class ibMetaDataConfigurationStorage;

public:

	class ibMetaDataConfigurationBase* GetMetaData() const { return m_metaData; }

	void SetBridge(ibDebuggerClientBridge* bridge) { m_adapter->SetBridge(bridge); }

	// The identity a listener stamps on the questions it asks, so its answers come back to it and
	// to nobody else. Empty when nothing is bridged, which is also when nothing can be asked.
	ibString BridgeId() const {
		const ibDebuggerClientBridge* bridge = m_adapter != nullptr ? m_adapter->GetBridge() : nullptr;
		return bridge != nullptr ? bridge->GetBridgeId() : ibString();
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
		const ibString& hostName = defaultHost,
		unsigned short startPort = ibPort::Debugger.m_first, unsigned short endPort = ibPort::Debugger.Last() + 1)
	{
		if (run_debug_server) {
			m_connectionSuccess = false;
			m_numberOfConnectionAttempts = numberOfLaunchConnectionAttempts;
		}

		// A new connection puts itself in the list (AppendConnection) — under this same lock, on this thread.
		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		for (unsigned short port = startPort; port < endPort; port++) {

			auto iterator = std::find_if(m_listConnection.begin(), m_listConnection.end(),
				[hostName, port](ibDebuggerClientConnection* client) { return client->m_hostName == hostName && client->m_port == port; }
			);

			// A connection to the port goes on scanning — unless it is on its way out (its counter is not reset).
			if (iterator != m_listConnection.end() && (*iterator)->ResetConnectionCounter())
				continue;

			// The connection is its thread's once the thread runs — it goes as the thread ends (Entry); one the system
			// gives no thread goes with the pointer here.
			std::unique_ptr<ibDebuggerClientConnection> createdConnection = std::make_unique<ibDebuggerClientConnection>(this, hostName, port);
			try {
				std::thread(&ibDebuggerClientConnection::Entry, createdConnection.get()).detach();
				(void)createdConnection.release();
			}
			catch (const std::system_error& err) {
				ibJournalWarning(ibT("debugger"), ibT("debug client: no thread to scan port %u: %s"),
					static_cast<unsigned>(port), ibString::FromUTF8(err.what()));
			}
		}
	}

	void AttachConnection(const ibString& hostName, unsigned short port) const {

		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		auto iterator = std::find_if(m_listConnection.begin(), m_listConnection.end(),
			[hostName, port](ibDebuggerClientConnection* client) { return client->m_hostName == hostName && client->m_port == port; }
		);

		if (iterator != m_listConnection.end()) (*iterator)->AttachConnection();
	}

	// Auto-attach every verified Scanner connection. Used by the web
	// debug path (RunApplication, useManifest): the web server's debug server runs
	// with wait=false so its connection-type is Scanner, which means
	// designer's RecvCommand does NOT auto-promote to Debugger on verify.
	// Manual UI attach works (ibDialogDebugItem → OnAvailableItemSelected
	// → AttachConnection), this hook does the same for the
	// programmatic spawn-then-attach flow.
	void AttachAllVerified() const {
		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		for (auto* conn : m_listConnection) {
			if (conn != nullptr) conn->AttachConnection();
		}
	}

	void DetachConnection(const ibString& hostName, unsigned short port, bool kill = false) {

		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		auto iterator = std::find_if(m_listConnection.begin(), m_listConnection.end(),
			[hostName, port](ibDebuggerClientConnection* client) { return client->m_hostName == hostName && client->m_port == port; }
		);

		if (iterator != m_listConnection.end()) (*iterator)->DetachConnection(kill);
	}

	// ⭐ EACH CONNECTION, VISITED UNDER THE LIST'S LOCK — a connection is a thread that deletes itself when it is done,
	// and its going waits on this lock (DeleteConnection): a connection visited is one alive. It was the list itself,
	// handed out by reference with the lock already let go, and walked on any thread while its entries died.
	template <typename Fn> void ForEachConnection(Fn&& visit) const {
		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		for (const ibDebuggerClientConnection* connection : m_listConnection)
			visit(*connection);
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
	void AddExpression(const ibString& strExpression, unsigned long long id, const ibString& asker);
	void ExpandExpression(const ibString& strExpression, unsigned long long id, const ibString& asker);
	void RemoveExpression(unsigned long long id);
#else
	void AddExpression(ibString strExpression, unsigned int id, const ibString& asker);
	void ExpandExpression(ibString strExpression, unsigned int id, const ibString& asker);
	void RemoveExpression(unsigned int id);
#endif 

	void SetLevelStack(unsigned int level);

	//evaluate for tooltip
	void EvaluateToolTip(const ibString& strFileName, const ibString& strModuleName, const ibString& strExpression);

	// Arbitrary code, run in the stopped runtime inside a transaction the far end always rolls
	// back. The answer arrives as OnSandboxResult.
	void RunSandbox(const ibString& code);

	// ⭐ ASK THE RUNNING APPLICATION FOR A PICTURE OF ITS WINDOW. `reason` is shown to the person on
	// the other end, who decides — the answer arrives as OnScreenshot, empty when they declined.
	// Unlike everything above it, this does not need the runtime to be stopped.
	void RequestScreenshot(const ibString& reason, const ibString& area, const ibString& format);

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
	void AskJobStatus(const ibString& token);
	void CancelJob(const ibString& token);

	// ⭐⭐ RUN A COMPOSITION over there and bring back what it answered. `request` is the whole schema
	// — resolved from a report, or assembled by the caller out of nothing — with its settings and
	// parameters, already serialised. The answer arrives as OnComposed.
	//
	// ⚠ It carries a SCHEMA rather than the name of one, which is what lets a caller run a report
	// that exists nowhere. And, like the fill, it needs no breakpoint: what happens over there is a
	// rented read on a connection of its own.
	void RequestCompose(const ibMemoryBuffer& request);

	//support calc strExpression in debugloop
	void EvaluateAutocomplete(const ibString& strFileName, const ibString& strModuleName, const ibString& strExpression, const ibString& keyWord, int currline);

	// The module's breakpoints as the editor draws them: editor line (from 0) -> condition (empty stops always).
	std::map<unsigned int, ibString> GetDebugList(const ibString& strModuleName);

	//special functions:
	void Continue();
	void StepOver();
	void StepInto();
	void StepOut();
	void Pause();
	void Stop(bool kill);

	//for breakpoints and offsets
	void InitializeModule(const ibString& strModuleName, unsigned int line_count);
	// atLineStart: the edit was at column 0 of `line` (whole line moved), so an
	// entry sitting ON `line` shifts too — fixes "Enter at line start" not moving a breakpoint.
	void PatchModule(const ibString& strModuleName, unsigned int line, int line_offset, bool atLineStart);
	bool SaveModule(const ibString& strModuleName, unsigned int line_count);
	void RemoveModule(const ibString& strModuleName);

	bool SaveAllBreakpoints();

	// One breakpoint, as the maps below keep it: the offset the edits since the last save have moved it by
	// (its committed line is the map's key - see ShiftLineMap), and the CONDITION it stops on. An empty
	// condition stops every time; any other is an expression of the language, evaluated in the frame that
	// reaches the line, and the run stops only when it is true.
	struct ibBreakpoint {
		int      m_offset = 0;
		ibString m_condition;
	};

	// `refusal`, when given, comes back carrying WHY it was refused — the engine states the reason
	// and the caller decides whether that is a dialog, a log line or an answer over a socket.
	// Sets the breakpoint, or gives one already there the condition passed (an empty one stops always).
	bool ToggleBreakpoint(const ibString& strModuleName, unsigned int line,
		ibString* refusal = nullptr, const ibString& condition = ibEmptyString);
	bool RemoveBreakpoint(const ibString& strModuleName, unsigned int line);
	// Same shape as ToggleBreakpoint: the reason travels to whoever asked, or is said through the
	// platform's own Message when nobody did.
	bool RemoveAllBreakpoint(ibString* refusal = nullptr);

	bool HasConnections() const {

		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);

		// ANY of them. It answered for the FIRST debugger connection in the list, so with two applications
		// attached the answer was whichever one happened to come first.
		for (auto connection : m_listConnection) {
			if (ConnectionType::ConnectionType_Debugger == connection->GetConnectionType() && connection->IsConnected())
				return true;
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
	//
	// (module guid -> (editor line -> condition)); the lines count from 0, as the editor's do.
	std::map<ibString, std::map<unsigned int, ibString>> GetBreakpoints() const;

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
	//
	// 🛑 THE CLOSURE WATCHES THE ADAPTER, IT DOES NOT POINT AT IT. A connection's thread ends with OnSessionEnd, queued as
	// the client goes; it ran after the destructor had deleted the adapter and waited on its freed lock — the designer
	// hung on exit (2026-10-26). An adapter gone tells nobody.
	template <typename T, typename... TArgs, typename... PArgs>
	void CallAfter(void (T::* method)(TArgs...), PArgs... args) {
		if (m_adapter != nullptr) {
			const std::weak_ptr<ibDebuggerClientAdapter> watched = m_adapter;
			m_adapter->Defer([watched, method, args...]() {
				if (const std::shared_ptr<ibDebuggerClientAdapter> target = watched.lock())
					(static_cast<T*>(target.get())->*method)(args...);
			});
		}
	}

	bool GetConnectionSuccess() const { return m_connectionSuccess; }

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
	void LoadBreakpointCollection(const ibString& strModuleName);

	bool ToggleBreakpointInDB(const ibString& strModuleName, unsigned int line, const ibString& condition);
	bool RemoveBreakpointInDB(const ibString& strModuleName, unsigned int line);
	bool OffsetBreakpointInDB(const ibString& strModuleName, unsigned int line, int offset, const ibString& condition);
	bool RemoveAllBreakpointInDB();

	//commands:
	void AppendConnection(ibDebuggerClientConnection* client) {
		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		m_listConnection.push_back(client);
	}

	void DeleteConnection(ibDebuggerClientConnection* client) {
		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
		if (m_activeSocket == client) m_activeSocket = nullptr;
		m_listConnection.erase(
			std::remove(m_listConnection.begin(), m_listConnection.end(), client), m_listConnection.end()
		);
		if (m_listConnection.size() == 0) m_enterLoop = false;
		m_connectionsCv.notify_all();   // the client's end may be waiting for it (~ibDebuggerClient)
	}

	void RecvCommand(void* pointer, unsigned int length) {}
	void SendCommand(void* pointer, unsigned int length) {
		std::lock_guard<std::recursive_mutex> lock(m_connectionsMutex);
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

	int GetLineOffset(const ibString& strModuleName, const int current_line) const {

		std::lock_guard<std::recursive_mutex> lock(m_breakpointsMutex);

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

	class ibMetaDataConfigurationBase* const m_metaData;

	// Shared only to be watched: what is queued for it holds it weakly (CallAfter).
	std::shared_ptr<ibDebuggerClientAdapter> m_adapter;

	// THE THREAD EVERY CONNECTION'S SOCKET GOES BY — one, for the scanners of every port; a connection's own thread
	// reads what arrived. Outlives them all: the destructor lets the connections go first.
	ibIoThreads m_ioThreads;

	// 🛑 THE CONNECTIONS AND THE STOP, UNDER ONE LOCK — the list, the connection a runtime is parked on and its session:
	// each connection's thread puts itself in, takes itself out and parks the stop, while the designer, the assistant
	// and the server's listeners walk the list. Taken on every touch of the three, the walk included. Recursive, as the
	// bridges' lock is: a call made under it may tell a listener at once (Defer with no session), who asks
	// HasConnections. Taken after m_breakpointsMutex, never before it.
	mutable std::recursive_mutex             m_connectionsMutex;
	std::condition_variable_any              m_connectionsCv;   // a connection took itself out of the list
	std::vector<ibDebuggerClientConnection*> m_listConnection;
	ibDebuggerClientConnection*              m_activeSocket = nullptr;   // the one a runtime is parked on
	// Session guid of the runtime currently parked at a breakpoint.
	// Updated on each CommandId_EnterLoop receive; tagged onto every
	// outgoing Continue/Step/Pause/Detach so the server can route the
	// command to the right ibSession in a multi-tab wes process.
	ibString                                 m_currentSessionGuid;

	// 🛑 THE BREAKPOINTS, THE LINES THE EDITS MOVED, WHAT IS WATCHED — set on the designer's thread, read on a
	// connection's (a stop's line, everything told again on a reconnect). Recursive: a module saved is initialised under it.
	mutable std::recursive_mutex m_breakpointsMutex;
	std::map <ibString, std::map<unsigned int, ibBreakpoint>> m_listBreakpoint; //list of points: committed line -> offset + condition
	std::map <ibString, std::map<unsigned int, int>> m_listOffsetBreakpoint; //list of changed transitions

	// WHAT IS WATCHED, AND WHO ASKED FOR EACH — re-registered with the runtime after every reconnect
	// (CommandId_GetArrayBreakpoint), which is the moment the name has to be said again and cannot be
	// worked out. See ibWatchedExpression (debugDefs.h).
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	std::map <unsigned long long, ibWatchedExpression> m_listExpression;
#else 
	std::map <unsigned int, ibWatchedExpression> m_listExpression;
#endif  

	// Set by a connection's thread, read by any.
	std::atomic<bool> m_enterLoop, m_connectionSuccess;

	// How many times a connection tries a port before its thread ends — more while a launched child boots
	// (SearchServer(true)), the usual again once a runtime is verified.
	std::atomic<short> m_numberOfConnectionAttempts;
};

#endif