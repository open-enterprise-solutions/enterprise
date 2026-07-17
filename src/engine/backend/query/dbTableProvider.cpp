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
#include "columnLayout.h"      // the column-layout tier: DescribeColumnLayout + ibColumnCodec (value codec) + HasReference

#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/valueInfo.h"                                    // ibReference (physical reference blob, GetQueryTableId source)
#include "backend/metaData.h"                                     // ibMetaData (threaded through reads/writes)
#include "backend/objCtor.h"                                      // ibCtorMetaValueType::GetQueryable — reference-target resolution (clsid -> ctor -> queryable, no cast)
#include "backend/system/value/valueType.h"                      // ibValueTypeDescription::AdjustValue (dot-walk typed empty)

#include <map>          // dot-walk join dedup
#include <vector>
#include <algorithm>    // stable_sort — ROLLUP rows by level for parent-before-child tree assembly
#include <stdexcept>    // std::logic_error — the un-co-locatable WHERE-tree guard (BuildColocatedPredicate)
#include <functional>   // std::function — the recursive dot-walk predicate-tree lowering in BuildPageIR

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
	// The reference's _RRRef field = the ReferenceId slot in the column's physical layout.
	for (const ibColumnSlot& slot : DescribeColumnLayout(refKeyColumn))
		if (slot.m_role == ibColumnRole::ReferenceId)
			return slot.m_name;
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

// (The "does this column admit a reference value" check moved to the column-layout tier —
//  ibColumnCodec::HasReference, query/columnLayout.h. Callers here use it directly.)

// (WriteFieldsOf removed — it was byte-identical to ColumnFieldNames (columnLayout.h): both are the
//  column's physical field names off the one layout authority, DescribeColumnLayout. Callers use
//  ColumnFieldNames directly so there is a single field-name path, not two.)

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
	std::vector<wxString> fields = ColumnFieldNames(col);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int position = 1;
	ibColumnCodec::WriteValue(col, metaData, value, &capture, position);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	ibQueryExprPtr pred;
	for (size_t i = 0; i < fields.size(); ++i) {
		ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
		pred = AndFold(pred, ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, fields[i]), c));
	}
	return pred;
}

// Decompose a COLUMN ordered compare (>=, <=, >, <) LEXICOGRAPHICALLY over its physical fields, reusing the
// SAME write-spread as DecomposeEquality so the constants are the column's real stored bytes (a reference's
// _RRRef blob, correctly encoded — NOT a mis-rendered ibConst). A single-field key (a plain reference = one
// _RRRef field) is just `field OP const`; a multi-field key compares strict on the leading fields and OP on
// the last. Ordering references this way is consistent with an ORDER BY on the SAME field(s) — the keyset the
// paged cursor needs. Matches the runtime's ibValue reference ordering (same metaObject -> by guid). L3/DB half.
ibQueryExprPtr DecomposeOrdered(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& value,
                                ibQueryBinOp op, const wxString& mainQual = wxEmptyString)
{
	std::vector<wxString> fields = ColumnFieldNames(col);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int position = 1;
	ibColumnCodec::WriteValue(col, metaData, value, &capture, position);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	auto constAt = [&](size_t i) -> ibQueryExprPtr {
		return (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
	};
	const ibQueryBinOp strictOp =
		(op == ibQueryBinOp::Ge || op == ibQueryBinOp::Gt) ? ibQueryBinOp::Gt : ibQueryBinOp::Lt;

	ibQueryExprPtr pred;
	for (size_t i = 0; i < fields.size(); ++i) {
		const bool isLast = (i + 1 == fields.size());
		ibQueryExprPtr eq;
		for (size_t j = 0; j < i; ++j)
			eq = AndFold(eq, ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, fields[j]), constAt(j)));
		pred = OrFold(pred, AndFold(eq, ibBinOp(isLast ? op : strictOp, ibColQ(mainQual, fields[i]), constAt(i))));
	}
	return pred;
}

// L3 filter op -> IR binary operator. Now the ONE op enum (Equal/NotEqual folded in from the retired
// ibComparisonType), so this maps the full set — no separate Eq/Ne path needed.
ibQueryBinOp FilterOpToBinOp(ibQueryFilterOp op)
{
	switch (op) {
	case ibQueryFilterOp::Equal:        return ibQueryBinOp::Eq;
	case ibQueryFilterOp::NotEqual:     return ibQueryBinOp::Ne;
	case ibQueryFilterOp::Like:         return ibQueryBinOp::Like;
	case ibQueryFilterOp::Less:         return ibQueryBinOp::Lt;
	case ibQueryFilterOp::LessEqual:    return ibQueryBinOp::Le;
	case ibQueryFilterOp::Greater:      return ibQueryBinOp::Gt;
	case ibQueryFilterOp::GreaterEqual: return ibQueryBinOp::Ge;
	}
	return ibQueryBinOp::Eq;
}

// L3 join-ON compare op -> IR binary op (1:1 — both enums list Eq/Ne/Lt/Le/Gt/Ge in the same order). Lets a
// co-located column-to-column THETA join render its real operator server-side instead of the forced Eq.
ibQueryBinOp JoinOpToBinOp(ibJoinCompareOp op)
{
	switch (op) {
	case ibJoinCompareOp::Eq: return ibQueryBinOp::Eq;
	case ibJoinCompareOp::Ne: return ibQueryBinOp::Ne;
	case ibJoinCompareOp::Lt: return ibQueryBinOp::Lt;
	case ibJoinCompareOp::Le: return ibQueryBinOp::Le;
	case ibJoinCompareOp::Gt: return ibQueryBinOp::Gt;
	case ibJoinCompareOp::Ge: return ibQueryBinOp::Ge;
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
	// pathAsExists (WRITE path only): a dot-walk condition (m_path) becomes a correlated EXISTS instead of
	// being skipped. Reads pre-resolve the path to a JOIN in BuildPageIR, so they leave it false.
	static ibQueryExprPtr BuildFilterPredicate(const ibBackendQueryable* queryable,
	                                           const std::vector<ibQueryCondition>& conditions,
	                                           const wxString& mainQual = wxEmptyString,
	                                           bool pathAsExists = false);
	// One door condition -> L2 expr (the per-leaf body shared by the AND-fold above and the tree below).
	static ibQueryExprPtr BuildConditionExpr(const ibBackendQueryable* queryable,
	                                         const ibQueryCondition& c,
	                                         const wxString& mainQual = wxEmptyString,
	                                         bool pathAsExists = false);
	// The full boolean WHERE TREE -> L2 expr (And/Or/Not/IsNull; leaves via BuildConditionExpr).
	static ibQueryExprPtr BuildPredicateExpr(const ibBackendQueryable* queryable,
	                                         const ibQueryPredicatePtr& predicate,
	                                         const wxString& mainQual = wxEmptyString,
	                                         bool pathAsExists = false);
	// A dot-walk RLS condition on the WRITE path -> a CORRELATED EXISTS (a write cannot JOIN). Scans the
	// reference-target chain, correlates the first hop back to the outer write row, and folds the leaf.
	static ibQueryExprPtr BuildDotWalkExists(const ibBackendQueryable* queryable,
	                                         const ibQueryCondition& c,
	                                         const wxString& mainQual);
	// An RLS `restrict … join …` SEMI-JOIN -> a CORRELATED EXISTS over the inner permission source: the outer
	// row passes iff a permitting row exists. `EXISTS(SELECT * FROM <inner> sj WHERE <inner Where over sj>
	// AND sj.<innerKey> <op> outer.<outerKey>)`. The inner's own conditions (incl. inner dot-walk -> nested
	// EXISTS via pathAsExists) ride along; `outerQual` qualifies the main-source correlation column.
	static ibQueryExprPtr BuildSemiJoinExists(const ibSemiJoinExists& sj, const wxString& outerQual);
	// A COMPUTED-COLUMN expression (arithmetic / CASE) -> L2 expr (ibBinOp / ibCase; columns qualified
	// by mainQual, WHEN predicates via BuildPredicateExpr). For a projected computed column.
	static ibQueryExprPtr BuildColumnExpr(const ibBackendQueryable* queryable,
	                                      const ibQueryColumnExprPtr& expr,
	                                      const wxString& mainQual = wxEmptyString);
	// conditions AND predicate-tree, AND-folded — the one WHERE a provider site builds from a spec.
	static ibQueryExprPtr BuildWhere(const ibBackendQueryable* queryable,
	                                 const std::vector<ibQueryCondition>& conditions,
	                                 const ibQueryPredicatePtr& predicate,
	                                 const wxString& mainQual = wxEmptyString,
	                                 bool pathAsExists = false);
	static std::vector<ibQuerySortKey> BuildSortKeys(const ibBackendQueryable* queryable,
	                                                 const std::vector<ibQuerySortItem>& sorts,
	                                                 bool reverse,
	                                                 const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildAnchorPredicate(const ibBackendQueryable* queryable,
	                                           const std::vector<ibQuerySortItem>& sorts,
	                                           const std::vector<ibValue>& values,
	                                           ibFetchDirection direction,
	                                           const wxString& mainQual = wxEmptyString);
	static ibQueryExprPtr BuildParentRefPredicate(const ibBackendQueryable* queryable,
	                                              const wxString& refDataField,
	                                              const ibValue& parentKey,
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
		if (c != nullptr && from->GetProvider().ResolveReferenceTarget(from, c) == to) return c;
	return nullptr;
}

// ONE join node's keys: explicit on-columns, else DERIVED by reference — derivation only when BOTH
// children are Source leaves (a referencing column on one matched to the other's self-reference); a
// node with a sub-join child needs explicit keys. False when neither given nor derivable.
bool ResolveNodeKeys(const ibQueryNode* node, const ibBackendQueryColumn*& onL, const ibBackendQueryColumn*& onR)
{
	onL = node->m_on.m_colL; onR = node->m_on.m_colR;
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
	// Co-located SQL emits only INNER / LEFT; a RIGHT / FULL (or cross / ON TRUE) join folds in the RAM stitch.
	std::function<bool(const ibQueryNode*)> innerOrLeftOnly = [&](const ibQueryNode* n) -> bool {
		if (n == nullptr || n->m_kind != ibQueryNode::Kind::Join) return true;
		if (n->m_on.m_cross || n->m_joinKind == ibQueryJoinKind::Right || n->m_joinKind == ibQueryJoinKind::Full) return false;
		return innerOrLeftOnly(n->m_left.get()) && innerOrLeftOnly(n->m_right.get());
	};
	if (!innerOrLeftOnly(root))                return false;
	// A COMPUTED ON (a.x+1 <op> b.y) has no column key to qualify, so it still folds in the RAM stitch. A plain
	// column-to-column THETA (a.x > b.y) now renders server-side — BuildColocatedFrom emits the node's real op —
	// so only computed ON forces RAM. EVERY co-located decider (join / aggregate / union) calls this gate.
	std::function<bool(const ibQueryNode*)> allColumnKeyed = [&](const ibQueryNode* n) -> bool {
		if (n == nullptr || n->m_kind != ibQueryNode::Kind::Join) return true;
		if (n->m_on.m_exprL != nullptr) return false;   // computed ON -> RAM (no column key to qualify)
		return allColumnKeyed(n->m_left.get()) && allColumnKeyed(n->m_right.get());
	};
	if (!allColumnKeyed(root))                 return false;
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
	if (ibColumnCodec::HasReference(col))                            return false;   // reference -> rehydration needed
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
	const ibTypeDescription& td = col->GetTypeDesc();
	int fields = 0;
	if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) ++fields;
	if (td.ContainType(ibValueTypes::TYPE_NUMBER))  ++fields;
	if (td.ContainType(ibValueTypes::TYPE_DATE))    ++fields;
	if (td.ContainType(ibValueTypes::TYPE_STRING))  ++fields;
	if (td.ContainType(ibValueTypes::TYPE_ENUM))    ++fields;
	if (ibColumnCodec::HasReference(col))           ++fields;
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
	// The ON operator is the join's real compare op — Eq (the common case) OR a column-to-column theta
	// (a.x > b.y): the gate admits only column-keyed joins here, so onL/onR are set and the op renders directly.
	ibQueryExprPtr on = ibBinOp(JoinOpToBinOp(node->m_on.m_op),
		ibCol(ColocatedQual(leaves, onL), FirstSqlFieldOfColumn(onL)),
		ibCol(ColocatedQual(leaves, onR), FirstSqlFieldOfColumn(onR)));
	return ibJoin(left, right, on, jt);
}

// The boolean WHERE TREE lowered for a co-located JOIN: each leaf column is qualified by ITS OWN
// leaf's table (ColocatedOwner), so an OR / NOT / IS NULL spanning two leaves becomes ONE server-side
// expression over the joined row. Mirrors ibMetaIRBuilder::BuildPredicateExpr but the per-leaf
// qualification replaces the single mainQual. A leaf condition whose column no leaf owns (e.g. a
// row-key condition, which L4 never emits for a join) cannot be qualified — throw rather than DROP it,
// because dropping a branch of an OR would silently WIDEN the filter (wrong rows). (docs §23)
// Does the predicate tree carry a reference dot-walk LEAF (a Compare/LIKE/BETWEEN leaf with m_path)?
// Such a tree needs the dot-walk join machinery (BuildPageIR), not the plain mainQual lowering.
bool PredicateHasPath(const ibQueryPredicatePtr& p)
{
	if (!p) return false;
	if (p->m_kind == ibQueryPredicateKind::Leaf)   return !p->m_leaf.m_path.empty();
	if (p->m_kind == ibQueryPredicateKind::IsNull) return !p->m_path.empty();
	for (const ibQueryPredicatePtr& c : p->m_children)
		if (PredicateHasPath(c)) return true;
	return false;
}

ibQueryExprPtr BuildColocatedPredicate(const ibQueryPredicatePtr& p, const ColocatedLeaves& leaves)
{
	if (!p) return nullptr;
	switch (p->m_kind) {
	case ibQueryPredicateKind::Leaf: {
		if (p->m_leaf.m_semiJoin) {
			// RLS semi-join — a correlated EXISTS; the outer correlation column lives on the MAIN (protected)
			// leaf, so qualify by whichever co-located leaf owns m_outerKey.
			const ibBackendQueryable* outerOwner = ColocatedOwner(leaves, p->m_leaf.m_semiJoin->m_outerKey);
			return ibMetaIRBuilder::BuildSemiJoinExists(*p->m_leaf.m_semiJoin,
				outerOwner != nullptr ? outerOwner->GetQueryTableName() : wxString());
		}
		const ibBackendQueryColumn* col = p->m_leaf.m_col;
		const ibBackendQueryable* owner = col != nullptr ? ColocatedOwner(leaves, col) : nullptr;
		if (owner == nullptr)
			throw std::logic_error("ColocatedWhere: a WHERE-tree leaf references no joined leaf column");
		return ibMetaIRBuilder::BuildConditionExpr(owner, p->m_leaf, owner->GetQueryTableName());
	}
	case ibQueryPredicateKind::And: {
		ibQueryExprPtr a;
		for (const ibQueryPredicatePtr& c : p->m_children) a = AndFold(a, BuildColocatedPredicate(c, leaves));
		return a;
	}
	case ibQueryPredicateKind::Or: {
		ibQueryExprPtr a;
		for (const ibQueryPredicatePtr& c : p->m_children) a = OrFold(a, BuildColocatedPredicate(c, leaves));
		return a;
	}
	case ibQueryPredicateKind::Not: {
		ibQueryExprPtr in = p->m_children.empty() ? nullptr : BuildColocatedPredicate(p->m_children.front(), leaves);
		return in ? ibNot(in) : nullptr;
	}
	case ibQueryPredicateKind::IsNull: {
		const ibBackendQueryable* owner = p->m_col != nullptr ? ColocatedOwner(leaves, p->m_col) : nullptr;
		if (owner == nullptr)
			throw std::logic_error("ColocatedWhere: an IS NULL references no joined leaf column");
		ibQueryExprPtr allNull;
		for (const wxString& f : ColumnValueFields(p->m_col))
			allNull = AndFold(allNull, ibIsNull(ibColQ(owner->GetQueryTableName(), f), false));
		if (!allNull) return nullptr;
		return p->m_negated ? ibNot(allNull) : allNull;
	}
	}
	return nullptr;
}

// The WHERE, partitioned per leaf and AND-folded (each leaf's flat conditions lowered qualified by its
// table — composite-safe via BuildFilterPredicate), then AND-folded with the boolean WHERE TREE
// (cross-leaf OR/NOT/IS NULL) qualified per leaf. N leaves.
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
	where = AndFold(where, BuildColocatedPredicate(spec.m_predicate, leaves));
	return where;
}

} // namespace

