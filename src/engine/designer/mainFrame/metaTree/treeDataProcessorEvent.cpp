////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "treeDataProcessor.h"

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); m_ownerTree->ActivateItem(curItem);
	}
	//event.Skip();
}

#include "frontend/mainFrame/mainFrame.h"

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnLeftDown(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk() && curItem == GetSelection()) m_ownerTree->SelectItem();
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRightUp(wxMouseEvent& event)
{
#ifdef __WXOSX__
	event.Skip();
#else
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); SetFocus();
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

	event.Skip();
#endif
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRightDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); SetFocus();
#ifdef __WXOSX__
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
#endif
	}

#ifndef __WXOSX__
	event.Skip();
#endif
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRightDClick(wxMouseEvent& event)
{
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnMouseMove(wxMouseEvent& event)
{
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCreateItem(wxCommandEvent& event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnEditItem(wxCommandEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRemoveItem(wxCommandEvent& event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnPropertyItem(wxCommandEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCommandItem(wxCommandEvent& event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

#include <wx/clipbrd.h>

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCopyItem(wxCommandEvent& event)
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

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnPasteItem(wxCommandEvent& event)
{
	if (!m_ownerTree->IsEditable())
		return;

	const wxTreeItemId& item = m_ownerTree->GetSelectionIdentifier();
	if (!item.IsOk())
		return;

	m_ownerTree->Freeze();

	if (wxTheClipboard->Open()
		&& wxTheClipboard->IsSupported(oes_clipboard_metadata)) {
		wxCustomDataObject data(oes_clipboard_metadata);
		if (wxTheClipboard->GetData(data)) {

			ibValueMetaObject* metaObject = m_ownerTree->NewItem(
				m_ownerTree->GetClassIdentifier(),
				m_ownerTree->GetMetaIdentifier(),
				false
			);

			if (metaObject != nullptr) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				if (metaObject->PasteObject(reader)) {
					objectInspector->SelectObject(metaObject);
				}
				m_ownerTree->FillItem(metaObject, item);
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

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSetFocus(wxFocusEvent& event)
{
	if (event.GetEventType() == wxEVT_SET_FOCUS) {
		docManager->ActivateView(m_metaView);
	}
	else if (event.GetEventType() == wxEVT_KILL_FOCUS) {
		const CAuiDocChildFrame* child =
			static_cast<CAuiDocChildFrame*>(mainFrame->GetActiveChild());
		wxView* view = child ? child->GetView() : docManager->GetAnyUsableView();
		if (view != nullptr && view != docManager->GetCurrentView())
			view->Activate(true);
		docManager->ActivateView(view);
	}

	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSelecting(wxTreeEvent& event)
{
	event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSelected(wxTreeEvent& event)
{
	m_ownerTree->SelectItem(); event.Skip();
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCollapsing(wxTreeEvent& event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnExpanding(wxTreeEvent& event)
{
	m_ownerTree->Expand(); event.Skip();
}
