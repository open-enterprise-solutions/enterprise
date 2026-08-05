////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"
#include "backend/fileKind.h"   // extensions live in one table, not at each call site
#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/docView/docView.h"
#include "backend/appData.h"
#include "backend/appEnv.h"
#include "backend/metaCollection/metaCommandObject.h"   // ibValueMetaObjectCommand::GetSubCommands (hub — nested commands)

#define metadataName _("Metadata")
#define commonName _("Common")

#define commonModulesName _("Common modules")
#define commonFormsName _("Common forms")
#define commonTemplatesName _("Common templates")

#define interfacesName _("Sections")
#define commandsName _("Common commands")
#define scheduledJobsName _("Scheduled jobs")
#define predefinedJobsName _("Predefined jobs")
#define sessionParametersName _("Session parameters")
#define rolesName _("Roles")
#define picturesName _("Pictures")
#define languagesName _("Languages")

#define constantsName _("Constants")

#define catalogsName _("Catalogs")
#define documentsName _("Documents")
#define enumerationsName _("Enumerations")
#define dataProcessorName _("Data processors")
#define reportsName _("Reports")
#define informationRegisterName _("Information Registers")
#define accumulationRegisterName _("Accumulation Registers")
#define chartsOfCharacteristicTypesName _("Charts of characteristic types")
#define chartsOfAccountsName _("Charts of accounts")
#define accountingRegistersName _("Accounting registers")

#define	objectFormsName _("Forms")
#define	objectModulesName _("Modules")
#define	objectTemplatesName _("Templates")
#define objectAttributesName _("Attributes")
#define objectCommandsName _("Commands")
#define objectDimensionsName _("Dimensions")
#define objectResourcesName _("Resources")

#define objectTablesName _("Tables")
#define objectEnumerationsName _("Enums")

//***********************************************************************
//*								metadata                                * 
//***********************************************************************

#include "frontend/mainFrame/mainFrame.h"
#include "frontend/win/dlgs/formSelector/formSelector.h"

ibFormID ibMetaDataTree::SelectFormType(ibValueMetaObjectForm* metaObject) const
{
	ibValueMetaObjectGenericData* parent = dynamic_cast<ibValueMetaObjectGenericData*>(metaObject->GetParent());

	ibDialogSelectTypeForm dlg(parent, metaObject);
	ibFormTypeList optList = parent->GetFormType();
	for (unsigned int idx = 0; idx < optList.GetItemCount(); idx++) {
		dlg.AppendTypeForm(optList.GetItemName(idx), optList.GetItemLabel(idx), optList.GetItemId(idx));
	}

	dlg.CreateSelector();
	return dlg.ShowModal();
}

void ibMetaDataTree::Activate()
{
	if (m_docParent == nullptr) {
		unsigned int count_doc = 0;
		for (auto doc : docManager->GetDocumentsVector()) count_doc++;
		if (count_doc <= 1) SetFocus();
	}
}

void ibMetaDataTree::Modify(bool modify)
{
	if (m_docParent != nullptr) {
		m_docParent->Modify(modify);
	}
	else {
		mainFrame->Modify(modify);
	}
}

bool ibMetaDataTree::OpenObjectForm(ibValueMetaObject* obj)
{
	ibMetaDocument* foundedDoc = GetDocument(obj);
	//not found in the list of existing ones
	if (foundedDoc == nullptr) {
		foundedDoc = docManager->OpenObjectForm(obj, m_docParent, m_bReadOnly ? ibDOC_READONLY : ibDOC_NEW);
		//So there was no suitable template!
		if (foundedDoc != nullptr)
			return true;
	}
	else {
		wxWindow* docWindow = foundedDoc->GetDocumentWindow();
		if (docWindow != nullptr) docWindow->Raise();
		return true;
	}

	return false;
}

bool ibMetaDataTree::OpenObjectForm(ibValueMetaObject* obj, ibBackendMetaDocument*& doc)
{
	ibMetaDocument* foundedDoc = GetDocument(obj);

	//not found in the list of existing ones
	if (foundedDoc == nullptr) {
		foundedDoc = docManager->OpenObjectForm(obj, m_docParent, m_bReadOnly ? ibDOC_READONLY : ibDOC_NEW);
		//So there was no suitable template!
		if (foundedDoc != nullptr) {
			doc = foundedDoc;
			return true;
		}
	}
	else {
		wxWindow* docWindow = foundedDoc->GetDocumentWindow();
		if (docWindow != nullptr) docWindow->Raise();
		doc = foundedDoc;
		return true;
	}

	return false;
}

bool ibMetaDataTree::CloseObjectForm(ibValueMetaObject* obj)
{
	ibMetaDocument* foundedDoc = GetDocument(obj);

	//not found in the list of existing ones
	if (foundedDoc != nullptr) {
		objectInspector->SelectObject(obj, this);
		if (foundedDoc->Close()) {
			// Delete the child document by deleting all its views.
			return foundedDoc->DeleteAllViews();
		}
	}

	return false;
}

ibMetaDocument* ibMetaDataTree::GetDocument(ibValueMetaObject* obj) const
{
	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr && obj == metaDoc->GetMetaObject()) {
			return metaDoc;
		}
		else if (metaDoc != nullptr) {
			for (auto& child_doc : metaDoc->GetChild()) {
				ibMetaDocument* child_metaDoc = wxDynamicCast(child_doc, ibMetaDocument);
				if (child_metaDoc != nullptr && obj == child_metaDoc->GetMetaObject()) {
					return child_metaDoc;
				}
			}
		}
	}
	return nullptr;
}

void ibMetaDataTree::EditModule(const ibGuid& moduleName, int lineNumber, bool setRunLine)
{
	ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return;

	ibValueMetaObject* metaObject = metaData->FindAnyObjectByFilter(moduleName, true);

	if (metaObject == nullptr || metaObject->IsDeleted())
		return;

	if (m_bReadOnly)
		return;

	ibMetaDocument* foundedDoc = GetDocument(metaObject);

	//not found in the list of existing ones
	if (foundedDoc == nullptr)
		foundedDoc = docManager->OpenObjectForm(metaObject, m_docParent, m_bReadOnly ? ibDOC_READONLY : ibDOC_NEW);

	ibValueModuleDocument* moduleDoc = static_cast<ibValueModuleDocument*>(foundedDoc);
	if (moduleDoc != nullptr) moduleDoc->SetCurrentLine(lineNumber, setRunLine);
}

//***********************************************************************
//*								 metaData                               * 
//***********************************************************************

void ibMetadataTree::ActivateItem(const wxTreeItemId& item)
{
	ibValueMetaObject* currObject = GetMetaObject(item);
	if (currObject == nullptr)
		return;

	OpenObjectForm(currObject);
}

