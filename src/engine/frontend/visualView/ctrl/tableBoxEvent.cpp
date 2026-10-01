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

// A COLUMN LANDS WHERE IT WAS DROPPED — including in whose GROUP.
//
// THE DRAG HAS ALREADY MOVED THE MEMBER (ibDataViewCtrl::ColumnMoved) — the runtime tree
// is the order, holders included, so this only brings the CONTROL tree to the same story:
// the column is asked which group it is in now, and it goes to that group's control, at
// the same place. Nothing is worked out from its neighbours a second time; two answers to
// "who holds it" is precisely how the tree and the form used to disagree.
void ibValueModelTableBox::OnColumnReordered(ibDataViewEvent& event)
{
	ibDataViewColumnObject* dataViewColumn =
		dynamic_cast<ibDataViewColumnObject*>(event.GetDataViewColumn());
	wxASSERT(dataViewColumn);

	ibValueModelTableBoxColumn* columnObject = dataViewColumn->GetControl();
	wxASSERT(columnObject);

	ibDataViewColumnGroup* holderGroup = dataViewColumn->GetParent();
	if (holderGroup == nullptr) {
		event.Skip();
		return;
	}

	// A GROUP the designer put there has a control of its own; the ROOT group has none —
	// it is the table itself, which is what "not in any group" means on a form.
	auto* groupObject = dynamic_cast<ibDataViewColumnGroupObject*>(holderGroup);
	ibValueFrame* holder = groupObject != nullptr
		? static_cast<ibValueFrame*>(groupObject->GetControl())
		: static_cast<ibValueFrame*>(this);
	const int at = holderGroup->GetMemberPosition(dataViewColumn);

	// ONE PATH for both cases — a move inside the same holder and a move into another one
	// differ only in which parent it is taken off.
	ibValuePtr<ibValueFrame> keep(columnObject);
	if (ibValueFrame* oldHolder = columnObject->GetParent())
		oldHolder->RemoveChild(columnObject);

	columnObject->SetParent(holder);
	holder->AddChild(at != wxNOT_FOUND
		? wxMin((unsigned int)at, holder->GetChildCount()) : holder->GetChildCount(),
		columnObject);

	if (g_visualHostContext != nullptr)
		g_visualHostContext->RefreshEditor();

	SetUpdateExpanderColumn();
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
	ActivateRow(item);

	CallAsEvent(m_eventOnActivateRow,
		GetValue() // control
	);

	event.Skip();
}

// Double-click / Enter dispatch: choice → SELECT; a row with an editable cell → inline editor (FRONT); a read-only
// row → open its VALUE (the model's ActivateItem raises the object form on the BACKEND — a list / register recorder).
void ibValueModelTableBox::ActivateRow(const ibDataViewItem& item)
{
	if (!item.IsOk() || m_tableModel == nullptr)
		return;

	if (IsChoiceMode()) {
		Command_Choose(m_formOwner);
		return;
	}

	if (!EditCurrentRow(item))                          // editable cell → inline editor started
		m_tableModel->ActivateItem(item, m_formOwner);  // none → open the row's value (a list opens its object form)
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
		// A non-editable line (a list row) has no inline editor — veto the edit start. Opening the row's value
		// (its object form) is the double-click concern (OnItemActivated: read-only → ShowValue), not this path.
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
	// (A row edited OUT OF THE FILTER leaves the list — but not from here. The renderer's FinishEditing is
	//  where an edit is over, and it is the one moment where a re-read is safe: see tableBoxColumnRenderer.h.)
	event.Skip();
}

void ibValueModelTableBox::OnItemStartAdding(ibDataViewEvent& event)
{
	m_formOwner->RefreshForm();

	const ibDataViewItem& item = event.GetItem();
	if (item.IsOk() && m_tableModel != nullptr) {
		// Record the new row as the current line WITHOUT touching the visual selection. The one actual select is
		// done by the ctrl — the paged bootstrap that re-fetches around the row (or, non-paged, the tree-insert
		// Select) — neither of which fires wxEVT_DATAVIEW_SELECTION_CHANGED, so we sync the engine here (otherwise a
		// choice "…" on the fresh row, notably the FIRST row of an empty table, finds no current line and no-ops).
		// A visual Select/Unselect here too would BLINK the highlight off before the bootstrap puts it back.
		m_tableCurrentLine = m_tableModel->GetRowAt(item);

		// GUI-driven Add → fire script OnAddRow with the just-appended row.
		// Programmatic createdValue path (OnIdle) goes through
		// ibValueModelStorage::Append too, so listeners observe creation
		// regardless of origin.
		if (m_eventOnAddRow != nullptr)
			CallAsEvent(m_eventOnAddRow,
				GetValue(),
				ibValue(m_tableModel->GetRowAt(item)));
	}

	event.Skip();
}

