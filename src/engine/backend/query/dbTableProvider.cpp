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
#include <vector>
#include <algorithm>    // stable_sort — ROLLUP rows by level for parent-before-child tree assembly

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
// when a dot-walk join is present. (Declared here, before the co-located helpers that call it.)
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

// Read a SCALAR output column of a co-located join row off the L2 cursor by its projection
// ALIAS (SELECT qual.field AS alias). The getter is chosen by the column's declared type — a
// single projected field, no _TYPE/_RRRef spread, so this covers primitive + raw columns only
// (reference / enum columns are excluded upstream by CanColocateJoin, which falls back to RAM).
// Mirrors GetValueColumn's assignment style (the const-ptr operator= trap is moot for real scalars).
ibValue ReadScalarByAlias(const ibBackendQueryColumn* col, const wxString& alias, ibQueryResult& cursor)
{
	ibValue v;
	if (col->IsRawColumn()) {
		switch (static_cast<const ibRawDBColumn*>(col)->GetRawType()) {
		case ibRawDBColumn::RawType::Number:  v = cursor.GetResultNumber(alias); break;
		case ibRawDBColumn::RawType::Date:    v = cursor.GetResultDate(alias);   break;
		case ibRawDBColumn::RawType::Boolean: v = cursor.GetResultBool(alias);   break;
		default:                              v = cursor.GetResultString(alias); break;   // String / Binary
		}
		return v;
	}
	const ibTypeDescription& td = col->GetTypeDesc();
	if      (td.ContainType(ibValueTypes::TYPE_NUMBER))  v = cursor.GetResultNumber(alias);
	else if (td.ContainType(ibValueTypes::TYPE_DATE))    v = cursor.GetResultDate(alias);
	else if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) v = cursor.GetResultBool(alias);
	else                                                 v = cursor.GetResultString(alias);
	return v;
}

// --- shared co-location gate (the JOIN read fast path AND the aggregate fast path) -------------

// --- auto-join key derivation (Join(b) without explicit on-columns) --------------------------------
// A source's self-reference column (the front of GetPrimaryKeyColumns — its data-reference _RRRef) —
// the auto-join binds a referencing column to THIS.
const ibBackendQueryColumn* ColocatedSelfRef(const ibBackendQueryable* q)
{
	const std::vector<const ibBackendQueryColumn*> keys = q->GetPrimaryKeyColumns();
	return keys.empty() ? nullptr : keys.front();
}

// A column of `from` whose reference resolves to `to` — the referencing side of an auto-join.
const ibBackendQueryColumn* ColocatedRefColumnTo(const ibBackendQueryable* from, const ibBackendQueryable* to)
{
	for (const ibBackendQueryColumn* c : from->GetColumns())
		if (c != nullptr && from->ResolveReferenceTarget(c) == to) return c;
	return nullptr;
}

// ONE join node's keys: explicit on-columns, else DERIVED by reference — derivation only when BOTH
// children are Source leaves (a referencing column on one matched to the other's self-reference); a
// node with a sub-join child needs explicit keys. False when neither given nor derivable.
bool ResolveNodeKeys(const ibQueryNode* node, const ibBackendQueryColumn*& onL, const ibBackendQueryColumn*& onR)
{
	onL = node->m_onLeft; onR = node->m_onRight;
	if (onL != nullptr && onR != nullptr) return true;
	const ibQueryNode* nL = node->m_left.get();
	const ibQueryNode* nR = node->m_right.get();
	if (nL == nullptr || nR == nullptr ||
	    nL->m_kind != ibQueryNode::Kind::Source || nR->m_kind != ibQueryNode::Kind::Source)
		return false;
	const ibBackendQueryable* qL = nL->m_queryable;
	const ibBackendQueryable* qR = nR->m_queryable;
	if (qL == nullptr || qR == nullptr) return false;
	if (const ibBackendQueryColumn* lref = ColocatedRefColumnTo(qL, qR)) { onL = lref; onR = ColocatedSelfRef(qR); return onR != nullptr; }
	if (const ibBackendQueryColumn* rref = ColocatedRefColumnTo(qR, qL)) { onR = rref; onL = ColocatedSelfRef(qL); return onL != nullptr; }
	return false;
}

// Every JOIN node in the tree has resolvable keys?
bool AllNodeKeysResolvable(const ibQueryNode* node)
{
	if (node == nullptr || node->m_kind != ibQueryNode::Kind::Join) return true;
	const ibBackendQueryColumn* l = nullptr; const ibBackendQueryColumn* r = nullptr;
	return ResolveNodeKeys(node, l, r) &&
	       AllNodeKeysResolvable(node->m_left.get()) && AllNodeKeysResolvable(node->m_right.get());
}

