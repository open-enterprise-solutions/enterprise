#include "tableBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#ifndef OES_USE_WEB
// Renderer pulls in dataview.h (wxDataView heavy). Web stubs don't
// touch the renderer at all.
#include "tableBoxColumnRenderer.h"
#endif

#include "form.h"

#include "frontend/visualView/visualHostClient.h"
#include "backend/system/value/valueTable.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"                 // FindAnyObjectByFilter (dot-path metaID -> name)
#include "backend/appData.h"
//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************



#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif

//***********************************************************************************
//*                                 Special tablebox func                           *
//***********************************************************************************

// Single point: resolve a dot-path column's value for ONE row (called per visible cell from the
// column renderer's CheckedGetValue). The model stays a plain id->value source: the FIRST hop is
// a real row column it resolves; the deeper hops walk the reference on the front by attribute name.
// A plain column (row-relative tail <= 1) returns false — the renderer falls through to the model.
// A dot-path column reaches past the tablebox prefix + one row column (>= 2 row-relative hops).
bool ibValueModelTableBox::IsPathColumn(const ibValueModelTableBoxColumn* column) const
{
	return column != nullptr && column->GetSourcePath().size() > GetSourcePath().size() + 1;
}

// A column whose path does NOT lie under this tablebox's own bound prefix is rooted at a DIFFERENT
// form source — the object ABOVE the table (its header). A normal column shares the tablebox's whole
// prefix, then diverges into its own column id / dot-walk; a foreign column diverges WITHIN the
// prefix (or is shorter than it). (Mode 2 — column from the header object.)
bool ibValueModelTableBox::IsForeignColumn(const ibValueModelTableBoxColumn* column) const
{
	if (column == nullptr)
		return false;
	const std::vector<ibSourceHop>& colPath = column->GetSourcePath();
	const std::vector<ibSourceHop>& myPath = GetSourcePath();
	if (colPath.empty())
		return false;
	for (size_t i = 0; i < myPath.size(); ++i) {
		if (i >= colPath.size() || colPath[i].m_id != myPath[i].m_id)
			return true;   // diverges from (or is shorter than) the tablebox prefix -> foreign root (structural, by id)
	}
	return false;
}

bool ibValueModelTableBox::ResolveCellValue(const ibDataViewItem& item,
	const ibValueModelTableBoxColumn* column, wxVariant& out) const
{
	// FOREIGN-root column (Mode 2): pulls from a form source ABOVE this tablebox (the header object),
	// not from a row of the bound table. Resolve it ONCE through the form — the value is the same for
	// every row of the tabular section (the header is constant across the lines). Read-only. The
	// primitive is the same the designer/web read uses (GetControlValue's dotted-path branch).
	if (IsForeignColumn(column)) {
		ibValue current;
		if (m_formOwner == nullptr || !m_formOwner->GetValueByAttributePath(column->GetSourceDesc(), current))
			return false;
		ibValueModel::ValueToVariant(out, current);
		return true;
	}

	// One hop past the prefix = a plain row column (the dumb model has it) — not ours.
	if (m_tableModel == nullptr || !IsPathColumn(column))
		return false;

	const std::vector<ibSourceHop>& colPath = column->GetSourcePath();
	const size_t prefix = GetSourcePath().size();   // row-relative tail starts here

	// The table STARTS the walk at the row (the first row-relative hop yields a source cell) and TRANSFERS the
	// deeper hops to that source object — ONE entry, like a control resolving an attribute path off the form.
	ibValue current;
	if (!m_tableModel->GetValueByPath(item, colPath, prefix, current))
		return false;

	ibValueModel::ValueToVariant(out, current);
	return true;
}

bool ibValueModelTableBox::GetControlValue(ibValue& pvarControlVal) const
{
	if (m_tableModel == nullptr) {
		if (appData->DesignerMode()) {
			if (!m_propertySource->IsEmptyProperty()) {
				if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
					m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal)) {
					return true;   // attribute-table / dotted path -> read-only walk
				}
			}
		}
		return false;
	}
	pvarControlVal = m_tableModel;
	return true;
}

bool ibValueModelTableBox::SetControlValue(const ibValue& varControlVal)
{
	m_tableModel = varControlVal.ConvertToType<ibValueModel>();
	return true;
}

