////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"        // universal (half-)L3 read entry
#include "backend/composition/listFetchDriver.h"   // L5 — the fetch driver (envelope in, rows out)

// (The build-once page-cache signature helpers moved INTO the L5 composer —
// dataComposer.cpp signs its rendered text + params + page shape itself.)

/////////////////////////////////////////////////////////////////////////////////////////////////
// Single-batch Fetch for enums — CASE/WHEN parent-position order has no
// stable cursor, so we always return everything in one go (hasMore=false).
/////////////////////////////////////////////////////////////////////////////////////////////////

// Common tail of every universal-API GetXxxFetch: hand row pointers off
// to the caller's ibDataViewItemArray via the Adopt-style refcount
// transfer and return the count.  Saves a 3-line copypaste at every
// callsite (7 of them across this file) without obscuring control flow.
template <class TRow>
static unsigned int AdoptAndCount(std::vector<TRow*>& rows, ibDataViewItemArray& out)
{
	const unsigned int n = static_cast<unsigned int>(rows.size());
	ibValueModel::AdoptRowsToItems(rows, out);
	return n;
}

// L5 — translate the view-layer filter / sort rows into composer settings (the
// user vocabulary; attribute NAMES pulled off the metadata, values travel as
// auto-&parameters). Re-applied per fetch — the user mutates them between
// scroll ticks.
template <class TMeta>
static void ApplyListFilters(ibDataComposer& composer, const TMeta* meta, const ibFilterRow& filters)
{
	for (const auto& f : filters.m_filters) {
		if (!f.m_filterUse)
			continue;
		const auto* attr = meta->FindAnyAttributeObjectByFilter(f.m_filterModel);
		if (attr == nullptr)
			continue;
		composer.Filter(attr->GetName(),
			f.m_filterComparison == ibComparisonType_NotEqual ? wxT("<>") : wxT("="),
			f.m_filterValue);
	}
}

// Record (catalog / enum / folder) variant — reference sorts are skipped exactly
// as on the door path: the uuid identity tail already orders by the row's own
// reference.
template <class TMeta>
static void ApplyListSettings(ibDataComposer& composer, const TMeta* meta,
                              const ibFilterRow& filters, const ibSortOrder* sorts)
{
	composer.ClearSettings();
	ApplyListFilters(composer, meta, filters);
	if (sorts == nullptr)
		return;
	for (const auto& s : sorts->m_sorts) {
		if (!s.m_sortEnable)
			continue;
		if (meta->IsDataReference(s.m_sortModel))
			continue;
		const auto* attr = meta->FindAnyAttributeObjectByFilter(s.m_sortModel);
		if (attr == nullptr)
			continue;
		composer.Sort(attr->GetName(), s.m_sortAscending);
	}
}

// Register variant — no row reference exists, so every enabled sort renders;
// the door appends the register's identity tail (recorder+line / period?+dims)
// to the rendered ORDER BY, one source of truth (EffectiveSort).
template <class TMeta>
static void ApplyRegisterSettings(ibDataComposer& composer, const TMeta* meta,
                                  const ibFilterRow& filters, const ibSortOrder& sorts)
{
	composer.ClearSettings();
	ApplyListFilters(composer, meta, filters);
	for (const auto& s : sorts.m_sorts) {
		if (!s.m_sortEnable)
			continue;
		const auto* attr = meta->FindAnyAttributeObjectByFilter(s.m_sortModel);
		if (attr == nullptr)
			continue;
		composer.Sort(attr->GetName(), s.m_sortAscending);
	}
}

