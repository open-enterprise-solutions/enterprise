////////////////////////////////////////////////////////////////////////////
//	Description : the designer's choice of a DECLARED value — see selectPredefined.h
////////////////////////////////////////////////////////////////////////////

#include "selectPredefined.h"

#include <wx/treectrl.h>
#include <wx/dialog.h>
#include <wx/sizer.h>

#include <map>
#include <vector>

#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"          // a reference built on a declared guid
#include "backend/metaCollection/partial/declaredPresentation.h"         // `CatalogRef.Goods` — written once
#include "backend/system/value/composition/valueComposerField.h"         // the technical type the references are chosen by
#include "frontend/visualView/ctrl/frame.h"                              // ibControlFrame::SetControlValue
#include "frontend/visualView/ctrl/typeControl.h"                        // ShowSelectType — the type list already in the product
// The window's own parts — nothing outside this file has business with a tree node or a branch.
namespace {

// The predefined value lives INSIDE the hierarchy metaobject — named once here rather than spelled
// out at every use.
using ibPredefinedValue = ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedValueObject;

// THE ANSWER RIDES THE NODE: which type, and which value of it. A null guid IS the empty reference
// of that type, so both answers are the same node kind and the window has one road out.
struct ibDesignerRefItem : public wxTreeItemData {
	ibDesignerRefItem(const ibValueMetaObjectRecordDataRef* ref, const ibGuid& guid)
		: m_ref(ref), m_guid(guid) {
	}
	const ibValueMetaObjectRecordDataRef* m_ref;
	ibGuid m_guid;
};

// WHAT ONE TYPE OFFERS: its empty reference, then everything it declares, nested as it is declared.
void ibFillOneTypeBranch(wxTreeCtrl* tree, const wxTreeItemId& root,
	const ibValueMetaObjectRecordDataRef* recordRef)
{
	if (recordRef == nullptr)
		return;

	// ⭐ EVERY REFERENCE TYPE HAS THIS ONE, not only the kinds that declare predefined values: a
	// DOCUMENT declares none and its empty reference is still a legitimate thing to say (Max,
	// 2026-08-28: "documents open too — a document just has only the reference").
	tree->AppendItem(root, wxT("EmptyRef"), -1, -1, new ibDesignerRefItem(recordRef, wxNullGuid));

	// ⭐ AN ENUMERATION IS A REFERENCE LIKE ANY OTHER — "all references — enumerations, documents,
	// catalogs — go through this form" (Max, 2026-08-28). Its members are DECLARED, which is exactly
	// the shape this window is for: no data is needed to name them.
	if (const auto* enumeration = dynamic_cast<const ibValueMetaObjectRecordDataEnumRef*>(recordRef)) {
		for (const ibValueMetaObjectEnum* member : enumeration->GetEnumObjectArray())
			if (member != nullptr)
				tree->AppendItem(root, member->GetName(), -1, -1,
					new ibDesignerRefItem(recordRef, member->GetGuid()));
	}

	if (const auto* hierarchy =
			dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(recordRef)) {

		// ⭐ FOLDERS NEST, AND SO DO THEIR CHILDREN — a predefined item names its parent and that
		// parent may itself sit under another folder ("there can be folders, and under a folder
		// items of a subordinate folder — all of it selectable"). The array is FLAT and says nothing
		// about who comes first, so placement is by repeated passes rather than by order.
		std::map<const ibPredefinedValue*, wxTreeItemId> placed;
		std::vector<const ibPredefinedValue*> pending;
		for (const auto& item : hierarchy->GetPredefinedValueArray())
			if (item)
				pending.push_back(item.get());

		for (bool moved = true; moved && !pending.empty(); ) {
			moved = false;
			std::vector<const ibPredefinedValue*> rest;
			for (const ibPredefinedValue* item : pending) {
				const ibPredefinedValue* under = item->GetPredefinedParent().get();
				wxTreeItemId at = root;
				if (under != nullptr) {
					const auto found = placed.find(under);
					if (found == placed.end()) {   // its folder is not placed yet — next pass
						rest.push_back(item);
						continue;
					}
					at = found->second;
				}
				placed[item] = tree->AppendItem(at, item->GetPredefinedName(), -1, -1,
					new ibDesignerRefItem(recordRef, item->GetPredefinedGuid()));
				moved = true;
			}
			pending.swap(rest);
		}

		// A parent that is not itself predefined leaves its children homeless — they still belong to
		// the type, so they stand at the top rather than disappearing.
		for (const ibPredefinedValue* item : pending)
			tree->AppendItem(root, item->GetPredefinedName(), -1, -1,
				new ibDesignerRefItem(recordRef, item->GetPredefinedGuid()));
	}
}

// EVERY ADMITTED REFERENCE TYPE IS A BRANCH — the composite case is not a second window but one more
// branch, which is what makes "choose a declared value" a single question however many types the
// parameter admits.
void ibFillDesignerRefTree(wxTreeCtrl* tree,
	const std::vector<const ibValueMetaObjectRecordDataRef*>& types)
{
	tree->DeleteAllItems();
	const wxTreeItemId hidden = tree->AddRoot(wxT("*"));   // hidden — the branches are the roots seen

	for (const ibValueMetaObjectRecordDataRef* recordRef : types)
		ibFillOneTypeBranch(tree, tree->AppendItem(hidden, ibDeclaredTypeName(recordRef)), recordRef);

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
}   // namespace

// ⭐⭐ THE DESIGNER'S OWN ROAD TO A REFERENCE VALUE — one window, no data behind it. The whole of it
// is described in the header; what follows is how the two questions are asked.
bool ibShowPredefinedSelector(ibControlFrame* ownerValue,
	const ibTypeDescription& declared, const ibMetaData* metaData, wxWindow* parent)
{
	if (ownerValue == nullptr || metaData == nullptr)
		return false;

	// ⚠⚠ NOTHING ABOUT `parent` SURVIVES THE MODAL BELOW — the rule already written at the top of
	// ChooseValue, and this window walked straight into it. The type list runs an event loop of its
	// own; while it is up the grid finishes editing the cell and DESTROYS the editor control, which
	// is the window handed in here. Measuring dialog units against it afterwards read freed memory
	// and took the designer down inside wxGetTopLevelParent (dump, 2026-08-28).
	//
	// So both things taken from it are taken NOW: the top-level frame, which outlives every modal,
	// and the size in pixels.
	wxWindow* const owner = parent != nullptr ? wxGetTopLevelParent(parent) : nullptr;
	const wxSize dialogSize = parent != nullptr
		? wxDLG_UNIT(parent, wxSize(170, 130)) : wxDefaultSize;

	// ⭐⭐ THE REFERENCES COLLAPSE INTO ONE LINE. What the type list offers is the declaration's own
	// PRIMITIVES — a date is typed in, and that road is unchanged — plus a single technical entry
	// standing for every reference it admits (`CompositionPredefinedValue`, see valueComposerField.h:
	// "such a technical type is introduced precisely so that all those references can be selected by
	// it" — Max, 2026-08-28). Asked through the window the product already has, not a list of ours.
	std::vector<const ibValueMetaObjectRecordDataRef*> types;
	ibTypeDescription offered;

	for (const ibClassID& clsid : declared.GetClsidList()) {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(clsid);
		const auto* recordRef = (so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference)
			? dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject()) : nullptr;
		if (recordRef != nullptr)
			types.push_back(recordRef);
		else
			offered.AppendMetaType(clsid);
	}