using ColocatedLeaves = std::vector<const ibBackendQueryable*>;

// Collect the Source-leaf queryables of a JOIN tree (DFS). False if any leaf is non-Source, null, or
// COMPUTED (a Union subtree / a computed-temp source) — those are not SQL-co-locatable here.
bool CollectColocatedLeaves(const ibQueryNode* node, ColocatedLeaves& out)
{
	if (node == nullptr) return false;
	if (node->m_kind == ibQueryNode::Kind::Source) {
		if (node->m_queryable == nullptr || node->m_queryable->IsComputedInRam()) return false;
		out.push_back(node->m_queryable);
		return true;
	}
	if (node->m_kind == ibQueryNode::Kind::Join)
		return CollectColocatedLeaves(node->m_left.get(), out) && CollectColocatedLeaves(node->m_right.get(), out);
	return false;
}

// The leaf owning a column (by name); null if none. Tables are distinct (the gate rejects self-join),
// so ownership is unambiguous for qualification.
const ibBackendQueryable* ColocatedOwner(const ColocatedLeaves& leaves, const ibBackendQueryColumn* col)
{
	for (const ibBackendQueryable* q : leaves)
		if (q != nullptr && q->OwnsColumn(col)) return q;
	return nullptr;
}

// Structural gate over the WHOLE join tree (ANY depth, N-way left/right-nested): collect the leaves
// and require ALL are real DB tables, the tables are DISTINCT (self-join is ambiguous in the column
// model -> RAM), every JOIN node's keys resolve, and every WHERE condition is owned by exactly one
// leaf. Fills `leaves`. INNER and LEFT both co-locate (the L3 node carries only Inner/Left).
bool ColocatableJoinTree(const ibDataQuerySpec& spec, ColocatedLeaves& leaves)
{
	const ibQueryNode* root = spec.m_root;
	if (root == nullptr || root->m_kind != ibQueryNode::Kind::Join)
		return false;
	leaves.clear();
	if (!CollectColocatedLeaves(root, leaves)) return false;   // all leaves real DB tables
	if (leaves.size() < 2)                     return false;
	for (size_t i = 0; i < leaves.size(); ++i)                 // distinct tables — no self-join
		for (size_t j = i + 1; j < leaves.size(); ++j)
			if (leaves[i]->GetQueryTableName() == leaves[j]->GetQueryTableName()) return false;
	if (!AllNodeKeysResolvable(root))          return false;
	for (const ibQueryCondition& c : *spec.m_conditions) {
		if (c.m_col == nullptr)                                return false;   // row-key cond ambiguous in a join
		if (ColocatedOwner(leaves, c.m_col) == nullptr)        return false;
	}
	return true;
}

// A column reads back as ONE scalar field (primitive / raw) — no _TYPE/_RRRef spread, so no
// reference / enum (those need value rehydration the co-located fast path does not do).
bool ScalarReadable(const ibBackendQueryColumn* col, const ColocatedLeaves& leaves)
{
	if (col == nullptr)     return false;
	if (col->IsRawColumn()) return true;
	const ibBackendQueryable* q = ColocatedOwner(leaves, col);
	const ibMetaData* m = q != nullptr ? q->GetMetaData() : nullptr;
	if (ColumnHasReference(col, m))                                  return false;   // reference -> rehydration needed
	if (col->GetTypeDesc().ContainType(ibValueTypes::TYPE_ENUM))     return false;   // enum -> variant reconstruction
	return true;
}

// A column's table qualifier = the table of the leaf that owns it (tables distinct -> unambiguous).
wxString ColocatedQual(const ColocatedLeaves& leaves, const ibBackendQueryColumn* col)
{
	const ibBackendQueryable* q = ColocatedOwner(leaves, col);
	return q != nullptr ? q->GetQueryTableName() : wxString();
}