ibFetchResponse<ibGuid, ibValueListDataObjectEnumRef::ibValueTableEnumRow>
ibValueListDataObjectEnumRef::Fetch(const ibFetchRequest<ibGuid>& req) const
{
	ibFetchResponse<ibGuid, ibValueTableEnumRow> resp;

	auto db = ses_query;
	if (db == nullptr || !db->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	const int fetchN = req.m_count > 0 ? req.m_count + 1 : 1024;

	ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
	ibValueMetaObjectAttributePredefined* metaOrder     = m_metaObject->GetDataOrder();

	// L5 — the composer's source was wired in the ctor; the settings are
	// re-applied per fetch. The projection is the row's own reference only (it
	// carries the guid key); the enum order is a METADATA concern (parent
	// position), not a DB column, so the read is unordered and the RAM sort
	// below stays.
	ApplyListSettings(m_composer, m_metaObject, m_filterRow, nullptr);
	m_composer.Select(metaReference->GetName());

	ibReadPageRequest page;
	page.m_count = fetchN;

	ibListFetchDriver fetchDriver(page);
	m_composer.Run(fetchDriver);

	const ibMetaID refID = metaReference->GetMetaID();
	for (ibListFetchDriver::Row& src : fetchDriver.Rows()) {
		ibValue refVal = src.GetValue(refID);
		auto* refObj = dynamic_cast<ibValueReferenceDataObject*>(refVal.GetRef());
		if (refObj == nullptr)
			continue;
		const ibGuid enumGuid = refObj->GetGuid();
		ibValueTableEnumRow* row = new ibValueTableEnumRow(enumGuid);
		row->AppendTableValue(refID, refVal);
		row->AppendTableValue(
			metaOrder->GetMetaID(),
			m_metaObject->FindEnumObjectByFilter(enumGuid)->GetParentPosition());
		resp.m_rows.push_back(row);
	}

	// Order by parent position (metadata) — only if a reference sort is enabled;
	// its direction picks ASC/DESC. Was an SQL CASE/WHEN built from the same
	// positions; a RAM sort over this single batch is the natural form.
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable || !m_metaObject->IsDataReference(s.m_sortModel)) continue;
		const bool asc = s.m_sortAscending;
		std::stable_sort(resp.m_rows.begin(), resp.m_rows.end(),
			[&](ibValueTableEnumRow* a, ibValueTableEnumRow* b) {
				const auto pa = m_metaObject->FindEnumObjectByFilter(a->GetGuid())->GetParentPosition();
				const auto pb = m_metaObject->FindEnumObjectByFilter(b->GetGuid())->GetParentPosition();
				return asc ? (pa < pb) : (pa > pb);
			});
		break;
	}

	// hasMore probe — if we got > count rows, drop the extra.
	if (req.m_count > 0 && static_cast<int>(resp.m_rows.size()) > req.m_count) {
		resp.m_hasMore = true;
		delete resp.m_rows.back();
		resp.m_rows.pop_back();
	}
	
	return resp;
}

unsigned int ibValueListDataObjectEnumRef::GetFirstFetch(
	const ibDataViewItem& parent, const ibDataViewItem& /*anchor*/,
	int /*count*/, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	// Enum is read-only and fixed by the designer — runtime never
	// adds / removes entries.  Single-batch fetch returns the entire
	// list regardless of the caller's batch size: GetNextFetch /
	// GetPrevFetch are stubs (no stable cursor on the CASE/WHEN order),
	// so paginating the bootstrap with a viewport-sized batch would
	// leave entries past the first viewport unreachable.  Use the
	// metadata's enum-object count as the exact request size; Fetch's
	// `+ 1` probe row keeps hasMore = false naturally.
	ibFetchRequest<ibGuid> req;
	req.m_anchor.m_empty = true;
	req.m_direction      = ibFetchDirection::Reset;
	const int total      = static_cast<int>(m_metaObject->GetEnumObjectArray().size());
	req.m_count          = total > 0 ? total : defaultCountPerPage;
	auto resp = Fetch(req);
	return AdoptAndCount(resp.m_rows, out);
}

unsigned int ibValueListDataObjectEnumRef::GetNextFetch(
	const ibDataViewItem& /*parent*/, const ibDataViewItem& /*anchor*/,
	int /*count*/, ibDataViewItemArray& /*out*/) const
{
	// Enum is a single-batch list — CASE/WHEN parent-position order
	// has no stable cursor, so we don't paginate forward.
	return 0;
}

