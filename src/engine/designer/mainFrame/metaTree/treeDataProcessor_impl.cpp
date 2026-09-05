////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "treeDataProcessor.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/docView/docView.h"
#include "backend/appData.h"
#include "backend/metaCollection/metaCommandObject.h"   // ibValueMetaObjectCommand::GetSubCommands

#include <cstdint>   // intptr_t — the widening step under the client-data cast

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************

void ibDataProcessorTree::ActivateItem(const wxTreeItemId& item)
{
	ibValueMetaObject* currObject = GetMetaObject(item);

	if (currObject == nullptr)
		return;

	OpenObjectForm(currObject);
}

ibValueMetaObject* ibDataProcessorTree::NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject)
{
	return m_metaData->CreateMetaObject(clsid, parent, runObject);
}

ibValueMetaObject* ibDataProcessorTree::CreateItem(bool showValue)
{
	const wxTreeItemId& item = GetSelectionIdentifier();
	if (!item.IsOk()) return nullptr;

	ibValueMetaObject* createdObject = NewItem(
		GetClassIdentifier(),
		GetMetaIdentifier()
	);

	if (createdObject != nullptr) {

		ibPropertyObject* prev_selected = objectInspector->GetSelectedObject();

		// ⭐ THE ROW IS NOT DRAWN HERE — see ibConfigurationTree::CreateItem. The click asks; the
		// row appears because the metadata answered — see AddItem.
		if (showValue) { OpenObjectForm(createdObject); }
	}

	m_metaTreeCtrl->RefreshSelectedItem();
	return createdObject;
}

wxTreeItemId ibDataProcessorTree::FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select, bool scroll)
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
	// A COMMAND HOLDS COMMANDS — a pasted group command must show what it brought with it, the
	// same rule the configuration tree's dispatcher applies.
	else if (metaItem->GetClassType() == g_metaCommandCLSID) {
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(metaItem)->GetSubCommands())
			AppendCommandNode(createdItem, sub);
	}

	m_metaTreeCtrl->InvalidateBestSize();

	// `select` means what it says — see the twin in treeConfiguration_impl.cpp. It used to wrap
	// SetEvtHandlerEnabled around a SelectItem that ran either way, so a caller asking for no
	// selection got one anyway, silently.
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

void ibDataProcessorTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeCtrl->GetSelection();

	if (!selection.IsOk())
		return;

	ibValueMetaObject* currObject = GetMetaObject(selection);

	if (!currObject)
		return;

	OpenObjectForm(currObject);
}

void ibDataProcessorTree::RemoveItem()
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

	const wxTreeItemId& nextSelection = m_metaTreeCtrl->GetFocusedItem();

	if (nextSelection.IsOk()) {
		UpdateToolbar(GetMetaObject(nextSelection), nextSelection);
	}

	//update choice if need
	UpdateChoiceSelection();
}

// CLOSE WHAT THIS ROW STANDS FOR, AND EVERYTHING UNDER IT — see the note on the configuration
// tree's twin. The caller walked the direct children, which are group nodes with no metaobject.
void ibDataProcessorTree::EraseItem(const wxTreeItemId& item)
{
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie))
		EraseItem(child);

	ibValueMetaObject* const metaObject = GetMetaObject(item);
	// NOTHING TO ERASE FOR A GROUP NODE. Without this the comparison below is nullptr == nullptr
	// for every document that has no metaobject either, and the loop closes unrelated editors.
	if (metaObject == nullptr)
		return;

	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr && metaObject == metaDoc->GetMetaObject()) {
			metaDoc->DeleteAllViews();
		}
	}
}

void ibDataProcessorTree::SelectItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE) return;
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);
	objectInspector->SelectObject(metaObject);
}

void ibDataProcessorTree::PropertyItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE) return;
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);
	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	objectInspector->SelectObject(metaObject);
}

// The row is the EVENT's, not the selection — see the twin in treeConfiguration_impl.cpp, where the
// same pair asserted out of RestoreExpanded with nothing selected.
void ibDataProcessorTree::Collapse(const wxTreeItemId& item)
{
	if (!item.IsOk())
		return;

	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(item));
	if (data != nullptr)
		data->m_expanded = false;
}

void ibDataProcessorTree::Expand(const wxTreeItemId& item)
{
	if (!item.IsOk())
		return;

	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(item));
	if (data != nullptr)
		data->m_expanded = true;
}

