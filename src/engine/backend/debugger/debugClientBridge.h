#ifndef _DEBUG_CLIENT_BRIDGE_H__
#define _DEBUG_CLIENT_BRIDGE_H__

#include <wx/wx.h>
#include <wx/socket.h>

#include "backend/backend_core.h"
#include "debugDefs.h"

class BACKEND_API IDebuggerClientBridge {
public:

	virtual ~IDebuggerClientBridge() {}

	//commands 
	virtual void OnSessionStart(wxSocketClient* sock) = 0;
	virtual void OnSessionEnd(wxSocketClient* sock) = 0;

	virtual void OnEnterLoop(wxSocketClient* sock, const debugLineData_t& data) = 0;
	virtual void OnLeaveLoop(wxSocketClient* sock, const debugLineData_t& data) = 0;

	virtual void OnAutoComplete(const debugAutoCompleteData_t& data) = 0;
	virtual void OnMessageFromServer(const debugLineData_t& data, const wxString& message) = 0;
	virtual void OnSetToolTip(const debugExpressionData_t& data, const wxString& resultStr) = 0;

	virtual void OnSetStack(const stackData_t& stackData) = 0;

	virtual void OnSetLocalVariable(const localWindowData_t& watchData) = 0;

	virtual void OnSetVariable(const watchWindowData_t& watchData) = 0;
	virtual void OnSetExpanded(const watchWindowData_t& watchData) = 0;
};

extern BACKEND_API void SetDebuggerClientBridge(IDebuggerClientBridge *bridge);
#endif