#include "tableBox.h" 
#include "tableBoxColumnRenderer.h"

#include "backend/appData.h"

void ibValueModelTableBox::OnColumnClick(ibDataViewEvent& event)
{
	ibDataViewColumnObject* dataViewColumn =
		dynamic_cast<ibDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(dataViewColumn);

	// Designer-side: hand keyboard / property-grid focus to the column control in the visual editor. No
	// sorting (no data) — let the default header handler run.
	if (g_visualHostContext != nullptr) {
		ibValueModelTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);
		g_visualHostContext->SelectControl(columnControl);
		event.Skip();
		return;
	}

	// Runtime: everything on the FRONT. Get this control's model, poke its composer, RefetchAll — the backend
	// stays blind (no sort method / no dispatch). This is the same shape the settings dialog uses
	// (ibCommitSettingsToComposer + refresh); the column already knows its OWN bound field (GetSourceFieldName),
	// and the composer dot-walks it exactly as the renderer resolves the cell value.
	ibValueModelTableBoxColumn* col = dataViewColumn->GetControl();
	if (m_tableModel == nullptr || col == nullptr
		|| !m_tableModel->GetFeatures().Has(ibValueModel::Features::Sorting)) {
		event.Skip();
		return;
	}

	const wxString field = col->GetSourceFieldName();
	if (field.IsEmpty()) {
		event.Skip();   // whole-attribute / foreign / unresolvable column — nothing to sort by
		return;
	}

	ibDataComposer& composer = m_tableModel->GetModelComposer();
	// Single-column toggle read off the composer (the SSOT): already the sole sort on this field → flip; else asc.
	bool ascending = true;
	wxString curField; bool curAsc = true;
	if (composer.SortCount() == 1 && composer.GetSortAt(0, curField, curAsc) && curField == field)
		ascending = !curAsc;

	composer.ClearSorts();
	composer.Sort(field, ascending);

	// Reflect the header arrow now (the columns also re-read the composer on rebuild via OnUpdated).
	dataViewColumn->GetOwner()->ResetAllSortColumns();
	dataViewColumn->SetSortOrder(ascending);
	// A sort change invalidates the paged keyset anchor (built for the OLD ORDER BY) — fetch fresh from the top.
	dataViewColumn->GetOwner()->SetPagedSkipRestoreCapture();
	m_tableModel->RefetchAll();
	// Handled — do NOT Skip: the generic default sort in datavgen must not also run.
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
	// Insert / Copy semantics — the row is cloned at a position. No
	// OnAddRow callback for this path; OnAddRow lives on the dedicated
	// _START_ADDING event below.
	m_formOwner->RefreshForm();
	event.Skip();
}

void ibValueModelTableBox::OnItemStartAdding(ibDataViewEvent& event)
{
	m_formOwner->RefreshForm();

	// GUI-driven Add → fire script OnAddRow with the just-appended row.
	// Programmatic createdValue path (OnIdle) goes through
	// ibValueModelStorage::Append too, so listeners observe creation
	// regardless of origin.
	const ibDataViewItem& item = event.GetItem();
	if (item.IsOk() && m_eventOnAddRow != nullptr && m_tableModel != nullptr) {
		CallAsEvent(m_eventOnAddRow,
			GetValue(),
			ibValue(m_tableModel->GetRowAt(item)));
	}

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

	m_formOwner->RefreshForm();

	if (cancel.GetBoolean()) {
		event.Veto();
	}
	else {
		// Symmetric to OnAddRow — fire OnDeleteRow with the row that
		// is about to be removed so script can take action while the
		// row is still resolvable.
		if (m_eventOnDeleteRow != nullptr && m_tableModel != nullptr) {
			CallAsEvent(m_eventOnDeleteRow,
				GetValue(),
				ibValue(m_tableModel->GetRowAt(item)));
		}
		event.Skip();
	}
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

