#ifndef _MAINAPP_H__
#define _MAINAPP_H__

#include <wx/app.h>

class CMainApp : public wxApp
{
	wxLocale m_locale;
public:
	virtual bool OnInit();
};

wxDECLARE_APP(CMainApp);

#endif 