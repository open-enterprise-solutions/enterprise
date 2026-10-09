#ifndef _FRMCLIENT_TABLE_VIEW_H__
#define _FRMCLIENT_TABLE_VIEW_H__

#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/button.h>
#include <wx/sizer.h>

#include "frmclient/win/ctrls/dataview/dataview.h"   // ibDataViewViewMode

// THE VIEW MODE WINDOW — the desktop's (frontend/win/ctrls/tableView.cpp, ibTableViewCtrl::ShowViewMode): the mode in
// force and the window's picture are what the server sent (ibProtocolRequestKind::ViewMode), the mode chosen goes back.
class wxTableViewModeDialog : public wxDialog {

public:

	wxTableViewModeDialog(wxWindow* parent, wxWindowID id, ibDataViewViewMode mode, const wxBitmap& picture);

	ibDataViewViewMode GetViewMode() const;

private:

	wxRadioButton* m_radioBtnTree;
	wxRadioButton* m_radioBtnHierarchy;
	wxRadioButton* m_radioBtnList;
	wxStdDialogButtonSizer* m_sdbSizer;
	wxButton* m_sdbSizerOK;
	wxButton* m_sdbSizerCancel;
};

#endif
