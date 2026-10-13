#ifndef _MAIN_DESIGNER_APP_H__
#define _MAIN_DESIGNER_APP_H__

#include <wx/app.h>

// THE THIN DESIGNER — the desktop's designer.exe (designer/mainApp) as a thin client: it opens a connection — to an
// application server by its address, or to a file base it opens in its own process (fileserver, loaded for it) — and
// puts up the designer's main window around it, which logs in as the designer and draws what it is answered. The
// configuration it edits is the server's, held by its session there; this process holds none.
class ibAppDesigner : public wxApp {

	// APPLICATION SERVER ENTRY — oes://server[:port]/<base>
	wxString m_strAddress;

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

public:

	virtual bool OnInit() override;

#if wxUSE_CMDLINE_PARSER
	// this one is called from OnInit() to add all supported options
	// to the given parser
	virtual void OnInitCmdLine(wxCmdLineParser& parser) override;
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser) override;
#endif // wxUSE_CMDLINE_PARSER

	virtual int FilterEvent(wxEvent& event) override;

protected:

	//global process events:
	void OnSetFocus(wxFocusEvent& event);
};

wxDECLARE_APP(ibAppDesigner);

#endif
