#include "journal.h"

#include "crashGuard.h"          // the journal lives beside the dumps — same directory decision
#include "backend/utils/debugTrace.h"   // ibDebugTraceEnabled — the house env-var switch

#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/utils.h>

#include <atomic>
#include <cstdarg>

namespace {

// The journal's whole state: a file, a lock, and a flag. Every producer in the process writes
// through them, and the lock is what makes two threads' lines two lines instead of one shredded one.
//
// ⚠ The file is opened by Open() and stays open for the life of the process, so a line written
// during teardown — after wx has torn its own logging down — still lands.
wxFFile            s_file;
wxCriticalSection  s_lock;
wxString           s_path;

// The gate the macro reads. An atomic and not the file handle, because the handle needs the lock to
// be looked at and the whole point is to decide WITHOUT taking one.
std::atomic<bool>  s_open{ false };

// Set while the journal is echoing a line through wx. Our own wx target checks it and lets that
// line pass: the file already has it, and taking it again is how a message becomes a loop.
// Per-thread, because two threads may be writing at once and one must not silence the other.
thread_local bool  s_echoing = false;

unsigned long CurrentTid()
{
	return static_cast<unsigned long>(wxThread::GetCurrentId());
}

// (The build number is NOT computed here. `GetBuildId()` in backend_core already derives it from
// __DATE__, and the About box prints that one — a journal with a second, independently correct
// number would be two truths about one binary, which is the failure this file exists to prevent.)

// The wx side. It only TAKES — the chain keeps the previous target alive, so the debugger's output
// window still shows every line it showed before.
class ibJournalLogTarget : public wxLog
{
protected:
	void DoLogRecord(wxLogLevel level, const wxString& msg, const wxLogRecordInfo& info) override
	{
		if (s_echoing)
			return;                   // the journal's own line, on its way to the debugger

		const wxChar* kind =
			  level == wxLOG_FatalError ? wxT("wx.fatal")
			: level == wxLOG_Error      ? wxT("wx.error")
			: level == wxLOG_Warning    ? wxT("wx.warn")
			: level == wxLOG_Message    ? wxT("wx.message")
			: level == wxLOG_Status     ? wxT("wx.status")
			: level == wxLOG_Debug      ? wxT("wx.debug")
			: level == wxLOG_Trace      ? wxT("wx.trace")
			                            : wxT("wx");

		// wx's severity becomes OUR mark, so a line that came through the chain is scanned the same
		// way as one written here. A wx ERROR does NOT take a dump with it, though: this target sees
		// every wxLogError in the process, including ones a caller is about to handle, and a
		// snapshot per handled error is exactly the loop the once-per-run rule exists to avoid.
		const ibJournalMark mark =
			  level == wxLOG_FatalError || level == wxLOG_Error || level == wxLOG_Warning
			    ? ibJournalMark::Warning
			    : ibJournalMark::Info;

		// The SITE, when wx knows it — a line is worth much more with the file and line that
		// produced it, and this is the one place that has them.
		if (info.filename != nullptr && info.line > 0)
			ibTechJournal::Write(mark, kind, wxString::Format(wxT("%s   [%s:%d]"),
				msg, wxFileName(info.filename).GetFullName(), info.line));
		else
			ibTechJournal::Write(mark, kind, msg);
	}
};

} // namespace

ibTechJournal& ibTechJournal::Instance()
{
	// A function-local static: constructed on first use, destroyed after main, and thread-safe to
	// initialise by the language's own rule. The state it stands for is the file-scope block above —
	// kept there because the wx log target and the vararg door reach it without going through the
	// object, and because a handle that outlives every subsystem must not depend on member layout.
	static ibTechJournal s_instance;
	return s_instance;
}

