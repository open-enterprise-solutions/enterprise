#ifndef _LINEINPUT_H__
#define _LINEINPUT_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include "deskgui/univ/ctrl/staticText.h"
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include "deskgui/univ/ctrl/textCtrl.h"
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/dialog.h"
#include "core/localization/dictionary.h"   // _() — the dialog's words

class ibDialogLineInput : public ibDialog
{
	ibStaticText* m_staticTextLine;
	ibTextCtrl* m_lineNumber;
	ibButton* m_buttonGoTo;
	ibButton* m_buttonCancel;

public:

	ibDialogLineInput(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Go to line"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE);
	~ibDialogLineInput();

protected:

	virtual void OnButtonGoToClick(wxCommandEvent& event);
	virtual void OnButtonCancelClick(wxCommandEvent& event);
};

#endif 
