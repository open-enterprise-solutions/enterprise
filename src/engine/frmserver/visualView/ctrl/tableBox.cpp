#include "tableBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

#include "form.h"

#include "backend/system/value/valueTable.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"                 // FindAnyObjectByFilter (dot-path metaID -> name)
#include "backend/functionalOption/functionalOptionGate.h"   // ibFunctionalOptionGate::IsAvailable — a tabular section this base does not use
#include "backend/settings/settingsComposer.h"          // the reader's shelf — the setting marked "restore on open" goes on here
#include "backend/system/systemManager.h"               // ibValueSystemFunction::Message — a mark whose setting is gone is said
#include "backend/composition/dataComposer.h"           // the composer a header click sorts through
#include "core/formatString.h"                       // ibFormatString — a column's format, found once per fetch
#include "formAttribute.h"                              // the attribute this box is bound to — its source IS the address
#include "backend/srcDataObject.h"                      // …and the source answers with the guid of what it reads
#include "backend/appData.h"
#include "backend/backend_picture.h"                    // ibBackendPicture::GetServerPicture — a row's picture, as the wire carries it
#include "frmserver/visualView/visualHost.h"            // IsDesignerHost — what the form is held by

#include <algorithm>
#include <optional>

#include <wx/scopeguard.h>   // wxON_BLOCK_EXIT_SET — a fetch's reads are its own, however it ends

// HOW MANY ROWS ONE FETCH MAY ASK FOR, and how many the table keeps handed out at once. Past the
// second, the rows furthest from where the client reads are forgotten (TrimRows).
static const int s_maxFetchCount = 500;
static const size_t s_maxFetchedRows = 2000;

//***********************************************************************************
//*                                 Special tablebox func                           *
//***********************************************************************************

// Single point: resolve a dot-path column's value for ONE row (asked per shown cell by the column's
// UpdateCell). The model stays a plain id->value source: the FIRST hop is a real row column it
// resolves; the deeper hops walk the reference by attribute name. A plain column (row-relative
// tail <= 1) returns false — the cell falls through to the model.
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
	// primitive is the same the designer read uses (GetControlValue's dotted-path branch).
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

// Depth-first through the control tree, columns in the order they will be shown in.
// A column is no longer always a direct child — it may sit inside a column GROUP, and
// groups nest — so the table walks the tree instead of looping over its children,
// which would silently see none of the grouped ones.
static void CollectColumns(const ibValueFrame* parent,
	std::vector<ibValueModelTableBoxColumn*>& out)
{
	if (parent == nullptr)
		return;

	for (unsigned int idx = 0; idx < parent->GetChildCount(); idx++) {

		ibValueFrame* child = parent->GetChild(idx);
		if (child == nullptr)
			continue;

		// dynamic_cast, NOT ConvertToType: the latter goes through CastValue, which
		// answers null for a value whose m_typeClass is TYPE_EMPTY — and a control is
		// such a value. Asking it here silently returned "no columns" for every table
		// in the product, which is what the original code avoided by casting plainly.
		ibValueModelTableBoxColumn* column = dynamic_cast<ibValueModelTableBoxColumn*>(child);
		if (column != nullptr) {
			out.push_back(column);
			continue;
		}

		// A group holds columns (and groups) — walk into it.
		if (dynamic_cast<ibValueModelTableBoxColumnGroup*>(child) != nullptr)
			CollectColumns(child, out);
	}
}


// A COLUMN THE SOURCE PUT IN A GROUP GOES INTO THAT GROUP. The name comes from the source
// (ibSourceExplorer::GetSourceGroup) — the columns of one family say the same thing there,
// and the register's dimension slots are the case this exists for: twelve of them in one
// row is a journal nobody can read, the same twelve in two stacks fit the screen.
ibValueFrame* ibValueModelTableBox::GetColumnGroupHolder(const wxString& group, bool* created)
{
	if (created != nullptr)
		*created = false;

	if (group.IsEmpty())
		return this;

	// The group's control name is the table's plus the family name, which is what makes it
	// findable — so a second column of the family lands in the SAME group, whether or not
	// it came right after the first.
	const wxString groupName = GetControlName() + group;

	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueFrame* child = GetChild(idx);
		if (dynamic_cast<ibValueModelTableBoxColumnGroup*>(child) != nullptr
			&& child->GetControlName() == groupName)
			return child;
	}

	wxASSERT(m_formOwner);
	ibValueModelTableBoxColumnGroup* newGroup =
		dynamic_cast<ibValueModelTableBoxColumnGroup*>(
			m_formOwner->CreateControl(wxT("TableboxColumnGroup"), this));
	if (newGroup == nullptr)
		return this;

	newGroup->SetControlName(groupName);
	newGroup->SetCaption(group);

	if (created != nullptr)
		*created = true;

	return newGroup;
}

void ibValueModelTableBox::AddColumn()
{
	wxASSERT(m_formOwner);

	// A BARE view column — NO source, and NO storage column injected into the bound value-table.
	// A tablebox column on the form is a VIEW that BINDS (through its Source, which may be a dotted path to
	// another / composite field) to a field the user picks; it must not silently add a fourth column to the
	// value-table's schema (that schema is edited via the attribute's own "Add column"). Auto-adding one both
	// duplicated the schema and froze the value-table column id to the CONTROL id (a different id space) — a
	// serialization hazard. Source-less, the new column stays hidden until the user binds it (visibility gate
	// in ibValueModelTableBoxColumn::IsColumnShown), exactly like any other unbound source control.
	m_formOwner->NewObject(g_controlTableBoxColumnCLSID, this);
}

void ibValueModelTableBox::AddColumnGroup()
{
	wxASSERT(m_formOwner);

	m_formOwner->NewObject(g_controlTableBoxColumnGroupCLSID, this);
}

void ibValueModelTableBox::CreateColumnCollection()
{
	if (appData->DesignerMode() || m_formOwner == nullptr || m_tableModel == nullptr)
		return;

	//clear all children — owning handles release the column controls (cascade)
	RemoveAllChildren();

	//create new columns
	ibValueModel::ibValueModelColumnCollection* tableColumns = m_tableModel->GetColumnCollection();
	wxASSERT(tableColumns);
	if (tableColumns == nullptr)
		return;

	for (unsigned int idx = 0; idx < tableColumns->GetColumnCount(); idx++) {

		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* columnInfo = tableColumns->GetColumnInfo(idx);
		ibValueModelTableBoxColumn* newTableBoxColumn =
			m_formOwner->NewObject<ibValueModelTableBoxColumn>(g_controlTableBoxColumnCLSID, this);
		if (columnInfo == nullptr || newTableBoxColumn == nullptr)
			continue;

		const ibTypeDescription& typeDescription = columnInfo->GetColumnType();
		if (typeDescription.IsOk())
			newTableBoxColumn->SetDefaultMetaType(typeDescription);
		else
			newTableBoxColumn->SetDefaultMetaType(ibValueTypes::TYPE_STRING);

		newTableBoxColumn->SetCaption(columnInfo->GetColumnCaption());
		newTableBoxColumn->SetWidthColumn(columnInfo->GetColumnWidth());
		newTableBoxColumn->SetModelColumn(columnInfo->GetColumnID());
	}
}

