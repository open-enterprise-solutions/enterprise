////////////////////////////////////////////////////////////////////////////
//	Description : visual editor — the command navigator tree (the command door)
////////////////////////////////////////////////////////////////////////////

// The parallel of the attribute/source tree, one door over — its OWN file, like the attribute tree has its
// own. It GATHERS the commands available to this form and is the surface a designer DRAGS from: read-only
// (commands are created / edited in the metadata tree), it only lists + drags. A drag serializes the
// projection (kind + id) through the engine's memory writer and emits "oes_command_drag", which the shared
// drop target decodes and binds to the nearest command bar under the drop point.

#include "visualEditor.h"
#include "designerTreeCtrl.h"                                          // focus-safe DeleteAllItems (our wxTreeCtrl projection)
#include "visualEditorDragItem.h"                                        // ibCommandDragItem — serialize + start the drag (one kind)
#include "frontend/visualView/ctrl/form.h"                              // ibValueForm — GetValueForm
#include "frontend/visualView/layers/commandBar.h"                      // GatherFormCommands / ibCommandSourceEntry — the shared 4-source gather
#include "backend/metaCollection/attribute/metaAttributeObject.h"       // GetIconGroup (the tree's default icon)
#include "backend/metaCollection/genericData.h"                         // ibValueMetaObjectGenericData — the form's own object (add-command parent)
#include "backend/metaCollection/metaObject.h"                          // g_metaCommandCLSID — the command metaobject class
#include "backend/metaCollection/metaCommandObject.h"                   // ibValueMetaObjectCommand — typed find (a command vs a standard action)
#include "frontend/visualView/ctrl/formCommand.h"                       // ibFormCommandValue — form-local command (property object)
#include "backend/metaData.h"                                            // ibMetaData — CreateMetaObject / find (the editor's own config tree)
#include "frontend/docView/docView.h"                                   // ibMetaDocument — the editor's document config (create in the edited tree, not the global)
#include "backend/backend_core.h"                                        // oes_clipboard_metadata — the metaobject clipboard format
#include "backend/fileSystem/fs.h"                                       // ibWriterMemory / ibReaderMemory — serialize (drag payload + copy/paste)

#include <map>            // std::map — pre-create the fixed command sections (shown even empty)
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/menu.h>      // wxMenu — the "add command" context menu
#include <wx/artprov.h>   // wxArtProvider — menu-item icons (SAME set the attribute tree uses)
#include <wx/accel.h>     // wxAcceleratorTable — Ctrl+C / X / V / Delete on the tree
#include <wx/clipbrd.h>   // wxTheClipboard — copy / paste a command metaobject (SAME blob the metadata tree uses)
#include <wx/dnd.h>       // wxDropSource / wxCustomDataObject / wxDataFormat — drag a command onto the form

enum { ID_COMMANDTREE_ADD_OBJECT = wxID_HIGHEST + 4210, ID_COMMANDTREE_ADD_GENERAL, ID_COMMANDTREE_COPY, ID_COMMANDTREE_CUT, ID_COMMANDTREE_PASTE, ID_COMMANDTREE_DELETE, ID_COMMANDTREE_PROPERTIES };

typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTreeItemData ibAttributeTreeItemData;
typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorCommandTree           ibCommandTree;

wxBEGIN_EVENT_TABLE(ibCommandTree, wxPanel)
	EVT_TREE_BEGIN_DRAG(wxID_ANY, ibCommandTree::OnBeginDrag)
wxEND_EVENT_TABLE()

