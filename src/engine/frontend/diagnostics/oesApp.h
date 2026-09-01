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