// A column joinable on ONE physical field — the join compares FirstSqlFieldOfColumn on both sides, so
// this is exactly when that single field IS the key. True for a raw column, or a MONOMORPHIC column
// whose value lives in one field: a single primitive type (-> _B/_N/_D/_S), a pure reference (-> the
// _RRRef blob), or an enum (-> _E). A VARIANT / composite (>1 field) is rejected — comparing only the
// first field would mismatch. Looser than ScalarReadable: a reference / enum KEY joins fine (the field
// is compared, not rehydrated), so this gates KEYS while ScalarReadable still gates value OUTPUTS.
bool SingleFieldJoinable(const ibBackendQueryColumn* col, const ColocatedLeaves& leaves)
{
	if (col == nullptr)     return false;
	if (col->IsRawColumn()) return true;
	const ibBackendQueryable* q = ColocatedOwner(leaves, col);
	const ibMetaData* m = q != nullptr ? q->GetMetaData() : nullptr;
	const ibTypeDescription& td = col->GetTypeDesc();
	int fields = 0;
	if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) ++fields;
	if (td.ContainType(ibValueTypes::TYPE_NUMBER))  ++fields;
	if (td.ContainType(ibValueTypes::TYPE_DATE))    ++fields;
	if (td.ContainType(ibValueTypes::TYPE_STRING))  ++fields;
	if (td.ContainType(ibValueTypes::TYPE_ENUM))    ++fields;
	if (ColumnHasReference(col, m))                 ++fields;
	return fields == 1;
}

// Every JOIN node's keys are single-field (resolvable + each side single-field-joinable)?
bool AllNodeKeysSingleField(const ibQueryNode* node, const ColocatedLeaves& leaves)
{
	if (node == nullptr || node->m_kind != ibQueryNode::Kind::Join) return true;
	const ibBackendQueryColumn* l = nullptr; const ibBackendQueryColumn* r = nullptr;
	if (!ResolveNodeKeys(node, l, r)) return false;
	if (!SingleFieldJoinable(l, leaves) || !SingleFieldJoinable(r, leaves)) return false;
	return AllNodeKeysSingleField(node->m_left.get(), leaves) && AllNodeKeysSingleField(node->m_right.get(), leaves);
}

// Build the L2 FROM tree (nested ibJoin) from the L3 join node — RECURSIVE, any depth. A Source ->
// ibScan(table); a Join -> its two children joined on the node's resolved keys, qualified by the
// owning leaf's table. INNER/LEFT per node. The shared co-located FROM both fast paths build over.
ibQueryRelPtr BuildColocatedFrom(const ibQueryNode* node, const ColocatedLeaves& leaves)
{
	if (node->m_kind == ibQueryNode::Kind::Source)
		return ibScan(node->m_queryable->GetQueryTableName());

	ibQueryRelPtr left  = BuildColocatedFrom(node->m_left.get(),  leaves);
	ibQueryRelPtr right = BuildColocatedFrom(node->m_right.get(), leaves);

	const ibBackendQueryColumn* onL = nullptr; const ibBackendQueryColumn* onR = nullptr;
	ResolveNodeKeys(node, onL, onR);   // the gate guaranteed every node resolves
	const ibQueryJoinType jt = (node->m_joinKind == ibQueryJoinKind::Left) ? ibQueryJoinType::Left : ibQueryJoinType::Inner;
	ibQueryExprPtr on = ibBinOp(ibQueryBinOp::Eq,
		ibCol(ColocatedQual(leaves, onL), FirstSqlFieldOfColumn(onL)),
		ibCol(ColocatedQual(leaves, onR), FirstSqlFieldOfColumn(onR)));
	return ibJoin(left, right, on, jt);
}

// The WHERE, partitioned per leaf and AND-folded (each leaf's conditions lowered qualified by its
// table — composite-safe via BuildFilterPredicate). N leaves.
ibQueryExprPtr ColocatedWhere(const ibDataQuerySpec& spec, const ColocatedLeaves& leaves)
{
	ibQueryExprPtr where;
	for (const ibBackendQueryable* q : leaves) {
		std::vector<ibQueryCondition> conds;
		for (const ibQueryCondition& c : *spec.m_conditions)
			if (c.m_col != nullptr && q->OwnsColumn(c.m_col)) conds.push_back(c);
		if (auto p = ibMetaIRBuilder::BuildFilterPredicate(q, conds, q->GetQueryTableName()))
			where = AndFold(where, p);
	}
	return where;
}

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

	// Aggregated read (totals) — a physical GROUP BY built from the spec. The
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

// ==========================================================================
// Co-located server-side JOIN — the multi-source FAST PATH (docs §22.1a). The composer
// asks CanColocateJoin first; on a yes it delegates the whole 2-leaf inner join to ONE
// server-side SELECT here, on a no it keeps its materialise-to-RAM + C++ stitch. So this
// is purely additive: a narrow shape goes server-side, everything else is unchanged.
// ==========================================================================
bool ibDbTableProvider::CanColocateJoin(const ibDataQuerySpec& spec)
	{
		ColocatedLeaves leaves;
		if (!ColocatableJoinTree(spec, leaves))
			return false;

		// This is the plain READ terminal: not an aggregate, no dot-walk / key-in (those have their
		// own paths). The multi-source output IS the explicit select list — non-empty.
		if (!spec.m_groupBy->empty() || !spec.m_aggregates->empty()) return false;
		if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty())     return false;
		if (spec.m_selectCols->empty())                              return false;

		// OUTPUTS: any column is projectable — a RAW column reads one scalar field, a metadata column its
		// FULL spread (reference / enum / variant reconstruct via GetValueColumn). Just require each is
		// owned by a leaf (so it qualifies).
		for (const auto& sc : *spec.m_selectCols)
			if (sc.first == nullptr || ColocatedOwner(leaves, sc.first) == nullptr) return false;
		// Every JOIN node's KEYS single-field (reference / enum ok — one comparable field).
		if (!AllNodeKeysSingleField(spec.m_root, leaves))
			return false;
		return true;
	}

