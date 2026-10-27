////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"

#include <cstdint>

#include <wx/app.h>   // wxTheApp — the window's loop, where the news is received

#include "core/diagnostics/journal.h"

#include "protocol/protocolNode.h"

#include "designer-thin/docManager/templates/docViewModuleEditor.h"
#include "designer-thin/mainFrame/mainFrameDesigner.h"
#include "frmclient/mainFrame/output/outputWindow.h"

///////////////////////////////////////////////////////////////////////
ibDebuggerClient* ibDebuggerClient::ms_debugClient = nullptr;
///////////////////////////////////////////////////////////////////////

namespace {

// The offset in either map's entry: the offset map holds it bare, the breakpoint map beside a condition.
static int& OffsetOf(int& entry) { return entry; }
static int& OffsetOf(ibDebuggerClient::ibBreakpoint& entry) { return entry.m_offset; }

// Module maps (m_listBreakpoint / m_listOffsetBreakpoint) are keyed by module GUID, matched
// case-insensitively via stringUtils::IsSameAs — so map::find can't be used. This is the
// one lookup the methods here kept inlining. Returns the inner (line -> entry) map, or null.
template <typename Entry>
static std::map<unsigned int, Entry>* FindModuleLines(
	std::map<wxString, std::map<unsigned int, Entry>>& modules, const wxString& guid)
{
	auto it = std::find_if(modules.begin(), modules.end(),
		[&guid](const auto& pair) { return stringUtils::IsSameAs(pair.first, guid); });
	return it != modules.end() ? &it->second : nullptr;
}

// Shift one module's (committedLine -> entry) map by `line_offset` lines for an edit at editor
// `line`. Each entry's CURRENT editor line is its committed line + its offset. A breakpoint's
// condition rides in the entry, so it moves with the line and needs nothing of its own.
//   atLineStart        — the edit was at column 0 of `line` (the whole line moved), so an entry
//                        sitting ON `line` shifts too; an edit later in the line leaves it in place.
//   collapseCollisions — on insert, when a following entry now lands on the same editor line as the
//                        one just shifted: true ERASES it (two breakpoints on one line collapse to
//                        one), false shifts it too (two committed lines must both keep moving).
template <typename Entry>
static void ShiftLineMap(std::map<unsigned int, Entry>& lines, unsigned int line, int line_offset,
                         bool atLineStart, bool collapseCollisions)
{
	auto it = lines.begin();
	while (it != lines.end()) {

		const unsigned int calc_line = it->first + OffsetOf(it->second);

		if (calc_line > line || (atLineStart && calc_line == line))
			OffsetOf(it->second) += line_offset;

		it = std::next(it);

		while (line_offset > 0 && it != lines.end() && (it->first + OffsetOf(it->second)) == calc_line) {
			if (collapseCollisions)
				it = lines.erase(it);
			else { OffsetOf(it->second) += line_offset; it = std::next(it); }
		}
	}
}

// THE STANDARD DOOR, the engine's ibValueSystemFunction::Message at Error: the designer's messages.
static void Message(const wxString& said)
{
	if (mainFrame != nullptr && mainFrame->GetOutputWindow() != nullptr)
		mainFrame->GetOutputWindow()->Output(said, ibProtocol::ibFrame::ibMessage::ibLevel::Error);
}

// A COMMAND TO THE SERVER'S DEBUGGER — debug {Command, …} on its own connection, answered at once, beside whatever the
// window's call is waiting on. A refusal is said in the messages: a step that does nothing has to say why.
static bool Send(ibProtocol::ibDebug::ibCommand command, ibProtocolNode params = ibProtocolNode(),
	ibProtocolNode* answer = nullptr)
{
	ibFrontendMainFrameDesigner* const frame = mainFrame;
	if (frame == nullptr)
		return false;
	params.SetValue(ibProtocol::ibDebug::Command, static_cast<int>(command));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	if (!frame->Debug(params, result, refusal, error)) {
		Message(ibString::Format(_("The debugger did not take the command: %s"), error));
		return false;
	}
	if (answer != nullptr)
		*answer = result;
	return true;
}

// A watched row's number — a pointer of the watch window's tree, said back by the server as the number it was sent.
static wxTreeItemId ItemOf(const ibProtocolNode& node)
{
	return wxTreeItemId(reinterpret_cast<void*>(static_cast<std::uintptr_t>(node.GetInt(ibProtocol::ibDebug::Item))));
}

// A watch's answer, as the server's debugger read it from the runtime — the row asked about, who asked, its values.
static ibWatchWindowData WatchOf(const ibProtocolNode& news)
{
	ibWatchWindowData data(ItemOf(news));
	data.SetBridgeId(news.GetString(ibProtocol::ibDebug::Asker));
	for (const ibProtocolNode& row : news.FindChild(ibProtocol::ibDebug::Rows).Children())
		data.AddWatch(row.GetString(ibProtocol::ibDebug::Name), row.GetString(ibProtocol::ibDebug::Value),
			row.GetString(ibProtocol::ibDebug::Type), row.GetBool(ibProtocol::ibDebug::Attributes), ItemOf(row));
	return data;
}

} // namespace
///////////////////////////////////////////////////////////////////////