ibValueMetaObject* ibMetadataTree::NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject)
{
	return m_metaData->CreateMetaObject(clsid, parent, runObject);
}

ibValueMetaObject* ibMetadataTree::CreateItem(bool showValue)
{
	const wxTreeItemId& item = GetSelectionIdentifier();
	if (!item.IsOk()) return nullptr;

	Freeze();

	ibValueMetaObject* createdObject = NewItem(
		GetClassIdentifier(),
		GetMetaIdentifier()
	);

	if (createdObject != nullptr) {

		ibPropertyObject* oldSelection = objectInspector->GetSelectedObject();
		if (showValue) { OpenObjectForm(createdObject); }
		UpdateToolbar(createdObject,
			FillItem(createdObject, item, oldSelection == objectInspector->GetSelectedObject(), false));

		// Notify every open editor that the config gained an object — a form editor's command navigator (and any
		// other config-wide surface) re-gathers, so a just-added global / object command shows up live. The delete
		// path already does this; the add path did not — hence a new command stayed invisible on an open form.
		for (auto& doc : docManager->GetDocumentsVector()) {
			ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
			if (metaDoc != nullptr) metaDoc->UpdateAllViews();
		}
	}

	Thaw();

	m_metaTreeCtrl->RefreshSelectedItem();
	return createdObject;
}

wxTreeItemId ibMetadataTree::FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select, bool scroll)
{
	m_metaTreeCtrl->Freeze();

	wxTreeItemId createdItem = nullptr;
	if (metaItem->GetClassType() == g_metaTableCLSID || metaItem->GetClassType() == g_metaTableRefCLSID) {
		createdItem = AppendGroupItem(item, g_metaAttributeCLSID, metaItem);
	}
	else if (metaItem->GetClassType() == g_metaSectionCLSID) {
		createdItem = AppendGroupItem(item, g_metaSectionCLSID, metaItem);
	}
	else {
		createdItem = AppendItem(item, metaItem);
	}

	//Advanced mode
	if (metaItem->GetClassType() == g_metaCatalogCLSID) AddCatalogItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaDocumentCLSID) AddDocumentItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaEnumerationCLSID) AddEnumerationItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaDataProcessorCLSID) AddDataProcessorItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaReportCLSID) AddReportItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaInformationRegisterCLSID) AddInformationRegisterItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaAccumulationRegisterCLSID) AddAccumulationRegisterItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaParameterizedJobCLSID) AddCatalogItem(metaItem, createdItem);   // a job row IS a catalog entry with a second verb
	else if (metaItem->GetClassType() == g_metaChartOfCharacteristicTypesCLSID) AddCatalogItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaChartOfAccountsCLSID) AddCatalogItem(metaItem, createdItem);
	else if (metaItem->GetClassType() == g_metaAccountingRegisterCLSID) AddAccumulationRegisterItem(metaItem, createdItem);

	else if (metaItem->GetClassType() == g_metaTableCLSID || metaItem->GetClassType() == g_metaTableRefCLSID) {

		ibValueMetaObjectTableData* metaItemRecord = metaItem->ConvertToType<ibValueMetaObjectTableData>();
		wxASSERT(metaItemRecord);

		for (auto attribute : metaItemRecord->GetAttributeArrayObject()) {
			if (attribute->IsDeleted())
				continue;
			if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
				continue;
			AppendItem(createdItem, attribute);
		}
	}
	else if (metaItem->GetClassType() == g_metaSectionCLSID) {
		ibValueMetaObjectSection* metaItemRecord = metaItem->ConvertToType<ibValueMetaObjectSection>();
		for (auto object : metaItemRecord->GetInterfaceArrayObject()) {
			if (object->IsDeleted())
				continue;
			AppendItem(createdItem, object);
		}
		for (auto metaCommand : metaItemRecord->GetCommandArrayObject())   // a section owns its own commands
			AppendCommandNode(createdItem, metaCommand);
	}

	m_metaTreeCtrl->InvalidateBestSize();
	m_metaTreeCtrl->SetEvtHandlerEnabled(select);
	m_metaTreeCtrl->SelectItem(createdItem);
	m_metaTreeCtrl->SetEvtHandlerEnabled(true);
	m_metaTreeCtrl->Expand(createdItem);

	m_metaTreeCtrl->Thaw();

	if (scroll)
		m_metaTreeCtrl->ScrollTo(createdItem);

	return createdItem;
}

// HUB — append a command node and, recursively, its sub-commands (a group command holds commands, shown nested,
// exactly as a subsystem holds subsystems). Skips deleted. The clsid gate makes the cast type-safe.
void ibMetadataTree::AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command)
{
	if (command == nullptr || command->IsDeleted())
		return;
	const wxTreeItemId hCmd = AppendItem(parent, command);
	if (command->GetClassType() == g_metaCommonCommandCLSID || command->GetClassType() == g_metaCommandCLSID)
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(command)->GetSubCommands())
			AppendCommandNode(hCmd, sub);
}

void ibMetadataTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (!selection.IsOk())
		return;
	ibValueMetaObject* m_currObject = GetMetaObject(selection);
	if (!m_currObject)
		return;

	OpenObjectForm(m_currObject);
}

void ibMetadataTree::RemoveItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();

	if (!selection.IsOk())
		return;

	wxTreeItemIdValue m_cookie;
	wxTreeItemId hItem = m_metaTreeCtrl->GetFirstChild(selection, m_cookie);

	while (hItem) {
		EraseItem(hItem);
		hItem = m_metaTreeCtrl->GetNextChild(hItem, m_cookie);
	}

	ibValueMetaObject* metaObject = GetMetaObject(selection);
	wxASSERT(metaObject);
	EraseItem(selection);
	m_metaData->RemoveMetaObject(metaObject);

	//Delete item from tree
	m_metaTreeCtrl->Delete(selection);

	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr) metaDoc->UpdateAllViews();
	}

	const wxTreeItemId nextSelection = m_metaTreeCtrl->GetFocusedItem();
	if (nextSelection.IsOk()) {
		UpdateToolbar(GetMetaObject(nextSelection), nextSelection);
	}
}

void ibMetadataTree::EraseItem(const wxTreeItemId& item)
{
	ibValueMetaObject* const metaObject = GetMetaObject(item);
	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr && metaObject != nullptr && metaObject == metaDoc->GetMetaObject()) {
			metaDoc->DeleteAllViews();
		}
	}
}

void ibMetadataTree::SelectItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);

	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	objectInspector->SelectObject(metaObject);
}

void ibMetadataTree::PropertyItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);

	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();

	objectInspector->SelectObject(metaObject);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetadataTree::Collapse()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = false;
}

