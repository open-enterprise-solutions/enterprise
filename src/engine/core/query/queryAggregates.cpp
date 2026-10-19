////////////////////////////////////////////////////////////////////////////
//	Description : Which aggregates a type admits, and the ready calls over a field (queryAggregates.h)
////////////////////////////////////////////////////////////////////////////

#include "core/query/queryAggregates.h"

std::vector<ibQueryKeyword> ibQueryAggregatesFor(const ibTypeDescription& type)
{
	// COUNT is always in — it asks nothing of the type, and a list with nothing in it would be a
	// cell a person cannot fill.
	std::vector<ibQueryKeyword> out{ ibQueryKeyword::Count };

	// UNKNOWN OR COMPOSITE: offer everything. Which type a row holds is the row's business, and
	// narrowing here on a guess would take away a choice the query can perfectly well make.
	if (type.GetClsidCount() != 1) {
		out.push_back(ibQueryKeyword::Sum);
		out.push_back(ibQueryKeyword::Min);
		out.push_back(ibQueryKeyword::Max);
		out.push_back(ibQueryKeyword::Avg);
		return out;
	}

	const bool numeric = type.ContainType(ibValueTypes::TYPE_NUMBER);
	const bool ordered = numeric || type.ContainType(ibValueTypes::TYPE_DATE)
	                             || type.ContainType(ibValueTypes::TYPE_STRING);

	if (numeric) out.push_back(ibQueryKeyword::Sum);
	if (ordered) { out.push_back(ibQueryKeyword::Min); out.push_back(ibQueryKeyword::Max); }
	if (numeric) out.push_back(ibQueryKeyword::Avg);
	return out;
}

std::vector<wxString> ibQueryAggregateCallsFor(const ibTypeDescription& type, const wxString& field)
{
	std::vector<wxString> calls;
	if (field.IsEmpty())
		return calls;

	for (const ibQueryKeyword keyword : ibQueryAggregatesFor(type)) {
		calls.push_back(ibQueryKeywordText(keyword) + wxT("(") + field + wxT(")"));

		// …AND ITS DISTINCT FORM where that asks a DIFFERENT question — `COUNT(DISTINCT x)` counts how
		// many different values there are, `COUNT(x)` how many rows have one. WHICH functions get the
		// twin is the keyword table's answer (ibDistinctMattersFor), so MIN and MAX do not: their
		// result is the same value however often it occurs, and a padded list is one people stop
		// reading. The same rule the expression editor already follows.
		if (ibDistinctMattersFor(keyword))
			calls.push_back(ibQueryKeywordText(keyword) + wxT("(")
				+ ibQueryKeywordText(ibQueryKeyword::Distinct) + wxT(" ") + field + wxT(")"));
	}
	return calls;
}
