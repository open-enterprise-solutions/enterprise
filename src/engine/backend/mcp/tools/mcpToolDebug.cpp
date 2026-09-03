////////////////////////////////////////////////////////////////////////////
//	Description : the debugger tools - watching a run that is happening
////////////////////////////////////////////////////////////////////////////
//
// THE LOOP THESE CLOSE. A module that compiles is not a module that works: the
// compiler checks that a call parses, not that the object allows it or takes
// that many arguments. Until now the answer to "did it run" was a person reading
// an error off a screen.
//
// With the journal, a failure comes back by itself - module, line, stack. That
// is enough to READ the code and see what is wrong most of the time. What it
// cannot give is a VALUE: what was actually in that variable at that moment,
// what a reference actually held, why the collection was empty.
//
// So these tools are for the remaining case. Put a breakpoint where the journal
// pointed, ask for the application to be run, and when it stops - which arrives
// as a message, not as something to poll for - read the stack, read the locals,
// and evaluate whatever the code is about to do. The developer's own debugger
// windows keep working throughout: the bridge is a list, and both are on it.
//
// WHAT IS DELIBERATELY NOT HERE. Starting the application. The runtime is the
// developer's process, started from their machine, under their session - asking
// for it is a sentence, and doing it silently would be a second way to launch
// software that already has one.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/debugger/debugClient.h"
#include "backend/mcp/mcpDebugBridge.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"

namespace {
using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgStop()
{
	static const ibArg s_a(wxT("stop"), ibArg::Kind::Flag,
		ibMcpText("Detach AND end the application - the menu's Stop debugging program. Whatever it "
		  "was doing does not finish, so this is not the polite way to close it: it is for a "
		  "runtime that is stuck or that must not go on."));
	return s_a;
}

const ibArg& ArgModule()
{
	static const ibArg s_a(wxT("module"), ibArg::Kind::Whole,
		ibMcpText("The module's NodeId."));
	return s_a;
}

const ibArg& ArgLine()
{
	static const ibArg s_a(wxT("line"), ibArg::Kind::Whole,
		ibMcpText("Line number, 1-based - the number the journal and the editor both show."));
	return s_a;
}

const ibArg& ArgRemove()
{
	static const ibArg s_a(wxT("remove"), ibArg::Kind::Flag,
		ibMcpText("Take the breakpoint off instead of putting it on."));
	return s_a;
}

const ibArg& ArgAll()
{
	static const ibArg s_a(wxT("all"), ibArg::Kind::Flag,
		ibMcpText("Clear EVERY breakpoint, in every module - then module and line are not used. "
			  "Breakpoints outlive the run that needed them, and a forgotten one stops somebody "
			  "else's."));
	return s_a;
}

const ibArg& ArgExpression()
{
	static const ibArg s_a(wxT("expression"), ibArg::Kind::Text,
		ibMcpText("The expression, exactly as it would be written in the module."), /*required*/ true);
	return s_a;
}

const ibArg& ArgCode()
{
	static const ibArg s_a(wxT("code"), ibArg::Kind::Text,
		ibMcpText("Whole statements, in the configuration's own dialect - platform_state says which. "
			  "Everything it writes is rolled back, so read what you need INSIDE it: the last "
			  "value, or `Result`, comes back as the answer."), /*required*/ true);
	return s_a;
}

const ibArg& ArgMembers()
{
	static const ibArg s_a(wxT("members"), ibArg::Kind::Flag,
		ibMcpText("Unfold the value into its parts instead of printing it."));
	return s_a;
}

const ibArg& ArgAction()
{
	static const ibArg s_a(wxT("action"), ibArg::Kind::Text,
		ibMcpText("What to do with the stopped runtime: continue it, step over the line, step into "
			  "the call, or pause a runtime that is running."),
			/*required*/ true, { wxT("continue"), wxT("over"), wxT("into"), wxT("pause") });
	return s_a;
}

const ibArg& ArgHost()
{
	static const ibArg s_a(wxT("host"), ibArg::Kind::Text,
		ibMcpText("Which runtime, as debug_sessions reports it. Omit to take every ready one."));
	return s_a;
}

const ibArg& ArgPort()
{
	static const ibArg s_a(wxT("port"), ibArg::Kind::Whole,
		ibMcpText("Its port, as debug_sessions reports it. Required when host is given - a host alone "
			  "does not name a runtime, since two can run on one machine."));
	return s_a;
}

const ibArg& ArgDetach()
{
	static const ibArg s_a(wxT("detach"), ibArg::Kind::Flag,
		ibMcpText("Let it go instead of taking it. The runtime keeps running; it simply stops "
			  "stopping. This is the Debug menu's Stop debugging."));
	return s_a;
}

// Every tool here needs the same two things, and both refuse in words rather
// than in silence: a debugger to talk to, and a bridge that has been listening.
ibMcpDebugBridge* Bridge(wxString& refusal)
{
	if (debugClient == nullptr) {
		refusal = ibMcpText("This process has no debugger client.");
		return nullptr;
	}

	ibMcpDebugBridge* bridge = ibMcpDebug();
	if (bridge == nullptr) {
		refusal = ibMcpText("The assistant is not attached to the debugger.");
		return nullptr;
	}

	return bridge;
}

// A BREAKPOINT IS ADDRESSED BY THE MODULE'S DOC PATH, not by its name. That is
// the same key the editor uses when a developer clicks the margin - the document
// is literally named by it - and it is the one identifier that survives the
// object being renamed.
wxString ModuleDocPath(const ibDataNode& params, wxString& moduleName, wxString& refusal)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return wxEmptyString;
	}

	const s32 id = (s32)ArgModule().Whole(params);
	if (id <= 0) {
		refusal = ibMcpText("Pass the module's NodeId - metadata_get on the owning object lists it.");
		return wxEmptyString;
	}

	ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id);
	if (object == nullptr) {
		refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %i."), (int)id);
		return wxEmptyString;
	}

	ibValueMetaObjectModuleBase* module = ibMcpModuleOf(object, refusal);
	if (module == nullptr)
		return wxEmptyString;

	moduleName = module->GetName();
	return module->GetDocPath();
}