ibQueryExprPtr ibMetaIRBuilder::BuildConditionExpr(const ibBackendQueryable* queryable,
                                                   const ibQueryCondition& c,
                                                   const wxString& mainQual,
                                                   bool pathAsExists)
{
	// RLS `restrict … join …` SEMI-JOIN payload — this condition IS a correlated EXISTS over the inner
	// permission source (m_col / m_value / m_path unused). Checked FIRST, so it renders on EVERY WHERE path
	// (read single / co-located / write / aggregate all reach BuildConditionExpr) — no missed site can leave a
	// write or a join-query unrestricted. The OUTER correlation column must be QUALIFIED (an empty mainQual —
	// the single-table write path — would bind ambiguously INSIDE the EXISTS subquery), so fall back to the
	// source's own table name, exactly as BuildDotWalkExists does for writes.
	if (c.m_semiJoin)
		return BuildSemiJoinExists(*c.m_semiJoin,
			!mainQual.IsEmpty() ? mainQual : (queryable != nullptr ? queryable->GetQueryTableName() : wxString()));

	// SEMI-JOIN dot-walk: a condition through a reference path (s.Ref.Field) rides as a CORRELATED EXISTS
	// instead of a JOIN — mandatory on WRITES (a DELETE/UPDATE/INSERT WHERE cannot JOIN), and now also on
	// READS for an RLS-folded condition (c.m_asExists), so the policy FILTERS without multiplying. A user's
	// own read dot-walk stays a JOIN (pathAsExists false, m_asExists false): pre-resolved in BuildPageIR,
	// the already-qualified leaf reaches here as a plain column.
	if ((pathAsExists || c.m_asExists) && !c.m_path.empty())
		return BuildDotWalkExists(queryable, c, mainQual);

	const ibQueryBinOp op = FilterOpToBinOp(c.m_op);   // ONE op (m_comparison + m_explicitOp collapsed into m_op)

	if (c.m_expr) {
		// COMPUTED left-hand side (WHERE Qty * Price > value) — lower the expression tree and
		// compare to the value. Checked BEFORE the null-column branch: an expr condition carries
		// m_col == null but is NOT a row-key lookup.
		return ibBinOp(op, BuildColumnExpr(queryable, c.m_expr, mainQual), ibConst(c.m_value));
	}
	if (c.m_col == nullptr) {
		// Row-key condition — a lookup by the row's own key (uuid, the identity tail), never
		// LIKE. No GetRowKeyColumn: the key field comes off GetIdentitySort like any column.
		return ibBinOp(op, ibColQ(mainQual, RowKeyField(queryable)), ibConst(c.m_value));
	}
	if (op == ibQueryBinOp::Eq && !c.m_col->IsRawColumn()) {
		// METADATA-column equality — decompose across ALL the column's physical fields (composite
		// / variant safe) via the value-spread. Guarded by !IsRawColumn: a RAW column (e.g. the
		// tabular parent uuid filter) has a single field and falls to the single-field branch
		// below. Column-based: the spread comes off the column + metadata, no attribute cast.
		return DecomposeEquality(c.m_col, queryable->GetMetaData(), c.m_value, mainQual);
	}
	if (op == ibQueryBinOp::Ne && !c.m_col->IsRawColumn()) {
		// METADATA-column inequality = NOT of the composite equality. A reference / variant / composite key
		// spreads across SEVERAL physical fields (and even a single-field reference needs the correctly-encoded
		// write-spread, NOT a bare ibConst). "Not equal" means NOT(all fields equal) — a naive first-field
		// `<>` would ignore the other fields AND mis-encode the value, so the filter would drop nothing.
		// Mirror the Eq branch, negated (De Morgan: differs when ANY field differs).
		ibQueryExprPtr eq = DecomposeEquality(c.m_col, queryable->GetMetaData(), c.m_value, mainQual);
		return eq ? ibNot(eq) : nullptr;
	}
	if ((op == ibQueryBinOp::Ge || op == ibQueryBinOp::Le || op == ibQueryBinOp::Gt || op == ibQueryBinOp::Lt)
	    && !c.m_col->IsRawColumn()) {
		// METADATA-column ORDERED compare (a reference / composite key with >=, <=, >, <) — decompose
		// LEXICOGRAPHICALLY over its physical fields with the SAME correctly-encoded write-spread as the
		// equality path. A plain reference is one _RRRef field, so this is `_RRRef OP <blob>` — the guid
		// order the paged anchor cursor needs (and the runtime's ibValue reference order mirrors it).
		return DecomposeOrdered(c.m_col, queryable->GetMetaData(), c.m_value, op, mainQual);
	}
	// RAW column (direct single physical field) OR ordered / inequality / LIKE compare —
	// the field name derives from the column itself (physical, type), no attribute needed.
	return ibBinOp(op, ibColQ(mainQual, FirstSqlFieldOfColumn(c.m_col)), ibConst(c.m_value));
}

ibQueryExprPtr ibMetaIRBuilder::BuildFilterPredicate(const ibBackendQueryable* queryable,
                                                     const std::vector<ibQueryCondition>& conditions,
                                                     const wxString& mainQual,
                                                     bool pathAsExists)
{
	ibQueryExprPtr pred;
	for (const ibQueryCondition& c : conditions) {
		// A user's read dot-walk is skipped here (BuildPageIR builds it as a JOIN alias). A WRITE (pathAsExists)
		// or an RLS-flagged (m_asExists) dot-walk is NOT skipped — it lowers to an EXISTS via BuildConditionExpr.
		if (!c.m_path.empty() && !pathAsExists && !c.m_asExists) continue;
		pred = AndFold(pred, BuildConditionExpr(queryable, c, mainQual, pathAsExists));
	}
	return pred;
}

ibQueryExprPtr ibMetaIRBuilder::BuildPredicateExpr(const ibBackendQueryable* queryable,
                                                   const ibQueryPredicatePtr& predicate,
                                                   const wxString& mainQual,
                                                   bool pathAsExists)
{
	if (!predicate)
		return nullptr;

	switch (predicate->m_kind) {
	case ibQueryPredicateKind::Leaf:
		return BuildConditionExpr(queryable, predicate->m_leaf, mainQual, pathAsExists);

	case ibQueryPredicateKind::And: {
		ibQueryExprPtr acc;
		for (const ibQueryPredicatePtr& child : predicate->m_children)
			acc = AndFold(acc, BuildPredicateExpr(queryable, child, mainQual, pathAsExists));
		return acc;
	}
	case ibQueryPredicateKind::Or: {
		ibQueryExprPtr acc;
		for (const ibQueryPredicatePtr& child : predicate->m_children)
			acc = OrFold(acc, BuildPredicateExpr(queryable, child, mainQual, pathAsExists));
		return acc;
	}
	case ibQueryPredicateKind::Not: {
		ibQueryExprPtr inner = predicate->m_children.empty()
			? nullptr : BuildPredicateExpr(queryable, predicate->m_children.front(), mainQual, pathAsExists);
		return inner ? ibNot(inner) : nullptr;
	}
	case ibQueryPredicateKind::IsNull: {
		// A metadata reference / composite column spans several physical fields; IS NULL holds only when
		// they are ALL null (AND-fold), IS NOT NULL when ANY is set (NOT of that). For a raw / single-field
		// column this collapses to one ibIsNull. Column-based: fields come off the column, no attribute.
		const ibBackendQueryColumn* col = predicate->m_col;
		ibQueryExprPtr allNull;
		if (col != nullptr) {
			for (const wxString& f : ColumnValueFields(col))
				allNull = AndFold(allNull, ibIsNull(ibColQ(mainQual, f), false));
		}
		if (!allNull)   // defensive: a column with no physical fields — no constraint
			return nullptr;
		return predicate->m_negated ? ibNot(allNull) : allNull;
	}
	}
	return nullptr;
}

ibQueryExprPtr ibMetaIRBuilder::BuildWhere(const ibBackendQueryable* queryable,
                                           const std::vector<ibQueryCondition>& conditions,
                                           const ibQueryPredicatePtr& predicate,
                                           const wxString& mainQual,
                                           bool pathAsExists)
{
	return AndFold(BuildFilterPredicate(queryable, conditions, mainQual, pathAsExists),
	               BuildPredicateExpr(queryable, predicate, mainQual, pathAsExists));
}

// A dot-walk RLS condition on the WRITE path -> a CORRELATED EXISTS (a write cannot JOIN). c.m_path is the
// reference chain [ref0, ref1, …, leafCol]: ref0 is a reference column on the outer (write) source, each
// further segment a reference on the prior target, and the last element the leaf attribute being compared.
//   EXISTS ( SELECT 1 FROM <t0> ex0 [JOIN <t1> ex1 ON ex0.<ref1> = ex1.<selfref> …]
//            WHERE ex0.<selfref> = <outer>.<ref0>            -- correlation back to the write row
//              AND <leaf condition on the last target> )
// Single-hop (s.Ref.Field) is the common case: one Scan, one correlation, the leaf. A path that cannot
// resolve a reference target / self-reference field THROWS (dropping it would widen the filter -> wrong rows).
ibQueryExprPtr ibMetaIRBuilder::BuildDotWalkExists(const ibBackendQueryable* queryable,
                                                   const ibQueryCondition& c,
                                                   const wxString& mainQual)
{
	const std::vector<const ibBackendQueryColumn*>& path = c.m_path;
	if (path.size() < 2)
		throw std::logic_error("BuildDotWalkExists: a dot-walk write condition needs at least ref + leaf");

	// First hop: ref0 on the outer source -> target t0, correlated to the outer write row.
	const ibBackendQueryColumn* ref0 = queryable->ResolveColumnByName(path[0]->GetName());
	const ibBackendQueryable*   t0   = ref0 ? queryable->GetProvider().ResolveReferenceTarget(queryable, ref0) : nullptr;
	if (ref0 == nullptr || t0 == nullptr || SelfReferenceField(t0).empty())
		throw std::logic_error("BuildDotWalkExists: unresolved first reference hop on the write path");

	ibQueryRelPtr    from  = ibScan(t0->GetQueryTableName(), wxT("ex0"));
	const ibBackendQueryable* owner = t0;
	wxString ownerAlias = wxT("ex0");

	// Middle hops: each further reference joins its target into the subquery.
	for (size_t i = 1; i + 1 < path.size(); ++i) {
		const ibBackendQueryColumn* refI = owner->ResolveColumnByName(path[i]->GetName());
		const ibBackendQueryable*   tI   = refI ? owner->GetProvider().ResolveReferenceTarget(owner, refI) : nullptr;
		if (refI == nullptr || tI == nullptr || SelfReferenceField(tI).empty())
			throw std::logic_error("BuildDotWalkExists: unresolved reference hop on the write path (composite mid-path not supported)");
		const wxString alias = wxString::Format(wxT("ex%d"), static_cast<int>(i));
		from = ibJoin(from, ibScan(tI->GetQueryTableName(), alias),
		              ibBinOp(ibQueryBinOp::Eq, ibColQ(ownerAlias, FirstSqlFieldOfColumn(refI)),
		                                        ibColQ(alias, SelfReferenceField(tI))),
		              ibQueryJoinType::Inner);
		owner = tI; ownerAlias = alias;
	}

	// Correlation back to the OUTER write row: ex0.<selfref> = <outer>.<ref0 field>. The outer is the write
	// TABLE itself (DELETE / UPDATE — mainQual empty) or the derived-row alias "src" (guarded INSERT create).
	// A bare name would ambiguously bind INSIDE the subquery, so qualify the outer column explicitly.
	const wxString outerQual = mainQual.IsEmpty() ? queryable->GetQueryTableName() : mainQual;
	ibQueryExprPtr correlation = ibBinOp(ibQueryBinOp::Eq,
		ibColQ(wxT("ex0"), SelfReferenceField(t0)),
		ibColQ(outerQual, FirstSqlFieldOfColumn(ref0)));

	// Leaf condition on the last target (c.m_col is the leaf column) — qualified by its alias, flat (no path).
	ibQueryExprPtr leaf = BuildConditionExpr(owner, c, ownerAlias, /*pathAsExists*/ false);

	// EXISTS only tests row presence -> SELECT * (a bare ibFilter renders as SELECT *; no projected const,
	// which sidesteps FB's untyped-placeholder -804). Correlation AND leaf as the subquery WHERE.
	return ibExists(ibFilter(from, AndFold(correlation, leaf)));
}

ibQueryExprPtr ibMetaIRBuilder::BuildSemiJoinExists(const ibSemiJoinExists& sj, const wxString& outerQual)
{
	if (sj.m_inner == nullptr || sj.m_outerKey == nullptr || sj.m_innerKey == nullptr)
		return nullptr;

	const wxString sjAlias = wxT("sj");

	// The inner's OWN conditions over the subquery alias — an inner dot-walk lowers to a NESTED EXISTS
	// (pathAsExists = true), so the inner's full richness (Where + dot-walk + captured runtime Params) rides
	// along, still never multiplying. Null predicate = the semi-join is presence-by-correlation only.
	ibQueryExprPtr innerWhere = BuildPredicateExpr(sj.m_inner, sj.m_where, sjAlias, /*pathAsExists*/ true);

	// Correlation: sj.<innerKey> <op> outer.<outerKey>. A reference key compares on its _RRRef field
	// (byte-identical), a scalar on its physical field — FirstSqlFieldOfColumn picks the single field either way.
	ibQueryExprPtr correlation = ibBinOp(FilterOpToBinOp(sj.m_op),
		ibColQ(sjAlias,  FirstSqlFieldOfColumn(sj.m_innerKey)),
		ibColQ(outerQual, FirstSqlFieldOfColumn(sj.m_outerKey)));

	// SELECT * (a bare ibFilter → SELECT *); inner Where AND the correlation as the subquery WHERE.
	return ibExists(ibFilter(ibScan(sj.m_inner->GetQueryTableName(), sjAlias), AndFold(innerWhere, correlation)),
	                sj.m_negated);
}

