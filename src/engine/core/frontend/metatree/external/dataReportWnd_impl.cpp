////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "dataReportWnd.h"
#include "frontend/objinspect/objinspect.h"
#include "common/docManager.h"
#include "appData.h"

#define	objectFormsName _("forms")
#define	objectModulesName _("modules")
#define	objectTemplatesName _("templates")
#define objectAttributesName _("attributes")
#define objectTablesName _("tables")
#define objectEnumerationsName _("enums")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

void CDataReportTree::ActivateItem(const wxTreeItemId& item)
{
	IMetaObject* m_currObject = GetMetaObject(item);

	if (!m_currObject)
		return;

	OpenFormMDI(m_currObject);
}

IMetaObject* CDataReportTree::CreateItem(bool showValue)
{
	wxTreeItemId selectedItem =
		m_metaTreeWnd->GetSelection(), parentItem = selectedItem;

	if (!selectedItem.IsOk())
		return NULL;

	ITreeClsidData* itemData = NULL;

	while (parentItem != NULL) {
		itemData = dynamic_cast<ITreeClsidData*>(m_metaTreeWnd->GetItemData(parentItem));
		if (itemData != NULL) {
			selectedItem = parentItem;
			break;
		}
		parentItem = m_metaTreeWnd->GetItemParent(parentItem);
	}

	if (itemData == NULL)
		return NULL;

	IMetaObject* metaParent = NULL;

	while (parentItem != NULL) {
		metaParent = GetMetaObject(parentItem);
		if (metaParent != NULL) {
			break;
		}
		parentItem = m_metaTreeWnd->GetItemParent(parentItem);
	}

	wxASSERT(metaParent);

	IMetaObject* newObject = m_metaData->CreateMetaObject(itemData->GetClassID(), metaParent);

	if (newObject == NULL)
		return NULL;

	wxTreeItemId createdItem = NULL;
	if (itemData->GetClassID() == g_metaTableCLSID) {
		createdItem = AppendGroupItem(selectedItem, g_metaAttributeCLSID, newObject);
	}
	else {
		createdItem = AppendItem(selectedItem, newObject);
	}

	if (showValue)
		OpenFormMDI(newObject);

	UpdateToolbar(newObject, createdItem);

	m_metaTreeWnd->InvalidateBestSize();
	m_metaTreeWnd->SelectItem(createdItem);
	m_metaTreeWnd->Expand(createdItem);

	objectInspector->SelectObject(newObject, m_metaTreeWnd->GetEventHandler());
	return newObject;
}

void CDataReportTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeWnd->GetSelection();

	if (!selection.IsOk())
		return;

	IMetaObject* m_currObject = GetMetaObject(selection);

	if (!m_currObject)
		return;

	OpenFormMDI(m_currObject);
}

void CDataReportTree::RemoveItem()
{
	wxTreeItemId selection = m_metaTreeWnd->GetSelection();

	if (!selection.IsOk())
		return;

	wxTreeItemIdValue m_cookie;
	wxTreeItemId hItem = m_metaTreeWnd->GetFirstChild(selection, m_cookie);

	while (hItem)
	{
		EraseItem(hItem);
		hItem = m_metaTreeWnd->GetNextChild(hItem, m_cookie);
	}

	IMetaObject* metaObject = GetMetaObject(selection);
	wxASSERT(metaObject);
	EraseItem(selection);
	m_metaData->RemoveMetaObject(metaObject);

	//Delete item from tree
	m_metaTreeWnd->Delete(selection);

	const wxTreeItemId nextSelection = m_metaTreeWnd->GetFocusedItem();

	if (nextSelection.IsOk()) {
		UpdateToolbar(GetMetaObject(nextSelection), nextSelection);
	}

	//update choice if need
	UpdateChoiceSelection();
}

