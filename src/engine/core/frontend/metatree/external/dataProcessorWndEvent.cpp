////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorWnd.h"

void CDataProcessorTree::CDataProcessorTreeWnd::OnLeftDClick(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) { 
		SelectItem(curItem); m_ownerTree->ActivateItem(curItem);
	} 
	//event.Skip();
}

#include "frontend/mainFrame.h"

void CDataProcessorTree::CDataProcessorTreeWnd::OnLeftUp(wxMouseEvent &event)
{
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnLeftDown(wxMouseEvent &event)
{
	//wxTreeItemId curItem = HitTest(event.GetPosition());
	//if (curItem.IsOk()) { 
	//	SelectItem(curItem); /*m_ownerTree->PropertyItem();*/ 
	//}
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnRightUp(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); SetFocus();
		wxMenu *defaultMenu = new wxMenu;
		m_ownerTree->PrepareContextMenu(defaultMenu, curItem);
		for (auto def_menu : defaultMenu->GetMenuItems()) {
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY) {
				continue;
			}
			GetEventHandler()->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnCommandItem, this, def_menu->GetId());
		}
		PopupMenu(defaultMenu, event.GetPosition());
		for (auto def_menu : defaultMenu->GetMenuItems()) {
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY) {
				continue;
			}
			GetEventHandler()->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnCommandItem, this, def_menu->GetId());
		}
		delete defaultMenu;
	}
	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnRightDown(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem); SetFocus();
		wxMenu *defaultMenu = new wxMenu;
		m_ownerTree->PrepareContextMenu(defaultMenu, curItem);
		for (auto def_menu : defaultMenu->GetMenuItems()) {
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY) {
				continue;
			}
			GetEventHandler()->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnCommandItem, this, def_menu->GetId());
		}
		PopupMenu(defaultMenu, event.GetPosition());
		for (auto def_menu : defaultMenu->GetMenuItems()) {
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY) {
				continue;
			}
			GetEventHandler()->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnCommandItem, this, def_menu->GetId());
		}
		delete defaultMenu;
	}
	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnRightDClick(wxMouseEvent &event)
{
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnKeyUp(wxKeyEvent &event)
{
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnKeyDown(wxKeyEvent &event)
{
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnMouseMove(wxMouseEvent &event)
{
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnCreateItem(wxCommandEvent &event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnEditItem(wxCommandEvent &event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnRemoveItem(wxCommandEvent &event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnPropertyItem(wxCommandEvent &event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnCommandItem(wxCommandEvent &event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

#include <wx/clipbrd.h>

void CDataProcessorTree::CDataProcessorTreeWnd::OnCopyItem(wxCommandEvent &event)
{
	wxTreeItemId item = GetSelection();
	if (!item.IsOk())
		return;
	// Write some text to the clipboard
	if (wxTheClipboard->Open()) {
		IMetaObject* metaObject = m_ownerTree->GetMetaObject(item);
		if (metaObject != NULL) {
			CMemoryWriter dataWritter;
			if (metaObject->CopyObject(dataWritter)) {
				// create an RTF data object
				wxCustomDataObject* pdo = new wxCustomDataObject(wxOES_Data);
				// the +1 is used to force copy of the \0 character
				pdo->SetData(dataWritter.size(), dataWritter.pointer());
				// tell clipboard about our RTF
				wxTheClipboard->SetData(pdo);
			}
			wxTheClipboard->Close();
		}
	}

	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnPasteItem(wxCommandEvent &event)
{
	if (m_ownerTree->m_bReadOnly)
		return;

	wxTreeItemId item = GetSelection();
	if (!item.IsOk())
		return;
	if (wxTheClipboard->Open()
		&& wxTheClipboard->IsSupported(wxOES_Data)) {
		wxCustomDataObject data(wxOES_Data);
		if (wxTheClipboard->GetData(data)) {
			IMetaObject* metaObject = m_ownerTree->CreateItem(false);
			if (metaObject != NULL) {
				CMemoryReader reader(data.GetData(), data.GetDataSize());
				if (metaObject->PasteObject(reader)) {
					objectInspector->SelectObject(metaObject);
				}
				m_ownerTree->Load(m_ownerTree->m_metaData);
			}
		}
		wxTheClipboard->Close();
	}

	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnDebugEvent(wxDebugEvent &event)
{
	switch (event.GetEventId())
	{
	case EventId_EnterLoop: 
		m_ownerTree->EditModule(event.GetModuleName(), event.GetLine(), true); 	
		break;
	}
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnSelecting(wxTreeEvent &event)
{
	event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnSelected(wxTreeEvent &event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnCollapsing(wxTreeEvent &event)
{
	if (GetRootItem() != event.GetItem()) {
		m_ownerTree->Collapse(); event.Skip();
	}
	else {
		event.Veto();
	}
}

void CDataProcessorTree::CDataProcessorTreeWnd::OnExpanding(wxTreeEvent &event)
{
	m_ownerTree->Expand(); event.Skip();
}
