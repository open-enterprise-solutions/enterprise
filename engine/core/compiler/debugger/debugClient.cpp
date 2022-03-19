////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "debugEvent.h"
#include "utils/fs/fs.h"
#if defined(_USE_NET_COMPRESSOR)
#include "utils/fs/lz/lzhuf.h"
#endif 
#include "frontend/mainFrame.h"
#include "frontend/stack/stackWindow.h"
#include "frontend/watch/watchwindow.h"
#include "frontend/output/outputWindow.h"
#include "frontend/codeEditor/codeEditorCtrl.h"
#include "frontend/metatree/metatreeWnd.h"
#include "utils/stringUtils.h"

wxBEGIN_EVENT_TABLE(CDebuggerClient, wxEvtHandler)
//special debugger event 
EVT_DEBUG(CDebuggerClient::OnDebugEvent)
EVT_DEBUG_TOOLTIP_EVENT(CDebuggerClient::OnDebugToolTipEvent)
EVT_DEBUG_AUTOCOMPLETE_EVENT(CDebuggerClient::OnDebugAutoCompleteEvent)
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
		CClientSocketThread *foundedConnection = s_instance->FindConnection(wxT("localhost"), currentPort);
		if (foundedConnection == NULL) {
			CClientSocketThread *sockPort =
				new CClientSocketThread(wxT("localhost"), currentPort);
			//run threading
			if (sockPort->Run() != wxTHREAD_NO_ERROR) {
				delete sockPort;
			}
		}
	}
}

void CDebuggerClient::ConnectToDebugger(const wxString &hostName, unsigned short port)
{
	CClientSocketThread *sockDebugger = FindConnection(hostName, port);
	if (sockDebugger) {
		sockDebugger->AttachConnection();
	}
}

CDebuggerClient::CClientSocketThread *CDebuggerClient::FindConnection(const wxString &hostName, unsigned short port)
{
	auto foundedConnection = std::find_if(m_aConnections.begin(), m_aConnections.end(), [hostName, port](CClientSocketThread *client) {
		return client->m_hostName == hostName && client->m_port == port;
	});

	if (foundedConnection != m_aConnections.end()) {
		return *foundedConnection;
	}

	return NULL;
}

CDebuggerClient::CClientSocketThread *CDebuggerClient::FindDebugger(const wxString &hostName, unsigned short port)
{
	auto foundedConnection = std::find_if(m_aConnections.begin(), m_aConnections.end(), [hostName, port](CClientSocketThread *client) {
		return client->m_hostName == hostName && client->m_port == port && client->GetConnectionType() == ConnectionType::ConnectionType_Debugger;
	});

	if (foundedConnection != m_aConnections.end()) {
		return *foundedConnection;
	}

	return NULL;
}

void CDebuggerClient::FindDebuggers(const wxString &hostName, unsigned short startPort)
{
	for (unsigned short currentPort = startPort; currentPort < startPort + diapasonDebuggerPort; currentPort++) {
		CClientSocketThread *sockDebugger = FindConnection(hostName, currentPort);
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

void CDebuggerClient::InitializeBreakpoints(const wxString &sModuleName, unsigned int from, unsigned int to)
{
	std::map<unsigned int, int> &moduleOffsets = m_aOffsetPoints[sModuleName];
	moduleOffsets.clear(); for (unsigned int i = from; i < to; i++) moduleOffsets[i] = 0;
}

void CDebuggerClient::PatchBreakpoints(const wxString &sModuleName, unsigned int line, int offsetLine)
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при очистке большого количества строк!")

	std::map<unsigned int, int> &moduleBreakpoints = m_aBreakpoints[sModuleName];
	for (auto it = moduleBreakpoints.begin(); it != moduleBreakpoints.end(); it++) {
		if (((it->first + it->second) >= line)) it->second += offsetLine;
	}

	std::map<unsigned int, int> &moduleOffsets = m_aOffsetPoints[sModuleName];
	for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) {
		if (((it->first + it->second) >= line)) it->second += offsetLine;
	}

	CMemoryWriter commandChannel;

	commandChannel.w_u16(offsetLine > 0 ? CommandId_PatchInsertLine : CommandId_PatchDeleteLine);
	commandChannel.w_stringZ(sModuleName);
	commandChannel.w_u32(line);
	commandChannel.w_s32(offsetLine);

	SendCommand(commandChannel.pointer(), commandChannel.size());
}