ibDataQueryResult ibDbTableProvider::ExecuteColocatedJoin(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
	{
		ColocatedLeaves leaves;
		ColocatableJoinTree(spec, leaves);   // the gate already validated; refill the leaf set

		ibDatabaseQueryBuilder q(spec.m_holder);
		q.From(BuildColocatedFrom(spec.m_root, leaves));

		// Per output column: a RAW column projects ONE field (read by RawType); a METADATA column
		// projects its FULL physical spread (TYPE + per-type data + _RTRef/_RRRef) under a UNIQUE prefix
		// (ocol<i>), so a reference / enum / variant output reconstructs via GetValueColumn exactly like a
		// single-source read — the unique prefix avoids cross-leaf field-name collisions.
		struct OutPlan { const ibBackendQueryColumn* col; wxString alias; wxString prefix; const ibMetaData* meta; bool raw; };
		std::vector<OutPlan>         plans;
		std::vector<ibQueryProjItem> projection;
		int oi = 0;
		for (const auto& sc : *spec.m_selectCols) {
			const ibBackendQueryColumn* col = sc.first;
			const ibBackendQueryable* owner = ColocatedOwner(leaves, col);
			const wxString    qual = owner != nullptr ? owner->GetQueryTableName() : wxString();
			const ibMetaData* meta = owner != nullptr ? owner->GetMetaData() : nullptr;
			if (col->IsRawColumn()) {
				projection.push_back(ibQueryProjItem{ ibCol(qual, FirstSqlFieldOfColumn(col)), sc.second });
				plans.push_back({ col, sc.second, wxString(), meta, true });
			}
			else {
				const wxString prefix = wxString::Format(wxT("ocol%d"), oi);
				const wxString base   = col->GetPhysicalName();
				for (const wxString& f : WriteFieldsOf(col, meta))
					projection.push_back(ibQueryProjItem{ ibCol(qual, f), prefix + f.Mid(base.length()) });   // <field> AS <prefix><suffix>
				plans.push_back({ col, sc.second, prefix, meta, false });
			}
			++oi;
		}
		q.Project(std::move(projection));

		if (ibQueryExprPtr where = ColocatedWhere(spec, leaves))
			q.Where(where);

		// ORDER BY — each user sort column qualified to its leaf (no keyset tail: a composed join
		// read is one-shot, not a keyset-paged scroll).
		for (const ibQuerySortItem& s : *spec.m_sorts) {
			if (s.m_col == nullptr) continue;
			ibQuerySortKey k;
			k.m_expr = ibCol(ColocatedQual(leaves, s.m_col), FirstSqlFieldOfColumn(s.m_col));
			k.m_dir  = s.m_ascending ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			q.AddSortKey(std::move(k));
		}

		if (page.m_count > 0) q.Limit(page.m_count);

		// Run server-side, assemble each output into the unified RAM table — keyed by the output column's
		// model id (so GetValue(col) works) AND named by its alias (so GetColumn(alias) works). The join +
		// the cross-table filter ran in the DBMS; only the projected result transits.
		ibQueryResult cursor = q.Execute();

		ibQueryRamTable out;
		for (const OutPlan& p : plans)
			out.AddColumn(p.col->GetModelID(), p.alias, p.col->GetTypeDesc());

		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (const OutPlan& p : plans) {
				ibValue v;
				if (p.raw) v = ReadScalarByAlias(p.col, p.alias, cursor);
				else       GetValueColumn(p.prefix, p.col, p.meta, v, cursor);   // reference/enum/variant reassembly
				out.SetCell(r, p.col->GetModelID(), v);
			}
		}
		return ibDataQueryResult(std::move(out), spec.m_queryable);
	}