void ibValueModelTableBox::AddColumn()
{
#ifndef OES_USE_WEB
	wxASSERT(m_formOwner);

	// Create a BARE view column — NO source, and NO storage column injected into the bound value-table.
	// A tablebox column on the form is a VIEW that BINDS (through its Source, which may be a dotted path to
	// another / composite field) to a field the user picks; it must not silently add a fourth column to the
	// value-table's schema (that schema is edited via the attribute's own "Add column"). Auto-adding one both
	// duplicated the schema and froze the value-table column id to the CONTROL id (a different id space) — a
	// serialization hazard. Source-less, the new column stays hidden until the user binds it (visibility gate
	// in ibValueModelTableBoxColumn::OnUpdated), exactly like any other unbound source control.
	ibValueModelTableBoxColumn* columnTable = dynamic_cast<ibValueModelTableBoxColumn*>(m_formOwner->NewObject(g_controlTableBoxColumnCLSID, this));
	g_visualHostContext->InsertControl(columnTable, this);
	g_visualHostContext->RefreshEditor();
#endif
}

#ifndef OES_USE_WEB
void ibValueModelTableBox::CreateColumnCollection(ibDataViewCtrl* dataViewCtrl)
{
	if (appData->DesignerMode())
		return;

	ibDataViewCtrl* tc = dataViewCtrl ?
		dataViewCtrl : dynamic_cast<ibDataViewCtrl*>(GetInnerWx());
	wxASSERT(tc);

	ibFormVisualDocument* visualDocument = m_formOwner->GetVisualDocument();
	//detach wx widgets first (while the column controls are still alive)
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueFrame* childColumn = GetChild(idx);
		wxASSERT(childColumn);
		if (visualDocument != nullptr) {
			ibVisualHostClient* visualView = visualDocument->GetFirstView() ?
				visualDocument->GetFirstView()->GetVisualHost() : nullptr;
			wxASSERT(visualView);
			visualView->RemoveControl(childColumn, this);
		}
	}

	//clear all children — owning handles release the column controls (cascade)
	RemoveAllChildren();

	//clear all old columns
	tc->ClearColumns();

	//create new columns
	ibValueModel::ibValueModelColumnCollection* tableColumns = m_tableModel->GetColumnCollection();
	wxASSERT(tableColumns);
	for (unsigned int idx = 0; idx < tableColumns->GetColumnCount(); idx++) {

		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* columnInfo = tableColumns->GetColumnInfo(idx);
		ibValueModelTableBoxColumn* newTableBoxColumn =
			m_formOwner->NewObject<ibValueModelTableBoxColumn>(g_controlTableBoxColumnCLSID, this);

		const ibTypeDescription& typeDescription = columnInfo->GetColumnType();
		if (typeDescription.IsOk())
			newTableBoxColumn->SetDefaultMetaType(typeDescription);
		else
			newTableBoxColumn->SetDefaultMetaType(ibValueTypes::TYPE_STRING);

		newTableBoxColumn->SetCaption(columnInfo->GetColumnCaption());
		newTableBoxColumn->SetWidthColumn(columnInfo->GetColumnWidth());
		newTableBoxColumn->SetModelColumn(columnInfo->GetColumnID());

		if (visualDocument != nullptr) {

			ibVisualHostClient* visualView = visualDocument->GetFirstView() ?
				visualDocument->GetFirstView()->GetVisualHost() : nullptr;

			wxASSERT(visualView);
			visualView->CreateControl(newTableBoxColumn, this);
		}
	}

	if (visualDocument != nullptr) {

		ibVisualHostClient* visualView = visualDocument->GetFirstView() ?
			visualDocument->GetFirstView()->GetVisualHost() : nullptr;

		wxASSERT(visualView);
		//fix size in parent window
		wxWindow* wndParent = visualView->GetParent();
		if (wndParent != nullptr) {
			wndParent->Layout();
		}
	}
}
#endif // !OES_USE_WEB

void ibValueModelTableBox::CreateTable(bool recreateModel) {

	if (recreateModel && m_tableModel != nullptr) m_tableModel = nullptr;

	if (m_tableModel == nullptr) {

		m_tableModel = ibTypeControlFactory::CreateAndConvertValueRef<ibValueModel>();

		if (m_tableModel != nullptr) {
			for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
				ibValueModelTableBoxColumn* columnTable = dynamic_cast<ibValueModelTableBoxColumn*>(GetChild(idx));
				if (columnTable != nullptr) {
					ibValueModel::ibValueModelColumnCollection* columnData = m_tableModel->GetColumnCollection();
					if (columnData == nullptr) continue;
					ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* column_info = columnData->AddColumn(
						columnTable->GetControlName(),
						columnTable->GetTypeDesc(),
						columnTable->GetCaption(),
						columnTable->GetWidthColumn()
					);

					if (column_info != nullptr) column_info->SetColumnID(columnTable->GetControlID());
				}
			}
		}
	}
}

