#ifndef _PROTOCOL_JOURNAL_H__
#define _PROTOCOL_JOURNAL_H__

#include <wx/string.h>

#include "protocol/protocolApi.h"

// THE PROTOCOL'S JOURNAL — what crossed the wire, in the order it did: the calls made and what they answered, how long
// the exchange took, how much came back. Debugging the protocol is not debugging a window, so it keeps a file of its
// own beside the GUI's (frmclient/diagnostics/journal.h) and the engine's — keyed the same way.
//
//   - ONE FILE PER RUN, `journal/<exe>_<stamp>_<pid>.protocol.log` beside the binary — opened by the first line
//     written, named after the program the library is loaded into: nobody opens it.
//   - ALWAYS ON in a Debug build, absent in Release: the calls compile either way.
//   - EVERY LINE IS TIMED to the millisecond and carries its thread — a table reads on a thread of its own.
//
// Writing to it is one line at a callsite:
//
//     ibProtocolJournalInfo(wxT("call"), wxT("%s: %lld ms"), name, ms);
enum class ibProtocolJournalMark {
	Info,
	Warning,
	Error,
};

class PROTOCOL_API ibProtocolJournal {
public:

	// A line: its kind, the time, the thread, who speaks, what is said — flushed, so a crash keeps what explains it.
	static void Write(ibProtocolJournalMark mark, const wxString& source, const wxString& message);
};

#ifdef NDEBUG
#	define ibProtocolJournalInfo(source, ...)    ((void)0)
#	define ibProtocolJournalWarning(source, ...) ((void)0)
#	define ibProtocolJournalError(source, ...)   ((void)0)
#else
#	define ibProtocolJournalInfo(source, ...)    ibProtocolJournal::Write(ibProtocolJournalMark::Info, source, wxString::Format(__VA_ARGS__))
#	define ibProtocolJournalWarning(source, ...) ibProtocolJournal::Write(ibProtocolJournalMark::Warning, source, wxString::Format(__VA_ARGS__))
#	define ibProtocolJournalError(source, ...)   ibProtocolJournal::Write(ibProtocolJournalMark::Error, source, wxString::Format(__VA_ARGS__))
#endif

#endif
