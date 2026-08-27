////////////////////////////////////////////////////////////////////////////
//	Description : ibDbTableProvider — the BIG DB provider's implementation: the
//	              real-table read / cached / aggregate / write engine, the name-
//	              substitution lowering (ibMetaIRBuilder -> physical L2-1 ibQueryIR),
//	              and the static GET / WRITE value templates (GetValueAttribute /
//	              SetValueAttribute). The ONLY place L2-1 and the attribute field-
//	              machinery meet for a physical table. Split out of queryProvider.cpp
//	              (which keeps the composer / computed provider / result sources).
//	              See docs/query-language-arc.md §18, §22.
////////////////////////////////////////////////////////////////////////////

#include "dbTableProvider.h"   // ibDbTableProvider + ibRenderedPageCache (+ queryProvider.h / databaseQueryBuilder.h / metaAttributeObject.h)
#include "dataQueryBuilder.h"  // ibDataQueryBuilder::EffectiveSort / ibDataQueryResult / ibReadPageRequest / ibDataQuerySpec / ibDotWalkColumn
#include "resultSource.h"      // ibDataResultSource — the selection backing ibDbResultSource derives
#include "columnLayout.h"      // the column-layout tier: DescribeColumnLayout + ibColumnCodec (value codec) + HasReference
#include "queryException.h"    // ibBackendQueryException — L3-L5 varieties (it used to arrive through the DB header)

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/valueInfo.h"                                    // ibReference (physical reference blob, GetQueryTableId source)
#include "backend/fnumber.h"                                      // ibNumber — the _RTRef type (clsid) keyset tiebreak const
#include "backend/metaData.h"                                     // ibMetaData (threaded through reads/writes)
#include "backend/objCtor.h"                                      // ibCtorMetaValueType::GetQueryable — reference-target resolution (clsid -> ctor -> queryable, no cast)
#include "backend/system/value/valueType.h"                      // ibValueTypeDescription::AdjustValue (dot-walk typed empty)
#include "backend/system/value/valueGuid.h"                      // ibValueGuid — a raw row key reads back AS a guid

#include <map>            // dot-walk join dedup
#include <unordered_map>  // ROLLUP node index — keyed by the group values themselves
#include <vector>
#include <algorithm>    // std::find / std::remove — column-list housekeeping (the ROLLUP rows are no longer sorted here: the server orders them)
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
// => the column is a reference and yields its _RRRef (pure guid) blob.
// The column's first VALUE field — the one a scalar comparison / sort / anchor rides. It is the
// layout tier's answer verbatim: this used to walk the clsid list and spell the suffix itself, a
// second copy of the B/N/D/S/E/RRRef ordering that only agreed with the codec's bind order for as
// long as nobody edited one of the two. Ask the slot; a caller that needs to know WHAT the field is
// reads the role beside it rather than the tail of the name.
ibColumnSlot FirstValueSlotOf(const ibBackendQueryColumn* col)
{
	ibColumnSlot slot = FirstValueSlot(col);
	if (slot.m_name.IsEmpty())
		slot.m_name = col->GetPhysicalName();   // a column that spreads to nothing IS its own field
	return slot;
}

wxString FirstSqlFieldOfColumn(const ibBackendQueryColumn* col)
{
	return FirstValueSlotOf(col).m_name;
}

// Whether the column's value is a REFERENCE — its identity is (guid, type), so an ordering or an
// anchor over it needs the _RTRef clsid as a tiebreak.
bool IsReferenceValued(const ibBackendQueryColumn* col)
{
	return FirstValueSlotOf(col).m_role == ibColumnRole::ReferenceId;
}

// ⭐⭐ THE ROW KEY IS THE PRIMARY KEY, ASKED FOR AS ONE.
//
// This used to read the LAST item of the identity sort and call it the key — "the identity's tail is
// the unique tiebreaker, so for a catalog it IS the uuid column". True while that sort ended with the
// key, and silently false the moment a source gained an ordering of its own: an enumeration sorts by
// Order first and by its reference second, so the tail became a NUMBER, and everything that took a
// key from here took a number instead. The same guess stood in six manager reads and cost a day.
//
// The primary key is the authority on what identifies a row, it is asked by name, and no rearranged
// sort can move it. A source with a COMPOSITE key (a register: recorder + line + period) has no single
// row-key column, and answering nullptr is correct — those paths (row-key condition, key-IN) are
// single-key by construction and a register never takes them.
const ibBackendQueryColumn* RowKeyColumn(const ibBackendQueryable* queryable)
{
	const std::vector<const ibBackendQueryColumn*> keys = queryable->GetPrimaryKeyColumns();
	return keys.size() == 1 ? keys.front() : nullptr;
}

wxString RowKeyField(const ibBackendQueryable* queryable)
{
	const ibBackendQueryColumn* key = RowKeyColumn(queryable);
	return key == nullptr ? wxString() : FirstSqlFieldOfColumn(key);
}

// ⭐⭐ A VALUE COMPARED AGAINST A COLUMN IS SPELLED IN THAT COLUMN'S FORM.
//
// A guid reaches this layer as TEXT — that is how a script writes it and how the runtime carries it
// — while the column stores sixteen bytes. Handed over as text it would compare 36 characters
// against a binary key and match NOTHING: not an error, not an empty result with a reason, just a
// row that is there and is never found. So the const is built FOR THE COLUMN, in one helper every
// comparison goes through, rather than at each site remembering which columns are keys.
//
// The reference twin of this is ReferenceKeyBlob further down; both produce the same storage byte
// order, which is what makes `uuid = <ref>_RRRef` a real binary compare.
// Encode a REFERENCE value into its physical _RRRef blob for a keyset compare — SELF-CONTAINED, no metadata:
// the target metaID is the clsid BODY (dynamic reference clsid = kind|metaID), the guid comes off the value's
// own reference object, laid out + byte-swapped EXACTLY as the stored _RRRef (mirrors BuildParentRefPredicate).
// So `_RRRef OP <blob>` is a real BINARY compare — same bytes the column stores, same order ORDER BY _RRRef
// sorts. Returns nullptr when v is not a reference (a scalar rides inline).
ibQueryExprPtr ReferenceKeyBlob(const ibValue& v)
{
	ibValueReferenceDataObject* refObj = nullptr;
	if (!v.ConvertToValue(refObj) || refObj == nullptr)
		return nullptr;
	// The _RRRef blob is the PURE guid (type is the _RTRef column, compared separately if a query needs it —
	// but a globally-unique guid identifies the object on its own). Write the guid in the field-normalized
	// storage byte order so `_RRRef OP blob` agrees byte-for-byte with the server's ORDER BY _RRRef.
	ibReference ref{ ibGuidImpl{} };
	const auto& be = refObj->GetGuid().GetGuid().bytes();
	auto* p = reinterpret_cast<unsigned char*>(&ref.m_guid);
	p[0] = be[3]; p[1] = be[2]; p[2] = be[1]; p[3] = be[0];
	p[4] = be[5]; p[5] = be[4];
	p[6] = be[7]; p[7] = be[6];
	for (int i = 8; i < 16; ++i) p[i] = be[i];
	return ibConstBlob(&ref, sizeof(ibReference));
}

ibQueryExprPtr ColumnConst(const ibBackendQueryColumn* col, const ibValue& v)
{
	if (col == nullptr)
		return ibConst(v);

	// ⭐ DOES THIS COLUMN HOLD SIXTEEN BYTES OF IDENTITY? A raw key / reference column says so by its
	// declared RawType; a metadata REFERENCE ATTRIBUTE says so by its first value slot being the
	// reference id - and both are then compared as bytes, never as text.
	//
	// The attribute case matters since a reference object is identified BY ITS REFERENCE rather than
	// by a row-key scaffold beside it: the key lookup then names an attribute here, and answering it
	// with the guid's text compared 36 characters against a sixteen-byte field - no error, no empty
	// result with a reason, just a row that is there and is never found.
	const bool identityValued = col->IsRawColumn()
		? (static_cast<const ibBackendColumnRawDB*>(col)->GetRawType() == ibBackendColumnRawDB::RawType::Guid
			|| static_cast<const ibBackendColumnRawDB*>(col)->GetRawType() == ibBackendColumnRawDB::RawType::Reference)
		: IsReferenceValued(col);
	if (!identityValued)
		return ibConst(v);

	// The value may arrive either way: a reference (a script wrote `WHERE Section.Ref = doc`) or a
	// guid handed round on its own; both spell the same key.
	if (ibQueryExprPtr fromReference = ReferenceKeyBlob(v))
		return fromReference;

	const ibReference key{ GuidOf(v) };
	return ibConstBlob(&key, sizeof(ibReference));
}

// The dot-walk self-reference field — the Reference-typed (_RRRef pure guid blob) physical field
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

