#include "journal.h"

#include "crashGuard.h"          // the journal lives beside the dumps — same directory decision
#include "core/diagnostics/debugTrace.h"   // ibDebugTraceEnabled — the house env-var switch
#include "core/build.h"                    // GetBuildId, OES_VERSION_* — what is running, said first
#include "core/fileSystem.h"               // journal/ sits with the dumps, in the diagnostic folder

#include "core/exception.h"                // ibErrorText — the system's words for a code
#include "core/fdatetime.h"                // ibDateTime — the run's stamp and each line's time
#include "core/localization/locale.h"      // ibSystemLanguage — the machine's language, in the opening lines
#include "core/system.h"                   // ibHostName, ibUserName, ibOsDescription — the machine, in the opening lines

#include <atomic>
#include <cstdio>   // fputs - the console's echo
#include <fstream>
#include <mutex>

#ifdef __IBMSW__
#include <windows.h>
#endif

namespace {

// The journal's whole state: a file, a lock, and a flag. Every producer in the process writes
// through them, and the lock is what makes two threads' lines two lines instead of one shredded one.
//
// ⚠ The file is opened by Open() and stays open for the life of the process, so a line written
// during teardown — after wx has torn its own logging down — still lands.
//
// ⚠ THE LOCK IS RECURSIVE: a write the file refuses is said by wx (wxLogSysError), and on the main thread wx's
// line comes back here at once through the program's chain (deskgui/app.h) — under the lock this thread holds.
std::ofstream      s_file;
std::recursive_mutex s_lock;
ibString           s_path;
ibString           s_program;   // the name the program opened the journal with — its own lines carry it as their source

// The gate the macro reads. An atomic and not the file handle, because the handle needs the lock to
// be looked at and the whole point is to decide WITHOUT taking one.
std::atomic<bool>  s_open{ false };

// Asked by the program itself to echo every line on standard error (EchoToStderr) — the variable's twin.
std::atomic<bool>  s_echoToStderr{ false };

// The first error's dump is taken — by this run, or by the one before it (SkipErrorDump).
std::atomic<bool>  s_errorDumped{ false };

// Who installed the question "which base is this line about" (SetContext); null when nobody did.
std::atomic<ibTechJournal::ContextFn> s_context{ nullptr };

// Who hears a kept line beyond the file (SetEcho) — a window program's wx; null when nobody does.
std::atomic<ibTechJournal::EchoFn> s_echo{ nullptr };

// (The build number is NOT computed here. `GetBuildId()` in backend_core already derives it from
// __DATE__, and the About box prints that one — a journal with a second, independently correct
// number would be two truths about one binary, which is the failure this file exists to prevent.)

} // namespace

ibTechJournal& ibTechJournal::Instance()
{
	// A function-local static: constructed on first use, destroyed after main, and thread-safe to
	// initialise by the language's own rule. The state it stands for is the file-scope block above —
	// kept there because the program's wx target and the vararg door reach it without going through the
	// object, and because a handle that outlives every subsystem must not depend on member layout.
	static ibTechJournal s_instance;
	return s_instance;
}

