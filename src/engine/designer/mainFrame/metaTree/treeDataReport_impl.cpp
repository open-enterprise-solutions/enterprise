////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/docView/docView.h"
#include "backend/appData.h"
#include "backend/metaCollection/metaCommandObject.h"   // ibValueMetaObjectCommand::GetSubCommands

#include <cstdint>   // intptr_t — the widening step under the client-data cast

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************

void ibDataReportTree::ActivateItem(const wxTreeItemId& item)
{
	ibValueMetaObject* currObject = GetMetaObject(item);

	if (!currObject)
		return;

	OpenObjectForm(currObject);
}

ibValueMetaObject* ibDataReportTree::NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject)
{
	return m_metaData->CreateMetaObject(clsid, parent, runObject);
}

ibValueMetaObject* ibDataReportTree::CreateItem(bool showValue)
{
	const wxTreeItemId& item = GetSelectionIdentifier();
	if (!item.IsOk()) return nullptr;

	ibValueMetaObject* createdObject = NewItem(
		GetClassIdentifier(),
		GetMetaIdentifier()
	);

	if (createdObject != nullptr) {

		ibPropertyObject* prev_selected = objectInspector->GetSelectedObject();

		if (showValue) { OpenObjectForm(createdObject); }
		UpdateToolbar(createdObject, FillItem(createdObject, item,
			prev_selected == objectInspector->GetSelectedObject(), false));
		// See the twin note in treeDataProcessor_impl.cpp — this loop stood here commented out.
		for (auto& doc : docManager->GetDocumentsVector()) {
			ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
			if (metaDoc != nullptr) metaDoc->UpdateAllViews();
		}
	}

	m_metaTreeCtrl->RefreshSelectedItem();
	return createdObject;
}

wxTreeItemId ibDataReportTree::FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select, bool scroll)
{
	m_metaTreeCtrl->Freeze();

	wxTreeItemId createdItem = nullptr;
	if (metaItem->GetClassType() == g_metaTableCLSID) {
		createdItem = AppendGroupItem(item, g_metaAttributeCLSID, metaItem);
	}
	else {
		createdItem = AppendItem(item, metaItem);
	}

	//Advanced mode
	if (metaItem->GetClassType() == g_metaTableCLSID) {

		ibValueMetaObjectTableData* metaItemRecord = dynamic_cast<ibValueMetaObjectTableData*>(metaItem);
		wxASSERT(metaItemRecord);

		for (auto attribute : metaItemRecord->GetAttributeArrayObject()) {
			if (!attribute->IsAcceptedByParent())
				continue;
			AppendItem(createdItem, attribute);
		}
	}
	// A COMMAND HOLDS COMMANDS — see the twin note in treeDataProcessor_impl.cpp.
	else if (metaItem->GetClassType() == g_metaCommandCLSID) {
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(metaItem)->GetSubCommands())
			AppendCommandNode(createdItem, sub);
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

void ibDataReportTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeCtrl->GetSelection();

	if (!selection.IsOk())
		return;

	ibValueMetaObject* currObject = GetMetaObject(selection);

	if (!currObject)
		return;

	OpenObjectForm(currObject);
}

void ibDataReportTree::RemoveItem()
{
	wxTreeItemId selection = m_metaTreeCtrl->GetSelection();

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

	// Read the focus AFTER the views have caught up — the twin and the configuration tree both do.
	const wxTreeItemId nextSelection = m_metaTreeCtrl->GetFocusedItem();
	if (nextSelection.IsOk()) {
		UpdateToolbar(GetMetaObject(nextSelection), nextSelection);
	}

	//update choice if need
	UpdateChoiceSelection();
}

// CLOSE WHAT THIS ROW STANDS FOR, AND EVERYTHING UNDER IT — see the twin in treeDataProcessor_impl.cpp.
void ibDataReportTree::EraseItem(const wxTreeItemId& item)
{
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie))
		EraseItem(child);

	ibValueMetaObject* const metaObject = GetMetaObject(item);
	// NOTHING TO ERASE FOR A GROUP NODE.
	if (metaObject == nullptr)
		return;

	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr && metaObject == metaDoc->GetMetaObject()) {
			metaDoc->DeleteAllViews();
		}
	}
}

void ibDataReportTree::SelectItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);
	objectInspector->SelectObject(metaObject);
}

void ibDataReportTree::PropertyItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);
	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	objectInspector->SelectObject(metaObject);
}

void ibDataReportTree::Collapse()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = false;
}

void ibDataReportTree::Expand()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(selection));
	if (data != nullptr)
		data->m_expanded = true;
}

