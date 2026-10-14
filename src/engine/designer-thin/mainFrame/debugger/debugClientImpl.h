#ifndef _DEBUG_CLIENT_IMPL_H__
#define _DEBUG_CLIENT_IMPL_H__

#include "designer-thin/backend/debugger/debugClientBridge.h"

#include <memory>   // the run line is HELD, not remembered — see m_runLine below

// THE DESKTOP DESIGNER'S BRIDGE (designer/mainFrame/debugger/debugClientImpl.h) — the windows it paints are this
// client's: the module a stop or a failure is in, the run line, the stack, the locals, the watch, the Debug menu.
class ibDebuggerClientBridgeDesigner : public ibDebuggerClientBridge {
public:

	// ⚠ DECLARED, NOT DEFAULTED HERE. m_runLine holds an incomplete type on purpose — the holder is
	// an implementation detail of the .cpp — and a destructor generated in this header would need it
	// complete. Defined beside the class it destroys.
	ibDebuggerClientBridgeDesigner();
	virtual ~ibDebuggerClientBridgeDesigner();

	//commands
	virtual void OnSessionStart(wxSocketClient* sock) override;
	virtual void OnSessionEnd(wxSocketClient* sock) override;

	virtual void OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data) override;
	virtual void OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data) override;

	virtual void OnAutoComplete(const ibDebugAutoCompleteData& data) override;
	virtual void OnMessageFromServer(const ibDebugLineData& data, const wxString& message) override;
	virtual void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr) override;

	virtual void OnSetStack(const ibStackData& stackData) override;

	virtual void OnSetLocalVariable(const ibLocalWindowData& data) override;

	virtual void OnSetVariable(const ibWatchWindowData& watchData) override;
	virtual void OnSetExpanded(const ibWatchWindowData& watchData) override;

private:

	// ⭐⭐ THE RUN LINE, HELD FOR AS LONG AS THE DEBUGGER IS STOPPED. Taken on a stop, released on
	// anything that ends one — so "exactly one arrow, and only while stopped" is a property of a
	// lifetime rather than of four handlers each remembering to clear.
	//
	// ⚠ ON THE BRIDGE, because the bridge IS the session here: one debugging session, one bridge,
	// one arrow. It stood as a file-scope static for one revision, which is the same object with
	// its owner hidden.
	std::unique_ptr<class ibDebugRunLine> m_runLine;
};

#endif