void ibMetadataTree::Expand()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetadataTree::UpItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	m_metaTreeCtrl->Freeze();

	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	const wxTreeItemId& nextItem = m_metaTreeCtrl->GetPrevSibling(selection);
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != nullptr && nextItem.IsOk()) {
		const wxTreeItemId& parentItem = m_metaTreeCtrl->GetItemParent(nextItem);
		wxTreeItemIdValue coockie; wxTreeItemId nextId = m_metaTreeCtrl->GetFirstChild(parentItem, coockie);
		size_t pos = 0;
		do {
			if (nextId == nextItem)
				break;
			nextId = m_metaTreeCtrl->GetNextChild(nextId, coockie); pos++;
		} while (nextId.IsOk());
		ibValueMetaObject* parentObject = metaObject->GetParent();
		ibValueMetaObject* nextObject = GetMetaObject(nextItem);
		if (parentObject->ChangeChildPosition(metaObject, parentObject->GetChildPosition(nextObject))) {
			wxTreeItemId newId = m_metaTreeCtrl->InsertItem(parentItem,
				pos + 2,
				m_metaTreeCtrl->GetItemText(nextItem),
				m_metaTreeCtrl->GetItemImage(nextItem),
				m_metaTreeCtrl->GetItemImage(nextItem),
				m_metaTreeCtrl->GetItemData(nextItem)
			);

			auto tree = m_metaTreeCtrl;
			std::function<void(ibMetaTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibMetaTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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

			m_metaTreeCtrl->SetItemData(nextItem, nullptr);
			m_metaTreeCtrl->Delete(nextItem);

			//m_metaTreeCtrl->Expand(newId);
		}
	}

	m_metaTreeCtrl->Thaw();
}

void ibMetadataTree::DownItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	m_metaTreeCtrl->Freeze();

	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	const wxTreeItemId& prevItem = m_metaTreeCtrl->GetNextSibling(selection);
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != nullptr && prevItem.IsOk()) {
		const wxTreeItemId& parentItem = m_metaTreeCtrl->GetItemParent(prevItem);
		wxTreeItemIdValue coockie; wxTreeItemId nextId = m_metaTreeCtrl->GetFirstChild(parentItem, coockie);
		size_t pos = 0;
		do {
			if (nextId == prevItem)
				break;
			nextId = m_metaTreeCtrl->GetNextChild(nextId, coockie); pos++;
		} while (nextId.IsOk());
		ibValueMetaObject* parentObject = metaObject->GetParent();
		ibValueMetaObject* prevObject = GetMetaObject(prevItem);
		if (parentObject->ChangeChildPosition(metaObject, parentObject->GetChildPosition(prevObject))) {
			wxTreeItemId newId = m_metaTreeCtrl->InsertItem(parentItem,
				pos - 1,
				m_metaTreeCtrl->GetItemText(prevItem),
				m_metaTreeCtrl->GetItemImage(prevItem),
				m_metaTreeCtrl->GetItemImage(prevItem),
				m_metaTreeCtrl->GetItemData(prevItem)
			);

			auto tree = m_metaTreeCtrl;
			std::function<void(ibMetaTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibMetaTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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

			m_metaTreeCtrl->SetItemData(prevItem, nullptr);
			m_metaTreeCtrl->Delete(prevItem);

			//m_metaTreeCtrl->Expand(newId);
		}
	}

	m_metaTreeCtrl->Thaw();
}

void ibMetadataTree::SortItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;
	m_metaTreeCtrl->Freeze();
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* prevObject = GetMetaObject(selection);
	if (prevObject != nullptr && selection.IsOk()) {
		const wxTreeItemId& parentItem =
			m_metaTreeCtrl->GetItemParent(selection);
		if (parentItem.IsOk()) {
			m_metaTreeCtrl->SortChildren(parentItem);
		}
	}
	m_metaTreeCtrl->Thaw();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/metadataDataProcessor.h"
#include "backend/metadataReport.h"

void ibMetadataTree::InsertItem()
{
	ibValueMetaObject* commonMetaObject = m_metaData->GetCommonMetaObject(); wxTreeItemId hSelItem = m_metaTreeCtrl->GetSelection();

	if (hSelItem == m_treeDATAPROCESSORS) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			ibFileFilter(ibFileKind::Tool), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		//create main metaObject
		ibMetaDataDataProcessor metadataDataProcessor(m_metaData);

		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectDataProcessor* dataProcessor = metadataDataProcessor.GetDataProcessor();
			wxASSERT(dataProcessor);
			const wxTreeItemId& createdItem = AppendItem(hSelItem, dataProcessor);
			AddDataProcessorItem(dataProcessor, createdItem);
			m_metaTreeCtrl->SelectItem(createdItem);
			dataProcessor->IncrRef();
			m_metaTreeCtrl->Thaw();
		}
	}
	else {
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			ibFileFilter(ibFileKind::Report), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibMetaDataReport metadataReport(m_metaData);

		if (metadataReport.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectReport* report = metadataReport.GetReport();
			wxASSERT(report);
			const wxTreeItemId& createdItem = AppendItem(hSelItem, report);
			AddReportItem(report, createdItem);
			m_metaTreeCtrl->SelectItem(createdItem);
			report->IncrRef();
			m_metaTreeCtrl->Thaw();
		}
	}

	m_metaData->Modify(true);
}

void ibMetadataTree::ReplaceItem()
{
	wxTreeItemId hSelItem = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* currentMetaObject = GetMetaObject(m_metaTreeCtrl->GetSelection());

	if (currentMetaObject->GetClassType() == g_metaDataProcessorCLSID) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			ibFileFilter(ibFileKind::Tool), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibMetaDataDataProcessor metadataDataProcessor(m_metaData);
		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectDataProcessor* metaObject = metadataDataProcessor.GetDataProcessor();
			wxTreeItemData* itemData = m_metaTreeCtrl->GetItemData(hSelItem);
			if (itemData != nullptr) {
				ibTreeDataMetaItem* metaItem = dynamic_cast<ibTreeDataMetaItem*>(itemData);
				if (metaItem != nullptr)
					metaItem->m_metaObject = metaObject;
			}
			m_metaData->RemoveMetaObject(currentMetaObject);
			m_metaTreeCtrl->SetItemText(hSelItem, metaObject->GetName());
			m_metaTreeCtrl->DeleteChildren(hSelItem);
			AddDataProcessorItem(metaObject, hSelItem);
			m_metaTreeCtrl->Thaw();
		}
	}
	else
	{
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			ibFileFilter(ibFileKind::Report), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibValueMetaObjectReport* newReport = dynamic_cast<ibValueMetaObjectReport*>(
			currentMetaObject
			);

		wxASSERT(newReport);

		ibMetaDataReport metadataDataProcessor(m_metaData);
		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectReport* metaObject = metadataDataProcessor.GetReport();
			wxTreeItemData* itemData = m_metaTreeCtrl->GetItemData(hSelItem);
			if (itemData != nullptr) {
				ibTreeDataMetaItem* metaItem = dynamic_cast<ibTreeDataMetaItem*>(itemData);
				if (metaItem != nullptr)
					metaItem->m_metaObject = metaObject;
			}
			m_metaData->RemoveMetaObject(currentMetaObject);
			m_metaTreeCtrl->SetItemText(hSelItem, newReport->GetName());
			m_metaTreeCtrl->DeleteChildren(hSelItem);
			AddReportItem(newReport, hSelItem);
			m_metaTreeCtrl->Thaw();
		}
	}

	m_metaData->Modify(true);
}

