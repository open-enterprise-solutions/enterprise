#ifndef _MAIN_APP_H__
#define _MAIN_APP_H__

#include <wx/app.h>
#include <wx/aui/framemanager.h>
#include <wx/socket.h>

#include "core/appData.h"

class CMainApp : public wxApp
{
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

	virtual int FilterEvent(wxEvent& event) override;

protected:

	//global process events:
	void OnKeyEvent(wxKeyEvent &event);
	void OnMouseEvent(wxMouseEvent &event);
	void OnSetFocus(wxFocusEvent &event);
};

wxDECLARE_APP(CMainApp);

#endif 