#ifndef _MAIN_DESIGNER_APP_H__
#define _MAIN_DESIGNER_APP_H__

#include <wx/app.h>

// THE THIN DESIGNER — the desktop's designer.exe (designer/mainApp) as a thin client: it opens a connection — to an
// application server by its address, or to a file base it opens in its own process (fileserver, loaded for it) — and
// puts up the designer's main window around it, which logs in as the designer and draws what it is answered. The
// configuration it edits is the server's, held by its session there; this process holds none.
// The base it opens is held by appData (frmclient/backend/appData.h), as the desktop's engine held it: the application
// fills it, a child started beside it is given it (RunApplication).
class ibAppDesigner : public wxApp {
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
	void OnMouseEvent(wxMouseEvent& event);
	void OnSetFocus(wxFocusEvent& event);
};

wxDECLARE_APP(ibAppDesigner);

#endif