bool CDebuggerClient::SaveBreakpoints(const wxString &sModuleName)
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при сохрании точек, если есть удаленные с ними строки!")

	//initialize breakpoint 
	auto itBreakpoint = m_aBreakpoints.find(sModuleName);

	if (itBreakpoint != m_aBreakpoints.end()) {
		std::map<unsigned int, int> &moduleBreakpoints = itBreakpoint->second, moduleBreakpointsNew;
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
	auto itOffsetpoint = m_aOffsetPoints.find(sModuleName);

	if (itOffsetpoint != m_aOffsetPoints.end()) {
		std::map<unsigned int, int> &moduleOffsets = itOffsetpoint->second;
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
	commandChannel.w_stringZ(sModuleName);

	SendCommand(commandChannel.pointer(), commandChannel.size());

	return true;
}

bool CDebuggerClient::SaveAllBreakpoints()
{
#pragma message("nouverbe to nouverbe: необходимо исправить проблему при сохрании точек, если есть удаленные с ними строки!")

	//initialize breakpoint 
	for (auto itBreakpoint = m_aBreakpoints.begin(); itBreakpoint != m_aBreakpoints.end(); itBreakpoint++) {
		std::map<unsigned int, int> &moduleBreakpoints = itBreakpoint->second, moduleBreakpointsNew;
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
	for (auto itOffsetpoint = m_aOffsetPoints.begin(); itOffsetpoint != m_aOffsetPoints.end(); itOffsetpoint++) {
		std::map<unsigned int, int> &moduleOffsets = itOffsetpoint->second;
		std::map<unsigned int, int> ::iterator it = moduleOffsets.begin(); std::advance(it, moduleOffsets.size() - 1);
		if (it != moduleOffsets.end()) {
			InitializeBreakpoints(itOffsetpoint->first, 0, it->first + it->second + 1);
		}
		else {
			InitializeBreakpoints(itOffsetpoint->first, 0, 1);
		}
	}

	for (auto itBreakpoint = m_aBreakpoints.begin(); itBreakpoint != m_aBreakpoints.end(); itBreakpoint++) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_PatchComplete);
		commandChannel.w_stringZ(itBreakpoint->first);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}

	return true;
}

