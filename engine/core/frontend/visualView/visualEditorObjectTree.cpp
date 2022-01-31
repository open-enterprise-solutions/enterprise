////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"

#include "common/objectbase.h"
#include "frontend/objinspect/events.h"

#include <wx/imaglist.h>

BEGIN_EVENT_TABLE(CVisualEditorContextForm::CVisualEditorObjectTree, wxPanel)
EVT_TREE_SEL_CHANGED(wxID_ANY, CVisualEditorContextForm::CVisualEditorObjectTree::OnSelChanged)
EVT_TREE_ITEM_RIGHT_CLICK(wxID_ANY, CVisualEditorContextForm::CVisualEditorObjectTree::OnRightClick)
EVT_TREE_BEGIN_DRAG(wxID_ANY, CVisualEditorContextForm::CVisualEditorObjectTree::OnBeginDrag)
EVT_TREE_END_DRAG(wxID_ANY, CVisualEditorContextForm::CVisualEditorObjectTree::OnEndDrag)
EVT_TREE_KEY_DOWN(wxID_ANY, CVisualEditorContextForm::CVisualEditorObjectTree::OnKeyDown)

EVT_PROJECT_LOADED(CVisualEditorContextForm::CVisualEditorObjectTree::OnProjectLoaded)
EVT_PROJECT_SAVED(CVisualEditorContextForm::CVisualEditorObjectTree::OnProjectSaved)
EVT_OBJECT_CREATED(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectCreated)
EVT_OBJECT_REMOVED(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectRemoved)
EVT_PROPERTY_MODIFIED(CVisualEditorContextForm::CVisualEditorObjectTree::OnPropertyModified)
EVT_PROJECT_REFRESH(CVisualEditorContextForm::CVisualEditorObjectTree::OnProjectRefresh)

END_EVENT_TABLE()

CVisualEditorContextForm::CVisualEditorObjectTree::CVisualEditorObjectTree(CVisualEditorContextForm *handler, wxWindow *parent, int id) :
	wxPanel(parent, id),
	m_formHandler(handler)
{
	m_formHandler->AddHandler(this->GetEventHandler());
	m_tcObjects = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_DEFAULT_STYLE | wxSIMPLE_BORDER);

	wxBoxSizer* sizer_1 = new wxBoxSizer(wxVERTICAL);
	sizer_1->Add(m_tcObjects, 1, wxEXPAND, 0);
	SetAutoLayout(true);
	SetSizer(sizer_1);
	sizer_1->Fit(this);
	sizer_1->SetSizeHints(this);

	Connect(wxID_ANY, wxEVT_OBJECT_EXPANDED, wxFrameObjectEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectExpanded));
	Connect(wxID_ANY, wxEVT_OBJECT_SELECTED, wxFrameObjectEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectSelected));
	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));

	m_altKeyIsDown = false;
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnKeyDown(wxTreeEvent &event)
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


CVisualEditorContextForm::CVisualEditorObjectTree::~CVisualEditorObjectTree()
{
	m_formHandler->RemoveHandler(this->GetEventHandler());
}

IValueFrame *CVisualEditorContextForm::CVisualEditorObjectTree::GetObjectFromTreeItem(const wxTreeItemId &item)
{
	if (item.IsOk())
	{
		wxTreeItemData* item_data = m_tcObjects->GetItemData(item);
		if (item_data) {
			IValueFrame *obj(((CVisualEditorObjectTreeItemData*)item_data)->GetObject());
			return obj;
		}
	}

	return NULL;
}

void CVisualEditorContextForm::CVisualEditorObjectTree::RebuildTree()
{
	m_tcObjects->Freeze();

	Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
	Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));

	IValueFrame *valueForm =
		m_formHandler->GetSelectedForm();

	// Clear the old tree and map
	m_tcObjects->DeleteAllItems();
	m_aItems.clear();

	if (valueForm) {
		wxTreeItemId dummy;
		AddChildren(valueForm, dummy, true);
		// Expand items that were previously expanded
		RestoreItemStatus(valueForm);
	}

	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
	Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));

	m_tcObjects->Thaw();
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnSelChanged(wxTreeEvent &event)
{
	wxTreeItemId id = event.GetItem();
	if (!id.IsOk()) return;

	// Make selected items bold
	wxTreeItemId oldId = event.GetOldItem();
	if (oldId.IsOk())
	{
		m_tcObjects->SetItemBold(oldId, false);
	}
	m_tcObjects->SetItemBold(id);

	wxTreeItemData *item_data = m_tcObjects->GetItemData(id);

	if (item_data)
	{
		IValueFrame * obj(((CVisualEditorObjectTreeItemData *)item_data)->GetObject());
		assert(obj);
		Disconnect(wxID_ANY, wxEVT_OBJECT_SELECTED, wxFrameObjectEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectSelected));
		m_formHandler->SelectObject(obj);
		Connect(wxID_ANY, wxEVT_OBJECT_SELECTED, wxFrameObjectEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectSelected));
	}
}

