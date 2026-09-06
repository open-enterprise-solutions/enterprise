#ifndef __IB_JOB_RUN_BYTE_CODE_H__
#define __IB_JOB_RUN_BYTE_CODE_H__

////////////////////////////////////////////////////////////////////////////
//	Description : RUN ARBITRARY CODE AS A BACKGROUND JOB, on the side that
//	              has a runtime - compiled here, wrapped in one transaction,
//	              then watched and stopped by token.
////////////////////////////////////////////////////////////////////////////
//
// ⚠ IT IS NOT "DATA FILLING". Opening balances are one USE of it; so is populating a test base, so
// is reposting a period's documents. The verb is: run this code, in the application, in the
// background.
//
// 🛑 THIS IS THE HALF THAT RUNS IN THE APPLICATION. The MCP server lives in the DESIGNER, where no
// session ever gets a runtime (`ibSession::EnsureRoot` returns early on DesignerMode). So the tool
// sends a debugger command and this is what the far end runs.
//
// ⭐ A BACKGROUND JOB, NOT A SANDBOX, and the difference is a stop. `debug_sandbox` borrows the
// PARKED session, which is what lets somebody step through code — and what makes them wait. This
// wants nobody waiting, so it takes a session of its own (Standalone) and runs beside them.
//
// ⚠ SO IT CANNOT BE STEPPED THROUGH, and that is the same fact read the other way. What a caller
// has instead is the trial stage and the REGISTRATION JOURNAL: a background session's `Message`
// reaches nobody — it is not tied to the caller's session — so the journal, filtered by m_session,
// is the channel.
//
// ⭐ IT IS LISTED, deliberately. A rented read is minted UNLISTED because it changes nothing; a run
// that can change the base must be findable by the person whose base it is — it takes a row, writes
// its activity line, and appears in Active Users where they can stop it.

#include "backend/backend.h"

#include <wx/string.h>

#include <memory>

// ⭐ WHAT IS BEING ASKED FOR — a shape rather than four arguments, because it crosses four layers
// (tool, bridge, adapter, server) and four spellings of one list is four chances to put `said` where
// `understood` goes. Both are strings; the compiler would not notice.
struct BACKEND_API ibJobRunRequest {
	wxString m_text;        // the code itself, in the configuration's own dialect
	wxString m_said;        // one line for the activity row the person reads
	wxString m_understood;  // the caller's own account of what committing will change
	bool     m_commit = false;

	void Write(class ibWriterMemory& to) const;
	void Read(const class ibReaderMemory& from);
};

// What a run says about itself. One shape, because start, status and cancel all answer with the
// state of the same run — a caller that has just started one and a caller checking on one an hour
// later are asking the same question.
struct BACKEND_API ibJobRunByteCodeState {
	// ⭐ THE REQUEST'S VERDICT RIDES WITH THE RUN'S STATE: everything a caller learns is one object,
	// so a new field costs one edit rather than four hand-matched ones.
	bool     m_accepted = false;   // the far end took the request
	wxString m_refusal;            // ...or why it did not

	// ⭐⭐ THE ONE NAME THIS RUN HAS — sys_session.session: the row in Active Users, the column every
	// journal line is stamped with, and what status and cancel are addressed by. Empty when the run
	// never started.
	//
	// 🛑 THERE WAS A SECOND ONE, a token this file minted ("fill1"), and it bought nothing: a caller
	// holding it still needed the session to read the journal or to let a person stop the run. Two
	// names for one thing is two things to keep in step.
	wxString m_session;
	wxString m_activity;   // where it says it has got to — the same line Active Users shows
	wxString m_error;      // why it stopped, when it did
	wxString m_result;     // what the code came out with, once it has finished
	bool     m_complete = false;
	bool     m_known    = false;   // …because a token that names nothing is not a finished run

	// ⭐ THE WIRE FORMAT LIVES WITH THE STRUCT — writer and reader as each other's mirror, three
	// lines apart, instead of two matching sequences in two files two thousand lines apart.
	void Write(class ibWriterMemory& to) const;
	void Read(const class ibReaderMemory& from);
};

// THE THREE REQUESTS, all by token after the first.
//
// `refusal` carries the reason when one of these answers false, in words meant for whoever asked —
// this is the layer that knows why, and a caller across a socket cannot work one out.
class BACKEND_API ibJobRunByteCode {
public:

	// Run `text` as a background job. The whole run is wrapped in ONE transaction, committed when
	// `commit` is true and rolled back otherwise — which is what makes the trial stage a rehearsal
	// of the real one rather than a model of it.
	//
	// ⭐ THE CODE ARRIVES AS TEXT AND IS COMPILED HERE, against this application's own root. Sending
	// compiled bytecode instead works and was built first; it just makes four standing promises that
	// two processes agree (AOT format version, dependency guids, re-attached parent, slot numbering),
	// and the day one of them stops holding, sent bytecode points at the wrong thing silently. The
	// caller still compiles it at their end to CHECK it — that is where the line numbers are — and
	// does not send the result.
	//
	// `understood` is not checked here for content and never will be: the gate that demands it is
	// the tool's, aimed at the caller, and this end only shows it to the person.
	//
	// ⚠ THE REFUSAL RIDES IN THE STATE, so a false RETURN says only "read m_refusal".
	static bool Start(const ibJobRunRequest& request, ibJobRunByteCodeState& state);

	static bool Status(const wxString& token, ibJobRunByteCodeState& state);

	// Cooperative: the flag is raised and the code unwinds at its next loop boundary. ⚠ Whatever a
	// committing run has already committed stays committed — stopping is not undoing.
	static bool Cancel(const wxString& token, ibJobRunByteCodeState& state);
};

#endif
