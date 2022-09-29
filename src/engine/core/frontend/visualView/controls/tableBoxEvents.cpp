#include "tableBox.h" 
#include "frontend/visualView/visualEditor.h"
#include "appData.h"

void CValueTableBox::OnColumnClick(wxDataViewEvent& event)
{
	CDataViewColumnObject* columnValue = dynamic_cast<CDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(columnValue);

	if (g_visualHostContext != NULL) {

		CVisualEditorContextForm::CVisualEditorHost* visualEditor = g_visualHostContext->GetVisualEditor();
		wxASSERT(visualEditor);

		IValueFrame* columnControl = visualEditor->GetObjectBase(columnValue);
		wxASSERT(columnControl);
		g_visualHostContext->SelectObject(columnControl);
	}

	event.Skip();
}

void CValueTableBox::OnColumnReordered(wxDataViewEvent& event)
{
	CDataViewColumnObject* columnValue =
		dynamic_cast<CDataViewColumnObject*>(event.GetDataViewColumn());

	wxASSERT(columnValue);

	if (g_visualHostContext != NULL) {

		CVisualEditorContextForm::CVisualEditorHost* visualEditor = g_visualHostContext->GetVisualEditor();
		wxASSERT(visualEditor);
		IValueFrame* columnControl = visualEditor->GetObjectBase(columnValue);
		wxASSERT(columnControl);
		if (ChangeChildPosition(columnControl, event.GetColumn())) {
			g_visualHostContext->RefreshEditor();
		}
	}
	else if (!appData->DesignerMode()) {

		CValueTableBoxColumn* columnControl = columnValue->GetControl();
		wxASSERT(columnControl);
		ChangeChildPosition(columnControl, event.GetColumn());
	}

	event.Skip();
}

//*********************************************************************
//*                          System event                             *
//*********************************************************************

#include "metadata/metaObjects/objects/object.h"

void CValueTableBox::OnSelectionChanged(wxDataViewEvent& event)
{
	// event is a wxDataViewEvent
	wxDataViewItem item = event.GetItem();

	if (!item.IsOk())
		return;

	CValue standardProcessing = true;

	CallEvent(m_eventSelection,
		CValue(this), // control
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
	wxDataViewItem item = event.GetItem();

	if (!item.IsOk())
		return;

	if (m_tableModel != NULL) {
		m_tableModel->ActivateItem(m_formOwner,
			item, event.GetColumn()
		);
	}

	CallEvent(m_eventOnActivateRow,
		CValue(this) // control
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
	wxDataViewItem item = event.GetItem();

	if (!item.IsOk())
		return;

	if (m_tableModel->EditableLine(item, event.GetColumn()))
		event.Skip();
	else
		event.Veto(); /*!!!*/
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
	wxDataViewItem item(event.GetItem());

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
	wxMenu menu;

	actionData_t& actions =
		CValueTableBox::GetActions(m_formOwner->GetTypeForm());

	for (unsigned int idx = 0; idx < actions.GetCount(); idx++) {
		const action_identifier_t& id = actions.GetID(idx);
		menu.Append(id, actions.GetCaptionByID(id));
	}

	wxDataViewCtrl* wnd = wxDynamicCast(
		event.GetEventObject(), wxDataViewCtrl
	);

	wxASSERT(wnd);
	wnd->PopupMenu(&menu);
}