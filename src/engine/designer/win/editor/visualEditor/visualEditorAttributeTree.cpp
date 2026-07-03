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
	ID_ATTR_CUT,
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
	EVT_MENU(ID_ATTR_CUT, ibAttributeTree::OnCutAttribute)
	EVT_MENU(ID_ATTR_PASTE, ibAttributeTree::OnPasteAttribute)
wxEND_EVENT_TABLE()

ibAttributeTree::ibVisualEditorAttributeTree(ibVisualEditor* owner, wxWindow* parent, int id)
	: wxPanel(parent, id), m_formHandler(owner)
{
	// No toolbar — every action lives on the context menu. wxTR_EDIT_LABELS enables the inline
	// rename used by "Edit".
	m_tcAttributes = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT | wxTR_EDIT_LABELS);
	m_tcAttributes->SetDoubleBuffered(true);   // no flicker on RebuildTree (add / set-main), like the object tree

	// Every entry carries the default meta-attribute icon (index 0).
	wxImageList* images = new wxImageList(16, 16);
	images->Add(ibValueMetaObjectAttribute::GetIconGroup());
	m_tcAttributes->AssignImageList(images);

	// Single click always (re)selects — even the already-selected item — so ONE click surfaces the
	// attribute in the inspector (OnSelChanged fires only on CHANGE). Same as the object tree, which
	// routes its click-reselect through SelectItemData.
	m_tcAttributes->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
		const wxTreeItemId id = m_tcAttributes->HitTest(event.GetPosition());
		if (id.IsOk() && id == m_tcAttributes->GetSelection())
			SelectInInspector(GetEntryFromItem(id));   // re-surface the already-selected attribute
		event.Skip();
	});

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tcAttributes, 1, wxEXPAND);
	SetSizer(sizer);
}

void ibAttributeTree::RebuildTree()
{
	if (m_tcAttributes == nullptr)
		return;

	// Remember the selected attribute BY IDENTITY (the holder pointer). The form owns the holders in
	// m_attributes; RebuildTree recreates only the tree items, so a survivor's pointer is stable —
	// the same key the object tree uses (m_listItem keyed by ibValueFrame*). A just-removed entry is
	// held alive by the undo command, so the compare below is safe and simply matches nothing.
	ibFormAttributeValue* keep = GetEntryFromItem(m_tcAttributes->GetSelection());

	m_rebuilding = true;        // suppress OnSelChanged while items churn (stale/freed holders)
	m_tcAttributes->Freeze();   // no flicker while the tree is torn down and re-appended
	m_tcAttributes->DeleteAllItems();
	const wxTreeItemId root = m_tcAttributes->AddRoot(wxT("Attributes"));

	if (ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr) {
		for (unsigned int idx = 0; idx < form->GetAttributeCount(); idx++) {
			ibFormAttributeValue* entry = form->GetAttribute(idx);
			const wxTreeItemId id = m_tcAttributes->AppendItem(
				root, entry->GetAttributeName(), 0, 0,
				new ibAttributeTreeItemData(entry));   // carry the holder (facade) — the selectable
			if (entry->IsMainAttribute())
				m_tcAttributes->SetItemBold(id);
		}
	}
	m_tcAttributes->Thaw();
	m_rebuilding = false;

	SelectEntry(keep);   // restore by identity — now fires OnSelChanged with a LIVE holder
}

ibFormAttributeValue* ibAttributeTree::GetEntryFromItem(const wxTreeItemId& item) const
{
	if (item.IsOk())
		if (wxTreeItemData* data = m_tcAttributes->GetItemData(item))
			return ((ibAttributeTreeItemData*)data)->GetEntry();
	return nullptr;
}

void ibAttributeTree::SelectEntry(ibFormAttributeValue* entry)
{
	if (entry == nullptr || m_tcAttributes == nullptr)
		return;
	const wxTreeItemId root = m_tcAttributes->GetRootItem();
	if (!root.IsOk())
		return;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId id = m_tcAttributes->GetFirstChild(root, cookie); id.IsOk();
		id = m_tcAttributes->GetNextChild(root, cookie)) {
		if (GetEntryFromItem(id) == entry) {   // match BY IDENTITY, like m_listItem.find(obj)
			m_tcAttributes->SelectItem(id);    // fires OnSelChanged → inspector shows it
			m_tcAttributes->EnsureVisible(id);
			return;
		}
	}
}

void ibAttributeTree::SelectInInspector(ibFormAttributeValue* entry)
{
	// The single decode-and-select point. Select the HOLDER (facade): it surfaces the attribute's +
	// the generated value's properties and intercepts their changes. SelectObject is a no-op while
	// the inspector is hidden, so raise it first.
	if (entry == nullptr)
		return;
	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	objectInspector->SelectObject(entry, true);
}

