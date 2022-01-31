////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "dataReportWnd.h"

void CDataReportTree::CDataReportTreeWnd::OnLeftDClick(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) { SelectItem(curItem); m_ownerTree->ActivateItem(curItem); } //event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnLeftUp(wxMouseEvent &event)
{
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnLeftDown(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());
	if (curItem.IsOk()) { SelectItem(curItem); /*m_ownerTree->PropertyItem();*/ }
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnRightUp(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		wxMenu *m_defaultMenu = new wxMenu;
		m_ownerTree->PrepareContextMenu(m_defaultMenu, curItem);

		for (auto def_menu : m_defaultMenu->GetMenuItems())
		{
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY)
			{
				continue;
			}

			GetEventHandler()->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		PopupMenu(m_defaultMenu, event.GetPosition());

		for (auto def_menu : m_defaultMenu->GetMenuItems())
		{
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY)
			{
				continue;
			}

			GetEventHandler()->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		delete m_defaultMenu;
	}

	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnRightDown(wxMouseEvent &event)
{
	wxTreeItemId curItem = HitTest(event.GetPosition());

	if (curItem.IsOk())
	{
		SelectItem(curItem); SetFocus();
		wxMenu *defaultMenu = new wxMenu;
		m_ownerTree->PrepareContextMenu(defaultMenu, curItem);

		for (auto def_menu : defaultMenu->GetMenuItems())
		{
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY)
			{
				continue;
			}

			GetEventHandler()->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		PopupMenu(defaultMenu, event.GetPosition());

		for (auto def_menu : defaultMenu->GetMenuItems())
		{
			if (def_menu->GetId() == ID_METATREE_NEW
				|| def_menu->GetId() == ID_METATREE_EDIT
				|| def_menu->GetId() == ID_METATREE_REMOVE
				|| def_menu->GetId() == ID_METATREE_PROPERTY)
			{
				continue;
			}

			GetEventHandler()->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCommandItem, this, def_menu->GetId());
		}

		delete defaultMenu;
	}

	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnRightDClick(wxMouseEvent &event)
{
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnKeyUp(wxKeyEvent &event)
{
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnKeyDown(wxKeyEvent &event)
{
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnMouseMove(wxMouseEvent &event)
{
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnCreateItem(wxCommandEvent &event)
{
	m_ownerTree->CreateItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnEditItem(wxCommandEvent &event)
{
	m_ownerTree->EditItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnRemoveItem(wxCommandEvent &event)
{
	m_ownerTree->RemoveItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnPropertyItem(wxCommandEvent &event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnCommandItem(wxCommandEvent &event)
{
	m_ownerTree->CommandItem(event.GetId()); event.Skip();
}

#include <wx/clipbrd.h>

void CDataReportTree::CDataReportTreeWnd::OnCopyItem(wxCommandEvent &event)
{
	wxTreeItemId item = GetSelection();

	if (!item.IsOk())
		return;

	// Write some text to the clipboard
	if (wxTheClipboard->Open())
	{
		IMetaObject *metaObject = m_ownerTree->GetMetaObject(item);

		if (metaObject)
		{
			CMemoryWriter dataWritter;

			if (metaObject->SaveMeta(dataWritter))
			{
				wxCustomDataObject *dataCustomObject = new wxCustomDataObject;
				dataCustomObject->SetData(dataWritter.size(), dataWritter.pointer());

				// This data objects are held by the clipboard,
				// so do not delete them in the app.
				wxTheClipboard->SetData(dataCustomObject);
				wxTheClipboard->Close();
			}
		}
	}

	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnPasteItem(wxCommandEvent &event)
{
	wxTreeItemId item = GetSelection();

	if (!item.IsOk())
		return;

	// Read some text
	if (wxTheClipboard->Open())
	{
		if (wxTheClipboard->IsSupported(wxDF_TEXT))
		{
			wxTextDataObject data;
			wxTheClipboard->GetData(data);
		}
		else
		{
			wxCustomDataObject data;
			wxTheClipboard->GetData(data);

			CMemoryReader dateReader(data.GetData(), data.GetDataSize());
			m_ownerTree->CreateItem();
		}

		wxTheClipboard->Close();
	}

	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnDebugEvent(wxDebugEvent &event)
{
	switch (event.GetEventId())
	{
	case EventId_EnterLoop: m_ownerTree->EditModule(event.GetModuleName(), event.GetLine(), true); break;
	}
}

void CDataReportTree::CDataReportTreeWnd::OnPropertyModified(wxFramePropertyEvent &event)
{
	Property *p = event.GetFrameProperty();
	wxASSERT(p);
	if (p->GetType() == PropertyType::PT_WXNAME)
	{
		IMetaObject *obj = dynamic_cast<IMetaObject *>(p->GetObject());
		wxASSERT(obj);
		if (!m_ownerTree->RenameMetaObject(obj, p->GetValue()))
			event.Skip();
	}
}

void CDataReportTree::CDataReportTreeWnd::OnSelecting(wxTreeEvent &event)
{
	event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnSelected(wxTreeEvent &event)
{
	m_ownerTree->PropertyItem(); event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnCollapsing(wxTreeEvent &event)
{
	if (GetRootItem() == event.GetItem())
		event.Veto();
	else
		event.Skip();
}

void CDataReportTree::CDataReportTreeWnd::OnExpanding(wxTreeEvent &event)
{
	event.Skip();
}