// Decompose a COLUMN equality into per-physical-field terms, REUSING the write decomposition: the
// COLUMN spreads the value across its own fields (BindWriteValue — the one bind door), and a
// capture-only statement records each as a Const node. A multi-field key (composite / variant /
// reference dimension) thus filters on ALL its fields, AND-folded. The statement is never run — a
// pure value sink.
//
// ⚠ ASKED OF THE COLUMN, NOT OF THE CODEC. The codec is the DEFAULT answer and not the only one: a
// column that knows better overrides it, and the MOMENT does — it has no value tag for the codec to
// drive off, so going straight there bound NULL into every field and the filter matched nothing.
// (Same rule as the write path, which has gone through this door all along.)
ibQueryExprPtr DecomposeEquality(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& value,
                                 const wxString& mainQual = wxEmptyString)
{
	std::vector<wxString> fields = ColumnFieldNames(col);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int position = 1;
	BindWriteValue(capture, col, metaData, value, position);
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
//
// ⭐⭐ AND THIS IS WHAT `WHERE Moment <= &Point` IS. The moment is a date and the record standing at it, so
// "up to this document" is exactly a lexicographic compare over the date's field and the reference's pair —
// which its layout already names, in that order. Nothing here knows about moments: a column said what it is
// made of and what its halves are worth, and the general rule read like the special one.
ibQueryExprPtr DecomposeOrdered(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& value,
                                ibQueryBinOp op, const wxString& mainQual = wxEmptyString)
{
	std::vector<wxString> fields = ColumnFieldNames(col);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int position = 1;
	BindWriteValue(capture, col, metaData, value, position);
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
	// `In` is SET-valued and has no binary counterpart — BuildConditionExpr branches on it BEFORE calling
	// this, so the mapping is unreachable there. Listed only to keep the switch exhaustive (the Eq answer
	// would be wrong for a caller that skipped that branch, which is why no such caller may exist: the
	// dot-walk condOp path below is the only other user and the producer never emits a dot-walk `In`).
	case ibQueryFilterOp::In:           return ibQueryBinOp::Eq;
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
// …and the same job for a WINDOW's call, which has three more members than the folds — the ranking
// functions. One table, in the tier that owns SQL spelling.
wxString WindowFnName(ibQueryWindowFn fn)
{
	switch (fn) {
	case ibQueryWindowFn::Sum:       return wxT("SUM");
	case ibQueryWindowFn::Count:     return wxT("COUNT");
	case ibQueryWindowFn::Min:       return wxT("MIN");
	case ibQueryWindowFn::Max:       return wxT("MAX");
	case ibQueryWindowFn::Avg:       return wxT("AVG");
	case ibQueryWindowFn::RowNumber: return wxT("ROW_NUMBER");
	case ibQueryWindowFn::Rank:      return wxT("RANK");
	case ibQueryWindowFn::DenseRank: return wxT("DENSE_RANK");
	}
	return wxT("SUM");
}

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
// conditions / sorts (columns) into physical L2-1 ibQueryIR fragments. Per-family
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
	// One door condition -> L2-1 expr (the per-leaf body shared by the AND-fold above and the tree below).
	static ibQueryExprPtr BuildConditionExpr(const ibBackendQueryable* queryable,
	                                         const ibQueryCondition& c,
	                                         const wxString& mainQual = wxEmptyString,
	                                         bool pathAsExists = false);
	// The full boolean WHERE TREE -> L2-1 expr (And/Or/Not/IsNull; leaves via BuildConditionExpr).
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
	// A COMPUTED-COLUMN expression (arithmetic / CASE) -> L2-1 expr (ibBinOp / ibCase; columns qualified
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

// Read a SCALAR output column of a co-located join row off the L2-1 cursor by its projection
// ALIAS (SELECT qual.field AS alias). A single projected field, no _TYPE/_RRRef spread, so this
// covers primitive + raw columns only (reference / enum columns are excluded upstream by
// CanColocateJoin, which falls back to RAM).
//
// ⭐ A RAW COLUMN IS ASKED, NOT DECODED HERE. It reads itself by its declared type, and the switch
// that used to stand in this function was that same one written a second time — with a difference
// nobody could see from the callsite: a GUID key came back as a STRING here while the column writes
// and reads it as sixteen bytes, so an identity read through this door was not the identity the same
// column had bound. The alias IS the field name to the cursor, which is exactly what ReadValue takes.
//
// The primitive branch below stays, and stays here: a metadata column projected by the co-located
// path is ONE field under the alias, so asking the column would send it through the codec's full
// spread and look for `<alias>_TYPE` fields the projection never wrote.
ibValue ReadScalarByAlias(const ibBackendQueryColumn* col, const wxString& alias,
                          const ibMetaData* metaData, ibQueryResult& cursor)
{
	ibValue v;
	if (col->IsRawColumn()) {
		col->ReadValue(alias, metaData, v, cursor);
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

// ⛔ THE AUTO-JOIN KEY DERIVATION STOOD HERE, and it is gone with the thing that fed it.
//
// It answered the question "these two tables were joined with no ON — which columns did the author
// mean?" by looking for a column on one side whose reference resolves to the other. That question no
// longer exists: a JOIN in this language always carries its ON, and two tables with nothing said
// between them are a PRODUCT written with a comma. The only producer of a keyless, non-cross join
// node was `ibDataQueryBuilder::Join(queryable, kind, alias)`, removed with it.
//
// Worth remembering WHY it went rather than just that it did: the derivation made the same query text
// mean different things depending on what the metadata happened to hold — add a reference between two
// catalogs and a query that multiplied them yesterday joins them today, silently.

// ONE join node's keys — explicit on-columns, and nothing else. False when they are not both there
// (a cross join, or a computed ON), which the co-location gate reads as "not this fast path".
bool ResolveNodeKeys(const ibQueryNode* node, const ibBackendQueryColumn*& onL, const ibBackendQueryColumn*& onR)
{
	onL = node->m_on.m_colL; onR = node->m_on.m_colR;
	return onL != nullptr && onR != nullptr;
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

// Build the L2-1 FROM tree (nested ibJoin) from the L3 join node — RECURSIVE, any depth. A Source ->
// ibScan(table); a Join -> its two children joined on the node's resolved keys, qualified by the
// owning leaf's table. INNER/LEFT per node. The shared co-located FROM both fast paths build over.
ibQueryRelPtr BuildColocatedFrom(const ibQueryNode* node, const ColocatedLeaves& leaves)
{
	if (node->m_kind == ibQueryNode::Kind::Source) {
		// A source may BE a derived table rather than a table — a register's balance is an
		// aggregate over its totals view, parameterised by a date that no view can hold. Asking
		// keeps the join in SQL; assuming a table name would force the whole thing into RAM first.
		if (ibQueryRelPtr rel = node->m_queryable->GetSourceRelation(node->m_queryable->GetQueryTableName()))
			return rel;
		return ibScan(node->m_queryable->GetQueryTableName());
	}

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

	// SET-valued `In` (the semi-join key reduction) — reads m_values, not m_value, so it MUST branch before
	// FilterOpToBinOp below, which would answer Eq and then compare against an unset m_value.
	if (c.m_op == ibQueryFilterOp::In) {
		// A METADATA column takes the SAME route Eq does, once per value, OR-folded. Not because the field
		// name would be wrong — FirstSqlFieldOfColumn already skips the _TYPE discriminator and picks the
		// first primitive slot, which is right for a single-primitive column. It is because of the other two
		// things the spread carries: the _TYPE discriminator (a VARIANT column would otherwise match rows of
		// the wrong variant, since a native IN compares one primitive field and ignores the tag), and the
		// correctly-encoded value (a REFERENCE needs its write-spread _RRRef blob, NOT a bare ibConst — the
		// same trap the Ne branch below spells out). Guarded on non-empty: an empty OR-fold comes back null,
		// i.e. NO predicate — which matches EVERYTHING, the exact opposite of the empty-set meaning.
		if (c.m_col != nullptr && !c.m_col->IsRawColumn() && !c.m_values.empty()) {
			ibQueryExprPtr pred;
			for (const ibValue& v : c.m_values)
				pred = OrFold(pred, DecomposeEquality(c.m_col, queryable->GetMetaData(), v, mainQual));
			return pred;
		}
		// Single-field lhs: the row's own key when m_col is null (same shape BuildKeyInPredicate renders for
		// .WhereKeyIn(), one IN instead of an OR-chain), else the column's first physical field. An EMPTY set
		// falls through here ON PURPOSE — L2-1 already renders `x IN ()` as `1 = 0`
		// (QueryRenderer.In_EmptyListIsConstantFalse), so "matches nothing" stays decided in ONE place.
		const ibBackendQueryColumn* inCol = c.m_col != nullptr ? c.m_col : RowKeyColumn(queryable);
		std::vector<ibQueryExprPtr> vals;
		vals.reserve(c.m_values.size());
		for (const ibValue& v : c.m_values)
			vals.push_back(ColumnConst(inCol, v));
		return ibIn(ibColQ(mainQual, c.m_col == nullptr ? RowKeyField(queryable) : FirstSqlFieldOfColumn(c.m_col)),
		            std::move(vals));
	}

	const ibQueryBinOp op = FilterOpToBinOp(c.m_op);   // ONE op (m_comparison + m_explicitOp collapsed into m_op)

	if (c.m_expr) {
		// COMPUTED left-hand side (WHERE Qty * Price > value) — lower the expression tree and
		// compare to the value. Checked BEFORE the null-column branch: an expr condition carries
		// m_col == null but is NOT a row-key lookup.
		return ibBinOp(op, BuildColumnExpr(queryable, c.m_expr, mainQual), ibConst(c.m_value));
	}
	if (c.m_col == nullptr) {
		// Row-key condition — a lookup by the row's own key (uuid, the identity tail), never
		// LIKE. No GetRowKeyColumn: the key field comes off the PRIMARY KEY like any column.
		return ibBinOp(op, ibColQ(mainQual, RowKeyField(queryable)),
		               ColumnConst(RowKeyColumn(queryable), c.m_value));
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
	return ibBinOp(op, ibColQ(mainQual, FirstSqlFieldOfColumn(c.m_col)), ColumnConst(c.m_col, c.m_value));
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
// a CANONICAL ibColumnType, NOT a SQL string: the L2-1 renderer spells it per-DBMS through the dialect
// TYPE-MAP (ibQueryRenderer::MapType), so the SQLite date-affinity (TEXT), boolean (INTEGER) and the
// FB narrow-DECIMAL forks are all closed at render time — the one place that owns the dialect.
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
		// A NAMED field wins over the first one: a composite column read field by field is the only way
		// a value spread across several of them survives an expression (see ibQueryColumnExpr::ColField).
		return expr->m_col != nullptr
			? ibColQ(mainQual, expr->m_field.IsEmpty() ? FirstSqlFieldOfColumn(expr->m_col) : expr->m_field)
			: nullptr;

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

	case ibQueryColumnExprKind::PeriodTrunc:
		// Straight through to the L2-1 node of the same name — the dialect's truncation map does the
		// spelling. Nothing engine-specific reaches this far up, which is the point: a totals rebuild
		// groups the movements by the very expression the maintenance trigger keys rows with.
		return ibPeriodTrunc(BuildColumnExpr(queryable, expr->m_lhs, mainQual), expr->m_periodUnit);

	case ibQueryColumnExprKind::WindowAgg: {
		// ⭐⭐ THE ONE FIGURE OF A REPORT THE SERVER CAN COMPUTE WITHOUT A PUSH-DOWN OF THE FOLD.
		// `SUM(x) OVER (PARTITION BY <levels above it>)` returns a value per ROW, so no ROLLUP and no
		// GROUPING SETS are involved — and the clause is spelled by the one renderer every driver
		// already shares (ibRenderOverClause). An EMPTY partition is `OVER ()`: the whole result,
		// which is what a share of the report is measured against.
		// ⚠ A RANKING CALL TAKES NO ARGUMENT — `ROW_NUMBER()`, `RANK()`, `DENSE_RANK()` — and that is
		// carried by the absence of one rather than by a flag: an expression with nothing to fold
		// simply has no input, and the same code writes both families.
		std::vector<ibQueryExprPtr> args;
		if (const ibQueryExprPtr arg = BuildColumnExpr(queryable, expr->m_lhs, mainQual))
			args.push_back(arg);
		ibQueryWindow window;
		for (const ibQueryColumnExprPtr& key : expr->m_partition)
			if (const ibQueryExprPtr k = BuildColumnExpr(queryable, key, mainQual))
				window.m_partitionBy.push_back(k);
		for (const auto& key : expr->m_windowOrder)
			if (const ibQueryExprPtr k = BuildColumnExpr(queryable, key.first, mainQual))
				window.m_orderBy.push_back(ibQuerySortKey{ k, key.second ? ibQuerySortDir::Asc
				                                                         : ibQuerySortDir::Desc });
		// ⭐ THE CONCEPT IS SPELLED HERE, and nowhere above. L3 names the call and the frame in its own
		// words; what they are written as belongs to this tier, exactly as a period truncation does.
		window.m_frame = expr->m_windowFrame == ibQueryWindowFrame::Rows  ? ibQueryFrame::RowsThroughCurrent
		               : expr->m_windowFrame == ibQueryWindowFrame::Range ? ibQueryFrame::RangeThroughPeers
		                                                                  : ibQueryFrame::NoFrame;
		return ibWindowed(ibFunc(WindowFnName(expr->m_windowFn), std::move(args)), std::move(window));
	}
	}
	return nullptr;
}

std::vector<ibQuerySortKey> ibMetaIRBuilder::BuildSortKeys(const ibBackendQueryable* queryable,
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
		if (s.m_expr) {   // ORDER BY <expression> (CASE / arithmetic / value) — lower the L3 expr to L2-1, sort on it
			ibQuerySortKey k;
			k.m_expr = BuildColumnExpr(queryable, s.m_expr, mainQual);
			k.m_dir  = (reverse ? !s.m_ascending : s.m_ascending) ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			keys.push_back(std::move(k));
			continue;
		}
		if (s.m_col == nullptr) continue;
		if (!s.m_path.empty()) continue;   // dot-walk sort — emitted inline by BuildPageIR (qualified by its join alias)
		const bool asc = reverse ? !s.m_ascending : s.m_ascending;
		for (const wxString& name : ColumnValueFields(s.m_col)) {
			ibQuerySortKey k;
			k.m_expr = ibColQ(mainQual, name);
			k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			keys.push_back(std::move(k));
		}
		// A reference's identity is (guid, type). ColumnValueFields gives the _RRRef guid; append the _RTRef
		// type as the tiebreak so an empty reference (all-zero guid) of type A orders distinctly from one of
		// type B — in lockstep with BuildAnchorPredicate and CompareValueLS (reference clsids order by metaID).
		// A single-type / self-reference column has a constant _RTRef, so this is a harmless no-op there.
		if (IsReferenceValued(s.m_col)) {
			ibQuerySortKey k;
			k.m_expr = ibColQ(mainQual, s.m_col->GetPhysicalName() + ibFieldSuffix(ibColumnRole::ReferenceType));
			k.m_dir  = asc ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			keys.push_back(std::move(k));
		}
	}

	return keys;
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
	// The keyset compares on the SAME fields BuildSortKeys drives the ORDER BY with, so keyset and sort agree
	// (a divergent field set re-reads the page head = duplicates). A REFERENCE contributes TWO fields — its
	// _RRRef guid then its _RTRef type — so the anchor value (a reference) encodes to its guid BLOB
	// (ReferenceKeyBlob — a true binary reference compare) plus its clsid; a scalar rides inline as ibConst.
	std::vector<SortCol> cols;
	for (const ibQuerySortItem& s : sorts) {
		if (s.m_col == nullptr) continue;
		if (!s.m_path.empty()) continue;   // dot-walk sort is not a keyset anchor key (joined column, not on the main scan)
		cols.push_back({ s.m_col, s.m_ascending });
	}

	auto strictOp    = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Gt : ibQueryBinOp::Lt; };
	auto inclusiveOp = [&](bool asc) { return (forward == asc) ? ibQueryBinOp::Ge : ibQueryBinOp::Le; };

	auto valueAt = [&](size_t i) -> ibValue { return i < values.size() ? values[i] : ibValue(); };
	auto operand = [&](const ibBackendQueryColumn* col, const ibValue& v) -> ibQueryExprPtr {
		if (ibQueryExprPtr blob = ReferenceKeyBlob(v)) return blob;   // reference -> real _RRRef binary blob
		return ColumnConst(col, v);                                   // the raw uuid key -> its bytes; else inline
	};

	// Flatten each sort column into its comparison TERMS: a scalar is one term; a REFERENCE is two — the
	// _RRRef guid, then the _RTRef type (its clsid) as the tiebreak — matching BuildSortKeys' ORDER BY and
	// CompareValueLS (reference clsids order by metaID). Both terms read the SAME anchor reference value.
	struct Term { ibQueryExprPtr field; ibQueryExprPtr operand; bool asc; };
	std::vector<Term> terms;
	for (size_t i = 0; i < cols.size(); ++i) {
		const ibBackendQueryColumn* col = cols[i].col;
		const bool     asc = cols[i].asc;
		const ibValue  v   = valueAt(i);
		terms.push_back({ ibColQ(mainQual, FirstSqlFieldOfColumn(col)), operand(col, v), asc });
		if (IsReferenceValued(col)) {
			ibValueReferenceDataObject* refObj = nullptr;
			const ibClassID clsid = (v.ConvertToValue(refObj) && refObj != nullptr) ? refObj->GetClassType() : 0;
			terms.push_back({ ibColQ(mainQual, col->GetPhysicalName() + ibFieldSuffix(ibColumnRole::ReferenceType)),
			                  ibConst(ibValue(ibNumber(clsid))), asc });
		}
	}

	auto eqUpTo = [&](size_t kExclusive) -> ibQueryExprPtr {
		ibQueryExprPtr eq;
		for (size_t j = 0; j < kExclusive; ++j)
			eq = AndFold(eq, ibBinOp(ibQueryBinOp::Eq, terms[j].field, terms[j].operand));
		return eq;
	};

	ibQueryExprPtr predicate;
	for (size_t i = 0; i < terms.size(); ++i) {
		const bool isLast = (i + 1 == terms.size());
		const ibQueryBinOp op =
			(inclusiveTail && isLast) ? inclusiveOp(terms[i].asc) : strictOp(terms[i].asc);
		ibQueryExprPtr clause = AndFold(eqUpTo(i), ibBinOp(op, terms[i].field, terms[i].operand));
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
	// _RRRef blob (ReferenceKeyBlob — the pure guid; a globally-unique guid identifies the parent within the
	// table); a non-reference key rides inline (ibConst). This is the SAME encoding the keyset anchor uses.
	if (!isTopLevel) {
		if (ibQueryExprPtr blob = ReferenceKeyBlob(parentKey))
			return ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField), blob);
		return ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField), ibConst(parentKey));
	}
	// Root level: the row has NO PARENT — and that has TWO spellings in the column, so the predicate must
	// accept both or it silently hides rows.
	//
	//   * the zero-guid SENTINEL, written by the paths that store an empty reference explicitly;
	//   * SQL NULL, in a row where the field was never written — a column added by a restructuring to
	//     rows that already existed, or a row written before the hierarchy column was there at all.
	//
	// ⚠⚠ IT USED TO BE THE SENTINEL ALONE, as a binary equality — and `NULL = <blob>` is UNKNOWN, not
	// true, so every null-parent row fell out of the ROOT level. Flat mode carries no parent predicate,
	// so the same list showed everything flat and lost part of itself the moment it became a tree: rows
	// that belong to nobody, visible nowhere, with no folder to expand to find them. Reported exactly
	// that way — "as soon as the hierarchy appears, some elements disappear".
	//
	// All rows of this single hierarchy table share the type (the _RTRef column), so the _RRRef test
	// alone identifies the roots. (A non-reference hierarchy's roots would compare inline; none exist.)
	ibReference ref{ ibGuidImpl{} };
	ibQueryExprPtr rootTest = OrFold(
		ibIsNull(ibColQ(mainQual, refDataField), false),
		ibBinOp(ibQueryBinOp::Eq, ibColQ(mainQual, refDataField),
		        ibConstBlob(&ref, sizeof(ibReference))));

	// ⚠ A THIRD SPELLING EXISTS IN OLD BASES, AND IT IS NOT THIS PREDICATE'S JOB TO ABSORB IT.
	//
	// Rows were found whose parent field holds neither NULL nor the sentinel this builds. Measured,
	// not guessed: in a base created before `sizeof(ibReference)` went 20 → 16 (the metaID moved out
	// to the _RTRef column), the column is still 20 bytes wide and every value written back then
	// carries the metaID in its tail — an EMPTY parent reads `00…00 ec030000`, a real one
	// `<guid> ec030000`. Today's code binds 16 bytes, the server pads with zeroes, and the compare
	// matches rows written since and none written before. Those rows are invisible in the tree,
	// unwritable (the rewrite keys on the same column) and yet readable and deletable.
	//
	// It was tempting to widen the ROOT test until they fell into it — the first attempt did exactly
	// that with `OR NOT EXISTS (SELECT … FROM <same table> WHERE key = <main>.<parent>)`, emptied the
	// list completely and was reverted (the outer scan carries no alias here, so the correlated
	// reference bound to the subquery's own table). It would also have been the wrong cure: the
	// parent is not dangling, it is EMPTY in an older layout, and a per-row existence subquery on
	// every page forever is a heavy price for a defect that belongs to a moment in the past.
	//
	// The two real answers, both elsewhere:
	//   * a genuine dangling parent — a folder that was deleted — no longer happens: deleting an
	//     object clears the reference to itself in its children, in the same transaction
	//     (ibValueRecordDataObjectRef::DeleteData);
	//   * old-layout rows are a MIGRATION, which is what the declared compatibility version exists
	//     for (docs/compatibility-version.md). A change to a physical constant with data already in
	//     the field needs a rung and a branch — not a predicate that carries the past forever.
	return rootTest;
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
// it through L2-1. A single static instance serves every DB queryable (GetProvider).
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

// ⭐⭐ THE READ, AS A RELATION — the projection twin of BuildAggregateRelation.
//
// `BuildPageIR` already builds and does not run, so this is the same lowering with the page taken
// off: an UNBOUNDED request. That is not an omission, it is the point — paging belongs to whoever
// composes with this relation, and a LIMIT baked in here would silently cap a join's input to one
// screenful of rows, which reads as "the register has 40 movements".
//
// ⚠ NOR IS THE SORT: the effective sort is still computed (a keyset read needs a total order to
// build its predicate against), but nothing anchors it here — there is no cursor to continue.
ibQueryRelPtr ibDbTableProvider::BuildReadRelation(const ibDataQuerySpec& spec)
	{
		const std::vector<ibQuerySortItem> effective =
			ibDataQueryBuilder::EffectiveSort(spec.m_queryable, *spec.m_sorts);

		ibReadPageRequest whole;
		whole.m_count     = 0;                          // 0 = unbounded — the composer pages, not this
		whole.m_direction = ibFetchDirection::Reset;    // no anchor: there is no cursor being continued
		whole.m_flatScan  = true;
		whole.m_isTopLevel = true;

		return BuildPageIR(spec, whole, effective).m_root;
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
void ibDbTableProvider::BuildAggregateQuery(const ibDataQuerySpec& spec, ibDatabaseQueryBuilder& q)
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

		if (auto predicate = ibMetaIRBuilder::BuildWhere(queryable, *spec.m_conditions, spec.m_predicate, mainQual))
			q.Where(predicate);

		std::vector<ibQueryProjItem> projection;
		for (size_t gi = 0; gi < spec.m_groupBy->size(); ++gi) {
			// A COMPUTED key (GroupByExpr) occupies the same slot with a null column: lower the tree
			// and project it under its alias. One GROUP BY item, not a field spread — an expression
			// has no reference typing to reconstruct, so the reader takes it by alias.
			if (spec.m_groupExprs != nullptr && gi < spec.m_groupExprs->size() && (*spec.m_groupExprs)[gi]) {
				const ibQueryExprPtr gexpr =
					ibMetaIRBuilder::BuildColumnExpr(queryable, (*spec.m_groupExprs)[gi], mainQual);
				q.GroupBy(gexpr);
				projection.push_back(ibQueryProjItem{ gexpr, (*spec.m_groupAliases)[gi] });
				continue;
			}
			const ibBackendQueryColumn* gcol = (*spec.m_groupBy)[gi];
			if (gcol == nullptr)
				continue;   // a null column with no expression is an empty slot — nothing to group by
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
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args), a.m_distinct), a.m_alias });
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
	}