void ibValueModelTableBox::CreateModel(bool recreateModel)
{
	if (!m_propertySource->IsEmptyProperty()) {

		if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
			m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), m_tableModel)) {
		}

		CreateTable(false);
	}
	else if (m_tableModel != nullptr && m_propertySource->GetValueAsTypeDesc() != m_tableModel->GetSourceClassType()) {
		CreateTable(true);
	}
	else {
		CreateTable(recreateModel);
	}
}

void ibValueModelTableBox::RefreshModel(bool recreateModel)
{
	ibValueModelTableBox::CreateModel(recreateModel);
}

void ibValueModelTableBox::ApplyCurrentLine(
	ibValueModel::ibValueModelReturnLine* line, bool focus)
{
	m_tableCurrentLine = line;

#ifndef OES_USE_WEB
	auto* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx());
	if (dataViewCtrl != nullptr) {
		if (line == nullptr) {
			dataViewCtrl->UnselectAllRows();
		}
		else {
			const ibDataViewItem item = line->GetLineItem();
			if (item.IsOk()) {
				// NOTHING IS UNSELECTED AHEAD OF THE FRAME THAT REPLACES IT. Select() itself
				// drops the old highlight (single-sel) in the same call that puts the new one
				// up — but ONLY when the row is already in the buffer. For a paged model the
				// row usually is NOT: Select routes the item into the bootstrap-restore channel
				// and the highlight lands when the fetch answers, a whole query later. Clearing
				// here first left that interval with no current row at all — the cursor
				// vanishing and then reappearing elsewhere, which is the flicker the rest of
				// the paged path (wipe + fill under one freeze, see PagedRefresh) already
				// avoids. The old row now keeps its highlight until OnPagedFetchResetComplete
				// swaps both inside the same frozen frame.
				dataViewCtrl->Select(item);
				if (focus) {
					// Select already moved the keyboard focus / edit-on-Enter anchor
					// (ChangeCurrentRow) — under single-sel SetCurrentItem IS Select, so calling
					// it here was the same work twice, and on a paged model a second stamp into
					// the restore channel can bump the fetch generation and cost an extra
					// round-trip. Only the viewport scroll is left to do.
					dataViewCtrl->EnsureVisible(item);
				}
			}
		}
	}
#else
	(void)focus;
#endif

	// Selection script event is intentionally NOT fired here — the line
	// passed in for programmatic restore is often a stub from
	// FindRowValue (GUID-only, body empty) and the user-side handler
	// would crash reading unpopulated columns.  User-click path goes
	// through OnSelectionChanged which fires the event with a real
	// fetched row.  For "new row created" observation use OnAddRow.
}

////////////////////////////////////////////////////////////////////////////////////

ibSourceObject* ibValueModelTableBox::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueModelTableBox::IsMainSourceBound() const
{
	// This table IS the form's main source when its WHOLE binding path is the main attribute (a single hop).
	// A NESTED source (a tabular section — path [mainAttr, section]) only has the main attribute as its HEAD,
	// not as its own source; it is a distinct list.
	const ibSourceDescription& desc = m_propertySource->GetValueAsSourceDesc();
	if (desc.GetHopCount() != 1)
		return false;
	ibBackendFormAttributeValue* holder = FindSourceHolder(desc.GetFirst());
	return holder != nullptr && holder->IsMain();
}

bool ibValueModelTableBox::HasCommandBar() const
{
	// No bar when this table is the form's main source — the form toolbar already serves those commands (its
	// command provider resolves to this view) and a table bar would duplicate. A nested source keeps its own
	// bar (Add/Copy/Edit/Delete).
	if (IsMainSourceBound())
		return false;
	return ibValueFrame::HasCommandBar();
}

bool ibValueModelTableBox::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

const ibValueMetaObjectCompositeData* ibValueModelTableBox::GetSourceMetaObject() const
{
	wxASSERT(m_tableModel);
	if (m_tableModel == nullptr) return nullptr;
	return m_tableModel->GetSourceMetaObject();
}