void ibValueModelTableBox::CreateTable(bool recreateModel) {

	if (recreateModel && m_tableModel != nullptr) m_tableModel = nullptr;

	if (m_tableModel == nullptr) {

		m_tableModel = ibTypeControlFactory::CreateValue();

		if (m_tableModel != nullptr) {
			// Through the WALK, not the children: a column inside a group is still a
			// column of this table and must reach the model like any other.
			std::vector<ibValueModelTableBoxColumn*> columns;
			CollectColumns(this, columns);
			for (ibValueModelTableBoxColumn* columnTable : columns) {
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

// ⭐ THE ADDRESS OF THIS LIST'S SAVED SETTINGS — asked of the ATTRIBUTE this box is bound to, through
// its own source. The source object answers with the guid of the metaobject it reads
// (ibBackendQueryable::GetQueryTableGuid), so "my settings for the Products list" is one address
// wherever that list is opened, and redrawing the control does not lose them.
ibGuid ibValueModelTableBox::SettingsObjectKey() const
{
	if (m_formOwner == nullptr || m_propertySource->IsEmptyProperty())
		return wxNullGuid;

	// The HEAD of the binding is the form's attribute — the deeper hops are how it walked from there.
	const ibFormAttributeValue* attribute = m_formOwner->FindAttributeById(GetSourceDesc().GetFirst());
	if (attribute == nullptr)
		return wxNullGuid;

	const ibSourceDataObject* source = attribute->GetSourceValue();
	if (source == nullptr)
		return wxNullGuid;   // an attribute holding no source shows nothing to configure

	// THE SOURCE'S OWN IDENTITY — for anything that reads a table it IS the metaobject's guid, which
	// is what the queryable answers with (ibBackendQueryable::GetQueryTableGuid).
	return source->GetGuid();
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

void ibValueModelTableBox::ApplyCurrentLine(ibValueModel::ibValueModelReturnLine* line)
{
	// NOTHING ELSE TO MOVE. The client's cursor follows from the frame (CurrentRow) and from the next
	// fetch, which positions on this row when no anchor is given — the row a filter or a refresh
	// re-reads is the row the client comes back to. A stub (FindRowValue: the key alone) is replaced
	// by the real row the moment a fetch hands that row out.
	m_tableCurrentLine = line;
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(this);

	// Selection script event is intentionally NOT fired here — the line
	// passed in for programmatic restore is often a stub from
	// FindRowValue (GUID-only, body empty) and the user-side handler
	// would crash reading unpopulated columns.  The client's cursor
	// goes through OnCurrentRowChanged, which fires the event with a
	// real fetched row.  For "new row created" observation use OnAddRow.
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

// The binding answers for a field, but a tabular section is no column — its node in the source carries none —
// so for it the SECTION the table shows answers. Only for a NESTED source (the table stands on a field of the
// form's object, [head, section]): the form's own list is always available, since a list form of an object
// the base does not use may still be opened from a reference to it, and must not come up empty. (Unbound is
// the other question — IsUnbound.)
bool ibValueModelTableBox::IsAvailable() const
{
	if (!m_propertySource->IsAvailable() || !ibValueWindowComposite::IsAvailable())
		return false;
	if (m_tableModel == nullptr || m_propertySource->GetValueAsSourceDesc().GetHopCount() < 2)
		return true;
	return ibFunctionalOptionGate::IsAvailable(m_tableModel->GetSourceMetaObject());
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
m_tableModel(nullptr), m_tableCurrentLine(nullptr),
m_modelView(nullptr), m_nextRowHandle(1), m_currentColumn(0), m_clientRow(0), m_editRow(0), m_editColumn(0), m_fetchFromTop(false)
{
	m_members.Bind(this, &ibValueModelTableBox::FillControlMembers);

	// Command bar is created by the ibValueWindowComposite base ctor.

	m_propertySource->SetValue(ibTypeDescription(g_valueTableCLSID));

	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 75));
	m_propertyBG->SetValue(wxColour(255, 255, 255));
}

ibValueModelTableBox::~ibValueModelTableBox()
{
	DetachModelView();
}

// Resolve a row identity (reference / column value) to the model's per-row return line wrapper,
// returning nullptr on miss so the caller can fall through to the next candidate value.
static ibValueModel::ibValueModelReturnLine*
ResolveLineByValue(ibValueModel* model, const ibValue& value)
{
	const ibDataViewItem& item = model->FindRowValue(value);
	return item.IsOk() ? model->GetRowAt(item) : nullptr;
}

void ibValueModelTableBox::OnCreated(ibVisualHost* host)
{
	ibValueModelTableBox::CreateModel();

	// ⚠ NOT FOR THE DESIGNER'S PICTURE OF THE FORM: there the box is a picture of itself, and the person
	// drawing the form is not the reader whose settings these are.
	if (host->IsDesignerHost())
		return;

	// ⭐⭐ THE SETTING MARKED "restore on open" GOES ON — right after the model is in place, which is the
	// one moment that happens once per opened form (Max, 2026-08-26). Addressed by the BINDING's leaf,
	// under the list's own category — the settings belong to what is shown, so the box may be redrawn
	// without losing them.
	if (m_tableModel != nullptr) {
		const ibGuid objectKey = SettingsObjectKey();
		// ⚠ SAID OUT LOUD when the mark points at a setting that is not there (Max, 2026-08-26: *"and if
		// it cannot find that setting afterwards, it complains"*): what the person then sees is the
		// author's settings, which looks exactly like the mark being ignored. Silent when nothing is marked.
		if (objectKey.isValid()
			&& ibRestoreDefaultComposerSettings(ibSettingsCategory::List, objectKey,
				m_tableModel->GetModelComposer(), GetMetaData()) == ibDefaultSettingsOutcome::Missing)
			ibValueSystemFunction::Message(
				_("The settings marked to be restored on open could not be found"),
				ibStatusMessage::ibStatusMessage_Warning);
	}

	// …AND A CHOICE FORM'S LIST STANDS ON THE VALUE IT WAS OPENED FOR — once, as the form opens: a later frame
	// must not pull the person back there (the window's OnUpdated did it the first time it ran). The row comes
	// into view with the first fetch, which reads around the current line.
	if (m_tableModel != nullptr && m_formOwner != nullptr && m_tableCurrentLine == nullptr) {
		if (ibControlFrame* const ownerControl = m_formOwner->GetOwnerControl()) {
			ibValue retValue; ownerControl->GetControlValue(retValue);
			if (ibValueModel::ibValueModelReturnLine* const line = ResolveLineByValue(m_tableModel, retValue))
				ApplyCurrentLine(line);
		}
	}
}

void ibValueModelTableBox::OnCleanup(ibVisualHost* host)
{
	// The form closed: the table is no longer its model's view (the window let go of the model here).
	DetachModelView();
}

//***********************************************************************************
//*                                 The model's view                                *
//***********************************************************************************

// ⭐ THE TABLE IS ITS MODEL'S VIEW — what the window was (it was handed the model's data view and listened).
// The model tells its views what happened to its rows, and two of those are the table's business: a row
// ADDED becomes the current line and is heard by the script; a row DELETED is heard by it and is no longer a
// row the client may name. A refresh (Cleared) leaves the handed-out rows as they are: each is a node its
// handle holds, and the client reads again with its next frame — told so by the rows' version, which every
// one of these puts up.
class ibValueModelTableBox::ibTableModelView : public ibDataViewModelNotifier {
public:

	explicit ibTableModelView(ibValueModelTableBox* table) : m_table(table) {}

	virtual bool ItemInserted(const ibDataViewItem& WXUNUSED(parent), const ibDataViewItem& item) override {
		m_table->OnRowAdded(item);
		m_table->OnRowsChanged();
		return true;
	}
	virtual bool ItemAppended(const ibDataViewItem& WXUNUSED(parent), const ibDataViewItem& item) override {
		m_table->OnRowAdded(item);
		m_table->OnRowsChanged();
		return true;
	}
	virtual bool ItemDeleted(const ibDataViewItem& WXUNUSED(parent), const ibDataViewItem& item) override {
		m_table->OnRowDeleted(item);
		m_table->OnRowsChanged();
		return true;
	}
	virtual bool ItemChanged(const ibDataViewItem& WXUNUSED(item)) override {
		m_table->OnRowsChanged();
		return true;
	}
	virtual bool ValueChanged(const ibDataViewItem& WXUNUSED(item), unsigned int WXUNUSED(col)) override {
		m_table->OnRowsChanged();
		return true;
	}
	virtual bool Cleared() override {
		m_table->OnRowsChanged();
		return true;
	}
	virtual void Resort() override { m_table->OnRowsChanged(); }

private:

	ibValueModelTableBox* m_table;
};

void ibValueModelTableBox::OnRowsChanged()
{
	if (m_fetching)
		return;
	m_rowsVersion++;
	// Its Version says so: the next frame draws the table anew, and its client reads the rows again.
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(this);
}

void ibValueModelTableBox::AttachModelView()
{
	if (m_viewModel == m_tableModel)
		return;

	// A REAL MODEL SWAP — a different dataset: the rows the client was given and the line it stood on
	// belong to the old one. The FIRST attach keeps the current line: it may already be set (a created
	// value, the script's CurrentRow) and it is what the first fetch positions on.
	const bool swapped = (m_viewModel != nullptr);
	DetachModelView();
	if (swapped) {
		m_tableCurrentLine.Reset();
		ForgetAllRows();
		m_drillItem = ibDataViewItem();
	}

	ibDataViewModel* dataView = m_tableModel != nullptr ? m_tableModel->GetDataViewModel() : nullptr;
	if (dataView == nullptr)
		return;

	m_modelView = new ibTableModelView(this);
	dataView->AddNotifier(m_modelView);   // …which owns it from here
	m_viewModel = m_tableModel;
}

void ibValueModelTableBox::DetachModelView()
{
	if (m_modelView != nullptr && m_viewModel != nullptr) {
		if (ibDataViewModel* dataView = m_viewModel->GetDataViewModel())
			dataView->RemoveNotifier(m_modelView);   // …which deletes it
	}
	m_modelView = nullptr;
	m_viewModel.Reset();
}

void ibValueModelTableBox::OnRowAdded(const ibDataViewItem& item)
{
	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();

	if (!item.IsOk() || m_tableModel == nullptr)
		return;

	// The new row becomes the current line — the client lands on it with its next fetch (with no anchor
	// a reset reads around the current row). Without it a choice "…" on the fresh row, notably the FIRST
	// row of an empty table, finds no current line and does nothing, and the next Insert lands where the
	// previous one did.
	ApplyCurrentLine(m_tableModel->GetRowAt(item));

	// …and the script hears OnAddRow with the row just added. An insert (Copy / Insert at a position)
	// comes this way too and is heard the same, as both of the window's handlers did.
	if (m_eventOnAddRow != nullptr)
		CallAsEvent(m_eventOnAddRow,
			GetValue(),
			ibValue(m_tableModel->GetRowAt(item)));
}

void ibValueModelTableBox::OnRowDeleted(const ibDataViewItem& item)
{
	if (!item.IsOk())
		return;

	// The model says so once the row is out of it, so a Cancel keeps nothing here — it withholds
	// OnDeleteRow, as the window's veto only kept the row on its own screen.
	ibValue cancel = false;
	CallAsEvent(m_eventBeforeDeleteRow,
		GetValue(), // control
		cancel //cancel
	);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();

	// Symmetric to OnAddRow — fire OnDeleteRow with the row that was removed, still resolvable here.
	if (!cancel.GetBoolean() && m_eventOnDeleteRow != nullptr && m_tableModel != nullptr) {
		CallAsEvent(m_eventOnDeleteRow,
			GetValue(),
			ibValue(m_tableModel->GetRowAt(item)));
	}

	// …and the client may no longer name it. THE CURSOR STAYS IN ITS PLACE — on the row after it, the last row gone on the
	// one before: what the desktop's data view did with its selection, the current line does here, since the client's
	// cursor is the current line.
	if (const s32 handle = FindRowHandle(item)) {
		if (m_tableModel != nullptr && m_tableCurrentLine != nullptr && m_tableCurrentLine->GetLineItem() == item) {
			const auto place = std::find(m_topRows.begin(), m_topRows.end(), handle);
			if (place != m_topRows.end()) {
				const auto next = std::next(place);
				const s32 neighbour = next != m_topRows.end() ? *next : place != m_topRows.begin() ? *std::prev(place) : 0;
				const ibDataViewItem neighbourItem = FindRow(neighbour);
				if (neighbourItem.IsOk())
					ApplyCurrentLine(m_tableModel->GetRowAt(neighbourItem));
			}
		}
		ForgetRow(handle);
	}
}

//***********************************************************************************
//*                                  Update                                         *
//***********************************************************************************

void ibValueModelTableBox::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindowComposite::OnUpdate(state, host);

	state.SetValue(wxT("ChoiceMode"), IsChoiceMode());
	state.SetValue(wxT("ViewMode"), static_cast<s32>(m_propertyViewMode->GetValueAsEnum()));

	// The designer's picture of the form must not attach the runtime data model (a read would issue SQL
	// against a metadata table that doesn't exist yet, or that the designer session has no runtime to query
	// against).
	if (host->IsDesignerHost() || m_tableModel == nullptr) {
		state.SetValue(wxT("Version"), m_rowsVersion);
		return;
	}

	AttachModelView();

	// THE ROWS READ AGAIN on an update of the form — its Update command, an object written in a form it opened: the
	// desktop's table re-read at every update, and a client reads the rows only when their version moves.
	if (m_formOwner != nullptr && m_formOwner->GetUpdateCount() != m_formUpdates) {
		m_formUpdates = m_formOwner->GetUpdateCount();
		m_rowsVersion++;
	}

	if (m_formOwner != nullptr) {

		// Consume createdValue once per save — clearing prevents the same anchor from re-positioning the
		// user on every subsequent manual Refresh / sort / filter, bouncing back to the create row
		// indefinitely. The row it names comes into view with the next fetch, which reads around it.
		//
		// NO changedValue BRANCH. Re-writing an EXISTING element does not re-position the list on it: the
		// current row is a REFCOUNTED node that survives the re-read and re-locates itself by its own
		// row-key (Fetch matches it by identity). A CREATE earns its branch — that row did not exist, so
		// there was nothing to stand on and nothing to re-locate.
		const ibValue createdValue = m_formOwner->ConsumeCreatedValue();
		if (!createdValue.IsEmpty()) {
			if (ibValueModel::ibValueModelReturnLine* const line = ResolveLineByValue(m_tableModel, createdValue))
				ApplyCurrentLine(line);
		}
	}
	state.SetValue(wxT("Version"), m_rowsVersion);

	// The current row as the handle the client was given — HANDED OUT HERE when it was not yet (a choice form opened on
	// a value, a created row not read yet): the client reads around the row it names, the rows above it too. A client
	// that knew no row read "around the current one" as the top of the table, and never read above it.
	s32 currentRow = 0;
	if (m_tableCurrentLine != nullptr) {
		const ibDataViewItem item = m_tableCurrentLine->GetLineItem();
		currentRow = FindRowHandle(item);
		if (currentRow == 0 && item.IsOk()) {
			bool added = false;
			currentRow = HandRow(item, 0, added);
			if (added)
				m_topRows.push_back(currentRow);
		}
	}
	// ⭐ ONLY A ROW THE SERVER MOVED TO — never the client's own said back: an answer drawn before the client told its
	// newer move put the cursor back where the person had left it, and the row "jumped".
	if (currentRow != 0 && currentRow != m_clientRow) {
		state.SetValue(wxT("CurrentRow"), currentRow);
		m_clientRow = currentRow;
	}
	if (m_currentColumn != 0)
		state.SetValue(wxT("CurrentColumn"), static_cast<s32>(m_currentColumn));

	// The cell whose editor is open — while its row is still one the client was given.
	if (m_editRow != 0 && m_fetchedRows.find(m_editRow) != m_fetchedRows.end()) {
		ibDataNode& edit = state.Child(wxT("Edit"));
		edit.SetValue(wxT("Row"), m_editRow);
		edit.SetValue(wxT("Column"), static_cast<s32>(m_editColumn));
	}

	// THE CONTEXT MENU — the desktop's OnContextMenu: the table's standard commands, picked back as Action {Id}; a
	// view-only form greys the ones that modify data here too, as its toolbar does.
	if (m_formOwner != nullptr) {
		const ibStandardCommandSet actions = GetStandardCommands(m_formOwner->GetTypeForm());
		const bool viewOnly = m_formOwner->IsViewOnly();
		ibDataNode& menu = state.Child(wxT("ContextMenu"));
		for (unsigned int idx = 0; idx < actions.GetCount(); idx++) {
			const ibActionID id = actions.GetID(idx);
			if (id == wxNOT_FOUND)
				continue;
			ibDataNode& entry = menu.AddChild(0, 0);
			entry.SetValue(wxT("Id"), static_cast<s32>(id));
			entry.SetValue(wxT("Caption"), actions.GetCaptionByID(id));
			entry.SetValue(wxT("Picture"), host->SendPicture(actions.GetPictureByID(id)));
			entry.SetValue(wxT("Enabled"), !(viewOnly && actions.GetModifiesDataByID(id)));
		}
	}
}

//***********************************************************************************
//*                              The rows handed out                                *
//***********************************************************************************

s32 ibValueModelTableBox::HandRow(const ibDataViewItem& item, s32 parentHandle, bool& added,
	const std::map<const ibDataViewObject*, s32>* handedBefore)
{
	// A row already handed out keeps its handle — the client may hold it.
	const auto known = m_fetchedHandles.find(item.GetID());
	if (known != m_fetchedHandles.end()) {
		added = false;
		return known->second;
	}

	// …and one handed out before a read anew, read again as the same object, the handle it had (Fetch).
	s32 handle = 0;
	if (handedBefore != nullptr) {
		const auto before = handedBefore->find(item.GetID());
		if (before != handedBefore->end())
			handle = before->second;
	}
	if (handle == 0)
		handle = m_nextRowHandle++;
	ibFetchedRow& row = m_fetchedRows[handle];
	row.m_item = item;
	row.m_parent = parentHandle;
	if (parentHandle != 0) {
		const auto parent = m_fetchedRows.find(parentHandle);
		if (parent != m_fetchedRows.end())
			parent->second.m_children.push_back(handle);
	}
	m_fetchedHandles[item.GetID()] = handle;
	added = true;
	return handle;
}

ibDataViewItem ibValueModelTableBox::FindRow(s32 handle) const
{
	const auto found = handle != 0 ? m_fetchedRows.find(handle) : m_fetchedRows.end();
	return found != m_fetchedRows.end() ? found->second.m_item : ibDataViewItem();
}

s32 ibValueModelTableBox::FindRowHandle(const ibDataViewItem& item) const
{
	if (!item.IsOk())
		return 0;

	const auto known = m_fetchedHandles.find(item.GetID());
	if (known != m_fetchedHandles.end())
		return known->second;

	// …a row read again as a new object, or a restore stub carrying its key alone: the same row by its
	// identity (ibDataViewObject::IsEqualTo).
	for (const auto& row : m_fetchedRows) {
		if (row.second.m_item == item)
			return row.first;
	}
	return 0;
}

void ibValueModelTableBox::ForgetRow(s32 handle)
{
	const auto found = m_fetchedRows.find(handle);
	if (found == m_fetchedRows.end())
		return;

	ForgetChildRows(handle);

	const s32 parentHandle = found->second.m_parent;
	if (parentHandle == 0) {
		const auto top = std::find(m_topRows.begin(), m_topRows.end(), handle);
		if (top != m_topRows.end())
			m_topRows.erase(top);
	}
	else {
		const auto parent = m_fetchedRows.find(parentHandle);
		if (parent != m_fetchedRows.end()) {
			std::vector<s32>& siblings = parent->second.m_children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
		}
	}

	m_fetchedHandles.erase(found->second.m_item.GetID());
	m_fetchedRows.erase(found);
}

void ibValueModelTableBox::ForgetChildRows(s32 handle)
{
	const auto found = m_fetchedRows.find(handle);
	if (found == m_fetchedRows.end())
		return;

	// Taken off first: each child, forgotten, would take itself off this list while it is walked.
	const std::vector<s32> children = std::move(found->second.m_children);
	found->second.m_children.clear();
	for (const s32 child : children)
		ForgetRow(child);
}

void ibValueModelTableBox::ForgetAllRows()
{
	m_fetchedRows.clear();
	m_fetchedHandles.clear();
	m_topRows.clear();
}

void ibValueModelTableBox::TrimRows(bool fromBack, s32 keep)
{
	while (m_fetchedRows.size() > s_maxFetchedRows && !m_topRows.empty()) {
		const s32 handle = fromBack ? m_topRows.back() : m_topRows.front();
		if (handle == keep)
			break;
		ForgetRow(handle);
	}
}

//***********************************************************************************
//*                                    Fetch                                        *
//***********************************************************************************

bool ibValueModelTableBox::Fetch(const ibDataNode& request, ibDataNode& response)
{
	// Nothing is read in the designer (see Update).
	if (m_tableModel == nullptr || appData->DesignerMode())
		return false;

	AttachModelView();

	// What the model says while this reads is this read's — not a change the client must read again for.
	m_fetching = true;
	wxON_BLOCK_EXIT_SET(m_fetching, false);

	const s32 direction = request.GetValue<s32>(wxT("Direction"));
	const s32 anchorHandle = request.GetValue<s32>(wxT("Anchor"));
	const s32 parentHandle = request.GetValue<s32>(wxT("Parent"));
	const int count = std::clamp<int>(request.GetValue<s32>(wxT("Count")), 1, s_maxFetchCount);

	// THE LEVEL — the container the client opened (a tree's expanded row, the folder drilled into), else
	// the top. A FLAT List view passes the ignore-parent SENTINEL, so the model walks the WHOLE table in one
	// order; any other view an EMPTY parent, so it returns the roots.
	ibDataViewItem parentItem;
	if (parentHandle != 0) {
		parentItem = FindRow(parentHandle);
		if (!parentItem.IsOk())
			return false;   // a container the table no longer holds — the client reads the top again
	}
	const ibDataViewViewMode viewMode = m_propertyViewMode->GetValueAsEnum();
	const ibDataViewItem scope = parentItem.IsOk() ? parentItem
		: (viewMode == ibDataViewList ? s_constIgnoreParent : ibDataViewItem());

	// THE DIRECTION — a step needs a row to step from: an anchor the table does not know is a reset.
	const ibDataViewItem anchorItem = FindRow(anchorHandle);
	ibFetchDirection dir = direction > 0 ? ibFetchDirection::Forward
		: direction < 0 ? ibFetchDirection::Backward : ibFetchDirection::Reset;
	if (dir != ibFetchDirection::Reset && !anchorItem.IsOk())
		dir = ibFetchDirection::Reset;

	// A table read from nothing — no anchor, the top — is one the client built anew: it has no cursor, and is told the
	// current row whatever it stood on before.
	if (dir == ibFetchDirection::Reset && anchorHandle == 0 && parentHandle == 0)
		m_clientRow = 0;

	// (A read that fails throws, and the throw is the caller's to put in front of the person — the rows
	// already handed out stay as they were.)
	ibDataViewItemArray items;
	unsigned int fetched = 0;

	// THE ROWS HANDED OUT BEFORE A READ ANEW — kept alive through it, so no object freed meanwhile comes back at the
	// same address as another row: one read again as the SAME object (a table in memory hands out its own rows) keeps
	// its handle, and the client's cursor stays on it through the re-read instead of going off and on with every change.
	std::map<s32, ibFetchedRow> rowsBefore;
	std::map<const ibDataViewObject*, s32> handlesBefore;
	switch (dir) {
	case ibFetchDirection::Forward:
		fetched = m_tableModel->GetNextFetch(scope, anchorItem, count, items);
		break;
	case ibFetchDirection::Backward:
		fetched = m_tableModel->GetPrevFetch(scope, anchorItem, count, items);
		break;
	default: {
		// A RESET READS AROUND THE CURRENT ROW when the client names no anchor — the row a filter, a
		// refresh or a created value left the table on comes back into view. Only a row of the level being
		// read, and not after a sort: its place was a place in the old order.
		ibDataViewItem around = anchorItem;
		if (!around.IsOk() && !m_fetchFromTop && m_tableCurrentLine != nullptr) {
			const ibDataViewItem current = m_tableCurrentLine->GetLineItem();
			if (scope == s_constIgnoreParent || current.GetParentItem() == parentItem)
				around = current;
		}
		// The level is read anew: the rows handed out at it are forgotten — every row, at the top.
		if (parentHandle == 0) {
			m_fetchFromTop = false;
			rowsBefore.swap(m_fetchedRows);
			handlesBefore.swap(m_fetchedHandles);
			ForgetAllRows();
		}
		else {
			ForgetChildRows(parentHandle);
		}
		// The Hierarchical view shows one level, and the level read last is the one the client stands in.
		if (viewMode == ibDataViewHierarchical)
			m_drillItem = parentItem;
		fetched = m_tableModel->GetFirstFetch(scope, around, count, items);
		// …AND ONE THAT FINDS NOTHING AROUND ITS ROW READS FROM THE TOP — the data view's own rule (its empty-fetch
		// fallback): a row a filter took away, or one the new order cannot be read around, leaves no blank table. Asked
		// here, since a client's own retry without an anchor would be read around the current row again.
		if (fetched == 0 && around.IsOk()) {
			items.Clear();
			fetched = m_tableModel->GetFirstFetch(scope, ibDataViewItem(), count, items);
		}
		break;
	}
	}

	// THE CELLS ARE WRITTEN THROUGH EACH COLUMN'S FORMAT, found once for the whole fetch (finding it per
	// cell was a third of preparing a value — paint probe, 2026-09-26) — and only for the columns shown.
	std::vector<ibValueModelTableBoxColumn*> allColumns, columns;
	CollectColumns(this, allColumns);
	for (ibValueModelTableBoxColumn* column : allColumns) {
		if (column->IsColumnShown())
			columns.push_back(column);
	}
	std::vector<std::optional<ibFormatString>> formats;
	std::vector<bool> columnEditable;
	formats.reserve(columns.size());
	columnEditable.reserve(columns.size());
	for (ibValueModelTableBoxColumn* column : columns) {
		const ibFormatString* format = column->GetCellFormat();
		formats.push_back(format != nullptr ? std::optional<ibFormatString>(*format) : std::nullopt);
		columnEditable.push_back(!column->IsReadOnly());
	}

	const ibDataViewItem current = m_tableCurrentLine != nullptr
		? m_tableCurrentLine->GetLineItem() : ibDataViewItem();
	s32 currentHandle = 0, firstHandle = 0;
	std::vector<s32> newTopRows;

	// The pictures handed with this read — made once, at the first: Child makes its node anew at every call, so a
	// second picture in one read wiped the first (a folder read with its first element went without its picture).
	ibDataNode* pictures = nullptr;

	for (size_t idx = 0; idx < items.GetCount(); idx++) {

		const ibDataViewItem& item = items[idx];
		if (!item.IsOk())
			continue;

		bool added = false;
		const s32 handle = HandRow(item, parentHandle, added, &handlesBefore);
		if (added && parentHandle == 0)
			newTopRows.push_back(handle);
		if (firstHandle == 0)
			firstHandle = handle;

		// The current row handed out. Known only by its key (a restore stub) or by an older object, it
		// becomes this row — the current line reads what was just read.
		if (currentHandle == 0 && current.IsOk() && item == current) {
			currentHandle = handle;
			if (item.GetID() != current.GetID())
				m_tableCurrentLine = m_tableModel->GetRowAt(item);
		}

		ibDataNode& row = response.AddChild(0, handle);
		row.SetValue(wxT("Container"), item.IsContainer());

		wxString groupCaption;
		if (item.GetGroupCaption(groupCaption))
			row.SetValue(wxT("Group"), groupCaption);

		// The row's state picture, by its id — a decimal string: a u64 is past what a JSON number keeps. The
		// picture itself goes with the first row of it the client is handed.
		const ibPictureID picture = m_tableModel->GetRowPicture(item);
		if (picture != 0) {
			const wxString pictureId = wxString::Format(wxT("%llu"), static_cast<unsigned long long>(picture));
			row.SetValue(wxT("Picture"), pictureId);
			if (m_sentPictures.insert(picture).second) {
				const ibServerPicture sentPicture = ibBackendPicture::GetServerPicture(ibPictureDescription(picture));
				if (!sentPicture.IsOk())
					ibJournalWarning(wxT("client"), wxT("row picture %s has no server picture: the client draws none"), pictureId);
				if (pictures == nullptr)
					pictures = &response.Child(wxT("Pictures"));
				ibDataNode& sent = pictures->AddChild(0, 0);
				sent.SetValue(wxT("Id"), pictureId);
				sent.SetValue(wxT("Picture"), wxString(sentPicture.GetData()));
			}
		}

		// A row none of whose shown cells can be edited (a list row, a group heading) — the client opens
		// no inline editor on it, and a double-click goes to the table (OnRowActivated).
		bool rowEditable = false;
		for (size_t col = 0; col < columns.size() && !rowEditable; col++)
			rowEditable = columnEditable[col]
				&& m_tableModel->EditableLine(item, static_cast<unsigned int>(columns[col]->GetModelColumn()));
		if (!rowEditable)
			row.SetValue(wxT("ReadOnly"), true);

		for (size_t col = 0; col < columns.size(); col++) {
			// A container's cells only where the model has a value — and always the first column shown, where the
			// data view draws a folder's own (the desktop's expander column). A cell not written is drawn empty.
			if (col != 0 && !m_tableModel->HasValue(item, static_cast<unsigned int>(columns[col]->GetModelColumn())))
				continue;
			ibDataNode& cell = row.AddChild(0, columns[col]->GetControlID());
			columns[col]->UpdateCell(item, cell, formats[col].has_value() ? &*formats[col] : nullptr, !columnEditable[col]);
		}
	}

	// A backward portion stands BEFORE the rows the client has; the others after them.
	if (dir == ibFetchDirection::Backward)
		m_topRows.insert(m_topRows.begin(), newTopRows.begin(), newTopRows.end());
	else
		m_topRows.insert(m_topRows.end(), newTopRows.begin(), newTopRows.end());

	// NO CURRENT ROW YET — the first one read becomes it, so the table stands where the client shows its
	// cursor (a choice "…" on that row would otherwise find no current line and skip the choice form).
	if (dir == ibFetchDirection::Reset && m_tableCurrentLine == nullptr && firstHandle != 0) {
		ApplyCurrentLine(m_tableModel->GetRowAt(FindRow(firstHandle)));
		currentHandle = firstHandle;
	}

	// Over the limit, the rows furthest from where the client reads go — never the top-level row this
	// portion was read under.
	s32 keep = parentHandle;
	for (auto up = m_fetchedRows.find(keep); up != m_fetchedRows.end() && up->second.m_parent != 0;
		up = m_fetchedRows.find(keep))
		keep = up->second.m_parent;
	TrimRows(dir == ibFetchDirection::Backward, keep);

	if (fetched < static_cast<unsigned int>(count))
		response.SetValue(wxT("End"), true);
	// …the same for a read: the row the server moved to, not the client's own.
	if (currentHandle != 0 && currentHandle != m_clientRow && m_fetchedRows.find(currentHandle) != m_fetchedRows.end()) {
		response.SetValue(wxT("CurrentRow"), currentHandle);
		m_clientRow = currentHandle;
	}
	return true;
}

//***********************************************************************************
//*                              What the client did                                *
//***********************************************************************************

bool ibValueModelTableBox::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	const bool onCell = args.FindField(wxT("Column")) != nullptr;

	switch (event) {
	case ibProtocolEvent::Row: {
		const s32 rowHandle = args.GetValue<s32>(wxT("Row"));
		const ibFormID columnId = onCell ? args.GetValue<s32>(wxT("Column")) : m_currentColumn;
		// The cursor moved off the cell being edited: its editor is closed, and what was typed and not
		// committed (Change) is dropped.
		if (rowHandle != m_editRow || columnId != m_editColumn)
			m_editRow = 0;
		OnCurrentRowChanged(rowHandle);
		if (onCell)
			OnCurrentColumnChanged(columnId);
		return true;
	}
	case ibProtocolEvent::Open:
		if (onCell)
			return OnCellEvent(event, args);
		OnRowActivated(args.GetValue<s32>(wxT("Row")));
		return true;
	case ibProtocolEvent::Input:
	case ibProtocolEvent::Change:
	case ibProtocolEvent::Select:
	case ibProtocolEvent::Clear:
		return OnCellEvent(event, args);
	case ibProtocolEvent::Sort:
		OnColumnClick(args.GetValue<s32>(wxT("Column")));
		return true;
	case ibProtocolEvent::Move:
		return OnColumnMoved(args.GetValue<s32>(wxT("Column")), args.GetValue<s32>(wxT("Holder")),
			static_cast<unsigned int>(args.GetValue<s32>(wxT("Position"))));
	case ibProtocolEvent::Resize:
		OnColumnResized(args.GetValue<s32>(wxT("Column")), args.GetValue<s32>(wxT("Width")));
		return true;
	case ibProtocolEvent::Action: {
		// One of the commands its context menu offered — the desktop's OnCommandMenu; one it did not offer, or greyed
		// there, is not done.
		if (m_formOwner == nullptr)
			return false;
		const ibActionID id = args.GetValue<s32>(wxT("Id"));
		const ibStandardCommandSet actions = GetStandardCommands(m_formOwner->GetTypeForm());
		bool offered = false;
		for (unsigned int idx = 0; idx < actions.GetCount() && !offered; idx++)
			offered = actions.GetID(idx) == id;
		if (!offered || (m_formOwner->IsViewOnly() && actions.GetModifiesDataByID(id)))
			return false;
		CallAsAction(id, m_formOwner);
		return true;
	}
	default:
		return ibValueWindowComposite::OnClientEvent(event, args);
	}
}

bool ibValueModelTableBox::OnCellEvent(ibProtocolEvent event, const ibDataNode& args)
{
	const s32 rowHandle = args.GetValue<s32>(wxT("Row"));
	ibValueModelTableBoxColumn* const column = FindColumn(args.GetValue<s32>(wxT("Column")));
	if (column == nullptr || !OnCurrentRowChanged(rowHandle))
		return false;
	OnCurrentColumnChanged(column->GetControlID());

	switch (event) {
	case ibProtocolEvent::Input:
		// The client opened its editor on the cell — the table takes it, or the next frame (no Edit) closes it.
		if (OnCellEditStarting(rowHandle, column->GetControlID())) {
			m_editRow = rowHandle;
			m_editColumn = column->GetControlID();
		}
		else if (m_editRow == rowHandle && m_editColumn == column->GetControlID())
			m_editRow = 0;
		return true;
	case ibProtocolEvent::Change:
		// The value committed into the current line's cell — with its type when the client has one (Value), else the
		// text it typed — and the edit is over.
		m_editRow = 0;
		if (const ibDataNode* const value = args.FindChild(wxT("Value")))
			column->ValueProcessing(*value);
		else
			column->OnTextEnter(args.GetValue<wxString>(wxT("Text")));
		column->OnEditingDone();
		return true;
	case ibProtocolEvent::Select: column->OnSelectButtonPressed(); return true;
	case ibProtocolEvent::Open:   column->OnOpenButtonPressed();   return true;
	case ibProtocolEvent::Clear:  column->OnClearButtonPressed();  return true;
	default:                    return false;
	}
}

bool ibValueModelTableBox::OnCurrentRowChanged(s32 rowHandle)
{
	// The client's cursor stands there now, whatever the script answers below.
	m_clientRow = rowHandle;

	const ibDataViewItem item = FindRow(rowHandle);
	if (!item.IsOk() || m_tableModel == nullptr)
		return false;

	// Already there — nothing moved, and the script hears only a move.
	if (m_tableCurrentLine != nullptr && m_tableCurrentLine->GetLineItem() == item)
		return true;

	// The script hears it first and may refuse it: standard processing off keeps the current line where it
	// was, and the next frame puts the client's cursor back on it.
	const ibValuePtr<ibValueModel::ibValueModelReturnLine> line(m_tableModel->GetRowAt(item));
	ibValue standardProcessing = true;
	CallAsEvent(m_eventSelection,
		GetValue(), // control
		ibValue(line), // rowSelected
		standardProcessing //standardProcessing
	);
	if (!standardProcessing.GetBoolean())
		return false;

	ApplyCurrentLine(line);
	return true;
}

void ibValueModelTableBox::OnCurrentColumnChanged(const ibFormID& columnId)
{
	m_currentColumn = FindColumn(columnId) != nullptr ? columnId : 0;
}

bool ibValueModelTableBox::OnColumnClick(const ibFormID& columnId)
{
	// Everything on the TABLE: its model's composer is poked and the rows re-read — the backend stays
	// blind (no sort method / no dispatch). The same shape the settings window uses (the setting on the
	// composer + a re-read); the column already knows its OWN bound field (GetSourceFieldName), and the
	// composer dot-walks it exactly as the cell resolves its value.
	ibValueModelTableBoxColumn* column = FindColumn(columnId);
	if (m_tableModel == nullptr || column == nullptr || appData->DesignerMode()
		|| !m_tableModel->GetFeatures().Has(ibValueModel::Features::Sorting))
		return false;

	const wxString field = column->GetSourceFieldName();
	if (field.IsEmpty())
		return false;   // whole-attribute / foreign / unresolvable column — nothing to sort by

	ibDataComposer& composer = m_tableModel->GetModelComposer();
	// Single-column toggle read off the composer (the SSOT): already the sole sort on this field → flip; else asc.
	bool ascending = true;
	wxString curField; bool curAsc = true;
	if (composer.SortCount() == 1 && composer.GetSortAt(0, curField, curAsc) && curField == field)
		ascending = !curAsc;

	composer.ClearSorts();
	composer.Sort(field, ascending);

	// The header arrows are each column's to say (its OnUpdate reads the composer). A sort change invalidates
	// the position the current row had (the keyset anchor was built for the OLD ORDER BY) — the next reset
	// reads fresh from the top.
	m_fetchFromTop = true;
	m_tableModel->RefetchAll();
	return true;
}

// A COLUMN LANDS WHERE IT WAS DROPPED — including in whose GROUP.
//
// The client says which holder the column is in now and at what place, and the CONTROL tree is brought to
// the same story: the column goes to that holder's control (a group, or the table itself — what "not in
// any group" means on a form) at that place. Two answers to "who holds it" is precisely how the tree and
// the form used to disagree.
bool ibValueModelTableBox::OnColumnMoved(const ibFormID& columnId, const ibFormID& holderId, unsigned int position)
{
	ibValueModelTableBoxColumn* column = FindColumn(columnId);
	if (column == nullptr)
		return false;

	ibValueFrame* holder = this;
	if (holderId != 0 && holderId != GetControlID()) {
		ibValueModelTableBoxColumnGroup* group =
			m_formOwner != nullptr ? dynamic_cast<ibValueModelTableBoxColumnGroup*>(FindControlByID(holderId)) : nullptr;
		if (group == nullptr || group->GetOwner() != this)
			return false;
		holder = group;
	}

	// ONE PATH for both cases — a move inside the same holder and a move into another one
	// differ only in which parent it is taken off.
	ibValuePtr<ibValueFrame> keep(column);
	if (ibValueFrame* oldHolder = column->GetParent())
		oldHolder->RemoveChild(column);

	column->SetParent(holder);
	holder->AddChild(wxMin(position, holder->GetChildCount()), column);
	return true;
}

void ibValueModelTableBox::OnColumnResized(const ibFormID& columnId, int width)
{
	// THE WIDTH THE COLUMN ASKS FOR, not the one it currently shows.
	//
	// What the form stores is a REQUEST — the width this column wants — and the client then stretches the
	// columns it has room for. Storing the stretched width instead made the request grow every time: a
	// column asking 80 and shown 126 came back as asking 126, so reopening the form (or narrowing it a
	// little) went straight to a scrollbar. The client sends the request; this only records it.
	if (ibValueModelTableBoxColumn* column = FindColumn(columnId)) {
		if (width > 0)
			column->SetWidthColumn(width);
	}
}

void ibValueModelTableBox::OnViewModeChanged(ibDataViewViewMode viewMode)
{
	// Written back onto the PROPERTY — the one the next fetch reads (a List view walks the whole table
	// flat) and the one the form is saved with. Not in the designer: there it is the author's choice.
	if (appData->DesignerMode())
		return;

	m_propertyViewMode->SetValue(viewMode);
	// A drill is the Hierarchical view's own; the client reads the top again in whichever it now is.
	m_drillItem = ibDataViewItem();
}

void ibValueModelTableBox::OnRowActivated(s32 rowHandle)
{
	// The row activated is the one the cursor stands on — a choice returns the current line.
	if (!OnCurrentRowChanged(rowHandle))
		return;

	const ibFormID editColumn = ActivateRow(FindRow(rowHandle));
	if (editColumn != 0) {
		m_editRow = rowHandle;
		m_editColumn = editColumn;
	}

	CallAsEvent(m_eventOnActivateRow,
		GetValue() // control
	);
}

// Double-click / Enter dispatch: choice → SELECT; a row with an editable cell → the client's inline editor
// (on the column answered); a read-only row → open its VALUE (the model's ActivateItem raises the object
// form — a list / register recorder).
ibFormID ibValueModelTableBox::ActivateRow(const ibDataViewItem& item)
{
	if (!item.IsOk() || m_tableModel == nullptr)
		return 0;

	if (IsChoiceMode()) {
		Command_Choose(m_formOwner);
		return 0;
	}

	if (const ibValueModelTableBoxColumn* column = FindEditableColumn(item))
		return column->GetControlID();               // editable cell → the client opens its editor there

	m_tableModel->ActivateItem(item, m_formOwner);  // none → open the row's value (a list opens its object form)
	return 0;
}

bool ibValueModelTableBox::OnCellEditStarting(s32 rowHandle, const ibFormID& columnId) const
{
	// A cell whose column is read-only (a dot-path / header column, a view-only form) or whose row the model
	// does not let edit (a group heading, a list row) takes no editor — the window's veto, said before it opens.
	const ibDataViewItem item = FindRow(rowHandle);
	const ibValueModelTableBoxColumn* column = FindColumn(columnId);
	return item.IsOk() && column != nullptr && column->IsCellEditable(item);
}

ibValueModelTableBoxColumn* ibValueModelTableBox::FindEditableColumn(const ibDataViewItem& item) const
{
	if (!item.IsOk() || m_tableModel == nullptr)
		return nullptr;

	// Prefer the column the cursor stands on; else the first shown column whose cell is editable (skips a
	// read-only one like the tabular line-number).
	ibValueModelTableBoxColumn* current = FindColumn(m_currentColumn);
	if (current != nullptr && current->IsColumnShown() && current->IsCellEditable(item))
		return current;

	std::vector<ibValueModelTableBoxColumn*> columns;
	CollectColumns(this, columns);
	for (ibValueModelTableBoxColumn* column : columns) {
		if (column->IsColumnShown() && column->IsCellEditable(item))
			return column;
	}
	return nullptr;
}

ibValueModelTableBoxColumn* ibValueModelTableBox::FindColumn(const ibFormID& columnId) const
{
	if (columnId == 0)
		return nullptr;

	// Walked, not looked up on the form: a column of ANOTHER table carrying the id is not this table's.
	std::vector<ibValueModelTableBoxColumn*> columns;
	CollectColumns(this, columns);
	for (ibValueModelTableBoxColumn* column : columns) {
		if (column->GetControlID() == columnId)
			return column;
	}
	return nullptr;
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
	// 🛑 THE VIEW MODE WAS MISSING FROM BOTH LISTS. It is read at build time and written back when the
	// user switches list/tree at runtime — and then had no road to the file, so a designer's choice
	// came back as the default on the next open (audit, 2026-08-24). Both halves are explicit lists
	// here, so a property added to the class and not to them is silently not saved.
	m_propertyViewMode->SetNodeValue(node.GetProperty(m_propertyViewMode->GetName()));

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
	node.SetProperty(m_propertyViewMode->GetName(), m_propertyViewMode->GetNodeValue());   // see ReadData

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

	if (refreshColumn && m_tableModel != nullptr && m_tableModel->AutoCreateColumn()) {
		ibValueModelTableBox::CreateColumnCollection();
	}

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

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

ENUM_TYPE_REGISTER(ibValueEnumTableBoxSelectionMode, "TableboxRowSelectionMode", enum_to_clsid("EN_TBXSL"));
ENUM_TYPE_REGISTER(ibValueEnumTableBoxViewMode, "TableboxViewMode", enum_to_clsid("EN_TBXVM"));
CONTROL_TYPE_REGISTER(ibValueModelTableBox, "Tablebox", "Container", g_controlTableBoxCLSID);
