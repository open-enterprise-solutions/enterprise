////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"   // DrainLastErrors for the startup-failure dialog
#include "backend/backend_mainFrame.h"
#include "frontend/session/guiSession.h"   // transitively pulls backend/session/session.h
#include "backend/session/sessionRegistry.h"

#include <wx/clipbrd.h>
#include <wx/fs_arc.h>
#include <wx/fs_filter.h>
#include <wx/fs_mem.h>

#ifdef __WXMSW__
#include <windows.h>   // DisableProcessWindowsGhosting
#include "backend/system/value/valueOLE.h"   // ibValueOLE::ReleaseComObjects in normal OnExit
#endif

#include "resources/splashLogo.xpm"

#if wxVERSION_NUMBER >= 2905 && wxVERSION_NUMBER <= 3100
#include <wx/xrc/xh_auinotbk.h>
#elif wxVERSION_NUMBER > 3100
#include <wx/xrc/xh_aui.h>
#endif

wxIMPLEMENT_APP(ibAppEnterprise);

//////////////////////////////////////////////////////////////////////////////////

//mainFrame
#include "mainFrame/mainFrameEnterprise.h"

//////////////////////////////////////////////////////////////////////////////////
#if wxUSE_CMDLINE_PARSER
#include <wx/cmdline.h>

void ibAppEnterprise::OnInitCmdLine(wxCmdLineParser& parser)
{
	// Short names are legacy (matched what the /flag-style spawner used);
	// long names match wenterprise-server so one builder emits flags that
	// parse identically across enterprise/designer/daemon/wes.
	parser.AddOption(wxT("file"),   wxT("file"),     "Database file path",      wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("srv"),    wxT("server"),   "Database server address", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("p"),      wxT("dbport"),   "Database server port",    wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("db"),     wxT("db"),       "Database name",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("usr"),    wxT("user"),     "Database user",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("pwd"),    wxT("password"), "Database password",       wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_usr"), wxT("ibuser"),   "IB user",                 wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_pwd"), wxT("ibpwd"),    "IB password",             wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("lc"),     wxT("locale"),   "UI locale",               wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddSwitch(wxT("debug"),  wxT("debug"),    "Enable debug attach.",    wxCMD_LINE_VAL_NONE);

	return wxApp::OnInitCmdLine(parser);
}

bool ibAppEnterprise::OnCmdLineParsed(wxCmdLineParser& parser)
{
	// FILE ENTRY 
	parser.Found(wxT("file"), &m_strFile);

	// SERVER ENTRY
	parser.Found(wxT("srv"), &m_strServer);
	parser.Found(wxT("p"), &m_strPort);
	parser.Found(wxT("db"), &m_strDatabase);
	parser.Found(wxT("usr"), &m_strUser);
	parser.Found(wxT("pwd"), &m_strPassword);

	// USER 
	parser.Found(wxT("ib_usr"), &m_strIBUser);
	parser.Found(wxT("ib_pwd"), &m_strIBPassword);

	// LOCALE
	parser.Found(wxT("lc"), &m_strLocale);

	// DEBUG 
	m_debugEnable = parser.FoundSwitch(wxT("debug")) == wxCMD_SWITCH_ON;

	return wxApp::OnCmdLineParsed(parser);
}
#endif

//////////////////////////////////////////////////////////////////////////////////

// No exe-specific session class. What made enterprise.exe's session
// different was that it built ibFrontendMainFrameEnterprise — and that
// now happens the other way round, in DoOnRun, with the window built
// around the holder. ibGUISession carries the rest (login prompt, what
// a forced close means on desktop), so there is nothing left to derive.

