#include <wx/app.h>
#include <wx/config.h>
#include <wx/cmdline.h>
#include <wx/bitmap.h>
#include <wx/socket.h>
#include <wx/stdpaths.h>
#include <wx/sysopt.h>
#include <wx/utils.h>
#include <wx/filename.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

#include "backend/appData.h"
#include "backend/webServer/ibWebServer.h"
#include "ibWebSessionManager.h"
#include "ibWebMainFrame.h"

// Defined in ibWebSessionRoutes.cpp
namespace httplib { class Server; }
void RegisterSessionRoutes(httplib::Server* svr);

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {

	//server mode
	{ wxCMD_LINE_OPTION, "srv", "srv", "Start using server address", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "p", "p", "Start using port", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_OPTION, "db", "db", "Start from current db", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_OPTION, "usr", "usr", "Start from current login", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "pwd", "pwd", "Start from current password", wxCMD_LINE_VAL_STRING, NULL },

	//ib user
	{ wxCMD_LINE_OPTION, "ib_usr", "ib_usr", "IB username", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "ib_pwd", "ib_pwd", "IB password", wxCMD_LINE_VAL_STRING, NULL },

	//debug
	{ wxCMD_LINE_SWITCH, "debug", "debug", "Enable debug mode", wxCMD_LINE_VAL_NONE, NULL },

	//web server
	{ wxCMD_LINE_OPTION, "wp", "web-port", "Web server port (default: 8765)", wxCMD_LINE_VAL_NUMBER, NULL },
	{ wxCMD_LINE_OPTION, "wd", "web-dir", "Web static files directory", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_SWITCH, "h", "help", "Show this help message.", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP },
	{ wxCMD_LINE_PARAM, NULL, NULL, "File to open.", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	{ wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_NONE, 0 }
};

int main(int argc, char** argv)
{
	wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "daemon");

	wxInitializer initializer(argc, argv);
	if (!initializer.IsOk()) {
		fprintf(stderr, "Failed to initialize the wxWidgets library, aborting.");
		return -1;
	}

	// Parse command line
	wxCmdLineParser parser(s_cmdLineDesc, argc, argv);
	if (parser.Parse() != 0) {
		return 1;
	}

	wxString strServer, strPort, strDatabase, strUser, strPassword;
	wxString strIBUser, strIBPassword;

	// SERVER ENTRY
	parser.Found(wxT("srv"), &strServer);
	parser.Found(wxT("p"), &strPort);
	parser.Found(wxT("db"), &strDatabase);
	parser.Found(wxT("usr"), &strUser);
	parser.Found(wxT("pwd"), &strPassword);

	//user db
	parser.Found(wxT("ib_usr"), &strIBUser);
	parser.Found(wxT("ib_pwd"), &strIBPassword);

	//debug
	bool debugEnable = parser.FoundSwitch("debug") == wxCMD_SWITCH_ON;

	// Web server options
	long webPort = 8765;
	parser.Found(wxT("wp"), &webPort);

	wxString strWebDir;
	if (!parser.Found(wxT("wd"), &strWebDir)) {
		// Default: look for web/ next to executable
		wxString exeStr(argv[0]);
		wxFileName exePath(exeStr);
		strWebDir = exePath.GetPath() + wxFileName::GetPathSeparator() + wxT("web");
	}

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

	wxSocketBase::Initialize();

	// Init appData
	bool connected = appDataCreateServer(ibRunMode::eSERVICE_MODE,
		strServer, strPort, strUser, strPassword, strDatabase, wxT("en")
	);

	// If connection is failed then exit from application
	if (!connected) {
		fprintf(stderr, "Failed to connect to database.\n");
		return 1;
	}

	// Initialize web main frame (enables CreateNewForm() in service mode)
	ibWebMainFrame::Initialize();
	ibWebSessionManager::Initialize();

	if (!appData->Connect(strIBUser, strIBPassword)) {
		fprintf(stderr, "Failed to authenticate.\n");
		return 1;
	}

	// Start web server
	if (!ibWebServer::Initialize(static_cast<int>(webPort), strWebDir)) {
		fprintf(stderr, "Failed to start web server on port %ld.\n", webPort);
		return 1;
	}

	// Register session endpoints (need frontend.dll which daemon links)
	RegisterSessionRoutes(webServer->GetHttpServer());

	fprintf(stdout, "OES Web Server started on http://0.0.0.0:%ld\n", webPort);
	fprintf(stdout, "Static files: %s\n", strWebDir.ToStdString().c_str());

	// Block until server stops (Ctrl+C or signal)
	webServer->WaitForShutdown();

	// Cleanup
	ibWebServer::Destroy();
	ibWebSessionManager::Destroy();
	ibWebMainFrame::Destroy();

	return 0;
}
