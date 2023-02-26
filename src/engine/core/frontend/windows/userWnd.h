#ifndef _USER_WND_H__
#define _USER_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/gdicmn.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include <3rdparty/guid/guid.h>

#include "frontend/theme/luna_auitabart.h"
#include "frontend/theme/luna_dockart.h"

class CUserWnd : public wxDialog
{
	bool m_bInitialized;

	wxString md5Password;

	wxAuiNotebook* m_mainNotebook;
	wxPanel* m_main;
	wxStaticText* m_staticName;
	wxStaticText* m_staticFullName;
	wxTextCtrl* m_textName;
	wxTextCtrl* m_textFullName;
	wxStaticLine* m_staticline;
	wxStaticText* m_staticPassword;
	wxTextCtrl* m_textPassword;
	wxPanel* m_other;
	wxStdDialogButtonSizer* m_bottom;
	wxButton* m_bottomOK;
	wxButton* m_bottomCancel;

	wxStaticText* m_staticRole;
	wxStaticText* m_staticInterface;
	wxChoice* m_choiceRole;
	wxChoice* m_choiceInterface;

	Guid m_userGuid; 

private:

	// Virtual event handlers, overide them in your derived class
	virtual void OnPasswordText(wxCommandEvent& event);
	virtual void OnOKButtonClick(wxCommandEvent& event);
	virtual void OnCancelButtonClick(wxCommandEvent& event); 

public:

	bool ReadUserData(const Guid &guid, bool copy = false);

	CUserWnd(wxWindow* parent, wxWindowID id = wxID_ANY, 
		const wxString& title = wxT("User"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(400, 264), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	virtual ~CUserWnd();
};

#endif 