unsigned int ibValueListDataObjectEnumRef::GetPrevFetch(
	const ibDataViewItem& /*parent*/, const ibDataViewItem& /*anchor*/,
	int /*count*/, ibDataViewItemArray& /*out*/) const
{
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////////////////////////////////////////
// Cursor-paginated Fetch — single SQL point used by ibTableViewBuffer.
// Replaces the RefreshModel + RefreshItemModel pair once IsPaged() is on.
/////////////////////////////////////////////////////////////////////////////////////////////////

ibFetchResponse<ibGuid, ibValueListDataObjectRef::ibValueTableListRow>
ibValueListDataObjectRef::Fetch(const ibFetchRequest<ibGuid>& req) const
{
	ibFetchResponse<ibGuid, ibValueTableListRow> resp;

	auto db = ses_query;
	if (db == nullptr || !db->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	const int fetchN = req.m_count + 1;   // +1 probe row → hasMore.

	const bool hasAnchor = !req.m_anchor.m_empty;
	const bool reverse   = (req.m_direction == ibFetchDirection::Backward);

	// L5 — the composer's source was wired in the ctor; user filters / sorts are
	// re-applied per fetch in the user vocabulary and RENDER into the query text.
	// The build-once page cache (Lever 1) now lives INSIDE the composer: the
	// signature is the rendered text + the page shape + the parameter values, and
	// a scroll tick re-renders the same text — no re-parse, the anchor rebinds.
	ApplyListSettings(m_composer, m_metaObject, m_filterRow, &m_sortOrder);

	ibReadPageRequest page;
	page.m_direction        = req.m_direction;
	page.m_hasAnchor        = hasAnchor;
	page.m_anchorSortValues = req.m_anchor.m_sortValues;
	page.m_anchorGuid       = req.m_anchor.m_empty ? wxString() : wxString(req.m_anchor.m_key);
	page.m_count            = fetchN;
	page.m_reverseSort      = reverse;

	const std::vector<ibValueMetaObjectAttributeBase*>& vecAttr =
		m_metaObject->GetGenericAttributeArrayObject();
	ibValueMetaObjectAttributePredefined* metaReference =
		m_metaObject->GetDataReference();

	ibListFetchDriver fetchDriver(page);
	m_composer.Run(fetchDriver);

	const ibMetaID refID = metaReference->GetMetaID();
	for (ibListFetchDriver::Row& src : fetchDriver.Rows()) {
		ibValue refVal = src.GetValue(refID);
		auto* refObj = dynamic_cast<ibValueReferenceDataObject*>(refVal.GetRef());
		if (refObj == nullptr)
			continue;
		ibValueTableListRow* row = new ibValueTableListRow(refObj->GetGuid());
		for (auto& attr : vecAttr) {
			if (m_metaObject->IsDataReference(attr->GetMetaID()))
				continue;
			row->AppendTableValue(attr->GetMetaID(), src.GetValue(attr->GetMetaID()));
		}
		// The reference column rides the same map — it carried the key above.
		row->AppendTableValue(refID, refVal);
		resp.m_rows.push_back(row);
	}

	// 4. hasMore via the +1 probe row.
	if (static_cast<int>(resp.m_rows.size()) > req.m_count) {
		resp.m_hasMore = true;
		delete resp.m_rows.back();
		resp.m_rows.pop_back();
	}

	

	return resp;
}

namespace {
ibFetchAnchor<ibGuid> BuildRefAnchor(
	const ibValueMetaObjectRecordDataMutableRef* meta,
	const ibSortOrder& sort,
	const ibValueListDataObjectRef::ibValueTableListRow* row)
{
	ibFetchAnchor<ibGuid> a;
	a.m_empty = false;
	a.m_key   = row->GetGuid();
	for (const auto& s : sort.m_sorts) {
		if (!s.m_sortEnable) continue;
		if (meta->IsDataReference(s.m_sortModel)) continue;
		// Safe lookup: stub rows (built by FindRowValue for post-Save
		// focus restore) carry only m_objGuid — m_nodeValues is empty.
		// GetTableValue's `m_nodeValues.at(id)` would throw
		// std::out_of_range for every sort column on a stub; GetValue
		// catches that and leaves `v` as the default empty ibValue,
		// which serialises as NULL in the cursor predicate and matches
		// from the start of the ordering (acceptable fallback — the
		// real selection-restore happens in IsEqualTo against the
		// fresh batch by m_objGuid, not by sort tuple).
		ibValue v;
		row->GetValue(s.m_sortModel, v);
		a.m_sortValues.push_back(v);
	}
	return a;
}
}

unsigned int ibValueListDataObjectRef::GetFirstFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	auto* row = anchor.IsOk()
		? static_cast<ibValueTableListRow*>(anchor.GetID()) : nullptr;
	ibFetchRequest<ibGuid> req;
	if (row != nullptr) {
		// Restoration fetch — backend positions the window so the
		// anchor row sits AT the top (Reset = forward + inclusive
		// tiebreak, so the anchor itself is in items[0]).  This lets
		// the control find the saved selection in the new ordering by
		// data-compare and re-apply focus + selection.
		req.m_anchor    = BuildRefAnchor(m_metaObject, m_sortOrder, row);
		req.m_direction = ibFetchDirection::Reset;
	} else {
		req.m_anchor.m_empty = true;
		req.m_direction      = ibFetchDirection::Reset;
	}
	req.m_count = count > 0 ? count : defaultCountPerPage;
	auto resp = Fetch(req);
	return AdoptAndCount(resp.m_rows, out);
}

unsigned int ibValueListDataObjectRef::GetNextFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	auto* row = anchor.IsOk() ? static_cast<ibValueTableListRow*>(anchor.GetID()) : nullptr;
	if (row == nullptr) return GetFirstFetch(parent, ibDataViewItem(), count, out);
	ibFetchRequest<ibGuid> req;
	req.m_anchor    = BuildRefAnchor(m_metaObject, m_sortOrder, row);
	req.m_direction = ibFetchDirection::Forward;
	req.m_count     = count > 0 ? count : defaultCountPerPage;
	auto resp = Fetch(req);
	return AdoptAndCount(resp.m_rows, out);
}

