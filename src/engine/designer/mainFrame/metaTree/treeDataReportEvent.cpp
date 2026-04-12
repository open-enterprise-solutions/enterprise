////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"

void ibDataReportTree::ibDataReportTreeCtrl::OnLeftDClick(wxMouseEvent &event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); m_ownerTree->ActivateItem(curItem);
	}
	//event.Skip();
}

#include "frontend/mainFrame/mainFrame.h"

void ibDataReportTree::ibDataReportTreeCtrl::OnLeftUp(wxMouseEvent &event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnLeftDown(wxMouseEvent &event)
{
	const wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk() && curItem == GetSelection()) m_ownerTree->SelectItem();
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRightUp(wxMouseEvent &event)
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

	m_ownerTree->SelectItem(); event.Skip();
#endif
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRightDown(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
#ifdef __WXOSX__
		m_ownerTree->ShowContextMenu(this, curItem, event.GetPosition());
#endif
	}

#ifndef __WXOSX__
	event.Skip();
#endif
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRightDClick(wxMouseEvent &event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnKeyUp(wxKeyEvent &event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnKeyDown(wxKeyEvent &event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnMouseMove(wxMouseEvent &event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnCreateItem(wxCommandEvent &event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnEditItem(wxCommandEvent &event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnRemoveItem(wxCommandEvent &event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnPropertyItem(wxCommandEvent &event)
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

void ibDataReportTree::ibDataReportTreeCtrl::OnCommandItem(wxCommandEvent &event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

#include <wx/clipbrd.h>

void ibDataReportTree::ibDataReportTreeCtrl::OnCopyItem(wxCommandEvent &event)
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

void ibDataReportTree::ibDataReportTreeCtrl::OnPasteItem(wxCommandEvent &event)
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
				if (metaObject->PasteObject(reader)) 		
					m_ownerTree->FillItem(metaObject, item);
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

void ibDataReportTree::ibDataReportTreeCtrl::OnSetFocus(wxFocusEvent& event)
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

void ibDataReportTree::ibDataReportTreeCtrl::OnSelecting(wxTreeEvent &event)
{
	event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnSelected(wxTreeEvent &event)
{
	m_ownerTree->SelectItem(); event.Skip();
}

void ibDataReportTree::ibDataReportTreeCtrl::OnCollapsing(wxTreeEvent &event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(); event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibDataReportTree::ibDataReportTreeCtrl::OnExpanding(wxTreeEvent &event)
{
	m_ownerTree->Expand(); event.Skip();
}
