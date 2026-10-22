////////////////////////////////////////////////////////////////////////////
//	Description : visual editor — the command navigator tree (the command door), its rows the server's
////////////////////////////////////////////////////////////////////////////

// The parallel of the attribute tree, one door over — its own file, as the desktop's. The commands available to the form
// are the server's gather (GatherFormCommands, its rows the View's CommandTree), and the tree is the surface a designer
// DRAGS from: a drag serializes the projection (kind + id) through the engine's memory writer and emits
// "oes_command_drag", which the shared drop target decodes and says to the server (CreateCommandButton). Its first
// section, the form's own commands, is managed here too — add / copy / cut / paste / delete, through the server's editor.

#include "visualEditor.h"
#include "designerTreeCtrl.h"                                          // focus-safe DeleteAllItems (our wxTreeCtrl projection)
#include "visualEditorDragItem.h"                                      // ibCommandDragItem — serialize + start the drag (one kind)

#include "frmclient/backend/backend_picture.h"                         // ibBackendPicture — the attribute's picture
#include "frmclient/backend/metaCollection/metaObject.h"               // g_metaAttributeCLSID — the tree's default picture
#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // its properties — a row's each
#include "designer-thin/docManager/templates/docViewModuleEditor.h"    // ibModuleEditDocument — what the holder is asked through
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/objinspect/objinspect.h"
#include "frmclient/win/picture.h"                                     // ibProtocolPicture — a row's picture

#include <wx/accel.h>     // wxAcceleratorTable — Ctrl+C / X / V / Delete on the tree
#include <wx/artprov.h>   // wxArtProvider — menu-item icons (SAME set the attribute tree uses)
#include <wx/imaglist.h>
#include <wx/menu.h>      // wxMenu — the "add command" context menu

#include <set>

enum { ID_COMMANDTREE_ADD_OBJECT = wxID_HIGHEST + 4210, ID_COMMANDTREE_COPY, ID_COMMANDTREE_CUT, ID_COMMANDTREE_PASTE, ID_COMMANDTREE_DELETE, ID_COMMANDTREE_PROPERTIES };

typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTreeItemData ibAttributeTreeItemData;
typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorCommandTree           ibCommandTree;

namespace {

// A row's command as the server wrote it (CommandPath — its hops, items {Id}; Kind) — none: a group's row.
ibCommandDescription CommandOf(const ibProtocolNode& row)
{
	ibCommandDescription desc;
	for (const ibProtocolNode& hop : row.GetList(ibProtocol::ibView::ibFormDesigner::CommandPath))
		desc.AppendCommand(static_cast<ibMetaID>(hop.GetInt(ibProtocol::ibView::ibFormDesigner::Id)));
	const long long kind = row.GetInt(ibProtocol::ibView::ibFormDesigner::Kind);
	desc.SetCommandType(kind != 0 ? static_cast<ibInterfaceCommandType>(kind) : ibInterfaceCommandType_Default);
	return desc;
}

// The rows' shape — each row's id, name, picture and command, nested as they are: a draw that says the same leaves the
// tree alone.
void ShapeOf(const ibProtocolNode& rows, wxString& shape)
{
	for (const ibProtocolNode& row : rows.Children()) {
		shape << row.GetId() << wxT(':') << row.GetString(ibProtocol::ibView::ibFormDesigner::Name) << wxT(':')
			<< row.GetString(ibProtocol::ibView::ibFormDesigner::Picture) << wxT(':') << row.GetInt(ibProtocol::ibView::ibFormDesigner::Kind);
		for (const ibProtocolNode& hop : row.GetList(ibProtocol::ibView::ibFormDesigner::CommandPath))
			shape << wxT('/') << hop.GetInt(ibProtocol::ibView::ibFormDesigner::Id);
		shape << wxT('{');
		ShapeOf(row, shape);
		shape << wxT('}');
	}
}

// The rows' pictures, by their ids, nested as they are — asked of the server at once, not a call per row drawn.
void PicturesOf(const ibProtocolNode& rows, std::vector<wxString>& pictures)
{
	for (const ibProtocolNode& row : rows.Children()) {
		pictures.push_back(row.GetString(ibProtocol::ibView::ibFormDesigner::Picture));
		PicturesOf(row, pictures);
	}
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// ibFormCommandValue
///////////////////////////////////////////////////////////////////////////////

ibFormCommandValue::ibFormCommandValue(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long id)
	: m_formHandler(formHandler), m_commandId(id)
{
}

bool ibFormCommandValue::IsEditable() const
{
	return m_formHandler->IsEditable();
}

bool ibFormCommandValue::Fetch()
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Properties));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::FormCommand, m_commandId);
	ibProtocolNode answer;
	return m_formHandler->GetEditorDocument()->Fetch(request, answer) && ibPropertyObject::Receive(answer);
}

