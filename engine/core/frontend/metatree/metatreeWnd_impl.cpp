////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metatree window
////////////////////////////////////////////////////////////////////////////

#include "metatreeWnd.h"
#include "frontend/objinspect/objinspect.h"
#include "common/reportManager.h"
#include "appData.h"

#define SETITEMTYPE(hItem,xnType,xnChildImage)\
{\
CObjectData m_dataObj;\
m_dataObj.m_clsid=xnType;\
m_dataObj.m_nChildImage=xnChildImage;\
m_aMetaClassObj[hItem]=m_dataObj;\
}

#define metadataName _("metadata")
#define commonName _("common")

#define commonModulesName _("common modules")
#define commonFormsName _("common forms")
#define commonTemplatesName _("common templates")

#define constantsName _("constants")

#define catalogsName _("catalogs")
#define documentsName _("documents")
#define enumerationsName _("enumerations")
#define dataProcessorName _("data processors")
#define reportsName _("reports")

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
#define	ICON_MODULE		309
#define	ICON_RUNMODULE	322
#define	ICON_CONFMODULE	241
#define	ICON_COMMON		377

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

void CMetadataTree::ActivateItem(const wxTreeItemId &item)
{
	IMetaObject *m_currObject = GetMetaObject(item);

	if (!m_currObject)
		return;

	OpenFormMDI(m_currObject);
}

void CMetadataTree::CreateItem()
{
	IMetaObject *metaParent = NULL;

	wxTreeItemId hSelItem = m_metaTreeWnd->GetSelection();
	wxTreeItemId hParentItem = hSelItem;

	if (!hSelItem.IsOk())
		return;

	CObjectData objData; bool m_dataFounded = false;

	while (hParentItem) {
		auto foundedIt = m_aMetaClassObj.find(hParentItem);
		if (foundedIt != m_aMetaClassObj.end()) {
			hSelItem = foundedIt->first; objData = foundedIt->second;
			m_dataFounded = true;
			break;
		}
		hParentItem = m_metaTreeWnd->GetItemParent(hParentItem);
	}

	if (!m_dataFounded)
		return;

	while (hParentItem) {
		auto foundedIt = m_aMetaObj.find(hParentItem);
		if (foundedIt != m_aMetaObj.end())
		{
			metaParent = foundedIt->second;
			break;
		}
		hParentItem = m_metaTreeWnd->GetItemParent(hParentItem);
	}

	wxASSERT(metaParent);
	IMetaObject *newObject = m_metaData->CreateMetaObject(objData.m_clsid, metaParent);
	if (!newObject) {
		return;
	}
	wxTreeItemId hNewItem = m_metaTreeWnd->AppendItem(hSelItem, newObject->GetName(), objData.m_nChildImage, objData.m_nChildImage);
	m_aMetaObj.insert_or_assign(hNewItem, newObject);

	//Advanced mode
	if (objData.m_clsid == g_metaCatalogCLSID)
		AddCatalogItem(newObject, hNewItem);
	else if (objData.m_clsid == g_metaDocumentCLSID)
		AddDocumentItem(newObject, hNewItem);
	else if (objData.m_clsid == g_metaEnumerationCLSID)
		AddEnumerationItem(newObject, hNewItem);
	else if (objData.m_clsid == g_metaDataProcessorCLSID)
		AddDataProcessorItem(newObject, hNewItem);
	else if (objData.m_clsid == g_metaReportCLSID)
		AddReportItem(newObject, hNewItem);

	if (objData.m_clsid == g_metaTableCLSID)
		SETITEMTYPE(hNewItem, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	OpenFormMDI(newObject);

	UpdateToolbar(newObject, hNewItem);

	m_metaTreeWnd->InvalidateBestSize();
	m_metaTreeWnd->SelectItem(hNewItem);
	m_metaTreeWnd->Expand(hNewItem);

	objectInspector->SelectObject(newObject, m_metaTreeWnd->GetEventHandler());
}

void CMetadataTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeWnd->GetSelection();
	if (!selection.IsOk())
		return;
	IMetaObject *m_currObject = GetMetaObject(selection);
	if (!m_currObject)
		return;
	OpenFormMDI(m_currObject);
}

void CMetadataTree::RemoveItem()
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
}

