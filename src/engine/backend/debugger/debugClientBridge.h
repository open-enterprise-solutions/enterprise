#ifndef _DEBUG_CLIENT_BRIDGE_H__
#define _DEBUG_CLIENT_BRIDGE_H__

#include <wx/wx.h>
#include <wx/socket.h>

#include "backend/backend_core.h"
#include "debugDefs.h"

class BACKEND_API ibDebuggerClientBridge {
public:

	static void SetDebuggerClientBridge(ibDebuggerClientBridge* bridge);

	virtual ~ibDebuggerClientBridge() {}

	// ⭐⭐ WHO ASKED — minted here, by the bridge itself, and never spoken by anyone else.
	//
	// A debugging session has as many listeners as there are things looking at it (the IDE's
	// windows, the assistant), and an ANSWER belongs to the one that asked. Without a source every
	// answer went to every listener, and each of them had to work out whether it was theirs — the
	// watch window did it by walking its own tree for the row id, which is a guess dressed as a
	// check: it is right only because a foreign id happens not to match, and a foreign id that DID
	// match would be indistinguishable from its own.
	//
	// Generated once, here, and handed out as TEXT — which is what a question carries and what an
	// answer brings back. Nothing takes it apart; it is only ever compared.
	const wxString& GetBridgeId() const { return m_bridgeId; }

	//commands 
	virtual void OnSessionStart(wxSocketClient* sock) = 0;
	virtual void OnSessionEnd(wxSocketClient* sock) = 0;

	virtual void OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data) = 0;
	virtual void OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data) = 0;

	virtual void OnAutoComplete(const ibDebugAutoCompleteData& data) = 0;
	// ⚠ THE LEVEL IS NOT A SEVERITY OF THE PLATFORM. A document refusing to post says so at
	// `MessageType_Error` — and that is a business rule declining, not the engine failing. A real
	// fault arrives by a different road entirely (ProcessExceptionError), so a listener must not
	// read this level as "something broke".
	virtual void OnMessageFromServer(const ibDebugLineData& data, const wxString& message) = 0;
	virtual void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr) = 0;

	virtual void OnSetStack(const ibStackData& stackData) = 0;

	virtual void OnSetLocalVariable(const ibLocalWindowData& watchData) = 0;

	virtual void OnSetVariable(const ibWatchWindowData& watchData) = 0;
	virtual void OnSetExpanded(const ibWatchWindowData& watchData) = 0;

	// ⚠ NOT PURE, AND DECLARED LAST. Every other method here is required of a bridge because every
	// bridge shows it; the sandbox is asked for by one caller and is nothing to a window that never
	// runs code. Made pure, it would have broken every implementer to add a method most of them
	// would leave empty — and appending rather than inserting keeps the vtable slots below it where
	// a partially built DLL expects them.
	// ⭐ `microseconds` is how long the code itself ran, measured in the process that ran it — the
	// only place holding both edges. The platform's own clock answers to the second (a business
	// date), so code cannot time itself, and timing it from this end would measure the socket.
	virtual void OnSandboxResult(bool ran, const wxString& answer, const wxString& json, wxLongLong_t microseconds) {}

	// A line printed by evaluated code. Not shown to the person: the designer drops these, and
	// whoever asked for the evaluation reads them.
	virtual void OnEvalMessage(const wxString& message, MessageType type) {}

	// A picture of the running application's window, sent because its user allowed it. Empty bytes
	// mean they declined — the one outcome a listener must not mistake for a broken transfer.
	virtual void OnScreenshot(const wxMemoryBuffer& png, const wxString& focus) {}

private:
	// Its own, from birth. Nothing hands it in, so two bridges cannot be given the same one and a
	// bridge cannot be created without one.
	wxString m_bridgeId = ibGuid(ibGuid::newGuid()).str();
};


#endif