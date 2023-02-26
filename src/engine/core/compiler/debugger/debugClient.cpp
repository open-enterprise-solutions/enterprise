////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "frontend/mainFrame.h"
#include "frontend/stack/stackWindow.h"
#include "frontend/watch/watchwindow.h"
#include "frontend/output/outputWindow.h"
#include "frontend/codeEditor/codeEditorCtrl.h"
#include "frontend/metatree/metaTreeWnd.h"
#include "utils/stringUtils.h"

#include "utils/fs/fs.h"
#if defined(_USE_NET_COMPRESSOR)
#include "utils/fs/lz/lzhuf.h"
#endif 

wxBEGIN_EVENT_TABLE(CDebuggerClient, wxEvtHandler)
//special debugger event 
EVT_DEBUG(CDebuggerClient::OnDebugEvent)
wxEND_EVENT_TABLE()

CDebuggerClient* CDebuggerClient::s_instance = NULL;

CDebuggerClient* CDebuggerClient::Get()
{
	if (!s_instance) {
		s_instance = new CDebuggerClient();
	}
	return s_instance;
}

void CDebuggerClient::Destroy()
{
	wxDELETE(s_instance);
}

void CDebuggerClient::Initialize()
{
	wxASSERT(s_instance);
	s_instance->LoadBreakpoints();

	for (unsigned short currentPort = defaultDebuggerPort; currentPort < defaultDebuggerPort + diapasonDebuggerPort; currentPort++) {
		CClientSocketThread* foundedConnection = s_instance->FindConnection(wxT("localhost"), currentPort);
		if (foundedConnection == NULL) {
			CClientSocketThread* sockPort =
				new CClientSocketThread(wxT("localhost"), currentPort);
			//run threading
			if (sockPort->Run() != wxTHREAD_NO_ERROR) {
				delete sockPort;
			}
		}
	}
}

void CDebuggerClient::ConnectToDebugger(const wxString& hostName, unsigned short port)
{
	CClientSocketThread* sockDebugger = FindConnection(hostName, port);
	if (sockDebugger != NULL) {
		sockDebugger->AttachConnection();
	}
}

CDebuggerClient::CClientSocketThread* CDebuggerClient::FindConnection(const wxString& hostName, unsigned short port)
{
	auto foundedConnection = std::find_if(m_aConnections.begin(), m_aConnections.end(), [hostName, port](CClientSocketThread* client) {
		return client->m_hostName == hostName && client->m_port == port;
		});

	if (foundedConnection != m_aConnections.end()) {
		return *foundedConnection;
	}

	return NULL;
}

CDebuggerClient::CClientSocketThread* CDebuggerClient::FindDebugger(const wxString& hostName, unsigned short port)
{
	auto foundedConnection = std::find_if(m_aConnections.begin(), m_aConnections.end(), [hostName, port](CClientSocketThread* client) {
		return client->m_hostName == hostName && client->m_port == port && client->GetConnectionType() == ConnectionType::ConnectionType_Debugger;
		});

	if (foundedConnection != m_aConnections.end()) {
		return *foundedConnection;
	}

	return NULL;
}

void CDebuggerClient::FindDebuggers(const wxString& hostName, unsigned short startPort)
{
	for (unsigned short currentPort = startPort; currentPort < startPort + diapasonDebuggerPort; currentPort++) {
		CClientSocketThread* sockDebugger = FindConnection(hostName, currentPort);
		if (sockDebugger && !sockDebugger->m_bAllowConnect) {
			sockDebugger->AttachConnection();
			break;
		}
	}
}

CDebuggerClient::CDebuggerClient() : wxEvtHandler(),
m_socketActive(NULL), m_bEnterLoop(false)
{
}

CDebuggerClient::~CDebuggerClient()
{
	while (m_aConnections.size()) {
		m_aConnections[m_aConnections.size() - 1]->Delete();
	}
}

//events: 
void CDebuggerClient::AddHandler(wxEvtHandler* handler)
{
	m_aHandlers.push_back(handler);
}

void CDebuggerClient::RemoveHandler(wxEvtHandler* handler)
{
	for (auto it = m_aHandlers.begin(); it != m_aHandlers.end(); ++it)
	{
		if (*it == handler) { m_aHandlers.erase(it); break; }
	}
}

