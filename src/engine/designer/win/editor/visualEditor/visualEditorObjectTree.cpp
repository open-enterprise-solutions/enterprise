////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "backend/propertyManager/propertyManager.h"
#include "frontend/visualView/layers/commandBar.h"      // ibValueCommandBar (command-interface node)
#include "frontend/mainFrame/objinspect/objinspect.h"   // objectInspector

#include <wx/imaglist.h>

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::RebuildTree()
{
	wxWindow::Freeze();

	Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
	Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

	ibValueFrame* valueForm =
		m_formHandler->GetValueForm();

	// Clear the old tree and map
	m_tcObjects->DeleteAllItems();
	m_listItem.clear();
	m_commandItemMap.clear();

	if (valueForm != nullptr) {
		wxTreeItemId dummy;
		AddChildren(valueForm, dummy, true);
		// Expand items that were previously expanded
		RestoreItemStatus(valueForm);
	}

	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));
	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange));

	wxWindow::Thaw();

	wxWindow::Refresh();
	wxWindow::Update();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::AddChildren(ibValueFrame* obj, const wxTreeItemId& parent, bool is_root)
{
	if (obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (obj->GetChildCount() > 0) {
			AddChildren(obj->GetChild(0), parent);
		}
		else {
			// If we reached here it is because the tree is malformed
			// and how it was built needs to be reviewed.
			wxString msg;
			ibValueFrame* itemParent = obj->GetParent();
			assert(parent);

			msg = wxString::Format(wxT("Item without object as child of \'%s:%s\'"),
				itemParent->GetControlName().c_str(),
				itemParent->GetClassName().c_str());

			wxLogError(msg);
		}
	}
	else {
		wxTreeItemId new_parent;
		ibVisualEditorObjectTreeItemData* item_data = new ibVisualEditorObjectTreeItemData(obj);
		if (is_root) {
			new_parent = m_tcObjects->AddRoot(wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
		}
		else {
			unsigned int pos = 0;

			ibValueFrame* parent_obj = obj->GetParent();

			// find a proper position where the added object should be displayed at
			if (parent_obj && parent_obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
				parent_obj = parent_obj->GetParent();
				if (parent_obj) {
					pos = parent_obj->GetChildPosition(obj->GetParent());
				}
			}
			else if (parent_obj) {
				pos = parent_obj->GetChildPosition(obj);
			}

			// insert tree item to proper position
			if (pos > 0) {
				new_parent = m_tcObjects->InsertItem(parent, pos, wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
			}
			else {
				new_parent = m_tcObjects->AppendItem(parent, wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
			}
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

		// Command interface (a chrome LAYER the frame carries): a static sub-node
		// added FIRST, so it sits above the content like the rendered toolbar.
		// Selecting it edits the bar's settings (AutoFill). Icon = the toolbar's.
		if (obj->HasCommandBar()) {
			ibValueCommandBar* cbar = obj->GetCommandBar();
			wxTreeItemId cmdNode = m_tcObjects->AppendItem(new_parent, _("Command interface"),
				GetImageIndex(wxT("Toolbar")), wxNOT_FOUND,
				new ibVisualEditorObjectTreeItemData(cbar));
			// Its child COMMANDS (runtime items) as sub-nodes — remembered in m_commandItemMap
			// so a just-added / moved command can be re-selected after the rebuild.
			for (unsigned int c = 0; c < cbar->GetCommandItemCount(); c++) {
				ibValueCommandBarItem* citem = cbar->GetCommandItem(c);
				if (citem != nullptr) {
					wxTreeItemId itemNode = m_tcObjects->AppendItem(cmdNode, citem->GetName(),
						GetImageIndex(wxT("Tool")), wxNOT_FOUND,
						new ibVisualEditorObjectTreeItemData(citem));
					m_commandItemMap[citem] = itemNode;
				}
			}
			// Restore the node's open-state (kept on the bar, like a control's expand flag).
			if (cbar->GetCommandItemCount() > 0 && cbar->IsTreeExpanded())
				m_tcObjects->Expand(cmdNode);
		}

		// Add the rest of the children
		unsigned int count = obj->GetChildCount();

		for (unsigned int i = 0; i < count; i++) {
			ibValueFrame* child = obj->GetChild(i);
			AddChildren(child, new_parent);
		}
	}
}

#define ICON_SIZE 16

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::CreateTree()
{
	if (m_iconList != nullptr)
		delete m_iconList;

	m_iconList = new wxImageList(ICON_SIZE, ICON_SIZE);
	
	for (auto objClass : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
		const wxIcon& controlIcon = objClass->GetClassIcon();
		if (controlIcon.IsOk()) {
			const int retIndex = m_iconList->Add(controlIcon);
			if (retIndex != wxNOT_FOUND) {
				m_iconIdx.insert(
					std::map<wxString, int>::value_type(objClass->GetClassName(), retIndex)
				);
			}
		}
	}

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

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectCommandItem(ibValueCommandBarItem* citem)
{
	auto it = m_commandItemMap.find(citem);
	if (it == m_commandItemMap.end() || !it->second.IsOk())
		return;
	m_tcObjects->EnsureVisible(it->second);   // expands ancestor nodes so the item shows
	m_tcObjects->SelectItem(it->second);      // drives OnSelChanged -> inspector shows its props
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::AddItem(ibValueFrame* item, ibValueFrame* parent)
{
	if (item && parent) {
		// find parent item displayed in the object tree
		while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			parent = parent->GetParent();
		}

		// add new item to the object tree
		std::map< ibValueFrame*, wxTreeItemId>::iterator it = m_listItem.find(parent);
		if ((it != m_listItem.end()) && it->second.IsOk()) {
			AddChildren(item, it->second, false);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// Enterprise ibEvent Handlers
/////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEditorLoaded()
{
	RebuildTree();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEditorRefresh()
{
	RebuildTree();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnObjectCreated(ibValueFrame* obj)
{
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
	else {
		wxLogError(wxT("There is no tree item associated with this object.\n\tClass: %s\n\tName: %s"), obj->GetClassName().c_str(), obj->GetControlName().c_str());
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

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnObjectRemoved(ibValueFrame* obj)
{
	RemoveItem(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnPropertyModified(ibProperty* prop)
{
	std::map< ibValueFrame*, wxTreeItemId>::iterator it =
		m_listItem.find((ibValueFrame*)prop->GetPropertyObject());
	if (it != m_listItem.end()) {
		UpdateItem(it->second, it->first);
	}
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
	m_tcObjects = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_NO_LINES | wxSIMPLE_BORDER | wxTR_TWIST_BUTTONS);

	wxBoxSizer* sizerMain = new wxBoxSizer(wxVERTICAL);
	sizerMain->Add(m_tcObjects, 1, wxEXPAND, 0);
	wxPanel::SetAutoLayout(true);
	wxPanel::SetSizer(sizerMain);
	sizerMain->Fit(this);
	sizerMain->SetSizeHints(this);

	m_tcObjects->SetDoubleBuffered(true);

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

	m_altKeyIsDown = false;
	m_notifySelecting = false;

	CreateTree();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnKeyDown(wxTreeEvent& event)
{
	if (event.GetKeyEvent().AltDown() && event.GetKeyCode() != WXK_ALT)
	{
#ifdef __WXGTK__
		switch (event.GetKeyCode())
		case WXK_UP:
		{
			m_formHandler->MovePosition(GetObjectFromTreeItem(m_tcObjects->GetSelection()), false);
			return;
		case WXK_DOWN:
			m_formHandler->MovePosition(GetObjectFromTreeItem(m_tcObjects->GetSelection()), true);
			return;
		case WXK_RIGHT:
			m_formHandler->MoveHierarchy(GetObjectFromTreeItem(m_tcObjects->GetSelection()), false);
			return;
		case WXK_LEFT:
			m_formHandler->MoveHierarchy(GetObjectFromTreeItem(m_tcObjects->GetSelection()), true);
			return;
		}
#endif
		event.Skip();
	}
	else
	{
		event.Skip();
	}
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

// Decode a tree item's payload and select it — one place for the click-reselect and the
// selection-changed handlers. A layer node (bar/command) goes to the inspector; a control node
// goes to the visual editor (which also scrolls to it).
void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectItemData(wxTreeItemData* item_data)
{
	if (item_data == nullptr)
		return;
	auto* data = (ibVisualEditorObjectTreeItemData*)item_data;
	if (ibValueLayerObject* layer = data->GetLayerObject()) {
		if (!objectInspector->IsShownInspector()) objectInspector->ShowInspector();
		objectInspector->SelectObject(layer, true);
		return;
	}
	ibValueFrame* obj(data->GetObject());
	assert(obj);
	m_formHandler->SelectObject(obj);
	m_formHandler->ScrollToObject(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnRightClick(wxTreeEvent& event)
{
	wxTreeItemId id = event.GetItem();
	wxTreeItemData* item_data = m_tcObjects->GetItemData(id);
	if (item_data != nullptr) {
		auto* data = (ibVisualEditorObjectTreeItemData*)item_data;
		if (ibValueLayerObject* layer = data->GetLayerObject()) {
			// Layer node (bar OR command) — the object fills its own menu and runs the choice;
			// the tree never branches per kind. Routing goes through the owner frame's editor.
			wxMenu menu;
			layer->PrepareDefaultMenu(&menu);
			const int sel = GetPopupMenuSelectionFromUser(menu, event.GetPoint());
			ibValueFrame* owner = layer->GetOwnerFrame();
			ibFrontendVisualEditorNotebook* editor = owner != nullptr ? owner->FindVisualEditor() : nullptr;
			if (sel != wxID_NONE && editor != nullptr)
				layer->ExecuteMenu(editor, sel);
			return;
		}
		ibValueFrame* obj(data->GetObject());
		assert(obj);
		m_formHandler->SelectObject(obj);
		wxMenu* menu = new ibVisualEditorItemPopupMenu(m_formHandler, this, obj);
		wxPoint pos = event.GetPoint();
		menu->UpdateUI(menu); PopupMenu(menu, pos.x, pos.y);
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnBeginDrag(wxTreeEvent& event)
{
	// need to explicitly allow drag
	if (event.GetItem() == m_tcObjects->GetRootItem())
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

	// set object to clipboard
	if (copy) {
		m_formHandler->CopyObject(objSrc);
	}
	else {
		m_formHandler->CutObject(objSrc, true);
	}

	if (!copy && !m_formHandler->PasteObject(objDst)) {
		m_formHandler->Undo();
	}
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
};

wxBEGIN_EVENT_TABLE(ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu, wxMenu)
EVT_MENU(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnMenuEvent)
EVT_UPDATE_UI(wxID_ANY, ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnUpdateEvent)
wxEND_EVENT_TABLE()

#include "frontend/artProvider/artProvider.h"

bool ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::HasDeleteObject() {
	return m_selID == MENU_DELETE;
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::ibVisualEditorItemPopupMenu(ibVisualEditor* handler, wxWindow* parent, ibValueFrame* obj)
	: wxMenu(), m_object(obj), m_formHandler(handler)
{
	obj->PrepareDefaultMenu(this);

	wxMenuItem* item = nullptr;
	
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

	switch (m_selID)
	{
	case MENU_CUT: m_formHandler->CutObject(m_formHandler->GetSelectedObject()); break;
	case MENU_COPY: m_formHandler->CopyObject(m_formHandler->GetSelectedObject()); break;
	case MENU_PASTE: m_formHandler->PasteObject(m_formHandler->GetSelectedObject()); break;
	case MENU_DELETE: m_formHandler->RemoveObject(m_formHandler->GetSelectedObject()); break;
	case MENU_MOVE_UP: m_formHandler->MovePosition(m_object, false); break;
	case MENU_MOVE_DOWN: m_formHandler->MovePosition(m_object, true); break;

	case MENU_PROPERTIES:
		if (!objectInspector->IsShownInspector()) objectInspector->ShowInspector();
		m_formHandler->SelectObject(m_object, true);
		break;

	default: { m_object->ExecuteMenu(m_formHandler->GetVisualEditor(), m_selID); }
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnUpdateEvent(wxUpdateUIEvent& e)
{
	ibValueFrame* currentControl = m_formHandler->GetSelectedObject();

	switch (e.GetId())
	{
	case MENU_CUT:
	case MENU_COPY:
	case MENU_MOVE_UP:
	case MENU_MOVE_DOWN:
	case MENU_MOVE_NEW_BOXSIZER: e.Enable(m_formHandler->CanCopyObject() && m_formHandler->IsEditable()); break;
	case MENU_DELETE: e.Enable(m_formHandler->CanCopyObject() && m_formHandler->IsEditable() && currentControl->CanDeleteControl()); break;
	case MENU_PASTE: e.Enable(m_formHandler->CanPasteObject() && m_formHandler->IsEditable()); break;
	}
}