// ==========================================================================
// Co-located server-side AGGREGATE — the multi-source totals fast path. A 2-leaf join whose
// terminal is GroupBy()/Sum()/Count()/Having() runs the JOIN + the GROUP BY + the aggregates in
// ONE server-side SELECT, instead of materialising both leaves to RAM and folding in C++
// (RamAggregate). Same leaf gate as the read fast path + scalar group keys / aggregate inputs.
// (docs/query-language-arc.md §22.1a, §22.5 step 3 — multi-source)
// ==========================================================================
bool ibDbTableProvider::CanColocateAggregate(const ibDataQuerySpec& spec)
	{
		ColocatedLeaves leaves;
		if (!ColocatableJoinTree(spec, leaves))
			return false;

		if (spec.m_groupBy->empty() && spec.m_aggregates->empty()) return false;   // not an aggregate terminal
		if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty())   return false;

		if (!AllNodeKeysSingleField(spec.m_root, leaves))
			return false;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			if (g == nullptr || ColocatedOwner(leaves, g) == nullptr) return false;   // group key owned by a leaf (scalar OR reference spread)
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
			using Fn = ibDataQueryBuilder::AggregateFn;
			if (a.m_col == nullptr) { if (a.m_fn != Fn::Count) return false; continue; }   // only COUNT takes a null column
			if (!ScalarReadable(a.m_col, leaves)) return false;                            // aggregate INPUT stays scalar (SUM/MIN/… of a value)
		}
		for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having)
			if (h.m_col != nullptr && !ScalarReadable(h.m_col, leaves)) return false;
		return true;
	}

ibDataQueryResult ibDbTableProvider::ExecuteColocatedAggregate(const ibDataQuerySpec& spec)
	{
		ColocatedLeaves leaves;
		ColocatableJoinTree(spec, leaves);
		auto qual = [&](const ibBackendQueryColumn* c) { return ColocatedQual(leaves, c); };

		ibDatabaseQueryBuilder q(spec.m_holder);
		q.From(BuildColocatedFrom(spec.m_root, leaves));

		if (ibQueryExprPtr where = ColocatedWhere(spec, leaves))
			q.Where(where);

		// GROUP BY the group columns + project them. A SCALAR group key is one field (AS g<i>); a
		// REFERENCE / variant key GROUP BYs its FULL spread and projects it under a unique prefix
		// (gcol<i>), reconstructed via GetValueColumn on read — grouping by the reference identity.
		struct GroupPlan { const ibBackendQueryColumn* col; wxString tag; const ibMetaData* meta; bool scalar; };
		std::vector<GroupPlan>       groupPlans;
		std::vector<ibQueryProjItem> projection;
		int gi = 0;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy) {
			const ibBackendQueryable* owner = ColocatedOwner(leaves, g);
			const ibMetaData* meta = owner != nullptr ? owner->GetMetaData() : nullptr;
			if (g->IsRawColumn() || ScalarReadable(g, leaves)) {
				const wxString galias = wxString::Format(wxT("g%d"), gi);
				const ibQueryExprPtr gexpr = ibCol(qual(g), FirstSqlFieldOfColumn(g));
				q.GroupBy(gexpr);
				projection.push_back(ibQueryProjItem{ gexpr, galias });
				groupPlans.push_back({ g, galias, meta, true });
			}
			else {
				const wxString prefix = wxString::Format(wxT("gcol%d"), gi);
				const wxString base   = g->GetPhysicalName();
				for (const wxString& f : WriteFieldsOf(g, meta)) {
					const ibQueryExprPtr fexpr = ibCol(qual(g), f);
					q.GroupBy(fexpr);
					projection.push_back(ibQueryProjItem{ fexpr, prefix + f.Mid(base.length()) });
				}
				groupPlans.push_back({ g, prefix, meta, false });
			}
			++gi;
		}
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(a.m_col != nullptr ? ibCol(qual(a.m_col), FirstSqlFieldOfColumn(a.m_col)) : ibCol(wxT("*")));
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
		}
		q.Project(std::move(projection));

		ibQueryExprPtr having;
		for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(h.m_col != nullptr ? ibCol(qual(h.m_col), FirstSqlFieldOfColumn(h.m_col)) : ibCol(wxT("*")));
			ibQueryExprPtr cmp = ibBinOp(FilterOpToBinOp(h.m_op),
				ibFunc(AggregateFnName(h.m_fn), std::move(args)), ibConst(h.m_value));
			having = having ? ibBinOp(ibQueryBinOp::And, having, cmp) : cmp;
		}
		if (having) q.Having(having);

		ibQueryResult cursor = q.Execute();

		// Materialise into the unified RAM table: group columns keyed by model id (read via the g<i>
		// scalar alias), aggregates keyed by a synthetic id far from any metaID (read by their alias).
		ibQueryRamTable out;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			out.AddColumn(g->GetModelID(), g->GetName(), g->GetTypeDesc());
		const ibMetaID aggBaseId = 0x40000000u;
		{
			ibMetaID aid = aggBaseId;
			for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
				out.AddColumn(aid++, a.m_alias, a.m_col != nullptr ? a.m_col->GetTypeDesc() : ibTypeDescription());
		}

		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (const GroupPlan& gp : groupPlans) {
				ibValue v;
				if (gp.scalar) v = ReadScalarByAlias(gp.col, gp.tag, cursor);
				else           GetValueColumn(gp.tag, gp.col, gp.meta, v, cursor);   // reference / variant group key
				out.SetCell(r, gp.col->GetModelID(), v);
			}
			ibMetaID aid = aggBaseId;
			for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
				using Fn = ibDataQueryBuilder::AggregateFn;
				ibValue v;
				if (a.m_fn == Fn::Min || a.m_fn == Fn::Max)
					v = ReadScalarByAlias(a.m_col, a.m_alias, cursor);     // MIN/MAX keep the input column's type
				else
					v = cursor.GetResultNumber(a.m_alias);                 // SUM / AVG / COUNT -> number
				out.SetCell(r, aid++, v);
			}
		}
		return ibDataQueryResult(std::move(out), spec.m_queryable);
	}

