#ifndef _DEBUGGER_SERVER_H__
#define _DEBUGGER_SERVER_H__

#include <map>
#include <queue>
#include <wx/thread.h>
#include <wx/socket.h>

class CRunContext;

#define debugServer           (CDebuggerServer::Get())
#define debugServerInit(f)    (CDebuggerServer::Initialize(f))
#define debugServerDestroy()  (CDebuggerServer::Destroy())

#include "backend/backend.h"
#include "debugDefs.h"

class BACKEND_API CDebuggerServer {

	static CDebuggerServer* ms_debugServer;

	bool		m_bUseDebug;
	bool		m_bDoLoop;
	bool		m_bDebugLoop;
	bool		m_bDebugStopLine;

	unsigned int m_nCurrentNumberStopContext;

	bool		m_waitConnection;

	std::map <wxString, std::map<unsigned int, int>> m_listBreakpoint; //список точек 
	std::map <wxString, std::map<unsigned int, int>> m_offsetPoints; //список измененных переходов

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	std::map <unsigned long long, wxString> m_expressions;
#else 
	std::map <unsigned int, wxString> m_expressions;
#endif 

	wxCriticalSection m_clearBreakpointsCS;

	CRunContext* m_runContext;
	wxSocketServer* m_socketServer;
	wxEvtHandler* m_evtHandler;

	CDebuggerServer();

public:

	virtual ~CDebuggerServer();
	static CDebuggerServer* Get() {
		return ms_debugServer;
	}

	// Force the static appData instance to Init()
	static void Initialize(const int flags);
	static void Destroy();

	bool EnableDebugging() const {
		if (m_socketServer == nullptr)
			return false;
		return true;
	}

	bool AllowDebugging() const {
		return !m_waitConnection;
	}

	bool CreateServer(const wxString& hostName = defaultHost, unsigned short startPort = defaultDebuggerPort, bool wait = false);
	void ShutdownServer();

	void EnterDebugger(CRunContext* pContext, struct CByteUnit& CurCode, long& nPrevLine);
	bool IsDebugLooped() const { return m_bDebugLoop; }

	void InitializeBreakpoints(const wxString& strDocPath, unsigned int from, unsigned int to);
	void SendErrorToClient(const wxString& strFileName, const wxString& strDocPath, unsigned int line, const wxString& strErrorMessage);

	class CDebuggerThreadServer : public wxThread {
		friend class CDebuggerServer;
	public:

		bool IsConnected() const {
			if (m_socket == nullptr)
				return false;
			if (!m_socket->IsConnected())
				return false;
			if (!m_socket->IsOk())
				return false;
			wxSocketError error =
				m_socket->LastError();
			return error == wxSOCKET_NOERROR ||
				error == wxSOCKET_WOULDBLOCK;
		}

		void Disconnect();

		ConnectionType GetConnectionType() const {
			return m_connectionType;
		}

		CDebuggerThreadServer(CDebuggerServer* server, wxSocketBase* socket);
		virtual ~CDebuggerThreadServer();

		// entry point for the thread - called by Run() and executes in the context
		// of this thread.
		virtual ExitCode Entry() override;

	protected:
		void RecvCommand(void* pointer, unsigned int length);
		void SendCommand(void* pointer, unsigned int length);
	public:
		virtual void EntryClient();
	private:
		wxSocketBase* m_socket;
		ConnectionType m_connectionType;
	};

	CDebuggerThreadServer* m_socketThread;

protected:

	void EnableNotify() {
		if (m_waitConnection) {
			if (m_socketServer != nullptr) {
				m_socketServer->Notify(true);
			}
			m_waitConnection = false;
		}
		ResetDebugger();
	}

	void ResetDebugger() {
		m_runContext = nullptr;
		m_bUseDebug = false;
		m_bDebugLoop = false;
		ClearCollectionBreakpoint();
	}

	void ClearCollectionBreakpoint();

	//main loop
	inline void DoDebugLoop(const wxString& filePath, const wxString& module, int nLine, CRunContext* pSetRunContext);

	//special functions:
	inline void SendExpressions();
	inline void SendLocalVariables();
	inline void SendStack();

	//commands:
	void RecvCommand(void* pointer, unsigned int length);
	void SendCommand(void* pointer, unsigned int length);

	//events:
	void OnSocketServerEvent(wxSocketEvent& event);

	friend class CBackendException;
};

#endif // __DEBUGGER_CLIENT_H__