ibClassID ibValueModelTableBox::GetSourceClassType() const
{
	wxASSERT(m_tableModel);
	if (m_tableModel == nullptr) return 0;
	return m_tableModel->GetSourceClassType();
}

//***********************************************************************************
//*                              ibValueModelTableBox                                     *
//***********************************************************************************

ibValueModelTableBox::ibValueModelTableBox() : ibValueWindowComposite(), ibTypeControlFactory(),
m_dataViewCreated(false), m_dataViewSelected(false),
m_need_calculate_pos(false),
m_tableModel(nullptr), m_tableCurrentLine(nullptr)
{
	m_members.Bind(this, &ibValueModelTableBox::FillControlMembers);

	// Command bar is created by the ibValueWindowComposite base ctor.

	m_propertySource->SetValue(ibTypeDescription(g_valueTableCLSID));

	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 75));
	m_propertyBG->SetValue(wxColour(255, 255, 255));
}

/////////////////////////////////////////////////////////////////////////////////////

#ifndef OES_USE_WEB
// Resolve a row identity (reference / column value) to the model's
// per-row return line wrapper, returning nullptr on miss so the caller
// can fall through to the next candidate value. Used only by
// OnUpdated's wxDataView-bound restore logic — web build doesn't link.
static ibValueModel::ibValueModelReturnLine*
ResolveLineByValue(ibValueModel* model, const ibValue& value)
{
	const ibDataViewItem& item = model->FindRowValue(value);
	return item.IsOk() ? model->GetRowAt(item) : nullptr;
}
#endif

void ibValueModelTableBox::CalculateColumnPos()
{
#ifndef OES_USE_WEB
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx());
	if (dataViewCtrl == nullptr)
		return;

	// Columns render in child (append) order — the generic dataview honours the order columns are appended /
	// inserted in, so there is nothing to force here. This used to DeleteColumn+InsertColumn every column to
	// match GetParentPosition(): a workaround for the NATIVE (pre-generic) header, which threw a logic error
	// when columns were moved and had to be re-synced. The generic header made that dead weight — and its
	// reorder decision read the header's DISPLAY order (GetColumnPos), transiently stale during a drop/refill,
	// which reshuffled freshly-dropped columns out of order. User header drag-reorder still persists through
	// OnColumnReordered -> ChangeChildPosition (the generic dataview has already moved the column visually).
	// All that remains is dropping the anchor: the tree-expander column = the first shown column.
	dataViewCtrl->SetExpanderColumn(nullptr);
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {

		const ibValueFrame* valueFrame = GetChild(idx);
		wxASSERT(valueFrame);

		ibDataViewColumn* column = dynamic_cast<ibDataViewColumn*>(valueFrame->GetWxObject());
		if (column != nullptr && column->IsShown() && dataViewCtrl->GetExpanderColumn() == nullptr)
			dataViewCtrl->SetExpanderColumn(column);
	}
#endif // !OES_USE_WEB
}

/////////////////////////////////////////////////////////////////////////////////////

wxObject* ibValueModelTableBox::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent; (void)visualHost;
	return new ibWebStubControl(wxT("tablebox"));
#else
	ibTableViewCtrl* dataViewCtrl = new ibTableViewCtrl(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxDV_SINGLE | wxDV_HORIZ_RULES | wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_SIMPLE);

	const ibFormVisualDocument* visualDoc = ibValueModelTableBox::GetVisualDocument();

	if (visualDoc == nullptr || (visualDoc != nullptr && !visualDoc->IsVisualDemonstrationDoc())) {

		dataViewCtrl->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ibValueModelTableBox::OnColumnClick, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_COLUMN_REORDERED, &ibValueModelTableBox::OnColumnReordered, this);

		//system events:
		dataViewCtrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ibValueModelTableBox::OnSelectionChanged, this);

		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibValueModelTableBox::OnItemActivated, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSED, &ibValueModelTableBox::OnItemCollapsed, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EXPANDED, &ibValueModelTableBox::OnItemExpanded, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_COLLAPSING, &ibValueModelTableBox::OnItemCollapsing, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EXPANDING, &ibValueModelTableBox::OnItemExpanding, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibValueModelTableBox::OnItemStartEditing, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &ibValueModelTableBox::OnItemEditingStarted, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &ibValueModelTableBox::OnItemEditingDone, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &ibValueModelTableBox::OnItemValueChanged, this);

		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_INSERTING, &ibValueModelTableBox::OnItemStartInserting, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_ADDING, &ibValueModelTableBox::OnItemStartAdding, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_START_DELETING, &ibValueModelTableBox::OnItemStartDeleting, this);

