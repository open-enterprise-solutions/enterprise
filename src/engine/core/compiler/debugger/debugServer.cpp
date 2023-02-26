////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - server part
////////////////////////////////////////////////////////////////////////////

#include "debugServer.h"
#include "core/compiler/procUnit.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/metadata/metadata.h"
#include "frontend/mainFrame.h"
#include "utils/fs/fs.h"
#if defined(_USE_NET_COMPRESSOR)
#include "utils/fs/lz/lzhuf.h"
#endif
#include "utils/stringUtils.h"
#include "appData.h"

#include <wx/sysopt.h>
#include <wx/evtloop.h>

CDebuggerServer* CDebuggerServer::s_instance = NULL;

CDebuggerServer* CDebuggerServer::Get()
{
	if (!s_instance) {
		s_instance = new CDebuggerServer();
	}
	return s_instance;
}

void CDebuggerServer::Initialize()
{
	if (wxSystemOptions::GetOption("debug.enable") == wxT("true")) {
		if (s_instance) {
			s_instance->CreateServer(
				wxT("localhost"), defaultDebuggerPort, true
			);
		}
	}
}

void CDebuggerServer::Destroy()
{
	if (s_instance) {
		s_instance->ShutdownServer();
	}
	wxDELETE(s_instance);
}

enum eSocketTypes
{
	wxID_SOCKET_SERVER = 1
};

wxBEGIN_EVENT_TABLE(CDebuggerServer, wxEvtHandler)
EVT_SOCKET(eSocketTypes::wxID_SOCKET_SERVER, CDebuggerServer::OnSocketServerEvent)
wxEND_EVENT_TABLE()

CDebuggerServer::CDebuggerServer() : m_waitConnection(false),
m_bUseDebug(false), m_bDoLoop(false), m_bDebugLoop(false), m_bDebugStopLine(false), m_nCurrentNumberStopContext(0),
m_runContext(NULL), m_socketServer(NULL), m_socketThread(NULL)
{
}

CDebuggerServer::~CDebuggerServer()
{
	if (m_socketServer) {
		m_socketServer->Destroy();
	}
}