void ibMetadataTree::SaveItem()
{
	ibValueMetaObject* currentMetaObject = GetMetaObject(m_metaTreeCtrl->GetSelection());

	if (currentMetaObject->GetClassType() == g_metaDataProcessorCLSID) {

		wxFileDialog saveFileDialog(this, _("Open data processor file"), "", "",
			ibFileFilter(ibFileKind::Tool), wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeCtrl->GetItemText(m_metaTreeCtrl->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibValueMetaObjectDataProcessor* newDataProcessor = dynamic_cast<ibValueMetaObjectDataProcessor*>(
			currentMetaObject
			);
		wxASSERT(newDataProcessor);
		ibMetaDataDataProcessor metadataDataProcessor(m_metaData, newDataProcessor);
		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
	else {
		wxFileDialog saveFileDialog(this, _("Open report file"), "", "",
			ibFileFilter(ibFileKind::Report), wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeCtrl->GetItemText(m_metaTreeCtrl->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibValueMetaObjectReport* newDataProcessor = dynamic_cast<ibValueMetaObjectReport*>(
			currentMetaObject
			);
		wxASSERT(newDataProcessor);
		ibMetaDataReport metadataDataProcessor(m_metaData, newDataProcessor);
		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
}

void ibMetadataTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	ibValueMetaObject* metaObject = GetMetaObject(m_metaTreeCtrl->GetSelection());

	if (!metaObject)
		return;

	metaObject->ProcessCommand(id);
}

void ibMetadataTree::PrepareReplaceMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_REPLACE, _("Replace data processor, report..."));
	menuItem->Enable(!m_bReadOnly);
	menuItem = defaultMenu->Append(ID_METATREE_SAVE, _("Save data processor, report..."));
	defaultMenu->AppendSeparator();
}

#include "frontend/artProvider/artProvider.h"

void ibMetadataTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
{
	ibValueMetaObject* metaObject = GetMetaObject(item);

	if (metaObject
		&& !metaObject->PrepareContextMenu(defaultMenu))
	{
		if (g_metaDataProcessorCLSID == metaObject->GetClassType()
			|| g_metaReportCLSID == metaObject->GetClassType()) {
			PrepareReplaceMenu(defaultMenu);
		}

		wxMenuItem* menuItem = nullptr;

		menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picAddCLSID));
		menuItem->Enable(!m_bReadOnly);
		menuItem = defaultMenu->Append(ID_METATREE_EDIT, _("Edit"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picEditCLSID));
		menuItem = defaultMenu->Append(ID_METATREE_DELETE, _("Remove"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picDeleteCLSID));
		menuItem->Enable(!m_bReadOnly);
		defaultMenu->AppendSeparator();
		menuItem = defaultMenu->Append(ID_METATREE_PROPERTY, _("Properties"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
	}
	else if (!metaObject && item != m_treeCOMMON) {
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picAddCLSID));
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
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
	}
}

void ibMetadataTree::ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos)
{
	wxMenu innerMenu;   // stack — PopupMenu does not take ownership, and it blocks until dismissed
	PrepareContextMenu(&innerMenu, item);

	// Collect IDs of custom (non-standard) menu items
	std::vector<int> boundIds;
	for (auto def_menu : innerMenu.GetMenuItems())
	{
		const int id = def_menu->GetId();
		if (id == ID_METATREE_NEW
			|| id == ID_METATREE_EDIT
			|| id == ID_METATREE_DELETE
			|| id == ID_METATREE_PROPERTY
			|| id == ID_METATREE_INSERT
			|| id == ID_METATREE_REPLACE
			|| id == ID_METATREE_SAVE
			|| id == wxID_SEPARATOR)
		{
			continue;
		}
		m_metaTreeCtrl->Bind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnCommandItem, m_metaTreeCtrl, id);
		boundIds.push_back(id);
	}

	m_metaTreeCtrl->PopupMenu(&innerMenu, m_metaTreeCtrl->ScreenToClient(eventSrc->ClientToScreen(pos)));

	for (int id : boundIds) {
		m_metaTreeCtrl->Unbind(wxEVT_MENU, &ibMetadataTree::ibMetaTreeCtrl::OnCommandItem, m_metaTreeCtrl, id);
	}
}

void ibMetadataTree::UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly && item != m_treeCOMMON);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

bool ibMetadataTree::RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName)
{
	wxTreeItemId curItem = m_metaTreeCtrl->GetSelection();

	if (!curItem.IsOk()) {
		return false;
	}

	if (m_metaData->RenameMetaObject(metaObject, newName)) {
		ibMetaDocument* currDocument = GetDocument(metaObject);
		if (currDocument != nullptr) {
			currDocument->SetTitle(metaObject->GetClassName() + wxT(": ") + newName);
			currDocument->OnChangeFilename(true);
		}
		m_metaTreeCtrl->SetItemText(curItem, newName);
		return true;
	}
	return false;
}

#include "backend/metaCollection/partial/commonObject.h"

void ibMetadataTree::AddInterfaceItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectSection* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectSection>();
	wxASSERT(metaObject);

	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {

		if (commonInterface->IsDeleted())
			continue;

		//const wxString& strName = commonInterface->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AddInterfaceItem(commonInterface,
			AppendGroupItem(hParentID, g_metaSectionCLSID, commonInterface));
	}

	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())   // a section owns its own commands
		AppendCommandNode(hParentID, metaCommand);
}