void CMetadataTree::EraseItem(const wxTreeItemId &item)
{
	IMetaObject *metaObject = GetMetaObject(item);

	if (metaObject)
	{
		auto itFounded = std::find_if(m_aMetaOpenedForms.begin(), m_aMetaOpenedForms.end(),
			[metaObject](CDocument *currDoc) {
			return metaObject == currDoc->GetMetaObject();
		});

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

void CMetadataTree::PropertyItem()
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;

	wxTreeItemId sel = m_metaTreeWnd->GetSelection();
	IMetaObject *metaObject = GetMetaObject(sel);

	objectInspector->ClearProperty();
	UpdateToolbar(metaObject, sel);

	if (!metaObject)
		return;

	objectInspector->SelectObject(metaObject, m_metaTreeWnd->GetEventHandler());
}

#include "metadata/external/metadataDataProcessor.h"
#include "metadata/external/metadataReport.h"

void CMetadataTree::InsertItem()
{
	IMetaObject *commonMetaObject = m_metaData->GetCommonMetaObject(); wxTreeItemId hSelItem = m_metaTreeWnd->GetSelection();

	if (hSelItem == m_treeDATAPROCESSORS) {
		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			"data processor files (*.dpr)|*.dpr", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		wxString classType = CValue::GetNameObjectFromID(g_metaDataProcessorCLSID);
		wxASSERT(classType.Length() > 0);
		CMetaObjectDataProcessorValue *newDataProcessor = CValue::CreateAndConvertObjectRef<CMetaObjectDataProcessorValue>(classType);
		wxASSERT(newDataProcessor);
		newDataProcessor->SetClsid(g_metaDataProcessorCLSID);

		//always create metabject
		bool bSuccess = newDataProcessor->OnCreateMetaObject(m_metaData);

		if (!bSuccess) {
			newDataProcessor->SetParent(NULL);
			wxDELETE(newDataProcessor);
			return;
		}

		if (commonMetaObject) {
			newDataProcessor->SetParent(commonMetaObject);
			commonMetaObject->AddChild(newDataProcessor);
		}

		CMetadataDataProcessor metadataDataProcessor(newDataProcessor);

		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {

			if (!m_metaData->RenameMetaObject(newDataProcessor, newDataProcessor->GetName())) {
				newDataProcessor->SetName(newDataProcessor->GetName() + wxT("1"));
			}

			wxTreeItemId hNewItem = m_metaTreeWnd->AppendItem(hSelItem, newDataProcessor->GetName(), 598, 598);
			AddDataProcessorItem(newDataProcessor, hNewItem);
			m_aMetaObj.insert_or_assign(hNewItem, newDataProcessor);

			if (commonMetaObject)
				commonMetaObject->AppendChild(newDataProcessor);

			newDataProcessor->ReadProperty();
			newDataProcessor->IncrRef();
		}
		else {
			newDataProcessor->SetParent(NULL);
			wxDELETE(newDataProcessor);
		}
	}
	else {
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			"report files (*.rpt)|*.rpt", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		wxString classType = CValue::GetNameObjectFromID(g_metaReportCLSID);
		wxASSERT(classType.Length() > 0);
		CMetaObjectReportValue *newDataProcessor = CValue::CreateAndConvertObjectRef<CMetaObjectReportValue>(classType);
		wxASSERT(newDataProcessor);
		newDataProcessor->SetClsid(g_metaReportCLSID);

		//always create metabject
		bool bSuccess = newDataProcessor->OnCreateMetaObject(m_metaData);

		if (!bSuccess) {
			newDataProcessor->SetParent(NULL);
			wxDELETE(newDataProcessor);
			return;
		}

		if (commonMetaObject) {
			newDataProcessor->SetParent(commonMetaObject);
			commonMetaObject->AddChild(newDataProcessor);
		}

		CMetadataReport metadataDataProcessor(newDataProcessor);

		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {

			if (!m_metaData->RenameMetaObject(newDataProcessor, newDataProcessor->GetName())) {
				newDataProcessor->SetName(newDataProcessor->GetName() + wxT("1"));
			}

			wxTreeItemId hNewItem = m_metaTreeWnd->AppendItem(hSelItem, newDataProcessor->GetName(), 597, 597);
			AddDataProcessorItem(newDataProcessor, hNewItem);
			m_aMetaObj.insert_or_assign(hNewItem, newDataProcessor);

			if (commonMetaObject)
				commonMetaObject->AppendChild(newDataProcessor);

			newDataProcessor->ReadProperty();
			newDataProcessor->IncrRef();
		}
		else {
			newDataProcessor->SetParent(NULL);
			wxDELETE(newDataProcessor);
		}
	}

	m_metaData->Modify(true);
}

void CMetadataTree::ReplaceItem()
{
#pragma message("nouverbe to nouverbe: доработать замену обработки/отчёта")

	wxTreeItemId hSelItem = m_metaTreeWnd->GetSelection();
	IMetaObject *currentMetaObject = GetMetaObject(m_metaTreeWnd->GetSelection());

	if (currentMetaObject->GetClsid() == g_metaDataProcessorCLSID) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			"data processor files (*.dpr)|*.dpr", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectDataProcessorValue *newDataProcessor = dynamic_cast<CMetaObjectDataProcessorValue *>(
			currentMetaObject
			);

		wxASSERT(newDataProcessor);

		CMetadataDataProcessor metadataDataProcessor(newDataProcessor);

		for (auto attributes : newDataProcessor->GetObjectAttributes()) {
			if (attributes->DeleteMetaObject(m_metaData)) {
				//m_metaData->RemoveCommonMetadata(attributes);
			}
			//attributes->SetParent(NULL);
			attributes->DecrRef();
		}

		for (auto table : newDataProcessor->GetObjectTables()) {
			if (table->DeleteMetaObject(m_metaData)) {
				for (auto attributes : table->GetObjectAttributes()) {
					if (attributes->DeleteMetaObject(m_metaData)) {
						//m_metaData->RemoveCommonMetadata(attributes);
						//attributes->SetParent(NULL);
						attributes->DecrRef();
					}
				}
				//m_metaData->RemoveCommonMetadata(table);
				//table->SetParent(NULL);
				table->DecrRef();
			}
		}

		for (auto forms : newDataProcessor->GetObjectForms()) {
			if (forms->DeleteMetaObject(m_metaData)) {
				//m_metaData->RemoveCommonMetadata(forms);
				//forms->SetParent(NULL);
				forms->DecrRef();
			}
		}

		for (auto templates : newDataProcessor->GetObjectTemplates()) {
			if (templates->DeleteMetaObject(m_metaData)) {
				//m_metaData->RemoveCommonMetadata(templates);
				//templates->SetParent(NULL);
				templates->DecrRef();
			}
		}

		metadataDataProcessor.LoadFromFile(openFileDialog.GetPath());

		if (!m_metaData->RenameMetaObject(newDataProcessor, newDataProcessor->GetName())) {
			newDataProcessor->SetName(newDataProcessor->GetName() + wxT("1"));
		}

		m_metaTreeWnd->SetItemText(hSelItem, newDataProcessor->GetName());
		m_metaTreeWnd->DeleteChildren(hSelItem);

		AddDataProcessorItem(newDataProcessor, hSelItem);
		newDataProcessor->ReadProperty();
	}
	else
	{
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			"report files (*.rpt)|*.rpt", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectReportValue *newDataProcessor = dynamic_cast<CMetaObjectReportValue *>(
			currentMetaObject
			);

		wxASSERT(newDataProcessor);

		CMetadataReport metadataDataProcessor(newDataProcessor);

		for (auto attributes : newDataProcessor->GetObjectAttributes()) {
			if (attributes->DeleteMetaObject(m_metaData)) {
				//m_metaData->RemoveCommonMetadata(attributes);
			}
			//attributes->SetParent(NULL);
			attributes->DecrRef();
		}

		for (auto table : newDataProcessor->GetObjectTables()) {
			if (table->DeleteMetaObject(m_metaData)) {
				for (auto attributes : table->GetObjectAttributes()) {
					if (attributes->DeleteMetaObject(m_metaData)) {
						//m_metaData->RemoveCommonMetadata(attributes);
						//attributes->SetParent(NULL);
						attributes->DecrRef();
					}
				}
				//m_metaData->RemoveCommonMetadata(table);
				//table->SetParent(NULL);
				table->DecrRef();
			}
		}

		for (auto forms : newDataProcessor->GetObjectForms()) {
			if (forms->DeleteMetaObject(m_metaData)) {
				//m_metaData->RemoveCommonMetadata(forms);
				//forms->SetParent(NULL);
				forms->DecrRef();
			}
		}

		for (auto templates : newDataProcessor->GetObjectTemplates()) {
			if (templates->DeleteMetaObject(m_metaData)) {
				//m_metaData->RemoveCommonMetadata(templates);
				//templates->SetParent(NULL);
				templates->DecrRef();
			}
		}

		metadataDataProcessor.LoadFromFile(openFileDialog.GetPath());

		if (!m_metaData->RenameMetaObject(newDataProcessor, newDataProcessor->GetName())) {
			newDataProcessor->SetName(newDataProcessor->GetName() + wxT("1"));
		}

		m_metaTreeWnd->SetItemText(hSelItem, newDataProcessor->GetName());
		m_metaTreeWnd->DeleteChildren(hSelItem);

		AddDataProcessorItem(newDataProcessor, hSelItem);
		newDataProcessor->ReadProperty();
	}

	m_metaData->Modify(true);
}

