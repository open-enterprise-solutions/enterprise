#ifndef _MAIN_DESIGNER_APP_H__
#define _MAIN_DESIGNER_APP_H__

#include <wx/app.h>
#include <wx/aui/framemanager.h>
#include <wx/socket.h>

class CEnterpriseApp : public wxApp {

	bool m_debugEnable;
	
	// SERVER ENTRY
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;

	// IB ENTRY
	wxString m_strIBUser;
	wxString m_strIBPassword;
	
	wxLocale m_locale;
public:
	virtual bool OnInit();
#if wxUSE_ON_FATAL_EXCEPTION
	virtual void OnUnhandledException() override;
#endif
#if wxUSE_ON_FATAL_EXCEPTION && wxUSE_STACKWALKER
	virtual void OnFatalException() override;
#endif
	virtual int OnRun() override;
	virtual int OnExit() override;

#if wxUSE_CMDLINE_PARSER
    // this one is called from OnInit() to add all supported options
    // to the given parser 
	virtual void OnInitCmdLine(wxCmdLineParser& parser);
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser);
#endif // wxUSE_CMDLINE_PARSER

	virtual int FilterEvent(wxEvent& event) override;

protected:

	//global process events:
	void OnKeyEvent(wxKeyEvent &event);
	void OnMouseEvent(wxMouseEvent &event);
	void OnSetFocus(wxFocusEvent &event);
};

wxDECLARE_APP(CEnterpriseApp);

#endif 