#if wxUSE_DRAG_AND_DROP 
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &ibValueModelTableBox::OnItemBeginDrag, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &ibValueModelTableBox::OnItemDropPossible, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_DROP, &ibValueModelTableBox::OnItemDrop, this);
#endif // wxUSE_DRAG_AND_DROP

		dataViewCtrl->Bind(wxEVT_DATAVIEW_VIEW_SET, &ibValueModelTableBox::OnViewSet, this);

		dataViewCtrl->GenericGetHeader()->Bind(wxEVT_HEADER_RESIZING, &ibValueModelTableBox::OnHeaderResizing, this);

		// Scroll-driven prefetch lives inside ibDataViewCtrl::OnScrollEvent —
		// the control owns viewport state and triggers RequestForward /
		// RequestBackward on the model directly.

		dataViewCtrl->GetMainWindow()->Bind(wxEVT_LEFT_DOWN, &ibValueModelTableBox::OnMainWindowClick, this);

#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
		dataViewCtrl->EnableDragSource(wxDF_UNICODETEXT);
		dataViewCtrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

		dataViewCtrl->Bind(wxEVT_MENU, &ibValueModelTableBox::OnCommandMenu, this);
		dataViewCtrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibValueModelTableBox::OnContextMenu, this);
	}

	return dataViewCtrl;
#endif // OES_USE_WEB
}

void ibValueModelTableBox::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifndef OES_USE_WEB
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(wxobject);

	// Bind the source FIRST (a just-dropped tablebox auto-binds a fresh value-table attribute — control-side
	// helper — so it renders bound, not hidden; its type comes from the source-type generator, _table ->
	// value table). Then CreateModel reads that bound value-table into m_tableModel. The value-table starts
	// with NO columns — they come from the bound source or the user's explicit "Add column", never auto-added.
	if (firstCreated)
		AutoBindNewSource(this);

	if (dataViewCtrl != nullptr) ibValueModelTableBox::CreateModel();

	// NO auto-first-column here. AddColumn() injects a column INTO the bound value-table (m_tableModel's
	// collection), so auto-calling it on every designer create/drop polluted the value-table attribute with a
	// spurious column each time (Column, Column1, Column2, …). Columns now come only from the bound source or
	// the user's explicit "Add column".
#endif
}

#ifndef OES_USE_WEB
#include <wx/itemattr.h>
#endif

void ibValueModelTableBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibTableViewCtrl* dataViewCtrl =
		dynamic_cast<ibTableViewCtrl*>(wxobject);

	if (dataViewCtrl != nullptr) {
		UpdateWindow(dataViewCtrl);
	}
#endif
}