// ⭐⭐ RUN WHAT WAS ASSEMBLED FROM **THIS SPEC** — the statement, and the declarations it stands on.
//
// `q.Execute()` renders the fluent state alone, and a spec's `WITH` list is not part of it: the door
// holds the named queries, the IR carries them, and the fluent builder never sees either. Every
// terminal that ran the fluent state directly therefore dropped them — which nothing noticed while
// the only declared source was a package's named result, read by the page road. It stops being
// invisible the moment an ordinary nested source is declared instead (queryLowering ResolveFrom): the
// engine is handed `FROM q_sub0` with nothing declaring `q_sub0`, and answers "table unknown".
//
// One door rather than the same three lines in five terminals: build the IR, attach what the spec
// declared, run that. A terminal keeps saying what it assembles; this says how a spec is run.
static ibQueryResult RunSpecStatement(const ibDataQuerySpec& spec, ibDatabaseQueryBuilder& q)
{
	ibQueryIR ir = q.Build();
	ibDbTableProvider::AttachNamedQueries(spec, ir);
	return q.ExecuteIR(ir);
}

ibDataQueryResult ibDbTableProvider::ExecuteAggregate(const ibDataQuerySpec& spec)
	{
		ibDatabaseQueryBuilder q(spec.m_holder);
		BuildAggregateQuery(spec, q);
		return ibDataQueryResult(RunSpecStatement(spec, q), spec.m_queryable);
	}

// ⭐⭐ THE SAME ASSEMBLY, STOPPED ONE STEP EARLIER. `Build()` renders nothing and touches no
// connection — it hands back the relation tree the execute path would have rendered — so a reading
// that answers `GetSourceRelation` with this puts its whole GROUP BY inside somebody else's FROM
// instead of materialising rows first.
//
// ⚠ The alias is NOT applied here. Whoever asked owns the name it goes under (the provider wraps a
// source relation as `FROM (<this>) AS <alias>`), and stamping one in the middle would be a second
// place deciding what this table is called.
ibQueryRelPtr ibDbTableProvider::BuildAggregateRelation(const ibDataQuerySpec& spec)
	{
		ibDatabaseQueryBuilder q(spec.m_holder);
		BuildAggregateQuery(spec, q);
		return q.Build().m_root;
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
// Defined below, beside the read that uses it — the gate and the execution ask the same question.
static bool CollectExprColumns(const ibQueryColumnExpr* e, std::vector<const ibBackendQueryColumn*>& into);

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
		// ⭐ A COMPUTED OUTPUT NO LONGER SENDS THE WHOLE JOIN TO RAM.
		//
		// It used to: `Qty * Price` in the select list meant every leaf was read whole and stitched in
		// memory, because the co-located projection had no way to write the expression. But the join
		// is what costs — the arithmetic is one evaluation per OUTPUT row either way, and there are
		// fewer of those than there are rows in the leaves. So the JOIN co-locates as it would have
		// without the expression, and the expression is evaluated over the joined rows that come back
		// (ExecuteColocatedJoin, the same evaluator the RAM road uses on the same shape).
		//
		// What is NOT claimed: the expression is not rendered into the server's select list — it is
		// computed here, over what came back. Its INPUTS are projected (they have to be, or it would
		// evaluate against absent cells), and a shape the walk does not fully understand keeps the RAM
		// road rather than being half-served.
		if (spec.m_selectExprs != nullptr)
			for (const ibQueryColumnSelect& sc : *spec.m_selectExprs) {
				std::vector<const ibBackendQueryColumn*> used;
				if (!CollectExprColumns(sc.m_expr.get(), used)) return false;
				for (const ibBackendQueryColumn* c : used)
					if (ColocatedOwner(leaves, c) == nullptr) return false;   // an input no leaf owns
			}
		if (spec.m_selectCols->empty())                              return false;

		// OUTPUTS: any column is projectable — a RAW column reads one scalar field, a metadata column its
		// FULL spread (reference / enum / variant reconstruct via GetValueColumn). Just require each is
		// owned by a leaf (so it qualifies).
		for (const auto& sc : *spec.m_selectCols)
			if (sc.first == nullptr || ColocatedOwner(leaves, sc.first) == nullptr) return false;
		return true;
	}

// ⭐⭐ WHAT AN EXPRESSION READS — every column it touches, and whether the walk understood all of it.
//
// A computed output over a co-located join is evaluated on the rows that COME BACK, so those rows owe
// it its inputs: `Qty * Price AS Total` needs Qty and Price in the projection even when nobody asked
// to see them. The same rule the declaration writer follows one layer up, asked here of an expression
// instead of of a query.
//
// It returns `false` when it meets something it does not know — a kind added later, a Case whose
// predicate carries columns of its own. False means "do not co-locate", never "no columns": a walk
// that quietly under-reports would project too little and evaluate against absent cells, which reads
// as an empty value rather than as a refusal.
static bool CollectExprColumns(const ibQueryColumnExpr* e, std::vector<const ibBackendQueryColumn*>& into)
{
	if (e == nullptr)
		return true;
	switch (e->m_kind) {
	case ibQueryColumnExprKind::Const:
		return true;
	case ibQueryColumnExprKind::Column:
		// ONE NAMED FIELD of a column is not the column's value — the evaluator reads a whole cell by
		// column id and answers empty for a field-pinned read, so that shape does not co-locate.
		if (e->m_col == nullptr || !e->m_field.IsEmpty())
			return false;
		if (std::find(into.begin(), into.end(), e->m_col) == into.end())
			into.push_back(e->m_col);
		return true;
	case ibQueryColumnExprKind::Arith:
		return CollectExprColumns(e->m_lhs.get(), into) && CollectExprColumns(e->m_rhs.get(), into);
	case ibQueryColumnExprKind::PeriodTrunc:
		return CollectExprColumns(e->m_lhs.get(), into);
	case ibQueryColumnExprKind::Case:
		// The THEN / ELSE arms are expressions and walk; the WHEN predicates are a different tree, and
		// this does not read them — so a CASE keeps the RAM road until somebody teaches it that half.
		return false;
	}
	return false;
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

		// …AND THE INPUTS OF EVERY COMPUTED OUTPUT, whether or not anybody asked to see them: the
		// expression is evaluated over the rows that come back, so they have to carry what it reads.
		// Projected under their OWN model id (not an output alias) — that is the key the evaluator
		// looks a cell up by — and skipped when the select list already carries them.
		std::vector<const ibBackendQueryColumn*> exprInputs;
		if (spec.m_selectExprs != nullptr)
			for (const ibQueryColumnSelect& sc : *spec.m_selectExprs)
				CollectExprColumns(sc.m_expr.get(), exprInputs);   // the gate already vouched for the walk
		for (const ibBackendQueryColumn* c : exprInputs) {
			const bool already = std::any_of(plans.begin(), plans.end(),
				[&](const OutPlan& p) { return p.col == c; });
			if (already)
				continue;
			// Read back exactly as an ordinary output of the same column would be — a raw column is one
			// scalar field, a metadata column its full spread under a unique prefix. Taking the raw
			// shortcut for both would read a composite column's FIRST field, which is its type tag.
			const ibBackendQueryable* owner = ColocatedOwner(leaves, c);
			const wxString    qual = owner != nullptr ? owner->GetQueryTableName() : wxString();
			const ibMetaData* meta = owner != nullptr ? owner->GetMetaData() : nullptr;
			const wxString    alias = wxString::Format(wxT("xin%d"), oi);
			if (c->IsRawColumn()) {
				projection.push_back(ibQueryProjItem{ ibCol(qual, FirstSqlFieldOfColumn(c)), alias });
				plans.push_back({ c, alias, wxString(), meta, true });
			}
			else {
				const wxString prefix = wxString::Format(wxT("ocol%d"), oi);
				const wxString base   = c->GetPhysicalName();
				for (const wxString& f : ColumnFieldNames(c))
					projection.push_back(ibQueryProjItem{ ibCol(qual, f), prefix + f.Mid(base.length()) });
				plans.push_back({ c, alias, prefix, meta, false });
			}
			++oi;
		}

		q.Project(std::move(projection));

		if (ibQueryExprPtr where = ColocatedWhere(spec, leaves))
			q.Where(where);

		// ORDER BY — each user sort column qualified to its leaf (no keyset tail: a composed join
		// read is one-shot, not a keyset-paged scroll).
		for (const ibQuerySortItem& s : *spec.m_sorts) {
			ibQuerySortKey k;
			// ⭐ ORDER BY <EXPRESSION> IS A SORT KEY LIKE ANY OTHER — and this loop used to drop it on the
			// floor: `m_col == nullptr` covers both "sort by the row key" and "sort by an expression",
			// and skipping on it threw the second away without a word. The single-source road has always
			// lowered it (BuildSortKeys); so does this one now.
			//
			// UNQUALIFIED, deliberately: a co-located join's leaves are distinct tables and their fields
			// are `fld<metaID>`, unique per metatype — the same reason the declaration writer spells them
			// bare. (A self-join is refused by the gate, so there is no second copy of a name.)
			if (s.m_expr)
				k.m_expr = ibMetaIRBuilder::BuildColumnExpr(spec.m_queryable, s.m_expr);
			else if (s.m_col != nullptr)
				k.m_expr = ibCol(ColocatedQual(leaves, s.m_col), FirstSqlFieldOfColumn(s.m_col));
			else
				continue;   // the row-key sort — no such thing across a join
			k.m_dir  = s.m_ascending ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
			q.AddSortKey(std::move(k));
		}

		if (page.m_count > 0) q.Limit(page.m_count);

		// Run server-side, assemble each output into the unified RAM table — keyed by the output column's
		// model id (so GetValue(col) works) AND named by its alias (so GetColumn(alias) works). The join +
		// the cross-table filter ran in the DBMS; only the projected result transits.
		ibQueryResult cursor = RunSpecStatement(spec, q);

		ibQueryRamTable out;
		for (const OutPlan& p : plans)
			out.AddColumn(p.col->GetColumnId(), p.alias, p.col->GetTypeDesc());

		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (const OutPlan& p : plans) {
				ibValue v;
				if (p.raw) v = ReadScalarByAlias(p.col, p.alias, p.meta, cursor);
				else       p.col->ReadValue(p.prefix, p.meta, v, cursor);   // asked of the COLUMN — the codec is its default
				out.SetCell(r, p.col->GetColumnId(), v);
			}
		}

		// ⭐ THE COMPUTED OUTPUTS, over the rows the join returned. One evaluation per OUTPUT row —
		// which is the whole reason this shape no longer sends the join itself to RAM. Read back by
		// GetColumn(alias); the synthetic ids are the range the RAM road already uses for the same
		// thing, so the two roads number these columns alike.
		if (spec.m_selectExprs != nullptr) {
			ibMetaID exprId = 0x70000000u;
			for (const ibQueryColumnSelect& sc : *spec.m_selectExprs) {
				const ibMetaID id = exprId++;
				out.AddColumn(id, sc.m_alias, ibTypeDescription());
				for (long r = 0; r < out.RowCount(); ++r)
					out.SetCell(r, id, ibQueryComposer::EvalColumnExpr(sc.m_expr.get(), out, r));
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
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args), a.m_distinct), a.m_alias });
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
		return ibDataQueryResult(RunSpecStatement(spec, q), queryable);
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
			projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args), a.m_distinct), a.m_alias });
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

		ibQueryResult cursor = RunSpecStatement(spec, q);

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
				if (gp.scalar) v = ReadScalarByAlias(gp.col, gp.tag, gp.meta, cursor);
				else           gp.col->ReadValue(gp.tag, gp.meta, v, cursor);   // reference / variant group key
				out.SetCell(r, gp.col->GetColumnId(), v);
			}
			ibMetaID aid = aggBaseId;
			for (const ibDataQueryBuilder::AggregateItem& a : *spec.m_aggregates) {
				using Fn = ibDataQueryBuilder::AggregateFn;
				ibValue v;
				if (a.m_fn == Fn::Min || a.m_fn == Fn::Max)
					v = ReadScalarByAlias(a.m_col, a.m_alias, spec.m_queryable->GetMetaData(), cursor);   // MIN/MAX keep the input column's type
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
		// the DBMS does it natively (the spelling lives in the L2-1 render: ibUnion vs ibUnionAll).
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
		ibQueryIR ir(outer);
		AttachNamedQueries(spec, ir);   // a branch may read a DECLARED name — see RunSpecStatement
		ibQueryResult cursor = q.ExecuteIR(ir);

		ibQueryRamTable out;
		for (size_t i = 0; i < outs.size(); ++i)
			out.AddColumn(outs[i].first->GetColumnId(), outs[i].second, outs[i].first->GetTypeDesc());
		while (cursor.Next()) {
			const long r = out.AppendRow();
			for (size_t i = 0; i < outs.size(); ++i)
				out.SetCell(r, outs[i].first->GetColumnId(),
					ReadScalarByAlias(outs[i].first, uAlias(i), spec.m_queryable->GetMetaData(), cursor));
		}
		return ibDataQueryResult(std::move(out), spec.m_queryable);
	}

