#ifndef _FRMCLIENT_CLIENT_JOURNAL_H__
#define _FRMCLIENT_CLIENT_JOURNAL_H__

#include <wx/string.h>

#include "frmclient/frmclient.h"

// THE THIN CLIENT'S JOURNAL — what the client did, in the order it did it: the calls it made and what they answered,
// what it read for a table, what it drew. The engine's technology journal (backend/diagnostics/journal.h) cut down to
// the client, which links no backend: a client of an application server has no engine in its process to write one,
// and a client of a file base writes both, side by side.
//
//   - ONE FILE PER RUN, `journal/<exe>_<stamp>_<pid>.client.log` beside the binary — the engine's own sits beside it
//     with a file base, keyed the same way.
//   - ALWAYS ON in a Debug build, absent in Release, as the engine's: the calls compile either way.
//   - EVERY LINE IS TIMED to the millisecond and carries its thread — a table reads on a thread of its own.
//
// Writing to it is one line at a callsite:
//
//     ibClientJournalInfo(wxT("fetch"), wxT("%d rows"), count);
enum class ibClientJournalMark {
	Info,
	Warning,
	Error,
};

class FRMCLIENT_API ibClientJournal {
public:

	// The run's file opened — the application's first act (its OnInit), before anything can go wrong. A machine with no
	// writable directory beside the binary still runs: every write is then nothing.
	static void Open(const wxString& exeName);

	static bool IsOpen();

	// A line: its kind, the time, the thread, who speaks, what is said — flushed, so a crash keeps what explains it.
	static void Write(ibClientJournalMark mark, const wxString& source, const wxString& message);
};

#ifdef NDEBUG
#	define ibClientJournalInfo(source, ...)    ((void)0)
#	define ibClientJournalWarning(source, ...) ((void)0)
#	define ibClientJournalError(source, ...)   ((void)0)
#else
#	define ibClientJournalInfo(source, ...)    ibClientJournal::Write(ibClientJournalMark::Info, source, wxString::Format(__VA_ARGS__))
#	define ibClientJournalWarning(source, ...) ibClientJournal::Write(ibClientJournalMark::Warning, source, wxString::Format(__VA_ARGS__))
#	define ibClientJournalError(source, ...)   ibClientJournal::Write(ibClientJournalMark::Error, source, wxString::Format(__VA_ARGS__))
#endif

#endif
