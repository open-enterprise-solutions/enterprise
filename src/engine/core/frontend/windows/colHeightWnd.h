#ifndef __COLHEIGHT_H__
#define __COLHEIGHT_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/checkbox.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/dialog.h>

class CGrid;

class CColWidthWnd : public wxDialog {
	wxCheckBox* m_maximumCol;
	wxSpinCtrlDouble* m_spinCtrlWidth;
	wxStdDialogButtonSizer* m_sdbSizerBottom;
	wxButton* m_sdbSizerBottomOK;
	wxButton* m_sdbSizerBottomCancel;
public:

	int GetWidth() const {
		return m_spinCtrlWidth->GetValue();
	}

	CColWidthWnd(CGrid* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Column width"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE);
	virtual ~CColWidthWnd();
};

#endif