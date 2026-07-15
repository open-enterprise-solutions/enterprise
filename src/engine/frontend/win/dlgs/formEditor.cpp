#include "formEditor.h"
#include "backend/compiler/value.h"
#include "frontend/propertyManager/property/private/propertyRegistry.h"

enum {
	WXOES_PROPERTY_GRID = wxID_HIGHEST + 1001
};

enum {
	MENU_MOVE_UP = wxID_HIGHEST + 2000,
	MENU_MOVE_DOWN
};

#pragma region __command_h__
class ibModifyPropertyCmd : public ibFormEditorCmd {
public:
	ibModifyPropertyCmd(ibValueFrame* object, ibProperty* prop, const wxVariant& newValue)
		: m_object(object), m_property(prop), m_newValue(newValue)
	{
	}
protected:
	virtual void DoExecute() override {
		// Get the ibValueFrame from the event
		m_property->SetValue(m_newValue);
	}
private:
	ibValueFrame* m_object = nullptr;
	ibProperty* m_property;
	wxVariant m_newValue;
};

class ibShiftChildCmd : public ibFormEditorCmd {
public:
	ibShiftChildCmd(ibValueFrame* object, int pos)
		: m_object(object), m_newPos(pos)
	{
	}
protected:
	virtual void DoExecute() override {
		ibValueFrame* parent(m_object->GetParent());
		parent->ChangeChildPosition(m_object,
			parent->GetChildPosition(m_object) + m_newPos);
	}

private:
	ibValueFrame* m_object = nullptr;
	int m_newPos;
};
#pragma endregion 

#define ICON_SIZE 16

void ibDialogFormEditor::CreateTree()
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

	m_treeControl->AssignImageList(m_iconList);
}

void ibDialogFormEditor::AddChildren(ibValueFrame* obj, const wxTreeItemId& parent, bool is_root)
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
		ibDialogFormEditorObjectTreeItemData* item_data = new ibDialogFormEditorObjectTreeItemData(obj);
		if (is_root) {
			new_parent = m_treeControl->AddRoot(wxT(""), wxNOT_FOUND, wxNOT_FOUND, item_data);
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
				new_parent = m_treeControl->InsertItem(parent, pos, wxEmptyString, wxNOT_FOUND, wxNOT_FOUND, item_data);
			}
			else {
				new_parent = m_treeControl->AppendItem(parent, wxEmptyString, wxNOT_FOUND, wxNOT_FOUND, item_data);
			}
		}

		// Add the item to the map
		m_listItem.insert(
			std::map< ibValueFrame*, wxTreeItemId>::value_type(obj, new_parent)
		);

		// Set the image
		int image_idx = GetImageIndex(obj->GetClassName());

		if (image_idx != wxNOT_FOUND) {
			m_treeControl->SetItemImage(new_parent, image_idx);
		}

		// Set the name
		UpdateItem(new_parent, obj);

		// Add the rest of the children
		unsigned int count = obj->GetChildCount();

		for (unsigned int i = 0; i < count; i++) {
			ibValueFrame* child = obj->GetChild(i);
			AddChildren(child, new_parent);
		}
	}
}

wxBEGIN_EVENT_TABLE(ibDialogFormEditor, wxDialog)

EVT_UPDATE_UI(wxID_ANY, ibDialogFormEditor::OnUpdateEvent)

EVT_BUTTON(wxID_OK, ibDialogFormEditor::OnButtonEvent)
EVT_BUTTON(wxID_CANCEL, ibDialogFormEditor::OnButtonEvent)
EVT_BUTTON(wxID_APPLY, ibDialogFormEditor::OnButtonEvent)

EVT_MENU(MENU_MOVE_UP, ibDialogFormEditor::OnMenuEvent)
EVT_MENU(MENU_MOVE_DOWN, ibDialogFormEditor::OnMenuEvent)

EVT_TREE_SEL_CHANGED(wxID_ANY, ibDialogFormEditor::OnSelChanged)
EVT_TREE_ITEM_RIGHT_CLICK(wxID_ANY, ibDialogFormEditor::OnRightClick)
EVT_TREE_BEGIN_DRAG(wxID_ANY, ibDialogFormEditor::OnBeginDrag)
EVT_TREE_END_DRAG(wxID_ANY, ibDialogFormEditor::OnEndDrag)
EVT_TREE_KEY_DOWN(wxID_ANY, ibDialogFormEditor::OnKeyDown)

EVT_PG_CHANGING(WXOES_PROPERTY_GRID, ibDialogFormEditor::OnPropertyGridChanging)
EVT_PG_SELECTED(WXOES_PROPERTY_GRID, ibDialogFormEditor::OnPropertyGridItemSelected)

wxEND_EVENT_TABLE()

#include "frontend/win/theme/luna_toolbarart.h"
#include "frontend/visualView/visualHostClient.h"

