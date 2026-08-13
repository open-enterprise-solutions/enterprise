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

#include <wx/intl.h>  // wxGetTranslation — the layout table holds SOURCE strings, translated on use

#include <iterator>   // std::rbegin / std::rend over the layout table

// The COMMON folder is not a metatype's group — it is the band itself, so its label stays here.
// Every GROUP label moved into the layout table further down (s_groups), where it sits beside the
// metatype it names instead of in a block of defines that nothing tied to anything.
#define commonName _("Common")

// The labels of the groups INSIDE an object — attributes, tabular sections, forms, commands,
// templates. These are still written at each adder, because an adder is what decides that a
// register shows dimensions and resources while a catalog shows tabular sections.
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

ibFormID ibMetaTreeBase::SelectFormType(ibValueMetaObjectForm* metaObject) const
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

void ibMetaTreeBase::Activate()
{
	if (m_docParent == nullptr) {
		unsigned int count_doc = 0;
		for (auto doc : docManager->GetDocumentsVector()) count_doc++;
		if (count_doc <= 1) SetFocus();
	}
}

void ibMetaTreeBase::Modify(bool modify)
{
	if (m_docParent != nullptr) {
		m_docParent->Modify(modify);
	}
	else {
		mainFrame->Modify(modify);
	}
}

bool ibMetaTreeBase::OpenObjectForm(ibValueMetaObject* obj)
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

bool ibMetaTreeBase::OpenObjectForm(ibValueMetaObject* obj, ibBackendMetaDocument*& doc)
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

bool ibMetaTreeBase::CloseObjectForm(ibValueMetaObject* obj)
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

ibMetaDocument* ibMetaTreeBase::GetDocument(ibValueMetaObject* obj) const
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

void ibMetaTreeBase::EditModule(const ibGuid& moduleName, int lineNumber, bool setRunLine)
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

	// ASK, do not assert by cast. A static_cast never yields null for a live pointer of the wrong
	// type, so the null check below was checking nothing — and a metaobject whose document is not a
	// module editor (the debugger can ask for any of them) had SetCurrentLine called on it anyway.
	//
	// ⚠ C++ dynamic_cast, NOT wxDynamicCast — the same door debugClientImpl.cpp uses on this very
	// type. wx RTTI answers from the base written BY HAND in the wxIMPLEMENT macro, and this chain
	// does not name ibValueModuleDocument: ibModuleDocument declares ibMetaDocument as its base
	// (docViewModuleEditor.cpp) though it really derives from ibValueModuleDocument, so the
	// interface is a SIBLING in the wx graph rather than an ancestor. wxDynamicCast therefore
	// answered null for every module document, and the debugger's current-line arrow never
	// appeared while breakpoints — which travel another path — kept working.
	ibValueModuleDocument* moduleDoc = dynamic_cast<ibValueModuleDocument*>(foundedDoc);
	if (moduleDoc != nullptr) moduleDoc->SetCurrentLine(lineNumber, setRunLine);
}

//***********************************************************************
//*								 metaData                               * 
//***********************************************************************

void ibConfigurationTree::ActivateItem(const wxTreeItemId& item)
{
	ibValueMetaObject* currObject = GetMetaObject(item);
	if (currObject == nullptr)
		return;

	OpenObjectForm(currObject);
}

ibValueMetaObject* ibConfigurationTree::NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject)
{
	return m_metaData->CreateMetaObject(clsid, parent, runObject);
}

ibValueMetaObject* ibConfigurationTree::CreateItem(bool showValue)
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

wxTreeItemId ibConfigurationTree::FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select, bool scroll)
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

	// HOW IT UNFOLDS — the same dispatcher the initial fill uses. It used to be a second chain of
	// `else if` written out here, and the two had drifted: a section created in this path listed
	// its sub-sections as plain rows and did not recurse, while a section LOADED by FillData went
	// through AddInterfaceItem and nested properly.
	ExpandMetaItem(metaItem, createdItem);

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
wxTreeItemId ibConfigurationTree::AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command)
{
	if (command == nullptr || command->IsDeleted())
		return wxTreeItemId();
	const wxTreeItemId hCmd = AppendItem(parent, command);
	if (command->GetClassType() == g_metaCommonCommandCLSID || command->GetClassType() == g_metaCommandCLSID)
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(command)->GetSubCommands())
			AppendCommandNode(hCmd, sub);

	// A command survives a search if IT matched or a command under it did — the recursion above has
	// already answered the second half, because a sub-command that matched nothing removed itself.
	if (!m_strSearch.IsEmpty() && !MatchesSearch(command) && !m_metaTreeCtrl->HasChildren(hCmd)) {
		m_metaTreeCtrl->Delete(hCmd);
		return wxTreeItemId();
	}
	return hCmd;
}

void ibConfigurationTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (!selection.IsOk())
		return;
	ibValueMetaObject* currObject = GetMetaObject(selection);
	if (!currObject)
		return;

	OpenObjectForm(currObject);
}

void ibConfigurationTree::RemoveItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();

	if (!selection.IsOk())
		return;

	ibValueMetaObject* metaObject = GetMetaObject(selection);
	// NOT AN ASSERT: wxASSERT is compiled out in Release, and a null here reaches RemoveMetaObject
	// and Delete(selection) — which, on a layout group row, would free a node the group map still
	// points at. A stale wxTreeItemId answers IsOk() == true, so every later guard would pass.
	if (metaObject == nullptr)
		return;
	EraseItem(selection);   // the row AND everything under it — see EraseItem
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

// CLOSE WHAT THIS ROW STANDS FOR, AND EVERYTHING UNDER IT. RemoveMetaObject destroys the whole
// subtree, so an editor open on a form five levels down has to go with it. The caller used to walk
// the DIRECT children instead — and those are group nodes ("Attributes", "Forms", …) that carry a
// class id and no metaobject, so the sweep closed exactly nothing and a form editor survived the
// object it was editing.
void ibConfigurationTree::EraseItem(const wxTreeItemId& item)
{
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie))
		EraseItem(child);

	ibValueMetaObject* const metaObject = GetMetaObject(item);
	if (metaObject == nullptr)
		return;   // a group node — nothing of its own to close

	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr && metaObject == metaDoc->GetMetaObject()) {
			metaDoc->DeleteAllViews();
		}
	}
}

void ibConfigurationTree::SelectItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);

	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	objectInspector->SelectObject(metaObject);
}

void ibConfigurationTree::PropertyItem()
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

void ibConfigurationTree::Collapse()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = false;
}

void ibConfigurationTree::Expand()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibConfigurationTree::UpItem()
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
			nextId = m_metaTreeCtrl->GetNextChild(parentItem, coockie); pos++;
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
					nextId = tree->GetNextChild(dst, coockie);
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

void ibConfigurationTree::DownItem()
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
			nextId = m_metaTreeCtrl->GetNextChild(parentItem, coockie); pos++;
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
					nextId = tree->GetNextChild(dst, coockie);
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

void ibConfigurationTree::SortItem()
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

void ibConfigurationTree::InsertItem()
{
	ibValueMetaObject* commonMetaObject = m_metaData->GetCommonMetaObject(); wxTreeItemId hSelItem = m_metaTreeCtrl->GetSelection();

	// ⚠ ASK WHETHER THE GROUP EXISTS, not just whether the ids match: two INVALID wxTreeItemIds
	// compare equal, and a group is legitimately absent while a search filter is on. The field
	// used to be unconditionally valid, so this test is new work rather than a port.
	if (Group(g_metaDataProcessorCLSID).IsOk() && hSelItem == Group(g_metaDataProcessorCLSID)) {

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

void ibConfigurationTree::ReplaceItem()
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

void ibConfigurationTree::SaveItem()
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

void ibConfigurationTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	ibValueMetaObject* metaObject = GetMetaObject(m_metaTreeCtrl->GetSelection());

	if (!metaObject)
		return;

	metaObject->ProcessCommand(id);
}

void ibConfigurationTree::PrepareReplaceMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_REPLACE, _("Replace data processor, report..."));
	menuItem->Enable(!m_bReadOnly);
	menuItem = defaultMenu->Append(ID_METATREE_SAVE, _("Save data processor, report..."));
	defaultMenu->AppendSeparator();
}

#include "frontend/artProvider/artProvider.h"

void ibConfigurationTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
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

		// Same rule as InsertItem: an absent group must not match an invalid selection.
		if ((Group(g_metaDataProcessorCLSID).IsOk() && item == Group(g_metaDataProcessorCLSID))
			|| (Group(g_metaReportCLSID).IsOk() && item == Group(g_metaReportCLSID))) {
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

void ibConfigurationTree::ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos)
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
		m_metaTreeCtrl->Bind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnCommandItem, m_metaTreeCtrl, id);
		boundIds.push_back(id);
	}

	m_metaTreeCtrl->PopupMenu(&innerMenu, m_metaTreeCtrl->ScreenToClient(eventSrc->ClientToScreen(pos)));

	for (int id : boundIds) {
		m_metaTreeCtrl->Unbind(wxEVT_MENU, &ibConfigurationTree::ibMetaTreeCtrl::OnCommandItem, m_metaTreeCtrl, id);
	}
}

void ibConfigurationTree::UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly && item != m_treeCOMMON);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

bool ibConfigurationTree::RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName)
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

void ibConfigurationTree::AddInterfaceItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectSection* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectSection>();
	wxASSERT(metaObject);

	// SECTIONS NEST, so a section survives a search if IT matched or something inside it did —
	// the recursion below answers the second half, exactly as it does for commands.
	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {

		if (commonInterface->IsDeleted())
			continue;

		const wxTreeItemId hSection = AppendGroupItem(hParentID, g_metaSectionCLSID, commonInterface);
		AddInterfaceItem(commonInterface, hSection);

		if (!m_strSearch.IsEmpty() && !MatchesSearch(commonInterface)
			&& !m_metaTreeCtrl->HasChildren(hSection))
			m_metaTreeCtrl->Delete(hSection);
	}

	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())   // a section owns its own commands
		AppendCommandNode(hParentID, metaCommand);
}

// A REFERENCE OBJECT — attributes, tabular sections, forms, commands, templates. Catalogs and
// documents render identically, and so do the two charts and a parameterized job (they reach here
// through ExpandMetaItem, which is where "renders AS a catalog" is written down).
void ibConfigurationTree::AddCatalogItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataRef>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	// A catalog / document is ALWAYS a reference, so its table is the DB-backed MD_TBLR;
	// processors / reports are RAM, so MD_TBL. Set explicitly per object kind.
	AppendTableGroup(hParentID, g_metaTableRefCLSID, objectTablesName,
		metaObjectValue->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A DOCUMENT renders exactly as a catalog does — the same five groups in the same order. It keeps
// an entry point of its own only because the dispatcher names KINDS, not shapes.
void ibConfigurationTree::AddDocumentItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	AddCatalogItem(metaObject, hParentID);
}

// AN ENUMERATION has values where the others have attributes and tabular sections.
void ibConfigurationTree::AddEnumerationItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataEnumRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataEnumRef>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaEnumCLSID, objectEnumerationsName,
		metaObjectValue->GetEnumObjectArray());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A DATA PROCESSOR — the same five groups as a catalog, except that its tabular sections live in
// RAM (MD_TBL) rather than in the database (MD_TBLR). That one clsid is the whole difference.
void ibConfigurationTree::AddDataProcessorItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendTableGroup(hParentID, g_metaTableCLSID, objectTablesName,
		metaObjectValue->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

void ibConfigurationTree::AddReportItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	AddDataProcessorItem(metaObject, hParentID);   // same shape, down to the RAM tabular sections
}

// A REGISTER — dimensions and resources where a reference object has tabular sections.
void ibConfigurationTree::AddInformationRegisterItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRegisterData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRegisterData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaDimensionCLSID, objectDimensionsName,
		metaObjectValue->GetDimensionArrayObject());
	AppendObjectGroup(hParentID, g_metaResourceCLSID, objectResourcesName,
		metaObjectValue->GetResourceArrayObject());
	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

void ibConfigurationTree::AddAccumulationRegisterItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	AddInformationRegisterItem(metaObject, hParentID);   // same shape — an accounting register too
}

#include "frontend/artProvider/artProvider.h"