//Notify event 
void CDebuggerClient::NotifyEvent(wxEvent& event)
{
	wxPostEvent(this, event);
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
	for (auto connection : m_aConnections) {
		if (connection->IsConnected()) {
			connection->DetachConnection(kill);
		}
	}
}

void CDebuggerClient::InitializeBreakpoints(const wxString& moduleName, unsigned int from, unsigned int to)
{
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[moduleName];
	moduleOffsets.clear(); for (unsigned int i = from; i < to; i++) moduleOffsets[i] = 0;
}

void CDebuggerClient::PatchBreakpoints(const wxString& moduleName, unsigned int line, int offsetLine)
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при очистке большого количества строк!")

	std::map<unsigned int, int>& moduleBreakpoints = m_breakpoints[moduleName];
	for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) {
		if (((it->first + it->second) >= line)) it->second += offsetLine;
	}

	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[moduleName];
	for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) {
		if (((it->first + it->second) >= line)) it->second += offsetLine;
	}

	CMemoryWriter commandChannel;

	commandChannel.w_u16(offsetLine > 0 ? CommandId_PatchInsertLine : CommandId_PatchDeleteLine);
	commandChannel.w_stringZ(moduleName);
	commandChannel.w_u32(line);
	commandChannel.w_s32(offsetLine);

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

bool CDebuggerClient::SaveBreakpoints(const wxString& moduleName)
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при сохрании точек, если есть удаленные с ними строки!")

	//initialize breakpoint 
	auto itBreakpoint = m_breakpoints.find(moduleName);

	if (itBreakpoint != m_breakpoints.end()) {
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
	auto itOffsetpoint = m_offsetPoints.find(moduleName);

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
	commandChannel.w_stringZ(moduleName);

	SendCommand(commandChannel.pointer(), commandChannel.size());

	return true;
}

bool CDebuggerClient::SaveAllBreakpoints()
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при сохрании точек, если есть удаленные с ними строки!")

	//initialize breakpoint 
	for (auto itBreakpoint = m_breakpoints.begin(); itBreakpoint != m_breakpoints.end(); itBreakpoint++) {
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

	for (auto itBreakpoint = m_breakpoints.begin(); itBreakpoint != m_breakpoints.end(); itBreakpoint++) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_PatchComplete);
		commandChannel.w_stringZ(itBreakpoint->first);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}

	return true;
}

bool CDebuggerClient::ToggleBreakpoint(const wxString& moduleName, unsigned int line)
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[moduleName];

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
	std::map<unsigned int, int>& moduleBreakpoints = m_breakpoints[moduleName];
	std::map<unsigned int, int>::iterator itBreakpoint = moduleBreakpoints.find(itOffset->first);
	unsigned int currLine = itOffset->first; int offset = itOffset->second;
	if (itBreakpoint == moduleBreakpoints.end()) {
		if (ToggleBreakpointInDB(moduleName, currLine)) {
			moduleBreakpoints.emplace(currLine, offset);
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_ToggleBreakpoint);
			commandChannel.w_stringZ(moduleName);
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

bool CDebuggerClient::RemoveBreakpoint(const wxString& moduleName, unsigned int line)
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	std::map<unsigned int, int>& moduleOffsets = m_offsetPoints[moduleName];
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
	std::map<unsigned int, int>& moduleBreakpoints = m_breakpoints[moduleName];
	std::map<unsigned int, int>::iterator itBreakpoint = moduleBreakpoints.find(itOffset->first);
	unsigned int currLine = itOffset->first;
	if (itBreakpoint != moduleBreakpoints.end()) {
		if (RemoveBreakpointInDB(moduleName, currLine)) {
			moduleBreakpoints.erase(itBreakpoint);
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_RemoveBreakpoint);
			commandChannel.w_stringZ(moduleName);
			commandChannel.w_u32(currLine);
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}
		else {
			return false;
		}
	}
	return true;
}

#include "core/frontend/docView/docManager.h"

