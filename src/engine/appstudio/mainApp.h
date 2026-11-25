#ifndef _MAIN_STUDIO_APP_H__
#define _MAIN_STUDIO_APP_H__

#include "deskgui/app.h"               // ibWxApp — the journal and the dumps, the machine's appearance, the refusals said

// THE STUDIO — the studio as a thin client: it opens a connection — to an
// application server by its address, or to a file base it opens in its own process (fileserver, loaded for it) — and
// puts up the studio's main window around it, which logs in as the studio and draws what it is answered. The
// application it edits is the server's, held by its session there; this process holds none.
// The base it opens is held by appData (frmclient/backend/appData.h), as the desktop's engine held it: the application
// fills it, a child started beside it is given it (RunApplication).
class ibAppStudio : public ibWxApp {
public:

	virtual wxString GetExeName() const override { return wxT("appstudio"); }
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
	void OnMouseEvent(wxMouseEvent& event);
	void OnSetFocus(wxFocusEvent& event);
};

wxDECLARE_APP(ibAppStudio);

#endif
