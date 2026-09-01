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

		// ⭐ THE ROW IS NOT DRAWN HERE — see ibConfigurationTree::CreateItem. The click asks; the row
		// appears because the metadata answered — see AddItem.
		if (showValue) { OpenObjectForm(createdObject); }

		// ⭐ THE HEADER'S CHOICES FOLLOW WHAT THE TREE NOW HOLDS. Adding a form or a composer changes
		// what "default" may point at — and the FIRST composer becomes the default by itself
		// (ibValueMetaObjectReport::OnCreateComposerObject), so without this the report already had a
		// main composer and the field above still read "<not selected>" (Max, 2026-08-20). Removal
		// and rename already refreshed; creation was the one road that did not.
		UpdateChoiceSelection();
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

	// `select` means what it says — see the twin in treeConfiguration_impl.cpp.
	if (select)
		m_metaTreeCtrl->SelectItem(createdItem);

	m_metaTreeCtrl->Expand(createdItem);

	m_metaTreeCtrl->Thaw();

	if (scroll)
		m_metaTreeCtrl->ScrollTo(createdItem);
	return createdItem;
}


// ⭐ ADDED, ANNOUNCED, HANDLED — the row is drawn where the answer arrives, not at the click. See the
// twin in treeConfiguration_impl.cpp for why `select` is plainly true and `scroll` plainly false.

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
	// ASK, and let the answer come back — the Removed stage closes the editors, the re-read
	// takes the row. Neither is done here.
	m_metaData->RemoveMetaObject(metaObject);

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

// The row is the EVENT's, not the selection — see the twin in treeConfiguration_impl.cpp.
void ibDataReportTree::Collapse(const wxTreeItemId& item)
{
	if (!item.IsOk())
		return;

	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(item));
	if (data != nullptr)
		data->m_expanded = false;
}

void ibDataReportTree::Expand(const wxTreeItemId& item)
{
	if (!item.IsOk())
		return;

	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(item));
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


#include "frontend/artProvider/artProvider.h"

void ibDataReportTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
{
	ibValueMetaObject* metaObject = GetMetaObject(item);

	if (metaObject != nullptr && !AppendMetaMenu(defaultMenu, metaObject))
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

	// ⭐ THE MENU'S OWN ITEMS CARRY THEIR ACTIONS — see the twin in treeConfiguration_impl.cpp.
	eventSrc->PopupMenu(&innerMenu, pos);
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

	// …AND THE DEFAULT COMPOSER, the same way. A report declares two things about itself — the form
	// it opens with and the composer it composes by — and both are chosen here (Max, 2026-08-20).
	// The FIRST composer added becomes the default on its own (ibValueMetaObjectReport::
	// OnCreateComposerObject); this is where that answer becomes visible and changeable.
	m_defaultComposerValue->Clear();
	m_defaultComposerValue->AppendString(_("<not selected>"));

	int defComposer = 0;

	for (auto metaComposer : commonMetadata->GetComposerArrayObject())
	{
		if (metaComposer->IsDeleted())
			continue;

		// WIDEN FIRST, then reinterpret — see the note on the form loop above.
		int selection_id = m_defaultComposerValue->Append(metaComposer->GetName(),
			reinterpret_cast<void*>(static_cast<intptr_t>(metaComposer->GetMetaID())));

		if (commonMetadata->GetDefComposer() == metaComposer->GetMetaID()) {
			defComposer = selection_id;
		}
	}

	m_defaultComposerValue->SetSelection(defComposer);
	m_defaultComposerValue->SendSelectionChangedEvent(wxEVT_CHOICE);
}

// ⭐ ASK, AND LET THE ANSWER COME BACK — see the twin in treeDataProcessor_impl.cpp.
bool ibDataReportTree::RenameMetaObject(ibValueMetaObject* obj, const wxString& sNewName)
{
	return m_metaData->RenameMetaObject(obj, sNewName);
}

// HUB — see the twin in treeDataProcessor_impl.cpp.

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
	// ⭐ AN EXTERNAL REPORT IS A REPORT (Max, 2026-08-20). It derives the very same metaobject, so it
	// already HELD its composers and answered for them — only this tree never showed the group, and
	// what a tree does not show cannot be added to. A report without its composers is a data
	// processor with a different name.
	//
	// LAST, exactly as in the configuration tree: there a report is "a data processor PLUS its
	// composers" and AddReportItem appends the group after everything the processor has
	// (treeConfiguration_impl.cpp). One report, one order, wherever it is opened from.
	{ g_metaComposerCLSID,  wxTRANSLATE("Composers")  },
};

} // namespace

void ibDataReportTree::InitTree()
{
	// SINGULAR: this tree edits ONE report, and the root is that report.
	m_treeRoot = AppendRootItem(g_metaReportCLSID, _("Report"));

	m_groups.clear();
	for (const ibExternalGroupDef& def : s_reportGroups)
		m_groups[def.m_clsid] = AppendGroupItem(m_treeRoot, def.m_clsid,
			wxGetTranslation(wxString::FromUTF8(def.m_label)));
}

void ibDataReportTree::ActivateTree()
{
	if (m_metaData != nullptr)
		objectInspector->SelectObject(GetMetaObject(m_metaTreeCtrl->GetSelection()));
}

// CLOSING THE EDITORS IS PART OF *LEAVING A FILE*, not of redrawing the tree — and those two used
// to be the same call, exactly as they were in the configuration navigator. Now that a rebuild is
// what ANY change to the metadata provokes, a clear that also closed documents would shut every
// editor the moment a property was written in one of them.

void ibDataReportTree::ClearTree()
{
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
	m_metaTreeCtrl->SetItemText(m_treeRoot, commonMetadata->GetName());
	m_metaTreeCtrl->SetItemData(m_treeRoot, new ibTreeItemObject(commonMetadata));

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

	// composers — LAST, as in the configuration tree: what makes a report a report. Each is a
	// declaration of what to read and how to fold it, and the DEFAULT one is what a generated form
	// is built from.
	for (auto metaComposer : commonMetadata->GetComposerArrayObject()) {
		if (metaComposer->IsDeleted())
			continue;
		AppendItem(Group(g_metaComposerCLSID), metaComposer);
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
	CloseDocuments();   // a file is being left — its editors go with it
	ClearTree();

	m_metaData = metaData;
	WatchMetaData(m_metaData);   // off the old list, onto this one — one call, one place
	m_metaTreeCtrl->Freeze();
	FillData(); //Fill all data from metaData

	// …and the metadata learns whether this file may be edited at all. Said HERE because the view
	// sets it on the widget before the metadata is known, and because one file has one view — so
	// there is nobody to disagree with.
	m_metaData->SetReadOnly(m_bReadOnly);
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