#include "frontend/objinspect/objinspect.h"

void CVisualEditorContextForm::CVisualEditorObjectTree::OnRightClick(wxTreeEvent &event)
{
	wxTreeItemId id = event.GetItem();
	wxTreeItemData *item_data = m_tcObjects->GetItemData(id);
	if (item_data)
	{
		IValueFrame *obj(((CVisualEditorObjectTreeItemData *)item_data)->GetObject());
		assert(obj);

		m_formHandler->SelectObject(obj);

		wxMenu * menu = new CVisualEditorItemPopupMenu(m_formHandler, this, obj);
		wxPoint pos = event.GetPoint();
		menu->UpdateUI(menu); PopupMenu(menu, pos.x, pos.y);
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnBeginDrag(wxTreeEvent &event)
{
	// need to explicitly allow drag
	if (event.GetItem() == m_tcObjects->GetRootItem())
		return;

	m_draggedItem = event.GetItem();
	event.Allow();
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnEndDrag(wxTreeEvent& event)
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

	IValueFrame *objSrc = 
		GetObjectFromTreeItem(itemSrc);
	if (!objSrc)
		return;

	IValueFrame *objDst = 
		GetObjectFromTreeItem(itemDst);
	if (!objDst)
		return;

	// backup clipboard
	IValueFrame *clipboard =
		m_formHandler->GetClipboardObject();

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

	m_formHandler->SetClipboardObject(clipboard);
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange(wxTreeEvent &event)
{
	wxTreeItemId id = event.GetItem();
	wxTreeItemData *item_data = m_tcObjects->GetItemData(id);

	if (item_data) {
		IValueFrame * obj(((CVisualEditorObjectTreeItemData *)item_data)->GetObject());
		assert(obj);

		Disconnect(wxID_ANY, wxEVT_OBJECT_EXPANDED, wxFrameObjectEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectExpanded));
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));

		m_formHandler->ExpandObject(obj, m_tcObjects->IsExpanded(id));

		Connect(wxID_ANY, wxEVT_OBJECT_EXPANDED, wxFrameObjectEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectExpanded));
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::AddChildren(IValueFrame *obj, const wxTreeItemId &parent, bool is_root)
{
	if (obj->IsItem())
	{
		if (obj->GetChildCount() > 0)
		{
			AddChildren(obj->GetChild(0), parent);
		}
		else
		{
			// Si hemos llegado aquí ha sido porque el arbol no está bien formado
			// y habrá que revisar cómo se ha creado.
			wxString msg;
			IValueFrame *itemParent = obj->GetParent();
			assert(parent);

			msg = wxString::Format(wxT("Item without object as child of \'%s:%s\'"),
				itemParent->GetPropertyAsString(wxT("name")).c_str(),
				itemParent->GetClassName().c_str());

			wxLogError(msg);
		}
	}
	else
	{
		wxTreeItemId new_parent;
		CVisualEditorObjectTreeItemData *item_data = new CVisualEditorObjectTreeItemData(obj);

		if (is_root)
		{
			new_parent = m_tcObjects->AddRoot(wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
		}
		else
		{
			unsigned int pos = 0;

			IValueFrame *parent_obj = obj->GetParent();

			// find a proper position where the added object should be displayed at
			if (parent_obj && parent_obj->IsItem())
			{
				parent_obj = parent_obj->GetParent();

				if (parent_obj)
				{
					pos = parent_obj->GetChildPosition(obj->GetParent());
				}
			}
			else if (parent_obj)
			{
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
		m_aItems.insert(
			std::map< IValueFrame *, wxTreeItemId>::value_type(obj, new_parent)
		);

		// Set the image
		int image_idx = GetImageIndex(obj->GetClassName());

		if (image_idx != wxNOT_FOUND) {
			m_tcObjects->SetItemImage(new_parent, image_idx);
		}

		// Set the name
		UpdateItem(new_parent, obj);

		// Add the rest of the children
		unsigned int count = obj->GetChildCount();

		for (unsigned int i = 0; i < count; i++)
		{
			IValueFrame *child = obj->GetChild(i);
			AddChildren(child, new_parent);
		}
	}
}

#include "utils/stringUtils.h"

int CVisualEditorContextForm::CVisualEditorObjectTree::GetImageIndex(const wxString &name)
{
	int index = wxNOT_FOUND; //default icon

	std::map<wxString, int>::iterator it = m_iconIdx.find(name);
	
	if (it != m_iconIdx.end()) {
		index = it->second;
	}

	return index;
}

void CVisualEditorContextForm::CVisualEditorObjectTree::UpdateItem(wxTreeItemId id, IValueFrame *obj)
{
	// mostramos el nombre
	wxString class_name(obj->GetClassName());
	Property* prop = obj->GetProperty(wxT("name"));

	wxString obj_name;
	if (prop) obj_name = prop->GetValue();
	wxString text = obj_name + wxT(" : ") + class_name;

	// actualizamos el item
	m_tcObjects->SetItemText(id, text);
}

#define ICON_SIZE 22

void CVisualEditorContextForm::CVisualEditorObjectTree::Create()
{
	m_iconList = new wxImageList(ICON_SIZE, ICON_SIZE);

	for (auto objClassName :
		CValue::GetAvailableObjects(eObjectType::eObjectType_object_control))
	{
		IControlValueAbstract *controlDesc = dynamic_cast<IControlValueAbstract *>(
			CValue::GetAvailableObject(objClassName)
		);
		wxASSERT(controlDesc);
		wxBitmap controlImage = 
			controlDesc->GetControlImage();
		if (controlImage.IsOk()) {
			wxIcon iconBMP;
			iconBMP.CopyFromBitmap(controlImage); /*!!!*/
			int retIndex = m_iconList->Add(iconBMP);
			if (retIndex != wxNOT_FOUND) {
				m_iconIdx.insert(
					std::map<wxString, int>::value_type(objClassName, retIndex)
				);
			}
		}
	}

	m_tcObjects->AssignImageList(m_iconList);
}

void CVisualEditorContextForm::CVisualEditorObjectTree::RestoreItemStatus(IValueFrame *obj)
{
	std::map< IValueFrame *, wxTreeItemId>::iterator item_it = m_aItems.find(obj);
	if (item_it != m_aItems.end()) {
		wxTreeItemId id = item_it->second;

		if (obj->GetExpanded()) {
			m_tcObjects->Expand(id);
		}
	}

	unsigned int i, count =
		obj->GetChildCount();

	for (i = 0; i < count; i++) {
		RestoreItemStatus(obj->GetChild(i));
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::AddItem(IValueFrame *item, IValueFrame *parent)
{
	if (item && parent) {
		// find parent item displayed in the object tree
		while (parent && parent->IsItem()) {
			parent = parent->GetParent();
		}

		// add new item to the object tree
		std::map< IValueFrame *, wxTreeItemId>::iterator it = m_aItems.find(parent);
		if ((it != m_aItems.end()) && it->second.IsOk()) {
			AddChildren(item, it->second, false);
		}
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::RemoveItem(IValueFrame *item)
{
	// remove affected object tree items only
	std::map< IValueFrame *, wxTreeItemId>::iterator it = m_aItems.find(item);
	if ((it != m_aItems.end()) && it->second.IsOk())
	{
		m_tcObjects->Delete(it->second);
		// clear map records for all item's children
		ClearMap(it->first);
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::ClearMap(IValueFrame *obj)
{
	m_aItems.erase(obj);

	for (unsigned int i = 0; i < obj->GetChildCount(); i++) {
		ClearMap(obj->GetChild(i));
	}
}

/////////////////////////////////////////////////////////////////////////////
// Enterprise Event Handlers
/////////////////////////////////////////////////////////////////////////////
void CVisualEditorContextForm::CVisualEditorObjectTree::OnProjectLoaded(wxFrameEvent &)
{
	RebuildTree();
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnProjectSaved(wxFrameEvent &)
{
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectExpanded(wxFrameObjectEvent& event)
{
	IValueFrame * obj = event.GetFrameObject();
	std::map< IValueFrame *, wxTreeItemId>::iterator it = m_aItems.find(obj);
	if (it != m_aItems.end())
	{
		if (m_tcObjects->IsExpanded(it->second) != obj->GetExpanded())
		{
			if (obj->GetExpanded())
			{
				m_tcObjects->Expand(it->second);
			}
			else
			{
				m_tcObjects->Collapse(it->second);
			}
		}
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectSelected(wxFrameObjectEvent &event)
{
	IValueFrame *obj = event.GetFrameObject();

	// Find the tree item associated with the object and select it
	std::map< IValueFrame *, wxTreeItemId>::iterator it = m_aItems.find(obj);
	if (it != m_aItems.end())
	{
		// Ignore expand/collapse events
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
		Disconnect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));

		m_tcObjects->EnsureVisible(it->second);
		m_tcObjects->SelectItem(it->second);

		// Restore event handling
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_EXPANDED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
		Connect(wxID_ANY, wxEVT_COMMAND_TREE_ITEM_COLLAPSED, wxTreeEventHandler(CVisualEditorContextForm::CVisualEditorObjectTree::OnExpansionChange));
	}
	else
	{
		wxLogError(wxT("There is no tree item associated with this object.\n\tClass: %s\n\tName: %s"), obj->GetClassName().c_str(), obj->GetPropertyAsString(wxT("name")).c_str());
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectCreated(wxFrameObjectEvent &event)
{
	//if (event.GetFrameObject()) AddItem(event.GetFrameObject(), event.GetFrameObject()->GetParent());
	RebuildTree();
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnObjectRemoved(wxFrameObjectEvent &event)
{
	RemoveItem(event.GetFrameObject());
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnPropertyModified(wxFramePropertyEvent &event)
{
	Property* prop =
		event.GetFrameProperty();

	if (prop->GetType() == PropertyType::PT_WXNAME) {
		std::map< IValueFrame *, wxTreeItemId>::iterator it = m_aItems.find((IValueFrame *)prop->GetObject());
		if (it != m_aItems.end()) {
			UpdateItem(it->second, it->first);
		}
	}
}

void CVisualEditorContextForm::CVisualEditorObjectTree::OnProjectRefresh(wxFrameEvent &)
{
	RebuildTree();
}

///////////////////////////////////////////////////////////////////////////////

CVisualEditorContextForm::CVisualEditorObjectTreeItemData::CVisualEditorObjectTreeItemData(IValueFrame * obj) : m_object(obj) {}

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
};


wxBEGIN_EVENT_TABLE(CVisualEditorContextForm::CVisualEditorItemPopupMenu, wxMenu)
EVT_MENU(wxID_ANY, CVisualEditorContextForm::CVisualEditorItemPopupMenu::OnMenuEvent)
EVT_UPDATE_UI(wxID_ANY, CVisualEditorContextForm::CVisualEditorItemPopupMenu::OnUpdateEvent)
wxEND_EVENT_TABLE()

bool CVisualEditorContextForm::CVisualEditorItemPopupMenu::HasDeleteObject() { return m_selID == MENU_DELETE; }

CVisualEditorContextForm::CVisualEditorItemPopupMenu::CVisualEditorItemPopupMenu(CVisualEditorContextForm *handler, wxWindow *parent, IValueFrame *obj)
	: wxMenu(), m_object(obj), m_formHandler(handler)
{
	obj->PrepareDefaultMenu(this);

	Append(MENU_CUT, wxT("Cut\tCtrl+X"));
	Append(MENU_COPY, wxT("Copy\tCtrl+C"));
	Append(MENU_PASTE, wxT("Paste\tCtrl+V"));
	AppendSeparator();
	Append(MENU_DELETE, wxT("Delete\tCtrl+D"));
	AppendSeparator();
	Append(MENU_MOVE_UP, wxT("Move Up\tAlt+Up"));
	Append(MENU_MOVE_DOWN, wxT("Move Down\tAlt+Down"));
}

void CVisualEditorContextForm::CVisualEditorItemPopupMenu::OnMenuEvent(wxCommandEvent & event)
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

	default: { m_object->ExecuteMenu(m_formHandler->GetVisualEditor(), m_selID); }
	}
}

void CVisualEditorContextForm::CVisualEditorItemPopupMenu::OnUpdateEvent(wxUpdateUIEvent& e)
{
	IValueFrame *currentControl = m_formHandler->GetSelectedObject();

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