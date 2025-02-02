#ifndef _DEBUG_CLIENT_IMPL_H__
#define _DEBUG_CLIENT_IMPL_H__

#include "backend/debugger/debugClientBridge.h"

class CDebuggerClientBridge : public IDebuggerClientBridge {
public:

	//commands 
	virtual void OnSessionStart(wxSocketClient* sock);
	virtual void OnSessionEnd(wxSocketClient* sock);

	virtual void OnEnterLoop(wxSocketClient* sock, const debugLineData_t& data);
	virtual void OnLeaveLoop(wxSocketClient* sock, const debugLineData_t& data);

	virtual void OnAutoComplete(const debugAutoCompleteData_t& data);
	virtual void OnMessageFromServer(const debugLineData_t& data, const wxString& message);
	virtual void OnSetToolTip(const debugExpressionData_t& data, const wxString& resultStr);

	virtual void OnSetStack(const stackData_t& stackData);

	virtual void OnSetLocalVariable(const localWindowData_t& data);

	virtual void OnSetVariable(const watchWindowData_t& watchData);
	virtual void OnSetExpanded(const watchWindowData_t& watchData);
};

#endif