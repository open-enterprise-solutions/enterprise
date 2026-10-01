#ifndef _ERROR_DLG_H__
#define _ERROR_DLG_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/stc/stc.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/checkbox.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/dialog.h>

#include "frontend/mainFrame/settings/editorsettings.h"
#include "frontend/mainFrame/settings/fontcolorsettings.h"

class FRONTEND_API ibDialogError : public wxDialog {
public:

	//Editor setting 
	void SetEditorSettings(const ibEditorSettings& settings);
	//Font setting 
	void SetFontColorSettings(const ibFontColorSettings& settings);

	ibDialogError(class ibFrontendMainFrame* parent, wxWindowID id = wxID_ANY,
		const wxString& title = _("Critical failure"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxCAPTION | wxSYSTEM_MENU | wxCLOSE_BOX | wxSTAY_ON_TOP);

	virtual ~ibDialogError();

	void SetErrorMessage(const wxString& strError) {
		m_errorOutput->SetReadOnly(false);
		m_errorOutput->SetText(strError);
		m_errorOutput->SetReadOnly(true);
	}

private:

	void OnButtonCloseProgramClick(wxCommandEvent& event);
	void OnButtonGotoDesignerClick(wxCommandEvent& event);
	void OnButtonCloseWindowClick(wxCommandEvent& event);

	wxStyledTextCtrl* m_errorOutput;

	wxButton* m_buttonCloseProgram;
	wxButton* m_buttonGotoDesigner;
	wxButton* m_buttonCloseWindow;
};

#endif 