// (The constructor — beside the store it asks: debugClientQuery.cpp.)

ibDebuggerClient::~ibDebuggerClient()
{
	wxDELETE(m_adapter);

	if (ms_debugClient == this)
		ms_debugClient = nullptr;
}

void ibDebuggerClient::Listen()
{
	ibFrontendMainFrameDesigner* const frame = mainFrame;
	if (frame == nullptr)
		return;

	// ⭐⭐ THE ONE PLACE THE NEWS CHANGES THREADS — told on the debugger connection's thread, received on the window's loop:
	// once, whatever the number of listeners; the fan-out comes after. Not waited for: this is news, not a call anybody
	// waits on. What a listener throws is said here, nobody holding it; and the debugger may be gone by then, with its
	// frame — the news has nobody left to reach.
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	const bool listening = frame->ListenDebug([](const ibProtocolNode& news) {
		if (wxTheApp == nullptr)
			return;
		wxTheApp->CallAfter([news]() {
			if (debugClient == nullptr)
				return;
			try {
				debugClient->Receive(news);
			}
			catch (const std::exception& err) {
				ibJournalWarning(wxT("debugger"), wxT("the debugger's news ended with an exception: %s"), wxString::FromUTF8(err.what()));
			}
			catch (...) {
				ibJournalWarning(wxT("debugger"), wxT("the debugger's news ended with an exception"));
			}
		});
	}, refusal, error);
	if (!listening)
		Message(ibString::Format(_("The debugger is not available: %s"), error));
}