// ==========================================================================
// Totals push-down — GROUP BY ROLLUP (docs/query-language-arc.md §22.1b). The DBMS computes every
// subtotal level (each from raw detail -> correct for COUNT/AVG) + the grand total in ONE pass; we
// read the result + the GROUPING(key) flags and assemble the ibSelectorTree node tree the runtime
// already consumes. Only the aggregated subtotal rows transit — no raw detail.
// ==========================================================================
// ⭐ THE LEVELS THIS SPEC FOLDS BY — asked in ONE place, because the two lists are filled by
// DIFFERENT verbs and every gate here needs the same answer. A door that folds hierarchical totals
// fills `m_totals` (TotalByLevel) and leaves `m_groupBy` empty; a hand-built spec (and the plain
// GROUP BY aggregate) fills `m_groupBy` and knows nothing of levels. Reading only the second is what
// made this whole tier unreachable from the composer: for `SELECT … TOTALS SUM(x) BY Warehouse` the
// list the gate looked at IS empty.
//
// A flat group key reads as a ONE-FIELD level, which is exactly what it is.
static std::vector<ibTotalLevel> RollupLevelsOf(const ibDataQuerySpec& spec)
{
	if (spec.m_totals != nullptr && !spec.m_totals->empty())
		return *spec.m_totals;

	std::vector<ibTotalLevel> levels;
	if (spec.m_groupBy != nullptr)
		for (const ibBackendQueryColumn* g : *spec.m_groupBy)
			levels.push_back(ibTotalLevel::One(g, ibDimensionKind::Elements));
	return levels;
}

// …AND WHAT THEY ROLL. Same split, same reason: `Totals()` diverts the aggregates into
// `m_totalAggregates`, so a totals door's `m_aggregates` is empty and a fold reading it would
// project no figures at all.
static const std::vector<ibDataQueryBuilder::AggregateItem>& RollupAggregatesOf(const ibDataQuerySpec& spec)
{
	static const std::vector<ibDataQueryBuilder::AggregateItem> s_none;
	if (spec.m_totalAggregates != nullptr && !spec.m_totalAggregates->empty())
		return *spec.m_totalAggregates;
	return spec.m_aggregates != nullptr ? *spec.m_aggregates : s_none;
}

// ⭐⭐ THE PHANTOM LEVEL — what a DETAIL level becomes on the way to the server.
//
// "A detail record is an empty grouping": the composition declares a level with no fields and the
// RAM fold answers with one node per row. A `GROUP BY` cannot answer that — it folds the rows away,
// which is exactly why the server road used to refuse a report that asked for its rows.
//
// Unless the grouping is by the ROW ITSELF. Group by a key that is unique per row and every group
// holds exactly one row, so the deepest set of ROLLUP(dimensions…, <row identity>) IS the detail —
// headings, subtotals, grand total and rows come back from ONE pass, and nothing is materialised
// here to produce them (Max: "they cannot be materialised, they have to come in pages, and that is
// what ROLLUP(dimensions…, key) is").
//
// The identity is the primary key PLUS everything the row prints: the projected columns are
// functionally dependent on the key, so adding them to the group changes no row and no figure, and
// it is what lets them be READ (a column not grouped and not aggregated cannot be projected at all).
// Which is also why a source with no primary key has no phantom level — the gate keeps it on the RAM
// road, where a row is a row because it was read as one.
//
// Asked BY THE GATE as well as by the fold, and that is deliberate: "can this report go to the
// server" and "what does its detail level group by" are the same question read twice.
static ibTotalLevel PhantomLevel(const ibDataQuerySpec& spec)
{
	ibTotalLevel level;
	auto add = [&level](const ibBackendQueryColumn* col) {
		if (col == nullptr)
			return;
		for (const ibTotalField& have : level.m_fields)
			if (have.m_col == col)
				return;
		level.m_fields.push_back(ibTotalField{ col, ibDimensionKind::Elements });
	};

	if (spec.m_queryable != nullptr)
		for (const ibBackendQueryColumn* key : spec.m_queryable->GetPrimaryKeyColumns())
			add(key);
	if (spec.m_selectCols != nullptr)
		for (const auto& sel : *spec.m_selectCols)
			add(sel.first);
	return level;
}