// A bare constant projected into a SELECT list (or a CASE branch) reaches the DB as an UNTYPED
// placeholder (SELECT ? AS x) — Firebird and other strict engines cannot infer its type and reject the
// statement (FB -804 "Data type unknown"). Pin the type with a CAST derived from the value. The target is
// a CANONICAL ibColumnType, NOT a SQL string: the L2 renderer spells it per-DBMS through the dialect
// TYPE-MAP (ibQueryRenderer::MapType), so the SQLite date-affinity (TEXT), boolean (INTEGER) and FB /
// MySQL narrow-DECIMAL forks are all closed at render time — the one place that owns the dialect.
static ibColumnType CastTypeForConst(const ibValue& v)
{
	switch (v.GetType()) {
	case ibValueTypes::TYPE_STRING: {
		const size_t n = v.GetString().length();
		return ibTypeString(static_cast<int>(n > 0 ? n : 1));
	}
	case ibValueTypes::TYPE_DATE:    return ibTypeDate();          // dialect: TIMESTAMP (FB / PG) / TEXT (SQLite)
	case ibValueTypes::TYPE_BOOLEAN: return ibTypeBoolean();       // dialect: BOOLEAN (FB / PG) / INTEGER (SQLite)
	default:                         return ibTypeNumber(18, 6);   // TYPE_NUMBER (and any other) — generous, no truncation
	}
}

ibQueryExprPtr ibMetaIRBuilder::BuildColumnExpr(const ibBackendQueryable* queryable,
                                                const ibQueryColumnExprPtr& expr,
                                                const wxString& mainQual)
{
	if (!expr)
		return nullptr;
	switch (expr->m_kind) {
	case ibQueryColumnExprKind::Column:
		return expr->m_col != nullptr ? ibColQ(mainQual, FirstSqlFieldOfColumn(expr->m_col)) : nullptr;

	case ibQueryColumnExprKind::Const:
		// CAST the placeholder so a bare projected constant carries a type (FB -804 otherwise). Harmless
		// inside arithmetic / CASE, where the type was already inferable.
		return ibCast(ibConst(expr->m_const), CastTypeForConst(expr->m_const));

	case ibQueryColumnExprKind::Arith: {
		const ibQueryBinOp op =
			expr->m_arith == ibQueryColumnArithOp::Add ? ibQueryBinOp::Add
			: expr->m_arith == ibQueryColumnArithOp::Sub ? ibQueryBinOp::Sub
			: expr->m_arith == ibQueryColumnArithOp::Mul ? ibQueryBinOp::Mul
			: expr->m_arith == ibQueryColumnArithOp::Div ? ibQueryBinOp::Div
			                                             : ibQueryBinOp::Mod;
		return ibBinOp(op, BuildColumnExpr(queryable, expr->m_lhs, mainQual),
		                   BuildColumnExpr(queryable, expr->m_rhs, mainQual));
	}

	case ibQueryColumnExprKind::Case: {
		std::vector<std::pair<ibQueryExprPtr, ibQueryExprPtr>> cases;
		for (const auto& wt : expr->m_cases)
			cases.emplace_back(BuildPredicateExpr(queryable, wt.first, mainQual),
			                   BuildColumnExpr(queryable, wt.second, mainQual));
		return ibCase(std::move(cases), expr->m_else ? BuildColumnExpr(queryable, expr->m_else, mainQual) : nullptr);
	}
	}
	return nullptr;
}

std::vector<ibQuerySortKey> ibMetaIRBuilder::BuildSortKeys(const ibBackendQueryable* /*queryable*/,
                                                           const std::vector<ibQuerySortItem>& sorts,
                                                           bool reverse,
                                                           const wxString& mainQual)
{
	std::vector<ibQuerySortKey> keys;

	// Every identity / user sort is a REAL column now (catalog's uuid included — no null
	// sentinel, no row-key special pass). Each column self-describes its physical fields
	// (GetValueFields), so no ResolveAttribute: an attribute column returns its authoritative
	// field list, a temp / raw column its bare field. One uniform pass.
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_col == nullptr) continue;
		if (!s.m_path.empty()) continue;   // dot-walk sort — emitted inline by BuildPageIR (qualified by its join alias)
		const bool asc = reverse ? !s.m_ascending : s.m_ascending;
		for (const wxString& name : ColumnValueFields(s.m_col)) {
			ibQuerySortKey k;
			k.m_expr = ibColQ(mainQual, name);
			k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			keys.push_back(std::move(k));
		}
	}

	return keys;
}

// Encode a REFERENCE value into its physical _RRRef blob for a keyset compare — SELF-CONTAINED, no metadata:
// the target metaID is the clsid BODY (dynamic reference clsid = kind|metaID), the guid comes off the value's
// own reference object, laid out + byte-swapped EXACTLY as the stored _RRRef (mirrors BuildParentRefPredicate).
// So `_RRRef OP <blob>` is a real BINARY compare — same bytes the column stores, same order ORDER BY _RRRef
// sorts. Returns nullptr when v is not a reference (a scalar rides inline).
static ibQueryExprPtr ReferenceKeyBlob(const ibValue& v)
{
	ibValueReferenceDataObject* refObj = nullptr;
	if (!v.ConvertToValue(refObj) || refObj == nullptr)
		return nullptr;
	const ibMetaID metaID = static_cast<ibMetaID>(refObj->GetClassType() & kIbClsidBodyMask);
	ibReference ref{ metaID, ibGuidImpl{} };
	const auto& be = refObj->GetGuid().GetGuid().bytes();
	auto* p = reinterpret_cast<unsigned char*>(&ref.m_guid);
	p[0] = be[3]; p[1] = be[2]; p[2] = be[1]; p[3] = be[0];
	p[4] = be[5]; p[5] = be[4];
	p[6] = be[7]; p[7] = be[6];
	for (int i = 8; i < 16; ++i) p[i] = be[i];
	return ibConstBlob(&ref, sizeof(ibReference));
}

ibQueryExprPtr ibMetaIRBuilder::BuildAnchorPredicate(const ibBackendQueryable* /*queryable*/,
                                                     const std::vector<ibQuerySortItem>& sorts,
                                                     const std::vector<ibValue>& values,
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
	// The keyset compares by FirstSqlFieldOfColumn — the SAME field ColumnValueFields drives the ORDER BY with,
	// so the keyset and the sort agree (a divergent field set re-reads the page head = duplicates). The anchor
	// value is EMBEDDED: a REFERENCE encodes to its real _RRRef BLOB (ReferenceKeyBlob — binary, so `_RRRef OP
	// blob` is a true reference compare, NOT a stringified guid); a scalar rides inline as ibConst.
	std::vector<SortCol> cols;
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_col == nullptr) continue;
		if (!s.m_path.empty()) continue;   // dot-walk sort is not a keyset anchor key (joined column, not on the main scan)
		cols.push_back({ s.m_col, s.m_ascending });
	}

	auto strictOp    = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Gt : ibQueryBinOp::Lt; };
	auto inclusiveOp = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Ge : ibQueryBinOp::Le; };

	auto valueAt = [&](size_t i) -> ibValue { return i < values.size() ? values[i] : ibValue(); };
	auto operand = [&](const ibValue& v) -> ibQueryExprPtr {
		if (ibQueryExprPtr blob = ReferenceKeyBlob(v)) return blob;   // reference -> real _RRRef binary blob
		return ibConst(v);                                            // scalar (uuid string / number / date / bool)
	};

	auto eqUpTo = [&](size_t kExclusive) -> ibQueryExprPtr {
		ibQueryExprPtr eq;
		for (size_t j = 0; j < kExclusive; ++j)
			eq = AndFold(eq, ibBinOp(ibQueryBinOp::Eq,
			                         ibColQ(mainQual, FirstSqlFieldOfColumn(cols[j].col)),
			                         operand(valueAt(j))));
		return eq;
	};

	ibQueryExprPtr predicate;
	for (size_t i = 0; i < cols.size(); ++i) {
		const bool isLast = (i + 1 == cols.size());
		const ibQueryBinOp op =
			(inclusiveTail && isLast) ? inclusiveOp(cols[i].asc) : strictOp(cols[i].asc);
		ibQueryExprPtr clause = AndFold(
			eqUpTo(i),
			ibBinOp(op, ibColQ(mainQual, FirstSqlFieldOfColumn(cols[i].col)), operand(valueAt(i))));
		predicate = OrFold(predicate, clause);
	}

	return predicate;
}

ibQueryExprPtr ibMetaIRBuilder::BuildParentRefPredicate(const ibBackendQueryable* queryable,
                                                        const wxString& refDataField,
                                                        const ibValue& parentKey,
                                                        bool isTopLevel,
                                                        const wxString& mainQual)
{
	// Non-root: compare the hierarchy column against the parent KEY value itself. A reference encodes to its own
	// _RRRef blob (ReferenceKeyBlob — self-describing metaID, so NO same-table assumption); a non-reference key
	// rides inline (ibConst). This is the SAME encoding the keyset anchor uses, not a bare-guid special case.
	if (!isTopLevel) {
		if (ibQueryExprPtr blob = ReferenceKeyBlob(parentKey))
			return ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField), blob);
		return ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField), ibConst(parentKey));
	}
	// Root level: the EMPTY parent reference — the table's own type + a zero guid, the sentinel stored for a
	// parentless row. (A non-reference hierarchy's roots would compare inline; none exist yet.)
	ibReference ref{ queryable->GetQueryTableId(), ibGuidImpl{} };
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

// Builds (and dedups) the reference dot-walk LEFT-join chain on a FROM tree: a path's prefix joined once
// is reused. Shared by the read (BuildPageIR) and the single-source aggregate (ExecuteAggregate) so both
// resolve `Producer.Region` to the joined target the SAME way — no per-path duplication. The leaf
// qualifies by the returned alias; a plain column by the root table. (docs/query-language-arc.md §22.4b)
class ibRefJoinChain
{
public:
	ibRefJoinChain(const ibBackendQueryable* root, const wxString& rootTable)
		: m_root(root), m_rootTable(rootTable), m_from(ibScan(rootTable)) {}

	// Resolve a reference path to its LEAF's join alias + target queryable, appending (deduped) joins.
	// Returns false if a segment is not a single-target reference (an unresolvable / composite edge).
	bool Resolve(const std::vector<const ibBackendQueryColumn*>& path,
	             wxString& outAlias, const ibBackendQueryable*& outTarget)
	{
		const ibBackendQueryable* curQ = m_root;
		wxString curQual = m_rootTable, prefixKey;
		for (size_t i = 0; i + 1 < path.size(); ++i) {
			const ibBackendQueryColumn* refCol = path[i];
			const ibBackendQueryable* tgtQ = (curQ != nullptr) ? curQ->GetProvider().ResolveReferenceTarget(curQ, refCol) : nullptr;
			const wxString tgtRefField = (tgtQ != nullptr) ? SelfReferenceField(tgtQ) : wxString();
			if (tgtQ == nullptr || tgtRefField.empty()) return false;
			prefixKey += wxString::Format(wxT("%p|"), (const void*)refCol);
			auto it = m_prefixAlias.find(prefixKey);
			if (it != m_prefixAlias.end())
				curQual = it->second;
			else
				curQual = m_prefixAlias[prefixKey] =
					AddLeftJoin(tgtQ->GetQueryTableName(), curQual, FirstSqlFieldOfColumn(refCol), tgtRefField);
			curQ = tgtQ;
		}
		outAlias = curQual; outTarget = curQ;
		return true;
	}

	// Append one LEFT join (NOT deduped — for the composite branch's per-target joins); returns its alias.
	wxString AddLeftJoin(const wxString& table, const wxString& leftQual,
	                     const wxString& leftField, const wxString& rightField)
	{
		const wxString alias = wxString::Format(wxT("dw%d"), m_aliasSeq++);
		m_from = ibJoin(m_from, ibScan(table, alias),
			ibBinOp(ibQueryBinOp::Eq, ibCol(leftQual, leftField), ibCol(alias, rightField)),
			ibQueryJoinType::Left);
		return alias;
	}

	ibQueryRelPtr From()  const { return m_from; }
	bool          Empty() const { return m_aliasSeq == 0; }

private:
	const ibBackendQueryable*    m_root;
	wxString                     m_rootTable;
	ibQueryRelPtr                m_from;
	std::map<wxString, wxString> m_prefixAlias;   // path-prefix key -> join alias (shared dedup across all paths)
	int                          m_aliasSeq = 0;
};

	// Aggregated read (totals) — a physical GROUP BY built from the spec. The
	// AggregateItem / HavingItem are public on the door, so the provider lowers them.
ibDataQueryResult ibDbTableProvider::ExecuteAggregate(const ibDataQuerySpec& spec)
	{
		const ibBackendQueryable* queryable = spec.m_queryable;
		const wxString mainTable = queryable->GetQueryTableName();

		// Reference dot-walk in a GROUP BY key (Producer.Region) or an aggregate input (SUM(Producer.Weight))
		// rides the SAME join chain as the read path. With joins present, a PLAIN key / input qualifies by the
		// main table (disambiguation) and a dot-walk leaf by its join alias.
		static const std::vector<std::vector<const ibBackendQueryColumn*>> kNoGroupPaths;
		const std::vector<std::vector<const ibBackendQueryColumn*>>& groupPaths =
			spec.m_groupPaths ? *spec.m_groupPaths : kNoGroupPaths;
		bool hasDotWalk = false;
		for (const auto& p : groupPaths)            if (!p.empty())        { hasDotWalk = true; break; }
		if (!hasDotWalk)
			for (const auto& a : *spec.m_aggregates) if (!a.m_path.empty()) { hasDotWalk = true; break; }
		const wxString mainQual = hasDotWalk ? mainTable : wxString();

		ibRefJoinChain chain(queryable, mainTable);
		auto joinLeaf = [&](const std::vector<const ibBackendQueryColumn*>& path) -> wxString {
			wxString a; const ibBackendQueryable* tq = nullptr;
			if (!chain.Resolve(path, a, tq) || tq == nullptr)
				ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
					_("a dot-walk GROUP BY / aggregate path did not resolve its reference join"));
			return a;
		};
		auto qualCol = [](const wxString& qual, const wxString& field) {
			return qual.empty() ? ibCol(field) : ibCol(qual, field);
		};

		ibDatabaseQueryBuilder q(spec.m_holder);

		if (auto predicate = ibMetaIRBuilder::BuildWhere(queryable, *spec.m_conditions, spec.m_predicate, mainQual))
			q.Where(predicate);

		std::vector<ibQueryProjItem> projection;
		for (size_t gi = 0; gi < spec.m_groupBy->size(); ++gi) {
			const ibBackendQueryColumn* gcol = (*spec.m_groupBy)[gi];
			const std::vector<const ibBackendQueryColumn*>& path = (gi < groupPaths.size()) ? groupPaths[gi]
			                                                      : std::vector<const ibBackendQueryColumn*>{};
			const wxString qual = path.empty() ? mainQual : joinLeaf(path);   // dot-walk leaf -> join alias
			// The group key GROUPs + projects by its FULL spread (ColumnFieldNames): a reference / variant key
			// carries its TYPE + _RTRef + _RRRef fields so the read side reconstructs the value via GetValue; a
			// plain scalar's spread IS its one field, so this is uniform. The reference-typing stays HERE in the
			// provider — the reader just calls GetValue, blind to whether the key is a reference.
			for (const wxString& field : ColumnFieldNames(gcol)) {   // tier derives its fields — no ResolveAttribute
				const ibQueryExprPtr gexpr = qualCol(qual, field);
				q.GroupBy(gexpr);
				projection.push_back(ibQueryProjItem{ gexpr, field });   // AS its physname -> GetValue(gcol) reads it back
			}
		}
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
			const wxString qual = a.m_path.empty() ? mainQual : joinLeaf(a.m_path);
			std::vector<ibQueryExprPtr> args;
			if (a.m_expr)                                  // COMPUTED input — SUM(Qty * Price): lower the tree
				args.push_back(ibMetaIRBuilder::BuildColumnExpr(queryable, a.m_expr, mainQual));
			else
				args.push_back(a.m_col != nullptr ? qualCol(qual, FirstSqlFieldOfColumn(a.m_col)) : ibCol(wxT("*")));
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
		}
		q.Project(std::move(projection));

		ibQueryExprPtr having;
		for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(h.m_col != nullptr ? qualCol(mainQual, FirstSqlFieldOfColumn(h.m_col)) : ibCol(wxT("*")));
			ibQueryExprPtr cmp = ibBinOp(FilterOpToBinOp(h.m_op),
				ibFunc(AggregateFnName(h.m_fn), std::move(args)), ibConst(h.m_value));
			having = having ? ibBinOp(ibQueryBinOp::And, having, cmp) : cmp;
		}
		if (having) q.Having(having);

		for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(queryable, *spec.m_sorts, /*reverse*/ false))
			q.AddSortKey(key);

		if (spec.m_topCount > 0)
			q.Limit(spec.m_topCount);   // SELECT TOP n + GROUP BY — the dialect LIMIT caps the groups

		if (hasDotWalk) q.From(chain.From());
		else            q.From(mainTable);

		return ibDataQueryResult(q.Execute(), queryable);
	}

