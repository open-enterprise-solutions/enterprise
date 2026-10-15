////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"   // ibFormDragItem — a row dropped on a node
#include "designerTreeCtrl.h"    // focus-safe DeleteAllItems (our wxTreeCtrl projection)

#include "core/diagnostics/journal.h"

#include "core/picturePredefined.h"   // g_picCommandBarCLSID — the command bar's node
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture::GetPicture — an engine's picture
#include "frmclient/mainFrame/objinspect/objinspect.h"
#include "frmclient/win/picture.h"   // ibProtocolPicture — a class's picture, as the answer brought it

#include <wx/imaglist.h>

namespace {

// The form as the tree shows it — each control, its class and name, nested as they are. The control is said by its
// address too: one built anew for the same node is another the tree must not keep.
void ShapeOf(ibValueFrame* obj, wxString& shape)
{
	shape << wxString::Format(wxT("%p"), static_cast<const void*>(obj)) << wxT(':') << obj->GetClassName() << wxT(':')
		<< obj->GetControlName() << wxT('[');
	for (unsigned int i = 0; i < obj->GetChildCount(); i++)
		ShapeOf(obj->GetChild(i), shape);
	shape << wxT(']');
}

// …and the commands of each control's bar, as its node has them: one added, removed, moved or renamed is drawn too.
void ShapeOf(const ibProtocolNode& node, const std::map<long long, ibProtocolNode>& rows, wxString& shape)
{
	const long long controlId = node.GetInt(ibProtocolName::ControlId);
	const auto found = rows.find(controlId);
	if (found != rows.end() && found->second.FindChild(ibProtocolName::CommandBar).IsNode()) {
		shape << controlId << wxT('{');
		for (const ibProtocolNode& item : node.FindChild(ibProtocolName::Layers).FindChild(ibProtocolName::CommandBar).Children())
			shape << item.GetString(ibProtocolName::Name) << wxT(';');
		shape << wxT('}');
	}
	for (const ibProtocolNode& child : node.Children())
		ShapeOf(child, rows, shape);
}

// The desktop's CanDeleteControl (frmserver ibValueForm, ibValueNotebookPage, ibValueModelTableBoxColumn): never the
// form, nor the last page of a notebook, nor the last column of a table.
bool CanDeleteControl(const ibValueFrame* obj)
{
	if (obj == nullptr || obj->GetParent() == nullptr)
		return false;
	if (obj->GetClassName() == ibProtocolType::NotebookPage || obj->GetClassName() == ibProtocolType::TableboxColumn)
		return obj->GetParent()->GetChildCount() > 1;
	return true;
}

} // namespace

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::RebuildTree()
{
	wxWindow::Freeze();
	// BLOCK the tree's events for the churn: DeleteAllItems / SelectItem otherwise fire native selection + focus
	// events, and the app's OnSetFocus follows that focus and switches the active MDI tab — a background rebuild
	// would yank the tab from the editor the user is in. Re-enabled before Thaw, after the row is restored.
	m_tcObjects->SetEvtHandlerEnabled(false);

	Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
	Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

	ibValueFrame* valueForm =
		m_formHandler->GetValueForm();

	// Remember the selected LAYER by identity (a command bar / a command — its holder outlives the rebuild): without
	// this the DeleteAllItems below drops the highlight onto the root form. A control is lit again by the selection the
	// command left (OnObjectSelected).
	const ibValueLayerObject* keepLayer = nullptr;
	if (const wxTreeItemId sel = m_tcObjects->GetSelection(); sel.IsOk())
		if (auto* keepData = (ibVisualEditorObjectTreeItemData*)m_tcObjects->GetItemData(sel))
			keepLayer = keepData->GetLayerObject();

	// Clear the old tree and map
	m_tcObjects->DeleteAllItems();
	m_listItem.clear();
	m_layerItemMap.clear();

	if (valueForm != nullptr) {
		wxTreeItemId dummy;
		AddChildren(valueForm, dummy, true);
		// Expand items that were previously expanded
		RestoreItemStatus(valueForm);

		// Restore the row highlight by identity — under m_notifySelecting so SelectItem sets the bold mark WITHOUT
		// re-pushing the inspector (OnSelChanged bolds before the guard early-out), matching OnObjectSelected.
		const auto kept = keepLayer != nullptr ? m_layerItemMap.find(keepLayer) : m_layerItemMap.end();
		if (kept != m_layerItemMap.end()) {
			m_notifySelecting = true;
			m_tcObjects->SelectItem(kept->second);
			m_notifySelecting = false;
		}
	}

	// …and a layer's holder whose node is gone goes too — let go of first where it is shown.
	const auto gone = [this](const ibValueLayerObject* layer) {
		if (m_layerItemMap.find(layer) != m_layerItemMap.end())
			return false;
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == layer)
			objectInspector->SelectObject(nullptr);
		return true;
	};
	for (auto it = m_commandBars.begin(); it != m_commandBars.end(); )
		it = gone(it->second.get()) ? m_commandBars.erase(it) : std::next(it);
	for (auto it = m_commandBarItems.begin(); it != m_commandBarItems.end(); )
		it = gone(it->second.get()) ? m_commandBarItems.erase(it) : std::next(it);

	m_tcObjects->SetEvtHandlerEnabled(true);   // churn done — resume normal event handling

	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

	wxWindow::Thaw();

	wxWindow::Refresh();
	wxWindow::Update();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::AddChildren(ibValueFrame* obj, const wxTreeItemId& parent, bool is_root)
{
	if (obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0)
			AddChildren(obj->GetChild(0), parent);
		else {
			// If we reached here it is because the tree is malformed and how it was built needs to be reviewed.
			ibValueFrame* const itemParent = obj->GetParent();
			ibJournalError(wxT("designer"), wxString::Format(wxT("Item without object as child of '%s:%s'"),
				itemParent != nullptr ? itemParent->GetControlName() : wxString(),
				itemParent != nullptr ? itemParent->GetClassName() : wxString()));
		}
		return;
	}

	wxTreeItemId new_parent;
	ibVisualEditorObjectTreeItemData* item_data = new ibVisualEditorObjectTreeItemData(obj);
	if (is_root) {
		new_parent = m_tcObjects->AddRoot(wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
	}
	else {
		new_parent = m_tcObjects->AppendItem(parent, wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
	}

	// Add the item to the map
	m_listItem.insert(
		std::map< ibValueFrame*, wxTreeItemId>::value_type(obj, new_parent)
	);

	// Set the image
	int image_idx = GetImageIndex(obj->GetClassName());

	if (image_idx != wxNOT_FOUND) {
		m_tcObjects->SetItemImage(new_parent, image_idx);
	}

	// Set the name
	UpdateItem(new_parent, obj);

	// Command interface (a chrome LAYER the frame carries): a static sub-node added FIRST, so it sits above the content
	// like the rendered toolbar.
	AddCommandBar(obj, new_parent);

	// Add the rest of the children
	unsigned int count = obj->GetChildCount();

	for (unsigned int i = 0; i < count; i++) {
		ibValueFrame* child = obj->GetChild(i);
		AddChildren(child, new_parent);
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::AddCommandBar(ibValueFrame* obj, const wxTreeItemId& parent)
{
	// The desktop's HasCommandBar: the server writes the bar's rows only of a control that has one.
	const long long controlId = obj->GetControlID();
	const auto found = m_formHandler->m_rows.find(controlId);
	if (found == m_formHandler->m_rows.end() || !found->second.FindChild(ibProtocolName::CommandBar).IsNode())
		return;

	// Selecting it edits the bar's settings (AutoFill) — what it holds received as it is shown. Icon = the toolbar's.
	std::unique_ptr<ibValueCommandBar>& bar = m_commandBars[controlId];
	if (bar == nullptr)
		bar.reset(new ibValueCommandBar(m_formHandler, controlId));
	const wxTreeItemId barNode = m_tcObjects->AppendItem(parent, _("Command interface"),
		GetImageIndex(ibProtocolName::CommandBar), wxNOT_FOUND, new ibVisualEditorObjectTreeItemData(bar.get()));
	m_layerItemMap[bar.get()] = barNode;   // the bar node — re-selectable by identity after a rebuild

	// Its commands as sub-nodes — remembered too, so a just-added / moved command can be re-selected after the rebuild.
	const std::vector<ibProtocolNode> items = bar->GetNode().Children();
	for (unsigned int idx = 0; idx < items.size(); idx++) {
		std::unique_ptr<ibValueCommandBarItem>& item = m_commandBarItems[{ controlId, idx }];
		if (item == nullptr)
			item.reset(new ibValueCommandBarItem(m_formHandler, controlId, idx));
		const wxTreeItemId itemNode = m_tcObjects->AppendItem(barNode, items[idx].GetString(ibProtocolName::Name),
			GetImageIndex(ibProtocolName::CommandBarItem), wxNOT_FOUND, new ibVisualEditorObjectTreeItemData(item.get()));
		m_layerItemMap[item.get()] = itemNode;
	}

	// Open, so a freshly added command is visible without a manual expand (the desktop's default) — unless it was closed.
	if (!items.empty() && bar->IsTreeExpanded())
		m_tcObjects->Expand(barNode);
}

#define ICON_SIZE 16

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::CreateTree(const ibProtocolNode& classes)
{
	if (m_iconList != nullptr)
		m_tcObjects->SetImageList(nullptr);
	m_iconIdx.clear();

	m_iconList = new wxImageList(ICON_SIZE, ICON_SIZE);

	auto addIcon = [this](const wxString& name, wxBitmap controlIcon) {
		if (!controlIcon.IsOk())
			return;
		if (controlIcon.GetSize() != wxSize(ICON_SIZE, ICON_SIZE))
			controlIcon = wxBitmap(controlIcon.ConvertToImage().Rescale(ICON_SIZE, ICON_SIZE, wxIMAGE_QUALITY_HIGH));
		const int retIndex = m_iconList->Add(controlIcon);
		if (retIndex != wxNOT_FOUND) {
			m_iconIdx.insert(
				std::map<wxString, int>::value_type(name, retIndex)
			);
		}
	};

	for (const ibProtocolNode& described : classes.Children())
		addIcon(described.GetString(ibProtocolName::Name), ibProtocolPicture(described.GetString(ibProtocolName::Picture)));

	// The command bar and its command — the engine's pictures, not a class the server describes (the desktop's were its
	// toolbar's and tool's).
	addIcon(ibProtocolName::CommandBar, ibBackendPicture::GetPicture(g_picCommandBarCLSID));
	addIcon(ibProtocolName::CommandBarItem, ibBackendPicture::GetPicture(g_picCommandBarItemCLSID));

	m_tcObjects->AssignImageList(m_iconList);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::RestoreItemStatus(ibValueFrame* obj)
{
	std::map< ibValueFrame*, wxTreeItemId>::iterator item_it = m_listItem.find(obj);
	if (item_it != m_listItem.end()) {
		wxTreeItemId id = item_it->second;

		if (obj->GetExpanded()) {
			m_tcObjects->Expand(id);
		}
	}

	unsigned int i, count = obj->GetChildCount();

	for (i = 0; i < count; i++) {
		RestoreItemStatus(obj->GetChild(i));
	}
}

ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::GetObjectFromTreeItem(const wxTreeItemId& item)
{
	if (item.IsOk()) {
		wxTreeItemData* item_data = m_tcObjects->GetItemData(item);
		if (item_data) {
			ibValueFrame* obj(((ibVisualEditorObjectTreeItemData*)item_data)->GetObject());
			return obj;
		}
	}

	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////
// Enterprise ibEvent Handlers
/////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEditorLoaded()
{
	m_shape.clear();
	OnEditorRefresh();
}

// THE FORM DRAWN AGAIN — the tree rebuilt only when what it shows changed (a control added, removed, moved or renamed, a
// command of a bar); the controls themselves are the frame's anew on a change of shape, so the map is rebuilt with them.
void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEditorRefresh()
{
	wxString shape;
	if (ibValueFrame* const valueForm = m_formHandler->GetValueForm())
		ShapeOf(valueForm, shape);
	ShapeOf(m_formHandler->m_formNode, m_formHandler->m_rows, shape);

	if (shape == m_shape && !m_listItem.empty()) {
		// …open or closed as the form has it now — an expansion undone or redone leaves the shape as it was.
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		for (const auto& listed : m_listItem)
			OnObjectExpanded(listed.first);
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		return;
	}

	m_shape = shape;
	RebuildTree();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnObjectSelected(ibValueFrame* obj)
{
	// Find the tree item associated with the object and select it
	std::map< ibValueFrame*, wxTreeItemId>::iterator it = m_listItem.find(obj);
	if (it != m_listItem.end()) {

		m_notifySelecting = true;

		// Ignore expand/collapse events
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

		m_tcObjects->EnsureVisible(it->second);
		m_tcObjects->SelectItem(it->second);

		// Restore event handling
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

		m_notifySelecting = false;
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnObjectExpanded(ibValueFrame* obj)
{
	std::map< ibValueFrame*, wxTreeItemId>::iterator it = m_listItem.find(obj);
	if (it != m_listItem.end())
	{
		if (m_tcObjects->IsExpanded(it->second) != obj->GetExpanded())
		{
			if (obj->GetExpanded()) {
				m_tcObjects->Expand(it->second);
			}
			else {
				m_tcObjects->Collapse(it->second);
			}
		}
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnPropertyModified(ibProperty* prop)
{
	std::map< ibValueFrame*, wxTreeItemId>::iterator it =
		m_listItem.find(dynamic_cast<ibValueFrame*>(prop->GetPropertyObject()));
	if (it != m_listItem.end()) {
		UpdateItem(it->second, it->first);
	}
}

ibValueLayerObject* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::FindEntry(const ibPropertyObject* owner) const
{
	for (const auto& held : m_commandBars) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	for (const auto& held : m_commandBarItems) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	return nullptr;
}

ibValueLayerObject* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::FindEntry(long long controlId) const
{
	const auto found = m_commandBars.find(controlId);
	return found != m_commandBars.end() ? found->second.get() : nullptr;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectCommandItem(const ibValueLayerObject* layer)
{
	const auto found = m_layerItemMap.find(layer);
	if (found == m_layerItemMap.end() || !found->second.IsOk())
		return;

	m_notifySelecting = true;
	m_tcObjects->EnsureVisible(found->second);   // expands ancestor nodes so the item shows
	m_tcObjects->SelectItem(found->second);
	m_notifySelecting = false;

	// …and the inspector shows it — also when it was the selected node already.
	SelectItemData(m_tcObjects->GetItemData(found->second));
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectCommandItem(long long controlId, unsigned int index)
{
	const auto found = m_commandBarItems.find({ controlId, index });
	if (found != m_commandBarItems.end())
		SelectCommandItem(found->second.get());
}

wxBEGIN_EVENT_TABLE(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree, wxPanel)
EVT_TREE_SEL_CHANGED(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnSelChanged)
EVT_TREE_ITEM_RIGHT_CLICK(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnRightClick)
EVT_TREE_BEGIN_DRAG(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnBeginDrag)
EVT_TREE_END_DRAG(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEndDrag)
EVT_TREE_KEY_DOWN(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnKeyDown)
wxEND_EVENT_TABLE()

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::ibVisualEditorObjectTree(ibVisualEditor* handler, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_formHandler(handler)
{
	m_tcObjects = new ibDesignerTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_NO_LINES | wxSIMPLE_BORDER | wxTR_TWIST_BUTTONS);

	wxBoxSizer* sizerMain = new wxBoxSizer(wxVERTICAL);
	sizerMain->Add(m_tcObjects, 1, wxEXPAND, 0);
	wxPanel::SetAutoLayout(true);
	wxPanel::SetSizer(sizerMain);
	sizerMain->Fit(this);
	sizerMain->SetSizeHints(this);

	m_tcObjects->SetDoubleBuffered(true);

	// Accept any registered drag KIND dropped ONTO a tree node: a source path → a bound control under that node
	// (its container), a command → a command button on the nearest bar up from it. ONE resolver for every kind:
	// hit-test the drop point to the tree item → its owning frame (a layer node resolves to its owner frame; an
	// empty area → null, which the source's CreateBoundControl falls back to the form root).
	m_tcObjects->SetDropTarget(
		new ibSourceDragDropTarget(
			[this](wxCoord x, wxCoord y) -> ibValueFrame* {
				m_formHandler->SetPendingDropBar(0);   // no command-bar toolbar in the object tree — clear the
				                                       // per-drop channel so a command drop here can't read a stale bar
				int flags = 0;
				const wxTreeItemId item = m_tcObjects->HitTest(wxPoint(x, y), flags);
				ibValueFrame* target = GetObjectFromTreeItem(item);
				ibValueForm* const valueForm = m_formHandler->GetValueForm();
				if (target == nullptr && item.IsOk() && valueForm != nullptr)
					if (wxTreeItemData* itemData = m_tcObjects->GetItemData(item))
						if (ibValueLayerObject* layer = ((ibVisualEditorObjectTreeItemData*)itemData)->GetLayerObject())
							target = layer->GetControlID() == valueForm->GetControlID() ?
								valueForm : valueForm->FindControlByID(layer->GetControlID());
				return target;
			},
			[this](const ibFormDragItem& item, ibValueFrame* target) { item.ApplyDrop(m_formHandler, target); }));

	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

	auto reselectOnClick = [this](wxMouseEvent& event) {
		const wxTreeItemId& id = m_tcObjects->HitTest(event.GetPosition());
		if (id.IsOk() && id == m_tcObjects->GetSelection())
			SelectItemData(m_tcObjects->GetItemData(id));
		event.Skip();
	};
	m_tcObjects->Bind(wxEVT_LEFT_DOWN, reselectOnClick);
	m_tcObjects->Bind(wxEVT_RIGHT_DOWN, reselectOnClick);
	// ACTIVATING the tree (it gains focus) reveals its SELECTED node too — not only a click. So switching to the object
	// tree re-surfaces the current control / layer in the inspector (SelectObject early-outs if it is already current).
	m_tcObjects->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
		event.Skip();
		if (const wxTreeItemId sel = m_tcObjects->GetSelection(); sel.IsOk())
			SelectItemData(m_tcObjects->GetItemData(sel));
	});
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::~ibVisualEditorObjectTree()
{
	// The inspector lets go of a holder going with the tree — it is no property object's that clears it.
	const auto release = [](const ibValueLayerObject& layer) {
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == &layer)
			objectInspector->SelectObject(nullptr);
	};
	for (const auto& held : m_commandBars)
		release(*held.second);
	for (const auto& held : m_commandBarItems)
		release(*held.second);
}

// Decode a tree item's payload and select it — one place for the click-reselect and the
// selection-changed handlers. A layer node (bar/command) goes to the inspector; a control node
// goes to the visual editor (which also scrolls to it).
void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectItemData(wxTreeItemData* item_data)
{
	if (item_data == nullptr)
		return;
	auto* data = (ibVisualEditorObjectTreeItemData*)item_data;
	if (ibValueLayerObject* const layer = data->GetLayerObject()) {
		m_formHandler->SetCurrentElement(layer);   // a tree click is SOFT — revealed in an open inspector, not popped open
		return;
	}
	ibValueFrame* obj(data->GetObject());
	assert(obj);
	m_formHandler->SelectObject(obj, false, false);
	m_formHandler->ScrollToObject(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnSelChanged(wxTreeEvent& event)
{
	// Make selected items bold
	wxTreeItemId oldId = event.GetOldItem();
	if (oldId.IsOk()) m_tcObjects->SetItemBold(oldId, false);

	const wxTreeItemId& id = event.GetItem();
	if (!id.IsOk())
		return;
	m_tcObjects->SetItemBold(id);

	wxTreeItemData* item_data = m_tcObjects->GetItemData(id);

	if (m_notifySelecting)
		return;

	SelectItemData(item_data);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnRightClick(wxTreeEvent& event)
{
	wxTreeItemId id = event.GetItem();
	wxTreeItemData* item_data = id.IsOk() ? m_tcObjects->GetItemData(id) : nullptr;
	if (item_data == nullptr)
		return;
	if (ibValueLayerObject* const layer = ((ibVisualEditorObjectTreeItemData*)item_data)->GetLayerObject()) {
		// Layer node (bar OR command) — the object fills its own menu and runs the choice; the tree never branches per
		// kind.
		wxMenu menu;
		layer->PrepareDefaultMenu(&menu);
		const int sel = GetPopupMenuSelectionFromUser(menu, event.GetPoint());
		if (sel != wxID_NONE)
			layer->ExecuteMenu(sel);
		return;
	}
	ibValueFrame* obj = GetObjectFromTreeItem(id);
	if (obj != nullptr) {
		m_formHandler->SelectObject(obj);
		// On the stack — PopupMenu does not take ownership and blocks until dismissed.
		ibVisualEditorItemPopupMenu menu(m_formHandler, this, obj);
		wxPoint pos = event.GetPoint();
		menu.UpdateUI(&menu);
		PopupMenu(&menu, pos.x, pos.y);
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnKeyDown(wxTreeEvent& event)
{
	// Alt+Up / Alt+Down reorder the selected control among its siblings (GTK only — on MSVC the tree drives this
	// itself). Guard the selection: a layer node (command bar) or empty selection resolves to null, which must NOT
	// be passed to MovePosition. Alt+Left/Right (move a control IN / OUT of a container) has no editor primitive yet
	// — it would be a real cross-platform reparent command with undo, not a keystroke — so it falls through to default.
	if (event.GetKeyEvent().AltDown() && event.GetKeyCode() != WXK_ALT)
	{
#ifdef __WXGTK__
		if (m_formHandler->IsEditable()) {   // view-only: no keyboard reorder, same gate as the MOVE menu / drag
			if (ibValueFrame* obj = GetObjectFromTreeItem(m_tcObjects->GetSelection())) {
				if (event.GetKeyCode() == WXK_UP)   { m_formHandler->MovePosition(obj, false); return; }
				if (event.GetKeyCode() == WXK_DOWN) { m_formHandler->MovePosition(obj, true);  return; }
			}
		}
#endif
		event.Skip();
	}
	else
	{
		event.Skip();
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnBeginDrag(wxTreeEvent& event)
{
	// need to explicitly allow drag
	if (event.GetItem() == m_tcObjects->GetRootItem())
		return;

	// A view-only form's layout is frozen — drag reorders/reparents controls, so gate it exactly like the
	// menu MOVE items (leaving event unallowed cancels the drag).
	if (!m_formHandler->IsEditable())
		return;

	m_draggedItem = event.GetItem();
	event.Allow();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEndDrag(wxTreeEvent& event)
{
	bool copy = ::wxGetKeyState(WXK_CONTROL);
	wxTreeItemId itemSrc = m_draggedItem, itemDst = event.GetItem();
	m_draggedItem = (wxTreeItemId)0l;

	// ensure that itemDst is not itemSrc or a child of itemSrc
	wxTreeItemId item = itemDst;
	while (item.IsOk()) {
		if (item == itemSrc)
			return;
		item = m_tcObjects->GetItemParent(item);
	}

	ibValueFrame* objSrc =
		GetObjectFromTreeItem(itemSrc);
	if (!objSrc)
		return;

	ibValueFrame* objDst =
		GetObjectFromTreeItem(itemDst);

	if (!objDst)
		return;

	// Done once the drag is over — and each step on the controls as the one before left them: a command draws the
	// form anew, the controls with it, so the target is found again by its id.
	ibVisualEditor* const handler = m_formHandler;
	const long long srcId = objSrc->GetControlID(), dstId = objDst->GetControlID();
	handler->CallAfter([handler, copy, srcId, dstId]() {
		const auto find = [handler](long long id) -> ibValueFrame* {
			ibValueForm* const form = handler->GetValueForm();
			if (form == nullptr)
				return nullptr;
			return form->GetControlID() == id ? form : form->FindControlByID(id);
		};

		// set object to clipboard
		if (copy) {
			handler->CopyObject(find(srcId));
		}
		else {
			handler->CutObject(find(srcId));
		}

		if (!copy && !handler->PasteObject(find(dstId))) {
			handler->Undo();
		}
	});
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange(wxTreeEvent& event)
{
	wxTreeItemId id = event.GetItem();
	wxTreeItemData* item_data = m_tcObjects->GetItemData(id);

	if (item_data != nullptr) {
		auto* data = (ibVisualEditorObjectTreeItemData*)item_data;
		ibValueFrame* obj(data->GetObject());
		if (obj == nullptr) {
			// layer node — keep its open-state on the object so RebuildTree restores it (leaf = no-op).
			if (ibValueLayerObject* layer = data->GetLayerObject())
				layer->SetTreeExpanded(m_tcObjects->IsExpanded(id));
			return;
		}
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
		m_formHandler->ExpandObject(obj, m_tcObjects->IsExpanded(id));
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
	}
}

///////////////////////////////////////////////////////////////////////////////

enum {
	MENU_MOVE_UP = wxID_HIGHEST + 2000,
	MENU_MOVE_DOWN,
	MENU_CUT,
	MENU_PASTE,
	MENU_EDIT_MENUS,
	MENU_COPY,
	MENU_MOVE_NEW_BOXSIZER,
	MENU_DELETE,
	MENU_PROPERTIES,

	MENU_CREATE_OBJECT = wxID_HIGHEST + 2050,   // …and the classes the control adds in itself, by their place
};

wxBEGIN_EVENT_TABLE(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu, wxMenu)
EVT_MENU(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnMenuEvent)
EVT_UPDATE_UI(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnUpdateEvent)
wxEND_EVENT_TABLE()

bool ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::HasDeleteObject() {
	return m_selID == MENU_DELETE;
}

// The control's own items first (a table's "Add column", a notebook's "Add page" — the desktop's PrepareDefaultMenu):
// its class's Menu in the tab's answer, each the class it adds and its title.
ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::ibVisualEditorItemPopupMenu(ibVisualEditor* handler, wxWindow* WXUNUSED(parent), ibValueFrame* obj)
	: wxMenu(), m_object(obj), m_formHandler(handler), m_selID(wxNOT_FOUND)
{
	wxMenuItem* item = nullptr;

	for (const ibProtocolNode& described : handler->m_classes.Children()) {
		if (described.GetString(ibProtocolName::Name) != obj->GetClassName())
			continue;
		for (const ibProtocolNode& added : described.FindChild(ibProtocolName::Menu).Children()) {
			const wxString className = added.GetString(ibProtocolName::Name);
			item = Append(MENU_CREATE_OBJECT + static_cast<int>(m_classNames.size()), added.GetString(ibProtocolName::Title));
			for (const ibProtocolNode& made : handler->m_classes.Children()) {
				if (made.GetString(ibProtocolName::Name) != className)
					continue;
				const wxBitmap picture = ibProtocolPicture(made.GetString(ibProtocolName::Picture));
				if (picture.IsOk())
					item->SetBitmap(picture);
				break;
			}
			m_classNames.push_back(className);
		}
		break;
	}
	if (!m_classNames.empty())
		AppendSeparator();

	item = Append(MENU_CUT, _("Cut\tCtrl+X"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_CUT, wxART_MENU));
	item = Append(MENU_COPY, _("Copy\tCtrl+C"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_COPY, wxART_MENU));
	item = Append(MENU_PASTE, _("Paste\tCtrl+V"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PASTE, wxART_MENU));
	AppendSeparator();
	item = Append(MENU_DELETE, _("Delete\tCtrl+D"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_MENU));
	AppendSeparator();
	item = Append(MENU_MOVE_UP, _("Move Up\tAlt+Up"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_MENU));
	item = Append(MENU_MOVE_DOWN, _("Move Down\tAlt+Down"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_MENU));
	AppendSeparator();
	item = Append(MENU_PROPERTIES, _("Properties"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnMenuEvent(wxCommandEvent& event)
{
	m_selID = event.GetId();

	// Done once the menu is gone: a command draws the form anew, and the control's window the menu stands on with it —
	// the control is the selected one then (the click selected it).
	ibVisualEditor* const handler = m_formHandler;

	switch (m_selID)
	{
	case MENU_CUT: handler->CallAfter([handler]() { handler->CutObject(handler->GetSelectedObject()); }); break;
	case MENU_COPY: handler->CallAfter([handler]() { handler->CopyObject(handler->GetSelectedObject()); }); break;
	case MENU_PASTE: handler->CallAfter([handler]() { handler->PasteObject(handler->GetSelectedObject()); }); break;
	case MENU_DELETE: handler->CallAfter([handler]() { handler->RemoveObject(handler->GetSelectedObject()); }); break;
	case MENU_MOVE_UP: handler->CallAfter([handler]() { handler->MovePosition(handler->GetSelectedObject(), false); }); break;
	case MENU_MOVE_DOWN: handler->CallAfter([handler]() { handler->MovePosition(handler->GetSelectedObject(), true); }); break;

	case MENU_PROPERTIES:
		// Explicit "Properties" — select the control (canvas highlight + scroll) then FORCE-open the inspector on it.
		m_formHandler->SelectObject(m_object, true);
		m_formHandler->ShowCurrentElement(true);
		break;

	default:
		if (m_selID >= MENU_CREATE_OBJECT && m_selID < MENU_CREATE_OBJECT + static_cast<int>(m_classNames.size())) {
			const wxString className = m_classNames[m_selID - MENU_CREATE_OBJECT];
			handler->CallAfter([handler, className]() { handler->CreateObject(className); });
		}
		break;
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnUpdateEvent(wxUpdateUIEvent& e)
{
	switch (e.GetId())
	{
	case MENU_CUT:
	case MENU_COPY:
	case MENU_MOVE_UP:
	case MENU_MOVE_DOWN:
	case MENU_MOVE_NEW_BOXSIZER: e.Enable(m_formHandler->CanCopyObject() && m_formHandler->IsEditable()); break;
	case MENU_DELETE: e.Enable(m_formHandler->CanCopyObject() && m_formHandler->IsEditable() &&
		CanDeleteControl(m_formHandler->GetSelectedObject())); break;
	case MENU_PASTE: e.Enable(m_formHandler->CanPasteObject() && m_formHandler->IsEditable()); break;
	default:
		if (e.GetId() >= MENU_CREATE_OBJECT && e.GetId() < MENU_CREATE_OBJECT + static_cast<int>(m_classNames.size()))
			e.Enable(m_formHandler->IsEditable());
		break;
	}
}
