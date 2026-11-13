#ifndef _DESKGUI_APP_H_
#define _DESKGUI_APP_H_

// ibWxApp — wxApp subclass with crash plumbing pre-wired.
//
// The desktop program's base, so in the desktop's library (2026-11-13, Max: it was the core's, beside the guard it
// wires; every program that derives from it - launcher, codeRunner, designer-thin, enterprise-thin - is a desktop one
// and places deskgui). What it catches is the core's refusal (ibCoreException — the engine's among them), so it knows
// nothing above the core. It is wx-aware (wxApp / wxDebugReport / wxMessageBox / wxLog) and header-only: every method
// body is inline, made in the program that derives from it.
//
// What the base wires for you:
//   OnInit              → ibCrashGuard::Install(GetExeName()), the journal's wx side
//                          (wx's log chained in, the journal's lines echoed into wx) + DoOnInit()
//   CleanUp             → the echo taken away before wx's log goes
//   OnRun               → headless WrapStartup around DoOnRun()
//   OnExceptionInMainLoop → ibCoreException = show + continue;
//                            anything else = unwind into OnUnhandled
//   OnUnhandledException  → log + wxDebugReport
//   OnFatalException      → wxDebugReport (main thread) /
//                            native MessageBox + TerminateProcess (bg)
//   OnAssertFailure       → journal + stderr, once per site, NO dialog
//
// Subclass contract:
//   - GetExeName() — pure virtual, e.g. "launcher" / "codeRunner".
//   - DoOnInit()   — replaces OnInit body. Default returns true.
//   - DoOnRun()    — replaces OnRun body. Default delegates to
//                    wxApp::OnRun() (plain main event loop).
//
// Anything outside the wxApp pipeline (ReportStartupError, etc.) goes
// through ibCrashGuard directly — core symbols, no wrapper needed.

#include "core/diagnostics/crashGuard.h"
#include "core/diagnostics/journal.h"   // ibJournalError — what the loop says of a refusal
#include "core/exception.h"             // ibCoreException — the refusal it says and goes on
#include "deskgui/univ/messageDialog.h"   // ibLogGui — wx's log window in the theme's box

#include <wx/app.h>
#include <wx/cmdline.h>   // the usage text an unparsed command line is answered with
#include <wx/debugrpt.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/string.h>
#include <wx/thread.h>

#ifndef __WXMSW__
#include <signal.h>  // SIGTERM / SIGINT, a request to quit; sigaction, wx's handler read back
#endif

#include <cstdio>    // stderr, for an assertion raised before the journal is open
#include <cstdlib>   // EXIT_FAILURE
#include <exception>
#include <filesystem>   // the file a wx line came from, by its name alone
#include <functional>
#include <mutex>
#include <set>
#include <utility>

// THE JOURNAL'S wx SIDE — the core has no wx (core/diagnostics/journal.h). wx's own log is chained into the journal
// here, and the journal's lines go back into wx through the echo the application installs (ibTechJournal::SetEcho).

// The mark a line the journal echoes through wx carries IN ITS RECORD (wxLogRecordInfo). The journal's own wx target
// lets a marked line pass: the file already has it, and taking it again is how a message becomes a loop.
// ⚠ IN THE RECORD, NOT IN THE THREAD. It was a thread_local flag, and wx does not deliver a line on the thread
// that logged it: a line from any thread but the main one is BUFFERED and handed over later on the main
// thread, where the flag was never set — every line of a registry thread came back as `wx.debug` (an
// application server's journal, 2026-10-01: 260 of its 876 lines, all at exit). A record keeps its values when
// wx buffers it.
inline const char* const kJournalEchoKey = "oes.journal.echo";