// ==========================================================================
// Co-located server-side JOIN — the multi-source FAST PATH (docs §22.1a). The composer
// asks CanColocateJoin first; on a yes it delegates the whole 2-leaf inner join to ONE
// server-side SELECT here, on a no it keeps its materialise-to-RAM + C++ stitch. So this
// is purely additive: a narrow shape goes server-side, everything else is unchanged.
// ==========================================================================
// Shared co-location preconditions for a JOIN-tree terminal (read OR aggregate): a colocatable join
// tree of real DB leaves, no dot-walk / key-in (their own paths), every join key a single field.
// The terminal gates (CanColocateJoin / CanColocateAggregate) add their output / aggregate checks.
static bool CanColocateBase(const ibDataQuerySpec& spec, ColocatedLeaves& leaves)
{
	if (!ColocatableJoinTree(spec, leaves)) return false;
	if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty()) return false;
	if (!AllNodeKeysSingleField(spec.m_root, leaves)) return false;
	return true;
}

bool ibDbTableProvider::CanColocateJoin(const ibDataQuerySpec& spec)
	{
		ColocatedLeaves leaves;
		if (!CanColocateBase(spec, leaves))
			return false;

		// This is the plain READ terminal: not an aggregate, no dot-walk / key-in (those have their
		// own paths). The multi-source output IS the explicit select list — non-empty.
		if (!spec.m_groupBy->empty() || !spec.m_aggregates->empty()) return false;
		// COMPUTED output columns (arithmetic / CASE) are evaluated per joined row by the RAM composer —
		// the co-located server-side projection cannot, so a computed JOIN must take the RAM stitch path.
		if (spec.m_selectExprs != nullptr && !spec.m_selectExprs->empty()) return false;
		if (spec.m_selectCols->empty())                              return false;

		// OUTPUTS: any column is projectable — a RAW column reads one scalar field, a metadata column its
		// FULL spread (reference / enum / variant reconstruct via GetValueColumn). Just require each is
		// owned by a leaf (so it qualifies).
		for (const auto& sc : *spec.m_selectCols)
			if (sc.first == nullptr || ColocatedOwner(leaves, sc.first) == nullptr) return false;
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
				for (const wxString& f : ColumnFieldNames(col))
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
			out.AddColumn(p.col->GetColumnId(), p.alias, p.col->GetTypeDesc());

		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (const OutPlan& p : plans) {
				ibValue v;
				if (p.raw) v = ReadScalarByAlias(p.col, p.alias, cursor);
				else       ibColumnCodec::ReadValue(p.prefix, p.col, p.meta, v, cursor);   // reference/enum/variant reassembly
				out.SetCell(r, p.col->GetColumnId(), v);
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
		if (!CanColocateBase(spec, leaves))
			return false;

		if (spec.m_groupBy->empty() && spec.m_aggregates->empty()) return false;   // not an aggregate terminal

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

// Single-level group KEYSET paging gate (docs: group-level paging). A drill fetches ONE grouping level at a
// time; when that level is a single PLAIN scalar dimension over a SINGLE real DB source, its groups run
// server-side as GROUP BY dim ORDER BY dim [dim > anchor] LIMIT count -- a keyset-paged group read -- rather
// than reading every detail row and folding all groups in RAM. Multi-level / dot-walk / multi-source groups
// keep the fold (a multi-source group is a co-location follow-up, mirroring the JOIN/UNION push-downs).
bool ibDbTableProvider::CanPageGroupLevel(const ibDataQuerySpec& spec)
	{
		// Exactly one grouping level (the per-level drill).
		if (spec.m_groupBy == nullptr || spec.m_groupBy->size() != 1) return false;
		if ((*spec.m_groupBy)[0] == nullptr) return false;
		// PLAIN scalar dimension -- a dot-walk key (path length > 1) expands to joins / a synthetic id that a
		// single-column keyset ORDER BY cannot page by; leave it on the RAM fold.
		if (spec.m_groupPaths != nullptr && !spec.m_groupPaths->empty() && (*spec.m_groupPaths)[0].size() > 1)
			return false;
		// ...and it must be a TRUE scalar. A REFERENCE / variant dimension's value is a multi-field spread
		// (TYPE + _RTRef + _RRRef): a single-column keyset ORDER BY / anchor cannot page it, and the paged
		// projection (ColumnValueFields) would DROP the TYPE field GetValue reconstructs from -> "field
		// _TYPE not found". Route it to the unpaged ExecuteAggregate (full-spread GROUP BY) -- grouping by a
		// reference has no meaningful keyset order anyway (its id, not its presentation). Scalar = 1 field.
		if (ColumnFieldNames((*spec.m_groupBy)[0]).size() > 1)
			return false;
		// SINGLE real DB source -- no relational tree, or a lone Source node. A JOIN / UNION group needs
		// co-location (a later step). The common case: a catalog / register grouped by one of its own fields.
		if (spec.m_root != nullptr && spec.m_root->m_kind != ibQueryNode::Kind::Source) return false;
		if (spec.m_queryable == nullptr) return false;
		// No key-in / computed-select complications ride this path (each has its own handling).
		if (spec.m_keyIn != nullptr && !spec.m_keyIn->empty()) return false;
		if (spec.m_selectExprs != nullptr && !spec.m_selectExprs->empty()) return false;
		return true;
	}

// ExecuteGroupLevelPage -- the single-level group KEYSET page (gate: CanPageGroupLevel). Runs the level's groups
// server-side instead of reading every detail row + folding in RAM: SELECT dim [, aggs] FROM src WHERE <conds>
// [AND dim </> anchor] GROUP BY dim ORDER BY dim LIMIT count. The dim is the group key AND the sort/keyset column,
// so the page is positioned by the anchor group's dim value (page.m_anchorSortValues, stamped by the model from the
// browsed anchor group). Mirrors ExecuteAggregate (single-source GROUP BY) + the read path's keyset ORDER/anchor/LIMIT.
ibDataQueryResult ibDbTableProvider::ExecuteGroupLevelPage(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
	{
		const ibBackendQueryable* queryable = spec.m_queryable;
		const wxString            mainTable  = queryable->GetQueryTableName();
		const wxString            mainQual;   // single PLAIN source (the gate rejects dot-walk / joins) -> no qualifier
		const ibBackendQueryColumn* dim = (*spec.m_groupBy)[0];

		ibDatabaseQueryBuilder q(spec.m_holder);

		// WHERE = the persistent conditions + boolean predicate (the drill SCOPE filters + the user filter + an RLS
		// semi-join all ride here, exactly as the detail read renders them).
		if (auto predicate = ibMetaIRBuilder::BuildWhere(queryable, *spec.m_conditions, spec.m_predicate, mainQual))
			q.Where(predicate);

		// GROUP BY the dimension + project it back AS its physname, so GetValue(dim) reads the group's value.
		std::vector<ibQueryProjItem> projection;
		for (const wxString& field : ColumnValueFields(dim)) {
			const ibQueryExprPtr gexpr = ibCol(field);
			q.GroupBy(gexpr);
			projection.push_back(ibQueryProjItem{ gexpr, field });
		}
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
			std::vector<ibQueryExprPtr> args;
			args.push_back(a.m_col != nullptr ? ibCol(FirstSqlFieldOfColumn(a.m_col)) : ibCol(wxT("*")));
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
		}
		q.Project(std::move(projection));

		// ORDER BY the group dimension -- the total order the keyset page rides (Backward flips it). The dim is the
		// single sort key of this level; the identity tail is unneeded (a distinct dim value is its own unique key).
		ibQuerySortItem dimSortItem; dimSortItem.m_col = dim; dimSortItem.m_ascending = true;
		const std::vector<ibQuerySortItem> dimSort = { dimSortItem };
		for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(queryable, dimSort, page.m_reverseSort, mainQual))
			q.AddSortKey(key);

		// KEYSET: dim </> the anchor group's dim value. Empty anchor = the first page (top / bottom by direction).
		if (page.m_hasAnchor && !page.m_anchorSortValues.empty())
			if (auto anchorPred = ibMetaIRBuilder::BuildAnchorPredicate(queryable, dimSort, page.m_anchorSortValues, page.m_direction, mainQual))
				q.Where(anchorPred);

		if (page.m_count > 0)
			q.Limit(page.m_count);   // LIMIT the GROUPS (dim is the group key) -- the page-sized level, not all groups

		q.From(mainTable);           // the single physical source (a plain single-table read; the gate rejects joins)
		return ibDataQueryResult(q.Execute(), queryable);
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
				for (const wxString& f : ColumnFieldNames(g)) {
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

		if (spec.m_topCount > 0)
			q.Limit(spec.m_topCount);   // SELECT TOP n + GROUP BY across the co-located join

		ibQueryResult cursor = q.Execute();

		// Materialise into the unified RAM table: group columns keyed by model id (read via the g<i>
		// scalar alias), aggregates keyed by a synthetic id far from any metaID (read by their alias).
		ibQueryRamTable out;
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			out.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
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
				else           ibColumnCodec::ReadValue(gp.tag, gp.col, gp.meta, v, cursor);   // reference / variant group key
				out.SetCell(r, gp.col->GetColumnId(), v);
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

// --- co-located UNION WHERE: push the full boolean tree + RLS semi-join into EACH branch -----------
// A UNION differs from a JOIN: the branches are structurally PARALLEL (branch B owns its OWN "Code"
// column, not the primary's), so the SAME predicate is pushed into EACH branch with every column
// resolved BY NAME against that branch -- vs BuildColocatedPredicate, which qualifies by owning leaf in
// ONE joined row space. This is what makes an RLS restriction ride the UNION read server-side (not just
// the JOIN); it is also the plain-correctness fix for a boolean WHERE (OR/NOT/IS NULL) over a union.

// Is the predicate renderable over every union branch? Every referenced column must resolve BY NAME +
// SCALAR in every branch; a dot-walk / computed leaf needs a join the branch scan lacks -> RAM.
static bool UnionPredicateColocatable(const ibQueryPredicatePtr& p,
	const std::vector<std::shared_ptr<ibQueryNode>>& parts)
{
	if (!p) return true;
	auto resolvesEveryBranch = [&](const ibBackendQueryColumn* c) -> bool {
		if (c == nullptr) return false;
		for (const auto& part : parts) {
			const ibBackendQueryable* q = part != nullptr ? part->m_queryable : nullptr;
			const ibBackendQueryColumn* bc = q != nullptr ? q->ResolveColumnByName(c->GetName()) : nullptr;
			if (bc == nullptr) return false;
			ColocatedLeaves one; one.push_back(q);
			if (!ScalarReadable(bc, one)) return false;
		}
		return true;
	};
	switch (p->m_kind) {
	case ibQueryPredicateKind::Leaf:
		if (p->m_leaf.m_semiJoin) return resolvesEveryBranch(p->m_leaf.m_semiJoin->m_outerKey);
		if (!p->m_leaf.m_path.empty() || p->m_leaf.m_expr) return false;   // dot-walk / computed -> RAM
		return resolvesEveryBranch(p->m_leaf.m_col);
	case ibQueryPredicateKind::IsNull:
		if (!p->m_path.empty()) return false;                              // dot-walk IS NULL -> RAM
		return resolvesEveryBranch(p->m_col);
	case ibQueryPredicateKind::And:
	case ibQueryPredicateKind::Or:
	case ibQueryPredicateKind::Not:
		for (const auto& ch : p->m_children)
			if (!UnionPredicateColocatable(ch, parts)) return false;
		return true;
	}
	return false;
}

// Render the predicate for ONE branch: columns resolved BY NAME against `q`, qualified by its table; an
// RLS semi-join re-correlates its OUTER key to THIS branch. Mirrors BuildColocatedPredicate for a single
// by-name-resolved source. The gate (UnionPredicateColocatable) guarantees every column resolves.
static ibQueryExprPtr BuildBranchPredicate(const ibQueryPredicatePtr& p, const ibBackendQueryable* q)
{
	if (!p) return nullptr;
	const wxString qual = q->GetQueryTableName();
	switch (p->m_kind) {
	case ibQueryPredicateKind::Leaf: {
		if (p->m_leaf.m_semiJoin) {
			ibSemiJoinExists sj = *p->m_leaf.m_semiJoin;
			sj.m_outerKey = q->ResolveColumnByName(sj.m_outerKey->GetName());   // correlate to THIS branch's key
			return ibMetaIRBuilder::BuildSemiJoinExists(sj, qual);
		}
		ibQueryCondition c = p->m_leaf;
		c.m_col = q->ResolveColumnByName(p->m_leaf.m_col->GetName());
		return ibMetaIRBuilder::BuildConditionExpr(q, c, qual);
	}
	case ibQueryPredicateKind::And: {
		ibQueryExprPtr a;
		for (const auto& ch : p->m_children) a = AndFold(a, BuildBranchPredicate(ch, q));
		return a;
	}
	case ibQueryPredicateKind::Or: {
		ibQueryExprPtr a;
		for (const auto& ch : p->m_children) a = OrFold(a, BuildBranchPredicate(ch, q));
		return a;
	}
	case ibQueryPredicateKind::Not: {
		ibQueryExprPtr in = p->m_children.empty() ? nullptr : BuildBranchPredicate(p->m_children.front(), q);
		return in ? ibNot(in) : nullptr;
	}
	case ibQueryPredicateKind::IsNull: {
		const ibBackendQueryColumn* bc = q->ResolveColumnByName(p->m_col->GetName());
		ibQueryExprPtr allNull;
		for (const wxString& f : ColumnValueFields(bc))
			allNull = AndFold(allNull, ibIsNull(ibColQ(qual, f), false));
		if (!allNull) return nullptr;
		return p->m_negated ? ibNot(allNull) : allNull;
	}
	}
	return nullptr;
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

		// A boolean WHERE tree / RLS semi-join (m_predicate) is pushed into EACH branch by
		// BuildBranchPredicate; co-locate only if every referenced column resolves BY NAME + SCALAR in
		// every branch (a dot-walk / computed leaf needs a join the branch scan lacks). Else RAM applies it.
		if (spec.m_predicate != nullptr && !UnionPredicateColocatable(spec.m_predicate, root->m_parts))
			return false;
		return true;
	}

ibDataQueryResult ibDbTableProvider::ExecuteColocatedUnion(const ibDataQuerySpec& spec, const ibReadPageRequest& page)
	{
		const ibQueryNode* root = spec.m_root;
		const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& outs = *spec.m_selectCols;
		auto uAlias = [](size_t i) { return wxString::Format(wxT("u%d"), static_cast<int>(i)); };

		// Build each branch as SELECT <field AS u<i>> FROM table WHERE <branch conds>. The operator per
		// branch follows the node's m_partAll flag: UNION ALL keeps duplicates, plain UNION dedupes —
		// the DBMS does it natively (the spelling lives in the L2 render: ibUnion vs ibUnionAll).
		ibQueryRelPtr unionRel;
		for (size_t pi = 0; pi < root->m_parts.size(); ++pi) {
			const ibBackendQueryable* q = root->m_parts[pi]->m_queryable;
			ibQueryRelPtr rel = ibScan(q->GetQueryTableName());

			std::vector<ibQueryCondition> conds;
			for (const ibQueryCondition& c : *spec.m_conditions) {
				if (c.m_col == nullptr) continue;
				const ibBackendQueryColumn* bc = q->ResolveColumnByName(c.m_col->GetName());
				if (bc == nullptr) continue;
				ibQueryCondition nc = c; nc.m_col = bc; conds.push_back(nc);
			}
			// Flat conditions AND the full boolean WHERE tree + RLS semi-join (m_predicate) pushed into
			// THIS branch (columns resolved BY NAME; the gate guarantees they resolve) -> RLS rides the
			// UNION read server-side, and a boolean OR/NOT/IS NULL WHERE is no longer silently dropped.
			ibQueryExprPtr where = ibMetaIRBuilder::BuildFilterPredicate(q, conds, wxString());
			where = AndFold(where, BuildBranchPredicate(spec.m_predicate, q));
			if (where)
				rel = ibFilter(rel, where);

			std::vector<ibQueryProjItem> proj;
			for (size_t i = 0; i < outs.size(); ++i) {
				const ibBackendQueryColumn* bc = q->ResolveColumnByName(outs[i].first->GetName());
				proj.push_back(ibQueryProjItem{ ibCol(FirstSqlFieldOfColumn(bc)), uAlias(i) });
			}
			rel = ibProject(rel, std::move(proj));

			const bool keepDups = pi >= root->m_partAll.size() || root->m_partAll[pi];   // missing flag = ALL (back-compat)
			unionRel = unionRel ? (keepDups ? ibUnionAll(unionRel, rel) : ibUnion(unionRel, rel)) : rel;
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
			out.AddColumn(outs[i].first->GetColumnId(), outs[i].second, outs[i].first->GetTypeDesc());
		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (size_t i = 0; i < outs.size(); ++i)
				out.SetCell(r, outs[i].first->GetColumnId(), ReadScalarByAlias(outs[i].first, uAlias(i), cursor));
		}
		return ibDataQueryResult(std::move(out), spec.m_queryable);
	}

// ==========================================================================
// Totals push-down — GROUP BY ROLLUP (docs/query-language-arc.md §22.1b). The DBMS computes every
// subtotal level (each from raw detail -> correct for COUNT/AVG) + the grand total in ONE pass; we
// read the result + the GROUPING(key) flags and assemble the ibSelectorTree node tree the runtime
// already consumes. Only the aggregated subtotal rows transit — no raw detail.
// ==========================================================================
bool ibDbTableProvider::CanRollupTotalsShape(const ibDataQuerySpec& spec)
	{
		// Single-source DB queryable (a multi-source totals goes through the co-located / RAM paths).
		if (spec.m_root != nullptr && spec.m_root->m_kind != ibQueryNode::Kind::Source) return false;
		const ibBackendQueryable* q = spec.m_queryable;
		if (q == nullptr || q->IsComputedInRam())                 return false;
		if (spec.m_groupBy->empty())                              return false;
		if (!spec.m_dotWalks->empty() || !spec.m_keyIn->empty())  return false;

		// A dot-walk GROUP key rides a reference JOIN chain (ExecuteRollupTotals builds it) — allowed WHEN every
		// NON-leaf path segment is a SINGLE-TARGET reference (structurally resolvable, metadata-only, no DB). A
		// composite / multi-target mid-hop can't be joined -> RAM-fold (correct there).
		if (spec.m_groupPaths != nullptr)
			for (const auto& gp : *spec.m_groupPaths) {
				if (gp.empty()) continue;
				const ibBackendQueryable* walk = q;
				for (size_t s = 0; s + 1 < gp.size() && walk != nullptr; ++s)
					walk = walk->GetProvider().ResolveReferenceTarget(walk, gp[s]);
				if (walk == nullptr) return false;   // an unresolvable hop -> RAM-fold
			}
		// Group keys: SCALAR or a REFERENCE / variant (a reference groups by its full spread as ONE composite
		// ROLLUP element, reassembled on read — RunRollupTotals handles it). Aggregate inputs stay SCALAR.
		ColocatedLeaves one; one.push_back(q);
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			if (g == nullptr) return false;
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (a.m_col != nullptr && !ScalarReadable(a.m_col, one)) return false;
		return true;
	}

bool ibDbTableProvider::CanPushRollupTotals(const ibDataQuerySpec& spec)
	{
		if (!CanRollupTotalsShape(spec)) return false;
		// The connected dialect must advertise ROLLUP (FB5 / PG / MySQL8; NOT SQLite -> RAM).
		ibConnectionScope scope(spec.m_holder);
		if (!scope) return false;
		return scope->GetDialect().m_features.m_rollup;
	}

// Shared ROLLUP-totals core: SELECT g<i>, GROUPING(g<i>) AS grp<i>, <agg> AS alias FROM <from>
// GROUP BY ROLLUP(keys) [HAVING …], run it, and assemble the ibSelectorTree from the GROUPING levels.
// `from` is the ALREADY-filtered source relation; `colExpr` maps a group / aggregate column to its SQL
// reference — UNqualified for a single table, table-qualified for a co-located JOIN. Both the
// single-source push (ExecuteRollupTotals) and the co-located multi-source push
// (ExecuteColocatedRollupTotals) funnel here, so the row-read + tree-assembly lives in ONE place.
// Per group-key rendering plan for a ROLLUP totals: a SCALAR key is one field (colExpr); a REFERENCE / variant
// key is its FULL SPREAD grouped as ONE composite ROLLUP element ((f0,f1,…)) so the whole reference is a single
// level, projected under a prefix, and reassembled on read via ibColumnCodec::ReadValue. The caller supplies the
// per-column plan (scalar? + qualifier for the spread fields + the metaData for reassembly).
struct RollupGroupKey { bool scalar; wxString qualifier; const ibMetaData* meta; };

static ibSelectorTree RunRollupTotals(const ibDataQuerySpec& spec, ibQueryRelPtr from,
	const std::function<ibQueryExprPtr(const ibBackendQueryColumn*)>& colExpr,
	const std::function<RollupGroupKey(const ibBackendQueryColumn*)>& keyInfo)
{
	// Build the IR: SELECT <g<i> | spread>, GROUPING(<key rep>) AS grp<i>, <agg> AS alias FROM <from>
	//               GROUP BY ROLLUP(g0 | (spread0), g1 | (spread1), …)
	struct KeyPlan { const ibBackendQueryColumn* col; bool scalar; wxString tag; const ibMetaData* meta; };
	std::vector<KeyPlan>         keyPlans;
	std::vector<ibQueryProjItem> projection;
	std::vector<ibQueryExprPtr>  groupKeys;
	std::vector<wxString>        groupingAliases;
	int gi = 0;
	for (const ibBackendQueryColumn* g : *spec.m_groupBy) {
		const RollupGroupKey ki       = keyInfo(g);
		const wxString       grpalias = wxString::Format(wxT("grp%d"), gi);
		auto fieldCol = [&](const wxString& f) { return ki.qualifier.empty() ? ibCol(f) : ibCol(ki.qualifier, f); };
		if (ki.scalar) {
			const wxString       galias = wxString::Format(wxT("g%d"), gi);
			const ibQueryExprPtr gexpr  = colExpr(g);
			groupKeys.push_back(gexpr);
			projection.push_back(ibQueryProjItem{ gexpr, galias });
			projection.push_back(ibQueryProjItem{ ibFunc(wxT("GROUPING"), { colExpr(g) }), grpalias });
			keyPlans.push_back({ g, true, galias, ki.meta });
		}
		else {
			// REFERENCE / variant key: GROUP BY its SPREAD as ONE composite ROLLUP element ((f0,f1,…) — an empty-
			// name Func renders as a parenthesised tuple) so the whole reference is a single ROLLUP level; project
			// the spread under a prefix; GROUPING on the FIRST field (a ROLLUP element's fields share the flag).
			const wxString prefix = wxString::Format(wxT("gcol%d"), gi);
			const wxString base   = g->GetPhysicalName();
			std::vector<ibQueryExprPtr> spread;
			ibQueryExprPtr firstField;
			for (const wxString& f : ColumnFieldNames(g)) {
				const ibQueryExprPtr fexpr = fieldCol(f);
				if (!firstField) firstField = fexpr;
				spread.push_back(fexpr);
				projection.push_back(ibQueryProjItem{ fexpr, prefix + f.Mid(base.length()) });
			}
			groupKeys.push_back(ibFunc(wxT(""), std::move(spread)));   // composite element -> "(f0, f1, …)"
			projection.push_back(ibQueryProjItem{ ibFunc(wxT("GROUPING"), { firstField }), grpalias });
			keyPlans.push_back({ g, false, prefix, ki.meta });
		}
		groupingAliases.push_back(grpalias);
		++gi;
	}
	for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
		std::vector<ibQueryExprPtr> args;
		args.push_back(a.m_col != nullptr ? colExpr(a.m_col) : ibCol(wxT("*")));
		projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args)), a.m_alias });
	}

	ibQueryExprPtr having;
	for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having) {
		std::vector<ibQueryExprPtr> args;
		args.push_back(h.m_col != nullptr ? colExpr(h.m_col) : ibCol(wxT("*")));
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
			const KeyPlan& kp = keyPlans[i];
			ibValue v;
			if (kp.scalar) v = ReadScalarByAlias(kp.col, kp.tag, cursor);
			else           ibColumnCodec::ReadValue(kp.tag, kp.col, kp.meta, v, cursor);   // reference / variant key reassembly
			rr.groups.push_back(v);
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
		tree.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
	for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
		if (a.m_col != nullptr) tree.AddColumn(a.m_col->GetColumnId(), a.m_col->GetName(), a.m_col->GetTypeDesc());

	// Parent-before-child: process by level ascending (a level-L node's level-(L-1) parent must
	// exist). The grand total (level 0) is the root.
	std::stable_sort(rrows.begin(), rrows.end(), [](const RRow& a, const RRow& b) { return a.level < b.level; });
	std::map<wxString, ibSelectorTree::Node*> nodes;
	nodes[wxString()] = &tree.Root();
	for (const RRow& rr : rrows) {
		wxString key, parentKey;
		for (int i = 0; i < rr.level; ++i) {
			const wxString seg = rr.groups[static_cast<size_t>(i)].GetHashKey() + wxT("\x1f");   // unique per value (scalar OR reference)
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
				node->m_values[(*spec.m_groupBy)[static_cast<size_t>(i)]->GetColumnId()] = rr.groups[static_cast<size_t>(i)];
			nodes[key] = node;
		}
		for (size_t i = 0; i < rr.aggs.size() && i < spec.m_aggregates->size(); ++i)
			if (const ibBackendQueryColumn* ac = (*spec.m_aggregates)[i].m_col)
				node->m_values[ac->GetColumnId()] = rr.aggs[i];   // IN-PLACE in the aggregate's own column
	}
	return tree;
}

