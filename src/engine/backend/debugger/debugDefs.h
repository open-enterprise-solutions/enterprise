#ifndef _DEBUGGER_DEFS_H__
#define _DEBUGGER_DEFS_H__

// ⭐ HOW LOUD A REPORTED LINE WAS SAID — the PROTOCOL's own word for it, which is why this file
// defines one instead of including the platform's `ibStatusMessage`. The wire is a contract between
// two processes and must not move when an application enum gains a member or renumbers; the two
// ends translate, and translating is three lines each, at the exact points where one vocabulary
// meets the other.
//
// (It sat here declared and unused until 2026-09-04, when a run's output first started travelling
// to whoever is debugging it and the level had to travel with it.)
enum MessageType
{
	MessageType_Normal = 0,
	MessageType_Warning = 1,
	MessageType_Error = 2,
};

enum CodeState
{
	CodeState_Normal = 0,    // The code is normal.
	CodeState_Unavailable = 1,    // The code for the script was not available.
	CodeState_Binary = 2,    // The code was loaded as a binary/compiled file
};

enum EventId
{
	EventId_SessionStart = 1,   // Sent when the backend is ready to have its initialize function called
	EventId_SessionEnd = 2,    // This is used internally and shouldn't be sent.
	EventId_EnterLoop = 3,     // When enter to loop in enterprise mode 
	EventId_LeaveLoop = 4,      // When leave from loop in enterprise mode 

	EventId_SetToolTip = 5,      // When activate tooltip 
	EventId_StartAutocomplete = 6,      // When activate autocomplete 
	EventId_ShowAutocomplete = 7,      // When activate autocomplete 
	EventId_SetData = 8,      // When set data 

	EventId_MessageFromEnterprise = 9,      // Message from enterprise
};

enum CommandId
{
	CommandId_VerifyConnection = 1, // verify configuration before connection
	CommandId_SetConnectionType = 2, // return if connection not passed verify 

	CommandId_StartSession = 3, // If verify is ok then start debugging 
	CommandId_Continue = 4,    // Continues execution until the next break point.
	CommandId_StepOver = 5,    // Steps to the next line, not entering any functions.
	CommandId_StepInto = 6,    // Steps to the next line, entering any functions.
	CommandId_ToggleBreakpoint = 7,    // Toggles a breakpoint on a line on and off.
	CommandId_RemoveBreakpoint = 8,    // Remove a breakpoint on a line on and off.
	CommandId_Pause = 9,    // Instructs the debugger to break on the next line of script code.
	CommandId_Detach = 10,    // Soft stop: disconnect debug client + reset debug state, listener stays so the debugger can reattach.
	CommandId_Destroy = 11,    // Hard stop: same as Detach plus a forced close of the stopped session (a thin client is told to go). The process's other sessions stay, unless the process lives for the debugger (ibStopProcess: a web server a designer started; so does Detach there).

	// 12–14 — the editor's patches (insert, delete, complete), which no runtime read: the edits are the editor's to count.

	CommandId_DeleteAllBreakpoints = 15,// Instructs the backend to clear all breakpoints set

	CommandId_EnterLoop = 16,// Instructs the backend to enter debug loop
	CommandId_LeaveLoop = 17,// Instructs the backend to leave debug loop

	CommandId_AddExpression = 18, // Get variables from enterprise 
	CommandId_ExpandExpression = 19, // Expand variable from enterprise 
	CommandId_RemoveExpression = 20, // Remove variable from enterprise 

	CommandId_GetArrayBreakpoint = 22, // Wait breakpoits from designer
	CommandId_SetArrayBreakpoint = 23, // Set breakpoits to enterprise

	CommandId_SetStack = 24, // Set up stack in designer 
	CommandId_SetLocalVariables = 25, // Set up local variables to designer 
	CommandId_SetExpressions = 26, //Set up all expressions

