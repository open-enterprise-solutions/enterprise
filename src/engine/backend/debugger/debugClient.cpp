////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "backend/metadataConfiguration.h"

#include "backend/fileSystem/fs.h"
#if _USE_NET_COMPRESSOR == 1
#include "utils/fs/lz/lzhuf.h"
#endif 

///////////////////////////////////////////////////////////////////////
CDebuggerClient* CDebuggerClient::ms_debugClient = nullptr;
///////////////////////////////////////////////////////////////////////

void CDebuggerClient::Destroy()
{
	wxDELETE(ms_debugClient);
}

bool CDebuggerClient::Initialize()
{
	if (!CDebuggerClient::TableAlreadyCreated()) {
		CDebuggerClient::CreateBreakpointDatabase();
	}

	if (ms_debugClient != nullptr) ms_debugClient->Destroy();

	ms_debugClient = new CDebuggerClient();
	ms_debugClient->LoadBreakpointCollection();

	for (unsigned short currentPort = defaultDebuggerPort; currentPort < defaultDebuggerPort + diapasonDebuggerPort; currentPort++) {
		CDebuggerThreadClient* foundedConnection = ms_debugClient->FindConnection(defaultHost, currentPort);
		if (foundedConnection == nullptr) {
			CDebuggerThreadClient* connection = new CDebuggerThreadClient(ms_debugClient, defaultHost, currentPort);
			//run threading
			if (connection->Run() != wxTHREAD_NO_ERROR) {
				wxDELETE(connection);
			}
		}
	}

	return true;
}

void CDebuggerClient::ConnectToDebugger(const wxString& hostName, unsigned short port)
{
	CDebuggerThreadClient* foundedConnection = FindConnection(hostName, port);
	if (foundedConnection != nullptr) {
		foundedConnection->AttachConnection();
	}
}

CDebuggerClient::CDebuggerThreadClient* CDebuggerClient::FindConnection(const wxString& hostName, unsigned short port)
{
	auto foundedConnection = std::find_if(m_connections.begin(), m_connections.end(), [hostName, port](CDebuggerThreadClient* client) {
		return client->m_hostName == hostName && client->m_port == port;
		}
	);

	if (foundedConnection != m_connections.end()) {
		return *foundedConnection;
	}

	return nullptr;
}

CDebuggerClient::CDebuggerThreadClient* CDebuggerClient::FindDebugger(const wxString& hostName, unsigned short port)
{
	auto foundedConnection = std::find_if(m_connections.begin(), m_connections.end(), [hostName, port](CDebuggerThreadClient* client) {
		return client->m_hostName == hostName && client->m_port == port && client->GetConnectionType() == ConnectionType::ConnectionType_Debugger;
		});

	if (foundedConnection != m_connections.end()) {
		return *foundedConnection;
	}

	return nullptr;
}

void CDebuggerClient::SearchDebugger(const wxString& hostName, unsigned short startPort)
{
	for (unsigned short currentPort = startPort; currentPort < startPort + diapasonDebuggerPort; currentPort++) {
		CDebuggerThreadClient* sockDebugger = FindConnection(hostName, currentPort);
		if (sockDebugger && !sockDebugger->m_verifiedConnection) { sockDebugger->AttachConnection(); break; }
	}
}

//special functions:
void CDebuggerClient::Continue()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_Continue);
	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerClient::StepOver()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_StepOver);
	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerClient::StepInto()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_StepInto);
	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerClient::Pause()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_Pause);
	SendCommand(commandChannel.pointer(), commandChannel.size());
}

void CDebuggerClient::Stop(bool kill)
{
	for (auto connection : m_connections) {
		if (connection->IsConnected()) {
			connection->DetachConnection(kill);
		}
	}
}

void CDebuggerClient::InitializeBreakpoints(const wxString& strModuleName, unsigned int from, unsigned int to)
{
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[strModuleName];
	moduleOffsets.clear(); for (unsigned int i = from; i < to; i++) moduleOffsets[i] = 0;
}

