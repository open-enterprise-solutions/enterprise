////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value-model base (shared) + the composer node
////////////////////////////////////////////////////////////////////////////
//
// The SHARED half of the value-model: the ibValueModel base (settings / selection / actions / iteration /
// column collection) + the ibComposerNode out-of-line members. The two paged-fetch realisations live in their
// own translation units — the DB fetch in tableInfoDb.cpp (ibValueModelCursor::RunComposerPage), the in-place RAM
// fetch + composer in tableInfoRam.cpp (ibValueModelStorage::RunComposerPage / ibDataRamComposer).

#include "tableInfo.h"

#include "backend/session/session.h"            // ibSession — SubmitFetchAsync
#include "backend/tableView.h"                  // ibDataViewModel / ibDataViewItem — the base view types
#include "backend/composition/listFilter.h"    // ibValueListSettings (dialog buffer) + ibLoad/CommitSettings — settings live on the composer
#include "backend/composition/dataComposer.h"  // ibDataComposer — IsGroupedModel reads GroupCount() off the composer
#include "backend/uniqueKey.h"                  // ibUniqueKey — GetItemKey return (base default = no key)

// (ibValueModelRamTreeBase::PopulateFromTree + its MirrorQueryNodes helper were DELETED with the dead
// RamTreeBase class — the in-memory mirror tree is superseded by hierarchy GROUPING over the composer.)


// ---------------------------------------------------------------------------
// ibComposerNode composer-fetch ctors + SetValue (in-memory source unification). ibComposerNode
// collapsed into the universal row (Max: "collapse …"); these out-of-line members are non-trivial, so they
// live here rather than in the header.

// DETAIL row (a DB-list fetch copy): copy the L5 driver row's values + keep the primary-key row-key as the
// re-fetch selection identity. NO write-back source / backing index any more — a DB grid is READ-ONLY (editing
// is via the object form), and a RAM list edits its LIVE storage rows directly (it never makes these copies).
ibValueModel::ibComposerNode::ibComposerNode(const std::map<ibMetaID, ibValue>& values,
	bool container, std::vector<ibValue> rowKey)
	: m_valueTable(nullptr), m_rowKey(std::move(rowKey)),
	  m_container(container), m_selfContained(true)
{
	for (const auto& kv : values)
		AppendTableValue(kv.first, kv.second);
}

// STUB row (FindRowValue selection-restore): only the row-key the caller navigates to.
ibValueModel::ibComposerNode::ibComposerNode(std::vector<ibValue> rowKey)
	: m_valueTable(nullptr), m_rowKey(std::move(rowKey)), m_selfContained(true)
{
}

// GROUP-drill row: the dimension-value path root->this + the drillable container flag.
ibValueModel::ibComposerNode::ibComposerNode(const std::map<ibMetaID, ibValue>& values,
	const std::vector<ibValue>& groupPath, bool container)
	: m_valueTable(nullptr), m_groupPath(groupPath), m_container(container), m_selfContained(true)
{
	for (const auto& kv : values)
		AppendTableValue(kv.first, kv.second);
}

// THE node mirrors an edited cell into its own value map and notifies the owning model. A RAM list edits its
// LIVE storage rows THROUGH this (m_valueTable set → the storage IS updated + the model refreshes); a DB-list
// COPY node (m_valueTable null, self-contained) just mirrors into the copy for the inline editor (the grid is
// read-only — a real edit goes through the object form). No write-back source / backing index any more.
bool ibValueModel::ibComposerNode::SetValue(const ibMetaID& id, const ibValue& variant, bool notify)
{
	auto iterator = m_nodeValues.find(id);
	if (iterator == m_nodeValues.end())
		return false;
	ibValue& cValue = m_nodeValues.at(id);
	if (notify && m_valueTable != nullptr && cValue != variant)
		m_valueTable->RowValueChanged(this, id);
	cValue = variant;
	return true;
}

// Out-of-line (declared in tableInfo.h): the ibValuePtr<ibValueListSettings> -> ibValueListSettings* conversion
// needs the COMPLETE ibValueListSettings type (listFilter.h, included here), not the header's forward decl.
ibValueListSettings* ibValueModel::GetListSettings() const { return m_listSettings; }