void ibDataReportTree::UpItem()
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
			std::function<void(ibDataReportTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibDataReportTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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

void ibDataReportTree::DownItem()
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
			std::function<void(ibDataReportTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibDataReportTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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

void ibDataReportTree::SortItem()
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

void ibDataReportTree::CommandItem(unsigned int id)
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;
	wxTreeItemId sel = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(sel);
	if (!metaObject)
		return;
	metaObject->ProcessCommand(id);
}

#include "frontend/artProvider/artProvider.h"

void ibDataReportTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
{
	ibValueMetaObject* metaObject = GetMetaObject(item);

	if (metaObject
		&& !metaObject->PrepareContextMenu(defaultMenu))
	{
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)));
		menuItem->Enable(!m_bReadOnly);
		menuItem = defaultMenu->Append(ID_METATREE_EDIT, _("Edit"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)));
		menuItem = defaultMenu->Append(ID_METATREE_DELETE, _("Delete"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)));
		menuItem->Enable(!m_bReadOnly);
		defaultMenu->AppendSeparator();
		menuItem = defaultMenu->Append(ID_METATREE_PROPERTY, _("Properties"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
	}
	else if (!metaObject) {
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)));
		menuItem->Enable(!m_bReadOnly);
	}
}

void ibDataReportTree::ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos)
{
	wxMenu innerMenu;   // stack — PopupMenu does not take ownership, and it blocks until dismissed
	PrepareContextMenu(&innerMenu, item);

	std::vector<int> boundIds;
	for (auto def_menu : innerMenu.GetMenuItems())
	{
		const int id = def_menu->GetId();
		if (id == ID_METATREE_NEW
			|| id == ID_METATREE_EDIT
			|| id == ID_METATREE_DELETE
			|| id == ID_METATREE_PROPERTY
			|| id == wxID_SEPARATOR)
		{
			continue;
		}
		eventSrc->GetEventHandler()->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnCommandItem, m_metaTreeCtrl, id);
		boundIds.push_back(id);
	}

	eventSrc->PopupMenu(&innerMenu, pos);

#ifdef __WXOSX__
	auto* handler = eventSrc->GetEventHandler();
	auto* treeCtrl = m_metaTreeCtrl;
	eventSrc->CallAfter([handler, treeCtrl, boundIds]() {
		for (int id : boundIds) {
			handler->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnCommandItem, treeCtrl, id);
		}
	});
#else
	for (int id : boundIds) {
		eventSrc->GetEventHandler()->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnCommandItem, m_metaTreeCtrl, id);
	}
#endif
}

void ibDataReportTree::UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

void ibDataReportTree::UpdateChoiceSelection()
{
	m_defaultFormValue->Clear();
	m_defaultFormValue->AppendString(_("<not selected>"));

	ibValueMetaObjectReport* commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);

	int defSelection = 0;

	for (auto metaForm : commonMetadata->GetFormArrayObject())
	{
		if (ibValueMetaObjectReport::eFormReport != metaForm->GetTypeForm())
			continue;

		// WIDEN FIRST, then reinterpret — see the twin note in treeDataProcessor_impl.cpp.
		int selection_id = m_defaultFormValue->Append(metaForm->GetName(),
			reinterpret_cast<void*>(static_cast<intptr_t>(metaForm->GetMetaID())));

		if (commonMetadata->GetDefFormObject() == metaForm->GetMetaID()) {
			defSelection = selection_id;
		}
	}

	m_defaultFormValue->SetSelection(defSelection);
	m_defaultFormValue->SendSelectionChangedEvent(wxEVT_CHOICE);
}

bool ibDataReportTree::RenameMetaObject(ibValueMetaObject* obj, const wxString& sNewName)
{
	wxTreeItemId curItem = m_metaTreeCtrl->GetSelection();

	if (!curItem.IsOk())
		return false;

	if (m_metaData->RenameMetaObject(obj, sNewName)) {
		ibMetaDocument* currDocument = GetDocument(obj);
		if (currDocument != nullptr) {
			currDocument->SetTitle(obj->GetClassName() + wxT(": ") + sNewName);
			currDocument->OnChangeFilename(true);
		}
		//update choice if need
		UpdateChoiceSelection();

		m_metaTreeCtrl->SetItemText(curItem, sNewName);
		return true;
	}

	return false;
}

// HUB — see the twin in treeDataProcessor_impl.cpp.
void ibDataReportTree::AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command)
{
	if (command == nullptr || command->IsDeleted())
		return;
	const wxTreeItemId hCmd = AppendItem(parent, command);
	if (command->GetClassType() == g_metaCommandCLSID)
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(command)->GetSubCommands())
			AppendCommandNode(hCmd, sub);
}

// THE LAYOUT — one table, same shape and same order as the data processor's and as the
// configuration tree's rendering of a report. See the twin note in treeDataProcessor_impl.cpp.
namespace {

struct ibExternalGroupDef {
	ibClassID   m_clsid;
	const char* m_label;
};

const ibExternalGroupDef s_reportGroups[] = {
	{ g_metaAttributeCLSID, wxTRANSLATE("Attributes") },
	{ g_metaTableCLSID,     wxTRANSLATE("Tables")     },
	{ g_metaFormCLSID,      wxTRANSLATE("Forms")      },
	{ g_metaCommandCLSID,   wxTRANSLATE("Commands")   },
	{ g_metaTemplateCLSID,  wxTRANSLATE("Templates")  },
};

} // namespace

