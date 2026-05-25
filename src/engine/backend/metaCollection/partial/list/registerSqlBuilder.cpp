////////////////////////////////////////////////////////////////////////////
//	Author      : (paging refactor)
//	Description : Cursor-paginated SQL builder for register lists.
//	              See docs/paging-design.md §8.
////////////////////////////////////////////////////////////////////////////

#include "registerSqlBuilder.h"

#include "objectList.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

/////////////////////////////////////////////////////////////////////////////

std::vector<const ibValueMetaObjectAttributeBase*>
ibRegisterSqlBuilder::IdentityAttrs(const ibValueMetaObjectRegisterData* meta)
{
	std::vector<const ibValueMetaObjectAttributeBase*> ret;
	if (meta == nullptr) return ret;

	if (meta->HasRecorder()) {
		ret.push_back(meta->GetRegisterRecorder());
		ret.push_back(meta->GetRegisterLineNumber());
		return ret;
	}

	if (meta->HasPeriod())
		ret.push_back(meta->GetRegisterPeriod());

	for (auto& dim : meta->GetGenericDimensionArrayObject())
		ret.push_back(dim);

	return ret;
}

/////////////////////////////////////////////////////////////////////////////

std::vector<ibRegisterSqlBuilder::OrderCol>
ibRegisterSqlBuilder::EffectiveOrder(const ibValueMetaObjectRegisterData* meta,
                                     const ibSortOrder& sort)
{
	std::vector<OrderCol> ret;

	// Pass 1: enabled user sort cols, in declaration order.
	std::vector<ibMetaID> seen;
	for (const auto& s : sort.m_sorts) {
		if (!s.m_sortEnable) continue;
		const ibValueMetaObjectAttributeBase* attr =
			meta->FindAnyAttributeObjectByFilter(s.m_sortModel);
		wxASSERT(attr);
		if (attr == nullptr) continue;

		ret.push_back({ attr, s.m_sortAscending });
		seen.push_back(attr->GetMetaID());
	}

	// Pass 2: identity tail — append cols not already in user sort.
	for (auto* idAttr : IdentityAttrs(meta)) {
		if (idAttr == nullptr) continue;
		const ibMetaID id = idAttr->GetMetaID();
		if (std::find(seen.begin(), seen.end(), id) != seen.end())
			continue;
		ret.push_back({ idAttr, /*ascending*/ true });
		seen.push_back(id);
	}

	return ret;
}

/////////////////////////////////////////////////////////////////////////////

wxString ibRegisterSqlBuilder::BuildSelectHead(ibDatabaseLayer* db,
                                               const wxString& tableName,
                                               int firstN)
{
	if (db != nullptr && db->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD)
		return wxString::Format("SELECT FIRST %d * FROM %s ", firstN, tableName);
	return wxString::Format("SELECT * FROM %s ", tableName);
}

wxString ibRegisterSqlBuilder::AppendLimit(ibDatabaseLayer* db, int n)
{
	if (db != nullptr && db->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		return wxString::Format(" LIMIT %d", n);
	return wxEmptyString;
}

/////////////////////////////////////////////////////////////////////////////

wxString ibRegisterSqlBuilder::BuildFilterWhere(const ibValueMetaObjectRegisterData* meta,
                                                const ibFilterRow& filter)
{
	wxString whereText;
	bool firstWhere = true;

	for (const auto& f : filter.m_filters) {
		if (!f.m_filterUse) continue;

		const ibValueMetaObjectAttributeBase* attr =
			meta->FindAnyAttributeObjectByFilter(f.m_filterModel);
		wxASSERT(attr);
		if (attr == nullptr) continue;

		const wxString op =
			f.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";

		whereText += firstWhere ? " WHERE " : " AND ";
		firstWhere = false;
		whereText += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attr, op);
	}
	return whereText;
}

/////////////////////////////////////////////////////////////////////////////

wxString ibRegisterSqlBuilder::BuildOrderBy(const std::vector<OrderCol>& effective,
                                            bool reverse)
{
	wxString orderText;
	bool firstOrder = true;

	for (const auto& c : effective) {
		const bool asc = reverse ? !c.m_ascending : c.m_ascending;
		const wxString op = asc ? "ASC" : "DESC";

		for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(c.m_attr)) {
			const wxString fieldName =
				(field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference)
				? field.m_field.m_fieldRefName.m_fieldRefName
				: field.m_field.m_fieldName;

			if (firstOrder) {
				orderText += " ORDER BY ";
				firstOrder = false;
			}
			else {
				orderText += ", ";
			}
			orderText += wxString::Format("%s %s", fieldName, op);
		}
	}

	return orderText;
}

/////////////////////////////////////////////////////////////////////////////

