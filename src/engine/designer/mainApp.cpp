////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"   // DrainLastErrors for the startup-failure dialog
#include "backend/backend_mainFrame.h"
#include "backend/debugger/debugClientBridge.h"
#include "frontend/session/guiSession.h"   // transitively pulls backend/session/session.h
#include "backend/session/sessionRegistry.h"
#include "mainFrame/debugger/debugClientImpl.h"

#include <wx/clipbrd.h>
#include <wx/cmdline.h>
#include <wx/fs_arc.h>
#include <wx/fs_filter.h>
#include <wx/fs_mem.h>
#include <wx/stdpaths.h>

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

wxIMPLEMENT_APP(ibAppDesigner);

//////////////////////////////////////////////////////////////////////////////////

//mainFrame
#include "mainFrame/mainFrameDesigner.h"

//////////////////////////////////////////////////////////////////////////////////
#if wxUSE_CMDLINE_PARSER
#include <wx/cmdline.h>

void ibAppDesigner::OnInitCmdLine(wxCmdLineParser& parser)
{
	// Same layout as enterprise.exe — short names legacy, long names match
	// wenterprise-server so RunApplication emits flags that parse in all bins.
	parser.AddOption(wxT("file"),   wxT("file"),     "Database file path",      wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("srv"),    wxT("server"),   "Database server address", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("p"),      wxT("dbport"),   "Database server port",    wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("db"),     wxT("db"),       "Database name",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("usr"),    wxT("user"),     "Database user",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("pwd"),    wxT("password"), "Database password",       wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_usr"), wxT("ibuser"),   "IB user",                 wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_pwd"), wxT("ibpwd"),    "IB password",             wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("lc"),     wxT("locale"),   "UI locale",               wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	return wxApp::OnInitCmdLine(parser);
}

bool ibAppDesigner::OnCmdLineParsed(wxCmdLineParser& parser)
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

	return wxApp::OnCmdLineParsed(parser);
}
#endif

//////////////////////////////////////////////////////////////////////////////////

// ibDesignerSession — concrete GUI session for designer.exe. OnCreateSession
// instantiates the exe-specific frame class (ibFrontendMainFrameDesigner),
// which is not exported to frontend.dll. Declared here so designer.exe owns
// its own concrete session type.
class ibDesignerSession : public ibGUISession {
public:
	using ibGUISession::ibGUISession;

	bool OnCreateSession() override {
		AttachFrame(new ibFrontendMainFrameDesigner);
		return m_frame != nullptr;
	}
};

int ibAppDesigner::DoOnRun()
{
	// Get the data directory
	bool ret = false;

	if (m_strFile.IsEmpty() && m_strServer.IsEmpty()) {
		// No command-line args — show folder picker
		wxDirDialog dlg(nullptr, _("Select configuration database folder"),
			wxStandardPaths::Get().GetDocumentsDir(),
			wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST | wxDD_NEW_DIR_BUTTON);
		if (dlg.ShowModal() == wxID_OK) {
			m_strFile = dlg.GetPath();
		} else {
			return 0; // user cancelled
		}
	}

	if (m_strFile.IsEmpty()) {
		ret = appDataCreateServer(ibRunMode::eDESIGNER_MODE,
			m_strServer, m_strPort, m_strUser, m_strPassword, m_strDatabase, m_strLocale
		);
	}
	else {
		ret = appDataCreateFile(ibRunMode::eDESIGNER_MODE,
			m_strFile, m_strLocale
		);
	}

	if (!ret) {
		// Same chain-aware reporting as enterprise.exe — see there.
		const std::vector<wxString> chain = ibBackendException::DrainLastErrors();
		if (!chain.empty()) {
			wxString combined;
			for (std::size_t i = 0; i < chain.size(); ++i) {
				if (!combined.IsEmpty()) combined += wxT("\n--\n");
				combined += chain[i];
			}
			wxMessageBox(combined, _("OES Designer - startup error"),
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
	// Flow (designer IDE):
	//   1. CreateSession<ibDesignerSession> — registry Add plus
	//      OnCreateSession hook that instantiates the designer frame on
	//      the main thread (DesignerExclusivePolicy veto happens inside
	//      the registry Connect before OnCreateSession runs).
	//   2. Authenticate — creds, dialog fallback via session->GetFrame().
	//   3. LoadMetadata — compile descriptors (intellisense).
	//   4. mainFrameShow — Designer kind → EnsureRuntime no-op;
	//                      AllowRun = true unconditionally.

	// AccessMode was set by appData's ctor based on runMode. Registry
	// listeners (wired in appData ctor) handle BindSessionToThread,
	// LoadMetadata, CreateRoot + CompileRoot through OnFirstConnect /
	// OnAuthenticated — nothing to compose here.
	ibSession* session = nullptr;
	wxString openError;
	ibSession::OpenResult openResult = ibSession::OpenResult::Failed;
	try {
		session = appData->CreateSession<ibDesignerSession>();
		if (session != nullptr) {
			openResult = session->Open(m_strIBUser, m_strIBPassword);
			if (openResult != ibSession::OpenResult::Authenticated) {
				session->Close();
				session = nullptr;
			}
		}
	} catch (const ibBackendException& e) {
		openError = e.GetErrorDescription();
		session   = nullptr;
		openResult = ibSession::OpenResult::Failed;
	} catch (const std::exception& e) {
		openError = wxString::FromUTF8(e.what());
		session   = nullptr;
		openResult = ibSession::OpenResult::Failed;
	}

	if (session == nullptr) {
		if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();
		// Same Cancel-vs-Failed split as enterprise.exe — see enterprise/
		// mainApp.cpp for the rationale.
		if (openResult == ibSession::OpenResult::Cancelled)
			return 0;
		const wxString message = openError.IsEmpty()
			? wxString(_("Authentication failed"))
			: openError;
		wxMessageBox(message, _("OES Designer"), wxOK | wxICON_ERROR);
		return 1;
	}

	// Wire the debug-client bridge AFTER m_session->Open(): debugClient
	// (the global ms_debugClient) is constructed inside metadataCreate
	// which fires from the OnFirstConnect listener during NotifyAuthenticated.
	// Calling SetDebuggerClientBridge before that point silently no-ops
	// (debugClient == nullptr branch in debugClientBridge.cpp), so
	// ibDebuggerClient's adapter never gets the bridge and OnEnterLoop /
	// OnSessionStart fire into the void — F5 then hits a breakpoint that
	// the IDE never displays.
	ibDebuggerClientBridge::SetDebuggerClientBridge(
		new ibDebuggerClientBridgeDesigner);

	if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();
	if (!session->ShowFrame()) return 1;
	return wxApp::OnRun();
}

int ibAppDesigner::OnExit()
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