void CDebuggerClient::RemoveAllBreakPoints()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_DeleteAllBreakpoints);
	SendCommand(commandChannel.pointer(), commandChannel.size());
	if (RemoveAllBreakPointsInDB()) {
		m_breakpoints.clear();
		for (auto m_document : docManager->GetDocumentsVector()) {
			m_document->UpdateAllViews();
		}
	}
	else {
		wxMessageBox("Error in : void CDebuggerClient::RemoveAllBreakPoints()");
	}
}

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
void CDebuggerClient::AddExpression(const wxString& expression, unsigned long long id)
#else 
void CDebuggerClient::AddExpression(const wxString& expression, unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_AddExpression);
	commandChannel.w_stringZ(expression);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	commandChannel.w_u64(id);
#else 
	commandChannel.w_u32(id);
#endif 

	SendCommand(commandChannel.pointer(), commandChannel.size());

	//set expression in map 
	m_expressions.insert_or_assign(id, expression);
}

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
void CDebuggerClient::ExpandExpression(const wxString& expression, unsigned long long id)
#else
void CDebuggerClient::ExpandExpression(const wxString& expression, unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_ExpandExpression);
	commandChannel.w_stringZ(expression);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	commandChannel.w_u64(id);
#else 
	commandChannel.w_u32(id);
#endif 

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
void CDebuggerClient::RemoveExpression(unsigned long long id)
#else
void CDebuggerClient::RemoveExpression(unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_RemoveExpression);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
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
	if (debugClient->IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_SetStack);
		commandChannel.w_u32(level);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