void CDataReportTree::EraseItem(const wxTreeItemId& item)
{
	IMetaObject* metaObject = GetMetaObject(item);

	if (metaObject)
	{
		auto itFounded = std::find_if(m_metaOpenedForms.begin(), m_metaOpenedForms.end(), [metaObject](CDocument* currDoc) { return metaObject == currDoc->GetMetaObject(); });

		if (itFounded != m_metaOpenedForms.end())
		{
			CDocument* m_foundedDoc = *itFounded;
			m_metaOpenedForms.erase(itFounded);
			m_foundedDoc->DeleteAllViews();
		}
	}
}

void CDataReportTree::PropertyItem()
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;
	wxTreeItemId sel = m_metaTreeWnd->GetSelection();
	objectInspector->ClearProperty();

	IMetaObject* metaObject = GetMetaObject(sel);

	UpdateToolbar(metaObject, sel);

	if (!metaObject)
		return;

	objectInspector->SelectObject(metaObject, m_metaTreeWnd->GetEventHandler());
}

void CDataReportTree::UpItem()
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;

	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	const wxTreeItemId& nextItem = m_metaTreeWnd->GetPrevSibling(selection);
	IMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != NULL && nextItem.IsOk()) {
		const wxTreeItemId& parentItem = m_metaTreeWnd->GetItemParent(nextItem);
		wxTreeItemIdValue coockie; wxTreeItemId nextId = m_metaTreeWnd->GetFirstChild(parentItem, coockie);
		size_t pos = 0;
		do {
			if (nextId == nextItem)
				break;
			nextId = m_metaTreeWnd->GetNextChild(nextId, coockie); pos++;
		} while (nextId.IsOk());
		IMetaObject* parentObject = metaObject->GetParent();
		IMetaObject* nextObject = GetMetaObject(nextItem);
		if (parentObject->ChangeChildPosition(metaObject, parentObject->GetChildPosition(nextObject))) {
			wxTreeItemId newId = m_metaTreeWnd->InsertItem(parentItem,
				pos + 2,
				m_metaTreeWnd->GetItemText(nextItem),
				m_metaTreeWnd->GetItemImage(nextItem),
				m_metaTreeWnd->GetItemImage(nextItem),
				m_metaTreeWnd->GetItemData(nextItem)
			);

			auto tree = m_metaTreeWnd;
			std::function<void(CDataReportTreeWnd*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](CDataReportTreeWnd* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
				wxTreeItemIdValue coockie; wxTreeItemId nextId = tree->GetFirstChild(dst, coockie);
				while (nextId.IsOk()) {
					wxTreeItemId newId = tree->AppendItem(src,
						tree->GetItemText(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemData(nextId)
					);
					if (tree->HasChildren(nextId)) {
						swap(tree, nextId, newId);
					}
					tree->SetItemData(nextId, NULL);
					nextId = tree->GetNextChild(nextId, coockie);
				}
			};

			swap(tree, nextItem, newId);

			m_metaTreeWnd->SetItemData(nextItem, NULL);
			m_metaTreeWnd->Delete(nextItem);

			m_metaTreeWnd->Expand(newId);
		}
	}
}

void CDataReportTree::DownItem()
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;

	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	const wxTreeItemId& prevItem = m_metaTreeWnd->GetNextSibling(selection);
	IMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != NULL && prevItem.IsOk()) {
		const wxTreeItemId& parentItem = m_metaTreeWnd->GetItemParent(prevItem);
		wxTreeItemIdValue coockie; wxTreeItemId nextId = m_metaTreeWnd->GetFirstChild(parentItem, coockie);
		size_t pos = 0;
		do {
			if (nextId == prevItem)
				break;
			nextId = m_metaTreeWnd->GetNextChild(nextId, coockie); pos++;
		} while (nextId.IsOk());
		IMetaObject* parentObject = metaObject->GetParent();
		IMetaObject* prevObject = GetMetaObject(prevItem);
		if (parentObject->ChangeChildPosition(metaObject, parentObject->GetChildPosition(prevObject))) {
			wxTreeItemId newId = m_metaTreeWnd->InsertItem(parentItem,
				pos - 1,
				m_metaTreeWnd->GetItemText(prevItem),
				m_metaTreeWnd->GetItemImage(prevItem),
				m_metaTreeWnd->GetItemImage(prevItem),
				m_metaTreeWnd->GetItemData(prevItem)
			);

			auto tree = m_metaTreeWnd;
			std::function<void(CDataReportTreeWnd*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](CDataReportTreeWnd* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
				wxTreeItemIdValue coockie; wxTreeItemId nextId = tree->GetFirstChild(dst, coockie);
				while (nextId.IsOk()) {
					wxTreeItemId newId = tree->AppendItem(src,
						tree->GetItemText(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemData(nextId)
					);
					if (tree->HasChildren(nextId)) {
						swap(tree, nextId, newId);
					}
					tree->SetItemData(nextId, NULL);
					nextId = tree->GetNextChild(nextId, coockie);
				}
			};

			swap(tree, prevItem, newId);

			m_metaTreeWnd->SetItemData(prevItem, NULL);
			m_metaTreeWnd->Delete(prevItem);

			m_metaTreeWnd->Expand(newId);
		}
	}
}

