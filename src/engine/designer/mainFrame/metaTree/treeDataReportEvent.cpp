////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"
#include <wx/wupdlock.h>   // wxWindowUpdateLocker - RAII Freeze/Thaw (a throwing paste must not leave the tree frozen)

void ibDataReportTree::ibDataReportTreeCtrl::OnLeftDClick(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); m_ownerTree->ActivateItem(curItem);
	}
	//event.Skip();
}

#include "frontend/mainFrame/mainFrame.h"

void ibDataReportTree::ibDataReportTreeCtrl::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnLeftDown(wxMouseEvent& event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk() && curItem == GetSelection()) m_ownerTree->SelectItem();
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRightUp(wxMouseEvent& event)
{
#ifdef __WXOSX__
	// On macOS, context menu is shown from OnRightDown
	event.Skip();
#else
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

	// (the extra m_ownerTree->SelectItem() that stood here is OnSelected's job — the selection
	// above already raises it, and neither the twin nor the configuration tree calls it twice)
	event.Skip();
#endif
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRightDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
	}

#ifndef __WXOSX__
	event.Skip();
#endif
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRightDClick(wxMouseEvent& event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnMouseMove(wxMouseEvent& event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnCreateItem(wxCommandEvent& event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnEditItem(wxCommandEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRemoveItem(wxCommandEvent& event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnPropertyItem(wxCommandEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnCommandItem(wxCommandEvent& event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

#include <wx/clipbrd.h>
#include "clipboardLock.h"   // the Open/Close pair, taken as a guard — one mechanism, all three trees

void ibDataReportTree::ibDataReportTreeCtrl::OnCopyItem(wxCommandEvent& event)
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

void ibDataReportTree::ibDataReportTreeCtrl::OnPasteItem(wxCommandEvent& event)
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

			ibValueMetaObject* metaObject = m_ownerTree->NewItem(
				m_ownerTree->GetClassIdentifier(),
				m_ownerTree->GetMetaIdentifier(),
				false
			);

			if (metaObject != nullptr) {
				ibReaderMemory reader(data.GetData(), data.GetDataSize());
				// Same rule as the configuration tree: a paste that failed leaves nothing behind,
				// and the object inspector is only pointed at an object that made it into the tree.
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

void ibDataReportTree::ibDataReportTreeCtrl::OnSetFocus(wxFocusEvent& event)
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

void ibDataReportTree::ibDataReportTreeCtrl::OnSelecting(wxTreeEvent& event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnSelected(wxTreeEvent& event)
{
	m_ownerTree->SelectItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnCollapsing(wxTreeEvent& event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibDataReportTree::ibDataReportTreeCtrl::OnExpanding(wxTreeEvent& event)
{
	m_ownerTree->Expand(); event.Skip();
}
