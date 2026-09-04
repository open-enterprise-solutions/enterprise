////////////////////////////////////////////////////////////////////////////
//	Description : the assistant's bridge onto a debugging session
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpDebugBridge.h"

#include "backend/appData.h"
#include "backend/debugger/debugClient.h"
#include "backend/mcp/mcpServer.h"

#include <chrono>

namespace {

// Null until a bridge attaches; the tools ask through this and refuse plainly
// when the process has no debugger at all.
ibMcpDebugBridge* g_debugBridge = nullptr;

} // namespace

ibMcpDebugBridge* ibMcpDebug()
{
	return g_debugBridge;
}

ibMcpDebugBridge::ibMcpDebugBridge()
{
}

ibMcpDebugBridge::~ibMcpDebugBridge()
{
	// The list that owns this object is what destroys it, so by the time we are
	// here we are already out of the fan-out. Only the global needs clearing.
	if (g_debugBridge == this)
		g_debugBridge = nullptr;
}

bool ibMcpDebugBridge::Attach()
{
	if (debugClient == nullptr)
		return false;

	debugClient->AddBridge(this);
	m_attached = true;
	g_debugBridge = this;
	return true;
}

void ibMcpDebugBridge::Detach()
{
	// RemoveBridge DESTROYS this object — the adapter owns what it holds. Nothing
	// may touch a member after the call, which is why it is the last statement.
	if (!m_attached || debugClient == nullptr)
		return;

	m_attached = false;
	debugClient->RemoveBridge(this);
}

ibMcpDebugBridge::Stop ibMcpDebugBridge::GetStop() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_stop;
}

void ibMcpDebugBridge::Running()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	// The stack and locals belonged to a moment that has passed. Keeping them
	// would let a reader take the previous stop for the current one, which is the
	// worst possible answer: plausible and wrong.
	m_stop.m_stopped = false;
	m_stop.m_stack.clear();
	m_stop.m_locals.clear();
	m_stop.m_module.Clear();
	m_stop.m_fileName.Clear();
	m_stop.m_line = 0;
}

bool ibMcpDebugBridge::Evaluate(const wxString& expression, wxString& answer, int timeoutMs)
{
	answer.Clear();

	if (debugClient == nullptr)
		return false;

	wxString module;
	wxString fileName;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_stop.m_stopped)
			return false;
		module = m_stop.m_module;
		fileName = m_stop.m_fileName;
		m_pending = Pending::Value;
		m_answer.Clear();
	}

	// The same road a tooltip travels when a developer hovers a variable: ask the
	// runtime, and the value comes back as an event. Nothing here is special-cased
	// for the assistant — it is hovering, by another name.
	debugClient->EvaluateToolTip(fileName, module, expression);

	std::unique_lock<std::mutex> lock(m_mutex);
	const bool arrived = m_answered.wait_for(lock,
		std::chrono::milliseconds(timeoutMs),
		[this]() { return m_pending == Pending::None; });

	if (arrived)
		answer = m_answer;

	m_pending = Pending::None;
	return arrived;
}

bool ibMcpDebugBridge::Sandbox(const wxString& code, bool keepWrites, bool& ran, wxString& answer,
	wxString& json, std::vector<wxString>& printed, wxLongLong_t& microseconds, int timeoutMs)
{
	ran = false;
	microseconds = 0;
	answer.Clear();
	json.Clear();
	printed.clear();

	if (debugClient == nullptr)
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_stop.m_stopped)
			return false;       // the runtime has to be parked: that is where the session is
		m_pending = Pending::Sandbox;
		m_answer.Clear();
		m_sandboxJson.Clear();
		m_sandboxPrinted.clear();
		m_sandboxRan = false;
		m_sandboxMicroseconds = 0;
	}

	debugClient->RunSandbox(code, keepWrites);

	std::unique_lock<std::mutex> lock(m_mutex);
	const bool arrived = m_answered.wait_for(lock,
		std::chrono::milliseconds(timeoutMs),
		[this]() { return m_pending == Pending::None; });

	if (arrived) {
		ran = m_sandboxRan;
		answer = m_answer;
		json = m_sandboxJson;
		microseconds = m_sandboxMicroseconds;
	}

	// HANDED OVER EITHER WAY. A run that timed out still printed whatever it printed before it
	// stopped, and those lines are the only account of how far it got.
	printed = m_sandboxPrinted;

	m_pending = Pending::None;
	return arrived;
}

