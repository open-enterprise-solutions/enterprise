////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree events
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"

void ibMetadataTree::ibMetaTreeCtrl::OnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem);
		m_ownerTree->ActivateItem(curItem);
	}
	//event.Skip();
}

#include "frontend/mainFrame/mainFrame.h"

void ibMetadataTree::ibMetaTreeCtrl::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnLeftDown(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk() && curItem == GetSelection()) m_ownerTree->SelectItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnRightUp(wxMouseEvent& event)
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

void ibMetadataTree::ibMetaTreeCtrl::OnRightDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
#ifdef __WXOSX__
		// On macOS, show context menu on mouse-down
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
#endif
	}

#ifndef __WXOSX__
	event.Skip();
#endif
}

void ibMetadataTree::ibMetaTreeCtrl::OnRightDClick(wxMouseEvent& event)
{
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnMouseMove(wxMouseEvent& event)
{
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnBeginDrag(wxTreeEvent& event) {

	wxTreeItemId curItem = event.GetItem();
	if (!curItem.IsOk())
		return;
	// need to explicitly allow drag
	if (curItem == GetRootItem())
		return;
	ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(curItem);
	if (metaObject == nullptr)
		return;
	m_draggedItem = curItem;
	event.Allow();
}

void ibMetadataTree::ibMetaTreeCtrl::OnEndDrag(wxTreeEvent& event) {

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

void ibMetadataTree::ibMetaTreeCtrl::OnStartSearch(wxCommandEvent& event)
{
	m_ownerTree->Search(event.GetString()); //Fill all data from metaData
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnCancelSearch(wxCommandEvent& event)
{
	const wxString& strSearch = event.GetString();
	if (strSearch.IsEmpty())
		m_ownerTree->Search(wxEmptyString); //Fill all data from metaData	
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnCreateItem(wxCommandEvent& event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnItemActivated(wxTreeEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnEditItem(wxCommandEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnRemoveItem(wxCommandEvent& event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnPropertyItem(wxCommandEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnInsertItem(wxCommandEvent& event)
{
	m_ownerTree->InsertItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnReplaceItem(wxCommandEvent& event)
{
	m_ownerTree->ReplaceItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnSaveItem(wxCommandEvent& event)
{
	m_ownerTree->SaveItem();
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnCommandItem(wxCommandEvent& event)
{
	m_ownerTree->CommandItem(event.GetId());
	event.Skip();
}

#include <wx/clipbrd.h>

void ibMetadataTree::ibMetaTreeCtrl::OnCopyItem(wxCommandEvent& event)
{
	const wxTreeItemId& item = GetSelection();
	if (!item.IsOk())
		return;

	// Write some text to the clipboard
	if (wxTheClipboard->Open()) {

		ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(item);
		if (metaObject != nullptr) {

			ibWriterMemory dataWritter;
			if (metaObject->CopyObject(dataWritter)) {

				wxDataObjectComposite* composite_object = new wxDataObjectComposite;
				wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_metadata);
				custom_object->SetData(dataWritter.size(), dataWritter.pointer()); // the +1 is used to force copy of the \0 character		

				composite_object->Add(custom_object);
				composite_object->Add(new wxTextDataObject(metaObject->GetName()), true);

				// tell clipboard 
				wxTheClipboard->SetData(composite_object);
			}

			wxTheClipboard->Close();
		}
	}

	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnPasteItem(wxCommandEvent& event)
{
	if (!m_ownerTree->IsEditable())
		return;

	const wxTreeItemId& item = m_ownerTree->GetSelectionIdentifier();
	if (!item.IsOk())
		return;

	m_ownerTree->Freeze();

	if (wxTheClipboard->Open() && wxTheClipboard->IsSupported(oes_clipboard_metadata)) {

		wxCustomDataObject data(oes_clipboard_metadata);

		if (wxTheClipboard->GetData(data)) {

			ibValueMetaObject* metaObject = m_ownerTree->NewItem(
				m_ownerTree->GetClassIdentifier(),
				m_ownerTree->GetMetaIdentifier(),
				false
			);

			if (metaObject != nullptr) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				if (metaObject->PasteObject(reader))
					m_ownerTree->FillItem(metaObject, item, true, false);
				objectInspector->SelectObject(metaObject);
			}
		}

		wxTheClipboard->Close();
	}

	m_ownerTree->Thaw();

	RefreshSelectedItem();
	event.Skip();
}

#include "frontend/docView/docManager.h"
#include "frontend/mainFrame/mainFrameChild.h"

void ibMetadataTree::ibMetaTreeCtrl::OnSetFocus(wxFocusEvent& event)
{
	if (docManager != nullptr && event.GetEventType() == wxEVT_SET_FOCUS) {

		const wxTreeItemId& item = GetSelection();

		wxView* view = docManager->GetCurrentView();
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

			const CAuiDocChildFrame* focus_child_win =
				static_cast<CAuiDocChildFrame*>(mainFrame->GetActiveChild());

			wxView* view = focus_child_win ? focus_child_win->GetView() : docManager->GetAnyUsableView();
			if (m_ownerTree->m_docParent == nullptr &&
				m_metaView != view &&
				m_metaView == docManager->GetCurrentView()) {
				m_metaView->Activate(false);
				if (view != nullptr) view->Activate(true);
			}
		}
	}

	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnSelecting(wxTreeEvent& event)
{
	event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnSelected(wxTreeEvent& event)
{
	m_ownerTree->SelectItem(); event.Skip();
}

void ibMetadataTree::ibMetaTreeCtrl::OnCollapsing(wxTreeEvent& event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibMetadataTree::ibMetaTreeCtrl::OnExpanding(wxTreeEvent& event)
{
	m_ownerTree->Expand(); event.Skip();
}
