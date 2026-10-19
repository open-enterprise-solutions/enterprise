////////////////////////////////////////////////////////////////////////////
//	Description : PredefinedSelector dialog — EmptyRef / predefined / enum for the thin designer
////////////////////////////////////////////////////////////////////////////

#include "selectPredefined.h"

#include <wx/choicdlg.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/treectrl.h>

#include "frmclient/mainFrame/mainFrame.h"

namespace {

// Tree node: MetaId + Guid + Name of the chosen leaf (Written built on OK).
struct ibDeclaredItem : public wxTreeItemData {
	ibDeclaredItem(s32 metaId, const wxString& guid, const wxString& name, const wxString& typeTitle)
		: m_metaId(metaId), m_guid(guid), m_name(name), m_typeTitle(typeTitle) {
	}
	s32      m_metaId;
	wxString m_guid;
	wxString m_name;
	wxString m_typeTitle;
};

// Fill one type branch from protocol children (Name, Guid; nested children for folders).
void FillValues(wxTreeCtrl* tree, const wxTreeItemId& parent, const ibProtocolNode& node,
	s32 metaId, const wxString& typeTitle)
{
	for (const ibProtocolNode& child : node.Children()) {
		const wxString name = child.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Name);
		if (name.IsEmpty() && !child.Has(ibProtocol::ibRequest::ibPredefinedSelector::Guid))
			continue;
		const wxString guid = child.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Guid);
		const wxTreeItemId at = tree->AppendItem(parent, name.IsEmpty() ? wxString(wxT("EmptyRef")) : name,
			-1, -1, new ibDeclaredItem(metaId, guid, name.IsEmpty() ? wxString(wxT("EmptyRef")) : name, typeTitle));
		FillValues(tree, at, child, metaId, typeTitle);
	}
}

void FillTree(wxTreeCtrl* tree, const ibProtocolNode& types)
{
	tree->DeleteAllItems();
	const wxTreeItemId hidden = tree->AddRoot(wxT("*"));
	for (const ibProtocolNode& branch : types.Children()) {
		const wxString title = branch.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Title);
		const s32 metaId = static_cast<s32>(branch.GetInt(ibProtocol::ibRequest::ibPredefinedSelector::MetaId));
		const wxTreeItemId at = tree->AppendItem(hidden, title);
		FillValues(tree, at, branch, metaId, title);
	}
	tree->ExpandAll();
	wxTreeItemIdValue branchCookie, leafCookie;
	const wxTreeItemId firstBranch = tree->GetFirstChild(hidden, branchCookie);
	if (firstBranch.IsOk()) {
		const wxTreeItemId firstLeaf = tree->GetFirstChild(firstBranch, leafCookie);
		if (firstLeaf.IsOk())
			tree->SelectItem(firstLeaf);
	}
}

// Id -1 = technical declared-value entry; other Ids are Primitives' Ids.
constexpr int kDeclaredId = -1;

} // namespace

bool ibShowPredefinedSelector(const ibProtocolNode& request, ibProtocolNode& response)
{
	wxWindow* const parent = ibFrontendMainFrame::GetFrame();
	const ibProtocolNode types = request.FindChild(ibProtocol::ibRequest::ibPredefinedSelector::Types);
	const ibProtocolNode primitives = request.FindChild(ibProtocol::ibRequest::ibPredefinedSelector::Primitives);
	if (!types.IsNode() || types.Children().empty())
		return false;

	// FIRST QUESTION — primitives plus one technical entry for the declared tree (desktop ShowSelectType).
	bool pickDeclared = true;
	if (primitives.IsNode() && !primitives.Children().empty()) {
		wxArrayString captions;
		std::vector<int> ids;
		std::vector<wxString> typeIds;
		for (const ibProtocolNode& item : primitives.Children()) {
			captions.Add(item.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Caption));
			ids.push_back(static_cast<int>(item.GetInt(ibProtocol::ibRequest::ibPredefinedSelector::Id)));
			typeIds.push_back(item.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Type));
		}
		captions.Add(_("Declared value"));
		ids.push_back(kDeclaredId);
		typeIds.push_back(wxString());

		wxSingleChoiceDialog typeDlg(parent,
			request.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Caption),
			request.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Caption),
			captions);
		if (typeDlg.ShowModal() != wxID_OK || typeDlg.GetSelection() < 0)
			return false;
		const int sel = typeDlg.GetSelection();
		if (ids[static_cast<size_t>(sel)] != kDeclaredId) {
			response.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Type, typeIds[static_cast<size_t>(sel)]);
			return true;
		}
		pickDeclared = true;
	}

	if (!pickDeclared)
		return false;

	// SECOND QUESTION — the tree of declared values.
	wxDialog dlg(parent, wxID_ANY,
		request.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Caption).IsEmpty()
			? wxString(_("Select a predefined value"))
			: request.GetString(ibProtocol::ibRequest::ibPredefinedSelector::Caption),
		wxDefaultPosition, parent != nullptr ? wxDLG_UNIT(parent, wxSize(170, 130)) : wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	wxTreeCtrl* valueTree = new wxTreeCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_HIDE_ROOT);
	FillTree(valueTree, types);
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
		? dynamic_cast<ibDeclaredItem*>(valueTree->GetItemData(atValue)) : nullptr;
	if (chosen == nullptr)
		return false;

	response.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::MetaId, static_cast<long long>(chosen->m_metaId));
	response.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Guid, chosen->m_guid);
	response.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Name, chosen->m_name);
	response.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Written,
		chosen->m_typeTitle + wxT(".") + chosen->m_name);
	return true;
}