void ibValueModelTableBox::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(wxobject);

	if (dataViewCtrl != nullptr) {

		ibDataViewModel* dataViewOldModel = dataViewCtrl->GetModel();
		// Designer = compile + intellisense only.  Form-editor preview
		// must not associate the runtime data model with the control —
		// AssociateModel arms PagedBootstrap, which would issue SQL
		// against a metadata table that doesn't exist yet (new Catalog /
		// Document being designed) or that the Designer session has no
		// runtime to query against.  Columns in designer are rendered
		// from child ibValueModelTableBoxColumn controls (see
		// CreateColumnCollection's own DesignerMode gate); header /
		// footer dimensions below operate on dataViewCtrl directly, no
		// model needed.
		ibDataViewModel* dataViewNewModel =
			(m_tableModel != nullptr && !appData->DesignerMode())
			? m_tableModel->GetDataViewModel() : nullptr;

		if (dataViewNewModel != dataViewOldModel) {
			// Fresh control attaching to an existing model (form rebuild
			// after createdValue / changedValue notify): m_tableCurrentLine
			// holds the just-applied target from the previous control's
			// ApplyCurrentLine — preserve it and re-route through Select
			// on the new control so the bootstrap-restore channel picks
			// up the new row.  Real model swap (oldModel != null && new !=
			// old) still resets — different dataset, old line meaningless.
			const bool freshControl = (dataViewOldModel == nullptr);
			if (freshControl) dataViewCtrl->SetFocus();
			dataViewCtrl->AssociateModel(dataViewNewModel);
			if (!freshControl) {
				m_tableCurrentLine.Reset();
			}
			else if (m_tableCurrentLine != nullptr) {
				const ibDataViewItem item = m_tableCurrentLine->GetLineItem();
				if (item.IsOk())
					dataViewCtrl->Select(item);
			}
		}

		if (appData->DesignerMode()) {
			if (!visualHost->IsDesignerHost() || m_propertyHeader->GetValueAsBoolean()) {
				dataViewCtrl->ShowHeaderWindow(m_propertyHeader->GetValueAsBoolean());
				dataViewCtrl->SetHeaderHeight(m_propertyHeaderHeight->GetValueAsUInteger());
			}
			else {
				dataViewCtrl->ShowHeaderWindow(true);
				dataViewCtrl->SetHeaderHeight(1);
				dataViewCtrl->SetForegroundColour(*wxLIGHT_GREY);
			}
		}
		else {
			dataViewCtrl->ShowHeaderWindow(m_propertyHeader->GetValueAsBoolean());
			dataViewCtrl->SetHeaderHeight(m_propertyHeaderHeight->GetValueAsUInteger());
		}

		dataViewCtrl->ShowFooterWindow(m_propertyFooter->GetValueAsBoolean());
		dataViewCtrl->SetFooterHeight(m_propertyFooterHeight->GetValueAsUInteger());

		dataViewCtrl->FreezeTo(
			m_propertyFreezeRow->GetValueAsUInteger(), m_propertyFreezeCol->GetValueAsUInteger());

		dataViewCtrl->SetSelectionMode(m_propertyRowSelectionMode->GetValueAsEnum());
		dataViewCtrl->SetViewMode(m_propertyViewMode->GetValueAsEnum());

		wxItemAttr attr(
			dataViewCtrl->GetForegroundColour(),
			dataViewCtrl->GetBackgroundColour(),
			dataViewCtrl->GetFont()
		);

		dataViewCtrl->SetHeaderAttr(attr);

		if (!appData->DesignerMode()) {
			
			m_dataViewCreated = true;

			// Force-refetch flag on the control.  All UpdateForm
			// entry points (form's Update button via enUpdate,
			// NotifyCreate/Change/Delete from a child form save) reach
			// here through the visual walker — and "UpdateForm" always
			// means "data may have changed, re-pull".  The rest
			// (size, sort, filter, first-time bind) is handled by the
			// control internally on its own idle pass.
			// SchedulePagedRefresh is debounced via
			// m_pagedRefreshScheduled, so coalesces with any
			// concurrent notifier-driven refresh.
			//
			// Selection seed chain — ApplyCurrentLine routes through
			// Select(item), which on paged stamps the bootstrap-
			// restore channel; when bootstrap fires on the next idle
			// it matches via IsEqualTo against the freshly-fetched
			// batch (FindRowValue returns a GUID-stub for paged
			// catalogs/documents) and lands focus on the new row.
			// If the row no longer exists, selection drops silently.
			if (m_tableModel != nullptr && m_formOwner != nullptr) {

				dataViewCtrl->SchedulePagedRefresh();

				ibValueModel::ibValueModelReturnLine* line = nullptr;

				// Consume createdValue / changedValue once per save —
				// clearing prevents the same anchor from re-positioning
				// the user on every subsequent manual Refresh / sort /
				// filter, bouncing back to the create row indefinitely.
				const ibValue createdValue = m_formOwner->ConsumeCreatedValue();
				if (!createdValue.IsEmpty()) {
					line = ResolveLineByValue(m_tableModel, createdValue);
				}
				else if (!m_dataViewSelected) {
					ibValueFrame* ownerControl = m_formOwner->GetOwnerControl();
					if (ownerControl != nullptr && m_tableCurrentLine == nullptr) {
						ibValue retValue; ownerControl->GetControlValue(retValue);
						line = ResolveLineByValue(m_tableModel, retValue);
					}
					m_dataViewSelected = true;
				}

				// NO changedValue BRANCH. Re-writing an EXISTING element used to re-position the list on it,
				// which yanked the user off the row they were standing on whenever an object form was saved
				// while the list was browsed elsewhere. It is gone rather than gated, because the knowledge it
				// carried is no longer needed anywhere: the current row is a REFCOUNTED node that survives the
				// wipe, and it re-locates itself in the new batch by its own row-key (PagedRefresh stamps it
				// into m_pagedRestoreFocus, OnPagedFetchResetComplete matches it via IsEqualTo). The channel
				// dates from when a row had to be SEARCHED for after a refresh. A CREATE still earns its
				// branch above — that row did not exist, so there was nothing to stand on and nothing to
				// re-locate. Known edge left open deliberately: editing a register record's KEY changes the
				// row's identity, so the old row-key no longer matches and focus drops — by then it is a
				// different row, and "stay on it" is a fiction.

				// Initial-open sync. On first populate the ctrl highlights the top row VISUALLY, but that
				// programmatic selection fires no wxEVT_DATAVIEW_SELECTION_CHANGED, so m_tableCurrentLine
				// stays null and the engine is out of sync with the row the user sees active. Downstream a
				// cell choice ("…") then finds no current row, cannot read the cell's real (reference) type,
				// falls back to a primitive (string is treated as a leaf), and silently skips the choice
				// form. When nothing above resolved a line and we have none, adopt whatever the ctrl actually
				// has active: its selection if any, else the top visible row.
				if (line == nullptr && m_tableCurrentLine == nullptr) {
					ibDataViewItemArray sel;
					const int selCount = dataViewCtrl->GetSelections(sel);
					const ibDataViewItem active =
						(selCount > 0 && sel[0].IsOk()) ? sel[0] : dataViewCtrl->GetTopItem();
					if (active.IsOk())
						line = m_tableModel->GetRowAt(active);
				}

				if (line != nullptr) {
					ApplyCurrentLine(line);
				}
			}
		}

		if (m_need_calculate_pos) {
			CalculateColumnPos();
			m_need_calculate_pos = false;
		}
	}
