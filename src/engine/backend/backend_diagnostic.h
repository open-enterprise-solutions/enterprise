#ifndef _BACKEND_DIAGNOSTIC_H__
#define _BACKEND_DIAGNOSTIC_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ibDiagnostic — one reported failure, as DATA
////////////////////////////////////////////////////////////////////////////
//
// WHY THIS EXISTS. A failure used to leave the engine as one assembled
// sentence — "{Module(42)}: Divide by zero" — and everything downstream had to
// read it the way a person does. The line number was in there, but as part of a
// phrase: changing the wording (or translating it) silently broke whoever was
// parsing it, and there was no way to tell a compile error from a runtime one
// except by looking.
//
// The position was never missing — it was just never assembled. The debug
// protocol has carried file / module / line as fields for a long time
// (ibDebugData, ibDebugLineData), and ibBackendException::ProcessError already
// takes every one of them as an argument. This type is those arguments given a
// name, so that the SAME failure can be:
//
//   * shown to a person      — the text is built FROM this, not instead of it;
//   * jumped to in the designer — module + line is an address, not a phrase;
//   * returned to a headless caller — a build step, a test, or an AI assistant
//     asking "compile this and tell me what is wrong", which is the one
//     consumer that cannot exist while the answer is prose.
//
// Delivery is a SINK with subscribers (ibDiagnostics below) rather than a
// return value: a failure is reported from deep inside the interpreter, where
// the stack between the error and whoever cares is not ours to change.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"

#include <vector>

#include <wx/string.h>

// WHAT KIND OF FAILURE. Not cosmetic: a compile error means the text was never
// run, a runtime error means it was — and a caller checking a module before
// saving it cares about exactly the first kind.
enum class ibDiagnosticKind {
	Compile,
	Runtime,
};

struct BACKEND_API ibDiagnostic {

	// ONE FRAME of the call stack, as data. The engine already formats these
	// into "3: ModuleName (#line 12)"; the pair is what that sentence is made
	// of, and a caller that wants to navigate needs the pair, not the sentence.
	struct Frame {
		wxString     m_module;
		unsigned int m_line = 0;   // 1-based, as a person counts
	};

	ibDiagnosticKind m_kind = ibDiagnosticKind::Runtime;

	// WHERE. Three fields because three different things answer "where":
	//   m_docPath   — the module's guid: the only one that is stable and the
	//                 only one the designer can navigate by;
	//   m_moduleName— what a person reads;
	//   m_fileName  — the external file (external report / data processor),
	//                 empty for a module inside the open configuration.
	wxString     m_fileName;
	wxString     m_moduleName;
	wxString     m_docPath;

	// 1-based line, the way the editor counts and the way the message shows it.
	unsigned int m_line = 0;
	// Offset into the module text — what FindErrorCodeLine walks to pull the
	// offending line out. Kept because a column can be derived from it later
	// without changing anything that produces a diagnostic today.
	unsigned int m_position = 0;

	// The engine's error code (ibBackendException::GetErrorDesc), or
	// wxNOT_FOUND when the failure carries only a description. Machine-readable
	// identity of the failure: a translated message changes, this does not.
	int          m_code = wxNOT_FOUND;

	// The failure itself, WITHOUT the "{Module(42)}: " decoration — that is
	// assembled for display and would be noise for anyone else.
	wxString     m_message;
	// The offending source line, when the module text was reachable.
	wxString     m_codeLine;

	std::vector<Frame> m_stack;

	bool IsOk() const { return !m_message.IsEmpty(); }
};

// WHO IS LISTENING. Implemented by whoever wants failures as data — a headless
// check, a test, a plugin (the AI-facing service is meant to be one).
class BACKEND_API ibDiagnosticSink {
public:
	virtual ~ibDiagnosticSink() = default;
	// Called on the thread that failed, inside the error path. Keep it short
	// and DO NOT throw: this runs while an exception is already in flight.
	virtual void OnDiagnostic(const ibDiagnostic& diagnostic) = 0;
};

// THE REGISTRY. Static and process-wide on purpose: an error is reported from
// wherever it happens — a job session, a compile in the designer, a background
// run — and a subscriber that had to be threaded through all of those would be
// threaded through none of them.
//
// Publishing is best-effort by construction: a sink that throws is swallowed
// (an exception is already unwinding), and no sinks at all is the ordinary case
// in the desktop client, where the dialog is the only consumer.
class BACKEND_API ibDiagnostics {
public:
	// Both are safe to call from any thread. Unsubscribe is by pointer identity
	// and must happen before the sink is destroyed.
	static void Subscribe(ibDiagnosticSink* sink);
	static void Unsubscribe(ibDiagnosticSink* sink);

	// Called by the error path. Cheap when nobody listens.
	static void Publish(const ibDiagnostic& diagnostic);

	static bool HasSubscribers();
};

#endif