// wx's log, into the journal. It only TAKES — the chain keeps the previous target alive, so the debugger's output
// window still shows every line it showed before.
class ibJournalLogTarget : public wxLog
{
protected:
	void DoLogRecord(wxLogLevel level, const wxString& msg, const wxLogRecordInfo& info) override
	{
		wxUIntPtr echoed = 0;
		if (info.GetNumValue(kJournalEchoKey, &echoed))
			return;                   // the journal's own line, on its way to the debugger

		const ibChar* kind =
			  level == wxLOG_FatalError ? ibT("wx.fatal")
			: level == wxLOG_Error      ? ibT("wx.error")
			: level == wxLOG_Warning    ? ibT("wx.warn")
			: level == wxLOG_Message    ? ibT("wx.message")
			: level == wxLOG_Status     ? ibT("wx.status")
			: level == wxLOG_Debug      ? ibT("wx.debug")
			: level == wxLOG_Trace      ? ibT("wx.trace")
			                            : ibT("wx");

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
			ibTechJournal::Write(mark, kind, ibString::Format(ibT("%s   [%s:%d]"),
				msg, ibString(std::filesystem::path(info.filename).filename().wstring()), info.line));
		else
			ibTechJournal::Write(mark, kind, msg);
	}
};

class ibWxApp : public wxApp {
public:

	// "enterprise" / "designer" / "codeRunner" / "launcher" — used as
	// dump-file prefix and dialog caption. Override on the concrete app.
	virtual wxString GetExeName() const = 0;

	// Subclass entry points. The default is `wxApp::OnInit()` (the wx
	// side of init: cmd-line parse, image handler registration). Override
	// only if the exe needs extra steps (codeRunner / launcher construct
	// a frame here; enterprise / designer keep the default).
	virtual bool DoOnInit() { return wxApp::OnInit(); }

	virtual int DoOnRun() { return wxApp::OnRun(); }

	// ⭐⭐ A COMMAND LINE IT DOES NOT UNDERSTAND IS SAID OUT LOUD — because the default is to say it
	// on a stream a windowed application does not have. wx prints the usage to stderr and returns
	// false; the process then ends with NOTHING anywhere: no window, no dump, and a journal holding
	// six start-up lines and an abrupt stop.
	//
	// 🛑 MEASURED, AND IT COST A TURN (2026-09-04): `designer.exe /F"…\fb_test301"` — a plausible
	// spelling, and wrong. The exe vanished, and the only way to learn that the option is `--file=`
	// was to open the source. Whoever arrives here NEXT — a person on a first run, an assistant on
	// its first launch — must be told by the platform rather than have to read it.
	//
	// The journal is the right ear: it is written whether or not anyone is watching, it survives the
	// process, and it is now readable from outside (`trace_read`). The usage text is the parser's
	// own, so it cannot drift from the options actually declared.
	// ⚠ INFO, AND BOTH LOUDER LEVELS WERE TRIED ON A LIVE RUN FIRST — each brought a cost that has
	// nothing to do with saying the sentence:
	//
	//   `ibJournalError`   takes a CRASH DUMP with it (once per run, by design — an error is a fault
	//                      worth a snapshot). A mistyped option is not a fault of the program, and a
	//                      dump per typo is litter in the directory somebody digs through when
	//                      something really did break.
	//   `ibJournalWarning` echoes to the SCREEN, so the process stopped at a modal box instead of
	//                      stopping — worse than the silence it replaced, because a launch from a
	//                      script now hangs where it used to exit.
	//
	// So it is written where it can always be read and never blocks: the file. The line names the
	// options verbatim, and `trace_read` reaches it from outside the process.
	bool OnCmdLineError(wxCmdLineParser& parser) override
	{
		ibJournalInfo(wxT("start"), wxT("this command line was not understood, so %s is stopping. "
			"It accepts:\n%s"), GetExeName(), parser.GetUsageString());

		return wxApp::OnCmdLineError(parser);
	}

	// …and the same courtesy for `--help`, which otherwise answers into the same missing stream.
	bool OnCmdLineHelp(wxCmdLineParser& parser) override
	{
		ibJournalInfo(wxT("start"), wxT("%s accepts:\n%s"), GetExeName(), parser.GetUsageString());

		return wxApp::OnCmdLineHelp(parser);
	}