wxString ibRegisterSqlBuilder::BuildAnchorPredicate(const std::vector<OrderCol>& effective,
                                                    bool whereAlreadyOpen,
                                                    ibFetchDirection direction)
{
	if (effective.empty()) return wxEmptyString;

	// Lexicographic OR-of-AND cursor predicate.  The earlier flat
	// "c1 cmp ? AND c2 cmp ? AND ... AND case-when tail" form was
	// wrong: a row with `c1` ahead of anchor but `c2` behind anchor
	// still sorts ahead lexicographically, but flat-AND rejected it
	// (one of its `>=` clauses fails).  Symptom: register list shows
	// a partial subset after refresh because non-trivial dimension
	// combinations are filtered out — same class of bug
	// listSqlBuilder.cpp:174-195 documents and fixes for catalogs.
	//
	// Proper composite predicate:
	//   (c_0 strict_op_0 ?)
	//   OR (c_0 = ? AND c_1 strict_op_1 ?)
	//   OR ...
	//   OR (c_0 = ? AND ... AND c_{K-1} = ? AND c_{K-1} tail_op ?)
	// where strict_op_i flips ASC/DESC by direction, and the LAST
	// col uses inclusive op on Reset (anchor row itself included)
	// or strict otherwise.  Register's identity tail (recorder+line
	// or period?+dimensions) is part of `effective` already, so the
	// last col IS the row-identity disambiguator — no separate uuid
	// clause needed.
	//
	// Bind order in BindAnchorParams must match: clause i needs
	// (c_0, ..., c_{i-1}) equality + c_i strict|incl → i+1 binds.

	const bool forward = (direction == ibFetchDirection::Forward) ||
	                     (direction == ibFetchDirection::Reset);
	const bool inclusiveTail = (direction == ibFetchDirection::Reset);

	auto strictOpFor = [&](bool asc) -> wxString {
		return (forward == asc) ? ">" : "<";
	};
	auto inclusiveOpFor = [&](bool asc) -> wxString {
		return (forward == asc) ? ">=" : "<=";
	};

	auto eqClauseUpTo = [&](size_t kExclusive) -> wxString {
		wxString eq;
		for (size_t j = 0; j < kExclusive; ++j) {
			if (!eq.IsEmpty()) eq += " AND ";
			eq += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(
				effective[j].m_attr, "=");
		}
		return eq;
	};

	wxString predicate;
	auto addOr = [&](const wxString& clause) {
		if (!predicate.IsEmpty()) predicate += " OR ";
		predicate += clause;
	};

	const size_t K = effective.size();
	for (size_t i = 0; i < K; ++i) {
		wxString clause;
		const wxString eqPart = eqClauseUpTo(i);
		if (!eqPart.IsEmpty()) clause += eqPart + " AND ";
		// Last col gets the inclusive op on Reset so the anchor row
		// itself is in items[0].
		const bool isLast = (i + 1 == K);
		const wxString op = (isLast && inclusiveTail)
			? inclusiveOpFor(effective[i].m_ascending)
			: strictOpFor(effective[i].m_ascending);
		clause += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(
			effective[i].m_attr, op);
		addOr("(" + clause + ")");
	}

	const wxString header = whereAlreadyOpen ? " AND " : " WHERE ";
	return header + "(" + predicate + ")";
}

/////////////////////////////////////////////////////////////////////////////

void ibRegisterSqlBuilder::BindFilterParams(ibPreparedStatement* statement,
                                            int& position,
                                            const ibValueMetaObjectRegisterData* meta,
                                            const ibFilterRow& filter)
{
	for (const auto& f : filter.m_filters) {
		if (!f.m_filterUse) continue;

		const ibValueMetaObjectAttributeBase* attr =
			meta->FindAnyAttributeObjectByFilter(f.m_filterModel);
		wxASSERT(attr);
		if (attr == nullptr) continue;

		ibValueMetaObjectAttributeBase::SetValueAttribute(
			attr, f.m_filterValue, statement, position);
	}
}

/////////////////////////////////////////////////////////////////////////////

void ibRegisterSqlBuilder::BindAnchorParams(ibPreparedStatement* statement,
                                            int& position,
                                            const std::vector<OrderCol>& effective,
                                            const std::vector<ibValue>& sortValues)
{
	wxASSERT(sortValues.size() == effective.size());

	// Bind order matches BuildAnchorPredicate's OR-of-AND form:
	// clause i (i = 0..K-1):  c_0 =, c_1 =, ..., c_{i-1} =, c_i strict|incl
	//                          → i+1 binds, all from sortValues[0..i].
	// Total binds: K(K+1)/2.
	for (size_t i = 0; i < effective.size(); ++i) {
		for (size_t j = 0; j <= i; ++j) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(
				effective[j].m_attr, sortValues[j], statement, position);
		}
	}
}