void ibTechJournal::Open(const wxString& exeName)
{
	// BESIDE THE DUMPS. `journal/` next to `crashdumps/`, so the two halves of one incident live in
	// one place: the dump says where it stopped, the journal says what it was doing.
	const wxString exeDir = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
	const wxString dir    = exeDir + wxFILE_SEP_PATH + wxT("journal");
	wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

	// ONE FILE PER RUN, KEYED — the same stamp+pid the dumps carry. A single overwritten file loses
	// exactly the run a person is reporting the moment they restart to try again.
	const wxString stamp = wxDateTime::Now().Format(wxT("%Y%m%dT%H%M%S"));
	{
		wxCriticalSectionLocker lock(s_lock);
		s_path = dir + wxFILE_SEP_PATH
			+ wxString::Format(wxT("%s_%s_%u.log"), exeName, stamp,
				static_cast<unsigned>(wxGetProcessId()));
		s_file.Open(s_path, wxT("w"));
		s_open.store(s_file.IsOpened(), std::memory_order_relaxed);
	}
	if (!s_open.load(std::memory_order_relaxed))
		return;                       // no writable directory — stay silent rather than half-work

	// ⭐ THE FIRST THING IN THE FILE IS WHAT IS RUNNING. A journal whose first line is already a
	// symptom is half useless: the next question is always "which build, on what, since when", and
	// by then the person asking is not at the machine any more. So the run announces itself —
	// binary, version, toolchain, platform, process, start time — before anything can go wrong.
	Write(ibJournalMark::Info, wxT("start"), wxString::Format(
		wxT("%s %d.%d.%d build %d (%s, %s, built %s %s)"),
		exeName,
		static_cast<int>(version_oes_last) / 1000,
		(static_cast<int>(version_oes_last) % 1000) / 100,
		static_cast<int>(version_oes_last) % 100,
		// THE BUILD NUMBER — the compile itself, not the product version. Two binaries carrying
		// `1.0.1` are the same release and may be different code; this is what tells them apart in
		// a report. The SAME number the About box shows, deliberately: a person reading the journal
		// and a person reading the dialog must be able to say they are looking at one binary.
		GetBuildId(),
#ifdef NDEBUG
		wxT("Release"),
#else
		wxT("Debug"),
#endif
		// THE ARCHITECTURE, NAMED — and named on every platform, not only on Windows. The suite runs
		// on macOS arm64 as well as x86-64 Linux and Windows, and "which machine was this" is the
		// first thing asked about a failure that reproduces in one place and not another. A word
		// like "native" answers that question with nothing.
#if defined(__aarch64__) || defined(_M_ARM64)
		wxT("arm64"),
#elif defined(__arm__) || defined(_M_ARM)
		wxT("arm32"),
#elif defined(__x86_64__) || defined(_M_X64)
		wxT("x86-64"),
#elif defined(__i386__) || defined(_M_IX86)
		wxT("x86"),
#else
		wxT("unknown-arch"),
#endif
		wxT(__DATE__), wxT(__TIME__)));
	Write(ibJournalMark::Info, wxT("start"), wxString::Format(wxT("%s, wxWidgets %d.%d.%d"),
		wxGetOsDescription(), wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER));
	Write(ibJournalMark::Info, wxT("start"), wxString::Format(wxT("pid %u, started %s, locale %s"),
		static_cast<unsigned>(wxGetProcessId()),
		wxDateTime::Now().Format(wxT("%Y-%m-%d %H:%M:%S")),
		wxLocale::GetLanguageCanonicalName(wxLocale::GetSystemLanguage())));
	Write(ibJournalMark::Info, wxT("start"), wxString::Format(wxT("user %s@%s, %s"),
		wxGetUserId(), wxGetHostName(), wxGetCwd()));
	Write(ibJournalMark::Info, wxT("start"), wxString::Format(wxT("binary: %s"),
		wxStandardPaths::Get().GetExecutablePath()));
	Write(ibJournalMark::Info, wxT("start"), wxString::Format(wxT("journal: %s"), s_path));

	// ⭐ CHAINED, NOT REPLACED — and only as a SAFETY NET. Our own code writes through ibJournal and
	// reaches the file directly; this catches what we do not own: wx's own diagnostics, and the
	// callsites not migrated yet. Replacing the target instead of chaining would take the
	// developer's live view away in exchange for the file, which is a trade nobody asked for.
	//
	// ⚠ THE GLOBAL LOG LEVEL IS LEFT EXACTLY AS IT WAS. Raising it would change what the PREVIOUS
	// target shows too — new lines appearing in the debugger that never appeared before — and the
	// promise is the opposite one: the same output as always, plus a file.
	//
	// Deliberately never deleted: it must outlive every other subsystem's last log line, and a
	// process-lifetime object is what that means.
	new wxLogChain(new ibJournalLogTarget());
}

