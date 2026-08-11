#ifndef __REGISTER_QUERY_LOWERING_H__
#define __REGISTER_QUERY_LOWERING_H__

// Shared L2-IR lowering helpers for register managers (information / accumulation /
// accounting): turn an attribute's metadata into physical-field IR — the flat field
// list and a composite (multi-field) predicate with bound Const values. Lets register
// balance / turnover / slice queries be built as structured ibQueryIR instead of raw
// concatenated SQL (dialect-free, injection-impossible, no manual positional binding).

#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dbTableProvider.h"   // ibDbTableProvider::SetValueAttribute — the DB write decomposition
#include "backend/query/columnLayout.h"      // the column-layout tier: ColumnFieldNames / ColumnFieldList / etc.
#include "backend/system/value/valueMap.h"   // ibValueStructure — the sugar ibRegFilterPredicate converts

// --- register-side convenience over the column-layout tier ---------------------------------------
// An attribute IS an ibBackendQueryColumn; the tier functions take (col, metaData). These thin
// wrappers derive the metadata from the attribute so register lowering reads cleanly — the SQL-field
// machinery lives in the tier, NOT on the attribute. The structured ibSQLField is GONE: a register
// builds the comma-joined field list with ibRegFieldList, or picks the _TYPE tag / first value field.
inline wxString       ibRegFieldList (const ibValueMetaObjectAttributeBase* a, const wxString& aggr = wxEmptyString) { return ColumnFieldList(a, aggr); }
inline wxString       ibRegComposite (const ibValueMetaObjectAttributeBase* a, const wxString& cmp = wxT("=")) { return ColumnComparePredicate(a, cmp); }

// All physical fields of an attribute (the TYPE tag + per-type fields; a reference
// expands to _RTRef + _RRRef), in the order SetValueAttribute binds them.
inline std::vector<wxString> ibRegFieldsOf(const ibValueMetaObjectAttributeBase* a)
{
	return ColumnFieldNames(a);
}

// The _TYPE discriminator field name (the first physical field of a composite column).
inline wxString ibRegTypeField(const ibValueMetaObjectAttributeBase* a)
{
	const std::vector<wxString> fields = ColumnFieldNames(a);
	return fields.empty() ? wxString() : fields[0];   // [0] is always the _TYPE tag
}

// The first VALUE field of an attribute (the field after the TYPE tag) — the column a
// register resource / record-type aggregate operates on (res_N, recordType_N / _E).
inline wxString ibRegValueField(const ibValueMetaObjectAttributeBase* a)
{
	const std::vector<wxString> fields = ColumnFieldNames(a);
	return fields.size() > 1 ? fields[1] : wxString();   // [0] is the _TYPE tag; [1] is the first value field
}

// "q.f1, q.f2, …" — the attribute's field list qualified by a table alias (the SELECT/GROUP BY form
// used in the dr/cr join queries). Replaces the hand-rolled qualified walks over ibSQLField.
inline wxString ibRegQualifiedList(const ibValueMetaObjectAttributeBase* a, const wxString& q)
{
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a))
		out += (out.empty() ? wxString() : wxString(",")) + q + wxT(".") + f;
	return out;
}

// "q.f1 AS <alias><suffix>, …" — qualified + aliased (dr.fld_N AS AccountDr_N). The suffix is the
// field's tail past the base name (_TYPE / _N / _RTRef …), so the alias mirrors the physical layout.
inline wxString ibRegAliasedList(const ibValueMetaObjectAttributeBase* a, const wxString& q, const wxString& alias)
{
	const wxString base = a->GetPhysicalName();
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a)) {
		const wxString suffix = f.length() > base.length() ? f.Mid(base.length()) : wxString();
		out += (out.empty() ? wxString() : wxString(", ")) + q + wxT(".") + f + wxT(" AS ") + alias + suffix;
	}
	return out;
}

// "lq.f1 = rq.f1 AND …" — an all-fields equality join between two qualifiers (the dr/cr recorder join).
inline wxString ibRegJoinEq(const ibValueMetaObjectAttributeBase* a, const wxString& lq, const wxString& rq)
{
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a))
		out += (out.empty() ? wxString() : wxString(" AND ")) + lq + wxT(".") + f + wxT(" = ") + rq + wxT(".") + f;
	return out;
}

// "q.f1 = ? AND …" — an all-fields equality against bound params (a qualified composite filter).
inline wxString ibRegQualifiedEqParams(const ibValueMetaObjectAttributeBase* a, const wxString& q)
{
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a))
		out += (out.empty() ? wxString() : wxString(" AND ")) + q + wxT(".") + f + wxT(" = ?");
	return out;
}

