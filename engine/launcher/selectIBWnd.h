#ifndef _SELECT_IB_WND_H__
#define _SELECT_IB_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/button.h>
#include <wx/dialog.h>

class CSelectIBWnd : public wxDialog
{
	wxStaticText* m_staticName;
	wxStaticText* m_staticPath;
	wxTextCtrl* m_textCtrlName;
	wxTextCtrl* m_textCtrlPath;
	wxButton* m_buttonSelect;
	wxButton* m_buttonOk;
	wxButton* m_buttonCancel;

public:

	wxString GetIbName() const {
		return m_textCtrlName->GetValue(); 
	}

	wxString GetIbPath() const {
		return m_textCtrlPath->GetValue();
	}

	void Init(const wxString &ibName, wxString &ibPath) {
		m_textCtrlName->SetValue(ibName);
		m_textCtrlPath->SetValue(ibPath);
	}

	CSelectIBWnd(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("IB"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 110), long style = wxCAPTION | wxCLOSE_BOX | wxDEFAULT_DIALOG_STYLE | wxTAB_TRAVERSAL);
	virtual ~CSelectIBWnd();

protected:

	void OnSelectButton(wxCommandEvent &event);
};

#endif