////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "listSqlBuilder.h"
#include "registerSqlBuilder.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/databaseLayer.h"



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

	

	// SELECT + filter WHERE (builder).
	wxString sql = ibListSqlBuilder::BuildSelectHead(db.get(), tableName, fetchN);
	sql += ibListSqlBuilder::BuildFilterWhere(m_metaObject, m_filterRow);

	// ORDER BY — enum-specific CASE/WHEN over parent positions.
	wxString orderText;
	bool firstOrder = true;
	for (auto& s : m_sortOrder.m_sorts) {
		if (!s.m_sortEnable) continue;
		if (!m_metaObject->IsDataReference(s.m_sortModel)) continue;

		const wxString op = s.m_sortAscending ? "ASC" : "DESC";
		if (firstOrder) { orderText += " ORDER BY "; firstOrder = false; }
		else            { orderText += ", "; }

		orderText += "CASE\n";
		for (const auto* obj : m_metaObject->GetEnumObjectArray()) {
			orderText += wxString::Format(
				"WHEN %s = '%s' THEN %s\n",
				guidName,
				obj->GetGuid().str(),
				stringUtils::IntToStr(
					m_metaObject->FindEnumObjectByFilter(obj->GetGuid())
						->GetParentPosition()));
		}
		orderText += "END " + op + "\n";
	}
	sql += orderText;
	sql += ibListSqlBuilder::AppendLimit(db.get(), fetchN);

	// Prepare + bind.
	ibPreparedStatement* statement = db->PrepareStatement(sql);
	if (statement == nullptr) return resp;

	int position = 1;
	ibListSqlBuilder::BindFilterParams(statement, position,
	                                   m_metaObject, m_filterRow);

	// Materialize.
	ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
	ibValueMetaObjectAttributePredefined* metaOrder     = m_metaObject->GetDataOrder();

	ibDatabaseResultSet* rs = statement->RunQueryWithResults();
	if (rs != nullptr) {
		while (rs->Next()) {
			ibGuid enumGuid = rs->GetResultString(guidName);
			ibValueTableEnumRow* row = new ibValueTableEnumRow(enumGuid);
			row->AppendTableValue(
				metaReference->GetMetaID(),
				ibValueReferenceDataObject::CreateFromResultSet(
					rs, m_metaObject, row->GetGuid()));
			row->AppendTableValue(
				metaOrder->GetMetaID(),
				m_metaObject->FindEnumObjectByFilter(enumGuid)->GetParentPosition());
			resp.m_rows.push_back(row);
		}
		db->CloseResultSet(rs);
	}
	db->CloseStatement(statement);

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

	const wxString& tableName = m_metaObject->GetTableNameDB();
	const int       fetchN    = req.m_count + 1;   // +1 probe row → hasMore.

	

	// 1. Build SQL.
	const wxString filterWhere =
		ibListSqlBuilder::BuildFilterWhere(m_metaObject, m_filterRow);
	const bool hasFilters = !filterWhere.IsEmpty();

	wxString sql = ibListSqlBuilder::BuildSelectHead(db.get(), tableName, fetchN);
	sql += filterWhere;

	if (!req.m_anchor.m_empty) {
		sql += ibListSqlBuilder::BuildAnchorPredicate(
			m_metaObject, m_sortOrder,
			hasFilters,
			req.m_direction,
			wxString(req.m_anchor.m_key));
	}

	const bool reverse = (req.m_direction == ibFetchDirection::Backward);
	sql += ibListSqlBuilder::BuildOrderBy(m_metaObject, m_sortOrder, reverse);
	sql += ibListSqlBuilder::AppendLimit(db.get(), fetchN);

	

	// 2. Prepare + bind.
	ibPreparedStatement* statement = db->PrepareStatement(sql);
	if (statement == nullptr) return resp;

	int position = 1;
	ibListSqlBuilder::BindFilterParams(statement, position,
	                                   m_metaObject, m_filterRow);
	if (!req.m_anchor.m_empty) {
		ibListSqlBuilder::BindAnchorSortParams(statement, position,
		                                       m_metaObject, m_sortOrder,
		                                       req.m_anchor.m_sortValues);
	}

	// 3. Run + materialise rows.
	const std::vector<ibValueMetaObjectAttributeBase*>& vecAttr =
		m_metaObject->GetGenericAttributeArrayObject();
	ibValueMetaObjectAttributePredefined* metaReference =
		m_metaObject->GetDataReference();

	ibDatabaseResultSet* rs = statement->RunQueryWithResults();
	if (rs != nullptr) {
		while (rs->Next()) {
			ibValueTableListRow* row =
				new ibValueTableListRow(rs->GetResultString(guidName));
			for (auto& attr : vecAttr) {
				if (m_metaObject->IsDataReference(attr->GetMetaID()))
					continue;
				ibValueMetaObjectAttributeBase::GetValueAttribute(
					attr, row->AppendTableValue(attr->GetMetaID()), rs);
			}
			row->AppendTableValue(
				metaReference->GetMetaID(),
				ibValueReferenceDataObject::CreateFromResultSet(
					rs, m_metaObject, row->GetGuid()));
			resp.m_rows.push_back(row);
		}
		db->CloseResultSet(rs);
	}
	db->CloseStatement(statement);

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

