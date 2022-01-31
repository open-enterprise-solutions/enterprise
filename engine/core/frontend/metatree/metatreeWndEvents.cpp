////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metatree events
////////////////////////////////////////////////////////////////////////////

#include "metatreeWnd.h"

void CMetadataTree::CMetadataTreeWnd::OnLeftDClick(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk()) {
		SelectItem(curItem);
		m_ownerTree->ActivateItem(curItem);
	} //event.Skip();
}

#include "frontend/objinspect/objinspect.h"

void CMetadataTree::CMetadataTreeWnd::OnLeftUp(wxMouseEvent &event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnLeftDown(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) {
		SelectItem(curItem);
	}
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnRightUp(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		wxMenu *m_defaultMenu = new wxMenu;
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

void CMetadataTree::CMetadataTreeWnd::OnRightDown(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		wxMenu *m_defaultMenu = new wxMenu;
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

void CMetadataTree::CMetadataTreeWnd::OnRightDClick(wxMouseEvent &event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnKeyUp(wxKeyEvent &event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnKeyDown(wxKeyEvent &event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnMouseMove(wxMouseEvent &event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnCreateItem(wxCommandEvent &event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnEditItem(wxCommandEvent &event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnRemoveItem(wxCommandEvent &event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnPropertyItem(wxCommandEvent &event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnInsertItem(wxCommandEvent &event)
{
	m_ownerTree->InsertItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnReplaceItem(wxCommandEvent &event)
{
	m_ownerTree->ReplaceItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnSaveItem(wxCommandEvent &event)
{
	m_ownerTree->SaveItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnCommandItem(wxCommandEvent &event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

static wxMemoryBuffer s_memoryBuf;

void CMetadataTree::CMetadataTreeWnd::OnCopyItem(wxCommandEvent &event)
{
	wxTreeItemId item = GetSelection();

	if (!item.IsOk())
		return;

	// Write some text to the clipboard
	/*if (wxTheClipboard->Open()) {
		IMetaObject *metaObject = m_ownerTree->GetMetaObject(item);
		if (metaObject) {
			CMemoryWriter dataWritter;
			unsigned int propertyCount = metaObject->GetPropertyCount();
			dataWritter.w_u32(propertyCount);
			// copiamos las propiedades
			for (unsigned i = 0; i < propertyCount; i++) {
				Property *objProp = metaObject->GetProperty(i);
				wxASSERT(objProp);
				dataWritter.w_stringZ(objProp->GetName());
				dataWritter.w_stringZ(objProp->GetValue());
			}

			unsigned int eventCount = metaObject->GetEventCount();
			dataWritter.w_u32(eventCount);
			// ...and the event handlers
			for (unsigned i = 0; i < eventCount; i++) {
				Event *objEvent = metaObject->GetEvent(i);
				wxASSERT(objEvent);
				dataWritter.w_stringZ(objEvent->GetName());
				dataWritter.w_stringZ(objEvent->GetValue());
			}

			/*wxCustomDataObject *dataCustomObject =
				new wxCustomDataObject(wxDF_TEXT);
			dataCustomObject->SetData(dataWritter.size(), dataWritter.pointer());

			// This data objects are held by the clipboard,
			// so do not delete them in the app.
			wxTheClipboard->SetData(dataCustomObject);
			wxTheClipboard->Close();
		}
	}*/

	IMetaObject *metaObject = m_ownerTree->GetMetaObject(item);
	if (metaObject) {
		s_memoryBuf.Clear();
		CMemoryWriter dataWritter;
		unsigned int propertyCount = metaObject->GetPropertyCount();
		dataWritter.w_u32(propertyCount);
		// copiamos las propiedades
		for (unsigned i = 0; i < propertyCount; i++) {
			Property *objProp = metaObject->GetProperty(i);
			wxASSERT(objProp);
			if (objProp->GetType() != PropertyType::PT_WXNAME) {
				dataWritter.w_stringZ(objProp->GetName());
				dataWritter.w_stringZ(objProp->GetValue());
			}
		}
		s_memoryBuf.AppendData(dataWritter.pointer(), dataWritter.size());
	}

	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnPasteItem(wxCommandEvent &event)
{
	wxTreeItemId item = GetSelection();

	if (!item.IsOk())
		return;

	// Read some text
	/*if (wxTheClipboard->Open()) {
		if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
			wxTextDataObject dataTextObject;
			wxTheClipboard->GetData(dataTextObject);
		}
		wxTheClipboard->Close();
	}*/

	if (!s_memoryBuf.IsEmpty()) {
		m_ownerTree->CreateItem();
		IMetaObject *metaObject =
			m_ownerTree->GetMetaObject(GetSelection());
		if (metaObject) {
			CMemoryReader reader(s_memoryBuf.GetData(), s_memoryBuf.GetDataLen());
			unsigned int propertyCount = reader.r_u32();
			for (unsigned i = 0; i < propertyCount; i++) {
				wxString propName; reader.r_stringZ(propName);
				Property *objProp = metaObject->GetProperty(propName);
				if (objProp) {
					wxString propValue; reader.r_stringZ(propValue);
					objProp->SetValue(propValue);
				}
			}
			metaObject->SaveProperty();
			objectInspector->RefreshProperty();
		}
	}

	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnDebugEvent(wxDebugEvent &event)
{
	if (m_ownerTree->m_docParent)
		return;

	switch (event.GetEventId())
	{
	case EventId_EnterLoop: m_ownerTree->EditModule(event.GetModuleName(), event.GetLine(), true); break;
	}
}

void CMetadataTree::CMetadataTreeWnd::OnPropertyModified(wxFramePropertyEvent &event)
{
	Property *p = event.GetFrameProperty();
	wxASSERT(p);
	if (p->GetType() == PropertyType::PT_WXNAME)
	{
		IMetaObject *obj = dynamic_cast<IMetaObject *>(p->GetObject());
		wxASSERT(obj);
		if (!m_ownerTree->RenameMetaObject(obj, p->GetValue())) {
			event.Skip();
		}
	}
}

void CMetadataTree::CMetadataTreeWnd::OnSelecting(wxTreeEvent &event)
{
	event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnSelected(wxTreeEvent &event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnCollapsing(wxTreeEvent &event)
{
	if (GetRootItem() == event.GetItem())
		event.Veto();
	else
		event.Skip();
}

void CMetadataTree::CMetadataTreeWnd::OnExpanding(wxTreeEvent &event)
{
	event.Skip();
}
