////////////////////////////////////////////////////////////////////////////
//	Description : ibDbTableProvider — the BIG DB provider's implementation: the
//	              real-table read / cached / aggregate / write engine, the name-
//	              substitution lowering (ibMetaIRBuilder -> physical L2 ibQueryIR),
//	              and the static GET / WRITE value templates (GetValueAttribute /
//	              SetValueAttribute). The ONLY place L2 and the attribute field-
//	              machinery meet for a physical table. Split out of queryProvider.cpp
//	              (which keeps the composer / computed provider / result sources).
//	              See docs/query-language-arc.md §18, §22.
////////////////////////////////////////////////////////////////////////////

#include "dbTableProvider.h"   // ibDbTableProvider + ibRenderedPageCache (+ queryProvider.h / databaseQueryBuilder.h / metaAttributeObject.h)
#include "dataQueryBuilder.h"  // ibDataQueryBuilder::EffectiveSort / ibDataQueryResult / ibReadPageRequest / ibDataQuerySpec / ibDotWalkColumn
#include "resultSource.h"      // ibDataResultSource — the selection backing ibDbResultSource derives

#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject (reference value assembly)
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/valueInfo.h"                                    // ibReference (physical reference blob)
#include "backend/metaData.h"                                     // ibMetaData::GetTypeCtor
#include "backend/objCtor.h"                                      // ibCtorMetaValueType (reference-target resolution)
#include "backend/appData.h"

#include <map>          // dot-walk join dedup

// ==========================================================================
// Name-substitution primitives (ibMetaIRBuilder) — query-native conditions /
// sorts (columns) -> physical query IR. Emits IR, not SQL text; the dialect fork
// and manual binding are gone. See docs/query-language-arc.md §18.
// ==========================================================================

namespace {

// First physical SQL field of a query COLUMN, derived from (physical name, type) —
// the L3 lowering of queryColumn.h, independent of any concrete metaobject attribute
// (an L3 source may be a temp table whose columns are not attributes). It probes the
// primitive types in the SAME FIXED ORDER as GetSQLFieldData (B, N, D, S, E, then
// Reference), so for an attribute column the result is BYTE-IDENTICAL to the former
// attribute-bound FirstSqlField it replaced — load-bearing because the keyset anchor
// must match the FIRST ORDER BY field, which BuildSortKeys emits in exactly this order
// (a composite attribute's declaration order is NOT this order). No primitive present
// => the column is a reference and yields its _RRRef (guid+metaID) blob.
wxString FirstSqlFieldOfColumn(const ibBackendQueryColumn* col)
{
	const wxString base = col->GetPhysicalName();
	const auto& clsids = col->GetTypeDesc().GetClsidList();
	if (clsids.empty())
		return base;
	auto has = [&](ibValueTypes vt) {
		for (const auto c : clsids)
			if (ibValue::GetVTByID(c) == vt) return true;
		return false;
	};
	if (has(ibValueTypes::TYPE_BOOLEAN)) return base + wxT("_B");
	if (has(ibValueTypes::TYPE_NUMBER))  return base + wxT("_N");
	if (has(ibValueTypes::TYPE_DATE))    return base + wxT("_D");
	if (has(ibValueTypes::TYPE_STRING))  return base + wxT("_S");
	if (has(ibValueTypes::TYPE_ENUM))    return base + wxT("_E");
	return base + wxT("_RRRef");   // reference -> the guid+metaID blob
}

// The row-key physical field of a single-key source (catalog / document: the uuid). Read off the
// UNIQUE tail of GetIdentitySort — the identity's last column is the unique tiebreaker, so for a
// catalog it IS the uuid column. No GetRowKeyColumn: the row-key is just the identity tail now,
// the same real-column mechanism a register's composite identity already rides. A register has a
// composite identity and never takes the single-key (row-key condition / key-IN) paths.
wxString RowKeyField(const ibBackendQueryable* queryable)
{
	const std::vector<ibQuerySortItem> ids = queryable->GetIdentitySort();
	return (ids.empty() || ids.back().m_col == nullptr) ? wxString()
	                                                    : FirstSqlFieldOfColumn(ids.back().m_col);
}

// The dot-walk self-reference field — the Reference-typed (_RRRef guid+metaID blob) physical field
// of a queryable's reference key (its data-reference, vended through GetPrimaryKeyColumns). A
// dot-walk binds source.<ref>_RRRef = target.<this>_RRRef, byte-identical. Empty when the source is
// not a reference target (register / constant). Replaces GetReferenceKeyColumn — same type-specific
// field search, now off the one key authority.
wxString ReferenceFieldOf(const ibBackendQueryColumn* refKeyColumn)
{
	const auto* attr = dynamic_cast<const ibValueMetaObjectAttributeBase*>(refKeyColumn);
	if (attr == nullptr)
		return wxString();
	for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attr))
		if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference)
			return field.m_field.m_fieldRefName.m_fieldRefName;
	return wxString();
}