ibProperty* ibFormCommandValue::CreateProperty(ibPropertyCategory* cat, ibDataNode&& row)
{
	return ibPropertyObject::CreateProperty<ibPropertySerializable>(cat, std::move(row));
}

void ibFormCommandValue::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	m_formHandler->ModifyProperty(property, oldValue, newValue);
}

void ibFormCommandValue::ReadRow(const ibProtocolNode& row)
{
	m_name = row.GetString(ibProtocol::ibView::ibFormDesigner::Name);
}

///////////////////////////////////////////////////////////////////////////////
// ibVisualEditorCommandTree
///////////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibCommandTree, wxPanel)
	EVT_TREE_BEGIN_DRAG(wxID_ANY, ibCommandTree::OnBeginDrag)
wxEND_EVENT_TABLE()

ibCommandTree::ibVisualEditorCommandTree(ibVisualEditor* owner, wxWindow* parent, int id)
	: wxPanel(parent, id), m_formHandler(owner)
{
	m_tcCommands = new ibDesignerTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT);
	m_tcCommands->SetDoubleBuffered(true);   // no flicker on a rebuild
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
	// idiom the attribute tree binds (OnActivated).
	m_tcCommands->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { RevealSelectedCommand(); });
	// ACTIVATING the tree (it gains focus) reveals its SELECTED command too — not only a click (a non-form node no-ops
	// inside RevealSelectedCommand).
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
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddCommand();    }, ID_COMMANDTREE_ADD_OBJECT);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopyCommand();   }, ID_COMMANDTREE_COPY);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { CutCommand();    }, ID_COMMANDTREE_CUT);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { PasteCommand();  }, ID_COMMANDTREE_PASTE);
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) { DeleteCommand(); }, ID_COMMANDTREE_DELETE);
	// "Properties" — the EXPLICIT one: the inspector raised, the selected command shown in it (the attribute tree's
	// OnProperties).
	m_tcCommands->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		if (mainFrame != nullptr)
			mainFrame->ShowInspector();
		RevealSelectedCommand();
	}, ID_COMMANDTREE_PROPERTIES);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tcCommands, 1, wxEXPAND);
	SetSizer(sizer);
}

ibCommandTree::~ibVisualEditorCommandTree()
{
	// The inspector lets go of a holder going with the tree — it is no property object's that clears it.
	for (const auto& held : m_entries) {
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == held.second.get())
			objectInspector->SelectObject(nullptr);
	}
}