bool CDebuggerClient::ToggleBreakpoint(const wxString &sModuleName, unsigned int line)
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	std::map<unsigned int, int> &moduleOffsets = m_aOffsetPoints[sModuleName];

	for (auto it = moduleOffsets.begin(); it != moduleOffsets.end(); it++) {
		if (it->second < 0 && (int)it->first < -it->second) { locOffsetPrev = it->second; continue; }
		locOffsetCurr = it->second;
		if ((it->first + locOffsetPrev) <= line && (it->first + locOffsetCurr) >= line) { startLine = it->first; break; }
		locOffsetPrev = it->second;
	}
	std::map<unsigned int, int>::iterator itOffset = moduleOffsets.find(startLine);
	if (itOffset != moduleOffsets.end()) {
		if (line != (itOffset->first + itOffset->second)) { wxMessageBox("Cannot set breakpoint in unsaved copy!"); return false; }
	}
	else {
		wxMessageBox("Cannot set breakpoint in unsaved copy!"); return false;
	}
	std::map<unsigned int, int> &moduleBreakpoints = m_aBreakpoints[sModuleName];
	std::map<unsigned int, int>::iterator itBreakpoint = moduleBreakpoints.find(itOffset->first);
	unsigned int currLine = itOffset->first; int offset = itOffset->second;
	if (itBreakpoint == moduleBreakpoints.end()) {
		if (ToggleBreakpointInDB(sModuleName, currLine)) {
			moduleBreakpoints.emplace(currLine, offset);
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_ToggleBreakpoint);
			commandChannel.w_stringZ(sModuleName);
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

bool CDebuggerClient::RemoveBreakpoint(const wxString &sModuleName, unsigned int line)
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	std::map<unsigned int, int> &moduleOffsets = m_aOffsetPoints[sModuleName];
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
	std::map<unsigned int, int> &moduleBreakpoints = m_aBreakpoints[sModuleName];
	std::map<unsigned int, int>::iterator itBreakpoint = moduleBreakpoints.find(itOffset->first);
	unsigned int currLine = itOffset->first;
	if (itBreakpoint != moduleBreakpoints.end()) {
		if (RemoveBreakpointInDB(sModuleName, currLine)) {
			moduleBreakpoints.erase(itBreakpoint);
			CMemoryWriter commandChannel;
			commandChannel.w_u16(CommandId_RemoveBreakpoint);
			commandChannel.w_stringZ(sModuleName);
			commandChannel.w_u32(currLine);
			SendCommand(commandChannel.pointer(), commandChannel.size());
		}
		else {
			return false;
		}
	}
	return true;
}

#include "common/reportManager.h"

void CDebuggerClient::RemoveAllBreakPoints()
{
	CMemoryWriter commandChannel;
	commandChannel.w_u16(CommandId_DeleteAllBreakpoints);
	SendCommand(commandChannel.pointer(), commandChannel.size());
	if (RemoveAllBreakPointsInDB()) {
		m_aBreakpoints.clear();
		for (auto m_document : reportManager->GetDocumentsVector()) {
			m_document->UpdateAllViews();
		}
	}
	else {
		wxMessageBox("Error in : void CDebuggerClient::RemoveAllBreakPoints()");
	}
}

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
void CDebuggerClient::AddExpression(const wxString &sExpression, unsigned long long id)
#else 
void CDebuggerClient::AddExpression(const wxString &sExpression, unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_AddExpression);
	commandChannel.w_stringZ(sExpression);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
	commandChannel.w_u64(id);
#else 
	commandChannel.w_u32(id);
#endif 

	SendCommand(commandChannel.pointer(), commandChannel.size());

	//set expression in map 
	m_aExpressions.insert_or_assign(id, sExpression);
}