void CMetadataTree::SaveItem()
{
	IMetaObject *currentMetaObject = GetMetaObject(m_metaTreeWnd->GetSelection());

	if (currentMetaObject->GetClsid() == g_metaDataProcessorCLSID) {

		wxFileDialog saveFileDialog(this, _("Open data processor file"), "", "",
			"data processor files (*.dpr)|*.dpr", wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeWnd->GetItemText(m_metaTreeWnd->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectDataProcessorValue *newDataProcessor = dynamic_cast<CMetaObjectDataProcessorValue *>(
			currentMetaObject
			);

		wxASSERT(newDataProcessor);

		CMetadataDataProcessor metadataDataProcessor(newDataProcessor);

		/*for (auto attributes : newDataProcessor->GetObjectAttributes()) {
			metadataDataProcessor.AppendCommonMetadata(attributes);
		}

		for (auto table : newDataProcessor->GetObjectTables()) {
			metadataDataProcessor.AppendCommonMetadata(table);
			for (auto attributes : table->GetObjectAttributes()) {
				metadataDataProcessor.AppendInnerMetadata(table);
			}
		}

		for (auto forms : newDataProcessor->GetObjectForms()) {
			metadataDataProcessor.AppendCommonMetadata(forms);
		}

		for (auto templates : newDataProcessor->GetObjectTemplates()) {
			metadataDataProcessor.AppendCommonMetadata(templates);
		}*/

		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
	else {
		wxFileDialog saveFileDialog(this, _("Open report file"), "", "",
			"report files (*.rpt)|*.rpt", wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeWnd->GetItemText(m_metaTreeWnd->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectReportValue *newDataProcessor = dynamic_cast<CMetaObjectReportValue *>(
			currentMetaObject
			);

		wxASSERT(newDataProcessor);

		CMetadataReport metadataDataProcessor(newDataProcessor);

		/*for (auto attributes : newDataProcessor->GetObjectAttributes()) {
			metadataDataProcessor.AppendCommonMetadata(attributes);
		}

		for (auto table : newDataProcessor->GetObjectTables()) {
			metadataDataProcessor.AppendCommonMetadata(table);
			for (auto attributes : table->GetObjectAttributes()) {
				metadataDataProcessor.AppendInnerMetadata(table);
			}
		}

		for (auto forms : newDataProcessor->GetObjectForms()) {
			metadataDataProcessor.AppendCommonMetadata(forms);
		}

		for (auto templates : newDataProcessor->GetObjectTemplates()) {
			metadataDataProcessor.AppendCommonMetadata(templates);
		}*/

		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
}

void CMetadataTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != eRunMode::DESIGNER_MODE)
		return;

	IMetaObject *metaObject = GetMetaObject(m_metaTreeWnd->GetSelection());

	if (!metaObject)
		return;

	metaObject->ProcessCommand(id);
}

void CMetadataTree::PrepareReplaceMenu(wxMenu *defultMenu)
{
	wxMenuItem *menuItem = defultMenu->Append(ID_METATREE_REPLACE, _("replace data processor, report..."));
	menuItem->Enable(!m_bReadOnly);
	menuItem = defultMenu->Append(ID_METATREE_SAVE, _("save data processor, report..."));
	defultMenu->AppendSeparator();
}

void CMetadataTree::PrepareContextMenu(wxMenu *defultMenu, const wxTreeItemId &item)
{
	IMetaObject *metaObject = GetMetaObject(item);

	if (metaObject
		&& !metaObject->PrepareContextMenu(defultMenu))
	{
		if (g_metaDataProcessorCLSID == metaObject->GetClsid()
			|| g_metaReportCLSID == metaObject->GetClsid()) {
			PrepareReplaceMenu(defultMenu);
		}

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
	else if (!metaObject && item != m_treeCOMMON)
	{
		wxMenuItem *m_menuItem = defultMenu->Append(ID_METATREE_NEW, _("new"));
		m_menuItem->SetBitmap(wxGetImageBMPFromResource(IDB_EDIT_NEW));
		m_menuItem->Enable(!m_bReadOnly);

		if (item == m_treeDATAPROCESSORS
			|| item == m_treeREPORTS) {
			defultMenu->AppendSeparator();
			wxMenuItem *menuItem = defultMenu->Append(ID_METATREE_INSERT, _("insert data processor, report..."));
			menuItem->Enable(!m_bReadOnly);
		}
	}
}

void CMetadataTree::UpdateToolbar(IMetaObject *obj, const wxTreeItemId &item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly && item != m_treeCOMMON);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != NULL && item != m_metaTreeWnd->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_REMOVE, obj != NULL && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

void CMetadataTree::OnCloseDocument(CDocument *doc)
{
	auto itFounded = std::find(m_aMetaOpenedForms.begin(), m_aMetaOpenedForms.end(), doc);

	if (itFounded != m_aMetaOpenedForms.end())
		m_aMetaOpenedForms.erase(itFounded);
}

#include "frontend/mainFrame.h"

void CMetadataTree::Modify(bool modify)
{
	if (m_docParent != NULL) {
		m_docParent->Modify(modify);
	}
	else {
		mainFrame->Modify(modify);
	}
}

bool CMetadataTree::OpenFormMDI(IMetaObject *obj)
{
	CDocument *foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc == NULL) {
		foundedDoc = reportManager->OpenFormMDI(obj, m_docParent, m_bReadOnly ? wxDOC_READONLY : wxDOC_NEW);
		//Значит, подходящего шаблона не было! 
		if (foundedDoc) {
			m_aMetaOpenedForms.push_back(foundedDoc); foundedDoc->Activate();
			return true;
		}
	}
	else {
		foundedDoc->Activate();
		return true;
	}

	return false;
}

bool CMetadataTree::OpenFormMDI(IMetaObject *obj, CDocument *&foundedDoc)
{
	foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc == NULL) {
		foundedDoc = reportManager->OpenFormMDI(obj, m_docParent, m_bReadOnly ? wxDOC_READONLY : wxDOC_NEW);
		//Значит, подходящего шаблона не было! 
		if (foundedDoc) {
			m_aMetaOpenedForms.push_back(foundedDoc); foundedDoc->Activate();
			return true;
		}
	}
	else {
		foundedDoc->Activate();
		return true;
	}

	return false;
}

bool CMetadataTree::CloseFormMDI(IMetaObject *obj)
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

CDocument *CMetadataTree::GetDocument(IMetaObject *obj)
{
	CDocument *m_foundedDoc = NULL;
	auto itFounded = std::find_if(m_aMetaOpenedForms.begin(), m_aMetaOpenedForms.end(), [obj](CDocument *currDoc) {return obj == currDoc->GetMetaObject(); });
	if (itFounded != m_aMetaOpenedForms.end()) {
		m_foundedDoc = *itFounded;
	}
	return m_foundedDoc;
}

bool CMetadataTree::RenameMetaObject(IMetaObject *obj, const wxString &sNewName)
{
	wxTreeItemId curItem = m_metaTreeWnd->GetSelection();

	if (!curItem.IsOk()) {
		return false;
	}

	if (m_metaData->RenameMetaObject(obj, sNewName))
	{
		CDocument *m_currDocument = GetDocument(obj);
		if (m_currDocument) {
			m_currDocument->SetTitle(obj->GetClassName() + wxT(": ") + sNewName);
			m_currDocument->OnChangeFilename(true);
		}
		m_metaTreeWnd->SetItemText(curItem, sNewName);
		return true;
	}
	return false;
}

#include "common/codeproc.h"
#include "metadata/objects/baseObject.h"
#include "metadata/metaObjects/tables/metaTableObject.h"

void CMetadataTree::AddCatalogItem(IMetaObject *metaObject, const wxTreeItemId &hParentID)
{
	IMetaObjectRefValue *metaObjectValue = dynamic_cast<IMetaObjectRefValue *>(metaObject);
	wxASSERT(metaObject);

	//Список аттрибутов 
	wxTreeItemId hAttributes = m_metaTreeWnd->AppendItem(hParentID, objectAttributesName, ICON_ATTRIBUTEGROUP, ICON_ATTRIBUTEGROUP);
	SETITEMTYPE(hAttributes, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;

		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hAttributes, metaAttribute->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, 217, 217);
	SETITEMTYPE(hTables, g_metaTableCLSID, 218);

	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTables, metaTable->GetName(), 218, 218);
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
	wxTreeItemId hForm = m_metaTreeWnd->AppendItem(hParentID, objectFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(hForm, g_metaFormCLSID, ICON_FORM);

	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hForm, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(hTemplates, g_metaTemplateCLSID, ICON_MAKET);

	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTemplates, metaTemplates->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplates);
	}
}

void CMetadataTree::AddDocumentItem(IMetaObject *metaObject, const wxTreeItemId &hParentID)
{
	IMetaObjectRefValue *metaObjectValue = dynamic_cast<IMetaObjectRefValue *>(metaObject);
	wxASSERT(metaObjectValue);

	//Список аттрибутов 
	wxTreeItemId hAttributes = m_metaTreeWnd->AppendItem(hParentID, objectAttributesName, ICON_ATTRIBUTEGROUP, ICON_ATTRIBUTEGROUP);
	SETITEMTYPE(hAttributes, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;

		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hAttributes, metaAttribute->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, 217, 217);
	SETITEMTYPE(hTables, g_metaTableCLSID, 218);

	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTables, metaTable->GetName(), 218, 218);
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
	wxTreeItemId hForm = m_metaTreeWnd->AppendItem(hParentID, objectFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(hForm, g_metaFormCLSID, ICON_FORM);

	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hForm, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(hTemplates, g_metaTemplateCLSID, ICON_MAKET);

	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTemplates, metaTemplates->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplates);
	}
}