void ibDebuggerClient::Receive(const ibProtocolNode& news)
{
	// Where a piece of news is of — the runtime's session, the file, the module by its address — and its line where the
	// editor's edits since have moved it: the server's debugger says the committed one.
	const auto lineOf = [this, &news]() {
		ibDebugLineData data;
		data.m_sessionGuid = news.GetString(ibProtocol::ibDebug::Session);
		data.m_fileName = news.GetString(ibProtocol::ibDebug::File);
		data.m_moduleName = news.GetString(ibProtocol::ibDebug::Module);
		data.m_line = GetLineOffset(data.m_moduleName, static_cast<int>(news.GetInt(ibProtocol::ibDebug::Line)));
		return data;
	};

	switch (static_cast<ibProtocol::ibDebug::ibEvent>(news.GetInt(ibProtocol::ibDebug::Event))) {
	case ibProtocol::ibDebug::ibEvent::SessionStart:
		m_hasConnections = news.GetBool(ibProtocol::ibDebug::Connected);
		m_adapter->OnSessionStart();
		break;
	case ibProtocol::ibDebug::ibEvent::SessionEnd:
		m_hasConnections = news.GetBool(ibProtocol::ibDebug::Connected);
		if (!m_hasConnections)
			m_enterLoop = false;
		m_adapter->OnSessionEnd();
		break;
	case ibProtocol::ibDebug::ibEvent::EnterLoop:
		m_enterLoop = true;
		m_adapter->OnEnterLoop(lineOf());
		break;
	case ibProtocol::ibDebug::ibEvent::LeaveLoop:
		m_enterLoop = false;
		m_adapter->OnLeaveLoop(lineOf());
		break;
	case ibProtocol::ibDebug::ibEvent::Message:
		m_adapter->OnMessageFromServer(lineOf(), news.GetString(ibProtocol::ibDebug::Text));
		break;
	case ibProtocol::ibDebug::ibEvent::ToolTip:
		if (IsEnterLoop()) {
			ibDebugExpressionData data;
			data.m_sessionGuid = news.GetString(ibProtocol::ibDebug::Session);
			data.m_fileName = news.GetString(ibProtocol::ibDebug::File);
			data.m_moduleName = news.GetString(ibProtocol::ibDebug::Module);
			data.m_expression = news.GetString(ibProtocol::ibDebug::Expression);
			m_adapter->OnSetToolTip(data, news.GetString(ibProtocol::ibDebug::Text));
		}
		break;
	case ibProtocol::ibDebug::ibEvent::Stack: {
		// Each call's line in its module, where the editor's edits since have moved it — as the stop's.
		ibStackData stackData;
		for (const ibProtocolNode& row : news.FindChild(ibProtocol::ibDebug::Rows).Children()) {
			const wxString module = row.GetString(ibProtocol::ibDebug::Module);
			stackData.AppendStack(module, row.GetString(ibProtocol::ibDebug::Name),
				static_cast<unsigned int>(GetLineOffset(module, static_cast<int>(row.GetInt(ibProtocol::ibDebug::Line)))));
		}
		m_adapter->OnSetStack(stackData);
		break;
	}
	case ibProtocol::ibDebug::ibEvent::Locals: {
		ibLocalWindowData locData;
		for (const ibProtocolNode& row : news.FindChild(ibProtocol::ibDebug::Rows).Children())
			locData.AddLocalVar(row.GetString(ibProtocol::ibDebug::Name), row.GetString(ibProtocol::ibDebug::Value),
				row.GetString(ibProtocol::ibDebug::Type), row.GetBool(ibProtocol::ibDebug::Attributes));
		m_adapter->OnSetLocalVariable(locData);
		break;
	}
	case ibProtocol::ibDebug::ibEvent::Variable:
		m_adapter->OnSetVariable(WatchOf(news));
		break;
	case ibProtocol::ibDebug::ibEvent::Expanded:
		m_adapter->OnSetExpanded(WatchOf(news));
		break;
	default:
		ibJournalWarning(wxT("debugger"), wxT("the debugger said something this designer does not know: %lld"),
			news.GetInt(ibProtocol::ibDebug::Event));
		break;
	}
}

void ibDebuggerClient::SearchServer(bool run_debug_server)
{
	if (run_debug_server) {
		Send(ibProtocol::ibDebug::ibCommand::Search);
		return;
	}

	ibProtocolNode answer;
	if (!Send(ibProtocol::ibDebug::ibCommand::Connections, ibProtocolNode(), &answer))
		return;
	m_listConnection.clear();
	for (const ibProtocolNode& row : answer.FindChild(ibProtocol::ibDebug::Rows).Children()) {
		ibDebuggerClientConnection connection;
		connection.m_hostName = row.GetString(ibProtocol::ibDebug::Host);
		connection.m_port = static_cast<unsigned short>(row.GetInt(ibProtocol::ibDebug::Port));
		connection.m_connectionType = static_cast<ConnectionType>(row.GetInt(ibProtocol::ibDebug::Type));
		connection.m_verifiedConnection = row.GetBool(ibProtocol::ibDebug::Verified);
		connection.m_connected = row.GetBool(ibProtocol::ibDebug::Connected);
		connection.m_userName = row.GetString(ibProtocol::ibDebug::User);
		connection.m_compName = row.GetString(ibProtocol::ibDebug::Computer);
		m_listConnection.push_back(std::move(connection));
	}
}

void ibDebuggerClient::AttachConnection(const wxString& hostName, unsigned short port) const
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibDebug::Host, hostName).SetValue(ibProtocol::ibDebug::Port, static_cast<int>(port));
	Send(ibProtocol::ibDebug::ibCommand::Attach, params);
}