void CDataReportTree::SortItem()
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;
	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	IMetaObject* prevObject = GetMetaObject(selection);
	if (prevObject != NULL && selection.IsOk()) {
		const wxTreeItemId& parentItem =
			m_metaTreeWnd->GetItemParent(selection);
		if (parentItem.IsOk()) {
			m_metaTreeWnd->SortChildren(parentItem);
		}
	}
}

void CDataReportTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;
	wxTreeItemId sel = m_metaTreeWnd->GetSelection();
	IMetaObject* metaObject = GetMetaObject(sel);
	if (!metaObject)
		return;
	metaObject->ProcessCommand(id);
}

#include <wx/artprov.h>

void CDataReportTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
{
	IMetaObject* metaObject = GetMetaObject(item);

	if (metaObject
		&& !metaObject->PrepareContextMenu(defaultMenu))
	{
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
		menuItem->Enable(!m_bReadOnly);
		menuItem = defaultMenu->Append(ID_METATREE_EDIT, _("Edit"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU));
		menuItem = defaultMenu->Append(ID_METATREE_REMOVE, _("Remove"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU));
		menuItem->Enable(!m_bReadOnly);
		defaultMenu->AppendSeparator();
		menuItem = defaultMenu->Append(ID_METATREE_PROPERTY, _("Properties"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_MENU));
	}
	else if (!metaObject) {
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
		menuItem->Enable(!m_bReadOnly);
	}
}

void CDataReportTree::UpdateToolbar(IMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != NULL && item != m_metaTreeWnd->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_REMOVE, obj != NULL && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != NULL && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != NULL && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != NULL && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

void CDataReportTree::UpdateChoiceSelection()
{
	m_defaultFormValue->Clear();
	m_defaultFormValue->AppendString(_("<not selected>"));

	CMetaObjectReport* commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);

	int defSelection = 0;

	for (auto metaForm : commonMetadata->GetObjectForms())
	{
		if (CMetaObjectReport::eFormReport != metaForm->GetTypeForm())
			continue;

		int selection_id = m_defaultFormValue->Append(metaForm->GetName(), reinterpret_cast<void*>(metaForm->GetMetaID()));

		if (commonMetadata->GetDefFormObject() == metaForm->GetMetaID()) {
			defSelection = selection_id;
		}
	}

	m_defaultFormValue->SetSelection(defSelection);
	m_defaultFormValue->SendSelectionChangedEvent(wxEVT_CHOICE);
}

bool CDataReportTree::RenameMetaObject(IMetaObject* obj, const wxString& sNewName)
{
	wxTreeItemId curItem = m_metaTreeWnd->GetSelection();

	if (!curItem.IsOk())
		return false;

	if (m_metaData->RenameMetaObject(obj, sNewName)) {
		CDocument* currDocument = GetDocument(obj);
		if (currDocument != NULL) {
			currDocument->SetTitle(obj->GetClassName() + wxT(": ") + sNewName);
			currDocument->OnChangeFilename(true);
		}
		//update choice if need
		UpdateChoiceSelection();

		m_metaTreeWnd->SetItemText(curItem, sNewName);
		return true;
	}

	return false;
}