// The self-reference _RRRef field of a dot-walk TARGET queryable — its reference key (the
// data-reference, vended as the front of GetPrimaryKeyColumns for a record) reduced to its
// Reference field. Empty when the source is not a reference target (register / constant). This is
// what GetReferenceKeyColumn used to return, now derived from the single key authority.
wxString SelfReferenceField(const ibBackendQueryable* queryable)
{
	const std::vector<const ibBackendQueryColumn*> keys = queryable->GetPrimaryKeyColumns();
	return keys.empty() ? wxString() : ReferenceFieldOf(keys.front());
}

// Does the column's type admit a REFERENCE value — i.e. its physical spread carries the
// _RTRef/_RRRef reference fields. Column-based equivalent of the former
// ibValueMetaObjectAttributeBase::ContainMetaType(Reference): a clsid in the column's type
// descriptor whose ctor meta-type is Reference. Needs the metadata context (a non-metaobject
// column / a source with no metadata yields false — it has no reference fields). (docs §22.4b)
bool ColumnHasReference(const ibBackendQueryColumn* col, const ibMetaData* metaData)
{
	if (metaData == nullptr)
		return false;
	for (const auto& clsid : col->GetTypeDesc().GetClsidList()) {
		const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
			return true;
	}
	return false;
}

// Physical fields a write column expands to — derived straight from (physical name, type
// descriptor), the SAME spread GetSQLFieldName generates: TYPE + each contained primitive (B/N/D/
// S/E) + the _RTRef/_RRRef reference pair, in SetValueColumn's bind order. A RAW column is its
// single field as-is. No attribute — the layout IS a function of the column's type (uniform in
// practice: 2 columns for a primitive attribute, 3 for a reference).
std::vector<wxString> WriteFieldsOf(const ibBackendQueryColumn* col, const ibMetaData* metaData)
{
	if (col->IsRawColumn())
		return std::vector<wxString>{ col->GetPhysicalName() };

	const ibTypeDescription& td = col->GetTypeDesc();
	const wxString f = col->GetPhysicalName();
	std::vector<wxString> out{ f + wxT("_TYPE") };
	if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) out.push_back(f + wxT("_B"));
	if (td.ContainType(ibValueTypes::TYPE_NUMBER))  out.push_back(f + wxT("_N"));
	if (td.ContainType(ibValueTypes::TYPE_DATE))    out.push_back(f + wxT("_D"));
	if (td.ContainType(ibValueTypes::TYPE_STRING))  out.push_back(f + wxT("_S"));
	if (td.ContainType(ibValueTypes::TYPE_ENUM))    out.push_back(f + wxT("_E"));
	if (ColumnHasReference(col, metaData)) { out.push_back(f + wxT("_RTRef")); out.push_back(f + wxT("_RRRef")); }
	return out;
}

// ibCol with an optional table qualifier — bare when `qualifier` is empty (the
// no-join path), qualified (table.col) when a dot-walk join is present.
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

// Decompose a COLUMN equality into per-physical-field terms, REUSING the write decomposition:
// SetValueColumn spreads the value across the column's fields, a capture-only statement records
// each as a Const node. A multi-field key (composite / variant / reference dimension) thus filters
// on ALL its fields, AND-folded. The statement is never run — a pure value sink. Column-based: the
// field list + the spread both come off the column + the metadata context, no attribute.
ibQueryExprPtr DecomposeEquality(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& value,
                                 const wxString& mainQual = wxEmptyString)
{
	std::vector<wxString> fields = WriteFieldsOf(col, metaData);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int position = 1;
	ibDbTableProvider::SetValueColumn(col, metaData, value, &capture, position);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	ibQueryExprPtr pred;
	for (size_t i = 0; i < fields.size(); ++i) {
		ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
		pred = AndFold(pred, ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, fields[i]), c));
	}
	return pred;
}

// Ordered / LIKE filter op -> IR binary operator.
ibQueryBinOp FilterOpToBinOp(ibQueryFilterOp op)
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
wxString AggregateFnName(ibDataQueryBuilder::AggregateFn fn)
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

