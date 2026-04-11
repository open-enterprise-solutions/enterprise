#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <wx/thread.h>

#define debugClient           (ibDebuggerClient::Get())
#define debugClientInit()     (ibDebuggerClient::Initialize())
#define debugClientDestroy()  (ibDebuggerClient::Destroy())

#include "debugClientBridge.h"

class BACKEND_API ibDebuggerClient {

	class BACKEND_API ibDebuggerClientAdapter : public wxEvtHandler {
	public:

		ibDebuggerClientAdapter() : m_debugBridge(nullptr) {}
		void SetBridge(ibDebuggerClientBridge* bridge) {
			if (m_debugBridge != nullptr)
				wxDELETE(m_debugBridge);
			m_debugBridge = bridge;
		}
		virtual ~ibDebuggerClientAdapter() { wxDELETE(m_debugBridge); }

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

	private:
		ibDebuggerClientBridge* m_debugBridge;
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

	ibDebuggerClient() :
		m_activeSocket(nullptr),
		m_adapter(new ibDebuggerClientAdapter),
		m_enterLoop(false), m_connectionSuccess(false)
	{
	}

public:

	void SetBridge(ibDebuggerClientBridge* bridge) { m_adapter->SetBridge(bridge); }

	virtual ~ibDebuggerClient() {
		while (m_listConnection.size()) {
			m_listConnection[m_listConnection.size() - 1]->Delete();
		}
		wxDELETE(m_adapter);
	}

	static ibDebuggerClient* Get() { return ms_debugClient; }

	// Force the static appData instance to Init()
	static bool Initialize();
	static void Destroy();

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

	//special public function:
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	void AddExpression(const wxString& strExpression, unsigned long long id);
	void ExpandExpression(const wxString& strExpression, unsigned long long id);
	void RemoveExpression(unsigned long long id);
#else 
	void AddExpression(wxString strExpression, unsigned int id);
	void ExpandExpression(wxString strExpression, unsigned int id);
	void RemoveExpression(unsigned int id);
#endif 

	void SetLevelStack(unsigned int level);

	//evaluate for tooltip
	void EvaluateToolTip(const wxString& strFileName, const wxString& strModuleName, const wxString& strExpression);

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
	void PatchModule(const wxString& strModuleName, unsigned int line, int line_offset);
	bool SaveModule(const wxString& strModuleName, unsigned int line_count);
	void RemoveModule(const wxString& strModuleName);

	bool SaveAllBreakpoints();

	bool ToggleBreakpoint(const wxString& strModuleName, unsigned int line);
	bool RemoveBreakpoint(const wxString& strModuleName, unsigned int line);
	void RemoveAllBreakpoint();

	bool HasConnections() const {

		wxCriticalSectionLocker enter(ms_criticalSectionConnection1);

		for (auto connection : m_listConnection) {
			if (ConnectionType::ConnectionType_Debugger == connection->GetConnectionType())
				return connection->IsConnected();
		}

		return false;
	}

	bool IsEnterLoop() const { return m_enterLoop; }

public:

	template <typename T>
	void CallAfter(void (T::* method)()) {
		if (m_adapter != nullptr) {
			wxQueueEvent(m_adapter, new wxAsyncMethodCallEvent0<T>(static_cast<T*>(m_adapter), method));
		}
	}

	template <typename T, typename T1, typename P1>
	void CallAfter(void (T::* method)(T1 x1), P1 x1) {
		if (m_adapter != nullptr) {
			wxQueueEvent(m_adapter, new wxAsyncMethodCallEvent1<T, T1>(static_cast<T*>(m_adapter), method, x1));
		}
	}

	template <typename T, typename T1, typename T2, typename P1, typename P2>
	void CallAfter(void (T::* method)(T1 x1, T2 x2), P1 x1, P2 x2) {
		if (m_adapter != nullptr) {
			wxQueueEvent(m_adapter, new wxAsyncMethodCallEvent2<T, T1, T2>(static_cast<T*>(m_adapter), method, x1, x2));
		}
	}

	template <typename T>
	void CallAfter(const T& fn) {
		if (m_adapter != nullptr) {
			wxQueueEvent(m_adapter, new wxAsyncMethodCallEventFunctor<T>(m_adapter, fn));
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
};

#endif