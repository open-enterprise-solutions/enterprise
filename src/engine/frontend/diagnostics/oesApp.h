#ifndef __OES_APP_H__
#define __OES_APP_H__

// ibWxApp — wxApp subclass with crash plumbing pre-wired.
//
// Header-only. Lives under frontend/diagnostics/ because it is
// wx-aware (wxApp / wxDebugReport / wxMessageBox / wxLog), but every
// method body is inline — the header does NOT pull a link-time
// dependency on frontend.dll. The only OES symbol it calls is
// BACKEND_API ibCrashGuard::*, satisfied by backend.lib which every
// GUI exe already links.
//
// Consequence: launcher.exe, which links only backend.lib (no
// frontend.dll), can still derive from this base. Including the
// header alone doesn't add a transitive frontend dependency — the .h
// is consumed entirely at compile time.
//
// What the base wires for you:
//   OnInit              → ibCrashGuard::Install(GetExeName()) + DoOnInit()
//   OnRun               → headless WrapStartup around DoOnRun()
//   OnExceptionInMainLoop → ibBackendException = show + continue;
//                            anything else = unwind into OnUnhandled
//   OnUnhandledException  → log + wxDebugReport
//   OnFatalException      → wxDebugReport (main thread) /
//                            native MessageBox + TerminateProcess (bg)
//
// Subclass contract:
//   - GetExeName() — pure virtual, e.g. "enterprise" / "designer".
//   - DoOnInit()   — replaces OnInit body. Default returns true.
//   - DoOnRun()    — replaces OnRun body. Default delegates to
//                    wxApp::OnRun() (plain main event loop).
//
// Anything outside the wxApp pipeline (ReportStartupError, etc.) goes
// through ibCrashGuard directly — backend symbols, no wrapper needed.

#include "backend/diagnostics/crashGuard.h"
#include "backend/backend_exception.h"

#include <wx/app.h>
#include <wx/cmdline.h>   // the usage text an unparsed command line is answered with
#include <wx/debugrpt.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/socket.h>
#include <wx/string.h>
#include <wx/thread.h>

#include <cstdlib>   // EXIT_FAILURE
#include <exception>
#include <functional>

class ibWxApp : public wxApp {
public:

	// "enterprise" / "designer" / "codeRunner" / "launcher" — used as
	// dump-file prefix and dialog caption. Override on the concrete app.
	virtual wxString GetExeName() const = 0;

	// Subclass entry points. Defaults match what enterprise / designer
	// were duplicating verbatim — `wxSocketBase::Initialize()` (needed
	// by appData's transport stack) + `wxApp::OnInit()` (the wx side of
	// init: cmd-line parse, image handler registration). Override only
	// if the exe needs extra steps (codeRunner / launcher construct a
	// frame here; enterprise / designer keep the default).
	virtual bool DoOnInit()
	{
		wxSocketBase::Initialize();
		return wxApp::OnInit();
	}

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
		return DoOnInit();
	}

	int OnRun() override
	{
		const wxString exe = GetExeName();
		try {
			return DoOnRun();
		}
		catch (const ibBackendException& e) {
			ReportStartupFault(exe,
				wxT("ibBackendException during startup: ") + e.GetErrorDescription());
			return 1;
		}
		catch (const std::exception& e) {
			ReportStartupFault(exe,
				wxT("std::exception during startup: ") + wxString::FromUTF8(e.what()));
			return 1;
		}
		catch (...) {
			ReportStartupFault(exe,
				wxT("Unknown exception during startup (non-std, non-ibBackend)"));
			return 1;
		}
	}

#if wxUSE_EXCEPTIONS
	bool OnExceptionInMainLoop() override
	{
		// wx delegates here when a C++ exception escapes from an event
		// handler back into the main loop. Returning true keeps the loop
		// running (recoverable); false escalates to OnUnhandledException.
		// ibBackendException is the only "show and continue" class —
		// known business / DB error. Everything else lets the loop unwind.
		try {
			throw;
		}
		catch (const ibBackendException& e) {
			// ⭐⭐ THE LAST RESORT SPEAKS ONLY WHEN NOBODY HAS. A script failure is reported where it
			// happens — ProcessError names the module, the line, the source text and the call stack —
			// and then keeps propagating, which is how it arrives here. Reporting it again said the
			// same thing in the poorest of the available words: the bare sentence, no module, no line.
			//
			// ⚠ AND THE TWO CALLS BELOW WERE THEMSELVES A PAIR. ibJournalError at Error severity IS a
			// dialog — the journal echoes at the severity it was given precisely so a migrated
			// wxLogError callsite keeps its window (journal.cpp) — so the wxMessageBox beside it
			// showed one sentence twice from one catch. Three windows, one failure.
			if (e.IsErrorHandled()) {
				// The record is still written; it is the WINDOW that would be the duplicate.
				ibJournalInfo(wxT("app"), wxT("already reported, loop continues: %s"),
					e.GetErrorDescription());
				return true;
			}

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
		catch (const ibBackendException& e) {
			diag = wxT("Unhandled ibBackendException: ") + e.GetErrorDescription();
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

#if wxUSE_ON_FATAL_EXCEPTION && wxUSE_STACKWALKER
	void OnFatalException() override
	{
		// wxSocketBase / wxDebugReport / wxDebugReportPreviewStd are
		// main-thread-only — they assert wxIsMainThread() inside. A
		// background-thread fault going through the wx path double-faults.
		// Surface a platform-native message + TerminateProcess.
		if (!wxIsMainThread()) {
			const wxString exe = GetExeName();
			const wxString caption = wxT("OES ") + exe + wxT(" - fatal error");
			const wxString msg = wxString::Format(
				wxT("A fatal error occurred on a background thread (tid=%lu).\n\n")
				wxT("A crash dump has been saved to:\n%s\n\n")
				wxT("The application will now close."),
				static_cast<unsigned long>(wxThread::GetCurrentId()),
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
		// The persistent minidump is already on disk via the backend's
		// SEH filter (it runs before wx's chained handler). A second
		// fault during OnExit cleanup is OK — the dump survives.
	}
#endif // wxUSE_ON_FATAL_EXCEPTION

protected:

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
};

#endif // __OES_APP_H__
