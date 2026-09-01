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
	virtual void OnMessageFromServer(const ibDebugLineData& data, const wxString& message) = 0;
	virtual void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr) = 0;

	virtual void OnSetStack(const ibStackData& stackData) = 0;

	virtual void OnSetLocalVariable(const ibLocalWindowData& watchData) = 0;

	virtual void OnSetVariable(const ibWatchWindowData& watchData) = 0;
	virtual void OnSetExpanded(const ibWatchWindowData& watchData) = 0;

private:
	// Its own, from birth. Nothing hands it in, so two bridges cannot be given the same one and a
	// bridge cannot be created without one.
	wxString m_bridgeId = ibGuid(ibGuid::newGuid()).str();
};


#endif