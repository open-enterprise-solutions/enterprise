#ifndef _FRMCLIENT_CHANGING_STRUCTURE_H__
#define _FRMCLIENT_CHANGING_STRUCTURE_H__

#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/listBox.h"
#include <wx/sizer.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/dialog.h"

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// THE RESTRUCTURE'S LEDGER — the old desktop's ibDialogApplyChange.What it lists the
// server sent (ibProtocol::ibRequest::ibKind::ApplyChange) — a row per entry, its Level and Text, and the window's Picture; the
// desktop's was handed the engine's ledger. Apply is the answer, the base held on the server until it comes.
class FRMCLIENT_API ibDialogApplyChange : public ibDialog
{
	ibStaticText* m_staticInformation;
	ibListBox* m_resultBox;
	ibButton* m_buttonApply;
	ibButton* m_buttonCancel;

private:
	ibDialogApplyChange(const ibProtocolNode& request, wxWindow* parent);
public:

	static bool ShowApplyChange(const ibProtocolNode& request, wxWindow* parent) {

		if (request.Children().empty())
			return true;

		ibDialogApplyChange dlg(request, parent);
		return dlg.ShowModal() == wxID_OK;
	}
};

#endif
