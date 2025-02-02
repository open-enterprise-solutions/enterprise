#include "debugClient.h"

void CDebuggerClient::CDebuggerAdaptourClient::OnSessionStart(wxSocketClient* sock)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSessionStart(sock);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnSessionEnd(wxSocketClient* sock)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSessionEnd(sock);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnEnterLoop(wxSocketClient* sock, const debugLineData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnEnterLoop(sock, data);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnLeaveLoop(wxSocketClient* sock, const debugLineData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnLeaveLoop(sock, data);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnAutoComplete(const debugAutoCompleteData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnAutoComplete(data);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnMessageFromServer(const debugLineData_t& data, const wxString& message)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnMessageFromServer(data, message);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnSetToolTip(const debugExpressionData_t& data, const wxString& strResult)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetToolTip(data, strResult);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnSetStack(const stackData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetStack(data);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnSetLocalVariable(const localWindowData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetLocalVariable(data);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnSetVariable(const watchWindowData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetVariable(data);
	}
}

void CDebuggerClient::CDebuggerAdaptourClient::OnSetExpanded(const watchWindowData_t& data)
{
	if (m_debugBridge != nullptr) {
		m_debugBridge->OnSetExpanded(data);
	}
}
