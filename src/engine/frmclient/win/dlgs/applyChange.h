#ifndef _FRMCLIENT_CHANGING_STRUCTURE_H__
#define _FRMCLIENT_CHANGING_STRUCTURE_H__

#include <wx/stattext.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/dialog.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// THE RESTRUCTURE'S LEDGER — the desktop's ibDialogApplyChange (frontend/win/dlgs/applyChange.h). What it lists the
// server sent (ibProtocol::ibRequest::ibKind::ApplyChange) — a row per entry, its Level and Text, and the window's Picture; the
// desktop's was handed the engine's ledger. Apply is the answer, the base held on the server until it comes.
class FRMCLIENT_API ibDialogApplyChange : public wxDialog
{
	wxStaticText* m_staticInformation;
	wxListBox* m_resultBox;
	wxButton* m_buttonApply;
	wxButton* m_buttonCancel;

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