	CommandId_EvalToolTip = 27,
	CommandId_EvalAutocomplete = 28,

	CommandId_MessageFromServer = 29, // When catch error in enterprise mode

	// ⭐⭐ ARBITRARY CODE, RUN AND THEN UNDONE. The runtime is stopped at a breakpoint with its
	// session, its data and its OPEN FORMS — everything an experiment needs — and the one thing
	// missing was permission to change anything, because a change is permanent and the base is
	// somebody's. A transaction that always rolls back removes the permanence, and with it the
	// reason to refuse (Max, 2026-09-02: *"an isolated environment - you write test data through
	// the debugger, take the measurement, and it rolls back automatically; the mechanism is built
	// right in"*).
	//
	// Not a new evaluator: `ibProcUnit::Evaluate` already compiles a BLOCK rather than an
	// expression when asked to (`compileBlock`), and the tooltip road has been carrying answers
	// back since before this. What is new here is the transaction around it and nothing else.
	CommandId_RunSandbox = 30,

	// ⭐⭐ WHAT CODE PRINTS WHILE IT IS BEING EVALUATED — a channel of its own, and that is the
	// point. `Message` returns at its first line when the process is in EVAL MODE
	// (ibBackendException::IsEvalMode), which is right: a watch expression, an autocomplete probe
	// and a tooltip must not talk to the person, or hovering over a variable would fill their
	// window. The sandbox is evaluation too — so everything it printed went nowhere at all.
	//
	// Sending those lines up the ORDINARY message road instead would work and would also mean
	// every message of a running application storms the debugger (Max, 2026-09-02, weighing it).
	// So they travel apart: the assistant that asked for the run reads them, the designer drops
	// them, and the person is shown whatever the assistant decides is worth showing.
	CommandId_EvalMessage = 31,

	// ⭐⭐ "SEND ME A PICTURE OF WHAT YOU ARE SEEING" — asked of the running application through the
	// designer, answered with the window as PNG bytes. The hardest reports to act on are the ones a
	// person cannot phrase: "the list displays wrongly", "the numbers do not add up". Which form,
	// which period, which column — all of it is in one image and none of it is in the sentence.
	//
	// ⭐ IT DOES NOT NEED A BREAKPOINT. Every other command here speaks to a runtime that is PARKED;
	// this one asks a window to draw itself, which a running application can do at any moment. That
	// is the point — the person is looking at the wrong list right now, not at a stop.
	//
	// 🛑 AND IT IS NEVER SILENT: the far end asks its user, showing them why, and a refusal comes
	// back as empty bytes. A screen holds whatever happened to be open, and no caller on this side
	// may photograph it by deciding to.
	CommandId_Screenshot = 32,

	// ⭐⭐ PUTTING DATA IN — the sandbox's road, WITHOUT the sandbox's stop, and keeping what it
	// wrote. Three commands rather than one with a mode, because they are three different requests
	// about one run; they share a REPLY shape, since all three answer with the state of that run.
	//
	// ⭐⭐ WHY IT COMES OVER THIS WIRE AT ALL, and it is not a preference. The MCP server lives in
	// the DESIGNER, and a designer builds no runtime for anybody: `ibSession::EnsureRoot` returns
	// early on `appData->DesignerMode()` — "Designer never executes script". So a background job
	// started there gets a session with no root module and nothing to call, every time. The runtime
	// is in the application, on the other side of this socket, and that is the only reason a fill
	// travels as a debugger command.
	//
	// ⭐ AND IT NEEDS NO BREAKPOINT, which is the difference from CommandId_RunSandbox and the same
	// exemption CommandId_Screenshot already has. A sandbox borrows the PARKED session's frame and
	// connection, so without a stop it has neither; a fill asks for a background job in its own
	// session, which a running application can start at any moment. So these are deliberately not
	// guarded by IsEnterLoop / IsDebugLooped.
	//
	// ⚠ WHICH IS ALSO WHY A FILL CANNOT BE STEPPED THROUGH (Max, 2026-09-06: "you will check it, but
	// you will not be able to debug it"). Parking nobody and having a frame to inspect are one
	// choice, not two features. Whoever needs to step through the code sends it as a SANDBOX
	// instead, at a stop, and pays for it with the person's session waiting.
	//
	// ⭐⭐ AND WHAT TRAVELS ON FillStart IS COMPILED BYTECODE, not a name and not source — the only
	// command on this wire that carries a program. Having no frame here to compile against was first
	// read as "so the caller must name a procedure the configuration already has", which made trying
	// a fill require editing somebody's configuration. The designer has the compiler; it compiles,
	// and the far end runs what arrives (Max, 2026-09-06: *"you can pass the bytecode you compiled
	// and send it there so it executes there — what's the problem?"*). Calls inside it bind at THIS
	// end by module-descriptor guid, versions checked, so a program built against a configuration
	// this application is not running is refused rather than answered wrongly.
	CommandId_JobStart  = 33,
	CommandId_JobStatus = 34,
	CommandId_JobCancel = 35,

