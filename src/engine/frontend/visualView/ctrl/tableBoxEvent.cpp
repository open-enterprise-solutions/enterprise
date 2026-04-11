#include "tableBox.h" 
#include "tableBoxColumnRenderer.h"

#include "backend/appData.h"

void ibValueModelTableBox::OnColumnClick(ibDataViewEvent& event)
{
	ibDataViewColumnObject* dataViewColumn =
		dynamic_cast<ibDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(dataViewColumn);
	if (g_visualHostContext != nullptr) {
		ibValueModelTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		g_visualHostContext->SelectControl(columnControl);
	}

	if (m_tableModel != nullptr) {
		ibSortOrder::ibSortData* sort = m_tableModel->GetSortByID(event.GetColumn());
		if (sort != nullptr && !sort->m_sortSystem) {

			m_tableModel->ResetSort();

			sort->m_sortAscending = !sort->m_sortAscending;
			sort->m_sortEnable = true;

			if (!appData->DesignerMode()) {

				ibDataViewCtrl* dataViewCtrl = dataViewColumn->GetOwner();
				wxASSERT(dataViewCtrl);

				try {
					m_tableModel->CallRefreshModel(dataViewCtrl->GetTopItem(), dataViewCtrl->GetCountPerPage());
				}
				catch (const ibBackendException* err) {
					dataViewCtrl->AssociateModel(nullptr);
					throw(err);
				}

				if (m_tableCurrentLine != nullptr && !m_tableModel->ValidateReturnLine(m_tableCurrentLine)) {
					const ibDataViewItem& currLine = m_tableModel->FindRowValue(&(*m_tableCurrentLine));
					if (currLine.IsOk())
						m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
					else m_tableCurrentLine.Reset();
				}

				if (m_tableCurrentLine != nullptr) {
					dataViewCtrl->Select(
						m_tableCurrentLine->GetLineItem()
					);
				}
			}
		}
		else {
			event.Veto();
			return;
		}
	}

	event.Skip();
}

void ibValueModelTableBox::OnColumnReordered(ibDataViewEvent& event)
{
	ibDataViewColumnObject* dataViewColumn =
		dynamic_cast<ibDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(dataViewColumn);

	ibValueModelTableBoxColumn* columnObject = dataViewColumn->GetControl();
	wxASSERT(columnObject);
	if (ChangeChildPosition(columnObject, event.GetColumn())) {
		if (g_visualHostContext != nullptr) g_visualHostContext->RefreshEditor();
	}

	SetCalculateColumnPos();
	event.Skip();
}

//*********************************************************************
//*                          System event                             *
//*********************************************************************

void ibValueModelTableBox::OnSelectionChanged(ibDataViewEvent& event)
{
	// event is a ibDataViewEvent
	const ibDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	ibValue standardProcessing = true;
	CallAsEvent(m_eventSelection,
		GetValue(), // control
		ibValue(m_tableModel->GetRowAt(item)), // rowSelected
		standardProcessing //standardProcessing
	);
	if (standardProcessing.GetBoolean()) {
		m_tableCurrentLine = m_tableModel->GetRowAt(item);
		event.Skip();
	}
	else {
		event.Veto();
	}
}

void ibValueModelTableBox::OnItemActivated(ibDataViewEvent& event)
{
	// event is a ibDataViewEvent
	const ibDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	if (m_tableModel != nullptr) {
		m_tableModel->ActivateItem(m_formOwner,
			item, event.GetColumn()
		);
	}

	CallAsEvent(m_eventOnActivateRow,
		GetValue() // control
	);

	event.Skip();
}