ibValueModel::ibValueModel()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
	m_modelProvider(nullptr)
{
	m_modelProvider = new ibDataViewModelProviderImpl(this);

	// The model's RUNTIME/script settings — a FACADE (thin live wrapper) over the composer (the store): a
	// script's list.Settings.Filter.Add(...) writes the composer IMMEDIATELY and fires this model's refresh
	// (NotifyReset). It is a SYSTEM type owned by the model, NOT created standalone (Max: "this is a system type,
	// lives inside the model, the composer is passed to it"). The settings DIALOG uses its OWN separate BUFFER copy
	// (load on open, commit on OK) so the form stays transactional. Held via ibValuePtr (DecrRef on dtor).
	m_listSettings = new ibValueListSettings(*this, [this] { NotifyReset(); });

	// (m_composer is POLYMORPHIC + lazily created — the facade resolves GetModelComposer() on first use, by
	// which time the full subclass exists so CreateComposer picks ibDataDBComposer (DB) / ibDataRamComposer (RAM).
	// Subclasses set their source + default sort/grouping STRAIGHT on it in their ctor —
	// GetModelComposer().FromSource(q).Sort(...) — the persistent settings store the fetch reads.)
}

ibValueModel::~ibValueModel()
{
	// The RAM node storage (ibRamValueStorage) is a member of ibValueModelStorage — its dtor DecRefs the nodes; the
	// base just drops the view provider. (A DB model has no node storage.)
	m_modelProvider->DecRef();
}

// (Header-click sort lives on the FRONT — ibValueModelTableBox::OnColumnClick pokes this model's composer
//  directly (ClearSorts + Sort by the column's own bound field name) + RefetchAll. No SortBy on the model.)

// (GetSortModels DELETED — ibSortModel is gone. The sort is L5 (the composer, by field NAME); the few
//  consumers that needed a {col-id, asc} pair — the keyset anchor + the frontend header arrows — now read
//  m_composer.GetSortAt + GetColumnIDByName inline, each at the point of use. Max: "ibSortModel is either part
//  of L5, or removed entirely" — L5 is name-based, so it was removed.)

// Any active filter? Reads the COMPOSER (the single settings store; m_filterRow abolished). The composer only
// ever holds ACTIVE filters (a disabled dialog line is dropped on commit), so a non-empty filter list = active.
bool ibValueModel::UseListSettings() const
{
	// CAPABILITY: does this table expose the settings affordance (Filter / Sort / Group)? Drives the toolbar
	// "Filter" (= open List-Settings) button group. NOT "is a filter active" — that is m_composer.FilterCount().
	return GetFeatures().Has(Features::Filters);
}

ibDataViewItem ibValueModel::GetSelection() const
{
	if (m_modelProvider == nullptr)
		return ibDataViewItem(nullptr);
	return m_modelProvider->GetSelection();
}

ibDataViewItem ibValueModel::GetDrillParent() const
{
	if (m_modelProvider == nullptr)
		return ibDataViewItem();
	return m_modelProvider->GetDrillParent();
}

std::future<void> ibValueModel::SubmitFetchAsync(std::function<void()> work)
{
	auto* sess = ibSession::Current();
	if (sess != nullptr)
		return sess->Submit(std::move(work));
	if (work) work();
	std::promise<void> p;
	p.set_value();
	return p.get_future();
}

namespace {
// Cursor-paginated iteration over any flat paged model. Drives the
// model's GetFirstFetch / GetNextFetch surface in fixed-size batches
// and wraps each item via GetRowAt — the same factory the GUI uses to
// turn an ibDataViewItem into a script-visible ReturnLine. RAM-paged
// children pick up filter+sort consistency for free (their Get*Fetch
// slices BuildVisibleView). DB-paged lists become script-iterable
// without any per-class override.
class ibValueModelPagedIteratorState : public ibValueIteratorState {
public:
	explicit ibValueModelPagedIteratorState(ibValueModel* model, int batchSize = 64)
		: m_model(model), m_batchSize(batchSize),
		  m_pos(0), m_started(false), m_exhausted(false) {}

