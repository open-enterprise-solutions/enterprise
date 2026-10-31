#ifndef _MAIN_ENTERPRISE_APP_H__
#define _MAIN_ENTERPRISE_APP_H__

#include "core/diagnostics/oesApp.h"   // ibWxApp — the journal and the dumps, the light appearance, the refusals said

// THE THIN RUNTIME — the runtime as a thin client: it opens a connection — to an
// application server by its address, or to a file base it opens in its own process (fileserver, loaded for it) — and
// puts up the runtime's main window around it, which logs in and draws what it is answered. It holds no base of its
// own and runs no code: that is the server's, wherever it runs.
class ibAppEnterprise : public ibWxApp {

	// APPLICATION SERVER ENTRY — oes://server[:port]/<base>, or oess:// over TLS; the authorities it trusts, a PEM file
	// (empty — the platform's store)
	wxString m_strAddress;
	wxString m_strTrusted;

	// FILE ENTRY
	wxString m_strFile;

	// SERVER ENTRY — a file base on a DBMS server (PostgreSQL)
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;

	// IB ENTRY
	wxString m_strIBUser;
	wxString m_strIBPassword;

	//LOCALE
	wxString m_strLocale;

	// DEBUG — a file base's engine brings its debug server up (the designer's Start debugging)
	bool m_debugEnable = false;

public:

	virtual wxString GetExeName() const override { return wxT("enterprise-thin"); }
	virtual bool DoOnInit() override;

#if wxUSE_CMDLINE_PARSER
	// this one is called from wxApp::OnInit() to add all supported options
	// to the given parser
	virtual void OnInitCmdLine(wxCmdLineParser& parser) override;
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser) override;
#endif // wxUSE_CMDLINE_PARSER

	virtual int FilterEvent(wxEvent& event) override;

protected:

	//global process events:
	void OnKeyEvent(wxKeyEvent& event);
	void OnSetFocus(wxFocusEvent& event);
};

wxDECLARE_APP(ibAppEnterprise);

#endif