	if (types.empty())
		return false;   // nothing referenceable is declared — the ordinary roads answer alone

	offered.AppendMetaType(g_compositionPredefinedCLSID);

	const ibClassID clsid = ibTypeControlFactory::ShowSelectType(metaData, offered);
	if (clsid == 0)
		return false;   // the list was closed

	// ⚠ A PRIMITIVE ENDS THE CONVERSATION HERE — the type is settled and the cell now holds an empty
	// value of it, which is typed into or opened again for the calendar behind a date. The choice is
	// NOT carried on in the same call: the type list was a modal, and a modal ends the life of the
	// editor this popup would hang on — the rule stated at the top of ChooseValue, not a second one.
	if (clsid != g_compositionPredefinedCLSID) {
		if (!metaData->IsRegisterCtor(clsid))
			return false;
		ownerValue->SetControlValue(metaData->CreateObject(clsid));
		return true;
	}

	// …AND THE TECHNICAL ENTRY ASKS THE SECOND QUESTION: which of the values the configuration
	// declares this is — an empty reference, a predefined element, an enumeration member — with every
	// admitted reference type standing as its own branch.
	wxDialog dlg(owner, wxID_ANY, _("Select a predefined value"), wxDefaultPosition,
		dialogSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	wxTreeCtrl* valueTree = new wxTreeCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_HIDE_ROOT);
	ibFillDesignerRefTree(valueTree, types);
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

	// ⭐⭐ WHAT IS WRITTEN IS THE DECLARATION, NOT A LIVE REFERENCE. A reference object is runtime — a
	// session, a register, a row to read — and this value is going into a DESCRIPTION that is saved
	// with the configuration and read back while the next load is still building the tree. So the
	// choice is stored as what it is: this metaobject, this declared value, and the name it reads by.
	// The runtime reference is made from it at execution, by the running composer
	// (ibMaterializeCompositionValue).
	const wxString written = ibDeclaredTypeName(chosen->m_ref) + wxT(".") +
		(atValue.IsOk() ? valueTree->GetItemText(atValue) : wxString());
	ownerValue->SetControlValue(ibValue(new ibValueCompositionPredefined(
		chosen->m_ref->GetMetaID(), chosen->m_guid, written)));
	return true;
}