void ibDebuggerClient::DetachConnection(const wxString& hostName, unsigned short port, bool kill) const
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibDebug::Host, hostName).SetValue(ibProtocol::ibDebug::Port, static_cast<int>(port))
		.SetValue(ibProtocol::ibDebug::Kill, kill);
	Send(ibProtocol::ibDebug::ibCommand::Detach, params);
}

//special functions:
void ibDebuggerClient::Continue()
{
	Send(ibProtocol::ibDebug::ibCommand::Continue);
}

void ibDebuggerClient::StepOver()
{
	Send(ibProtocol::ibDebug::ibCommand::StepOver);
}

void ibDebuggerClient::StepInto()
{
	Send(ibProtocol::ibDebug::ibCommand::StepInto);
}

void ibDebuggerClient::StepOut()
{
	Send(ibProtocol::ibDebug::ibCommand::StepOut);
}

void ibDebuggerClient::Pause()
{
	Send(ibProtocol::ibDebug::ibCommand::Pause);
}

std::map<wxString, std::map<unsigned int, wxString>> ibDebuggerClient::GetBreakpoints() const
{
	std::map<wxString, std::map<unsigned int, wxString>> out;

	for (const auto& module : m_listBreakpoint) {

		std::map<unsigned int, wxString>& lines = out[module.first];

		// THE SUM IS THE ADDRESS. A row is (committed line -> offset the edits since have moved it
		// by); either half alone points at a line nobody is looking at. Same arithmetic ShiftLineMap
		// does when it decides which rows an edit moves.
		for (const auto& line : module.second)
			lines[line.first + line.second.m_offset] = line.second.m_condition;
	}

	// A module whose last breakpoint was taken off keeps an empty row in the map, and reporting it
	// would read as "something is set here" — which is the one thing this must never say wrongly.
	for (auto it = out.begin(); it != out.end(); ) {
		if (it->second.empty()) it = out.erase(it);
		else ++it;
	}

	return out;
}

void ibDebuggerClient::Stop(bool kill)
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibDebug::Kill, kill);
	Send(ibProtocol::ibDebug::ibCommand::Stop, params);
}

void ibDebuggerClient::InitializeModule(const wxString& strModuleName, unsigned int line_count)
{
	m_listOffsetBreakpoint[strModuleName].clear();
	for (unsigned int i = 0; i < line_count; i++)
		m_listOffsetBreakpoint[strModuleName].emplace(i, 0);

	LoadBreakpointCollection(strModuleName);
}

// THE EDITOR'S LINES MOVED — its breakpoints and the committed lines' places with them, here alone: the runtime counts
// the committed lines (debugServer.cpp, CommandId_ToggleBreakpoint), and the server is told those.
void ibDebuggerClient::PatchModule(const wxString& strModuleName, unsigned int line, int line_offset, bool atLineStart)
{
	// Two maps shift the same way for this edit, differing only in collision handling:
	// breakpoints collapse onto one line; the dense committed<->editor offset map keeps all lines.
	if (auto* breakpoints = FindModuleLines(m_listBreakpoint, strModuleName))
		ShiftLineMap(*breakpoints, line, line_offset, atLineStart, /*collapseCollisions*/ true);

	if (auto* offsets = FindModuleLines(m_listOffsetBreakpoint, strModuleName))
		ShiftLineMap(*offsets, line, line_offset, atLineStart, /*collapseCollisions*/ false);
}