void ibCommandTree::Draw(const ibProtocolNode& rows)
{
	wxString shape;
	ShapeOf(rows, shape);
	if (shape == m_shape)
		return;
	m_shape = shape;

	// THE HOLDERS — one per form command's row (the first section's, the rows with an id), kept by its id: the one the
	// inspector shows survives the rebuild.
	std::set<long long> drawn;
	for (const ibProtocolNode& section : rows.Children()) {
		for (const ibProtocolNode& row : section.Children()) {
			if (row.GetId() == 0)
				continue;
			std::unique_ptr<ibFormCommandValue>& held = m_entries[row.GetId()];
			if (held == nullptr)
				held.reset(new ibFormCommandValue(m_formHandler, row.GetId()));
			held->ReadRow(row);
			drawn.insert(row.GetId());
		}
	}

	m_rebuilding = true;
	m_tcCommands->Freeze();
	// BLOCK the tree's events for the churn: DeleteAllItems / SelectItem otherwise fire native selection + focus
	// events, and the app's OnSetFocus follows that focus and switches the active MDI tab — a background rebuild
	// would yank the tab from the editor the user is in. Re-enabled after the row is restored below.
	m_tcCommands->SetEvtHandlerEnabled(false);
	m_tcCommands->DeleteAllItems();

	// A fresh image list per rebuild: index 0 is the group / default picture (the attribute's, as the desktop's), then
	// one slot per command's own picture.
	wxImageList* images = new wxImageList(16, 16);
	const wxBitmap attributePicture = ibBackendPicture::GetPicture(g_metaAttributeCLSID);
	if (attributePicture.IsOk())
		images->Add(attributePicture);

	const wxTreeItemId root = m_tcCommands->AddRoot(wxT("Commands"));

	std::vector<wxString> pictures;
	PicturesOf(rows, pictures);
	ibProtocolPictureReceive(pictures);

	// ALL three sections (form / standard / global) come, in order, even EMPTY — the server's. The FORM COMMANDS one is
	// remembered: the context menu (Add / Copy / Paste / Delete) works ONLY there.
	m_formCommandsGroup = wxTreeItemId();
	wxTreeItemId keepNode;
	for (const ibProtocolNode& section : rows.Children()) {
		const wxTreeItemId sectionNode = m_tcCommands->AppendItem(root, section.GetString(ibProtocol::ibView::ibFormDesigner::Name), 0, 0);
		if (!m_formCommandsGroup.IsOk())
			m_formCommandsGroup = sectionNode;
		AppendRows(sectionNode, section, images, m_selectedDesc, keepNode);
	}
	m_tcCommands->AssignImageList(images);

	m_tcCommands->ExpandAll();   // sections + their sub-groups all open

	m_tcCommands->Thaw();

	// …and a holder whose row is gone goes too — let go of first where it is shown.
	for (auto it = m_entries.begin(); it != m_entries.end(); ) {
		if (drawn.find(it->first) != drawn.end()) {
			++it;
			continue;
		}
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == it->second.get())
			objectInspector->SelectObject(nullptr);
		it = m_entries.erase(it);
	}

	// Restore the remembered row, KEEPING the guard set so SelectItem's SEL_CHANGED is swallowed — a rebuild driven by
	// an edit on another surface must not push THIS tree's selection into the inspector.
	if (keepNode.IsOk()) {
		m_tcCommands->SelectItem(keepNode);
		m_tcCommands->EnsureVisible(keepNode);
	}
	m_tcCommands->SetEvtHandlerEnabled(true);   // churn done, row restored — resume normal event handling
	m_rebuilding = false;
}

void ibCommandTree::AppendRows(const wxTreeItemId& parent, const ibProtocolNode& rows, wxImageList* images,
	const ibCommandDescription& keepDesc, wxTreeItemId& keepNode)
{
	for (const ibProtocolNode& row : rows.Children()) {
		const ibCommandDescription desc = CommandOf(row);
		if (!desc.IsOk()) {   // a per-source SUB-group (the form / a table) — its commands under it
			AppendRows(m_tcCommands->AppendItem(parent, row.GetString(ibProtocol::ibView::ibFormDesigner::Name), 0, 0), row, images, keepDesc,
				keepNode);
			continue;
		}
		const wxBitmap picture = ibProtocolPicture(row.GetString(ibProtocol::ibView::ibFormDesigner::Picture));
		const int icon = picture.IsOk() ? images->Add(picture) : 0;   // the command's own picture, else default
		const wxTreeItemId node = m_tcCommands->AppendItem(parent, row.GetString(ibProtocol::ibView::ibFormDesigner::Name), icon, icon,
			new ibAttributeTreeItemData(desc));
		if (keepDesc.IsOk() && desc == keepDesc)   // the row that was selected before this rebuild — remember it
			keepNode = node;
	}
}

