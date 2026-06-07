////////////////////////////////////////////////////////////////////////////
//	Description : universal (half-)L3 read entry + selection. Metadata in,
//	              ready ibValue rows out. Generates L2 (ibDatabaseQueryBuilder)
//	              by substituting metadata names with physical names (via
//	              ibMetaIRBuilder), over a borrowed holder. One door for list /
//	              tree / enum reads. See docs/query-language-arc.md §18.
////////////////////////////////////////////////////////////////////////////

#include "dataQueryBuilder.h"
#include "queryProvider.h"                                           // ibBackendQueryProvider (the DB provider implements it)

// L2 is included HERE (not in the L3 header) — the .cpp is where L3 lowers to L2.
#include "backend/databaseLayer/databaseQueryBuilder.h"              // L2: IR + builder + statement + rendered query
#include "backend/metaCollection/partial/list/objectList.h"          // meta types + guidName
#include "backend/metaCollection/attribute/metaAttributeObject.h"    // GetValueAttribute / SetValueAttribute / GetSQLFieldName
#include "backend/metaCollection/partial/reference/reference.h"      // ibValueReferenceDataObject
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/session/session.h"                                 // ibSession::Current()->Holder()
#include "backend/valueInfo.h"                                       // ibReference (physical reference blob)
#include "backend/system/value/valueTable.h"                         // ibValueModelTable — RAM backing for computed virtual tables

#include <map>                                                        // dot-walk join dedup (path-prefix -> alias)

// ibRenderedPageCache — opaque in the header; full layout lives here (it stores an
// L2 ibRenderedQuery). Created via ibDataQueryBuilder::NewPageCache().
struct ibRenderedPageCache
{
	wxString                     m_sig;             // signature of the SQL-determining inputs
	std::vector<ibQuerySortItem> m_effectiveSort;   // resolved once (identity tail walk)
	ibRenderedQuery              m_rendered;         // SQL + bind plan, rendered once
	bool                         m_valid = false;
};

namespace {
// Split a GetSQLFieldName list ("a_TYPE,a_S,b_RTRef,b_RRRef" — comma, no spaces)
// into individual column names, in the order SetValueAttribute binds them.
std::vector<wxString> SplitSqlFieldNames(const wxString& joined)
{
	std::vector<wxString> out;
	wxString cur;
	for (const wxUniChar ch : joined) {
		if (ch == wxT(',')) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
		else                  cur += ch;
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}
} // namespace

// ==========================================================================
// Name-substitution primitives (ibMetaIRBuilder) — query-native conditions /
// sorts (resolved attributes) -> physical query IR. Emits IR, not SQL text;
// the dialect fork and manual binding are gone. See docs/query-language-arc.md §18.
// ==========================================================================

namespace {

// First physical SQL field of an attribute (single-field common case).
wxString FirstSqlField(const ibValueMetaObjectAttributeBase* attr)
{
	for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attr)) {
		return (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference)
			? field.m_field.m_fieldRefName.m_fieldRefName
			: field.m_field.m_fieldName;
	}
	return wxString();
}

// First physical SQL field of a query COLUMN, derived from (physical name, type) —
// the L3 lowering of queryColumn.h, independent of any concrete metaobject attribute
// (an L3 source may be a temp table whose columns are not attributes). Single-type
// columns (the dot-walk case) map to one suffix; a reference column yields its _RRRef.
wxString FirstSqlFieldOfColumn(const ibBackendQueryColumn* col)
{
	const wxString base = col->GetPhysicalName();
	const ibTypeDescription& td = col->GetTypeDesc();
	if (td.GetClsidList().empty())
		return base;
	switch (ibValue::GetVTByID(td.GetFirstClsid())) {
	case ibValueTypes::TYPE_BOOLEAN: return base + wxT("_B");
	case ibValueTypes::TYPE_NUMBER:  return base + wxT("_N");
	case ibValueTypes::TYPE_DATE:    return base + wxT("_D");
	case ibValueTypes::TYPE_STRING:  return base + wxT("_S");
	case ibValueTypes::TYPE_ENUM:    return base + wxT("_E");
	default:                          return base + wxT("_RRRef");   // reference -> the guid+metaID blob
	}
}

// ibCol with an optional table qualifier — bare when `qualifier` is empty (the
// no-join path), qualified (table.col) when a dot-walk join is present and the main
// columns must be disambiguated from the joined target's same-named columns (uuid).
ibQueryExprPtr ibColQ(const wxString& qualifier, const wxString& name)
{
	return qualifier.empty() ? ibCol(name) : ibCol(qualifier, name);
}

ibQueryExprPtr AndFold(ibQueryExprPtr a, ibQueryExprPtr b)
{
	if (!a) return b;
	if (!b) return a;
	return ibBinOp(ibQueryBinOp::And, a, b);
}

ibQueryExprPtr OrFold(ibQueryExprPtr a, ibQueryExprPtr b)
{
	if (!a) return b;
	if (!b) return a;
	return ibBinOp(ibQueryBinOp::Or, a, b);
}

// Decompose an attribute equality into per-physical-field terms, REUSING the
// write decomposition: SetValueAttribute spreads the value across the attribute's
// fields (the statement reserves one slot per field and binds in order — no
// counter leaks out), a capture-only statement records each as a Const node. A
// multi-field key (composite / variant / reference dimension) thus filters on ALL
// its fields, AND-folded — symmetric with how WriteRow writes it. A single field
// yields one term. The statement is never run — a pure value sink, invisible to
// the L3 surface (this is lowering; the L3 column itself stays purely typed).
ibQueryExprPtr DecomposeEquality(const ibValueMetaObjectAttributeBase* attr, const ibValue& value,
                                 const wxString& mainQual = wxEmptyString)
{
	std::vector<wxString> fields =
		SplitSqlFieldNames(ibValueMetaObjectAttributeBase::GetSQLFieldName(attr));
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int position = 1;
	ibValueMetaObjectAttributeBase::SetValueAttribute(attr, value, &capture, position);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	ibQueryExprPtr pred;
	for (size_t i = 0; i < fields.size(); ++i) {
		ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
		pred = AndFold(pred, ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, fields[i]), c));
	}
	return pred;
}