unsigned int ibValueListDataObjectRef::GetPrevFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	auto* row = anchor.IsOk() ? static_cast<ibValueTableListRow*>(anchor.GetID()) : nullptr;
	if (row == nullptr) return 0;
	ibFetchRequest<ibGuid> req;
	req.m_anchor    = BuildRefAnchor(m_metaObject, m_sortOrder, row);
	req.m_direction = ibFetchDirection::Backward;
	req.m_count     = count > 0 ? count : defaultCountPerPage;
	auto resp = Fetch(req);
	// Backward fetch returns rows in reverse order; reverse them
	// in memory so the caller gets natural ascending sequence.
	std::reverse(resp.m_rows.begin(), resp.m_rows.end());
	return AdoptAndCount(resp.m_rows, out);
}

/////////////////////////////////////////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////////////////////////////////////////
// Cursor-paginated Fetch for registers.  Effective ORDER BY is
// `[user sorts] ++ [identity tail]` (recorder+line for HasRecorder,
// period?+dimensions otherwise); identity tail guarantees a total
// order even when the user disables every column-sort, so the
// anchor predicate has stable forward / backward cursoring.
/////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<ibQuerySortItem> ibValueListRegisterObject::EffectiveSortOrder() const
{
	// Enabled user sort, query-native.
	std::vector<ibQuerySortItem> userSorts;
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		const ibValueMetaObjectAttributeBase* attr =
			m_metaObject->FindAnyAttributeObjectByFilter(s.m_sortModel);
		if (attr == nullptr) continue;
		userSorts.push_back({ attr, s.m_sortAscending });
	}
	// The register's identity tail is appended by the door — one source of truth.
	return ibDataQueryBuilder::EffectiveSort(m_metaObject->GetQueryable(), userSorts);
}

ibFetchResponse<ibUniqueKeyPair, ibValueListRegisterObject::ibValueTableKeyRow>
ibValueListRegisterObject::Fetch(const ibFetchRequest<ibUniqueKeyPair>& req) const
{
	ibFetchResponse<ibUniqueKeyPair, ibValueTableKeyRow> resp;

	const int fetchN = req.m_count > 0 ? req.m_count + 1 : 8192;

	// L5 — the register is composed through the SAME rendered text path as the
	// catalogs. The door appends the register's identity tail (recorder+line /
	// period?+dimensions) to the rendered ORDER BY via GetIdentitySort, so the
	// keyset has a total order; the anchor rides in the ENVELOPE and the
	// composer's internal page cache keeps the SQL build-once across ticks.
	const bool hasAnchor = !req.m_anchor.m_empty;
	const bool reverse   = (req.m_direction == ibFetchDirection::Backward);

	ApplyRegisterSettings(m_composer, m_metaObject, m_filterRow, m_sortOrder);

	ibReadPageRequest page;
	page.m_direction        = req.m_direction;
	page.m_hasAnchor        = hasAnchor;
	page.m_anchorSortValues = req.m_anchor.m_sortValues;   // built over EffectiveSortOrder
	page.m_count            = fetchN;
	page.m_reverseSort      = reverse;
	// No m_anchorGuid: a register has no single row-key (identity is composite,
	// carried by the sort columns themselves).

	// Row shape: identity columns (recorder+line / dimensions) are the node key;
	// the full column set (predefined + dimensions + resources + attributes) goes
	// into the table values, exactly as the SQL path materialised it.
	const std::vector<ibValueMetaObjectAttributeBase*>& vecAttr =
		m_metaObject->GetGenericAttributeArrayObject();
	const std::vector<ibValueMetaObjectAttributeBase*>& vecDim =
		m_metaObject->GetGenericDimensionArrayObject();

	ibListFetchDriver fetchDriver(page);
	m_composer.Run(fetchDriver);

	for (ibListFetchDriver::Row& src : fetchDriver.Rows()) {
		ibValueTableKeyRow* row = new ibValueTableKeyRow;
		if (m_metaObject->HasRecorder()) {
			ibValueMetaObjectAttributePredefined* attrRecorder = m_metaObject->GetRegisterRecorder();
			ibValueMetaObjectAttributePredefined* attrLine     = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attrRecorder && attrLine);
			row->AppendNodeValue(attrRecorder->GetMetaID(), src.GetValue(attrRecorder->GetMetaID()));
			row->AppendNodeValue(attrLine->GetMetaID(),     src.GetValue(attrLine->GetMetaID()));
		}
		else {
			for (auto& dim : vecDim)
				row->AppendNodeValue(dim->GetMetaID(), src.GetValue(dim->GetMetaID()));
		}
		for (auto& attr : vecAttr)
			row->AppendTableValue(attr->GetMetaID(), src.GetValue(attr->GetMetaID()));
		resp.m_rows.push_back(row);
	}

	// hasMore via the +1 probe row.
	if (req.m_count > 0 && static_cast<int>(resp.m_rows.size()) > req.m_count) {
		resp.m_hasMore = true;
		delete resp.m_rows.back();
		resp.m_rows.pop_back();
	}

	return resp;
}