void ibMetadataTree::AddCatalogItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataRef>();
	wxASSERT(metaObject);

	// attribute list
	const wxTreeItemId& hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto attribute : metaObjectValue->GetAttributeArrayObject()) {

		if (attribute->IsDeleted())
			continue;

		if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = attribute->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hAttributes, attribute);
	}

	// tabular section list
	// tabular sections group. A catalog / document is ALWAYS a reference, so its table is the
	// DB-backed MD_TBLR; processors / reports are RAM, so MD_TBL. Set explicitly per object kind.
	const wxTreeItemId& hTables = AppendGroupItem(hParentID, g_metaTableRefCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetTableArrayObject()) {

		if (metaTable->IsDeleted())
			continue;

		//const wxString strName = metaTable->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		const wxTreeItemId& hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);

		for (auto attribute : metaTable->GetAttributeArrayObject()) {

			if (attribute->IsDeleted())
				continue;

			if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
				continue;

			//const wxString strName = attribute->GetName();

			//if (!m_strSearch.IsEmpty()
			//	&& strName.Find(m_strSearch) < 0)
			//	continue;

			AppendItem(hItem, attribute);
		}
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

void ibMetadataTree::AddDocumentItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataRef>();

	wxASSERT(metaObject);

	// attribute list
	const wxTreeItemId& hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto attribute : metaObjectValue->GetAttributeArrayObject()) {

		if (attribute->IsDeleted())
			continue;

		if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = attribute->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hAttributes, attribute);
	}

	// tabular section list
	// tabular sections group. A catalog / document is ALWAYS a reference, so its table is the
	// DB-backed MD_TBLR; processors / reports are RAM, so MD_TBL. Set explicitly per object kind.
	const wxTreeItemId& hTables = AppendGroupItem(hParentID, g_metaTableRefCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetTableArrayObject()) {

		if (metaTable->IsDeleted())
			continue;

		//const wxString strName = metaTable->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		const wxTreeItemId& hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {

			if (attribute->IsDeleted())
				continue;

			if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
				continue;

			//const wxString strName = attribute->GetName();

			//if (!m_strSearch.IsEmpty()
			//	&& strName.Find(m_strSearch) < 0)
			//	continue;

			AppendItem(hItem, attribute);
		}
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

void ibMetadataTree::AddEnumerationItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataEnumRef* metaObjectValue = metaObject->ConvertToType <ibValueMetaObjectRecordDataEnumRef>();
	wxASSERT(metaObjectValue);

	//Enumerations
	wxTreeItemId hEnums = AppendGroupItem(hParentID, g_metaEnumCLSID, objectEnumerationsName);

	for (auto metaEnumerations : metaObjectValue->GetEnumObjectArray()) {

		if (metaEnumerations->IsDeleted())
			continue;

		//const wxString strName = metaEnumerations->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hEnums, metaEnumerations);
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

void ibMetadataTree::AddDataProcessorItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordData* metaObjectValue = metaObject->ConvertToType <ibValueMetaObjectRecordData>();
	wxASSERT(metaObjectValue);

	// attribute list
	const wxTreeItemId& hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto attribute : metaObjectValue->GetAttributeArrayObject()) {

		if (attribute->IsDeleted())
			continue;

		if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = attribute->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hAttributes, attribute);
	}

	// tabular section list
	// tabular sections group. A catalog / document is ALWAYS a reference, so its table is the
	// DB-backed MD_TBLR; processors / reports are RAM, so MD_TBL. Set explicitly per object kind.
	const wxTreeItemId& hTables = AppendGroupItem(hParentID, g_metaTableCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetTableArrayObject()) {

		if (metaTable->IsDeleted())
			continue;

		//const wxString strName = metaTable->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		const wxTreeItemId& hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {

			if (attribute->IsDeleted())
				continue;

			if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
				continue;

			//const wxString strName = attribute->GetName();

			//if (!m_strSearch.IsEmpty()
			//	&& strName.Find(m_strSearch) < 0)
			//	continue;

			AppendItem(hItem, attribute);
		}
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

void ibMetadataTree::AddReportItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordData>();
	wxASSERT(metaObjectValue);

	// attribute list
	const wxTreeItemId& hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto attribute : metaObjectValue->GetAttributeArrayObject()) {

		if (attribute->IsDeleted())
			continue;

		if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = attribute->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hAttributes, attribute);
	}

	// tabular section list
	// tabular sections group. A catalog / document is ALWAYS a reference, so its table is the
	// DB-backed MD_TBLR; processors / reports are RAM, so MD_TBL. Set explicitly per object kind.
	const wxTreeItemId& hTables = AppendGroupItem(hParentID, g_metaTableCLSID, objectTablesName);
	for (auto metaTable : metaObjectValue->GetTableArrayObject()) {
		if (metaTable->IsDeleted())
			continue;

		//const wxString strName = metaTable->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		const wxTreeItemId& hItem = AppendGroupItem(hTables, g_metaAttributeCLSID, metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {

			if (attribute->IsDeleted())
				continue;

			if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
				continue;

			//const wxString strName = attribute->GetName();

			//if (!m_strSearch.IsEmpty()
			//	&& strName.Find(m_strSearch) < 0)
			//	continue;

			AppendItem(hItem, attribute);
		}
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

void ibMetadataTree::AddInformationRegisterItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRegisterData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRegisterData>();
	wxASSERT(metaObjectValue);

	// dimension list
	const wxTreeItemId& hDimensions = AppendGroupItem(hParentID, g_metaDimensionCLSID, objectDimensionsName);
	for (auto metaDimension : metaObjectValue->GetDimensionArrayObject()) {

		if (metaDimension->IsDeleted())
			continue;

		if (metaDimension->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = metaDimension->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hDimensions, metaDimension);
	}

	// resource list
	const wxTreeItemId& hResources = AppendGroupItem(hParentID, g_metaResourceCLSID, objectResourcesName);
	for (auto metaResource : metaObjectValue->GetResourceArrayObject()) {

		if (metaResource->IsDeleted())
			continue;

		if (metaResource->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = metaResource->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hResources, metaResource);
	}

	// attribute list
	const wxTreeItemId& hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto attribute : metaObjectValue->GetAttributeArrayObject()) {

		if (attribute->IsDeleted())
			continue;

		if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = attribute->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hAttributes, attribute);
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