void ibTechJournal::Open(const ibString& exeName)
{
	{
		std::lock_guard<std::recursive_mutex> lock(s_lock);
		s_program = exeName;
	}
#ifdef NDEBUG
	// ⭐ A DEBUG BUILD IS THE CONSENT to the file (ibCrashGuard::Install has why): a Release build opens none. The
	// program's name is kept all the same — a program that narrates in its console says there what it says (Write).
	return;
#endif

	// BESIDE THE DUMPS. `journal/` next to `crashdumps/`, so the two halves of one incident live in
	// one place: the dump says where it stopped, the journal says what it was doing. Inside an
	// installed bundle that place is ~/Library/Logs/OES — /Applications is not writable — and
	// everywhere else it is the program folder.
	const ibString dir = ibDiagnosticFolder() + ibFILE_SEP_PATH + ibT("journal");
	std::error_code error;
	std::filesystem::create_directories(ibPathOf(dir), error);

	// ONE FILE PER RUN, KEYED — the same stamp+pid the dumps carry. A single overwritten file loses
	// exactly the run a person is reporting the moment they restart to try again.
	const ibString stamp = ibDateTime::Now().Format(ibT("%Y%m%dT%H%M%S"));
	{
		std::lock_guard<std::recursive_mutex> lock(s_lock);
		s_path = dir + ibFILE_SEP_PATH
			+ ibString::Format(ibT("%s_%s_%u.log"), exeName, stamp, ibCrashGuard::CurrentPidPortable());
		s_file.open(ibPathOf(s_path), std::ios::binary | std::ios::trunc);
		s_open.store(s_file.is_open(), std::memory_order_relaxed);
	}
	if (!s_open.load(std::memory_order_relaxed))
		return;                       // no writable directory — stay silent rather than half-work

	// ⭐ THE FIRST THING IN THE FILE IS WHAT IS RUNNING. A journal whose first line is already a
	// symptom is half useless: the next question is always "which build, on what, since when", and
	// by then the person asking is not at the machine any more. So the run announces itself —
	// binary, version, toolchain, platform, process, start time — before anything can go wrong.
	Write(ibJournalMark::Info, ibT("start"), ibString::Format(
		ibT("%s %d.%d.%d build %d (%s, %s, built %s %s)"),
		exeName,
		OES_VERSION_MAJOR,
		OES_VERSION_MINOR,
		OES_VERSION_RELEASE,
		// THE BUILD NUMBER — the compile itself, not the product version. Two binaries carrying
		// `2.0.0` are the same release and may be different code; this is what tells them apart in
		// a report. The SAME number the About box shows, deliberately: a person reading the journal
		// and a person reading the dialog must be able to say they are looking at one binary.
		GetBuildId(),
#ifdef NDEBUG
		ibT("Release"),
#else
		ibT("Debug"),
#endif
		// THE ARCHITECTURE, NAMED — and named on every platform, not only on Windows. The suite runs
		// on macOS arm64 as well as x86-64 Linux and Windows, and "which machine was this" is the
		// first thing asked about a failure that reproduces in one place and not another. A word
		// like "native" answers that question with nothing.
#if defined(__aarch64__) || defined(_M_ARM64)
		ibT("arm64"),
#elif defined(__arm__) || defined(_M_ARM)
		ibT("arm32"),
#elif defined(__x86_64__) || defined(_M_X64)
		ibT("x86-64"),
#elif defined(__i386__) || defined(_M_IX86)
		ibT("x86"),
#else
		ibT("unknown-arch"),
#endif
		ibString(__DATE__), ibString(__TIME__)));
	Write(ibJournalMark::Info, ibT("start"), ibOsDescription());
	Write(ibJournalMark::Info, ibT("start"), ibString::Format(ibT("pid %u, started %s, locale %s"),
		ibCrashGuard::CurrentPidPortable(),
		ibDateTime::Now().Format(ibT("%Y-%m-%d %H:%M:%S")),
		ibSystemLanguage()));
	Write(ibJournalMark::Info, ibT("start"), ibString::Format(ibT("user %s@%s, %s"),
		ibUserName(), ibHostName(), ibGetCwd()));
	Write(ibJournalMark::Info, ibT("start"), ibString::Format(ibT("binary: %s"), ibExecutablePath()));
	Write(ibJournalMark::Info, ibT("start"), ibString::Format(ibT("journal: %s"), s_path));
	// (Which wx a window program runs on is said by that program, as it chains wx's log in — deskgui/app.h.)
}

void ibTechJournal::Close()
{
	std::lock_guard<std::recursive_mutex> lock(s_lock);
	s_open.store(false, std::memory_order_relaxed);
	if (s_file.is_open()) {
		s_file.flush();
		s_file.close();
	}
}

void ibTechJournal::EchoToStderr()
{
	// (THE CONSOLE HAS ONE WRITER. The journal chained wx's log in here, and a console program's previous wx target IS
	// standard error, so every line was printed twice — the application server's console, 2026-10-06 — until this took
	// the previous target off the chain. A console program chains nothing now: wx's own lines reach its console once,
	// through wx's own target, and the journal's through the echo below.)
	s_echoToStderr.store(true, std::memory_order_relaxed);
}

