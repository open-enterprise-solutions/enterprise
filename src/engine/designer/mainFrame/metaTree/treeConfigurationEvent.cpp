////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree events
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"
#include <wx/wupdlock.h>   // wxWindowUpdateLocker - RAII Freeze/Thaw (a throwing paste must not leave the tree frozen)

void ibConfigurationTree::ibMetaTreeCtrl::OnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem);
		m_ownerTree->ActivateItem(curItem);
	}
	//event.Skip();
}

#include "frontend/mainFrame/mainFrame.h"

void ibConfigurationTree::ibMetaTreeCtrl::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnLeftDown(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk() && curItem == GetSelection()) m_ownerTree->SelectItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRightUp(wxMouseEvent& event)
{
#ifdef __WXOSX__
	// On macOS, context menu is shown from OnRightDown, skip here
	event.Skip();
#else
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

	event.Skip();
#endif
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRightDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();

		// On macOS, show context menu on mouse-down
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

#ifndef __WXOSX__
	event.Skip();
#endif
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRightDClick(wxMouseEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnMouseMove(wxMouseEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnBeginDrag(wxTreeEvent& event) {

	wxTreeItemId curItem = event.GetItem();
	if (!curItem.IsOk())
		return;
	// need to explicitly allow drag
	if (curItem == GetRootItem())
		return;
	ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(curItem);
	if (metaObject == nullptr)
		return;
	// A read-only configuration is not restructured — the drag copies a metaobject onto another node. Refuse it at
	// the START: OnEndDrag already refused the DROP, but the item still followed the cursor as if it would land.
	if (!m_ownerTree->IsEditable())
		return;
	m_draggedItem = curItem;
	event.Allow();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnEndDrag(wxTreeEvent& event) {

	bool copy = ::wxGetKeyState(WXK_CONTROL);
	wxTreeItemId itemSrc = m_draggedItem, itemDst = event.GetItem();
	m_draggedItem = (wxTreeItemId)0l;

	if (!m_ownerTree->IsEditable())
		return;

	// ensure that itemDst is not itemSrc or a child of itemSrc
	ibValueMetaObject* metaSrcObject = m_ownerTree->GetMetaObject(itemSrc);

	if (metaSrcObject != nullptr) {

		const wxTreeItemId& item = m_ownerTree->GetSelectionIdentifier(itemDst);

		if (!item.IsOk())
			return;

		ibValueMetaObject* createdMetaObject = m_ownerTree->NewItem(
			m_ownerTree->GetClassIdentifier(item),
			m_ownerTree->GetMetaIdentifier(item),
			false
		);

		if (createdMetaObject != nullptr) {

			ibWriterMemory dataWritter;
			if (metaSrcObject->CopyObject(dataWritter)) {

				ibReaderMemory reader(dataWritter.pointer(), dataWritter.size());
				if (createdMetaObject->PasteObject(reader)) {
					m_ownerTree->FillItem(createdMetaObject, item);
				}
			}
		}
	}

	Update();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnStartSearch(wxCommandEvent& event)
{
	m_ownerTree->Search(event.GetString()); //Fill all data from metaData
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCancelSearch(wxCommandEvent& event)
{
	const wxString& strSearch = event.GetString();
	if (strSearch.IsEmpty())
		m_ownerTree->Search(wxEmptyString); //Fill all data from metaData	
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCreateItem(wxCommandEvent& event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnItemActivated(wxTreeEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnEditItem(wxCommandEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnRemoveItem(wxCommandEvent& event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnPropertyItem(wxCommandEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnInsertItem(wxCommandEvent& event)
{
	m_ownerTree->InsertItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnReplaceItem(wxCommandEvent& event)
{
	m_ownerTree->ReplaceItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSaveItem(wxCommandEvent& event)
{
	m_ownerTree->SaveItem();
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCommandItem(wxCommandEvent& event)
{
	m_ownerTree->CommandItem(event.GetId());
	event.Skip();
}

#include <wx/clipbrd.h>
#include "clipboardLock.h"   // the Open/Close pair, taken as a guard — one mechanism, all three trees

void ibConfigurationTree::ibMetaTreeCtrl::OnCopyItem(wxCommandEvent& event)
{
	const wxTreeItemId& item = GetSelection();
	if (!item.IsOk())
		return;

	// Write some text to the clipboard
	const ibClipboardLock clipboard;   // closes on every path out — see clipboardLock.h
	if (clipboard.IsOpen()) {

		ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(item);
		if (metaObject != nullptr) {

			ibWriterMemory dataWritter;
			if (metaObject->CopyObject(dataWritter)) {

				wxDataObjectComposite* composite_object = new wxDataObjectComposite;
				wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_metadata);
				custom_object->SetData(dataWritter.size(), dataWritter.pointer());

				composite_object->Add(custom_object);
				composite_object->Add(new wxTextDataObject(metaObject->GetName()), true);

				// tell clipboard
				wxTheClipboard->SetData(composite_object);
			}
		}
	}

	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnPasteItem(wxCommandEvent& event)
{
	if (!m_ownerTree->IsEditable())
		return;

	const wxTreeItemId& item = m_ownerTree->GetSelectionIdentifier();
	if (!item.IsOk())
		return;

	// RAII Freeze/Thaw: a throw out of PasteObject (or of the cleanup that follows it) used to
	// fly past the paired Thaw() and leave the tree frozen and unresponsive for the rest of the
	// session - the error dialog appeared over a navigator that never came back.
	wxWindowUpdateLocker freeze(m_ownerTree);

	const ibClipboardLock clipboard;
	if (clipboard.IsOpen() && wxTheClipboard->IsSupported(oes_clipboard_metadata)) {

		wxCustomDataObject data(oes_clipboard_metadata);

		if (wxTheClipboard->GetData(data)) {

			ibValueMetaObject* metaObject = m_ownerTree->NewItem(
				m_ownerTree->GetClassIdentifier(),
				m_ownerTree->GetMetaIdentifier(),
				false
			);

			if (metaObject != nullptr) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				// A PASTE THAT FAILED LEAVES NOTHING BEHIND. The metaobject was already created in
				// the metadata by NewItem, so a bad payload used to leave it there — unshown when
				// FillItem was skipped, and saved into the configuration all the same.
				if (metaObject->PasteObject(reader)) {
					m_ownerTree->FillItem(metaObject, item, true, false);
					objectInspector->SelectObject(metaObject);
				}
				else if (ibMetaData* metaData = m_ownerTree->GetMetaData()) {
					metaData->RemoveMetaObject(metaObject);
				}
			}
		}
	}


	RefreshSelectedItem();
	event.Skip();
}

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/mainFrameChild.h"

void ibConfigurationTree::ibMetaTreeCtrl::OnSetFocus(wxFocusEvent& event)
{
	if (docManager != nullptr && event.GetEventType() == wxEVT_SET_FOCUS) {

		const wxTreeItemId& item = GetSelection();

		ibView* view = docManager->GetCurrentView();
		if (m_ownerTree->m_docParent == nullptr &&
			m_metaView != view) {
			if (view != nullptr) view->Activate(false);
			m_metaView->Activate(true);
		}
	}
	else if (docManager != nullptr && event.GetEventType() == wxEVT_KILL_FOCUS) {

		wxWindow* focus_win = event.GetWindow();
		while (focus_win != nullptr && focus_win != objectInspector) {
			focus_win = focus_win->GetParent();
		}

		if (focus_win == nullptr) {

			const ibAuiDocChildFrame* focus_child_win =
				static_cast<ibAuiDocChildFrame*>(mainFrame->GetActiveChild());

			ibView* view = focus_child_win ? focus_child_win->GetView() : docManager->GetAnyUsableView();
			// Do NOT gate this on `m_metaView == docManager->GetCurrentView()`:
			// when the user opens a doc directly from the tree (double-click on
			// a metadata item), the new doc becomes the current view before
			// KILL_FOCUS fires. That guard then skipped re-activation, leaving
			// the toolbar and menu of the newly-opened doc disabled (the tree's
			// SET_FOCUS path had already cleared them via view->Activate(false)).
			// ibDocManager::ActivateView(v, false) is a no-op when v is not the
			// current view, so unconditionally calling it is safe.
			if (m_ownerTree->m_docParent == nullptr &&
				m_metaView != view) {
				m_metaView->Activate(false);
				if (view != nullptr) view->Activate(true);
			}
		}
	}

	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSelecting(wxTreeEvent& event)
{
	event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnSelected(wxTreeEvent& event)
{
	m_ownerTree->SelectItem(); event.Skip();
}

void ibConfigurationTree::ibMetaTreeCtrl::OnCollapsing(wxTreeEvent& event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibConfigurationTree::ibMetaTreeCtrl::OnExpanding(wxTreeEvent& event)
{
	m_ownerTree->Expand(); event.Skip();
}
