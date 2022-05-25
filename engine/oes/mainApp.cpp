////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "databaseLayer/databaseLayer.h"

#include <wx/apptrait.h>
#include <wx/clipbrd.h>
#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/docview.h>
#include <wx/debugrpt.h>
#include <wx/evtloop.h>
#include <wx/fs_arc.h>
#include <wx/fs_filter.h>
#include <wx/fs_mem.h>
#include <wx/splash.h>
#include <wx/stdpaths.h>
#include <wx/sysopt.h>

#include "core/resources/splashLogo.xpm"

#if wxVERSION_NUMBER >= 2905 && wxVERSION_NUMBER <= 3100
#include <wx/xrc/xh_auinotbk.h>
#elif wxVERSION_NUMBER > 3100
#include <wx/xrc/xh_aui.h>
#endif

//////////////////////////////////////////////////////////////////////////////////

class CSplashScreen : public wxSplashScreen {
public:
	CSplashScreen(const wxBitmap& bitmap, long splashStyle, int milliseconds,
		wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP) :
		wxSplashScreen(bitmap, splashStyle, milliseconds,
			parent, id,
			pos, size, style
		)
	{
	}
	virtual int FilterEvent(wxEvent& event) wxOVERRIDE {
		return Event_Skip;
	}
};

//////////////////////////////////////////////////////////////////////////////////

