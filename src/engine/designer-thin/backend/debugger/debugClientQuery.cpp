////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"

#include "designer-thin/mainFrame/mainFrameDesigner.h"
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibDebugger

// THE STORE IS THE SERVER'S — the engine's sys_dbg, of the base the configuration is in (ibSchemaDebugger,
// frmserver/schema/designer/debugger.cpp): what the engine's client asked its builder here is asked of the schema, by the
// same verbs. And so are the lines: the server's client keeps the breakpoints at the editor's lines, moved by the edits
// the editor's text carries (ibModuleTextView, Change) — one map, as the desktop's one process had it; none here.

namespace {

	// The schema's Debugger: its Build (no command) or one of its commands, with its arguments — refused with the store's
	// reason (`said`): no debugger, a line the running application does not have.
	bool AskStore(ibProtocolNode args, ibProtocolNode& answer, int command, wxString& said)
	{
		ibFrontendMainFrameDesigner* const frame = mainFrame;
		if (frame == nullptr)
			return false;
		ibProtocolNode params;
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Debugger));
		if (command != 0)
			params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
		params.SetValue(ibProtocol::ibSchema::Args, args);
		ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
		ibString error;
		if (frame->Call(ibProtocol::ibMethod::Schema, params, answer, refusal, error))
			return true;
		said = error;
		return false;
	}

	bool AskStore(ibProtocol::ibSchema::ibDebugger::ibCommand command, ibProtocolNode args, wxString& said)
	{
		ibProtocolNode answer;
		return AskStore(args, answer, static_cast<int>(command), said);
	}
}

///////////////////////////////////////////////////////////////////////////////////////

// THE CONFIGURATION THE DEBUGGER IS OF: its guid, the runtime's verify — asked of the store…
ibDebuggerClient::ibDebuggerClient() :
	m_adapter(new ibDebuggerClientAdapter)
{
	ibDebuggerClient::CreateBreakpointDatabase();
	ms_debugClient = this;

	ibProtocolNode store;
	wxString said;
	if (AskStore(ibProtocolNode(), store, 0, said))
		m_configGuid = ibGuid(store.GetString(ibProtocol::ibSchema::ibDebugger::Guid));
	Listen();
}

// …or as the login read it (the designer's GetStartSchemas).
ibDebuggerClient::ibDebuggerClient(const ibProtocolNode& store) :
	m_adapter(new ibDebuggerClientAdapter)
{
	ibDebuggerClient::CreateBreakpointDatabase();
	ms_debugClient = this;

	m_configGuid = ibGuid(store.GetString(ibProtocol::ibSchema::ibDebugger::Guid));
	Listen();
}

// The table is the server's, made by its own client with the configuration (CreateBreakpointDatabase there).
bool ibDebuggerClient::TableAlreadyCreated()
{
	return true;
}

bool ibDebuggerClient::CreateBreakpointDatabase()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////

//db support

// The module's breakpoints at the editor's lines, as the server's client has them now — the edits held with the text
// carried ahead of the question (ibCodeEditor::SaveModule).
std::map<unsigned int, wxString> ibDebuggerClient::GetDebugList(const wxString& strModuleName)
{
	ibProtocolNode args, answer;
	args.SetValue(ibProtocol::ibSchema::ibDebugger::Module, strModuleName);
	std::map<unsigned int, wxString> breakpoints;
	wxString said;
	if (!AskStore(args, answer, 0, said))
		return breakpoints;   // not answered — none drawn
	for (const ibProtocolNode& breakpoint : answer.FindChild(ibProtocol::ibSchema::ibDebugger::Breakpoints).Children())
		breakpoints[static_cast<unsigned int>(breakpoint.GetInt(ibProtocol::ibSchema::ibDebugger::Line))] =
			breakpoint.GetString(ibProtocol::ibSchema::ibDebugger::Condition);
	return breakpoints;
}

bool ibDebuggerClient::ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line, const wxString& condition,
	wxString& said)
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibDebugger::Module, strModuleName);
	args.SetValue(ibProtocol::ibSchema::ibDebugger::Line, static_cast<long long>(line));
	args.SetValue(ibProtocol::ibSchema::ibDebugger::Condition, condition);
	return AskStore(ibProtocol::ibSchema::ibDebugger::ibCommand::ToggleBreakpoint, args, said);
}

bool ibDebuggerClient::RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line, wxString& said)
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibDebugger::Module, strModuleName);
	args.SetValue(ibProtocol::ibSchema::ibDebugger::Line, static_cast<long long>(line));
	return AskStore(ibProtocol::ibSchema::ibDebugger::ibCommand::RemoveBreakpoint, args, said);
}

bool ibDebuggerClient::RemoveAllBreakpointInDB()
{
	wxString said;
	AskStore(ibProtocol::ibSchema::ibDebugger::ibCommand::RemoveAllBreakpoint, ibProtocolNode(), said);   // best-effort
	return true;
}