// ⭐⭐ A REFUSAL THAT SAYS ITS OWN NAME. This gate answers a bare `false` about a dozen different
// things, and a caller reading it learns only that the report will be slower — never why, and never
// whether the reason is one it could fix (a sort it did not need) or one nothing can (an engine with
// no ROLLUP). Written at the point the refusal is BORN, each one is a sentence.
//
// Takes a FORMAT AND ITS ARGUMENTS, like every other line in this codebase: the fact that names a
// refusal (the column, the source, the level) is usually right there, and a door that took only a
// finished string made each callsite build one first. The prefix stays here — one place decides how
// a refusal reads — and the callsite writes only what is particular to it.
//
// ⚠ THE PREFIX IS GLUED TO THE FORMAT, not printed through it. Adjacent literals are one literal to
// the compiler, so there is ONE format string and ONE pass — a `wxString::Format` fed into a `%s`
// would be a format rendered by a format, which costs a string, drops the compile-time check on the
// arguments and reads like an accident.
//
// Debug only, arguments included: in Release the journal call is `((void)0)` and this is `return
// false` — which is what the gate was before.
#define RollupDecline(fmt, ...) \
	do { ibJournalInfo(wxT("query.road"), wxT("server fold declined: ") fmt, ##__VA_ARGS__); \
	     return false; } while (false)

bool ibDbTableProvider::CanRollupTotalsShape(const ibDataQuerySpec& spec)
	{
		// Single-source DB queryable (a multi-source totals goes through the co-located / RAM paths).
		if (spec.m_root != nullptr && spec.m_root->m_kind != ibQueryNode::Kind::Source)
			RollupDecline(wxT("more than one source (co-located / RAM road)"));
		const ibBackendQueryable* q = spec.m_queryable;
		if (q == nullptr)                                         RollupDecline(wxT("no source"));
		if (q->IsComputedInRam())
			RollupDecline(wxT("source '%s' is computed in RAM"), q->GetQueryName());

		const std::vector<ibTotalLevel> levels = RollupLevelsOf(spec);
		if (levels.empty())                                       RollupDecline(wxT("no totals levels"));

		// 🛑⭐ BRANCHES ARE NOT A LADDER, AND `ROLLUP` ONLY KNOWS LADDERS. `ROLLUP(a, b, c)` is the
		// prefixes of ONE list; a `SPLIT` asks for several lists that share a head — which SQL spells
		// `GROUPING SETS`, not `ROLLUP`. Handed to ROLLUP as the flat list it is stored as, the
		// branches would come back as levels NESTED inside one another: a report that runs, prints
		// headings in an order nobody asked for, and reconciles to nothing.
		//
		// So this refuses, loudly and by name, and the RAM fold answers — where a branch is a fork
		// and the shape is right. ⏭ AND THIS IS THE PLACE THE PUSH-DOWN GOES: the sets are exactly
		// "the common prefix + each branch's own prefixes", which `GROUPING SETS` states directly.
		// Engines are already asked whether they have it (ibSqlFeatures::m_grouping), and Firebird —
		// which has neither it nor ROLLUP — keeps the RAM road either way.
		for (const ibTotalLevel& level : levels)
			if (level.m_branch != nullptr)
				RollupDecline(wxT("the totals fork (SPLIT) - ROLLUP states one ladder, and branches need GROUPING SETS"));

		// ⚠ A DERIVED SOURCE AND A DOT-WALK CANNOT BOTH BE HONOURED. A source that is not a table puts
		// its own relation in the FROM (GetDerivedRelation), and the reference JOIN chain a dot-walked
		// key rides is built over the main TABLE — there is none to hang it on. Both are fine alone;
		// together they keep the RAM fold, which reads either.
		if (q->GetSourceRelation(q->GetQueryTableName()) != nullptr) {
			if (spec.m_dimWalks != nullptr && !spec.m_dimWalks->empty())
				RollupDecline(wxT("a derived source cannot also carry a dot-walked dimension"));
			if (spec.m_groupPaths != nullptr)
				for (const std::vector<const ibBackendQueryColumn*>& gp : *spec.m_groupPaths)
					if (!gp.empty())
						RollupDecline(wxT("a derived source cannot also carry a dot-walked group key"));
			for (const ibTotalLevel& level : levels)
				for (const ibTotalField& f : level.m_fields)
					if (!f.m_path.empty())
						RollupDecline(wxT("a derived source cannot also carry a dot-walked level field"));
		}

		// ⚠ THREE REFUSALS THAT COULD NOT BE STATED BEFORE, because a flat column list does not carry
		// what they ask about. Each is a SILENT wrong answer if it is missed, not a slow one:
		for (size_t li = 0; li < levels.size(); ++li) {
			const ibTotalLevel& level = levels[li];
			// ⭐ DETAIL RECORDS — a level with no fields — travel as the PHANTOM LEVEL: the row's own
			// identity as the deepest group key, so ROLLUP returns the rows along with the headings
			// instead of folding them away. Two things have to hold, and each says its own name.
			if (level.m_fields.empty()) {
				// The rows are the BOTTOM. A grouping below a detail level would be a grouping of
				// rows by something they were already split by — the composition does not produce it,
				// and the phantom level could not express it.
				if (li + 1 != levels.size())
					RollupDecline(wxT("a level with no fields that is not the last - detail records are the bottom"));
				// …AND THERE HAS TO BE AN IDENTITY TO GROUP BY. Without a primary key (a register, a
				// temp table) grouping by "the row" would fold equal rows into one, silently losing
				// the duplicates — so those stay on the RAM road, where a row is a row because it was
				// read as one.
				if (PhantomLevel(spec).m_fields.empty())
					RollupDecline(wxT("detail records over '%s', which has no row identity to group by"),
					              q->GetQueryName());
				// …AND EVERY PRINTED COLUMN HAS TO BE GROUPABLE. A COMPUTED projection (Qty * Price)
				// is not part of the row's identity and cannot ride in the GROUP BY, so the detail
				// rows would come back missing exactly the column the author computed. The RAM fold
				// evaluates it per row and prints it, so that is where such a report belongs.
				if (spec.m_selectExprs != nullptr && !spec.m_selectExprs->empty())
					RollupDecline(wxT("detail records beside a computed projection"));
				continue;   // its fields are the phantom's, checked as they are built
			}
			for (const ibTotalField& f : level.m_fields) {
				if (f.m_col == nullptr)                           RollupDecline(wxT("a level field with no column"));
				// BY PERIODS groups by a TRUNCATION of the field, and a truncation is a scalar
				// expression over a scalar column — there is no truncating a reference's spread.
				// The RAM fold answers such a level by reading the value and truncating it there.
				if (f.ByPeriods()) {
					ColocatedLeaves one; one.push_back(q);
					if (!f.m_path.empty())
						RollupDecline(wxT("a dot-walked field read BY PERIODS"));
					if (!f.m_col->IsRawColumn() && !ScalarReadable(f.m_col, one))
						RollupDecline(wxT("field '%s' is read BY PERIODS but is not a scalar column"), f.m_col->GetName());
				}
				// A HIERARCHY UNFOLD walks the reference's parent chain — the RAM fold's
				// AttachDimValue does that, and no GROUP BY can. Folded here it would come back as a
				// plain grouping under a word that asked for something else.
				if (f.m_dim != ibDimensionKind::Elements)
					RollupDecline(wxT("level '%s' asks for a HIERARCHY unfold"), f.m_col->GetName());
				// A DOT-WALKED DIMENSION (`Producer.Region`) rides a reference JOIN chain, the same
				// one a dot-walked flat key rides — allowed WHEN every non-leaf hop is a SINGLE-TARGET
				// reference, so the chain is resolvable from metadata alone. A composite / multi-target
				// mid-hop cannot be joined and keeps the RAM fold, which handles it.
				if (!f.m_path.empty()) {
					const ibBackendQueryable* walk = q;
					for (size_t s = 0; s + 1 < f.m_path.size() && walk != nullptr; ++s)
						walk = walk->GetProvider().ResolveReferenceTarget(walk, f.m_path[s]);
					if (walk == nullptr)
						RollupDecline(wxT("dot-walked dimension '%s' has an unresolvable hop"), f.m_col->GetName());
				}
			}
		}

		// ⚠ AND THE ORDER HAS TO BE SAYABLE OVER THE FOLDED RESULT. A grouped query may only order by
		// what it grouped by (or by an aggregate); a sort naming an ordinary field would be rejected by
		// the engine — an error where there used to be a slower but correct answer. In this model the
		// sort names the very fields the groupings run over, so the ordinary report passes; anything
		// else keeps the RAM fold, which orders the rows and then groups them in first-seen order.
		if (spec.m_sorts != nullptr)
			for (const ibQuerySortItem& s : *spec.m_sorts) {
				if (s.m_expr != nullptr || !s.m_path.empty())
					RollupDecline(wxT("a computed / dot-walked sort over a folded result"));
				if (s.m_col == nullptr)
					RollupDecline(wxT("a row-key sort - there is no such thing over groups"));
				bool isLevelField = false;
				for (const ibTotalLevel& level : levels)
					for (const ibTotalField& f : level.m_fields)
						if (f.m_col == s.m_col) { isLevelField = true; break; }
				if (!isLevelField)
					RollupDecline(wxT("sort by '%s', which no level groups by"), s.m_col->GetName());
			}

		if (!spec.m_dotWalks->empty())                            RollupDecline(wxT("a dot-walked projection"));
		if (!spec.m_keyIn->empty())                               RollupDecline(wxT("a row-key IN set"));

		// A dot-walk GROUP key rides a reference JOIN chain (ExecuteRollupTotals builds it) — allowed WHEN every
		// NON-leaf path segment is a SINGLE-TARGET reference (structurally resolvable, metadata-only, no DB). A
		// composite / multi-target mid-hop can't be joined -> RAM-fold (correct there).
		if (spec.m_groupPaths != nullptr)
			for (const auto& gp : *spec.m_groupPaths) {
				if (gp.empty()) continue;
				const ibBackendQueryable* walk = q;
				for (size_t s = 0; s + 1 < gp.size() && walk != nullptr; ++s)
					walk = walk->GetProvider().ResolveReferenceTarget(walk, gp[s]);
				if (walk == nullptr)
					RollupDecline(wxT("a dot-walked group key has an unresolvable hop"));
			}
		// Group keys: SCALAR or a REFERENCE / variant (a reference groups by its full spread as ONE composite
		// ROLLUP element, reassembled on read — RunRollupTotals handles it). Aggregate inputs stay SCALAR;
		// the null-column check for the keys is made above, where the levels are walked.
		ColocatedLeaves one; one.push_back(q);
		for (const ibDataQueryBuilder::AggregateItem& a : RollupAggregatesOf(spec))
			if (a.m_col != nullptr && !ScalarReadable(a.m_col, one))
				RollupDecline(wxT("aggregate input '%s' is not a scalar column"), a.m_col->GetName());
		return true;
	}

bool ibDbTableProvider::CanPushRollupTotals(const ibDataQuerySpec& spec)
	{
		if (!CanRollupTotalsShape(spec)) return false;   // the shape said why, where it decided
		// The connected driver must be able to fold the levels itself (PG; NOT Firebird through 5.0,
		// NOT SQLite -> RAM). Asked of L2 rather than read off its dictionary: what a driver CAN DO is
		// a question, and this tier has no business knowing which field carries the answer.
		ibConnectionScope scope(spec.m_holder);
		if (!scope)                        RollupDecline(wxT("no connection to ask whether it folds"));
		if (!ibCanPushRollup(scope.get())) RollupDecline(wxT("this engine has no GROUP BY ROLLUP"));
		return true;
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

// A NODE THAT HAS CHILDREN SAYS SO. The flag is what every reader folds by — a report tints and
// bolds a heading, a grid draws the expander, the walk asks HasChildren() — and a tree assembled
// from ROLLUP rows had nobody to set it: parents are found through the key map, not by descending,
// so nothing ever looked at its own children. Left false, a server-folded report printed its
// headings as ordinary rows. The RAM fold sets it as it descends (FoldDimLevel); this is the same
// statement made once the shape is complete.
static void MarkRollupFolders(ibSelectorTree::Node& node)
{
	node.m_hasChildren = !node.m_children.empty();
	for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
		if (child != nullptr)
			MarkRollupFolders(*child);
}

static ibSelectorTree RunRollupTotals(const ibDataQuerySpec& spec, ibQueryRelPtr from,
	const std::function<ibQueryExprPtr(const ibBackendQueryColumn*)>& colExpr,
	const std::function<RollupGroupKey(const ibBackendQueryColumn*)>& keyInfo,
	std::vector<ibQuerySortKey> orderKeys)
{
	// Build the IR: SELECT <field | spread>, GROUPING(<level's first field>) AS grp<L>, <agg> AS alias
	//               FROM <from> GROUP BY ROLLUP(<level 0>, <level 1>, …)
	//
	// ⭐⭐ ONE ROLLUP ELEMENT PER **LEVEL**, NOT PER COLUMN. A level may group by SEVERAL fields
	// together — `BY (Partner, Contract)` is one heading whose key is the tuple — so the element is
	// all of its fields, and the flat "one column, one level" reading would have turned one heading
	// into two nested ones and silently changed the report. A reference field spreads into its own
	// fields inside that element, so a tuple of references is simply a longer element.
	//
	// A lone scalar field stays a BARE expression, exactly as it rendered before: `ROLLUP(a, b)`, not
	// `ROLLUP((a), (b))`. The parenthesised form is legal SQL, but the common shape has no business
	// changing spelling because a rarer one was made expressible.
	// Does this engine have GROUPING()? Asked ONCE, of L2, the same way "can it fold at all" is asked
	// — the two are separate facts (Firebird 5 has the fold and not the function).
	bool hasGrouping = false;
	{
		ibConnectionScope scope(spec.m_holder);
		hasGrouping = scope && ibCanUseGrouping(scope.get());
	}

	struct FieldPlan { const ibBackendQueryColumn* col; bool scalar; wxString tag; const ibMetaData* meta; };
	// ⭐ …AND THE ALIAS OF THE LEVEL'S FIRST PROJECTED FIELD. It is what says whether this row is AT
	// this level, on an engine with no GROUPING(): a level that the rollup folded away comes back
	// SQL NULL in its own keys. Exact here — OES attributes hold typed empties and never SQL NULL, so
	// a NULL in a result can only have come from the fold (see ibSqlFeatures::m_grouping).
	// The EXPRESSION for that same field rides along, because the ORDER BY below says the same thing
	// to the server and an ORDER BY cannot lean on a projection alias inside an expression.
	struct LevelPlan { std::vector<FieldPlan> fields; wxString firstAlias; ibQueryExprPtr firstExpr; };
	std::vector<ibTotalLevel> levels = RollupLevelsOf(spec);

	// A DETAIL level (no fields) is lowered to the phantom level — the row's own identity as a group
	// key. Its nodes are stamped Detail when the tree is assembled: they are rows, and only the node
	// knows which of the two a visit is. (The gate guarantees there is an identity to group by.)
	int detailLevel = -1;
	for (size_t i = 0; i < levels.size(); ++i)
		if (levels[i].m_fields.empty()) {
			levels[i]   = PhantomLevel(spec);
			detailLevel = static_cast<int>(i);
			ibJournalInfo(wxT("query.road"), wxT("SERVER: detail records lowered to the phantom level (%u identity fields)"),
			              static_cast<unsigned>(levels[i].m_fields.size()));
		}

	std::vector<LevelPlan>       levelPlans;
	std::vector<ibQueryProjItem> projection;
	std::vector<ibQueryExprPtr>  groupKeys;
	std::vector<wxString>        groupingAliases;
	int li = 0;
	for (const ibTotalLevel& level : levels) {
		const wxString grpalias = wxString::Format(wxT("grp%d"), li);
		LevelPlan                   plan;
		std::vector<ibQueryExprPtr> element;      // every field of this level, in order
		ibQueryExprPtr              firstField;   // GROUPING is asked of ONE of them — they share the flag

		int fi = 0;
		for (const ibTotalField& field : level.m_fields) {
			// ⭐ TWO COLUMNS, ONE FIELD. A dot-walked dimension groups by a SYNTHETIC column — the id
			// the tree keys on, and the one the reader asks `GetValue(col)` for — while the SQL has to
			// name the JOINED LEAF, which is where the value physically lives. For a plain field the
			// two are the same column and nothing below notices the difference.
			const ibBackendQueryColumn* g  = field.SqlCol();   // what the STATEMENT names
			const RollupGroupKey        ki = keyInfo(g);
			auto fieldCol = [&](const wxString& f) { return ki.qualifier.empty() ? ibCol(f) : ibCol(ki.qualifier, f); };

			if (ki.scalar) {
				const wxString       galias = wxString::Format(wxT("g%d_%d"), li, fi);
				// ⭐ BY PERIODS — the key is the START OF THE PERIOD containing the value, so the
				// element grouped by and the field projected are both the truncation. The dialect
				// spells PeriodTrunc its own way, and `ibTruncateToPeriod` is the RAM twin of the
				// very same definition — which is what lets a row land in the same bucket whichever
				// road read it.
				auto keyExpr = [&](void) {
					const ibQueryExprPtr raw = colExpr(g);
					return field.ByPeriods() ? ibPeriodTrunc(raw, field.m_periods->m_unit) : raw;
				};
				const ibQueryExprPtr gexpr  = keyExpr();
				if (!firstField) firstField = keyExpr();
				element.push_back(gexpr);
				projection.push_back(ibQueryProjItem{ gexpr, galias });
				if (plan.firstAlias.IsEmpty()) plan.firstAlias = galias;
				plan.fields.push_back({ g, true, galias, ki.meta });
			}
			else {
				// REFERENCE / variant field: its FULL SPREAD joins this level's element, projected under a
				// prefix and reassembled on read (ibColumnCodec::ReadValue).
				const wxString prefix = wxString::Format(wxT("gcol%d_%d"), li, fi);
				const wxString base   = g->GetPhysicalName();
				for (const wxString& f : ColumnFieldNames(g)) {
					const ibQueryExprPtr fexpr = fieldCol(f);
					if (!firstField) firstField = fexpr;
					element.push_back(fexpr);
					const wxString falias = prefix + f.Mid(base.length());
					projection.push_back(ibQueryProjItem{ fexpr, falias });
					if (plan.firstAlias.IsEmpty()) plan.firstAlias = falias;
				}
				plan.fields.push_back({ g, false, prefix, ki.meta });
			}
			++fi;
		}

		// One field, scalar: the bare expression (the shape that was always rendered). Anything else is
		// a composite element — an empty-name Func renders as "(f0, f1, …)".
		if (element.size() == 1) groupKeys.push_back(element.front());
		else                     groupKeys.push_back(ibFunc(wxT(""), std::move(element)));

		// ⭐ THE LEVEL FLAG, ONLY WHERE THE ENGINE HAS THE FUNCTION. Without GROUPING the same fact is
		// read off the keys themselves (the row comes back NULL in the level the fold rolled away), so
		// projecting a call the engine does not know would fail the whole statement for a label.
		if (hasGrouping)
			projection.push_back(ibQueryProjItem{ ibFunc(wxT("GROUPING"), { firstField }), grpalias });
		groupingAliases.push_back(grpalias);
		plan.firstExpr = firstField;
		levelPlans.push_back(std::move(plan));
		++li;
	}
	const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates = RollupAggregatesOf(spec);
	for (const ibDataQueryBuilder::AggregateItem& a : aggregates) {
		std::vector<ibQueryExprPtr> args;
		args.push_back(a.m_col != nullptr ? colExpr(a.m_col) : ibCol(wxT("*")));
		projection.push_back(ibQueryProjItem{ ibFunc(AggregateFnName(a.m_fn), std::move(args), a.m_distinct), a.m_alias });
	}

	ibQueryExprPtr having;
	for (const ibDataQueryBuilder::HavingItem& h : *spec.m_having) {
		std::vector<ibQueryExprPtr> args;
		args.push_back(h.m_col != nullptr ? colExpr(h.m_col) : ibCol(wxT("*")));
		ibQueryExprPtr cmp = ibBinOp(FilterOpToBinOp(h.m_op),
			ibFunc(AggregateFnName(h.m_fn), std::move(args)), ibConst(h.m_value));
		having = having ? ibBinOp(ibQueryBinOp::And, having, cmp) : cmp;
	}

	// THE ORDER IS THE QUERY'S, and it is stated ONCE — where the composition states it. The fold does
	// not decide anything about order and does not derive a second one from the levels: it carries
	// what it was handed (empty = the query asked for no order, and then neither does the fold).
	//
	// ⭐⭐ WHAT IT DOES ASK FOR IS PARENTS BEFORE CHILDREN — which is not an order over the DATA but
	// over the LEVELS, and the difference is why it can be said here without contradicting the line
	// above. A ROLLUP result is levels interleaved: the grand total, the subtotals, the leaves, in
	// whatever order the engine happened to emit them. The tree needs each node's parent to exist by
	// the time the node arrives, and that used to be arranged by BUFFERING EVERY FOLDED ROW and
	// stable_sorting it by level here — the one place a server-side fold still held its whole answer.
	//
	// Said as a sort key, it costs nothing and the buffer goes away: a level the rollup folded away
	// comes back NULL in its own key, so `CASE WHEN <key> IS NULL THEN 0 ELSE 1 END` ascending puts
	// every folded level ahead of the level below it. It is the SAME fact the row reader below uses
	// to work out a row's level, and it is spelled without GROUPING() so that an engine which folds
	// but has no GROUPING() gets the same guarantee. The query's own keys follow, deciding the order
	// WITHIN a level exactly as before.
	std::vector<ibQuerySortKey> parentsFirst;
	for (const LevelPlan& plan : levelPlans)
		if (plan.firstExpr)
			parentsFirst.push_back(ibQuerySortKey{
				ibCase({ { ibIsNull(plan.firstExpr), ibConst(ibValue(ibNumber(0L))) } }, ibConst(ibValue(ibNumber(1L)))),
				ibQuerySortDir::Asc });
	for (ibQuerySortKey& key : orderKeys)
		parentsFirst.push_back(std::move(key));

	ibQueryRelPtr folded = ibAggregate(from, std::move(projection), std::move(groupKeys), having, /*rollup*/ true);
	if (!parentsFirst.empty())
		folded = ibSort(folded, std::move(parentsFirst));
	ibQueryIR ir(folded);
	// ⭐ …AND WHATEVER THIS STATEMENT DECLARED COMES WITH IT. The fold assembles its own IR out of the
	// spec rather than going through BuildPageIR, so the `WITH` list had nobody to attach it: a source
	// that is a DECLARED name (ibCteQueryable — an author's query the server reads itself) rendered as
	// a bare `FROM q_sub0` and the engine answered "table unknown". The FROM and the declaration are
	// one statement; whichever door writes the first writes the second.
	ibDbTableProvider::AttachNamedQueries(spec, ir);
	ibDatabaseQueryBuilder qb(spec.m_holder);
	ibQueryResult cursor = qb.ExecuteIR(ir);

	// Assemble the tree AS THE ROWS ARRIVE. Columns = group cols + aggregates IN-PLACE in their own
	// source columns.
	ibSelectorTree tree;
	for (const ibTotalLevel& level : levels)
		for (const ibTotalField& field : level.m_fields)
			if (field.m_col != nullptr)
				tree.AddColumn(field.m_col->GetColumnId(), field.m_col->GetName(), field.m_col->GetTypeDesc());
	for (const ibDataQueryBuilder::AggregateItem& a : aggregates)
		if (a.m_col != nullptr) tree.AddColumn(a.m_col->GetColumnId(), a.m_col->GetName(), a.m_col->GetTypeDesc());

	// KEYED BY THE VALUES, NOT BY A STRING BUILT FROM THEM.
	//
	// This used to fold each level through GetHashKey() and glue the results with \x1f: per row, per
	// level, a text conversion (a number goes through ToString, a reference through
	// wxString::Format) plus the concatenation — and twice over, since the parent key is the same
	// prefix built again. The std::map then compared those strings character by character.
	//
	// A group key IS a sequence of values, so it is one here: ibValueSeqHash / ibValueSeqEqual
	// (value.h) hash and compare the values themselves, the same policy the LINQ join and group-by
	// indexes use. The parent key stops being a second string and becomes the prefix it always was —
	// the key minus its last element.
	//
	// ⭐ A LEVEL CONTRIBUTES AS MANY VALUES AS IT HAS FIELDS, so the parent key is the key minus the
	// LAST LEVEL, not minus one value. With a tuple level, "minus one" would have pointed at a key
	// that belongs to nobody, and every node under it would have been re-parented onto the root.
	//
	// ⭐ …AND ONLY NODES THAT CAN HAVE CHILDREN GO IN IT. The map is what the fold holds, so what it
	// holds is a function of the GROUPS. A row at the deepest level is nobody's parent — with the
	// phantom level below the dimensions those rows ARE the detail records, and keeping them here
	// would put the whole detail back in memory to answer a question nobody asks.
	std::unordered_map<std::vector<ibValue>, ibSelectorTree::Node*,
	                   ibValueSeqHash, ibValueSeqEqual> nodes;
	nodes[std::vector<ibValue>()] = &tree.Root();

	// Read every ROLLUP row: the values of each LEVEL's fields, its aggregate values, and its LEVEL
	// depth (= count of GROUPING=0 elements — for ROLLUP they are always a prefix). The server was
	// asked for parents-before-children (see the sort keys above), so a row's parent is always
	// already in the map by the time the row is read.
	struct RRow { std::vector<std::vector<ibValue>> levelValues; std::vector<ibValue> aggs; int level; };
	while (cursor.Next()) {
		RRow rr; rr.level = 0;
		for (size_t i = 0; i < levelPlans.size(); ++i) {
			std::vector<ibValue> values;
			for (const FieldPlan& fp : levelPlans[i].fields) {
				ibValue v;
				if (fp.scalar) v = ReadScalarByAlias(fp.col, fp.tag, fp.meta, cursor);
				else           fp.col->ReadValue(fp.tag, fp.meta, v, cursor);   // reference / variant reassembly
				values.push_back(v);
			}
			rr.levelValues.push_back(std::move(values));

			// ⭐ IS THIS ROW AT THIS LEVEL — asked of GROUPING where the engine has it, and of the KEY
			// ITSELF where it does not. A level the fold rolled away comes back SQL NULL in its own
			// columns, and in this storage that is unambiguous: attributes hold typed empties and
			// never SQL NULL, so a NULL here cannot have come from the data. Read as an ibValue,
			// which carries TYPE_NULL from the driver — the codec would have turned it into a typed
			// empty and lost exactly the distinction being made.
			const bool atThisLevel = hasGrouping
				? (cursor.GetResultInt(groupingAliases[i]) == 0)
				: !cursor.GetValue(levelPlans[i].firstAlias).IsNull();
			if (atThisLevel) ++rr.level;
		}
		for (const ibDataQueryBuilder::AggregateItem& a : aggregates) {
			using Fn = ibDataQueryBuilder::AggregateFn;
			rr.aggs.push_back((a.m_fn == Fn::Min || a.m_fn == Fn::Max)
				? ReadScalarByAlias(a.m_col, a.m_alias, spec.m_queryable->GetMetaData(), cursor)
				: ibValue(cursor.GetResultNumber(a.m_alias)));
		}

		// --- this row becomes its node, here, while the cursor stands on it ---------------------
		const size_t level = static_cast<size_t>(rr.level);
		std::vector<ibValue> key;
		for (size_t i = 0; i < level && i < rr.levelValues.size(); ++i)
			key.insert(key.end(), rr.levelValues[i].begin(), rr.levelValues[i].end());

		ibSelectorTree::Node* node = nullptr;
		if (rr.level == 0) {
			node = &tree.Root();          // grand total
		}
		else {
			const size_t lastLevelWidth = rr.levelValues[level - 1].size();
			const std::vector<ibValue> parentKey(key.begin(), key.end() - static_cast<long>(lastLevelWidth));
			const auto pit = nodes.find(parentKey);
			ibSelectorTree::Node* parent = (pit != nodes.end()) ? pit->second : &tree.Root();
			node = parent->AddChild(rr.level);
			// A ROW OR A HEADING — said by the node, because only the node knows. The phantom level's
			// nodes are the detail records the composition asked for.
			if (detailLevel >= 0 && rr.level == detailLevel + 1)
				node->m_kind = ibSelectorNodeKind::Detail;
			for (size_t i = 0; i < level && i < levels.size(); ++i)
				for (size_t f = 0; f < levels[i].m_fields.size() && f < rr.levelValues[i].size(); ++f)
					if (const ibBackendQueryColumn* gc = levels[i].m_fields[f].m_col)
						node->m_values[gc->GetColumnId()] = rr.levelValues[i][f];
			// Only a node that can still have children is worth remembering — see the map's note.
			if (level < levels.size())
				nodes[std::move(key)] = node;
		}
		for (size_t i = 0; i < rr.aggs.size() && i < aggregates.size(); ++i) {
			const ibBackendQueryColumn* ac = aggregates[i].m_col;
			if (ac == nullptr)
				continue;
			// ⚠ AND NOTHING IS ROLLED OVER A DETAIL ROW THAT ALREADY HOLDS ITS VALUE. The figure
			// column carries two things — the subtotal at a heading, the row's own value on a row —
			// and on the phantom level the row's value is right there, read as part of its identity.
			// Overwriting it with the aggregate OF THAT ONE ROW is invisible for SUM (a sum of one
			// row is the row) and wrong for every other function: COUNT(Number) would print a 1
			// where the document's number belongs. Same rule the RAM fold states, same reason.
			if (node->m_kind == ibSelectorNodeKind::Detail && node->m_values.find(ac->GetColumnId()) != node->m_values.end())
				continue;
			node->m_values[ac->GetColumnId()] = rr.aggs[i];   // IN-PLACE in the aggregate's own column
		}
	}
	MarkRollupFolders(tree.Root());
	// …AND THE PERIODS THE SERVER HAD NOTHING TO REPORT FOR. A `GROUP BY` returns the periods that
	// have rows; the quiet month is missing by construction, on this road exactly as on the RAM one.
	// One statement of what padding means, applied to whichever tree came out.
	ibQueryComposer::PadPeriodLevels(tree, levels, aggregates);
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
		// The flat group keys and THEIR paths (parallel lists) — the plain GROUP BY road.
		for (size_t i = 0; i < spec.m_groupBy->size(); ++i)
			resolveDot((*spec.m_groupBy)[i], i < spec.m_groupPaths->size() ? (*spec.m_groupPaths)[i]
			                                                               : std::vector<const ibBackendQueryColumn*>{});
		// …AND THE TOTALS LEVELS' OWN FIELDS, which carry their paths themselves (ibTotalField). A
		// dot-walked DIMENSION rides the very same reference JOIN chain a dot-walked flat key rides —
		// it was refused outright only because nothing walked it through here. Keyed by the LEAF,
		// which is what colExpr / keyInfo will ask about.
		for (const ibTotalLevel& level : RollupLevelsOf(spec))
			for (const ibTotalField& f : level.m_fields)
				resolveDot(f.SqlCol(), f.m_path);
		for (const ibDataQueryBuilder::AggregateItem& a : RollupAggregatesOf(spec))
			resolveDot(a.m_col, a.m_path);

		// ⭐ A SOURCE THAT IS NOT A PLAIN TABLE PUTS ITSELF IN THE FROM. `GetSourceRelation(alias)` is
		// the queryable's own answer to "what am I read from" — null for an ordinary table (scan its
		// name), a derived table for the ones that cannot be a view: a register's Balance / Turnover
		// "as of a date" is an aggregate whose date lives in that subquery. The flat read asks it
		// (BuildSourceTree does, with the table name as the alias); this path did not, and built
		// `ibScan(name)` unconditionally — so a totals push-down over such a source rendered
		// `FROM  GROUP BY ROLLUP(…)`, with nothing at all between FROM and GROUP. Firebird said
		// exactly that: "Token unknown - line 1, column 219: GROUP" (2026-08-22, with the road forced
		// open). The engine was right; the statement was ours.
		const ibQueryRelPtr derived = q->GetSourceRelation(mainTable);
		ibQueryRelPtr from = hasDotWalk ? chain.From()
		                   : (derived ? derived : ibScan(mainTable));
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
			},
			// ⭐ THE QUERY'S OWN ORDER, HANDED OVER — not a second one derived from the levels. The
			// composition sorts the data the groupings run over and says so ONCE; the fold reflects it.
			// (The RAM fold gets the same thing for free: it groups in first-seen order over that read.)
			ibMetaIRBuilder::BuildSortKeys(q, *spec.m_sorts, /*reverse*/ false, mainQual));
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

		// ⚠ DELIBERATE BOUNDARY, and it is what this line MEANS today. A door that folds TOTALS levels
		// fills `m_totals`, not `m_groupBy` — so a MULTI-SOURCE totals refuses here and keeps the RAM
		// fold, which answers correctly. The single-source gate was taught to read the levels
		// (RollupLevelsOf); this one is not, because a level's fields would also have to be validated
		// against every union branch, and a push-down that skipped that check would return a WRONG
		// report rather than a slow one. Next step, not an oversight.
		if (spec.m_groupBy->empty()) return false;

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
		if (!CanColocateRollupTotals(spec))
			RollupDecline(wxT("the join tree is not co-locatable for a folded read"));
		// The connected driver must be able to fold the levels itself (FB5 / PG; NOT SQLite
		// -> RAM). Asked of L2 rather than read off its dictionary: what a driver CAN DO is a question,
		// and this tier has no business knowing which field carries the answer.
		ibConnectionScope scope(spec.m_holder);
		if (!scope)                        RollupDecline(wxT("no connection to ask whether it folds"));
		if (!ibCanPushRollup(scope.get())) RollupDecline(wxT("this engine has no GROUP BY ROLLUP"));
		return true;
	}