namespace {
ibFetchAnchor<ibUniqueKeyPair> BuildRegisterAnchor(
	const ibValueMetaObjectRegisterData* meta,
	const std::vector<ibQuerySortItem>& effective,
	const ibValueListRegisterObject::ibValueTableKeyRow* row)
{
	ibFetchAnchor<ibUniqueKeyPair> a;
	a.m_empty = false;
	a.m_key   = row->GetUniquePairKey(meta);
	a.m_sortValues.reserve(effective.size());
	for (const auto& c : effective) {
		// Safe lookup: stub rows (built by FindRowValue for post-Save focus
		// restore) carry only m_nodeKeys (identity columns) — m_nodeValues is
		// empty. GetValue leaves `v` as the default empty ibValue (binds as NULL
		// in the cursor predicate). IsEqualTo on the fresh batch matches by
		// m_nodeKeys, so missing sort values don't affect identity restoration.
		ibValue v;
		if (c.m_col != nullptr)
			row->GetValue(c.m_col->GetColumnId(), v);   // column self-describes its metaID — no ResolveAttribute
		a.m_sortValues.push_back(v);
	}
	return a;
}
}

unsigned int ibValueListRegisterObject::GetFirstFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	auto* row = anchor.IsOk()
		? static_cast<ibValueTableKeyRow*>(anchor.GetID()) : nullptr;
	ibFetchRequest<ibUniqueKeyPair> req;
	if (row != nullptr) {
		// Restoration fetch — backend positions the window so the
		// anchor row sits AT the top (Reset = forward + inclusive
		// tiebreak, so the anchor itself is in items[0]).
		req.m_anchor    = BuildRegisterAnchor(m_metaObject, EffectiveSortOrder(), row);
		req.m_direction = ibFetchDirection::Reset;
	}
	else {
		req.m_anchor.m_empty = true;
		req.m_direction      = ibFetchDirection::Reset;
	}
	req.m_count = count > 0 ? count : defaultCountPerPage;
	auto resp = Fetch(req);
	return AdoptAndCount(resp.m_rows, out);
}

unsigned int ibValueListRegisterObject::GetNextFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	auto* row = anchor.IsOk()
		? static_cast<ibValueTableKeyRow*>(anchor.GetID()) : nullptr;
	if (row == nullptr) return GetFirstFetch(parent, ibDataViewItem(), count, out);
	ibFetchRequest<ibUniqueKeyPair> req;
	req.m_anchor    = BuildRegisterAnchor(m_metaObject, EffectiveSortOrder(), row);
	req.m_direction = ibFetchDirection::Forward;
	req.m_count     = count > 0 ? count : defaultCountPerPage;
	auto resp = Fetch(req);
	return AdoptAndCount(resp.m_rows, out);
}

unsigned int ibValueListRegisterObject::GetPrevFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (parent.IsOk()) return 0;
	auto* row = anchor.IsOk()
		? static_cast<ibValueTableKeyRow*>(anchor.GetID()) : nullptr;
	if (row == nullptr) return 0;
	ibFetchRequest<ibUniqueKeyPair> req;
	req.m_anchor    = BuildRegisterAnchor(m_metaObject, EffectiveSortOrder(), row);
	req.m_direction = ibFetchDirection::Backward;
	req.m_count     = count > 0 ? count : defaultCountPerPage;
	auto resp = Fetch(req);
	// Backward fetch returns rows in reverse order; reverse them
	// in memory so the caller gets natural ascending sequence.
	std::reverse(resp.m_rows.begin(), resp.m_rows.end());
	return AdoptAndCount(resp.m_rows, out);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<ibGuid>