ibDialogFormEditor::ibDialogFormEditor(ibValueForm* valueForm) :
	wxDialog(valueForm->GetVisualDocument()->GetDocumentWindow(), wxID_ANY, _("Form editor"), wxDefaultPosition, wxSize(600, 350), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_owner(valueForm), m_selectedControl(nullptr)
{
	wxBoxSizer* commonSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* controlSizer = new wxBoxSizer(wxHORIZONTAL);

	m_mainToolBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT | wxAUI_TB_HORZ_TEXT);
	m_mainToolBar->SetArtProvider(new wxAuiLunaToolBarArt());

	m_toolUpItem = m_mainToolBar->AddTool(MENU_MOVE_UP, _("Up control"), wxArtProvider::GetBitmapBundle(wxASCII_STR(wxART_GO_UP), wxASCII_STR(wxART_TOOLBAR)), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL);
	m_toolDownItem = m_mainToolBar->AddTool(MENU_MOVE_DOWN, _("Down control"), wxArtProvider::GetBitmapBundle(wxASCII_STR(wxART_GO_DOWN), wxASCII_STR(wxART_TOOLBAR)), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL);
	m_mainToolBar->Realize();

	commonSizer->Add(m_mainToolBar, 0, wxEXPAND);

	m_treeControl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxSIMPLE_BORDER | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	controlSizer->Add(m_treeControl, 1, wxALL | wxEXPAND, FromDIP(5));
	m_treeControl->SetDoubleBuffered(true);

	CreateTree();

	m_propertyGridManager = new wxPropertyGridManager(this, WXOES_PROPERTY_GRID, wxDefaultPosition, wxDefaultSize, wxPG_BOLD_MODIFIED | wxPG_SPLITTER_AUTO_CENTER | wxPG_DESCRIPTION | wxPG_TOOLTIPS | wxPGMAN_DEFAULT_STYLE);
	m_propertyGridManager->SetExtraStyle(wxPG_EX_NATIVE_DOUBLE_BUFFERING);

	m_propertyGridPage = m_propertyGridManager->AddPage(_("Default"), wxNullBitmap);

	controlSizer->Add(m_propertyGridManager, 1, wxALL | wxEXPAND, FromDIP(5));
	commonSizer->Add(controlSizer, 1, wxEXPAND, FromDIP(5));

	m_propertyGridManager->SendSizeEvent();

	m_sdbSizer = new wxStdDialogButtonSizer();
	m_sdbSizerOK = new wxButton(this, wxID_OK);
	m_sdbSizer->AddButton(m_sdbSizerOK);
	m_sdbSizerApply = new wxButton(this, wxID_APPLY);
	m_sdbSizer->AddButton(m_sdbSizerApply);
	m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
	m_sdbSizer->AddButton(m_sdbSizerCancel);
	m_sdbSizer->Realize();

	commonSizer->Add(m_sdbSizer, 0, wxALIGN_RIGHT, FromDIP(5));

	RebuildTree();
	SetSizer(commonSizer);
	Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picChangeFormCLSID));
	
	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

void ibDialogFormEditor::OnUpdateEvent(wxUpdateUIEvent& event)
{
	if (event.GetId() == wxID_APPLY)
		event.Enable(m_cmdArray.size() > 0);
}

void ibDialogFormEditor::OnMenuEvent(wxCommandEvent& e)
{
	switch (e.GetId())
	{
	case MENU_MOVE_UP:
		MovePosition(m_selectedControl, false);
		break;
	case MENU_MOVE_DOWN:
		MovePosition(m_selectedControl, true);
		break;
	}

	e.Skip();
}

void ibDialogFormEditor::OnButtonEvent(wxCommandEvent& e)
{
	if (e.GetId() == wxID_OK || e.GetId() == wxID_APPLY) {
		for (auto& cmd : m_cmdArray) {
			cmd->Execute();
		}
		m_owner->UpdateForm();
	}

	if (e.GetId() != wxID_APPLY) m_parent->SetFocus();
	m_cmdArray.clear();
	if (e.GetId() == wxID_APPLY) RebuildTree();
	e.Skip();
}