void CMetadataTree::AddEnumerationItem(IMetaObject *metaObject, const wxTreeItemId &hParentID)
{
	IMetaObjectRefValue *metaObjectValue = dynamic_cast<IMetaObjectRefValue *>(metaObject);
	wxASSERT(metaObjectValue);

	//Enumerations
	wxTreeItemId hEnums = m_metaTreeWnd->AppendItem(hParentID, objectEnumerationsName, 215, 215);
	SETITEMTYPE(hEnums, g_metaEnumCLSID, 215);

	for (auto metaEnumerations : metaObjectValue->GetObjectEnums()) {
		if (metaEnumerations->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hEnums, metaEnumerations->GetName(), 215, 215);
		m_aMetaObj.insert_or_assign(hItem, metaEnumerations);
	}

	//Формы
	wxTreeItemId hForm = m_metaTreeWnd->AppendItem(hParentID, objectFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(hForm, g_metaFormCLSID, ICON_FORM);

	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hForm, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(hTemplates, g_metaTemplateCLSID, ICON_MAKET);

	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTemplates, metaTemplates->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplates);
	}
}

void CMetadataTree::AddDataProcessorItem(IMetaObject *metaObject, const wxTreeItemId &hParentID)
{
	IMetaObjectValue *metaObjectValue = dynamic_cast<IMetaObjectValue *>(metaObject);
	wxASSERT(metaObjectValue);

	//Список аттрибутов 
	wxTreeItemId hAttributes = m_metaTreeWnd->AppendItem(hParentID, objectAttributesName, ICON_ATTRIBUTEGROUP, ICON_ATTRIBUTEGROUP);
	SETITEMTYPE(hAttributes, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hAttributes, metaAttribute->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, 217, 217);
	SETITEMTYPE(hTables, g_metaTableCLSID, 218);

	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTables, metaTable->GetName(), 218, 218);
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
	wxTreeItemId hForm = m_metaTreeWnd->AppendItem(hParentID, objectFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(hForm, g_metaFormCLSID, ICON_FORM);

	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hForm, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(hTemplates, g_metaTemplateCLSID, ICON_MAKET);

	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTemplates, metaTemplates->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplates);
	}
}