bool ibMcpDebugBridge::Screenshot(const wxString& reason, const wxString& area, const wxString& format,
	bool& allowed, wxMemoryBuffer& png, wxString& focus, int timeoutMs)
{
	allowed = false;
	png.SetDataLen(0);
	focus.Clear();

	if (debugClient == nullptr)
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		// ⚠ NO STOP REQUIRED, and that is the difference from everything else here. A parked runtime
		// is needed to read a stack or evaluate in a session; a window can draw itself while the
		// application is running, which is precisely when somebody is looking at the wrong list.
		if (!m_stop.m_connected)
			return false;

		m_pending = Pending::Picture;
		m_picture.SetDataLen(0);
		m_pictureAllowed = false;
		m_pictureFocus.Clear();
	}

	debugClient->RequestScreenshot(reason, area, format);

	// ⭐ THE DEADLINE IS LONG BECAUSE A PERSON IS IN IT. What is being waited for is not a machine
	// answering but somebody reading a question and deciding — a three-second timeout would report
	// "no answer" while they were still looking at it.
	std::unique_lock<std::mutex> lock(m_mutex);
	const bool arrived = m_answered.wait_for(lock,
		std::chrono::milliseconds(timeoutMs),
		[this]() { return m_pending == Pending::None; });

	if (arrived) {
		allowed = m_pictureAllowed;
		focus = m_pictureFocus;
		if (m_picture.GetDataLen() > 0)
			png.AppendData(m_picture.GetData(), m_picture.GetDataLen());
	}

	m_pending = Pending::None;
	return arrived;
}

void ibMcpDebugBridge::OnScreenshot(const wxMemoryBuffer& png, const wxString& focus)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_pending != Pending::Picture)
			return;             // nobody is waiting — a late answer to a request that timed out

		// EMPTY MEANS THEY SAID NO. It is an answer, and it must not read as a failed transfer:
		// the difference decides whether asking again is reasonable or is pestering somebody who
		// has already declined.
		m_pictureAllowed = png.GetDataLen() > 0;
		m_pictureFocus = focus;

		m_picture.SetDataLen(0);
		if (png.GetDataLen() > 0)
			m_picture.AppendData(png.GetData(), png.GetDataLen());

		m_pending = Pending::None;
	}
	m_answered.notify_all();
}

bool ibMcpDebugBridge::Unfold(const wxString& expression, std::vector<Local>& members, int timeoutMs)
{
	members.clear();

	if (debugClient == nullptr)
		return false;

	// ⭐ THROUGH THE WATCH CHANNEL, which it could not use until 2026-08-31.
	//
	// A watch is registered as (expression, id), and THE ID IS A wxTreeItemId — a raw pointer to
	// the row in the requester's own window. It travels to the runtime and comes back, and every
	// listener used to render every answer by dereferencing it. Sound while exactly one thing can
	// ask; a crash the moment two can, because a number chosen here arrives at the IDE's watch
	// window as an address it never handed out. It took the designer down — 0x41, from an id of 1 —
	// and this asked nothing at all for a day rather than risk it again.
	//
	// What changed is that the QUESTION NOW CARRIES WHO ASKED (ibDebuggerClientBridge::GetBridgeId,
	// written into the packet and echoed back with the answer), so an answer reaches the bridge
	// that asked and nobody else. The id stops being a shared namespace and becomes what it always
	// meant: a handle in the asker's own world. Ours is a counter, and no one else ever sees it.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_stop.m_stopped)
			return false;
		m_pending = Pending::Members;
		m_members.clear();
	}

	// Registered, then expanded — two commands because the runtime answers them as two events, and
	// it is the SECOND that carries the parts (the first is the value itself).
	const unsigned long long id = m_nextWatchId++;

	debugClient->AddExpression(expression, id, GetBridgeId());
	debugClient->ExpandExpression(expression, id, GetBridgeId());

	std::unique_lock<std::mutex> lock(m_mutex);
	const bool arrived = m_answered.wait_for(lock,
		std::chrono::milliseconds(timeoutMs),
		[this]() { return m_pending == Pending::None; });

	if (arrived)
		members = m_members;

	m_pending = Pending::None;
	return arrived;
}