	bool MoveNext(ibValue& current) override {
		if (!m_started) {
			m_started = true;
			m_model->GetFirstFetch(ibDataViewItem(), ibDataViewItem(),
				m_batchSize, m_batch);
			m_pos = 0;
			if (m_batch.size() < static_cast<size_t>(m_batchSize))
				m_exhausted = true;
		} else {
			++m_pos;
			if (m_pos >= m_batch.size()) {
				if (m_exhausted) return false;
				ibDataViewItem anchor = m_batch.empty()
					? ibDataViewItem()
					: m_batch[m_batch.size() - 1];
				ibDataViewItemArray nextBatch;
				m_model->GetNextFetch(ibDataViewItem(), anchor,
					m_batchSize, nextBatch);
				if (nextBatch.empty()) {
					m_exhausted = true;
					m_batch.Clear();
					return false;
				}
				if (nextBatch.size() < static_cast<size_t>(m_batchSize))
					m_exhausted = true;
				m_batch = std::move(nextBatch);
				m_pos = 0;
			}
		}
		if (m_pos >= m_batch.size()) return false;

		auto* line = m_model->GetRowAt(m_batch[m_pos]);
		if (line == nullptr) {
			current = ibValue();
		} else {
			current = ibValue(static_cast<ibValue*>(line));
		}
		return true;
	}

	void Reset() override {
		m_batch.Clear();
		m_pos = 0;
		m_started = false;
		m_exhausted = false;
	}

	bool PeekSample(ibValue& current) const override {
		current = m_model->GetEmptyRow();
		// Default GetEmptyRow returns ibValue() (TYPE_EMPTY).
		// Concrete model children override with a typed ReturnLine
		// wrapped as TYPE_REFFER.
		return current.m_typeClass != ibValueTypes::TYPE_EMPTY;
	}

private:
	ibValueModel* m_model;
	int m_batchSize;
	ibDataViewItemArray m_batch;
	size_t m_pos;
	bool m_started;
	bool m_exhausted;
};

} // namespace

// RAM-backed models have NO source primary key (RunComposerPage stamps an EMPTY row-key for them) → restore by
// index; a DB list / register has a PK → restore by key. Replaces the retired RamFetch flag, derived from source.
bool ibValueModel::HasKeyedRows() const
{
	const ibBackendQueryable* q = GetSourceQueryable();
	return q != nullptr && !q->GetPrimaryKeyColumns().empty();
}

// Grouped when the composer carries at least one grouping dimension (the display is then a tree). The control
// uses this to route a grouped model's row mutations through the re-fetch + selection-restore path.
bool ibValueModel::IsGroupedModel() const
{
	return GetModelComposer().GroupCount() > 0;
}

// Base default: no key. A RAM / in-memory model has no DB identity; the DB cursor + register subclasses override
// to build the row's reference (guid) or dimensions (composite) from the item.
ibUniqueKey ibValueModel::GetItemKey(const ibDataViewItem& /*item*/) const
{
	return ibUniqueKey();
}

// (GetSortArrows DELETED — the header arrow is set on the FRONT: OnColumnClick sets it on the clicked column,
//  and each tablebox column re-reads the composer's active sort on rebuild (OnUpdated), matching by its OWN
//  bound field name. No {col-id, asc} bridge on the model.)

std::shared_ptr<ibValueIteratorState> ibValueModel::CreateIterator()
{
	// Every ibValueModel is paged (fetch is uniform through RunComposerPage), so iteration drives the Get*Fetch
	// cursor — UNLESS the composer currently GROUPS, in which case it is shaped as a TREE (ANY list with grouping
	// is a tree — Max) and the flat iterator would walk only one level.
	if (GetModelComposer().GroupCount() == 0)
		return std::make_shared<ibValueModelPagedIteratorState>(this);
	return ibValue::CreateIterator();
}


void ibValueModel::RowValueStartEdit(const ibDataViewItem& item, unsigned int col)
{
	if (m_modelProvider == nullptr)
		return;
	m_modelProvider->StartEditing(item, col);
}