void CMetadataTree::AddReportItem(IMetaObject *metaObject, const wxTreeItemId &hParentID)
{
	IMetaObjectValue *metaObjectValue = dynamic_cast<IMetaObjectValue *>(metaObject);
	wxASSERT(metaObjectValue);

	//Список аттрибутов 
	wxTreeItemId hAttributes = m_metaTreeWnd->AppendItem(hParentID, objectAttributesName, ICON_ATTRIBUTEGROUP, ICON_ATTRIBUTEGROUP);
	SETITEMTYPE(hAttributes, g_metaAttributeCLSID, ICON_ATTRIBUTE);

	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;

		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hAttributes, metaAttribute->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, 217, 217);
	SETITEMTYPE(hTables, g_metaTableCLSID, 218);

	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTables, metaTable->GetName(), 218, 218);
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
	wxTreeItemId hForm = m_metaTreeWnd->AppendItem(hParentID, objectFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(hForm, g_metaFormCLSID, ICON_FORM);

	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hForm, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = m_metaTreeWnd->AppendItem(hParentID, objectTablesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(hTemplates, g_metaTemplateCLSID, ICON_MAKET);

	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(hTemplates, metaTemplates->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplates);
	}
}

void CMetadataTree::EditModule(const wxString &fullName, int lineNumber, bool setRunLine)
{
	IMetaObject *metaObject = m_metaData->FindByName(fullName);

	if (!metaObject)
		return;

	if (m_bReadOnly)
		return;

	CDocument *m_foundedDoc = NULL;

	if (!OpenFormMDI(metaObject, m_foundedDoc))
		return;

	ICodeInfo *m_codeInfo = dynamic_cast<ICodeInfo *>(m_foundedDoc);

	if (m_codeInfo) {
		m_codeInfo->SetCurrentLine(lineNumber, setRunLine);
	}
}