#endif // !OES_USE_WEB
}

void ibValueModelTableBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibTableViewCtrl* dataViewCtrl = dynamic_cast<ibTableViewCtrl*>(obj);
	m_dataViewCreated = false;
	if (dataViewCtrl != nullptr) dataViewCtrl->AssociateModel(nullptr);
#endif
}

//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

bool ibValueModelTableBox::ReadData(const ibDataNode& node)
{
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	m_propertyHeader->SetNodeValue(node.GetProperty(m_propertyHeader->GetName()));
	m_propertyHeaderHeight->SetNodeValue(node.GetProperty(m_propertyHeaderHeight->GetName()));
	m_propertyFooter->SetNodeValue(node.GetProperty(m_propertyFooter->GetName()));
	m_propertyFooterHeight->SetNodeValue(node.GetProperty(m_propertyFooterHeight->GetName()));

	m_propertyFreezeRow->SetNodeValue(node.GetProperty(m_propertyFreezeRow->GetName()));
	m_propertyFreezeCol->SetNodeValue(node.GetProperty(m_propertyFreezeCol->GetName()));

	m_propertyRowSelectionMode->SetNodeValue(node.GetProperty(m_propertyRowSelectionMode->GetName()));
	m_propertyChoiceMode->SetNodeValue(node.GetProperty(m_propertyChoiceMode->GetName()));

	//events
	m_eventSelection->SetNodeValue(node.GetProperty(m_eventSelection->GetName()));
	m_eventBeforeAddRow->SetNodeValue(node.GetProperty(m_eventBeforeAddRow->GetName()));
	m_eventBeforeDeleteRow->SetNodeValue(node.GetProperty(m_eventBeforeDeleteRow->GetName()));
	m_eventOnActivateRow->SetNodeValue(node.GetProperty(m_eventOnActivateRow->GetName()));
	m_eventOnAddRow->SetNodeValue(node.GetProperty(m_eventOnAddRow->GetName()));
	m_eventOnDeleteRow->SetNodeValue(node.GetProperty(m_eventOnDeleteRow->GetName()));

	// Chain to the composite base (reads the "Layers" block) → ibValueWindow.
	return ibValueWindowComposite::ReadData(node);
}