// ibMetaIRBuilder — the GENERIC name-substitution primitives that turn query-native
// conditions / sorts (columns) into physical L2 ibQueryIR fragments. Per-family
// knowledge arrives through ibBackendQueryable, so catalog and register share ONE
// keyset with no fork. `mainQual` (default empty) qualifies the MAIN table's columns
// when a dot-walk join is present.
class ibMetaIRBuilder {
public:
	static ibQueryExprPtr BuildFilterPredicate(const ibBackendQueryable* queryable,
	                                           const std::vector<ibQueryCondition>& conditions,
	                                           const wxString& mainQual = wxEmptyString);
	static std::vector<ibQuerySortKey> BuildSortKeys(const ibBackendQueryable* queryable,
	                                                 const std::vector<ibQuerySortItem>& sorts,
	                                                 bool reverse,
	                                                 const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildAnchorPredicate(const ibBackendQueryable* queryable,
	                                           const std::vector<ibQuerySortItem>& sorts,
	                                           ibFetchDirection direction,
	                                           const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildParentRefPredicate(const ibBackendQueryable* queryable,
	                                              const wxString& refDataField,
	                                              const ibGuid& parentGuid,
	                                              bool isTopLevel,
	                                              const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildKeyInPredicate(const ibBackendQueryable* queryable,
	                                          const std::vector<ibValue>& keyValues,
	                                          const wxString& mainQual = wxEmptyString);
};

} // namespace

ibQueryExprPtr ibMetaIRBuilder::BuildFilterPredicate(const ibBackendQueryable* queryable,
                                                     const std::vector<ibQueryCondition>& conditions,
                                                     const wxString& mainQual)
{
	ibQueryExprPtr pred;

	for (const ibQueryCondition& c : conditions) {
		const ibQueryBinOp op =
			c.m_explicitOp ? FilterOpToBinOp(c.m_op)
			               : (c.m_comparison == ibComparisonType::ibComparisonType_Equal
			                  ? ibQueryBinOp::Eq : ibQueryBinOp::Ne);

		ibQueryExprPtr cmp;
		if (c.m_col == nullptr) {
			// Row-key condition — a lookup by the row's own key (uuid, the identity tail), never
			// LIKE. No GetRowKeyColumn: the key field comes off GetIdentitySort like any column.
			cmp = ibBinOp(op, ibColQ(mainQual, RowKeyField(queryable)), ibConst(c.m_value));
		}
		else if (op == ibQueryBinOp::Eq && !c.m_col->IsRawColumn()) {
			// METADATA-column equality — decompose across ALL the column's physical fields (composite
			// / variant safe) via the value-spread. Guarded by !IsRawColumn: a RAW column (e.g. the
			// tabular parent uuid filter) has a single field and falls to the single-field branch
			// below. Column-based: the spread comes off the column + metadata, no attribute cast.
			cmp = DecomposeEquality(c.m_col, queryable->GetMetaData(), c.m_value, mainQual);
		}
		else {
			// RAW column (direct single physical field) OR ordered / inequality / LIKE compare —
			// the field name derives from the column itself (physical, type), no attribute needed.
			cmp = ibBinOp(op, ibColQ(mainQual, FirstSqlFieldOfColumn(c.m_col)), ibConst(c.m_value));
		}
		pred = AndFold(pred, cmp);
	}

	return pred;
}

std::vector<ibQuerySortKey> ibMetaIRBuilder::BuildSortKeys(const ibBackendQueryable* /*queryable*/,
                                                           const std::vector<ibQuerySortItem>& sorts,
                                                           bool reverse,
                                                           const wxString& mainQual)
{
	std::vector<ibQuerySortKey> keys;

	// Every identity / user sort is a REAL column now (catalog's uuid included — no null
	// sentinel, no row-key special pass). Each column self-describes its physical fields
	// (GetSQLFields), so no ResolveAttribute: an attribute column returns its authoritative
	// field list, a temp / raw column its bare field. One uniform pass.
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_col == nullptr) continue;
		const bool asc = reverse ? !s.m_ascending : s.m_ascending;
		for (const wxString& name : s.m_col->GetSQLFields()) {
			ibQuerySortKey k;
			k.m_expr = ibColQ(mainQual, name);
			k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			keys.push_back(std::move(k));
		}
	}

	return keys;
}

ibQueryExprPtr ibMetaIRBuilder::BuildAnchorPredicate(const ibBackendQueryable* /*queryable*/,
                                                     const std::vector<ibQuerySortItem>& sorts,
                                                     ibFetchDirection direction,
                                                     const wxString& mainQual)
{
	const bool forward = (direction == ibFetchDirection::Forward) ||
	                     (direction == ibFetchDirection::Reset);
	const bool inclusiveTail = (direction == ibFetchDirection::Reset);

	struct SortCol {
		const ibBackendQueryColumn* col;
		bool asc;
	};
	// Every identity column is REAL now (the catalog's uuid included) — the LAST one is the
	// unique tiebreaker, so the keyset tail is just the last col, no null-sentinel branch.
	std::vector<SortCol> cols;
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_col == nullptr) continue;
		cols.push_back({ s.m_col, s.m_ascending });
	}

	auto strictOp    = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Gt : ibQueryBinOp::Lt; };
	auto inclusiveOp = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Ge : ibQueryBinOp::Le; };

	auto eqUpTo = [&](size_t kExclusive) -> ibQueryExprPtr {
		ibQueryExprPtr eq;
		for (size_t j = 0; j < kExclusive; ++j)
			eq = AndFold(eq, ibBinOp(ibQueryBinOp::Eq,
			                         ibColQ(mainQual, FirstSqlFieldOfColumn(cols[j].col)),
			                         ibParam(static_cast<int>(j))));
		return eq;
	};

	ibQueryExprPtr predicate;
	for (size_t i = 0; i < cols.size(); ++i) {
		const bool isLast = (i + 1 == cols.size());
		const ibQueryBinOp op =
			(inclusiveTail && isLast) ? inclusiveOp(cols[i].asc) : strictOp(cols[i].asc);
		ibQueryExprPtr clause = AndFold(
			eqUpTo(i),
			ibBinOp(op, ibColQ(mainQual, FirstSqlFieldOfColumn(cols[i].col)), ibParam(static_cast<int>(i))));
		predicate = OrFold(predicate, clause);
	}

	return predicate;
}

