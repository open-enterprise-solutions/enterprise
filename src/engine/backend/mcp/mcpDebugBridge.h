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

	// ONE LINE THE RUN PRINTED, with the level it printed at. Kept as data rather than as a
	// sentence: "did the posting complain" is a filter, and a filter over prose is a guess.
	struct Printed {
		wxString    m_text;
		MessageType m_level = MessageType_Normal;
	};

	ibMcpDebugBridge();
	~ibMcpDebugBridge() override;

	// ⭐ THE RUN'S OUTPUT BUFFER — everything the application said since this was last emptied.
	// It is the assistant's copy: the person already has these lines in the application's own
	// window and does not want them in the designer, so nothing is announced as it arrives and the
	// buffer is read when there is a reason to.
	std::vector<Printed> TakeOutput(bool clear);

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

	// ⭐⭐ CODE, NOT AN EXPRESSION — run in the stopped runtime and, by default, UNDONE. The far end
	// wraps it in a transaction, so the base the person is using is not changed by an experiment run
	// inside their own session. Longer by default than an evaluation: this may write documents, post
	// them and read them back, which is work rather than a lookup.
	//
	// `keepWrites` COMMITS instead — the caller saying this run is meant to change the base rather
	// than to ask it something. Off unless asked for; a failed run is rolled back either way.
	//
	// `microseconds` comes back with the rest: how long the code itself took, timed in the process
	// that ran it. Measuring from this side would be measuring the socket and the wait.
	bool Sandbox(const wxString& code, bool keepWrites, bool& ran, wxString& answer, wxString& json,
		std::vector<wxString>& printed, wxLongLong_t& microseconds, int timeoutMs = 30000);

	// ⭐⭐ ASK THE RUNNING APPLICATION FOR A PICTURE OF ITS WINDOW, and wait for the answer. `reason`
	// is shown to the PERSON at that window, who decides — this is the one verb here whose outcome
	// belongs to somebody else, and `allowed` comes back false when they say no.
	//
	// ⚠ IT DOES NOT NEED A STOP. Everything else on this bridge speaks to a parked runtime; a window
	// draws itself while the application runs, which is exactly the moment somebody is looking at
	// the wrong list. Longer default than an evaluation: a person has to read the question first.
	bool Screenshot(const wxString& reason, const wxString& area, const wxString& format, bool& allowed,
		wxMemoryBuffer& bytes, wxString& focus, int timeoutMs = 60000);

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
	void OnEvalMessage(const wxString& message, MessageType type) override;
	void OnScreenshot(const wxMemoryBuffer& png, const wxString& focus) override;
	void OnSandboxResult(bool ran, const wxString& answer, const wxString& json, wxLongLong_t microseconds) override;

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
	enum class Pending { None, Value, Members, Sandbox, Picture };

	std::condition_variable m_answered;
	Pending                 m_pending = Pending::None;
	wxString                m_answer;
	bool                    m_sandboxRan = false;   // …and whether the sandbox got as far as running
	wxString                m_sandboxJson;          // …and its result AS A VALUE, when it could travel
	wxLongLong_t            m_sandboxMicroseconds = 0;  // …and how long it took, measured where it ran
	std::vector<wxString>   m_sandboxPrinted;       // …and every line it printed while it ran

	// What the RUN printed outside any sandbox — the application talking, kept for whoever asked
	// for the run. Trimmed from the front; see OnEvalMessage.
	std::vector<Printed>    m_output;

	// The picture asked for, and whether its owner allowed it at all — empty bytes with allowed
	// true would be a broken transfer, which is a different answer from "they said no".
	wxMemoryBuffer          m_picture;
	bool                    m_pictureAllowed = false;
	wxString                m_pictureFocus;   // …and what had the focus when it was taken
	std::vector<Local>      m_members;
	unsigned long long      m_nextWatchId = 1;
};

// The one instance, created with the MCP server. Null when the process has no
// MCP server running.
BACKEND_API ibMcpDebugBridge* ibMcpDebug();

#endif