void CMetadataTree::InitTree()
{
	m_treeMETADATA = m_metaTreeWnd->AddRoot(wxT("configuration"), 225, 225, 0);
	SETITEMTYPE(m_treeMETADATA, g_metaCommonMetadataCLSID, ICON_METADATA);
	m_treeCOMMON = m_metaTreeWnd->AppendItem(m_treeMETADATA, commonName, ICON_COMMON, ICON_COMMON);

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	m_treeMODULES = m_metaTreeWnd->AppendItem(m_treeCOMMON, commonModulesName, ICON_MODULEGROUP, ICON_MODULEGROUP);
	SETITEMTYPE(m_treeMODULES, g_metaCommonModuleCLSID, ICON_MODULE);

	m_treeFORMS = m_metaTreeWnd->AppendItem(m_treeCOMMON, commonFormsName, ICON_FORMGROUP, ICON_FORMGROUP);
	SETITEMTYPE(m_treeFORMS, g_metaCommonFormCLSID, ICON_FORM);

	m_treeTEMPLATES = m_metaTreeWnd->AppendItem(m_treeCOMMON, commonTemplatesName, ICON_MAKETGROUP, ICON_MAKETGROUP);
	SETITEMTYPE(m_treeTEMPLATES, g_metaCommonTemplateCLSID, ICON_MAKET);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	m_treeCONSTANTS = m_metaTreeWnd->AppendItem(m_treeMETADATA, constantsName, 86, 86);
	SETITEMTYPE(m_treeCONSTANTS, g_metaConstantCLSID, ICON_ATTRIBUTE);

	m_treeCATALOGS = m_metaTreeWnd->AppendItem(m_treeMETADATA, catalogsName, 85, 85);
	SETITEMTYPE(m_treeCATALOGS, g_metaCatalogCLSID, 226);

	m_treeDOCUMENTS = m_metaTreeWnd->AppendItem(m_treeMETADATA, documentsName, 171, 171);
	SETITEMTYPE(m_treeDOCUMENTS, g_metaDocumentCLSID, 216);

	m_treeENUMERATIONS = m_metaTreeWnd->AppendItem(m_treeMETADATA, enumerationsName, 599, 599);
	SETITEMTYPE(m_treeENUMERATIONS, g_metaEnumerationCLSID, 600);

	m_treeDATAPROCESSORS = m_metaTreeWnd->AppendItem(m_treeMETADATA, dataProcessorName, 88, 88);
	SETITEMTYPE(m_treeDATAPROCESSORS, g_metaDataProcessorCLSID, 598);

	m_treeREPORTS = m_metaTreeWnd->AppendItem(m_treeMETADATA, reportsName, 87, 87);
	SETITEMTYPE(m_treeREPORTS, g_metaReportCLSID, 597);

	//Set item bold and name
	m_metaTreeWnd->SetItemText(m_treeMETADATA, wxT("configuration"));
	m_metaTreeWnd->SetItemBold(m_treeMETADATA);
}