ibCommandTree::ibVisualEditorCommandTree(ibVisualEditor* owner, wxWindow* parent, int id)
	: wxPanel(parent, id), m_formHandler(owner)
{
	m_tcCommands = new ibDesignerTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT);
	m_tcCommands->SetDoubleBuffered(true);   // no flicker on RebuildTree
	// Right-click anywhere in the tree -> add a command without leaving the form (bound on the tree, not the panel,
	// so an empty-space click still fires).
	m_tcCommands->Bind(wxEVT_CONTEXT_MENU, &ibVisualEditorCommandTree::OnContextMenu, this);
	// Click a form command -> show it in the inspector (a form command IS a property object, like an attribute).
	m_tcCommands->Bind(wxEVT_TREE_SEL_CHANGED, &ibVisualEditorCommandTree::OnSelChanged, this);
	// Single click always (re)surfaces the command — even the ALREADY-selected item (SEL_CHANGED fires only on
	// CHANGE, so a click on the highlighted row would otherwise do nothing). Mirrors the attribute tree.
	m_tcCommands->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
		const wxTreeItemId id = m_tcCommands->HitTest(event.GetPosition());
		if (id.IsOk() && id == m_tcCommands->GetSelection())
			RevealSelectedCommand();
		event.Skip();
	});
	// Double-click / Enter activates the command's properties in the inspector — the SAME "activate the property"
	// idiom the attribute tree binds (OnActivated), so a manually-added form command reaches the inspector on
	// activation, not only on a plain select.
	m_tcCommands->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { RevealSelectedCommand(); });
	// ACTIVATING the tree (it gains focus) reveals its SELECTED command too — not only a click. So switching to the
	// tree re-surfaces the current form command in the inspector (a non-form node no-ops inside RevealSelectedCommand).
	m_tcCommands->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { event.Skip(); RevealSelectedCommand(); });

	// Ctrl+C / Ctrl+X / Ctrl+V / Delete — an accelerator table ON THE TREE, mirroring the attribute tree (the
	// editor panel maps Ctrl+C to control-copy, so the table must sit closer in the focus chain to route the keys
	// here). Each fires the SAME id as its menu item; the Binds below dispatch both the accelerator and the menu.
	wxAcceleratorEntry accelEntries[4];
	accelEntries[0].Set(wxACCEL_CTRL,   (int)'C',   ID_COMMANDTREE_COPY);
	accelEntries[1].Set(wxACCEL_CTRL,   (int)'X',   ID_COMMANDTREE_CUT);
	accelEntries[2].Set(wxACCEL_CTRL,   (int)'V',   ID_COMMANDTREE_PASTE);
	accelEntries[3].Set(wxACCEL_NORMAL, WXK_DELETE, ID_COMMANDTREE_DELETE);
	m_tcCommands->SetAcceleratorTable(wxAcceleratorTable(4, accelEntries));

	// ONE dispatch for menu items AND accelerators — a form-command action gates on SelectedFormCommand inside,
	// so an accelerator on a non-form node is a safe no-op.
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddCommand(false); }, ID_COMMANDTREE_ADD_OBJECT);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopyCommand();      }, ID_COMMANDTREE_COPY);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { CutCommand();       }, ID_COMMANDTREE_CUT);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { PasteCommand();     }, ID_COMMANDTREE_PASTE);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { DeleteCommand();    }, ID_COMMANDTREE_DELETE);
	// "Properties" — make the selected command the current element, then route through the editor's ONE mechanism
	// (force-open the inspector on it), the SAME as the attribute tree's OnProperties.
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		RevealSelectedCommand();                       // record the selected form command as the current element
		if (m_formHandler != nullptr)
			m_formHandler->ShowCurrentElement();       // force-open the inspector on it
	}, ID_COMMANDTREE_PROPERTIES);

	wxImageList* images = new wxImageList(16, 16);
	images->Add(ibValueMetaObjectAttribute::GetIconGroup());   // one default icon shared by every command node
	m_tcCommands->AssignImageList(images);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tcCommands, 1, wxEXPAND);
	SetSizer(sizer);
}

