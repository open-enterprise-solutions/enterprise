#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <wx/thread.h>

#define debugClient           (CDebuggerClient::Get())
#define debugClientInit()     (CDebuggerClient::Initialize())
#define debugClientDestroy()  (CDebuggerClient::Destroy())

#include "debugClientBridge.h"

class BACKEND_API CDebuggerClient {

	static CDebuggerClient* ms_debugClient;

	std::map <wxString, std::map<unsigned int, int>> m_listBreakpoint; //список точек 
	std::map <wxString, std::map<unsigned int, int>> m_offsetPoints; //список измененных переходов

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	std::map <unsigned long long, wxString> m_expressions;
#else 
	std::map <unsigned int, wxString> m_expressions;
#endif  

	bool	m_enterLoop;

protected:

	class CDebuggerAdaptourClient : public wxEvtHandler {
		IDebuggerClientBridge* m_debugBridge;
	public:

		CDebuggerAdaptourClient() : m_debugBridge(nullptr) {}
		void SetBridge(IDebuggerClientBridge* bridge) {
			if (m_debugBridge != nullptr)
				wxDELETE(m_debugBridge);
			m_debugBridge = bridge;
		}
		virtual ~CDebuggerAdaptourClient() { wxDELETE(m_debugBridge); }

		//commands 
		void OnSessionStart(wxSocketClient* sock);
		void OnSessionEnd(wxSocketClient* sock);

		void OnEnterLoop(wxSocketClient* sock, const debugLineData_t& data);
		void OnLeaveLoop(wxSocketClient* sock, const debugLineData_t& data);

		void OnAutoComplete(const debugAutoCompleteData_t& data);
		void OnMessageFromServer(const debugLineData_t& data, const wxString& message);
		void OnSetToolTip(const debugExpressionData_t& data, const wxString& resultStr);
		void OnSetStack(const stackData_t& data);

		void OnSetLocalVariable(const localWindowData_t& data);

		void OnSetVariable(const watchWindowData_t& data);
		void OnSetExpanded(const watchWindowData_t& data);
	};

	class BACKEND_API CDebuggerThreadClient : public wxThread {
		friend class CDebuggerClient;
	private:
		bool			m_verifiedConnection;

		wxString		m_hostName;
		unsigned short	m_port;

		wxSocketClient* m_socketClient;

		wxString		m_confGuid;
		wxString		m_md5Hash;
		wxString		m_userName;
		wxString		m_compName;

		ConnectionType	m_connectionType;

	public:

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

		wxString GetHostName() const {
			return m_hostName;
		}

		unsigned short GetPort() const {
			return m_port;
		}

		wxString GetComputerName() const {
			return m_compName;
		}

		wxString GetUserName() const {
			return m_userName;
		}

		bool AttachConnection();
		bool DetachConnection(bool kill = false);

		CDebuggerClient::CDebuggerThreadClient::CDebuggerThreadClient(CDebuggerClient* client, const wxString& hostName, unsigned short port) :
			wxThread(wxTHREAD_DETACHED),
			m_hostName(hostName),
			m_port(port),
			m_verifiedConnection(false),
			m_connectionType(ConnectionType::ConnectionType_Scanner),
			m_socketClient(nullptr) {
			if (debugClient != nullptr) debugClient->AppendConnection(this);		
		}

		CDebuggerClient::CDebuggerThreadClient::~CDebuggerThreadClient() {
			if (debugClient != nullptr) debugClient->DeleteConnection(this);
			if (m_socketClient != nullptr) m_socketClient->Destroy();
		}

		// This one is called by Kill() before killing the thread and is executed
		// in the context of the thread that called Kill().
		virtual void OnKill() override;

		// entry point for the thread - called by Run() and executes in the context
		// of this thread.
		virtual ExitCode Entry();

		virtual ConnectionType GetConnectionType() const {
			return m_connectionType;
		};

	protected:

		void VerifyConnection();
		void EntryClient();

		void RecvCommand(void* pointer, unsigned int length);
		void SendCommand(void* pointer, unsigned int length);
	};

	CDebuggerClient() :
		m_activeSocket(nullptr), m_adaptour(new CDebuggerAdaptourClient), m_enterLoop(false) {
	}