void ibMetadataTree::AddAccumulationRegisterItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRegisterData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRegisterData>();
	wxASSERT(metaObjectValue);

	// dimension list
	const wxTreeItemId& hDimensions = AppendGroupItem(hParentID, g_metaDimensionCLSID, objectDimensionsName);
	for (auto metaDimension : metaObjectValue->GetDimensionArrayObject()) {

		if (metaDimension->IsDeleted())
			continue;

		if (metaDimension->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = metaDimension->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hDimensions, metaDimension);
	}

	// resource list
	const wxTreeItemId& hResources = AppendGroupItem(hParentID, g_metaResourceCLSID, objectResourcesName);
	for (auto metaResource : metaObjectValue->GetResourceArrayObject()) {

		if (metaResource->IsDeleted())
			continue;

		if (metaResource->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = metaResource->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hResources, metaResource);
	}

	// attribute list
	const wxTreeItemId& hAttributes = AppendGroupItem(hParentID, g_metaAttributeCLSID, objectAttributesName);
	for (auto attribute : metaObjectValue->GetAttributeArrayObject()) {

		if (attribute->IsDeleted())
			continue;

		if (attribute->GetClassType() == g_metaPredefinedAttributeCLSID)
			continue;

		//const wxString strName = attribute->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hAttributes, attribute);
	}

	// forms
	const wxTreeItemId& hForm = AppendGroupItem(hParentID, g_metaFormCLSID, objectFormsName);
	for (auto metaForm : metaObjectValue->GetFormArrayObject()) {

		if (metaForm->IsDeleted())
			continue;

		//const wxString strName = metaForm->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hForm, metaForm);
	}

	// commands (object scope) — each business object owns its own commands
	const wxTreeItemId& hCommands = AppendGroupItem(hParentID, g_metaCommandCLSID, objectCommandsName);
	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())
		AppendCommandNode(hCommands, metaCommand);   // hub — nests sub-commands, skips deleted

	// tables
	const wxTreeItemId& hTemplates = AppendGroupItem(hParentID, g_metaTemplateCLSID, objectTemplatesName);
	for (auto metaTemplate : metaObjectValue->GetTemplateArrayObject()) {

		if (metaTemplate->IsDeleted())
			continue;

		//const wxString strName = metaTemplate->GetName();

		//if (!m_strSearch.IsEmpty()
		//	&& strName.Find(m_strSearch) < 0)
		//	continue;

		AppendItem(hTemplates, metaTemplate);
	}
}

#include "frontend/artProvider/artProvider.h"

void ibMetadataTree::InitTree()
{
	wxImageList* imageList = m_metaTreeCtrl->GetImageList();
	wxASSERT(imageList);

	m_treeMETADATA = AppendRootItem(g_metaCommonMetadataCLSID, _("Configuration"));

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	const int imageCommonIndex = imageList->Add(wxArtProvider::GetBitmapBundle(wxART_COMMON_FOLDER, wxART_METATREE).GetBitmap(wxDefaultSize));
	m_treeCOMMON = m_metaTreeCtrl->AppendItem(m_treeMETADATA, commonName, imageCommonIndex, imageCommonIndex);

	///////////////////////////////////////////////////////////////////////////////////////////////////////

	m_treeMODULES = AppendGroupItem(m_treeCOMMON, g_metaCommonModuleCLSID, commonModulesName);
	m_treeFORMS = AppendGroupItem(m_treeCOMMON, g_metaCommonFormCLSID, commonFormsName);
	m_treeCOMMANDS = AppendGroupItem(m_treeCOMMON, g_metaCommonCommandCLSID, commandsName);

	m_treeTEMPLATES = AppendGroupItem(m_treeCOMMON, g_metaCommonTemplateCLSID, commonTemplatesName);

	// SCHEDULED JOBS stay under COMMON — unattended work belongs to the configuration as a whole,
	// not to any one business object, which is exactly what this umbrella means. One branch, two
	// kinds inside it: the branch itself holds the PARAMETERIZED jobs (it is their metatype's group
	// node, so File → New reaches them the usual way), and the PREDEFINED ones live in a sub-branch
	// declared FIRST, which is what puts them above — a configuration declares a handful of those
	// and they never multiply with the data, while the parameterized list is the one that grows.
	m_treeJOBS = AppendGroupItem(m_treeCOMMON, g_metaParameterizedJobCLSID, scheduledJobsName);
	m_treeSCHEDULED_JOBS = AppendGroupItem(m_treeJOBS, g_metaScheduledJobCLSID, predefinedJobsName);

	// SESSION PARAMETERS sit beside the jobs, under COMMON, for the same reason those do: they
	// belong to the configuration as a whole and to no business object. Each one is an ATTRIBUTE
	// whose owner is the session — declared here, set once by the session module, read everywhere.
	m_treeSESSION_PARAMETERS = AppendGroupItem(m_treeCOMMON, g_metaSessionParameterCLSID, sessionParametersName);

	m_treePICTURES = AppendGroupItem(m_treeCOMMON, g_metaPictureCLSID, picturesName);

	// Sections come AFTER the common items — a top-level navigation grouping, not a common asset.
	m_treeINTERFACES = AppendGroupItem(m_treeCOMMON, g_metaSectionCLSID, interfacesName);

	m_treeROLES = AppendGroupItem(m_treeCOMMON, g_metaRoleCLSID, rolesName);

	m_treeLANGUAGES = AppendGroupItem(m_treeCOMMON, g_metaLanguageCLSID, languagesName);

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
	m_treeCHARTS_OF_CHARACTERISTIC_TYPES = AppendGroupItem(m_treeMETADATA, g_metaChartOfCharacteristicTypesCLSID, chartsOfCharacteristicTypesName);
	m_treeCHARTS_OF_ACCOUNTS = AppendGroupItem(m_treeMETADATA, g_metaChartOfAccountsCLSID, chartsOfAccountsName);
	m_treeACCOUNTING_REGISTERS = AppendGroupItem(m_treeMETADATA, g_metaAccountingRegisterCLSID, accountingRegistersName);

	//Set item bold and name
	m_metaTreeCtrl->SetItemText(m_treeMETADATA, _("Configuration"));
	m_metaTreeCtrl->SetItemBold(m_treeMETADATA);
}

void ibMetadataTree::ActivateTree()
{
	if (m_metaData != nullptr)
		objectInspector->SelectObject(GetMetaObject(m_metaTreeCtrl->GetSelection()));
}

void ibMetadataTree::ClearTree()
{
	for (auto& doc : docManager->GetDocumentsVector()) {
		// docManager->GetDocumentsVector() now mixes ibMetaDocument
		// instances (Catalog/Document/Form editors) with plain ibDocument
		// (AuditLog, Text, Help) after step-4b decoupling. Skip non-meta
		// docs — they have no metaobject to compare against this tree.
		const ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc == nullptr) continue;
		const ibValueMetaObject* metaObject = metaDoc->GetMetaObject();
		if (metaObject != nullptr && this == metaObject->GetMetaDataTree()) {
			doc->DeleteAllViews();
		}
	}

	//disable event
	m_metaTreeCtrl->SetEvtHandlerEnabled(false);

	//*****************************************************************************************************
	//*                                      Common objects                                               *
	//*****************************************************************************************************

	if (m_treeMODULES.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeMODULES);
	if (m_treeFORMS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeFORMS);
	if (m_treeTEMPLATES.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeTEMPLATES);

	if (m_treeINTERFACES.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeINTERFACES);
	if (m_treeCOMMANDS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeCOMMANDS);
	// The jobs branch holds the predefined sub-branch, so clearing the branch clears both —
	// InitTree re-creates the pair.
	if (m_treeJOBS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeJOBS);
	if (m_treeROLES.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeROLES);
	if (m_treePICTURES.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treePICTURES);

	if (m_treeCONSTANTS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeCONSTANTS);

	//*****************************************************************************************************
	//*                                      Custom objects                                               *
	//*****************************************************************************************************

	if (m_treeCATALOGS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeCATALOGS);
	if (m_treeDOCUMENTS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeDOCUMENTS);
	if (m_treeENUMERATIONS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeENUMERATIONS);
	if (m_treeDATAPROCESSORS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeDATAPROCESSORS);
	if (m_treeREPORTS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeREPORTS);
	if (m_treeINFORMATION_REGISTERS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeINFORMATION_REGISTERS);
	if (m_treeACCUMULATION_REGISTERS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeACCUMULATION_REGISTERS);
	if (m_treeCHARTS_OF_CHARACTERISTIC_TYPES.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeCHARTS_OF_CHARACTERISTIC_TYPES);
	if (m_treeCHARTS_OF_ACCOUNTS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeCHARTS_OF_ACCOUNTS);
	if (m_treeACCOUNTING_REGISTERS.IsOk())
		m_metaTreeCtrl->DeleteChildren(m_treeACCOUNTING_REGISTERS);

	//delete all items
	m_metaTreeCtrl->DeleteAllItems();

	//initialize tree
	InitTree();

	//enable event 
	m_metaTreeCtrl->SetEvtHandlerEnabled(true);
}