ibQueryExprPtr ibMetaIRBuilder::BuildParentRefPredicate(const ibBackendQueryable* queryable,
                                                        const wxString& refDataField,
                                                        const ibGuid& parentGuid,
                                                        bool isTopLevel,
                                                        const wxString& mainQual)
{
	ibReference ref{ queryable->GetQueryMetaID(), ibGuidImpl{} };
	if (!isTopLevel) {
		const auto& be = parentGuid.bytes();
		auto* p = reinterpret_cast<unsigned char*>(&ref.m_guid);
		p[0] = be[3]; p[1] = be[2]; p[2] = be[1]; p[3] = be[0];
		p[4] = be[5]; p[5] = be[4];
		p[6] = be[7]; p[7] = be[6];
		for (int i = 8; i < 16; ++i) p[i] = be[i];
	}
	return ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField),
	               ibConstBlob(&ref, sizeof(ibReference)));
}

ibQueryExprPtr ibMetaIRBuilder::BuildKeyInPredicate(const ibBackendQueryable* queryable,
                                                    const std::vector<ibValue>& keyValues,
                                                    const wxString& mainQual)
{
	ibQueryExprPtr pred;
	const wxString keyField = RowKeyField(queryable);
	for (const ibValue& v : keyValues)
		pred = OrFold(pred, ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, keyField), ibConst(v)));
	return pred;
}

// ==========================================================================

// ==========================================================================
// ibDbTableProvider — the real-table engine. STATELESS: every method reads the
// ibDataQuerySpec the door hands it. It owns the name-substitution lowering and runs
// it through L2. A single static instance serves every DB queryable (GetProvider).
// ==========================================================================
ibDataQueryResult ibDbTableProvider::ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& req)
	{
		const std::vector<ibQuerySortItem> effective =
			ibDataQueryBuilder::EffectiveSort(spec.m_queryable, *spec.m_sorts);
		const ibQueryIR            ir       = BuildPageIR(spec, req, effective);
		const std::vector<ibValue> external = BuildExternal(req, effective);

		ibDatabaseQueryBuilder q(spec.m_holder);
		return ibDataQueryResult(q.ExecuteIR(ir, external), spec.m_queryable);
	}

ibDataQueryResult ibDbTableProvider::ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                                    ibRenderedPageCache& cache, const wxString& signature)
	{
		if (!cache.m_valid || cache.m_sig != signature) {
			cache.m_effectiveSort = ibDataQueryBuilder::EffectiveSort(spec.m_queryable, *spec.m_sorts);
			const ibQueryIR ir = BuildPageIR(spec, req, cache.m_effectiveSort);
			ibDatabaseQueryBuilder qr(spec.m_holder);
			cache.m_rendered = qr.Render(ir);          // borrows only for the dialect
			cache.m_sig      = signature;
			cache.m_valid    = true;
		}

		const std::vector<ibValue> external = BuildExternal(req, cache.m_effectiveSort);

		ibDatabaseQueryBuilder q(spec.m_holder);
		return ibDataQueryResult(q.ExecuteRendered(cache.m_rendered, external), spec.m_queryable);
	}

	// Aggregated read (ИТОГИ) — a physical GROUP BY built from the spec. The
	// AggregateItem / HavingItem are public on the door, so the provider lowers them.
ibDataQueryResult ibDbTableProvider::ExecuteAggregate(const ibDataQuerySpec& spec)
	{
		const ibBackendQueryable* queryable = spec.m_queryable;
		ibDatabaseQueryBuilder q(spec.m_holder);
		q.From(queryable->GetQueryTableName());

		if (auto predicate = ibMetaIRBuilder::BuildFilterPredicate(queryable, *spec.m_conditions))
			q.Where(predicate);

		std::vector<ibQueryProjItem> projection;
		for (const ibBackendQueryColumn* gcol : *spec.m_groupBy) {
			for (const wxString& field : gcol->GetSQLFields()) {   // column self-describes its fields — no ResolveAttribute
				q.GroupBy(ibCol(field));
				projection.push_back(ibQueryProjItem{ ibCol(field), wxString() });
			}
		}
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(a.m_col != nullptr ? ibCol(FirstSqlFieldOfColumn(a.m_col)) : ibCol(wxT("*")));
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
		}
		q.Project(std::move(projection));

		ibQueryExprPtr having;
		for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(h.m_col != nullptr ? ibCol(FirstSqlFieldOfColumn(h.m_col)) : ibCol(wxT("*")));
			ibQueryExprPtr cmp = ibBinOp(FilterOpToBinOp(h.m_op),
				ibFunc(AggregateFnName(h.m_fn), std::move(args)), ibConst(h.m_value));
			having = having ? ibBinOp(ibQueryBinOp::And, having, cmp) : cmp;
		}
		if (having) q.Having(having);

		for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(queryable, *spec.m_sorts, /*reverse*/ false))
			q.AddSortKey(key);

		return ibDataQueryResult(q.Execute(), queryable);
	}

