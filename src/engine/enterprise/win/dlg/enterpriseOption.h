#ifndef _OPTIONS_WND_H__
#define _OPTIONS_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/panel.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/aui/auibook.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/checkbox.h>

class CDialogEnterpriseOption : public wxDialog {
	wxAuiNotebook* m_mainNotebook;
	wxPanel* m_generalPanel;
	wxPanel* m_systemPanel;
	wxCheckBox* m_enableDebugger;
private:
	void FillOption();
public:
	CDialogEnterpriseOption(wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxString& title = _("Options"), 
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(400, 300), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
protected:
	void OnEnableDebugger(wxCommandEvent &event); 
};

#endif 