////////////////////////////////////////////////////////////////////////////
// THE LAYOUT — one table, read top to bottom
////////////////////////////////////////////////////////////////////////////
//
// Every group node in the navigator is a row here, and the row is the whole truth about it: the
// METATYPE it stands for (which already gives it its icon, its "New" and its context menu), the
// words on it, which band it lives in, and how a member of it is put in. THE ORDER OF THE TABLE
// IS THE ORDER ON SCREEN — there is no second place that says it, where before there were three
// (the sequence of AppendGroupItem calls, the sequence of fill loops, and a list of DeleteChildren
// that had silently fallen three entries behind).
//
// This is deliberately a table and NOT YET a walk over the type registry. What a metatype would
// have to answer for a navigator to draw it without this table is exactly the columns below —
// writing them out in one place is what makes that question askable. Once a metatype answers it
// itself (a band + a rank + a label, beside the GetIconGroup it already has), this table goes away
// and the tree becomes a walk over whatever registered — which is what would let a metatype added
// later appear here with nothing edited in this file. Same shape as backend_picture.cpp, which
// already walks ibValue::GetListCtorsByType(object_metadata) for exactly one such answer.
//
// ⚠ ORDER MATTERS TWICE: a row that nests inside another group (m_owner) must come AFTER it — the
// parent node has to exist to hang from.

namespace {

enum class ibMetaBand { Common, Metadata };   // the two bands of the navigator

// HOW A MEMBER OF THE GROUP IS PUT IN. Three shapes and no more: an ordinary row, a row that is
// itself a group (a section holds sections), and a command (which nests its sub-commands).
enum class ibMetaRow { Item, Group, Command };

struct ibMetaTreeGroupDef {
	ibClassID   m_clsid;
	// The SOURCE string, marked for extraction and left untranslated here: this table is static
	// data, built before a locale is loaded (same rule as backend/fileKind.cpp).
	const char* m_label;
	ibMetaBand  m_band;
	ibClassID   m_owner;   // 0 = straight in the band; otherwise the group it nests under
	ibMetaRow   m_row;
};

const ibMetaTreeGroupDef s_groups[] = {
	// ——— Common: what belongs to the configuration as a whole and to no business object ———
	{ g_metaCommonModuleCLSID,    wxTRANSLATE("Common modules"),   ibMetaBand::Common, 0, ibMetaRow::Item    },
	{ g_metaCommonFormCLSID,      wxTRANSLATE("Common forms"),     ibMetaBand::Common, 0, ibMetaRow::Item    },
	{ g_metaCommonCommandCLSID,   wxTRANSLATE("Common commands"),  ibMetaBand::Common, 0, ibMetaRow::Command },
	{ g_metaCommonTemplateCLSID,  wxTRANSLATE("Common templates"), ibMetaBand::Common, 0, ibMetaRow::Item    },

	// SCHEDULED JOBS stay under COMMON — unattended work belongs to the configuration as a whole.
	// One branch, two kinds inside it: the branch itself holds the PARAMETERIZED jobs (it is their
	// metatype's group node, so File → New reaches them the usual way), and the PREDEFINED ones
	// live in a sub-branch declared FIRST, which is what puts them above — a configuration declares
	// a handful of those and they never multiply with the data, while the parameterized list grows.
	{ g_metaParameterizedJobCLSID, wxTRANSLATE("Scheduled jobs"),  ibMetaBand::Common, 0, ibMetaRow::Item },
	{ g_metaScheduledJobCLSID,     wxTRANSLATE("Predefined jobs"), ibMetaBand::Common,
	  g_metaParameterizedJobCLSID, ibMetaRow::Item },

	// SESSION PARAMETERS sit beside the jobs for the same reason: each is an ATTRIBUTE whose owner
	// is the session — declared here, set once by the session module, read everywhere.
	{ g_metaSessionParameterCLSID, wxTRANSLATE("Session parameters"), ibMetaBand::Common, 0, ibMetaRow::Item },
	// COMMON ATTRIBUTES — declared here, carried by many objects. What the declaration puts INTO
	// each object is a child of THAT object and appears there, in its own attribute list.
	{ g_metaCommonAttributeCLSID,  wxTRANSLATE("Common attributes"),  ibMetaBand::Common, 0, ibMetaRow::Item },
	{ g_metaPictureCLSID,          wxTRANSLATE("Pictures"),           ibMetaBand::Common, 0, ibMetaRow::Item },
	// Sections come AFTER the common items — a top-level navigation grouping, not a common asset.
	{ g_metaSectionCLSID,          wxTRANSLATE("Sections"),           ibMetaBand::Common, 0, ibMetaRow::Group },
	{ g_metaRoleCLSID,             wxTRANSLATE("Roles"),              ibMetaBand::Common, 0, ibMetaRow::Item },
	{ g_metaLanguageCLSID,         wxTRANSLATE("Languages"),          ibMetaBand::Common, 0, ibMetaRow::Item },

	// ——— Metadata: the business objects a configuration is made of ———
	{ g_metaConstantCLSID,                   wxTRANSLATE("Constants"),        ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaCatalogCLSID,                    wxTRANSLATE("Catalogs"),         ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaDocumentCLSID,                   wxTRANSLATE("Documents"),        ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaEnumerationCLSID,                wxTRANSLATE("Enumerations"),     ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaDataProcessorCLSID,              wxTRANSLATE("Data processors"),  ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaReportCLSID,                     wxTRANSLATE("Reports"),          ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaInformationRegisterCLSID,        wxTRANSLATE("Information Registers"),  ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaAccumulationRegisterCLSID,       wxTRANSLATE("Accumulation Registers"), ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaChartOfCharacteristicTypesCLSID, wxTRANSLATE("Charts of characteristic types"), ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaChartOfAccountsCLSID,            wxTRANSLATE("Charts of accounts"),      ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaAccountingRegisterCLSID,         wxTRANSLATE("Accounting registers"),    ibMetaBand::Metadata, 0, ibMetaRow::Item },
};

} // namespace