ibDataValue MemberEntry(const ibMcpDebugBridge::Local& member)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
	node->SetValue(wxT("name"), member.m_name);
	node->SetValue(wxT("value"), member.m_value);
	if (!member.m_type.IsEmpty())
		node->SetValue(wxT("type"), member.m_type);

	// WHETHER IT OPENS FURTHER. Without it a caller cannot tell a value that has
	// no parts from one whose parts were simply not asked for.
	if (member.m_hasAttributes)
		node->AddField(wxT("hasMembers"), ibDataValue::Bool(true));

	return ibDataValue::Child(node);
}


} // namespace

//---------------------------------------------------------------------------
// debug_breakpoint
//---------------------------------------------------------------------------
class ibMcpToolDebugBreakpoint : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_breakpoint"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ArgRemove().Flag(params)
				? ibMcpText("taking the breakpoint off '%s' line %i")
				: ibMcpText("putting a breakpoint on '%s' line %i"),
			ibMcpNameOf(params, ArgModule().Name()),
			(int)(s32)ArgLine().Whole(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Put a breakpoint on a line of a module, or take one off. The line is the one "
			"the journal reported, counted the way the editor counts. Set the breakpoints "
			"first, then ask for the application to be run - the stop arrives as a message. "
			"`all: true` with nothing else clears every breakpoint there is, which is the menu's "
			"Remove all breakpoints and the way to leave a base as you found it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgModule(), ArgLine(), ArgRemove(), ArgAll() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (debugClient == nullptr) {
			refusal = ibMcpText("This process has no debugger client.");
			return false;
		}

		// ⭐ THE WHOLE SWEEP FIRST, because it takes neither of the arguments below and asking a
		// module for its id on the way to clearing every module would refuse for the wrong reason.
		if (ArgAll().Flag(params)) {

			debugClient->RemoveAllBreakpoint();

			result.AddField(wxT("done"), ibDataValue::Bool(true));
			result.SetValue(wxT("action"), wxString(ibMcpText("removed all")));
			result.SetValue(wxT("note"),
				ibMcpText("Every breakpoint is gone, in every module. Nothing will stop until one is set "
				  "again."));
			return true;
		}

		wxString moduleName;
		const wxString docPath = ModuleDocPath(params, moduleName, refusal);
		if (docPath.IsEmpty())
			return false;

		const s32 line = (s32)ArgLine().Whole(params);
		if (line <= 0) {
			refusal = ibMcpText("A line number is 1 or more.");
			return false;
		}

		const bool remove = ArgRemove().Flag(params);

		// ⭐ THE REASON IS ASKED FOR. Without the out-parameter the engine states it through the
		// window the session owns — right for a person at the designer, useless to a caller on a
		// socket, who then reads a bare "not accepted" and guesses (2026-09-02: the sentence went
		// to a modal box in front of Max, and this tool answered "the debugger did not accept it").
		wxString said;

		const bool done = remove
			? debugClient->RemoveBreakpoint(docPath, (unsigned int)line)
			: debugClient->ToggleBreakpoint(docPath, (unsigned int)line, &said);

		// WHAT HAPPENED, ALWAYS. A breakpoint that was refused and a breakpoint
		// that was set look identical from outside unless the answer says which.
		result.AddField(wxT("done"), ibDataValue::Bool(done));
		result.SetValue(wxT("module"), moduleName);
		result.AddField(wxT("line"), ibDataValue::Int((s64)line));
		result.SetValue(wxT("action"), wxString(remove ? wxT("removed") : wxT("set")));

		if (!done)
			result.SetValue(wxT("note"), said.IsEmpty()
				? ibMcpText("The debugger did not accept it. There may be no line there, "
				  "or the module may not be known to a running application yet.")
				: said);

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugBreakpoint);