void ibMetadataTree::FillData()
{
	ibValueMetaObject* commonMetadata = m_metaData->GetCommonMetaObject();
	wxASSERT(commonMetadata);

	m_metaTreeCtrl->SetItemText(m_treeMETADATA, m_metaData->GetConfigName());
	m_metaTreeCtrl->SetItemData(m_treeMETADATA, new wxTreeItemMetaData(commonMetadata));

	//****************************************************************
	//*                          CommonModules                       *
	//****************************************************************
	for (auto commonModule : m_metaData->GetAnyArrayObject(g_metaCommonModuleCLSID)) {

		if (commonModule->IsDeleted())
			continue;

		const wxString& strName = commonModule->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeMODULES, commonModule);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeMODULES))
		m_metaTreeCtrl->Delete(m_treeMODULES);

	//****************************************************************
	//*                          CommonForms                         *
	//****************************************************************
	for (auto commonForm : m_metaData->GetAnyArrayObject(g_metaCommonFormCLSID)) {

		if (commonForm->IsDeleted())
			continue;

		const wxString& strName = commonForm->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeFORMS, commonForm);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeFORMS))
		m_metaTreeCtrl->Delete(m_treeFORMS);

	//****************************************************************
	//*                          CommonMakets                        *
	//****************************************************************
	for (auto commonTemlate : m_metaData->GetAnyArrayObject(g_metaCommonTemplateCLSID)) {

		if (commonTemlate->IsDeleted())
			continue;

		const wxString& strName = commonTemlate->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeTEMPLATES, commonTemlate);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeTEMPLATES))
		m_metaTreeCtrl->Delete(m_treeTEMPLATES);

	//****************************************************************
	//*         Scheduled jobs — predefined first, then the rows      *
	//****************************************************************
	for (auto scheduledJob : m_metaData->GetAnyArrayObject(g_metaScheduledJobCLSID)) {

		if (scheduledJob->IsDeleted())
			continue;

		const wxString& strName = scheduledJob->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeSCHEDULED_JOBS, scheduledJob);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeSCHEDULED_JOBS))
		m_metaTreeCtrl->Delete(m_treeSCHEDULED_JOBS);

	// The parameterized ones are ordinary reference objects, so they unfold exactly like a catalog
	// — attributes, tabular sections, forms, commands, templates — through the same AddCatalogItem.
	for (auto parameterizedJob : m_metaData->GetAnyArrayObject(g_metaParameterizedJobCLSID)) {

		if (parameterizedJob->IsDeleted())
			continue;

		const wxString& strName = parameterizedJob->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddCatalogItem(parameterizedJob,
			AppendItem(m_treeJOBS, parameterizedJob));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeJOBS))
		m_metaTreeCtrl->Delete(m_treeJOBS);

	//****************************************************************
	//*                          Pictures							 *
	//****************************************************************
	for (auto picture : m_metaData->GetAnyArrayObject(g_metaPictureCLSID)) {

		if (picture->IsDeleted())
			continue;

		const wxString& strName = picture->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treePICTURES, picture);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treePICTURES))
		m_metaTreeCtrl->Delete(m_treePICTURES);

	//****************************************************************
	//*                          Interfaces							 *
	//****************************************************************

	for (auto commonInterface : m_metaData->GetAnyArrayObject(g_metaSectionCLSID)) {

		if (commonInterface->IsDeleted())
			continue;

		const wxString& strName = commonInterface->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddInterfaceItem(commonInterface,
			AppendGroupItem(m_treeINTERFACES, g_metaSectionCLSID, commonInterface));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeINTERFACES))
		m_metaTreeCtrl->Delete(m_treeINTERFACES);

	//****************************************************************
	//*                          Roles								 *
	//****************************************************************
	for (auto role : m_metaData->GetAnyArrayObject(g_metaRoleCLSID)) {

		if (role->IsDeleted())
			continue;

		const wxString& strName = role->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeROLES, role);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeROLES))
		m_metaTreeCtrl->Delete(m_treeROLES);

	//****************************************************************
	//*                          Languages							 *
	//****************************************************************
	for (auto language : m_metaData->GetAnyArrayObject(g_metaLanguageCLSID)) {

		if (language->IsDeleted())
			continue;

		const wxString& strName = language->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeLANGUAGES, language);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeLANGUAGES))
		m_metaTreeCtrl->Delete(m_treeLANGUAGES);

	//****************************************************************
	//*                          Constants                           *
	//****************************************************************
	for (auto constant : m_metaData->GetAnyArrayObject(g_metaConstantCLSID)) {

		if (constant->IsDeleted())
			continue;

		const wxString& strName = constant->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendItem(m_treeCONSTANTS, constant);
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeCONSTANTS))
		m_metaTreeCtrl->Delete(m_treeCONSTANTS);

	//****************************************************************
	//*                        Commands                              *
	//****************************************************************
	for (auto command : m_metaData->GetAnyArrayObject(g_metaCommonCommandCLSID)) {

		if (command->IsDeleted())
			continue;

		const wxString& strName = command->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AppendCommandNode(m_treeCOMMANDS, command);   // hub — nests sub-commands
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeCOMMANDS))
		m_metaTreeCtrl->Delete(m_treeCOMMANDS);

	//****************************************************************
	//*                        Catalogs                              *
	//****************************************************************
	for (auto catalog : m_metaData->GetAnyArrayObject(g_metaCatalogCLSID)) {

		if (catalog->IsDeleted())
			continue;

		const wxString& strName = catalog->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddCatalogItem(catalog,
			AppendItem(m_treeCATALOGS, catalog));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeCATALOGS))
		m_metaTreeCtrl->Delete(m_treeCATALOGS);

	//****************************************************************
	//*                        Documents                             *
	//****************************************************************
	for (auto document : m_metaData->GetAnyArrayObject(g_metaDocumentCLSID)) {

		if (document->IsDeleted())
			continue;

		const wxString& strName = document->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddDocumentItem(document,
			AppendItem(m_treeDOCUMENTS, document));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeDOCUMENTS))
		m_metaTreeCtrl->Delete(m_treeDOCUMENTS);

	//****************************************************************
	//*                          Enumerations                        *
	//****************************************************************
	for (auto enumeration : m_metaData->GetAnyArrayObject(g_metaEnumerationCLSID)) {

		if (enumeration->IsDeleted())
			continue;

		const wxString& strName = enumeration->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddEnumerationItem(enumeration,
			AppendItem(m_treeENUMERATIONS, enumeration));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeENUMERATIONS))
		m_metaTreeCtrl->Delete(m_treeENUMERATIONS);

	//****************************************************************
	//*                          Data processor                      *
	//****************************************************************
	for (auto dataProcessor : m_metaData->GetAnyArrayObject(g_metaDataProcessorCLSID)) {

		if (dataProcessor->IsDeleted())
			continue;

		const wxString& strName = dataProcessor->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddDataProcessorItem(dataProcessor,
			AppendItem(m_treeDATAPROCESSORS, dataProcessor));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeDATAPROCESSORS))
		m_metaTreeCtrl->Delete(m_treeDATAPROCESSORS);

	//****************************************************************
	//*                          Report			                     *
	//****************************************************************
	for (auto report : m_metaData->GetAnyArrayObject(g_metaReportCLSID)) {

		if (report->IsDeleted())
			continue;

		const wxString& strName = report->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddReportItem(report,
			AppendItem(m_treeREPORTS, report));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeREPORTS))
		m_metaTreeCtrl->Delete(m_treeREPORTS);

	//****************************************************************
	//*                          Information register			     *
	//****************************************************************
	for (auto informationRegister : m_metaData->GetAnyArrayObject(g_metaInformationRegisterCLSID)) {

		if (informationRegister->IsDeleted())
			continue;

		const wxString& strName = informationRegister->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddInformationRegisterItem(informationRegister,
			AppendItem(m_treeINFORMATION_REGISTERS, informationRegister));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeINFORMATION_REGISTERS))
		m_metaTreeCtrl->Delete(m_treeINFORMATION_REGISTERS);

	//****************************************************************
	//*                          Accumulation register			     *
	//****************************************************************
	for (auto accumulationRegister : m_metaData->GetAnyArrayObject(g_metaAccumulationRegisterCLSID)) {

		if (accumulationRegister->IsDeleted())
			continue;

		const wxString& strName = accumulationRegister->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddAccumulationRegisterItem(accumulationRegister,
			AppendItem(m_treeACCUMULATION_REGISTERS, accumulationRegister));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeACCUMULATION_REGISTERS))
		m_metaTreeCtrl->Delete(m_treeACCUMULATION_REGISTERS);

	//****************************************************************
	//*                          Charts of characteristic types      *
	//****************************************************************
	for (auto chartOfCharacteristicTypes : m_metaData->GetAnyArrayObject(g_metaChartOfCharacteristicTypesCLSID)) {

		if (chartOfCharacteristicTypes->IsDeleted())
			continue;

		const wxString& strName = chartOfCharacteristicTypes->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddCatalogItem(chartOfCharacteristicTypes,
			AppendItem(m_treeCHARTS_OF_CHARACTERISTIC_TYPES, chartOfCharacteristicTypes));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeCHARTS_OF_CHARACTERISTIC_TYPES))
		m_metaTreeCtrl->Delete(m_treeCHARTS_OF_CHARACTERISTIC_TYPES);

	//****************************************************************
	//*                          Charts of accounts                  *
	//****************************************************************
	for (auto chartOfAccounts : m_metaData->GetAnyArrayObject(g_metaChartOfAccountsCLSID)) {

		if (chartOfAccounts->IsDeleted())
			continue;

		const wxString& strName = chartOfAccounts->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddCatalogItem(chartOfAccounts,
			AppendItem(m_treeCHARTS_OF_ACCOUNTS, chartOfAccounts));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeCHARTS_OF_ACCOUNTS))
		m_metaTreeCtrl->Delete(m_treeCHARTS_OF_ACCOUNTS);

	//****************************************************************
	//*                          Accounting register                 *
	//****************************************************************
	for (auto accountingRegister : m_metaData->GetAnyArrayObject(g_metaAccountingRegisterCLSID)) {

		if (accountingRegister->IsDeleted())
			continue;

		const wxString& strName = accountingRegister->GetName();

		if (!m_strSearch.IsEmpty()
			&& strName.Find(m_strSearch) < 0)
			continue;

		AddAccumulationRegisterItem(accountingRegister,
			AppendItem(m_treeACCOUNTING_REGISTERS, accountingRegister));
	}

	if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(m_treeACCOUNTING_REGISTERS))
		m_metaTreeCtrl->Delete(m_treeACCOUNTING_REGISTERS);

	//set modify
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(commonMetadata, m_treeMETADATA);
}