// Single-source ROLLUP totals push-down — SELECT … GROUP BY ROLLUP over ONE physical table.
ibSelectorTree ibDbTableProvider::ExecuteRollupTotals(const ibDataQuerySpec& spec)
	{
		const ibBackendQueryable* q = spec.m_queryable;
		const wxString mainTable = q->GetQueryTableName();

		// DOT-WALK group keys / aggregate inputs (Producer.Region) ride a reference JOIN chain, exactly like the
		// non-ROLLUP aggregate: resolve each path to a join alias + its JOINED source (whose leaf-set / metaData
		// drive the leaf's scalar test + a reference leaf's spread reassembly). A plain column keeps the main table.
		ibRefJoinChain chain(q, mainTable);
		std::map<const ibBackendQueryColumn*, std::pair<wxString, const ibBackendQueryable*>> dw;   // col -> (alias, joined source)
		bool hasDotWalk = false;
		auto resolveDot = [&](const ibBackendQueryColumn* col, const std::vector<const ibBackendQueryColumn*>& path) {
			if (col == nullptr || path.empty()) return;
			wxString a; const ibBackendQueryable* tq = nullptr;
			if (chain.Resolve(path, a, tq) && tq != nullptr) { dw[col] = { a, tq }; hasDotWalk = true; }   // gate guaranteed resolvable
		};
		for (size_t i = 0; i < spec.m_groupBy->size(); ++i)
			resolveDot((*spec.m_groupBy)[i], i < spec.m_groupPaths->size() ? (*spec.m_groupPaths)[i]
			                                                               : std::vector<const ibBackendQueryColumn*>{});
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			resolveDot(a.m_col, a.m_path);

		ibQueryRelPtr from = hasDotWalk ? chain.From() : ibScan(mainTable);
		if (ibQueryExprPtr where = ibMetaIRBuilder::BuildWhere(q, *spec.m_conditions, spec.m_predicate))
			from = ibFilter(from, where);

		// A plain column qualifies by the main table WHEN joins are present (disambiguation), else UNqualified.
		const wxString mainQual = hasDotWalk ? mainTable : wxString();
		auto qualOf = [dw, mainQual](const ibBackendQueryColumn* c) {
			const auto it = dw.find(c); return it != dw.end() ? it->second.first : mainQual;
		};
		return RunRollupTotals(spec, from,
			[qualOf](const ibBackendQueryColumn* c) {
				const wxString ql = qualOf(c);
				return ql.empty() ? ibCol(FirstSqlFieldOfColumn(c)) : ibCol(ql, FirstSqlFieldOfColumn(c));
			},
			[dw, qualOf, q](const ibBackendQueryColumn* g) -> RollupGroupKey {
				// scalar key -> one field; a reference / variant key -> its spread (composite ROLLUP element). A
				// dot-walk key qualifies by its join alias + tests scalar / reassembles against the JOINED source.
				const auto it = dw.find(g);
				const ibBackendQueryable* owner = (it != dw.end()) ? it->second.second : q;
				ColocatedLeaves ls; ls.push_back(owner);
				return { g->IsRawColumn() || ScalarReadable(g, ls), qualOf(g), owner->GetMetaData() };
			});
	}

