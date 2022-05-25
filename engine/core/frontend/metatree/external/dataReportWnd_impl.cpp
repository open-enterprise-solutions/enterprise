////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "dataReportWnd.h"
#include "frontend/objinspect/objinspect.h"
#include "common/docManager.h"
#include "appData.h"

#define SETITEMTYPE(hItem,xnType,xnChildImage)\
{\
CObjectData m_dataObj;\
m_dataObj.m_clsid=xnType;\
m_dataObj.m_nChildImage=xnChildImage;\
m_aMetaClassObj[hItem]=m_dataObj;\
}

#define	objectFormsName _("forms")
#define	objectModulesName _("modules")
#define	objectTemplatesName _("templates")
#define objectAttributesName _("attributes")
#define objectTablesName _("tables")
#define objectEnumerationsName _("enums")

#define	ICON_ATTRIBUTEGROUP	219
#define	ICON_ATTRIBUTE	219
#define	ICON_OBJECTGROUP 268//134
#define	ICON_OBJECT		270//134
#define	ICON_METADATA	209
#define	ICON_MAKETGROUP	318
#define	ICON_MAKET		79
#define	ICON_FORMGROUP	293
#define	ICON_FORM		294
#define	ICON_MODULEGROUP 317
#define	ICON_MODULE		308
#define	ICON_RUNMODULE	322
#define	ICON_CONFMODULE	241
#define	ICON_INITMODULE	309

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

void CDataReportTree::ActivateItem(const wxTreeItemId &item)
{
	IMetaObject *m_currObject = GetMetaObject(item);

	if (!m_currObject)
		return;

	OpenFormMDI(m_currObject);
}

void CDataReportTree::CreateItem()
{
	IMetaObject *m_metaParent = NULL;

	wxTreeItemId hSelItem = m_metaTreeWnd->GetSelection();
	wxTreeItemId hParentItem = hSelItem;

	if (!hSelItem.IsOk())
		return;

	CObjectData m_objData; bool m_dataFounded = false;

	while (hParentItem)
	{
		auto foundedIt = m_aMetaClassObj.find(hParentItem);

		if (foundedIt != m_aMetaClassObj.end())
		{
			hSelItem = foundedIt->first; m_objData = foundedIt->second;
			m_dataFounded = true;
			break;
		}

		hParentItem = m_metaTreeWnd->GetItemParent(hParentItem);
	}

	if (!m_dataFounded)
		return;

	while (hParentItem)
	{
		auto foundedIt = m_aMetaObj.find(hParentItem);

		if (foundedIt != m_aMetaObj.end())
		{
			m_metaParent = foundedIt->second;
			break;
		}

		hParentItem = m_metaTreeWnd->GetItemParent(hParentItem);
	}

	wxASSERT(m_metaParent);
	IMetaObject *m_newObject = m_metaData->CreateMetaObject(m_objData.m_clsid, m_metaParent);
	if (!m_newObject) {
		return;
	}
	wxTreeItemId hNewItem = m_metaTreeWnd->AppendItem(hSelItem, m_newObject->GetName(), m_objData.m_nChildImage, m_objData.m_nChildImage);
	m_aMetaObj.insert_or_assign(hNewItem, m_newObject);

	if (m_objData.m_clsid == g_metaTableCLSID)
		SETITEMTYPE(hNewItem, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	OpenFormMDI(m_newObject);

	//update choice if need
	UpdateChoiceSelection();

	//update toolbar
	UpdateToolbar(m_newObject, hNewItem);

	m_metaTreeWnd->InvalidateBestSize();
	m_metaTreeWnd->SelectItem(hNewItem);
	m_metaTreeWnd->Expand(hNewItem);

	objectInspector->SelectObject(m_newObject, m_metaTreeWnd->GetEventHandler());
}

void CDataReportTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeWnd->GetSelection();

	if (!selection.IsOk())
		return;

	IMetaObject *m_currObject = GetMetaObject(selection);

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

	IMetaObject *metaObject = GetMetaObject(selection);
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

void CDataReportTree::EraseItem(const wxTreeItemId &item)
{
	IMetaObject *metaObject = GetMetaObject(item);

	if (metaObject)
	{
		auto itFounded = std::find_if(m_aMetaOpenedForms.begin(), m_aMetaOpenedForms.end(), [metaObject](CDocument *currDoc) { return metaObject == currDoc->GetMetaObject(); });

		if (itFounded != m_aMetaOpenedForms.end())
		{
			CDocument *m_foundedDoc = *itFounded;
			m_aMetaOpenedForms.erase(itFounded);
			m_foundedDoc->DeleteAllViews();
		}
	}

	m_aMetaClassObj.erase(item);
	m_aMetaObj.erase(item);
}

void CDataReportTree::PropertyItem()
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;
	wxTreeItemId sel = m_metaTreeWnd->GetSelection();
	objectInspector->ClearProperty();

	IMetaObject *metaObject = GetMetaObject(sel);

	UpdateToolbar(metaObject, sel);

	if (!metaObject)
		return;

	objectInspector->SelectObject(metaObject, m_metaTreeWnd->GetEventHandler());
}

void CDataReportTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;
	wxTreeItemId sel = m_metaTreeWnd->GetSelection();
	IMetaObject *metaObject = GetMetaObject(sel);
	if (!metaObject)
		return;
	metaObject->ProcessCommand(id);
}

void CDataReportTree::PrepareContextMenu(wxMenu *defultMenu, const wxTreeItemId &item)
{
	IMetaObject *metaObject = GetMetaObject(item);

	if (metaObject
		&& !metaObject->PrepareContextMenu(defultMenu))
	{
		wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_NEW, _("new"));
		m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_NEW));
		m_menuItem->Enable(!m_bReadOnly);
		m_menuItem = defultMenu->Append(ID_METATREE_EDIT, _("edit"));
		m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT));
		m_menuItem = defultMenu->Append(ID_METATREE_REMOVE, _("remove"));
		m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_CUT));
		m_menuItem->Enable(!m_bReadOnly);
		defultMenu->AppendSeparator();
		m_menuItem = defultMenu->Append(ID_METATREE_PROPERTY, _("property"));
	}
	else if (!metaObject)
	{
		wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_NEW, _("new"));
		m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_NEW));
		m_menuItem->Enable(!m_bReadOnly);
	}
}

void CDataReportTree::UpdateToolbar(IMetaObject *obj, const wxTreeItemId &item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != NULL && item != m_metaTreeWnd->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_REMOVE, obj != NULL && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

void CDataReportTree::UpdateChoiceSelection()
{
	m_defaultFormValue->Clear();
	m_defaultFormValue->AppendString(_("<not selected>"));

	CMetaObjectReport *commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);

	int defSelection = 0;

	for (auto metaForm : commonMetadata->GetObjectForms())
	{
		if (CMetaObjectReport::eFormReport != metaForm->GetTypeForm())
			continue;

		int selection_id = m_defaultFormValue->Append(metaForm->GetName(), reinterpret_cast<void *>(metaForm->GetMetaID()));

		if (commonMetadata->m_defaultFormObject == metaForm->GetMetaID()) {
			defSelection = selection_id;
		}
	}

	m_defaultFormValue->SetSelection(defSelection);
	m_defaultFormValue->SendSelectionChangedEvent(wxEVT_CHOICE);
}

void CDataReportTree::OnCloseDocument(CDocument *doc)
{
	auto itFounded = std::find(m_aMetaOpenedForms.begin(), m_aMetaOpenedForms.end(), doc);

	if (itFounded != m_aMetaOpenedForms.end())
		m_aMetaOpenedForms.erase(itFounded);
}

bool CDataReportTree::OpenFormMDI(IMetaObject *obj)
{
	CDocument *foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc == NULL)
	{
		foundedDoc = docManager->OpenFormMDI(obj, m_docParent, m_bReadOnly ? wxDOC_READONLY : wxDOC_NEW);

		//Значит, подходящего шаблона не было! 
		if (foundedDoc)
		{
			m_aMetaOpenedForms.push_back(foundedDoc); foundedDoc->Activate();
			return true;
		}
	}
	else
	{
		foundedDoc->Activate();
		return true;
	}

	return false;
}

bool CDataReportTree::OpenFormMDI(IMetaObject *obj, CDocument *&foundedDoc)
{
	foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc == NULL)
	{
		foundedDoc = docManager->OpenFormMDI(obj, m_docParent, m_bReadOnly ? wxDOC_READONLY : wxDOC_NEW);

		//Значит, подходящего шаблона не было! 
		if (foundedDoc)
		{
			m_aMetaOpenedForms.push_back(foundedDoc); foundedDoc->Activate();
			return true;
		}
	}
	else
	{
		foundedDoc->Activate();
		return true;
	}

	return false;
}

bool CDataReportTree::CloseFormMDI(IMetaObject *obj)
{
	CDocument *foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc != NULL) {

		objectInspector->SelectObject(obj, this);

		if (foundedDoc->Close()) {
			// Delete the child document by deleting all its views.
			return foundedDoc->DeleteAllViews();
		}
	}

	return false;
}

CDocument *CDataReportTree::GetDocument(IMetaObject *obj)
{
	CDocument *foundedDoc = NULL;

	auto itFounded = std::find_if(m_aMetaOpenedForms.begin(), m_aMetaOpenedForms.end(), [obj](CDocument *currDoc) {return obj == currDoc->GetMetaObject(); });

	if (itFounded != m_aMetaOpenedForms.end())
		foundedDoc = *itFounded;

	return foundedDoc;
}

