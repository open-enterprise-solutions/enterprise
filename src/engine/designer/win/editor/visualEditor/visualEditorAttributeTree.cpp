////////////////////////////////////////////////////////////////////////////
//	Description : visual editor — the form source-attribute tree
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "frontend/visualView/ctrl/formAttribute.h"                    // ibFormAttributeValue (+ clipboard statics)
#include "frontend/visualView/ctrl/form.h"                              // ibValueForm attribute API
#include "frontend/mainFrame/objinspect/objinspect.h"                  // objectInspector
#include "backend/metaCollection/attribute/metaAttributeObject.h"      // GetIconGroup (default attribute icon)

#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/menu.h>

typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTree     ibAttributeTree;
typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTreeItemData ibAttributeTreeItemData;

enum {
	ID_ATTR_ADD = wxID_HIGHEST + 2100,
	ID_ATTR_EDIT,
	ID_ATTR_REMOVE,
	ID_ATTR_SETMAIN,
	ID_ATTR_PROPERTIES,
	ID_ATTR_COPY,
	ID_ATTR_PASTE,
};

wxBEGIN_EVENT_TABLE(ibAttributeTree, wxPanel)
	EVT_TREE_SEL_CHANGED(wxID_ANY, ibAttributeTree::OnSelChanged)
	EVT_TREE_ITEM_ACTIVATED(wxID_ANY, ibAttributeTree::OnActivated)
	EVT_CONTEXT_MENU(ibAttributeTree::OnContextMenu)
	EVT_TREE_END_LABEL_EDIT(wxID_ANY, ibAttributeTree::OnEndLabelEdit)
	EVT_MENU(ID_ATTR_ADD, ibAttributeTree::OnAddAttribute)
	EVT_MENU(ID_ATTR_EDIT, ibAttributeTree::OnEditAttribute)
	EVT_MENU(ID_ATTR_REMOVE, ibAttributeTree::OnRemoveAttribute)
	EVT_MENU(ID_ATTR_SETMAIN, ibAttributeTree::OnSetMainAttribute)
	EVT_MENU(ID_ATTR_PROPERTIES, ibAttributeTree::OnPropertiesAttribute)
	EVT_MENU(ID_ATTR_COPY, ibAttributeTree::OnCopyAttribute)
	EVT_MENU(ID_ATTR_PASTE, ibAttributeTree::OnPasteAttribute)
wxEND_EVENT_TABLE()

ibAttributeTree::ibVisualEditorAttributeTree(ibVisualEditor* owner, wxWindow* parent, int id)
	: wxPanel(parent, id), m_formHandler(owner)
{
	// No toolbar — every action lives on the context menu. wxTR_EDIT_LABELS enables the inline
	// rename used by "Edit".
	m_tcAttributes = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT | wxTR_EDIT_LABELS);

	// Every entry carries the default meta-attribute icon (index 0).
	wxImageList* images = new wxImageList(16, 16);
	images->Add(ibValueMetaObjectAttribute::GetIconGroup());
	m_tcAttributes->AssignImageList(images);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tcAttributes, 1, wxEXPAND);
	SetSizer(sizer);
}

void ibAttributeTree::RebuildTree()
{
	if (m_tcAttributes == nullptr)
		return;

	m_tcAttributes->DeleteAllItems();
	const wxTreeItemId root = m_tcAttributes->AddRoot(wxT("Attributes"));

	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	for (unsigned int idx = 0; idx < form->GetAttributeCount(); idx++) {
		ibFormAttributeValue* entry = form->GetAttribute(idx);
		const wxTreeItemId id = m_tcAttributes->AppendItem(
			root, entry->GetAttributeName(), 0, 0,
			new ibAttributeTreeItemData(entry->GetAttribute()));
		if (entry->IsMainAttribute())
			m_tcAttributes->SetItemBold(id);
	}
}

ibValueFormAttribute* ibAttributeTree::GetAttributeFromItem(const wxTreeItemId& item) const
{
	if (item.IsOk()) {
		if (wxTreeItemData* data = m_tcAttributes->GetItemData(item))
			return ((ibAttributeTreeItemData*)data)->GetAttribute();
	}
	return nullptr;
}

void ibAttributeTree::OnSelChanged(wxTreeEvent& event)
{
	if (ibValueFormAttribute* attr = GetAttributeFromItem(event.GetItem())) {
		// Ensure the inspector is up — SelectObject is a no-op while hidden;
		// force a rebuild so the attribute's properties are shown.
		if (!objectInspector->IsShownInspector())
			objectInspector->ShowInspector();
		objectInspector->SelectObject(attr, true);
	}
}