	// ⭐⭐ READING A REPORT, and it is here for a reason of RIGHTS rather than of plumbing (Max,
	// 2026-09-06: *"we do not work with data in the designer, we get it through the wire — and the
	// wire is what the user sees, that they really gave access"*). A designer holds a CONFIGURATION;
	// the rows belong to the application somebody started, and starting it with the debugger
	// attached is the visible act that grants access. Reading them over the designer's own
	// connection would take them by deciding to — which is what the screenshot command already
	// refuses to do with a window.
	//
	// ⭐⭐ WHAT CROSSES IS A SCHEMA, NOT THE NAME OF ONE, and that single choice is what makes the
	// interesting case possible: the caller may send a composition that EXISTS NOWHERE. Assembling
	// one and running it is then the same act as running a report — the far end builds a composer
	// out of what arrived and has nothing to look up. Both directions carry a serialised node, so
	// the payload is the tool's own request and the tool's own answer.
	//
	// ⚠ NO STOP REQUIRED either: what happens over there is a rented read, and a running application
	// can start one whenever. It does not block the person — the rental is a connection of its own.
	CommandId_Compose = 36,

	CommandId_StepOut = 37,   // Runs to the first line of the frame that called this one; from the outermost frame, as Continue.
};

enum ConnectionType {

	ConnectionType_Scanner,
	ConnectionType_Waiter,
	ConnectionType_Debugger,

	ConnectionType_Unknown = 100
};

/////////////////////////////////////////////////////////////////////////////////////////////

struct ibDebugData {
	// Identifies which ibSession (web tab or main desktop) the event
	// came from / should be routed to. Empty on protocol-level packets
	// (verify, set-connection-type) — only event/command packets that
	// touch a specific runtime carry it.
	ibString m_sessionGuid;
	ibString m_fileName;
	ibString m_moduleName;
};

struct ibDebugLineData : public ibDebugData {
	unsigned int m_line;
};

struct ibDebugExpressionData : public ibDebugData {
	ibString m_expression;
};

struct ibDebugAutoCompleteData : public ibDebugExpressionData {

	struct ibDebugVariableData {
		ibString m_variableName;
	};

	std::vector<ibDebugVariableData> m_arrVar;

	struct ibDebugMethodData {
		ibString m_methodName;
		ibString m_methodHelper;
		bool m_methodRet;
	};

	std::vector<ibDebugMethodData> m_arrMeth;

public:

	ibString m_keyword;
	int      m_currentPos;
};

///////////////////////////////////////////////////////////////////////////////////////////// 

struct ibStackData {

