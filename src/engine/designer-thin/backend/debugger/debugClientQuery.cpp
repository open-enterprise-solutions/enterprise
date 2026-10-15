////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"

#include "designer-thin/mainFrame/mainFrameDesigner.h"

// THE STORE IS THE SERVER'S — the engine's sys_dbg, of the base the configuration is in (ibSchemaDebugger,
// frmserver/schema/designer/debugger.cpp): what the engine's client asked its builder here is asked of the schema, by the
// same verbs. The server keeps its own client's rows the same (its apply rewrites them by its own map).

namespace {

	// The schema's Debugger: its Build (no command) or one of its commands, with its arguments.
	bool AskStore(ibProtocolNode args, ibProtocolNode& answer, int command = 0)
	{
		ibFrontendMainFrameDesigner* const frame = mainFrame;
		if (frame == nullptr)
			return false;
		ibProtocolNode params;
		params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Debugger));
		if (command != 0)
			params.SetValue(ibProtocolName::Command, static_cast<long long>(command));
		params.SetValue(ibProtocolName::Args, args);
		return frame->Call(ibProtocolMethod::Schema, params, answer);
	}

	bool AskStore(ibProtocolDebuggerCommand command, ibProtocolNode args)
	{
		ibProtocolNode answer;
		return AskStore(args, answer, static_cast<int>(command));
	}

	// A module's breakpoints as the store says them (Breakpoints — Line, Condition), in place of those held.
	void ReadBreakpoints(const ibProtocolNode& node, std::map<unsigned int, ibDebuggerClient::ibBreakpoint>& breakpoints)
	{
		breakpoints.clear();
		for (const ibProtocolNode& breakpoint : node.FindChild(ibProtocolName::Breakpoints).Children())
			breakpoints[static_cast<unsigned int>(breakpoint.GetInt(ibProtocolName::Line))] =
				ibDebuggerClient::ibBreakpoint{ 0, breakpoint.GetString(ibProtocolName::Condition) };
	}
}

///////////////////////////////////////////////////////////////////////////////////////

ibDebuggerClient::ibDebuggerClient() :
	m_activeSocket(nullptr),
	m_adapter(new ibDebuggerClientAdapter),
	m_enterLoop(false), m_connectionSuccess(false), m_numberOfConnectionAttempts(numberOfConnectionAttempts)
{
	ibDebuggerClient::CreateBreakpointDatabase();
	ms_debugClient = this;

	LoadModuleCollection();
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

// THE CONFIGURATION THE DEBUGGER IS OF, as the engine's was given it: its guid (the runtime's verify) and every module,
// read with its lines — what the desktop's tree did for each module at load (OnBeforeRunMetaObject), and its modules
// again at an apply (OnSaveMetaObject).
void ibDebuggerClient::LoadModuleCollection()
{
	ibProtocolNode answer;
	if (!AskStore(ibProtocolNode(), answer))
		return;
	m_configGuid = ibGuid(answer.GetString(ibProtocolName::Guid));
	// InitializeModule over the one answer: each row brings its module's breakpoints, where InitializeModule would ask
	// the store for them a module at a time — a call each, 71 at a designer's start: ten seconds 130 ms from the
	// server (2026-10-15).
	for (const ibProtocolNode& module : answer.FindChild(ibProtocolName::Rows).Children()) {
		const wxString strModuleName = module.GetString(ibProtocolName::Module);
		std::map<unsigned int, int>& list_module_offset = m_listOffsetBreakpoint[strModuleName];
		list_module_offset.clear();
		for (unsigned int i = 0; i < static_cast<unsigned int>(module.GetInt(ibProtocolName::Count)); i++)
			list_module_offset.emplace(i, 0);
		ReadBreakpoints(module, m_listBreakpoint[strModuleName]);
	}
}

void ibDebuggerClient::LoadBreakpointCollection(const wxString& strModuleName)
{
	ibProtocolNode args, answer;
	args.SetValue(ibProtocolName::Module, strModuleName);
	AskStore(args, answer);   // not answered — none held
	ReadBreakpoints(answer, m_listBreakpoint[strModuleName]);
}

bool ibDebuggerClient::ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line, const wxString& condition)
{
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Module, strModuleName);
	args.SetValue(ibProtocolName::Line, static_cast<long long>(line));
	args.SetValue(ibProtocolName::Condition, condition);
	return AskStore(ibProtocolDebuggerCommand::ToggleBreakpoint, args);
}

bool ibDebuggerClient::RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line)
{
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Module, strModuleName);
	args.SetValue(ibProtocolName::Line, static_cast<long long>(line));
	return AskStore(ibProtocolDebuggerCommand::RemoveBreakpoint, args);
}

bool ibDebuggerClient::OffsetBreakpointInDB(const wxString& strModuleName, unsigned int lineFrom, int offset, const wxString& condition)
{
	// Move a breakpoint from lineFrom to lineFrom+offset: delete the old row, upsert the new one - with its
	// condition, which moves with the line.
	return RemoveBreakpointInDB(strModuleName, lineFrom)
		&& ToggleBreakpointInDB(strModuleName, static_cast<unsigned int>(lineFrom + offset), condition);
}

bool ibDebuggerClient::RemoveAllBreakpointInDB()
{
	AskStore(ibProtocolDebuggerCommand::RemoveAllBreakpoint, ibProtocolNode());   // best-effort
	return true;
}