//---------------------------------------------------------------------------
// debug_state
//---------------------------------------------------------------------------
class ibMcpToolDebugState : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_state"); }

	// A stopped runtime is a state to be able to READ while the designer waits on a dialog — the
	// two are independent, and knowing where the runtime stands is often what decides what to say
	// about the dialog.
	bool RunsWhileBusy() const override { return true; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking at where the runtime is stopped");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Where the runtime is right now: whether an application is connected, whether "
			"it is stopped, and if it is - the module and line it stopped on, the whole call "
			"stack, and every local variable with its value and type. This is the first thing "
			"to ask after being told it stopped.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMcpDebugBridge* bridge = Bridge(refusal);
		if (bridge == nullptr)
			return false;

		const ibMcpDebugBridge::Stop stop = bridge->GetStop();

		result.AddField(wxT("connected"), ibDataValue::Bool(stop.m_connected));
		result.AddField(wxT("stopped"), ibDataValue::Bool(stop.m_stopped));

		if (!stop.m_message.IsEmpty())
			result.SetValue(wxT("message"), stop.m_message);

		if (!stop.m_stopped) {
			// NOT AN ERROR. A running application is an ordinary state, and the
			// answer to "where is it" is "it has not stopped".
			result.SetValue(wxT("note"), stop.m_connected
				? ibMcpText("An application is connected but running. Set a breakpoint and ask for it "
				    "to reach that code.")
				: ibMcpText("No application is connected. Ask for it to be started with debugging on."));
			return true;
		}

		result.SetValue(wxT("module"),
			stop.m_where.IsEmpty() ? stop.m_module : stop.m_where);
		result.AddField(wxT("line"), ibDataValue::Int((s64)stop.m_line));

		std::vector<ibDataValue> frames;
		for (const ibMcpDebugBridge::Frame& frame : stop.m_stack) {
			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("module"), frame.m_module);
			node->AddField(wxT("line"), ibDataValue::Int((s64)frame.m_line));
			frames.push_back(ibDataValue::Child(node));
		}
		result.AddField(wxT("stack"), ibDataValue::Array(frames));

		std::vector<ibDataValue> locals;
		for (const ibMcpDebugBridge::Local& local : stop.m_locals)
			locals.push_back(MemberEntry(local));
		result.AddField(wxT("locals"), ibDataValue::Array(locals));

		// The stack and the locals arrive as their own messages just behind the
		// stop. Empty here usually means "ask again in a moment", not "there are
		// none", and saying so costs one line.
		if (frames.empty() && locals.empty())
			result.SetValue(wxT("note"),
				ibMcpText("Stopped, but the stack and locals have not arrived yet. Ask again."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugState);

//---------------------------------------------------------------------------
// debug_evaluate
//---------------------------------------------------------------------------
class ibMcpToolDebugEvaluate : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_evaluate"); }

	// 🛑 NOT ON THE MAIN THREAD — see ibMcpTool::NeedsMainThread. This asks the runtime and WAITS,
	// and the answer comes back through wxTheApp::CallAfter: run here and the wait blocks the only
	// thread that could deliver it. Nothing in this verb touches a window; it talks to a socket and
	// a condition variable, both of which are indifferent to where they are called from.
	bool NeedsMainThread() const override { return false; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("working out '%s' in the stopped runtime"),
			ArgExpression().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Work out an expression IN THE STOPPED RUNTIME - anything the code at that "
			"line could have written, in the same scope. Pass members:true to get what the "
			"value is MADE OF rather than what it prints as, which is the only useful answer "
			"for a reference, a record set or a collection.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgExpression(), ArgMembers() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMcpDebugBridge* bridge = Bridge(refusal);
		if (bridge == nullptr)
			return false;

		const wxString expression = ArgExpression().Text(params);
		if (expression.IsEmpty()) {
			refusal = ibMcpText("Nothing to work out - pass an expression.");
			return false;
		}

		if (!bridge->GetStop().m_stopped) {
			refusal = ibMcpText("The runtime is not stopped. An expression can only be worked out at "
				"a stop, because that is where the variables in it exist.");
			return false;
		}

		result.SetValue(wxT("expression"), expression);

		if (ArgMembers().Flag(params)) {

			std::vector<ibMcpDebugBridge::Local> members;
			if (!bridge->Unfold(expression, members)) {
				// A TIMEOUT OR A VALUE WITH NO PARTS, and the caller can tell which by looking:
				// this used to be a flat "not supported" because the watch channel identified a
				// request by a pointer into the asking window's own row and nothing without a
				// window could use it. The question now carries who asked, so it can.
				refusal = wxString::Format(
					ibMcpText("'%s' did not answer with its parts. It may have none - debug_state says "
					  "which locals do - or the runtime did not answer in time."), expression);
				return false;
			}

			std::vector<ibDataValue> out;
			for (const ibMcpDebugBridge::Local& member : members)
				out.push_back(MemberEntry(member));

			result.AddField(wxT("members"), ibDataValue::Array(out));

			// A value with no parts is an answer; silence is not.
			if (out.empty())
				result.SetValue(wxT("note"),
					ibMcpText("That value has no parts to show. Ask without members for its printed form."));

			return true;
		}

		wxString answer;
		if (!bridge->Evaluate(expression, answer)) {
			refusal = ibMcpText("The runtime did not answer in time.");
			return false;
		}

		result.SetValue(wxT("value"), answer);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugEvaluate);

//---------------------------------------------------------------------------
// debug_run
//---------------------------------------------------------------------------
class ibMcpToolDebugRun : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_run"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString action = ArgAction().Text(params).Lower();
		if (action == wxT("over"))  return ibMcpText("stepping over a line");
		if (action == wxT("into"))  return ibMcpText("stepping into a call");
		if (action == wxT("pause")) return ibMcpText("pausing the runtime");
		return ibMcpText("letting the runtime carry on");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Let the stopped runtime carry on: continue to the next breakpoint, step over "
			"the current line, step into the call on it, or pause a running one. After "
			"continuing or stepping, ask debug_state again - the next stop arrives as a "
			"message.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgAction() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMcpDebugBridge* bridge = Bridge(refusal);
		if (bridge == nullptr)
			return false;

		const wxString action = ArgAction().Text(params).Lower();
		const bool stopped = bridge->GetStop().m_stopped;

		if (action == wxT("pause")) {
			debugClient->Pause();
			result.SetValue(wxT("action"), wxString(wxT("pause")));
			return true;
		}

		if (!stopped) {
			refusal = ibMcpText("The runtime is not stopped, so there is nothing to carry on from.");
			return false;
		}

		if (action == wxT("continue"))   debugClient->Continue();
		else if (action == wxT("over"))  debugClient->StepOver();
		else if (action == wxT("into"))  debugClient->StepInto();
		else {
			// An unknown word must not fall through to a default action - a
			// misspelling would then silently do something else.
			refusal = ibMcpText("Unknown action. Use continue, over, into or pause.");
			return false;
		}

		// The stop we were on is over the moment the runtime is told to move, and
		// the stack that belonged to it must not read as current.
		bridge->Running();

		result.SetValue(wxT("action"), action);
		result.SetValue(wxT("note"),
			ibMcpText("The runtime is going. Ask debug_state after the next stop is announced."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugRun);

//---------------------------------------------------------------------------
// debug_sessions
//---------------------------------------------------------------------------
//
// ⭐ WHAT THE DEBUG DIALOG SHOWS, ASKED INSTEAD OF LOOKED AT. The designer already keeps a live
// list of every runtime it can see - the scanner builds it, ibDialogDebugItem paints it, and
// picking a row calls AttachConnection. Both halves existed; neither had a door.
//
// ⭐ AND THE TYPE IS THE ANSWER, not decoration. A connection is a SCANNER (seen, not attached), a
// WAITER (a runtime holding still until somebody attaches) or a DEBUGGER (attached, stopping on
// breakpoints). Which one it is decides what to do next, so it is reported as a word rather than
// left to be inferred from whether commands work.
//
//---------------------------------------------------------------------------

namespace {

// The word for what a connection currently IS. The enum spells the mechanism; a caller acts on
// the state.
wxString ConnectionWord(ConnectionType type)
{
	switch (type) {
	case ConnectionType_Debugger: return wxT("attached");
	case ConnectionType_Waiter:   return wxT("waiting");
	case ConnectionType_Scanner:  return wxT("seen");
	default:                      return wxT("unknown");
	}
}

} // namespace

class ibMcpToolDebugSessions : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_sessions"); }

	wxString GetActivity(const ibDataNode& WXUNUSED(params)) const override
	{
		return ibMcpText("looking at what can be debugged");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Every runtime this designer can see, and whether the debugger is attached to it. "
			"A session is 'seen' (found by the scanner), 'waiting' (started with the debugger and "
			"holding still until something attaches) or 'attached' (breakpoints are live). Ask "
			"this before debug_breakpoint: a breakpoint set with nothing attached is set and never "
			"reached, which looks exactly like code that does not run.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& WXUNUSED(params), ibDataNode& result, wxString& refusal) const override
	{
		if (debugClient == nullptr) {
			refusal = ibMcpText("There is no debugger in this process.");
			return false;
		}

		std::vector<ibDataValue> sessions;
		int attached = 0;

		// ⚠ `auto`, NOT THE TYPE'S NAME. GetListConnection is public and hands the list out, but
		// the connection class itself is private to ibDebuggerClient - so the name cannot be
		// written here even though the objects can be read. Deducing the type asks nothing of the
		// name, and the alternative was widening a debugger header for a spelling.
		for (const auto* connection : debugClient->GetListConnection()) {

			if (connection == nullptr || !connection->IsConnected())
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

			entry->SetValue(wxT("host"), connection->GetHostName());
			entry->AddField(wxT("port"), ibDataValue::Int((s64)connection->GetPort()));
			entry->SetValue(wxT("state"), ConnectionWord(connection->GetConnectionType()));

			// WHOSE RUN THIS IS. On one machine it is noise; the moment two developers debug the
			// same base it is the only thing telling their runtimes apart.
			entry->SetValue(wxT("computer"), connection->GetComputerName());
			entry->SetValue(wxT("user"), connection->GetUserName());

			// ⚠ VERIFIED is not the same as connected: a socket that answered is not yet a
			// runtime that proved it speaks this protocol, and only a verified one can be
			// attached to.
			entry->AddField(wxT("ready"), ibDataValue::Bool(connection->IsVerifiedConnection()));

			if (connection->GetConnectionType() == ConnectionType_Debugger)
				attached++;

			sessions.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)sessions.size()));
		result.AddField(wxT("attached"), ibDataValue::Int((s64)attached));
		result.AddField(wxT("sessions"), ibDataValue::Array(sessions));

		if (sessions.empty())
			result.SetValue(wxT("note"),
				ibMcpText("Nothing is running to debug. app_run with debug: true starts one, and it "
				  "appears here a moment later - the scanner has to find it first."));
		else if (attached == 0)
			result.SetValue(wxT("note"),
				ibMcpText("Nothing is attached yet, so breakpoints will not be reached. debug_attach "
				  "takes one of these."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugSessions);

//---------------------------------------------------------------------------
// debug_attach
//---------------------------------------------------------------------------

class ibMcpToolDebugAttach : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_attach"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ArgDetach().Flag(params)
			? ibMcpText("letting a runtime go")
			: ibMcpText("attaching the debugger to a runtime");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Attach the debugger to a running application, or let one go - the same thing as "
			"picking a row in the designer's debug dialog. With no host and port it attaches to "
			"everything that is ready, which is what you want after app_run started exactly one. "
			"Breakpoints only stop a runtime the debugger is attached to.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgHost(), ArgPort(), ArgDetach(), ArgStop() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (debugClient == nullptr) {
			refusal = ibMcpText("There is no debugger in this process.");
			return false;
		}

		const wxString host = ArgHost().Text(params);
		const bool stop = ArgStop().Flag(params);

		// ⭐ STOPPING IS A DETACH THAT ALSO KILLS — one flag downstream, so `stop` cannot be asked
		// for while the connection is somehow left attached.
		const bool detach = stop || ArgDetach().Flag(params);

		const ibDataValue* portGiven = params.FindField(ArgPort().Name());

		// ⚠ HALF AN ADDRESS IS NOT AN ADDRESS. A host with no port would silently match nothing,
		// and the tool would report success over having done exactly nothing.
		if (!host.IsEmpty() && (portGiven == nullptr || portGiven->Kind() != ibDataKind::Number)) {
			refusal = ibMcpText("A host needs its port too - debug_sessions reports both. Omit both to "
				"take every ready runtime.");
			return false;
		}

		if (host.IsEmpty()) {

			if (detach) {
				refusal = stop
					? ibMcpText("Say which one to end - host and port, as debug_sessions reports them. "
						"Ending every runtime at once is not something to do by omission.")
					: ibMcpText("Say which one to let go - host and port, as debug_sessions reports "
						"them. Detaching everything at once is not something to do by omission.");
				return false;
			}

			// ⭐ THE ONE THE PLATFORM ALREADY HAS for the spawn-then-attach flow. Not a loop
			// written here: `AttachAllVerified` is what the web debug path uses, and two roads to
			// the same act would drift.
			debugClient->AttachAllVerified();

			result.AddField(wxT("attached"), ibDataValue::Bool(true));
			result.SetValue(wxT("note"),
				ibMcpText("Every ready runtime was taken. debug_sessions says which are now attached - "
				  "one that was not verified yet is simply not among them, and appears once it "
				  "is."));
			return true;
		}

		const unsigned short port = (unsigned short)portGiven->AsInt();

		if (detach)
			debugClient->DetachConnection(host, port, stop);
		else
			debugClient->AttachConnection(host, port);

		result.SetValue(wxT("host"), host);
		result.AddField(wxT("port"), ibDataValue::Int((s64)port));
		result.AddField(wxT("attached"), ibDataValue::Bool(!detach));

		if (stop)
			result.AddField(wxT("stopped"), ibDataValue::Bool(true));

		// ⚠ ASKED FOR, NOT CONFIRMED. Both of those calls find the connection by address and say
		// nothing when there is none, so this reports the REQUEST and points at the reading that
		// settles it. Claiming a state the call never returned would be the writer without a
		// reader all over again.
		result.SetValue(wxT("note"),
			ibMcpText("Asked. debug_sessions says whether it took - a runtime that had gone, or was not "
			  "verified yet, is not attached and does not say so here."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugAttach);

//---------------------------------------------------------------------------
// debug_sandbox
//---------------------------------------------------------------------------
//
// ⭐⭐ THE DOOR THAT WAS MISSING, AND IT IS NOT "RUN A SCRIPT". Everything built here today was
// built blind: a report can be composed, saved and applied without one number ever being seen,
// because reading data means a running application and writing test data means changing somebody's
// base. Both halves of that are the same missing thing — a place where code may run AND change
// nothing (Max, 2026-09-02: *"an isolated environment; you write test data, take the measurement,
// and it rolls back automatically"*).
//
// ⭐ AND IT RUNS INSIDE THE PERSON'S OWN SESSION, which is the point rather than a compromise. The
// situation to investigate — *"my total does not add up"* — exists in THEIR data, under THEIR
// rights, with THEIR forms open. A copy that behaves nearly the same is what makes a bug
// irreproducible. The rollback is what makes it acceptable to go in there at all: the person is
// insured by the transaction, and told, in their own window, that code is running.
//
// ⛔ AND THERE IS NO STEPPING INSIDE IT, deliberately. A breakpoint in sandbox code would mean a
// second debug loop nested in the parked one — the runtime stopped twice, one channel serving both
// — which is a great deal of machinery for a case where the code is the CALLER'S OWN and can be
// instrumented as freely as they like: `Message` between the lines says more than a step would, and
// it says it to the person watching as well (Max, 2026-09-02, weighing exactly this: *"debugging
// inside debugging… or you can just fill it with messages, and that is that"*).
//
// ⭐⭐ AND THE STOP IS THE INTERLOCK, NOT A LIMITATION. Code needs a session and a frame to run in,
// and both exist at a breakpoint — but the reason to insist on it is the other one: while the
// runtime is parked the PERSON cannot be changing the same data from the other side (Max,
// 2026-09-02: *"you probably have to be in the loop, so the user does not start changing something
// in parallel with you — the loop is the insurance"*). A sandbox running against a base somebody
// is actively typing into measures a moving target and rolls back over their work's shoulder.
class ibMcpToolDebugSandbox : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("debug_sandbox"); }

	// 🛑 NOT ON THE MAIN THREAD — the same reason debug_evaluate is not: this WAITS for an answer
	// that arrives through wxTheApp::CallAfter, and waiting on the thread that delivers it is a
	// deadlock rather than a slow call.
	bool NeedsMainThread() const override { return false; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("running code in a sandbox - everything it writes is rolled back");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("RUN CODE IN THE STOPPED RUNTIME AND UNDO IT. Whole statements, not an "
			"expression: build a query and set its parameters, walk what it returns, create a "
			"document, post it, read a register back, work out why a total is wrong. The query "
			"language and the global functions are written up in syntax_search and syntax_get, and "
			"the way a query is put together is a pattern of its own (pattern_read 'query-craft'). "
			"HOW TO GET SOMETHING BACK: a block returns no value of its own, so PRINT what you want "
			"- `Message(x)` for a line, `Message(SerializeValue(x))` for the whole structure of a "
			"selection, a record set or an array. Everything printed comes back in `printed`, and "
			"the person watching sees the same lines as they happen. "
			"SAY WHAT YOU ARE MEASURING, before and as you go: chat_say for what you are about to "
			"try and why, Message() inside the code for the figures as they come out. The person "
			"is watching their own application while somebody else's code runs in it, and a "
			"silent assistant is indistinguishable from a malfunction. "
			"It runs in the person's own session - their data, their rights, their open "
			"forms - INSIDE A TRANSACTION THAT IS ALWAYS ROLLED BACK, so nothing it writes "
			"survives, and code that commits on purpose is undone with the rest. The person at "
			"the application is told that code is running. Use `Result = ...` or return a value "
			"as the last statement to get something back. Needs the runtime stopped at a "
			"breakpoint: debug_state says whether it is.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgCode() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMcpDebugBridge* bridge = Bridge(refusal);
		if (bridge == nullptr)
			return false;

		const wxString code = ArgCode().Text(params);
		if (code.IsEmpty()) {
			refusal = ibMcpText("Nothing to run - pass the code.");
			return false;
		}

		if (!bridge->GetStop().m_stopped) {
			// ⭐⭐ AND THE WAY IN IS A CONVERSATION, not a sequence of calls. The person starts the
			// debugger knowing why - *"I need to check how this total is worked out; run it with
			// the debugger and I will stop where it is computed"* - and what follows happens in
			// their session with their consent (Max, 2026-09-02). So the refusal says the whole
			// road, including the part that is not a tool call.
			refusal = ibMcpText("The runtime is not stopped, and code can only run at a stop - that is "
				"where the session and the frame it runs in exist, and while it is parked the "
				"person cannot be changing the same data from the other side. Ask them to run the "
				"application with the debugger and say what you need to check; put a breakpoint "
				"where the figure is worked out (debug_breakpoint), have them reach it, and try "
				"again when debug_state says it is stopped.");
			return false;
		}

		bool ran = false;
		wxString answer, json;
		std::vector<wxString> printed;

		if (!bridge->Sandbox(code, ran, answer, json, printed)) {
			refusal = ibMcpText("The runtime did not answer in time. Whatever the code did was still "
				"rolled back - the transaction is on the far end and does not depend on this "
				"answer arriving.");
			return false;
		}

		result.AddField(wxT("ran"), ibDataValue::Bool(ran));

		if (!answer.IsEmpty())
			result.SetValue(wxT("answer"), answer);

		// ⭐⭐ THE RESULT AS A VALUE, beside the way it prints. `answer` is what a person would read;
		// this is what it is MADE OF — a selection's columns, a structure's fields — written by the
		// language's own SerializeValue. Both, because they answer different questions and the
		// printed form is the one that fits in a sentence.
		//
		// Absent when the value cannot travel, which is not a failure of the run: plenty of live
		// objects are not transferable by design, and `ran` already said what happened.
		if (!json.IsEmpty())
			result.SetValue(wxT("value"), json);

		// ⭐⭐ WHAT THE CODE PRINTED, WHICH IS HOW A BLOCK HANDS ANYTHING BACK. A block yields no
		// value by construction — the compiler puts no return in one — so `Message` is the way out,
		// and `Message(SerializeValue(x))` carries a whole structure through it. Collected here as
		// they arrived rather than read from the journal afterwards, where they would be mixed with
		// everything else the runtime said.
		if (!printed.empty()) {

			std::vector<ibDataValue> lines;
			for (const wxString& line : printed)
				lines.push_back(ibDataValue::String(line));

			result.AddField(wxT("printed"), ibDataValue::Array(lines));
		}

		// (⛔ THE FRAME'S LOCALS ARE NOT REPEATED HERE. They arrive with the stop and debug_state
		//  answers them at any time, and the sandbox's OWN variables never land in that frame — so
		//  a snapshot after the run would restate what the caller already has and still not show
		//  what it wants (Max, 2026-09-02). What the code computed and wants seen, it prints.)

		// ⭐ SAID ON EVERY ANSWER, not only the first. A caller that forgets this ran in a sandbox
		// reads an empty register back as a defect in the code it just tested - the write DID
		// happen, and then it was undone, which is the one thing that makes the reading different
		// from every other reading in this server.
		result.SetValue(wxT("rolledBack"),
			ibMcpText("Everything this wrote has been undone. Read anything you need INSIDE the same "
			  "code - a second call starts from the base as it was."));

		if (!ran)
			result.SetValue(wxT("note"),
				ibMcpText("The code did not run to the end - `answer` carries what the platform said "
				  "about it. messages_read has anything it printed before it stopped."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolDebugSandbox);