ibValueModelTreeDataObjectFolderRef::GetAncestorChain(const ibGuid& fromGuid) const
{
	if (m_chainCachedFor == fromGuid && !m_chainCache.empty()) {
		return m_chainCache;
	}
	std::vector<ibGuid> chain;
	if (!fromGuid.isValid()) {
		m_chainCachedFor = fromGuid;
		m_chainCache.clear();
		return chain;
	}

	auto db = ses_query;
	if (db == nullptr || !db->IsOpen()) return chain;

	auto* metaParent = m_metaObject->GetDataParent();
	wxASSERT(metaParent);

	ibGuid current = fromGuid;
	chain.push_back(current);

	// Cap depth — defends against cycles in malformed data.
	for (int depth = 0; depth < 100; ++depth) {
		// Look the row up by its key through the L3 door, read its parent ref.
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel =
			ibDataQueryBuilder().From(m_metaObject->GetQueryable()).WhereKey(current).Execute(page);

		ibGuid parentGuid;
		if (sel.Next()) {
			ibValue parentValue = sel.GetValue(metaParent);
			ibValueReferenceDataObject* refData = nullptr;
			if (parentValue.ConvertToValue(refData) && refData != nullptr)
				parentGuid = refData->GetGuid();
		}

		if (!parentGuid.isValid()) break;   // reached root
		// Cycle guard: parent already in chain.
		if (std::find(chain.begin(), chain.end(), parentGuid) != chain.end()) break;

		chain.push_back(parentGuid);
		current = parentGuid;
	}

	
	m_chainCachedFor = fromGuid;
	m_chainCache     = chain;
	return chain;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// Paged tree fetch — unified Next/Prev API.  Single source of truth for
// any GUI-driven incremental load: GUI caches last/first row's guid as
// anchor, calls NextFetch / PrevFetch with the parent scope.  Model
// stays stateless; each call is independent given args.
/////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

// Build a row from the current L3 selection row, fully materialised with
// attributes + reference. Used by both NextFetch and PrevFetch. Reads ONLY
// through the L3 selection (GetGuidString / GetValue) — no raw result set, so
// the L3 surface never leaks the L2 cursor. The data-reference attribute
// materialises to the row's own reference object via GetValue, same as every
// other attribute. Returns the nested ibValueTreeListNode under FolderRef —
// qualified fully because we live in an anonymous namespace at file scope.
static ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode*
BuildTreeRowFromSelection(
	const ibDataQueryResult& sel,
	const ibValueModelTreeDataObjectFolderRef* owner,
	const ibValueMetaObjectRecordDataHierarchyMutableRef* meta)
{
	auto* metaIsFolder    = meta->GetDataIsFolder();
	auto* metaReference   = meta->GetDataReference();
	const auto& vec_attr  = meta->GetGenericAttributeArrayObject();

	const ibValue isFolderVal = sel.GetValue(metaIsFolder);
	const ibBackendQueryColumn* keyCol = meta->GetQueryable()->GetIdentitySort().back().m_col;   // uuid identity column

	auto* row = new ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode(
		nullptr,
		sel.GetValue(keyCol).GetString(),
		const_cast<ibValueModelTreeDataObjectFolderRef*>(owner),
		isFolderVal.GetBoolean());
	for (auto& attribute : vec_attr) {
		if (meta->IsDataReference(attribute->GetMetaID())) continue;
		row->AppendTableValue(attribute->GetMetaID()) = sel.GetValue(attribute);
	}
	row->AppendTableValue(metaReference->GetMetaID(), sel.GetValue(metaReference));
	return row;
}

// L5 — the composer-path twin of BuildTreeRowFromSelection: the same node off a
// fetch-driver row (values by attribute metaID). The row guid comes from the
// data-reference column's VALUE (the uuid raw column is outside the query
// language). Null when the reference cell did not materialise.
static ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode*
BuildTreeRowFromComposed(
	const ibListFetchDriver::Row& src,
	const ibValueModelTreeDataObjectFolderRef* owner,
	const ibValueMetaObjectRecordDataHierarchyMutableRef* meta)
{
	auto* metaIsFolder  = meta->GetDataIsFolder();
	auto* metaReference = meta->GetDataReference();

	ibValue refVal = src.GetValue(metaReference->GetMetaID());
	auto* refObj = dynamic_cast<ibValueReferenceDataObject*>(refVal.GetRef());
	if (refObj == nullptr)
		return nullptr;

	auto* row = new ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode(
		nullptr,
		refObj->GetGuid(),
		const_cast<ibValueModelTreeDataObjectFolderRef*>(owner),
		src.GetValue(metaIsFolder->GetMetaID()).GetBoolean());
	for (auto& attribute : meta->GetGenericAttributeArrayObject()) {
		if (meta->IsDataReference(attribute->GetMetaID())) continue;
		row->AppendTableValue(attribute->GetMetaID()) = src.GetValue(attribute->GetMetaID());
	}
	row->AppendTableValue(metaReference->GetMetaID(), refVal);
	return row;
}

}  // namespace