void ibCommandTree::RebuildTree()
{
	if (m_tcCommands == nullptr)
		return;

	// Remember the selected node's command DESC — ANY node (a form command, a standard action, a global command, a
	// section item), not only a form command — so the current row SURVIVES a rebuild the way a control does, instead
	// of dropping when the selection is not a form command. The guard swallows OnSelChanged while items churn. The
	// node is re-captured DURING the append loop (below) and re-selected after — identity restore, exactly the way
	// the object tree remembers its row, no separate walk.
	ibCommandDescription keepDesc;
	if (const wxTreeItemId sel = m_tcCommands->GetSelection(); sel.IsOk())
		if (const ibAttributeTreeItemData* data = (const ibAttributeTreeItemData*)m_tcCommands->GetItemData(sel))
			keepDesc = data->GetCommandDesc();
	m_rebuilding = true;
	m_tcCommands->Freeze();
	// BLOCK the tree's events for the churn: DeleteAllItems / SelectItem otherwise fire native selection + focus
	// events, and the app's OnSetFocus follows that focus and switches the active MDI tab — a background rebuild
	// would yank the tab from the editor the user is in. Re-enabled after the row is restored below.
	m_tcCommands->SetEvtHandlerEnabled(false);
	m_tcCommands->DeleteAllItems();

	// A fresh image list per rebuild: index 0 is the group / default icon, then one slot per command's OWN
	// picture (the command determines its icon — GatherFormCommands resolved it). AssignImageList replaces the
	// ctor's single-icon list.
	wxImageList* images = new wxImageList(16, 16);
	images->Add(ibValueMetaObjectAttribute::GetIconGroup());   // 0 = group / default

	const wxTreeItemId root = m_tcCommands->AddRoot(wxT("Commands"));

	// Pre-create ALL three sections (the 1C model: form / standard / global) in order, so they show even when
	// EMPTY — a designer always sees where each kind of command lives. GatherFormCommands labels its entries with
	// exactly these strings (GetCommandSections is the ONE source), so each entry lands in its pre-made section.
	std::map<wxString, wxTreeItemId> groups;
	for (const wxString& section : GetCommandSections())
		groups[section] = m_tcCommands->AppendItem(root, section, 0, 0);
	// Remember the FORM COMMANDS group — the context menu (Add / Copy / Paste / Delete) works ONLY there; the
	// standard / global sections are read-only (drag out only), so a right-click on them shows nothing.
	const std::vector<wxString> sectionNames = GetCommandSections();
	m_formCommandsGroup = sectionNames.empty() ? wxTreeItemId() : groups[sectionNames.front()];
	std::map<wxString, wxTreeItemId> subgroups;   // "section\x1fsubgroup" -> node (Standard nests per source)

	// The node whose desc matches the pre-rebuild selection — captured in the loop as it's appended, restored after.
	wxTreeItemId keepNode;
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	for (const ibCommandSourceEntry& entry : GatherFormCommands(form)) {
		wxTreeItemId& sectionNode = groups[entry.group];
		if (!sectionNode.IsOk())   // defensive: an unexpected section -> append it at the end
			sectionNode = m_tcCommands->AppendItem(root, entry.group, 0, 0);
		wxTreeItemId parent = sectionNode;
		if (!entry.subgroup.IsEmpty()) {   // nest under a per-source SUB-group (the form / a table)
			wxTreeItemId& sub = subgroups[entry.group + wxT("\x1f") + entry.subgroup];
			if (!sub.IsOk())
				sub = m_tcCommands->AppendItem(sectionNode, entry.subgroup, 0, 0);
			parent = sub;
		}
		const int icon = entry.icon.IsOk() ? images->Add(entry.icon) : 0;   // the command's own picture, else default
		const wxTreeItemId node = m_tcCommands->AppendItem(parent, entry.label, icon, icon, new ibAttributeTreeItemData(entry.desc));
		if (keepDesc.IsOk() && entry.desc == keepDesc)   // the row that was selected before this rebuild — remember it
			keepNode = node;
	}
	m_tcCommands->AssignImageList(images);

	m_tcCommands->ExpandAll();   // sections + their sub-groups all open

	m_tcCommands->Thaw();

	// Restore the remembered row, KEEPING the guard set so SelectItem's SEL_CHANGED is swallowed — a rebuild driven
	// by an edit on another surface must not push THIS tree's selection into the inspector. Callers that DO want the
	// inspector to follow (AddCommand / PasteCommand) call SelectCommand themselves afterwards.
	if (keepNode.IsOk()) {
		m_tcCommands->SelectItem(keepNode);
		m_tcCommands->EnsureVisible(keepNode);
	}
	m_tcCommands->SetEvtHandlerEnabled(true);   // churn done, row restored — resume normal event handling
	m_rebuilding = false;
}

// Land the row on a form command BY IDENTITY — the twin of the attribute tree's SelectEntry. Form-command nodes
// live directly under the "Form commands" group (they carry no sub-group); match the one whose 1-hop desc leaf is
// the command's id. Drives OnSelChanged -> the inspector (unless m_rebuilding swallows it).
void ibCommandTree::SelectCommand(ibFormCommandValue* cmd)
{
	if (cmd == nullptr || m_tcCommands == nullptr || !m_formCommandsGroup.IsOk())
		return;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId id = m_tcCommands->GetFirstChild(m_formCommandsGroup, cookie); id.IsOk();
		id = m_tcCommands->GetNextChild(m_formCommandsGroup, cookie)) {
		const ibAttributeTreeItemData* data = (const ibAttributeTreeItemData*)m_tcCommands->GetItemData(id);
		if (data == nullptr)
			continue;
		const ibCommandDescription& desc = data->GetCommandDesc();
		if (!desc.IsHop() && desc.IsOk() && desc.GetLeaf() == cmd->GetId()) {
			m_tcCommands->SelectItem(id);      // fires OnSelChanged -> inspector (when not rebuilding)
			m_tcCommands->EnsureVisible(id);
			return;
		}
	}
}