	CDebuggerThreadClient* m_activeSocket = nullptr;
	CDebuggerAdaptourClient* m_adaptour = nullptr;
	std::vector<CDebuggerThreadClient*>	m_connections;

public:

	void SetBridge(IDebuggerClientBridge* bridge) {
		m_adaptour->SetBridge(bridge);
	}

	virtual ~CDebuggerClient() {
		while (m_connections.size()) {
			m_connections[m_connections.size() - 1]->Kill();
		}
		wxDELETE(m_adaptour);
	}

	static CDebuggerClient* Get() {
		return ms_debugClient;
	}

	// Force the static appData instance to Init()
	static bool Initialize();
	static void Destroy();

public:

	void ConnectToDebugger(const wxString& hostName, unsigned short port);
	CDebuggerThreadClient* FindConnection(const wxString& hostName, unsigned short port);
	CDebuggerThreadClient* FindDebugger(const wxString& hostName, unsigned short port);
	void SearchDebugger(const wxString& hostName = defaultHost, unsigned short startPort = defaultDebuggerPort);
	std::vector<CDebuggerThreadClient*>& GetConnections() {
		return m_connections;
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
	void InitializeBreakpoints(const wxString& strModuleName, unsigned int from, unsigned int to);
	void PatchBreakpoints(const wxString& strModuleName, unsigned int line, int offsetLine);

	bool SaveBreakpoints(const wxString& strModuleName);
	bool SaveAllBreakpoints();

	bool ToggleBreakpoint(const wxString& strModuleName, unsigned int line);
	bool RemoveBreakpoint(const wxString& strModuleName, unsigned int line);
	void RemoveAllBreakpoint();

	bool HasConnections() const {
		for (auto connection : m_connections) {
			if (connection->GetConnectionType() == ConnectionType::ConnectionType_Debugger) return connection->IsConnected();
		}
		return false;
	}

	bool IsEnterLoop() const {
		return m_enterLoop;
	}

public:

	template <typename T>
	void CallAfter(void (T::* method)()) {
		if (m_adaptour != nullptr) {
			wxQueueEvent(m_adaptour, new wxAsyncMethodCallEvent0<T>(static_cast<T*>(m_adaptour), method));
		}
	}

	template <typename T, typename T1, typename P1>
	void CallAfter(void (T::* method)(T1 x1), P1 x1) {
		if (m_adaptour != nullptr) {
			wxQueueEvent(m_adaptour, new wxAsyncMethodCallEvent1<T, T1>(static_cast<T*>(m_adaptour), method, x1));
		}
	}

	template <typename T, typename T1, typename T2, typename P1, typename P2>
	void CallAfter(void (T::* method)(T1 x1, T2 x2), P1 x1, P2 x2) {
		if (m_adaptour != nullptr) {
			wxQueueEvent(m_adaptour, new wxAsyncMethodCallEvent2<T, T1, T2>(static_cast<T*>(m_adaptour), method, x1, x2));
		}
	}

	template <typename T>
	void CallAfter(const T& fn) {
		if (m_adaptour != nullptr) {
			wxQueueEvent(m_adaptour, new wxAsyncMethodCallEventFunctor<T>(m_adaptour, fn));
		}
	}

protected:

	static bool TableAlreadyCreated();
	static bool CreateBreakpointDatabase();

	//db support 
	void LoadBreakpointCollection();

	bool ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line);
	bool RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line);
	bool OffsetBreakpointInDB(const wxString& strModuleName, unsigned int line, int offset);
	bool RemoveAllBreakpointInDB();

	//commands:
	void AppendConnection(CDebuggerThreadClient* client) {
		m_connections.push_back(client);
	}

	void DeleteConnection(CDebuggerThreadClient* client) {
		if (m_activeSocket == client) {
			m_activeSocket = nullptr;
		}
		if (m_connections.size() == 0) {
			m_enterLoop = false;
		}
		auto& it = std::find(
			m_connections.begin(), m_connections.end(), client
		);
		if (it != m_connections.end()) {
			m_connections.erase(it);
		}
	}

	void RecvCommand(void* pointer, unsigned int length) {}
	void SendCommand(void* pointer, unsigned int length) {
		if (m_activeSocket != nullptr) {
			m_activeSocket->SendCommand(pointer, length);
		}
		else {
			for (auto connection : m_connections) {
				connection->SendCommand(pointer, length);
			}
		}
	}
};

#endif