	bool OnInit() override
	{
		// Headless crash plumbing first — before any wx subsystem so
		// faults during wxApp::OnInit / image-handler registry / locale
		// init also land in a dump.
		ibCrashGuard::Install(GetExeName());

		// ⭐ THE THEME'S LOG WINDOW (ibLogGui), set here and not in a module: wx deletes the target the modules leave as
		// it finishes starting (wxEntryStart, "the temporary sink ... for the initialization messages"), and the one it
		// then made on demand was its own - a journal's error stood as the system's box over the theme's (2026-11-13,
		// a login refused). The journal chains this one. Every port alike: wxEntryStart is wx's common code.
		delete wxLog::SetActiveTarget(new ibLogGui);

		// ⭐ wx's LOG INTO THE JOURNAL — CHAINED, NOT REPLACED, and only as a SAFETY NET. Our own code writes through
		// ibJournal and reaches the file directly; this catches what we do not own: wx's own diagnostics, and the
		// callsites not migrated yet. Replacing the target instead of chaining would take the developer's live view
		// away in exchange for the file, which is a trade nobody asked for.
		//
		// ⚠ THE GLOBAL LOG LEVEL IS LEFT EXACTLY AS IT WAS. Raising it would change what the PREVIOUS target shows
		// too — new lines appearing in the debugger that never appeared before — and the promise is the opposite one:
		// the same output as always, plus a file.
		//
		// Never deleted by us: it must outlive every other subsystem's last log line; wx's CleanUp lets go of it.
		// …and the journal's lines back into wx (EchoIntoWx), and which wx this is, in the opening lines.
		if (ibTechJournal::IsOpen()) {
			new wxLogChain(new ibJournalLogTarget());
			ibJournalInfo(ibT("start"), ibT("wxWidgets %d.%d.%d"), wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER);
		}
		ibTechJournal::SetEcho(&ibWxApp::EchoIntoWx);

		// ⭐ THE APPLICATION IS LIGHT, AND NOW IT SAYS SO — because the half that answers to the
		// system was contradicting the half that does not.
		//
		// Every surface this platform draws is painted from a fixed LIGHT palette, in code:
		// #FAF7F0 for the metadata tree, #D3DBE1 for the panels around it, and so on through
		// docs/private/ui-palette.md. What is NOT set anywhere is the text colour — no SetForegroundColour
		// accompanies those backgrounds — so glyphs come from the system.
		//
		// Under a dark system appearance the system duly hands back white. White on #FAF7F0 is
		// invisible: the metadata tree renders as a column of blank rows (macOS 15, 2026-09-04).
		// Nothing is broken, nothing is logged, and the window is simply unreadable.
		//
		// Declaring the appearance is the honest fix, not a workaround: an application whose
		// palette cannot follow the system should not claim to. It costs one call and makes every
		// system-derived colour — label text, disabled states, focus rings, native scrollbars —
		// agree with the palette they are drawn against.
		//
		// A real dark theme is a different piece of work: it means a second palette and a
		// theme-aware source for every hard-coded colour, not a switch. Until that exists, this is
		// what keeps the window legible.
		//
		// Called before DoOnInit() so it lands ahead of every window: wx wants it at the very start
		// of OnInit, and MSW in particular can only apply it before controls exist. The result is
		// deliberately ignored — CannotChange is the correct answer on a platform with no such
		// concept, and it is not a startup failure anywhere.
		SetAppearance(Appearance::Light);

		// ⭐ SIGTERM AND SIGINT ARE A REQUEST TO QUIT, and the application answers it. Unanswered, the only answer was
		// the database client's: Firebird's own handler shuts every attachment down and the process lives on — the
		// window open over a base that is gone, its session registry failing once a second ("invalid database handle")
		// until somebody killed it, or until a failure took the terminate path (enterprise-thin, 2026-10-29).
		//
		// Installed BEFORE DoOnInit opens a base (ibCrashGuard::InstallStopSignals says why). The main loop is woken by
		// wx's own handler: SetSignalHandler puts it in place and calls OnStopSignal from the loop, and it is read back
		// to be the wake of the door in core, which sits over it; nothing polls. Windows has no such signal for a window:
		// a process is asked to go by WM_CLOSE (ibStopProcess).
#ifndef __WXMSW__
		if (SetSignalHandler(SIGTERM, &ibWxApp::OnStopSignal) && SetSignalHandler(SIGINT, &ibWxApp::OnStopSignal)) {
			struct sigaction wxHandler;
			if (::sigaction(SIGTERM, nullptr, &wxHandler) == 0)
				ibCrashGuard::InstallStopSignals(wxHandler.sa_handler);
		}
#endif

		return DoOnInit();
	}

