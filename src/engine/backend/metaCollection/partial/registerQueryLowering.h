#ifndef __REGISTER_QUERY_LOWERING_H__
#define __REGISTER_QUERY_LOWERING_H__

// Shared L2-IR lowering helpers for register managers (information / accumulation /
// accounting): turn an attribute's metadata into physical-field IR — the flat field
// list and a composite (multi-field) predicate with bound Const values. Lets register
// balance / turnover / slice queries be built as structured ibQueryIR instead of raw
// concatenated SQL (dialect-free, injection-impossible, no manual positional binding).

#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

// All physical fields of an attribute (the TYPE tag + per-type fields; a reference
// expands to _RTRef + _RRRef), in the order SetValueAttribute binds them.
inline std::vector<wxString> ibRegFieldsOf(const ibValueMetaObjectAttributeBase* a)
{
	std::vector<wxString> out;
	const ibValueMetaObjectAttributeBase::ibSQLField sql = ibValueMetaObjectAttributeBase::GetSQLFieldData(a);
	out.push_back(sql.m_fieldTypeName);
	for (const auto& d : sql.m_types) {
		if (d.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
			out.push_back(d.m_field.m_fieldRefName.m_fieldRefType);
			out.push_back(d.m_field.m_fieldRefName.m_fieldRefName);
		}
		else out.push_back(d.m_field.m_fieldName);
	}
	return out;
}

// The first VALUE field of an attribute (the field after the TYPE tag) — the column a
// register resource / record-type aggregate operates on (res_N, recordType_N / _E).
inline wxString ibRegValueField(const ibValueMetaObjectAttributeBase* a)
{
	const ibValueMetaObjectAttributeBase::ibSQLField sql = ibValueMetaObjectAttributeBase::GetSQLFieldData(a);
	return sql.m_types.empty() ? wxString() : sql.m_types[0].m_field.m_fieldName;
}

// A composite predicate (attr <op> value) as IR, each per-field value bound as a Const —
// the structured form of GetCompositeSQLFieldName + SetValueAttribute. The TYPE tag (and
// a reference's _RTRef) always compare '='; the value fields use `op`. `qualifier`
// optionally qualifies the columns (table.col) for a join.
inline ibQueryExprPtr ibRegCompositeIR(const ibValueMetaObjectAttributeBase* a, const ibValue& v,
                                       ibQueryBinOp op, const wxString& qualifier = wxEmptyString)
{
	std::vector<wxString> fields; wxString cur;
	for (const wxUniChar ch : ibValueMetaObjectAttributeBase::GetSQLFieldName(a)) {
		if (ch == wxT(',')) { if (!cur.empty()) fields.push_back(cur); cur.clear(); }
		else cur += ch;
	}
	if (!cur.empty()) fields.push_back(cur);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibValueMetaObjectAttributeBase::SetValueAttribute(a, v, &capture, pos);
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