// STRUCTURAL gate for a co-located UNION totals: every branch a real DB Source leaf, and every column
// the ROLLUP references (group keys + aggregate inputs + having + flat WHERE conditions) resolves BY
// NAME in EVERY branch and is SCALAR there — so BuildUnionRollupFrom's union-of-projections can carry
// them. A boolean WHERE TREE / RLS semi-join (m_predicate) is NOT rendered on the union-branch path, so
// its presence forces RAM (which applies it) — a co-located UNION totals is never an under-restricted read.
static bool CanColocateUnionRollupShape(const ibDataQuerySpec& spec)
{
	const ibQueryNode* root = spec.m_root;
	if (root == nullptr || root->m_kind != ibQueryNode::Kind::Union || root->m_parts.empty()) return false;
	if (!spec.m_keyIn->empty())      return false;   // a row-key IN is not a union-totals shape
	if (spec.m_predicate != nullptr) return false;   // RLS / boolean tree -> RAM (branch path renders only flat conds)

	for (const auto& part : root->m_parts) {
		const ibQueryNode* p = part.get();
		if (p == nullptr || p->m_kind != ibQueryNode::Kind::Source)         return false;
		if (p->m_queryable == nullptr || p->m_queryable->IsComputedInRam())  return false;
	}
	// Resolve-by-name + SCALAR in EVERY branch (a reference / enum column is not co-located here).
	auto okEveryBranch = [&](const ibBackendQueryColumn* c) -> bool {
		if (c == nullptr) return true;   // COUNT(*) — no input column
		for (const auto& part : root->m_parts) {
			const ibBackendQueryable* q = part->m_queryable;
			const ibBackendQueryColumn* bc = q->ResolveColumnByName(c->GetName());
			if (bc == nullptr) return false;
			ColocatedLeaves one; one.push_back(q);
			if (!ScalarReadable(bc, one)) return false;
		}
		return true;
	};
	for (const ibBackendQueryColumn* g : *spec.m_groupBy)                  if (!okEveryBranch(g))      return false;
	for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)  if (!okEveryBranch(a.m_col)) return false;
	for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having)         if (!okEveryBranch(h.m_col)) return false;
	for (const ibQueryCondition& c : *spec.m_conditions) {
		if (c.m_col == nullptr)                                          return false;   // row-key over a union — nonsensical
		if (!c.m_path.empty() || c.m_expr != nullptr || c.m_semiJoin)   return false;   // dot-walk / computed / semi-join need a join the branch path lacks
		if (!okEveryBranch(c.m_col))                                    return false;
	}
	return true;
}

// Build the derived table for a co-located UNION totals: each branch projects the ROLLUP's referenced
// columns (group keys + aggregate inputs + having) resolved BY NAME under stable inner aliases k<n>,
// UNION[/ALL]-stacked per m_partAll, filtered per branch by the flat conditions, and wrapped as a
// subquery "u". Fills `aliasOf` (referenced column -> inner alias) so the caller's colExpr references
// u.<alias>. The gate (CanColocateUnionRollupShape) guarantees every column resolves in every branch.
static ibQueryRelPtr BuildUnionRollupFrom(const ibDataQuerySpec& spec,
	std::map<const ibBackendQueryColumn*, wxString>& aliasOf)
{
	std::vector<const ibBackendQueryColumn*> cols;
	auto addCol = [&](const ibBackendQueryColumn* c) {
		if (c == nullptr) return;
		for (const ibBackendQueryColumn* e : cols) if (e == c) return;
		cols.push_back(c);
	};
	for (const ibBackendQueryColumn* g : *spec.m_groupBy)                  addCol(g);
	for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)  addCol(a.m_col);
	for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having)         addCol(h.m_col);
	for (size_t i = 0; i < cols.size(); ++i)
		aliasOf[cols[i]] = wxString::Format(wxT("k%d"), static_cast<int>(i));

	const ibQueryNode* root = spec.m_root;
	ibQueryRelPtr unionRel;
	for (size_t pi = 0; pi < root->m_parts.size(); ++pi) {
		const ibBackendQueryable* q = root->m_parts[pi]->m_queryable;
		ibQueryRelPtr rel = ibScan(q->GetQueryTableName());

		// Flat WHERE, resolved per branch by name (m_predicate is gated off -> RLS goes RAM).
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
		for (const ibBackendQueryColumn* c : cols) {
			const ibBackendQueryColumn* bc = q->ResolveColumnByName(c->GetName());   // gate-guaranteed non-null
			proj.push_back(ibQueryProjItem{ ibCol(FirstSqlFieldOfColumn(bc)), aliasOf[c] });
		}
		rel = ibProject(rel, std::move(proj));

		const bool keepDups = pi >= root->m_partAll.size() || root->m_partAll[pi];   // missing flag = ALL (back-compat)
		unionRel = unionRel ? (keepDups ? ibUnionAll(unionRel, rel) : ibUnion(unionRel, rel)) : rel;
	}
	return ibSubquery(unionRel, wxT("u"));
}

// Multi-source co-located ROLLUP totals — the SAME GROUP BY ROLLUP mechanism over a co-located
// INNER/LEFT JOIN tree. Split so the routing decision is unit-testable without a DB (the single-source
// CanPushRollupTotals conflates shape + dialect and is consequently untested):
//   CanColocateRollupTotals    — the STRUCTURAL half: a colocatable JOIN tree (CanColocateBase) with
//                                SCALAR group keys / aggregate inputs and no dot-walk / computed group
//                                or aggregate (BuildColocatedFrom renders only the .From/.Join tree, so
//                                anything needing an extra join or a non-qualifiable column RAM-folds).
//   CanPushColocatedRollupTotals — adds the DB-intrinsic ROLLUP-dialect capability. The composer
//                                dispatches on this; ExecuteColocatedRollupTotals then runs GROUP BY
//                                ROLLUP over the co-located JOIN (server-side) instead of the composer
//                                materialising both leaves and folding the totals tree in RAM.
// (docs/query-language-arc.md §22.1b)
bool ibDbTableProvider::CanColocateRollupTotals(const ibDataQuerySpec& spec)
	{
		const ibQueryNode* root = spec.m_root;
		if (root == nullptr)         return false;   // single-source -> CanRollupTotalsShape, not this gate
		if (spec.m_groupBy->empty()) return false;   // a totals query always has levels

		// Neither the JOIN nor the UNION co-located FROM renders a dot-walk / dimension join or a
		// COMPUTED group / aggregate -> those RAM-fold (which DOES handle them). (mirrors §22 honest-fail)
		for (const auto& gp : *spec.m_groupPaths) if (!gp.empty()) return false;
		if (!spec.m_dimWalks->empty())            return false;
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (!a.m_path.empty() || a.m_expr != nullptr) return false;

		// UNION tree -> the branch-union derived table; JOIN tree -> the co-located join.
		if (root->m_kind == ibQueryNode::Kind::Union)
			return CanColocateUnionRollupShape(spec);

		ColocatedLeaves leaves;
		if (!CanColocateBase(spec, leaves)) return false;   // colocatable JOIN tree, no dot-walk / key-in, single-field keys
		// Group keys: SCALAR or a REFERENCE / variant (a reference groups by its full spread as ONE composite
		// ROLLUP element, reassembled on read — RunRollupTotals handles it), each owned by a leaf.
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			if (g == nullptr || ColocatedOwner(leaves, g) == nullptr) return false;
		// SCALAR aggregate inputs.
		for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates)
			if (a.m_col != nullptr && !ScalarReadable(a.m_col, leaves)) return false;
		for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having)
			if (h.m_col != nullptr && !ScalarReadable(h.m_col, leaves)) return false;
		return true;
	}

bool ibDbTableProvider::CanPushColocatedRollupTotals(const ibDataQuerySpec& spec)
	{
		if (!CanColocateRollupTotals(spec)) return false;
		// The connected dialect must advertise ROLLUP (FB5 / PG / MySQL8; NOT SQLite -> RAM).
		ibConnectionScope scope(spec.m_holder);
		if (!scope) return false;
		return scope->GetDialect().m_features.m_rollup;
	}

ibSelectorTree ibDbTableProvider::ExecuteColocatedRollupTotals(const ibDataQuerySpec& spec)
	{
		// UNION tree -> ROLLUP over the branch-union derived table; the group / aggregate columns
		// reference its inner aliases (u.k<n>), the outer GROUP BY ROLLUP folds every subtotal level.
		if (spec.m_root != nullptr && spec.m_root->m_kind == ibQueryNode::Kind::Union) {
			std::map<const ibBackendQueryColumn*, wxString> aliasOf;
			ibQueryRelPtr from = BuildUnionRollupFrom(spec, aliasOf);
			return RunRollupTotals(spec, from,
				[&aliasOf](const ibBackendQueryColumn* c) {
					const auto it = aliasOf.find(c);
					return ibCol(wxT("u"), it != aliasOf.end() ? it->second : FirstSqlFieldOfColumn(c));
				},
				[](const ibBackendQueryColumn*) -> RollupGroupKey { return { true, wxString(), nullptr }; });   // CanColocateRollupTotals guarantees SCALAR group keys (a co-located reference ROLLUP still RAM-folds)
		}

		// JOIN tree -> ROLLUP over the co-located join; qualify each column by its owning leaf's table.
		ColocatedLeaves leaves;
		ColocatableJoinTree(spec, leaves);   // the gate already validated; refill the leaf set
		ibQueryRelPtr from = BuildColocatedFrom(spec.m_root, leaves);
		if (ibQueryExprPtr where = ColocatedWhere(spec, leaves))
			from = ibFilter(from, where);
		return RunRollupTotals(spec, from,
			[&leaves](const ibBackendQueryColumn* c) { return ibCol(ColocatedQual(leaves, c), FirstSqlFieldOfColumn(c)); },
			[&leaves](const ibBackendQueryColumn* g) -> RollupGroupKey {
				// scalar key -> one field; a reference / variant key -> its spread under the leaf's qualifier,
				// reassembled via the owning leaf's metaData.
				const ibBackendQueryable* o = ColocatedOwner(leaves, g);
				return { g->IsRawColumn() || ScalarReadable(g, leaves), ColocatedQual(leaves, g), o ? o->GetMetaData() : nullptr };
			});
	}

// Bind a write column's value positionally: a RAW column straight by its declared RawType (no
// translation — the uuid guid just goes in as a string); a metadata column via the TYPE-tagged
// SetValueColumn decomposition (over the column's type descriptor + the metadata context). The
// ONLY place SetValueColumn is called.
static void BindWriteValue(ibQueryStatement& stmt, const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& v, int& pos)
{
	if (col->IsRawColumn()) {
		switch (static_cast<const ibRawDBColumn*>(col)->GetRawType()) {
			case ibRawDBColumn::RawType::String:    stmt.SetParamString(pos++, v.GetString());  break;
			case ibRawDBColumn::RawType::Guid:      stmt.SetParamString(pos++, v.GetString());  break;
			case ibRawDBColumn::RawType::Number:    stmt.SetParamNumber(pos++, v.GetNumber());  break;
			case ibRawDBColumn::RawType::Date:      stmt.SetParamDate  (pos++, v.GetDate());    break;
			case ibRawDBColumn::RawType::Boolean:   stmt.SetParamBool  (pos++, v.GetBoolean()); break;
			case ibRawDBColumn::RawType::Reference: stmt.SetParamString(pos++, v.GetString());  break;   // TODO: real blob bind
			case ibRawDBColumn::RawType::Blob:      stmt.SetParamString(pos++, v.GetString());  break;   // TODO: real blob bind
		}
		return;
	}
	ibColumnCodec::WriteValue(col, metaData, v, &stmt, pos);
}