// ⭐⭐ WHAT A FILTER IS: a STRUCTURE that converts into a PREDICATE the query engine understands.
//
// That is the whole definition, and it settles every question the old code kept re-asking — what a
// filter may express, who unwraps it, which register does it differently. Nobody does it
// differently: there is one converter, and past it there are no structures, only conditions.
//
// ⭐ AND THE QUERY TEXT ALREADY DOES THIS, IMPLICITLY. `WHERE Warehouse = &Warehouse` is parsed into
// the same predicate; nobody had to write a converter for it because parsing IS the conversion. So
// the predicate has TWO producers — the parser and this function — and one form. That is why the
// structure had to be converted rather than carried: carried, it would have been a second currency
// that only one of the two entrances could spend.
// A script hands `New Structure("Warehouse", W)` — a runtime value, name to value. A query hands a
// condition. They were two different things all the way down: the structure was unwrapped BY HAND in
// five places, each rebuilding the same map, each able to drift from the others, and each able to
// express one thing only — equality on a dimension.
//
// So the sugar is converted ONCE, into what a query would have written: `Dimension = <value>`,
// AND-folded, the values riding as bound comparison values exactly as a parameter does. Everything
// downstream then sees a predicate and stops caring which door the reader came through.
//
// ⚠ Only DIMENSIONS are filterable here. A resource is what the table FOLDED, and a condition over a
// fold belongs to the result rather than to an argument of the source — the same rule
// FillConditionExplorer offers to the window, so the two cannot disagree about what may be filtered.
template <typename TRegister>
inline ibQueryPredicatePtr ibRegFilterPredicate(const TRegister* reg, const ibValue& filter)
{
	ibValueStructure* structure = nullptr;
	if (reg == nullptr || !filter.ConvertToValue(structure) || structure == nullptr)
		return nullptr;   // nothing was asked for — which is not an empty filter, but no filter at all

	ibQueryPredicatePtr folded;
	for (const auto dimension : reg->GetDimensionArrayObject()) {
		ibValue value;
		if (dimension == nullptr || !structure->Property(dimension->GetName(), value))
			continue;

		ibQueryCondition leaf;
		leaf.m_col   = dimension;
		leaf.m_op    = ibQueryFilterOp::Equal;
		leaf.m_value = value;

		ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
		folded = folded ? ibQueryPredicate::Compose(ibQueryPredicateKind::And, folded, one) : one;
	}
	return folded;
}

// The flat AND-leaves of a predicate, in order.
//
// ⚠ This is what the hand-built L2 aggregates can apply TODAY, and it is deliberately narrow: an OR,
// a range, a NOT are exactly the shapes those aggregates cannot express — and exactly the reason the
// readings are moving onto the door, where a predicate rides natively. Until then a richer condition
// is not silently half-applied: it simply has no leaves at this level and the caller sees that.
// It yields the leaf's COLUMN as it stands — nothing narrows it back to a metaobject, because
// nothing below needs one: ibRegCompositeIR spreads a column by asking the column.
inline void ibRegFlatLeaves(const ibQueryPredicatePtr& predicate,
                            std::vector<std::pair<const ibBackendQueryColumn*, ibValue>>& out)
{
	if (!predicate)
		return;
	if (predicate->m_kind == ibQueryPredicateKind::Leaf) {
		if (predicate->m_leaf.m_col != nullptr && predicate->m_leaf.m_op == ibQueryFilterOp::Equal)
			out.push_back({ predicate->m_leaf.m_col, predicate->m_leaf.m_value });
		return;
	}
	if (predicate->m_kind != ibQueryPredicateKind::And)
		return;
	for (const ibQueryPredicatePtr& child : predicate->m_children)
		ibRegFlatLeaves(child, out);
}

// A composite predicate (column <op> value) as IR, each per-field value bound as a Const —
// the structured form of the compare predicate. The TYPE tag (and a reference's _RTRef) always
// compare '='; the value fields use `op`. `qualifier` optionally qualifies the columns (table.col)
// for a join.
//
// ⭐ NAME THE COLUMN, GET ITS WHOLE FIELD SET. The spread is the COLUMN's own — `ColumnFieldNames`
// asks it, the codec writes through it — so a caller says "this resource" and never lists fields.
//
// ⚠ IT TAKES A COLUMN, NOT AN ATTRIBUTE, and that is a removal rather than a widening. The attribute
// was required for exactly one thing: to hand over `GetMetaData()`. Everything else already ran on
// the column face. Demanding the metaobject made every caller holding a plain column narrow back to
// one — a cast, which is the model saying it lost something on the way in. Now the metadata is
// passed, the column is enough, and nothing casts.
inline ibQueryExprPtr ibRegCompositeIR(const ibBackendQueryColumn* a, const ibMetaData* metaData,
                                       const ibValue& v,
                                       ibQueryBinOp op, const wxString& qualifier = wxEmptyString)
{
	// ⭐ THE SLOTS, NOT JUST THE NAMES. Which fields TAG the value (_TYPE, a reference's _RTRef) and
	// which CARRY it is a question about the field's ROLE, and the layout tier answers it — this used
	// to guess from position (`i == 0`) and spelling (`EndsWith("_RTRef")`), i.e. re-derive the
	// layout here, where a change to the lettering would never arrive.
	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(a);

	std::vector<wxString> fields;
	fields.reserve(slots.size());
	for (const ibColumnSlot& slot : slots)
		fields.push_back(slot.m_name);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(a, metaData, v, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	ibQueryExprPtr pred;
	for (size_t i = 0; i < slots.size(); ++i) {
		ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
		const bool tag = !ibIsValueRole(slots[i].m_role);
		ibQueryExprPtr col = qualifier.empty() ? ibCol(fields[i]) : ibCol(qualifier, fields[i]);
		ibQueryExprPtr term = ibBinOp(tag ? ibQueryBinOp::Eq : op, col, c);
		pred = pred ? ibBinOp(ibQueryBinOp::And, pred, term) : term;
	}
	return pred;
}

#endif // __REGISTER_QUERY_LOWERING_H__
