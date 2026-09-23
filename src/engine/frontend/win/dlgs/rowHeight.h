#ifndef __ROWHEIGHT_H__
#define __ROWHEIGHT_H__

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

class ibGridEditor;

class ibDialogRowHeight : public wxDialog {
	wxCheckBox* m_autoHeight;
	wxSpinCtrlDouble* m_spinCtrlHeight;
	wxStdDialogButtonSizer* m_sdbSizerBottom;
	wxButton* m_sdbSizerBottomOK;
	wxButton* m_sdbSizerBottomCancel;
public:

	int GetHeight() const {
		return m_spinCtrlHeight->GetValue(); 
	}

	// On: the rows have no height of their own and follow their text (spreadsheetDescription.h, HasRowSize).
	bool IsAutoHeight() const { return m_autoHeight->GetValue(); }

	ibDialogRowHeight(ibGridEditor* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Row height"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE);
	virtual ~ibDialogRowHeight();
};

#endif