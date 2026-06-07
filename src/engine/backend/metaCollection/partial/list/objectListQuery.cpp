////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/query/dataQueryBuilder.h"   // universal (half-)L3 read entry

namespace {

// --- build-once page cache (Lever 1) signature helpers ---------------------
// The signature must capture EVERY SQL-determining input so a stale render is
// never reused. Filter values ride as Const (baked into the rendered plan), so
// they are part of the signature; the anchor rides as an external Param, so it
// is NOT. A value type the signature can't render losslessly (reference /
// composite) disables caching for that fetch — correctness over speed.

bool ValueSignable(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: case TYPE_NUMBER: case TYPE_DATE:
	case TYPE_STRING:  case TYPE_NULL:   case TYPE_EMPTY:
		return true;
	default:
		return false;   // reference / object — would alias in the signature
	}
}

wxString ValueSig(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: return v.GetBoolean() ? wxT("B1") : wxT("B0");
	case TYPE_NUMBER:  return wxT("N") + v.GetNumber().ToString();
	case TYPE_DATE:    return wxT("D") + v.GetDateTime().GetValue().ToString();
	case TYPE_STRING:  return wxT("S") + v.GetString();
	default:           return wxT("_");
	}
}

} // namespace



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

ibFetchResponse<ibGuid, ibValueListDataObjectEnumRef::ibValueTableEnumRow>
ibValueListDataObjectEnumRef::Fetch(const ibFetchRequest<ibGuid>& req) const
{
	ibFetchResponse<ibGuid, ibValueTableEnumRow> resp;

	auto db = ses_query;
	if (db == nullptr || !db->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();
	const int       fetchN    = req.m_count > 0 ? req.m_count + 1 : 1024;

	

	// L3 read entry — filter via Where. The enum order is a METADATA concern
	// (parent position), not a DB column, so we fetch unordered and sort in RAM
	// below: a single batch (the whole fixed list), so no DB-side CASE/WHEN.
	ibDataQueryBuilder readQuery;          // holder from the current session
	readQuery.From(m_metaObject->GetQueryable());
	for (const auto& f : m_filterRow.m_filters) {
		if (!f.m_filterUse) continue;
		readQuery.Where(m_metaObject->FindAnyAttributeObjectByFilter(f.m_filterModel),
		                f.m_filterComparison, f.m_filterValue);
	}

	ibReadPageRequest page;
	page.m_count = fetchN;

	ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
	ibValueMetaObjectAttributePredefined* metaOrder     = m_metaObject->GetDataOrder();

	ibDataQueryResult selection = readQuery.Select(page);
	while (selection.Next()) {
		ibGuid enumGuid = selection.GetGuidString();
		ibValueTableEnumRow* row = new ibValueTableEnumRow(enumGuid);
		row->AppendTableValue(metaReference->GetMetaID(), selection.GetValue(metaReference));
		row->AppendTableValue(
			metaOrder->GetMetaID(),
			m_metaObject->FindEnumObjectByFilter(enumGuid)->GetParentPosition());
		resp.m_rows.push_back(row);
	}
	// selection's destructor closes the cursor + statement (RAII).

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

	// 1-2. (half-)L3 read model: generate L2 by substituting metadata names with
	// physical names and run it over the session holder. The dialect fork and
	// the manual positional binding are gone; the anchor rides as a Param so the
	// SQL text is stable across scroll ticks (docs/query-language-arc.md §18).
	// The dynamic-list layer translates its UI filter/sort into query-native
	// conditions — ibFilterRow/ibSortOrder are NOT query types.
	const bool hasAnchor = !req.m_anchor.m_empty;
	const bool reverse   = (req.m_direction == ibFetchDirection::Backward);

	// Build the query AND the build-once cache signature in one pass. The
	// signature captures every SQL-determining input (filter fields/ops/values,
	// sort, count, direction, anchor presence); a non-signable filter value
	// disables caching (cacheable=false) for correctness.
	ibDataQueryBuilder readQuery;   // holder pulled from the current session
	readQuery.From(m_metaObject->GetQueryable());

	wxString sig;
	bool cacheable = true;
	sig << wxT("c") << fetchN << wxT("d") << static_cast<int>(req.m_direction)
	    << wxT("a") << (hasAnchor ? 1 : 0) << wxT("r") << (reverse ? 1 : 0) << wxT("|F");
	for (const auto& f : m_filterRow.m_filters) {
		if (!f.m_filterUse) continue;
		readQuery.Where(m_metaObject->FindAnyAttributeObjectByFilter(f.m_filterModel),
		                f.m_filterComparison, f.m_filterValue);
		if (!ValueSignable(f.m_filterValue)) cacheable = false;
		sig << wxT(";") << f.m_filterModel << wxT(":")
		    << static_cast<int>(f.m_filterComparison) << wxT("=") << ValueSig(f.m_filterValue);
	}
	sig << wxT("|S");
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		const bool isRef = m_metaObject->IsDataReference(s.m_sortModel);
		readQuery.OrderBy(isRef ? nullptr
		                        : m_metaObject->FindAnyAttributeObjectByFilter(s.m_sortModel),
		                  s.m_sortAscending);
		sig << wxT(";") << s.m_sortModel << (s.m_sortAscending ? wxT("+") : wxT("-"));
	}

	ibReadPageRequest page;
	page.m_direction        = req.m_direction;
	page.m_hasAnchor        = hasAnchor;
	page.m_anchorSortValues = req.m_anchor.m_sortValues;
	page.m_anchorGuid       = req.m_anchor.m_empty ? wxString() : wxString(req.m_anchor.m_key);
	page.m_count            = fetchN;
	page.m_reverseSort      = reverse;

	// 3. Open the L3 selection and materialise rows. The selection yields ready
	// ibValue (the raw cursor is hidden inside it); its dtor releases the holder.
	const std::vector<ibValueMetaObjectAttributeBase*>& vecAttr =
		m_metaObject->GetGenericAttributeArrayObject();
	ibValueMetaObjectAttributePredefined* metaReference =
		m_metaObject->GetDataReference();

	if (!m_pageCache) m_pageCache = ibDataQueryBuilder::NewPageCache();
	ibDataQueryResult selection = cacheable
		? readQuery.Select(page, *m_pageCache, sig)
		: readQuery.Select(page);
	while (selection.Next()) {
		ibValueTableListRow* row =
			new ibValueTableListRow(selection.GetGuidString());
		for (auto& attr : vecAttr) {
			if (m_metaObject->IsDataReference(attr->GetMetaID()))
				continue;
			row->AppendTableValue(attr->GetMetaID(), selection.GetValue(attr));
		}
		// The reference column goes through the same accessor.
		row->AppendTableValue(metaReference->GetMetaID(), selection.GetValue(metaReference));
		resp.m_rows.push_back(row);
	}
	// selection's destructor closes the cursor + statement (RAII).

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

	// (half-)L3 read model: the register is queried through the SAME universal
	// door as catalogs. The door appends the register's identity tail
	// (recorder+line / period?+dimensions) to the user sort via GetIdentitySort,
	// so the keyset has a total order; the anchor rides as a Param (stable SQL
	// text across ticks → driver prepared-statement cache hits — L1-fast).
	const bool hasAnchor = !req.m_anchor.m_empty;
	const bool reverse   = (req.m_direction == ibFetchDirection::Backward);

	ibDataQueryBuilder readQuery;        // holder pulled from the current session
	readQuery.From(m_metaObject->GetQueryable());

	wxString sig;
	bool cacheable = true;
	sig << wxT("c") << fetchN << wxT("d") << static_cast<int>(req.m_direction)
	    << wxT("a") << (hasAnchor ? 1 : 0) << wxT("r") << (reverse ? 1 : 0) << wxT("|F");
	for (const auto& f : m_filterRow.m_filters) {
		if (!f.m_filterUse) continue;
		readQuery.Where(m_metaObject->FindAnyAttributeObjectByFilter(f.m_filterModel),
		                f.m_filterComparison, f.m_filterValue);
		if (!ValueSignable(f.m_filterValue)) cacheable = false;
		sig << wxT(";") << f.m_filterModel << wxT(":")
		    << static_cast<int>(f.m_filterComparison) << wxT("=") << ValueSig(f.m_filterValue);
	}
	sig << wxT("|S");
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		readQuery.OrderBy(m_metaObject->FindAnyAttributeObjectByFilter(s.m_sortModel),
		                  s.m_sortAscending);
		sig << wxT(";") << s.m_sortModel << (s.m_sortAscending ? wxT("+") : wxT("-"));
	}

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

	if (!m_pageCache) m_pageCache = ibDataQueryBuilder::NewPageCache();
	ibDataQueryResult selection = cacheable
		? readQuery.Select(page, *m_pageCache, sig)
		: readQuery.Select(page);
	while (selection.Next()) {
		ibValueTableKeyRow* row = new ibValueTableKeyRow;
		if (m_metaObject->HasRecorder()) {
			ibValueMetaObjectAttributePredefined* attrRecorder = m_metaObject->GetRegisterRecorder();
			ibValueMetaObjectAttributePredefined* attrLine     = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attrRecorder && attrLine);
			row->AppendNodeValue(attrRecorder->GetMetaID(), selection.GetValue(attrRecorder));
			row->AppendNodeValue(attrLine->GetMetaID(),     selection.GetValue(attrLine));
		}
		else {
			for (auto& dim : vecDim)
				row->AppendNodeValue(dim->GetMetaID(), selection.GetValue(dim));
		}
		for (auto& attr : vecAttr)
			row->AppendTableValue(attr->GetMetaID(), selection.GetValue(attr));
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
		if (c.m_attr != nullptr)
			row->GetValue(c.m_attr->GetMetaID(), v);
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
			ibDataQueryBuilder().From(m_metaObject->GetQueryable()).WhereKey(current).Select(page);

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

	auto* row = new ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode(
		nullptr,
		sel.GetGuidString(),
		const_cast<ibValueModelTreeDataObjectFolderRef*>(owner),
		isFolderVal.GetBoolean());
	for (auto& attribute : vec_attr) {
		if (meta->IsDataReference(attribute->GetMetaID())) continue;
		row->AppendTableValue(attribute->GetMetaID()) = sel.GetValue(attribute);
	}
	row->AppendTableValue(metaReference->GetMetaID(), sel.GetValue(metaReference));
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
		ibDataQueryBuilder().From(m_metaObject->GetQueryable()).WhereKeyIn(guids).Select(page);

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
	ibTreeFetchResponse resp;
	auto db = ses_query;
	if (db == nullptr || !db->IsOpen()) return resp;

	const wxString& tableName = m_metaObject->GetTableNameDB();
	auto* metaParent          = m_metaObject->GetDataParent();

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
	const std::vector<ibValue> anchorSortValues =
		extractSortValues(args.m_viewportAnchor);

	// REFDATA column name for the parent ref attribute.
	wxString refDataField;
	for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(metaParent)) {
		if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
			refDataField = field.m_field.m_fieldRefName.m_fieldRefName;
			break;
		}
	}
	if (refDataField.IsEmpty()) return resp;

	const bool flatScan   = (args.m_parent == s_constIgnoreParent);
	const bool isTopLevel = !parentGuid.isValid();
	const bool hasAnchor  = anchorGuid.isValid();
	const int  fetchN     = args.m_count > 0 ? args.m_count + 1 : 21;

	// Build via the universal L3 read entry. The dynamic-list layer translates
	// its filter/sort into query-native conditions; the hierarchy parent filter
	// rides in the page request (the parent ref blob is built inside
	// ibMetaIRBuilder::BuildParentRefPredicate — opaque to L2). The empty parent
	// stays "top-level only"; flatScan drops the parent filter (whole-table walk).
	ibDataQueryBuilder readQuery;          // holder from the current session
	readQuery.From(m_metaObject->GetQueryable());
	for (const auto& f : m_filterRow.m_filters) {
		if (!f.m_filterUse) continue;
		readQuery.Where(m_metaObject->FindAnyAttributeObjectByFilter(f.m_filterModel),
		                f.m_filterComparison, f.m_filterValue);
	}
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		const bool isRef = m_metaObject->IsDataReference(s.m_sortModel);
		readQuery.OrderBy(isRef ? nullptr
		                        : m_metaObject->FindAnyAttributeObjectByFilter(s.m_sortModel),
		                  s.m_sortAscending);
	}

	ibReadPageRequest page;
	page.m_direction      = direction;
	page.m_count          = fetchN;
	page.m_reverseSort    = false;            // tree order is not reversed by direction
	page.m_parentFilter   = true;
	page.m_parentRefField = refDataField;
	page.m_parentGuid     = parentGuid;
	page.m_isTopLevel     = isTopLevel;
	page.m_flatScan       = flatScan;
	page.m_hasAnchor      = hasAnchor;
	if (hasAnchor) {
		page.m_anchorGuid       = wxString(anchorGuid);
		page.m_anchorSortValues = anchorSortValues;
	}

	ibDataQueryResult selection = readQuery.Select(page);
	while (selection.Next()) {
		if (static_cast<int>(resp.m_rows.size()) > args.m_count) break;
		resp.m_rows.push_back(BuildTreeRowFromSelection(selection, this, m_metaObject));
	}
	// selection's destructor closes the cursor + statement (RAII).
	

	if (static_cast<int>(resp.m_rows.size()) > args.m_count) {
		resp.m_hasMore = true;
		delete resp.m_rows.back();
		resp.m_rows.pop_back();
	}
	
	return resp;
}