void ibDialogFormEditor::OnSelChanged(wxTreeEvent& event)
{
	// Make selected items bold
	wxTreeItemId oldId = event.GetOldItem();
	if (oldId.IsOk()) m_treeControl->SetItemBold(oldId, false);

	const wxTreeItemId& id = event.GetItem();
	if (!id.IsOk())
		return;

	m_treeControl->SetItemBold(id);

	wxTreeItemData* item_data = m_treeControl->GetItemData(id);

	m_propertyGridManager->Freeze();

	m_propertyGridPage->Clear();
	m_pgArray.clear();

	if (item_data != nullptr) {

		ibValueFrame* obj(((ibDialogFormEditorObjectTreeItemData*)item_data)->GetObject());
		assert(obj);

		struct ibPropertyConnector {

			static void FillPropertyByFrameValue(wxPropertyGridPage* propertyGridPage, ibValueFrame* obj, std::map<wxPGProperty*, ibProperty*>& pgArray) {
				pgArray.clear(); propertyGridPage->Clear();

				for (auto strProperty : ibValueFrame::GetAllowedUserProperty()) {

					ibProperty* prop = obj->GetProperty(strProperty);
					if (prop == nullptr) {

						ibValueFrame* parent = obj->GetParent();
						if (parent != nullptr && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
							prop = parent->GetProperty(strProperty);
						}

						if (prop == nullptr)
							continue;
					}

					wxPGProperty* pg = ibPropertyRegistry::Create(prop);
					propertyGridPage->Append(pg);
					pg->SetHelpString(prop->GetHelp());
					pg->RefreshChildren();

					pgArray.emplace(pg, prop);
				}

				propertyGridPage->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, (long)1);
			}
		};

		m_selectedControl = obj;
		ibPropertyConnector::FillPropertyByFrameValue(m_propertyGridPage, obj, m_pgArray);
	}

	m_propertyGridManager->Refresh();
	m_propertyGridManager->Update();

	m_propertyGridManager->Thaw();
}

void ibDialogFormEditor::OnRightClick(wxTreeEvent& event)
{
	wxTreeItemId id = event.GetItem();
	wxTreeItemData* item_data = m_treeControl->GetItemData(id);
	if (item_data != nullptr) {
		ibValueFrame* obj(((ibDialogFormEditorObjectTreeItemData*)item_data)->GetObject());
		assert(obj);
		wxMenu* menu = new ibDialogFormEditorItemPopupMenu(this, obj);
		wxPoint pos = event.GetPoint();
		menu->UpdateUI(menu); PopupMenu(menu, pos.x, pos.y);
	}
}

void ibDialogFormEditor::OnBeginDrag(wxTreeEvent& event)
{
	// need to explicitly allow drag
	if (event.GetItem() == m_treeControl->GetRootItem())
		return;

	m_draggedItem = event.GetItem();
	event.Allow();
}

void ibDialogFormEditor::OnEndDrag(wxTreeEvent& event)
{
	wxTreeItemId itemSrc = m_draggedItem, itemDst = event.GetItem();
	m_draggedItem = (wxTreeItemId)0l;

	// ensure that itemDst is not itemSrc or a child of itemSrc
	wxTreeItemId item = itemDst;
	while (item.IsOk()) {
		if (item == itemSrc)
			return;
		item = m_treeControl->GetItemParent(item);
	}

	ibValueFrame* objSrc = GetObjectFromTreeItem(itemSrc);
	if (!objSrc)
		return;

	ibValueFrame* objDst = GetObjectFromTreeItem(itemDst);
	if (!objDst)
		return;

	if (objSrc->GetParent() != objDst->GetParent())
		return;

	const wxTreeItemId& parent_item = m_treeControl->GetItemParent(itemDst);

	int pos1 = 0, pos2 = 0; int total_in_group = 0;

	wxTreeItemIdValue coockie;
	wxTreeItemId next_item = m_treeControl->GetFirstChild(parent_item, coockie);

	do {
		if (next_item == itemDst) pos1 = total_in_group;
		else if (next_item == itemSrc) pos2 = total_in_group;
		next_item = m_treeControl->GetNextChild(next_item, coockie);
		total_in_group++;
	} while (next_item.IsOk());

	MovePosition(objSrc, pos1 > pos2,
		pos1 > pos2 ? pos1 - pos2 : pos2 - pos1);
}

void ibDialogFormEditor::OnKeyDown(wxTreeEvent& event)
{
	event.Skip();
}

void ibDialogFormEditor::OnPropertyGridChanging(wxPropertyGridEvent& event)
{
	auto it = m_pgArray.find(event.GetProperty());
	if (it != m_pgArray.end()) {
		ExecuteCommand(
			new ibModifyPropertyCmd(m_selectedControl, it->second, event.GetValue())
		);
	}

	event.Skip();
}

void ibDialogFormEditor::OnPropertyGridItemSelected(wxPropertyGridEvent& event)
{
	wxPGProperty* propPtr = event.GetProperty();

	// Update displayed description for the new selection
	const wxString& helpString = propPtr->GetHelpString();
	const wxString& localized = wxGetTranslation(helpString);

	m_propertyGridManager->SetPropertyHelpString(propPtr, localized);
	m_propertyGridManager->SetDescription(propPtr->GetLabel(), localized);

	event.Skip();
}