bool ibDebuggerClient::SaveModule(const wxString& strModuleName, unsigned int line_count)
{
	//initialize breakpoint
	auto breakpoint_iterator = std::find_if(m_listBreakpoint.begin(), m_listBreakpoint.end(),
		[&strModuleName](const auto& pair) { return stringUtils::IsSameAs(pair.first, strModuleName); });

	if (breakpoint_iterator != m_listBreakpoint.end()) {

		std::map<unsigned int, ibBreakpoint>& list_breakpoint = breakpoint_iterator->second, moduleBreakpointsNew;
		for (auto it = list_breakpoint.begin(); it != list_breakpoint.end(); it++) {
			if (!OffsetBreakpointInDB(breakpoint_iterator->first, it->first, it->second.m_offset, it->second.m_condition))
				return false;
			moduleBreakpointsNew.emplace(it->first + it->second.m_offset, ibBreakpoint{ 0, it->second.m_condition });
		}

		list_breakpoint = std::move(moduleBreakpointsNew);
	}

	// THE MODULE'S LENGTH IS THE TEXT SAVED (line_count), not one worked out from the offsets map: the map knows where
	// the committed lines went, not the lines added after the last of them — and in a server's client, where nobody
	// edits, it never moved at all, so a module kept the length it was loaded with and every line past it was refused
	// as "the running application does not have this line" (2026-10-14).
	auto module_offset_iterator = std::find_if(m_listOffsetBreakpoint.begin(), m_listOffsetBreakpoint.end(),
		[&strModuleName](const auto& pair) { return stringUtils::IsSameAs(pair.first, strModuleName); });

	InitializeModule(module_offset_iterator != m_listOffsetBreakpoint.end() ? module_offset_iterator->first : strModuleName,
		line_count);
	return true;
}

void ibDebuggerClient::RemoveModule(const wxString& strModuleName)
{
	auto breakpoint_iterator = std::find_if(m_listBreakpoint.begin(), m_listBreakpoint.end(),
		[&strModuleName](const auto& pair) { return stringUtils::IsSameAs(pair.first, strModuleName); });
	if (breakpoint_iterator != m_listBreakpoint.end())
		m_listBreakpoint[breakpoint_iterator->first].clear();

	auto module_offset_iterator = std::find_if(m_listOffsetBreakpoint.begin(), m_listOffsetBreakpoint.end(),
		[&strModuleName](const auto& pair) { return stringUtils::IsSameAs(pair.first, strModuleName); });
	if (module_offset_iterator != m_listOffsetBreakpoint.end())
		m_listOffsetBreakpoint[module_offset_iterator->first].clear();
}

bool ibDebuggerClient::SaveAllBreakpoints()
{
	//initialize breakpoint
	for (auto breakpoint_iterator = m_listBreakpoint.begin(); breakpoint_iterator != m_listBreakpoint.end(); breakpoint_iterator++) {
		std::map<unsigned int, ibBreakpoint>& list_breakpoint = breakpoint_iterator->second, moduleBreakpointsNew;
		for (auto it = list_breakpoint.begin(); it != list_breakpoint.end(); it++) {
			if (!OffsetBreakpointInDB(breakpoint_iterator->first, it->first, it->second.m_offset, it->second.m_condition))
				return false;
			moduleBreakpointsNew.emplace(it->first + it->second.m_offset, ibBreakpoint{ 0, it->second.m_condition });
		}
		list_breakpoint = std::move(moduleBreakpointsNew);
	}

	// The modules as applied — their lengths from the store, which read the saved text (SaveModule there); the offsets
	// map would miss the lines added after the last committed one.
	LoadModuleCollection();
	return true;
}

std::map<unsigned int, int>::iterator ibDebuggerClient::ResolveOriginalLine(
	std::map<unsigned int, int>& list_module_offset, unsigned int line) const
{
	unsigned int startLine = line; int locOffsetPrev = 0, locOffsetCurr = 0;
	for (auto it = list_module_offset.begin(); it != list_module_offset.end(); it++) {
		if (it->second < 0 && (int)it->first < -it->second) { locOffsetPrev = it->second; continue; }
		locOffsetCurr = it->second;
		if ((it->first + locOffsetPrev) <= line && (it->first + locOffsetCurr) >= line) { startLine = it->first; break; }
		locOffsetPrev = it->second;
	}
	return list_module_offset.find(startLine);
}