void CDebuggerClient::PatchBreakpoints(const wxString& strModuleName, unsigned int line, int offsetLine)
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при очистке большого количества строк!")

	std::map<unsigned int, int>& moduleBreakpoints = m_listBreakpoint[strModuleName];
	for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) {
		if (((it->first + it->second) >= line)) it->second += offsetLine;
	}

	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[strModuleName];
	for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) {
		if (((it->first + it->second) >= line)) it->second += offsetLine;
	}

	CMemoryWriter commandChannel;

	commandChannel.w_u16(offsetLine > 0 ? CommandId_PatchInsertLine : CommandId_PatchDeleteLine);
	commandChannel.w_stringZ(strModuleName);
	commandChannel.w_u32(line);
	commandChannel.w_s32(offsetLine);

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

bool CDebuggerClient::SaveBreakpoints(const wxString& strModuleName)
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при сохрании точек, если есть удаленные с ними строки!")

	//initialize breakpoint 
	auto itBreakpoint = m_listBreakpoint.find(strModuleName);

	if (itBreakpoint != m_listBreakpoint.end()) {
		std::map<unsigned int, int>& moduleBreakpoints = itBreakpoint->second, moduleBreakpointsNew;
		for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) {
			if (!OffsetBreakpointInDB(itBreakpoint->first, it->first, it->second))
				return false;
			moduleBreakpointsNew.emplace(it->first + it->second, 0);
		}
		moduleBreakpoints.clear();
		for (auto it = moduleBreakpointsNew.begin(); it != moduleBreakpointsNew.end(); it++) {
			moduleBreakpoints.emplace(it->first, it->second);
		}
	}

	//initialize offsets 
	auto itOffsetpoint = m_offsetPoints.find(strModuleName);

	if (itOffsetpoint != m_offsetPoints.end()) {
		std::map<unsigned int, int>& moduleOffsets = itOffsetpoint->second;
		std::map<unsigned int, int> ::iterator it = moduleOffsets.begin(); std::advance(it, moduleOffsets.size() - 1);
		if (it != moduleOffsets.end()) {
			InitializeBreakpoints(itOffsetpoint->first, 0, it->first + it->second + 1);
		}
		else {
			InitializeBreakpoints(itOffsetpoint->first, 0, 1);
		}
	}

	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_PatchComplete);
	commandChannel.w_stringZ(strModuleName);

	SendCommand(commandChannel.pointer(), commandChannel.size());

	return true;
}

bool CDebuggerClient::SaveAllBreakpoints()
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при сохрании точек, если есть удаленные с ними строки!")

	//initialize breakpoint 
	for (auto itBreakpoint = m_listBreakpoint.begin(); itBreakpoint != m_listBreakpoint.end(); itBreakpoint++) {
		std::map<unsigned int, int>& moduleBreakpoints = itBreakpoint->second, moduleBreakpointsNew;
		for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) {
			if (!OffsetBreakpointInDB(itBreakpoint->first, it->first, it->second))
				return false;
			moduleBreakpointsNew.emplace(it->first + it->second, 0);
		}
		moduleBreakpoints.clear();
		for (auto it = moduleBreakpointsNew.begin(); it != moduleBreakpointsNew.end(); it++) {
			moduleBreakpoints.emplace(it->first, it->second);
		}
	}

	//initialize offsets 
	for (auto itOffsetpoint = m_offsetPoints.begin(); itOffsetpoint != m_offsetPoints.end(); itOffsetpoint++) {
		std::map<unsigned int, int>& moduleOffsets = itOffsetpoint->second;
		std::map<unsigned int, int> ::iterator it = moduleOffsets.begin(); std::advance(it, moduleOffsets.size() - 1);
		if (it != moduleOffsets.end()) {
			InitializeBreakpoints(itOffsetpoint->first, 0, it->first + it->second + 1);
		}
		else {
			InitializeBreakpoints(itOffsetpoint->first, 0, 1);
		}
	}

	for (auto itBreakpoint = m_listBreakpoint.begin(); itBreakpoint != m_listBreakpoint.end(); itBreakpoint++) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_PatchComplete);
		commandChannel.w_stringZ(itBreakpoint->first);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}

	return true;
}