ibFetchResponse<ibUniqueKeyPair, ibValueListRegisterObject::ibValueTableKeyRow>
ibValueListRegisterObject::Fetch(const ibFetchRequest<ibUniqueKeyPair>& req) const
{
	ibFetchResponse<ibUniqueKeyPair, ibValueTableKeyRow> resp;

	auto db = ses_query;
	if (db == nullptr || !db->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();
	const int       fetchN    = req.m_count > 0 ? req.m_count + 1 : 8192;

	

	// 1. Build SQL.
	const auto effective =
		ibRegisterSqlBuilder::EffectiveOrder(m_metaObject, m_sortOrder);

	const wxString filterWhere =
		ibRegisterSqlBuilder::BuildFilterWhere(m_metaObject, m_filterRow);
	const bool hasFilters = !filterWhere.IsEmpty();

	wxString sql = ibRegisterSqlBuilder::BuildSelectHead(db.get(), tableName, fetchN);
	sql += filterWhere;

	if (!req.m_anchor.m_empty) {
		sql += ibRegisterSqlBuilder::BuildAnchorPredicate(
			effective, hasFilters, req.m_direction);
	}

	const bool reverse = (req.m_direction == ibFetchDirection::Backward);
	sql += ibRegisterSqlBuilder::BuildOrderBy(effective, reverse);
	sql += ibRegisterSqlBuilder::AppendLimit(db.get(), fetchN);

	

	// 2. Prepare + bind.
	ibPreparedStatement* statement = db->PrepareStatement(sql);
	if (statement == nullptr) return resp;

	int position = 1;
	ibRegisterSqlBuilder::BindFilterParams(statement, position,
	                                       m_metaObject, m_filterRow);
	if (!req.m_anchor.m_empty) {
		ibRegisterSqlBuilder::BindAnchorParams(statement, position,
		                                       effective, req.m_anchor.m_sortValues);
	}

	// 3. Materialize.
	const std::vector<ibValueMetaObjectAttributeBase*>& vecAttr =
		m_metaObject->GetGenericAttributeArrayObject();
	const std::vector<ibValueMetaObjectAttributeBase*>& vecDim =
		m_metaObject->GetGenericDimensionArrayObject();

	ibDatabaseResultSet* rs = statement->RunQueryWithResults();
	if (rs != nullptr) {
		while (rs->Next()) {
			ibValueTableKeyRow* row = new ibValueTableKeyRow;
			if (m_metaObject->HasRecorder()) {
				ibValueMetaObjectAttributePredefined* attrRecorder =
					m_metaObject->GetRegisterRecorder();
				wxASSERT(attrRecorder);
				ibValueMetaObjectAttributeBase::GetValueAttribute(
					attrRecorder, row->AppendNodeValue(attrRecorder->GetMetaID()), rs);
				ibValueMetaObjectAttributePredefined* attrLine =
					m_metaObject->GetRegisterLineNumber();
				wxASSERT(attrLine);
				ibValueMetaObjectAttributeBase::GetValueAttribute(
					attrLine, row->AppendNodeValue(attrLine->GetMetaID()), rs);
			}
			else {
				for (auto& dim : vecDim) {
					ibValueMetaObjectAttributeBase::GetValueAttribute(
						dim, row->AppendNodeValue(dim->GetMetaID()), rs);
				}
			}
			for (auto& attr : vecAttr) {
				ibValueMetaObjectAttributeBase::GetValueAttribute(
					attr, row->AppendTableValue(attr->GetMetaID()), rs);
			}
			resp.m_rows.push_back(row);
		}
		db->CloseResultSet(rs);
	}
	db->CloseStatement(statement);

	// 4. hasMore via the +1 probe row.
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
	const std::vector<ibRegisterSqlBuilder::OrderCol>& effective,
	const ibValueListRegisterObject::ibValueTableKeyRow* row)
{
	ibFetchAnchor<ibUniqueKeyPair> a;
	a.m_empty = false;
	a.m_key   = row->GetUniquePairKey(meta);
	a.m_sortValues.reserve(effective.size());
	for (const auto& c : effective) {
		// Safe lookup: stub rows (built by FindRowValue for post-Save
		// focus restore) carry only m_nodeKeys (identity columns) —
		// m_nodeValues is empty.  GetTableValue would throw
		// std::out_of_range for every sort column on a stub; GetValue
		// catches that and leaves `v` as the default empty ibValue,
		// which serialises as NULL in the cursor predicate.
		// IsEqualTo on the fresh batch matches by m_nodeKeys, so
		// missing sort values don't affect identity restoration.
		ibValue v;
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
		const auto effective =
			ibRegisterSqlBuilder::EffectiveOrder(m_metaObject, m_sortOrder);
		req.m_anchor    = BuildRegisterAnchor(m_metaObject, effective, row);
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
	const auto effective =
		ibRegisterSqlBuilder::EffectiveOrder(m_metaObject, m_sortOrder);
	ibFetchRequest<ibUniqueKeyPair> req;
	req.m_anchor    = BuildRegisterAnchor(m_metaObject, effective, row);
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
	const auto effective =
		ibRegisterSqlBuilder::EffectiveOrder(m_metaObject, m_sortOrder);
	ibFetchRequest<ibUniqueKeyPair> req;
	req.m_anchor    = BuildRegisterAnchor(m_metaObject, effective, row);
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

	const wxString& tableName = m_metaObject->GetTableNameDB();
	auto* metaParent          = m_metaObject->GetDataParent();
	wxASSERT(metaParent);

	const wxString sql = wxString::Format(
		"SELECT * FROM %s WHERE %s = ?",
		tableName, guidName);

	ibGuid current = fromGuid;
	chain.push_back(current);

	// Cap depth — defends against cycles in malformed data.
	for (int depth = 0; depth < 100; ++depth) {
		ibPreparedStatement* stmt = db->PrepareStatement(sql);
		if (stmt == nullptr) break;
		stmt->SetParamString(1, wxString(current));

		ibValue parentValue;
		ibGuid parentGuid;
		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		if (rs != nullptr) {
			if (rs->Next()) {
				ibValueMetaObjectAttributeBase::GetValueAttribute(
					metaParent, parentValue, rs);
				ibValueReferenceDataObject* refData = nullptr;
				if (parentValue.ConvertToValue(refData) && refData != nullptr) {
					parentGuid = refData->GetGuid();
				}
			}
			db->CloseResultSet(rs);
		}
		db->CloseStatement(stmt);

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

// Build a row from the current result-set row, fully materialised
// with attributes + reference. Used by both NextFetch and PrevFetch.
// Returns the nested ibValueTreeListNode under FolderRef — qualified
// fully because we live in an anonymous namespace at file scope.
static ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode*
BuildTreeRowFromResultSet(
	ibDatabaseResultSet* rs,
	const ibValueModelTreeDataObjectFolderRef* owner,
	const ibValueMetaObjectRecordDataHierarchyMutableRef* meta)
{
	auto* metaIsFolder    = meta->GetDataIsFolder();
	auto* metaReference   = meta->GetDataReference();
	const auto& vec_attr  = meta->GetGenericAttributeArrayObject();

	ibValue isFolderVal;
	ibValueMetaObjectAttributeBase::GetValueAttribute(metaIsFolder, isFolderVal, rs);

	auto* row = new ibValueModelTreeDataObjectFolderRef::ibValueTreeListNode(
		nullptr,
		rs->GetResultString(guidName),
		const_cast<ibValueModelTreeDataObjectFolderRef*>(owner),
		isFolderVal.GetBoolean());
	for (auto& attribute : vec_attr) {
		if (meta->IsDataReference(attribute->GetMetaID())) continue;
		ibValueMetaObjectAttributeBase::GetValueAttribute(
			attribute, row->AppendTableValue(attribute->GetMetaID()), rs);
	}
	row->AppendTableValue(metaReference->GetMetaID(),
		ibValueReferenceDataObject::CreateFromResultSet(rs, meta, row->GetGuid()));
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

	const wxString& tableName = m_metaObject->GetTableNameDB();

	// One SELECT with WHERE uuid IN (?, ?, …).  We don't ORDER BY here
	// because we need to reorder the result rows to match `guids` input
	// order anyway (caller depends on it for breadcrumb sequencing).
	wxString placeholders;
	for (size_t i = 0; i < guids.size(); ++i) {
		if (i > 0) placeholders += ",";
		placeholders += "?";
	}
	const wxString sql = wxString::Format(
		"SELECT * FROM %s WHERE %s IN (%s)",
		tableName, guidName, placeholders);

	ibPreparedStatement* stmt = db->PrepareStatement(sql);
	if (stmt == nullptr) return rows;
	for (size_t i = 0; i < guids.size(); ++i)
		stmt->SetParamString(static_cast<int>(i + 1), wxString(guids[i]));

	// Build a guid → row* map; reorder to match input.
	std::unordered_map<wxString, ibValueTreeListNode*> byGuid;
	ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
	if (rs != nullptr) {
		while (rs->Next()) {
			ibValueTreeListNode* row = BuildTreeRowFromResultSet(rs, this, m_metaObject);
			if (row != nullptr)
				byGuid.emplace(wxString(row->GetGuid()), row);
		}
		db->CloseResultSet(rs);
	}
	db->CloseStatement(stmt);

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

	// FB stores parent ref in BINARY(sizeof(ibReference)) = 20 bytes:
	// [ibGuidImpl 16][ibMetaID 4]. On save, ibValueReferenceDataObject::
	// GetReferenceData() is bound via SetParamBlob(20). We mirror that
	// here for the WHERE clause so FB does the parent filtering instead
	// of a C++-side byte matcher running over rows the cursor already
	// trimmed.
	//
	// The ibMetaID half is ALWAYS the catalog's own metaID (=
	// m_metaObject->GetMetaID()), even for top-level rows whose logical
	// parent is empty — there m_guid is all zeros but m_id still names
	// the table.  Mirror that exactly; otherwise the bound blob differs
	// from the stored value by 4 bytes at the tail and equality fails.
	//
	// ibDataViewAllItem() — sentinel from the control side meaning
	// "every row, ignore parent".  Flat List view of a hierarchical
	// catalog passes it instead of GetTopParentItem(); we drop the
	// WHERE clause and let the cursor walk the whole table in one
	// ORDER BY (no recursive per-folder fetch — see paging-design.md
	// §8.4).  Tree / Hierarchical view keeps the parent filter; the
	// empty parent stays "top-level only".
	//
	// Anchor cursor uses the same composite predicate as Ref's paged
	// fetch (BuildAnchorPredicate): composite >=/<= over user sort cols
	// + case-when strict tail on uuid.
	ibReference parentRefBlob{ m_metaObject->GetMetaID(), ibGuidImpl{} };
	if (!isTopLevel) {
		const auto& be = parentGuid.bytes();
		// ibGuidImpl is MS-canonical: LE m_data1/2/3 + BE m_data4[8].
		// be[] is BE for all 16 bytes — reverse first 3 fields.
		auto* p = reinterpret_cast<unsigned char*>(&parentRefBlob.m_guid);
		p[0] = be[3]; p[1] = be[2]; p[2] = be[1]; p[3] = be[0];
		p[4] = be[5]; p[5] = be[4];
		p[6] = be[7]; p[7] = be[6];
		for (int i = 8; i < 16; ++i) p[i] = be[i];
	}

	const wxString parentClause = flatScan
		? wxString()
		: wxString::Format("WHERE %s = ? ", refDataField);

	const wxString filterClause = ibListSqlBuilder::BuildFilterWhere(
		m_metaObject, m_filterRow, /*whereAlreadyOpen=*/!flatScan);
	const bool hasFilters = !filterClause.IsEmpty();

	const wxString cursorClause = hasAnchor
		? ibListSqlBuilder::BuildAnchorPredicate(
		      m_metaObject, m_sortOrder,
		      /*whereOpen=*/!flatScan || hasFilters,
		      direction, wxString(anchorGuid))
		: wxString();

	// ORDER BY built from m_sortOrder.  System isFolder DESC (Tree /
	// Hierarchical modes) + user column sort + uuid ASC (system
	// reference sort — guarantees tail tiebreaker).
	const wxString orderBy = ibListSqlBuilder::BuildOrderBy(
		m_metaObject, m_sortOrder, /*reverse=*/false);

	wxString sql = ibListSqlBuilder::BuildSelectHead(db.get(), tableName, fetchN);
	sql += parentClause;
	sql += filterClause;
	sql += cursorClause;
	sql += orderBy;
	sql += ibListSqlBuilder::AppendLimit(db.get(), fetchN);

	const char* dirStr = (direction == ibFetchDirection::Reset) ? "Reset"
	                   : (direction == ibFetchDirection::Forward) ? "Forward"
	                   : "Backward";
	

	ibPreparedStatement* stmt = db->PrepareStatement(sql);
	if (stmt == nullptr) return resp;

	int pos = 1;
	if (!flatScan)
		stmt->SetParamBlob(pos++, &parentRefBlob, sizeof(ibReference));
	ibListSqlBuilder::BindFilterParams(stmt, pos, m_metaObject, m_filterRow);
	if (hasAnchor) {
		ibListSqlBuilder::BindAnchorSortParams(stmt, pos,
		    m_metaObject, m_sortOrder, anchorSortValues);
	}

	ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
	int matched = 0;
	if (rs != nullptr) {
		while (rs->Next()) {
			++matched;
			if (static_cast<int>(resp.m_rows.size()) > args.m_count) break;
			resp.m_rows.push_back(BuildTreeRowFromResultSet(rs, this, m_metaObject));
		}
		db->CloseResultSet(rs);
	}
	db->CloseStatement(stmt);
	

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
	ibReference parentRefBlob{ m_metaObject->GetMetaID(), ibGuidImpl{} };
	if (!isTopLevel) {
		const auto& be = parentGuid.bytes();
		auto* p = reinterpret_cast<unsigned char*>(&parentRefBlob.m_guid);
		p[0] = be[3]; p[1] = be[2]; p[2] = be[1]; p[3] = be[0];
		p[4] = be[5]; p[5] = be[4];
		p[6] = be[7]; p[7] = be[6];
		for (int i = 8; i < 16; ++i) p[i] = be[i];
	}

	const wxString parentClause = flatScan
		? wxString()
		: wxString::Format("WHERE %s = ? ", refDataField);

	const wxString filterClause = ibListSqlBuilder::BuildFilterWhere(
		m_metaObject, m_filterRow, /*whereAlreadyOpen=*/!flatScan);
	const bool hasFilters = !filterClause.IsEmpty();

	const wxString cursorClause = hasAnchor
		? ibListSqlBuilder::BuildAnchorPredicate(
		      m_metaObject, m_sortOrder,
		      /*whereOpen=*/!flatScan || hasFilters,
		      ibFetchDirection::Backward, wxString(anchorGuid))
		: wxString();

	const wxString orderBy = ibListSqlBuilder::BuildOrderBy(
		m_metaObject, m_sortOrder, /*reverse=*/true);

	wxString sql = ibListSqlBuilder::BuildSelectHead(db.get(), tableName, fetchN);
	sql += parentClause;
	sql += filterClause;
	sql += cursorClause;
	sql += orderBy;
	sql += ibListSqlBuilder::AppendLimit(db.get(), fetchN);

	

	ibPreparedStatement* stmt = db->PrepareStatement(sql);
	if (stmt == nullptr) return resp;

	int pos = 1;
	if (!flatScan)
		stmt->SetParamBlob(pos++, &parentRefBlob, sizeof(ibReference));
	ibListSqlBuilder::BindFilterParams(stmt, pos, m_metaObject, m_filterRow);
	if (hasAnchor) {
		ibListSqlBuilder::BindAnchorSortParams(stmt, pos,
		    m_metaObject, m_sortOrder, anchorSortValues);
	}

	ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
	int matched = 0;
	if (rs != nullptr) {
		while (rs->Next()) {
			++matched;
			if (static_cast<int>(resp.m_rows.size()) > args.m_count) break;
			resp.m_rows.push_back(BuildTreeRowFromResultSet(rs, this, m_metaObject));
		}
		db->CloseResultSet(rs);
	}
	db->CloseStatement(stmt);
	

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
