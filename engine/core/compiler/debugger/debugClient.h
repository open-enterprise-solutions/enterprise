#ifndef __DEBUGGER_CLIENT_H__
#define __DEBUGGER_CLIENT_H__

#include <wx/wx.h>
#include <wx/socket.h>
#include <wx/thread.h>
#include <map>

#define debugClient           (CDebuggerClient::Get())
#define debugClientInit()     (CDebuggerClient::Initialize())
#define debugClientDestroy()  (CDebuggerClient::Destroy())

#include "core.h"
#include "debugEvent.h"
#include "compiler/compiler.h"

class CORE_API CDebuggerClient :
	public wxEvtHandler {

	static CDebuggerClient *s_instance;

	std::map <wxString, std::map<unsigned int, int>> m_aBreakpoints; //список точек 
	std::map <wxString, std::map<unsigned int, int>> m_aOffsetPoints; //список измененных переходов

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	std::map <unsigned long long, wxString> m_aExpressions;
#else 
	std::map <unsigned int, wxString> m_aExpressions;
#endif  

	bool m_bEnterLoop;

protected:

	class CORE_API CClientSocketThread : public wxThread {
		friend class CDebuggerClient;
	private:
		bool m_bAllowConnect;

		wxString m_hostName;
		unsigned short m_port;

		wxSocketClient *m_socketClient;

		wxString m_confGuid;
		wxString m_md5Hash;
		wxString m_userName;
		wxString m_compName;

		ConnectionType m_connectionType;

	public:

		bool IsConnected() const {
			if (m_socketClient == NULL)
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

		bool AttachConnection();
		bool DetachConnection(bool kill = false);

		CClientSocketThread(const wxString &hostName, unsigned short port);
		virtual ~CClientSocketThread();

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

		void RecvCommand(void *pointer, unsigned int length);
		void SendCommand(void *pointer, unsigned int length);
	};

	CClientSocketThread *m_socketActive;

	std::vector<CClientSocketThread *> m_aConnections;
	std::vector< wxEvtHandler* > m_aHandlers;

	CDebuggerClient();

public:

	static CDebuggerClient* Get();
	// Force the static appData instance to Init()
	static void Initialize();
	static void Destroy();

	virtual ~CDebuggerClient();

	//events: 
	void AddHandler(wxEvtHandler* handler);
	void RemoveHandler(wxEvtHandler* handler);

public:

	void ConnectToDebugger(const wxString &hostName, unsigned short port);
	CClientSocketThread *FindConnection(const wxString &hostName, unsigned short port);
	CClientSocketThread *FindDebugger(const wxString &hostName, unsigned short port);
	void FindDebuggers(const wxString &hostName = wxT("localhost"), unsigned short startPort = defaultDebuggerPort);
	std::vector<CClientSocketThread *> &GetConnections() { return m_aConnections; }

	//special public function:
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	void AddExpression(const wxString &sExpression, unsigned long long id);
	void ExpandExpression(const wxString &sExpression, unsigned long long id);
	void RemoveExpression(unsigned long long id);
#else 
	void AddExpression(wxString sExpression, unsigned int id);
	void ExpandExpression(wxString sExpression, unsigned int id);
	void RemoveExpression(unsigned int id);
#endif 

	void SetLevelStack(unsigned int level);

	//evaluate for tooltip
	void EvaluateToolTip(const wxString &sModuleName, const wxString &sExpression);

	//support calc expression in debugloop
	void EvaluateAutocomplete(void *pointer, const wxString &sExpression, const wxString &sKeyWord, int currline);

	//get debug list
	std::vector<unsigned int> GetDebugList(const wxString &sModuleName);

	//special functions:
	void Continue();
	void StepOver();
	void StepInto();
	void Pause();
	void Stop(bool kill);

	//for breakpoints and offsets 
	void InitializeBreakpoints(const wxString &sModuleName, unsigned int from, unsigned int to);
	void PatchBreakpoints(const wxString &sModuleName, unsigned int line, int offsetLine);

	bool SaveBreakpoints(const wxString &sModuleName);
	bool SaveAllBreakpoints();

	bool ToggleBreakpoint(const wxString &sModuleName, unsigned int line);
	bool RemoveBreakpoint(const wxString &sModuleName, unsigned int line);
	void RemoveAllBreakPoints();

	bool HasConnections() const { 
		for (auto connection : m_aConnections) {
			if (connection->GetConnectionType() == ConnectionType::ConnectionType_Debugger) {
				return connection->IsConnected();
			}
		}
		return false; 
	}

	bool IsEnterLoop() const { return m_bEnterLoop; }

protected:

	static wxString GetDebugPointTableName();

	//db support 
	void LoadBreakpoints();

	bool ToggleBreakpointInDB(const wxString &sModuleName, unsigned int line);
	bool RemoveBreakpointInDB(const wxString &sModuleName, unsigned int line);
	bool OffsetBreakpointInDB(const wxString &sModuleName, unsigned int line, int offset);
	bool RemoveAllBreakPointsInDB();

	//notify event: 
	void NotifyEvent(wxEvent& event);

	//commands:
	void AppendConnection(CClientSocketThread *client);
	void DeleteConnection(CClientSocketThread *client);

	void RecvCommand(void *pointer, unsigned int length);
	void SendCommand(void *pointer, unsigned int length);

	//events:
	void OnDebugEvent(wxDebugEvent &event);
	void OnDebugToolTipEvent(wxDebugToolTipEvent &event);
	void OnDebugAutoCompleteEvent(wxDebugAutocompleteEvent &event);

	wxDECLARE_EVENT_TABLE();
};

#endif