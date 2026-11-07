#ifndef _FRMCLIENT_GENERATION_DATA_WND_H__
#define _FRMCLIENT_GENERATION_DATA_WND_H__

#include "gui/univ/ctrl/listCtrl.h"
#include "gui/univ/ctrl/button.h"
#include <wx/sizer.h>
#include "gui/univ/dialog.h"

#include "frmclient/backend/backend_core.h"   // ibMetaID
#include "protocol/protocolNode.h"

// THE OBJECT TO BASE A NEW ONE ON — the old desktop's ibDialogGeneration.What it lists
// the server sent (ibProtocol::ibRequest::ibKind::Generation) — a row per object generated, its Id, Caption and Picture, and
// the window's Picture; the desktop's read them of the configuration.
class ibDialogGeneration : public ibDialog {
	ibListCtrl* m_listData;
	ibButton* m_buttonOk;
	ibButton* m_buttonCancel;
public:

	bool ShowModal(ibMetaID& id);

	ibDialogGeneration(const ibProtocolNode& request);
	virtual ~ibDialogGeneration();

protected:
	virtual void OnListItemSelected(wxListEvent& event);
};

#endif