void CDataReportTree::InitTree()
{
	m_treeREPORTS = AppendRootItem(g_metaReportCLSID, _("reports"));
	//Список аттрибутов 
	m_treeATTRIBUTES = AppendGroupItem(m_treeREPORTS, g_metaAttributeCLSID, objectAttributesName);
	//список табличных частей 
	m_treeTABLES = AppendGroupItem(m_treeREPORTS, g_metaTableCLSID, objectTablesName);
	//Формы
	m_treeFORM = AppendGroupItem(m_treeREPORTS, g_metaFormCLSID, objectFormsName);
	//Таблицы
	m_treeTEMPLATES = AppendGroupItem(m_treeREPORTS, g_metaTemplateCLSID, objectTablesName);
}

void CDataReportTree::ClearTree()
{
	for (auto doc : m_metaOpenedForms) {
		doc->DeleteAllViews();
	}

	m_metaOpenedForms.clear();

	//delete all child item
	if (m_treeATTRIBUTES.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeATTRIBUTES);
	if (m_treeTABLES.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeTABLES);
	if (m_treeFORM.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeFORM);
	if (m_treeTEMPLATES.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeTEMPLATES);

	//delete all items
	m_metaTreeWnd->DeleteAllItems();

	//Initialize tree
	InitTree();
}

void CDataReportTree::FillData()
{
	CMetaObjectReport* commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);
	m_metaTreeWnd->SetItemText(m_treeREPORTS, commonMetadata->GetName());
	m_metaTreeWnd->SetItemData(m_treeREPORTS, new wxTreeItemMetaData(commonMetadata));

	//set value data
	m_nameValue->SetValue(commonMetadata->GetName());
	m_synonymValue->SetValue(commonMetadata->GetSynonym());
	m_commentValue->SetValue(commonMetadata->GetComment());

	//set default form value 
	m_defaultFormValue->Clear();
	//append default value 
	m_defaultFormValue->AppendString(_("<not selected>"));

	//Список аттрибутов 
	for (auto metaAttribute : commonMetadata->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeATTRIBUTES, metaAttribute);
	}

	//Список табличных частей 
	for (auto metaTable : commonMetadata->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendGroupItem(m_treeTABLES, g_metaAttributeCLSID, metaTable);
		for (auto metaAttribute : metaTable->GetObjectAttributes()) {
			if (metaAttribute->IsDeleted())
				continue;
			if (metaAttribute->DefaultAttribute())
				continue;
			wxTreeItemId hItemNew = AppendItem(hItem, metaAttribute);
		}
	}

	//Формы
	for (auto metaForm : commonMetadata->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeFORM, metaForm);
	}

	//Таблицы
	for (auto metaTemplates : commonMetadata->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeTEMPLATES, metaTemplates);
	}

	//update choice selection
	UpdateChoiceSelection();

	//set init flag
	m_initialize = true;

	//set modify 
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(NULL, m_treeATTRIBUTES);
}

bool CDataReportTree::Load(CMetadataReport* metaData)
{
	ClearTree();
	m_metaData = metaData;
	m_metaTreeWnd->Freeze();
	FillData(); //Fill all data from metadata
	m_metaData->SetMetaTree(this);
	m_metaTreeWnd->SelectItem(m_treeATTRIBUTES);
	m_metaTreeWnd->ExpandAll();
	m_metaTreeWnd->Thaw();
	return true;
}

bool CDataReportTree::Save()
{
	CMetaObjectReport* m_commonMetadata = m_metaData->GetReport();
	wxASSERT(m_commonMetadata);

	m_commonMetadata->SetName(m_nameValue->GetValue());
	m_commonMetadata->SetSynonym(m_synonymValue->GetValue());
	m_commonMetadata->SetComment(m_commentValue->GetValue());

	wxASSERT(m_metaData);

	if (m_metaData->IsModified())
		return m_metaData->SaveMetadata();

	return false;
}