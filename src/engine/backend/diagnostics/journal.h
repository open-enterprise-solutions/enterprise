#ifndef __IB_TECH_JOURNAL_H__
#define __IB_TECH_JOURNAL_H__

// ibTechJournal — the TECHNOLOGY JOURNAL: what the platform did, in the order it did it.
//
// A subsystem of its own, beside the crash guard and the audit logger, because it answers a
// question neither of them does:
//
//   - ibLogger (`ibLog`)  — what the USER did. Login, a document written, a schema applied. It is
//                           an audit surface: rows, retention, a viewer, a place in the product.
//   - ibCrashGuard        — what happened at the END. Dumps and last words, written once, after
//                           everything else has already stopped being able to speak.
//   - ibTechJournal       — what the ENGINE is doing, continuously, while it works. Which query was
//                           run, which road the fold took, what a source refused to read, how long
//                           a step took. Nobody is meant to read it in production; it is the thing
//                           a developer opens when the answer is wrong and the code looks right.
//
// Shape, and the reasons for it:
//
//   - ONE FILE PER RUN, keyed `<exe>_<stamp>_<pid>.log`, in `journal/` beside `crashdumps/`. The
//     same two keys the dumps carry, so a dump and the journal that explains it are found together.
//     A single overwritten file loses exactly the run a person is reporting the moment they restart.
//   - ALWAYS ON in a Debug build, absent in Release. A developer running Debug has already
//     consented; a Release story needs decisions about disk, privacy and support that have not
//     been made yet. The API exists in both — a callsite compiles either way.
//   - wx LOGGING IS CHAINED INTO IT. wxLogDebug / Message / Warning / Error keep reaching the
//     debugger exactly as before AND land here with a timestamp. Doubled, never diverted: what is
//     watched live is what can be mailed back afterwards.
//   - EVERY LINE IS TIMED to the millisecond and carries its thread. Ordering two events and
//     measuring the gap between them is most of what this file is for.
//
// Writing to it is one line at a callsite:
//
//     ibJournalInfo(wxT("query"), wxT("%s read %d rows"), name, count);
//
// See docs/technology-journal.md.

// ⚠ backend.h, NOT backend_core.h. The core header includes THIS one at its end so that every file
// in the engine gets `ibJournal` for free — which makes the pair a cycle, and a cycle resolves
// differently depending on which file the compiler reaches first. Taking only what is actually
// needed (the export macro) breaks it: this header can then be included from anywhere, including
// from inside the core's own chain.
#include "backend/backend.h"

#include <wx/wx.h>          // WX_DEFINE_VARARG_FUNC + wxFormatString
#include <wx/string.h>

// ⭐ THE KIND IN THE FIRST COLUMN — spelled out and bracketed, because it is what a person scans a
// thousand lines by: `[info]`, `[warning]`, `[error]`. A word rather than a symbol, so it survives
// being read aloud, pasted into a ticket and searched for.
//
// INFO is the ordinary running commentary — most of the file, and exactly what used to be written
// with wxLogDebug. There is no fourth, quieter kind: two names rendering one word is a distinction
// a reader cannot see and a writer has to guess at, so there are exactly three, one per line kind.
//
// It is a LEVEL, not decoration: a reader (and later a filter) can keep the marked lines and drop
// the rest without parsing anything else on the line.
//
// ⚠ AND IT IS ABOUT THE FILE, NOT ABOUT THE USER. `Info` says "worth finding later", not "interrupt
// somebody now" — it echoes to the debugger like an ordinary line. Only `Warning` and `Error` reach
// the screen, and they do so because their callsites were wxLogWarning / wxLogError before the
// migration and must keep saying what they said.
enum class ibJournalMark : wxChar {
	Info    = wxT('*'),
	Warning = wxT('?'),
	Error   = wxT('!'),
};

class BACKEND_API ibTechJournal {
public:
	// ⭐ THE ONE INSTANCE. Callers do not need it — `ibJournalInfo(...)` and the statics below are the
	// whole surface — and it exists for the same reason the statics do: there is exactly one
	// journal per process, and no layer above may hand out a second.
	//
	// ⚠ IT IS NOT OWNED BY appData, and that is the whole point of its lifetime: it opens inside
	// ibCrashGuard::Install, the first act of every application, long before a database is chosen or
	// a session made — so that whatever happens during bring-up has somewhere to be said. Owning it
	// there would mean the journal starts after the part of the run most worth journalling. It ends
	// with the process, deliberately: a line written during teardown still lands.
	static ibTechJournal& Instance();

	// Open the run's file and start doubling wx logging into it. Called once, from
	// ibCrashGuard::Install, before any other subsystem starts — so the journal is alive from the
	// process's first line and needs no database, no session and no configuration.
	//
	// Failures are silent by design: a machine with no writable directory beside the binary must
	// still run. IsOpen() then answers false and every write is a no-op.
	static void Open(const wxString& exeName);

	// Flush and close. Not required — the process may simply end — but a caller that wants the file
	// complete before it does something drastic can ask.
	static void Close();