// HOW A METAOBJECT UNFOLDS — the ONE dispatcher. The initial fill and the create path both come
// through here, so a kind cannot unfold one way when it is loaded and another way when it is made
// (they were two separate chains of `else if` before, and they had already drifted apart).
//
// The reuse is meaningful and is the only real knowledge in it: a chart of characteristic types
// and a chart of accounts render AS a catalog, an accounting register AS an accumulation register,
// a parameterized job AS a catalog entry with a second verb.
void ibConfigurationTree::ExpandMetaItem(ibValueMetaObject* metaItem, const wxTreeItemId& item)
{
	const ibClassID clsid = metaItem->GetClassType();

	if      (clsid == g_metaCatalogCLSID)                    AddCatalogItem(metaItem, item);
	else if (clsid == g_metaDocumentCLSID)                   AddDocumentItem(metaItem, item);
	else if (clsid == g_metaEnumerationCLSID)                AddEnumerationItem(metaItem, item);
	else if (clsid == g_metaDataProcessorCLSID)              AddDataProcessorItem(metaItem, item);
	else if (clsid == g_metaReportCLSID)                     AddReportItem(metaItem, item);
	else if (clsid == g_metaInformationRegisterCLSID)        AddInformationRegisterItem(metaItem, item);
	else if (clsid == g_metaAccumulationRegisterCLSID)       AddAccumulationRegisterItem(metaItem, item);
	else if (clsid == g_metaParameterizedJobCLSID)           AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfCharacteristicTypesCLSID) AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfAccountsCLSID)            AddCatalogItem(metaItem, item);
	else if (clsid == g_metaAccountingRegisterCLSID)         AddAccumulationRegisterItem(metaItem, item);
	else if (clsid == g_metaSectionCLSID)                    AddInterfaceItem(metaItem, item);

	// A COMMAND HOLDS COMMANDS. The fill path always knew this (it goes through AppendCommandNode);
	// the create/paste path did not, so pasting a command that owns sub-commands drew a leaf — the
	// children were restored in the metadata and stayed invisible until the configuration reopened.
	else if (clsid == g_metaCommandCLSID || clsid == g_metaCommonCommandCLSID) {
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(metaItem)->GetSubCommands())
			AppendCommandNode(item, sub);
	}

	// A TABULAR SECTION shows its own columns. It reaches this dispatcher from the create path
	// only — a table is never a top-level group — but it belongs here rather than beside the call,
	// so "how does a kind unfold" has one answer wherever it is asked.
	else if (clsid == g_metaTableCLSID || clsid == g_metaTableRefCLSID) {
		ibValueMetaObjectTableData* metaTable = metaItem->ConvertToType<ibValueMetaObjectTableData>();
		wxASSERT(metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {
			if (!attribute->IsAcceptedByParent())
				continue;
			AppendItem(item, attribute);
		}
	}
	// Anything else is a leaf row — a module, a form, a picture, a role, a language.
}