// Bind a write column's value positionally: a RAW column straight by its declared RawType (no
// translation — the uuid guid just goes in as a string); a metadata column via the TYPE-tagged
// SetValueColumn decomposition (over the column's type descriptor + the metadata context). The
// ONLY place SetValueColumn is called.
static void BindWriteValue(ibQueryStatement& stmt, const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& v, int& pos)
{
	if (col->IsRawColumn()) {
		switch (static_cast<const ibRawDBColumn*>(col)->GetRawType()) {
			case ibRawDBColumn::RawType::String:  stmt.SetParamString(pos++, v.GetString());  break;
			case ibRawDBColumn::RawType::Number:  stmt.SetParamNumber(pos++, v.GetNumber());  break;
			case ibRawDBColumn::RawType::Date:    stmt.SetParamDate  (pos++, v.GetDate());    break;
			case ibRawDBColumn::RawType::Boolean: stmt.SetParamBool  (pos++, v.GetBoolean()); break;
			case ibRawDBColumn::RawType::Binary:  stmt.SetParamString(pos++, v.GetString());  break;   // TODO: real blob bind
		}
		return;
	}
	ibDbTableProvider::SetValueColumn(col, metaData, v, &stmt, pos);
}

// The L2 write CORE — buried in the provider. Identity is the WHERE section (spec conditions):
// each condition column (a RAW uuid column, or a register's primary-key attribute) = value.
// The SetValue() data rides ONLY for INSERT / UPSERT; DELETE is WHERE-only. INSERT/UPSERT write
// every assignment column; UPSERT matches on the IsPrimaryKey ones (the raw uuid reports
// IsPrimaryKey, so it both inserts and matches; a register's composite key is several columns).
// Each column expands + binds through WriteFieldsOf / BindWriteValue (raw direct vs attribute
// decomposition). One ibQueryStatement, run once.
bool ibDbTableProvider::ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind)
	{
		using WriteKind = ibDataQueryBuilder::WriteKind;
		const wxString table     = spec.m_queryable->GetQueryTableName();
		const wxString keyColumn = RowKeyField(spec.m_queryable);   // uuid — the single-key DELETE-by-key column (read off the identity tail)
		const ibMetaData* metaData = spec.m_queryable->GetMetaData();   // context for the column-based field spread / bind

		if (kind == WriteKind::Delete) {
			// DELETE ... WHERE <each condition column> = value.
			std::vector<wxString> whereCols;
			for (const ibQueryCondition& c : *spec.m_conditions) {
				if (c.m_col == nullptr) { if (!keyColumn.empty()) whereCols.push_back(keyColumn); }
				else for (const wxString& f : WriteFieldsOf(c.m_col, metaData)) whereCols.push_back(f);
			}
			ibQueryStatement statement(ibQueryStatement::Kind::Delete, table, whereCols, {}, spec.m_holder);
			int position = 1;
			for (const ibQueryCondition& c : *spec.m_conditions) {
				if (c.m_col == nullptr) { if (!keyColumn.empty()) statement.SetParamString(position++, c.m_value.GetString()); }
				else BindWriteValue(statement, c.m_col, metaData, c.m_value, position);
			}
			try { statement.RunQuery(); return true; }
			catch (...) { return false; }
		}

		// INSERT / UPSERT — columns = every SetValue() assignment; UPSERT matches the IsPrimaryKey ones.
		const ibQueryStatement::Kind l2kind =
			(kind == WriteKind::Upsert) ? ibQueryStatement::Kind::Upsert : ibQueryStatement::Kind::Insert;

		std::vector<wxString> columns;
		for (const auto& wv : *spec.m_writeValues)
			for (const wxString& f : WriteFieldsOf(wv.first, metaData)) columns.push_back(f);

		// UPSERT match keys = the source's uniqueness key, OWNED by the queryable through
		// GetPrimaryKeyColumns: a record's data-reference (_RTRef+_RRRef — _RTRef is constant for
		// a monomorphic self-reference, so the match is effectively on the unique _RRRef blob), a
		// register's recorder+line+period / period+dimensions. NOT the uuid (that stays the read
		// keyset / DELETE key, a second link key until cleaned). The conflict target needs a unique
		// index on these fields — see CreateAndUpdateTableDB. Not scanned off the values.
		std::vector<wxString> matchKeys;
		if (kind == WriteKind::Upsert)
			for (const ibBackendQueryColumn* col : spec.m_queryable->GetPrimaryKeyColumns())
				for (const wxString& f : WriteFieldsOf(col, metaData)) matchKeys.push_back(f);

		ibQueryStatement statement(l2kind, table, columns, matchKeys, spec.m_holder);
		int position = 1;
		for (const auto& wv : *spec.m_writeValues)
			BindWriteValue(statement, wv.first, metaData, wv.second, position);

		try { statement.RunQuery(); return true; }
		catch (...) { return false; }
	}