bool CDebuggerClient::ToggleBreakpoint(const wxString& strModuleName, unsigned int line)
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[strModuleName];

	for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) {
		if (it->second < 0 && (int)it->first < -it->second) { locOffsetPrev = it->second; continue; }
		locOffsetCurr = it->second;
		if ((it->first + locOffsetPrev) <= line && (it->first + locOffsetCurr) >= line) { startLine = it->first; break; }
		locOffsetPrev = it->second;
	}
	std::map<unsigned int, int>::iterator itOffset = moduleOffsets.find(startLine);
	if (itOffset != moduleOffsets.end()) {
		if (line != (itOffset->first + itOffset->second)) {
			wxMessageBox("Cannot set breakpoint in unsaved copy!");
			return false;
		}
	}
	else {
		wxMessageBox("Cannot set breakpoint in unsaved copy!"); return false;
	}
	std::map<unsigned int, int>& moduleBreakpoints = m_listBreakpoint[strModuleName];
	std::map<unsigned int, int>::iterator itBreakpoint = moduleBreakpoints.find(itOffset->first);
	unsigned int currLine = itOffset->first; int offset = itOffset->second;
	if (itBreakpoint == moduleBreakpoints.end()) {
		if (ToggleBreakpointInDB(strModuleName, currLine)) {
			moduleBreakpoints.emplace(currLine, offset);
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_ToggleBreakpoint);
			commandChannel.w_stringZ(strModuleName);
			commandChannel.w_u32(currLine);
			commandChannel.w_s32(offset);
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}
		else {
			return false;
		}
	}

	return true;
}

bool CDebuggerClient::RemoveBreakpoint(const wxString& strModuleName, unsigned int line)
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[strModuleName];
	for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) {
		if (it->second < 0 && (int)it->first < -it->second) {
			locOffsetPrev = it->second; continue;
		}
		locOffsetCurr = it->second;
		if ((it->first + locOffsetPrev) <= line && (it->first + locOffsetCurr) >= line) {
			startLine = it->first; break;
		}
		locOffsetPrev = it->second;
	}
	std::map<unsigned int, int>::iterator itOffset = moduleOffsets.find(startLine);
	std::map<unsigned int, int>& moduleBreakpoints = m_listBreakpoint[strModuleName];
	std::map<unsigned int, int>::iterator itBreakpoint = moduleBreakpoints.find(itOffset->first);
	unsigned int currLine = itOffset->first;
	if (itBreakpoint != moduleBreakpoints.end()) {
		if (RemoveBreakpointInDB(strModuleName, currLine)) {
			moduleBreakpoints.erase(itBreakpoint);
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_RemoveBreakpoint);
			commandChannel.w_stringZ(strModuleName);
			commandChannel.w_u32(currLine);
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}
		else {
			return false;
		}
	}
	return true;
}

#include "backend/backend_mainFrame.h"

void CDebuggerClient::RemoveAllBreakpoint()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_DeleteAllBreakpoints);
	SendCommand(commandChannel.pointer(), commandChannel.size());
	if (RemoveAllBreakpointInDB()) {
		m_listBreakpoint.clear();
		if (backend_mainFrame != nullptr) {
			backend_mainFrame->RefreshFrame();
		}
	}
	else {
		wxMessageBox("Error in : void CDebuggerClient::RemoveAllBreakpoint()");
	}
}

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
void CDebuggerClient::AddExpression(const wxString& strExpression, unsigned long long id)
#else 
void CDebuggerClient::AddExpression(const wxString& strExpression, unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_AddExpression);
	commandChannel.w_stringZ(strExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	commandChannel.w_u64(id);
#else 
	commandChannel.w_u32(id);
#endif 

	SendCommand(commandChannel.pointer(), commandChannel.size());

	//set expression in map 
	m_expressions.insert_or_assign(id, strExpression);
}

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
void CDebuggerClient::ExpandExpression(const wxString& strExpression, unsigned long long id)
#else
void CDebuggerClient::ExpandExpression(const wxString& strExpression, unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_ExpandExpression);
	commandChannel.w_stringZ(strExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	commandChannel.w_u64(id);
#else 
	commandChannel.w_u32(id);
#endif 

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
void CDebuggerClient::RemoveExpression(unsigned long long id)
#else
void CDebuggerClient::RemoveExpression(unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_RemoveExpression);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
	commandChannel.w_u64(id);
#else 
	commandChannel.w_u32(id);
#endif 

	SendCommand(commandChannel.pointer(), commandChannel.size());

	//delete expression from map
	m_expressions.erase(id);
}

