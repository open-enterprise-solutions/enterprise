#ifndef __QUERY_PROVIDER_INTERNAL_H__
#define __QUERY_PROVIDER_INTERNAL_H__

////////////////////////////////////////////////////////////////////////////
//	The composer's OWN vocabulary — what its folds over rows in memory say to each other.
////////////////////////////////////////////////////////////////////////////
//
// queryProvider.h is the face every caller includes; this is what queryComposer.cpp's folds and evaluators share, and
// nothing here reaches the face. The same shape as queryLoweringInternal.h. The names were file-local and stay as they
// were, so they live in a namespace of their own.

#include "backend/query/queryProvider.h"            // ibDataQuerySpec / ibDataQueryResult (+ dataQueryBuilder.h, queryable.h)
#include "backend/query/queryRamTable.h"            // ibQueryRamTable — the rows a fold reads
#include "backend/query/queryRowCursor.h"           // ibQueryRow — what an expression is evaluated over

#include <memory>
#include <vector>

namespace ibProvider {

// ⭐ Ids for columns nothing declared, made where a RESULT is assembled rather than where a query is
// written — so there is no door to ask, and the ORDINARY value is the position they already count by.
// The KIND is what keeps them clear of every other minted id; nobody has to know anybody's range.
// (ibBackendQueryColumn::SyntheticId — see there for what replaced the old band map.)
using ibSynthKind = ibBackendQueryColumn::SyntheticKind;
inline ibMetaID ibSynthId(ibSynthKind kind, ibMetaID value) {
	return ibBackendQueryColumn::SyntheticId(kind, value);
}
// ⚠ THE ORDINAL IS WHAT ADVANCES, NOT THE ID. A composed id is a kind stamped onto a value, so
// `id + 1` names ANOTHER KIND of the same value — never the next column. Everything below counts
// the slot (the place the column already occupies in the list that holds it) and composes at use.
inline ibMetaID ibSynthSlotId(ibSynthKind kind, size_t slot) {
	return ibSynthId(kind, static_cast<ibMetaID>(slot));
}

// The spec with every «IN HIERARCHY» named on rows filtered here resolved into its subtree — body in
// queryComposer.cpp, beside RefuseNamedHierarchy.
ibDataQuerySpec WithSubtreesResolved(const ibDataQuerySpec& spec, const std::vector<std::shared_ptr<const ibBackendQueryable>>& sources);
// Per-row evaluation of a computed (arithmetic / CASE) expression — body in queryComposer.cpp; a fold's computed figure
// and a result's computed output are evaluated with it.
// It takes a ROW, whoever holds it (a materialised table's row, a streaming cursor's current row): the
// evaluation reads the current row and nothing else, so it names nothing bigger. A window is no value of one row —
// the statement folds it (OVER), on either executor.
ibValue EvalColumnExprRow(const ibQueryColumnExprPtr& e, const ibQueryRow& row);

} // namespace ibProvider

#endif
