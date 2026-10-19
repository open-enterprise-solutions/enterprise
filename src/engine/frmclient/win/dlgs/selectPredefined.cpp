////////////////////////////////////////////////////////////////////////////
//	Description : the designer's choice of a DECLARED value — see selectPredefined.h
////////////////////////////////////////////////////////////////////////////

#include "selectPredefined.h"

#include <wx/treectrl.h>
#include <wx/dialog.h>
#include <wx/sizer.h>

#include "protocol/protocol.h"

// The window's own parts — nothing outside this file has business with a tree node or a branch.
namespace {

// THE ANSWER RIDES THE NODE: the number the server knows the value by. A type's node carries none — it is
// not a value.
struct ibDesignerRefItem : public wxTreeItemData {
	explicit ibDesignerRefItem(int id) : m_id(id) {
	}
	int m_id;
};

// WHAT ONE TYPE OFFERS, as the server wrote it: its empty reference, then everything it declares, nested as it is
// declared.
void ibFillOneTypeBranch(wxTreeCtrl* tree, const wxTreeItemId& root, const ibProtocolNode& branch)
{
	for (const ibProtocolNode& value : branch.Children()) {
		const wxTreeItemId item = tree->AppendItem(root, value.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Name),
			-1, -1, new ibDesignerRefItem(static_cast<int>(value.GetInt(ibProtocol::ibRequest::ibPredefinedSelector::Id))));
		ibFillOneTypeBranch(tree, item, value);
	}
}

// EVERY ADMITTED REFERENCE TYPE IS A BRANCH — the composite case is not a second window but one more
// branch, which is what makes "choose a declared value" a single question however many types the
// parameter admits.
void ibFillDesignerRefTree(wxTreeCtrl* tree, const ibProtocolNode& types)
{
	tree->DeleteAllItems();
	const wxTreeItemId hidden = tree->AddRoot(wxT("*"));   // hidden — the branches are the roots seen

	for (const ibProtocolNode& branch : types.Children())
		ibFillOneTypeBranch(tree, tree->AppendItem(hidden, branch.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Title)),
			branch);

	tree->ExpandAll();

	// STANDING ON SOMETHING CHOOSABLE: the first type's empty reference, so OK alone is an answer.
	wxTreeItemIdValue branchCookie, leafCookie;
	const wxTreeItemId branch = tree->GetFirstChild(hidden, branchCookie);
	if (branch.IsOk()) {
		const wxTreeItemId leaf = tree->GetFirstChild(branch, leafCookie);
		if (leaf.IsOk())
			tree->SelectItem(leaf);
	}
}

} // namespace

bool ibShowPredefinedSelector(wxWindow* parent, const ibProtocolNode& request, ibProtocolNode& response)
{
	wxWindow* const owner = parent != nullptr ? wxGetTopLevelParent(parent) : nullptr;
	const wxSize dialogSize = parent != nullptr
		? wxDLG_UNIT(parent, wxSize(170, 130)) : wxDefaultSize;

	wxDialog dlg(owner, wxID_ANY, _("Select a predefined value"), wxDefaultPosition,
		dialogSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	wxTreeCtrl* valueTree = new wxTreeCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_HIDE_ROOT);
	ibFillDesignerRefTree(valueTree, request.FindChild(ibProtocol::ibRequest::ibPredefinedSelector::Types));
	valueTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&](wxTreeEvent& event) {
		if (valueTree->GetItemData(event.GetItem()) != nullptr)
			dlg.EndModal(wxID_OK);
		});

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(valueTree, 1, wxALL | wxEXPAND, dlg.FromDIP(6));
	sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, dlg.FromDIP(6));
	dlg.SetSizer(sizer);

	if (dlg.ShowModal() != wxID_OK)
		return false;

	const wxTreeItemId atValue = valueTree->GetSelection();
	const auto* chosen = atValue.IsOk()
		? dynamic_cast<ibDesignerRefItem*>(valueTree->GetItemData(atValue)) : nullptr;
	if (chosen == nullptr)
		return false;   // the type node itself is not a value

	response.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Id, chosen->m_id);
	return true;
}