// Generate L2 by substituting names — all read from the spec; Build() is connection-free.
// The dot-walk join-tree + projection, the parent/tree filter, the user conditions, the
// key-in set, the keyset anchor, the sort keys, the limit.
ibQueryIR ibDbTableProvider::BuildPageIR(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                             const std::vector<ibQuerySortItem>& effective)
	{
		const ibBackendQueryable* queryable = spec.m_queryable;
		const bool hasAnchor  = req.m_hasAnchor && !effective.empty();
		const wxString mainTable = queryable->GetQueryTableName();
		const bool hasDotWalk = !spec.m_dotWalks->empty();
		const wxString mainQual = hasDotWalk ? mainTable : wxString();

		ibDatabaseQueryBuilder q(spec.m_holder);

		if (hasDotWalk) {
			ibQueryRelPtr from = ibScan(mainTable);
			std::vector<ibQueryProjItem> projection;
			projection.push_back(ibQueryProjItem{ ibCol(mainTable, wxT("*")), wxString() });

			std::map<wxString, wxString> prefixAlias;   // path-prefix key -> join alias
			int aliasSeq = 0;
			for (const ibDotWalkColumn& dw : *spec.m_dotWalks) {
				const ibBackendQueryable* curQ = queryable;
				wxString curQual = mainTable;
				wxString prefixKey;
				bool ok = true;
				for (size_t i = 0; i + 1 < dw.m_path.size(); ++i) {
					const ibBackendQueryColumn* refCol = dw.m_path[i];
					const ibBackendQueryable* tgtQ = (curQ != nullptr) ? curQ->ResolveReferenceTarget(refCol) : nullptr;
					const wxString tgtRefField = (tgtQ != nullptr) ? SelfReferenceField(tgtQ) : wxString();
					if (tgtQ == nullptr || tgtRefField.empty()) { ok = false; break; }
					prefixKey += wxString::Format(wxT("%p|"), (const void*)refCol);
					auto it = prefixAlias.find(prefixKey);
					wxString alias;
					if (it == prefixAlias.end()) {
						alias = wxString::Format(wxT("dw%d"), aliasSeq++);
						prefixAlias[prefixKey] = alias;
						from = ibJoin(from, ibScan(tgtQ->GetQueryTableName(), alias),
							ibBinOp(ibQueryBinOp::Eq,
								ibCol(curQual, FirstSqlFieldOfColumn(refCol)),
								ibCol(alias, tgtRefField)),
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

		if (req.m_parentFilter && !req.m_flatScan)
			q.Where(ibMetaIRBuilder::BuildParentRefPredicate(
				queryable, req.m_parentRefField, req.m_parentGuid, req.m_isTopLevel, mainQual));

		if (auto predicate = ibMetaIRBuilder::BuildFilterPredicate(queryable, *spec.m_conditions, mainQual))
			q.Where(predicate);

		if (!spec.m_keyIn->empty())
			if (auto keyPred = ibMetaIRBuilder::BuildKeyInPredicate(queryable, *spec.m_keyIn, mainQual))
				q.Where(keyPred);

		if (hasAnchor)
			q.Where(ibMetaIRBuilder::BuildAnchorPredicate(queryable, effective, req.m_direction, mainQual));

		for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(queryable, effective, req.m_reverseSort, mainQual))
			q.AddSortKey(key);

		if (req.m_count > 0) q.Limit(req.m_count);   // count <= 0 = unbounded (full scan, e.g. FindValue)

		ibQueryIR ir = q.Build();
		ir.m_lockForUpdate = req.m_lockForUpdate;   // pessimistic register set lock — dialect appends the clause
		return ir;
	}

// External binds for the anchor Params: [ sortValue_0 .. sortValue_{K-1}, rowKeyValue? ].
std::vector<ibValue> ibDbTableProvider::BuildExternal(const ibReadPageRequest& req,
	                                          const std::vector<ibQuerySortItem>& effective)
	{
		const bool hasAnchor = req.m_hasAnchor && !effective.empty();

		std::vector<ibValue> params;
		if (hasAnchor) {
			params = req.m_anchorSortValues;
			// A single-key source (catalog) captures its identity tail (uuid) SEPARATELY as the
			// anchor guid; a register folds its composite identity into m_anchorSortValues and
			// leaves the guid empty. The non-empty guid IS the tail signal — no null sentinel.
			if (!req.m_anchorGuid.empty())
				params.push_back(ibValue(req.m_anchorGuid));
		}
		return params;
	}

void ibDbTableProvider::SetValueColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData,
	const ibValue& cValue, ibQueryStatement* statement, int& position)
{
	using A = ibValueMetaObjectAttributeBase;   // the ibFieldTypes_* tags are a class-scoped enum (no instance)
	//write type & data
	if (cValue.GetType() == ibValueTypes::TYPE_EMPTY) {

		statement->SetParamInt(position++, A::ibFieldTypes_Empty); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_BOOLEAN) {

		statement->SetParamInt(position++, A::ibFieldTypes_Boolean); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, cValue.GetBoolean()); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_NUMBER) {

		statement->SetParamInt(position++, A::ibFieldTypes_Number); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, cValue.GetNumber()); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_DATE) {

		statement->SetParamInt(position++, A::ibFieldTypes_Date); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, cValue.GetDate()); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_STRING) {

		statement->SetParamInt(position++, A::ibFieldTypes_String); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, cValue.GetString()); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_NULL) {

		statement->SetParamInt(position++, A::ibFieldTypes_Null); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_ENUM) {

		statement->SetParamInt(position++, A::ibFieldTypes_Enum); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, cValue.GetNumber()); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 	
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, cValue.GetInteger()); //DATA enum 

		if (ColumnHasReference(col, metaData)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else {

		statement->SetParamInt(position++, A::ibFieldTypes_Reference); //TYPE

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		const ibClassID& clsid = cValue.GetClassType();
		wxASSERT(clsid > 0);
		wxASSERT(metaData);

		const ibCtorMetaValueType* typeCtor = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
		wxASSERT(typeCtor);

		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference) {
			ibValueReferenceDataObject* refData = nullptr;
			if (cValue.ConvertToValue(refData)) {
				statement->SetParamNumber(position++, clsid); //TYPE REF
				statement->SetParamBlob(position++, refData->GetReferenceData(), sizeof(ibReference)); //DATA REF
			}
			else {
				statement->SetParamNumber(position++, 0); //TYPE REF
				statement->SetParamNull(position++); //DATA REF
			}
		}
		else {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
}

void ibDbTableProvider::SetValueColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& cValue, ibQueryStatement* statement)
{
	int position = 1;
	SetValueColumn(col, metaData, cValue, statement, position);
}