void ibAttributeTree::OnContextMenu(wxContextMenuEvent& event)
{
	// Fires on an item OR empty space — so Add / Paste stay reachable even with no attributes
	// and no selection (the old item-only menu showed nothing on an empty tree).
	const wxPoint pos = event.GetPosition();
	if (pos != wxDefaultPosition) {
		int flags = 0;
		const wxTreeItemId hit = m_tcAttributes->HitTest(m_tcAttributes->ScreenToClient(pos), flags);
		if (hit.IsOk())
			m_tcAttributes->SelectItem(hit);
	}
	const bool hasItem = m_tcAttributes->GetSelection().IsOk();

	wxMenu menu;
	auto appendItem = [&menu](int menuId, const wxString& label, const wxArtID& art, bool enabled) {
		wxMenuItem* item = new wxMenuItem(&menu, menuId, label);
		item->SetBitmap(wxArtProvider::GetBitmap(art, wxART_MENU));
		menu.Append(item);
		item->Enable(enabled);
	};

	appendItem(ID_ATTR_ADD, _("Add"), wxART_NEW, true);              // always available
	appendItem(ID_ATTR_EDIT, _("Edit"), wxART_EDIT, hasItem);
	appendItem(ID_ATTR_REMOVE, _("Delete"), wxART_DELETE, hasItem);
	menu.AppendSeparator();
	appendItem(ID_ATTR_COPY, _("Copy"), wxART_COPY, hasItem);
	appendItem(ID_ATTR_PASTE, _("Paste"), wxART_PASTE, true);        // always available
	menu.AppendSeparator();
	appendItem(ID_ATTR_SETMAIN, _("Set as main"), wxART_TICK_MARK, hasItem);
	appendItem(ID_ATTR_PROPERTIES, _("Properties"), wxART_LIST_VIEW, hasItem);

	PopupMenu(&menu);
}

void ibAttributeTree::OnActivated(wxTreeEvent& event)
{
	// Double-click / Enter on an item → activate its properties in the inspector, even when it is
	// already the selected item (OnSelChanged would not re-fire). "Activate the property."
	if (ibValueFormAttribute* attr = GetAttributeFromItem(event.GetItem())) {
		if (!objectInspector->IsShownInspector())
			objectInspector->ShowInspector();
		objectInspector->SelectObject(attr, true);
	}
}

void ibAttributeTree::OnAddAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	// A new attribute is never empty: a UNIQUE name and a default Type (String). The user
	// changes the Type via the inspector afterwards.
	ibFormAttributeValue* entry = form->AddAttribute(
		form->MakeUniqueAttributeName(wxT("Attribute")),
		ibValue::GetIDByVT(ibValueTypes::TYPE_STRING), ibValue());

	m_formHandler->Modify(true);
	RebuildTree();
	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	objectInspector->SelectObject(entry->GetAttribute(), true);
}

void ibAttributeTree::OnEditAttribute(wxCommandEvent& WXUNUSED(event))
{
	// "Edit" = rename inline; OnEndLabelEdit validates (non-empty + unique) and applies.
	const wxTreeItemId sel = m_tcAttributes->GetSelection();
	if (sel.IsOk())
		m_tcAttributes->EditLabel(sel);
}

void ibAttributeTree::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled())
		return;

	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibValueFormAttribute* attr = GetAttributeFromItem(event.GetItem());
	if (form == nullptr || attr == nullptr) { event.Veto(); return; }

	ibFormAttributeValue* entry = form->FindAttributeById(attr->GetAttributeId());
	// Reject an empty or duplicate name — RenameAttribute re-binds the module variable on success.
	if (entry == nullptr || !form->RenameAttribute(entry, event.GetLabel())) {
		event.Veto();
		return;
	}
	m_formHandler->Modify(true);
	objectInspector->SelectObject(attr, true);
}

void ibAttributeTree::OnRemoveAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibValueFormAttribute* attr = GetAttributeFromItem(m_tcAttributes->GetSelection());
	if (form == nullptr || attr == nullptr)
		return;

	form->DeleteAttribute(attr->GetAttributeName());
	m_formHandler->Modify(true);
	RebuildTree();
	m_formHandler->RefreshEditor();   // controls bound to the deleted attribute now render broken/empty
}

void ibAttributeTree::OnSetMainAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibValueFormAttribute* attr = GetAttributeFromItem(m_tcAttributes->GetSelection());
	if (form == nullptr || attr == nullptr)
		return;

	if (ibFormAttributeValue* entry = form->FindAttributeById(attr->GetAttributeId()))
		form->SetMainAttribute(entry);   // sole-main invariant + source flows to it
	m_formHandler->Modify(true);
	RebuildTree();
	m_formHandler->RefreshEditor();   // source moved to the new main → controls re-render accordingly
}

void ibAttributeTree::OnPropertiesAttribute(wxCommandEvent& WXUNUSED(event))
{
	if (ibValueFormAttribute* attr = GetAttributeFromItem(m_tcAttributes->GetSelection())) {
		if (!objectInspector->IsShownInspector())
			objectInspector->ShowInspector();
		objectInspector->SelectObject(attr, true);
	}
}

void ibAttributeTree::OnCopyAttribute(wxCommandEvent& WXUNUSED(event))
{
	// Clipboard copy lives on the attribute wrapper (serialization-based, own format id).
	if (ibValueFormAttribute* attr = GetAttributeFromItem(m_tcAttributes->GetSelection()))
		ibFormAttributeValue::CopyToClipboard(attr);
}

void ibAttributeTree::OnPasteAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	if (ibFormAttributeValue* entry = ibFormAttributeValue::PasteFromClipboard(form)) {
		m_formHandler->Modify(true);
		RebuildTree();
		if (!objectInspector->IsShownInspector())
			objectInspector->ShowInspector();
		objectInspector->SelectObject(entry->GetAttribute(), true);
	}
}
