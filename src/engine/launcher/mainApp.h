#ifndef _MAINAPP_H__
#define _MAINAPP_H__

#include <wx/app.h>
#include "launcher.h"
#include "frontend/diagnostics/oesApp.h"

class ibAppLauncher :
	public ibWxApp {
public:
	wxString GetExeName() const override { return wxT("launcher"); }

	bool DoOnInit() override;
	int OnExit() override;

private:
	ibFrameLauncher* m_launcher = nullptr;
};

wxDECLARE_APP(ibAppLauncher);

#endif 