// The L2 write CORE — buried in the provider. Identity is the WHERE section (spec conditions):
// each condition column (a RAW uuid column, or a register's primary-key attribute) = value.
// The SetValue() data rides ONLY for INSERT / UPSERT; DELETE is WHERE-only. INSERT/UPSERT write
// every assignment column; UPSERT matches on the IsPrimaryKey ones (the raw uuid reports
// IsPrimaryKey, so it both inserts and matches; a register's composite key is several columns).
// Each column expands + binds through WriteFieldsOf / BindWriteValue (raw direct vs attribute
// decomposition). One ibQueryStatement, run once.
long ibDbTableProvider::ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind)
	{
		using WriteKind = ibDataQueryBuilder::WriteKind;
		const wxString table     = spec.m_queryable->GetQueryTableName();
		const ibMetaData* metaData = spec.m_queryable->GetMetaData();   // context for the column-based field spread / bind

		if (kind == WriteKind::Delete) {
			// DELETE ... WHERE <row-key conditions> AND <folded RLS predicate>. BuildWhere lowers BOTH
			// m_conditions (the row-key — a null-col condition maps to RowKeyField) AND m_predicate (where
			// the RLS Where folds). The flat loop we used before read only m_conditions and IGNORED
			// m_predicate, so the restriction never reached the DELETE and was silently unenforced.
			// Unqualified columns (single-table DELETE, no alias).
			ibQueryExprPtr where = ibMetaIRBuilder::BuildWhere(spec.m_queryable, *spec.m_conditions,
			                                                   spec.m_predicate, wxEmptyString, /*pathAsExists*/ true);
			ibDatabaseQueryBuilder q(spec.m_holder);
			try { return q.Execute(ibDelete(table, where)); }   // rows deleted; 0 under a policy = no accessible row
			catch (...) { return -1; }
		}

		// INSERT / UPSERT — columns = every SetValue() assignment; UPSERT matches the IsPrimaryKey ones.
		const ibQueryStatement::Kind l2kind =
			(kind == WriteKind::Upsert) ? ibQueryStatement::Kind::Upsert : ibQueryStatement::Kind::Insert;

		std::vector<wxString> columns;
		for (const auto& wv : *spec.m_writeValues)
			for (const wxString& f : ColumnFieldNames(wv.first)) columns.push_back(f);

		// UPSERT match keys = the source's uniqueness key, OWNED by the queryable through
		// GetPrimaryKeyColumns: a record's data-reference (_RTRef+_RRRef — _RTRef is constant for
		// a monomorphic self-reference, so the match is effectively on the unique _RRRef blob), a
		// register's recorder+line+period / period+dimensions. NOT the uuid (that stays the read
		// keyset / DELETE key, a second link key until cleaned). The conflict target needs a unique
		// index on these fields — see CreateAndUpdateTableDB. Not scanned off the values.
		std::vector<wxString> matchKeys;
		if (kind == WriteKind::Upsert || kind == WriteKind::Update)
			for (const ibBackendQueryColumn* col : spec.m_queryable->GetPrimaryKeyColumns())
				for (const wxString& f : ColumnFieldNames(col)) matchKeys.push_back(f);

		if (kind == WriteKind::Update) {
			// REWRITE — ONE guarded UPDATE: SET the columns, WHERE the primary key AND the folded RLS
			// predicate. 0 rows affected under a policy -> the builder denies (the row filter excluded this
			// row). This is what enforces a write Restrict on the object main row; a plain UPSERT has no
			// WHERE and would ignore the folded predicate. One statement, the row count is the answer.
			ibQueryStatement upd(ibQueryStatement::Kind::Update, table, columns, matchKeys, spec.m_holder);
			int p = 1;
			for (const auto& wv : *spec.m_writeValues)
				BindWriteValue(upd, wv.first, metaData, wv.second, p);
			if (spec.m_predicate)
				upd.SetWherePredicate(ibMetaIRBuilder::BuildPredicateExpr(spec.m_queryable, spec.m_predicate, wxEmptyString, /*pathAsExists*/ true));
			return upd.RunQuery();
		}

		// WITH CHECK on CREATE — a restricted INSERT. The folded RLS predicate rides on a derived ONE-ROW
		// relation of this row's own values: INSERT INTO t (cols) SELECT * FROM (SELECT val AS f, …
		// [FROM dual]) src WHERE <rls over src>. The row is inserted IFF it satisfies the restriction; 0
		// rows -> the new row is outside this role's scope -> the builder denies. One statement, entirely in
		// the DB. Reuses the write value-spread (Const capture) exactly as DecomposeEquality, so the
		// projected bytes match what the predicate compares against; the src alias qualifies the predicate.
		if (kind == WriteKind::Insert && spec.m_predicate) {
			std::vector<ibQueryProjItem> projItems;
			for (const auto& wv : *spec.m_writeValues) {
				const std::vector<wxString> fields = ColumnFieldNames(wv.first);
				ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
				int cp = 1;
				ibColumnCodec::WriteValue(wv.first, metaData, wv.second, &capture, cp);
				const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
				for (size_t i = 0; i < fields.size(); ++i) {
					ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
					projItems.push_back(ibQueryProjItem{ c, fields[i] });   // this row's value AS <field>
				}
			}
			ibQueryRelPtr valuesRow = ibProject(nullptr, std::move(projItems));               // SELECT val AS f… [FROM dual]
			ibQueryExprPtr rls = ibMetaIRBuilder::BuildPredicateExpr(spec.m_queryable, spec.m_predicate, wxT("src"), /*pathAsExists*/ true);
			ibQueryRelPtr checked = ibFilter(ibSubquery(valuesRow, wxT("src")), rls);          // SELECT * FROM (…) src WHERE rls
			ibDatabaseQueryBuilder q(spec.m_holder);
			try { return q.Execute(ibInsertSelect(table, columns, checked)); }                 // 0 inserted -> WITH CHECK denied
			catch (...) { return -1; }
		}

		ibQueryStatement statement(l2kind, table, columns, matchKeys, spec.m_holder);
		int position = 1;
		for (const auto& wv : *spec.m_writeValues)
			BindWriteValue(statement, wv.first, metaData, wv.second, position);

		try { return statement.RunQuery(); }        // rows inserted / upserted
		catch (...) { return -1; }
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

		// A reference dot-walk join is needed for a projected path (SelectPath), a path FILTER
		// (Where(path,…)) OR a path SORT (OrderBy(path,…)). One join chain serves all three.
		auto anyPath = [](const auto& items) {
			for (const auto& it : items) if (!it.m_path.empty()) return true;
			return false;
		};
		const bool hasComputed = spec.m_selectExprs != nullptr && !spec.m_selectExprs->empty();
		const bool hasDimWalk  = spec.m_dimWalks != nullptr && !spec.m_dimWalks->empty();
		const bool hasDotWalk = !spec.m_dotWalks->empty() || anyPath(*spec.m_conditions) || anyPath(effective)
		                        || PredicateHasPath(spec.m_predicate) || hasComputed || hasDimWalk;
		const wxString mainQual = hasDotWalk ? mainTable : wxString();

		ibDatabaseQueryBuilder q(spec.m_holder);

		ibQueryExprPtr dotWalkWhere;                          // flat path-condition predicate (qualified by join alias)
		ibQueryExprPtr treeWhere;                             // boolean predicate tree (dot-walk leaves qualified by join alias)
		std::map<const ibQuerySortItem*, wxString> sortAlias; // path-sort -> its join alias (built before From)
		std::map<const ibQuerySortItem*, ibQueryExprPtr> sortExpr; // COMPOSITE path-sort -> its COALESCE leaf expr

		if (hasDotWalk) {
			// The reference dot-walk join chain — shared by the projection, the path filters, and the path
			// sorts (a prefix joined once is reused). MUST run before q.From() (it mutates the from-tree).
			ibRefJoinChain chain(queryable, mainTable);
			auto resolvePath = [&chain](const std::vector<const ibBackendQueryColumn*>& path,
			                            wxString& outAlias, const ibBackendQueryable*& outTarget) {
				return chain.Resolve(path, outAlias, outTarget);
			};

			// A dot-walk through an EMPTY or broken reference must read its target attribute's TYPED EMPTY
			// empty value, not SQL NULL — a typed empty reference has empty attributes. The LEFT JOIN
			// yields NULL on a non-match, so the projection coalesces it: CASE WHEN col IS NULL THEN <empty>
			// ELSE col END. Only PLAIN SCALAR leaves (string/number/date/bool, single type) — a reference /
			// enum / composite leaf is a single-field read already and stays NULL (multi-type dot-walk = a
			// separate feature). The empty literal is the attribute type's own AdjustValue() empty.
			auto scalarEmpty = [](const ibBackendQueryColumn* leaf) -> ibQueryExprPtr {
				const ibTypeDescription& td = leaf->GetTypeDesc();
				if (td.GetClsidCount() != 1) return nullptr;   // composite -> deferred
				if (td.ContainType(ibValueTypes::TYPE_NUMBER) || td.ContainType(ibValueTypes::TYPE_DATE)
					|| td.ContainType(ibValueTypes::TYPE_BOOLEAN) || td.ContainType(ibValueTypes::TYPE_STRING))
					return ibConst(ibValueTypeDescription::AdjustValue(td));
				return nullptr;   // reference / enum leaf -> deferred
			};

			// A dot-walk path with a COMPOSITE (multi-type) reference at ANY segment, as ONE scalar SQL
			// expression. Recursively walk from the main table: a SINGLE-target ref → one LEFT JOIN, continue;
			// a COMPOSITE ref → FORK, one LEFT JOIN + recursive tail per target type. Each branch contributes
			// its RAW leaf field (NULL on a non-matching join); the whole is COALESCE(branch1, …, <typed-empty>)
			// so a row reads its one matched branch's value, else the empty value. Each segment is re-resolved BY NAME per
			// branch (the path columns were resolved against the representative type at lowering). Returns
			// nullptr for a PURE single-target path (caller keeps the existing qualified-alias path / full spread)
			// or a non-scalar leaf. (docs/query-language-arc.md §22.4b)
			auto pathCompositeScalarExpr = [&](const std::vector<const ibBackendQueryColumn*>& path) -> ibQueryExprPtr {
				if (path.size() < 2) return nullptr;
				ibQueryExprPtr empty = scalarEmpty(path.back());
				if (!empty) return nullptr;                                   // non-scalar leaf — not handled here

				bool sawComposite = false;
				std::function<void(const wxString&, const ibBackendQueryable*, size_t, std::vector<ibQueryExprPtr>&)> walk =
					[&](const wxString& ownerQual, const ibBackendQueryable* ownerQ, size_t seg, std::vector<ibQueryExprPtr>& out) {
						if (ownerQ == nullptr) return;
						const ibBackendQueryColumn* col = ownerQ->ResolveColumnByName(path[seg]->GetName());
						if (col == nullptr) return;                           // this branch's type lacks the attribute — skip
						if (seg + 1 == path.size()) {                         // LEAF — raw scalar field (NULL on non-match)
							out.push_back(ibCol(ownerQual, FirstSqlFieldOfColumn(col)));
							return;
						}
						if (const ibBackendQueryable* single = ownerQ->GetProvider().ResolveReferenceTarget(ownerQ, col)) {
							const wxString f = SelfReferenceField(single);
							if (f.empty()) return;
							walk(chain.AddLeftJoin(single->GetQueryTableName(), ownerQual, FirstSqlFieldOfColumn(col), f),
								single, seg + 1, out);
							return;
						}
						sawComposite = true;
						// A REGISTER's Recorder is a composite of MANY document types (15+); a field
						// pulled through it often exists on only ONE. Join ONLY the types that actually have the next
						// segment — no point in 15 LEFT JOINs for a field on 1. The deeper tail still self-skips.
						for (const ibBackendQueryable* tq : ownerQ->GetProvider().ResolveReferenceTargets(ownerQ, col)) {
							if (tq->ResolveColumnByName(path[seg + 1]->GetName()) == nullptr) continue;
							const wxString f = SelfReferenceField(tq);
							if (f.empty()) continue;
							walk(chain.AddLeftJoin(tq->GetQueryTableName(), ownerQual, FirstSqlFieldOfColumn(col), f),
								tq, seg + 1, out);
						}
					};

				std::vector<ibQueryExprPtr> out;
				walk(mainTable, queryable, 0, out);
				if (!sawComposite || out.empty()) return nullptr;            // pure single-target — caller's path
				out.push_back(empty);
				return out.size() == 1 ? out.front() : ibFunc(wxT("COALESCE"), out);
			};
			auto condOp = [](const ibQueryCondition& c) { return FilterOpToBinOp(c.m_op); };

			std::vector<ibQueryProjItem> projection;
			projection.push_back(ibQueryProjItem{ ibCol(mainTable, wxT("*")), wxString() });
			for (const ibDotWalkColumn& dw : *spec.m_dotWalks) {
				const std::vector<const ibBackendQueryColumn*>& fp = dw.m_path;
				if (fp.size() < 2) continue;
				const ibBackendQueryColumn* leaf = fp.back();

				// COMPOSITE reference ANYWHERE in the path + a SCALAR leaf -> ONE COALESCE expression. THIS is
				// the register Recorder case: a recorder is a composite of MANY document types (15+) and the
				// pulled field exists on only one — the walk joins ONLY the types that have it and COALESCEs.
				// nullptr for a pure single-target path or a non-scalar leaf (handled below).
				if (ibQueryExprPtr e = pathCompositeScalarExpr(fp)) {
					projection.push_back(ibQueryProjItem{ e, dw.m_alias });
					continue;
				}

				// PURE single-target path: a scalar leaf (typed-empty coalesce) OR a reference / enum leaf
				// (project its FULL field spread under the alias prefix, reassembled via GetValueColumn).
				{
					wxString a; const ibBackendQueryable* tq = nullptr;
					if (resolvePath(fp, a, tq) && tq != nullptr) {
						if (ibQueryExprPtr empty = scalarEmpty(leaf)) {
							ibQueryExprPtr colE = ibCol(a, FirstSqlFieldOfColumn(leaf));
							projection.push_back(ibQueryProjItem{ ibCase({ { ibIsNull(colE), empty } }, colE), dw.m_alias });
						}
						else {
							const wxString    base = leaf->GetPhysicalName();
							for (const wxString& f : ColumnFieldNames(leaf))
								projection.push_back(ibQueryProjItem{ ibCol(a, f), dw.m_alias + f.Mid(base.length()) });
						}
						continue;
					}
				}

				// COMPOSITE reference anywhere in the path + a NON-scalar leaf (reference / enum /
				// composite): each branch joins its target type and contributes the leaf's FULL field
				// spread; the spreads merge PER SUFFIX with COALESCE under the alias prefix (a row
				// matches at most one branch, so exactly one branch's fields are non-null). The reader
				// (GetColumnObject) reassembles the object off the merged spread exactly like a
				// single-target full-spread projection. Suffix alignment rides the REPRESENTATIVE leaf
				// (the first branch — the same type the lowering resolved the path against); a branch
				// whose leaf lacks a representative suffix simply skips that COALESCE argument.
				{
					// Collect the leaf occurrences — the same recursive walk as the scalar case (composite
					// fork + peek optimisation), but yielding (join alias, branch leaf column, branch
					// target) instead of expressions.
					struct LeafOcc { wxString m_alias; const ibBackendQueryColumn* m_col; const ibBackendQueryable* m_q; };
					std::vector<LeafOcc> occs;
					bool sawComposite = false;
					std::function<void(const wxString&, const ibBackendQueryable*, size_t)> collect =
						[&](const wxString& ownerQual, const ibBackendQueryable* ownerQ, size_t seg) {
							if (ownerQ == nullptr) return;
							const ibBackendQueryColumn* col = ownerQ->ResolveColumnByName(fp[seg]->GetName());
							if (col == nullptr) return;
							if (seg + 1 == fp.size()) {
								occs.push_back(LeafOcc{ ownerQual, col, ownerQ });
								return;
							}
							if (const ibBackendQueryable* single = ownerQ->GetProvider().ResolveReferenceTarget(ownerQ, col)) {
								const wxString f = SelfReferenceField(single);
								if (f.empty()) return;
								collect(chain.AddLeftJoin(single->GetQueryTableName(), ownerQual, FirstSqlFieldOfColumn(col), f),
									single, seg + 1);
								return;
							}
							sawComposite = true;
							for (const ibBackendQueryable* tq : ownerQ->GetProvider().ResolveReferenceTargets(ownerQ, col)) {
								if (tq->ResolveColumnByName(fp[seg + 1]->GetName()) == nullptr) continue;
								const wxString f = SelfReferenceField(tq);
								if (f.empty()) continue;
								collect(chain.AddLeftJoin(tq->GetQueryTableName(), ownerQual, FirstSqlFieldOfColumn(col), f),
									tq, seg + 1);
							}
						};
					collect(mainTable, queryable, 0);
					if (!sawComposite || occs.empty())
						continue;   // unresolvable path — same skip as before (the read yields the empty value)

					// Per-occurrence field spreads, cached; the representative drives the suffix list.
					std::vector<std::vector<wxString>> occFields;
					occFields.reserve(occs.size());
					for (const LeafOcc& o : occs)
						occFields.push_back(ColumnFieldNames(o.m_col));

					const wxString repBase = occs.front().m_col->GetPhysicalName();
					for (const wxString& repField : occFields.front()) {
						const wxString suffix = repField.Mid(repBase.length());
						std::vector<ibQueryExprPtr> args;
						for (size_t oi = 0; oi < occs.size(); ++oi) {
							const wxString branchField = occs[oi].m_col->GetPhysicalName() + suffix;
							for (const wxString& bf : occFields[oi])
								if (bf == branchField) { args.push_back(ibCol(occs[oi].m_alias, branchField)); break; }
						}
						if (args.empty()) continue;
						projection.push_back(ibQueryProjItem{
							args.size() == 1 ? args.front() : ibFunc(wxT("COALESCE"), args),
							dw.m_alias + suffix });
					}
				}
			}
			// computed columns (arithmetic / CASE) — lower the L3 expression tree, project AS its alias.
			if (hasComputed)
				for (const ibQueryColumnSelect& sc : *spec.m_selectExprs)
					projection.push_back(ibQueryProjItem{ ibMetaIRBuilder::BuildColumnExpr(queryable, sc.m_expr, mainQual), sc.m_alias });

			// dot-walk TOTALS dimensions — join the path, project the leaf's SCALAR value under the dimension's
			// DISTINCT alias (dw.m_alias), so it never clashes with the main table's same-named field on a
			// self-reference (Parent.Code). A synthetic dimension column (physname == alias) reads it, and the
			// fold groups by that column's own unique id. Scalar leaves only (the lowering rejects others).
			if (hasDimWalk)
				for (const ibDotWalkColumn& dw : *spec.m_dimWalks) {
					wxString a; const ibBackendQueryable* tq = nullptr;
					if (!resolvePath(dw.m_path, a, tq) || tq == nullptr) continue;
					const ibBackendQueryColumn* leaf = dw.m_path.back();
					ibQueryExprPtr colE  = ibCol(a, FirstSqlFieldOfColumn(leaf));
					ibQueryExprPtr empty = scalarEmpty(leaf);   // typed empty for an empty / broken parent ref
					projection.push_back(ibQueryProjItem{
						empty ? ibCase({ { ibIsNull(colE), empty } }, colE) : colE, dw.m_alias });
				}

			// path WHERE — qualified by the leaf's join alias; BuildConditionExpr on the TARGET queryable
			// (composite / reference-safe via DecomposeEquality), the alias standing in for mainQual.
			// An unresolvable path (a composite segment with a NON-scalar leaf in the condition) THROWS —
			// silently dropping the condition would widen the filter (wrong rows).
			for (const ibQueryCondition& c : *spec.m_conditions) {
				if (c.m_path.empty()) continue;
				if (c.m_asExists) {   // RLS semi-join: a correlated EXISTS (filters once/zero per row) — NOT a JOIN alias, so it cannot multiply
					dotWalkWhere = AndFold(dotWalkWhere, ibMetaIRBuilder::BuildConditionExpr(queryable, c, mainQual));
					continue;
				}
				if (ibQueryExprPtr lhs = pathCompositeScalarExpr(c.m_path)) {   // composite scalar leaf -> COALESCE <op> value
					dotWalkWhere = AndFold(dotWalkWhere, ibBinOp(condOp(c), lhs, ibConst(c.m_value)));
					continue;
				}
				wxString a; const ibBackendQueryable* tq = nullptr;
				if (!resolvePath(c.m_path, a, tq) || tq == nullptr)
					throw std::logic_error("BuildPageIR: a dot-walk WHERE on a composite non-scalar leaf is not yet supported");
				dotWalkWhere = AndFold(dotWalkWhere, ibMetaIRBuilder::BuildConditionExpr(tq, c, a));
			}

			// path ORDER BY — pre-build the joins now; the alias / composite expr is consumed (in effective
			// order) below. Composite leaf -> a COALESCE expression; single-target -> its join alias. An
			// unresolvable path (composite + non-scalar leaf) THROWS — a silently dropped sort key would
			// return mis-ordered rows.
			for (const ibQuerySortItem& s : effective) {
				if (s.m_path.empty()) continue;
				if (ibQueryExprPtr e = pathCompositeScalarExpr(s.m_path)) { sortExpr[&s] = e; continue; }
				wxString a; const ibBackendQueryable* tq = nullptr;
				if (!resolvePath(s.m_path, a, tq))
					throw std::logic_error("BuildPageIR: a dot-walk ORDER BY on a composite non-scalar leaf is not yet supported");
				sortAlias[&s] = a;
			}

			// boolean predicate TREE — lowered HERE (before From) so a dot-walk leaf joins via resolvePath.
			// A path leaf qualifies by its join alias (BuildConditionExpr on the target); a plain leaf by
			// mainQual. Mirrors BuildPredicateExpr but path-aware. An unresolvable path leaf THROWS (never
			// drop — a dropped OR branch would widen the filter, wrong rows).
			std::function<ibQueryExprPtr(const ibQueryPredicatePtr&)> lowerTree =
				[&](const ibQueryPredicatePtr& p) -> ibQueryExprPtr {
				if (!p) return nullptr;
				switch (p->m_kind) {
				case ibQueryPredicateKind::Leaf: {
					if (!p->m_leaf.m_path.empty()) {
						if (p->m_leaf.m_asExists)   // RLS semi-join: correlated EXISTS (filters, no multiply) — NOT a JOIN alias
							return ibMetaIRBuilder::BuildConditionExpr(queryable, p->m_leaf, mainQual);
						if (ibQueryExprPtr lhs = pathCompositeScalarExpr(p->m_leaf.m_path))   // composite scalar leaf
							return ibBinOp(condOp(p->m_leaf), lhs, ibConst(p->m_leaf.m_value));
						wxString a; const ibBackendQueryable* tq = nullptr;
						if (!resolvePath(p->m_leaf.m_path, a, tq) || tq == nullptr)
							throw std::logic_error("BuildPageIR: a dot-walk WHERE-tree leaf did not resolve its join");
						return ibMetaIRBuilder::BuildConditionExpr(tq, p->m_leaf, a);
					}
					return ibMetaIRBuilder::BuildConditionExpr(queryable, p->m_leaf, mainQual);
				}
				case ibQueryPredicateKind::And: {
					ibQueryExprPtr acc;
					for (const ibQueryPredicatePtr& c : p->m_children) acc = AndFold(acc, lowerTree(c));
					return acc;
				}
				case ibQueryPredicateKind::Or: {
					ibQueryExprPtr acc;
					for (const ibQueryPredicatePtr& c : p->m_children) acc = OrFold(acc, lowerTree(c));
					return acc;
				}
				case ibQueryPredicateKind::Not: {
					ibQueryExprPtr in = p->m_children.empty() ? nullptr : lowerTree(p->m_children.front());
					return in ? ibNot(in) : nullptr;
				}
				case ibQueryPredicateKind::IsNull: {
					wxString qual = mainQual;   // a dot-walk IS NULL qualifies by its join alias, else main table
					if (!p->m_path.empty()) {
						wxString a; const ibBackendQueryable* tq = nullptr;
						if (!resolvePath(p->m_path, a, tq) || tq == nullptr)
							throw std::logic_error("BuildPageIR: a dot-walk IS NULL did not resolve its join");
						qual = a;
					}
					ibQueryExprPtr allNull;
					if (p->m_col != nullptr)
						for (const wxString& f : ColumnValueFields(p->m_col))
							allNull = AndFold(allNull, ibIsNull(ibColQ(qual, f), false));
					if (!allNull) return nullptr;
					return p->m_negated ? ibNot(allNull) : allNull;
				}
				}
				return nullptr;
			};
			treeWhere = lowerTree(spec.m_predicate);

			q.From(chain.From());
			q.Project(std::move(projection));
		}
		else {
			q.From(mainTable);
		}

		if (req.m_hierarchyFilter && !req.m_flatScan) {
			// The envelope carries the parent COLUMN; the physical field derives HERE
			// — the field machinery is the provider's job, not the consumer's.
			const wxString parentField = ReferenceFieldOf(req.m_hierarchyCol);
			q.Where(ibMetaIRBuilder::BuildParentRefPredicate(
				queryable, parentField, req.m_hierarchyKey, req.m_isTopLevel, mainQual));
		}

		// WHERE = flat plain conditions  AND  the boolean tree  AND  the flat dot-walk conditions.
		// hasDotWalk: the tree was lowered above (path-aware, treeWhere). Else: lower it here by mainQual.
		ibQueryExprPtr where = ibMetaIRBuilder::BuildFilterPredicate(queryable, *spec.m_conditions, mainQual);
		where = AndFold(where, hasDotWalk ? treeWhere
		                                  : ibMetaIRBuilder::BuildPredicateExpr(queryable, spec.m_predicate, mainQual));
		where = AndFold(where, dotWalkWhere);
		// (An RLS semi-join rides as a payload CONDITION in spec.m_conditions → BuildFilterPredicate above →
		//  BuildConditionExpr renders it as a correlated EXISTS. No per-site append — it covers every WHERE path.)
		if (where)
			q.Where(where);

		if (!spec.m_keyIn->empty())
			if (auto keyPred = ibMetaIRBuilder::BuildKeyInPredicate(queryable, *spec.m_keyIn, mainQual))
				q.Where(keyPred);

		if (hasAnchor)
			q.Where(ibMetaIRBuilder::BuildAnchorPredicate(queryable, effective, req.m_anchorSortValues, req.m_direction, mainQual));

		if (hasDotWalk) {
			// Emit sort keys in effective ORDER, interleaving plain (main-table) and dot-walk (joined) sorts.
			for (const ibQuerySortItem& s : effective) {
				if (s.m_col == nullptr) continue;
				const bool asc = req.m_reverseSort ? !s.m_ascending : s.m_ascending;
				if (!s.m_path.empty()) {
					ibQuerySortKey k;
					auto se = sortExpr.find(&s);
					if (se != sortExpr.end()) {
						k.m_expr = se->second;                                  // composite leaf -> COALESCE(...) sort
					}
					else {
						auto it = sortAlias.find(&s);
						if (it == sortAlias.end()) continue;   // unresolvable path -> skip (matches projection's skip)
						k.m_expr = ibCol(it->second, FirstSqlFieldOfColumn(s.m_col));   // single-target -> alias.leafField
					}
					k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
					q.AddSortKey(std::move(k));
				}
				else {
					for (const wxString& name : ColumnValueFields(s.m_col)) {
						ibQuerySortKey k;
						k.m_expr = ibCol(mainQual, name);
						k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
						q.AddSortKey(std::move(k));
					}
				}
			}
		}
		else {
			for (const ibQuerySortKey& key : ibMetaIRBuilder::BuildSortKeys(queryable, effective, req.m_reverseSort, mainQual))
				q.AddSortKey(key);
		}

		if (req.m_count > 0) q.Limit(req.m_count);   // count <= 0 = unbounded (full scan, e.g. FindValue)

		ibQueryIR ir = q.Build();
		if (spec.m_distinct) ir.m_root = ibDistinct(ir.m_root);   // SELECT DISTINCT — SELECT DISTINCT over the read
		ir.m_lockForUpdate = req.m_lockForUpdate;   // pessimistic register set lock — dialect appends the clause
		return ir;
	}

