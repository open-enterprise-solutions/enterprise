////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "treeDataProcessor.h"
#include <wx/wupdlock.h>   // wxWindowUpdateLocker - RAII Freeze/Thaw (a throwing paste must not leave the tree frozen)

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
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
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


#include <wx/clipbrd.h>
#include "clipboardLock.h"   // the Open/Close pair, taken as a guard — one mechanism, all three trees

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCopyItem(wxCommandEvent& event)
{
	const wxTreeItemId& item = GetSelection();
	if (!item.IsOk())
		return;

	// Write some text to the clipboard
	const ibClipboardLock clipboard;   // closes on every path out — see clipboardLock.h
	if (clipboard.IsOpen()) {

		ibValueMetaObject* metaObject = m_ownerTree->GetMetaObject(item);
		ibMetaData* const metaData = m_ownerTree->GetMetaData();

		if (metaObject != nullptr && metaData != nullptr) {

			ibWriterMemory dataWritter;
			if (metaData->CopyMetaObject(metaObject, dataWritter)) {

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

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnPasteItem(wxCommandEvent& event)
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
	if (clipboard.IsOpen()
		&& wxTheClipboard->IsSupported(oes_clipboard_metadata)) {
		wxCustomDataObject data(oes_clipboard_metadata);
		if (wxTheClipboard->GetData(data)) {

			// ⭐ WHERE IT GOES AND WHAT GOES THERE — see the twin in treeConfigurationEvent.cpp.
			// The shell, the read, the announcement that draws the row and the cleanup after a bad
			// payload are the paste's, not the caller's.
			if (ibMetaData* metaData = m_ownerTree->GetMetaData()) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				metaData->PasteMetaObject(
					m_ownerTree->GetClassIdentifier(),
					m_ownerTree->GetMetaIdentifier(),
					reader);
			}
		}
	}

	RefreshSelectedItem();

	event.Skip();
}

#include "frontend/docView/docView.h"
#include "frontend/mainFrame/mainFrameChild.h"

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSetFocus(wxFocusEvent& event)
{
	if (event.GetEventType() == wxEVT_SET_FOCUS) {
		docManager->ActivateView(m_metaView);
	}
	else if (event.GetEventType() == wxEVT_KILL_FOCUS) {
		const ibAuiDocChildFrame* child =
			static_cast<ibAuiDocChildFrame*>(mainFrame->GetActiveChild());
		ibView* view = child ? child->GetView() : docManager->GetAnyUsableView();
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
		m_ownerTree->Collapse(event.GetItem()); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibDataProcessorTree::ibDataProcessorTreeCtrl::OnExpanding(wxTreeEvent& event)
{
	m_ownerTree->Expand(event.GetItem()); event.Skip();
}