void ibTechJournal::Close()
{
	wxCriticalSectionLocker lock(s_lock);
	s_open.store(false, std::memory_order_relaxed);
	if (s_file.IsOpened()) {
		s_file.Flush();
		s_file.Close();
	}
}

void ibTechJournal::SysError(const wxString& source, long code, const wxString& message)
{
	// The platform's own words for the number, when it has any. wxSysErrorMsgStr answers for the
	// LAST error on some platforms and for the given code on others; a code it cannot explain comes
	// back empty, and then the number alone is still worth having — it is what a support forum is
	// searched by.
	const wxString explained = wxSysErrorMsgStr(static_cast<unsigned long>(code));
	Write(ibJournalMark::Error, source, explained.IsEmpty()
		? wxString::Format(wxT("%s (system code 0x%08lX)"), message, code)
		: wxString::Format(wxT("%s (system code 0x%08lX: %s)"), message, code, explained));
}

bool ibTechJournal::IsOpen()
{
	return s_open.load(std::memory_order_relaxed);
}

wxString ibTechJournal::Path()
{
	wxCriticalSectionLocker lock(s_lock);
	return s_path;
}

void ibTechJournal::Write(ibJournalMark mark, const wxString& source, const wxString& message)
{
	wxString line;
	{
		wxCriticalSectionLocker lock(s_lock);
		if (!s_file.IsOpened())
			return;

		// THE LINE: kind, time, thread, event source, detail.
		//
		// The KIND is first, spelled out and bracketed, because it is what a person scans by and a
		// word survives being read aloud, pasted into a ticket and searched for — a punctuation mark
		// does none of those. INFO is the ordinary running commentary, which is most of the file and
		// is exactly what used to go to the debug output. TIME to the millisecond, because ordering
		// two events and measuring the gap between them is most of what this file is for. THREAD,
		// because the same message from two threads is two different stories. Then who is speaking,
		// then what they say.
		//
		// Padded to one width so the columns line up down the page: a reader's eye follows a column,
		// and a ragged left edge costs more than the three spaces it saves.
		const wxChar* kind =
			  mark == ibJournalMark::Error   ? wxT("[error]  ")
			: mark == ibJournalMark::Warning ? wxT("[warning]")
			                                 : wxT("[info]   ");

		line = wxString::Format(wxT("%s %s  t%-5lu  %-14s  %s\n"),
			kind,
			wxDateTime::UNow().Format(wxT("%H:%M:%S.%l")),
			CurrentTid(),
			source,
			message);

		s_file.Write(line);
		s_file.Flush();               // a crash must not cost the lines that explain it
	}

	// ⭐ AND TO STANDARD ERROR, ON REQUEST — for a machine with no debugger to attach to: a console
	// build, a headless run, a CI job where the journal file is thrown away with the container.
	//
	// ⚠ OFF BY DEFAULT, and that is a correction of the first shape of this. Always-on drowned the
	// one place it was meant to help: a suite of twelve hundred tests would put every line the engine
	// writes into the job log, and a log nobody can scroll is a log nobody reads. The file is always
	// written; the stream is for a run that has been asked to narrate itself.
	//
	//     set OES_JOURNAL_STDERR=1
	static const bool s_toStderr = ibDebugTraceEnabled("OES_JOURNAL_STDERR");
	if (s_toStderr) {
		std::fputs(line.utf8_str(), stderr);
		std::fflush(stderr);
	}

	// ⭐ AND THE SAME LINE INTO THE DEBUGGER, so the two views never disagree: what is watched live
	// is what is read afterwards, in the same words and the same order.
	//
	// ⚠ THE LOOP IS BROKEN BY A FLAG, not by bypassing wx. The line would otherwise come straight
	// back through the chain into this function; instead the echo announces itself and the wx target
	// lets a line marked this way pass. Written OUTSIDE the lock — wx may take locks of its own, and
	// holding two in one order here and the other order there is how a deadlock is built.
	{
		struct EchoGuard {
			EchoGuard()  { s_echoing = true;  }
			~EchoGuard() { s_echoing = false; }
		} echo;

		// ⭐⭐ THE ECHO SPEAKS IN THE SEVERITY IT WAS GIVEN — and this is what makes migrating a
		// `wxLogError` callsite to `ibJournalError` safe. wx's verbs are not interchangeable: in a
		// GUI application wxLogError puts a dialog in front of the user, wxLogMessage shows a
		// message, and wxLogDebug is invisible. Routing everything through wxLogDebug would have
		// journalled the errors and SILENTLY TAKEN THEM OFF THE SCREEN — the callsite would keep
		// compiling and the user would stop being told.
		//
		// So the layer calls both: the file gets the line, and wx gets the same words at the same
		// severity it would have got from the original call.
		//
		// ⚠ THE USER SEES THE MESSAGE, NOT THE JOURNAL LINE. A timestamp and a thread id belong in a
		// file, not in a dialog; the debug echo keeps the full line because there the timestamp is
		// the point.
		// ⚠ INFO DOES NOT SHOUT. `*` marks a line worth FINDING in the file; it does not mean the
		// user must be interrupted. Routed through wxLogMessage it was exactly that: in a GUI
		// application wxLogMessage opens a modal dialog, so the startup banner — four lines nobody
		// asked to be told — became four dialogs before the launcher even appeared (seen live,
		// 2026-08-22). The mark is about the FILE; the echo is about the PERSON, and they are not
		// the same decision.
		//
		// Errors and warnings do keep their dialogs: those callsites were wxLogError / wxLogWarning
		// before the migration, and taking their dialog away would be the silent-removal this layer
		// exists to prevent.
		switch (mark) {
		case ibJournalMark::Error:   wxLogError  (wxT("%s"), message); break;
		case ibJournalMark::Warning: wxLogWarning(wxT("%s"), message); break;
		default:                     wxLogDebug  (wxT("%s"), line.Left(line.Len() - 1)); break;
		}
	}

	// ⭐⭐ AN ERROR TAKES A DUMP WITH IT. By the time anyone reads the line, the state that produced
	// it is gone — the stack has unwound, the objects are destroyed, the thread has moved on. The
	// dump is the only way to keep it, and the machinery for writing one already exists for crashes.
	//
	// ⚠ ONCE PER RUN. A failure that happens once happens in a loop soon after, and a full-process
	// snapshot per iteration would fill a disk while the program is still trying to work. The first
	// one is the one worth having: it is the failure before anything downstream reacted to it.
	//
	// ⚠ AND IT DOES NOT END THE PROCESS. A log verb that can terminate is a trap — someone will put
	// it inside a loop, or on a path that recovers perfectly well. Deliberate abort stays a
	// deliberate call (ibCrashGuard::TerminateProcessFast), written where the decision is made.
	if (mark == ibJournalMark::Error) {
		static std::atomic<bool> s_dumped{ false };
		if (!s_dumped.exchange(true, std::memory_order_acq_rel)) {
			ibCrashGuard::WriteDumpNow(wxT("_error"));
			Write(ibJournalMark::Info, wxT("journal"),
				wxString::Format(wxT("first error - dump written to %s"), ibCrashGuard::GetCrashDir()));
		}
	}
}

// The vararg door. The GATE IS TESTED FIRST, before FormatV runs: with the journal off the call is a
// load, a branch and a return, and the string is never built.
#if !wxUSE_UTF8_LOCALE_ONLY
void ibTechJournal::DoPrintWchar(ibJournalMark mark, const wxString& source, const wxChar* format, ...)
{
	if (!IsOpen())
		return;
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);
	Write(mark, source, message);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibTechJournal::DoPrintUtf8(ibJournalMark mark, const wxString& source, const char* format, ...)
{
	if (!IsOpen())
		return;
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);
	Write(mark, source, message);
}
#endif