#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
void CDebuggerClient::ExpandExpression(const wxString &sExpression, unsigned long long id)
#else
void CDebuggerClient::ExpandExpression(const wxString &sExpression, unsigned int id)
#endif 
{
	CMemoryWriter commandChannel;

	commandChannel.w_u16(CommandId_ExpandExpression);
	commandChannel.w_stringZ(sExpression);
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
	m_aExpressions.erase(id);
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

void CDebuggerClient::EvaluateToolTip(const wxString &sModuleName, const wxString &sExpression)
{
	if (debugClient->IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_EvalToolTip);
		commandChannel.w_stringZ(sModuleName);
		commandChannel.w_stringZ(sExpression);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

void CDebuggerClient::EvaluateAutocomplete(void *pointer, const wxString &sExpression, const wxString &sKeyWord, int currline)
{
	if (debugClient->IsEnterLoop()) {
		CMemoryWriter commandChannel;
		commandChannel.w_u16(CommandId_EvalAutocomplete);
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		commandChannel.w_u64(reinterpret_cast<u64>(pointer));
#else 
		commandChannel.w_u32(reinterpret_cast<u32>(pointer));
#endif
		commandChannel.w_stringZ(sExpression);
		commandChannel.w_stringZ(sKeyWord);
		commandChannel.w_s32(currline);
		SendCommand(commandChannel.pointer(), commandChannel.size());
	}
}

std::vector<unsigned int> CDebuggerClient::GetDebugList(const wxString &sModuleName)
{
	std::map<unsigned int, int> &moduleBreakpoints = m_aBreakpoints[sModuleName]; std::vector<unsigned int> aBreakpointsVector;
	for (auto breakpoint : moduleBreakpoints) {
		aBreakpointsVector.push_back(breakpoint.first + breakpoint.second);
	}
	return aBreakpointsVector;
}

void CDebuggerClient::AppendConnection(CClientSocketThread *client)
{
	m_aConnections.push_back(client);
}

void CDebuggerClient::DeleteConnection(CClientSocketThread *client)
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

void CDebuggerClient::RecvCommand(void *pointer, unsigned int length)
{
}

void CDebuggerClient::SendCommand(void *pointer, unsigned int length)
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

void CDebuggerClient::OnDebugEvent(wxDebugEvent &event)
{
	wxString fileName = event.GetFileName();
	wxString moduleName = event.GetModuleName();

	if (event.GetEventId() == EventId::EventId_EnterLoop) {
		if (!fileName.IsEmpty()) {
			wxDocument * const foundedDoc = reportManager->FindDocumentByPath(fileName);
			if (foundedDoc == NULL) {
				reportManager->CreateDocument(fileName, wxDOC_SILENT);
			}
		}
	}
	else if (event.GetEventId() == EventId::EventId_MessageFromEnterprise) {
		if (fileName.IsEmpty()) {
			IMetadataTree *metaTree = metadata->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(moduleName, event.GetLine(), false);
		}
		if (!fileName.IsEmpty()) {
			IMetaDocument *foundedDoc = dynamic_cast<IMetaDocument *>(
				reportManager->FindDocumentByPath(fileName)
				);
			if (!foundedDoc) {
				foundedDoc = dynamic_cast<IMetaDocument *>(
					reportManager->CreateDocument(fileName, wxDOC_SILENT)
					);
			}
			if (foundedDoc) {
				IMetadata *metaData = foundedDoc->GetMetadata();
				wxASSERT(metaData);
				IMetadataTree *metaTree = metaData->GetMetaTree();
				wxASSERT(metaTree);
				metaTree->EditModule(moduleName, event.GetLine(), false);
			}
		}
		outputWindow->OutputError(event.GetMessage(),
			fileName, moduleName, event.GetLine());
	}

	for (auto handler : m_aHandlers) {
		handler->AddPendingEvent(event);
	}
}

void CDebuggerClient::OnDebugToolTipEvent(wxDebugToolTipEvent &event)
{
	for (auto handler : m_aHandlers) {
		handler->AddPendingEvent(event);
	}
}

void CDebuggerClient::OnDebugAutoCompleteEvent(wxDebugAutocompleteEvent &event)
{
	for (auto handler : m_aHandlers) {
		handler->AddPendingEvent(event);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

enum eSocketTypes
{
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

		if (m_socketClient) {
			m_socketClient->Close();
		}

		m_bAllowConnect = false;
		return true;
	}

	return false;
}

CDebuggerClient::CClientSocketThread::CClientSocketThread(const wxString &hostName, unsigned short port) : wxThread(wxTHREAD_DETACHED),
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

	try
	{
		EntryClient();
	}
	catch (...)
	{
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

		if (m_socketClient) {
			m_socketClient->Close();
		}

		m_bAllowConnect = false;
	}
}

void CDebuggerClient::CClientSocketThread::RecvCommand(void *pointer, unsigned int length)
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
		for (auto expression : debugClient->m_aExpressions) {
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
		commandChannel.w_u32(debugClient->m_aBreakpoints.size());
		//send breakpoints with offsets 
		for (auto breakpoint : debugClient->m_aBreakpoints) {
			commandChannel.w_u32(breakpoint.second.size());
			commandChannel.w_stringZ(breakpoint.first);
			for (auto line : breakpoint.second) {
				commandChannel.w_u32(line.first);
				commandChannel.w_s32(line.second);
			}
		}
		commandChannel.w_u32(debugClient->m_aOffsetPoints.size());
		//send line offsets 
		for (auto offset : debugClient->m_aOffsetPoints) {
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
		wxString sFileName; commandReader.r_stringZ(sFileName);
		wxString sModuleName; commandReader.r_stringZ(sModuleName);
		wxDebugEvent event(EventId::EventId_EnterLoop, m_socketClient);
		event.SetFileName(sFileName);
		event.SetModuleName(sModuleName);
		event.SetLine(commandReader.r_s32());
		debugClient->NotifyEvent(event);
	}
	else if (commandFromClient == CommandId_LeaveLoop) {
		debugClient->m_bEnterLoop = false;
		debugClient->m_socketActive = NULL;
		wxString sFileName; commandReader.r_stringZ(sFileName);
		wxString sModuleName; commandReader.r_stringZ(sModuleName);
		wxDebugEvent event(EventId::EventId_LeaveLoop, m_socketClient);
		event.SetFileName(sFileName);
		event.SetModuleName(sModuleName);
		event.SetLine(commandReader.r_s32());
		debugClient->NotifyEvent(event);
	}
	else if (commandFromClient == CommandId_EvalToolTip) {
		wxString sModuleName, sExpression, sResult;
		commandReader.r_stringZ(sModuleName);
		commandReader.r_stringZ(sExpression);
		commandReader.r_stringZ(sResult);
		if (debugClient->IsEnterLoop()) {
			wxDebugToolTipEvent event(EventId::EventId_SetToolTip, m_socketClient);
			event.SetModuleName(sModuleName);
			event.SetExpression(sExpression);
			event.SetResult(sResult);
			debugClient->NotifyEvent(event);
		}
	}
	else if (commandFromClient == CommandId_EvalAutocomplete) {
#if defined(_USE_64_BIT_POINT_IN_DEBUGGER)
		CCodeEditorCtrl *autocompleteCtrl = reinterpret_cast<CCodeEditorCtrl *>(commandReader.r_u64());
#else 
		CCodeEditorCtrl *autocompleteCtrl = reinterpret_cast<CCodeEditorCtrl *>(commandReader.r_u32());
#endif 
		if (autocompleteCtrl) autocompleteCtrl->ShowAutoCompleteFromDebugger(commandReader);
	}
	else if (commandFromClient == CommandId_SetExpressions) {
		watchWindow->SetVariable(commandReader);
	}
	else if (commandFromClient == CommandId_ExpandExpression) {
		watchWindow->SetExpanded(commandReader);
	}
	else if (commandFromClient == CommandId_SetStack) {
		stackWindow->SetStack(commandReader);
	}
	else if (commandFromClient == CommandId_SetLocalVariables) {
	}
	else if (commandFromClient == CommandId_MessageFromEnterprise) {
		if (mainFrame->IsFocusable()) {
			mainFrame->Iconize(false); // restore the window if minimized
			mainFrame->SetFocus();     // focus on my window
			mainFrame->Raise();        // bring window to front
		}

		wxString fileName,
			docPath, errorMessage; unsigned int currLine;

		commandReader.r_stringZ(fileName);
		commandReader.r_stringZ(docPath);
		currLine = commandReader.r_u32();
		commandReader.r_stringZ(errorMessage);

		wxDebugEvent event(EventId::EventId_MessageFromEnterprise, m_socketClient);
		event.SetFileName(fileName);
		event.SetModuleName(docPath);
		event.SetMessage(errorMessage);
		event.SetLine(currLine);
		debugClient->NotifyEvent(event);
	}

	debugClient->RecvCommand(pointer, length);
	}

void CDebuggerClient::CClientSocketThread::SendCommand(void *pointer, unsigned int length)
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
	if (m_socketClient && m_socketClient->IsOk()) {
		m_socketClient->WriteMsg(&length, sizeof(unsigned int));
		m_socketClient->WriteMsg(pointer, length);
}
#endif
	}