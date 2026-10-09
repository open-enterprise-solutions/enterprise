#ifndef _FRMCLIENT_GENERATION_DATA_WND_H__
#define _FRMCLIENT_GENERATION_DATA_WND_H__

#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/dialog.h>

#include "frmclient/backend/backend_core.h"   // ibMetaID
#include "protocol/protocolNode.h"

// THE OBJECT TO BASE A NEW ONE ON — the desktop's ibDialogGeneration (frontend/win/dlgs/generation.h). What it lists
// the server sent (ibProtocolRequestKind::Generation) — a row per object generated, its Id, Caption and Picture, and
// the window's Picture; the desktop's read them of the configuration.
class ibDialogGeneration : public wxDialog {
	wxListCtrl* m_listData;
	wxButton* m_buttonOk;
	wxButton* m_buttonCancel;
public:

	bool ShowModal(ibMetaID& id);

	ibDialogGeneration(const ibProtocolNode& request);
	virtual ~ibDialogGeneration();

protected:
	virtual void OnListItemSelected(wxListEvent& event);
};

#endif
