#include <wx/app.h>
#include <wx/config.h>
#include <wx/cmdline.h>
#include <wx/bitmap.h>
#include <wx/socket.h>
#include <wx/stdpaths.h>
#include <wx/sysopt.h>
#include <wx/utils.h> 

#include <windows.h>

#include "core/appData.h"

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {

	//embedded mode 
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

int main(int argc, char **argv)
{
	wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "deamon");

	wxInitializer initializer;

	if (!initializer.IsOk()) {
		fprintf(stderr, "Failed to initialize the wxWidgets library, aborting.");
		return -1;
	}

	// Parse command line
	wxCmdLineParser parser(s_cmdLineDesc, argc, argv);
	if (parser.Parse() != 0) {
		return 1;
	}

	wxSystemOptions::SetOption("debug.enable", "true");

	// Get the data directory
	wxStandardPaths& stdPaths = wxStandardPaths::Get();
	wxString dataDir = stdPaths.GetDataDir();

	wxString pathIB, serverIB, portIB, userIB, passwordIB;

	if (parser.Found("ib", &pathIB)) {
		dataDir = pathIB;
	}

	parser.Found("srv", &serverIB);
	parser.Found("port", &portIB);

	parser.Found("l", &userIB);
	parser.Found("p", &passwordIB);

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

	wxSocketBase::Initialize();

	// Init appData
	bool connected = ApplicationData::CreateAppData(eDBMode::eFirebird, eSERVICE_MODE, dataDir);

	// If connection is failed then exit from application 
	if (!connected) {
		wxMessageBox(_("Failed to connection!"), _("Connection error"), wxOK | wxCENTRE | wxICON_ERROR);
		return 1;
	}

	// This is not necessary for Enterprise to work. However, Windows sets the Current Working Directory
	// to the directory from which a .md file was opened, if opened from Windows Explorer.
	// This puts an unneccessary lock on the directory.
	// This changes the CWD to the already locked app directory as a workaround
#ifdef __WXMSW__
	::wxSetWorkingDirectory(dataDir);
#endif

	if (!appData->Initialize(userIB, passwordIB)) {
		return 1;
	}

	return 0;
}