#ifndef _DESIGNER_THIN_DEBUGGER_CLIENT_H__
#define _DESIGNER_THIN_DEBUGGER_CLIENT_H__


#include "frmclient/frmclient.h"
// THE DEBUGGER CLIENT — the engine's ibDebuggerClient (backend/debugger/debugClient.h): the server's, made for the
// configuration its session holds (ibMetaDataConfigurationStorage::OnInitialize). The copied windows ask it by the
// engine's names, through the engine's `debugClient`.
//
// ⚠ NOT ON THE WIRE YET (plan: thin-designer-editors.md §8, the debugger): the protocol says nothing of the debugger's
// state or commands, so nothing is attached — HasConnections answers false, and every command it gates (the steps,
// Continue, Pause, both stops) stays dimmed, as the desktop's with nothing attached. What the commands ask of it
// arrives with the protocol's words for them.

#include "frmclient/backend/debugger/debugDefs.h"

#define debugClient           (ibDebuggerClient::Get())

// Not exported: a stub of the header alone, nothing of it in the library to import.
class ibDebuggerClient {
public:

	// One runtime the client found (a scanner) or debugs (a debugger) — the engine's, as the attach dialog reads it.
	class ibDebuggerClientConnection {
	public:

		bool IsVerifiedConnection() const { return false; }
		bool IsConnected() const { return false; }

		wxString GetHostName() const { return wxString(); }
		unsigned short GetPort() const { return 0; }
		wxString GetComputerName() const { return wxString(); }
		wxString GetUserName() const { return wxString(); }

		ConnectionType GetConnectionType() const { return ConnectionType::ConnectionType_Unknown; }
	};

	static ibDebuggerClient* Get() {
		static ibDebuggerClient s_debugClient;
		return &s_debugClient;
	}

	// The identity a listener stamps on the questions it asks, so its answers come back to it. Empty when nothing is
	// bridged, which is also when nothing can be asked.
	wxString BridgeId() const { return wxString(); }

	// The runtimes on the ports the debugger listens to — none found yet.
	void SearchServer() {}

	void AttachConnection(const wxString& WXUNUSED(hostName), unsigned short WXUNUSED(port)) const {}
	void DetachConnection(const wxString& WXUNUSED(hostName), unsigned short WXUNUSED(port), bool WXUNUSED(kill) = false) {}

	const std::vector<ibDebuggerClientConnection*>& GetListConnection() { return m_listConnection; }

	//special public function:
	void AddExpression(const wxString& WXUNUSED(strExpression), unsigned long long WXUNUSED(id), const wxString& WXUNUSED(asker)) {}
	void ExpandExpression(const wxString& WXUNUSED(strExpression), unsigned long long WXUNUSED(id), const wxString& WXUNUSED(asker)) {}
	void RemoveExpression(unsigned long long WXUNUSED(id)) {}

	void SetLevelStack(unsigned int WXUNUSED(level)) {}

	//special functions:
	void Continue() {}
	void StepOver() {}
	void StepInto() {}
	void StepOut() {}
	void Pause() {}
	void Stop(bool WXUNUSED(kill)) {}

	// None is set — the editor's margin does not set them yet.
	bool RemoveAllBreakpoint(wxString* WXUNUSED(refusal) = nullptr) { return true; }

	bool HasConnections() const { return false; }
	bool IsEnterLoop() const { return false; }

private:

	std::vector<ibDebuggerClientConnection*> m_listConnection;
};

#endif