bool CDebuggerServer::CreateServer(const wxString& hostName, unsigned short startPort, bool wait)
{
	ShutdownServer();

	unsigned short currentPort = startPort;
	wxIPV4address addr;

	addr.Hostname(hostName);
	addr.Service(currentPort);

	m_waitConnection = wait;

	m_socketServer = new wxSocketServer(addr, wxSOCKET_WAITALL);

	while (!m_socketServer->IsOk()) {
		if (m_socketServer != NULL) {
			wxDELETE(m_socketServer);
			currentPort++;
		}
		if (currentPort > startPort + diapasonDebuggerPort)
			break;
		wxIPV4address addr;
		addr.Hostname(hostName);
		addr.Service(currentPort);

		m_socketServer = new wxSocketServer(addr);
	}

	if (m_socketServer != NULL) {
		if (wait) {
			wxSocketBase* sockClient = m_socketServer->Accept();
			wxASSERT(sockClient != NULL && m_socketThread == NULL);
			if (sockClient) {
				m_socketThread = new CServerSocketThread(sockClient);
				if (m_socketThread->Run() == wxTHREAD_NO_ERROR) {
					while (true) {
						if (!m_socketThread->IsConnected())
							break;
						if (debugServer->m_bUseDebug)
							break;
					}
				}
				if (!m_socketThread->IsConnected()) {
					m_waitConnection = false;
					if (m_socketThread->IsRunning()) {
						m_socketThread->Delete();
					}
					else {
						delete m_socketThread;
					}
					m_socketThread = NULL;
				}
			}
		}
		m_socketServer->SetEventHandler(*this, eSocketTypes::wxID_SOCKET_SERVER);
		m_socketServer->SetNotify(wxSOCKET_CONNECTION_FLAG | wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
		m_socketServer->Notify(!m_waitConnection);
	}

	return m_socketServer != NULL;
}

void CDebuggerServer::ShutdownServer()
{
	if (m_socketThread != NULL) {
		if (m_socketThread->IsConnected()) {
			m_socketThread->Disconnect();
		}
	}

	if (m_socketServer) {
		m_socketServer->Destroy();
		m_socketServer = NULL;
	}

	m_waitConnection = false;
}

#include "core/compiler/valueOLE.h"

void CDebuggerServer::ClearBreakpoints()
{
	// anytime we access the m_pThread pointer we must ensure that it won't
	// be modified in the meanwhile; since only a single thread may be
	// inside a given critical section at a given time, the following code
	// is safe:
	wxCriticalSectionLocker enter(m_clearBreakpointsCS);

	m_breakpoints.clear();
	m_offsetPoints.clear();
}

void CDebuggerServer::DoDebugLoop(const wxString& filePath, const wxString& moduleName, int line, CRunContext* pSetRunContext)
{
	m_runContext = pSetRunContext;

	if (m_socketThread == NULL || !m_socketThread->IsConnected()) {
		CDebuggerServer::ResetDebugger();
		return;
	}

	m_nCurrentNumberStopContext = 0;

	CMemoryWriter commandChannelEnterLoop;
	commandChannelEnterLoop.w_u16(CommandId_EnterLoop);
	commandChannelEnterLoop.w_stringZ(filePath);
	commandChannelEnterLoop.w_stringZ(moduleName);
	commandChannelEnterLoop.w_s32(line);
	SendCommand(commandChannelEnterLoop.pointer(), commandChannelEnterLoop.size());

	//send expressions from user 
	SendExpressions();

	//send local variable
	SendLocalVariables();

	//send stack data to designer
	SendStack();

	//start debug loop
	m_bDebugLoop = true;
	m_bDebugStopLine = false;

	//create stream for this loop
	CValueOLE::CreateStreamForDispatch();

	//start debug loop
	while (m_bDebugLoop) {
		// нет конфигуратора или как-то отвалилось соединение 
		if (!m_socketThread || !m_socketThread->IsConnected()) {
			m_bUseDebug = false;
			m_bDebugLoop = false;
			break;
		}

		//отвалился поток 
		if (!m_socketThread || !m_socketThread->IsRunning()) {
			m_bUseDebug = false;
			m_bDebugLoop = false;
			break;
		}

		wxMilliSleep(5);
	}

	if (appData->EnterpriseMode()
		&& mainFrame->IsFocusable()) {
		mainFrame->Iconize(false); // restore the window if minimized
		mainFrame->SetFocus();     // focus on my window
		mainFrame->Raise();        // bring window to front
	}

	CMemoryWriter commandChannelLeaveLoop;
	commandChannelLeaveLoop.w_u16(CommandId_LeaveLoop);
	commandChannelLeaveLoop.w_stringZ(filePath);
	commandChannelLeaveLoop.w_stringZ(moduleName);
	commandChannelLeaveLoop.w_s32(line);
	SendCommand(commandChannelLeaveLoop.pointer(), commandChannelLeaveLoop.size());

	m_runContext = NULL;
}

#include "core/compiler/definition.h"

void CDebuggerServer::EnterDebugger(CRunContext* runContext, byteRaw_t& CurCode, long& nPrevLine)
{
	if (m_bUseDebug) {
		if (CurCode.m_nOper != OPER_FUNC && CurCode.m_nOper != OPER_END
			&& CurCode.m_nOper != OPER_SET && CurCode.m_nOper != OPER_SETCONST && CurCode.m_nOper != OPER_SET_TYPE
			&& CurCode.m_nOper != OPER_TRY && CurCode.m_nOper != OPER_ENDTRY) {
			if (CurCode.m_nNumberLine != nPrevLine) {
				int offsetPoint = 0; m_bDoLoop = false;
				if (m_bDebugStopLine &&
					CurCode.m_nNumberLine >= 0) { //шагнуть в 
					std::map<unsigned int, int> offsetPointList = m_offsetPoints[CurCode.m_sDocPath];
					std::map<unsigned int, int>::iterator foundedOffsetList = offsetPointList.find(CurCode.m_nNumberLine);
					m_bDebugStopLine = false;
					m_bDoLoop = true;
					if (foundedOffsetList != offsetPointList.end()) {
						offsetPoint = foundedOffsetList->second;
					}
				}
				else if (m_nCurrentNumberStopContext &&
					m_nCurrentNumberStopContext >= CProcUnit::GetCountRunContext() &&
					CurCode.m_nNumberLine >= 0) { // шагнуть через
					std::map<unsigned int, int> aOffsetPointList = m_offsetPoints[CurCode.m_sDocPath];
					std::map<unsigned int, int>::iterator foundedOffsetList = aOffsetPointList.find(CurCode.m_nNumberLine);
					m_nCurrentNumberStopContext = CProcUnit::GetCountRunContext();
					m_bDoLoop = true;
					if (foundedOffsetList != aOffsetPointList.end()) {
						offsetPoint = foundedOffsetList->second;
					}
				}
				else {//произвольная точка останова
					if (CurCode.m_nNumberLine >= 0) {
						std::map<unsigned int, int> debugPointList = m_breakpoints[CurCode.m_sDocPath];
						std::map<unsigned int, int>::iterator foundedDebugPoint = debugPointList.find(CurCode.m_nNumberLine);

						if (foundedDebugPoint != debugPointList.end()) {
							offsetPoint = foundedDebugPoint->second; m_bDoLoop = true;
						}
					}
				}
				if (m_bDoLoop) {
					DoDebugLoop(
						CurCode.m_sFileName,
						CurCode.m_sDocPath,
						CurCode.m_nNumberLine + offsetPoint + 1,
						runContext
					);
				}
			}
			nPrevLine = CurCode.m_nNumberLine;
		}
	}
}

void CDebuggerServer::InitializeBreakpoints(const wxString& docPath, unsigned int from, unsigned int to)
{
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[docPath];
	moduleOffsets.clear(); for (unsigned int i = from; i < to; i++) {
		moduleOffsets[i] = 0;
	}
}

void CDebuggerServer::SendErrorToDesigner(const wxString& fileName,
	const wxString& docPath, unsigned int line, const wxString& errorMessage)
{
	if (!m_socketThread || !m_socketThread->IsConnected())
		return;

	if (!m_bUseDebug)
		return;

	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_MessageFromEnterprise);
	commandChannel.w_stringZ(fileName); // file name
	commandChannel.w_stringZ(docPath); // module name
	commandChannel.w_u32(line); // line code 
	commandChannel.w_stringZ(errorMessage); // error message 

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerServer::SendExpressions()
{
	if (!m_expressions.size())
		return;

	CMemoryWriter commandChannel;

	//header 
	commandChannel.w_u16(CommandId_SetExpressions);
	commandChannel.w_u32(m_expressions.size());

	for (auto expression : m_expressions)
	{
		bool bError = false;
		const CValue& vResult =
			CProcUnit::Evaluate(expression.second, m_runContext, false, &bError);
		//header 
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		commandChannel.w_u64(expression.first);
#else 
		commandChannel.w_u32(expression.first);
#endif 
		//variable
		commandChannel.w_stringZ(expression.second);
		commandChannel.w_stringZ(vResult.GetString());
		if (!bError) commandChannel.w_stringZ(vResult.GetTypeString());
		else commandChannel.w_stringZ(wxEmptyString);
		//array
		commandChannel.w_u32(vResult.GetNProps());
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerServer::SendLocalVariables()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_SetLocalVariables);

	compileContext_t* compileContext = m_runContext->m_compileContext;
	wxASSERT(compileContext);
	commandChannel.w_u32(compileContext->m_cVariables.size());

	for (auto variable : compileContext->m_cVariables) {
		const variable_t& currentVariable = variable.second;
		const CValue* locRefValue = m_runContext->m_pRefLocVars[currentVariable.m_nNumber];
		//send temp var 
		commandChannel.w_u8(currentVariable.m_bTempVar);
		//send attribute body
		commandChannel.w_stringZ(currentVariable.m_sRealName);
		commandChannel.w_stringZ(locRefValue->GetString());
		commandChannel.w_stringZ(locRefValue->GetTypeString());
		//send attribute count 
		commandChannel.w_u32(locRefValue->GetNProps());
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerServer::SendStack()
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_SetStack);
	commandChannel.w_u32(CProcUnit::GetCountRunContext());

	for (unsigned int i = CProcUnit::GetCountRunContext(); i > 0; i--) { //передаем снизу вверх

		CRunContext* runContext = CProcUnit::GetRunContext(i - 1);
		byteCode_t* byteCode = runContext->GetByteCode();
		wxASSERT(runContext && byteCode);
		compileContext_t* compileContext = runContext->m_compileContext;
		wxASSERT(compileContext);
		CCompileModule* compileModule = compileContext->m_compileModule;
		wxASSERT(compileModule);
		if (compileModule->m_bExpressionOnly)
			continue;
		if (byteCode != NULL) {
			long lCurLine = runContext->m_lCurLine;
			if (lCurLine >= 0 && lCurLine <= (long)byteCode->m_aCodeList.size()) {
				wxString sFullName = byteCode->m_aCodeList[lCurLine].m_sModuleName;
				sFullName += wxT(".");
				if (compileContext->m_functionContext) {
					sFullName += compileContext->m_functionContext->m_sRealName;
					sFullName += wxT("(");
					for (unsigned int j = 0; j < compileContext->m_functionContext->m_aParamList.size(); j++) {
						const wxString& valStr = runContext->m_pRefLocVars[compileContext->m_cVariables[StringUtils::MakeUpper(compileContext->m_functionContext->m_aParamList[j].m_sName)].m_nNumber]->GetString();
						if (j != compileContext->m_functionContext->m_aParamList.size() - 1) {
							sFullName += compileContext->m_functionContext->m_aParamList[j].m_sName + wxT(" = ") + valStr + wxT(", ");
						}
						else {
							sFullName += compileContext->m_functionContext->m_aParamList[j].m_sName + wxT(" = ") + valStr;
						}

						sFullName += wxT(")");
					}
				}
				else {
					sFullName += wxT("<initializer>");
				}
				commandChannel.w_stringZ(sFullName);
				commandChannel.w_u32(byteCode->m_aCodeList[lCurLine].m_nNumberLine + 1);
			}
		}
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerServer::RecvCommand(void* pointer, unsigned int length)
{
	if (m_socketThread != NULL) {
		m_socketThread->RecvCommand(pointer, length);
	}
}

void CDebuggerServer::SendCommand(void* pointer, unsigned int length)
{
	if (m_socketThread != NULL) {
		m_socketThread->SendCommand(pointer, length);
	}
}

void CDebuggerServer::OnSocketServerEvent(wxSocketEvent& event)
{
	if (event.GetSocketEvent() == wxSOCKET_CONNECTION) {
		if (m_socketThread == NULL && !m_waitConnection) {
			wxSocketBase* sockClient = m_socketServer->Accept();
			wxASSERT(sockClient != NULL);
			if (sockClient) {
				m_socketThread = new CServerSocketThread(sockClient);
				if (m_socketThread->Run() == wxTHREAD_NO_ERROR) {
					if (!m_socketThread->IsConnected()) {
						if (m_socketThread->IsRunning()) {
							m_socketThread->Delete();
						}
						m_socketThread = NULL;
					}
				}
				else {
					wxDELETE(m_socketThread);
				}
			}
		}
	}
	else if (event.GetSocketEvent() == wxSOCKET_LOST)
	{
	}
}

//////////////////////////////////////////////////////////////////////

void CDebuggerServer::CServerSocketThread::Disconnect()
{
	if (CServerSocketThread::IsConnected()) {
		if (m_socketClient != NULL) {
			m_socketClient->Destroy();
			m_socketClient = NULL;
		}
	}
	debugServer->EnableNotify();
}

CDebuggerServer::CServerSocketThread::CServerSocketThread(wxSocketBase* client) :
	wxThread(wxTHREAD_DETACHED), m_socketClient(client), m_connectionType(ConnectionType::ConnectionType_Unknown)
{
}

CDebuggerServer::CServerSocketThread::~CServerSocketThread()
{
	if (m_socketClient != NULL) {
		m_socketClient->Destroy();
	}

	// the thread is being destroyed; make sure not to leave dangling pointers around
	debugServer->m_socketThread = NULL;
}

wxThread::ExitCode CDebuggerServer::CServerSocketThread::Entry()
{
#ifdef __WXMSW__
	HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		wxLogSysError(hr, _("Failed to create an instance in thread!"));
	}
#endif // !_WXMSW

	ExitCode retCode = 0;

	try {
		EntryClient();
	}
	catch (...) {
		retCode = (ExitCode)1;
	}

#ifdef __WXMSW__
	if (SUCCEEDED(hr)) {
		::CoUninitialize();
	}
#endif // !_WXMSW

	debugServer->ResetDebugger();
	return retCode;
}

#define defWait 50 //msec

void CDebuggerServer::CServerSocketThread::EntryClient()
{
	while (!TestDestroy() && CServerSocketThread::IsConnected()) {
		if (m_socketClient && m_socketClient->WaitForRead(0, defWait)) {
			unsigned int length = 0;
			m_socketClient->ReadMsg(&length, sizeof(unsigned int));
			if (m_socketClient && m_socketClient->WaitForRead(0, defWait)) {
				wxMemoryBuffer bufferData(length);
				m_socketClient->ReadMsg(bufferData.GetData(), length);
				if (length > 0) {
					CValueOLE::GetInterfaceAndReleaseStream();
#if defined(_USE_NET_COMPRESSOR)
					BYTE* dest = NULL; unsigned int dest_sz = 0;
					_decompressLZ(&dest, &dest_sz, bufferData.GetData(), bufferData.GetBufSize());
					RecvCommand(dest, dest_sz); free(dest);
#else
					RecvCommand(bufferData.GetData(), bufferData.GetBufSize());
#endif 
					length = 0;
				}
			}
		}
	}

	if (CServerSocketThread::IsConnected()) {
		m_socketClient->Close();
	}

	m_connectionType = ConnectionType::ConnectionType_Unknown;
}

void CDebuggerServer::CServerSocketThread::RecvCommand(void* pointer, unsigned int length)
{
	CMemoryReader commandReader(pointer, length);
	u16 commandFromServer = commandReader.r_u16();

	if (commandFromServer == CommandId_VerifyCofiguration) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_VerifyCofiguration);
		commandChannel.w_stringZ(metadata->GetMetadataGuid());
		commandChannel.w_stringZ(metadata->GetMetadataMD5());
		commandChannel.w_stringZ(appData->GetUserName());
		commandChannel.w_stringZ(appData->GetComputerName());
		SendCommand(commandChannel.pointer(), commandChannel.size());
		m_connectionType = ConnectionType::ConnectionType_Scanner;
	}
	else if (commandFromServer == CommandId_StartSession) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_GetBreakPoints);
		SendCommand(commandChannel.pointer(), commandChannel.size());
		m_connectionType = ConnectionType::ConnectionType_Debugger;
	}
	else if (commandFromServer == CommandId_SetBreakPoints) {
		unsigned int countBreakpoints = commandReader.r_u32();
		//parse breakpoints 
		for (unsigned int i = 0; i < countBreakpoints; i++) {
			unsigned int countBreakPoints = commandReader.r_u32();
			wxString moduleName; commandReader.r_stringZ(moduleName);
			for (unsigned int j = 0; j < countBreakPoints; j++) {
				unsigned int line = commandReader.r_u32(); int offsetLine = commandReader.r_s32();
				debugServer->m_breakpoints[moduleName][line] = offsetLine;
			}
		}
		unsigned int countOffsetLines = commandReader.r_u32();
		//parse line offsets
		for (unsigned int i = 0; i < countOffsetLines; i++) {
			unsigned int countBreakPoints = commandReader.r_u32();
			wxString moduleName; commandReader.r_stringZ(moduleName);
			for (unsigned int j = 0; j < countBreakPoints; j++) {
				unsigned int line = commandReader.r_u32(); int offsetLine = commandReader.r_s32();
				debugServer->m_offsetPoints[moduleName][line] = offsetLine;
			}
		}
		debugServer->m_bUseDebug = true;
	}
	else if (commandFromServer == CommandId_ToggleBreakpoint) {
		wxString moduleName; commandReader.r_stringZ(moduleName);
		unsigned int line = commandReader.r_u32(); int offset = commandReader.r_s32();
		std::map<unsigned int, int>& moduleBreakpoints = debugServer->m_breakpoints[moduleName];
		std::map<unsigned int, int>::iterator it = moduleBreakpoints.find(line);
		if (it == moduleBreakpoints.end()) {
			moduleBreakpoints[line] = offset;
		}
	}
	else if (commandFromServer == CommandId_RemoveBreakpoint) {
		wxString moduleName; commandReader.r_stringZ(moduleName);
		unsigned int line = commandReader.r_u32();
		std::map<unsigned int, int>& moduleBreakpoints = debugServer->m_breakpoints[moduleName];
		std::map<unsigned int, int>::iterator it = moduleBreakpoints.find(line);
		if (it != moduleBreakpoints.end()) {
			moduleBreakpoints.erase(it); if (!moduleBreakpoints.size()) {
				debugServer->m_breakpoints.erase(moduleName);
			}
		}
	}
	else if (commandFromServer == CommandId_AddExpression) {
		wxString expression; commandReader.r_stringZ(expression);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		unsigned long long id = commandReader.r_u64();
#else 
		unsigned int id = commandReader.r_u32();
#endif 
		CMemoryWriter commandChannel; bool bError = false;

		commandChannel.w_u16(CommandId_SetExpressions);
		commandChannel.w_u32(1); // first elements 

		if (debugServer->IsDebugLooped()) {
			const CValue& vResult =
				CProcUnit::Evaluate(expression, debugServer->m_runContext, false, &bError);
			//header 
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
			commandChannel.w_u64(id);
#else
			commandChannel.w_u32(id);
#endif 
			//variable
			commandChannel.w_stringZ(expression);
			commandChannel.w_stringZ(vResult.GetString());
			if (!bError) {
				commandChannel.w_stringZ(vResult.GetTypeString());
			}
			else {
				commandChannel.w_stringZ(wxEmptyString);
			}
			//count of elemetns 
			commandChannel.w_u32(vResult.GetNProps());
			//send expression 
			SendCommand(commandChannel.pointer(), commandChannel.size());
			//set expression in map 
			debugServer->m_expressions.insert_or_assign(id, expression);
		}
		else {
			//header 
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
			commandChannel.w_u64(id);
#else
			commandChannel.w_u32(id);
#endif 
			//variable
			commandChannel.w_stringZ(expression);
			commandChannel.w_stringZ(wxEmptyString);
			if (!bError) {
				commandChannel.w_stringZ(wxEmptyString);
			}
			else {
				commandChannel.w_stringZ(wxEmptyString);
			}
			//count of elemetns 
			commandChannel.w_u32(0);
			//send expression 
			SendCommand(commandChannel.pointer(), commandChannel.size());
			//set expression in map 
			debugServer->m_expressions.insert_or_assign(id, expression);
		}
	}
	else if (commandFromServer == CommandId_ExpandExpression) {
		wxString expression; bool bError = false;
		commandReader.r_stringZ(expression);
		if (debugServer->IsDebugLooped()) {
			CValue vResult =
				CProcUnit::Evaluate(expression, debugServer->m_runContext, false, &bError);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
			unsigned long long id = commandReader.r_u64();
#else 
			unsigned int id = commandReader.r_u32();
#endif
			if (!bError) {
				CMemoryWriter commandChannel;
				commandChannel.w_u16(CommandId_ExpandExpression);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
				commandChannel.w_u64(id);
#else 
				commandChannel.w_u32(id);
#endif 
				//count of attribute  
				commandChannel.w_u32(vResult.GetNProps());

				//send varables 
				for (long i = 0; i < vResult.GetNProps(); i++) {
					const wxString& propName = vResult.GetPropName(i); const long lPropNum = vResult.FindProp(propName);
					if (lPropNum != wxNOT_FOUND) {
						try {
							if (!vResult.IsPropReadable(lPropNum))
								CTranslateError::Error("Object field not readable (%s)", propName);
							CValue vAttribute; vResult.GetPropVal(lPropNum, vAttribute);
							//send attribute body
							commandChannel.w_stringZ(propName);
							commandChannel.w_stringZ(vAttribute.GetString());
							if (!bError) {
								commandChannel.w_stringZ(vAttribute.GetTypeString());
							}
							else commandChannel.w_stringZ(wxEmptyString);
							//count of attribute   
							commandChannel.w_u32(vAttribute.GetNProps());
						}
						catch (const CTranslateError* err) {
							wxString errorMessage = err->what();
							errorMessage.Replace('\n', ' ');
							//send attribute body
							commandChannel.w_stringZ(propName);
							commandChannel.w_stringZ(errorMessage);
							commandChannel.w_stringZ(wxEmptyString);
							//count of attribute   
							commandChannel.w_u32(0);
						}
					}
					else {
						//send attribute body
						commandChannel.w_stringZ(propName);
						commandChannel.w_stringZ(wxEmptyString);
						commandChannel.w_stringZ(wxEmptyString);
						//count of attribute   
						commandChannel.w_u32(0);
					}
				}
				SendCommand(commandChannel.pointer(), commandChannel.size());
			}
		}
	}
	else if (commandFromServer == CommandId_RemoveExpression) {
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		debugServer->m_expressions.erase(commandReader.r_u64());
#else 
		debugServer->m_expressions.erase(commandReader.r_u32());
#endif 
	}
	else if (commandFromServer == CommandId_EvalToolTip) {
		wxString fileName, moduleName, expression; bool bError = false;
		commandReader.r_stringZ(fileName);
		commandReader.r_stringZ(moduleName);
		commandReader.r_stringZ(expression);
		if (debugServer->IsDebugLooped()) {
			const CValue& vResult =
				CProcUnit::Evaluate(expression, debugServer->m_runContext, false, &bError);
			if (vResult.GetType() != eValueTypes::TYPE_EMPTY) {
				CMemoryWriter commandChannel;

				commandChannel.w_u16(CommandId_EvalToolTip);
				commandChannel.w_stringZ(fileName);
				commandChannel.w_stringZ(moduleName);
				commandChannel.w_stringZ(expression);
				commandChannel.w_stringZ(vResult.GetString());

				if (debugServer->IsDebugLooped() && !bError) {
					SendCommand(commandChannel.pointer(), commandChannel.size());
				}
			}
		}
	}
	else if (commandFromServer == CommandId_SetStack) {
		unsigned int stackLevel = commandReader.r_u32();
		CRunContext* newRunContext =
			CProcUnit::GetRunContext(stackLevel);
		if (newRunContext) {
			debugServer->m_runContext = newRunContext;
			//send expressions from user 
			debugServer->SendExpressions();
			//send local variable
			debugServer->SendLocalVariables();
		}
	}
	else if (commandFromServer == CommandId_EvalAutocomplete) {
		
		wxString fileName, moduleName, expression, keyWord; bool bError = false;
	
		commandReader.r_stringZ(fileName);
		commandReader.r_stringZ(moduleName);
		commandReader.r_stringZ(expression);
		commandReader.r_stringZ(keyWord);

		s32 currPos = commandReader.r_s32();
		if (debugServer->IsDebugLooped()) {
			const CValue& vResult =
				CProcUnit::Evaluate(expression, debugServer->m_runContext, false, &bError);
			if (!bError) {
				
				CMemoryWriter commandChannel;		
				commandChannel.w_u16(CommandId_EvalAutocomplete);
				commandChannel.w_stringZ(fileName);
				commandChannel.w_stringZ(moduleName);
				commandChannel.w_stringZ(expression);
				commandChannel.w_stringZ(keyWord);
				commandChannel.w_s32(currPos);
				
				commandChannel.w_u32(vResult.GetNProps());
				//send varables 
				for (long i = 0; i < vResult.GetNProps(); i++) {
					const wxString& attributeName = vResult.GetPropName(i);
					commandChannel.w_stringZ(attributeName);
				}
				
				commandChannel.w_u32(vResult.GetNMethods());
				//send functions 
				for (long i = 0; i < vResult.GetNMethods(); i++) {
					const wxString& methodName = vResult.GetMethodName(i);
					const wxString& methodDescription = vResult.GetMethodHelper(i);
					//send attribute body
					commandChannel.w_stringZ(methodName);
					commandChannel.w_stringZ(methodDescription);
					commandChannel.w_u8(vResult.HasRetVal(i));
				}

				SendCommand(commandChannel.pointer(), commandChannel.size());
			}
		}
	}
	else if (commandFromServer == CommandId_PatchInsertLine ||
		commandFromServer == CommandId_PatchDeleteLine) {
		wxString moduleName; commandReader.r_stringZ(moduleName); unsigned int line = commandReader.r_u32(); int offsetLine = commandReader.r_s32();
		std::map<unsigned int, int>& moduleBreakpoints = debugServer->m_breakpoints[moduleName];
		for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) { if (((it->first + it->second) >= line)) it->second += offsetLine; }
		std::map<unsigned int, int>& moduleOffsets = debugServer->m_offsetPoints[moduleName];
		for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) { if (((it->first + it->second) >= line)) it->second += offsetLine; }
	}
	else if (commandFromServer == CommandId_PatchComplete) {
	}
	else if (commandFromServer == CommandId_Continue) {
		debugServer->m_bDebugLoop = debugServer->m_bDoLoop = false;
	}
	else if (commandFromServer == CommandId_StepInto) {
		if (debugServer->IsDebugLooped()) {
			debugServer->m_bDebugStopLine = true;
			debugServer->m_bDebugLoop = debugServer->m_bDoLoop = false;
		}
	}
	else if (commandFromServer == CommandId_StepOver) {
		if (debugServer->IsDebugLooped()) {
			debugServer->m_nCurrentNumberStopContext = CProcUnit::GetCountRunContext();
			debugServer->m_bDebugLoop = debugServer->m_bDoLoop = false;
		}
	}
	else if (commandFromServer == CommandId_Pause) {
		debugServer->m_bDebugStopLine = true;
	}
	else if (commandFromServer == CommandId_Detach) {
		debugServer->m_bUseDebug = debugServer->m_bDebugLoop = debugServer->m_bDoLoop = false;
		m_connectionType = ConnectionType::ConnectionType_Scanner;
		debugServer->EnableNotify();
	}
	else if (commandFromServer == CommandId_Destroy) {
		if (appData->Disconnect()) {
			databaseLayer->Close();
#ifdef __WXMSW__
			::CoUninitialize();
#endif // !_WXMSW
			if (m_socketClient) {
				m_socketClient->Destroy();
			}
			std::exit(EXIT_SUCCESS);
		}
	}
	else if (commandFromServer == CommandId_DeleteAllBreakpoints) {
		debugServer->m_breakpoints.clear();
	}
}

void CDebuggerServer::CServerSocketThread::SendCommand(void* pointer, unsigned int length)
{
#if defined(_USE_NET_COMPRESSOR)
	BYTE* dest = NULL; unsigned int dest_sz = 0;
	_compressLZ(&dest, &dest_sz, pointer, length);
	if (m_socketClient && m_socketClient->IsOk()) {
		m_socketClient->WriteMsg(&dest_sz, sizeof(unsigned int));
		m_socketClient->WriteMsg(dest, dest_sz);
	}
	free(dest);
#else
	if (m_socketClient && CServerSocketThread::IsConnected()) {
		m_socketClient->WriteMsg(&length, sizeof(unsigned int));
		m_socketClient->WriteMsg(pointer, length);
	}
#endif
}