bool CMainApp::OnInit()
{
	wxDateTime::SetCountry(wxDateTime::Country::USA);

	m_locale.AddCatalogLookupPathPrefix(_T("lang"));
	m_locale.AddCatalog(m_locale.GetCanonicalName());

#if defined(__WXMSW__) && defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	_CrtSetBreakAlloc(9554);
	_CrtSetBreakAlloc(9553);
	_CrtSetBreakAlloc(9552);
#endif 

	return m_locale.Init(wxLANGUAGE_ENGLISH);
}

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {
	{ wxCMD_LINE_OPTION, "m", "mode", "Run mode for this application. ", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_SWITCH, "d", "debug", "Enable debugging for enterprise application.", wxCMD_LINE_VAL_NONE, NULL },

	//embedded mode 
	{ wxCMD_LINE_OPTION, "embedded", "embedded", "Start enterprise in embedded mode", wxCMD_LINE_VAL_NONE, NULL },
	{ wxCMD_LINE_OPTION, "ib", "ib", "Start enterprise from current path", wxCMD_LINE_VAL_STRING, NULL },

	//server mode 
	{ wxCMD_LINE_OPTION, "srv", "srv", "Start enterprise using server address", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "port", "port", "Start enterprise using port", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_OPTION, "l", "l", "Start enterprise from current login", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "p", "p", "Start enterprise from current password", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_SWITCH, "h", "help", "Show this help message.", wxCMD_LINE_VAL_STRING, wxCMD_LINE_OPTION_HELP },
	{ wxCMD_LINE_PARAM, NULL, NULL, "File to open.", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	{ wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_NONE, 0 }
};

wxIMPLEMENT_APP(CMainApp);

int CMainApp::OnRun()
{
	// Abnormal Termination Handling
#if wxUSE_ON_FATAL_EXCEPTION && wxUSE_STACKWALKER
	::wxHandleFatalExceptions(true);
#elif defined(_WIN32) && defined(__MINGW32__)
	// Structured Exception handlers are stored in a linked list at FS:[0]
	// THIS MUST BE A LOCAL VARIABLE - windows won't use an object outside of the thread's stack valueFrame
	EXCEPTION_REGISTRATION ex;
	ex.handler = StructuredExceptionHandler;
	asm volatile ("movl %%fs:0, %0" : "=r" (ex.prev));
	asm volatile ("movl %0, %%fs:0" : : "r" (&ex));
#endif

	// Parse command line
	wxCmdLineParser parser(s_cmdLineDesc, argc, argv);

	if (parser.Parse() != 0) {
		return 1;
	}

	wxString mode;
	parser.Found(wxT("m"), &mode);

	bool debugEnable = parser.Found(wxT("d"));

	if (debugEnable) {
		wxSystemOptions::SetOption("debug.enable", "true");
	}
	else {
		wxSystemOptions::SetOption("debug.enable", "false");
	}

	eRunMode modeRun = eRunMode::ENTERPRISE_MODE;

	if (mode == wxT("designer")) {
		modeRun = eRunMode::DESIGNER_MODE;
	}

	// Get the data directory
	wxStandardPaths& stdPaths = wxStandardPaths::Get();
	wxString dataDir = stdPaths.GetDataDir();

	bool embeddedMode = parser.Found(wxT("embedded"));

	wxString pathIB, serverIB, portIB, userIB, passwordIB;

	if (parser.Found("ib", &pathIB)) {
		dataDir = pathIB;
	}

	parser.Found("srv", &serverIB);
	parser.Found("port", &portIB);

	parser.Found("l", &userIB);
	parser.Found("p", &passwordIB);

	CSplashScreen* splashWnd = new CSplashScreen(wxBitmap(splashLogo_xpm),
		wxSPLASH_CENTRE_ON_SCREEN,
		-1, NULL, -1, wxDefaultPosition, wxDefaultSize,
		wxBORDER_SIMPLE);

	wxApp::SetTopWindow(splashWnd);

	//Needed to get the splashscreen to paint
	splashWnd->Update();

	if (modeRun == eRunMode::ENTERPRISE_MODE)
	{
		// Using a space so the initial 'w' will not be capitalized in wxLogGUI dialogs
		wxApp::SetAppName(_("OES Enterprise"));

		// Creating the wxConfig manually so there will be no space
		// The old config (if any) is returned, delete it
		delete wxConfigBase::Set(new wxConfig(wxT("Enterprise")));
	}
	else if (modeRun == eRunMode::DESIGNER_MODE)
	{
		// Using a space so the initial 'w' will not be capitalized in wxLogGUI dialogs
		wxApp::SetAppName(_("OES Designer"));

		// Creating the wxConfig manually so there will be no space
		// The old config (if any) is returned, delete it
		delete wxConfigBase::Set(new wxConfig(wxT("Designer")));
	}

	// Log to stderr while working on the command line
	delete wxLog::SetActiveTarget(new wxLogStderr);

	// Message output to the same as the log target
	delete wxMessageOutput::Set(new wxMessageOutputLog);

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

#if _DEBUG 
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

#if wxUSE_LIBPNG
	wxImage::AddHandler(new wxPNGHandler);
#endif

	// Support loading files from memory
	// Used to load the XRC preview, but could be useful elsewhere
	wxFileSystem::AddHandler(new wxMemoryFSHandler);

	// Support for loading files from archives
	wxFileSystem::AddHandler(new wxArchiveFSHandler);
	wxFileSystem::AddHandler(new wxFilterFSHandler);

	wxSocketBase::Initialize();

	// Init appData
	appDataCreate(dataDir);

	// If connection is failed then exit from application 
	if (!databaseLayer->IsOpen()) {
		wxMessageBox(databaseLayer->GetErrorMessage(), wxT("Failed to connection!"), wxOK | wxCENTRE | wxICON_ERROR);
		return 1;
	}

	// This is not necessary for Enterprise to work. However, Windows sets the Current Working Directory
	// to the directory from which a .md file was opened, if opened from Windows Explorer.
	// This puts an unneccessary lock on the directory.
	// This changes the CWD to the already locked app directory as a workaround
#ifdef __WXMSW__
	::wxSetWorkingDirectory(dataDir);
#endif

	if (!appData->Initialize(modeRun, userIB, passwordIB)) {
		return 1;
	}

	if (splashWnd->Destroy()) {
		return wxApp::OnRun();
	}

	return 0;
}

#include "core/compiler/valueOLE.h"

void CMainApp::OnUnhandledException()
{
	//release all created com-objects
	CValueOLE::ReleaseCoObjects();

	//close connection to db
	databaseLayer->Close();

	// Allow clipboard data to persist after close
	if (wxTheClipboard->IsOpened()) {
		if (wxTheClipboard->Open()) {
			wxTheClipboard->Flush();
			wxTheClipboard->Close();
		}
	}

	wxApp::OnUnhandledException();
}

void CMainApp::OnFatalException()
{
	//release all created com-objects
	CValueOLE::ReleaseCoObjects();

	//generate dump
	wxDebugReport report;
	report.AddExceptionDump();

	//close connection to db
	databaseLayer->Close();

	//decr socket
	if (wxSocketBase::IsInitialized()) {
		wxSocketBase::Shutdown();
	}

	// Allow clipboard data to persist after close
	if (wxTheClipboard->IsOpened()) {
		if (wxTheClipboard->Open()) {
			wxTheClipboard->Flush();
			wxTheClipboard->Close();
		}
	}

	//show error
	wxDebugReportPreviewStd preview;
	if (preview.Show(report)) report.Process();
}

int CMainApp::OnExit()
{
	if (wxSocketBase::IsInitialized()) {
		wxSocketBase::Shutdown();
	}

	appDataDestroy();

	if (!wxTheClipboard->IsOpened()) {
		if (!wxTheClipboard->Open()) {
			return wxApp::OnExit();
		}
	}

	// Allow clipboard data to persist after close
	wxTheClipboard->Flush();
	wxTheClipboard->Close();

	return wxApp::OnExit();
}