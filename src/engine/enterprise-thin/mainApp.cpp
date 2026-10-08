////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the thin runtime's application
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include <memory>

#include <wx/cmdline.h>
#include <wx/image.h>
#include <wx/msgdlg.h>

#include "frmclient/diagnostics/journal.h"
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

	return wxApp::OnCmdLineParsed(parser);
}

#endif

bool ibAppEnterprise::OnInit()
{
	// The client's journal — the first act, so whatever goes wrong after it has somewhere to be said.
	ibClientJournal::Open(wxT("enterprise-thin"));

	// THE COMMAND LINE FIRST, then the connection: wx calls OnExit only after an OnInit that succeeded, so nothing is
	// opened before a refusal here could leave it behind (the codeRunner exit crash, 2026-10-07).
	if (!wxApp::OnInit())
		return false;

	wxInitAllImageHandlers();

	std::unique_ptr<ibProtocolConnection> connection;
	wxString error;

	if (!m_strAddress.IsEmpty()) {
		// An application server — over its port.
		auto server = std::make_unique<ibProtocolConnectionServer>();
		if (!server->Open(m_strAddress, error)) {
			wxMessageBox(error, _("OES Enterprise - startup error"), wxOK | wxICON_ERROR);
			return false;
		}
		connection = std::move(server);
	}
	else if (!m_strFile.IsEmpty() || (!m_strServer.IsEmpty() && !m_strDatabase.IsEmpty())) {
		// A file base — opened in this process, by the library that brings the server in.
		ibProtocolNode request;
		request.SetValue(ibProtocolName::Locale, m_strLocale);
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
			wxMessageBox(error + wxT("\n\n") + (m_strFile.IsEmpty() ? m_strServer + wxT(" / ") + m_strDatabase : m_strFile),
				_("OES Enterprise - startup error"), wxOK | wxICON_ERROR);
			return false;
		}
		connection = std::move(file);
	}
	else {
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