void ibDataProcessorTree::UpItem()
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
			std::function<void(ibDataProcessorTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibDataProcessorTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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

void ibDataProcessorTree::DownItem()
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
			std::function<void(ibDataProcessorTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibDataProcessorTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
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

void ibDataProcessorTree::SortItem()
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

void ibDataProcessorTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
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

void ibDataProcessorTree::ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos)
{
	wxMenu innerMenu;   // stack — PopupMenu does not take ownership, and it blocks until dismissed
	PrepareContextMenu(&innerMenu, item);

	// ⭐ THE MENU'S OWN ITEMS CARRY THEIR ACTIONS — see the twin in treeConfiguration_impl.cpp.
	eventSrc->PopupMenu(&innerMenu, pos);
}

void ibDataProcessorTree::UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

void ibDataProcessorTree::UpdateChoiceSelection()
{
	m_defaultFormValue->Clear();
	m_defaultFormValue->AppendString(_("<not selected>"));

	ibValueMetaObjectDataProcessor* commonMetadata = m_metaData->GetDataProcessor();
	wxASSERT(commonMetadata);

	int defSelection = 0;

	for (auto metaForm : commonMetadata->GetFormArrayObject()) {
		if (ibValueMetaObjectDataProcessor::eFormDataProcessor != metaForm->GetTypeForm())
			continue;
		// WIDEN FIRST, then reinterpret. `ibMetaID` is a 32-bit int and the client data is a
		// pointer, so the one-step cast is MSVC C4312 / clang -Wint-to-pointer-cast on every
		// toolchain. The value round-trips either way; the intermediate is what says so out loud.
		// The way back already does this (static_cast<ibMetaID>(reinterpret_cast<intptr_t>(…))).
		int selection_id = m_defaultFormValue->Append(metaForm->GetName(),
			reinterpret_cast<void*>(static_cast<intptr_t>(metaForm->GetMetaID())));
		if (commonMetadata->GetDefFormObject() == metaForm->GetMetaID()) {
			defSelection = selection_id;
		}
	}

	m_defaultFormValue->SetSelection(defSelection);
	m_defaultFormValue->SendSelectionChangedEvent(wxEVT_CHOICE);
}

// ⭐ ASK, AND LET THE ANSWER COME BACK. This used to rename through the metadata and then draw the
// consequences itself — the row's text, the editor's tab, the default-form combo — off the SELECTED
// row, which is not necessarily the row of the object being renamed. All three are now done by the
// shared handler for the Renamed stage, for every tree at once and off the object rather than off
// the selection; and a rename that reaches the metadata by any other road (a tool, a paste) draws
// them too, which it never used to.
bool ibDataProcessorTree::RenameMetaObject(ibValueMetaObject* obj, const wxString& sNewName)
{
	return m_metaData->RenameMetaObject(obj, sNewName);
}

// HUB — the same shape the configuration tree uses: a group command holds commands, shown nested.

// THE LAYOUT — one table, read top to bottom, exactly as the configuration tree does it
// (treeConfiguration_impl.cpp). A data processor edited as a FILE shows the same groups in the same
// order as the same object edited inside a configuration; saying that once, as data, is what keeps
// the two from drifting apart again.
namespace {

struct ibExternalGroupDef {
	ibClassID   m_clsid;
	const char* m_label;   // SOURCE string, translated when the node is made (backend/fileKind.cpp rule)
};

const ibExternalGroupDef s_dataProcessorGroups[] = {
	{ g_metaAttributeCLSID, wxTRANSLATE("Attributes") },
	{ g_metaTableCLSID,     wxTRANSLATE("Tables")     },   // RAM tabular sections — a processor is not a reference
	{ g_metaFormCLSID,      wxTRANSLATE("Forms")      },
	// Commands — the object owns them here exactly as it does inside a configuration; this tree
	// simply had no node, so they could be neither seen nor created from a file being edited.
	{ g_metaCommandCLSID,   wxTRANSLATE("Commands")   },
	{ g_metaTemplateCLSID,  wxTRANSLATE("Templates")  },
};

} // namespace

void ibDataProcessorTree::InitTree()
{
	m_treeRoot = AppendRootItem(g_metaDataProcessorCLSID, _("Data processor"));

	m_groups.clear();
	for (const ibExternalGroupDef& def : s_dataProcessorGroups)
		m_groups[def.m_clsid] = AppendGroupItem(m_treeRoot, def.m_clsid,
			wxGetTranslation(wxString::FromUTF8(def.m_label)));
}

void ibDataProcessorTree::ActivateTree()
{
	if (m_metaData != nullptr)
		objectInspector->SelectObject(GetMetaObject(m_metaTreeCtrl->GetSelection()));
}

// CLOSING THE EDITORS IS PART OF *LEAVING A FILE*, not of redrawing the tree — and those two used
// to be the same call, exactly as they were in the configuration navigator. Now that a rebuild is
// what ANY change to the metadata provokes, a clear that also closed documents would shut every
// editor the moment a property was written in one of them.

void ibDataProcessorTree::ClearTree()
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

	//Initialize tree
	InitTree();

}

void ibDataProcessorTree::FillData()
{
	ibValueMetaObjectDataProcessor* commonMetadata = m_metaData->GetDataProcessor();
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

	//update choice selection
	UpdateChoiceSelection();

	//set init flag
	m_initialized = true;

	// 🛑 NOT FROM HERE — see the note in ibConfigurationTree::FillData. A fill is a read; the mark is
	// said once by Load and after that only by a signal.

	//update toolbar
	UpdateToolbar(nullptr, Group(g_metaAttributeCLSID));
}

bool ibDataProcessorTree::Load(ibMetaDataDataProcessor* metaData)
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

	// ⭐ ONCE, HERE — the load half of the rule; see ibConfigurationTree::Load.
	Modify(m_metaData->IsModified());

	m_metaTreeCtrl->SelectItem(Group(g_metaAttributeCLSID));
	m_metaTreeCtrl->ExpandAll();
	m_metaTreeCtrl->Thaw();
	return true;
}

bool ibDataProcessorTree::Save()
{
	ibValueMetaObjectDataProcessor* commonMetadata = m_metaData->GetDataProcessor();
	wxASSERT(commonMetadata);

	commonMetadata->SetName(m_nameValue->GetValue());
	commonMetadata->SetSynonym(m_synonymValue->GetValue());
	commonMetadata->SetComment(m_commentValue->GetValue());

	wxASSERT(m_metaData);

	if (m_metaData->IsModified())
		return m_metaData->SaveDatabase();

	return false;
}