void ibValueModelTableBox::OnItemCollapsed(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemExpanded(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemCollapsing(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemExpanding(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemStartEditing(ibDataViewEvent& event)
{
	// event is a ibDataViewEvent
	const ibDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	ibDataViewCtrl* dataViewCtrl = dynamic_cast<ibDataViewCtrl*>(event.GetEventObject());
	if (dataViewCtrl != nullptr) {
		ibDataViewColumn* currentColumn = dataViewCtrl->GetCurrentColumn();
		if (currentColumn != nullptr) {
			ibDataViewRenderer* renderer = currentColumn->GetRenderer();
			if (renderer != nullptr) renderer->FinishEditing();
		}
	}
	if (!m_tableModel->EditableLine(item, event.GetColumn())) {
		if (m_tableModel != nullptr) m_tableModel->EditValue();
		event.Veto(); /*!!!*/
	}
	else
		event.Skip();
}

void ibValueModelTableBox::OnItemEditingStarted(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemEditingDone(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemValueChanged(ibDataViewEvent& event)
{
	event.Skip();
}

void ibValueModelTableBox::OnItemStartInserting(ibDataViewEvent& event)
{
	if (m_tableModel != nullptr && !m_tableModel->IsCallRefreshModel())
		m_formOwner->RefreshForm();
	else if (m_tableModel == nullptr)
		m_formOwner->RefreshForm();

	event.Skip();
}

void ibValueModelTableBox::OnItemStartDeleting(ibDataViewEvent& event)
{
	// event is a ibDataViewEvent
	const ibDataViewItem& item = event.GetItem();
	if (!item.IsOk())
		return;
	ibValue cancel = false;
	CallAsEvent(m_eventBeforeDeleteRow,
		GetValue(), // control
		cancel //cancel
	);

	if (m_tableModel != nullptr && !m_tableModel->IsCallRefreshModel()) m_formOwner->RefreshForm();
	else if (m_tableModel == nullptr) m_formOwner->RefreshForm();

	if (cancel.GetBoolean())
		event.Veto();
	else
		event.Skip();
}

void ibValueModelTableBox::OnViewSet(ibDataViewEvent& event)
{
	if (m_dataViewCreated)
		m_propertyViewMode->SetValue(event.GetViewMode());
	
	event.Skip();
}

void ibValueModelTableBox::OnHeaderResizing(ibHeaderGenericCtrlEvent& event)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(GetWxObject());
	if (dataViewCtrl != nullptr) {
		ibDataViewColumnObject* dataViewColumn =
			dynamic_cast<ibDataViewColumnObject*>(dataViewCtrl->GetColumn(event.GetColumn()));
		ibValueModelTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		columnControl->SetWidthColumn(event.GetWidth());
		if (g_visualHostContext != nullptr)
			g_visualHostContext->SelectControl(columnControl);
	}
	event.Skip();
}

void ibValueModelTableBox::OnMainWindowClick(wxMouseEvent& event)
{
	if (g_visualHostContext != nullptr)
		g_visualHostContext->SelectControl(this);
	event.Skip();
}

#if wxUSE_DRAG_AND_DROP

void ibValueModelTableBox::OnItemBeginDrag(ibDataViewEvent& event)
{
}

void ibValueModelTableBox::OnItemDropPossible(ibDataViewEvent& event)
{
	if (event.GetDataFormat() != wxDF_UNICODETEXT)
		event.Veto();
	else
		event.SetDropEffect(wxDragMove);	// check 'move' drop effect
}

void ibValueModelTableBox::OnItemDrop(ibDataViewEvent& event)
{
	if (event.GetDataFormat() != wxDF_UNICODETEXT) {
		event.Veto();
		return;
	}
}

#endif // wxUSE_DRAG_AND_DROP

void ibValueModelTableBox::OnCommandMenu(wxCommandEvent& event)
{
	ibValueModelTableBox::ExecuteAction(event.GetId(), m_formOwner);
}

void ibValueModelTableBox::OnContextMenu(ibDataViewEvent& event)
{
	const ibActionCollection& actionData =
		ibValueModelTableBox::GetActionCollection(m_formOwner->GetTypeForm());

	wxMenu menu;
	for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
		const ibActionID& id = actionData.GetID(idx);
		if (id != wxNOT_FOUND) {
			wxMenuItem* menuItem = menu.Append(id, actionData.GetCaptionByID(id));
			ibPictureDescription pictureDesc = actionData.GetPictureByID(id);
			if (!pictureDesc.IsEmptyPicture())
				menuItem->SetBitmap(ibBackendPicture::CreatePicture(pictureDesc));
		}
	}
	ibDataViewCtrl* wnd = wxDynamicCast(
		event.GetEventObject(), ibDataViewCtrl
	);
	wxASSERT(wnd);
	wnd->PopupMenu(&menu, event.GetPosition());
}

void ibValueModelTableBox::OnSize(wxSizeEvent& event)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(event.GetEventObject());

	if (m_dataViewCreated)
		m_dataViewSizeChanged = m_dataViewUpdated || (m_dataViewSize != dataViewCtrl->GetSize()) && (m_dataViewSize != wxDefaultSize);

	event.Skip();
}

void ibValueModelTableBox::OnIdle(wxIdleEvent& event)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(event.GetEventObject());

	if (m_dataViewSizeChanged && m_tableModel != nullptr) {

		try {
			m_tableModel->CallRefreshModel(dataViewCtrl->GetTopItem(), dataViewCtrl->GetCountPerPage());
		}
		catch (const ibBackendException* err) {
			dataViewCtrl->AssociateModel(nullptr);
			throw(err);
		}

		const ibValue& createdValue = m_formOwner->GetCreatedValue();
		if (!createdValue.IsEmpty()) {
			const ibDataViewItem& currLine = m_tableModel->FindRowValue(createdValue);
			if (currLine.IsOk()) m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
			else m_tableCurrentLine.Reset();
		}
		else if (!m_dataViewSelected) {
			ibValueFrame* ownerControl = m_formOwner->GetOwnerControl();
			if (ownerControl != nullptr && m_tableCurrentLine == nullptr) {
				ibValue retValue; ownerControl->GetControlValue(retValue);
				const ibDataViewItem& currLine = m_tableModel->FindRowValue(retValue);
				if (currLine.IsOk()) m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
				else m_tableCurrentLine.Reset();
			}

			if (m_tableCurrentLine != nullptr) {

				const ibDataViewItem& item =
					m_tableModel->GetParent(m_tableCurrentLine->GetLineItem());

				dataViewCtrl->SetTopParent(item);
			}

			m_dataViewSelected = true;
		}

		if (m_tableCurrentLine != nullptr && !m_tableModel->ValidateReturnLine(m_tableCurrentLine)) {

			const ibDataViewItem& currLine = m_tableModel->FindRowValue(&(*m_tableCurrentLine));
			if (currLine.IsOk()) {
				m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
			}
			else {
				const ibValue& changedValue = m_formOwner->GetChangedValue();
				if (!changedValue.IsEmpty()) {
					const ibDataViewItem& currLine = m_tableModel->FindRowValue(changedValue);
					if (currLine.IsOk()) m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
					else m_tableCurrentLine.Reset();
				}
				else {
					m_tableCurrentLine.Reset();
				}
			}
		}

		if (m_tableCurrentLine != nullptr)
			dataViewCtrl->Select(m_tableCurrentLine->GetLineItem());
	}

	if (m_need_calculate_pos) {
		CalculateColumnPos();
		m_need_calculate_pos = false;
	}

	m_dataViewSize = dataViewCtrl->GetSize();
	m_dataViewUpdated = m_dataViewSizeChanged = false;
	event.Skip();
}

void ibValueModelTableBox::HandleOnScroll(wxScrollWinEvent& event)
{
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(event.GetEventObject());

	const short scroll = (event.GetEventType() == wxEVT_SCROLLWIN_TOP ||
		event.GetEventType() == wxEVT_SCROLLWIN_LINEUP ||
		event.GetEventType() == wxEVT_SCROLLWIN_PAGEUP) ? 1 : -1;

	if (dataViewCtrl != nullptr) {

		if (m_tableModel != nullptr) {

			const int countPerPage = dataViewCtrl->GetCountPerPage();
			const ibDataViewItem& top_item = dataViewCtrl->GetTopItem();
			const ibDataViewItem& focused_item = m_tableCurrentLine != nullptr ?
				m_tableCurrentLine->GetLineItem() : ibDataViewItem(nullptr);

			m_tableModel->CallRefreshItemModel(top_item, focused_item, countPerPage, scroll);

			if (m_tableCurrentLine != nullptr && !m_tableModel->ValidateReturnLine(m_tableCurrentLine)) {
				const ibDataViewItem& currLine = m_tableModel->FindRowValue(m_tableCurrentLine);
				if (currLine.IsOk()) {
					m_tableCurrentLine = m_tableModel->GetRowAt(currLine);
					dataViewCtrl->Select(m_tableCurrentLine->GetLineItem());
				}
				else {
					m_tableCurrentLine.Reset();
				}
			}
		}
	}

	event.Skip();
}