void ibCommandTree::OnBeginDrag(wxTreeEvent& event)
{
	// A command LEAF carries a command-hop path; a GROUP node does not → not draggable.
	const ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcCommands->GetItemData(event.GetItem());
	if (data == nullptr || !data->GetCommandDesc().IsOk())
		return;

	// The COMMAND drag KIND owns its format + serialization (symmetric to the attribute tree's source kind) — the
	// command-hop path goes through the ENGINE writer, not a raw byte pack. Construction IS the drag.
	ibCommandDragItem(m_tcCommands, data->GetCommandDesc());
}

void ibCommandTree::OnContextMenu(wxContextMenuEvent& event)
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;
	// The menu (Add / Copy / Paste / Delete) works ONLY inside the FORM COMMANDS group — its header or a command
	// under it. Right-click on the standard / global sections (read-only, drag-out only) shows nothing.
	const wxPoint screenPt = event.GetPosition() != wxDefaultPosition ? event.GetPosition() : wxGetMousePosition();
	const wxTreeItemId hit = m_tcCommands->HitTest(m_tcCommands->ScreenToClient(screenPt));
	// Right-click SELECTS the item under the cursor first — otherwise the previously selected row stays highlighted
	// and TWO rows look selected. Mirrors the attribute tree's OnContextMenu. Also makes SelectedFormCommand() below
	// (menu gating) and "Properties" act on the clicked command, not a stale selection.
	if (hit.IsOk())
		m_tcCommands->SelectItem(hit);
	wxTreeItemId section = hit;
	while (section.IsOk() && m_tcCommands->GetItemParent(section) != m_tcCommands->GetRootItem())
		section = m_tcCommands->GetItemParent(section);
	if (!section.IsOk() || section != m_formCommandsGroup)
		return;

	// The SAME icon'd list the attribute tree shows — Add / Copy / Cut / Paste / Delete, each with its wxArtProvider
	// bitmap, greyed when it does not apply. Copy / Cut / Delete gate on a FORM COMMAND node (a standard action /
	// a config command isn't ours to manage); Paste greys when the command clipboard is empty. PopupMenu lets the
	// EVT_MENU Binds dispatch — the SAME handlers the accelerators fire.
	const bool hasCmd = SelectedFormCommand() != nullptr;
	wxMenu menu;
	auto appendItem = [&menu](int menuId, const wxString& label, const wxArtID& art, bool enabled) {
		wxMenuItem* item = new wxMenuItem(&menu, menuId, label);
		item->SetBitmap(wxArtProvider::GetBitmap(art, wxART_MENU));
		menu.Append(item);
		item->Enable(enabled);
	};
	appendItem(ID_COMMANDTREE_ADD_OBJECT, _("Add command"), wxART_NEW, true);   // the form always holds its own commands
	menu.AppendSeparator();
	appendItem(ID_COMMANDTREE_CUT,   _("Cut"),   wxART_CUT,    hasCmd);
	appendItem(ID_COMMANDTREE_COPY,  _("Copy"),  wxART_COPY,   hasCmd);
	appendItem(ID_COMMANDTREE_PASTE, _("Paste"), wxART_PASTE,  ibFormCommandValue::HasClipboardData());
	menu.AppendSeparator();
	appendItem(ID_COMMANDTREE_DELETE, _("Delete"), wxART_DELETE, hasCmd);
	menu.AppendSeparator();
	appendItem(ID_COMMANDTREE_PROPERTIES, _("Properties"), wxART_LIST_VIEW, hasCmd);   // open the inspector on this command
	m_tcCommands->PopupMenu(&menu);   // pop on the TREE so the menu events reach its EVT_MENU Binds (events bubble up)
}

void ibCommandTree::OnSelChanged(wxTreeEvent& WXUNUSED(event))
{
	if (m_rebuilding)
		return;   // mid-rebuild: the item may be a churned/stale node — don't touch the inspector
	RevealSelectedCommand();
}

void ibCommandTree::RevealSelectedCommand()
{
	// A form command IS a property object — show it in the inspector on click. Other nodes (standard actions /
	// global config commands / groups) have nothing to edit here, so leave the inspector as-is.
	if (ibFormCommandValue* cmd = SelectedFormCommand())
		m_formHandler->SetCurrentElement(cmd);   // the command IS a property object — the ONE selector soft-lights it
}