#undef RollupDecline   // the gate's own word — it ends with the gate

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
				[](const ibBackendQueryColumn*) -> RollupGroupKey { return { true, wxString(), nullptr }; },   // CanColocateRollupTotals guarantees SCALAR group keys (a co-located reference ROLLUP still RAM-folds)
				// No order here: the branch-union derived table renames every column to an inner alias,
				// so the query's sort keys do not name anything this statement can order by. A co-located
				// totals keeps whatever order the union produced — as it did before.
				std::vector<ibQuerySortKey>{});
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
			},
			// The co-located JOIN qualifies every column by its own leaf's table, and the query's sort
			// keys are built against the PRIMARY source — carrying them here would order by a name this
			// statement does not have. Unordered, as it was.
			std::vector<ibQuerySortKey>{});
	}

// Bind a write column's value positionally: a RAW column straight by its declared RawType (no
// translation — the uuid guid just goes in as a string); a metadata column via the TYPE-tagged
// SetValueColumn decomposition (over the column's type descriptor + the metadata context). The
// ONLY place SetValueColumn is called.
void BindWriteValue(ibQueryStatement& stmt, const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& v, int& pos)
{
	// The column decides — a raw one binds itself (ibBackendColumnRawDB::BindValue, below), everything
	// else is the codec's decomposition. This door stays because callers hold a column and a statement
	// and should not have to know which of the two applies.
	col->BindValue(stmt, metaData, v, pos);
}

// (A RAW COLUMN'S OWN READ AND BIND live in queryColumn.cpp, beside the defaults they override — what
//  a column DOES is one file, and this one stays the provider.)