ibValueModel::ibActionCollection ibValueModel::GetActionCollection(const ibFormID& formType)
{
	ibActionCollection action(this);

	if (UseStandartCommand()) {
		action.AddAction(wxT("Add"), _("Add"), g_picAddCLSID, true, eAddValue);
		action.AddAction(wxT("Copy"), _("Copy"), g_picCopyCLSID, false, eCopyValue);
		action.AddAction(wxT("Edit"), _("Edit"), g_picEditCLSID, false, eEditValue);
		action.AddAction(wxT("Delete"), _("Delete"), g_picDeleteCLSID, false, eDeleteValue);
	}

	// The settings command GROUP appears when the table HAS settings (UseListSettings = the Filters capability),
	// NOT when a filter is already active — otherwise you could never open the dialog to ADD the first filter
	// (chicken-and-egg). The "Filter" button opens the List-Settings window (Filter / Sort / Group tabs).
	if (UseListSettings()) {
		if (UseStandartCommand()) action.AddSeparator();
		action.AddAction(wxT("Filter"), _("Filter"), g_picFilterCLSID, false, eFilter);
		action.AddAction(wxT("FilterByColumn"), _("Filter by column"), g_picFilterSetCLSID, false, eFilterByColumn);
		action.AddAction(wxT("FilterClear"), _("Filter clear"), g_picFilterClearCLSID, false, eFilterClear);
	}

	if (UseViewMode()) {
		if (UseStandartCommand() || UseListSettings()) action.AddSeparator();
		action.AddAction(wxT("ViewMode"), _("View mode"), g_picHierarchyCLSID, false, eViewMode);
	}

	return action;
}

void ibValueModel::ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eAddValue:
		AddValue();
		break;
	case eCopyValue:
		CopyValue();
		break;
	case eEditValue:
		EditValue();
		break;
	case eDeleteValue:
		DeleteValue();
		break;
	case eFilter:
		// The "Filter" button opens the List-Settings window (Filter / Sort / Group tabs). It edits this
		// model's GetListSettings() in place and RefetchAll()s on apply — the composer IS the fetch path
		// now. (eFilterByColumn / eFilterClear below write the SAME ListSettings->Filter directly.)
		ShowListSettings();
		break;
	case eFilterByColumn:
	{
		const ibDataViewItem& item = GetSelection();
		if (!item.IsOk())
			break;
		if (m_modelProvider != nullptr) {
			const unsigned int colId = m_modelProvider->GetCurrentModelColumn();
			ibValue retValue; GetValueByMetaID(item, colId, retValue);
			const wxString colName = GetColumnNameByID(colId);
			if (!colName.empty() && GetListSettings() != nullptr)
				GetListSettings()->GetFilter()->Add(colName, ibComparisonKind_Equal, retValue);
		}
		RefetchAll();
		break;
	}
	case eFilterClear:
		if (GetListSettings() != nullptr)
			GetListSettings()->GetFilter()->Clear();
		RefetchAll();
		break;
	case eViewMode:
		ShowViewMode();
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////

bool ibValueModel::ShowListSettings()
{
	if (m_modelProvider == nullptr)
		return false;
	// The settings window edits GetListSettings() (Filter / Order / Group) IN PLACE and returns
	// synchronously (modal). On a confirmed edit, RefetchAll() signals the control (Before/AfterReset)
	// to re-dispatch GetFirstFetch, so the table re-reads the L5 settings and visibly filters / sorts.
	// (This reset IS the whole trigger — change ListSettings → RefetchAll → new portion.)
	const bool applied = m_modelProvider->ShowListSettings(this);
	if (applied)
		RefetchAll();
	return applied;
}

bool ibValueModel::ShowViewMode()
{
	if (m_modelProvider == nullptr)
		return false;
	return m_modelProvider->ShowViewMode();
}

///////////////////////////////////////////////////////////////////////////////////////

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::ibValueModelColumnInfo() :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
	m_members.Bind(this, &ibValueModelColumnInfo::FillMembers);
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::~ibValueModelColumnInfo()
{
}

enum Prop {
	enColumnName,
	enColumnTypes,
	enColumnCaption,
	enColumnWidth
};

void ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Name"));
	helper.AppendProp(wxT("Types"));
	helper.AppendProp(wxT("Caption"));
	helper.AppendProp(wxT("Width"));
}

bool ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColumnName:
		pvarPropVal = GetColumnName();
		return true;
	case enColumnTypes:
		pvarPropVal = new ibValueTypeDescription(GetColumnType());
		return true;
	case enColumnCaption:
		pvarPropVal = GetColumnCaption();
		return true;
	case enColumnWidth:
		pvarPropVal = GetColumnWidth();
		return true;
	}

	return false;
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ibValueModel::ibValueModelColumnCollection::GetColumnByID(unsigned int col) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		ibValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (col == columnInfo->GetColumnID())
			return columnInfo;
	}

	return nullptr;
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ibValueModel::ibValueModelColumnCollection::GetColumnByName(const wxString& colName) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		ibValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (stringUtils::CompareString(colName, columnInfo->GetColumnName()))
			return columnInfo;
	}

	return nullptr;
}