void ibCommandTree::AddCommand(bool /*general*/)
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;
	// A form command is a form-LOCAL event (NOT a metaobject — nothing goes into the metadata tree). Its Action
	// starts EMPTY — the handler procedure does not exist yet; the developer creates it later through the event
	// editor. The command bar / a button binds to the command BY ID (a normal 1-hop command path). It IS
	// a property object — reveal it in the inspector so Name / Caption / Action / Picture are editable right away.
	const wxString name = form->MakeUniqueFormCommandName();
	ibFormCommandValue* cmd = form->AddFormCommand(name);
	m_formHandler->Modify(true);   // mark the form dirty so the change is saved (serialized) — like an attribute add
	// Build SYNCHRONOUSLY (RefreshEditor is deferred, its nodes wouldn't exist yet), then land the row ON THE NEW
	// command BY IDENTITY — exactly the attribute tree's OnAdd idiom. The later deferred rebuild restores the
	// current selection (now the new command), so the row stays put.
	RebuildTree();
	SelectCommand(cmd);            // select the new command node (drives OnSelChanged -> inspector)
	m_formHandler->RefreshEditor();// re-render the form canvas so bound command projections re-gather
}

// After a command is created / pasted: refresh the form-editor navigator AND the metadata tree (the two surfaces
// that list it — Reload() is the metadata tree's own "config changed, rebuild"), then hand the command to the
// inspector. A command IS a property object, so its name / synonym / handler are editable there right away — the
// SAME surface the metadata tree edits it through. SelectObject no-ops while the inspector is hidden.
// The selected tree node's FORM COMMAND — or null when the node is a group / a standard action / a global config
// command (not ours to manage), or nothing is selected. A form-command node carries a by-name desc; match it back
// to the entry by name.
ibFormCommandValue* ibCommandTree::SelectedFormCommand() const
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (m_tcCommands == nullptr || form == nullptr)
		return nullptr;
	const wxTreeItemId sel = m_tcCommands->GetSelection();
	if (!sel.IsOk())
		return nullptr;
	const ibAttributeTreeItemData* data = (const ibAttributeTreeItemData*)m_tcCommands->GetItemData(sel);
	if (data == nullptr)
		return nullptr;
	// A form-command node is a straight 1-hop [id] path; the id is the form command's own id. Other nodes (standard
	// actions / metaobject commands / groups) either have no match in the form's registry or aren't 1-hop → null.
	const ibCommandDescription& desc = data->GetCommandDesc();
	if (desc.IsHop() || !desc.IsOk())
		return nullptr;
	return form->FindFormCommandById(desc.GetLeaf());
}

// Copy the selected form command to the clipboard (our own format) — exactly like an attribute.
void ibCommandTree::CopyCommand()
{
	ibFormCommandValue::CopyToClipboard(SelectedFormCommand());
}

// Cut = copy to the command clipboard, then delete — the attribute tree's OnCut idiom, one buffer.
void ibCommandTree::CutCommand()
{
	if (ibFormCommandValue* cmd = SelectedFormCommand()) {
		ibFormCommandValue::CopyToClipboard(cmd);
		DeleteCommand();
	}
}

// Paste a clipboard form command as a fresh entry on the form (unique name + id), then reveal it in the inspector.
void ibCommandTree::PasteCommand()
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;
	if (ibFormCommandValue* cmd = ibFormCommandValue::PasteFromClipboard(form)) {
		m_formHandler->Modify(true);
		RebuildTree();
		SelectCommand(cmd);            // land on the pasted command BY IDENTITY (drives OnSelChanged -> inspector)
		m_formHandler->RefreshEditor();
	}
}

// Delete the selected form command from the form. A button bound to it now resolves to NOTHING — the button ASKS
// the command door on its own refresh (like a control asks its source) and, getting no command back, breaks its
// own look. No walk-and-clear from here: the projection self-validates.
void ibCommandTree::DeleteCommand()
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibFormCommandValue* cmd = SelectedFormCommand();
	if (form == nullptr || cmd == nullptr)
		return;
	form->DeleteFormCommand(cmd->GetName());
	m_formHandler->Modify(true);
	RebuildTree();
	m_formHandler->RefreshEditor();   // re-render → bound buttons re-ask the door and break on the gone command
}