std::vector<ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode*>
ibValueModelTreeDataObjectFolderRef::LoadRowsByGuids(const std::vector<ibGuid>& guids) const
{
	std::vector<ibValueTreeListNode*> rows;
	if (guids.empty()) return rows;
	auto db = ses_query;
	if (db == nullptr || !db->IsOpen()) return rows;

	// One SELECT with guidName IN (...) through the L3 door (rendered as
	// OR-of-equals). No ORDER BY — we reorder rows to match `guids` input order
	// below (caller depends on it for breadcrumb sequencing).
	ibReadPageRequest page;
	page.m_count = static_cast<int>(guids.size());
	ibDataQueryResult sel =
		ibDataQueryBuilder().From(m_metaObject->GetQueryable()).WhereKeyIn(guids).Execute(page);

	// Build a guid → row* map; reorder to match input.
	std::unordered_map<wxString, ibValueTreeListNode*> byGuid;
	while (sel.Next()) {
		ibValueTreeListNode* row = BuildTreeRowFromSelection(sel, this, m_metaObject);
		if (row != nullptr)
			byGuid.emplace(wxString(row->GetGuid()), row);
	}

	rows.reserve(guids.size());
	for (const auto& g : guids) {
		auto it = byGuid.find(wxString(g));
		if (it != byGuid.end()) rows.push_back(it->second);
	}
	return rows;
}

void ibValueModelTreeDataObjectFolderRef::BuildAncestorBreadcrumb(
	const ibDataViewItem& fromRow, ibDataViewItemArray& out) const
{
	out.Clear();
	if (!fromRow.IsOk()) return;
	auto* node = ibValueModel::GetViewData<ibValueTreeListNode>(fromRow);
	if (node == nullptr) return;

	// Chain returned by GetAncestorChain is [self, parent, …, top].
	// We want [parent, …, top] — drop the row itself; the caller wants
	// crumbs above the selected row, not including it.
	std::vector<ibGuid> chain = GetAncestorChain(node->GetGuid());
	if (chain.size() <= 1) return;
	chain.erase(chain.begin());

	std::vector<ibValueTreeListNode*> rows = LoadRowsByGuids(chain);

	// Adopt-style refcount transfer: the row created by LoadRowsByGuids
	// has refcount=1; ibDataViewItem ctor IncRef's to 2; DecRef brings
	// it back to 1, owned by the item alone.
	for (auto* r : rows) {
		out.Add(ibDataViewItem(r));
		r->DecRef();
	}
}

ibValueModelTreeDataObjectFolderRef::ibTreeFetchResponse
ibValueModelTreeDataObjectFolderRef::GetFirstFetch(const ibTreeFetchArgs& args) const
{
	// "First page" can mean two things:
	//   a) cold open — no anchor, just fetch top-N from start;
	//   b) restoration fetch (paged Refresh / sort change) — anchor IS
	//      the row the control wants in items[0] so the viewport stays
	//      at the same business row.
	// (a) reuses GetNextFetch with empty anchor (no cursor clause).
	// (b) needs INCLUSIVE cursor (`>=` instead of `>`); GetNextFetch is
	// strict (`>`) so we run a dedicated fetch path with Reset
	// direction so anchor itself ends up in items[0].
	if (!args.m_viewportAnchor.IsOk())
		return GetNextFetch(args);
	return FetchWithDirection(args, ibFetchDirection::Reset);
}

ibValueModelTreeDataObjectFolderRef::ibTreeFetchResponse
ibValueModelTreeDataObjectFolderRef::GetNextFetch(const ibTreeFetchArgs& args) const
{
	return FetchWithDirection(args, ibFetchDirection::Forward);
}

