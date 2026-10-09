#ifndef _function_all_h__
#define _function_all_h__

#include <functional>

#include <wx/dialog.h>
#include <wx/intl.h>
#include <wx/treectrl.h>

#include "protocol/protocolNode.h"

// ALL FUNCTIONS — the objects this person may open, grouped by kind: the desktop's dialog (enterprise/win/dlg), drawn
// from the server's schema (frmserver/schema/runtime/allFunctions.cpp) instead of reading the configuration itself.
// An item opened is the schema's command, made by the window (`open`); the dialog goes when it is done.
class ibDialogFunctionAll : public wxDialog {
public:

	using Open = std::function<bool(long long item)>;

	ibDialogFunctionAll(wxWindow* parent, const ibProtocolNode& shown, Open open,
		wxWindowID id = wxID_ANY,
		const wxString& title = _("All operations"),
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(300, 400),
		long style = wxDEFAULT_DIALOG_STYLE
	);

protected:
	void OnTreeCtrlElementsItemActivated(wxTreeEvent& event);

private:
	void BuildTree(const ibProtocolNode& shown);

	wxTreeCtrl* m_treeCtrlElements;
	Open        m_open;
};


#endif // !_function_all_h__
