#include "tableBox.h" 
#include "frontend/visualView/visualEditor.h"
#include "appData.h"

void CValueTableBox::OnColumnClick(wxDataViewEvent& event)
{
	wxDataViewColumnObject* dataViewColumn =
		dynamic_cast<wxDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(dataViewColumn);
	if (g_visualHostContext != NULL) {
		CValueTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		g_visualHostContext->SelectControl(columnControl);
	}

	if (m_tableModel != NULL) {
		sortOrder_t::sortData_t* sort = m_tableModel->GetSortByID(event.GetColumn());
		if (sort != NULL && !sort->m_sortSystem) {
			m_tableModel->ResetSort();
			sort->m_sortAscending = !sort->m_sortAscending;
			sort->m_sortEnable = true;
		}
		else {
			event.Veto();
			return;
		}
	}

	event.Skip();
}

void CValueTableBox::OnColumnReordered(wxDataViewEvent& event)
{
	wxDataViewColumnObject* dataViewColumn =
		dynamic_cast<wxDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(dataViewColumn);
	if (g_visualHostContext != NULL) {
		CValueTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		if (ChangeChildPosition(columnControl, event.GetColumn())) {
			if (g_visualHostContext != NULL)
				g_visualHostContext->RefreshTree();
		}
	}
	else if (!appData->DesignerMode()) {
		CValueTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		ChangeChildPosition(columnControl, event.GetColumn());
	}

	event.Skip();
}

//*********************************************************************
//*                          System event                             *
//*********************************************************************

#include "core/metadata/metaObjects/objects/object.h"

void CValueTableBox::OnSelectionChanged(wxDataViewEvent& event)
{
	// event is a wxDataViewEvent
	const wxDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	CValue standardProcessing = true;
	CallEvent(m_eventSelection,
		GetValue(), // control
		CValue(m_tableModel->GetRowAt(item)), // rowSelected
		standardProcessing //standardProcessing
	);
	if (standardProcessing.GetBoolean()) {
		if (m_tableCurrentLine != NULL)
			m_tableCurrentLine->DecrRef();
		m_tableCurrentLine = m_tableModel->GetRowAt(item);
		m_tableCurrentLine->IncrRef();
		event.Skip();
	}
	else {
		event.Veto();
	}
}

void CValueTableBox::OnItemActivated(wxDataViewEvent& event)
{
	// event is a wxDataViewEvent
	const wxDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	if (m_tableModel != NULL) {
		m_tableModel->ActivateItem(m_formOwner,
			item, event.GetColumn()
		);
	}

	CallEvent(m_eventOnActivateRow,
		GetValue() // control
	);

	event.Skip();
}

void CValueTableBox::OnItemCollapsed(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemExpanded(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemCollapsing(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemExpanding(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemStartEditing(wxDataViewEvent& event)
{
	// event is a wxDataViewEvent
	const wxDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	wxDataViewCtrl* dataViewCtrl = dynamic_cast<wxDataViewCtrl*>(event.GetEventObject());
	if (dataViewCtrl != NULL) {
		wxDataViewColumn* currentColumn = dataViewCtrl->GetCurrentColumn();
		if (currentColumn != NULL) {
			wxDataViewRenderer* renderer = currentColumn->GetRenderer();
			if (renderer != NULL)
				renderer->FinishEditing();
		}
	}
	if (!m_tableModel->EditableLine(item, event.GetColumn())) {
		if (m_tableModel != NULL)
			m_tableModel->EditValue();
		event.Veto(); /*!!!*/
	}
	else
		event.Skip();
}

void CValueTableBox::OnItemEditingStarted(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemEditingDone(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemValueChanged(wxDataViewEvent& event)
{
	event.Skip();
}

void CValueTableBox::OnItemStartDeleting(wxDataViewEvent& event)
{
	// event is a wxDataViewEvent
	const wxDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	CValue cancel = false;
	CallEvent(m_eventBeforeDeleteRow,
		GetValue(), // control
		cancel //cancel
	);

	if (cancel.GetBoolean())
		event.Veto();
	else
		event.Skip();
}

void CValueTableBox::OnHeaderResizing(wxHeaderCtrlEvent& event)
{
	wxDataModelViewCtrl* dataViewCtrl = dynamic_cast<wxDataModelViewCtrl*>(GetWxObject());
	if (dataViewCtrl != NULL) {
		wxDataViewColumnObject* dataViewColumn =
			dynamic_cast<wxDataViewColumnObject*>(dataViewCtrl->GetColumn(event.GetColumn()));
		CValueTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		columnControl->SetWidthColumn(event.GetWidth());
		if (g_visualHostContext != NULL)
			g_visualHostContext->SelectControl(columnControl);
	}
	event.Skip();
}

void CValueTableBox::OnMainWindowClick(wxMouseEvent& event)
{
	if (g_visualHostContext != NULL)
		g_visualHostContext->SelectControl(this);
	event.Skip();
}

#if wxUSE_DRAG_AND_DROP

void CValueTableBox::OnItemBeginDrag(wxDataViewEvent& event)
{
}

void CValueTableBox::OnItemDropPossible(wxDataViewEvent& event)
{
	if (event.GetDataFormat() != wxDF_UNICODETEXT)
		event.Veto();
	else
		event.SetDropEffect(wxDragMove);	// check 'move' drop effect
}

void CValueTableBox::OnItemDrop(wxDataViewEvent& event)
{
	if (event.GetDataFormat() != wxDF_UNICODETEXT) {
		event.Veto();
		return;
	}
}

#endif // wxUSE_DRAG_AND_DROP

void CValueTableBox::OnCommandMenu(wxCommandEvent& event)
{
	CValueTableBox::ExecuteAction(event.GetId(), m_formOwner);
}

void CValueTableBox::OnContextMenu(wxDataViewEvent& event)
{
	const actionData_t& actions =
		CValueTableBox::GetActions(m_formOwner->GetTypeForm());

	wxMenu menu;
	for (unsigned int idx = 0; idx < actions.GetCount(); idx++) {
		const action_identifier_t& id = actions.GetID(idx);
		if (id != wxNOT_FOUND) {
			wxMenuItem* menuItem = menu.Append(id, actions.GetCaptionByID(id));
			//item->SetBitmap(wxNullBitmap);
		}
	}

	wxDataViewCtrl* wnd = wxDynamicCast(
		event.GetEventObject(), wxDataViewCtrl
	);
	wxASSERT(wnd);
	wnd->PopupMenu(&menu);
}

void CValueTableBox::HandleOnScroll(wxScrollWinEvent& event)
{
	wxDataModelViewCtrl* dataViewCtrl =
		dynamic_cast<wxDataModelViewCtrl*>(event.GetEventObject());

	if (dataViewCtrl != NULL) {
		//m_tableModel->RefreshModel(dataViewCtrl->GetTopItem(), dataViewCtrl->GetCountPerPage());
	}

	event.Skip();
}