ibValueModelTreeDataObjectFolderRef::ibTreeFetchResponse
ibValueModelTreeDataObjectFolderRef::FetchWithDirection(
	const ibTreeFetchArgs& args, ibFetchDirection direction) const
{
	// ONE body for First / Next / Prev — the direction is just envelope state
	// (Backward = inverse scan + the buffer reverses at the end); the query
	// decides everything else itself.
	ibTreeFetchResponse resp;
	auto db = ses_query;
	if (db == nullptr || !db->IsOpen()) return resp;

	// Decode opaque ibDataViewItem → ibGuid + sort col values.  The
	// composite cursor needs the anchor row's value at every enabled
	// non-reference sort col; we read them off the live row before the
	// fetch so the SQL predicate can compare apples to apples.
	auto extractGuid = [this](const ibDataViewItem& item) -> ibGuid {
		if (!item.IsOk()) return ibGuid();
		auto* node = GetViewData<ibValueTreeListNode>(item);
		return node ? node->GetGuid() : ibGuid();
	};
	auto extractSortValues = [this](const ibDataViewItem& item) -> std::vector<ibValue> {
		std::vector<ibValue> out;
		auto* node = GetViewData<ibValueTreeListNode>(item);
		if (node == nullptr) return out;
		for (const auto& s : m_sortOrder.m_sorts) {
			if (!s.m_sortEnable) continue;
			if (m_metaObject->IsDataReference(s.m_sortModel)) continue;
			// Safe lookup: stub anchors may carry empty m_nodeValues;
			// GetTableValue would throw out_of_range, GetValue
			// gracefully leaves `v` empty.
			ibValue v;
			node->GetValue(s.m_sortModel, v);
			out.push_back(v);
		}
		return out;
	};
	const ibGuid parentGuid = extractGuid(args.m_parent);
	const ibGuid anchorGuid = extractGuid(args.m_viewportAnchor);

	const bool backward  = (direction == ibFetchDirection::Backward);
	const bool hasAnchor = anchorGuid.isValid();
	const int  fetchN    = args.m_count > 0 ? args.m_count + 1 : 21;

	// L5 — the composer's source was wired in the ctor; user filters / sorts are
	// re-applied per fetch and RENDER into the query text. The hierarchy is a
	// SCOPE on the fetch driver: the model hands the parent COLUMN + the node it
	// browses, the provider derives the physical field — no field names here.
	const ibBackendQueryColumn* parentCol = m_metaObject->GetQueryable()->GetParentColumn();
	if (parentCol == nullptr) return resp;

	ApplyListSettings(m_composer, m_metaObject, m_filterRow, &m_sortOrder);

	ibReadPageRequest page;
	page.m_direction   = direction;
	page.m_count       = fetchN;
	page.m_reverseSort = backward;            // inverse scan; the buffer reverses below
	page.m_hasAnchor   = hasAnchor;
	if (hasAnchor) {
		page.m_anchorGuid       = wxString(anchorGuid);
		page.m_anchorSortValues = extractSortValues(args.m_viewportAnchor);
	}

	ibListFetchDriver::ibTreeScope scope;
	scope.m_parentCol  = parentCol;
	scope.m_parentGuid = parentGuid;
	scope.m_flatScan   = (args.m_parent == s_constIgnoreParent);

	ibListFetchDriver fetchDriver(page, scope);
	m_composer.Run(fetchDriver);
	for (ibListFetchDriver::Row& src : fetchDriver.Rows()) {
		if (static_cast<int>(resp.m_rows.size()) > args.m_count) break;
		ibValueTreeListNode* row = BuildTreeRowFromComposed(src, this, m_metaObject);
		if (row != nullptr)
			resp.m_rows.push_back(row);
	}

	if (static_cast<int>(resp.m_rows.size()) > args.m_count) {
		resp.m_hasMore = true;
		delete resp.m_rows.back();
		resp.m_rows.pop_back();
	}
	if (backward)
		std::reverse(resp.m_rows.begin(), resp.m_rows.end());

	return resp;
}

ibValueModelTreeDataObjectFolderRef::ibTreeFetchResponse
ibValueModelTreeDataObjectFolderRef::GetPrevFetch(const ibTreeFetchArgs& args) const
{
	// The backward page is just envelope state of the ONE fetch body —
	// Backward sets the inverse scan there and reverses the buffer.
	return FetchWithDirection(args, ibFetchDirection::Backward);
}

// Bridge typed Get*Fetch(ibTreeFetchArgs) to the universal virtual on
// ibValueModel base. ibValueModel::AdoptRowsToItems handles the
// IncRef/DecRef dance so `out` owns a single ref per row.

// Universal-API adapter: pack ibDataViewItem parent/anchor + count into
// ibTreeFetchArgs, dispatch to one of the typed Get*Fetch overloads,
// adopt resulting rows into the universal item array.
template <class TypedFetch>
static unsigned int FolderRefUniversalAdapter(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out, TypedFetch&& typedFetch)
{
	ibValueModelTreeDataObjectFolderRef::ibTreeFetchArgs args;
	args.m_parent         = parent;
	args.m_viewportAnchor = anchor;
	args.m_count          = count > 0 ? count : defaultCountPerPage;
	auto resp = typedFetch(args);
	return AdoptAndCount(resp.m_rows, out);
}

unsigned int ibValueModelTreeDataObjectFolderRef::GetFirstFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	return FolderRefUniversalAdapter(parent, anchor, count, out,
		[this](const ibTreeFetchArgs& a) { return GetFirstFetch(a); });
}

unsigned int ibValueModelTreeDataObjectFolderRef::GetNextFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	return FolderRefUniversalAdapter(parent, anchor, count, out,
		[this](const ibTreeFetchArgs& a) { return GetNextFetch(a); });
}

unsigned int ibValueModelTreeDataObjectFolderRef::GetPrevFetch(
	const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	return FolderRefUniversalAdapter(parent, anchor, count, out,
		[this](const ibTreeFetchArgs& a) { return GetPrevFetch(a); });
}

/////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////