// 🛑⭐⭐ THE REASON GOES TO WHOEVER ASKED, and it used to go to a MODAL BOX instead.
//
// A breakpoint on a line the running application does not have is a refusal with a real cause —
// the module was edited and not applied, so the runtime's copy has different lines. That cause was
// shown as `wxMessageBox` from inside the engine, which is wrong three times over: backend.dll is
// GUI-free by rule; a modal blocks the designer until a person clicks it; and a caller that is not
// a person — an assistant over MCP — got back a bare `false` and had to guess, while the sentence
// explaining it stood on somebody else's screen (measured 2026-09-02: the box appeared in front of
// Max, the tool answered "the debugger did not accept it", and the two never met).
//
// Now the sentence travels with the answer and the caller decides what to do with it: the code
// editor shows it, the tool returns it.
//
// The runtimes are told by the store's command — the server's debugger sets it on its own, and tells them.
bool ibDebuggerClient::ToggleBreakpoint(const wxString& strModuleName, unsigned int line,
	wxString* refusal, const wxString& condition)
{
	std::map<unsigned int, int>& list_module_offset = m_listOffsetBreakpoint[strModuleName];
	std::map<unsigned int, int>::iterator itOffset = ResolveOriginalLine(list_module_offset, line);
	if (itOffset == list_module_offset.end() || line != (itOffset->first + itOffset->second)) {

		const wxString said =
			_("The running application does not have this line: the module has been edited "
			  "and not applied, so its copy is a different text. Apply the configuration, "
			  "or set the breakpoint on a line both copies share.");

		// ⭐ AND WHEN NOBODY ASKED FOR THE REASON, IT GOES OUT THE STANDARD DOOR — the platform's own
		// `Message`, which already knows how to reach whoever is on the other side: it asks the
		// SESSION for its frame, and a web client's frame queues while a desktop one shows
		// (systemManagerFunc.cpp). Reaching for the frame here would be that same code written a
		// second time, minus the eval-mode guard that keeps autocomplete from talking out loud
		// (Max, 2026-09-02: *"the standard message function knows how to deliver to the client"*).
		if (refusal != nullptr)
			*refusal = said;
		else
			Message(said);

		return false;
	}
	std::map<unsigned int, ibBreakpoint>& list_breakpoint = m_listBreakpoint[strModuleName];
	auto breakpoint_iterator = list_breakpoint.find(itOffset->first);
	const unsigned int currLine = itOffset->first;
	const int offset = breakpoint_iterator != list_breakpoint.end() ? breakpoint_iterator->second.m_offset : itOffset->second;

	// Already there, with this very condition: nothing to write and nothing to tell the runtime.
	if (breakpoint_iterator != list_breakpoint.end() && breakpoint_iterator->second.m_condition == condition)
		return true;

	// A NEW BREAKPOINT, OR THE SAME ONE WITH ANOTHER CONDITION - one row and one message either way: the
	// store upserts by (module, line), and the runtime keeps one condition per line, the last it was sent.
	if (!ToggleBreakpointInDB(strModuleName, currLine, condition))
		return false;

	list_breakpoint[currLine] = ibBreakpoint{ offset, condition };
	return true;
}

bool ibDebuggerClient::RemoveBreakpoint(const wxString& strModuleName, unsigned int line)
{
	std::map<unsigned int, int>& list_module_offset = m_listOffsetBreakpoint[strModuleName];
	std::map<unsigned int, int>::iterator itOffset = ResolveOriginalLine(list_module_offset, line);
	if (itOffset == list_module_offset.end())
		return false;
	std::map<unsigned int, ibBreakpoint>& list_breakpoint = m_listBreakpoint[strModuleName];
	auto breakpoint_iterator = list_breakpoint.find(itOffset->first);
	unsigned int currLine = itOffset->first;
	if (breakpoint_iterator != list_breakpoint.end()) {
		if (RemoveBreakpointInDB(strModuleName, currLine))
			list_breakpoint.erase(breakpoint_iterator);
		else
			return false;
	}
	return true;
}