	int OnRun() override
	{
		const wxString exe = GetExeName();
		try {
			return DoOnRun();
		}
		catch (const ibCoreException& e) {
			ReportStartupFault(exe,
				wxT("ibCoreException during startup: ") + e.GetErrorDescription());
			return 1;
		}
		catch (const std::exception& e) {
			ReportStartupFault(exe,
				wxT("std::exception during startup: ") + wxString::FromUTF8(e.what()));
			return 1;
		}
		catch (...) {
			ReportStartupFault(exe,
				wxT("Unknown exception during startup (non-std, non-ibCore)"));
			return 1;
		}
	}

	// THE ECHO GOES BEFORE wx's LOG DOES. A line written after it — a static's destructor, a registry's last word —
	// would ask wx for a log target, and wx would make one on demand that nobody deletes. Here and not in OnExit: the
	// programs answer OnExit themselves, and wx calls CleanUp whether OnInit succeeded or not.
	void CleanUp() override
	{
		ibTechJournal::SetEcho(nullptr);
		wxApp::CleanUp();
	}

#if wxUSE_EXCEPTIONS
	bool OnExceptionInMainLoop() override
	{
		// wx delegates here when a C++ exception escapes from an event
		// handler back into the main loop. Returning true keeps the loop
		// running (recoverable); false escalates to OnUnhandledException.
		// A refusal (ibCoreException — the engine's among them) is the only "show and
		// continue" class — known business / DB error. Everything else lets the loop unwind.
		//
		// (A script failure the engine had already reported — ProcessError names the module and the line,
		//  then lets it propagate — was told apart here by ibBackendException::IsErrorHandled, so its window
		//  was not shown twice. In the core that type is not known; the two programs on this base catch
		//  a script's refusal themselves, and none reaches the loop.)
		try {
			throw;
		}
		catch (const ibCoreException& e) {
			ibJournalError(wxT("app"), wxT("%s"), e.GetErrorDescription());
			return true;
		}
		catch (const std::exception& e) {
			ibJournalError(wxT("app"), wxT("std::exception in main loop: %s"),
				wxString::FromUTF8(e.what()));
			return false;
		}
		catch (...) {
			ibJournalError(wxT("app"), wxT("Unknown exception in main loop"));
			return false;
		}
	}

