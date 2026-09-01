#ifndef __IB_MCP_DEBUG_BRIDGE_H__
#define __IB_MCP_DEBUG_BRIDGE_H__

#include "backend/backend.h"
#include "backend/debugger/debugClientBridge.h"
#include "backend/debugger/debugDefs.h"

#include <condition_variable>
#include <mutex>
#include <vector>

// THE ASSISTANT'S OWN BRIDGE ONTO A DEBUGGING SESSION SOMEBODY ELSE IS DRIVING.
//
// The IDE installs its bridge and paints its windows from it. This one is the
// same interface, added as an OBSERVER, so a developer opens a module, runs the
// application and stops on a breakpoint exactly as before — and the assistant
// sees the same stop, the same stack and the same locals, without taking
// anything away from the windows.
//
// WHY IT HOLDS STATE. The debugger speaks in EVENTS and a tool answers a
// QUESTION. "Where are we stopped" has no event; it is the standing consequence
// of the last OnEnterLoop, and something has to remember it between the moment
// it arrives and the moment it is asked for. So this is the memory of the last
// stop: where, which stack, which locals — replaced wholesale each time the
// runtime stops, and cleared when it runs on.
//
// WHY IT ALSO WAITS. Reading a value is a round trip: ask, and the answer comes
// back later through OnSetToolTip on another thread. A tool that returned
// immediately would answer "asked" instead of the value, so the request waits on
// the reply with a deadline — and says plainly when the deadline is what it got.
class BACKEND_API ibMcpDebugBridge : public ibDebuggerClientBridge {
public:

	struct Frame {
		wxString     m_module;
		unsigned int m_line = 0;
	};

	struct Local {
		wxString m_name;
		wxString m_value;
		wxString m_type;
		bool     m_hasAttributes = false;
	};

	// Everything known about the current stop, taken under one lock so a reader
	// never sees a stack from one stop beside locals from the next.
	struct Stop {
		bool               m_connected = false;
		bool               m_stopped = false;
		// TWO ANSWERS TO "WHERE", and they are not interchangeable. m_module is the
		// ADDRESS the runtime answers to — a guid, what an evaluation is sent
		// against. m_where is what a person reads, filled in from the top of the
		// stack: "Document.GoodsIssue.ObjectModule.Posting(...)".
		wxString           m_module;
		wxString           m_where;
		wxString           m_fileName;
		unsigned int       m_line = 0;
		std::vector<Frame> m_stack;
		std::vector<Local> m_locals;
		wxString           m_message;      // last message the runtime sent up
	};

	ibMcpDebugBridge();
	~ibMcpDebugBridge() override;

	// Installs itself as an observer on the debug client, if there is one. Safe
	// to call when the process has no debugger at all — answers false.
	bool Attach();
	void Detach();

	Stop GetStop() const;

	// Ask the runtime for the value of an expression and WAIT for the answer.
	// Returns false on timeout, with `answer` left empty — a deadline is a fact
	// worth reporting, not a value worth inventing.
	bool Evaluate(const wxString& expression, wxString& answer, int timeoutMs = 3000);

	// THE SAME QUESTION, ASKED FOR STRUCTURE. Evaluate answers what a value LOOKS
	// like; this answers what it IS MADE OF — the members with their own values
	// and types, which is what a reference or a record set has instead of a
	// printable form. Same road the watch window uses.
	bool Unfold(const wxString& expression, std::vector<Local>& members, int timeoutMs = 3000);

	// Forgets the stop. Called when the runtime is told to continue, so a stale
	// stack cannot be read back as the current one.
	void Running();

	// ibDebuggerClientBridge
	void OnSessionStart(wxSocketClient* sock) override;
	void OnSessionEnd(wxSocketClient* sock) override;
	void OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data) override;
	void OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data) override;
	void OnAutoComplete(const ibDebugAutoCompleteData& data) override;
	void OnMessageFromServer(const ibDebugLineData& data, const wxString& message) override;
	void OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr) override;
	void OnSetStack(const ibStackData& stackData) override;
	void OnSetLocalVariable(const ibLocalWindowData& watchData) override;
	void OnSetVariable(const ibWatchWindowData& watchData) override;
	void OnSetExpanded(const ibWatchWindowData& watchData) override;

private:

	// Tells the assistant's window that the runtime has stopped. The wake-up road
	// already exists — this is the same one a chat message travels.
	void Announce(const wxString& text) const;

	// Both watch events carry the same shape; `complete` says whether this one
	// ends the wait.
	void RecordMembers(const ibWatchWindowData& watchData, bool complete);

	mutable std::mutex      m_mutex;
	Stop                    m_stop;
	bool                    m_attached = false;

	// The pending question: one at a time, which is all a stopped runtime can
	// meaningfully answer anyway. Two kinds because the runtime answers them
	// through two different events, and mixing them would let a tooltip satisfy
	// a request for structure.
	enum class Pending { None, Value, Members };

	std::condition_variable m_answered;
	Pending                 m_pending = Pending::None;
	wxString                m_answer;
	std::vector<Local>      m_members;
	unsigned long long      m_nextWatchId = 1;
};

// The one instance, created with the MCP server. Null when the process has no
// MCP server running.
BACKEND_API ibMcpDebugBridge* ibMcpDebug();

#endif