// DOES THIS OBJECT ANSWER THE SEARCH BOX. One predicate, asked in one place, so a filtered tree
// cannot disagree with itself the way it did when the test was written out at each loop.
//
// CASE-INSENSITIVE, and by NAME OR SYNONYM: `Find` is case-sensitive, so typing what is on screen
// in the wrong case found nothing — and the words a person reads in this tree are the name, while
// the words they remember are often the synonym.
bool ibConfigurationTree::MatchesSearch(const ibValueMetaObject* metaObject) const
{
	if (m_strSearch.IsEmpty())
		return true;
	const wxString needle = m_strSearch.Lower();
	return metaObject->GetName().Lower().Find(needle) != wxNOT_FOUND
		|| metaObject->GetSynonym().Lower().Find(needle) != wxNOT_FOUND;
}

void ibConfigurationTree::InitTree()
{
	wxImageList* imageList = m_metaTreeCtrl->GetImageList();
	wxASSERT(imageList);

	m_treeMETADATA = AppendRootItem(g_metaCommonMetadataCLSID, _("Configuration"));

	const int imageCommonIndex = imageList->Add(wxArtProvider::GetBitmapBundle(wxART_COMMON_FOLDER, wxART_METATREE).GetBitmap(wxDefaultSize));
	m_treeCOMMON = m_metaTreeCtrl->AppendItem(m_treeMETADATA, commonName, imageCommonIndex, imageCommonIndex);

	m_groups.clear();
	for (const ibMetaTreeGroupDef& def : s_groups) {
		const wxTreeItemId parent = def.m_owner != 0
			? Group(def.m_owner)                                                   // nested (predefined jobs)
			: (def.m_band == ibMetaBand::Common ? m_treeCOMMON : m_treeMETADATA);
		wxASSERT(parent.IsOk());   // a nested row placed before its owner — see the table's ⚠
		m_groups[def.m_clsid] = AppendGroupItem(parent, def.m_clsid,
			wxGetTranslation(wxString::FromUTF8(def.m_label)));
	}


	//Set item bold and name
	m_metaTreeCtrl->SetItemText(m_treeMETADATA, _("Configuration"));
	m_metaTreeCtrl->SetItemBold(m_treeMETADATA);
}

void ibConfigurationTree::ActivateTree()
{
	if (m_metaData != nullptr)
		objectInspector->SelectObject(GetMetaObject(m_metaTreeCtrl->GetSelection()));
}

// CLOSING THE EDITORS IS PART OF *LEAVING A CONFIGURATION*, not of redrawing the tree — and those
// two used to be the same call. Rebuilding the rows is what a search does on every keystroke, so
// typing into the search box closed every editor opened from this navigator, and so did deleting
// the last character (the empty-string search is what restores the full tree).
void ibConfigurationTree::CloseOwnedDocuments()
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
}

void ibConfigurationTree::ClearTree()
{
	// disable events for the whole rebuild - RAII, so a throw from InitTree cannot leave them off
	const ibEventsOff eventsOff(m_metaTreeCtrl);

	// THE CLEAR IS TOTAL, and it always was. A per-group DeleteChildren pass used to stand here,
	// written as a list of nineteen branches — but it ran immediately before DeleteAllItems, so
	// nothing it did could survive, and the list had fallen three entries behind (session
	// parameters, common attributes, languages) without any way for that to show. The intent it
	// carried — "clear the contents on demand" — is what these two lines do, for every group
	// including the ones nobody remembered to add.
	m_groups.clear();

	// ROWS FIRST, THEN THE LIST THEY INDEX INTO — the twins already do it in this order. A row
	// holds an INDEX into the image list, so dropping the images while the rows still reference
	// them leaves every surviving row pointing past the end for as long as the delete pass runs.
	m_metaTreeCtrl->DeleteAllItems();

	// THE IMAGE LIST IS PART OF THE TREE, so it is cleared with it. Every Append* adds a bitmap and
	// nothing ever removed one, so each rebuild — and a search is a rebuild — grew the list by the
	// whole configuration again and kept it for the life of the process. InitTree / FillData re-add
	// what they need; the indices they hand out are only ever read back from the same pass.
	if (wxImageList* imageList = m_metaTreeCtrl->GetImageList())
		imageList->RemoveAll();

	//initialize tree
	InitTree();

}