void ibValueModelTableBox::OnItemStartInserting(ibDataViewEvent& event)
{
	// Insert / Copy semantics — the row is cloned at a position. No OnAddRow callback for this path (Copy / Insert
	// ≠ Add); OnAddRow lives on the dedicated _START_ADDING event below.
	m_formOwner->RefreshForm();

	// The just-inserted row STILL becomes the current line — same reason as _START_ADDING: the ctrl selects it
	// visually, but that programmatic selection fires no wxEVT_DATAVIEW_SELECTION_CHANGED, so without this the
	// current line would stay on the OLD row and the next Insert / Add would keep landing at the same spot (the
	// cursor looks frozen — "after the second row it stops moving"). Only the OnAddRow SCRIPT event is withheld.
	const ibDataViewItem& item = event.GetItem();
	if (item.IsOk() && m_tableModel != nullptr) {
		// Record the new row as the current line WITHOUT touching the visual selection. The one actual select is
		// done by the ctrl — the paged bootstrap that re-fetches around the row (or, non-paged, the tree-insert
		// Select) — neither of which fires wxEVT_DATAVIEW_SELECTION_CHANGED, so we sync the engine here (otherwise a
		// choice "…" on the fresh row, notably the FIRST row of an empty table, finds no current line and no-ops).
		// A visual Select/Unselect here too would BLINK the highlight off before the bootstrap puts it back.
		m_tableCurrentLine = m_tableModel->GetRowAt(item);

		// GUI-driven Add → fire script OnAddRow with the just-appended row.
		// Programmatic createdValue path (OnIdle) goes through
		// ibValueModelStorage::Append too, so listeners observe creation
		// regardless of origin.
		if (m_eventOnAddRow != nullptr)
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
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx());
	if (dataViewCtrl != nullptr) {
		ibDataViewColumnObject* dataViewColumn =
			dynamic_cast<ibDataViewColumnObject*>(dataViewCtrl->GetColumn(event.GetColumn()));
		ibValueModelTableBoxColumn* columnControl = dataViewColumn->GetControl();
		wxASSERT(columnControl);

		// THE WIDTH THE COLUMN ASKS FOR, not the one it currently shows.
		//
		// What the form stores is a REQUEST — the width this column wants — and the table
		// then stretches the columns it has room for. Storing the stretched width instead
		// made the request grow every time: a column asking 80 and shown 126 came back as
		// asking 126, so reopening the form (or narrowing it a little) went straight to a
		// scrollbar. The drag has already worked out the request (WXApplyColumnWidth); this
		// only records it.
		columnControl->SetWidthColumn(dataViewColumn->WXGetSpecifiedWidth());
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
	ibValueModelTableBox::CallAsAction(event.GetId(), m_formOwner);
}

void ibValueModelTableBox::OnContextMenu(ibDataViewEvent& event)
{
	const ibStandardCommandSet& actionData =
		ibValueModelTableBox::GetStandardCommands(m_formOwner->GetTypeForm());

	// A view-only form greys the DATA-MODIFYING entries (Add / Copy / Edit / Delete) here too — the context
	// menu builds straight from the action collection, so it must honour the same modify flag as the toolbar
	// (BuildCommands). Read-only entries (Filter / View mode / Select) stay live.
	const bool viewOnly = m_formOwner != nullptr && m_formOwner->IsViewOnly();

	wxMenu menu;
	for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
		const ibActionID& id = actionData.GetID(idx);
		if (id != wxNOT_FOUND) {
			wxMenuItem* menuItem = menu.Append(id, actionData.GetCaptionByID(id));
			ibPictureDescription pictureDesc = actionData.GetPictureByID(id);
			if (!pictureDesc.IsEmptyPicture())
				menuItem->SetBitmap(ibBackendPicture::CreatePicture(pictureDesc));
			if (viewOnly && actionData.GetModifiesDataByID(id))
				menuItem->Enable(false);   // grey data-modifying command in view-only
		}
	}
	ibDataViewCtrl* wnd = wxDynamicCast(
		event.GetEventObject(), ibDataViewCtrl
	);
	wxASSERT(wnd);
	wnd->PopupMenu(&menu, event.GetPosition());
}

