////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metatree window
////////////////////////////////////////////////////////////////////////////

#include "metatreeWnd.h"
#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/docView/docManager.h"
#include "backend/appData.h"

#define metadataName _("metaData")
#define commonName _("common")

#define commonModulesName _("common modules")
#define commonFormsName _("common forms")
#define commonTemplatesName _("common templates")

#define interfacesName _("interfaces")
#define rolesName _("roles")

#define constantsName _("constants")

#define catalogsName _("catalogs")
#define documentsName _("documents")
#define enumerationsName _("enumerations")
#define dataProcessorName _("data processors")
#define reportsName _("reports")
#define informationRegisterName _("information Registers")
#define accumulationRegisterName _("accumulation Registers")

#define	objectFormsName _("forms")
#define	objectModulesName _("modules")
#define	objectTemplatesName _("templates")
#define objectAttributesName _("attributes")
#define objectDimensionsName _("dimensions")
#define objectResourcesName _("resources")

#define objectTablesName _("tables")
#define objectEnumerationsName _("enums")

//***********************************************************************
//*								metaData                                * 
//***********************************************************************

void IMetaDataTree::OnCloseDocument(IBackendMetaDocument* doc)
{
}

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/win/dlgs/formSelector/formSelector.h"

form_identifier_t IMetaDataTree::SelectFormType(CMetaObjectForm* metaObject) const
{
	IMetaObjectGenericData* parent = wxDynamicCast(
		metaObject->GetParent(), IMetaObjectGenericData
	);
	OptionList* optList = parent->GetFormType();
	wxASSERT(optList);
	CDialogSelectTypeForm* selectTypeForm = new CDialogSelectTypeForm(parent, metaObject);
	for (auto option : optList->GetOptions()) {
		selectTypeForm->AppendTypeForm(option.m_name, option.m_label, option.m_intVal);
	}
	wxDELETE(optList);
	selectTypeForm->CreateSelector();
	const form_identifier_t& sel_id = selectTypeForm->ShowModal();
	selectTypeForm->Destroy();
	return sel_id;
}

void IMetaDataTree::Modify(bool modify)
{
	if (m_docParent != nullptr) {
		m_docParent->Modify(modify);
	}
	else {
		mainFrame->Modify(modify);
	}
}

bool IMetaDataTree::OpenFormMDI(IMetaObject* obj)
{
	CMetaDocument* foundedDoc = GetDocument(obj);
	//не найден в списке уже существующих
	if (foundedDoc == nullptr) {
		foundedDoc = docManager->OpenFormMDI(obj, m_docParent, m_bReadOnly ? wxDOC_READONLY : wxDOC_NEW);
		//Значит, подходящего шаблона не было! 
		if (foundedDoc != nullptr) {
			return true;
		}
	}
	else {
		foundedDoc->Activate();
		return true;
	}

	return false;
}

bool IMetaDataTree::OpenFormMDI(IMetaObject* obj, IBackendMetaDocument*& doc)
{
	CMetaDocument* foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc == nullptr) {
		foundedDoc = docManager->OpenFormMDI(obj, m_docParent, m_bReadOnly ? wxDOC_READONLY : wxDOC_NEW);
		//Значит, подходящего шаблона не было! 
		if (foundedDoc != nullptr) {
			doc = foundedDoc;
			foundedDoc->Activate();
			return true;
		}
	}
	else {
		doc = foundedDoc;
		foundedDoc->Activate();
		return true;
	}

	return false;
}

bool IMetaDataTree::CloseFormMDI(IMetaObject* obj)
{
	CMetaDocument* foundedDoc = GetDocument(obj);

	//не найден в списке уже существующих
	if (foundedDoc != nullptr) {
		objectInspector->SelectObject(obj, this);
		if (foundedDoc->Close()) {
			// Delete the child document by deleting all its views.
			return foundedDoc->DeleteAllViews();
		}
	}

	return false;
}

CMetaDocument* IMetaDataTree::GetDocument(IMetaObject* obj) const
{
	for (auto& doc : docManager->GetDocumentsVector()) {
		CMetaDocument* metaDoc = wxDynamicCast(doc, CMetaDocument);
		if (metaDoc != nullptr && obj == metaDoc->GetMetaObject()) {
			return metaDoc;
		}
		else if (metaDoc != nullptr) {
			for (auto& child_doc : metaDoc->GetChild()) {
				CMetaDocument* child_metaDoc = wxDynamicCast(child_doc, CMetaDocument);
				if (child_metaDoc != nullptr && obj == child_metaDoc->GetMetaObject()) {
					return child_metaDoc;
				}
			}
		}
	}
	return nullptr;
}