	void OnUnhandledException() override
	{
		// Reached when a C++ exception escapes the event loop. Identify
		// the in-flight type before wxDebugReport — the throw site is
		// already unwound, so the type-aware diagnostic is the last
		// chance to see what actually went wrong.
		wxString diag = wxT("Unhandled exception: <unknown>");
		try {
			auto p = std::current_exception();
			if (p) std::rethrow_exception(p);
		}
		catch (const ibCoreException& e) {
			diag = wxT("Unhandled ibCoreException: ") + e.GetErrorDescription();
		}
		catch (const std::exception& e) {
			diag = wxT("Unhandled std::exception: ") + wxString::FromUTF8(e.what());
		}
		catch (...) {
		}

		ibCrashGuard::LogUnhandledException(GetExeName(), diag);
		ibJournalError(wxT("app"), "%s", diag);

		wxDebugReportCompress report;
		report.AddAll(wxDebugReport::Context_Current);
		wxDebugReportPreviewStd preview;
		if (preview.Show(report)) report.Process();
	}
#endif // wxUSE_EXCEPTIONS

#if wxDEBUG_LEVEL
	// A FAILED ASSERTION IS A RECORD, NOT A WINDOW. wx answers one with a modal
	// message box, and it does that wherever the assertion happened to fire —
	// including inside a paint handler, which is where this was found on
	// 2026-09-07: a `wxAuiToolBar` painted before layout had given it a size
	// asserted in `wxGCDC::DoDrawBitmap` ("invalid bitmap"), the dialog opened
	// from inside the paint, and the client stopped with no window a person
	// could see and no line anyone could read. Opening a document looked like a
	// hang. It was worse than a plain modal, too: AppKit logs a call stack when
	// a modal session starts nested like that, and the process spent every cycle
	// it had symbolicating it, so the box never finished drawing either.
	//
	// The same rule the engine already follows (CLAUDE.md: do not raise a modal
	// from a caller that may not be a person) applies here for the same reason.
	// The assertion still gets said, in the place things are said: the journal,
	// at Info, which reaches the file and not the screen — plus stderr, because
	// an assertion during startup fires before the journal is open.
	//
	// ONCE PER SITE. An assertion in a paint handler fires on every paint; the
	// hundredth copy of a line carries nothing the first did not, and the flood
	// is what would make the journal unreadable at the moment it matters.
	//
	// The core's road, the engine's own assertions take (ibAssertFailure): once per site there, said by a program's
	// handler or the journal.
	void OnAssertFailure(const wxChar* file, int line, const wxChar* func,
		const wxChar* cond, const wxChar* msg) override
	{
		ibAssertFailure(file != nullptr ? wxString(file).utf8_str().data() : nullptr, line,
			func != nullptr ? wxString(func).utf8_str().data() : nullptr,
			cond != nullptr ? wxString(cond).utf8_str().data() : nullptr,
			msg != nullptr ? ibString(msg) : ibString());
	}
#endif // wxDEBUG_LEVEL

#if wxUSE_ON_FATAL_EXCEPTION && wxUSE_STACKWALKER
	void OnFatalException() override
	{
		// wxDebugReport / wxDebugReportPreviewStd are main-thread-only
		// — they assert wxIsMainThread() inside. A
		// background-thread fault going through the wx path double-faults.
		// Surface a platform-native message + TerminateProcess.
		if (!wxIsMainThread()) {
			const wxString exe = GetExeName();
			const wxString caption = wxT("OES ") + exe + wxT(" - fatal error");
			const wxString msg = wxString::Format(
				wxT("A fatal error occurred on a background thread (tid=%lu).\n\n")
				wxT("A crash dump has been saved to:\n%s\n\n")
				wxT("The application will now close."),
				ibCrashGuard::CurrentTidPortable(),
				ibCrashGuard::GetCrashDir());
			ibCrashGuard::PlatformNativeMessage(caption, msg);
			ibCrashGuard::TerminateProcessFast(EXIT_FAILURE);
			return;
		}

		// Main thread — full wx debug-report flow. AddAll(Context_Exception)
		// captures register/stack state at the fault site.
		wxDebugReportCompress report;
		report.AddAll(wxDebugReport::Context_Exception);
		wxDebugReportPreviewStd preview;
		if (preview.Show(report)) report.Process();
		// The persistent minidump is already on disk via the core's
		// SEH filter (it runs before wx's chained handler). A second
		// fault during OnExit cleanup is OK — the dump survives.
	}
#endif // wxUSE_ON_FATAL_EXCEPTION

protected:

#ifndef __WXMSW__
	// SIGTERM / SIGINT, on the main loop: the main window closes, as if the person closed it, except that nobody is
	// asked anything — the base, the sessions and the journal are let go in order. A signal cannot answer a question.
	static void OnStopSignal(int)
	{
		ibJournalInfo(wxT("start"), wxT("asked to stop by a signal - closing"));
		if (wxWindow* const top = wxTheApp->GetTopWindow())
			top->Close(true);
		else
			wxTheApp->ExitMainLoop();
	}
#endif