// ==========================================================================
// Co-located server-side UNION — the branches stack as one SQL UNION ALL (docs §22.1b). Each branch
// (a real DB table) gets a SELECT projecting the output columns resolved BY NAME, aligned by position
// under stable u<i> aliases; ORDER BY / LIMIT wrap the union in a subquery (SQL requires it). Scalar
// outputs only (a reference / enum branch column -> RamUnion). All bind values ride as Const nodes, so
// the combined IR renders + binds in one pass.
// ==========================================================================
bool ibDbTableProvider::CanColocateUnion(const ibDataQuerySpec& spec)
	{
		const ibQueryNode* root = spec.m_root;
		if (root == nullptr || root->m_kind != ibQueryNode::Kind::Union || root->m_parts.empty())
			return false;
		if (!spec.m_groupBy->empty() || !spec.m_aggregates->empty()) return false;
		if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty())     return false;
		if (spec.m_selectCols->empty())                              return false;

		for (const auto& part : root->m_parts) {
			const ibQueryNode* p = part.get();
			if (p == nullptr || p->m_kind != ibQueryNode::Kind::Source)            return false;
			if (p->m_queryable == nullptr || p->m_queryable->IsComputedInRam())     return false;
		}
		// Every output column resolves by name in EVERY branch and is SCALAR there.
		for (const auto& sc : *spec.m_selectCols) {
			if (sc.first == nullptr) return false;
			for (const auto& part : root->m_parts) {
				const ibBackendQueryable* q = part->m_queryable;
				const ibBackendQueryColumn* bc = q->ResolveColumnByName(sc.first->GetName());
				if (bc == nullptr) return false;
				ColocatedLeaves one; one.push_back(q);
				if (!ScalarReadable(bc, one)) return false;
			}
		}
		return true;
	}