void ibMcpDebugBridge::Announce(const wxString& text) const
{
	// The wake-up road already exists: this is the same one a chat message
	// travels, so a stop reaches the assistant's window without a mechanism of
	// its own.
	if (appData == nullptr)
		return;

	if (ibMcpServer* server = appData->GetMcpServer())
		server->Say(text);
}

//---------------------------------------------------------------------------
// events
//---------------------------------------------------------------------------

void ibMcpDebugBridge::OnSessionStart(wxSocketClient* sock)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop.m_connected = true;
	}
	Announce(wxT("debugger: a runtime connected"));
}

void ibMcpDebugBridge::OnSessionEnd(wxSocketClient* sock)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = Stop();
	}
	Announce(wxT("debugger: the runtime disconnected"));
}

void ibMcpDebugBridge::OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	wxString where;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop.m_connected = true;
		m_stop.m_stopped = true;
		// The module arrives as its guid — the only thing stable enough to send.
		// A reader wants the name, and the stack that follows carries it, so the
		// guid is kept as the address and the name is filled in from the stack.
		m_stop.m_module = data.m_moduleName;
		m_stop.m_fileName = data.m_fileName;
		m_stop.m_line = data.m_line;

		// The stack and the locals arrive as their own events, right behind this
		// one. Clearing here means a reader that asks in between gets "stopped,
		// nothing yet" rather than the previous stop's frames.
		m_stop.m_stack.clear();
		m_stop.m_locals.clear();

		where = data.m_moduleName;
	}

	// THE WAKE-UP. Without it, stopping on a breakpoint is a state nobody is told
	// about, and the assistant would have to poll to find out it had happened.
	Announce(wxString::Format(wxT("debugger: stopped at %s line %u"),
		where, data.m_line));
}

void ibMcpDebugBridge::OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	Running();
}

void ibMcpDebugBridge::OnAutoComplete(const ibDebugAutoCompleteData& data)
{
	// Completion in the debuggee's own scope is answered elsewhere; nothing here
	// needs it, and pretending to keep it would be state nobody reads.
}

void ibMcpDebugBridge::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop.m_message = message;

		// (⛔ AN ORDINARY MESSAGE IS NOT A SANDBOX'S OUTPUT. What a sandbox prints comes up the EVAL
		//  channel — OnEvalMessage below — because evaluated code is silent to the window by
		//  design. Collecting here would gather whatever the application happened to say at the
		//  same moment and call it the run's result.)
	}
	// READ BY BOTH OF US. This road ends in the designer's output pane, where the person is; the
	// assistant is told as well because it is usually the one that asked for the run.
	Announce(wxT("the run reported a failure: ") + message);
}

void ibMcpDebugBridge::OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_pending != Pending::Value)
			return;             // nobody asked — a tooltip the developer hovered
		m_answer = resultStr;
		m_pending = Pending::None;
	}
	m_answered.notify_all();
}

// ⭐⭐ WHAT EVALUATED CODE PRINTED. This is the sandbox's own voice: a block returns no value of
// its own, so printing is how it hands anything back, and `Message(SerializeValue(x))` carries a
// whole structure through here. Kept while a run is pending, so the answer to THAT run is what it
// printed and not whatever else was said meanwhile.
void ibMcpDebugBridge::OnEvalMessage(const wxString& message, MessageType type)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		// DURING A SANDBOX THE LINES ARE ITS OWN OUTPUT, and they are handed back with its result —
		// in the order they were printed, which is how a long experiment narrates itself.
		if (m_pending == Pending::Sandbox) {
			m_sandboxPrinted.push_back(message);
			return;
		}
	}

	// ⭐⭐ AND OUTSIDE ONE, THIS IS THE RUN'S OUTPUT BUFFER, KEPT — not announced. Everything not
	// arriving inside a sandbox used to fall on the floor, so a `Message` from a posting handler —
	// the ordinary way a configuration reports what it did — reached nobody at all.
	//
	// ⚠ KEPT RATHER THAN SAID, and that is the whole design (Max, 2026-09-04: *"this is you reading
	// the output buffer; I do not need it — I read only the errors, when I press the button myself.
	// The buffer you can show yourself"*). A line per message would wake somebody for every
	// "posted 12 documents"; a buffer is read when there is a reason to read it, and what is worth
	// repeating to the person is then a decision rather than a reflex.
	std::lock_guard<std::mutex> lock(m_mutex);

	Printed line;
	line.m_text  = message;
	line.m_level = type;
	m_output.push_back(line);

	// A window, not a history: a long run would otherwise grow this without bound, and what matters
	// is what it said RECENTLY. Oldest out first, in one block so the trimming is not per-line work.
	if (m_output.size() > 500)
		m_output.erase(m_output.begin(), m_output.begin() + 100);
}