	struct ibStackRow {
		ibString m_moduleName;      // the module's address, as ibDebugData's — what its line is counted in
		ibString m_fullName;        // the call as the runtime names it: the module, the procedure, its parameters
		unsigned int m_moduleLine;
	public:
		ibStackRow(const ibString& strModuleName, const ibString& strFullName, unsigned int moduleLine) :
			m_moduleName(strModuleName), m_fullName(strFullName), m_moduleLine(moduleLine) {
		}
	};

	std::vector<ibStackRow> m_stackData;

public:

	void AppendStack(const ibString& strModuleName, const ibString& strFullName, unsigned int moduleLine) {
		m_stackData.emplace_back(
			strModuleName, strFullName, moduleLine
		);
	}

	unsigned int GetStackCount() const {
		return m_stackData.size();
	}

	ibString GetFullName(unsigned int idx) const {
		if (idx >= m_stackData.size()) return ibEmptyString;
		return m_stackData[idx].m_fullName;
	}

	ibString GetModuleLine(unsigned int idx) const {
		if (idx >= m_stackData.size()) return ibEmptyString;
		return ibString(std::to_wstring(m_stackData[idx].m_moduleLine));
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>   // std::uint64_t — a watch's row, the number the wire carries

struct ibLocalWindowData {

	struct ibLocalWindowItem {

		ibString	 m_name;
		ibString	 m_value;
		ibString	 m_type;
		bool		 m_hasAttributes;

		ibLocalWindowItem(const ibString& n, const ibString& v, const ibString& t, bool a) :
			m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<ibLocalWindowItem> m_listExpression;

public:

	ibLocalWindowData() {}

	void AddLocalVar(const ibString& name, const ibString& value, const ibString& type, bool hasAttributes) {
		m_listExpression.emplace_back(name, value, type, hasAttributes);
	}

	ibString GetName(unsigned int idx) const {
		return m_listExpression[idx].m_name;
	}

	ibString GetValue(unsigned int idx) const {
		return m_listExpression[idx].m_value;
	}
	ibString GetType(unsigned int idx) const {
		return m_listExpression[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_listExpression[idx].m_hasAttributes;
	}

	unsigned int GetVarCount() const {
		return m_listExpression.size();
	}
};

struct ibWatchWindowData {

	// The row asked about, numbered by the asker (a pointer of its tree) and said back by the runtime — a number
	// nothing on this side takes apart.
	std::uint64_t m_item;

	struct ibWatchWindowItem {

		std::uint64_t m_item;
		ibString	 m_name;
		ibString	 m_value;
		ibString	 m_type;
		bool		 m_hasAttributes;

		ibWatchWindowItem(const ibString& n, const ibString& v, const ibString& t, bool a, std::uint64_t i) :
			m_item(i), m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<ibWatchWindowItem> m_listExpression;

	// ⭐⭐ WHO ASKED FOR THIS. `m_item` above is a handle in the ASKER's own world — a row of its
	// tree — so an answer is only meaningful to the listener that produced it, and every other one
	// must let it pass.
	//
	// ⚠ A STRING, not the guid type. This header is the protocol's vocabulary and is included
	// everywhere the debugger is; it carries what goes over the wire, and over the wire this is
	// text. Naming the type here would drag the guid header behind it into every one of them for a
	// value nothing on this side takes apart.
	ibString m_bridgeId;

public:

	ibWatchWindowData(std::uint64_t item = 0) : m_item(item) {}

	const ibString& GetBridgeId() const { return m_bridgeId; }
	void SetBridgeId(const ibString& id) { m_bridgeId = id; }

	std::uint64_t GetItem() const {
		return m_item;
	}

	void AddWatch(const ibString& name, const ibString& value, const ibString& type, bool hasAttributes) {
		m_listExpression.emplace_back(name, value, type, hasAttributes, m_item);
	}

	void AddWatch(const ibString& name, const ibString& value, const ibString& type, bool hasAttributes, std::uint64_t item) {
		m_listExpression.emplace_back(name, value, type, hasAttributes, item);
	}

	std::uint64_t GetItem(unsigned int idx) const {
		return m_listExpression[idx].m_item;
	}

	ibString GetName(unsigned int idx) const {
		return m_listExpression[idx].m_name;
	}

	ibString GetValue(unsigned int idx) const {
		return m_listExpression[idx].m_value;
	}
	ibString GetType(unsigned int idx) const {
		return m_listExpression[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_listExpression[idx].m_hasAttributes;
	}

	unsigned int GetWatchCount() const {
		return m_listExpression.size();
	}
};

// ⭐⭐ A WATCHED EXPRESSION AND WHO ASKED FOR IT — what BOTH ends keep about one row of a watch window,
// for the same reason and under one name. It is not a shape that travels: each end registers what it
// knows so it can say the name again later. The client re-registers everything after a reconnect
// (CommandId_GetArrayBreakpoint); the runtime refreshes everything at every stop (SendExpressions), and
// an answer only reaches the listener it is addressed to — every other one lets it pass
// (ibWatchWindowData::GetBridgeId below).
//
// 🛑 KEPT AS THE TEXT ALONE, both ends had nobody to name when the moment came: the refresh wrote its
// item count where the reader expects the asker, and the re-registration left the field out entirely. A
// frame read one field out of step walks off its own end, and the side reading it closes the connection
// rather than parse rubbish — "the debugger detaches by itself", deterministically, on the first step
// with anything in the watch window (2026-09-25).
struct ibWatchedExpression {
	ibString m_asker;        // the bridge that asked — empty means nobody is listening for it
	ibString m_expression;
};

/////////////////////////////////////////////////////////////////////////////////////////////

// The ports — ibPort::Debugger: a debug server listens on the first free one, the designer looks in all of them.
#include "core/net/ports.h"
#define defaultHost ibT("localhost")

/////////////////////////////////////////////////////////////////////////////////////////////
#define numberOfConnectionAttempts 1
// A launched child (SearchServer(true)) — it waits 30 s for its designer (ibDebuggerServer::CreateServer), and an attempt
// on a port nobody listens on yet takes ~2 s on Windows.
#define numberOfLaunchConnectionAttempts 15
/////////////////////////////////////////////////////////////////////////////////////////////

#define waitDebuggerTimeout 50 //msec

/////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

// ⭐ WHAT ONE SOCKET SAID, FOR THE THREAD THAT READS IT — its commands in the order they came, and its going. One per
// socket, at either end: the far end's commands are dispatched on the connection's own thread (RecvCommand), never on
// the socket's. Held by the socket's handlers as much as by the connection — a handler may be told after the
// connection let the socket go, and what it says then lands here, read by nobody.
class ibDebugCommandQueue {
public:

	// A command, whole — from the socket's thread.
	void Push(std::string command)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queue.push_back(std::move(command));
		}
		m_cv.notify_one();
	}

	// The socket is gone; `reason` — why, empty when either end simply closed it. What it said before is still popped.
	void Stop(const std::string& reason)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stop = true;
			m_reason = reason;
		}
		m_cv.notify_one();
	}

	// The next command, waiting up to `maxWaitMs` — or false: none came, or the socket has gone and nothing it said is
	// left (IsStopped).
	bool Pop(std::string& command, int maxWaitMs)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_cv.wait_for(lock, std::chrono::milliseconds(maxWaitMs), [this]() { return !m_queue.empty() || m_stop; });
		if (m_queue.empty())
			return false;
		command = std::move(m_queue.front());
		m_queue.pop_front();
		return true;
	}

	bool IsStopped() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_stop;
	}

	std::string GetReason() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_reason;
	}

private:
	mutable std::mutex      m_mutex;
	std::condition_variable m_cv;
	std::deque<std::string> m_queue;
	bool                    m_stop = false;
	std::string             m_reason;
};

/////////////////////////////////////////////////////////////////////////////////////////////

#endif 