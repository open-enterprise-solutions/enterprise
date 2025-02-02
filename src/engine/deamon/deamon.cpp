#include <wx/app.h>
#include <wx/config.h>
#include <wx/cmdline.h>
#include <wx/bitmap.h>
#include <wx/socket.h>
#include <wx/stdpaths.h>
#include <wx/sysopt.h>
#include <wx/utils.h> 

#include <windows.h>

#include "backend/appData.h"

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {

	//server mode 
	{ wxCMD_LINE_OPTION, "srv", "srv", "Start using server address", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "p", "p", "Start using port", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_OPTION, "db", "db", "Start from current db", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_OPTION, "usr", "usr", "Start from current login", wxCMD_LINE_VAL_STRING, NULL },
	{ wxCMD_LINE_OPTION, "p", "p", "Start from current password", wxCMD_LINE_VAL_STRING, NULL },

	{ wxCMD_LINE_SWITCH, "h", "help", "Show this help message.", wxCMD_LINE_VAL_STRING, wxCMD_LINE_OPTION_HELP },
	{ wxCMD_LINE_PARAM, NULL, NULL, "File to open.", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	{ wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_NONE, 0 }
};

int main(int argc, char** argv)
{
	wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "deamon");

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
	bool connected = appDataCreate(eRunMode::eENTERPRISE_MODE,
		strServer, strPort, strUser, strPassword, strDatabase
	);

	// If connection is failed then exit from application 
	if (!connected) {
		wxMessageBox(_("Failed to connection!"), _("Connection error"), wxOK | wxCENTRE | wxICON_ERROR);
		return 1;
	}

	if (!appData->Connect(strIBUser, strIBPassword)) {
		return 1;
	}

	return 0;
}