void ibDialogFormEditor::MovePosition(ibValueFrame* move_obj, bool right, unsigned int num)
{
	auto it = m_listItem.find(move_obj);
	if (it != m_listItem.end() && num > 0) {

		const wxTreeItemId& parent_item = m_treeControl->GetItemParent(it->second);

		wxTreeItemIdValue coockie;
		wxTreeItemId next_item = m_treeControl->GetFirstChild(parent_item, coockie);

		int index = 0, total_in_group = 0;

		do {
			if (!right && next_item == it->second && index == 0) { index = total_in_group; }
			next_item = m_treeControl->GetNextChild(next_item, coockie); total_in_group++;
			if (right && next_item == it->second && index == 0) { index = total_in_group; }
		} while (next_item.IsOk());

		index = right ?
			index + num + 1 : index - num;

		const wxTreeItemId& inserted_item = m_treeControl->InsertItem(parent_item,
			index > total_in_group ? 0 : index,
			m_treeControl->GetItemText(it->second),
			m_treeControl->GetItemImage(it->second),
			m_treeControl->GetItemImage(it->second),
			m_treeControl->GetItemData(it->second)
		);

		struct ibTreeMove {

			static void Swap(ibDialogFormEditor* formEditor, const wxTreeItemId& dst, const wxTreeItemId& src) {

				wxTreeItemIdValue coockie; wxTreeItemId next_item = formEditor->m_treeControl->GetFirstChild(dst, coockie);
				while (next_item.IsOk()) {

					wxTreeItemId inserted_item = formEditor->m_treeControl->AppendItem(src,
						formEditor->m_treeControl->GetItemText(next_item),
						formEditor->m_treeControl->GetItemImage(next_item),
						formEditor->m_treeControl->GetItemImage(next_item),
						formEditor->m_treeControl->GetItemData(next_item)
					);

					if (formEditor->m_treeControl->HasChildren(next_item))
						Swap(formEditor, next_item, inserted_item);

					formEditor->m_listItem[formEditor->GetObjectFromTreeItem(inserted_item)] = inserted_item;
					formEditor->m_treeControl->SetItemData(next_item, nullptr);

					next_item = formEditor->m_treeControl->GetNextChild(next_item, coockie);
				}
			}
		};

		ibTreeMove::Swap(this, it->second, inserted_item);

		m_treeControl->SetItemData(it->second, nullptr);
		m_treeControl->DeleteChildren(it->second);
		m_treeControl->Delete(it->second);

		ibValueFrame* parent = move_obj->GetParent();

		if (parent != nullptr) {

			// If the object is contained within an item, the item must be
			// shifted
			ibValueFrame* object = move_obj;

			while (parent && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
				object = parent;
				parent = object->GetParent();
			}

			ExecuteCommand(new ibShiftChildCmd(object, (right ? num : -1 * num)));
		}

		m_listItem[move_obj] = inserted_item;
		m_treeControl->SelectItem(inserted_item);
	}
}

wxBEGIN_EVENT_TABLE(ibDialogFormEditor::ibDialogFormEditorItemPopupMenu, wxMenu)
EVT_MENU(wxID_ANY, ibDialogFormEditor::ibDialogFormEditorItemPopupMenu::OnMenuEvent)
EVT_UPDATE_UI(wxID_ANY, ibDialogFormEditor::ibDialogFormEditorItemPopupMenu::OnUpdateEvent)
wxEND_EVENT_TABLE()

ibDialogFormEditor::ibDialogFormEditorItemPopupMenu::ibDialogFormEditorItemPopupMenu(ibDialogFormEditor* parent, ibValueFrame* obj)
	: wxMenu(), m_handler(parent), m_object(obj)
{
	Append(MENU_MOVE_UP, wxT("Move Up\tAlt+Up"))->SetBitmap(wxArtProvider::GetBitmapBundle(wxASCII_STR(wxART_GO_UP), wxASCII_STR(wxART_MENU)));
	Append(MENU_MOVE_DOWN, wxT("Move Down\tAlt+Down"))->SetBitmap(wxArtProvider::GetBitmapBundle(wxASCII_STR(wxART_GO_DOWN), wxASCII_STR(wxART_MENU)));
}

void ibDialogFormEditor::ibDialogFormEditorItemPopupMenu::OnMenuEvent(wxCommandEvent& event)
{
	m_selID = event.GetId();

	switch (m_selID)
	{
	case MENU_MOVE_UP: m_handler->MovePosition(m_object, false); break;
	case MENU_MOVE_DOWN: m_handler->MovePosition(m_object, true); break;
	}
}

void ibDialogFormEditor::ibDialogFormEditorItemPopupMenu::OnUpdateEvent(wxUpdateUIEvent& e)
{
	ibValueFrame* currentControl = m_handler->GetSelectedObject();

	switch (e.GetId())
	{
	case MENU_MOVE_UP:
	case MENU_MOVE_DOWN: e.Enable(true); break;
	}
}