void CMetadataTree::ClearTree()
{
	for (auto doc : m_aMetaOpenedForms) {
		doc->DeleteAllViews();
	}

	m_aMetaObj.clear();
	m_aMetaClassObj.clear();

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	if (m_treeMODULES.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeMODULES);
	if (m_treeFORMS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeFORMS);
	if (m_treeTEMPLATES.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeTEMPLATES);
	if (m_treeCONSTANTS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeCONSTANTS);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	if (m_treeCATALOGS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeCATALOGS);
	if (m_treeDOCUMENTS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeDOCUMENTS);
	if (m_treeENUMERATIONS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeENUMERATIONS);
	if (m_treeDATAPROCESSORS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeDATAPROCESSORS);
	if (m_treeREPORTS.IsOk()) m_metaTreeWnd->DeleteChildren(m_treeREPORTS);

	//delete all items
	m_metaTreeWnd->DeleteAllItems();

	//Initialize tree
	InitTree();
}

void CMetadataTree::FillData()
{
	IMetaObject *m_commonMetadata = m_metaData->GetCommonMetaObject();
	wxASSERT(m_commonMetadata);
	m_metaTreeWnd->SetItemText(m_treeMETADATA, m_metaData->GetMetadataName());

	//set parent object
	m_aMetaObj.insert_or_assign(m_treeMETADATA, m_commonMetadata);

	//****************************************************************
	//*                          CommonModules                       *
	//****************************************************************
	for (auto metaModule : m_metaData->GetMetaObjects(g_metaCommonModuleCLSID)) {
		if (metaModule->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeMODULES, metaModule->GetName(), ICON_MODULE, ICON_MODULE);
		m_aMetaObj.insert_or_assign(hItem, metaModule);
	}

	//****************************************************************
	//*                          CommonForms                         *
	//****************************************************************
	for (auto metaForm : m_metaData->GetMetaObjects(g_metaCommonFormCLSID)) {
		if (metaForm->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeFORMS, metaForm->GetName(), ICON_FORM, ICON_FORM);
		m_aMetaObj.insert_or_assign(hItem, metaForm);
	}

	//****************************************************************
	//*                          CommonMakets                        *
	//****************************************************************
	for (auto metaTemplate : m_metaData->GetMetaObjects(g_metaCommonTemplateCLSID)) {
		if (metaTemplate->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeTEMPLATES, metaTemplate->GetName(), ICON_MAKET, ICON_MAKET);
		m_aMetaObj.insert_or_assign(hItem, metaTemplate);
	}

	//****************************************************************
	//*                          Constants                           *
	//****************************************************************
	for (auto metaConstant : m_metaData->GetMetaObjects(g_metaConstantCLSID)) {
		if (metaConstant->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeCONSTANTS, metaConstant->GetName(), ICON_ATTRIBUTE, ICON_ATTRIBUTE);
		m_aMetaObj.insert_or_assign(hItem, metaConstant);
	}

	//****************************************************************
	//*                        Catalogs                              *
	//****************************************************************
	for (auto catalog : m_metaData->GetMetaObjects(g_metaCatalogCLSID)) {
		if (catalog->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeCATALOGS, catalog->GetName(), 226, 226);
		AddCatalogItem(catalog, hItem);
		m_aMetaObj.insert_or_assign(hItem, catalog);
	}

	//****************************************************************
	//*                        Documents                             *
	//****************************************************************
	for (auto document : m_metaData->GetMetaObjects(g_metaDocumentCLSID)) {
		if (document->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeDOCUMENTS, document->GetName(), 216, 216);
		AddDocumentItem(document, hItem);
		m_aMetaObj.insert_or_assign(hItem, document);
	}

	//****************************************************************
	//*                          Enumerations                        *
	//****************************************************************
	for (auto enumeration : m_metaData->GetMetaObjects(g_metaEnumerationCLSID)) {
		if (enumeration->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeENUMERATIONS, enumeration->GetName(), 600, 600);
		AddEnumerationItem(enumeration, hItem);
		m_aMetaObj.insert_or_assign(hItem, enumeration);
	}

	//****************************************************************
	//*                          Data processor                      *
	//****************************************************************
	for (auto dataProcessor : m_metaData->GetMetaObjects(g_metaDataProcessorCLSID)) {
		if (dataProcessor->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeDATAPROCESSORS, dataProcessor->GetName(), 598, 598);
		AddDataProcessorItem(dataProcessor, hItem);
		m_aMetaObj.insert_or_assign(hItem, dataProcessor);
	}

	//****************************************************************
	//*                          Report			                     *
	//****************************************************************
	for (auto report : m_metaData->GetMetaObjects(g_metaReportCLSID)) {
		if (report->IsDeleted())
			continue;
		wxTreeItemId hItem = m_metaTreeWnd->AppendItem(m_treeREPORTS, report->GetName(), 597, 597);
		AddReportItem(report, hItem);
		m_aMetaObj.insert_or_assign(hItem, report);
	}

	//set modify 
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(m_commonMetadata, m_treeMETADATA);
}

bool CMetadataTree::Load(CConfigFileMetadata *metadataObj)
{
	ClearTree();
	m_metaData = metadataObj ? metadataObj : IConfigMetadata::Get();
	m_metaTreeWnd->Freeze();
	FillData(); //Fill all data from metadata
	m_metaData->SetMetaTree(this);
	m_metaTreeWnd->SelectItem(m_treeMETADATA);
	m_metaTreeWnd->Expand(m_treeMETADATA);
	m_metaTreeWnd->Expand(m_treeCOMMON);
	m_metaTreeWnd->Thaw();
	return true;
}

bool CMetadataTree::Save()
{
	wxASSERT(m_metaData);

	if (m_metaData->IsModified() && wxMessageBox("Configuration '" + m_metaData->GetMetadataName() + "' has been changed. Save?", wxT("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES)
		return m_metaData->SaveMetadata();

	return false;
}