std::vector<ibMcpDebugBridge::Printed> ibMcpDebugBridge::TakeOutput(bool clear)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::vector<Printed> out = m_output;
	if (clear)
		m_output.clear();

	return out;
}

void ibMcpDebugBridge::OnSandboxResult(bool ran, const wxString& answer, const wxString& json,
	wxLongLong_t microseconds)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_pending != Pending::Sandbox)
			return;             // nobody is waiting — a late answer to a request that timed out
		m_sandboxRan = ran;
		m_answer = answer;
		m_sandboxJson = json;
		m_sandboxMicroseconds = microseconds;
		m_pending = Pending::None;
	}
	m_answered.notify_all();
}

void ibMcpDebugBridge::OnSetStack(const ibStackData& stackData)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_stop.m_stack.clear();
	for (const ibStackData::ibStackRow& row : stackData.m_stackData) {
		Frame frame;
		frame.m_module = row.m_moduleName;
		frame.m_line = row.m_moduleLine;
		m_stop.m_stack.push_back(frame);
	}

	// THE TOP FRAME NAMES WHERE WE ARE, in words. Kept BESIDE the module rather
	// than over it: the module field is the ADDRESS the runtime answers to, and
	// an expression is evaluated against it — overwriting it with the readable
	// name would make the stop legible and the evaluation broken.
	if (!m_stop.m_stack.empty())
		m_stop.m_where = m_stop.m_stack.front().m_module;
}

void ibMcpDebugBridge::OnSetLocalVariable(const ibLocalWindowData& watchData)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_stop.m_locals.clear();
	for (const ibLocalWindowData::ibLocalWindowItem& item : watchData.m_listExpression) {
		Local local;
		local.m_name = item.m_name;
		local.m_value = item.m_value;
		local.m_type = item.m_type;

		// WHETHER IT CAN BE OPENED FURTHER is half the answer. A value that says
		// "Catalog.Goods" and nothing else is a dead end unless the reader knows it
		// may be asked to unfold.
		local.m_hasAttributes = item.m_hasAttributes;
		m_stop.m_locals.push_back(local);
	}
}

void ibMcpDebugBridge::OnSetVariable(const ibWatchWindowData& watchData)
{
	// A registered watch answered. Recorded only when somebody is waiting for
	// structure — the expansion that follows usually says more, and overwrites it.
	RecordMembers(watchData, false);
}

void ibMcpDebugBridge::OnSetExpanded(const ibWatchWindowData& watchData)
{
	// The parts of a value. THIS is the answer that makes a reference useful:
	// "Catalog.Goods" as text says nothing, its attributes say what it holds.
	RecordMembers(watchData, true);
}

void ibMcpDebugBridge::RecordMembers(const ibWatchWindowData& watchData, bool complete)
{
	// ⭐ IS THIS OUR ANSWER — asked of the packet, not of our own mood. Every bridge on the session
	// is handed every answer, and "we happen to be waiting" is not the same question: the
	// developer's watch answering while we wait would be taken for ours. The asker's identity rode
	// out with the question and comes back stamped on the answer.
	if (!(watchData.GetBridgeId() == GetBridgeId()))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_pending != Pending::Members)
			return;             // an answer to a question we are no longer waiting on

		m_members.clear();
		for (const ibWatchWindowData::ibWatchWindowItem& item : watchData.m_listExpression) {
			Local member;
			member.m_name = item.m_name;
			member.m_value = item.m_value;
			member.m_type = item.m_type;
			member.m_hasAttributes = item.m_hasAttributes;
			m_members.push_back(member);
		}

		// The first answer is the value itself; the waiter is released by the
		// expansion, which is the one that carries the parts.
		if (!complete)
			return;

		m_pending = Pending::None;
	}
	m_answered.notify_all();
}