// The L2-1 write CORE — buried in the provider. Identity is the WHERE section (spec conditions):
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
			catch (const ibBackendException&) { throw; }        // the DB's own reason — see the note at the INSERT below
			catch (...) { return -1; }
		}

		// INSERT / UPSERT — columns = every SetValue() assignment; UPSERT matches the IsPrimaryKey ones.
		const ibQueryStatement::Kind l2kind =
			(kind == WriteKind::Upsert) ? ibQueryStatement::Kind::Upsert : ibQueryStatement::Kind::Insert;

		// THE COLUMNS ARE THE STATEMENT'S, NOT THE ROW'S — so they are read off the first row and
		// every other row must agree with it. A row naming different columns, or the same ones in a
		// different order, would have its values written into somebody else's fields: the bind is
		// POSITIONAL once the statement exists, and nothing downstream can notice the mismatch.
		// Refused here, where the two lists are still side by side.
		const std::vector<ibWriteRow>& writeRows = *spec.m_writeRows;   // never empty — one row is the degenerate set
		const ibWriteRow&              firstRow  = writeRows.front();

		std::vector<wxString> columns;
		for (const auto& wv : firstRow)
			for (const wxString& f : ColumnFieldNames(wv.first)) columns.push_back(f);

		if (writeRows.size() > 1) {
			// Only a plain INSERT batches. An UPSERT needs the dialect's own match form and
			// Firebird's UPDATE OR INSERT takes no SELECT source; UPDATE and DELETE address rows by
			// key. Saying so is better than writing the first row and reporting success for N.
			if (kind != WriteKind::Insert)
				ibBackendCoreException::Error(_("Only an insert can write several rows in one statement"));

			// COMPARED BY FIELD NAME, NOT BY COLUMN POINTER.
			//
			// A raw column (ibBackendColumnRawDB — the parent row key a tabular section puts on every line)
			// is handed to SetValue BY VALUE, and the door owns a COPY of each one. So the same
			// logical column staged on two rows is two objects at two addresses, and a pointer
			// comparison calls them different — which would refuse every tabular section with more
			// than one line. The names are what the positional bind actually rides on, so they are
			// what is checked.
			std::vector<wxString> firstFields;
			for (const auto& wv : firstRow)
				for (const wxString& f : ColumnFieldNames(wv.first)) firstFields.push_back(f);

			for (const ibWriteRow& row : writeRows) {
				std::vector<wxString> rowFields;
				for (const auto& wv : row)
					for (const wxString& f : ColumnFieldNames(wv.first)) rowFields.push_back(f);

				if (rowFields.size() != firstFields.size())
					ibBackendCoreException::Error(_("A batched write has rows with different column counts"));
				for (std::size_t i = 0; i < rowFields.size(); ++i)
					if (rowFields[i] != firstFields[i])
						ibBackendCoreException::Error(_("A batched write has rows naming different columns"));
			}
		}

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
			for (size_t idx = 0; idx < firstRow.size(); idx++) {
				const auto& wv = firstRow[idx];
				const bool additive = spec.m_writeAdditive != nullptr
					&& !spec.m_writeAdditive->empty()
					&& idx < spec.m_writeAdditive->front().size()
					&& spec.m_writeAdditive->front()[idx];

				// ACCUMULATE (AddValue) — `col = col + <delta>`, computed by the DB. A statement's
				// values ARE IR expressions, so this occupies exactly the slot a bound Const would:
				// no L2 change and no raw-SQL hatch. It matters because a read-modify-write on the
				// client silently discards whatever landed between the read and the write, while an
				// in-statement addition composes with it — the same property that lets a totals
				// trigger accumulate without coordinating with anyone.
				if (additive) {
					const std::vector<wxString> fields = ColumnFieldNames(wv.first);
					// Arithmetic has no meaning on a spread column (a reference is two fields), so
					// this is single-field only. Falls through to a plain assignment rather than
					// emitting something wrong if that is ever violated.
					wxASSERT_MSG(fields.size() == 1, wxT("AddValue: additive write needs a single-field numeric column"));
					if (fields.size() == 1) {
						upd.SetParamAccumulate(p++, wv.second.GetNumber());
						continue;
					}
				}
				wv.first->BindValue(upd, metaData, wv.second, p);   // asked of the COLUMN — the codec is its default
			}
			// WHERE = the folded RLS predicate AND the door's OWN .Where() conditions. The conditions
			// were dropped here before, which made `.Where(...).Update()` update by primary key alone
			// — a caller narrowing to a subset silently hit every row the key matched. DELETE has
			// always folded both through BuildWhere; UPDATE now agrees with it.
			if (ibQueryExprPtr where = ibMetaIRBuilder::BuildWhere(spec.m_queryable, *spec.m_conditions,
			                                                       spec.m_predicate, wxEmptyString, /*pathAsExists*/ true))
				upd.SetWherePredicate(where);
			return upd.RunQuery();
		}

		// ONE ROW'S VALUES AS IR EXPRESSIONS, in ColumnFieldNames order — the same capture-statement
		// trick the temp-table manager uses (DecomposeCell): a write-only ibQueryStatement records
		// each SetParam* as an IR node, so the bytes are exactly the ones the column codec produces
		// for a bound parameter. Nothing here knows any SQL.
		//
		// ⭐ THROUGH BindWriteValue, THE SAME DOOR THE SINGLE-ROW PATH USES — not through
		// ibColumnCodec::WriteValue underneath it. A RAW column (the parent row key on every
		// tabular-section line) has its own branch in that door: one field, bound straight by its
		// declared RawType. The codec below the door serves METADATA columns, and its first act is
		// always a `_TYPE` discriminator — so a raw key captured through it yielded the TAG as its
		// first value, and the loop below, seeing the column has one field, took that tag AS THE KEY.
		// A number went to a CHAR(16) key column, the driver refused the bind, and because that
		// refusal was only logged the write reported success and wrote nothing: a tabular section
		// that saved cleanly and came back empty — but only from the SECOND row on, since one row
		// never reaches this path.
		auto rowAsValues = [&](const ibWriteRow& row) -> std::vector<ibQueryExprPtr> {
			std::vector<ibQueryExprPtr> out;
			out.reserve(columns.size());
			for (const auto& wv : row) {
				const std::vector<wxString> fields = ColumnFieldNames(wv.first);
				ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
				int cp = 1;
				wv.first->BindValue(capture, metaData, wv.second, cp);
				const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

				// ONE VALUE PER FIELD, EXACTLY — the bind is positional, so a column that produced a
				// different number of values than it declares fields does not misplace ITS OWN value,
				// it shifts every value after it into somebody else's column. This loop used to pad
				// and truncate silently, which is how the raw-key defect above stayed invisible: the
				// key column produced a tag plus a key, the loop kept the first of the two, and the
				// statement went out looking perfectly well-formed.
				if (consts.size() != fields.size())
					ibBackendCoreException::Error(
						_("A batched write captured %d values for a column declaring %d fields"),
						(int)consts.size(), (int)fields.size());
				for (size_t i = 0; i < fields.size(); ++i)
					out.push_back(consts[i] ? consts[i] : ibConst(ibValue()));
			}
			return out;
		};

		// BATCHED INSERT — N rows, one statement per chunk.
		//
		// It rides ibDmlStatement::m_extraRows, which ALREADY EXISTED for the temp-table manager's
		// bulk fill; the only thing that had to change is that L2 now spells it two ways, because
		// Firebird has no multi-row VALUES (see RenderDML). So this is a second TENANT of a
		// mechanism, not a second mechanism — and the temp filler gained Firebird for free.
		//
		// A thousand register lines used to be a thousand doors, a thousand renders and a thousand
		// round trips. Now it is a handful of statements, in the caller's own transaction.
		if (kind == WriteKind::Insert && writeRows.size() > 1) {
			// UNDER A ROW POLICY, STAY ONE ROW AT A TIME. The WITH CHECK below decides per row and
			// answers with a count; folded into a batch, "3 of 1000 were refused" and "1000 were
			// written" become the same number and the denial disappears. Correctness first: a batch
			// is an optimisation, and an optimisation may not change who may write what.
			if (spec.m_predicate) {
				long total = 0;
				for (const ibWriteRow& row : writeRows) {
					const std::vector<ibQueryExprPtr> vals = rowAsValues(row);
					std::vector<ibQueryProjItem> projItems;
					for (size_t i = 0; i < vals.size() && i < columns.size(); ++i)
						projItems.push_back(ibQueryProjItem{ vals[i], columns[i] });   // value AS <field>

					ibQueryRelPtr valuesRow = ibProject(nullptr, std::move(projItems));
					ibQueryExprPtr rls = ibMetaIRBuilder::BuildPredicateExpr(spec.m_queryable, spec.m_predicate, wxT("src"), /*pathAsExists*/ true);
					ibQueryRelPtr checked = ibFilter(ibSubquery(valuesRow, wxT("src")), rls);
					ibDatabaseQueryBuilder q(spec.m_holder);
					try {
						const long n = q.Execute(ibInsertSelect(table, columns, checked));
						if (n < 0) return -1;         // a refused row stops the set — the caller's TX rolls back
						total += n;
					}
					catch (const ibBackendException&) { throw; }
					catch (...) { return -1; }
				}
				return total;
			}

			// CHUNKED, because one statement is not the same as one good statement. A thousand-row
			// VALUES list (or its UNION ALL twin) is a very large parse tree and a very long
			// statement text, and Firebird in particular has a hard ceiling on both; past some width
			// the parse costs more than the round trips it saves. The temp-table manager settled on
			// 50 for the same reason, and a register row is wider than a temp row — several physical
			// fields per logical column — so this stays in the same neighbourhood rather than
			// inventing a second number. A thousand lines become a handful of statements either way;
			// the curve is flat well before here, so there is nothing to win by tuning it per driver.
			const std::size_t kRowsPerStatement = 50;

			long total = 0;
			for (std::size_t start = 0; start < writeRows.size(); start += kRowsPerStatement) {
				const std::size_t end = (std::min)(start + kRowsPerStatement, writeRows.size());

				ibDmlStatement ins(ibDmlKind::Insert);
				ins.m_table = table;
				const std::vector<ibQueryExprPtr> first = rowAsValues(writeRows[start]);
				for (size_t k = 0; k < columns.size() && k < first.size(); ++k)
					ins.m_assignments.push_back(ibDmlAssign{ columns[k], first[k] });
				for (std::size_t r = start + 1; r < end; ++r)
					ins.m_extraRows.push_back(rowAsValues(writeRows[r]));

				ibDatabaseQueryBuilder q(spec.m_holder);
				try {
					const long n = q.Execute(ins);
					if (n < 0) return -1;
					total += n;
				}
				catch (const ibBackendException&) { throw; }   // the DB's own reason travels up intact
				catch (...) { return -1; }
			}
			return total;
		}

		// WITH CHECK on CREATE — a restricted INSERT. The folded RLS predicate rides on a derived ONE-ROW
		// relation of this row's own values: INSERT INTO t (cols) SELECT * FROM (SELECT val AS f, …
		// [FROM dual]) src WHERE <rls over src>. The row is inserted IFF it satisfies the restriction; 0
		// rows -> the new row is outside this role's scope -> the builder denies. One statement, entirely in
		// the DB. Reuses the write value-spread (Const capture) exactly as DecomposeEquality, so the
		// projected bytes match what the predicate compares against; the src alias qualifies the predicate.
		if (kind == WriteKind::Insert && spec.m_predicate) {
			std::vector<ibQueryProjItem> projItems;
			for (const auto& wv : firstRow) {
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
			catch (const ibBackendException&) { throw; }
			catch (...) { return -1; }
		}

		// The single-row path — one row staged, or an UPSERT (which does not batch).
		ibQueryStatement statement(l2kind, table, columns, matchKeys, spec.m_holder);
		int position = 1;
		for (const auto& wv : firstRow)
			wv.first->BindValue(statement, metaData, wv.second, position);

		// A FAILED WRITE MUST SAY WHY. `catch (...) -> -1` turned every driver error — no such table, no such
		// column, a constraint — into a bare "false" that the object's SaveData reported as "failed to save the
		// object data". The one thing the user needed (WHICH table, WHICH column) was thrown away at the exact
		// moment something decided to stop. An ibBackendException carries that text, so it goes up; anything
		// else still degrades to -1 rather than crossing the door as an unknown type.
		try { return statement.RunQuery(); }        // rows inserted / upserted
		catch (const ibBackendException&) { throw; }
		catch (...) { return -1; }
	}

// Generate L2-1 by substituting names — all read from the spec; Build() is connection-free.
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

					// ⭐ A NON-SCALAR LEAF IS NOT ONE FIELD. A reference / enum / composite dimension
					// (group by Parent, by Ref.Ref) is stored as a SPREAD, so it is projected the way
					// every other object output is: each physical field under the dimension's alias as
					// a prefix, reassembled on the read (ColumnObject). Projecting only its first field
					// gave the fold a key it could not read — every row landed in one empty group.
					if (!leaf->IsRawColumn()) {
						const wxString base = leaf->GetPhysicalName();
						for (const wxString& f : ColumnFieldNames(leaf))
							projection.push_back(ibQueryProjItem{ ibCol(a, f), dw.m_alias + f.Mid(base.length()) });
						continue;
					}

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
				if (s.m_expr) {   // computed sort (ORDER BY <expression>) — lower the L3 expr to L2-1, sort on it
					ibQuerySortKey k;
					k.m_expr = ibMetaIRBuilder::BuildColumnExpr(queryable, s.m_expr, mainQual);
					k.m_dir  = (req.m_reverseSort ? !s.m_ascending : s.m_ascending) ? ibQuerySortDir::Asc : ibQuerySortDir::Desc;
					q.AddSortKey(std::move(k));
					continue;
				}
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
		AttachNamedQueries(spec, ir);               // WITH … — the queries this statement declared
		return ir;
	}

// ⭐ THE NAMED QUERIES THIS STATEMENT DECLARED, into the SAME IR — `WITH a AS (…) SELECT …`.
//
// Each is an ordinary door, so it is lowered by the ordinary road: its own spec through this very
// function, and what comes back is a RELATION — the same tree a FROM subquery would hold. That is
// the whole implementation, and it is why nothing about building a query had to change: a CTE is a
// query that ended up written in a different PLACE.
//
// Recursion is the inner query's own business: a named query that itself declares one arrives here
// with its own list and is handed back whole. (A cycle is a query the engine refuses, and refusing
// it there rather than counting hops here keeps one opinion about what a valid query is.)
// ⭐⭐ AND THE SAME QUESTION, ASKED ONE STEP EARLIER. The lowering has to know BEFORE it declares a
// query whether the declaration will render — because after it has declared one there is no road
// back, and the statement would name a table its own text never wrote (`-206`).
//
// A single source always renders: that is the page read, unchanged. A JOIN renders when the tree
// co-locates, which is the read fast path's own gate — no second opinion about what a renderable
// join is, and no chance of the two drifting.
bool ibDbTableProvider::CanDeclareAsNamedQuery(const ibDataQueryBuilder& inner)
	{
		const ibDataQuerySpec spec = inner.BuildSpec();
		if (spec.m_queryable == nullptr)
			return false;
		if (spec.m_root == nullptr || spec.m_root->m_kind != ibQueryNode::Kind::Join)
			return true;   // one source — the ordinary declared read
		// A joined declaration is the co-located join, so it must clear that path's gate: real DB
		// leaves, distinct tables, column keys, per-leaf conditions. The OUTPUT check is the read
		// gate's own (CanColocateJoin), which additionally refuses aggregates and computed columns —
		// a declaration carrying either has no server-side form here either.
		return CanColocateJoin(spec);
	}

