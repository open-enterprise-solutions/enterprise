#ifndef __QUERY_PROVIDER_INTERNAL_H__
#define __QUERY_PROVIDER_INTERNAL_H__

////////////////////////////////////////////////////////////////////////////
//	The RAM engine's OWN vocabulary — what the composer and the computed provider say to each other.
////////////////////////////////////////////////////////////////////////////
//
// queryProvider.h is the face every caller includes; this is how the composer (queryComposer.cpp) and the
// computed provider (computed/computedProvider.cpp) speak to each other when they read rows in memory, and
// nothing here reaches the face. The same shape as queryLoweringInternal.h. The names were file-local and
// stay as they were, so they live in a namespace of their own.

#include "backend/query/queryProvider.h"            // ibDataQuerySpec / ibDataQueryResult (+ dataQueryBuilder.h, queryable.h)
#include "backend/query/queryRamTable.h"            // ibQueryRamTable — what the RAM engine reads and writes
#include "backend/query/queryRowCursor.h"           // ibQueryRow — what an expression is evaluated over

#include <map>
#include <memory>
#include <vector>

namespace ibProvider {

// An empty RAM table with the given columns, and a row of src appended to dst by those columns.
ibQueryRamTable RamTableOf(const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols);
void AppendRowByCols(const ibQueryRamTable& src, long srcRow, ibQueryRamTable& dst,
                     const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols);

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

// The source's rows — computed by it, or read through its provider when only its walks brought it here — + dot-walk
// resolution + dot-walk WHERE (shared read/aggregate).
ibQueryRamTable ComputeRowsResolved(const ibDataQuerySpec& spec,
                                    std::vector<std::shared_ptr<const ibBackendQueryColumn>>& present);
// The spec with every «IN HIERARCHY» named on rows filtered here resolved into its subtree — body in
// queryComposer.cpp, beside RefuseNamedHierarchy.
ibDataQuerySpec WithSubtreesResolved(const ibDataQuerySpec& spec, const std::vector<std::shared_ptr<const ibBackendQueryable>>& sources);
// Per-row RAM evaluation of a computed (arithmetic / CASE) expression — body in queryComposer.cpp; the
// computed read / WHERE / aggregate paths evaluate SELECT / WHERE / SUM expressions with it.
// It takes a ROW, whoever holds it (a materialised table's row, a streaming cursor's current row): the
// evaluation reads the current row and nothing else, so it names nothing bigger. The (table, index)
// spelling stays for the paths that address rows by index.
// ⭐⭐ …AND THE WINDOWS ALREADY FOLDED, when the read is composed HERE rather than by the server.
//
// A window has no value on one row — it is a fold over the row's PARTITION — so it cannot be answered
// by an evaluator that sees a row and nothing else. But when the rows were stitched in memory they are
// all in hand, which is exactly what a fold needs. So they are computed ONCE over the whole table,
// before the projection loop, and the evaluator is handed the result: node -> a value per row.
//
// Null (the default) = "no windows here", which is every read that has none and every road that lets
// the server do it. A window node reaching the evaluator with no value for it is then still a defect
// worth being told about, and says so.
struct ibRamWindowValues {
	std::map<const ibQueryColumnExpr*, std::vector<ibValue>> m_byNode;   // per WindowAgg node, per TABLE row
	long m_row = 0;                                                      // which row is being evaluated
};
ibValue EvalColumnExprRow(const ibQueryColumnExprPtr& e, const ibQueryRow& row, const ibRamWindowValues* win = nullptr);
ibValue EvalColumnExprRow(const ibQueryColumnExprPtr& e, const ibQueryRamTable& t, long row, const ibRamWindowValues* win = nullptr);
// Fold every window in `e` over `t`, into `out`. Recursive: a window's own input is evaluated first.
void RamComputeWindows(const ibQueryColumnExprPtr& e, const ibQueryRamTable& t, ibRamWindowValues& out);

// A synthetic output column for a computed SELECT expression (Qty * Price AS x) over a computed source:
// the alias as its name + a unique id. ExecuteRead evaluates the expr into this column; GetColumn(alias)
// finds it by name, the sort / limit rebuild keeps it by id.
class ibComputedExprColumn : public ibBackendQueryColumn {
public:
	ibComputedExprColumn(const ibString& name, ibMetaID id) : m_name(name), m_id(id) {}
	ibString           GetName()         const override { return m_name; }
	ibString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
	// COMPUTED: an expression evaluated per row, named by its alias — stored nowhere.
	Kind               GetColumnKind()   const override { return Kind::Computed; }
private:
	ibString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

// RAM GROUP BY over a composed table: bucket the rows by the group keys, fold the aggregates per bucket.
ibDataQueryResult RamAggregate(const ibQueryRamTable& TC, const ibDataQuerySpec& spec);

// The rows of a page held in memory, in order — sorted by `sorting` (a key that is an expression computed for its row),
// turned round for a page read backwards, started past the anchor. `order` gets every row, the page first (what
// ibQueryRamTable::ReorderRows takes); the answer is how many of them are the page. Body in queryComposer.cpp.
long RamPageOrder(const ibQueryRamTable& rows, const std::vector<ibQuerySortItem>& sorting, const ibReadPageRequest& page,
                  std::vector<long>& order);

} // namespace ibProvider

#endif
