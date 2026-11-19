#ifndef __IB_SIMILAR_NAMES_H__
#define __IB_SIMILAR_NAMES_H__

// ⭐ THE NAMES SPELLED SIMILAR TO ONE ASKED FOR, as a list for a refusal - empty when none is. Similar: one name holds
// the other (`StrFind` holds `Find`), or they differ by one letter - two from six letters on (`StrSplt`), as two in a
// short name is another word. Upper and lower case are one, as the language reads names. Up to three, in the order
// they are handed.
//
// One rule, asked where a name was not found: by the compiler of the functions a call can reach, and by the
// interpreter of a value's members, for one the value does not have (2026-11-19, an assistant writing a demo base:
// `StrFind` for `Find`; a member of a query's result and of a spreadsheet document guessed and refused unnamed).
//
// ⚠ It is a "did you mean", and wants PRECISION: a wrong suggestion beside a refusal sends the author the wrong way.
// metadata_create's warning of an object by nearly the same name wants the opposite - not to miss `Company` beside
// `Companies` - and keeps a looser rule of its own (NearlyTheSameName, mcpToolMetadataWrite.cpp).

#include "backend/compiler/value.h"

#include <algorithm>
#include <vector>

inline ibString ibSimilarNames(const ibString& name, const std::vector<ibString>& names)
{
	if (name.Len() < 3)
		return ibString();

	const ibString asked = name.Upper();
	const auto distance = [](const ibString& a, const ibString& b) {
		std::vector<size_t> row(b.Len() + 1);
		for (size_t j = 0; j <= b.Len(); j++)
			row[j] = j;
		for (size_t i = 1; i <= a.Len(); i++) {
			size_t diagonal = row[0];
			row[0] = i;
			for (size_t j = 1; j <= b.Len(); j++) {
				const size_t above = row[j];
				row[j] = std::min({ row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1) });
				diagonal = above;
			}
		}
		return row[b.Len()];
	};

	ibString found;
	int count = 0;
	for (const ibString& one : names) {
		if (count >= 3)
			break;
		const ibString candidate = one.Upper();
		const bool holds = candidate.Len() >= 3 && (asked.Contains(candidate) || candidate.Contains(asked));
		if (holds || distance(asked, candidate) <= (asked.Len() >= 6 ? 2u : 1u)) {
			found += (found.IsEmpty() ? ibString() : ibString(ibT(", "))) + one;
			count++;
		}
	}
	return found;
}

// …among a value's members: its properties, then its methods - the table the lookup that missed has just asked.
inline ibString ibSimilarNames(const ibString& name, const ibValue& value)
{
	std::vector<ibString> members;
	for (long idx = 0; idx < value.GetNProps(); idx++)
		members.push_back(value.GetPropName(idx));
	for (long idx = 0; idx < value.GetNMethods(); idx++)
		members.push_back(value.GetMethodName(idx));
	return ibSimilarNames(name, members);
}

#endif