// --- attribute convenience adapters — the attribute IS a column, metaData is its own. ----------
void ibDbTableProvider::SetValueAttribute(const ibValueMetaObjectAttributeBase* attr, const ibValue& cValue, ibQueryStatement* statement, int& position)
{
	SetValueColumn(attr, attr->GetMetaData(), cValue, statement, position);
}

bool ibDbTableProvider::GetValueAttribute(const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return GetValueColumn(attr, attr->GetMetaData(), retValue, result, createData);
}

bool ibDbTableProvider::GetValueAttribute(const wxString& fieldName, ibValueMetaObjectAttributeBase::ibFieldTypes fieldType,
	const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return GetValueColumn(fieldName, fieldType, attr, attr->GetMetaData(), retValue, result, createData);
}

#include "backend/compiler/enumUnit.h"

bool ibDbTableProvider::GetValueColumn(const wxString& fieldName,
	ibValueMetaObjectAttributeBase::ibFieldTypes fieldType, const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	using A = ibValueMetaObjectAttributeBase;
	switch (fieldType)
	{
	case A::ibFieldTypes_Boolean:
		retValue = result.GetResultBool(fieldName);
		return true;
	case A::ibFieldTypes_Number:
		retValue = result.GetResultNumber(fieldName);
		return true;
	case A::ibFieldTypes_Date:
		retValue = result.GetResultDate(fieldName);
		return true;
	case A::ibFieldTypes_String:
		retValue = result.GetResultString(fieldName);
		return true;
	case A::ibFieldTypes_Null:
		retValue = ibValue(ibValueTypes::TYPE_NULL);   // fresh NULL — releases any prior reffer/string in the slot (operator=(ibValueTypes) would leak it)
		return true;
	case A::ibFieldTypes_Enum:
	{
		wxASSERT(metaData);
		// The enum ctor is keyed by the ENUM clsid — taken from the column's type descriptor (was
		// the attribute's CreateValue().GetClassType()). The error fallback yields an empty value
		// (the former typed default needed the attribute; an unreadable enum degrades to empty).
		const ibClassID enumClsid = col->GetTypeDesc().GetFirstClsid();
		const ibCtorAbstractType* so = metaData != nullptr ? metaData->GetAvailableCtor(enumClsid) : nullptr;

		if (so != nullptr) {

			ibValue enumVariant(result.GetResultInt(fieldName));
			ibValue* ppParams[] = { &enumVariant };

			try {
				ibValuePtr<ibValueEnumerationWrapper> creator(
					metaData->CreateAndConvertObjectRef<ibValueEnumerationWrapper>(so->GetClassName(), ppParams, 1));
				retValue = creator->GetEnumVariantValue();
			}
			catch (...) {
				retValue = ibValue();
				return false;
			}

			return true;
		}

		retValue = ibValue();
		return false;
	}
	case A::ibFieldTypes_Reference:
	{
		wxASSERT(metaData);
		if (metaData == nullptr)
			return false;
		const ibClassID refType = static_cast<ibClassID>(result.GetResultLong(fieldName + wxT("_RTRef")));

		wxMemoryBuffer bufferData;
		result.GetResultBlob(fieldName + wxT("_RRRef"), bufferData);
		if (!bufferData.IsEmpty()) {

			if (createData) {

				ibValuePtr<ibValueReferenceDataObject> created_reference(
					ibValueReferenceDataObject::CreateFromPtr(metaData, bufferData.GetData()));

				retValue = created_reference;
				return created_reference != nullptr;
			}

			ibValuePtr<ibValueReferenceDataObject> created_reference(
				ibValueReferenceDataObject::Create(metaData, bufferData.GetData()));

			retValue = created_reference;
			return created_reference != nullptr;
		}
		else if (refType > 0) {

			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(refType);
			if (typeCtor != nullptr) {

				const ibValueMetaObject* metaObject = typeCtor->GetMetaObject();
				wxASSERT(metaObject);

				ibValuePtr<ibValueReferenceDataObject> created_reference(
					ibValueReferenceDataObject::Create(metaData, metaObject->GetMetaID()));

				retValue = created_reference;
				return created_reference != nullptr;
			}

			return false;
		}

		break;
	}
	}

	return false;
}

