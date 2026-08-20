////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value-model base (shared) + the composer node
////////////////////////////////////////////////////////////////////////////
//
// The SHARED half of the value-model: the ibValueModel base (settings / selection / actions / iteration /
// column collection) + the ibComposerNode out-of-line members. The two paged-fetch realisations live in their
// own translation units — the DB fetch in tabularModelDb.cpp (ibValueModelCursor::RunComposerPage), the in-place RAM
// fetch + composer in tabularModelRam.cpp (ibValueModelStorage::RunComposerPage / ibDataRamComposer).

#include "tabularModel.h"

#include "backend/session/session.h"            // ibSession — the caller a rented run borrows from
#include "backend/job/jobManager.h"             // ibJobManager / ibJobTenancy — the rented run a portion reads on
#include "backend/backend_exception.h"          // ibBackendException — a refusal to rent falls back to inline
#include "backend/tabularModelView.h"           // ibDataViewModel / ibDataViewItem — the base view types
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

// Out-of-line (declared in tabularModel.h): the ibValuePtr<ibValueListSettings> -> ibValueListSettings* conversion
// needs the COMPLETE ibValueListSettings type (listFilter.h, included here), not the header's forward decl.
//
// ⭐ BUILT ON FIRST ASK. The facade is handed the STORE it writes through — and the store is the
// SUBCLASS's composer, which does not exist while this base's constructor runs (GetModelComposer is
// pure virtual there). Built here instead, the model is whole by definition: nobody can ask a model
// for its settings before the model exists.
ibValueListSettings* ibValueModel::GetListSettings() const
{
	if (m_listSettings == nullptr)
		m_listSettings = new ibValueListSettings(GetModelComposer(), [this] { NotifyReset(); });

	return m_listSettings;
}


ibValueModel::ibValueModel()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
	m_modelProvider(nullptr)
{
	m_modelProvider = new ibDataViewModelProviderImpl(this);

	// (The model's RUNTIME/script settings — a FACADE over the composer, so a script's
	//  list.Settings.Filter.Add(...) writes the store IMMEDIATELY and fires this model's refresh — are
	//  built on the first ASK, in GetListSettings above: they take the composer, and the composer is
	//  the SUBCLASS's, so it does not exist yet here. The settings DIALOG uses its own separate BUFFER
	//  copy (load on open, commit on OK) so the form stays transactional.)

	// (m_composer is POLYMORPHIC + lazily created, by which time the full subclass exists so
	// CreateComposer picks ibDataDBComposer (DB) / ibDataRamComposer (RAM).
	// Subclasses set their source + default sort/grouping STRAIGHT on it in their ctor —
	// GetModelComposer().FromSource(q).Sort(...) — the persistent settings store the fetch reads.)
}

ibValueModel::~ibValueModel()
{
	// A READ MUST NOT OUTLIVE WHAT IT IS READING. The rented run walks this model
	// from a worker, and nothing else keeps the model alive: the control's alive
	// token answers for the CONTROL, and dropping a form (or AssociateModel) frees
	// the model outright. So the last read is waited out here — the same promise
	// the base door keeps by joining its thread.
	//
	// Cheap in the ordinary case: by the time a form closes its portion has long
	// since landed, and a run that has finished returns from Wait immediately.
	CancelFetch();

	// The RAM node storage (ibRamValueStorage) is a member of ibValueModelStorage — its dtor DecRefs the nodes; the
	// base just drops the view provider. (A DB model has no node storage.)
	m_modelProvider->DecRef();
}

// ⭐ SAY STOP OUT LOUD. The destructor has always done this, but a form CLOSING is not the same moment
// as the model dying: a report still composing holds references of its own, so the window goes and the
// read carries on against a session nobody is watching. A control that starts a read is the one that
// tells it to stop when its window is going away (Max, 2026-08-19: "it has to understand it must break
// off, forcibly").
//
// Cooperative and blocking, in that order: raise the cancel flag, then wait the run out — a read that
// is already inside a query finishes that query, and returning before it did would let the worker walk
// a model the caller is about to release.
void ibValueModel::CancelFetch()
{
	if (!m_fetchRun)
		return;
	m_fetchRun->Cancel();   // cooperative — a query already in flight still finishes
	m_fetchRun->Wait();
	m_fetchRun.reset();
}

// (Header-click sort lives on the FRONT — ibValueModelTableBox::OnColumnClick pokes this model's composer
//  directly (ClearSorts + Sort by the column's own bound field name) + RefetchAll. No SortBy on the model.)

// (GetSortModels DELETED — ibSortModel is gone. The sort is L5 (the composer, by field NAME); the few
//  consumers that needed a {col-id, asc} pair — the keyset anchor + the frontend header arrows — now read
//  m_composer.GetSortAt + GetColumnIDByName inline, each at the point of use. Max: "ibSortModel is either part
//  of L5, or removed entirely" — L5 is name-based, so it was removed.)

void ibValueModel::SubmitFetchAsync(std::function<void()> work)
{
	if (!work)
		return;

	// Under the door's own lock — one portion at a time. The lock lives on the view
	// side of this model (its provider bridge IS the ibDataViewModel), which is the
	// one object both halves of a table share.
	auto locked = GetDataViewModel()->GuardFetch(std::move(work));

	// The rented run — see the header for what is borrowed and what is not. The
	// try/catch IS the fallback: StartBackground refuses out loud (no session to
	// rent from, no free connection within the tenant's short wait, the manager
	// stopping), and a refusal here means "read it here instead", never "no data".
	if (ibJobManager* const jobs = ibApplicationData::GetJobManager()) {
		try {
			// The handle is KEPT, not dropped: it is what the destructor waits on so
			// a read cannot outlive this model. One at a time by construction — the
			// door's lock already serialises them — so one slot is enough.
			m_fetchRun = jobs->StartBackground(
				[locked](ibSession*) -> ibValue { locked(); return ibValue(); },
				_("reading table data"),
				ibJobTenancy::Tenant);
			return;
		}
		catch (const ibBackendException&) {
			// Nothing to rent — fall through.
		}
	}

	locked();
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

#include "backend/picturePredefined.h"   // g_pic*CLSID — the standard command icons

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