bool CDataReportTree::RenameMetaObject(IMetaObject *obj, const wxString &sNewName)
{
	wxTreeItemId curItem = m_metaTreeWnd->GetSelection();

	if (!curItem.IsOk())
		return false;

	if (m_metaData->RenameMetaObject(obj, sNewName))
	{
		CDocument *currDocument = GetDocument(obj);

		if (currDocument)
		{
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

#include "common/codeproc.h"

void CDataReportTree::EditModule(const wxString &fullName, int lineNumber, bool setRunLine)
{
	IMetaObject *metaObject = m_metaData->FindByName(fullName);

	if (!metaObject)
		return;

	CDocument *m_foundedDoc = NULL;

	if (!OpenFormMDI(metaObject, m_foundedDoc))
		return;

	ICodeInfo *m_codeInfo = dynamic_cast<ICodeInfo *>(m_foundedDoc);

	if (m_codeInfo)
		m_codeInfo->SetCurrentLine(lineNumber, setRunLine);
}

void CDataReportTree::InitTree()
{
	m_treeREPORTS = m_metaTreeWnd->AddRoot(wxT("dataProcessor"), 225, 225, 0);
	SETITEMTYPE(m_treeREPORTS, g_metaDataProcessorCLSID, ICON_METADATA);

	//Список аттрибутов 
	m_treeATTRIBUTES = m_metaTreeWnd->AppendItem(m_treeREPORTS, objectAttributesName, ICON_ATTRIBUTEGROUP, ICON_ATTRIBUTEGROUP);
	SETITEMTYPE(m_treeATTRIBUTES, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	//список табличных частей 
	m_treeTABLES = m_metaTreeWnd->AppendItem(m_treeREPORTS, objectTablesName, 217, 217);
	SETITEMTYPE(m_treeTABLES, g_metaTableCLSID, 218);

	//Формы
	m_treeFORM = m_metaTreeWnd->AppendItem(m_treeREPORTS, objectFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(m_treeFORM, g_metaFormCLSID, ICON_FORM);

	//Таблицы
	m_treeTEMPLATES = m_metaTreeWnd->AppendItem(m_treeREPORTS, objectTablesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(m_treeTEMPLATES, g_metaTemplateCLSID, ICON_MAKET);
}

void CDataReportTree::ClearTree()
{
	for (auto doc : m_aMetaOpenedForms) {
		doc->DeleteAllViews();
	}

	m_aMetaObj.clear();
	m_aMetaClassObj.clear();

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
	CMetaObjectReport *commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);
	m_metaTreeWnd->SetItemText(m_treeREPORTS, commonMetadata->GetName());

	//set parent object
	m_aMetaObj.insert_or_assign(m_treeREPORTS, commonMetadata);

	//Список аттрибутов 
	for (auto metaAttribute : commonMetadata->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;

		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeATTRIBUTES, metaAttribute->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaAttribute);
	}

	//Список табличных частей 
	for (auto metaTable : commonMetadata->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeTABLES, metaTable->GetName(), 218, 218);
		SETITEMTYPE(hItem, g_metaAttributeCLSID, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaTable);

		for (auto metaAttribute : metaTable->GetObjectAttributes()) {
			if (metaAttribute->IsDeleted())
				continue;
			if (metaAttribute->DefaultAttribute())
				continue;

			wxTreeItemId hItemNew = m_metaTreeWnd->AppendItem(hItem, metaAttribute->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
			m_aMetaObj.insert_or_assign(hItemNew, metaAttribute);
		}
	}

	//Формы
	for (auto metaForm : commonMetadata->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeFORM, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//Таблицы
	for (auto metaTemplates : commonMetadata->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeTEMPLATES, metaTemplates->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplates);
	}

	//set value data
	m_nameValue->SetValue(commonMetadata->GetName());
	m_synonymValue->SetValue(commonMetadata->GetSynonym());
	m_commentValue->SetValue(commonMetadata->GetComment());

	//set default form value 
	m_defaultFormValue->Clear();

	//append default value 
	m_defaultFormValue->AppendString("<not selected>");

	//update choice selection
	UpdateChoiceSelection();

	//set init flag
	m_initialize = true;

	//set modify 
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(NULL, m_treeATTRIBUTES);
}

bool CDataReportTree::Load(CMetadataReport *metaData)
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
	CMetaObjectReport *m_commonMetadata = m_metaData->GetReport();
	wxASSERT(m_commonMetadata);

	m_commonMetadata->SetName(m_nameValue->GetValue());
	m_commonMetadata->SetSynonym(m_synonymValue->GetValue());
	m_commonMetadata->SetComment(m_commentValue->GetValue());

	wxASSERT(m_metaData);

	if (m_metaData->IsModified())
		return m_metaData->SaveMetadata();

	return false;
}