#ifndef _DEBUGGER_SERVER_H__
#define _DEBUGGER_SERVER_H__

#include <map>
#include <queue>
#include <wx/thread.h>
#include <wx/socket.h>

class CRunContext;

#define debugServer           (CDebuggerServer::Get())
#define debugServerInit()     (CDebuggerServer::Initialize())
#define debugServerDestroy()  (CDebuggerServer::Destroy())

#include "core.h"
#include "debugDefs.h"
#include "compiler/compiler.h"

class CORE_API CDebuggerServer :
	public wxEvtHandler
{
	static CDebuggerServer *s_instance;

	bool m_bUseDebug;
	bool m_bDoLoop;
	bool m_bDebugLoop;
	bool m_bDebugStopLine;

	unsigned int m_nCurrentNumberStopContext;

	bool m_waitConnection;

	std::map <wxString, std::map<unsigned int, int>> m_aBreakpoints; //список точек 
	std::map <wxString, std::map<unsigned int, int>> m_aOffsetPoints; //список измененных переходов

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	std::map <unsigned long long, wxString> m_aExpressions;
#else 
	std::map <unsigned int, wxString> m_aExpressions;
#endif 

	wxCriticalSection m_clearBreakpointsCS;

	CRunContext *m_pRunContext;
	wxSocketServer *m_socketServer;

	CDebuggerServer();

public:

	virtual ~CDebuggerServer();

	static CDebuggerServer* Get();
	// Force the static appData instance to Init()
	static void Initialize();
	static void Destroy();

	bool EnableDebugging() const {
		if (m_socketServer == NULL)
			return false;
		return true;
	}

	bool AllowDebugging() const {
		return !m_waitConnection;
	}

	bool CreateServer(const wxString &hostName = wxT("localhost"), unsigned short startPort = defaultDebuggerPort, bool wait = false);
	void ShutdownServer();

	void EnterDebugger(CRunContext *pContext, struct CByte &CurCode, int &nPrevLine);
	bool IsDebugLooped() const { return m_bDebugLoop; }

	void InitializeBreakpoints(const wxString &docPath, unsigned int from, unsigned int to);
	void SendErrorToDesigner(const wxString &fileName, const wxString &docPath, unsigned int line, const wxString &errorMessage);

	class CORE_API CServerSocketThread : public wxThread {
		friend class CDebuggerServer;
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

		void Disconnect();

		ConnectionType GetConnectionType() const {
			return m_connectionType;
		}

		CServerSocketThread(wxSocketBase *client);
		virtual ~CServerSocketThread();

		// entry point for the thread - called by Run() and executes in the context
		// of this thread.
		virtual ExitCode Entry() override;

	protected:
		void RecvCommand(void *pointer, unsigned int length);
		void SendCommand(void *pointer, unsigned int length);
	public:
		virtual void EntryClient();
	private:

		ConnectionType m_connectionType;
		wxSocketBase *m_socketClient;
	};

	CServerSocketThread *m_socketThread;

protected:

	void EnableNotify() {
		if (m_waitConnection) {
			if (m_socketServer) {
				m_socketServer->Notify(true);
			}
			m_waitConnection = false;
		}
		ResetDebugger(); 
	}

	void ResetDebugger() {
		m_pRunContext	= NULL;
		m_bUseDebug		= false;
		m_bDebugLoop	= false;
		ClearBreakpoints(); 
	}

	void ClearBreakpoints();

	//main loop
	inline void DoDebugLoop(const wxString &filePath, const wxString &module, int nLine, CRunContext *pSetRunContext);

	//special functions:
	inline void SendExpressions();
	inline void SendLocalVariables();
	inline void SendStack();

	//commands:
	void RecvCommand(void *pointer, unsigned int length);
	void SendCommand(void *pointer, unsigned int length);

	//events:
	void OnSocketServerEvent(wxSocketEvent &evt);

	friend class CTranslateError;

protected:

	wxDECLARE_EVENT_TABLE();
};

#endif // __DEBUGGER_CLIENT_H__