void IMetaDataTree::EditModule(const wxString& fullName, int lineNumber, bool setRunLine)
{
	IMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return;

	IMetaObject* metaObject = metaData->FindByName(fullName);

	if (metaObject == nullptr)
		return;

	if (m_bReadOnly)
		return;

	IBackendMetaDocument* foundedDoc = nullptr;
	if (OpenFormMDI(metaObject, foundedDoc)) {
		IModuleDocument* moduleDoc = static_cast<IModuleDocument*>(foundedDoc);
		if (moduleDoc != nullptr) {
			moduleDoc->SetCurrentLine(lineNumber, setRunLine);
		}
	}
}

//***********************************************************************
//*								 metaData                               * 
//***********************************************************************

void CMetadataTree::ActivateItem(const wxTreeItemId& item)
{
	IMetaObject* currObject = GetMetaObject(item);
	if (currObject == nullptr)
		return;

	OpenFormMDI(currObject);
}

IMetaObject* CMetadataTree::CreateItem(bool showValue)
{
	wxTreeItemId selectedItem =
		m_metaTreeWnd->GetSelection(), parentItem = selectedItem;

	if (!selectedItem.IsOk())
		return nullptr;

	treeClassIdentifierData_t* itemData = nullptr;

	while (parentItem != nullptr) {
		itemData = dynamic_cast<treeClassIdentifierData_t*>(m_metaTreeWnd->GetItemData(parentItem));
		if (itemData != nullptr) {
			selectedItem = parentItem;
			break;
		}
		parentItem = m_metaTreeWnd->GetItemParent(parentItem);
	}

	if (itemData == nullptr)
		return nullptr;

	IMetaObject* metaParent = nullptr;

	while (parentItem != nullptr) {
		metaParent = GetMetaObject(parentItem);
		if (metaParent != nullptr) {
			break;
		}
		parentItem = m_metaTreeWnd->GetItemParent(parentItem);
	}

	wxASSERT(metaParent);

	IMetaObject* newObject = m_metaData->CreateMetaObject(itemData->m_clsid, metaParent);

	if (newObject == nullptr)
		return nullptr;

	m_metaTreeWnd->Freeze();

	wxTreeItemId createdItem = nullptr;
	if (itemData->m_clsid == g_metaTableCLSID) {
		createdItem = AppendGroupItem(selectedItem, g_metaAttributeCLSID, newObject);
	}
	else {
		createdItem = AppendItem(selectedItem, newObject);
	}

	//Advanced mode
	if (itemData->m_clsid == g_metaCatalogCLSID)
		AddCatalogItem(newObject, createdItem);
	else if (itemData->m_clsid == g_metaDocumentCLSID)
		AddDocumentItem(newObject, createdItem);
	else if (itemData->m_clsid == g_metaEnumerationCLSID)
		AddEnumerationItem(newObject, createdItem);
	else if (itemData->m_clsid == g_metaDataProcessorCLSID)
		AddDataProcessorItem(newObject, createdItem);
	else if (itemData->m_clsid == g_metaReportCLSID)
		AddReportItem(newObject, createdItem);
	else if (itemData->m_clsid == g_metaInformationRegisterCLSID)
		AddInformationRegisterItem(newObject, createdItem);
	else if (itemData->m_clsid == g_metaAccumulationRegisterCLSID)
		AddAccumulationRegisterItem(newObject, createdItem);

	if (showValue) {
		OpenFormMDI(newObject);
	}

	UpdateToolbar(newObject, createdItem);

	m_metaTreeWnd->InvalidateBestSize();
	m_metaTreeWnd->SelectItem(createdItem);
	m_metaTreeWnd->Expand(createdItem);

	m_metaTreeWnd->Thaw();

	for (auto doc : docManager->GetDocumentsVector()) {
		doc->UpdateAllViews();
	}

	objectInspector->SelectObject(newObject, m_metaTreeWnd->GetEventHandler());
	return newObject;
}

void CMetadataTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeWnd->GetSelection();
	if (!selection.IsOk())
		return;
	IMetaObject* m_currObject = GetMetaObject(selection);
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

	while (hItem) {
		EraseItem(hItem);
		hItem = m_metaTreeWnd->GetNextChild(hItem, m_cookie);
	}

	IMetaObject* metaObject = GetMetaObject(selection);
	wxASSERT(metaObject);
	EraseItem(selection);
	m_metaData->RemoveMetaObject(metaObject);

	//Delete item from tree
	m_metaTreeWnd->Delete(selection);

	for (auto doc : docManager->GetDocumentsVector()) {
		doc->UpdateAllViews();
	}

	const wxTreeItemId nextSelection = m_metaTreeWnd->GetFocusedItem();

	if (nextSelection.IsOk()) {
		UpdateToolbar(GetMetaObject(nextSelection), nextSelection);
	}
}

void CMetadataTree::EraseItem(const wxTreeItemId& item)
{
	IMetaObject* const metaObject = GetMetaObject(item);
	for (auto& doc : docManager->GetDocumentsVector()) {
		CMetaDocument* metaDoc = wxDynamicCast(doc, CMetaDocument);
		if (metaDoc != nullptr && metaObject == metaDoc->GetMetaObject()) {
			metaDoc->DeleteAllViews();
		}
	}
}

void CMetadataTree::PropertyItem()
{
	if (appData->GetAppMode() != eRunMode::eDESIGNER_MODE)
		return;

	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	IMetaObject* metaObject = GetMetaObject(selection);

	objectInspector->ClearProperty();
	UpdateToolbar(metaObject, selection);

	if (!metaObject)
		return;

	objectInspector->CallAfter(&CObjectInspector::SelectObject, metaObject, m_metaTreeWnd->GetEventHandler());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CMetadataTree::Collapse()
{
	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	treeData_t* data =
		dynamic_cast<treeData_t*>(m_metaTreeWnd->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = false;
}

void CMetadataTree::Expand()
{
	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	treeData_t* data =
		dynamic_cast<treeData_t*>(m_metaTreeWnd->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CMetadataTree::UpItem()
{
	if (appData->GetAppMode() != eRunMode::eDESIGNER_MODE)
		return;

	m_metaTreeWnd->Freeze();

	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	const wxTreeItemId& nextItem = m_metaTreeWnd->GetPrevSibling(selection);
	IMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != nullptr && nextItem.IsOk()) {
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
			std::function<void(CMetadataTreeWnd*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](CMetadataTreeWnd* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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
					tree->SetItemData(nextId, nullptr);
					nextId = tree->GetNextChild(nextId, coockie);
				}
				};

			swap(tree, nextItem, newId);

			m_metaTreeWnd->SetItemData(nextItem, nullptr);
			m_metaTreeWnd->Delete(nextItem);

			//m_metaTreeWnd->Expand(newId);
		}
	}

	m_metaTreeWnd->Thaw();
}

void CMetadataTree::DownItem()
{
	if (appData->GetAppMode() != eRunMode::eDESIGNER_MODE)
		return;

	m_metaTreeWnd->Freeze();

	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	const wxTreeItemId& prevItem = m_metaTreeWnd->GetNextSibling(selection);
	IMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != nullptr && prevItem.IsOk()) {
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
			std::function<void(CMetadataTreeWnd*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](CMetadataTreeWnd* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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
					tree->SetItemData(nextId, nullptr);
					nextId = tree->GetNextChild(nextId, coockie);
				}
				};

			swap(tree, prevItem, newId);

			m_metaTreeWnd->SetItemData(prevItem, nullptr);
			m_metaTreeWnd->Delete(prevItem);

			//m_metaTreeWnd->Expand(newId);
		}
	}

	m_metaTreeWnd->Thaw();
}

