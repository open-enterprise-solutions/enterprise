#ifndef __QUERY_HIERARCHY_H__
#define __QUERY_HIERARCHY_H__

////////////////////////////////////////////////////////////////////////////
//	Description : «IN HIERARCHY» - the operator's own machinery: what a named value
//	              STANDS FOR, and what its subordinates are reported UNDER
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ A VALUE NAMES ITS SUBTREE, AND THAT IS ONE WORD ANSWERING TWO QUESTIONS.
//
//   Elements       «in»            - exactly the values handed over. A list is a list; nothing is
//                                    implied about what stands under them.
//   Hierarchy      «in hierarchy»  - each named value AND everything subordinate to it, reported
//                                    UNDER THE NAMED ONE: a summary account with ten detail accounts
//                                    under it is ONE line, and the ten are its parts.
//   HierarchyOnly                  - the subordinates WITHOUT the value that names them.
//
// So the word carries a FILTER (which values are admitted) and a FOLD (which value a row is reported
// under). They are two halves of one answer, which is why one object holds both: a caller that
// filtered by the subtree and then reported every row under its own value has answered a question
// nobody asked.
//
// ⭐⭐ ASKED OF THE COLUMN, THROUGH THE PROVIDER - never of the value. The left-hand side of the
// filter says which source the subtree lives in: the provider resolves that column to its TARGET
// (`ResolveReferenceTarget`, the one metadata owner), the target vends its row key and its parent
// column, and the whole parent map is read in ONE query. This is the same road `TOTALS BY x
// HIERARCHY` already takes (`ibQueryComposer::BuildReferenceHierarchy`) — one mechanism, so a report
// and a filter that both say "in hierarchy" cannot come to mean different things.
//
// ⚠ It was written the other way round first — from the VALUE: cast it to a reference, ask its
// metaobject for a parent link, then one query PER NODE. That is a metadata cast in a tier that is
// not allowed metadata, and a read per node where one read does; both are gone.
//
// ⚠ NOT A CODE PREFIX. "Every account whose code starts with 60" is the tempting one-liner and it
// is wrong: a code is a presentation somebody may re-number, the parent link is the fact.
//
// ⚠ THE SUBTREE IS RESOLVED INTO VALUES, not expressed as an operator the database has to
// understand: what reaches the server is an ordinary IN, which every one of the four drivers renders
// with no recursive CTE and no dialect that has to spell one. That is also why the language's
// `IN HIERARCHY` takes a PARAMETER and never a subquery - the values have to be IN HAND first.

#include "backend/backend.h"          // BACKEND_API
#include "backend/compiler/value.h"   // ibValue
#include "queryAst.h"                 // ibQueryDimUnfold - the three words, spelled once for both venues

// ⚠ NAMED, NOT INHERITED — MSVC hands these over transitively and GCC / Clang do not.
// See docs/portability.md.
#include <map>
#include <memory>
#include <unordered_map>   // value-keyed indexes — see ibValueHash (value.h)
#include <vector>

class ibBackendQueryable;
class ibBackendQueryColumn;

class BACKEND_API ibQueryHierarchyScope
{
public:
	ibQueryHierarchyScope() = default;

	// `source` + `column` are the LEFT-HAND SIDE of the filter — the column whose values these are —
	// and they are what the subtree is read through. The values as they were NAMED, plus the word
	// saying how far down each of them reaches: Elements keeps them as they are and asks the database
	// nothing; the other two read the target's parent map once and walk down it.
	//
	// A column that resolves to no target, or a target with no parent column (a FLAT list — the one
	// arrangement that records no parent), leaves every named value standing for itself. That is a
	// degradation and not a failure: «in hierarchy» over a flat list IS the list.
	ibQueryHierarchyScope(const ibBackendQueryable* source, const ibBackendQueryColumn* column,
	                      const std::vector<ibValue>& named, ibQueryDimUnfold unfold);

	// Nothing named at all. NOT the same as "nothing matches": what an empty scope means is the
	// caller's to decide - a register argument left out means every account, while an `IN ()`
	// written in a query means the empty set.
	bool IsEmpty() const { return m_accepted.empty(); }

	// Every value the filter admits - the named ones, their subordinates, or only the subordinates.
	const std::vector<ibValue>& Accepted() const { return m_accepted; }

	// The value a row is REPORTED under. Named in hierarchy: the value that was asked for, which is
	// what makes the subordinates add up into it. Otherwise the row's own value - and that is also
	// the answer when nothing was asked for at all, where every value stands for itself.
	ibValue ReportedUnder(const ibValue& value) const;

private:
	std::vector<ibValue> m_accepted;               // what the filter admits
	// Keyed by the VALUE, not by a string rendered from it — see ibValueHash in
	// value.h. A reference keys by its guid there just as it did through
	// GetHashKey, without the text.
	std::unordered_map<ibValue, ibValue, ibValueHash, ibValueEqual> m_reportedUnder;   // admitted value -> the one it rolls up into
};

// ONE VALUE OR A LIST OF THEM, READ THE SAME WAY. A parameter may hold a single reference or a
// collection of them, and «in hierarchy» takes either - so the flattening is written once, beside
// the operator, rather than at each place that accepts the operand.
BACKEND_API std::vector<ibValue> ibQueryHierarchyNamedValues(const ibValue& given);

#endif
