#include <wx/app.h>
#include <wx/config.h>
#include <wx/cmdline.h>
#include <wx/bitmap.h>
#include <wx/socket.h>
#include <wx/stdpaths.h>
#include <wx/sysopt.h>
#include <wx/utils.h> 

#ifdef __WXMSW__
#include <windows.h>
#endif

#include "backend/appData.h"
#include "backend/session/session.h"
#include "frontend/diagnostics/oesConsole.h"

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {
	// Short names stay legacy; long names match wenterprise-server /
	// enterprise / designer so RunApplication emits one flag form that
	// parses across every bin.
	{ wxCMD_LINE_OPTION, "srv",    "server",   "Database server address", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "p",      "dbport",   "Database server port",    wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "db",     "db",       "Database name",           wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "usr",    "user",     "Database user",           wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "pwd",    "password", "Database password",       wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "ib_usr", "ibuser",   "IB user",                 wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "ib_pwd", "ibpwd",    "IB password",             wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "lc",     "locale",   "UI locale",               wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_SWITCH, "d",      "debug",    "Enable debugger attach",  wxCMD_LINE_VAL_NONE,   0 },
	{ wxCMD_LINE_SWITCH, "h",      "help",     "Show this help message.", wxCMD_LINE_VAL_NONE,   wxCMD_LINE_OPTION_HELP },
	{ wxCMD_LINE_PARAM,  NULL,     NULL,       "File to open.",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	{ wxCMD_LINE_NONE,   NULL,     NULL,       NULL,                      wxCMD_LINE_VAL_NONE,   0 }
};

int main(int argc, char** argv)
{
	wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "daemon");

	// wxInitializer + wxSocketBase::Initialize + ibCrashGuard::Install,
	// in one line. Headless: no wxApp, faults from worker / debug-listener
	// threads write minidumps and surface platform-native messages
	// instead of silent abort.
	ibOesConsoleBoot boot(wxT("daemon"), argc, argv);
	if (!boot.IsOk()) {
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

	// wxSocketBase::Initialize() already ran inside ibOesConsoleBoot above.

	// Init appData (sets AccessMode internally based on runMode).
	bool connected = appDataCreateServer(ibRunMode::eRUNTIME_MODE,
		strServer, strPort, strUser, strPassword, strDatabase, wxT("en")
	);

	// If connection is failed then exit from application
	if (!connected) {
		wxMessageBox(_("Failed to connection!"), _("Connection error"), wxOK | wxCENTRE | wxICON_ERROR);
		appDataDestroy();
		return 1;
	}

	// CreateSession + Authenticate. Registry's lifecycle listeners (wired
	// in ibApplicationData ctor) handle metadata load + per-session
	// runtime bring-up through OnFirstConnect / OnAuthenticated.
	//
	// The daemon has no frame, so the holder stays right here: this scope
	// IS the owner, and the session ends when the holder goes out of it.
	ibSessionHolder holder = appData->CreateSession();
	if (!holder ||
	    holder->Open(strIBUser, strIBPassword) != ibSession::OpenResult::Authenticated) {
		appDataDestroy();
		return 1;
	}

	// Without appDataDestroy() the session-updater thread and DB connection
	// stayed alive past main() and the process crashed during static teardown.
	appDataDestroy();

	return 0;
}