void CMetadataTree::SortItem()
{
	if (appData->GetAppMode() != eRunMode::eDESIGNER_MODE)
		return;
	m_metaTreeWnd->Freeze();
	const wxTreeItemId& selection = m_metaTreeWnd->GetSelection();
	IMetaObject* prevObject = GetMetaObject(selection);
	if (prevObject != nullptr && selection.IsOk()) {
		const wxTreeItemId& parentItem =
			m_metaTreeWnd->GetItemParent(selection);
		if (parentItem.IsOk()) {
			m_metaTreeWnd->SortChildren(parentItem);
		}
	}
	m_metaTreeWnd->Thaw();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/external/metadataDataProcessor.h"
#include "backend/external/metadataReport.h"

void CMetadataTree::InsertItem()
{
	IMetaObject* commonMetaObject = m_metaData->GetCommonMetaObject(); wxTreeItemId hSelItem = m_metaTreeWnd->GetSelection();

	if (hSelItem == m_treeDATAPROCESSORS) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			_("data processor files (*.edp)|*.edp"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		//create main metaObject
		CMetaDataDataProcessor metadataDataProcessor(m_metaData);

		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeWnd->Freeze();
			CMetaObjectDataProcessor* dataProcessor = metadataDataProcessor.GetDataProcessor();
			wxASSERT(dataProcessor);
			const wxTreeItemId& createdItem = AppendItem(hSelItem, dataProcessor);
			AddDataProcessorItem(dataProcessor, createdItem);
			m_metaTreeWnd->SelectItem(createdItem);
			dataProcessor->IncrRef();
			m_metaTreeWnd->Thaw();
		}
	}
	else {
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			"report files (*.erp)|*.erp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaDataReport metadataReport(m_metaData);

		if (metadataReport.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeWnd->Freeze();
			CMetaObjectReport* report = metadataReport.GetReport();
			wxASSERT(report);
			const wxTreeItemId& createdItem = AppendItem(hSelItem, report);
			AddReportItem(report, createdItem);
			m_metaTreeWnd->SelectItem(createdItem);
			report->IncrRef();
			m_metaTreeWnd->Thaw();
		}
	}

	m_metaData->Modify(true);
}

void CMetadataTree::ReplaceItem()
{
	wxTreeItemId hSelItem = m_metaTreeWnd->GetSelection();
	IMetaObject* currentMetaObject = GetMetaObject(m_metaTreeWnd->GetSelection());

	if (currentMetaObject->GetClassType() == g_metaDataProcessorCLSID) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			"data processor files (*.edp)|*.edp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaDataDataProcessor metadataDataProcessor(m_metaData);
		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeWnd->Freeze();
			CMetaObjectDataProcessor* metaObject = metadataDataProcessor.GetDataProcessor();
			wxTreeItemData* itemData = m_metaTreeWnd->GetItemData(hSelItem);
			if (itemData != nullptr) {
				treeMetaData_t* metaItem = dynamic_cast<treeMetaData_t*>(itemData);
				if (metaItem != nullptr)
					metaItem->m_metaObject = metaObject;
			}
			m_metaData->RemoveMetaObject(currentMetaObject);
			m_metaTreeWnd->SetItemText(hSelItem, metaObject->GetName());
			m_metaTreeWnd->DeleteChildren(hSelItem);
			AddDataProcessorItem(metaObject, hSelItem);
			m_metaTreeWnd->Thaw();
		}
	}
	else
	{
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			"report files (*.erp)|*.erp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectReport* newReport = dynamic_cast<CMetaObjectReport*>(
			currentMetaObject
			);

		wxASSERT(newReport);

		CMetaDataReport metadataDataProcessor(m_metaData);
		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeWnd->Freeze();
			CMetaObjectReport* metaObject = metadataDataProcessor.GetReport();
			wxTreeItemData* itemData = m_metaTreeWnd->GetItemData(hSelItem);
			if (itemData != nullptr) {
				treeMetaData_t* metaItem = dynamic_cast<treeMetaData_t*>(itemData);
				if (metaItem != nullptr)
					metaItem->m_metaObject = metaObject;
			}
			m_metaData->RemoveMetaObject(currentMetaObject);
			m_metaTreeWnd->SetItemText(hSelItem, newReport->GetName());
			m_metaTreeWnd->DeleteChildren(hSelItem);
			AddReportItem(newReport, hSelItem);
			m_metaTreeWnd->Thaw();
		}
	}

	m_metaData->Modify(true);
}

