////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - server part
////////////////////////////////////////////////////////////////////////////

#include "debugServer.h"

#include "backend/compiler/procUnit.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metadataConfiguration.h"

#include "backend/fileSystem/fs.h"
#if _USE_NET_COMPRESSOR == 1
#include "utils/fs/lz/lzhuf.h"
#endif

#include "backend/appData.h"

///////////////////////////////////////////////////////////////////////
CDebuggerServer* CDebuggerServer::ms_debugServer = nullptr;
///////////////////////////////////////////////////////////////////////

void CDebuggerServer::Initialize(const int flags)
{
	if (ms_debugServer != nullptr) ms_debugServer->Destroy();
	ms_debugServer = new CDebuggerServer();
}

void CDebuggerServer::Destroy()
{
	if (ms_debugServer != nullptr) ms_debugServer->ShutdownServer();
	wxDELETE(ms_debugServer);
}

enum eSocketTypes
{
	wxID_SOCKET_SERVER = 1
};

CDebuggerServer::CDebuggerServer() : m_waitConnection(false),
m_bUseDebug(false), m_bDoLoop(false), m_bDebugLoop(false), m_bDebugStopLine(false), m_nCurrentNumberStopContext(0),
m_runContext(nullptr), m_socketServer(nullptr), m_socketThread(nullptr)
{
	m_evtHandler = new wxEvtHandler();
	m_evtHandler->Bind(wxEVT_SOCKET, &CDebuggerServer::OnSocketServerEvent, this, eSocketTypes::wxID_SOCKET_SERVER, eSocketTypes::wxID_SOCKET_SERVER);
}

