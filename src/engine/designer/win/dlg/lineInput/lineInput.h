#ifndef _LINEINPUT_H__
#define _LINEINPUT_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/button.h>
#include <wx/dialog.h>

class CDialogLineInput : public wxDialog
{
	wxStaticText* m_staticTextLine;
	wxTextCtrl* m_lineNumber;
	wxButton* m_buttonGoTo;
	wxButton* m_buttonCancel;

public:

	CDialogLineInput(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Go to line"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE);
	~CDialogLineInput();

protected:

	virtual void OnButtonGoToClick(wxCommandEvent& event);
	virtual void OnButtonCancelClick(wxCommandEvent& event);
};

#endif 
