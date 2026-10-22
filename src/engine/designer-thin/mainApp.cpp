////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the thin designer's application
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include <memory>

#include <wx/cmdline.h>
#include <wx/dirdlg.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the process's journal and dumps
#include "frmclient/artProvider/splash/splashLogo.h"   // the splash's picture, the desktop's
#include "frmclient/backend/appData.h"                 // the base it opens — held there, as the engine's appData held it
#include "frmclient/win/picture.h"                     // ibProtocolPicture — a PNG in base64, made a bitmap
#include "protocol/connectionFile.h"
#include "protocol/connectionServer.h"

#include "mainFrame/mainFrameDesigner.h"
#include "mainFrame/debugger/debugClientImpl.h"

wxIMPLEMENT_APP(ibAppDesigner);

#if wxUSE_CMDLINE_PARSER

void ibAppDesigner::OnInitCmdLine(wxCmdLineParser& parser)
{
	parser.AddOption(wxT("address"), wxT("address"), "Application server: oes://server[:port]/<base>, oess:// over TLS", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("trusted"), wxT("trusted"), "oess://: the authorities trusted, a PEM file (default: the system's store)", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("file"),   wxT("file"),     "Database file path",      wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("srv"),    wxT("server"),   "Database server address", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("p"),      wxT("dbport"),   "Database server port",    wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("db"),     wxT("db"),       "Database name",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("usr"),    wxT("user"),     "Database user",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("pwd"),    wxT("password"), "Database password",       wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_usr"), wxT("ibuser"),   "IB user",                 wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_pwd"), wxT("ibpwd"),    "IB password",             wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("lc"),     wxT("locale"),   "UI locale",               wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	wxApp::OnInitCmdLine(parser);
}

bool ibAppDesigner::OnCmdLineParsed(wxCmdLineParser& parser)
{
	parser.Found(wxT("address"), &appData->m_strAddress);
	parser.Found(wxT("trusted"), &appData->m_strTrusted);

	parser.Found(wxT("file"), &appData->m_strFile);

	parser.Found(wxT("srv"), &appData->m_strServer);
	parser.Found(wxT("p"), &appData->m_strPort);
	parser.Found(wxT("db"), &appData->m_strDatabase);
	parser.Found(wxT("usr"), &appData->m_strUser);
	parser.Found(wxT("pwd"), &appData->m_strPassword);

	parser.Found(wxT("ib_usr"), &appData->m_strIBUser);
	parser.Found(wxT("ib_pwd"), &appData->m_strIBPassword);

	parser.Found(wxT("lc"), &appData->m_strLocale);

	return wxApp::OnCmdLineParsed(parser);
}

#endif

bool ibAppDesigner::OnInit()
{
	// THE JOURNAL AND THE DUMPS — the first act, as every application of the platform does (ibCrashGuard::Install), so
	// whatever goes wrong after it has somewhere to be said: one journal of the process, the client's, the protocol's
	// and a file base's engine's lines in it alike.
	ibCrashGuard::Install(wxT("designer-thin"));

	// THE COMMAND LINE FIRST, then the connection: wx calls OnExit only after an OnInit that succeeded, so nothing is
	// opened before a refusal here could leave it behind (the codeRunner exit crash, 2026-10-07).
	if (!wxApp::OnInit())
		return false;

	wxInitAllImageHandlers();

	// NO BASE NAMED — the desktop designer's: the person picks the folder of a file base, which is made there when
	// there is none yet.
	if (appData->m_strAddress.IsEmpty() && appData->m_strFile.IsEmpty() && appData->m_strServer.IsEmpty()) {
		wxDirDialog dlg(nullptr, _("Select configuration database folder"),
			wxStandardPaths::Get().GetDocumentsDir(),
			wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST | wxDD_NEW_DIR_BUTTON);
		if (dlg.ShowModal() != wxID_OK)
			return false;
		appData->m_strFile = dlg.GetPath();
	}

	// THE SPLASH — the desktop's (designer/mainApp.cpp): up while the base opens and the client logs in, gone with the
	// main window shown — or before a refusal is said, which it would stand over.
	const auto splashDestroy = [](wxSplashScreen* shown) { shown->Destroy(); };
	std::unique_ptr<wxSplashScreen, decltype(splashDestroy)> splash(new ibProcessSplashScreen(ibProtocolPicture(s_splashLogo_png),
		wxSPLASH_CENTRE_ON_SCREEN, -1, nullptr, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE), splashDestroy);

	std::unique_ptr<ibProtocolConnection> connection;
	wxString error;

	if (!appData->m_strAddress.IsEmpty()) {
		// An application server — over its port.
		auto server = std::make_unique<ibProtocolConnectionServer>();
		if (!server->Open(appData->m_strAddress, appData->m_strTrusted, error)) {
			splash.reset();
			wxMessageBox(error, _("OES Designer - startup error"), wxOK | wxICON_ERROR);
			return false;
		}
		connection = std::move(server);
	}
	else if (!appData->m_strFile.IsEmpty() || (!appData->m_strServer.IsEmpty() && !appData->m_strDatabase.IsEmpty())) {
		// A file base — opened in this process, by the library that brings the server in; the designer's is made
		// where there is none yet, as the desktop designer made it.
		ibProtocolNode request;
		request.SetValue(ibProtocol::ibFileBase::Locale, appData->m_strLocale)
			.SetValue(ibProtocol::ibFileBase::Create, true);
		if (!appData->m_strFile.IsEmpty()) {
			request.SetValue(ibProtocol::ibFileBase::Directory, appData->m_strFile);
		}
		else {
			request.SetValue(ibProtocol::ibFileBase::Server, appData->m_strServer)
				.SetValue(ibProtocol::ibFileBase::Port, appData->m_strPort)
				.SetValue(ibProtocol::ibFileBase::User, appData->m_strUser)
				.SetValue(ibProtocol::ibFileBase::Password, appData->m_strPassword)
				.SetValue(ibProtocol::ibFileBase::Database, appData->m_strDatabase);
		}
		auto file = std::make_unique<ibProtocolConnectionFile>();
		if (!file->Open(request.Write(), error)) {
			splash.reset();
			wxMessageBox(error + wxT("\n\n") + (appData->m_strFile.IsEmpty()
				? appData->m_strServer + wxT(" / ") + appData->m_strDatabase : appData->m_strFile),
				_("OES Designer - startup error"), wxOK | wxICON_ERROR);
			return false;
		}
		connection = std::move(file);
	}
	else {
		splash.reset();
		wxMessageBox(
			_("Cannot start designer-thin.exe - no infobase specified.\n\n"
			  "Provide one of:\n"
			  "  --address=oes://<server>[:port]/<base>   (an application server; oess:// over TLS)\n"
			  "  --file=<path>          (Firebird embedded / SQLite file)\n"
			  "  --server=<host> --db=<name> [--dbport=...] [--user=...] [--password=...]"),
			_("OES Designer"),
			wxOK | wxICON_ERROR
		);
		return false;
	}

	// The window holds the client from here: it logs in as it is shown, and a refused start takes it down again —
	// the communicator with it, and a file base's connection closes the base.
	auto* frame = new ibFrontendMainFrameDesigner(
		std::make_unique<ibCommunicator>(std::move(connection)), appData->m_strIBUser, appData->m_strIBPassword);
	if (!frame->Show()) {
		frame->Destroy();
		return false;
	}

	// The designer's bridge onto its debugger, as the desktop's wired it after its login — the frame made the debugger
	// as it logged in (Show).
	ibDebuggerClientBridge::SetDebuggerClientBridge(
		new ibDebuggerClientBridgeDesigner);
	return true;
}