void ibDbTableProvider::AttachNamedQueries(const ibDataQuerySpec& spec, ibQueryIR& ir)
	{
		if (spec.m_with == nullptr || spec.m_with->empty())
			return;
		for (const ibDataQueryBuilder::ibNamedQuery& named : *spec.m_with) {
			if (named.m_name.IsEmpty() || !named.m_inner)
				continue;
			const ibDataQuerySpec innerSpec = named.m_inner->BuildSpec();
			if (innerSpec.m_queryable == nullptr)
				continue;   // nothing to read — a declaration of nothing declares nothing
			const std::vector<ibQuerySortItem> innerSort =
				ibDataQueryBuilder::EffectiveSort(innerSpec.m_queryable, *innerSpec.m_sorts);

			// ⭐ A JOINED DECLARATION IS THE CO-LOCATED JOIN, WRITTEN SOMEWHERE ELSE. BuildPageIR reads
			// ONE queryable (it asks the spec for its table name), so a declaration whose door holds a
			// join tree has to be assembled the way the join READ is: the leaves into one server-side
			// FROM, the per-leaf conditions qualified by their own tables. Same builders, same gate —
			// CanDeclareAsNamedQuery asked the same question before the lowering chose this road, so a
			// tree that reaches here is one that renders.
			//
			// No ORDER BY on this branch: a declared query is a SET the reader orders, and a join
			// tree has no single table to qualify a keyset against anyway.
			ColocatedLeaves leaves;
			ibQueryIR innerIR;
			if (ColocatableJoinTree(innerSpec, leaves)) {
				ibDatabaseQueryBuilder jq(innerSpec.m_holder);
				jq.From(BuildColocatedFrom(innerSpec.m_root, leaves));
				if (ibQueryExprPtr where = ColocatedWhere(innerSpec, leaves))
					jq.Where(where);
				if (innerSpec.m_topCount > 0)
					jq.Limit(innerSpec.m_topCount);   // …on this road too — see the note on the other one
				innerIR = jq.Build();
			}
			else {
				leaves.clear();
				// The inner query is read WHOLE — a page request belongs to the reader, not to what it
				// reads: limiting the named query would silently narrow every mention of it.
				// ⭐ AND ITS OWN LIMIT TRAVELS WITH IT. `SELECT TOP 10 … INTO x` declares ten rows, not the
				// table: an empty page request rendered the body unbounded, which is why a declaration was
				// refused outright for carrying a TOP ("whose limit a declaration would drop" — it did).
				// The limit belongs to the declared query, so it is written inside the WITH, where every
				// engine that has WITH accepts it; nothing outside has to know the declaration was capped.
				ibReadPageRequest innerPage;
				innerPage.m_count = innerSpec.m_topCount;   // 0 = unbounded, as before
				innerIR = BuildPageIR(innerSpec, innerPage, innerSort);
			}

			// ⭐⭐ A DECLARED QUERY PUBLISHES NAMES, SO ITS SELECT HAS TO SPELL THEM.
			//
			// An ordinary read never projects: BuildPageIR renders `SELECT *` and the RESULT picks the
			// columns apart afterwards, by column object. That is fine when the reader holds the source's
			// own columns — and wrong for a CTE, whose reader holds ibCteQueryable's columns and writes
			// their names into SQL. Unprojected, the declaration published `Ref` while the statement
			// inside it published `fld1021_RRRef`, and Firebird answered `-206 Column unknown REF_RRREF`
			// (measured 2026-08-23, the first report that ever took this road).
			//
			// The spread is the same one every projected read uses: a raw column is one field under its
			// alias; a metadata column is its FULL physical spread under the alias as PREFIX
			// (`fld1021_RRRef AS Ref_RRRef`), which is exactly what the outer query asks for when it
			// spreads the CTE's own column of that name. One rule, both sides of the declaration.
			//
			// 🛑⭐ AND THE OUTPUTS LIVE IN **TWO** LISTS. A door records a plain read in `m_selectCols`
			// and a computed one in `m_selectExprs` — two lists because they are written differently,
			// and this loop knew only the first. So `34 AS YTFDS` was PUBLISHED (the publisher walks the
			// output SCHEMA, where both kinds stand side by side) and never WRITTEN: ten fields declared,
			// nine spelled. The reader then asked for a name the statement had not written — first
			// silently, as `Field 'YTFDS_TYPE' not found` per row, and then out loud the moment a person
			// sorted by it: `-206 Column unknown YTFDS_N` (Max, 2026-08-24).
			//
			// The two halves of a declaration must agree about WHAT EXISTS, and agreeing means walking
			// the same outputs — not the same-looking list.
			const bool hasExprs = innerSpec.m_selectExprs != nullptr && !innerSpec.m_selectExprs->empty();
			if ((innerSpec.m_selectCols != nullptr && !innerSpec.m_selectCols->empty()) || hasExprs) {
				std::vector<ibQueryProjItem> proj;
				std::vector<const ibBackendQueryColumn*> projected;      // …once each — see below
				std::vector<wxString>                    projectedNames;  // …under each NAME once — see below
				const std::vector<std::pair<const ibBackendQueryColumn*, wxString>> noCols;
				for (const auto& sc : (innerSpec.m_selectCols != nullptr ? *innerSpec.m_selectCols : noCols)) {
					const ibBackendQueryColumn* col = sc.first;
					if (sc.second.IsEmpty())
						continue;   // an output with no name cannot be read by one

					// ⭐⭐ A COMPUTED OUTPUT IS SELECTED BY ITS NAME. `34 AS YTFDS`, `Amount * Rate AS X`,
					// anything the author wrote as an expression: it has no COLUMN behind it, and the
					// relation this projection wraps has ALREADY computed it under that very alias — so
					// the way to keep it is to name it, not to rebuild it.
					//
					// 🛑 IT WAS DROPPED HERE while the declaration went on PUBLISHING it (the publisher's
					// guards all pass for a null column), so the two halves disagreed about what exists —
					// the one thing both of their comments swear cannot happen. The reader then looked
					// for a name the statement never wrote: *"Field 'YTFDS_TYPE' not found in the
					// resultset"*, eight times per compose (measured 2026-08-24).
					if (col == nullptr) {
						proj.push_back(ibQueryProjItem{ ibColQ(wxString(), sc.second), sc.second });
						continue;
					}
					// ⚠ WHAT HAS NO FIELDS CANNOT BE PROJECTED. Asked as "what does the layout say this
					// column is made of" rather than as a question about its type: a column with slots
					// projects them, a column with none has nothing to write into a SELECT and is left
					// out (DeclareNamedResultAsCte leaves it out of the published set in the same
					// breath, so the two sides agree on what exists). This is what the `-206` was: a
					// name in the statement that no table had a field for.
					//
					// The alias is the column's PHYSICAL name plus the slot's ROLE — `fld1672_D`, not
					// `PointInTime_D`. Two reasons, and the second is why it is not the output name:
					//   * the role, because a COMPUTED column's fields belong to OTHER columns and
					//     share no base with it (the moment's date field is the DATE column's), so
					//     cutting a base off the field name would cut the wrong string;
					//   * the physical name, because it is `fld<metaID>` — unique per metatype. Two
					//     sources both publishing `Ref` (or `PointInTime`) would otherwise write the
					//     same alias twice, and the engine refuses the statement outright.
					// The reader spells the same pair, since the declared column carries that physical
					// name (ibCteQueryable::Field), so the two agree by construction.
					// ⚠ A COLUMN WHOSE FIELDS ARE SOMEBODY ELSE'S IS NOT PROJECTED — it says so itself.
					// The MOMENT lays out as the date's field and the reference's pair, and those two
					// project themselves under those very names; writing them again would put one alias
					// in the select list twice (`-104 … specified multiple times`, 2026-08-23). It needs
					// no projection: the read assembles it from the two the statement already carries.
					//
					// ⭐ …BUT IT IS STILL PUBLISHED. Not projected and not published are two different
					// answers, and giving the second one here is what made a declared moment vanish:
					// `unknown attribute 'PointInTime' on source 'q_sub0'`. DeclareNamedResultAsCte
					// publishes the COLUMN ITSELF once its parts are in this select list — so the outer
					// query may name it, and reading it lands on these very fields.
					if (col->IsSyntheticColumn())
						continue;

					// ⚠ …AND A COLUMN PROJECTED ONCE IS NOT PROJECTED AGAIN. The select list is a list of
					// OUTPUTS, and two outputs may stand over ONE column — the same field asked for twice,
					// under two names. Their aliases are built from the column's PHYSICAL name, so both
					// spell `fld<metaID>_TYPE` and the engine refuses the statement:
					// `-104 … column FLD1667_TYPE was specified multiple times for derived table Q_SUB0`
					// (measured 2026-08-24, a report that would not compose).
					//
					// The published set is deduped by the same rule (DeclareNamedResultAsCte), so the two
					// sides go on agreeing about what exists — which is the whole reason the synthetic
					// test above is written in both places.
					// ⭐ …AND "AGAIN" MEANS UNDER THE SAME NAME. One column may legitimately be projected
					// TWICE under two aliases (`Attribute2`, and `Attribute2 AS Attribute21`) — those are
					// two outputs, and the outer query may fold by either. What must not happen is the
					// same output written twice, which is the `-104` this guard was added for.
					//
					// 🛑 IT DEDUPED BY THE COLUMN ALONE, so the second alias was silently dropped from
					// the select list — and from the published set by its twin — while the outer query
					// went on naming it: *"unknown attribute 'Attribute21' on source 'q_sub0'"*, a report
					// that would not compose (measured 2026-08-24).
					const bool repeated = std::find(projected.begin(), projected.end(), col) != projected.end();
					if (repeated && std::find(projectedNames.begin(), projectedNames.end(), sc.second) != projectedNames.end())
						continue;
					projected.push_back(col);
					projectedNames.push_back(sc.second);

					// …and QUALIFIED BY THE LEAF THAT OWNS IT when the declaration is a join: two tables
					// in one FROM make a bare field name ambiguous to the engine even where the two
					// spellings differ, and the qualifier is the same one the join's own ON uses.
					// Empty for a single-source declaration, which is the unqualified read it was.
					//
					// ⭐ A REPEATED COLUMN SPELLS ITS ALIAS FROM THE OUTPUT NAME, not from `fld<metaID>`:
					// the physical name is already taken by the first projection of it, and two items of
					// one name is the very refusal above. The publisher declares the same spelling for it,
					// so the two sides keep agreeing.
					const wxString base = repeated ? sc.second : col->GetPhysicalName();
					const wxString qual = leaves.empty() ? wxString() : ColocatedQual(leaves, col);
					for (const ibColumnSlot& slot : DescribeColumnLayout(col))
						proj.push_back(ibQueryProjItem{ ibColQ(qual, slot.m_name),
							slot.m_role == ibColumnRole::Raw ? base
							                                 : base + ibFieldSuffix(slot.m_role) });
				}

				// …AND THE COMPUTED OUTPUTS ARE **EVALUATED** HERE, not named.
				//
				// 🛑 Naming them was the first attempt and it refused one layer deeper: `-206 Column
				// unknown YTFDS` INSIDE the declaration (measured 2026-08-25). The premise was wrong —
				// the relation this wraps has not computed anything, because an ordinary read renders
				// `SELECT *` and the computed columns only ever enter a projection that someone writes.
				// This IS that projection, so it owes the expression itself.
				//
				// Lowered exactly as the ordinary projected read lowers it (BuildColumnExpr AS the
				// alias) — one way of writing a computed column, wherever it is written.
				if (hasExprs) {
					for (const ibQueryColumnSelect& se : *innerSpec.m_selectExprs) {
						if (se.m_alias.IsEmpty())
							continue;   // an output with no name cannot be read by one
						if (std::find(projectedNames.begin(), projectedNames.end(), se.m_alias) != projectedNames.end())
							continue;
						projectedNames.push_back(se.m_alias);
						proj.push_back(ibQueryProjItem{
							ibMetaIRBuilder::BuildColumnExpr(innerSpec.m_queryable, se.m_expr), se.m_alias });
					}
				}

				if (!proj.empty())
					innerIR.m_root = ibProject(innerIR.m_root, std::move(proj));
			}

			ir.m_with.push_back({ named.m_name, innerIR.m_root });
			// …and anything IT declared travels with it, ahead of it: an engine reads the list top to
			// bottom, and a name must be declared before the query that mentions it.
			ir.m_with.insert(ir.m_with.end() - 1, innerIR.m_with.begin(), innerIR.m_with.end());
		}
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
	return attr->ReadValue(attr->GetPhysicalName(), attr->GetMetaData(), retValue, result, createData);
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
// ibDbResultSource — the DB cursor MATERIALISATION: walk the L2-1 result, lift each column up
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
	col->ReadValue(col->GetPhysicalName(), metaData, v, result);
	return v;
}

// Physical scan — walks the L2-1 cursor; each column is lifted via the column-based read rule. It
// holds the metadata context (from the source's queryable) so a reference / enum column can
// reconstruct its value without the attribute. The materialisation lives here, over the L2-1 result.
class ibDbResultSource : public ibDataResultSource {
public:
	ibDbResultSource(ibQueryResult&& cursor, const ibBackendQueryable* queryable)
		: m_cursor(std::move(cursor)), m_metaData(queryable != nullptr ? queryable->GetMetaData() : nullptr) {}

	bool Next() override { return m_cursor.Next(); }

	ibValue Value(const ibBackendQueryColumn* col) const override {
		if (col == nullptr)
			return ibValue();
		// ONE ROAD FOR EVERY KIND — the column reads itself. This used to open with
		// `if (col->IsRawColumn())` and repeat the raw switch (guid out of sixteen bytes, a
		// single-target reference through its target) beside the same switch in the bind and in a
		// second reader; each copy had to remember the same facts. A raw column now carries that
		// itself (queryColumn.cpp), so this asks and does not test.
		return ProviderReadColumn(col, m_metaData, m_cursor);
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
		col->ReadValue(prefix, m_metaData, v, m_cursor);
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
	: m_source(std::make_shared<ibDbResultSource>(std::move(cursor), queryable))
{
}
