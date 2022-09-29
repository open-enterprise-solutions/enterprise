////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metatree events
////////////////////////////////////////////////////////////////////////////

#include "metatreeWnd.h"

void CMetadataTree::CMetadataTreeWnd::OnLeftDClick(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk()) {
		SelectItem(curItem);
		m_ownerTree->ActivateItem(curItem);
	} //event.Skip();
}

#include "frontend/objinspect/objinspect.h"

void CMetadataTree::CMetadataTreeWnd::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnLeftDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem);
	}
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnRightUp(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		wxMenu* m_defaultMenu = new wxMenu;
		m_ownerTree->PrepareContextMenu(m_defaultMenu, curItem);

		for (auto def_menu : m_defaultMenu->GetMenuItems())
		{
			if (
				def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY

				|| def_menu->GetId() == ID_METATREE_INSERT
				|| def_menu->GetId() == ID_METATREE_REPLACE
				|| def_menu->GetId() == ID_METATREE_SAVE
				)
			{
				continue;
			}

			GetEventHandler()->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		PopupMenu(m_defaultMenu, event.GetPosition());

		for (auto def_menu : m_defaultMenu->GetMenuItems())
		{
			if (
				def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY

				|| def_menu->GetId() == ID_METATREE_INSERT
				|| def_menu->GetId() == ID_METATREE_REPLACE
				|| def_menu->GetId() == ID_METATREE_SAVE
				)
			{
				continue;
			}

			GetEventHandler()->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		delete m_defaultMenu;
	}

	m_ownerTree->PropertyItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnRightDown(wxMouseEvent& event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		wxMenu* m_defaultMenu = new wxMenu;
		m_ownerTree->PrepareContextMenu(m_defaultMenu, curItem);

		for (auto def_menu : m_defaultMenu->GetMenuItems())
		{
			if (
				def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY

				|| def_menu->GetId() == ID_METATREE_INSERT
				|| def_menu->GetId() == ID_METATREE_REPLACE
				|| def_menu->GetId() == ID_METATREE_SAVE
				)
			{
				continue;
			}

			GetEventHandler()->Bind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		PopupMenu(m_defaultMenu, event.GetPosition());

		for (auto def_menu : m_defaultMenu->GetMenuItems())
		{
			if (
				def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY

				|| def_menu->GetId() == ID_METATREE_INSERT
				|| def_menu->GetId() == ID_METATREE_REPLACE
				|| def_menu->GetId() == ID_METATREE_SAVE
				)
			{
				continue;
			}

			GetEventHandler()->Unbind(wxEVT_MENU, &CMetadataTree::CMetadataTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		delete m_defaultMenu;
	}

	m_ownerTree->PropertyItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnRightDClick(wxMouseEvent& event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnMouseMove(wxMouseEvent& event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnBeginDrag(wxTreeEvent& event) {

	wxTreeItemId curItem = event.GetItem();
	if (!curItem.IsOk())
		return; 
	// need to explicitly allow drag
	if (curItem == GetRootItem())
		return;
	IMetaObject *metaObject = m_ownerTree->GetMetaObject(curItem);
	if (metaObject == NULL)
		return;
	m_draggedItem = curItem;
	event.Allow();
}

void CMetadataTree::CMetadataTreeWnd::OnEndDrag(wxTreeEvent& event) {
	
	bool copy = ::wxGetKeyState(WXK_CONTROL);
	wxTreeItemId itemSrc = m_draggedItem, itemDst = event.GetItem();
	m_draggedItem = (wxTreeItemId)0l;
	// ensure that itemDst is not itemSrc or a child of itemSrc
	wxTreeItemId item = itemDst;
	while (item.IsOk()) {
		if (item == itemSrc)
			return;
		item = GetItemParent(item);
	}

	IMetaObject *metaSrcObject = m_ownerTree->GetMetaObject(itemSrc);
	IMetaObject* metaDstObject = m_ownerTree->GetMetaObject(itemDst);
	
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnCreateItem(wxCommandEvent& event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnEditItem(wxCommandEvent& event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnRemoveItem(wxCommandEvent& event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnPropertyItem(wxCommandEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnUpItem(wxCommandEvent& event)
{
	m_ownerTree->UpItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnDownItem(wxCommandEvent& event)
{
	m_ownerTree->DownItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnSortItem(wxCommandEvent& event)
{
	m_ownerTree->SortItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnInsertItem(wxCommandEvent& event)
{
	m_ownerTree->InsertItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnReplaceItem(wxCommandEvent& event)
{
	m_ownerTree->ReplaceItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnSaveItem(wxCommandEvent& event)
{
	m_ownerTree->SaveItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnCommandItem(wxCommandEvent& event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

#include <wx/clipbrd.h>

void CMetadataTree::CMetadataTreeWnd::OnCopyItem(wxCommandEvent& event)
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
				wxCustomDataObject* pdo = new wxCustomDataObject();
				pdo->SetFormat(wxOES_Data);
				pdo->SetData(dataWritter.size(), dataWritter.pointer()); // the +1 is used to force copy of the \0 character
				// tell clipboard about our RTF
				wxTheClipboard->SetData(pdo);
			}
			wxTheClipboard->Close();
		}
	}

	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnPasteItem(wxCommandEvent& event)
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
					objectInspector->RefreshProperty();
				}

				m_ownerTree->Load(m_ownerTree->m_metaData);
			}
		}
		wxTheClipboard->Close();
	}

	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnDebugEvent(wxDebugEvent& event)
{
	if (m_ownerTree->m_docParent)
		return;

	switch (event.GetEventId())
	{
	case EventId_EnterLoop: m_ownerTree->EditModule(event.GetModuleName(), event.GetLine(), true); break;
	}
}

void CMetadataTree::CMetadataTreeWnd::OnPropertyModified(wxFramePropertyEvent& event)
{
	Property* p = event.GetFrameProperty();
	wxASSERT(p);
	if (p->GetType() == PropertyType::PT_WXNAME) {
		IMetaObject* obj = dynamic_cast<IMetaObject*>(p->GetObject());
		wxASSERT(obj);
		if (!m_ownerTree->RenameMetaObject(obj, event.GetValue())) {
			event.Skip();
		}
	}
}

void CMetadataTree::CMetadataTreeWnd::OnSelecting(wxTreeEvent& event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnSelected(wxTreeEvent& event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnCollapsing(wxTreeEvent& event)
{
	if (GetRootItem() == event.GetItem())
		event.Veto();
	else
		event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnExpanding(wxTreeEvent& event)
{
	event.Skip();
}