// ibMetaIRBuilder — file-local now (was a BACKEND_API class in the header). The
// GENERIC name-substitution primitives that turn query-native conditions / sorts
// (resolved attributes) into physical L2 ibQueryIR fragments. Only this TU lowers
// to L2, so the class no longer belongs on the public surface; per-family
// knowledge still arrives through ibBackendQueryable, so catalog and register
// share ONE keyset with no fork.
// `mainQual` (default empty) qualifies the MAIN table's columns when a dot-walk join
// is present — so the main row-key (uuid) and fields don't collide with the joined
// target's same-named columns. Empty on the plain (no-join) path: columns stay bare.
class ibMetaIRBuilder {
public:
	static ibQueryExprPtr BuildFilterPredicate(const ibBackendQueryable* meta,
	                                           const std::vector<ibQueryCondition>& conditions,
	                                           const wxString& mainQual = wxEmptyString);
	static std::vector<ibQuerySortKey> BuildSortKeys(const ibBackendQueryable* meta,
	                                                 const std::vector<ibQuerySortItem>& sorts,
	                                                 bool reverse,
	                                                 const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildAnchorPredicate(const ibBackendQueryable* meta,
	                                           const std::vector<ibQuerySortItem>& sorts,
	                                           ibFetchDirection direction,
	                                           const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildParentRefPredicate(const ibBackendQueryable* meta,
	                                              const wxString& refDataField,
	                                              const ibGuid& parentGuid,
	                                              bool isTopLevel,
	                                              const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildKeyInPredicate(const ibBackendQueryable* meta,
	                                          const std::vector<ibValue>& keyValues,
	                                          const wxString& mainQual = wxEmptyString);
};

} // namespace

ibQueryExprPtr ibMetaIRBuilder::BuildFilterPredicate(const ibBackendQueryable* meta,
                                                     const std::vector<ibQueryCondition>& conditions,
                                                     const wxString& mainQual)
{
	ibQueryExprPtr pred;

	// Translate the L3-native filter op to a physical IR operator (kept here, not
	// on the metadata side, so queryable.h carries no L2 dependency).
	auto toBinOp = [](ibQueryFilterOp op) {
		switch (op) {
		case ibQueryFilterOp::Like:         return ibQueryBinOp::Like;
		case ibQueryFilterOp::Less:         return ibQueryBinOp::Lt;
		case ibQueryFilterOp::LessEqual:    return ibQueryBinOp::Le;
		case ibQueryFilterOp::Greater:      return ibQueryBinOp::Gt;
		case ibQueryFilterOp::GreaterEqual: return ibQueryBinOp::Ge;
		}
		return ibQueryBinOp::Eq;
	};

	for (const ibQueryCondition& c : conditions) {
		const ibQueryBinOp op =
			c.m_explicitOp ? toBinOp(c.m_op)
			               : (c.m_comparison == ibComparisonType::ibComparisonType_Equal
			                  ? ibQueryBinOp::Eq : ibQueryBinOp::Ne);

		ibQueryExprPtr cmp;
		if (c.m_attr == nullptr) {
			// Row-key condition — a lookup by the row's own key (never LIKE).
			cmp = ibBinOp(op, ibColQ(mainQual, meta->GetRowKeyColumn()), ibConst(c.m_value));
		}
		else if (meta->IsReferenceAttribute(c.m_attr->GetMetaID())) {
			// Reference condition: compare the row-key column against the ref's
			// guid (bound Const — no string concatenation).
			wxString guid = wxNullUniqueKey;
			ibValueReferenceDataObject* refData = nullptr;
			if (c.m_value.ConvertToValue(refData) && refData)
				guid = refData->GetGuid();
			cmp = ibBinOp(op, ibColQ(mainQual, meta->GetRowKeyColumn()), ibConst(ibValue(guid)));
		}
		else if (op == ibQueryBinOp::Eq) {
			// Equality on a regular attribute — decompose across ALL its physical
			// fields (composite / variant / reference-dimension safe), AND-folded.
			// This is what makes a register's composite-key read match every
			// dimension field, not just the first.
			cmp = DecomposeEquality(c.m_attr, c.m_value, mainQual);
		}
		else {
			// Ordered / inequality / LIKE compare — single-field attributes only
			// (code / description), value bound as a Const.
			cmp = ibBinOp(op, ibColQ(mainQual, FirstSqlField(c.m_attr)), ibConst(c.m_value));
		}
		pred = AndFold(pred, cmp);
	}

	return pred;
}

std::vector<ibQuerySortKey> ibMetaIRBuilder::BuildSortKeys(const ibBackendQueryable* meta,
                                                           const std::vector<ibQuerySortItem>& sorts,
                                                           bool reverse,
                                                           const wxString& mainQual)
{
	std::vector<ibQuerySortKey> keys;

	// Pass 1 — attribute sorts. Each attribute may expand to several physical
	// fields (composite key components).
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_attr == nullptr) continue;   // the row-key sort is pass 2

		const bool asc = reverse ? !s.m_ascending : s.m_ascending;
		for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(s.m_attr)) {
			const wxString name =
				(field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference)
				? field.m_field.m_fieldRefName.m_fieldRefName
				: field.m_field.m_fieldName;
			ibQuerySortKey k;
			k.m_expr = ibColQ(mainQual, name);
			k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			keys.push_back(std::move(k));
		}
	}

	// Pass 2 — a null-attr sort item appends the row-key column (catalog).
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_attr != nullptr) continue;

		const bool asc = reverse ? !s.m_ascending : s.m_ascending;
		ibQuerySortKey k;
		k.m_expr = ibColQ(mainQual, meta->GetRowKeyColumn());
		k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
		keys.push_back(std::move(k));
	}

	return keys;
}

