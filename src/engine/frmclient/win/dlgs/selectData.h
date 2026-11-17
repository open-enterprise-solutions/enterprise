#ifndef _SELECT_DATA_WND_H__
#define _SELECT_DATA_WND_H__

#include <vector>

#include <wx/bitmap.h>

#include "deskgui/univ/ctrl/listCtrl.h"
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/dialog.h"

// ONE OF SEVERAL, EACH WITH ITS PICTURE — the server's question as it puts it (ibProtocol::ibRequest::ibKind::Choice): the
// desktop's type list (ShowSelectType) — its classes named and pictured by the server, which holds the configuration.
class ibDialogSelectDataType : public ibDialog {
	ibListCtrl* m_listData;
	ibButton* m_buttonOk;
	ibButton* m_buttonCancel;
public:
	// The item's number among those given; false — cancelled.
	bool ShowModal(int& chosen);
	ibDialogSelectDataType(wxWindow* parent, const wxString& caption, const wxArrayString& captions, const std::vector<wxBitmap>& pictures);
	virtual ~ibDialogSelectDataType();

protected:

	void OnListItemActivated(wxListEvent& event);
};

#endif