// External binds — EMPTY. The anchor keyset EMBEDS its values (ibConst for scalars, ibConstBlob for a reference
// key's real _RRRef blob) so a reference compares by BINARY, not a stringified param. Nothing rides a positional
// external param; the page is anchored entirely by the embedded predicate.
std::vector<ibValue> ibDbTableProvider::BuildExternal(const ibReadPageRequest& /*req*/,
	                                          const std::vector<ibQuerySortItem>& /*effective*/)
	{
		return std::vector<ibValue>();
	}

// --- attribute convenience adapters — the attribute IS a column, metaData is its own. The value
//     codec lives on the column-layout tier (ibColumnCodec); these adapters just supply the
//     attribute's own metaData. The plain SetValueColumn / GetValueColumn forwarders were inlined
//     to ibColumnCodec::WriteValue / ReadValue at their call sites — no second name for the tier.
void ibDbTableProvider::SetValueAttribute(const ibValueMetaObjectAttributeBase* attr, const ibValue& cValue, ibQueryStatement* statement, int& position)
{
	ibColumnCodec::WriteValue(attr, attr->GetMetaData(), cValue, statement, position);
}

bool ibDbTableProvider::GetValueAttribute(const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return ibColumnCodec::ReadValue(attr, attr->GetMetaData(), retValue, result, createData);
}

bool ibDbTableProvider::GetValueAttribute(const wxString& fieldName, ibFieldTypes fieldType,
	const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return ibColumnCodec::ReadField(fieldName, static_cast<int>(fieldType), attr, attr->GetMetaData(), retValue, result, createData);
}

// ==========================================================================
// Reference dot-walk target resolution — THE metadata home in the provider layer (moved here from
// ibRecordQueryable so the query-provider layer names no metadata). A single-target reference COLUMN
// resolves off `queryable`'s metadata context: the clsid KIND (bit-check, no metadata lookup) says
// reference, then metaData->GetTypeCtor vends the reference ctor and ctor->GetQueryable() the target
// queryable by VIRTUAL dispatch — NO cast, no RTTI on this dot-walk hot path. The base provider returns
// null; the computed provider forwards here. (docs/query-language-arc.md §22 dot-walk; kind-typing)
// ==========================================================================
const ibBackendQueryable* ibDbTableProvider::ResolveReferenceTarget(const ibBackendQueryable* queryable,
                                                                    const ibBackendQueryColumn* refColumn) const
{
	if (queryable == nullptr || refColumn == nullptr)
		return nullptr;
	const ibTypeDescription& td = refColumn->GetTypeDesc();
	if (td.GetClsidList().size() != 1)
		return nullptr;                                  // polymorphic / non-typed -> use ResolveReferenceTargets
	const ibClassID clsid = td.GetFirstClsid();
	if (!IsReference(clsid))
		return nullptr;                                  // KIND straight off the clsid bits — skip non-refs, no metadata lookup
	const ibMetaData* metaData = queryable->GetMetaData();
	if (metaData == nullptr)
		return nullptr;                                  // a temp / metadata-free source has no reference reconstruction
	const ibCtorMetaValueType* ctor = metaData->GetTypeCtor(clsid);
	return ctor != nullptr ? ctor->GetQueryable() : nullptr;   // reference ctor -> target queryable (virtual dispatch, no cast)
}

// ALL reference targets of a COLUMN — one queryable per reference type in the column's (possibly
// composite) type. Loops the whole CLSID list: a composite "Catalog.A or Catalog.B" yields both
// queryables; a non-reference alternative (bit-check) and a target vending no queryable are skipped.
std::vector<const ibBackendQueryable*> ibDbTableProvider::ResolveReferenceTargets(const ibBackendQueryable* queryable,
                                                                                  const ibBackendQueryColumn* refColumn) const
{
	std::vector<const ibBackendQueryable*> targets;
	if (queryable == nullptr || refColumn == nullptr)
		return targets;
	const ibMetaData* metaData = queryable->GetMetaData();
	if (metaData == nullptr)
		return targets;
	for (const ibClassID& clsid : refColumn->GetTypeDesc().GetClsidList()) {
		if (!IsReference(clsid))
			continue;                                    // a non-reference alternative of the composite type
		const ibCtorMetaValueType* ctor = metaData->GetTypeCtor(clsid);
		if (ctor != nullptr)
			if (const ibBackendQueryable* q = ctor->GetQueryable())   // virtual dispatch to the reference ctor — no cast
				targets.push_back(q);
	}
	return targets;
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
	ibColumnCodec::ReadValue(col, metaData, v, result);
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
				case ibRawDBColumn::RawType::String:    return ibValue(m_cursor.GetResultString(f));
				case ibRawDBColumn::RawType::Guid:      return ibValue(m_cursor.GetResultString(f));
				case ibRawDBColumn::RawType::Number:    return ibValue(m_cursor.GetResultNumber(f));
				case ibRawDBColumn::RawType::Date:      return ibValue(m_cursor.GetResultDate(f));
				case ibRawDBColumn::RawType::Boolean:   return ibValue(m_cursor.GetResultBool(f));
				case ibRawDBColumn::RawType::Reference: return ibValue(m_cursor.GetResultString(f));   // TODO: real blob
				case ibRawDBColumn::RawType::Blob:      return ibValue(m_cursor.GetResultString(f));   // TODO: real blob
			}
			return ibValue();
		}
		return ProviderReadColumn(col, m_metaData, m_cursor);   // metadata column — column-based assembly
	}

	ibValue Column(const wxString& alias) const override { return m_cursor.GetValue(alias); }

	// A dot-walk leaf that is a reference / enum / composite is projected as its FULL field spread under
	// `prefix` (<prefix>_TYPE/_RTRef/_RRRef/…) — reassemble the object value off those fields, exactly as a
	// normal metadata column reads. A non-matching join (empty / broken ref) leaves the fields null, so the
	// reassembly yields the type's empty value on its own.
	ibValue ColumnObject(const wxString& prefix, const ibBackendQueryColumn* col) const override {
		if (col == nullptr)
			return ibValue();
		ibValue v;
		ibColumnCodec::ReadValue(prefix, col, m_metaData, v, m_cursor);
		return v;
	}

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