void CMetadataTree::SaveItem()
{
	IMetaObject* currentMetaObject = GetMetaObject(m_metaTreeWnd->GetSelection());

	if (currentMetaObject->GetClassType() == g_metaDataProcessorCLSID) {

		wxFileDialog saveFileDialog(this, _("Open data processor file"), "", "",
			"data processor files (*.edp)|*.edp", wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeWnd->GetItemText(m_metaTreeWnd->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectDataProcessor* newDataProcessor = dynamic_cast<CMetaObjectDataProcessor*>(
			currentMetaObject
			);
		wxASSERT(newDataProcessor);
		CMetaDataDataProcessor metadataDataProcessor(m_metaData, newDataProcessor);
		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
	else {
		wxFileDialog saveFileDialog(this, _("Open report file"), "", "",
			"report files (*.erp)|*.erp", wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeWnd->GetItemText(m_metaTreeWnd->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		CMetaObjectReport* newDataProcessor = dynamic_cast<CMetaObjectReport*>(
			currentMetaObject
			);
		wxASSERT(newDataProcessor);
		CMetaDataReport metadataDataProcessor(m_metaData, newDataProcessor);
		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
}

void CMetadataTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != eRunMode::eDESIGNER_MODE)
		return;

	IMetaObject* metaObject = GetMetaObject(m_metaTreeWnd->GetSelection());

	if (!metaObject)
		return;

	metaObject->ProcessCommand(id);
}

void CMetadataTree::PrepareReplaceMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_REPLACE, _("Replace data processor, report..."));
	menuItem->Enable(!m_bReadOnly);
	menuItem = defaultMenu->Append(ID_METATREE_SAVE, _("Save data processor, report..."));
	defaultMenu->AppendSeparator();
}

#include <wx/artprov.h>

void CMetadataTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
{
	IMetaObject* metaObject = GetMetaObject(item);

	if (metaObject
		&& !metaObject->PrepareContextMenu(defaultMenu))
	{
		if (g_metaDataProcessorCLSID == metaObject->GetClassType()
			|| g_metaReportCLSID == metaObject->GetClassType()) {
			PrepareReplaceMenu(defaultMenu);
		}

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
	else if (!metaObject && item != m_treeCOMMON) {
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
		menuItem->Enable(!m_bReadOnly);

		if (item == m_treeDATAPROCESSORS
			|| item == m_treeREPORTS) {
			defaultMenu->AppendSeparator();
			wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_INSERT, _("Insert data processor, report..."));
			menuItem->Enable(!m_bReadOnly);
		}
	}
	else if (item == m_treeMETADATA) {
		defaultMenu->AppendSeparator();
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_PROPERTY, _("Properties"));
		menuItem->SetBitmap(wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_MENU));
	}
}

void CMetadataTree::UpdateToolbar(IMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly && item != m_treeCOMMON);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != nullptr && item != m_metaTreeWnd->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_REMOVE, obj != nullptr && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != nullptr && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != nullptr && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != nullptr && item != m_metaTreeWnd->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

bool CMetadataTree::RenameMetaObject(IMetaObject* metaObject, const wxString& newName)
{
	wxTreeItemId curItem = m_metaTreeWnd->GetSelection();

	if (!curItem.IsOk()) {
		return false;
	}

	if (m_metaData->RenameMetaObject(metaObject, newName)) {
		CMetaDocument* currDocument = GetDocument(metaObject);
		if (currDocument != nullptr) {
			currDocument->SetTitle(metaObject->GetClassName() + wxT(": ") + newName);
			currDocument->OnChangeFilename(true);
		}
		m_metaTreeWnd->SetItemText(curItem, newName);
		return true;
	}
	return false;
}

#include "backend/metaCollection/partial/object.h"
#include "backend/metaCollection/table/metaTableObject.h"