void ibAttributeTree::OnSelChanged(wxTreeEvent& event)
{
	if (m_rebuilding)
		return;   // mid-rebuild: the item's holder may be stale/freed — don't touch the inspector
	SelectInInspector(GetEntryFromItem(event.GetItem()));
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
	ibFormAttributeValue* selEntry = GetEntryFromItem(m_tcAttributes->GetSelection());
	const bool isMain = selEntry != nullptr && selEntry->IsMainAttribute();

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
	appendItem(ID_ATTR_CUT, _("Cut"), wxART_CUT, hasItem);
	appendItem(ID_ATTR_COPY, _("Copy"), wxART_COPY, hasItem);
	appendItem(ID_ATTR_PASTE, _("Paste"), wxART_PASTE, true);        // always available
	menu.AppendSeparator();
	appendItem(ID_ATTR_SETMAIN, isMain ? _("Unset main") : _("Set as main"), wxART_TICK_MARK, hasItem);
	appendItem(ID_ATTR_PROPERTIES, _("Properties"), wxART_LIST_VIEW, hasItem);

	PopupMenu(&menu);
}

void ibAttributeTree::OnActivated(wxTreeEvent& event)
{
	// Double-click / Enter on an item → activate its properties in the inspector, even when it is
	// already the selected item (OnSelChanged would not re-fire). "Activate the property."
	SelectInInspector(GetEntryFromItem(event.GetItem()));
}

void ibAttributeTree::OnAddAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	// A new attribute is never empty: a UNIQUE name and a default Type (String) — the user changes
	// the Type via the inspector afterwards. Build it UNOWNED and run it through the command
	// processor so the add is UNDOABLE; the command attaches it and refreshes the editor (this tree
	// re-reads the form's attribute set). Then land the current row on the new entry BY IDENTITY —
	// the holder is ref-counted, so the raw pointer survives being handed to the command.
	ibValuePtr<ibFormAttributeValue> holder = form->MakeAttribute(form->MakeUniqueAttributeName());
	ibFormAttributeValue* added = holder;
	m_formHandler->InsertAttribute(holder);   // attach + RefreshEditor → RebuildTree
	SelectEntry(added);                        // select the new attribute (drives OnSelChanged → inspector)
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
	ibFormAttributeValue* entry = GetEntryFromItem(event.GetItem());
	if (form == nullptr || entry == nullptr) { event.Veto(); return; }

	// Reject an empty or duplicate name — RenameAttribute re-binds the module variable on success.
	if (!form->RenameAttribute(entry, event.GetLabel())) {
		event.Veto();
		return;
	}
	m_formHandler->Modify(true);
	m_formHandler->NotifyEditorSaved();

	SelectInInspector(entry);
}

void ibAttributeTree::OnRemoveAttribute(wxCommandEvent& WXUNUSED(event))
{
	// Through the command processor (undoable). The command detaches the entry from the form,
	// marks the editor modified and refreshes it — this tree (and controls bound to the deleted
	// attribute, which now render broken/empty) re-read along with the rest.
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		m_formHandler->RemoveAttribute(entry);
}

void ibAttributeTree::OnSetMainAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection());
	if (form == nullptr || entry == nullptr)
		return;

	// Toggle: clicking on the CURRENT main clears it (no main → its controls get their own command
	// bar back); otherwise make it main. SetMainAttribute keeps the sole-main invariant.
	form->SetMainAttribute(entry->IsMainAttribute() ? nullptr : entry);
	m_formHandler->Modify(true);
	m_formHandler->NotifyEditorSaved();
	RebuildTree();
	m_formHandler->RefreshEditor();   // source moved to the new main → controls re-render accordingly
}

void ibAttributeTree::OnPropertiesAttribute(wxCommandEvent& WXUNUSED(event))
{
	SelectInInspector(GetEntryFromItem(m_tcAttributes->GetSelection()));
}

void ibAttributeTree::OnCopyAttribute(wxCommandEvent& WXUNUSED(event))
{
	// Clipboard copy lives on the holder (serialization-based, own format id).
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		ibFormAttributeValue::CopyToClipboard(entry);
}

void ibAttributeTree::OnCutAttribute(wxCommandEvent& WXUNUSED(event))
{
	// Cut = copy to the clipboard, then remove through the command processor (the removal is undoable).
	ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection());
	if (entry == nullptr)
		return;
	ibFormAttributeValue::CopyToClipboard(entry);
	m_formHandler->RemoveAttribute(entry);
}

void ibAttributeTree::OnPasteAttribute(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	if (ibFormAttributeValue* entry = ibFormAttributeValue::PasteFromClipboard(form)) {
		m_formHandler->Modify(true);
		RebuildTree();
		SelectEntry(entry);   // land on the pasted attribute BY IDENTITY (drives OnSelChanged → inspector)
	}
}
