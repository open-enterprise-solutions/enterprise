#ifndef _function_all_h__
#define _function_all_h__

#include <wx/dialog.h>
#include <wx/treectrl.h>

#include "backend/backend_core.h"

class CDialogFunctionAll : public wxDialog {
	wxTreeCtrl* m_treeCtrlElements;
private:
	
	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const class_identifier_t& clsid, const wxString& name = wxEmptyString) const;
	
	void BuildTree();

public:
	virtual bool Show(bool show = true) override {
		if (show) {
			CDialogFunctionAll::BuildTree();
		}
		return wxDialog::Show(show);
	}

	// show the dialog modally and return the value passed to EndModal()
	virtual int ShowModal() override {
		CDialogFunctionAll::BuildTree();
		return wxDialog::ShowModal();
	}

	CDialogFunctionAll(wxWindow* parent, wxWindowID id = wxID_ANY, 
		const wxString& title = _("All operations"), 
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(300, 400), 
		long style = wxDEFAULT_DIALOG_STYLE
	);

protected:
	void OnTreeCtrlElementsOnLeftDClick(wxMouseEvent& event);
};


#endif // !_function_all_h__