ibDataQueryResult ibDbTableProvider::ExecuteColocatedUnion(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
	{
		const ibQueryNode* root = spec.m_root;
		const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& outs = *spec.m_selectCols;
		auto uAlias = [](size_t i) { return wxString::Format(wxT("u%d"), static_cast<int>(i)); };

		// Build each branch as SELECT <field AS u<i>> FROM table WHERE <branch conds>, UNION ALL them.
		ibQueryRelPtr unionRel;
		for (const auto& part : root->m_parts) {
			const ibBackendQueryable* q = part->m_queryable;
			ibQueryRelPtr rel = ibScan(q->GetQueryTableName());

			std::vector<ibQueryCondition> conds;
			for (const ibQueryCondition& c : *spec.m_conditions) {
				if (c.m_col == nullptr) continue;
				const ibBackendQueryColumn* bc = q->ResolveColumnByName(c.m_col->GetName());
				if (bc == nullptr) continue;
				ibQueryCondition nc = c; nc.m_col = bc; conds.push_back(nc);
			}
			if (ibQueryExprPtr pred = ibMetaIRBuilder::BuildFilterPredicate(q, conds, wxString()))
				rel = ibFilter(rel, pred);

			std::vector<ibQueryProjItem> proj;
			for (size_t i = 0; i < outs.size(); ++i) {
				const ibBackendQueryColumn* bc = q->ResolveColumnByName(outs[i].first->GetName());
				proj.push_back(ibQueryProjItem{ ibCol(FirstSqlFieldOfColumn(bc)), uAlias(i) });
			}
			rel = ibProject(rel, std::move(proj));

			unionRel = unionRel ? ibUnionAll(unionRel, rel) : rel;
		}

		// ORDER BY / LIMIT wrap the union in a subquery and apply outside (referencing the u<i> aliases).
		ibQueryRelPtr outer = ibSubquery(unionRel, wxT("u"));
		std::vector<ibQuerySortKey> sortKeys;
		for (const ibQuerySortItem& s : *spec.m_sorts) {
			if (s.m_col == nullptr) continue;
			for (size_t i = 0; i < outs.size(); ++i)
				if (outs[i].first == s.m_col) {
					ibQuerySortKey k; k.m_expr = ibCol(uAlias(i));
					k.m_dir = s.m_ascending ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
					sortKeys.push_back(k);
					break;
				}
		}
		if (!sortKeys.empty()) outer = ibSort(outer, sortKeys);
		if (page.m_count > 0)  outer = ibLimit(outer, page.m_count);

		ibDatabaseQueryBuilder q(spec.m_holder);
		ibQueryResult cursor = q.ExecuteIR(ibQueryIR(outer));

		ibQueryRamTable out;
		for (size_t i = 0; i < outs.size(); ++i)
			out.AddColumn(outs[i].first->GetModelID(), outs[i].second, outs[i].first->GetTypeDesc());
		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (size_t i = 0; i < outs.size(); ++i)
				out.SetCell(r, outs[i].first->GetModelID(), ReadScalarByAlias(outs[i].first, uAlias(i), cursor));
		}
		return ibDataQueryResult(std::move(out), spec.m_queryable);
	}

// ==========================================================================
// Totals push-down — GROUP BY ROLLUP (docs/query-language-arc.md §22.1b). The DBMS computes every
// subtotal level (each from raw detail -> correct for COUNT/AVG) + the grand total in ONE pass; we
// read the result + the GROUPING(key) flags and assemble the ibSelectorTree node tree the runtime
// already consumes. Only the aggregated subtotal rows transit — no raw detail.
// ==========================================================================
bool ibDbTableProvider::CanPushRollupTotals(const ibDataQuerySpec& spec)
	{
		// Single-source DB queryable (a multi-source totals goes through the composer's RAM fold).
		if (spec.m_root != nullptr && spec.m_root->m_kind != ibQueryNode::Kind::Source) return false;
		const ibBackendQueryable* q = spec.m_queryable;
		if (q == nullptr || q->IsComputedInRam())                 return false;
		if (spec.m_groupBy->empty())                              return false;
		if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty())  return false;

		// Scalar group keys + aggregate inputs (reference group-by-rollup needs spread GROUP BY -> RAM).
		ColocatedLeaves one; one.push_back(q);
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			if (g == nullptr || !ScalarReadable(g, one)) return false;
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (a.m_col != nullptr && !ScalarReadable(a.m_col, one)) return false;

		// The connected dialect must advertise ROLLUP (FB5 / PG / MySQL8; NOT SQLite -> RAM).
		ibConnectionScope scope(spec.m_holder);
		if (!scope) return false;
		return scope->GetDialect().m_features.m_rollup;
	}