	// Startup-error surface. Logs to file + prefers wxMessageBox if the
	// app has reached an UI-ready state, otherwise falls back to
	// platform-native (MessageBoxW on Win, stderr on POSIX). Use from
	// DoOnRun when bailing out before the main loop starts.
	static void ReportStartupFault(const wxString& exeName, const wxString& message)
	{
		ibCrashGuard::LogStartupError(exeName, message);

		const wxString caption = wxT("OES ") + exeName + wxT(" - startup error");
#ifdef __WXMSW__
		// Always platform-native during startup — wx state may be
		// half-initialised and wxMessageBox can re-enter the broken
		// path. The fallback is intentional and consistent.
		ibCrashGuard::PlatformNativeMessage(caption, message);
#else
		if (wxTheApp != nullptr)
			wxMessageBox(message, caption, wxOK | wxICON_ERROR);
		else
			ibCrashGuard::PlatformNativeMessage(caption, message);
#endif
	}

private:

	// THE JOURNAL'S LINE, INTO wx (ibTechJournal::SetEcho) — so the debugger shows what the file keeps, and an error
	// reaches the person.
	//
	// ⭐⭐ THE ECHO SPEAKS IN THE SEVERITY IT WAS GIVEN — and this is what makes migrating a `wxLogError` callsite to
	// `ibJournalError` safe. wx's verbs are not interchangeable: in a GUI application wxLogError puts a dialog in front
	// of the user, wxLogMessage shows a message, and wxLogDebug is invisible. Routing everything through wxLogDebug
	// would have journalled the errors and SILENTLY TAKEN THEM OFF THE SCREEN — the callsite would keep compiling and
	// the user would stop being told.
	//
	// ⚠ THE USER SEES THE MESSAGE, NOT THE JOURNAL LINE. A timestamp and a thread id belong in a file, not in a
	// dialog; the debug echo keeps the full line because there the timestamp is the point.
	// ⚠ INFO DOES NOT SHOUT. `*` marks a line worth FINDING in the file; it does not mean the user must be
	// interrupted. Routed through wxLogMessage it was exactly that: in a GUI application wxLogMessage opens a modal
	// dialog, so the startup banner — four lines nobody asked to be told — became four dialogs before the launcher
	// even appeared (seen live, 2026-08-22). The mark is about the FILE; the echo is about the PERSON, and they are
	// not the same decision.
	//
	// 🛑⭐ AND NEITHER DOES A WARNING (Max, 2026-09-23: "when it is a warning, no window — take the sending to the
	// widgets away"). Reading the 56 wxLogWarning callsites settled it: an fsync that failed on a Firebird lease, a
	// bytecode cache that could not be read, a replication peer on another version, a sweep backlog that would not
	// answer — every one is engine plumbing addressed to whoever opens the file, and none is a sentence to stop a
	// person mid-keystroke with.
	//
	// ⚠ AN ERROR STILL DOES. That one says the thing the person asked for did not happen, which they have to be told
	// whether or not anybody ever opens the journal.
	//
	// (wxLogError / wxLogDebug spelled out — the same logger the two macros make, with the mark stored: the line
	// comes back through the chain, and ibJournalLogTarget lets it pass.)
	static void EchoIntoWx(ibJournalMark mark, [[maybe_unused]] const ibString& line, const ibString& message)
	{
		switch (mark) {
		case ibJournalMark::Error:
			if (wxLOG_IS_ENABLED(Error))
				wxMAKE_LOGGER(Error).Store(kJournalEchoKey, wxUIntPtr(1)).Log(wxT("%s"), message);
			break;
		default:
#if wxUSE_LOG_DEBUG
			if (wxLOG_IS_ENABLED(Debug))
				wxMAKE_LOGGER(Debug).Store(kJournalEchoKey, wxUIntPtr(1)).Log(wxT("%s"), line);
#endif
			break;
		}
	}
};

#endif // _DESKGUI_APP_H_
