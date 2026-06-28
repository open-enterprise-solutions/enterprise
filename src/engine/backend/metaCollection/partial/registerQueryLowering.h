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

// A composite predicate (attr <op> value) as IR, each per-field value bound as a Const —
// the structured form of the compare predicate + SetValueAttribute. The TYPE tag (and
// a reference's _RTRef) always compare '='; the value fields use `op`. `qualifier`
// optionally qualifies the columns (table.col) for a join.
inline ibQueryExprPtr ibRegCompositeIR(const ibValueMetaObjectAttributeBase* a, const ibValue& v,
                                       ibQueryBinOp op, const wxString& qualifier = wxEmptyString)
{
	const std::vector<wxString> fields = ColumnFieldNames(a);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibDbTableProvider::SetValueAttribute(a, v, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	ibQueryExprPtr pred;
	for (size_t i = 0; i < fields.size(); ++i) {
		ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
		const bool tag = (i == 0) || fields[i].EndsWith(wxT("_RTRef"));
		ibQueryExprPtr col = qualifier.empty() ? ibCol(fields[i]) : ibCol(qualifier, fields[i]);
		ibQueryExprPtr term = ibBinOp(tag ? ibQueryBinOp::Eq : op, col, c);
		pred = pred ? ibBinOp(ibQueryBinOp::And, pred, term) : term;
	}
	return pred;
}

#endif // __REGISTER_QUERY_LOWERING_H__