void CDebuggerClient::SetLevelStack(unsigned int level)
{
	if (CDebuggerClient::IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_SetStack);
		commandChannel.w_u32(level);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

void CDebuggerClient::EvaluateToolTip(const wxString& strFileName, const wxString& strModuleName, const wxString& strExpression)
{
	if (CDebuggerClient::IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_EvalToolTip);
		commandChannel.w_stringZ(strFileName);
		commandChannel.w_stringZ(strModuleName);
		commandChannel.w_stringZ(strExpression);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

void CDebuggerClient::EvaluateAutocomplete(const wxString& strFileName, const wxString& strModuleName, const wxString& strExpression, const wxString& keyWord, int currline)
{
	if (CDebuggerClient::IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_EvalAutocomplete);
		commandChannel.w_stringZ(strFileName);
		commandChannel.w_stringZ(strModuleName);
		commandChannel.w_stringZ(strExpression);
		commandChannel.w_stringZ(keyWord);
		commandChannel.w_s32(currline);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

std::vector<unsigned int> CDebuggerClient::GetDebugList(const wxString& strModuleName)
{
	std::vector<unsigned int> listBreakpoint;
	if (m_listBreakpoint.find(strModuleName) != m_listBreakpoint.end()) {
		for (auto& breakpoint : m_listBreakpoint.at(strModuleName)) listBreakpoint.push_back(breakpoint.first + breakpoint.second);
	}
	return listBreakpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

enum eSocketType {
	wxID_SOCKET_CLIENT = 1
};

bool CDebuggerClient::CDebuggerThreadClient::AttachConnection()
{
	if (m_connectionType != ConnectionType::ConnectionType_Scanner) return false;

	if (m_verifiedConnection) {

		m_connectionType = ConnectionType::ConnectionType_Debugger;

		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_StartSession);
		SendCommand(commandChannel.pointer(), commandChannel.size());

		// Send the start event message to the UI.
		debugClient->CallAfter(&CDebuggerClient::CDebuggerAdaptourClient::OnSessionStart, m_socketClient);
	}

	return true;
}

bool CDebuggerClient::CDebuggerThreadClient::DetachConnection(bool kill)
{
	if (m_connectionType != ConnectionType::ConnectionType_Debugger) return false;

	if (m_connectionType == ConnectionType::ConnectionType_Debugger) {

		// Send the exit event message to the UI.
		debugClient->CallAfter(&CDebuggerClient::CDebuggerAdaptourClient::OnSessionEnd, m_socketClient);

		m_connectionType = ConnectionType::ConnectionType_Scanner;

		CMemoryWriter commandChannel;
		commandChannel.w_u16(kill ? CommandId_Destroy : CommandId_Detach);
		SendCommand(commandChannel.pointer(), commandChannel.size());

		if (m_socketClient != nullptr) m_socketClient->Close();
		m_verifiedConnection = false;

		return true;
	}

	return false;
}

wxThread::ExitCode CDebuggerClient::CDebuggerThreadClient::Entry()
{
	ExitCode retCode = (ExitCode)0;

	try {
		EntryClient();
	}
	catch (...) {
		retCode = (ExitCode)1;
	}

	return retCode;
}

void CDebuggerClient::CDebuggerThreadClient::OnKill()
{
	if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
		// Send the exit event message to the UI.
		debugClient->CallAfter(&CDebuggerClient::CDebuggerAdaptourClient::OnSessionEnd, m_socketClient);
	}

	if (m_socketClient != nullptr && m_socketClient->IsConnected()) m_socketClient->Close();
}

void CDebuggerClient::CDebuggerThreadClient::VerifyConnection()
{
	///////////////////////////////////////////////////////////////////////
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_VerifyConnection);
	SendCommand(commandChannel.pointer(), commandChannel.size());
	///////////////////////////////////////////////////////////////////////

	unsigned int length = 0;
	while (!TestDestroy() && CDebuggerThreadClient::IsConnected() && !m_verifiedConnection) {
		if (m_socketClient && m_socketClient->WaitForRead(waitDebuggerTimeout)) {
			m_socketClient->ReadMsg(&length, sizeof(unsigned int));
			if (m_socketClient && m_socketClient->WaitForRead()) {
				wxMemoryBuffer bufferData(length);
				m_socketClient->ReadMsg(bufferData.GetData(), length);
				if (length > 0) {
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

	if (m_verifiedConnection && m_connectionType == ConnectionType::ConnectionType_Debugger) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_StartSession);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

#define defWait 50 //msec

void CDebuggerClient::CDebuggerThreadClient::EntryClient()
{
	if (m_socketClient != nullptr) m_socketClient->Close();
	// set the appropriate flags for the socket
	m_socketClient = new wxSocketClient(wxSOCKET_WAITALL | wxSOCKET_BLOCK);

	while (!TestDestroy()) {

		while (!CDebuggerThreadClient::IsConnected()) {
			if (TestDestroy()) break;
			wxIPV4address addr;
			addr.Hostname(m_hostName);
			addr.Service(m_port);
			m_socketClient->Connect(addr);
		}

		if ((m_connectionType == ConnectionType::ConnectionType_Scanner || m_connectionType == ConnectionType::ConnectionType_Waiter) && m_socketClient->IsConnected()) VerifyConnection();

		if (m_verifiedConnection) {

			if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
				// Send the start event message to the UI.
				debugClient->CallAfter(&CDebuggerClient::CDebuggerAdaptourClient::OnSessionStart, m_socketClient);
			}

			while (CDebuggerThreadClient::IsConnected()) {
				if (m_socketClient && m_socketClient->WaitForRead(0, defWait)) {
					unsigned int length = 0;
					m_socketClient->ReadMsg(&length, sizeof(unsigned int));
					if (m_socketClient && m_socketClient->WaitForRead(0, defWait)) {
						wxMemoryBuffer bufferData(length);
						m_socketClient->ReadMsg(bufferData.GetData(), length);
						if (m_connectionType == ConnectionType::ConnectionType_Debugger && length > 0) {
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
				if (TestDestroy()) break;
			}

			if (debugClient != nullptr && m_connectionType == ConnectionType::ConnectionType_Debugger) {
				// Send the exit event message to the UI.
				debugClient->CallAfter(&CDebuggerClient::CDebuggerAdaptourClient::OnSessionEnd, m_socketClient);
			}
		}

		m_connectionType = ConnectionType::ConnectionType_Scanner;
		if (m_socketClient != nullptr) m_socketClient->Close();
		m_verifiedConnection = false;
	}
}

void CDebuggerClient::CDebuggerThreadClient::RecvCommand(void* pointer, unsigned int length)
{
	CMemoryReader commandReader(pointer, length);
	wxASSERT(debugClient != nullptr);
	u16 commandFromServer = commandReader.r_u16();

	if (commandFromServer == CommandId_VerifyConnection) {
		
		commandReader.r_stringZ(m_confGuid);
		commandReader.r_stringZ(m_md5Hash);
		commandReader.r_stringZ(m_userName);
		commandReader.r_stringZ(m_compName);
	
		m_verifiedConnection = commonMetaData->GetConfigGuid() == m_confGuid;

		if (m_verifiedConnection && m_connectionType == ConnectionType::ConnectionType_Waiter)
			m_connectionType = ConnectionType::ConnectionType_Debugger;
		else if (m_verifiedConnection && m_connectionType == ConnectionType::ConnectionType_Scanner)
			m_connectionType = ConnectionType::ConnectionType_Scanner;
		else
			m_connectionType = ConnectionType::ConnectionType_Unknown;
		
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_SetConnectionType);
		commandChannel.w_u32(m_connectionType);
		
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
	else if (commandFromServer == CommandId_SetConnectionType) {
		m_connectionType = static_cast<ConnectionType>(commandReader.r_u16());
	}
	else if (commandFromServer == CommandId_GetArrayBreakpoint) {
		//send expression 
		for (auto& expression : debugClient->m_expressions) {
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_AddExpression);
			commandChannel.w_stringZ(expression.second);
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			commandChannel.w_u64(expression.first);
#else 
			commandChannel.w_u32(expression.first);
#endif 
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}

		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_SetArrayBreakpoint);
		commandChannel.w_u32(debugClient->m_listBreakpoint.size());
		//send breakpoints with offsets 
		for (auto breakpoint : debugClient->m_listBreakpoint) {
			commandChannel.w_u32(breakpoint.second.size());
			commandChannel.w_stringZ(breakpoint.first);
			for (auto line : breakpoint.second) {
				commandChannel.w_u32(line.first);
				commandChannel.w_s32(line.second);
			}
		}
		commandChannel.w_u32(debugClient->m_offsetPoints.size());
		//send line offsets 
		for (auto offset : debugClient->m_offsetPoints) {
			commandChannel.w_u32(offset.second.size());
			commandChannel.w_stringZ(offset.first);
			for (auto line : offset.second) {
				commandChannel.w_u32(line.first);
				commandChannel.w_s32(line.second);
			}
		}
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
	else if (commandFromServer == CommandId_EnterLoop) {
		debugClient->m_enterLoop = true;
		debugClient->m_activeSocket = this;
		wxString strFileName; commandReader.r_stringZ(strFileName);
		wxString strModuleName; commandReader.r_stringZ(strModuleName);
		debugLineData_t data;
		data.m_fileName = strFileName;
		data.m_moduleName = strModuleName;
		data.m_line = commandReader.r_s32();
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnEnterLoop, m_socketClient, data
		);
	}
	else if (commandFromServer == CommandId_LeaveLoop) {
		debugClient->m_enterLoop = false;
		debugClient->m_activeSocket = nullptr;
		const wxString& strFileName = commandReader.r_stringZ();
		const wxString& strModuleName = commandReader.r_stringZ();
		debugLineData_t data;
		data.m_fileName = strFileName;
		data.m_moduleName = strModuleName;
		data.m_line = commandReader.r_s32();
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnLeaveLoop, m_socketClient, data
		);
	}
	else if (commandFromServer == CommandId_EvalToolTip) {

		wxString strFileName, strModuleName, strExpression, resultStr;
		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strModuleName);
		commandReader.r_stringZ(strExpression);
		commandReader.r_stringZ(resultStr);

		if (debugClient->IsEnterLoop()) {
			debugExpressionData_t data;
			data.m_fileName = strFileName;
			data.m_moduleName = strModuleName;
			data.m_expression = strExpression;
			debugClient->CallAfter(
				&CDebuggerClient::CDebuggerAdaptourClient::OnSetToolTip, data, resultStr
			);
		}
	}
	else if (commandFromServer == CommandId_EvalAutocomplete) {

		wxString strFileName, strModuleName, strExpression, strKeyWord;
		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strModuleName);
		commandReader.r_stringZ(strExpression);
		commandReader.r_stringZ(strKeyWord);
		int currPos = commandReader.r_s32();

		debugAutoCompleteData_t debugAutocompleteData;

		debugAutocompleteData.m_fileName = strFileName;
		debugAutocompleteData.m_moduleName = strModuleName;
		debugAutocompleteData.m_expression = strExpression;
		debugAutocompleteData.m_keyword = strKeyWord;
		debugAutocompleteData.m_currentPos = currPos;

		unsigned int nCountA = commandReader.r_u32();
		for (unsigned int i = 0; i < nCountA; i++) {
			const wxString& strAttributeName = commandReader.r_stringZ();
			debugAutocompleteData.m_arrVar.push_back({ strAttributeName });
		}

		unsigned int nCountM = commandReader.r_u32();
		for (unsigned int i = 0; i < nCountM; i++) {
			const wxString& strMethodName = commandReader.r_stringZ();
			const wxString& strMethodDescription = commandReader.r_stringZ();
			const bool methodRet = commandReader.r_u8();
			debugAutocompleteData.m_arrMeth.push_back({
				strMethodName,
				strMethodDescription,
				methodRet
				}
			);
		}
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnAutoComplete, debugAutocompleteData
		);
	}
	else if (commandFromServer == CommandId_SetExpressions) {
		unsigned int countExpression = commandReader.r_u32(); watchWindowData_t watchData;
		for (unsigned int i = 0; i < countExpression; i++) {
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
			const wxTreeItemId& item = reinterpret_cast<void*>(commandReader.r_u64());
#else 
			const wxTreeItemId& item = reinterpret_cast<void*>(commandReader.r_u32());
#endif 
			wxString strExpression, strValue, strType;

			//expressions
			commandReader.r_stringZ(strExpression);
			commandReader.r_stringZ(strValue);
			commandReader.r_stringZ(strType);

			//set space 
			strValue.Replace('\n', ' ');

			//refresh child elements 
			unsigned int attributeCount = commandReader.r_u32();
			watchData.AddWatch(strExpression, strValue, strType, attributeCount > 0, item);
		}
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnSetVariable, watchData
		);
	}
	else if (commandFromServer == CommandId_ExpandExpression) {
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
		watchWindowData_t watchData = reinterpret_cast<void*>(commandReader.r_u64());
#else 
		watchWindowData_t watchData = reinterpret_cast<void*>(commandReader.r_u32());
#endif 
		//generate event 
		unsigned int attributeCount = commandReader.r_u32();
		for (unsigned int i = 0; i < attributeCount; i++) {
			wxString strName, strValue, strType;
			commandReader.r_stringZ(strName);
			commandReader.r_stringZ(strValue);
			commandReader.r_stringZ(strType);
			unsigned int attributeChildCount = commandReader.r_u32();
			watchData.AddWatch(strName, strValue, strType, attributeChildCount > 0);
		}
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnSetExpanded, watchData
		);
	}
	else if (commandFromServer == CommandId_SetStack) {
		unsigned int count = commandReader.r_u32(); stackData_t stackData;
		for (unsigned int i = 0; i < count; i++) {
			const wxString& strModuleName = commandReader.r_stringZ();
			stackData.AppendStack(
				strModuleName,
				commandReader.r_u32()
			);
		}
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnSetStack, stackData
		);
	}
	else if (commandFromServer == CommandId_SetLocalVariables) {
		localWindowData_t locData;
		//generate event 
		unsigned int attributeCount = commandReader.r_u32();
		for (unsigned int i = 0; i < attributeCount; i++) {
			bool tempVar = commandReader.r_u8();
			wxString strName, strValue, strType;
			commandReader.r_stringZ(strName);
			commandReader.r_stringZ(strValue);
			commandReader.r_stringZ(strType);
			unsigned int attributeChildCount = commandReader.r_u32();
			if (!tempVar)
				locData.AddLocalVar(strName, strValue, strType, attributeChildCount > 0);
		}
		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnSetLocalVariable, locData
		);
	}
	else if (commandFromServer == CommandId_MessageFromServer) {

		wxString strFileName,
			strDocPath, strErrorMessage; unsigned int currLine;

		commandReader.r_stringZ(strFileName);
		commandReader.r_stringZ(strDocPath);
		currLine = commandReader.r_u32();
		commandReader.r_stringZ(strErrorMessage);

		debugLineData_t debugData;
		debugData.m_fileName = strFileName;
		debugData.m_moduleName = strDocPath;
		debugData.m_line = currLine;

		debugClient->CallAfter(
			&CDebuggerClient::CDebuggerAdaptourClient::OnMessageFromServer, debugData, strErrorMessage
		);
	}

	debugClient->RecvCommand(pointer, length);
}

void CDebuggerClient::CDebuggerThreadClient::SendCommand(void* pointer, unsigned int length)
{
#if _USE_NET_COMPRESSOR == 1
	BYTE* dest = nullptr; unsigned int dest_sz = 0;
	_compressLZ(&dest, &dest_sz, pointer, length);
	if (m_socketClient && m_socketClient->IsOk()) {
		m_socketClient->WriteMsg(&dest_sz, sizeof(unsigned int));
		m_socketClient->WriteMsg(dest, dest_sz);
	}
	free(dest);
#else
	if (m_socketClient && CDebuggerThreadClient::IsConnected()) {
		m_socketClient->WriteMsg(&length, sizeof(unsigned int));
		m_socketClient->WriteMsg(pointer, length);
	}
#endif
}