#ifndef _FRMCLIENT_TABLE_VIEW_H__
#define _FRMCLIENT_TABLE_VIEW_H__

#include <wx/dialog.h>
#include "deskgui/univ/ctrl/radioButton.h"
#include "deskgui/univ/ctrl/button.h"
#include <wx/sizer.h>

#include "deskgui/univ/dataview/dataview.h"   // ibDataViewViewMode
#include "deskgui/univ/dialog.h"

// THE VIEW MODE WINDOW — the old desktop's (ibTableViewCtrl::ShowViewMode):the mode in
// force and the window's picture are what the server sent (ibProtocol::ibRequest::ibKind::ViewMode), the mode chosen goes back.
class wxTableViewModeDialog : public ibDialog {

public:

	wxTableViewModeDialog(wxWindow* parent, wxWindowID id, ibDataViewViewMode mode, const wxBitmap& picture);

	ibDataViewViewMode GetViewMode() const;

private:

	ibRadioButton* m_radioBtnTree;
	ibRadioButton* m_radioBtnHierarchy;
	ibRadioButton* m_radioBtnList;
	ibStdDialogButtonSizer* m_sdbSizer;
	ibButton* m_sdbSizerOK;
	ibButton* m_sdbSizerCancel;
};

#endif
