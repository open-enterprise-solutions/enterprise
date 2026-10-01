#ifndef _DEBUG_CLIENT_BRIDGE_H__
#define _DEBUG_CLIENT_BRIDGE_H__

#include <wx/wx.h>
#include <wx/socket.h>

#include "backend/backend_core.h"
#include "debugDefs.h"

class BACKEND_API ibDebuggerClientBridge {
public:

	static void SetDebuggerClientBridge(ibDebuggerClientBridge* bridge);

	// ⭐ WHAT A BRIDGE IS BORN WITH, and both of these are the same kind of fact: an identity nobody
	// else may mint, and the SESSION it belongs to. Both are read HERE, at construction, on the
	// thread of whoever installs the listener — which is the only moment either is knowable. Read on
	// the socket thread instead, the session answers null, because nothing is bound there.
	ibDebuggerClientBridge();

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

	// (⛔ NO SESSION HERE. A bridge briefly carried the one it was built under, so a reply could be
	//  routed to its worker — and every bridge answered with the SAME one, because they are all
	//  installed in the same process by the same person. A fact that is identical on every instance
	//  is not a property of the instance: it belongs to the adapter, which is where it now lives.
	//  Max, 2026-09-06: *"remove the sessions from the bridges, they duplicate"*.)

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

	// The state of a filling run over there: one shape for start, status and cancel alike, since all
	// three ask about the same run. `which` is the CommandId being answered, so a waiter can tell an
	// answer to its own request from an answer to somebody else's; `accepted` false means the far end
	// refused, and `refusal` says why.
	//
	// ⚠ EMPTY BODY, LIKE ITS NEIGHBOURS ABOVE, and appended rather than inserted: the designer's own
	// bridge wants none of this, and moving a vtable slot under a partially rebuilt DLL is a fault
	// with no message.
	virtual void OnJobState(unsigned int which, const struct ibJobRunByteCodeState& state) {}

	// What a composition answered: `answered` true and the tables in `result`, or false and the
	// reason in `refusal`. One or the other, and never silence — a read that produced nothing
	// because a parameter was not set is a sentence, not an empty table.
	virtual void OnComposed(bool answered, const wxString& refusal, const wxMemoryBuffer& result) {}

private:
	// Its own, from birth — see the constructor. Nothing hands it in, so two bridges cannot be given
	// the same one and a bridge cannot be created without one.
	wxString m_bridgeId;
};


#endif