void ibTechJournal::SetContext(ContextFn context)
{
	s_context.store(context, std::memory_order_release);
}

void ibTechJournal::SetEcho(EchoFn echo)
{
	s_echo.store(echo, std::memory_order_release);
}

void ibTechJournal::SysError(const ibString& source, long code, const ibString& message)
{
	// The platform's own words for the number, when it has any; a code it cannot explain comes back
	// empty, and then the number alone is still worth having — it is what a support forum is searched by.
	const ibString explained = ibString::FromUTF8(
		ibErrorText(std::error_code(static_cast<int>(code), std::system_category())).c_str());
	Write(ibJournalMark::Error, source, explained.IsEmpty()
		? ibString::Format(ibT("%s (system code 0x%08lX)"), message, code)
		: ibString::Format(ibT("%s (system code 0x%08lX: %s)"), message, code, explained));
}

bool ibTechJournal::IsOpen()
{
	return s_open.load(std::memory_order_relaxed);
}

ibString ibTechJournal::Path()
{
	std::lock_guard<std::recursive_mutex> lock(s_lock);
	return s_path;
}

void ibTechJournal::Write(ibJournalMark mark, const ibString& source, const ibString& message)
{
	// …AND WHICH BASE THE LINE IS ABOUT, in brackets before the source — asked of whoever installed the
	// question (SetContext), before the lock: the journal knows nothing about bases. Empty for a line about
	// none (a process with no base yet, a thread that works for none), and then the line is what it always was.
	const ContextFn context = s_context.load(std::memory_order_acquire);
	const ibString about = context != nullptr ? context() : ibString();

	static const bool s_toStderr = ibDebugTraceEnabled("OES_JOURNAL_STDERR");   // see the console's echo below

	ibString line;
	bool forConsole = false;   // what a program that narrates (EchoToStderr) shows in its console — see below
	bool kept = false;         // the file has it — and only then the debugger's echo and the error's dump, below
	{
		std::lock_guard<std::recursive_mutex> lock(s_lock);
		kept = s_file.is_open();
		forConsole = s_toStderr
			|| ((mark != ibJournalMark::Info || source == s_program) && s_echoToStderr.load(std::memory_order_relaxed));
		// ⭐ NO FILE IS NOT NO CONSOLE. A Release build keeps no file (Open), and the console went with it: a Release
		// application server refused at its start said nothing and left with code 1 (2026-10-23). A line nobody
		// keeps and nobody watches goes nowhere.
		if (!kept && !forConsole)
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
		const ibChar* kind =
			  mark == ibJournalMark::Error   ? ibT("[error]  ")
			: mark == ibJournalMark::Warning ? ibT("[warning]")
			                                 : ibT("[info]   ");

		line = ibString::Format(ibT("%s %s  t%-5lu  %-14s  %s\n"),
			kind,
			ibDateTime::Now().Format(ibT("%H:%M:%S.%l")),
			ibCrashGuard::CurrentTidPortable(),
			about.IsEmpty() ? source : ibT("(") + about + ibT(") ") + source,
			message);

		if (kept) {
			s_file << line.ToUtf8();
			s_file.flush();           // a crash must not cost the lines that explain it
		}
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
	//
	// …or the program asks, when narrating is what it is for (EchoToStderr) — one road, not a print of its own
	// beside it.
	//
	// ⭐⭐ AND THEN THE CONSOLE SHOWS WHAT A PERSON WATCHING IT NEEDS: what the program itself says (a line whose
	// source is the program's name) and every warning and error, whoever wrote it; the running commentary — every
	// statement prepared, every query rendered — is the FILE's. It was every line, and a console is slow and has one
	// lock: four sessions of an application server stood in a queue to the console window, 43 of 45 threads caught
	// in the journal were in this echo (2026-10-06, crawlers ×4: 130 s with the window, 99 s with stderr in a file).
	// Max chose: the console only what matters. Hundreds of lines a second are not read there anyway.
	if (forConsole) {
#ifdef __IBMSW__
		// ⚠ A WINDOWS CONSOLE IS TOLD IN ITS OWN CHARACTERS. Bytes reach it read in its code page (866, 1251), so
		// UTF-8 came out as `????N'N?N` for every Cyrillic word (the application server's console, 2026-10-06); a
		// console takes UTF-16 as is (WriteConsoleW), and changes nothing for the window it may share with whoever
		// started the program. A stream that is not a console — a file, a pipe — gets the UTF-8 the file has.
		static const HANDLE s_stderr = ::GetStdHandle(STD_ERROR_HANDLE);
		DWORD mode = 0;
		if (s_stderr != nullptr && s_stderr != INVALID_HANDLE_VALUE && ::GetConsoleMode(s_stderr, &mode)) {
			DWORD written = 0;
			::WriteConsoleW(s_stderr, line.wc_str(), static_cast<DWORD>(line.length()), &written, nullptr);
		}
		else
#endif
		{
			std::fputs(line.ToUtf8().c_str(), stderr);
			std::fflush(stderr);
		}
	}

	// ⭐ AND THE SAME LINE TO THE PROGRAM'S EAR (SetEcho), so the two views never disagree: what is watched live is
	// what is read afterwards, in the same words and the same order. A window program's ear is wx — deskgui/app.h says what
	// an error puts in front of the person, and why a warning does not. Called OUTSIDE the lock — the ear may take locks
	// of its own, and holding two in one order here and the other order there is how a deadlock is built.
	//
	// ⚠ NOT IN A PROGRAM THAT NARRATES IN ITS CONSOLE (EchoToStderr): the console is its ear. Through wx, from any thread
	// but the main one, a line is not delivered: wx keeps the record in a buffer, under a lock of its own, until the main
	// thread flushes — which an application server's never does, so every line of every session was held in memory
	// until exit (2026-10-06).
	if (kept && !s_echoToStderr.load(std::memory_order_relaxed)) {
		if (const EchoFn echo = s_echo.load(std::memory_order_acquire))
			echo(mark, line.Left(line.Len() - 1), message);   // the line without its end
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
	if (kept && mark == ibJournalMark::Error) {
		if (!s_errorDumped.exchange(true, std::memory_order_acq_rel)) {
			ibCrashGuard::WriteDumpNow(ibT("_error"));
			Write(ibJournalMark::Info, ibT("journal"),
				ibString::Format(ibT("first error - dump written to %s"), ibCrashGuard::GetCrashDir()));
		}
	}
}

bool ibTechJournal::Read(const ibString& line, ibJournalMark& mark, ibString& source, ibString& message)
{
	// The line as Write lays it out: the kind (9), a space, the time (12), two spaces, `t` and the thread, two spaces,
	// the source — `(base) source` for a line about a base — padded to 14, two spaces, the message.
	if (line.StartsWith(ibT("[error]")))
		mark = ibJournalMark::Error;
	else if (line.StartsWith(ibT("[warning]")))
		mark = ibJournalMark::Warning;
	else if (line.StartsWith(ibT("[info]")))
		mark = ibJournalMark::Info;
	else
		return false;

	constexpr size_t kThreadAt = 9 + 1 + 12 + 2;
	if (line.length() <= kThreadAt || line[kThreadAt] != ibT('t'))
		return false;
	const size_t sourceAt = line.find_first_not_of(ibT(' '), line.find(ibT("  "), kThreadAt));
	if (sourceAt == ibString::npos)
		return false;
	const size_t sourceEnd = line.find(ibT("  "), sourceAt);
	if (sourceEnd == ibString::npos)
		return false;
	const size_t messageAt = line.find_first_not_of(ibT(' '), sourceEnd);
	source = line.Mid(sourceAt, sourceEnd - sourceAt);
	message = messageAt == ibString::npos ? ibString() : line.Mid(messageAt);
	return true;
}

void ibTechJournal::SkipErrorDump()
{
	s_errorDumped.store(true, std::memory_order_release);
}
