////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "backend/appData.h"

#include <wx/clipbrd.h>
#include <wx/cmdline.h>
#include <wx/debugrpt.h>
#include <wx/fs_arc.h>
#include <wx/fs_filter.h>
#include <wx/fs_mem.h>
#include <wx/stdpaths.h>

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
	// FILE ENTRY 
	parser.AddOption(wxT("file"), wxT("pwd"), "Start from current dir", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	// SERVER ENTRY 
	parser.AddOption(wxT("srv"), wxT("srv"), "Start using server address", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("p"), wxT("p"), "Start using port", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("db"), wxT("db"), "Start from current database", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("usr"), wxT("usr"), "Start from current user", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("pwd"), wxT("pwd"), "Start from current password", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	// USER 
	parser.AddOption(wxT("ib_usr"), wxT("ib_usr"), "Start from current user", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_pwd"), wxT("ib_pwd"), "Start from current password", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	// LOCALE
	parser.AddOption(wxT("lc"), wxT("lc"), "Start from current locale", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

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

bool ibAppDesigner::OnInit()
{
	wxSocketBase::Initialize();
	return wxApp::OnInit();
}

int ibAppDesigner::OnRun()
{
	// Abnormal Termination Handling
#if wxUSE_ON_FATAL_EXCEPTION && wxUSE_STACKWALKER
	::wxHandleFatalExceptions(true);
#endif

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
		const wxString& strLastError = ibBackendException::GetLastError();
		if (!strLastError.IsEmpty()) wxMessageBox(strLastError);
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
	mainFrameCreate(ibFrontendDocMDIFrameDesigner);
	if (!appData->Connect(m_strIBUser, m_strIBPassword)) {
		const wxString& strLastError = ibBackendException::GetLastError();
		if (!strLastError.IsEmpty()) wxMessageBox(strLastError);
		mainFrameDestroy();
		if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();
		return 1;
	}
	if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();
	bool allow = mainFrameShow();
	if (!allow) return 1;
	return wxApp::OnRun();
}

void ibAppDesigner::OnUnhandledException()
{
}

#ifdef __WXMSW__
#include "backend/system/value/valueOLE.h"
#endif

void ibAppDesigner::OnFatalException()
{
	//generate dump
	wxDebugReport report;

	report.AddCurrentContext();
	report.AddCurrentContext();

	//release all created com-objects
#ifdef __WXMSW__
	ibValueOLE::ReleaseComObjects();
#endif

	if (wxSocketBase::IsInitialized())
		wxSocketBase::Shutdown();

	appDataDestroy();

	//show error
	wxDebugReportPreviewStd preview;
	if (preview.Show(report)) report.Process();
}

int ibAppDesigner::OnExit()
{
	//release all created com-objects
#ifdef __WXMSW__
	ibValueOLE::ReleaseComObjects();
#endif

	if (wxSocketBase::IsInitialized())
		wxSocketBase::Shutdown();

	mainFrameDestroy();

	bool suсcess_exit = wxApp::OnExit();

	appDataDestroy();

	// Allow clipboard data to persist after close
	if (wxTheClipboard->Open()) {
		wxTheClipboard->Flush();
		wxTheClipboard->Close();
	}

	return suсcess_exit;
}