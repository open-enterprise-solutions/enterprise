#ifndef _MAIN_DESIGNER_APP_H__
#define _MAIN_DESIGNER_APP_H__

#include <wx/app.h>
#include <wx/aui/framemanager.h>
#include <wx/socket.h>

#include <memory>

#include "frontend/diagnostics/oesApp.h"

class ibAppEnterprise : public ibWxApp {

	bool m_debugEnable;

	// FILE ENTRY
	wxString m_strFile;

	// SERVER ENTRY
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;

	// IB ENTRY
	wxString m_strIBUser;
	wxString m_strIBPassword;

#ifdef DEBUG
	//LOCALE
	wxString m_strLocale = wxT("en");
#else
	//LOCALE
	wxString m_strLocale;
#endif // wxDEBUG

public:

	// ibWxApp pre-wires Install / WrapStartup / 3 exception overrides.
	// We only fill in the exe-specific name + boot bodies.
	wxString GetExeName() const override { return wxT("enterprise"); }

	// DoOnInit defaulted on the base (wxSocketBase::Initialize + wxApp::OnInit).
	int DoOnRun() override;

	int OnExit() override;

public:

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

wxDECLARE_APP(ibAppEnterprise);

#endif 