void ibConfigurationTree::FillData()
{
	ibValueMetaObject* commonMetadata = m_metaData->GetCommonMetaObject();
	wxASSERT(commonMetadata);

	m_metaTreeCtrl->SetItemText(m_treeMETADATA, m_metaData->GetConfigName());
	m_metaTreeCtrl->SetItemData(m_treeMETADATA, new wxTreeItemMetaData(commonMetadata));

	// ONE PASS OVER THE LAYOUT. Every group asks the metadata for its own kind and puts what comes
	// back in the way its row says. This was twenty copies of the loop below, one per group, each
	// with its own spelling of the same three tests — and the copies had already diverged (the
	// search test was written out in some and left commented out in others).
	for (const ibMetaTreeGroupDef& def : s_groups) {

		const wxTreeItemId group = Group(def.m_clsid);
		if (!group.IsOk())
			continue;

		for (auto metaObject : m_metaData->GetAnyArrayObject(def.m_clsid)) {

			if (metaObject->IsDeleted())
				continue;

			wxTreeItemId node;
			switch (def.m_row) {
			case ibMetaRow::Command:
				node = AppendCommandNode(group, metaObject);   // hub — nests sub-commands, skips deleted
				break;
			case ibMetaRow::Group:
				// A section holds sections, so its row is a group node in its own right.
				node = AppendGroupItem(group, def.m_clsid, metaObject);
				ExpandMetaItem(metaObject, node);
				break;
			default:
				node = AppendItem(group, metaObject);
				ExpandMetaItem(metaObject, node);
				break;
			}

			// AN OBJECT SURVIVES A SEARCH IF IT MATCHED — or if anything inside it did. The unfold
			// above has already filtered its contents, so "nothing left under it" is the answer to
			// the second half. This is what makes searching for an attribute name show the catalog
			// that carries it, instead of finding nothing at all.
			if (!m_strSearch.IsEmpty() && node.IsOk() && !MatchesSearch(metaObject)
				&& !m_metaTreeCtrl->HasChildren(node))
				m_metaTreeCtrl->Delete(node);
		}
	}

	// A GROUP THAT MATCHED NOTHING GOES AWAY — but ONLY while a search is running. Empty is the
	// normal state of a group otherwise: it is where an object of that kind gets created, so
	// hiding it would hide the way in.
	//
	// BOTTOM-UP, because a nested group counts as a child of its owner: sweeping top-down left the
	// jobs branch standing on the strength of a sub-branch that the same pass was about to remove.
	if (!m_strSearch.IsEmpty()) {
		for (auto def = std::rbegin(s_groups); def != std::rend(s_groups); ++def) {
			const wxTreeItemId group = Group(def->m_clsid);
			if (group.IsOk() && !m_metaTreeCtrl->HasChildren(group)) {
				m_metaTreeCtrl->Delete(group);
				m_groups.erase(def->m_clsid);   // the entry goes with the node — no dangling id
			}
		}
	}

	//set modify
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(commonMetadata, m_treeMETADATA);
}

bool ibConfigurationTree::Load(ibMetaDataConfigurationBase* metaData)
{
	m_metaTreeCtrl->Freeze();
	CloseOwnedDocuments();   // a configuration is being left — its editors go with it
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

bool ibConfigurationTree::Save()
{
	wxASSERT(m_metaData);

	if (m_metaData->IsModified() && wxMessageBox(wxString::Format(_("Configuration '%s' has been changed. Save?"), m_metaData->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES)
		return m_metaData->SaveDatabase();

	return false;
}

/////////////////////////////////////////////////////////////

void ibConfigurationTree::Search(const wxString& strSearch)
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
