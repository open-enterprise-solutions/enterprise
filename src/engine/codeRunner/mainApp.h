#ifndef _MAINAPP_H__
#define _MAINAPP_H__

#include <wx/app.h>
#include "codeRunner.h"
#include "frontend/diagnostics/oesApp.h"

class ibAppCodeRunner :
	public ibWxApp {
public:

	wxString GetExeName() const override { return wxT("codeRunner"); }

	bool DoOnInit() override;
	int OnExit() override;

	void AppendOutput(const wxString& str);

private:
	ibFrameCodeRunner* m_codeRunner = nullptr;
};

wxDECLARE_APP(ibAppCodeRunner);

#endif 