bool ibDbTableProvider::GetValueColumn(const wxString& fieldName,
	const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	using A = ibValueMetaObjectAttributeBase;
	A::ibFieldTypes fieldType =
		static_cast<A::ibFieldTypes>(result.GetResultInt(fieldName + wxT("_TYPE")));

	switch (fieldType)
	{
	case A::ibFieldTypes_Boolean:
		return ibDbTableProvider::GetValueColumn(fieldName + wxT("_B"), A::ibFieldTypes_Boolean, col, metaData, retValue, result, createData);
	case A::ibFieldTypes_Number:
		return ibDbTableProvider::GetValueColumn(fieldName + wxT("_N"), A::ibFieldTypes_Number, col, metaData, retValue, result, createData);
	case A::ibFieldTypes_Date:
		return ibDbTableProvider::GetValueColumn(fieldName + wxT("_D"), A::ibFieldTypes_Date, col, metaData, retValue, result, createData);
	case A::ibFieldTypes_String:
		return ibDbTableProvider::GetValueColumn(fieldName + wxT("_S"), A::ibFieldTypes_String, col, metaData, retValue, result, createData);
	case A::ibFieldTypes_Null:
		retValue = ibValue(ibValueTypes::TYPE_NULL);   // fresh NULL — releases any prior reffer/string in the slot (operator=(ibValueTypes) would leak it)
		return true;
	case A::ibFieldTypes_Enum:
		return ibDbTableProvider::GetValueColumn(fieldName + wxT("_E"), A::ibFieldTypes_Enum, col, metaData, retValue, result, createData);
	case A::ibFieldTypes_Reference:
		return ibDbTableProvider::GetValueColumn(fieldName, A::ibFieldTypes_Reference, col, metaData, retValue, result, createData);
	default:
		retValue = ibValue();   // unrecognized stored TYPE tag (column type changed) — empty (the former attribute typed default needed the attribute)
		return true;
	}

	return false;
}

bool ibDbTableProvider::GetValueColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return ibDbTableProvider::GetValueColumn(
		col->GetPhysicalName(),
		col, metaData, retValue, result, createData
	);
}

// ==========================================================================
// ibDbResultSource — the DB cursor MATERIALISATION: walk the L2 result, lift each column up
// through the provider's read rule. This is the DB provider's read side, so it lives here
// (moved out of queryProvider.cpp with the rest of ibDbTableProvider). (docs §22.4d)
// ==========================================================================
namespace {

// The default DB provider's column READ RULE — COLUMN-BASED: assemble the value off the column's
// type descriptor + the metadata context (a reference column rebuilds its object from the (clsid,
// blob) fields, an enum its variant), with NO static_cast to the attribute. A balances / hierarchy
// provider OVERRIDES this rule for its derived columns.
ibValue ProviderReadColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData, ibQueryResult& result)
{
	if (col == nullptr)
		return ibValue();
	ibValue v;
	ibDbTableProvider::GetValueColumn(col, metaData, v, result);
	return v;
}

// Physical scan — walks the L2 cursor; each column is lifted via the column-based read rule. It
// holds the metadata context (from the source's queryable) so a reference / enum column can
// reconstruct its value without the attribute. The materialisation lives here, over the L2 result.
class ibDbResultSource : public ibDataResultSource {
public:
	ibDbResultSource(ibQueryResult&& cursor, const ibBackendQueryable* queryable)
		: m_cursor(std::move(cursor)), m_metaData(queryable != nullptr ? queryable->GetMetaData() : nullptr) {}

	bool Next() override { return m_cursor.Next(); }

	ibValue Value(const ibBackendQueryColumn* col) const override {
		if (col == nullptr)
			return ibValue();
		if (col->IsRawColumn()) {
			// RAW column (the row-key uuid; a balance's computed field) — read STRAIGHT off the
			// cursor by its declared RawType, no attribute decomposition. (symmetric with the
			// write side's BindWriteValue; this is how the row-key reads now — no GuidString.)
			const wxString f = col->GetPhysicalName();
			switch (static_cast<const ibRawDBColumn*>(col)->GetRawType()) {
				case ibRawDBColumn::RawType::String:  return ibValue(m_cursor.GetResultString(f));
				case ibRawDBColumn::RawType::Number:  return ibValue(m_cursor.GetResultNumber(f));
				case ibRawDBColumn::RawType::Date:    return ibValue(m_cursor.GetResultDate(f));
				case ibRawDBColumn::RawType::Boolean: return ibValue(m_cursor.GetResultBool(f));
				case ibRawDBColumn::RawType::Binary:  return ibValue(m_cursor.GetResultString(f));   // TODO: real blob
			}
			return ibValue();
		}
		return ProviderReadColumn(col, m_metaData, m_cursor);   // metadata column — column-based assembly
	}

	ibValue Column(const wxString& alias) const override { return m_cursor.GetValue(alias); }

private:
	mutable ibQueryResult m_cursor;   // mutable: ibQueryResult::GetValue is non-const (observational read)
	const ibMetaData*     m_metaData; // the source's metadata context (reference / enum reconstruction)
};

} // namespace

// ibDataQueryResult — the DB-cursor backing ctor (the RAM ctor + dtor / move / accessors live in
// queryProvider.cpp, where the RAM source is). The selection hides which backing it holds.
ibDataQueryResult::ibDataQueryResult(ibQueryResult&& cursor, const ibBackendQueryable* queryable)
	: m_source(std::make_unique<ibDbResultSource>(std::move(cursor), queryable))
{
}