ibSelectorTree ibDbTableProvider::ExecuteRollupTotals(const ibDataQuerySpec& spec)
	{
		const ibBackendQueryable* q = spec.m_queryable;

		// Build the IR: SELECT g<i>, GROUPING(g<i>) AS grp<i>, <agg> AS alias FROM table WHERE …
		//               GROUP BY ROLLUP(g0, g1, …)
		ibQueryRelPtr from = ibScan(q->GetQueryTableName());
		if (ibQueryExprPtr where = ibMetaIRBuilder::BuildFilterPredicate(q, *spec.m_conditions))
			from = ibFilter(from, where);

		std::vector<ibQueryProjItem> projection;
		std::vector<ibQueryExprPtr>  groupKeys;
		std::vector<wxString>        groupAliases, groupingAliases;
		int gi = 0;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy) {
			const wxString       field = FirstSqlFieldOfColumn(g);
			const ibQueryExprPtr gexpr = ibCol(field);
			const wxString       galias   = wxString::Format(wxT("g%d"),   gi);
			const wxString       grpalias = wxString::Format(wxT("grp%d"), gi);
			groupKeys.push_back(gexpr);
			projection.push_back(ibQueryProjItem{ gexpr, galias });
			projection.push_back(ibQueryProjItem{ ibFunc(wxT("GROUPING"), { ibCol(field) }), grpalias });
			groupAliases.push_back(galias);
			groupingAliases.push_back(grpalias);
			++gi;
		}
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(a.m_col != nullptr ? ibCol(FirstSqlFieldOfColumn(a.m_col)) : ibCol(wxT("*")));
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
		}

		ibQueryExprPtr having;
		for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(h.m_col != nullptr ? ibCol(FirstSqlFieldOfColumn(h.m_col)) : ibCol(wxT("*")));
			ibQueryExprPtr cmp = ibBinOp(FilterOpToBinOp(h.m_op),
				ibFunc(AggregateFnName(h.m_fn), std::move(args)), ibConst(h.m_value));
			having = having ? ibBinOp(ibQueryBinOp::And, having, cmp) : cmp;
		}

		ibQueryIR ir(ibAggregate(from, std::move(projection), std::move(groupKeys), having, /*rollup*/ true));
		ibDatabaseQueryBuilder qb(spec.m_holder);
		ibQueryResult cursor = qb.ExecuteIR(ir);

		// Read every ROLLUP row: its group values, its aggregate values, and its LEVEL (= count of
		// GROUPING=0 keys — for ROLLUP they are always a prefix).
		struct RRow { std::vector<ibValue> groups; std::vector<ibValue> aggs; int level; };
		const size_t nGroup = spec.m_groupBy->size();
		std::vector<RRow> rrows;
		while (cursor.Next()) {
			RRow rr; rr.level = 0;
			for (size_t i = 0; i < nGroup; ++i) {
				rr.groups.push_back(ReadScalarByAlias((*spec.m_groupBy)[i], groupAliases[i], cursor));
				if (cursor.GetResultInt(groupingAliases[i]) == 0) ++rr.level;
			}
			for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
				using Fn = ibDataQueryBuilder::AggregateFn;
				rr.aggs.push_back((a.m_fn == Fn::Min || a.m_fn == Fn::Max)
					? ReadScalarByAlias(a.m_col, a.m_alias, cursor)
					: ibValue(cursor.GetResultNumber(a.m_alias)));
			}
			rrows.push_back(std::move(rr));
		}

		// Assemble the tree. Columns = group cols + aggregates IN-PLACE in their own source columns.
		ibSelectorTree tree;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			tree.AddColumn(g->GetModelID(), g->GetName(), g->GetTypeDesc());
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (a.m_col != nullptr) tree.AddColumn(a.m_col->GetModelID(), a.m_col->GetName(), a.m_col->GetTypeDesc());

		// Parent-before-child: process by level ascending (a level-L node's level-(L-1) parent must
		// exist). The grand total (level 0) is the root.
		std::stable_sort(rrows.begin(), rrows.end(), [](const RRow& a, const RRow& b) { return a.level < b.level; });
		std::map<wxString, ibSelectorTree::Node*> nodes;
		nodes[wxString()] = &tree.Root();
		for (const RRow& rr : rrows) {
			wxString key, parentKey;
			for (int i = 0; i < rr.level; ++i) {
				const wxString seg = rr.groups[static_cast<size_t>(i)].GetString() + wxT("\x1f");
				if (i + 1 < rr.level) parentKey += seg;
				key += seg;
			}
			ibSelectorTree::Node* node = nullptr;
			if (rr.level == 0) {
				node = &tree.Root();          // grand total
			}
			else {
				const auto pit = nodes.find(parentKey);
				ibSelectorTree::Node* parent = (pit != nodes.end()) ? pit->second : &tree.Root();
				node = parent->AddChild(rr.level);
				for (int i = 0; i < rr.level; ++i)
					node->m_values[(*spec.m_groupBy)[static_cast<size_t>(i)]->GetModelID()] = rr.groups[static_cast<size_t>(i)];
				nodes[key] = node;
			}
			for (size_t i = 0; i < rr.aggs.size() && i < spec.m_aggregates->size(); ++i)
				if (const ibBackendQueryColumn* ac = (*spec.m_aggregates)[i].m_col)
					node->m_values[ac->GetModelID()] = rr.aggs[i];   // IN-PLACE in the aggregate's own column
		}
		return tree;
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
		if (spec.m_distinct) ir.m_root = ibDistinct(ir.m_root);   // SELECT DISTINCT — SELECT DISTINCT over the read
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

// Public forwarder to the file-local spread (so the temp-table manager reuses the EXACT field
// derivation the DB write/read path uses — same TYPE/_N/_S/_RTRef/_RRRef layout).
std::vector<wxString> ibDbTableProvider::WriteFieldsOf(const ibBackendQueryColumn* col, const ibMetaData* metaData)
{
	return ::WriteFieldsOf(col, metaData);
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