	// Is anything listening? One atomic read, and the gate every write tests BEFORE formatting, so
	// a callsite on a hot path costs a load and a branch when the journal is off.
	static bool IsOpen();

	// The run's file, absolute. Empty when the journal never opened.
	static wxString Path();

	// The write itself — the source (a subsystem name: "query", "query.compose", "session") and the
	// line. Timestamped, thread-stamped and flushed here; echoed to the debugger by the same call.
	static void Write(ibJournalMark mark, const wxString& source, const wxString& message);

	// ⭐ A FAILURE THE OPERATING SYSTEM EXPLAINS. A COM HRESULT or a Win32 error code is a number
	// nobody can read; the platform holds the sentence that goes with it, and this is the one verb
	// that asks for it. Everything else is an ordinary error: the line is journalled, the dump is
	// taken, the user is told.
	//
	// Kept as its own door rather than folded into ibJournalError because the CODE is not part of the
	// message — a caller that has one must not have to render it itself, and a caller that has none
	// must not have to invent one.
	static void SysError(const wxString& source, long code, const wxString& message);

	// …and the same with the message's OWN arguments, on wx's vararg machinery — the shape every
	// raise in this codebase uses (`ibBackendQuerySyntaxException::ErrorAt`, `ibDatabaseLayer::
	// RunQuery`). Two things a hand-rolled parameter pack does not give: the format string is
	// CHECKED AGAINST ITS ARGUMENTS at compile time, and both wx builds (wchar and UTF-8) get the
	// right implementation without the callsite knowing which one it is in.
	//
	// The gate is tested first, so with the journal off nothing is allocated or converted.
	WX_DEFINE_VARARG_FUNC(static void, Print, 3, (ibJournalMark, const wxString&, const wxFormatString&),
		DoPrintWchar, DoPrintUtf8);

private:
#if !wxUSE_UTF8_LOCALE_ONLY
	static void DoPrintWchar(ibJournalMark mark, const wxString& source, const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static void DoPrintUtf8(ibJournalMark mark, const wxString& source, const char* format, ...);
#endif
};

// ⭐ THE ONE LINE A CALLSITE WRITES. Named the way the other process-wide handles are (`appData`,
// `db_query`, `ibLog`), and deliberately a MACRO: a Release build compiles the whole thing away,
// ARGUMENTS INCLUDED, so a site costs nothing there and needs no `#ifdef` of its own.
//
//     ibJournalInfo(wxT("query"), wxT("compose: %s took %d rows"), name, count);
// ⭐ …AND A GUARD FOR THE WORK BEFORE THE LINE. A single `ibJournalInfo(...)` needs no guard — the gate
// is inside it and its arguments are not evaluated when it is closed. But a line that must be
// ASSEMBLED first — walking a column list, joining names, counting rows — is real work that would
// run whether or not anyone is listening, and writing `if (ibTechJournal::IsOpen())` by hand at
// every such site is the sort of noise that gets copied wrong once and stays.
//
//     ibJournalIf {
//         wxString cols;
//         for (const auto& c : columns) cols += c.m_name + wxT(", ");
//         ibJournalInfo(wxT("query"), wxT("columns: %s"), cols);
//     }
//
// In Release it is `if (false)`, so the block compiles and is removed entirely.
#ifdef NDEBUG
#	define ibJournalIf  if (false)
#else
#	define ibJournalIf  if (ibTechJournal::IsOpen())
#endif

// Three verbs, one door — the mark is the only difference, and it is what a reader scans by.
#ifdef NDEBUG
#	define ibJournalInfo(source, ...)     ((void)0)
#	define ibJournalWarning(source, ...)  ((void)0)
#	define ibJournalError(source, ...)    ((void)0)
#	define ibJournalSysError(source, code, message)  ((void)0)
#else
// ⚠ WRAPPED IN do/while(false), and not out of habit. A bare `if` inside a macro STEALS A DANGLING
// `else`: `if (x) ibJournalInfo(...); else Report();` binds Report() to the macro's own `if`, so it
// runs when x is TRUE and the journal is closed — silently, with no warning from the compiler. The
// wrapper makes the macro one statement, which is what every callsite already assumes it is.
#	define ibJournalInfo(source, ...) \
		do { if (ibTechJournal::IsOpen()) ibTechJournal::Print(ibJournalMark::Info,    (source), __VA_ARGS__); } while (false)
#	define ibJournalWarning(source, ...) \
		do { if (ibTechJournal::IsOpen()) ibTechJournal::Print(ibJournalMark::Warning, (source), __VA_ARGS__); } while (false)
#	define ibJournalError(source, ...) \
		do { if (ibTechJournal::IsOpen()) ibTechJournal::Print(ibJournalMark::Error,   (source), __VA_ARGS__); } while (false)
// A code the OPERATING SYSTEM issued — the number plus the sentence the platform keeps for it.
#	define ibJournalSysError(source, code, message) \
		do { if (ibTechJournal::IsOpen()) ibTechJournal::SysError((source), (code), (message)); } while (false)
#endif

#endif // __IB_TECH_JOURNAL_H__