ibFormCommandValue* ibCommandTree::FindEntry(const ibPropertyObject* owner) const
{
	for (const auto& held : m_entries) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	return nullptr;
}

// Land the row on a form command BY IDENTITY — the twin of the attribute tree's SelectEntry. Form-command nodes live
// directly under the "Form commands" group (they carry no sub-group); match the one whose 1-hop desc leaf is the
// command's id. The row lit, not said again: what selected it is the server's already.
void ibCommandTree::SelectCommand(long long id)
{
	if (id == 0 || m_tcCommands == nullptr || !m_formCommandsGroup.IsOk())
		return;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId item = m_tcCommands->GetFirstChild(m_formCommandsGroup, cookie); item.IsOk();
		item = m_tcCommands->GetNextChild(m_formCommandsGroup, cookie)) {
		const ibAttributeTreeItemData* data = (const ibAttributeTreeItemData*)m_tcCommands->GetItemData(item);
		if (data == nullptr)
			continue;
		const ibCommandDescription& desc = data->GetCommandDesc();
		if (!desc.IsHop() && desc.IsOk() && desc.GetLeaf() == static_cast<ibMetaID>(id)) {
			const bool rebuilding = m_rebuilding;
			m_rebuilding = true;
			m_tcCommands->SelectItem(item);
			m_tcCommands->EnsureVisible(item);
			m_rebuilding = rebuilding;
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

	// Dragging a command onto a control BINDS it — a form edit; blocked on a view-only form (leaving the
	// drag unstarted). Same gate as the menu / object-tree drag.
	if (!m_formHandler->IsEditable())
		return;

	// The COMMAND drag KIND owns its format + serialization (symmetric to the attribute tree's source kind) — the
	// command-hop path goes through the ENGINE writer, not a raw byte pack. Construction IS the drag.
	ibCommandDragItem(m_tcCommands, data->GetCommandDesc());
}

void ibCommandTree::OnContextMenu(wxContextMenuEvent& event)
{
	// The menu (Add / Copy / Paste / Delete) works ONLY inside the FORM COMMANDS group — its header or a command
	// under it. Right-click on the standard / global sections (read-only, drag-out only) shows nothing.
	const wxPoint screenPt = event.GetPosition() != wxDefaultPosition ? event.GetPosition() : wxGetMousePosition();
	const wxTreeItemId hit = m_tcCommands->HitTest(m_tcCommands->ScreenToClient(screenPt));
	// Right-click SELECTS the item under the cursor first — otherwise the previously selected row stays highlighted
	// and TWO rows look selected. Mirrors the attribute tree's OnContextMenu.
	if (hit.IsOk())
		m_tcCommands->SelectItem(hit);
	wxTreeItemId section = hit;
	while (section.IsOk() && m_tcCommands->GetItemParent(section) != m_tcCommands->GetRootItem())
		section = m_tcCommands->GetItemParent(section);
	if (!section.IsOk() || section != m_formCommandsGroup)
		return;

	// The SAME icon'd list the attribute tree shows — Add / Copy / Cut / Paste / Delete, greyed when it does not
	// apply. Copy / Cut / Delete gate on a FORM COMMAND node; Paste greys when the command clipboard is empty. A
	// view-only form can be browsed but not restructured — every MUTATING item is disabled.
	const bool hasCmd = SelectedFormCommand() != nullptr;
	const bool editable = m_formHandler->IsEditable();
	wxMenu menu;
	auto appendItem = [&menu](int menuId, const wxString& label, const wxArtID& art, bool enabled) {
		wxMenuItem* item = new wxMenuItem(&menu, menuId, label);
		item->SetBitmap(wxArtProvider::GetBitmap(art, wxART_MENU));
		menu.Append(item);
		item->Enable(enabled);
	};
	appendItem(ID_COMMANDTREE_ADD_OBJECT, _("Add command"), wxART_NEW, editable);   // mutates → locked when read-only
	menu.AppendSeparator();
	appendItem(ID_COMMANDTREE_CUT,   _("Cut"),   wxART_CUT,    hasCmd && editable);
	appendItem(ID_COMMANDTREE_COPY,  _("Copy"),  wxART_COPY,   hasCmd);                         // read-only safe
	appendItem(ID_COMMANDTREE_PASTE, _("Paste"), wxART_PASTE,  m_formHandler->CanPasteFormCommand() && editable);
	menu.AppendSeparator();
	appendItem(ID_COMMANDTREE_DELETE, _("Delete"), wxART_DELETE, hasCmd && editable);
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
	// The row kept by its command — any node's, so it survives a rebuild. A form command IS a property object — the
	// current element (the desktop's SetCurrentElement); other nodes (standard actions / global commands / groups) have
	// nothing to edit, so the inspector is left as-is.
	const wxTreeItemId sel = m_tcCommands->GetSelection();
	const ibAttributeTreeItemData* data = sel.IsOk() ? (const ibAttributeTreeItemData*)m_tcCommands->GetItemData(sel) : nullptr;
	if (data != nullptr)
		m_selectedDesc = data->GetCommandDesc();
	if (ibFormCommandValue* const cmd = SelectedFormCommand())
		m_formHandler->SetCurrentElement(cmd);
}

void ibCommandTree::AddCommand()
{
	// A form command is a form-LOCAL event (NOT a metaobject — nothing goes into the metadata tree), uniquely named, its
	// Action empty — the server's editor makes it; the tree lands on it and shows it, as the desktop's AddCommand. View-only
	// form — none (the accelerator reaches here bypassing the greyed menu item).
	if (!m_formHandler->IsEditable())
		return;
	const long long id = m_formHandler->CreateFormCommand();
	if (id == 0)
		return;
	SelectCommand(id);
	RevealSelectedCommand();
}

// The selected tree node's FORM COMMAND — or null when the node is a group / a standard action / a global config
// command (not ours to manage), or nothing is selected. A form-command node is a straight 1-hop [id] path; the id is
// the form command's own, the holder's.
ibFormCommandValue* ibCommandTree::SelectedFormCommand() const
{
	if (m_tcCommands == nullptr)
		return nullptr;
	const wxTreeItemId sel = m_tcCommands->GetSelection();
	if (!sel.IsOk())
		return nullptr;
	const ibAttributeTreeItemData* data = (const ibAttributeTreeItemData*)m_tcCommands->GetItemData(sel);
	if (data == nullptr)
		return nullptr;
	const ibCommandDescription& desc = data->GetCommandDesc();
	if (desc.IsHop() || !desc.IsOk())
		return nullptr;
	const auto found = m_entries.find(static_cast<long long>(desc.GetLeaf()));
	return found != m_entries.end() ? found->second.get() : nullptr;
}

// Copy the selected form command to the clipboard (our own format) — exactly like an attribute; written on the server.
void ibCommandTree::CopyCommand()
{
	if (ibFormCommandValue* cmd = SelectedFormCommand())
		m_formHandler->CopyFormCommand(cmd);
}

// Cut = copy to the command clipboard, then delete — the attribute tree's OnCut idiom, one buffer.
void ibCommandTree::CutCommand()
{
	if (!m_formHandler->IsEditable())
		return;
	if (ibFormCommandValue* cmd = SelectedFormCommand())
		m_formHandler->CutFormCommand(cmd);
}

// Paste a clipboard form command as a fresh entry on the form (unique name + id) — the server shows it, the tree lands
// on it.
void ibCommandTree::PasteCommand()
{
	const long long id = m_formHandler->PasteFormCommand();
	if (id == 0)
		return;
	SelectCommand(id);
	RevealSelectedCommand();
}

// Delete the selected form command from the form. A button bound to it now resolves to NOTHING — the button asks the
// command door on its own refresh and, getting no command back, breaks its own look.
void ibCommandTree::DeleteCommand()
{
	if (!m_formHandler->IsEditable())
		return;
	if (ibFormCommandValue* cmd = SelectedFormCommand())
		m_formHandler->RemoveFormCommand(cmd);
}