int ibAppEnterprise::DoOnRun()
{
	// Decide whether this is a file-based launch (Firebird embedded /
	// SQLite — `--file=…`) or a server launch (`--server=… --db=…`).
	// Reject the no-arg case explicitly: the previous behaviour fell
	// through to appDataCreateServer with empty server/port/db, the
	// PostgreSQL driver opened a connection with all-empty credentials
	// and the resulting ThrowDatabaseException re-entered the
	// half-initialised session registry. Surfacing the missing-arg case
	// here is far easier to diagnose than the assertion behind it.
	bool ret = false;

	if (!m_strFile.IsEmpty()) {
		ret = appDataCreateFile(ibRunMode::eRUNTIME_MODE,
			m_strFile, m_strLocale
		);
	}
	else if (!m_strServer.IsEmpty() && !m_strDatabase.IsEmpty()) {
		ret = appDataCreateServer(ibRunMode::eRUNTIME_MODE,
			m_strServer, m_strPort, m_strUser, m_strPassword, m_strDatabase, m_strLocale
		);
	}
	else {
		wxMessageBox(
			_("Cannot start enterprise.exe - no infobase specified.\n\n"
			  "Provide one of:\n"
			  "  --file=<path>          (Firebird embedded / SQLite file)\n"
			  "  --server=<host> --db=<name> [--dbport=…] [--user=…] [--password=…]\n\n"
			  "Or launch through launcher.exe to pick a saved infobase."),
			_("OES Enterprise"),
			wxOK | wxICON_ERROR
		);
		return 1;
	}

	if (!ret) {
		// Show the whole chain of failures recorded on this thread, not
		// just the most recent one — the visible cause is often a wrapper
		// thrown deep in the bring-up after the real root cause already
		// failed (e.g. metadata-load wraps a driver-level FB error).
		const std::vector<wxString> chain = ibBackendException::DrainLastErrors();
		if (!chain.empty()) {
			wxString combined;
			for (std::size_t i = 0; i < chain.size(); ++i) {
				if (!combined.IsEmpty()) combined += wxT("\n--\n");
				combined += chain[i];
			}
			wxMessageBox(combined, _("OES Enterprise - startup error"),
				wxOK | wxICON_ERROR);
		}
		return 1;
	}

	ibProcessSplashScreen* splashScreenLoader =
		new ibProcessSplashScreen(wxBitmap(splashLogo_xpm),
			wxSPLASH_CENTRE_ON_SCREEN,
			-1, nullptr, -1, wxDefaultPosition, wxDefaultSize,
			wxBORDER_SIMPLE
		);

	// Init handlers
	wxInitAllImageHandlers();

	wxXmlResource::Get()->InitAllHandlers();
#if wxVERSION_NUMBER >= 2905 && wxVERSION_NUMBER <= 3100
	wxXmlResource::Get()->AddHandler(new wxAuiNotebookXmlHandler);
#elif wxVERSION_NUMBER > 3100
	wxXmlResource::Get()->AddHandler(new wxAuiXmlHandler);
#endif

#ifdef __WXMSW__
	::DisableProcessWindowsGhosting();
#endif

#if DEBUG 
	wxLog::AddTraceMask(wxTRACE_MemAlloc);
	wxLog::AddTraceMask(wxTRACE_ResAlloc);
#if wxUSE_LOG
#if defined(__WXGTK__)
	wxLog::AddTraceMask("clipboard");
#elif defined(__WXMSW__)
	wxLog::AddTraceMask(wxTRACE_OleCalls);
#endif
#endif // wxUSE_LOG
#endif

	// Log to stderr while working on the command line
	delete wxLog::SetActiveTarget(new wxLogStderr);

	// Message output to the same as the log target
	delete wxMessageOutput::Set(new wxMessageOutputLog);

#if wxUSE_LIBPNG
	wxImage::AddHandler(new wxPNGHandler);
#endif

	// Support loading files from memory
	// Used to load the XRC preview, but could be useful elsewhere
	wxFileSystem::AddHandler(new wxMemoryFSHandler);

	// Support for loading files from archives
	wxFileSystem::AddHandler(new wxArchiveFSHandler);
	wxFileSystem::AddHandler(new wxFilterFSHandler);

#if wxUSE_LIBPNG
	wxImage::AddHandler(new wxPNGHandler);
#endif
	// Flow (enterprise thick client):
	//   1. CreateSession — the registry registers the session and hands
	//      back the holder.
	//   2. Open — attaches user creds; the login dialog is standalone, so
	//      no window is needed yet.
	//   3. new ibFrontendMainFrameEnterprise(std::move(holder)) — the
	//      window takes ownership; from here it IS the session's life.
	//   4. Show — EnsureRuntime creates root + ProcUnits, AllowRun fires
	//      BeforeStart.
	// Stash flags so OnFirstConnect listener picks them up when
	// LoadMetadata fires from the registry event chain.
	appData->m_loadMetadataFlags = m_debugEnable
		? _app_start_create_debug_server_flag
		: _app_start_default_flag;

	// AccessMode was set by appData's ctor based on runMode. Registry
	// listeners (wired in appData ctor) handle BindSessionToThread,
	// LoadMetadata, CreateRoot + CompileRoot + AttachRuntime
	// through OnFirstConnect / OnAuthenticated.
	// The holder lives on this stack frame until it is handed to the main
	// form. Every failure path below simply lets it go — dropping the
	// holder IS closing the session (anonymous sys_session row removed,
	// registry entry dropped). There is no error-path cleanup to forget.
	ibSessionHolder holder;
	wxString openError;
	ibSession::OpenResult openResult = ibSession::OpenResult::Failed;
	try {
		holder = appData->CreateSession<ibGUISession>();
		if (holder) {
			openResult = holder->Open(m_strIBUser, m_strIBPassword);
			if (openResult != ibSession::OpenResult::Authenticated)
				holder.Reset();
		}
	} catch (const ibBackendException& e) {
		openError = e.GetErrorDescription();
		holder.Reset();
		openResult = ibSession::OpenResult::Failed;
	} catch (const std::exception& e) {
		openError = wxString::FromUTF8(e.what());
		holder.Reset();
		openResult = ibSession::OpenResult::Failed;
	}

	if (!holder) {
		if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();
		// Cancelled = user clicked Cancel on the login dialog. They
		// already know they cancelled — a second "Authentication failed"
		// modal on top is noise. Exit silently with success code so the
		// launcher that spawned us doesn't read a non-zero exit as
		// "something went wrong, retry/report".
		if (openResult == ibSession::OpenResult::Cancelled)
			return 0;
		const wxString message = openError.IsEmpty()
			? wxString(_("Authentication failed"))
			: openError;
		wxMessageBox(message, _("OES Enterprise"), wxOK | wxICON_ERROR);
		return 1;
	}

	if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();

	// The window IS the session's owner: it takes the holder and from here
	// the session lives exactly as long as the window.
	auto* frame = new ibFrontendMainFrameEnterprise(std::move(holder));
	if (!frame->Show()) {
		// BeforeStart vetoed (or the runtime never came up). Destroy() on a
		// top-level window is DELAYED — wxPendingDelete, pruned on the next
		// idle — and we are about to return without ever entering the event
		// loop, so the destructor will not run and the holder will not be
		// released on this path. registry->Stop() in OnExit is what removes
		// the session here. Every other path goes through the window.
		frame->Destroy();
		return 1;
	}
	return wxApp::OnRun();
}

int ibAppEnterprise::OnExit()
{
	//release all created com-objects
#ifdef __WXMSW__
	ibValueOLE::ReleaseComObjects();
#endif

	if (wxSocketBase::IsInitialized())
		wxSocketBase::Shutdown();

	// Tear every session down through the session manager BEFORE
	// wxApp::OnExit. registry->Stop() submits Remove@Urgent for each
	// session in m_own and drains the queue — OnDisconnect listeners
	// fire while the wx event loop is still alive, so any frame-Destroy
	// scheduled from there gets dispatched. Without this the event
	// loop dies first and the Destroy events stay queued. Idempotent —
	// ~ibApplicationData calls Stop again best-effort.
	if (auto* registry = ibApplicationData::GetSessionRegistry())
		registry->Stop();

	bool success_exit = wxApp::OnExit();

	appDataDestroy();

	// Allow clipboard data to persist after close
	if (wxTheClipboard->Open()) {
		wxTheClipboard->Flush();
		wxTheClipboard->Close();
	}

	return success_exit;
}