void CDebuggerClient::EvaluateToolTip(const wxString& fileName, const wxString& moduleName, const wxString& expression)
{
	if (debugClient->IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_EvalToolTip);
		commandChannel.w_stringZ(fileName);
		commandChannel.w_stringZ(moduleName);
		commandChannel.w_stringZ(expression);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

void CDebuggerClient::EvaluateAutocomplete(const wxString& fileName, const wxString& moduleName, const wxString& expression, const wxString& keyWord, int currline)
{
	if (debugClient->IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_EvalAutocomplete);
		commandChannel.w_stringZ(fileName);
		commandChannel.w_stringZ(moduleName);
		commandChannel.w_stringZ(expression);
		commandChannel.w_stringZ(keyWord);
		commandChannel.w_s32(currline);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

std::vector<unsigned int> CDebuggerClient::GetDebugList(const wxString& moduleName)
{
	std::map<unsigned int, int>& moduleBreakpoints = m_breakpoints[moduleName]; std::vector<unsigned int> aBreakpointsVector;
	for (auto breakpoint : moduleBreakpoints) {
		aBreakpointsVector.push_back(breakpoint.first + breakpoint.second);
	}
	return aBreakpointsVector;
}

void CDebuggerClient::AppendConnection(CClientSocketThread* client)
{
	m_aConnections.push_back(client);
}

void CDebuggerClient::DeleteConnection(CClientSocketThread* client)
{
	if (m_socketActive == client) {
		m_socketActive = NULL;
	}

	if (m_aConnections.size() == 0) {
		m_bEnterLoop = false;
	}

	auto foundedIt = std::find(m_aConnections.begin(), m_aConnections.end(), client);
	if (foundedIt != m_aConnections.end()) {
		m_aConnections.erase(foundedIt);
	}
}

void CDebuggerClient::RecvCommand(void* pointer, unsigned int length)
{
}

void CDebuggerClient::SendCommand(void* pointer, unsigned int length)
{
	if (m_socketActive != NULL) {
		m_socketActive->SendCommand(pointer, length);
	}
	else {
		for (auto connection : m_aConnections) {
			connection->SendCommand(pointer, length);
		}
	}
}

void CDebuggerClient::OnDebugEvent(wxDebugEvent& event)
{
	wxString fileName = event.GetFileName();
	wxString moduleName = event.GetModuleName();

	if (event.GetEventId() == EventId::EventId_EnterLoop) {
		if (!fileName.IsEmpty()) {
			wxDocument* const foundedDoc = docManager->FindDocumentByPath(fileName);
			if (foundedDoc == NULL) {
				docManager->CreateDocument(fileName, wxDOC_SILENT);
			}
		}
	}

	for (auto handler : m_aHandlers) {
		handler->AddPendingEvent(event);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

enum eSocketType {
	wxID_SOCKET_CLIENT = 1
};

bool CDebuggerClient::CClientSocketThread::AttachConnection()
{
	if (m_connectionType != ConnectionType::ConnectionType_Scanner)
		return false;

	m_connectionType = ConnectionType::ConnectionType_Debugger;

	if (m_bAllowConnect) {

		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_StartSession);
		SendCommand(commandChannel.pointer(), commandChannel.size());

		// Send the start event message to the UI.
		wxDebugEvent eventStartSession(EventId::EventId_SessionStart, m_socketClient);
		debugClient->NotifyEvent(eventStartSession);
	}

	return true;
}

bool CDebuggerClient::CClientSocketThread::DetachConnection(bool kill)
{
	if (m_connectionType != ConnectionType::ConnectionType_Debugger)
		return false;

	if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
		// Send the exit event message to the UI.
		wxDebugEvent eventEndSession(EventId::EventId_SessionEnd, m_socketClient);
		debugClient->NotifyEvent(eventEndSession);

		m_connectionType = ConnectionType::ConnectionType_Scanner;

		CMemoryWriter commandChannel;
		commandChannel.w_u16(kill ? CommandId_Destroy : CommandId_Detach);
		SendCommand(commandChannel.pointer(), commandChannel.size());

		if (m_socketClient != NULL) {
			m_socketClient->Close();
		}

		m_bAllowConnect = false;
		return true;
	}

	return false;
}

CDebuggerClient::CClientSocketThread::CClientSocketThread(const wxString& hostName, unsigned short port) : wxThread(wxTHREAD_DETACHED),
m_hostName(hostName), m_port(port), m_bAllowConnect(false), m_connectionType(ConnectionType::ConnectionType_Scanner), m_socketClient(NULL)
{
	debugClient->AppendConnection(this);
}

CDebuggerClient::CClientSocketThread::~CClientSocketThread()
{
	debugClient->DeleteConnection(this);

	if (m_socketClient) {
		m_socketClient->Destroy();
	}
}

wxThread::ExitCode CDebuggerClient::CClientSocketThread::Entry()
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

void  CDebuggerClient::CClientSocketThread::OnKill()
{
	if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
		// Send the exit event message to the UI.
		wxDebugEvent eventEndSession(EventId::EventId_SessionEnd, m_socketClient);
		debugClient->NotifyEvent(eventEndSession);
	}
	else if (m_connectionType == ConnectionType::ConnectionType_Scanner) {
		if (m_socketClient->IsConnected()) {
			m_socketClient->Close();
		}
	}
}

void CDebuggerClient::CClientSocketThread::VerifyConnection()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_VerifyCofiguration);
	SendCommand(commandChannel.pointer(), commandChannel.size());

	///////////////////////////////////////////////////////////////////////

	unsigned int length = 0;
	while (!TestDestroy() && CClientSocketThread::IsConnected() && !m_bAllowConnect) {
		if (m_socketClient && m_socketClient->WaitForRead(waitDebuggerTimeout)) {
			m_socketClient->ReadMsg(&length, sizeof(unsigned int));
			if (m_socketClient && m_socketClient->WaitForRead()) {
				wxMemoryBuffer bufferData(length);
				m_socketClient->ReadMsg(bufferData.GetData(), length);
				if (length > 0) {
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

	if (m_bAllowConnect) {
		if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_StartSession);
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}
	}
}

#define defWait 50 //msec

void CDebuggerClient::CClientSocketThread::EntryClient()
{
	// set the appropriate flags for the socket
	m_socketClient = new wxSocketClient(wxSOCKET_WAITALL | wxSOCKET_BLOCK);

	while (!TestDestroy()) {

		while (!CClientSocketThread::IsConnected()) {
			if (TestDestroy())
				break;
			wxIPV4address addr;
			addr.Hostname(m_hostName);
			addr.Service(m_port);
			m_socketClient->Connect(addr);
		}

		if (m_socketClient->IsConnected()) {
			VerifyConnection();
		}

		if (m_bAllowConnect) {
			if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
				// Send the start event message to the UI.
				wxDebugEvent eventStartSession(EventId::EventId_SessionStart, m_socketClient);
				debugClient->NotifyEvent(eventStartSession);
			}
			while (CClientSocketThread::IsConnected()) {
				if (m_socketClient && m_socketClient->WaitForRead(0, defWait)) {
					unsigned int length = 0;
					m_socketClient->ReadMsg(&length, sizeof(unsigned int));
					if (m_socketClient && m_socketClient->WaitForRead(0, defWait)) {
						wxMemoryBuffer bufferData(length);
						m_socketClient->ReadMsg(bufferData.GetData(), length);
						if (m_connectionType == ConnectionType::ConnectionType_Debugger && length > 0) {
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
			if (m_connectionType == ConnectionType::ConnectionType_Debugger) {
				// Send the exit event message to the UI.
				wxDebugEvent eventEndSession(EventId::EventId_SessionEnd, m_socketClient);
				debugClient->NotifyEvent(eventEndSession);
			}
		}

		m_connectionType = ConnectionType::ConnectionType_Scanner;

		if (m_socketClient != NULL) {
			m_socketClient->Close();
		}

		m_bAllowConnect = false;
	}
}

void CDebuggerClient::CClientSocketThread::RecvCommand(void* pointer, unsigned int length)
{
	CMemoryReader commandReader(pointer, length);

	u16 commandFromClient = commandReader.r_u16();
	if (commandFromClient == CommandId_VerifyCofiguration) {
		commandReader.r_stringZ(m_confGuid);
		commandReader.r_stringZ(m_md5Hash);
		commandReader.r_stringZ(m_userName);
		commandReader.r_stringZ(m_compName);
		m_bAllowConnect = metadata->GetMetadataGuid() == m_confGuid;
	}
	else if (commandFromClient == CommandId_GetBreakPoints) {
		//send expression 
		for (auto expression : debugClient->m_expressions) {
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_AddExpression);
			commandChannel.w_stringZ(expression.second);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
			commandChannel.w_u64(expression.first);
#else 
			commandChannel.w_u32(expression.first);
#endif 
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}

		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_SetBreakPoints);
		commandChannel.w_u32(debugClient->m_breakpoints.size());
		//send breakpoints with offsets 
		for (auto breakpoint : debugClient->m_breakpoints) {
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
	else if (commandFromClient == CommandId_EnterLoop) {
		debugClient->m_bEnterLoop = true;
		if (mainFrame->IsFocusable()) {
			mainFrame->Iconize(false); // restore the window if minimized
			mainFrame->SetFocus();     // focus on my window
			mainFrame->Raise();        // bring window to front
		}
		debugClient->m_socketActive = this;
		wxString fileName; commandReader.r_stringZ(fileName);
		wxString moduleName; commandReader.r_stringZ(moduleName);
		wxDebugEvent event(EventId::EventId_EnterLoop, m_socketClient);
		event.SetFileName(fileName);
		event.SetModuleName(moduleName);
		event.SetLine(commandReader.r_s32());
		debugClient->NotifyEvent(event);
	}
	else if (commandFromClient == CommandId_LeaveLoop) {
		debugClient->m_bEnterLoop = false;
		debugClient->m_socketActive = NULL;
		const wxString& fileName = commandReader.r_stringZ();
		const wxString& moduleName = commandReader.r_stringZ();
		wxDebugEvent event(EventId::EventId_LeaveLoop, m_socketClient);
		event.SetFileName(fileName);
		event.SetModuleName(moduleName);
		event.SetLine(commandReader.r_s32());
		debugClient->NotifyEvent(event);
	}
	else if (commandFromClient == CommandId_EvalToolTip) {
		wxString fileName, moduleName, expression, resultStr;
		commandReader.r_stringZ(fileName);
		commandReader.r_stringZ(moduleName);
		commandReader.r_stringZ(expression);
		commandReader.r_stringZ(resultStr);

		if (debugClient->IsEnterLoop()) {

			debugTipData_t debugToolTip;

			debugToolTip.m_fileName = fileName;
			debugToolTip.m_moduleName = moduleName;
			debugToolTip.m_expression = expression;

			debugClient->CallAfter(&CDebuggerClient::OnSetToolTip, debugToolTip, resultStr);
		}
	}
	else if (commandFromClient == CommandId_EvalAutocomplete) {

		wxString fileName, moduleName, expression, keyWord;
		commandReader.r_stringZ(fileName);
		commandReader.r_stringZ(moduleName);
		commandReader.r_stringZ(expression);
		commandReader.r_stringZ(keyWord);
		int currPos = commandReader.r_s32();

		debugAutoCompleteData_t debugAutocompleteData;

		debugAutocompleteData.m_fileName = fileName;
		debugAutocompleteData.m_moduleName = moduleName;
		debugAutocompleteData.m_expression = expression;
		debugAutocompleteData.m_keyword = keyWord;
		debugAutocompleteData.m_currentPos = currPos;

		unsigned int nCountA = commandReader.r_u32();
		for (unsigned int i = 0; i < nCountA; i++) {
			const wxString& attributeName = commandReader.r_stringZ();
			debugAutocompleteData.m_arrVar.push_back({ attributeName });
		}

		unsigned int nCountM = commandReader.r_u32();
		for (unsigned int i = 0; i < nCountM; i++) {
			const wxString& methodName = commandReader.r_stringZ();
			const wxString& methodDescription = commandReader.r_stringZ();
			const bool methodRet = commandReader.r_u8();
			debugAutocompleteData.m_arrMeth.push_back(
				{
					methodName,
					methodDescription,
					methodRet
				}
			);
		}

		debugClient->CallAfter(&CDebuggerClient::OnFillAutoComplete, debugAutocompleteData);
	}
	else if (commandFromClient == CommandId_SetExpressions) {
		unsigned int countExpression = commandReader.r_u32(); watchWindowData_t watchData;
		for (unsigned int i = 0; i < countExpression; i++) {
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
			const wxTreeItemId& item = reinterpret_cast<void*>(commandReader.r_u64());
#else 
			const wxTreeItemId& item = reinterpret_cast<void*>(commandReader.r_u32());
#endif 
			wxString expression, sValue, sType;
			//expressions
			commandReader.r_stringZ(expression);
			commandReader.r_stringZ(sValue);
			commandReader.r_stringZ(sType);

			//refresh child elements 
			unsigned int attributeCount = commandReader.r_u32();
			watchData.AddWatch(expression, sValue, sType, attributeCount > 0, item);
		}
		watchWindow->CallAfter(&CWatchWindow::SetVariable, watchData);
	}
	else if (commandFromClient == CommandId_ExpandExpression) {
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		watchWindowData_t watchData = reinterpret_cast<void*>(commandReader.r_u64());
#else 
		watchWindowData_t watchData = reinterpret_cast<void*>(commandReader.r_u32());
#endif 
		//generate event 
		unsigned int attributeCount = commandReader.r_u32();
		for (unsigned int i = 0; i < attributeCount; i++) {

			wxString name, value, type;
			commandReader.r_stringZ(name);
			commandReader.r_stringZ(value);
			commandReader.r_stringZ(type);
			unsigned int attributeChildCount = commandReader.r_u32();
			watchData.AddWatch(name, value, type, attributeChildCount > 0);
		}
		watchWindow->CallAfter(&CWatchWindow::SetExpanded, watchData);
	}
	else if (commandFromClient == CommandId_SetStack) {
		unsigned int count = commandReader.r_u32(); stackData_t stackData;
		for (unsigned int i = 0; i < count; i++) {
			const wxString& moduleName = commandReader.r_stringZ();
			stackData.AppendStack(
				moduleName,
				commandReader.r_u32()
			);
		}
		stackWindow->CallAfter(&CStackWindow::SetStack, stackData);
	}
	else if (commandFromClient == CommandId_SetLocalVariables) {
	}
	else if (commandFromClient == CommandId_MessageFromEnterprise) {

		wxString fileName,
			docPath, errorMessage; unsigned int currLine;

		commandReader.r_stringZ(fileName);
		commandReader.r_stringZ(docPath);
		currLine = commandReader.r_u32();
		commandReader.r_stringZ(errorMessage);

		debugData_t debugData;
		debugData.m_fileName = fileName;
		debugData.m_moduleName = docPath;
		debugData.m_line = currLine;

		debugClient->CallAfter(&CDebuggerClient::OnMessageFromEnterprise, debugData, errorMessage);
	}

	debugClient->RecvCommand(pointer, length);
}

void CDebuggerClient::CClientSocketThread::SendCommand(void* pointer, unsigned int length)
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
	if (m_socketClient && CClientSocketThread::IsConnected()) {
		m_socketClient->WriteMsg(&length, sizeof(unsigned int));
		m_socketClient->WriteMsg(pointer, length);
	}
#endif
}