CDebuggerServer::~CDebuggerServer()
{
	if (m_socketServer != nullptr) m_socketServer->Destroy();
	wxDELETE(m_evtHandler);
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
	m_socketServer->SetTimeout(10);

	while (!m_socketServer->IsOk()) {

		if (m_socketServer != nullptr) {
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

	if (m_socketServer != nullptr) {

		if (wait) {

			wxSocketBase* sockClient = m_socketServer->Accept();
			wxASSERT_MSG(sockClient != nullptr && m_socketThread == nullptr, "client not connected!");
			if (sockClient != nullptr) {
				m_socketThread = new CDebuggerThreadServer(this, sockClient);
				if (m_socketThread->Run() == wxTHREAD_NO_ERROR) {
					while (m_socketThread != nullptr) {
						if (!m_socketThread->IsConnected() || m_bUseDebug)
							break;
						wxMilliSleep(5);
					}
				}
				if (m_socketThread == nullptr) {
					m_waitConnection = false;
				}
				else if (!m_socketThread->IsConnected()) {
					if (m_socketThread->IsRunning()) {
						m_socketThread->Delete();
						m_socketThread = nullptr;
					}
					else {
						wxDELETE(m_socketThread);
					}
				}
			}
		}

		m_socketServer->SetEventHandler(*m_evtHandler, eSocketTypes::wxID_SOCKET_SERVER);
		m_socketServer->SetNotify(wxSOCKET_CONNECTION_FLAG | wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
		m_socketServer->Notify(!m_waitConnection);
	}

	return m_socketServer != nullptr;
}

void CDebuggerServer::ShutdownServer()
{
	if (m_socketThread != nullptr && m_socketThread->IsConnected())
		m_socketThread->Disconnect();

	if (m_socketServer != nullptr) {
		m_socketServer->Destroy();
		m_socketServer = nullptr;
	}

	m_waitConnection = false;
}

#include "backend/compiler/value/valueOLE.h"
#include "backend/backend_mainFrame.h"

void CDebuggerServer::ClearCollectionBreakpoint()
{
	// anytime we access the m_pThread pointer we must ensure that it won't
	// be modified in the meanwhile; since only a single thread may be
	// inside a given critical section at a given time, the following code
	// is safe:
	wxCriticalSectionLocker enter(m_clearBreakpointsCS);

	m_listBreakpoint.clear();
	m_offsetPoints.clear();
}

void CDebuggerServer::DoDebugLoop(const wxString& filePath, const wxString& strModuleName, int line, CRunContext* pSetRunContext)
{
	m_runContext = pSetRunContext;

	if (m_socketThread == nullptr || !m_socketThread->IsConnected()) {
		CDebuggerServer::ResetDebugger();
		return;
	}

	m_nCurrentNumberStopContext = 0;

	CMemoryWriter commandChannelEnterLoop;
	commandChannelEnterLoop.w_u16(CommandId_EnterLoop);
	commandChannelEnterLoop.w_stringZ(filePath);
	commandChannelEnterLoop.w_stringZ(strModuleName);
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
		if (m_socketThread == nullptr || !m_socketThread->IsConnected()) {
			m_bUseDebug = false;
			m_bDebugLoop = false;
			break;
		}

		//отвалился поток 
		if (m_socketThread == nullptr || !m_socketThread->IsRunning()) {
			m_bUseDebug = false;
			m_bDebugLoop = false;
			break;
		}

		wxMilliSleep(5);
	}

	CValueOLE::ReleaseStreamForDispatch();

	if (backend_mainFrame != nullptr)
		backend_mainFrame->RaiseFrame();

	CMemoryWriter commandChannelLeaveLoop;
	commandChannelLeaveLoop.w_u16(CommandId_LeaveLoop);
	commandChannelLeaveLoop.w_stringZ(filePath);
	commandChannelLeaveLoop.w_stringZ(strModuleName);
	commandChannelLeaveLoop.w_s32(line);
	SendCommand(commandChannelLeaveLoop.pointer(), commandChannelLeaveLoop.size());

	m_runContext = nullptr;
}

void CDebuggerServer::EnterDebugger(CRunContext* runContext, CByteUnit& CurCode, long& nPrevLine)
{
	if (m_bUseDebug) {
		if (CurCode.m_nOper != OPER_FUNC && CurCode.m_nOper != OPER_END
			&& CurCode.m_nOper != OPER_SET && CurCode.m_nOper != OPER_SETCONST && CurCode.m_nOper != OPER_SET_TYPE
			&& CurCode.m_nOper != OPER_TRY && CurCode.m_nOper != OPER_ENDTRY) {
			if (CurCode.m_nNumberLine != nPrevLine) {
				int offsetPoint = 0; m_bDoLoop = false;
				if (m_bDebugStopLine &&
					CurCode.m_nNumberLine >= 0) { //шагнуть в 
					std::map<unsigned int, int> offsetPointList = m_offsetPoints[CurCode.m_strDocPath];
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
					std::map<unsigned int, int> aOffsetPointList = m_offsetPoints[CurCode.m_strDocPath];
					std::map<unsigned int, int>::iterator foundedOffsetList = aOffsetPointList.find(CurCode.m_nNumberLine);
					m_nCurrentNumberStopContext = CProcUnit::GetCountRunContext();
					m_bDoLoop = true;
					if (foundedOffsetList != aOffsetPointList.end()) {
						offsetPoint = foundedOffsetList->second;
					}
				}
				else {//произвольная точка останова
					if (CurCode.m_nNumberLine >= 0) {
						std::map<unsigned int, int> debugPointList = m_listBreakpoint[CurCode.m_strDocPath];
						std::map<unsigned int, int>::iterator foundedDebugPoint = debugPointList.find(CurCode.m_nNumberLine);

						if (foundedDebugPoint != debugPointList.end()) {
							offsetPoint = foundedDebugPoint->second; m_bDoLoop = true;
						}
					}
				}
				if (m_bDoLoop) {
					DoDebugLoop(
						CurCode.m_strFileName,
						CurCode.m_strDocPath,
						CurCode.m_nNumberLine + offsetPoint + 1,
						runContext
					);
				}
			}
			nPrevLine = CurCode.m_nNumberLine;
		}
	}
}

void CDebuggerServer::InitializeBreakpoints(const wxString& strDocPath, unsigned int from, unsigned int to)
{
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[strDocPath];
	moduleOffsets.clear(); for (unsigned int i = from; i < to; i++) {
		moduleOffsets[i] = 0;
	}
}

void CDebuggerServer::SendErrorToClient(const wxString& strFileName,
	const wxString& strDocPath, unsigned int line, const wxString& strErrorMessage)
{
	if (!m_socketThread || !m_socketThread->IsConnected())
		return;

	if (!m_bUseDebug)
		return;

	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_MessageFromServer);
	commandChannel.w_stringZ(strFileName); // file name
	commandChannel.w_stringZ(strDocPath); // module name
	commandChannel.w_u32(line); // line code 
	commandChannel.w_stringZ(strErrorMessage); // error message 

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

	CValue vResult;

	for (auto expression : m_expressions) {
		//header 
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		commandChannel.w_u64(expression.first);
#else 
		commandChannel.w_u32(expression.first);
#endif 
		//variable
		commandChannel.w_stringZ(expression.second);

		if (CProcUnit::Evaluate(expression.second, m_runContext, vResult, false)) {
			commandChannel.w_stringZ(vResult.GetString());
			commandChannel.w_stringZ(vResult.GetClassName());
			//array
			commandChannel.w_u32(vResult.GetNProps());
		}
		else {
			commandChannel.w_stringZ(CBackendException::GetLastError());
			commandChannel.w_stringZ(wxT("<error>"));
			//array
			commandChannel.w_u32(0);
		}
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerServer::SendLocalVariables()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_SetLocalVariables);

	CCompileContext* compileContext = m_runContext->m_compileContext;
	wxASSERT(compileContext);
	commandChannel.w_u32(compileContext->m_cVariables.size());

	for (auto variable : compileContext->m_cVariables) {
		const CVariable& currentVariable = variable.second;
		const CValue* locRefValue = m_runContext->m_pRefLocVars[currentVariable.m_nNumber];
		//send temp var 
		commandChannel.w_u8(currentVariable.m_bTempVar);
		//send attribute body
		commandChannel.w_stringZ(currentVariable.m_strRealName);
		commandChannel.w_stringZ(locRefValue->GetString());
		commandChannel.w_stringZ(locRefValue->GetClassName());
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
		CByteCode* byteCode = runContext->GetByteCode();
		wxASSERT(runContext && byteCode);
		CCompileContext* compileContext = runContext->m_compileContext;
		wxASSERT(compileContext);
		CCompileCode* compileCode = compileContext->m_compileModule;
		wxASSERT(compileCode);
		if (compileCode->m_bExpressionOnly)
			continue;
		if (byteCode != nullptr) {
			long lCurLine = runContext->m_lCurLine;
			if (lCurLine >= 0 && lCurLine <= (long)byteCode->m_aCodeList.size()) {
				wxString strFullName = byteCode->m_aCodeList[lCurLine].m_strModuleName;
				strFullName += wxT(".");
				if (compileContext->m_functionContext) {
					strFullName += compileContext->m_functionContext->m_strRealName;
					strFullName += wxT("(");
					for (unsigned int j = 0; j < compileContext->m_functionContext->m_aParamList.size(); j++) {
						const wxString& valStr = runContext->m_pRefLocVars[compileContext->m_cVariables[stringUtils::MakeUpper(compileContext->m_functionContext->m_aParamList[j].m_strName)].m_nNumber]->GetString();
						if (j != compileContext->m_functionContext->m_aParamList.size() - 1) {
							strFullName += compileContext->m_functionContext->m_aParamList[j].m_strName + wxT(" = ") + valStr + wxT(", ");
						}
						else {
							strFullName += compileContext->m_functionContext->m_aParamList[j].m_strName + wxT(" = ") + valStr;
						}
					}
					strFullName += wxT(")");
				}
				else {
					strFullName += wxT("<initializer>");
				}
				commandChannel.w_stringZ(strFullName);
				commandChannel.w_u32(byteCode->m_aCodeList[lCurLine].m_nNumberLine + 1);
			}
		}
	}

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerServer::RecvCommand(void* pointer, unsigned int length)
{
	if (m_socketThread != nullptr) {
		m_socketThread->RecvCommand(pointer, length);
	}
}

void CDebuggerServer::SendCommand(void* pointer, unsigned int length)
{
	if (m_socketThread != nullptr) {
		m_socketThread->SendCommand(pointer, length);
	}
}

void CDebuggerServer::OnSocketServerEvent(wxSocketEvent& event)
{
	if (event.GetSocketEvent() == wxSOCKET_CONNECTION) {
		if (m_socketThread == nullptr && !m_waitConnection) {
			wxSocketBase* sockClient = m_socketServer->Accept();
			if (sockClient != nullptr) {
				m_socketThread = new CDebuggerThreadServer(this, sockClient);
				if (m_socketThread->Run() == wxTHREAD_NO_ERROR) {
					if (!m_socketThread->IsConnected()) {
						if (m_socketThread->IsRunning()) {
							m_socketThread->Delete();
						}
						m_socketThread = nullptr;
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

void CDebuggerServer::CDebuggerThreadServer::Disconnect()
{
	if (CDebuggerThreadServer::IsConnected()) {
		if (m_socket != nullptr) {
			m_socket->Destroy();
			m_socket = nullptr;
		}
	}
	ms_debugServer->EnableNotify();
}

CDebuggerServer::CDebuggerThreadServer::CDebuggerThreadServer(CDebuggerServer* server, wxSocketBase* socket) :
	wxThread(wxTHREAD_DETACHED), m_socket(socket), m_connectionType(ConnectionType::ConnectionType_Unknown)
{
}

CDebuggerServer::CDebuggerThreadServer::~CDebuggerThreadServer()
{
	if (m_socket != nullptr) {
		m_socket->Destroy();
	}

	// the thread is being destroyed; make sure not to leave dangling pointers around
	ms_debugServer->m_socketThread = nullptr;
}

wxThread::ExitCode CDebuggerServer::CDebuggerThreadServer::Entry()
{
#ifdef __WXMSW__
	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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

	if (ms_debugServer != nullptr) ms_debugServer->ResetDebugger();
	return retCode;
}

#define defWait 50 //msec

void CDebuggerServer::CDebuggerThreadServer::EntryClient()
{
	while (!TestDestroy() && CDebuggerThreadServer::IsConnected()) {
		if (m_socket && m_socket->WaitForRead(0, defWait)) {
			unsigned int length = 0;
			m_socket->ReadMsg(&length, sizeof(unsigned int));
			if (m_socket && m_socket->WaitForRead(0, defWait)) {
				wxMemoryBuffer bufferData(length);
				m_socket->ReadMsg(bufferData.GetData(), length);
				if (length > 0) {
					CValueOLE::GetInterfaceAndReleaseStream();
#if _USE_NET_COMPRESSOR == 1
					BYTE* dest = nullptr; unsigned int dest_sz = 0;
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

	if (CDebuggerThreadServer::IsConnected()) m_socket->Close();
	m_connectionType = ConnectionType::ConnectionType_Unknown;
}

void CDebuggerServer::CDebuggerThreadServer::RecvCommand(void* pointer, unsigned int length)
{
	CMemoryReader commandReader(pointer, length);
	wxASSERT(debugServer != nullptr);
	u16 commandFromClient = commandReader.r_u16();

	if (commandFromClient == CommandId_VerifyConnection) {

		m_connectionType = ms_debugServer->m_waitConnection ?
			ConnectionType::ConnectionType_Waiter : ConnectionType::ConnectionType_Scanner;

		CMemoryWriter commandChannel_SetConnectionType;
		commandChannel_SetConnectionType.w_u16(CommandId_SetConnectionType);
		commandChannel_SetConnectionType.w_u16(m_connectionType);
		SendCommand(commandChannel_SetConnectionType.pointer(), commandChannel_SetConnectionType.size());

		CMemoryWriter commandChannel_VerifyConnection;
		commandChannel_VerifyConnection.w_u16(CommandId_VerifyConnection);
		commandChannel_VerifyConnection.w_stringZ(commonMetaData->GetConfigGuid());
		commandChannel_VerifyConnection.w_stringZ(commonMetaData->GetConfigMD5());
		commandChannel_VerifyConnection.w_stringZ(appData->GetUserName());
		commandChannel_VerifyConnection.w_stringZ(appData->GetComputerName());
		SendCommand(commandChannel_VerifyConnection.pointer(), commandChannel_VerifyConnection.size());
	}
	else if (commandFromClient == CommandId_SetConnectionType) {
		m_connectionType = static_cast<ConnectionType>(commandReader.r_u16());
		if (m_connectionType == ConnectionType::ConnectionType_Unknown) {
			if (debugServer != nullptr && debugServer->m_socketThread != nullptr) debugServer->m_socketThread->Disconnect();
		}
	}
	else if (commandFromClient == CommandId_StartSession) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_GetArrayBreakpoint);
		SendCommand(commandChannel.pointer(), commandChannel.size());
		m_connectionType = ConnectionType::ConnectionType_Debugger;
	}
	else if (commandFromClient == CommandId_SetArrayBreakpoint) {
		unsigned int countBreakpoints = commandReader.r_u32();
		//parse breakpoints 
		for (unsigned int i = 0; i < countBreakpoints; i++) {
			unsigned int countBreakPoints = commandReader.r_u32();
			wxString strModuleName; commandReader.r_stringZ(strModuleName);
			for (unsigned int j = 0; j < countBreakPoints; j++) {
				unsigned int line = commandReader.r_u32(); int offsetLine = commandReader.r_s32();
				ms_debugServer->m_listBreakpoint[strModuleName][line] = offsetLine;
			}
		}
		unsigned int countOffsetLines = commandReader.r_u32();
		//parse line offsets
		for (unsigned int i = 0; i < countOffsetLines; i++) {
			unsigned int countBreakPoints = commandReader.r_u32();
			wxString strModuleName; commandReader.r_stringZ(strModuleName);
			for (unsigned int j = 0; j < countBreakPoints; j++) {
				unsigned int line = commandReader.r_u32(); int offsetLine = commandReader.r_s32();
				ms_debugServer->m_offsetPoints[strModuleName][line] = offsetLine;
			}
		}
		ms_debugServer->m_bUseDebug = true;
	}
	else if (commandFromClient == CommandId_ToggleBreakpoint) {
		wxString strModuleName; commandReader.r_stringZ(strModuleName);
		unsigned int line = commandReader.r_u32(); int offset = commandReader.r_s32();
		std::map<unsigned int, int>& moduleBreakpoints = ms_debugServer->m_listBreakpoint[strModuleName];
		std::map<unsigned int, int>::iterator it = moduleBreakpoints.find(line);
		if (it == moduleBreakpoints.end()) {
			moduleBreakpoints[line] = offset;
		}
	}
	else if (commandFromClient == CommandId_RemoveBreakpoint) {
		wxString strModuleName; commandReader.r_stringZ(strModuleName);
		unsigned int line = commandReader.r_u32();
		std::map<unsigned int, int>& moduleBreakpoints = ms_debugServer->m_listBreakpoint[strModuleName];
		std::map<unsigned int, int>::iterator it = moduleBreakpoints.find(line);
		if (it != moduleBreakpoints.end()) {
			moduleBreakpoints.erase(it); if (!moduleBreakpoints.size()) {
				ms_debugServer->m_listBreakpoint.erase(strModuleName);
			}
		}
	}
	else if (commandFromClient == CommandId_AddExpression) {
		wxString strExpression; commandReader.r_stringZ(strExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		unsigned long long id = commandReader.r_u64();
#else 
		unsigned int id = commandReader.r_u32();
#endif 
		CMemoryWriter commandChannel;

		commandChannel.w_u16(CommandId_SetExpressions);
		commandChannel.w_u32(1); // first elements 

		if (ms_debugServer->IsDebugLooped()) {
			CValue vResult;
			//header 
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			commandChannel.w_u64(id);
#else
			commandChannel.w_u32(id);
#endif 
			//variable
			commandChannel.w_stringZ(strExpression);
			if (CProcUnit::Evaluate(strExpression, ms_debugServer->m_runContext, vResult, false)) {
				commandChannel.w_stringZ(vResult.GetString());
				commandChannel.w_stringZ(vResult.GetClassName());
				//count of elemetns 
				commandChannel.w_u32(vResult.GetNProps());
			}
			else {
				commandChannel.w_stringZ(CBackendException::GetLastError());
				commandChannel.w_stringZ(wxT("<error>"));
				//count of elemetns 
				commandChannel.w_u32(0);
			}
			//send expression 
			SendCommand(commandChannel.pointer(), commandChannel.size());
			//set expression in map 
			ms_debugServer->m_expressions.insert_or_assign(id, strExpression);
		}
		else {
			//header 
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			commandChannel.w_u64(id);
#else
			commandChannel.w_u32(id);
#endif 
			//variable
			commandChannel.w_stringZ(strExpression);
			commandChannel.w_stringZ(wxEmptyString);
			commandChannel.w_stringZ(wxEmptyString);
			//count of elemetns 
			commandChannel.w_u32(0);
			//send expression 
			SendCommand(commandChannel.pointer(), commandChannel.size());
			//set expression in map 
			ms_debugServer->m_expressions.insert_or_assign(id, strExpression);
		}
	}
	else if (commandFromClient == CommandId_ExpandExpression) {
		wxString strExpression;
		commandReader.r_stringZ(strExpression);
		if (ms_debugServer->IsDebugLooped()) {
			CValue vResult;
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			unsigned long long id = commandReader.r_u64();
#else 
			unsigned int id = commandReader.r_u32();
#endif
			if (CProcUnit::Evaluate(strExpression, ms_debugServer->m_runContext, vResult, false)) {
				CMemoryWriter commandChannel;
				commandChannel.w_u16(CommandId_ExpandExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
				commandChannel.w_u64(id);
#else 
				commandChannel.w_u32(id);
#endif 
				//count of attribute  
				commandChannel.w_u32(vResult.GetNProps());

				//send varables 
				for (long i = 0; i < vResult.GetNProps(); i++) {
					const wxString& strPropName = vResult.GetPropName(i); const long lPropNum = vResult.FindProp(strPropName);
					if (lPropNum != wxNOT_FOUND) {
						try {
							if (!vResult.IsPropReadable(lPropNum))
								CBackendException::Error("Object field not readable (%s)", strPropName);
							//send attribute body
							commandChannel.w_stringZ(strPropName);
							CValue vAttribute;
							if (vResult.GetPropVal(lPropNum, vAttribute)) {
								commandChannel.w_stringZ(vAttribute.GetString());
								commandChannel.w_stringZ(vAttribute.GetClassName());
							}
							else {
								commandChannel.w_stringZ(CBackendException::GetLastError());
								commandChannel.w_stringZ(wxT("<error>"));
							}
							//count of attribute   
							commandChannel.w_u32(vAttribute.GetNProps());
						}
						catch (const CBackendException* err) {
							wxString strErrorMessage = err->what();
							strErrorMessage.Replace('\n', ' ');
							//send attribute body
							commandChannel.w_stringZ(strPropName);
							commandChannel.w_stringZ(strErrorMessage);
							commandChannel.w_stringZ(wxT("<error>"));
							//count of attribute   
							commandChannel.w_u32(0);
						}
					}
					else {
						//send attribute body
						commandChannel.w_stringZ(strPropName);
						commandChannel.w_stringZ(wxT("<not found>"));
						commandChannel.w_stringZ(wxT("<error>"));
						//count of attribute   
						commandChannel.w_u32(0);
					}
				}
				SendCommand(commandChannel.pointer(), commandChannel.size());
			}
		}
	}
	else if (commandFromClient == CommandId_RemoveExpression) {
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		ms_debugServer->m_expressions.erase(commandReader.r_u64());
#else 
		ms_debugServer->m_expressions.erase(commandReader.r_u32());
#endif 
	}
	else if (commandFromClient == CommandId_EvalToolTip) {
		wxString strFileName, strModuleName, strExpression;
		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strModuleName);
		commandReader.r_stringZ(strExpression);
		if (ms_debugServer->IsDebugLooped()) {
			CValue vResult;
			if (CProcUnit::Evaluate(strExpression, ms_debugServer->m_runContext, vResult, false)) {
				CMemoryWriter commandChannel;
				commandChannel.w_u16(CommandId_EvalToolTip);
				commandChannel.w_stringZ(strFileName);
				commandChannel.w_stringZ(strModuleName);
				commandChannel.w_stringZ(strExpression);
				commandChannel.w_stringZ(vResult.GetString());
				if (ms_debugServer->IsDebugLooped()) {
					SendCommand(commandChannel.pointer(), commandChannel.size());
				}
			}
		}
	}
	else if (commandFromClient == CommandId_SetStack) {
		unsigned int stackLevel = commandReader.r_u32();
		CRunContext* newRunContext =
			CProcUnit::GetRunContext(stackLevel);
		if (newRunContext) {
			ms_debugServer->m_runContext = newRunContext;
			//send expressions from user 
			ms_debugServer->SendExpressions();
			//send local variable
			ms_debugServer->SendLocalVariables();
		}
	}
	else if (commandFromClient == CommandId_EvalAutocomplete) {

		wxString strFileName, strModuleName, strExpression, strKeyWord;

		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strModuleName);
		commandReader.r_stringZ(strExpression);
		commandReader.r_stringZ(strKeyWord);

		s32 currPos = commandReader.r_s32();
		if (ms_debugServer->IsDebugLooped()) {
			CValue vResult;
			if (CProcUnit::Evaluate(strExpression, ms_debugServer->m_runContext, vResult, false)) {

				CMemoryWriter commandChannel;
				commandChannel.w_u16(CommandId_EvalAutocomplete);
				commandChannel.w_stringZ(strFileName);
				commandChannel.w_stringZ(strModuleName);
				commandChannel.w_stringZ(strExpression);
				commandChannel.w_stringZ(strKeyWord);
				commandChannel.w_s32(currPos);

				commandChannel.w_u32(vResult.GetNProps());
				//send varables 
				for (long i = 0; i < vResult.GetNProps(); i++) {
					const wxString& strAttributeName = vResult.GetPropName(i);
					commandChannel.w_stringZ(strAttributeName);
				}

				commandChannel.w_u32(vResult.GetNMethods());
				//send functions 
				for (long i = 0; i < vResult.GetNMethods(); i++) {
					const wxString& strMethodName = vResult.GetMethodName(i);
					const wxString& strMethodDescription = vResult.GetMethodHelper(i);
					//send attribute body
					commandChannel.w_stringZ(strMethodName);
					commandChannel.w_stringZ(strMethodDescription);
					commandChannel.w_u8(vResult.HasRetVal(i));
				}

				SendCommand(commandChannel.pointer(), commandChannel.size());
			}
		}
	}
	else if (commandFromClient == CommandId_PatchInsertLine ||
		commandFromClient == CommandId_PatchDeleteLine) {
		wxString strModuleName; commandReader.r_stringZ(strModuleName); unsigned int line = commandReader.r_u32(); int offsetLine = commandReader.r_s32();
		std::map<unsigned int, int>& moduleBreakpoints = ms_debugServer->m_listBreakpoint[strModuleName];
		for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) { if (((it->first + it->second) >= line)) it->second += offsetLine; }
		std::map<unsigned int, int>& moduleOffsets = ms_debugServer->m_offsetPoints[strModuleName];
		for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) { if (((it->first + it->second) >= line)) it->second += offsetLine; }
	}
	else if (commandFromClient == CommandId_PatchComplete) {
	}
	else if (commandFromClient == CommandId_Continue) {
		ms_debugServer->m_bDebugLoop = ms_debugServer->m_bDoLoop = false;
	}
	else if (commandFromClient == CommandId_StepInto) {
		if (ms_debugServer->IsDebugLooped()) {
			ms_debugServer->m_bDebugStopLine = true;
			ms_debugServer->m_bDebugLoop = ms_debugServer->m_bDoLoop = false;
		}
	}
	else if (commandFromClient == CommandId_StepOver) {
		if (ms_debugServer->IsDebugLooped()) {
			ms_debugServer->m_nCurrentNumberStopContext = CProcUnit::GetCountRunContext();
			ms_debugServer->m_bDebugLoop = ms_debugServer->m_bDoLoop = false;
		}
	}
	else if (commandFromClient == CommandId_Pause) {
		ms_debugServer->m_bDebugStopLine = true;
	}
	else if (commandFromClient == CommandId_Detach) {
		ms_debugServer->m_bUseDebug = ms_debugServer->m_bDebugLoop = ms_debugServer->m_bDoLoop = false;
		m_connectionType = ConnectionType::ConnectionType_Scanner;
		ms_debugServer->EnableNotify();
	}
	else if (commandFromClient == CommandId_Destroy) {
		if (appData->Disconnect()) {
#ifdef __WXMSW__
			::CoUninitialize();
#endif // !_WXMSW
			if (m_socket) {
				m_socket->Destroy();
			}
			std::exit(EXIT_SUCCESS);
		}
	}
	else if (commandFromClient == CommandId_DeleteAllBreakpoints) {
		ms_debugServer->m_listBreakpoint.clear();
	}
}

void CDebuggerServer::CDebuggerThreadServer::SendCommand(void* pointer, unsigned int length)
{
#if _USE_NET_COMPRESSOR == 1
	BYTE* dest = nullptr; unsigned int dest_sz = 0;
	_compressLZ(&dest, &dest_sz, pointer, length);
	if (m_socket && m_socket->IsOk()) {
		m_socket->WriteMsg(&dest_sz, sizeof(unsigned int));
		m_socket->WriteMsg(dest, dest_sz);
	}
	free(dest);
#else
	if (m_socket && CDebuggerThreadServer::IsConnected()) {
		m_socket->WriteMsg(&length, sizeof(unsigned int));
		m_socket->WriteMsg(pointer, length);
	}
#endif
}