// 🛑 THE SAME LEFTOVER, ONE FLOOR ALONG — and this one showed a person a C++ SIGNATURE. "Error in :
// void ibDebuggerClient::RemoveAllBreakpoint()", in a modal box: untranslatable, telling the reader
// nothing they can act on, and blocking the window until it is clicked. Whoever it was written for
// was reading a debugger's own source, not using the product.
//
// It says what failed and what that means for them, through the standard door, and the outcome
// comes back to the caller instead of only to a screen.
bool ibDebuggerClient::RemoveAllBreakpoint(wxString* refusal)
{
	if (RemoveAllBreakpointInDB()) {
		m_listBreakpoint.clear();
		// The engine's frame->RefreshFrame(): every module's editor draws its breakpoints again.
		for (ibFrontendDocument* doc : docManager->GetDocumentsVector()) {
			const ibModuleEditDocument* moduleDoc = dynamic_cast<ibModuleEditDocument*>(doc);
			ibCodeEditor* codeEditor = moduleDoc != nullptr ? moduleDoc->GetCodeEditor() : nullptr;
			if (codeEditor != nullptr)
				codeEditor->RefreshBreakpoint();
		}
		return true;
	}

	const wxString said =
		_("The breakpoints could not be cleared: the store they are kept in did not accept the "
		  "change. They are still set, and the running application still has them.");

	if (refusal != nullptr)
		*refusal = said;
	else
		Message(said);

	return false;
}

// ⭐⭐ WHICH STOP THIS IS ABOUT, and WHO ASKED — the server's debugger names the stop, as every step of its own does; the
// asker is written here, echoed back with the answer, read by nobody in between. What is watched is the server's to
// register again after a runtime's reconnect.
#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
void ibDebuggerClient::AddExpression(const wxString& strExpression, unsigned long long id, const wxString& asker)
#else
void ibDebuggerClient::AddExpression(const wxString& strExpression, unsigned int id, const wxString& asker)
#endif
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibDebug::Expression, strExpression)
		.SetValue(ibProtocol::ibDebug::Item, static_cast<long long>(id))
		.SetValue(ibProtocol::ibDebug::Asker, asker);
	Send(ibProtocol::ibDebug::ibCommand::AddExpression, params);
}

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
void ibDebuggerClient::ExpandExpression(const wxString& strExpression, unsigned long long id, const wxString& asker)
#else
void ibDebuggerClient::ExpandExpression(const wxString& strExpression, unsigned int id, const wxString& asker)
#endif
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibDebug::Expression, strExpression)
		.SetValue(ibProtocol::ibDebug::Item, static_cast<long long>(id))
		.SetValue(ibProtocol::ibDebug::Asker, asker);
	Send(ibProtocol::ibDebug::ibCommand::ExpandExpression, params);
}

#if _USE_64_BIT_POINT_IN_DEBUGGER == 1
void ibDebuggerClient::RemoveExpression(unsigned long long id)
#else
void ibDebuggerClient::RemoveExpression(unsigned int id)
#endif
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibDebug::Item, static_cast<long long>(id));
	Send(ibProtocol::ibDebug::ibCommand::RemoveExpression, params);
}

void ibDebuggerClient::SetLevelStack(unsigned int level)
{
	if (ibDebuggerClient::IsEnterLoop()) {
		ibProtocolNode params;
		params.SetValue(ibProtocol::ibDebug::Level, static_cast<long long>(level));
		Send(ibProtocol::ibDebug::ibCommand::Stack, params);
	}
}

void ibDebuggerClient::EvaluateToolTip(const wxString& strFileName, const wxString& strModuleName, const wxString& strExpression)
{
	if (ibDebuggerClient::IsEnterLoop()) {
		ibProtocolNode params;
		params.SetValue(ibProtocol::ibDebug::File, strFileName)
			.SetValue(ibProtocol::ibDebug::Module, strModuleName)
			.SetValue(ibProtocol::ibDebug::Expression, strExpression);
		Send(ibProtocol::ibDebug::ibCommand::ToolTip, params);
	}
}

std::map<unsigned int, wxString> ibDebuggerClient::GetDebugList(const wxString& strModuleName)
{
	std::map<unsigned int, wxString> listBreakpoint;
	if (auto* breakpoints = FindModuleLines(m_listBreakpoint, strModuleName))
		for (const auto& breakpoint : *breakpoints)
			listBreakpoint[breakpoint.first + breakpoint.second.m_offset] = breakpoint.second.m_condition;
	return listBreakpoint;
}