void CMetadataTree::AddCatalogItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRecordDataRef* metaObjectValue = dynamic_cast<IMetaObjectRecordDataRef*>(metaObject);
	wxASSERT(metaObject);

	//Список аттрибутов 	
	wxTreeItemId hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		AppendItem(hAttributes, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = AppendGroupItem(hParentID, g_metaTableCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto metaAttribute : metaTable->GetObjectAttributes()) {
			if (metaAttribute->IsDeleted())
				continue;
			if (metaAttribute->DefaultAttribute())
				continue;
			AppendItem(hItem, metaAttribute);
		}
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

void CMetadataTree::AddDocumentItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRecordDataRef* metaObjectValue = dynamic_cast<IMetaObjectRecordDataRef*>(metaObject);
	wxASSERT(metaObject);

	//Список аттрибутов 	
	wxTreeItemId hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		AppendItem(hAttributes, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = AppendGroupItem(hParentID, g_metaTableCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto metaAttribute : metaTable->GetObjectAttributes()) {
			if (metaAttribute->IsDeleted())
				continue;
			if (metaAttribute->DefaultAttribute())
				continue;
			AppendItem(hItem, metaAttribute);
		}
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

void CMetadataTree::AddEnumerationItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRecordDataRef* metaObjectValue = dynamic_cast<IMetaObjectRecordDataRef*>(metaObject);
	wxASSERT(metaObjectValue);

	//Enumerations
	wxTreeItemId hEnums = AppendGroupItem(hParentID, g_metaEnumCLSID, objectEnumerationsName);

	for (auto metaEnumerations : metaObjectValue->GetObjectEnums()) {
		if (metaEnumerations->IsDeleted())
			continue;
		AppendItem(hEnums, metaEnumerations);
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

void CMetadataTree::AddDataProcessorItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRecordData* metaObjectValue = dynamic_cast<IMetaObjectRecordData*>(metaObject);
	wxASSERT(metaObjectValue);

	//Список аттрибутов 	
	wxTreeItemId hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		AppendItem(hAttributes, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = AppendGroupItem(hParentID, g_metaTableCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto metaAttribute : metaTable->GetObjectAttributes()) {
			if (metaAttribute->IsDeleted())
				continue;
			if (metaAttribute->DefaultAttribute())
				continue;
			AppendItem(hItem, metaAttribute);
		}
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

void CMetadataTree::AddReportItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRecordData* metaObjectValue = dynamic_cast<IMetaObjectRecordData*>(metaObject);
	wxASSERT(metaObjectValue);

	//Список аттрибутов 	
	wxTreeItemId hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		AppendItem(hAttributes, metaAttribute);
	}

	//список табличных частей 
	wxTreeItemId hTables = AppendGroupItem(hParentID, g_metaTableCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetObjectTables()) {
		if (metaTable->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto metaAttribute : metaTable->GetObjectAttributes()) {
			if (metaAttribute->IsDeleted())
				continue;
			if (metaAttribute->DefaultAttribute())
				continue;
			AppendItem(hItem, metaAttribute);
		}
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

void CMetadataTree::AddInformationRegisterItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRegisterData* metaObjectValue = dynamic_cast<IMetaObjectRegisterData*>(metaObject);
	wxASSERT(metaObjectValue);

	//Список измерений 
	wxTreeItemId hDimentions = AppendGroupItem(hParentID, g_metaDimensionCLSID, objectDimensionsName);
	for (auto metaDimension : metaObjectValue->GetObjectDimensions()) {
		if (metaDimension->IsDeleted())
			continue;
		if (metaDimension->DefaultAttribute())
			continue;
		AppendItem(hDimentions, metaDimension);
	}

	//Список ресурсов 
	wxTreeItemId hResources = AppendGroupItem(hParentID, g_metaResourceCLSID, objectResourcesName);
	for (auto metaResource : metaObjectValue->GetObjectResources()) {
		if (metaResource->IsDeleted())
			continue;
		if (metaResource->DefaultAttribute())
			continue;
		AppendItem(hResources, metaResource);
	}

	//Список аттрибутов 	
	wxTreeItemId hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		AppendItem(hAttributes, metaAttribute);
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

void CMetadataTree::AddAccumulationRegisterItem(IMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	IMetaObjectRegisterData* metaObjectValue = dynamic_cast<IMetaObjectRegisterData*>(metaObject);
	wxASSERT(metaObjectValue);

	//Список измерений 
	wxTreeItemId hDimentions = AppendGroupItem(hParentID, g_metaDimensionCLSID, objectDimensionsName);
	for (auto metaDimension : metaObjectValue->GetObjectDimensions()) {
		if (metaDimension->IsDeleted())
			continue;
		if (metaDimension->DefaultAttribute())
			continue;
		AppendItem(hDimentions, metaDimension);
	}

	//Список ресурсов 
	wxTreeItemId hResources = AppendGroupItem(hParentID, g_metaResourceCLSID, objectResourcesName);
	for (auto metaResource : metaObjectValue->GetObjectResources()) {
		if (metaResource->IsDeleted())
			continue;
		if (metaResource->DefaultAttribute())
			continue;
		AppendItem(hResources, metaResource);
	}

	//Список аттрибутов 	
	wxTreeItemId hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto metaAttribute : metaObjectValue->GetObjectAttributes()) {
		if (metaAttribute->IsDeleted())
			continue;
		if (metaAttribute->DefaultAttribute())
			continue;
		AppendItem(hAttributes, metaAttribute);
	}

	//Формы
	wxTreeItemId hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetObjectForms()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(hForm, metaForm);
	}

	//Таблицы
	wxTreeItemId hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplates : metaObjectValue->GetObjectTemplates()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(hTemplates, metaTemplates);
	}
}

#include "frontend/artProvider/artProvider.h"

void CMetadataTree::InitTree()
{
	wxImageList* imageList = m_metaTreeWnd->GetImageList();
	wxASSERT(imageList);

	m_treeMETADATA = AppendRootItem(g_metaCommonMetadataCLSID, _("configuration"));

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	int imageCommonIndex = imageList->Add(wxArtProvider::GetIcon(wxART_COMMON_FOLDER, wxART_METATREE));
	m_treeCOMMON = m_metaTreeWnd->AppendItem(m_treeMETADATA, commonName, imageCommonIndex, imageCommonIndex);

	///////////////////////////////////////////////////////////////////////////////////////////////////////

	m_treeMODULES = AppendGroupItem(m_treeCOMMON, g_metaCommonModuleCLSID, commonModulesName);
	m_treeFORMS = AppendGroupItem(m_treeCOMMON, g_metaCommonFormCLSID, commonFormsName);
	m_treeTEMPLATES = AppendGroupItem(m_treeCOMMON, g_metaCommonTemplateCLSID, commonTemplatesName);

	m_treeINTERFACES = AppendGroupItem(m_treeCOMMON, g_metaInterfaceCLSID, interfacesName);
	m_treeROLES = AppendGroupItem(m_treeCOMMON, g_metaRoleCLSID, rolesName);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	m_treeCONSTANTS = AppendGroupItem(m_treeMETADATA, g_metaConstantCLSID, constantsName);
	m_treeCATALOGS = AppendGroupItem(m_treeMETADATA, g_metaCatalogCLSID, catalogsName);
	m_treeDOCUMENTS = AppendGroupItem(m_treeMETADATA, g_metaDocumentCLSID, documentsName);
	m_treeENUMERATIONS = AppendGroupItem(m_treeMETADATA, g_metaEnumerationCLSID, enumerationsName);
	m_treeDATAPROCESSORS = AppendGroupItem(m_treeMETADATA, g_metaDataProcessorCLSID, dataProcessorName);
	m_treeREPORTS = AppendGroupItem(m_treeMETADATA, g_metaReportCLSID, reportsName);

	m_treeINFORMATION_REGISTERS = AppendGroupItem(m_treeMETADATA, g_metaInformationRegisterCLSID, informationRegisterName);
	m_treeACCUMULATION_REGISTERS = AppendGroupItem(m_treeMETADATA, g_metaAccumulationRegisterCLSID, accumulationRegisterName);

	//Set item bold and name
	m_metaTreeWnd->SetItemText(m_treeMETADATA, _("configuration"));
	m_metaTreeWnd->SetItemBold(m_treeMETADATA);
}

void CMetadataTree::ClearTree()
{
	for (auto& doc : docManager->GetDocumentsVector()) {
		CMetaDocument* metaDoc = wxDynamicCast(doc, CMetaDocument);
		IMetaObject* metaObject = metaDoc->GetMetaObject();
		if (metaObject != nullptr && this == metaObject->GetMetaDataTree()) {
			doc->DeleteAllViews();
		}
	}

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	if (m_treeMODULES.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeMODULES);
	if (m_treeFORMS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeFORMS);
	if (m_treeTEMPLATES.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeTEMPLATES);

	if (m_treeINTERFACES.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeINTERFACES);
	if (m_treeROLES.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeROLES);

	if (m_treeCONSTANTS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeCONSTANTS);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	if (m_treeCATALOGS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeCATALOGS);
	if (m_treeDOCUMENTS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeDOCUMENTS);
	if (m_treeENUMERATIONS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeENUMERATIONS);
	if (m_treeDATAPROCESSORS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeDATAPROCESSORS);
	if (m_treeREPORTS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeREPORTS);
	if (m_treeINFORMATION_REGISTERS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeINFORMATION_REGISTERS);
	if (m_treeACCUMULATION_REGISTERS.IsOk())
		m_metaTreeWnd->DeleteChildren(m_treeACCUMULATION_REGISTERS);

	//delete all items
	m_metaTreeWnd->DeleteAllItems();

	//Initialize tree
	InitTree();
}

void CMetadataTree::FillData()
{
	IMetaObject* commonMetadata = m_metaData->GetCommonMetaObject();
	wxASSERT(commonMetadata);

	m_metaTreeWnd->SetItemText(m_treeMETADATA, m_metaData->GetConfigName());
	m_metaTreeWnd->SetItemData(m_treeMETADATA, new wxTreeItemMetaData(commonMetadata));

	//****************************************************************
	//*                          CommonModules                       *
	//****************************************************************
	for (auto commonModule : m_metaData->GetMetaObject(g_metaCommonModuleCLSID)) {
		if (commonModule->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeMODULES, commonModule);
	}

	//****************************************************************
	//*                          CommonForms                         *
	//****************************************************************
	for (auto commonForm : m_metaData->GetMetaObject(g_metaCommonFormCLSID)) {
		if (commonForm->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeFORMS, commonForm);
	}

	//****************************************************************
	//*                          CommonMakets                        *
	//****************************************************************
	for (auto commonTemlate : m_metaData->GetMetaObject(g_metaCommonTemplateCLSID)) {
		if (commonTemlate->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeTEMPLATES, commonTemlate);
	}

	//****************************************************************
	//*                          Interfaces							 *
	//****************************************************************
	for (auto commonInterface : m_metaData->GetMetaObject(g_metaInterfaceCLSID)) {
		if (commonInterface->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeINTERFACES, commonInterface);
	}

	//****************************************************************
	//*                          Roles								 *
	//****************************************************************
	for (auto commonRole : m_metaData->GetMetaObject(g_metaRoleCLSID)) {
		if (commonRole->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeROLES, commonRole);
	}

	//****************************************************************
	//*                          Constants                           *
	//****************************************************************
	for (auto constant : m_metaData->GetMetaObject(g_metaConstantCLSID)) {
		if (constant->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeCONSTANTS, constant);
	}

	//****************************************************************
	//*                        Catalogs                              *
	//****************************************************************
	for (auto catalog : m_metaData->GetMetaObject(g_metaCatalogCLSID)) {
		if (catalog->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeCATALOGS, catalog);
		AddCatalogItem(catalog, hItem);
	}

	//****************************************************************
	//*                        Documents                             *
	//****************************************************************
	for (auto document : m_metaData->GetMetaObject(g_metaDocumentCLSID)) {
		if (document->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeDOCUMENTS, document);
		AddDocumentItem(document, hItem);
	}

	//****************************************************************
	//*                          Enumerations                        *
	//****************************************************************
	for (auto enumeration : m_metaData->GetMetaObject(g_metaEnumerationCLSID)) {
		if (enumeration->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeENUMERATIONS, enumeration);
		AddEnumerationItem(enumeration, hItem);
	}

	//****************************************************************
	//*                          Data processor                      *
	//****************************************************************
	for (auto dataProcessor : m_metaData->GetMetaObject(g_metaDataProcessorCLSID)) {
		if (dataProcessor->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeDATAPROCESSORS, dataProcessor);
		AddDataProcessorItem(dataProcessor, hItem);
	}

	//****************************************************************
	//*                          Report			                     *
	//****************************************************************
	for (auto report : m_metaData->GetMetaObject(g_metaReportCLSID)) {
		if (report->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeREPORTS, report);
		AddReportItem(report, hItem);
	}

	//****************************************************************
	//*                          Information register			     *
	//****************************************************************
	for (auto informationRegister : m_metaData->GetMetaObject(g_metaInformationRegisterCLSID)) {
		if (informationRegister->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeINFORMATION_REGISTERS, informationRegister);
		AddInformationRegisterItem(informationRegister, hItem);
	}

	//****************************************************************
	//*                          Accumulation register			     *
	//****************************************************************
	for (auto accumulationRegister : m_metaData->GetMetaObject(g_metaAccumulationRegisterCLSID)) {
		if (accumulationRegister->IsDeleted())
			continue;
		wxTreeItemId hItem = AppendItem(m_treeACCUMULATION_REGISTERS, accumulationRegister);
		AddAccumulationRegisterItem(accumulationRegister, hItem);
	}

	//set modify 
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(commonMetadata, m_treeMETADATA);
}

bool CMetadataTree::Load(IMetaDataConfiguration* metaData)
{
	ClearTree();
	m_metaData = metaData ? metaData : IMetaDataConfiguration::Get();
	m_metaTreeWnd->Freeze();
	FillData(); //Fill all data from metaData
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

	if (m_metaData->IsModified() && wxMessageBox("Configuration '" + m_metaData->GetConfigName() + "' has been changed. Save?", wxT("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES)
		return m_metaData->SaveConfiguration();

	return false;
}