ibQueryExprPtr ibMetaIRBuilder::BuildAnchorPredicate(const ibBackendQueryable* meta,
                                                     const std::vector<ibQuerySortItem>& sorts,
                                                     ibFetchDirection direction,
                                                     const wxString& mainQual)
{
	// Values are emitted as Param nodes; the caller binds, in order,
	// [ sortValue_0 .. sortValue_{K-1}, rowKeyValue? ] (row-key at index K = #cols).
	const bool forward = (direction == ibFetchDirection::Forward) ||
	                     (direction == ibFetchDirection::Reset);
	const bool inclusiveTail = (direction == ibFetchDirection::Reset);

	struct SortCol {
		const ibValueMetaObjectAttributeBase* attr;
		bool asc;
	};
	// A null-attr sort item is the row-key tiebreaker (catalog). Registers have
	// no row-key — their identity columns are real attributes already in `cols`,
	// so the last col is the unique tiebreaker and there is no separate tail.
	std::vector<SortCol> cols;
	bool refAsc = true;
	bool hasKeyTail = false;
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_attr == nullptr) { refAsc = s.m_ascending; hasKeyTail = true; continue; }
		cols.push_back({ s.m_attr, s.m_ascending });
	}

	auto strictOp    = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Gt : ibQueryBinOp::Lt; };
	auto inclusiveOp = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Ge : ibQueryBinOp::Le; };

	// Equality chain on cols[0 .. kExclusive-1]. Each sort col value = Param j.
	auto eqUpTo = [&](size_t kExclusive) -> ibQueryExprPtr {
		ibQueryExprPtr eq;
		for (size_t j = 0; j < kExclusive; ++j)
			eq = AndFold(eq, ibBinOp(ibQueryBinOp::Eq,
			                         ibColQ(mainQual, FirstSqlField(cols[j].attr)),
			                         ibParam(static_cast<int>(j))));
		return eq;
	};

	// OR-of-AND: clause i = (c_0 = .. c_{i-1} =) AND c_i strict. When there is no
	// row-key tail (register), the LAST col is the unique tiebreaker, so it
	// carries the inclusive op on Reset (the anchor row is included).
	ibQueryExprPtr predicate;
	for (size_t i = 0; i < cols.size(); ++i) {
		const bool isLast = (i + 1 == cols.size());
		const ibQueryBinOp op =
			(inclusiveTail && isLast && !hasKeyTail) ? inclusiveOp(cols[i].asc)
			                                         : strictOp(cols[i].asc);
		ibQueryExprPtr clause = AndFold(
			eqUpTo(i),
			ibBinOp(op, ibColQ(mainQual, FirstSqlField(cols[i].attr)), ibParam(static_cast<int>(i))));
		predicate = OrFold(predicate, clause);
	}

	// Tail row: all equalities + the row-key tiebreak — ONLY when a row-key tail
	// exists (catalog). Row-key value = Param at index K (= #cols).
	if (hasKeyTail) {
		const ibQueryBinOp tailOp = inclusiveTail ? inclusiveOp(refAsc) : strictOp(refAsc);
		ibQueryExprPtr clause = AndFold(
			eqUpTo(cols.size()),
			ibBinOp(tailOp, ibColQ(mainQual, meta->GetRowKeyColumn()), ibParam(static_cast<int>(cols.size()))));
		predicate = OrFold(predicate, clause);
	}

	return predicate;
}

ibQueryExprPtr ibMetaIRBuilder::BuildParentRefPredicate(const ibBackendQueryable* meta,
                                                        const wxString& refDataField,
                                                        const ibGuid& parentGuid,
                                                        bool isTopLevel,
                                                        const wxString& mainQual)
{
	// Mirror the save path: _RRRef BINARY = [ibGuidImpl 16][ibMetaID 4]. The
	// metaID half is ALWAYS this metaobject's id (even top-level, guid all-zero),
	// else the bound blob differs from the stored value by 4 tail bytes.
	ibReference ref{ meta->GetQueryMetaID(), ibGuidImpl{} };
	if (!isTopLevel) {
		const auto& be = parentGuid.bytes();
		// ibGuidImpl is MS-canonical (LE m_data1/2/3 + BE m_data4[8]); be[] is
		// BE for all 16 bytes — reverse the first three fields.
		auto* p = reinterpret_cast<unsigned char*>(&ref.m_guid);
		p[0] = be[3]; p[1] = be[2]; p[2] = be[1]; p[3] = be[0];
		p[4] = be[5]; p[5] = be[4];
		p[6] = be[7]; p[7] = be[6];
		for (int i = 8; i < 16; ++i) p[i] = be[i];
	}
	// Opaque blob Const — L2 binds the bytes via SetParamBlob, metadata-blind.
	return ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField),
	               ibConstBlob(&ref, sizeof(ibReference)));
}

ibQueryExprPtr ibMetaIRBuilder::BuildKeyInPredicate(const ibBackendQueryable* meta,
                                                    const std::vector<ibValue>& keyValues,
                                                    const wxString& mainQual)
{
	// row-key IN (...) as OR-of-equals — each value a bound Const.
	ibQueryExprPtr pred;
	for (const ibValue& v : keyValues)
		pred = OrFold(pred, ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, meta->GetRowKeyColumn()), ibConst(v)));
	return pred;
}

// ==========================================================================
// ibDataQueryBuilder
// ==========================================================================

ibDataQueryBuilder::ibDataQueryBuilder()
	: m_holder(ibSession::Current() != nullptr ? ibSession::Current()->Holder() : nullptr)
{
}