ibValueModelTreeDataObjectFolderRef::ibTreeFetchResponse
ibValueModelTreeDataObjectFolderRef::GetPrevFetch(const ibTreeFetchArgs& args) const
{
	ibTreeFetchResponse resp;
	auto db = ses_query;
	if (db == nullptr || !db->IsOpen()) return resp;

	const wxString& tableName = m_metaObject->GetTableNameDB();
	auto* metaParent          = m_metaObject->GetDataParent();

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
	const std::vector<ibValue> anchorSortValues =
		extractSortValues(args.m_viewportAnchor);

	wxString refDataField;
	for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(metaParent)) {
		if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
			refDataField = field.m_field.m_fieldRefName.m_fieldRefName;
			break;
		}
	}
	if (refDataField.IsEmpty()) return resp;

	const bool flatScan   = (args.m_parent == s_constIgnoreParent);
	const bool isTopLevel = !parentGuid.isValid();
	const bool hasAnchor  = anchorGuid.isValid();
	const int  fetchN     = args.m_count > 0 ? args.m_count + 1 : 21;

	// SQL parent filter via 20-byte ibReference blob bind — see
	// GetNextFetch above for the rationale.  Backward composite
	// cursor: BuildAnchorPredicate with Backward walks rows BEFORE the
	// anchor in display order; reverse=true on BuildOrderBy flips
	// ASC↔DESC across all cols so the result set's leading edge is
	// the rows immediately before anchor.  std::reverse below restores
	// natural forward order before returning to the caller.
	// Mirror GetNextFetch: ibMetaID half is ALWAYS the catalog's own
	// metaID, even for top-level rows whose logical parent guid is
	// zero.  Top-level rows are stored with {metaID, zero-guid} per
	// the FB BINARY(20) reference convention, so binding {0, zero} as
	// the WHERE parameter never matches and backward fetch silently
	// returns 0 — visible to the user as "rows above viewport
	// disappear after refresh" because the bootstrap-issued backward
	// fetch can't load any predecessors of the saved-top anchor.
	// Same universal L3 entry as GetNextFetch, but the backward walk scans in
	// inverse sort order (reverseSort) and the buffer reverses the result below.
	ibDataQueryBuilder readQuery;          // holder from the current session
	readQuery.From(m_metaObject->GetQueryable());
	for (const auto& f : m_filterRow.m_filters) {
		if (!f.m_filterUse) continue;
		readQuery.Where(m_metaObject->FindAnyAttributeObjectByFilter(f.m_filterModel),
		                f.m_filterComparison, f.m_filterValue);
	}
	for (const auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		const bool isRef = m_metaObject->IsDataReference(s.m_sortModel);
		readQuery.OrderBy(isRef ? nullptr
		                        : m_metaObject->FindAnyAttributeObjectByFilter(s.m_sortModel),
		                  s.m_sortAscending);
	}

	ibReadPageRequest page;
	page.m_direction      = ibFetchDirection::Backward;
	page.m_count          = fetchN;
	page.m_reverseSort    = true;             // inverse scan; buffer reverses below
	page.m_parentFilter   = true;
	page.m_parentRefField = refDataField;
	page.m_parentGuid     = parentGuid;
	page.m_isTopLevel     = isTopLevel;
	page.m_flatScan       = flatScan;
	page.m_hasAnchor      = hasAnchor;
	if (hasAnchor) {
		page.m_anchorGuid       = wxString(anchorGuid);
		page.m_anchorSortValues = anchorSortValues;
	}

	ibDataQueryResult selection = readQuery.Select(page);
	while (selection.Next()) {
		if (static_cast<int>(resp.m_rows.size()) > args.m_count) break;
		resp.m_rows.push_back(BuildTreeRowFromSelection(selection, this, m_metaObject));
	}
	// selection's destructor closes the cursor + statement (RAII).
	

	if (static_cast<int>(resp.m_rows.size()) > args.m_count) {
		resp.m_hasMore = true;
		delete resp.m_rows.back();
		resp.m_rows.pop_back();
	}
	std::reverse(resp.m_rows.begin(), resp.m_rows.end());
	
	return resp;
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
