////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the thin runtime's application
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include <memory>

#include <wx/cmdline.h>
#include <wx/image.h>
#include <wx/msgdlg.h>

#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::Install — the process's journal and dumps
#include "frmclient/artProvider/splash/splashLogo.h"   // the splash's picture, the desktop's
#include "frmclient/win/picture.h"                     // ibProtocolPicture — a PNG in base64, made a bitmap
#include "protocol/connectionFile.h"
#include "protocol/connectionServer.h"

#include "mainFrame/mainFrameEnterprise.h"

wxIMPLEMENT_APP(ibAppEnterprise);

#if wxUSE_CMDLINE_PARSER

void ibAppEnterprise::OnInitCmdLine(wxCmdLineParser& parser)
{
	parser.AddOption(wxT("address"), wxT("address"), "Application server: oes://server[:port]/<base>", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
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

	wxApp::OnInitCmdLine(parser);
}

bool ibAppEnterprise::OnCmdLineParsed(wxCmdLineParser& parser)
{
	parser.Found(wxT("address"), &m_strAddress);

	parser.Found(wxT("file"), &m_strFile);

	parser.Found(wxT("srv"), &m_strServer);
	parser.Found(wxT("p"), &m_strPort);
	parser.Found(wxT("db"), &m_strDatabase);
	parser.Found(wxT("usr"), &m_strUser);
	parser.Found(wxT("pwd"), &m_strPassword);

	parser.Found(wxT("ib_usr"), &m_strIBUser);
	parser.Found(wxT("ib_pwd"), &m_strIBPassword);

	parser.Found(wxT("lc"), &m_strLocale);

	m_debugEnable = parser.FoundSwitch(wxT("debug")) == wxCMD_SWITCH_ON;

	return wxApp::OnCmdLineParsed(parser);
}

#endif

bool ibAppEnterprise::OnInit()
{
	// THE JOURNAL AND THE DUMPS — the first act, as every application of the platform does (ibCrashGuard::Install), so
	// whatever goes wrong after it has somewhere to be said: one journal of the process, the client's, the protocol's
	// and a file base's engine's lines in it alike.
	ibCrashGuard::Install(wxT("enterprise-thin"));

	// THE COMMAND LINE FIRST, then the connection: wx calls OnExit only after an OnInit that succeeded, so nothing is
	// opened before a refusal here could leave it behind (the codeRunner exit crash, 2026-10-07).
	if (!wxApp::OnInit())
		return false;

	wxInitAllImageHandlers();

	// THE SPLASH — the desktop's (enterprise/mainApp.cpp): up while the base opens and the client logs in, gone with the
	// main window shown — or before a refusal is said, which it would stand over.
	const auto splashDestroy = [](wxSplashScreen* shown) { shown->Destroy(); };
	std::unique_ptr<wxSplashScreen, decltype(splashDestroy)> splash(new ibProcessSplashScreen(ibProtocolPicture(s_splashLogo_png),
		wxSPLASH_CENTRE_ON_SCREEN, -1, nullptr, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE), splashDestroy);

	std::unique_ptr<ibProtocolConnection> connection;
	wxString error;

	if (!m_strAddress.IsEmpty()) {
		// An application server — over its port.
		auto server = std::make_unique<ibProtocolConnectionServer>();
		if (!server->Open(m_strAddress, error)) {
			splash.reset();
			wxMessageBox(error, _("OES Enterprise - startup error"), wxOK | wxICON_ERROR);
			return false;
		}
		connection = std::move(server);
	}
	else if (!m_strFile.IsEmpty() || (!m_strServer.IsEmpty() && !m_strDatabase.IsEmpty())) {
		// A file base — opened in this process, by the library that brings the server in.
		ibProtocolNode request;
		request.SetValue(ibProtocolName::Locale, m_strLocale)
			.SetValue(ibProtocolName::Debug, m_debugEnable);
		if (!m_strFile.IsEmpty()) {
			request.SetValue(ibProtocolName::Directory, m_strFile);
		}
		else {
			request.SetValue(ibProtocolName::Server, m_strServer)
				.SetValue(ibProtocolName::Port, m_strPort)
				.SetValue(ibProtocolName::User, m_strUser)
				.SetValue(ibProtocolName::Password, m_strPassword)
				.SetValue(ibProtocolName::Database, m_strDatabase);
		}
		auto file = std::make_unique<ibProtocolConnectionFile>();
		if (!file->Open(request.Write(), error)) {
			splash.reset();
			wxMessageBox(error + wxT("\n\n") + (m_strFile.IsEmpty() ? m_strServer + wxT(" / ") + m_strDatabase : m_strFile),
				_("OES Enterprise - startup error"), wxOK | wxICON_ERROR);
			return false;
		}
		connection = std::move(file);
	}
	else {
		splash.reset();
		wxMessageBox(
			_("Cannot start enterprise-thin.exe - no infobase specified.\n\n"
			  "Provide one of:\n"
			  "  --address=oes://<server>[:port]/<base>   (an application server)\n"
			  "  --file=<path>          (Firebird embedded / SQLite file)\n"
			  "  --server=<host> --db=<name> [--dbport=...] [--user=...] [--password=...]"),
			_("OES Enterprise"),
			wxOK | wxICON_ERROR
		);
		return false;
	}

	// The window holds the client from here: it logs in as it is shown, and a refused start takes it down again —
	// the communicator with it, and a file base's connection closes the base.
	auto* frame = new ibFrontendMainFrameEnterprise(
		std::make_unique<ibCommunicator>(std::move(connection)), m_strIBUser, m_strIBPassword);
	if (!frame->Show()) {
		frame->Destroy();
		return false;
	}
	return true;
}