bool ibMetadataTree::Load(ibMetaDataConfigurationBase* metaData)
{
	m_metaTreeCtrl->Freeze();
	ClearTree();
	m_metaData = metaData ? metaData : appEnv::ActiveMetaData();
	FillData(); //Fill all data from metaData
	m_metaData->SetMetaTree(this);
	m_metaTreeCtrl->SelectItem(m_treeMETADATA);
	m_metaTreeCtrl->Expand(m_treeMETADATA);
	m_metaTreeCtrl->Expand(m_treeCOMMON);
	m_metaTreeCtrl->Thaw();
	return true;
}

bool ibMetadataTree::Save()
{
	wxASSERT(m_metaData);

	if (m_metaData->IsModified() && wxMessageBox(wxString::Format(_("Configuration '%s' has been changed. Save?"), m_metaData->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES)
		return m_metaData->SaveDatabase();

	return false;
}

/////////////////////////////////////////////////////////////

void ibMetadataTree::Search(const wxString& strSearch)
{
	m_metaTreeCtrl->Freeze();

	//InitTree();
	ClearTree();

	m_strSearch = strSearch;

	FillData(); //Fill all data from metaData

	m_metaTreeCtrl->SelectItem(m_treeMETADATA);
	m_metaTreeCtrl->Expand(m_treeMETADATA);
	m_metaTreeCtrl->Expand(m_treeCOMMON);

	if (!m_strSearch.IsEmpty())
		m_metaTreeCtrl->ExpandAll();

	m_strSearch = wxEmptyString;

	m_metaTreeCtrl->Thaw();
}

/////////////////////////////////////////////////////////////