bool ibValueModelTableBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	node.SetProperty(m_propertyHeader->GetName(), m_propertyHeader->GetNodeValue());
	node.SetProperty(m_propertyHeaderHeight->GetName(), m_propertyHeaderHeight->GetNodeValue());
	node.SetProperty(m_propertyFooter->GetName(), m_propertyFooter->GetNodeValue());
	node.SetProperty(m_propertyFooterHeight->GetName(), m_propertyFooterHeight->GetNodeValue());

	node.SetProperty(m_propertyFreezeRow->GetName(), m_propertyFreezeRow->GetNodeValue());
	node.SetProperty(m_propertyFreezeCol->GetName(), m_propertyFreezeCol->GetNodeValue());

	node.SetProperty(m_propertyRowSelectionMode->GetName(), m_propertyRowSelectionMode->GetNodeValue());
	node.SetProperty(m_propertyChoiceMode->GetName(), m_propertyChoiceMode->GetNodeValue());

	//events
	node.SetProperty(m_eventSelection->GetName(), m_eventSelection->GetNodeValue());
	node.SetProperty(m_eventBeforeAddRow->GetName(), m_eventBeforeAddRow->GetNodeValue());
	node.SetProperty(m_eventBeforeDeleteRow->GetName(), m_eventBeforeDeleteRow->GetNodeValue());
	node.SetProperty(m_eventOnActivateRow->GetName(), m_eventOnActivateRow->GetNodeValue());
	node.SetProperty(m_eventOnAddRow->GetName(), m_eventOnAddRow->GetNodeValue());
	node.SetProperty(m_eventOnDeleteRow->GetName(), m_eventOnDeleteRow->GetNodeValue());

	// Chain to the composite base (writes the "Layers" block) → ibValueWindow.
	return ibValueWindowComposite::WriteData(node);
}

//***********************************************************************************

enum prop {
	eTableValue,
	eCurrentRow,
};

const ibMetaData* ibValueModelTableBox::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueModelTableBox::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Value"), eTableValue, eControl);
	helper.AppendProp(wxT("CurrentRow"), eCurrentRow, eControl);
}

bool ibValueModelTableBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eTableValue) {
			m_tableModel = varPropVal.ConvertToType<ibValueModel>();
			m_tableCurrentLine.Reset();
			refreshColumn = true;
		}
		else if (lPropData == eCurrentRow) {
			ibValueModel::ibValueModelReturnLine* tableReturnLine = nullptr;
			if (varPropVal.ConvertToValue(tableReturnLine)
				&& m_tableModel == tableReturnLine->GetOwnerModel()) {
				ApplyCurrentLine(tableReturnLine);
			}
			else {
				ApplyCurrentLine(nullptr);
			}
		}
	}

	bool result = ibValueFrame::SetPropVal(lPropNum, varPropVal);

#ifndef OES_USE_WEB
	if (refreshColumn && m_tableModel != nullptr && m_tableModel->AutoCreateColumn()) {
		ibValueModelTableBox::CreateColumnCollection();
	}
#endif

	return result;
}

bool ibValueModelTableBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eTableValue) {
			pvarPropVal = m_tableModel;
			return true;
		}
		else if (lPropData == eCurrentRow) {
			pvarPropVal = m_tableCurrentLine;
			return true;
		}
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

#ifdef OES_USE_WEB
// Methods declared in tableBox.h but normally implemented in the
// auxiliary tableBox*.cpp files (Event/Property/Action/Menu) — those
// aren't compiled on web, so provide no-op stubs here so the linker
// finds them.
void ibValueModelTableBox::OnPropertyCreated(ibProperty* /*property*/) {}
bool ibValueModelTableBox::OnPropertyChanging(ibProperty* /*property*/, const wxVariant& /*newValue*/) { return true; }
void ibValueModelTableBox::OnPropertyChanged(ibProperty* /*property*/, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/) {}

ibValueModelTableBox::ibStandardCommandSet ibValueModelTableBox::GetStandardCommands(const ibFormID& /*formType*/)
{
	return ibStandardCommandSet();
}
void ibValueModelTableBox::CallAsAction(const ibActionID& /*lNumAction*/, ibBackendValueForm* /*srcForm*/) {}

void ibValueModelTableBox::PrepareDefaultMenu(wxMenu* /*m_menu*/) {}
void ibValueModelTableBox::ExecuteMenu(ibVisualHost* /*visualHost*/, int /*id*/) {}

// Column refill lives in the desktop-only tableBoxProperty.cpp. Web still needs the vtable symbol:
// RefillFromSource is a no-op (web rebuilds columns through its own stateless pass).
void ibValueModelTableBox::RefillFromSource() {}
#endif // OES_USE_WEB

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

ENUM_TYPE_REGISTER(ibValueEnumTableBoxSelectionMode, "TableboxRowSelectionMode", enum_to_clsid("EN_TBXSL"));
ENUM_TYPE_REGISTER(ibValueEnumTableBoxViewMode, "TableboxViewMode", enum_to_clsid("EN_TBXVM"));
CONTROL_TYPE_REGISTER(ibValueModelTableBox, "Tablebox", "Container", g_controlTableBoxCLSID);
