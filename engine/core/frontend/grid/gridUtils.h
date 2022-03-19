#ifndef _GRIDUTILS_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/dialog.h>

class CInputSectionWnd : public wxDialog
{
private:

	wxTextCtrl* m_section;
	wxStdDialogButtonSizer* m_sdbSizer;
	wxButton* m_sdbSizerOK;
	wxButton* m_sdbSizerCancel;

public:

	CInputSectionWnd(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxEmptyString, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE);
	~CInputSectionWnd();

	void SetSection(const wxString &section) { m_section->SetValue(section); }
	wxString GetSection() { return m_section->GetValue(); }
};

#endif // !_GRIDUTILS_H__