ibDataQueryBuilder::ibDataQueryBuilder(ibDatabaseConnectionHolder* holder)
	: m_holder(holder)
{
}

ibDataQueryBuilder& ibDataQueryBuilder::From(const ibBackendQueryable* meta)
{
	m_meta = meta;
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Where(const ibValueMetaObjectAttributeBase* attr,
                                              ibComparisonType comparison, const ibValue& value)
{
	m_conditions.push_back(ibQueryCondition{ attr, comparison, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereCompare(const ibValueMetaObjectAttributeBase* attr,
                                                     ibQueryFilterOp op, const ibValue& value)
{
	ibQueryCondition c;
	c.m_attr       = attr;
	c.m_value      = value;
	c.m_explicitOp = true;
	c.m_op         = op;
	m_conditions.push_back(c);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereLike(const ibValueMetaObjectAttributeBase* attr, const ibValue& pattern)
{
	return WhereCompare(attr, ibQueryFilterOp::Like, pattern);
}

ibDataQueryBuilder& ibDataQueryBuilder::OrderBy(const ibValueMetaObjectAttributeBase* attr, bool ascending)
{
	m_sorts.push_back(ibQuerySortItem{ attr, ascending });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::GroupBy(const ibValueMetaObjectAttributeBase* attr)
{
	if (attr) m_groupBy.push_back(attr);
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Aggregate(AggregateFn fn,
	const ibValueMetaObjectAttributeBase* attr, const wxString& alias)
{
	m_aggregates.push_back(AggregateItem{ fn, attr, alias });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::Having(AggregateFn fn,
	const ibValueMetaObjectAttributeBase* attr, ibQueryFilterOp op, const ibValue& value)
{
	m_having.push_back(HavingItem{ fn, attr, op, value });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SelectPath(
	const std::vector<const ibBackendQueryColumn*>& path, const wxString& alias)
{
	if (!path.empty())
		m_dotWalks.push_back(ibDotWalkColumn{ path, alias });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereKey(const ibGuid& rowGuid)
{
	// Row-key equality — m_attr == nullptr means the row-key column in the IR builder.
	m_conditions.push_back(ibQueryCondition{
		nullptr, ibComparisonType::ibComparisonType_Equal, ibValue(wxString(rowGuid)) });
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::WhereKeyIn(const std::vector<ibGuid>& rowGuids)
{
	for (const ibGuid& g : rowGuids)
		m_keyIn.push_back(ibValue(wxString(g)));
	return *this;
}

ibDataQueryBuilder& ibDataQueryBuilder::SetValue(const ibValueMetaObjectAttributeBase* attr, const ibValue& value)
{
	m_writeValues.emplace_back(attr, value);
	return *this;
}

bool ibDataQueryBuilder::WriteRow(WriteKind kind, const wxString& table,
                                 const wxString& keyColumn, const ibValue& keyValue,
                                 const std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>>& assignments,
                                 const std::vector<const ibValueMetaObjectAttributeBase*>& matchKeyAttrs,
                                 ibDatabaseConnectionHolder* holder)
{
	// Lower the L3 write kind to the L2 statement kind — the ONLY place the two
	// meet, kept inside the .cpp so the public surface never names the L2 type.
	const ibQueryStatement::Kind l2kind =
		kind == WriteKind::Upsert ? ibQueryStatement::Kind::Upsert
		: kind == WriteKind::Delete ? ibQueryStatement::Kind::Delete
		                            : ibQueryStatement::Kind::Insert;

	// Column model — owned HERE, never at the call site: a 1->1 key column
	// (uuid) plus 1->N attribute columns (each expanded to its TYPE + per-type
	// data + reference fields). The field split is internal.
	std::vector<wxString> columns;
	if (!keyColumn.empty())
		columns.push_back(keyColumn);
	for (const auto& a : assignments)
		for (const wxString& field : SplitSqlFieldNames(ibValueMetaObjectAttributeBase::GetSQLFieldName(a.first)))
			columns.push_back(field);

	std::vector<wxString> matchKeys;
	if (kind == WriteKind::Upsert) {
		if (!keyColumn.empty()) matchKeys.push_back(keyColumn);
		for (const auto attr : matchKeyAttrs)
			for (const wxString& field : SplitSqlFieldNames(ibValueMetaObjectAttributeBase::GetSQLFieldName(attr)))
				matchKeys.push_back(field);
	}

	// Positional binding — also owned here. The key (1->1) binds first as a
	// string; every attribute (1->N) decomposes through SetValueAttribute.
	ibQueryStatement statement(l2kind, table, columns, matchKeys, holder);
	int position = 1;
	if (!keyColumn.empty())
		statement.SetParamString(position++, keyValue.GetString());
	for (const auto& a : assignments)
		ibValueMetaObjectAttributeBase::SetValueAttribute(a.first, a.second, &statement, position);

	try { statement.RunQuery(); return true; }
	catch (...) { return false; }
}

std::vector<ibQuerySortItem> ibDataQueryBuilder::EffectiveSort(
	const ibBackendQueryable* meta, const std::vector<ibQuerySortItem>& userSorts)
{
	// Effective order = user sort ++ the queryable's identity tail (recorder+line
	// / period?+dims for registers; the row-key sentinel for catalogs). The
	// metaobject decides its identity; L3 just appends what is not already in the
	// user sort, so the cursor has a TOTAL order with no family fork.
	std::vector<ibQuerySortItem> effective = userSorts;
	bool hasKeyTail = false;
	for (const ibQuerySortItem& s : userSorts)
		if (s.m_attr == nullptr) hasKeyTail = true;

	for (const ibQuerySortItem& id : meta->GetIdentitySort()) {
		if (id.m_attr == nullptr) {
			if (!hasKeyTail) { effective.push_back(id); hasKeyTail = true; }
			continue;
		}
		bool dup = false;
		for (const ibQuerySortItem& e : effective)
			if (e.m_attr != nullptr && e.m_attr->GetMetaID() == id.m_attr->GetMetaID()) { dup = true; break; }
		if (!dup) effective.push_back(id);
	}
	return effective;
}

// ==========================================================================
// ibDbTableProvider — the query provider for a real DB table (the first and, today,
// only provider behind ibBackendQueryProvider). It owns the name-substitution
// lowering (BuildPageIR via ibMetaIRBuilder) and runs it through L2. Created per
// read from the querybuilder's state; the virtual / temp providers join it behind
// the interface later (docs §22). Behaviour is byte-identical to the door's former
// inline Select — this is pure relocation (migration step 1).
// ==========================================================================
namespace {
class ibDbTableProvider : public ibBackendQueryProvider {
public:
	ibDbTableProvider(ibDatabaseConnectionHolder* holder,
	                  const ibBackendQueryable* meta,
	                  const std::vector<ibQueryCondition>& conditions,
	                  const std::vector<ibValue>& keyIn,
	                  const std::vector<ibQuerySortItem>& sorts,
	                  const std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>>& writeValues,
	                  const std::vector<ibDotWalkColumn>& dotWalks)
		: m_holder(holder), m_meta(meta), m_conditions(conditions), m_keyIn(keyIn),
		  m_sorts(sorts), m_writeValues(writeValues), m_dotWalks(dotWalks)
	{
	}

	ibDataQueryResult ExecuteRead(const ibReadPageRequest& req) override
	{
		const std::vector<ibQuerySortItem> effective = ibDataQueryBuilder::EffectiveSort(m_meta, m_sorts);
		const ibQueryIR            ir       = BuildPageIR(req, effective);
		const std::vector<ibValue> external = BuildExternal(req, effective);

		// Run + open the selection. The L2 cursor moves into the L3 selection; its
		// dtor releases the holder reservation when the selection dies.
		ibDatabaseQueryBuilder q(m_holder);
		return ibDataQueryResult(q.ExecuteIR(ir, external), m_meta);
	}

	// Build-once render-cache path (a DB-provider optimisation, not on the
	// interface): resolve identity + build IR + render only on a signature change.
	ibDataQueryResult ExecuteReadCached(const ibReadPageRequest& req,
	                                    ibRenderedPageCache& cache, const wxString& signature)
	{
		if (!cache.m_valid || cache.m_sig != signature) {
			cache.m_effectiveSort = ibDataQueryBuilder::EffectiveSort(m_meta, m_sorts);
			const ibQueryIR ir = BuildPageIR(req, cache.m_effectiveSort);
			ibDatabaseQueryBuilder qr(m_holder);
			cache.m_rendered = qr.Render(ir);          // borrows only for the dialect
			cache.m_sig      = signature;
			cache.m_valid    = true;
		}

		const std::vector<ibValue> external = BuildExternal(req, cache.m_effectiveSort);

		ibDatabaseQueryBuilder q(m_holder);
		return ibDataQueryResult(q.ExecuteRendered(cache.m_rendered, external), m_meta);
	}

	// Write the row keyed by `rowKey`: Upsert binds the accumulated SetValue()s,
	// Delete binds nothing. The 1->1 row-key column is the match key; the write
	// core (WriteRow) owns the field decomposition + the dialect UPSERT/DELETE.
	bool ExecuteWrite(ibDataQueryBuilder::WriteKind kind, const ibGuid& rowKey) override
	{
		static const std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>> kNoAssignments;
		return ibDataQueryBuilder::WriteRow(kind, m_meta->GetQueryTableName(),
			m_meta->GetRowKeyColumn(), ibValue(wxString(rowKey)),
			kind == ibDataQueryBuilder::WriteKind::Upsert ? m_writeValues : kNoAssignments,
			{}, m_holder);
	}

private:
	// Generate L2 by substituting names. Predicates AND-fold in the renderer, so
	// each clause is an independent q.Where(): parent (tree) + conditions + anchor.
	// Build() is connection-free — no borrow happens here.
	ibQueryIR BuildPageIR(const ibReadPageRequest& req,
	                      const std::vector<ibQuerySortItem>& effective) const
	{
		const bool hasAnchor  = req.m_hasAnchor && !effective.empty();
		const wxString mainTable = m_meta->GetQueryTableName();
		const bool hasDotWalk = !m_dotWalks.empty();
		// Qualify the main columns ONLY when a dot-walk join is present (else the
		// row-key / fields stay bare — byte-identical to the no-join path).
		const wxString mainQual = hasDotWalk ? mainTable : wxString();

		ibDatabaseQueryBuilder q(m_holder);

		if (hasDotWalk) {
			// FROM join-tree + dot-walk projection. main.* keeps the row's own columns
			// (materialised by name as before); each leaf is target.<field> AS alias,
			// read via GetColumn(alias). One LEFT JOIN per distinct reference-path prefix
			// (a null reference still yields the main row; shared prefixes reuse a join).
			ibQueryRelPtr from = ibScan(mainTable);
			std::vector<ibQueryProjItem> projection;
			projection.push_back(ibQueryProjItem{ ibCol(mainTable, wxT("*")), wxString() });

			std::map<wxString, wxString> prefixAlias;   // path-prefix key -> join alias
			int aliasSeq = 0;
			for (const ibDotWalkColumn& dw : m_dotWalks) {
				const ibBackendQueryable* curQ = m_meta;
				wxString curQual = mainTable;
				wxString prefixKey;
				bool ok = true;
				for (size_t i = 0; i + 1 < dw.m_path.size(); ++i) {
					const ibBackendQueryColumn* refCol = dw.m_path[i];
					const ibBackendQueryable* tgtQ = (curQ != nullptr) ? curQ->ResolveReferenceTarget(refCol) : nullptr;
					if (tgtQ == nullptr || tgtQ->GetReferenceKeyColumn().empty()) { ok = false; break; }
					prefixKey += wxString::Format(wxT("%p|"), (const void*)refCol);
					auto it = prefixAlias.find(prefixKey);
					wxString alias;
					if (it == prefixAlias.end()) {
						alias = wxString::Format(wxT("dw%d"), aliasSeq++);
						prefixAlias[prefixKey] = alias;
						from = ibJoin(from, ibScan(tgtQ->GetQueryTableName(), alias),
							ibBinOp(ibQueryBinOp::Eq,
								ibCol(curQual, FirstSqlFieldOfColumn(refCol)),
								ibCol(alias, tgtQ->GetReferenceKeyColumn())),
							ibQueryJoinType::Left);
					}
					else {
						alias = it->second;
					}
					curQ    = tgtQ;
					curQual = alias;
				}
				if (!ok)
					continue;   // unresolvable segment -> skip this dot-walk column
				const ibBackendQueryColumn* leaf = dw.m_path.back();
				projection.push_back(ibQueryProjItem{ ibCol(curQual, FirstSqlFieldOfColumn(leaf)), dw.m_alias });
			}

			q.From(from);
			q.Project(std::move(projection));
		}
		else {
			q.From(mainTable);
		}

		// Hierarchy: parent-reference filter (tree mode). The parent ref blob is an
		// opaque Const — L2 binds the bytes, metadata-blind.
		if (req.m_parentFilter && !req.m_flatScan)
			q.Where(ibMetaIRBuilder::BuildParentRefPredicate(
				m_meta, req.m_parentRefField, req.m_parentGuid, req.m_isTopLevel, mainQual));

		// User conditions (.Where / .WhereKey) — values ride as Const.
		if (auto predicate = ibMetaIRBuilder::BuildFilterPredicate(m_meta, m_conditions, mainQual))
			q.Where(predicate);

		// Row-key set (.WhereKeyIn) — row-key IN (...) as OR-of-equals.
		if (!m_keyIn.empty())
			if (auto keyPred = ibMetaIRBuilder::BuildKeyInPredicate(m_meta, m_keyIn, mainQual))
				q.Where(keyPred);

		// Keyset cursor anchor — values ride as Param (stable SQL text across ticks).
		if (hasAnchor)
			q.Where(ibMetaIRBuilder::BuildAnchorPredicate(m_meta, effective, req.m_direction, mainQual));

		for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(m_meta, effective, req.m_reverseSort, mainQual))
			q.AddSortKey(key);

		if (req.m_count > 0) q.Limit(req.m_count);   // count <= 0 = unbounded (full scan, e.g. FindValue)
		return q.Build();
	}

	std::vector<ibValue> BuildExternal(const ibReadPageRequest& req,
	                                   const std::vector<ibQuerySortItem>& effective) const
	{
		bool hasKeyTail = false;            // any null-attr (row-key) item in the effective order
		for (const ibQuerySortItem& s : effective)
			if (s.m_attr == nullptr) hasKeyTail = true;

		const bool hasAnchor = req.m_hasAnchor && !effective.empty();

		// External binds for the anchor Params, in the order BuildAnchorPredicate
		// assigned: [ sortValue_0 .. sortValue_{K-1}, rowKeyValue? ]. One value per
		// non-null-attr effective column; the row-key value (catalog) is appended
		// only when the effective order has a key tail. Empty when no anchor —
		// filter Consts + the parent-ref blob bind themselves.
		std::vector<ibValue> params;
		if (hasAnchor) {
			params = req.m_anchorSortValues;
			if (hasKeyTail)
				params.push_back(ibValue(req.m_anchorGuid));
		}
		return params;
	}

	ibDatabaseConnectionHolder*           m_holder;
	const ibBackendQueryable*             m_meta;
	const std::vector<ibQueryCondition>&  m_conditions;
	const std::vector<ibValue>&           m_keyIn;
	const std::vector<ibQuerySortItem>&   m_sorts;
	const std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>>& m_writeValues;
	const std::vector<ibDotWalkColumn>&   m_dotWalks;
};

// ibComputedProvider — the provider for a COMPUTED virtual table (register slice /
// balance / turnover). It produces the rows in RAM from the conditions, with no
// physical scan; the result it returns is RAM-backed. Read-only (ExecuteWrite stays
// the base's false). Same ibBackendQueryProvider interface as the DB provider, so
// the door runs it identically and never learns the backing.
class ibComputedProvider : public ibBackendQueryProvider {
public:
	ibComputedProvider(const ibBackendQueryable* meta, const std::vector<ibQueryCondition>& conditions)
		: m_meta(meta), m_conditions(conditions) {}

	ibDataQueryResult ExecuteRead(const ibReadPageRequest& /*req*/) override
	{
		return ibDataQueryResult(m_meta->ComputeRows(m_conditions), m_meta);
	}

private:
	const ibBackendQueryable*            m_meta;
	const std::vector<ibQueryCondition>& m_conditions;
};

// The ONE place the read backing is chosen — a computed queryable gets the RAM
// provider, everything else the DB-table scan. Both return through the same
// ibBackendQueryProvider, so every door terminal below executes BLIND: neither the
// door nor the selection it returns learns whether the rows came from a DB cursor
// or a RAM table. (docs/query-language-arc.md §22.4d — L3 is backing-agnostic.)
static std::unique_ptr<ibBackendQueryProvider> MakeProvider(
	ibDatabaseConnectionHolder* holder, const ibBackendQueryable* meta,
	const std::vector<ibQueryCondition>& conditions, const std::vector<ibValue>& keyIn,
	const std::vector<ibQuerySortItem>& sorts,
	const std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>>& writeValues,
	const std::vector<ibDotWalkColumn>& dotWalks)
{
	if (meta != nullptr && meta->IsComputedInRam())
		return std::make_unique<ibComputedProvider>(meta, conditions);
	return std::make_unique<ibDbTableProvider>(holder, meta, conditions, keyIn, sorts, writeValues, dotWalks);
}
} // namespace

// Every terminal goes through MakeProvider → ExecuteRead/ExecuteWrite. The door is
// execution-free AND backing-blind: it asks for a provider and runs it.
ibDataQueryResult ibDataQueryBuilder::Select(const ibReadPageRequest& req) const
{
	return MakeProvider(m_holder, m_meta, m_conditions, m_keyIn, m_sorts, m_writeValues, m_dotWalks)->ExecuteRead(req);
}

ibDataQueryResult ibDataQueryBuilder::Select(const ibReadPageRequest& req,
                                             ibRenderedPageCache& cache,
                                             const wxString& signature) const
{
	// The build-once render cache is a physical-scan optimisation (it stores rendered
	// SQL), so it is a DB-provider entry — paged list scrolling, the only caller, is
	// always a physical source. A computed table has no SQL to cache and uses plain
	// Select. Constructing the DB provider here is not a backing decision: this
	// overload IS the DB fast path.
	ibDbTableProvider provider(m_holder, m_meta, m_conditions, m_keyIn, m_sorts, m_writeValues, m_dotWalks);
	return provider.ExecuteReadCached(req, cache, signature);
}

// Ordered / LIKE filter op -> IR binary operator (HAVING comparisons).
static ibQueryBinOp FilterOpToBinOp(ibQueryFilterOp op)
{
	switch (op) {
	case ibQueryFilterOp::Like:         return ibQueryBinOp::Like;
	case ibQueryFilterOp::Less:         return ibQueryBinOp::Lt;
	case ibQueryFilterOp::LessEqual:    return ibQueryBinOp::Le;
	case ibQueryFilterOp::Greater:      return ibQueryBinOp::Gt;
	case ibQueryFilterOp::GreaterEqual: return ibQueryBinOp::Ge;
	}
	return ibQueryBinOp::Eq;
}

// Aggregate function -> SQL name.
static wxString AggregateFnName(ibDataQueryBuilder::AggregateFn fn)
{
	switch (fn) {
	case ibDataQueryBuilder::AggregateFn::Sum:   return wxT("SUM");
	case ibDataQueryBuilder::AggregateFn::Count: return wxT("COUNT");
	case ibDataQueryBuilder::AggregateFn::Min:   return wxT("MIN");
	case ibDataQueryBuilder::AggregateFn::Max:   return wxT("MAX");
	case ibDataQueryBuilder::AggregateFn::Avg:   return wxT("AVG");
	}
	return wxT("SUM");
}

// Aggregated read (ИТОГИ) — build the GROUP BY query directly (no paging) and run.
// Group columns are SELECTed (so GetValue(attr) reads them); aggregates become
// SUM/COUNT/… AS alias (read via GetColumn(alias)). attr == null => COUNT(*).
ibDataQueryResult ibDataQueryBuilder::SelectAggregate() const
{
	ibDatabaseQueryBuilder q(m_holder);
	q.From(m_meta->GetQueryTableName());

	if (auto predicate = ibMetaIRBuilder::BuildFilterPredicate(m_meta, m_conditions))
		q.Where(predicate);

	std::vector<ibQueryProjItem> projection;
	for (const ibValueMetaObjectAttributeBase* gattr : m_groupBy) {
		for (const wxString& field : SplitSqlFieldNames(ibValueMetaObjectAttributeBase::GetSQLFieldName(gattr))) {
			q.GroupBy(ibCol(field));
			projection.push_back(ibQueryProjItem{ ibCol(field), wxString() });
		}
	}
	for (const AggregateItem& a : m_aggregates) {
		std::vector<ibQueryExprPtr> args;
		args.push_back(a.m_attr != nullptr ? ibCol(FirstSqlField(a.m_attr)) : ibCol(wxT("*")));
		projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
	}
	q.Project(std::move(projection));

	// HAVING — aggregate-expression predicates, AND-folded (post-aggregation filter).
	ibQueryExprPtr having;
	for (const HavingItem& h : m_having) {
		std::vector<ibQueryExprPtr> args;
		args.push_back(h.m_attr != nullptr ? ibCol(FirstSqlField(h.m_attr)) : ibCol(wxT("*")));
		ibQueryExprPtr cmp = ibBinOp(FilterOpToBinOp(h.m_op),
			ibFunc(AggregateFnName(h.m_fn), std::move(args)), ibConst(h.m_value));
		having = having ? ibBinOp(ibQueryBinOp::And, having, cmp) : cmp;
	}
	if (having) q.Having(having);

	for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(m_meta, m_sorts, /*reverse*/ false))
		q.AddSortKey(key);

	return ibDataQueryResult(q.Execute(), m_meta);
}

// Write terminals — symmetric to Select: build the provider for the source and
// delegate. The provider owns the write (table / row-key / dialect UPSERT-DELETE);
// a computed source's provider is read-only (ExecuteWrite returns false).
bool ibDataQueryBuilder::Upsert(const ibGuid& rowKey) const
{
	return MakeProvider(m_holder, m_meta, m_conditions, m_keyIn, m_sorts, m_writeValues, m_dotWalks)
		->ExecuteWrite(WriteKind::Upsert, rowKey);
}

bool ibDataQueryBuilder::DeleteByKey(const ibGuid& rowKey) const
{
	return MakeProvider(m_holder, m_meta, m_conditions, m_keyIn, m_sorts, m_writeValues, m_dotWalks)
		->ExecuteWrite(WriteKind::Delete, rowKey);
}

// ==========================================================================
// ibMetaResultSource — the selection's backing. Two implementations, NEVER mixed:
// a physical DB cursor and a computed RAM table. The selection forwards to the
// source and never branches on which — so L3 is blind to RAM vs DB, top to bottom
// (the door picks the provider, the provider builds the source, the result just
// walks it). (docs/query-language-arc.md §22.4d)
// ==========================================================================
class ibMetaResultSource {
public:
	virtual ~ibMetaResultSource() = default;
	virtual bool     Next()                                                   = 0;
	virtual wxString GuidString()                                       const = 0;
	virtual ibValue  Value(const ibValueMetaObjectAttributeBase* attr)  const = 0;
	virtual ibValue  Column(const wxString& alias)                      const = 0;   // by output name (aggregates)
};

namespace {

// Physical scan — walks the L2 cursor, materialises through the queryable.
class ibDbResultSource : public ibMetaResultSource {
public:
	ibDbResultSource(ibQueryResult&& cursor, const ibBackendQueryable* meta)
		: m_cursor(std::move(cursor)), m_meta(meta) {}

	bool Next() override { return m_cursor.Next(); }

	wxString GuidString() const override {
		ibDatabaseResultSet* rs = m_cursor.RawResultSet();
		// The queryable knows its own row-key shape (catalog: guidName; register: none).
		return rs != nullptr ? m_meta->MaterializeRowKey(rs) : wxString();
	}

	ibValue Value(const ibValueMetaObjectAttributeBase* attr) const override {
		ibDatabaseResultSet* rs = m_cursor.RawResultSet();
		// The reference attribute yields the row's reference object too, so callers
		// loop every attribute through one accessor.
		return rs != nullptr ? m_meta->MaterializeAttribute(attr, rs) : ibValue();
	}

	// Aggregate column by its output alias — the normalised value straight off the
	// L2 cursor (no source attribute to materialise through).
	ibValue Column(const wxString& alias) const override { return m_cursor.GetValue(alias); }

private:
	mutable ibQueryResult     m_cursor;   // mutable: ibQueryResult::GetValue is non-const (observational read)
	const ibBackendQueryable* m_meta;
};

// Computed virtual table — walks a RAM table of already-materialised ibValues.
class ibRamResultSource : public ibMetaResultSource {
public:
	explicit ibRamResultSource(ibValue table) : m_table(std::move(table)) {
		m_table.ConvertToValue(m_rows);   // borrow the raw view (m_table owns it)
	}

	bool Next() override { return m_rows != nullptr && ++m_row < m_rows->GetRowCount(); }

	// A computed virtual table has no row-key (register identity is composite).
	wxString GuidString() const override { return wxString(); }

	ibValue Value(const ibValueMetaObjectAttributeBase* attr) const override {
		if (m_rows == nullptr || m_row < 0)
			return ibValue();
		// The compute added each column with SetColumnID = the attribute's metaID.
		ibValue out;
		m_rows->GetValueByMetaID(m_rows->GetItem(m_row), attr->GetMetaID(), out);
		return out;
	}

	// A computed virtual table's column by its NAME — a register balance / turnover
	// carries DERIVED columns (Balance / Receipt / Expense) that are not register
	// attributes, so they are read here by name (the dimensions still read by metaID
	// through Value()). Finds the column in the RAM table's collection and reads the
	// current row's value at its id.
	ibValue Column(const wxString& alias) const override {
		if (m_rows == nullptr || m_row < 0)
			return ibValue();
		auto* cols = m_rows->GetColumnCollection();
		if (cols == nullptr)
			return ibValue();
		for (unsigned int i = 0; i < cols->GetColumnCount(); ++i) {
			auto* info = cols->GetColumnInfo(i);
			if (info != nullptr && info->GetColumnName() == alias) {
				ibValue out;
				m_rows->GetValueByMetaID(m_rows->GetItem(m_row), info->GetColumnID(), out);
				return out;
			}
		}
		return ibValue();
	}

private:
	ibValue            m_table;          // owns the computed rows
	ibValueModelTable* m_rows = nullptr; // borrowed raw view; survives a move (the table is not relocated)
	long               m_row  = -1;
};

} // namespace

// ==========================================================================
// ibDataQueryResult — the L3 selection: a thin value handle over a polymorphic
// ibMetaResultSource. Ready ibValue rows out; the backing is hidden even from
// this class (it only ever forwards). Special members out-of-line where the
// source types are complete.
// ==========================================================================
ibDataQueryResult::ibDataQueryResult(ibQueryResult&& cursor, const ibBackendQueryable* meta)
	: m_source(std::make_unique<ibDbResultSource>(std::move(cursor), meta))
{
}

ibDataQueryResult::ibDataQueryResult(ibValue ramTable, const ibBackendQueryable* /*meta*/)
	: m_source(std::make_unique<ibRamResultSource>(std::move(ramTable)))
{
}

ibDataQueryResult::~ibDataQueryResult() = default;
ibDataQueryResult::ibDataQueryResult(ibDataQueryResult&&) noexcept = default;
ibDataQueryResult& ibDataQueryResult::operator=(ibDataQueryResult&&) noexcept = default;

bool     ibDataQueryResult::Next()                                                     { return m_source->Next(); }
wxString ibDataQueryResult::GetGuidString()                                      const { return m_source->GuidString(); }
ibValue  ibDataQueryResult::GetValue(const ibValueMetaObjectAttributeBase* attr) const { return m_source->Value(attr); }
ibValue  ibDataQueryResult::GetColumn(const wxString& alias)                     const { return m_source->Column(alias); }

// Factory for the opaque build-once cache (defined where ibRenderedPageCache is
// complete) — the list model owns the result via shared_ptr.
std::shared_ptr<ibRenderedPageCache> ibDataQueryBuilder::NewPageCache()
{
	return std::make_shared<ibRenderedPageCache>();
}