void ibDataReportTree::InitTree()
{
	// SINGULAR: this tree edits ONE report, and the root is that report.
	m_treeREPORTS = AppendRootItem(g_metaReportCLSID, _("Report"));

	m_groups.clear();
	for (const ibExternalGroupDef& def : s_reportGroups)
		m_groups[def.m_clsid] = AppendGroupItem(m_treeREPORTS, def.m_clsid,
			wxGetTranslation(wxString::FromUTF8(def.m_label)));
}

void ibDataReportTree::ActivateTree()
{
	if (m_metaData != nullptr)
		objectInspector->SelectObject(GetMetaObject(m_metaTreeCtrl->GetSelection()));
}

void ibDataReportTree::ClearTree()
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

	// disable events for the whole rebuild - RAII, so a throw from InitTree cannot leave them off
	const ibEventsOff eventsOff(m_metaTreeCtrl);

	// The clear is TOTAL: a per-group DeleteChildren pass stood here, immediately before
	// DeleteAllItems, so nothing it did could survive it. InitTree re-creates the groups from
	// the layout table, and the map goes with them.
	m_groups.clear();
	m_initialized = false;   // the tree is gone; a later Load must re-seed it, not resume
	m_metaTreeCtrl->DeleteAllItems();
	if (wxImageList* imageList = m_metaTreeCtrl->GetImageList())
		imageList->RemoveAll();   // every Append* adds one; nothing ever removed them

	//initialize tree
	InitTree();

}

void ibDataReportTree::FillData()
{
	ibValueMetaObjectReport* commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);
	m_metaTreeCtrl->SetItemText(m_treeREPORTS, commonMetadata->GetName());
	m_metaTreeCtrl->SetItemData(m_treeREPORTS, new wxTreeItemMetaData(commonMetadata));

	// SEED THE FIELDS, do not pretend the user typed in them. SetValue emits wxEVT_TEXT, which the
	// constructor connected to OnEditCaptionName — and that handler REGENERATES the synonym from the
	// name. So filling the name here overwrote a hand-written synonym, and the next line then read
	// back what had just been destroyed. ChangeValue is the wx call that sets without notifying.
	m_nameValue->ChangeValue(commonMetadata->GetName());
	m_synonymValue->ChangeValue(commonMetadata->GetSynonym());
	m_commentValue->ChangeValue(commonMetadata->GetComment());

	// (the default-form choice is filled by UpdateChoiceSelection at the end of this function —
	// clearing and seeding it here as well was doing the same work twice)

	// attribute list
	for (auto attribute : commonMetadata->GetAttributeArrayObject()) {
		if (!attribute->IsAcceptedByParent())
			continue;
		AppendItem(Group(g_metaAttributeCLSID), attribute);
	}

	// tabular section list
	for (auto metaTable : commonMetadata->GetTableArrayObject()) {
		if (!metaTable->IsAcceptedByParent())   // predefined section — same rule as the attributes above
			continue;
		const wxTreeItemId& hItem = AppendGroupItem(Group(g_metaTableCLSID), g_metaAttributeCLSID, metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {
			if (!attribute->IsAcceptedByParent())
				continue;
			AppendItem(hItem, attribute);
		}
	}

	// forms
	for (auto metaForm : commonMetadata->GetFormArrayObject()) {
		if (metaForm->IsDeleted())
			continue;
		AppendItem(Group(g_metaFormCLSID), metaForm);
	}

	// commands — nests its sub-commands, skips deleted
	for (auto metaCommand : commonMetadata->GetCommandArrayObject())
		AppendCommandNode(Group(g_metaCommandCLSID), metaCommand);

	// templates
	for (auto metaTemplates : commonMetadata->GetTemplateArrayObject()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(Group(g_metaTemplateCLSID), metaTemplates);
	}

	//update choice selection
	UpdateChoiceSelection();

	//set init flag
	m_initialized = true;

	//set modify 
	Modify(m_metaData->IsModified());

	//update toolbar 
	UpdateToolbar(nullptr, Group(g_metaAttributeCLSID));
}

bool ibDataReportTree::Load(ibMetaDataReport* metaData)
{
	ClearTree();
	m_metaData = metaData;
	m_metaTreeCtrl->Freeze();
	FillData(); //Fill all data from metaData
	m_metaData->SetMetaTree(this);
	m_metaTreeCtrl->SelectItem(Group(g_metaAttributeCLSID));
	m_metaTreeCtrl->ExpandAll();
	m_metaTreeCtrl->Thaw();
	return true;
}

bool ibDataReportTree::Save()
{
	ibValueMetaObjectReport* commonMetadata = m_metaData->GetReport();
	wxASSERT(commonMetadata);

	commonMetadata->SetName(m_nameValue->GetValue());
	commonMetadata->SetSynonym(m_synonymValue->GetValue());
	commonMetadata->SetComment(m_commentValue->GetValue());

	wxASSERT(m_metaData);

	if (m_metaData->IsModified())
		return m_metaData->SaveDatabase();

	return false;
}