////////////////////////////////////////////////////////////////////////////
//	Description : L3 query COMPOSER (queryComposer.h). The door
//	              (dataQueryBuilder.cpp) is metadata- and L2-BLIND: it builds an
//	              ibDataQuerySpec and runs it through the verbs it declares and this
//	              file defines. THIS TU realizes:
//	                * the folds a selection runs over a result's rows (namespace ibProvider)
//	                  — declared in the face (querySelector.h), defined here;
//	                * ibSubqueryQueryable (nested-query source) out-of-line bodies;
//	                * ibQueryComposer — the road a spec takes: a join or a union as one
//	                  source (OverComposite), what its own base cannot read in a base in
//	                  memory (OverMemorySpace), read by one translation either way
//	                  (docs/private/query-one-road.md §7);
//	                * the door's verbs (ibDataQueryBuilder::ExecuteRead … ExecuteWrite).
//	              The selection a road ends in (ibDataQueryResult) is queryResult.cpp's.
//	              The BIG DB provider (real-table read/write engine + ibMetaIRBuilder
//	              lowering + Get/SetValueAttribute) lives in database/dbTableProvider.cpp.
//	              See docs/private/query-language-arc.md §18, §22.
////////////////////////////////////////////////////////////////////////////

#include "queryComposer.h"                                            // ibQueryComposer (+ queryProvider.h: ibBackendQueryProvider; dataQueryBuilder.h, queryable.h)
#include "queryProviderInternal.h"                                    // ibProvider — the folds' vocabulary (ids minted for a result, the windows)
#include "backend/query/queryRamTable.h"                              // ibQueryRamTable — L3's raw flat snapshot (names no runtime type but ibValue)
#include "backend/query/queryRowCursor.h"                             // ibQueryRow / ibQueryRowCursor — what a fold reads (one pass, no table)
#include "backend/query/querySelector.h"                              // ibSelector — result.Select(mode) hands the drained snapshot to it
#include "backend/query/queryLowering.h"                              // CastTarget / WalkEnters — a walk a CAST narrowed, read where the walk is joined; ExprIsAnsweredHere / ibTypeTestOf — what has no SQL
#include "backend/query/resultSource.h"                               // ibDataResultSource — the backing ibRamTableResultSource derives
#include "backend/query/columnLayout.h"                               // ibSqlAliasOf — what the STATEMENT calls an output, vs what its author does
#include "backend/query/provider/database/tempTableManager.h"         // ibTempTableManager — a source copied into a base in memory, a semi-join's keys into a temp
#include "backend/query/provider/composedQueryable.h"                 // ibComposedQueryable — a join or a union read as one source
#include "backend/query/queryException.h"                             // ibBackendQueryException — a named hierarchy refused over rows in memory
#include "backend/query/queryHierarchy.h"                             // ibQueryHierarchyScope — «IN HIERARCHY» resolved where rows are filtered in memory
#include "backend/databaseLayer/databaseMemoryBase.h"                 // ibMemorySpace — where a read its own base cannot write runs


#include <deque>                                                      // the streaming fold's pools — see ibStreamingFold
#include <map>                                                        // dot-walk join dedup + col->attr cache
#include <functional>                                                 // std::function — reference-hierarchy parent chain-up
#include <algorithm>                                                  // stable_sort — a fold's groups in their order
#include <set>                                                        // a nested query's minted ids
#include <unordered_set>                                              // DISTINCT folds — keyed by value, see ibValueHash
#include <unordered_map>                                              // a fold's buckets, keyed by value
#include <limits>                                                     // a value with no place in its kind's order sorts last

using namespace ibProvider;

// (THE QUERYABLE'S OWN BODIES MOVED OUT — GetProvider, ResolveColumnByName and the identity of a source
//  with no metaobject live in queryable.cpp, beside their header, the way a column's do in
//  queryColumn.cpp. This file is the COMPOSER; the computed provider is computed/computedProvider.cpp.)

// ==========================================================================
// ibSubqueryQueryable — a nested query as a source. It is a COMPUTED (RAM) source: the
// inner query is RUN and its rows materialised into an ibQueryRamTable, so the outer query
// reads / filters / joins it like any computed leaf. Columns = the inner explicit
// Select(col, alias) list, else (SELECT *) the inner primary source's full column set; an
// AGGREGATE inner exposes its GROUP BY keys + a synthetic numeric column per aggregate
// alias. The bodies live here (where ibDataQueryBuilder is complete). (docs §22 / §23)
// ==========================================================================

// Synthetic output column of an AGGREGATE subquery (SUM(x) AS total): a raw numeric field
// named by the aggregate alias, under its OWN unique model id (the RAM-table read key). The
// id is kind-stamped (SyntheticKind::Subquery), so it is negative and cannot be an application
// number nor collide with anything else this tier mints.
namespace ibProvider {

class ibSubqueryAggColumn final : public ibBackendColumnRawDB
{
public:
	// `available` — whether what it folds may be shown (feature options), taken when it is made.
	ibSubqueryAggColumn(const ibString& alias, ibMetaID id, bool available)
		: ibBackendColumnRawDB(alias, RawType::Number), m_id(id), m_available(available) {}
	ibMetaID GetColumnId() const override { return m_id; }
	bool IsAvailable() const override { return m_available; }
private:
	ibMetaID m_id;
	bool     m_available;
};

// ⭐⭐ A COLUMN OF THE INNER QUERY, SEEN UNDER ITS OUTPUT NAME.
//
// A nested table's columns are its OUTPUT — `… AS Attribute1Code` is what the outer query can write,
// and the only thing it can write. The exposed set was the inner columns themselves, so an aliased
// projection appeared under the underlying column's name (`Code`) and the outer query asking for the
// alias was told there is no such attribute — about a name the inner query plainly declares.
//
// Renaming in place is not possible (the same column object belongs to its own table and is read
// through it), so the two questions are separated: this answers "what is it called out here", and
// the value is fetched through the real column it stands for.
class ibSubqueryAliasColumn final : public ibBackendQueryColumn
{
public:
	// 🛑 WHAT IT NEEDS FROM THE COLUMN IT STANDS FOR IS TAKEN HERE, NOT KEPT AS A POINTER. It asks
	// exactly two things — the physical name and the type — and both are known at construction, so
	// holding the column afterwards buys nothing. It once cost a lifetime too: the source could be a
	// nested wrapper's own column, handed out raw, which died with that wrapper while this alias was
	// read for as long as the outer query ran. That is what crashed on 2026-08-19 — `GetTypeDesc`
	// through a freed column (0xdddddddd) while a report was being composed.
	// `available` — what the road to `from` adds: a dot-walk's hops before its leaf (one of them hidden and
	// the leaf goes with it).
	// `role` — what the output is in a balance: the query's own `ROLE`, else the column's (the schema's answer).
	ibSubqueryAliasColumn(const ibString& alias, const std::shared_ptr<const ibBackendQueryColumn>& from, ibMetaID id, bool available,
	                      ibBalanceRole role, int rank)
		: m_alias(alias)
		, m_physical(from != nullptr ? from->GetPhysicalName() : alias)
		, m_type(from != nullptr ? from->GetTypeDesc() : ibTypeDescription())
		, m_metaData(from != nullptr ? from->GetMetaData() : nullptr)
		, m_id(id)
		, m_available(available && (from == nullptr || from->IsAvailable()))
		, m_balanceRole(role)
		, m_periodRank(rank) {}

	ibString GetName() const override { return m_alias; }
	ibString GetPhysicalName() const override { return m_physical; }
	// The TYPE is the column's own — an alias renames, it does not re-type — taken at construction.
	ibTypeDescription& GetTypeDesc() const override { return m_type; }
	ibMetaID GetColumnId() const override { return m_id; }
	// …and so is whether it may be shown (feature options), for the same reason: taken, not pointed at.
	bool IsAvailable() const override { return m_available; }
	// …and what it is in a balance: a renamed opening balance is still one.
	ibBalanceRole GetBalanceRole() const override { return m_balanceRole; }
	int           GetPeriodRank()  const override { return m_periodRank; }
	// …and whose values it holds — its column's application, taken with its type.
	const ibMetaData* GetMetaData() const override { return m_metaData; }

private:
	ibString                  m_alias;
	ibString                  m_physical;
	mutable ibTypeDescription m_type;   // GetTypeDesc returns a non-const ref (engine-wide signature)
	const ibMetaData*         m_metaData;
	ibMetaID                  m_id;
	bool                      m_available;
	ibBalanceRole             m_balanceRole;
	int                       m_periodRank;
};

// A COMPUTED projection of the inner query — `a * b`, a CASE — seen from outside under its alias.
//
// ⚠ ITS TYPE IS UNKNOWN, and that is said rather than guessed: the door records a computed select as
// expression + alias and nothing else, so there is no column to take a type from. An EMPTY type
// description is how this engine already spells "unknown" — the places that ask (which aggregates
// fit, can this be unfolded) treat it as "offer everything" instead of believing a wrong answer.
class ibSubqueryExprColumn final : public ibBackendQueryColumn
{
public:
	// …and WITH a type where the schema knows one — an aggregate over a typed column, a constant
	// projection. Empty stays "unknown"; what is known travels. `available` — whether what it is
	// computed from may be shown (feature options), taken when it is made: it keeps no inputs.
	// `metaData` — the application whose values it holds (GetMetaData), the query's.
	ibSubqueryExprColumn(const ibString& alias, ibMetaID id, const ibMetaData* metaData, const ibTypeDescription& type = ibTypeDescription(),
	                     bool available = true, ibBalanceRole role = ibBalanceRole::None, int rank = 0)
		: m_alias(alias), m_type(type), m_id(id), m_metaData(metaData), m_available(available), m_balanceRole(role), m_periodRank(rank) {}

	ibString GetName() const override { return m_alias; }
	ibString GetPhysicalName() const override { return m_alias; }
	ibTypeDescription& GetTypeDesc() const override { return m_type; }
	ibMetaID GetColumnId() const override { return m_id; }
	bool IsAvailable() const override { return m_available; }
	// …and what the query SAID it is in a balance (`<expr> ROLE OPENING AS x`) — a computed field has no
	// column of its own to say it.
	ibBalanceRole GetBalanceRole() const override { return m_balanceRole; }
	int           GetPeriodRank()  const override { return m_periodRank; }
	const ibMetaData* GetMetaData() const override { return m_metaData; }

private:
	ibString                  m_alias;
	mutable ibTypeDescription m_type;   // empty = unknown; GetTypeDesc returns a non-const ref
	ibMetaID                  m_id;
	const ibMetaData*         m_metaData;
	bool                      m_available;
	ibBalanceRole             m_balanceRole;
	int                       m_periodRank;
};

// «IN HIERARCHY» AS NAMED (ibQueryCondition::m_unfold) never reaches a cell: the side that filters rows in memory
// resolves it first (SubtreeResolved, below). One that still does would be compared as named and every
// subordinate lost, a total that looks right; refused in words instead.
void RefuseNamedHierarchy(const ibQueryCondition& c)
{
	if (c.m_unfold != ibQueryDimUnfold::Elements)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			_("IN HIERARCHY reached a filter over rows held in memory, which cannot walk the subtree"));
}

// ⭐⭐ THE SIDE THAT FILTERS THE ROWS ANSWERS THE WORD. The lowering hands «IN HIERARCHY» down as named on every
// road to the door, and what it costs is decided by whoever filters: the database provider has the server walk
// the subtree (BuildSubtreeIn — WITH RECURSIVE); rows filtered HERE get it read once per condition, through the
// column the condition tests, and the leaf goes on as the plain IN every evaluator below already understands.
// It used to be decided above, in three places of the lowering that each asked «is this source in memory?» —
// a question for the source, asked by everyone who reads it.
//
// The subtree is read through the column's own table: the source that owns the column, or — when the field
// stands at the end of a walk — the table the walk reaches, one provider hop per segment (the lowering's
// OwnerOfPathLeaf takes the same hops).
std::shared_ptr<const ibBackendQueryable> OwnerOfNamedLeaf(const ibQueryCondition& c, const std::vector<std::shared_ptr<const ibBackendQueryable>>& sources)
{
	std::shared_ptr<const ibBackendQueryColumn> head = c.m_path.empty() ? c.m_col : c.m_path.front();
	std::shared_ptr<const ibBackendQueryable> owner;
	for (const std::shared_ptr<const ibBackendQueryable>& source : sources)
		if (source != nullptr && head != nullptr && source->ContainColumn(head)) {
			owner = source;
			break;
		}
	if (owner == nullptr && !sources.empty())
		owner = sources.front();   // the target is the column's either way — a source only lends the metadata to find it
	for (size_t i = 0; owner != nullptr && i + 1 < c.m_path.size(); ++i) {
		const std::shared_ptr<const ibBackendQueryable> next = owner->GetProvider().ResolveReferenceTarget(owner, c.m_path[i]);
		owner = next != nullptr ? next : CastTarget(c.m_path[i + 1]);   // …or the type a CAST named
	}
	return owner;
}

ibQueryCondition SubtreeResolved(ibQueryCondition c, const std::vector<std::shared_ptr<const ibBackendQueryable>>& sources)
{
	if (c.m_unfold == ibQueryDimUnfold::Elements)
		return c;
	std::shared_ptr<const ibBackendQueryable> owner = OwnerOfNamedLeaf(c, sources);
	if (owner == nullptr || c.m_col == nullptr)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			_("IN HIERARCHY names a field whose own table this reading cannot find"));
	c.m_values = ibQueryHierarchyScope(owner, c.m_col, c.m_values, c.m_unfold).Accepted();
	c.m_unfold = ibQueryDimUnfold::Elements;
	return c;
}

bool NamesSubtree(const ibQueryPredicatePtr& p)
{
	if (p == nullptr)
		return false;
	if (p->m_kind == ibQueryPredicateKind::Leaf && p->m_leaf.m_unfold != ibQueryDimUnfold::Elements)
		return true;
	for (const ibQueryPredicatePtr& child : p->m_children)
		if (NamesSubtree(child))
			return true;
	return false;
}

ibQueryPredicatePtr SubtreeResolved(const ibQueryPredicatePtr& p, const std::vector<std::shared_ptr<const ibBackendQueryable>>& sources)
{
	if (!NamesSubtree(p))
		return p;   // shared as it is — nothing in it to resolve
	auto here = std::make_shared<ibQueryPredicate>(*p);
	for (ibQueryPredicatePtr& child : here->m_children)
		child = SubtreeResolved(child, sources);
	if (here->m_kind == ibQueryPredicateKind::Leaf)
		here->m_leaf = SubtreeResolved(here->m_leaf, sources);
	return here;
}

ibDataQuerySpec WithSubtreesResolved(const ibDataQuerySpec& spec, const std::vector<std::shared_ptr<const ibBackendQueryable>>& sources)
{
	ibDataQuerySpec out = spec;
	out.m_predicate = SubtreeResolved(spec.m_predicate, sources);
	if (std::any_of(spec.m_conditions.begin(), spec.m_conditions.end(),
	                [](const ibQueryCondition& c) { return c.m_unfold != ibQueryDimUnfold::Elements; }))
		for (ibQueryCondition& c : out.m_conditions)
			c = SubtreeResolved(c, sources);
	return out;
}

// SQL LIKE on a string cell — case-insensitive, '%' = any run, '_' = one char (no escape handling). The one reading
// of a LIKE over rows in memory, and the database's own (ibQueryRenderer asks it with the case aside too).
bool RamLike(const ibString& s, const ibString& pat)
{
	const ibString S = s.Lower(), P = pat.Lower();
	std::function<bool(size_t, size_t)> m = [&](size_t si, size_t pi) -> bool {
		if (pi == P.size()) return si == S.size();
		if (P[pi] == '%')   return m(si, pi + 1) || (si < S.size() && m(si + 1, pi));
		if (si < S.size() && (P[pi] == '_' || P[pi] == S[si])) return m(si + 1, pi + 1);
		return false;
	};
	return m(0, 0);
}

// SQL three-valued (Kleene) logic: a comparison with a NULL operand is UNKNOWN, and a
// WHERE keeps a row ONLY on a definite TRUE. This is what makes the RAM filter agree with
// the database — e.g. NOT(col = x) DROPS a NULL-col row (it does not keep it), and the same
// settings feed the DB push-down and the RAM fallback alike. See tests/test_queryParity.cpp.
enum class RamTri { False = 0, True = 1, Unknown = 2 };

RamTri RamTriNot(RamTri a)
{
	return a == RamTri::Unknown ? RamTri::Unknown : (a == RamTri::True ? RamTri::False : RamTri::True);
}

// (defined below, beside the absent value it tells apart — SQL NULL only)
bool RamIsNullValue(const ibValue& v);

// ⭐ ONE CELL AGAINST ONE CONDITION, in the database's three values — the comparison every filter over rows in memory
// makes: a WHERE over a row (RamEvalLeaf, which reads both sides off it), the post-filter of a nested aggregate query
// (the condition references POST-aggregation output, HAVING semantics, so it cannot ride the inner WHERE). `value` is
// the right side as the caller holds it: the condition's constant, or a computed right side evaluated over the row.
// 🛑 They used to compare each on its own, in two values: a post-filter's `NotEqual` kept the NULL row the database
// drops.
RamTri MatchRamCondition(const ibValue& cell, const ibQueryCondition& c, const ibValue& value)
{
	// SET-valued `In` — handled BEFORE the scalar null guard below, which would read the unset m_value as
	// NULL and answer UNKNOWN for every row. Semantics match SQL: a NULL probe is UNKNOWN, an empty set is
	// FALSE, otherwise membership. (m_values itself never carries NULL — the producer strips them.)
	if (c.m_op == ibQueryFilterOp::In) {
		RefuseNamedHierarchy(c);
		if (RamIsNullValue(cell)) return RamTri::Unknown;
		for (const ibValue& v : c.m_values)
			if (cell.CompareValueEQ(v)) return RamTri::True;
		return RamTri::False;
	}
	// Any NULL operand -> UNKNOWN (the row survives only on a definite TRUE below).
	if (RamIsNullValue(cell) || RamIsNullValue(value))
		return RamTri::Unknown;
	bool res = false;
	switch (c.m_op) {   // ONE op now (m_comparison + m_explicitOp collapsed into m_op)
		case ibQueryFilterOp::Equal:        res =  cell.CompareValueEQ(value); break;
		case ibQueryFilterOp::NotEqual:     res = !cell.CompareValueEQ(value); break;
		case ibQueryFilterOp::Less:         res = cell.CompareValueLS(value) < 0; break;   // three-way int -> '<'
		case ibQueryFilterOp::LessEqual:    res = cell.CompareValueLE(value); break;
		case ibQueryFilterOp::Greater:      res = cell.CompareValueGT(value) > 0; break;   // three-way int -> '>'
		case ibQueryFilterOp::GreaterEqual: res = cell.CompareValueGE(value); break;
		// …and a LIKE read as every other LIKE over rows in memory is: the case aside.
		case ibQueryFilterOp::Like:         res = RamLike(cell.GetString(), value.GetString()); break;
		case ibQueryFilterOp::In:           break;   // unreachable — returned above; listed to keep the switch exhaustive
	}
	return res ? RamTri::True : RamTri::False;
}

// (defined below — a derived nested query asks it which columns a computed projection reads)
void GatherColumnExprColumns(const ibQueryColumnExprPtr& e, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add);

// (defined below — a WHERE asked of a row held in memory, and the columns it reads: a question put to a value is
// answered over a source's rows before they are copied, ibQueryComposer::OverMemorySpace)
RamTri RamEvalLeaf(const ibQueryCondition& c, const ibQueryRow& row);
RamTri RamEvalPredicate(const ibQueryPredicatePtr& p, const ibQueryRow& row);
bool AsksTheValue(const ibQueryCondition& c);
bool PredicateAsksTheValue(const ibQueryPredicatePtr& p);
void GatherConditionColumns(const ibQueryCondition& c, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add);
void GatherPredicateColumns(const ibQueryPredicatePtr& p, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add);

// A computed input may be shown (feature options) while every column it reads may be. Asked when a nested
// table's column is made: the column keeps no inputs of its own.
bool IsColumnExprAvailable(const ibQueryColumnExprPtr& e)
{
	bool available = true;
	GatherColumnExprColumns(e, [&available](const std::shared_ptr<const ibBackendQueryColumn>& input) {
		available = available && (input == nullptr || input->IsAvailable());
	});
	return available;
}
} // namespace ibProvider

ibSubqueryQueryable::ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount)
	: m_inner(std::make_unique<ibDataQueryBuilder>(inner)), m_top(topCount)
{
	// ⭐ A QUERY THAT GROUPS IS A QUERY THAT FOLDS, whether or not it also aggregates. `GROUP BY Ref`
	// with no SUM beside it is a DISTINCT over the keys — one row per key — and it has to run the same
	// UNPAGED, full-spread GROUP BY. Read as a page instead, the projection drops the `_TYPE` field a
	// reference is reassembled from, and the read dies on a field the SELECT never listed
	// ("Field 'fldNNNN_TYPE' not found in the resultset"). The provider guards that shape already
	// (CanPageGroupLevel); the fold is decided HERE, so the guard is never reached with the wrong read.
	const auto& aggs = m_inner->GetAggregates();
	m_aggregate = !aggs.empty() || !m_inner->GetGroupBy().empty();

	// ⭐⭐ A MINTED NUMBER MUST NOT BE ONE A PASSED-THROUGH COLUMN ALREADY CARRIES. A column published
	// as it came keeps its own id — and when it came from ANOTHER nested query, that id was minted
	// there, from the same zero. `SELECT Q.Month, Q.Balance FROM (SELECT M.Month, SUM(…) AS Balance
	// FROM (SELECT DISTINCT BEGINOFPERIOD(…) AS Month …) AS M … GROUP BY M.Month) AS Q` published M's
	// Month (slot 0 of M) beside its own Balance (slot 0 of Q): one key in the row, and the month read
	// back the balance (a receivables report through the composer, 2026-09-11). So the ids of what is
	// passed through are gathered first, and a minted one steps over them.
	std::set<ibMetaID> passedThrough;
	for (const std::shared_ptr<const ibBackendQueryColumn>& g : m_inner->GetGroupBy())
		if (g != nullptr) passedThrough.insert(g->GetColumnId());
	for (const auto& sc : m_inner->GetSelectColumns())
		if (sc.first != nullptr) passedThrough.insert(sc.first->GetColumnId());
	if (const std::shared_ptr<const ibBackendQueryable> primary = m_inner->GetPrimarySource())
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : primary->GetQueryColumns())
			if (c != nullptr) passedThrough.insert(c->GetColumnId());
	size_t nextSlot = 0;
	auto mintId = [&passedThrough, &nextSlot]() {
		ibMetaID id;
		do { id = ibSynthSlotId(ibSynthKind::Subquery, nextSlot++); } while (passedThrough.contains(id));
		return id;
	};

	if (m_aggregate) {
		// AGGREGATE shape: exposed columns = the GROUP BY keys (real columns, by name) + one
		// owned synthetic numeric column per aggregate alias.
		for (const std::shared_ptr<const ibBackendQueryColumn>& g : m_inner->GetGroupBy())
			if (g != nullptr) m_columns.push_back(g);
		for (const std::shared_ptr<const ibBackendQueryColumn>& g : m_inner->GetGroupBy())
			if (g != nullptr) { m_readFrom.push_back(g); m_readAlias.push_back(ibEmptyString); }
		// ⭐ THE NUMBER IS ALREADY THERE: a minted column's place among the columns this schema OWNS —
		// stepping over the ids passed through (mintId, above). Four lists mint through one counter
		// (aggregates, aliased selections, dot-walks, computed), so none of them needs a band of its own.
		for (const ibDataQueryBuilder::AggregateItem& a : aggs) {
			// …available while what it folds is: every hop of its walk, its column, what its expression reads.
			// COUNT(*) folds rows and reads none.
			const bool available = ibIsWalkAvailable(a.m_path) && (a.m_col == nullptr || a.m_col->IsAvailable())
				&& IsColumnExprAvailable(a.m_expr);
			m_columns.push_back(std::make_shared<ibSubqueryAggColumn>(a.m_alias, mintId(), available));
			m_readFrom.push_back(nullptr);
			// 🛑⭐⭐ THE OUTPUT NAME, NOT THE STATEMENT'S SPELLING — the prefix is added by the READER,
			// and adding it here too produced `out_out_Total`.
			//
			// ibSqlAliasOf is deliberately INJECTIVE and therefore NOT idempotent (`Total` -> `out_Total`,
			// `out_Total` -> `out_out_Total`), and its own note states the contract: writer and reader
			// agree by COMPUTING the spelling, never by passing it between them. Both readers keep that
			// contract — the SQL cursor asks `GetValue(ibSqlAliasOf(alias))`, the RAM table matches the
			// output name as written — so what belongs in this list is the name the column ANSWERS to.
			//
			// ⚠ AND IT FAILED THE WAY THAT NOTE PREDICTED: silently, as an empty value. An aggregate
			// read through a nested source came back EMPTY on every row, so `FROM (SELECT Goods,
			// SUM(Qty) AS Total … GROUP BY Goods) AS t WHERE t.Total > 3` filtered on nothing and
			// returned no rows at all — while the same inner query, run on its own, answered 10 / 20 /
			// 40 (measured 2026-09-04).
			m_readAlias.push_back(a.m_alias);
		}
		m_readPrefix.assign(m_columns.size(), ibEmptyString);
		return;
	}
	const auto& selectCols = m_inner->GetSelectColumns();
	if (!selectCols.empty()) {
		// AN ALIAS IS THE NAME OUT HERE. Where the inner query gave one, that is what the outer query
		// writes — and the value still comes from the real column (m_readFrom, parallel to m_columns).
		for (const auto& sc : selectCols) {
			if (sc.first == nullptr)
				continue;
			if (!sc.second.IsEmpty() && sc.second != sc.first->GetName()) {
				m_columns.push_back(std::make_shared<ibSubqueryAliasColumn>(sc.second, sc.first, mintId(), true,
					sc.first->GetBalanceRole(), sc.first->GetPeriodRank()));
			}
			else {
				m_columns.push_back(sc.first);
			}
			m_readFrom.push_back(sc.first);
			m_readAlias.push_back(ibEmptyString);
		}
	}
	else if (const std::shared_ptr<const ibBackendQueryable> primary = m_inner->GetPrimarySource()) {
		m_columns = primary->GetQueryColumns();
		m_readFrom = m_columns;
		m_readAlias.assign(m_columns.size(), ibEmptyString);
	}

	// ⭐ THE DOT-WALKED SELECTIONS, under their aliases. They never reach the select list — the door
	// records them separately and the result gives them back BY ALIAS — so they need their own pass,
	// and their own way of being read.
	for (const ibDotWalkColumn& walk : m_inner->GetDotWalks()) {
		if (walk.m_alias.IsEmpty() || walk.m_path.empty() || walk.m_path.back() == nullptr)
			continue;
		// Every hop of the walk answers for the leaf — one the options take away takes the leaf with it.
		m_columns.push_back(std::make_shared<ibSubqueryAliasColumn>(walk.m_alias, walk.m_path.back(), mintId(),
			ibIsWalkAvailable(walk.m_path), walk.m_path.back()->GetBalanceRole(), walk.m_path.back()->GetPeriodRank()));
		m_readFrom.push_back(nullptr);
		// ⚠ THE NAME AND THE READ KEY ARE TWO NAMES, and the statement's spelling is derived from this
		// one by whoever writes or reads the statement — see the note on the aggregate list above.
		// What is stored here is the OUTPUT name; ibSqlAliasOf is applied once, at the read.
		m_readAlias.push_back(walk.m_alias);
	}

	// ⭐ AND THE COMPUTED ONES — arithmetic, CASE. A third list, a third way in, and the same rule:
	// what the inner query publishes under a name, the outer query can name.
	//
	// (This is the whole class, walked once instead of a fix per report: a projection reaches the
	// door as a plain column, a dot-walk, an aggregate or an expression, and a nested table has to
	// publish all four. Two of them were missing.)
	for (const ibQueryColumnSelect& computed : m_inner->GetSelectExprs()) {
		if (computed.m_alias.IsEmpty())
			continue;
		m_columns.push_back(std::make_shared<ibSubqueryExprColumn>(computed.m_alias, mintId(), GetMetaData(), ibTypeDescription(),
			IsColumnExprAvailable(computed.m_expr)));
		m_readFrom.push_back(nullptr);
		m_readAlias.push_back(computed.m_alias);   // the OUTPUT name - the read applies ibSqlAliasOf (see above)
	}

	m_readPrefix.assign(m_columns.size(), ibEmptyString);   // derived here: no schema, so no spread to reassemble
}

// ⭐⭐ TOLD ITS OUTPUT, not guessing it. Every published column comes from the inner select's schema:
// the name it is known by out here, and where its value comes from — a real column, or an alias the
// door answers to. That covers all four kinds of projection at once (plain, dot-walk, aggregate,
// computed) because the schema IS what the inner query produces, whichever way each one got there.
ibSubqueryQueryable::ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount,
                                         const std::vector<ibSubqueryOutput>& outputs)
	: m_inner(std::make_unique<ibDataQueryBuilder>(inner)), m_top(topCount)
{
	// Grouping folds, with or without an aggregate beside it — same rule as the derived ctor above,
	// and the reason it matters is written there.
	m_aggregate = !m_inner->GetAggregates().empty() || !m_inner->GetGroupBy().empty();

	// ⭐⭐ A MINTED NUMBER STEPS OVER EVERY ID HANDED IN — the derived ctor above says why: a column
	// borrowed from another nested query carries an id minted there from the same zero, and two ids
	// alike are one cell in the row.
	std::set<ibMetaID> handedIn;
	for (const ibSubqueryOutput& out : outputs)
		if (out.m_col != nullptr) handedIn.insert(out.m_col->GetColumnId());
	size_t nextSlot = 0;

	for (const ibSubqueryOutput& out : outputs) {
		if (out.m_name.IsEmpty())
			continue;

		// The slot this output will occupy — taken BEFORE the borrowed column below is pushed, so a
		// minted id names the output and never the thing it was handed.
		ibMetaID mintedId;
		do { mintedId = ibSynthSlotId(ibSynthKind::Subquery, nextSlot++); } while (handedIn.contains(mintedId));

		// THE SIMPLE CASE: a real column that already answers to this name IS the published column —
		// unless the schema knows it may not be shown while the column says it may (a walk's leaf):
		// then it is published through a thin column that says so.
		if (out.m_alias.IsEmpty() && out.m_objectPrefix.IsEmpty()
		    && out.m_col != nullptr && out.m_col->GetName() == out.m_name
		    && out.m_available == out.m_col->IsAvailable()
		    && out.m_balanceRole == out.m_col->GetBalanceRole()      // …and it is what the column says it is
		    && out.m_periodRank == out.m_col->GetPeriodRank()) {
			m_columns.push_back(out.m_col);
			m_readFrom.push_back(out.m_col);
			m_readAlias.push_back(ibEmptyString);
			m_readPrefix.push_back(ibEmptyString);
			continue;
		}

		// Otherwise a thin column carries the NAME and the TYPE, and the value is fetched the way the
		// schema says. The type is taken from the schema rather than from the column, because those are
		// different questions: a dot-walk leaf is read by alias yet still holds THAT column's type, and
		// a reference with no type is not a reference — the outer query could not walk into it.
		m_columns.push_back(out.m_col != nullptr
			? std::static_pointer_cast<const ibBackendQueryColumn>(
			      std::make_shared<ibSubqueryAliasColumn>(out.m_name, out.m_col, mintedId, out.m_available,
			                                              out.m_balanceRole, out.m_periodRank))
			: std::static_pointer_cast<const ibBackendQueryColumn>(
			      std::make_shared<ibSubqueryExprColumn>(out.m_name, mintedId, GetMetaData(), out.m_type, out.m_available,
			                                             out.m_balanceRole, out.m_periodRank)));
		m_readFrom.push_back(out.m_col);
		// The OUTPUT name again — one spelling stored, the read computes the statement's (see above).
		m_readAlias.push_back(out.m_objectPrefix.IsEmpty() ? out.m_alias : ibString());
		m_readPrefix.push_back(out.m_objectPrefix);
	}
}

ibSubqueryQueryable::~ibSubqueryQueryable() = default;   // unique_ptr<ibDataQueryBuilder> — complete here

// Vends the computed (RAM) provider — the one every computed source uses.
ibBackendQueryProvider& ibSubqueryQueryable::GetProvider() const
{
	return ibComputedProviderInstance();
}

// Run the inner query (with the outer's pushed-down conditions) and materialise its rows
// into a RAM table keyed by column model-id — the same leaf-materialise recipe. m_top > 0
// limits the materialised rows (SELECT TOP n in the branch / subquery).
// See the header: the application this wrapper reads is the inner query's own, and it is what a
// reference walk needs to find its target. Asked of the inner source rather than stored, so a
// wrapper built over another wrapper answers through the chain and never goes stale.
const ibMetaData* ibSubqueryQueryable::GetMetaData() const
{
	if (!m_inner)
		return nullptr;
	std::shared_ptr<const ibBackendQueryable> primary = m_inner->GetPrimarySource();
	return primary != nullptr ? primary->GetMetaData() : nullptr;
}

ibQueryRamTable ibSubqueryQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable t;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : m_columns)
		if (col != nullptr) t.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());

	// ⭐ ONE READ RULE for every shape, the same three-way rule the selection reader uses: a reference /
	// enum / composite leaf is REASSEMBLED from its prefixed field spread, an aliased projection (a
	// dot-walk, an aggregate, a computed expression) is asked for BY NAME, everything else comes through
	// its real column. Which of the three applies was decided when the exposed set was built — here it
	// is only obeyed, so the aggregate path and the plain path cannot drift apart.
	auto readCell = [this](const ibDataQueryResult& sel, size_t i) -> ibValue {
		if (i < m_readPrefix.size() && !m_readPrefix[i].IsEmpty() && m_readFrom[i] != nullptr)
			return sel.GetColumn(m_readPrefix[i], m_readFrom[i]);
		if (i < m_readAlias.size() && !m_readAlias[i].IsEmpty())
			return sel.GetColumn(m_readAlias[i]);
		if (i < m_readFrom.size() && m_readFrom[i] != nullptr)
			return sel.GetValue(m_readFrom[i]);
		return ibValue();
	};

	if (m_aggregate) {
		// AGGREGATE inner — run SelectAggregate; group keys read by column, aggregate aliases by
		// name. The outer's pushed-down conditions reference POST-aggregation output (HAVING
		// semantics), so they apply as a RAM post-filter here, never on the inner WHERE.
		ibDataQueryResult sel = m_inner->SelectAggregate();
		if (!m_computedOverRow.empty())
			sel.SetComputedOverRow(m_computedOverRow);   // an output answered over the row is answered when read
		long emitted = 0;
		std::vector<ibValue> rowVals(m_columns.size());
		while (sel.Next()) {
			for (size_t i = 0; i < m_columns.size(); ++i)
				rowVals[i] = m_columns[i] != nullptr ? readCell(sel, i) : ibValue();

			bool keep = true;
			for (const ibQueryCondition& c : extra) {
				if (c.m_col == nullptr) continue;
				for (size_t i = 0; i < m_columns.size(); ++i)
					if (m_columns[i] == c.m_col) { keep = MatchRamCondition(rowVals[i], c, c.m_value) == RamTri::True; break; }
				if (!keep) break;
			}
			if (!keep) continue;

			const long r = t.AppendRow();
			for (size_t i = 0; i < m_columns.size(); ++i)
				t.SetCell(r, m_columns[i]->GetColumnId(), rowVals[i]);
			if (m_top > 0 && ++emitted >= m_top) break;
		}
		ibJournalInfo(ibT("query.road"), ibT("RAM: nested query (aggregate) computed in memory - %ld rows"),
		              t.RowCount());
		return t;
	}

	// The subquery's exposed columns ARE the inner columns, so the outer's extra
	// conditions apply straight onto a copy of the inner query.
	//
	// ⚠ …WHERE AN EXPOSED COLUMN IS ONE. A projection the inner query COMPUTED — `BEGINOFPERIOD(R.Period,
	// MONTH) AS Month`, a dot-walk reassembled from its spread — is read back by its alias and is no
	// column of the inner source. Handed to the inner WHERE, it was written as a field of the register
	// table that does not exist (`AccumulationRegister1281.Month_D`: a receivables report filtering the
	// months it joins to the turnovers, 2026-09-11). Such a condition is applied after the read, the
	// way the aggregate branch above applies all of them; a real column still filters at the source.
	std::vector<std::pair<size_t, ibQueryCondition>> afterRead;
	ibDataQueryBuilder q(*m_inner);
	for (const ibQueryCondition& c : extra) {
		if (c.m_col == nullptr) continue;
		size_t at = m_columns.size();
		for (size_t i = 0; i < m_columns.size(); ++i)
			if (m_columns[i] == c.m_col) { at = i; break; }
		const bool computed = at < m_columns.size()
			&& ((at < m_readAlias.size() && !m_readAlias[at].IsEmpty())
			    || (at < m_readPrefix.size() && !m_readPrefix[at].IsEmpty()));
		if (computed)
			afterRead.emplace_back(at, c);
		else
			q.Where(c.m_col, c.m_op, c.m_value);   // ONE op — Where carries any ibQueryFilterOp now
	}

	// A limit counts the rows that PASS, so with a filter still to apply it cannot be handed down.
	ibReadPageRequest page; page.m_count = afterRead.empty() ? m_top : 0;   // 0 = all rows; TOP n = the branch limit
	// The four stages this read is made of, measured apart — see the line at the end.
	ibJournalStopwatch execute, fetch, cells, store;
	execute.Resume();
	ibDataQueryResult sel = q.Execute(page);
	execute.Pause();
	// An output answered over the row is answered when it is read — and a result that answers one hands no
	// table over (GetTable), so the rows go through readCell below.
	if (!m_computedOverRow.empty())
		sel.SetComputedOverRow(m_computedOverRow);

	// ⭐ AN INNER QUERY THAT CAME BACK AS A TABLE IS TAKEN WHOLE — its rows re-keyed under the columns
	// this query publishes, not read out cell by cell into a second table while the first one dies. Each
	// read is mapped onto the table first, exactly as readCell would make it: a plain one is its column, a
	// read by alias or by a dot-walk prefix is the table's column of that NAME (a table answers those by
	// name — ibRamTableResultSource::Column). Nothing may be left to filter after the read, since that has
	// to see each row, and a column read for two outputs is a copy, not a move. The payroll sheet's outer
	// nested query copied 126 thousand rows here (2026-09-12).
	const ibQueryRamTable* const inner = afterRead.empty() ? sel.GetTable() : nullptr;
	if (inner != nullptr) {
		const auto byName = [inner](const ibString& name, ibMetaID& id) {
			for (const ibQueryRamColumn& c : inner->Columns())
				if (c.m_name == name) { id = c.m_id; return true; }
			return false;
		};
		std::vector<std::pair<ibMetaID, ibMetaID>> rekey;
		bool plain = true;
		for (size_t i = 0; i < m_columns.size() && plain; ++i) {
			if (m_columns[i] == nullptr)
				continue;   // nothing is published there, and nothing is read
			const std::shared_ptr<const ibBackendQueryColumn> readFrom = i < m_readFrom.size() ? m_readFrom[i] : nullptr;
			ibMetaID from = 0;
			bool found = false;
			if (i < m_readPrefix.size() && !m_readPrefix[i].IsEmpty() && readFrom != nullptr)
				found = byName(m_readPrefix[i], from);          // GetColumn(prefix, col) -> Column(prefix)
			else if (i < m_readAlias.size() && !m_readAlias[i].IsEmpty())
				found = byName(m_readAlias[i], from);           // GetColumn(alias)
			else if (readFrom != nullptr) {
				from  = readFrom->GetColumnId();                // GetValue(column)
				found = true;
			}
			if (!found)
				continue;   // the read answers empty — and a cell nobody moves there reads empty too
			for (const std::pair<ibMetaID, ibMetaID>& had : rekey)
				plain = plain && had.first != from;
			rekey.emplace_back(from, m_columns[i]->GetColumnId());
		}
		if (plain) {
			store.Resume();
			t.AppendRowsRekeyed(std::move(sel).GetTable(), rekey);
			store.Pause();
			ibJournalInfo(ibT("query.road"), ibT("RAM: nested query computed in memory - %ld rows, taken whole; ")
				ibT("execute %lld ms, store %lld ms"), t.RowCount(), execute.Ms(), store.Ms());
			return t;
		}
	}

	std::vector<ibValue> rowVals(m_columns.size());
	long emitted = 0;
	for (;;) {
		fetch.Resume();
		const bool more = sel.Next();
		fetch.Pause();
		if (!more)
			break;
		// ⚠ READ THE WAY THE SCHEMA SAYS, STORE UNDER THE EXPOSED COLUMN. Where the inner query gave an
		// alias the two differ, and asking the result for the alias column would find nothing — the
		// result knows the inner query's own columns, not the names it publishes them under.
		cells.Resume();
		for (size_t i = 0; i < m_columns.size(); ++i)
			rowVals[i] = m_columns[i] != nullptr ? readCell(sel, i) : ibValue();
		cells.Pause();

		bool keep = true;
		for (const auto& condition : afterRead)
			if (!(keep = MatchRamCondition(rowVals[condition.first], condition.second, condition.second.m_value) == RamTri::True))
				break;
		if (!keep)
			continue;

		store.Resume();
		const long r = t.AppendRow();
		for (size_t i = 0; i < m_columns.size(); ++i) {
			if (m_columns[i] == nullptr)
				continue;
			t.SetCell(r, m_columns[i]->GetColumnId(), std::move(rowVals[i]));   // read afresh for the next row
		}
		store.Pause();
		if (!afterRead.empty() && m_top > 0 && ++emitted >= m_top)
			break;
	}
	// ⭐ A NESTED QUERY THAT RAN HERE IS THE CTE ROAD NOT TAKEN. Since the lowering declares a nested
	// source to the server where it can (`WITH q_sub0 AS (…)`, ResolveFrom), reaching this line means
	// something sent it back: no `WITH` in the engine, a shape the declaration cannot carry, or a
	// source of its own that lives in memory. The refusal itself is journalled where it is decided —
	// this says what it cost, and WHERE: the statement (for a nested union, everything under it), the
	// cursor's rows, the values made of their cells, and the table they were kept in.
	ibJournalInfo(ibT("query.road"), ibT("RAM: nested query computed in memory - %ld rows; ")
		ibT("execute %lld ms, fetch %lld ms, cells %lld ms, store %lld ms"),
		t.RowCount(), execute.Ms(), fetch.Ms(), cells.Ms(), store.Ms());
	return t;
}

// ==========================================================================
// ibQueryComposer — realizes the relational tree the door built. A single Source leaf
// delegates to that leaf's provider; a Join / Union is one source, the composite it is,
// read by the same translation — on the base its leaves stand on, or in a base in memory
// where that base cannot write them (docs/private/query-one-road.md §7).
// ==========================================================================
namespace ibProvider {
bool IsSingleSource(const ibDataQuerySpec& spec)
{
	return spec.m_root == nullptr || spec.m_root->m_kind == ibQueryNode::Kind::Source;
}

// ⭐ EVERY WALK THE SPEC MAKES THROUGH ITS SOURCE'S REFERENCES — each a chain longer than one hop: the projection
// (m_dotWalks), ORDER BY (m_sorts), GROUP BY (m_groupPaths), aggregate inputs (m_aggregates[].m_path), a flat WHERE
// (Ref.Field = X) and the leaves of the boolean one (OR / NOT / IS NULL). A source its base cannot join reads its walks
// in a base in memory, beside the copied targets (ReadsInMemorySpace, GatherWalkKeys).
std::vector<const std::vector<std::shared_ptr<const ibBackendQueryColumn>>*> DotWalkPaths(const ibDataQuerySpec& spec)
{
	std::vector<const std::vector<std::shared_ptr<const ibBackendQueryColumn>>*> paths;
	for (const ibDotWalkColumn& dw : spec.m_dotWalks)
		if (dw.m_path.size() > 1) paths.push_back(&dw.m_path);
	// ⭐ A TOTALS DIMENSION IS A DOT-WALK LIKE ANY OTHER (m_dimWalks — TOTALS BY Ref.Ref). It was the
	// one walker this gathering did not know about, so over a COMPUTED source (a subquery wrapper, a
	// register slice) the level's value was never brought in: the DB provider joins and projects it,
	// this path simply had no case for it, and the level folded every row into one empty group.
	for (const ibDotWalkColumn& dw : spec.m_dimWalks)
		if (dw.m_path.size() > 1) paths.push_back(&dw.m_path);
	for (const ibQuerySortItem& s : spec.m_sorts)
		if (s.m_path.size() > 1) paths.push_back(&s.m_path);
	for (const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& gp : spec.m_groupPaths)
		if (gp.size() > 1) paths.push_back(&gp);
	for (const ibAggregateItem& a : spec.m_aggregates)
		if (a.m_path.size() > 1) paths.push_back(&a.m_path);
	for (const ibQueryCondition& c : spec.m_conditions)
		if (c.m_path.size() > 1) paths.push_back(&c.m_path);   // flat WHERE Ref.Field = X — join the leaf, filter in RAM
	// Boolean WHERE predicate (OR / NOT / IS NULL over dot-walk leaves) — walk the tree, collect each leaf's path.
	std::function<void(const ibQueryPredicatePtr&)> gatherPred = [&](const ibQueryPredicatePtr& p) {
		if (p == nullptr) return;
		if (p->m_leaf.m_path.size() > 1) paths.push_back(&p->m_leaf.m_path);   // Leaf condition dot-walk
		if (p->m_path.size() > 1)        paths.push_back(&p->m_path);          // IS NULL dot-walk
		for (const ibQueryPredicatePtr& ch : p->m_children) gatherPred(ch);
	};
	gatherPred(spec.m_predicate);
	return paths;
}

// ⭐ WHO READS A SPEC — the provider of its primary source, asked every question the composer asks (a read, a fold, a
// page of groups), once the spec is one statement can read (ReadAsOneStatement). A spec with no source has no statement
// to write: the RAM engine answers it, and declines what it cannot.
ibBackendQueryProvider& ProviderOf(const ibDataQuerySpec& spec)
{
	return spec.m_queryable != nullptr ? spec.m_queryable->GetProvider() : ibComputedProviderInstance();
}

// ⭐⭐ THE SPEC AS ONE STATEMENT READS IT — the two roads there are (query-one-road.md §7). A tree whose every source its
// base joins is the composite over them (OverComposite); a source computed in memory, a tree with one, or a source whose
// walks its own reader cannot join, is read in a base in memory (OverMemorySpace); a source of the base is itself.
//
// …and so is a spec with a condition that puts a question to a value, whatever its sources: a statement has no SQL for
// it, and it is answered over the source's rows before they are copied — over a table as over a source computed in
// memory, where a table's read refused it.
bool ReadsInMemorySpace(const ibDataQuerySpec& spec)
{
	const bool asksTheValue = PredicateAsksTheValue(spec.m_predicate)
		|| std::any_of(spec.m_conditions.begin(), spec.m_conditions.end(), [](const ibQueryCondition& c) { return AsksTheValue(c); });
	if (!IsSingleSource(spec))
		return asksTheValue || !ibCanJoinOn(spec.m_root, spec.m_holder);
	if (spec.m_queryable == nullptr)
		return false;
	const ibBackendQueryProvider& provider = spec.m_queryable->GetProvider();
	return asksTheValue || provider.IsComputedInRam() || (!provider.CanJoinOn(spec.m_holder) && !DotWalkPaths(spec).empty());
}

ibDataQuerySpec ReadAsOneStatement(const ibDataQuerySpec& spec)
{
	if (ReadsInMemorySpace(spec))
		return ibQueryComposer::OverMemorySpace(spec);
	return ibQueryComposer::OverComposite(spec);   // a single source handed back as it is
}

// ==========================================================================
// ⭐⭐ A SPEC OVER OTHER COLUMNS — every column it names put through `over`: a composite's twins (OverComposite), a
// table's columns where its rows were moved. ONE walk over every field that holds a column, so a move cannot forget
// one — the promotions it replaces each forgot several (the boolean WHERE, the computed outputs, the walks, the levels
// of totals; query-one-road.md §7).
//
// A PATH is the spec's column at its first hop and the targets' columns after it: of a walk only the first segment
// moves, and the leaf (`m_col`) stays on its target. A path of one hop is the column itself.
// ==========================================================================
using ibColumnOver = std::function<std::shared_ptr<const ibBackendQueryColumn>(const std::shared_ptr<const ibBackendQueryColumn>&)>;

static std::shared_ptr<const ibBackendQueryColumn> ColumnOver(const std::shared_ptr<const ibBackendQueryColumn>& col, const ibColumnOver& over)
{
	return col != nullptr ? over(col) : nullptr;
}

static void PathOver(std::shared_ptr<const ibBackendQueryColumn>& col, std::vector<std::shared_ptr<const ibBackendQueryColumn>>& path,
                     const ibColumnOver& over)
{
	if (path.size() > 1) {
		path.front() = ColumnOver(path.front(), over);
		return;
	}
	col = ColumnOver(col, over);
	if (!path.empty())
		path.front() = ColumnOver(path.front(), over);
}

static ibQueryPredicatePtr PredicateOver(const ibQueryPredicatePtr& p, const ibColumnOver& over);

static ibQueryColumnExprPtr ExprOver(const ibQueryColumnExprPtr& e, const ibColumnOver& over)
{
	if (e == nullptr)
		return nullptr;
	ibQueryColumnExprPtr out = std::make_shared<ibQueryColumnExpr>(*e);
	if (e->m_col != nullptr) {
		out->m_col = over(e->m_col);
		// ONE NAMED FIELD of the column moves with it: the same suffix after the name the column now has.
		ibString suffix;
		if (!e->m_field.IsEmpty() && out->m_col != e->m_col && e->m_field.StartsWith(e->m_col->GetPhysicalName(), &suffix))
			out->m_field = out->m_col->GetPhysicalName() + suffix;
	}
	out->m_lhs  = ExprOver(e->m_lhs, over);
	out->m_rhs  = ExprOver(e->m_rhs, over);
	out->m_else = ExprOver(e->m_else, over);
	for (ibQueryColumnExprPtr& arg : out->m_args)
		arg = ExprOver(arg, over);
	for (std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>& wt : out->m_cases) {
		wt.first  = PredicateOver(wt.first, over);
		wt.second = ExprOver(wt.second, over);
	}
	for (ibQueryColumnExprPtr& part : out->m_partition)
		part = ExprOver(part, over);
	for (std::pair<ibQueryColumnExprPtr, bool>& order : out->m_windowOrder)
		order.first = ExprOver(order.first, over);
	return out;
}

static ibQueryCondition ConditionOver(const ibQueryCondition& c, const ibColumnOver& over)
{
	ibQueryCondition out = c;
	PathOver(out.m_col, out.m_path, over);
	out.m_expr      = ExprOver(c.m_expr, over);
	out.m_valueExpr = ExprOver(c.m_valueExpr, over);
	// A policy's semi-join correlates THIS source's column with its inner's: the outer key moves, the inner stays.
	if (c.m_semiJoin != nullptr) {
		std::shared_ptr<ibSemiJoinExists> semiJoin = std::make_shared<ibSemiJoinExists>(*c.m_semiJoin);
		semiJoin->m_outerKey = ColumnOver(semiJoin->m_outerKey, over);
		for (ibSemiJoinExists::ibSemiJoinKeyPart& part : semiJoin->m_alsoKeys)
			part.m_outerKey = ColumnOver(part.m_outerKey, over);
		out.m_semiJoin = semiJoin;
	}
	return out;
}

static ibQueryPredicatePtr PredicateOver(const ibQueryPredicatePtr& p, const ibColumnOver& over)
{
	if (p == nullptr)
		return nullptr;
	ibQueryPredicatePtr out = std::make_shared<ibQueryPredicate>(*p);
	out->m_leaf = ConditionOver(p->m_leaf, over);
	PathOver(out->m_col, out->m_path, over);
	out->m_expr = ExprOver(p->m_expr, over);
	for (ibQueryPredicatePtr& child : out->m_children)
		child = PredicateOver(child, over);
	return out;
}

static void AggregatesOver(std::vector<ibAggregateItem>& aggregates, const ibColumnOver& over)
{
	for (ibAggregateItem& a : aggregates) {
		PathOver(a.m_col, a.m_path, over);
		a.m_expr     = ExprOver(a.m_expr, over);
		a.m_receiver = ColumnOver(a.m_receiver, over);
		for (std::shared_ptr<const ibBackendQueryColumn>& moment : a.m_balanceMoments)
			moment = ColumnOver(moment, over);
		for (std::shared_ptr<const ibBackendQueryColumn>& key : a.m_balanceKeys)
			key = ColumnOver(key, over);
	}
}

static ibDataQuerySpec SpecOver(const ibDataQuerySpec& spec, const ibColumnOver& over)
{
	ibDataQuerySpec out = spec;
	for (ibQueryCondition& c : out.m_conditions)
		c = ConditionOver(c, over);
	out.m_predicate = PredicateOver(spec.m_predicate, over);
	for (ibQuerySortItem& s : out.m_sorts) {
		PathOver(s.m_col, s.m_path, over);
		s.m_expr = ExprOver(s.m_expr, over);
	}
	for (size_t gi = 0; gi < out.m_groupBy.size(); ++gi) {
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> none;
		PathOver(out.m_groupBy[gi], gi < out.m_groupPaths.size() ? out.m_groupPaths[gi] : none, over);
	}
	for (ibQueryColumnExprPtr& e : out.m_groupExprs)
		e = ExprOver(e, over);
	for (ibTotalLevel& level : out.m_totals)
		for (ibTotalField& field : level.m_fields)
			PathOver(field.m_col, field.m_path, over);
	AggregatesOver(out.m_aggregates, over);
	AggregatesOver(out.m_totalAggregates, over);
	for (ibHavingItem& h : out.m_having)
		h.m_col = ColumnOver(h.m_col, over);
	for (std::vector<ibDotWalkColumn>* walks : { &out.m_dotWalks, &out.m_dimWalks })
		for (ibDotWalkColumn& w : *walks) {
			std::shared_ptr<const ibBackendQueryColumn> none;
			PathOver(none, w.m_path, over);
		}
	for (ibQueryColumnSelect& sc : out.m_selectExprs) {
		sc.m_expr = ExprOver(sc.m_expr, over);
		for (ibQueryColumnSelect::Input& in : sc.m_inputs)
			in.m_col = ColumnOver(in.m_col, over);
	}
	for (std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>& sc : out.m_selectCols)
		sc.first = ColumnOver(sc.first, over);
	for (std::shared_ptr<const ibBackendQueryColumn>& c : out.m_distinctBy)
		c = ColumnOver(c, over);
	return out;
}

// Every column a spec names — the same walk, keeping what it meets.
static std::vector<std::shared_ptr<const ibBackendQueryColumn>> ColumnsOf(const ibDataQuerySpec& spec)
{
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> named;
	SpecOver(spec, [&named](const std::shared_ptr<const ibBackendQueryColumn>& col) {
		if (std::find(named.begin(), named.end(), col) == named.end())
			named.push_back(col);
		return col;
	});
	return named;
}

// Every column the tree's joins compare — a key of a composite inside the one that joins it is twinned there too.
static void JoinColumnsOf(const std::shared_ptr<ibQueryNode>& node, std::vector<std::shared_ptr<const ibBackendQueryColumn>>& named)
{
	if (node == nullptr)
		return;
	const ibColumnOver keep = [&named](const std::shared_ptr<const ibBackendQueryColumn>& col) {
		if (std::find(named.begin(), named.end(), col) == named.end())
			named.push_back(col);
		return col;
	};
	if (node->m_kind == ibQueryNode::Kind::Join) {
		ColumnOver(node->m_on.m_colL, keep);
		ColumnOver(node->m_on.m_colR, keep);
		ExprOver(node->m_on.m_exprL, keep);
		ExprOver(node->m_on.m_exprR, keep);
		for (const ibJoinOn::ibJoinKeyPart& part : node->m_on.m_alsoOn) {
			ColumnOver(part.m_colL, keep);
			ColumnOver(part.m_colR, keep);
			ExprOver(part.m_exprL, keep);
			ExprOver(part.m_exprR, keep);
		}
	}
	JoinColumnsOf(node->m_left, named);
	JoinColumnsOf(node->m_right, named);
	for (const std::shared_ptr<ibQueryNode>& part : node->m_parts)
		JoinColumnsOf(part, named);
}

ibDataQuerySpec ComposedOver(const ibDataQuerySpec& spec, std::shared_ptr<ibMemorySpace> space,
                             std::vector<std::unique_ptr<ibTempTableManager>> copies);

} // namespace ibProvider

// ==========================================================================
// ⭐⭐ A COMPOSITE READ AS ONE SOURCE — the spec over the composite its tree is, every column it names that composite's
// twin (composedQueryable.h), so the single source's translation reads a join or a union whole: computed outputs,
// windows, the anchored page, folds, ROLLUP totals, walks. One road where there were four (query-one-road.md §7,
// decision 7).
//
// A UNION BELOW A JOIN, or a join or a union as a union's branch, is a composite of its own first and a source of the
// one above it, so each relation is one level deep: a join of sources, a union of sources. They are numbered within
// the statement — not by a counter that runs on — so the text of one query is the same text every time it is read.
// ==========================================================================
ibDataQuerySpec ibQueryComposer::OverComposite(const ibDataQuerySpec& spec)
{
	if (IsSingleSource(spec))
		return spec;
	return ComposedOver(spec, nullptr, {});
}

namespace ibProvider {

// The composite the spec's tree is, and the spec over it — holding `space` and `copies` when the leaves are copies in a
// space of the base in memory (OverMemorySpace), one leaf included: the result read through it holds them.
ibDataQuerySpec ComposedOver(const ibDataQuerySpec& spec, std::shared_ptr<ibMemorySpace> space,
                             std::vector<std::unique_ptr<ibTempTableManager>> copies)
{
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> named = ColumnsOf(spec);
	JoinColumnsOf(spec.m_root, named);
	// ONE SOURCE STANDS FOR ITSELF — every column it has, as the source did before it was copied: a page asks for what
	// the spec does not name (a list's parent, a hierarchy's own key).
	if (IsSingleSource(spec) && spec.m_queryable != nullptr)
		for (const std::shared_ptr<const ibBackendQueryColumn>& col : spec.m_queryable->GetQueryColumns())
			if (std::find(named.begin(), named.end(), col) == named.end())
				named.push_back(col);

	std::vector<std::shared_ptr<const ibComposedQueryable>> inner;   // in the order they were made, the deepest first
	const ibColumnOver throughInner = [&inner](const std::shared_ptr<const ibBackendQueryColumn>& col) {
		std::shared_ptr<const ibBackendQueryColumn> c = col;
		for (const std::shared_ptr<const ibComposedQueryable>& composite : inner)
			if (std::shared_ptr<const ibBackendQueryColumn> twin = composite->TwinOf(c))
				c = twin;
		return c;
	};
	const auto namedThroughInner = [&]() {
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> out;
		for (const std::shared_ptr<const ibBackendQueryColumn>& col : named)
			out.push_back(throughInner(col));
		return out;
	};
	// The source a key is taken from: the spec's primary, or the composite made above that it went into.
	const auto primaryAmong = [&](const std::vector<std::shared_ptr<const ibBackendQueryable>>& leaves) -> std::shared_ptr<const ibBackendQueryable> {
		for (const std::shared_ptr<const ibBackendQueryable>& leaf : leaves) {
			if (leaf == spec.m_queryable)
				return leaf;
			for (const std::shared_ptr<const ibComposedQueryable>& composite : inner)
				if (composite == leaf &&
				    std::find(composite->Leaves().begin(), composite->Leaves().end(), spec.m_queryable) != composite->Leaves().end())
					return leaf;
		}
		return nullptr;
	};

	int numbered = 0;
	std::function<std::shared_ptr<ibQueryNode>(const std::shared_ptr<ibQueryNode>&)> flatten;
	// A subtree that cannot stand where it is becomes a composite of its own — a source.
	const auto ownComposite = [&](const std::shared_ptr<ibQueryNode>& node) -> std::shared_ptr<ibQueryNode> {
		const std::shared_ptr<ibQueryNode> flat = flatten(node);
		std::vector<std::shared_ptr<const ibBackendQueryable>> leaves;
		std::function<void(const std::shared_ptr<ibQueryNode>&)> collect = [&](const std::shared_ptr<ibQueryNode>& n) {
			if (n == nullptr) return;
			if (n->m_kind == ibQueryNode::Kind::Source) { leaves.push_back(n->m_queryable); return; }
			collect(n->m_left); collect(n->m_right);
			for (const std::shared_ptr<ibQueryNode>& p : n->m_parts) collect(p);
		};
		collect(flat);
		const auto composite = std::make_shared<ibComposedQueryable>(flat, namedThroughInner(),
			std::vector<std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>>(), primaryAmong(leaves),
			ibString::Format(ibT("oes_composed%d"), ++numbered), nullptr, std::vector<std::unique_ptr<ibTempTableManager>>());
		inner.push_back(composite);
		return ibQueryNode::Source(composite);
	};
	flatten = [&](const std::shared_ptr<ibQueryNode>& node) -> std::shared_ptr<ibQueryNode> {
		if (node == nullptr || node->m_kind == ibQueryNode::Kind::Source)
			return node;
		std::shared_ptr<ibQueryNode> out = std::make_shared<ibQueryNode>(*node);
		if (node->m_kind == ibQueryNode::Kind::Union) {
			for (std::shared_ptr<ibQueryNode>& part : out->m_parts)
				part = part != nullptr && part->m_kind != ibQueryNode::Kind::Source ? ownComposite(part) : part;
			return out;
		}
		out->m_left  = node->m_left  != nullptr && node->m_left->m_kind  == ibQueryNode::Kind::Union ? ownComposite(node->m_left)  : flatten(node->m_left);
		out->m_right = node->m_right != nullptr && node->m_right->m_kind == ibQueryNode::Kind::Union ? ownComposite(node->m_right) : flatten(node->m_right);
		// …and what the join compares moves into the composites its sides became.
		out->m_on.m_colL  = ColumnOver(node->m_on.m_colL, throughInner);
		out->m_on.m_colR  = ColumnOver(node->m_on.m_colR, throughInner);
		out->m_on.m_exprL = ExprOver(node->m_on.m_exprL, throughInner);
		out->m_on.m_exprR = ExprOver(node->m_on.m_exprR, throughInner);
		for (ibJoinOn::ibJoinKeyPart& part : out->m_on.m_alsoOn) {
			part.m_colL  = ColumnOver(part.m_colL, throughInner);
			part.m_colR  = ColumnOver(part.m_colR, throughInner);
			part.m_exprL = ExprOver(part.m_exprL, throughInner);
			part.m_exprR = ExprOver(part.m_exprR, throughInner);
		}
		return out;
	};

	const std::shared_ptr<ibQueryNode> root = flatten(spec.m_root != nullptr ? spec.m_root : ibQueryNode::Source(spec.m_queryable));
	std::vector<std::shared_ptr<const ibBackendQueryable>> rootLeaves;
	if (root->m_kind == ibQueryNode::Kind::Union)
		for (const std::shared_ptr<ibQueryNode>& p : root->m_parts) rootLeaves.push_back(p->m_queryable);
	else {
		std::function<void(const std::shared_ptr<ibQueryNode>&)> collect = [&](const std::shared_ptr<ibQueryNode>& n) {
			if (n == nullptr) return;
			if (n->m_kind == ibQueryNode::Kind::Source) { rootLeaves.push_back(n->m_queryable); return; }
			collect(n->m_left); collect(n->m_right);
		};
		collect(root);
	}
	std::vector<std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>> outputs;
	for (const std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>& sc : spec.m_selectCols)
		outputs.emplace_back(throughInner(sc.first), sc.second);
	const auto composite = std::make_shared<ibComposedQueryable>(root, namedThroughInner(), outputs, primaryAmong(rootLeaves),
		ibString(ibT("oes_composed")), std::move(space), std::move(copies));

	ibDataQuerySpec over = SpecOver(spec, [&](const std::shared_ptr<const ibBackendQueryColumn>& col) {
		std::shared_ptr<const ibBackendQueryColumn> twin = composite->TwinOf(throughInner(col));
		return twin != nullptr ? twin : col;
	});
	over.m_root      = ibQueryNode::Source(composite);
	over.m_queryable = composite;
	return over;
}

// Materialisation SEAM — read a leaf through its OWN provider (single-source, its
// conditions pushed down) and collect the needed columns into a RAM table, keyed by
// GetColumnId. (docs/private/query-language-arc.md §22.1a, docs/private/temp-db.md)
ibQueryRamTable MaterialiseLeafToRam(const std::shared_ptr<const ibBackendQueryable>& leaf, ibDatabaseConnectionHolder* holder,
                                     const std::vector<ibQueryCondition>& conds,
                                     const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols,
                                     const std::vector<ibValue>* keys = nullptr)
{
	ibQueryRamTable t;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : cols)
		t.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());
	if (keys != nullptr && keys->empty())
		return t;   // asked for no row — nothing to read

	// ⭐ AN ALIASED LEAF IS READ THROUGH THE TABLE IT ALIASES. `FROM T AS A JOIN T AS B` gives the
	// second reading its own columns so the composer can tell the sides apart — but there is only one
	// table to read, with one set of metadata columns, and the DB provider must meet those. So the
	// READ is the origin's and the KEY the twin's: the rows land under the identity this side of the
	// join carries, and nothing downstream meets a column the metadata cannot resolve. (queryable.h)
	const std::shared_ptr<const ibBackendQueryable> src = ibOriginQueryable(leaf);
	auto readCol = [leaf](const std::shared_ptr<const ibBackendQueryColumn>& c) { return ibOriginColumn(leaf, c); };

	ibDataQueryBuilder q(holder);
	q.From(src);
	for (const ibQueryCondition& c : conds) {
		ibQueryCondition rc = c;
		rc.m_col = readCol(c.m_col);
		q.Where(rc);   // VERBATIM but for the column — rebuilding from (col, op, value) drops m_values / m_path / m_expr
	}
	// ⭐ …AND ONLY THE ROWS ASKED FOR, WHERE THE CALLER KNOWS THEM: a walk reads its target for the references its
	// rows hold — a read by keys that names its columns, which projects those and no others (the batch that says
	// what references are reads the same way, ibValueReferenceDataObject::ReadBatch; the provider carries a long
	// list in as many runs as its driver takes). Read whole, a sort by a reference over a register's turnovers
	// brought every goods row with every attribute, and a recorder every document of every kind it may be.
	if (keys != nullptr) {
		q.WhereKeyIn(*keys);
		for (const std::shared_ptr<const ibBackendQueryColumn>& col : cols)
			q.Select(readCol(col), ibEmptyString);
	}
	ibReadPageRequest page; page.m_count = 0;   // every matching row
	ibJournalStopwatch execute, fetch, cells;
	execute.Resume();
	ibDataQueryResult sel = q.Execute(page);     // reads through the cursor — never names a runtime table
	execute.Pause();
	// ⭐ A LEAF THAT CAME BACK AS A TABLE IS TAKEN WHOLE. A computed leaf — a nested query, a register's
	// virtual table — answers with rows it already holds under these very column ids, and the loop below
	// would copy each of them into a second table cell by cell only to let the first one die: 126 thousand
	// rows of the payroll sheet paid for that once per branch of its union (2026-09-12). Only where the
	// leaf is read as itself — an aliased twin keys its cells by ids of its own, and is copied as before.
	bool readsAsItself = (src == leaf);
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : cols)
		readsAsItself = readsAsItself && readCol(col) == col;
	if (readsAsItself && sel.GetTable() != nullptr) {
		t.AppendRowsFrom(std::move(sel).GetTable());
		ibJournalInfo(ibT("query.road"), ibT("RAM: materialised leaf '%s' - %ld rows, %u columns, taken whole; execute %lld ms"),
		              leaf != nullptr ? leaf->GetQueryName() : ibString(ibT("<none>")),
		              t.RowCount(), static_cast<unsigned>(cols.size()), execute.Ms());
		return t;
	}
	for (;;) {
		fetch.Resume();
		const bool more = sel.Next();
		fetch.Pause();
		if (!more)
			break;
		cells.Resume();
		const long r = t.AppendRow();
		for (const std::shared_ptr<const ibBackendQueryColumn>& col : cols)
			t.SetCell(r, col->GetColumnId(), sel.GetValue(readCol(col)));
		cells.Pause();
	}
	// ⭐⭐ THE LINE THAT SAYS "THESE ROWS CAME HOME". Every road that ends in our memory rather than in
	// the DBMS passes through here, so ONE line answers the question a wrong-looking report actually
	// raises: was this computed by the server, or did we read it all and do the work ourselves?
	//
	// The ROW COUNT is the point of it. "Went to RAM" is a road; 400 000 rows is a diagnosis — the
	// composition's own acceptance criterion is that memory grows with the number of GROUPS, and this
	// is where that is either true or visibly not. Debug only: the whole call compiles away in Release.
	ibJournalInfo(ibT("query.road"), ibT("RAM: materialised leaf '%s' - %ld rows, %u columns; ")
	              ibT("execute %lld ms, fetch %lld ms, cells %lld ms"),
	              leaf != nullptr ? leaf->GetQueryName() : ibString(ibT("<none>")),
	              t.RowCount(), static_cast<unsigned>(cols.size()), execute.Ms(), fetch.Ms(), cells.Ms());
	return t;
}

// ⭐ THE REFERENCES A COLUMN OF THESE ROWS HOLDS, each once — all a walk from it has to read of its target. An empty
// reference points at no row and is left out.
std::vector<ibValue> ReferencesIn(const ibQueryRamTable& rows, const std::shared_ptr<const ibBackendQueryColumn>& refCol)
{
	std::vector<ibValue> keys;
	std::unordered_set<ibValue, ibValueHash, ibValueEqual> seen;
	for (long row = 0; row < rows.RowCount(); ++row) {
		const ibValue v = rows.GetCell(row, refCol->GetColumnId());
		if (v.IsEmpty() || v.IsNull())
			continue;
		if (seen.insert(v).second)
			keys.push_back(v);
	}
	return keys;
}

// …and those of them ONE table can hold: a reference value carries its own type, and a table's own key is typed to
// its kind (the Ref attribute, typed at run time) — so a recorder of fifteen kinds asks each kind for its own, not
// every kind for all of them.
std::vector<ibValue> ReferencesOf(const std::vector<ibValue>& keys, const std::shared_ptr<const ibBackendQueryColumn>& targetKey)
{
	const std::vector<ibClassID>& kinds = targetKey->GetTypeDesc().GetClsidList();
	if (kinds.empty())
		return keys;   // a key that says no type narrows nothing
	std::vector<ibValue> own;
	for (const ibValue& v : keys)
		if (std::find(kinds.begin(), kinds.end(), v.GetClassType()) != kinds.end())
			own.push_back(v);
	return own;
}

// Source leaves of a subtree (depth-first) — for "which side provides a column".
void CollectLeaves(const std::shared_ptr<ibQueryNode>& node, std::vector<std::shared_ptr<ibQueryNode>>& out)
{
	if (node == nullptr) return;
	if (node->m_kind == ibQueryNode::Kind::Source) { out.push_back(node); return; }
	CollectLeaves(node->m_left,  out);
	CollectLeaves(node->m_right, out);
	for (const auto& p : node->m_parts) CollectLeaves(p, out);
}

// This source's SELF-REFERENCE column — the join key a reference TO it binds to (its own
// reference, the _RRRef blob: source.<ref>_RRRef = target.<selfref>_RRRef). It is the source's
// reference key, vended as the front of GetPrimaryKeyColumns (catalog/document: the data-reference
// attribute; empty for registers / temp, which are not reference targets). One key authority — no
// IsReferenceAttribute. (docs §22.1)
std::shared_ptr<const ibBackendQueryColumn> SelfReferenceColumn(const std::shared_ptr<const ibBackendQueryable>& q)
{
	// ⚠ NO SOURCE, NO KEY — and this is asked of things that legitimately have none. The callers
	// hand over whatever `ResolveReferenceTarget` answered, and a COMPOSITE reference has no single
	// target to answer with: it returns null by design and the caller is meant to skip the hop.
	// Taking a pointer and dereferencing it regardless made "there is no such target" into a crash.
	if (q == nullptr)
		return nullptr;

	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys = q->GetPrimaryKeyColumns();
	return keys.empty() ? nullptr : keys.front();
}

// ⭐ THE ROWS EACH WALK'S TARGET IS READ FOR, gathered hop by hop: `rows` of `owner` hold `path[seg]`, a reference into
// the next table; that table is wanted for the references the rows hold (ReferencesIn) of its own kind (ReferencesOf),
// and a walk that goes on is followed through those of its rows, read narrowly — their key and the next hop.
void GatherWalkKeys(const std::shared_ptr<const ibBackendQueryable>& owner, const ibQueryRamTable& rows,
                    const std::shared_ptr<const ibBackendQueryColumn>& refCol,
                    const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& path, size_t seg,
                    ibDatabaseConnectionHolder* from,
                    std::vector<std::pair<std::shared_ptr<const ibBackendQueryable>, std::vector<ibValue>>>& wanted)
{
	if (owner == nullptr || refCol == nullptr || seg + 1 >= path.size())
		return;
	const std::vector<ibValue> held = ReferencesIn(rows, refCol);
	std::vector<std::shared_ptr<const ibBackendQueryable>> targets;
	if (const std::shared_ptr<const ibBackendQueryable> single = owner->GetProvider().ResolveReferenceTarget(owner, refCol))
		targets.push_back(single);
	else
		targets = owner->GetProvider().ResolveReferenceTargets(owner, refCol);
	for (const std::shared_ptr<const ibBackendQueryable>& target : targets) {
		if (!WalkEnters(target, path[seg + 1]))
			continue;   // a CAST names the one type the walk goes into
		const std::shared_ptr<const ibBackendQueryColumn> targetKey = SelfReferenceColumn(target);
		if (targetKey == nullptr)
			continue;   // not a reference target — nothing to walk into
		const std::vector<ibValue> own = ReferencesOf(held, targetKey);
		auto at = std::find_if(wanted.begin(), wanted.end(),
			[&target](const std::pair<std::shared_ptr<const ibBackendQueryable>, std::vector<ibValue>>& w) { return w.first == target; });
		if (at == wanted.end())
			at = wanted.insert(wanted.end(), { target, {} });
		std::unordered_set<ibValue, ibValueHash, ibValueEqual> known(at->second.begin(), at->second.end());
		for (const ibValue& key : own)
			if (known.insert(key).second)
				at->second.push_back(key);
		if (seg + 2 >= path.size())
			continue;   // the next segment is the field the walk reads
		const std::shared_ptr<const ibBackendQueryColumn> next = target->ResolveColumnByName(path[seg + 1]->GetName());
		if (next == nullptr || own.empty())
			continue;
		GatherWalkKeys(target, MaterialiseLeafToRam(target, from, {}, { targetKey, next }, &own), next, path, seg + 1, from, wanted);
	}
}

// Does a condition put a question to a value the statement has no SQL for — PRESENTATION, a CAST, VALUETYPE but against
// a type (ExprIsAnsweredHere, ibTypeTestOf)? It is answered by the value, off the loaded object.
bool AsksTheValue(const ibQueryCondition& c)
{
	ibClassID type = 0;
	return (ExprIsAnsweredHere(c.m_expr) || ExprIsAnsweredHere(c.m_valueExpr)) && ibTypeTestOf(c, type) == nullptr;
}

bool PredicateAsksTheValue(const ibQueryPredicatePtr& p)
{
	if (p == nullptr)
		return false;
	if (p->m_kind == ibQueryPredicateKind::Leaf)
		return AsksTheValue(p->m_leaf);
	if (ExprIsAnsweredHere(p->m_expr))
		return true;
	return std::any_of(p->m_children.begin(), p->m_children.end(), [](const ibQueryPredicatePtr& c) { return PredicateAsksTheValue(c); });
}

// A source's row asked by the columns the spec names. A union's spec names its outputs by its first branch's columns,
// and each branch holds its own column of the name: such a column is read by its name. Any other is read as it is.
class ibBranchRow : public ibQueryRow {
public:
	ibBranchRow(const ibQueryRamTable& table, long row, const std::map<ibMetaID, ibString>& names)
		: m_row(table, row), m_names(names) {}
	ibValue Get(ibMetaID id) const override {
		const auto it = m_names.find(id);
		return it != m_names.end() ? m_row.Get(it->second) : m_row.Get(id);
	}
	ibValue Get(const ibString& name) const override { return m_row.Get(name); }

private:
	ibRamTableRow                       m_row;
	const std::map<ibMetaID, ibString>& m_names;
};

} // namespace ibProvider

// ==========================================================================
// ⭐⭐ A SPEC READ IN A BASE IN MEMORY — what one statement of the spec's own base cannot read (a source computed in
// memory, one another connection reads) is copied into a space taken for this execution (ibMemorySpace) from the
// process's base in memory (ibQueryMemoryManager): each source as the rows its own provider gives, the tables its walks go into
// under their own names, cut to the rows the walks reach.
// The spec is then read there as ONE statement by the same translation as on a server — the composite over the copies,
// which holds them (query-one-road.md §7). One road, where the rows in memory had a second one of their own.
//
// What only the base the sources came from can answer is answered there, on the way out: a named hierarchy
// (WithSubtreesResolved) and a policy's restriction (each source's read carries it — its semi-join reads tables of that
// base). A plain condition on a source is read with it too, unless the source stands on the side of an outer join that
// is filled with NULLs: there the WHERE runs after the join (decision 3), as it still does over the copies.
// ==========================================================================
ibDataQuerySpec ibQueryComposer::OverMemorySpace(const ibDataQuerySpec& given)
{
	ibDatabaseConnectionHolder* const from = given.m_holder;
	const std::shared_ptr<ibQueryNode> root = given.m_root != nullptr ? given.m_root : ibQueryNode::Source(given.m_queryable);

	std::vector<std::shared_ptr<ibQueryNode>> nodes;
	CollectLeaves(root, nodes);
	std::vector<std::shared_ptr<const ibBackendQueryable>> sources;
	for (const std::shared_ptr<ibQueryNode>& node : nodes)
		if (node->m_queryable != nullptr && std::find(sources.begin(), sources.end(), node->m_queryable) == sources.end())
			sources.push_back(node->m_queryable);
	if (sources.empty())
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			_("a reading in memory has no source to copy"));
	ibDataQuerySpec spec = WithSubtreesResolved(given, sources);

	// The sources an outer join fills with NULLs where nothing matches.
	std::vector<std::shared_ptr<const ibBackendQueryable>> nullable;
	std::function<void(const std::shared_ptr<ibQueryNode>&, bool)> markNullable = [&](const std::shared_ptr<ibQueryNode>& node, bool filled) {
		if (node == nullptr)
			return;
		if (node->m_kind == ibQueryNode::Kind::Source) {
			if (filled)
				nullable.push_back(node->m_queryable);
			return;
		}
		if (node->m_kind == ibQueryNode::Kind::Join) {
			const bool full = node->m_joinKind == ibQueryJoinKind::Full;
			markNullable(node->m_left,  filled || full || node->m_joinKind == ibQueryJoinKind::Right);
			markNullable(node->m_right, filled || full || node->m_joinKind == ibQueryJoinKind::Left);
			return;
		}
		for (const std::shared_ptr<ibQueryNode>& part : node->m_parts)
			markNullable(part, filled);
	};
	markNullable(root, false);

	// The sources a union names by another's columns — its branches after the first, under the root or deeper: each
	// answers a name by its own column of it.
	std::vector<std::shared_ptr<const ibBackendQueryable>> byName;
	std::function<void(const std::shared_ptr<ibQueryNode>&, bool)> markByName = [&](const std::shared_ptr<ibQueryNode>& node, bool later) {
		if (node == nullptr)
			return;
		if (node->m_kind == ibQueryNode::Kind::Source) {
			if (later)
				byName.push_back(node->m_queryable);
			return;
		}
		markByName(node->m_left, later);
		markByName(node->m_right, later);
		for (size_t p = 0; p < node->m_parts.size(); ++p)
			markByName(node->m_parts[p], later || (node->m_kind == ibQueryNode::Kind::Union && p > 0));
	};
	markByName(root, false);
	const auto readsByName = [&byName](const std::shared_ptr<const ibBackendQueryable>& q) {
		return std::find(byName.begin(), byName.end(), q) != byName.end();
	};

	const auto lists = [](const std::shared_ptr<const ibBackendQueryable>& q, const std::shared_ptr<const ibBackendQueryColumn>& col) {
		if (q == nullptr || col == nullptr)
			return false;
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : q->GetQueryColumns())
			if (c == col)
				return true;
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : q->GetPrimaryKeyColumns())
			if (c == col)
				return true;
		return false;
	};

	// Each source's own conditions, read with it; a policy's restriction leaves the spec — it is the source's.
	//
	// ⚠ A SOURCE COMPUTED IN MEMORY IS HANDED ITS PLAIN CONDITIONS AS A NARROWING, NOT A DUTY: a register's reading
	// rides what an index can and returns the rest (ibCalcNarrowingOf), so they stay in the spec and the WHERE over the
	// copy enforces them — as the road in memory enforced them after ComputeRows. A walk said as EXISTS stays there too:
	// its target is copied beside it. A semi-join reads tables of the source's base, which no copy has.
	std::vector<std::vector<ibQueryCondition>> own(sources.size());
	std::vector<ibQueryCondition> kept;
	for (const ibQueryCondition& c : spec.m_conditions) {
		const bool restricts = c.m_semiJoin != nullptr || (c.m_asExists && !c.m_path.empty());
		const std::shared_ptr<const ibBackendQueryColumn> head = c.m_semiJoin != nullptr ? c.m_semiJoin->m_outerKey
			: !c.m_path.empty() ? c.m_path.front() : c.m_col;
		const bool plain = c.m_path.empty() && c.m_expr == nullptr && c.m_valueExpr == nullptr && c.m_semiJoin == nullptr;
		bool taken = false, overComputed = false;
		for (size_t s = 0; s < sources.size(); ++s) {
			if (!lists(sources[s], head))
				continue;
			const bool filled = std::find(nullable.begin(), nullable.end(), sources[s]) != nullable.end();
			if (sources[s]->GetProvider().IsComputedInRam()) {
				if (c.m_semiJoin != nullptr)
					ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
						ibString::Format(_("a condition the server was to answer (EXISTS over a nested query) names '%s', a source computed in memory"),
							sources[s]->GetQueryName()));
				if (plain && !filled)
					own[s].push_back(c);
				overComputed = true;
				continue;
			}
			if (restricts || (plain && !filled)) {
				own[s].push_back(c);
				taken = true;
			}
		}
		if (restricts && !taken && !overComputed)
			ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
				_("a restriction of access names a field no source of the reading has"));
		if (!restricts || overComputed)
			kept.push_back(c);
	}
	spec.m_conditions = std::move(kept);

	// ⭐ A SIDE OF A JOIN IS READ ONLY FOR THE KEYS THE SIDE READ BEFORE IT HOLDS (sideways information passing): a row no
	// key of the other side meets cannot reach the result, so it is not read — the keys ride into its read as an IN (a
	// table filters by it, a computed source narrows by it if it can). DIRECTION IS A CORRECTNESS GATE: an inner join
	// reduces either side; a LEFT join only its right one, which it fills with NULLs where nothing matches; a RIGHT or
	// FULL join, a theta, a computed ON or a product, nothing. A NULL key meets nothing and is left out — an empty value
	// (0, "") is a key like any other; past 512 keys the IN costs more than the read it saves, and an empty side says
	// nothing (the join is empty either way).
	struct ibJoinKeys { size_t m_left, m_right; std::shared_ptr<const ibBackendQueryColumn> m_colL, m_colR; bool m_inner; };
	std::vector<ibJoinKeys> joins;
	std::function<void(const std::shared_ptr<ibQueryNode>&)> gatherJoins = [&](const std::shared_ptr<ibQueryNode>& node) {
		if (node == nullptr || node->m_kind == ibQueryNode::Kind::Source)
			return;
		gatherJoins(node->m_left);
		gatherJoins(node->m_right);
		for (const std::shared_ptr<ibQueryNode>& part : node->m_parts)
			gatherJoins(part);
		const ibJoinOn& on = node->m_on;
		if (node->m_kind != ibQueryNode::Kind::Join || on.m_cross || on.m_op != ibJoinCompareOp::Eq || on.m_exprL != nullptr
		    || on.m_exprR != nullptr || (node->m_joinKind != ibQueryJoinKind::Inner && node->m_joinKind != ibQueryJoinKind::Left))
			return;
		const auto ownerOf = [&](const std::shared_ptr<const ibBackendQueryColumn>& col) {
			size_t s = 0;
			while (s < sources.size() && !lists(sources[s], col))
				++s;
			return s;
		};
		const size_t l = ownerOf(on.m_colL), r = ownerOf(on.m_colR);
		if (l < sources.size() && r < sources.size() && l != r)
			joins.push_back({ l, r, on.m_colL, on.m_colR, node->m_joinKind == ibQueryJoinKind::Inner });
	};
	gatherJoins(root);
	const auto keysOf = [](const ibQueryRamTable& held, const std::shared_ptr<const ibBackendQueryColumn>& col) {
		constexpr size_t kMaxKeys = 512;
		std::vector<ibValue> keys;
		std::unordered_set<ibValue, ibValueHash, ibValueEqual> seen;
		for (long row = 0; row < held.RowCount(); ++row) {
			const ibValue v = held.GetCell(row, col->GetColumnId());
			if (v.IsNull() || v.GetType() == ibValueTypes::TYPE_EMPTY || !seen.insert(v).second)
				continue;
			if (seen.size() > kMaxKeys)
				return std::vector<ibValue>();
			keys.push_back(v);
		}
		return keys;
	};

	// Each source's rows: a source computed in memory computes them, a table is read for the fields the spec names. The
	// computed ones first — their rows are made whole either way, and their keys narrow a table's read, where it saves.
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> named = ColumnsOf(spec);
	JoinColumnsOf(root, named);
	std::vector<size_t> order;
	for (const bool computed : { true, false })
		for (size_t s = 0; s < sources.size(); ++s)
			if (sources[s]->GetProvider().IsComputedInRam() == computed)
				order.push_back(s);
	std::vector<ibQueryRamTable> rows(sources.size());
	std::vector<bool> read(sources.size(), false);
	for (const size_t s : order) {
		const std::shared_ptr<const ibBackendQueryable>& source = sources[s];
		for (const ibJoinKeys& join : joins) {
			const bool byLeft  = join.m_right == s && read[join.m_left];
			const bool byRight = join.m_inner && join.m_left == s && read[join.m_right];
			if (!byLeft && !byRight)
				continue;
			ibQueryCondition in;
			in.m_col    = byLeft ? join.m_colR : join.m_colL;
			in.m_op     = ibQueryFilterOp::In;
			in.m_values = byLeft ? keysOf(rows[join.m_left], join.m_colL) : keysOf(rows[join.m_right], join.m_colR);
			if (!in.m_values.empty())
				own[s].push_back(std::move(in));
		}
		read[s] = true;
		if (source->GetProvider().IsComputedInRam()) {
			rows[s] = source->ComputeRows(own[s]);
			// …a plain one on a field its rows do not hold is answered by the computing, if at all: the copy has no such
			// field to filter by, and its missing cell, read as NULL, would drop every row.
			const ibQueryRamTable& computed = rows[s];
			std::erase_if(spec.m_conditions, [&](const ibQueryCondition& c) {
				return c.m_path.empty() && c.m_expr == nullptr && c.m_valueExpr == nullptr && c.m_semiJoin == nullptr
					&& lists(source, c.m_col) && std::none_of(computed.Columns().begin(), computed.Columns().end(),
						[&c](const ibQueryRamColumn& held) { return held.m_id == c.m_col->GetColumnId(); });
			});
			continue;
		}
		// …a union's branch for its own column of each name: the union stacks the branches by name.
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols;
		for (const std::shared_ptr<const ibBackendQueryColumn>& asked : named) {
			std::shared_ptr<const ibBackendQueryColumn> col = lists(source, asked) ? asked : nullptr;
			if (col == nullptr && readsByName(source) && asked != nullptr)
				col = source->ResolveColumnByName(asked->GetName());
			if (col != nullptr && std::find(cols.begin(), cols.end(), col) == cols.end())
				cols.push_back(col);
		}
		for (const std::shared_ptr<const ibBackendQueryColumn>& key : source->GetPrimaryKeyColumns())
			if (key != nullptr && std::find(cols.begin(), cols.end(), key) == cols.end())
				cols.push_back(key);
		rows[s] = MaterialiseLeafToRam(source, from, own[s], cols);
	}

	// ⭐ A QUESTION PUT TO A VALUE — PRESENTATION, VALUETYPE, a CAST — HAS NO SQL, so a condition that asks it of ONE
	// source's fields is answered over that source's rows before they are copied, where the values are; a statement
	// could only refuse it, as a server does. Not of a source an outer join fills with NULLs (it holds after the join),
	// nor of several sources: those reach the statement and are refused there, in words.
	const auto ownerOfAll = [&](const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols) {
		size_t owner = sources.size();
		for (const std::shared_ptr<const ibBackendQueryColumn>& col : cols) {
			size_t s = 0;
			while (s < sources.size() && !lists(sources[s], col))
				++s;
			if (s == sources.size() || (owner != sources.size() && owner != s))
				return sources.size();
			owner = s;
		}
		return owner < sources.size() && std::find(nullable.begin(), nullable.end(), sources[owner]) == nullable.end()
			? owner : sources.size();
	};
	// …and of a union, of every branch: the union's outputs are its first branch's names, which each branch answers by its
	// own column. A union under another one, or with a branch that is no source, keeps the condition for the statement.
	const auto branchesOf = [&](size_t s) -> std::vector<size_t> {
		std::vector<std::shared_ptr<ibQueryNode>> unions;   // the unions above the source
		std::function<bool(const std::shared_ptr<ibQueryNode>&)> seek = [&](const std::shared_ptr<ibQueryNode>& node) {
			if (node == nullptr)
				return false;
			if (node->m_kind == ibQueryNode::Kind::Source)
				return node->m_queryable == sources[s];
			if (node->m_kind == ibQueryNode::Kind::Union)
				unions.push_back(node);
			if (seek(node->m_left) || seek(node->m_right))
				return true;
			for (const std::shared_ptr<ibQueryNode>& part : node->m_parts)
				if (seek(part))
					return true;
			if (node->m_kind == ibQueryNode::Kind::Union)
				unions.pop_back();
			return false;
		};
		seek(root);
		if (unions.empty())
			return { s };
		if (unions.size() > 1)
			return {};
		std::vector<size_t> branches;
		for (const std::shared_ptr<ibQueryNode>& part : unions.front()->m_parts) {
			if (part == nullptr || part->m_kind != ibQueryNode::Kind::Source)
				return {};
			branches.push_back(static_cast<size_t>(std::find(sources.begin(), sources.end(), part->m_queryable) - sources.begin()));
		}
		return !branches.empty() && branches.front() == s ? branches : std::vector<size_t>();
	};
	const auto narrow = [&](const std::vector<size_t>& branches, const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols,
	                        const std::function<bool(const ibQueryRow&)>& keeps) {
		std::map<ibMetaID, ibString> names;
		if (branches.size() > 1)
			for (const std::shared_ptr<const ibBackendQueryColumn>& col : cols)
				names.emplace(col->GetColumnId(), col->GetName());
		for (const size_t b : branches) {
			std::vector<long> order, dropped;
			for (long r = 0; r < rows[b].RowCount(); ++r)
				(keeps(ibBranchRow(rows[b], r, names)) ? order : dropped).push_back(r);
			const long kept = static_cast<long>(order.size());
			order.insert(order.end(), dropped.begin(), dropped.end());
			rows[b].ReorderRows(order, kept);
		}
	};
	std::erase_if(spec.m_conditions, [&](const ibQueryCondition& c) {
		if (c.m_semiJoin != nullptr || !c.m_path.empty() || !AsksTheValue(c))
			return false;
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols;
		GatherConditionColumns(c, [&cols](const std::shared_ptr<const ibBackendQueryColumn>& col) { if (col != nullptr) cols.push_back(col); });
		const size_t s = ownerOfAll(cols);
		const std::vector<size_t> branches = s < sources.size() ? branchesOf(s) : std::vector<size_t>();
		if (branches.empty())
			return false;
		narrow(branches, cols, [&c](const ibQueryRow& row) { return RamEvalLeaf(c, row) == RamTri::True; });
		return true;
	});
	if (PredicateAsksTheValue(spec.m_predicate)) {
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols;
		GatherPredicateColumns(spec.m_predicate, [&cols](const std::shared_ptr<const ibBackendQueryColumn>& col) { if (col != nullptr) cols.push_back(col); });
		const size_t s = ownerOfAll(cols);
		const std::vector<size_t> branches = s < sources.size() ? branchesOf(s) : std::vector<size_t>();
		if (!branches.empty()) {
			const ibQueryPredicatePtr predicate = spec.m_predicate;
			narrow(branches, cols, [&predicate](const ibQueryRow& row) { return RamEvalPredicate(predicate, row) == RamTri::True; });
			spec.m_predicate = nullptr;
		}
	}

	// The tables the walks go into, each for the rows they reach.
	// A union's walk starts from every branch: from the column of the walk's name each branch has.
	std::vector<std::pair<std::shared_ptr<const ibBackendQueryable>, std::vector<ibValue>>> wanted;
	for (const std::vector<std::shared_ptr<const ibBackendQueryColumn>>* path : DotWalkPaths(spec))
		for (size_t s = 0; s < sources.size(); ++s) {
			std::shared_ptr<const ibBackendQueryColumn> head = lists(sources[s], path->front()) ? path->front() : nullptr;
			if (head == nullptr && readsByName(sources[s]))
				head = sources[s]->ResolveColumnByName(path->front()->GetName());
			if (head != nullptr)
				GatherWalkKeys(sources[s], rows[s], head, *path, 0, from, wanted);
		}

	auto space = std::make_shared<ibMemorySpace>();
	ibDatabaseConnectionHolder* const memory = space->Holder();
	std::vector<std::unique_ptr<ibTempTableManager>> copies;
	const auto keep = [&copies](std::unique_ptr<ibTempTableManager> copy, const ibString& what) {
		if (copy == nullptr)
			ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
				ibString::Format(_("'%s' could not be copied into a base in memory"), what));
		copies.push_back(std::move(copy));
		return copies.back()->Queryable();
	};
	for (const std::pair<std::shared_ptr<const ibBackendQueryable>, std::vector<ibValue>>& target : wanted)
		keep(ibTempTableManager::Materialise(memory, target.first, target.second, from), target.first->GetQueryName());
	std::vector<std::shared_ptr<const ibBackendQueryable>> copyOf(sources.size());
	for (size_t s = 0; s < sources.size(); ++s) {
		copyOf[s] = keep(ibTempTableManager::Materialise(memory, rows[s], sources[s]), sources[s]->GetQueryName());
		ibJournalInfo(ibT("query.road"), ibT("MEMORY: source '%s' copied into a base in memory - %ld rows"),
		              sources[s]->GetQueryName(), rows[s].RowCount());
	}

	// The tree over the copies: each source where it stood, its copy in its place.
	std::function<std::shared_ptr<ibQueryNode>(const std::shared_ptr<ibQueryNode>&)> overCopies =
		[&](const std::shared_ptr<ibQueryNode>& node) -> std::shared_ptr<ibQueryNode> {
		if (node == nullptr)
			return nullptr;
		std::shared_ptr<ibQueryNode> out = std::make_shared<ibQueryNode>(*node);
		if (node->m_kind == ibQueryNode::Kind::Source) {
			const size_t s = static_cast<size_t>(std::find(sources.begin(), sources.end(), node->m_queryable) - sources.begin());
			out->m_queryable = s < copyOf.size() ? copyOf[s] : node->m_queryable;
			return out;
		}
		out->m_left  = overCopies(node->m_left);
		out->m_right = overCopies(node->m_right);
		for (std::shared_ptr<ibQueryNode>& part : out->m_parts)
			part = overCopies(part);
		return out;
	};
	spec.m_root = overCopies(root);
	const size_t primary = static_cast<size_t>(std::find(sources.begin(), sources.end(), given.m_queryable) - sources.begin());
	spec.m_queryable = primary < copyOf.size() ? copyOf[primary] : copyOf.front();
	spec.m_holder    = memory;
	return ComposedOver(spec, std::move(space), std::move(copies));
}

namespace ibProvider {

// SQL NULL only (TYPE_NULL — what the DB driver yields for a NULL column). NOT TYPE_EMPTY
// (Undefined = composite with no type chosen): that is a runtime placeholder, never a DB null.
bool RamIsNullValue(const ibValue& v)
{
	return v.IsNull();
}

// ⭐⭐ THE ABSENT VALUE, SPELLED ONCE. TYPE_NULL — not a default ibValue, which is TYPE_EMPTY ("no
// type chosen") and answers FALSE to `IS NULL`. Both the unmatched side of an outer join and a strict
// scalar's refusal below produce the same thing, because downstream they mean the same thing.
ibValue RamNullValue()
{
	ibValue v; v.SetType(ibValueTypes::TYPE_NULL); return v;
}

// --- the boolean WHERE TREE asked of one row held in memory ----------------------------------------
// A CASE a fold computes, a nested query's post-filter: the tree evaluated over the row it is asked of.
// (Three-valued, as MatchRamCondition answers — RamTri is stated beside it.)

RamTri RamEvalLeaf(const ibQueryCondition& c, const ibQueryRow& row)
{
	// A SEMI-JOIN IS THE SERVER'S TO ANSWER — an `IN (SELECT …)` said as EXISTS, or a policy's restriction. Rows in
	// memory have no inner table to look in, and the empty column below would answer FALSE for every row (TRUE under
	// a NOT): a filter that looks applied. Refused in words instead.
	if (c.m_semiJoin)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			_("a condition the server was to answer (EXISTS over a nested query) reached a filter over rows held in memory"));

	// A COMPUTED lhs (WHERE Qty * Price > 100, a CASE) evaluates its expression per row; else read the column.
	ibValue cell;
	if (c.m_expr)                cell = EvalColumnExprRow(c.m_expr, row);
	else if (c.m_col != nullptr) cell = row.Get(c.m_col);
	else                         return RamTri::False;
	// …and so does a computed right side (WHERE ActionPeriod < RegistrationPeriod); else the constant.
	const ibValue value = c.m_valueExpr ? EvalColumnExprRow(c.m_valueExpr, row) : c.m_value;
	return MatchRamCondition(cell, c, value);
}

RamTri RamEvalPredicate(const ibQueryPredicatePtr& p, const ibQueryRow& row)
{
	if (p == nullptr) return RamTri::True;
	switch (p->m_kind) {
		case ibQueryPredicateKind::Leaf:
			return RamEvalLeaf(p->m_leaf, row);
		case ibQueryPredicateKind::And: {
			RamTri acc = RamTri::True;                          // FALSE dominates; else UNKNOWN if any
			for (const auto& c : p->m_children) {
				const RamTri r = RamEvalPredicate(c, row);
				if (r == RamTri::False)   return RamTri::False;
				if (r == RamTri::Unknown) acc = RamTri::Unknown;
			}
			return acc;
		}
		case ibQueryPredicateKind::Or: {
			RamTri acc = RamTri::False;                         // TRUE dominates; else UNKNOWN if any
			for (const auto& c : p->m_children) {
				const RamTri r = RamEvalPredicate(c, row);
				if (r == RamTri::True)    return RamTri::True;
				if (r == RamTri::Unknown) acc = RamTri::Unknown;
			}
			return acc;
		}
		case ibQueryPredicateKind::Not:
			return p->m_children.empty() ? RamTri::True
			                             : RamTriNot(RamEvalPredicate(p->m_children.front(), row));
		case ibQueryPredicateKind::IsNull: {
			// IS NULL / IS NOT NULL are DEFINITE (never UNKNOWN).
			// …asked about an EXPRESSION where there is no column to point at — a fold read back by
			// name (`ISNULL(MIN(x), y)`). Same test, one operand deeper.
			if (p->m_expr)
				return ((p->m_negated) != RamIsNullValue(EvalColumnExprRow(p->m_expr, row)))
					? RamTri::True : RamTri::False;
			if (p->m_col == nullptr) return RamTri::False;
			const bool isNull = RamIsNullValue(row.Get(p->m_col));
			return (p->m_negated ? !isNull : isNull) ? RamTri::True : RamTri::False;
		}
		// ⭐ `x REFS Document.Order`, ASKED OF THE VALUE. The SQL road compares the stored `_RTRef`
		// field; a RAM table has no such field — it holds the reassembled VALUE, and a value knows its
		// own class. Same question, answered from what this road actually has in hand.
		//
		// DEFINITE like IS NULL: an empty cell simply is not of that type, which is FALSE (or TRUE
		// under NOT) rather than UNKNOWN — "it does not point there" is a complete answer.
		// A barrier (`CatalogRef`, `AnyRef`) is answered off the two ids, as the SQL road answers it with a range.
		case ibQueryPredicateKind::RefType: {
			if (p->m_col == nullptr) return RamTri::False;
			const ibValue cell = row.Get(p->m_col);
			const bool same = !RamIsNullValue(cell) && clsid_admits(p->m_refTypeClsid, cell.GetClassType());
			return (p->m_negated ? !same : same) ? RamTri::True : RamTri::False;
		}
	}
	return RamTri::True;
}

// Every column ONE CONDITION reads: its field, and the sides it computes — two fields of one row (`S.Qty <> L.Qty`),
// an expression on either side. Asked for the field alone, a comparison of two columns rode into a composed join
// with neither of them, and was answered over two empty cells (2026-11-14, lots against stock).
void GatherConditionColumns(const ibQueryCondition& c, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add)
{
	add(c.m_col);
	GatherColumnExprColumns(c.m_expr, add);
	GatherColumnExprColumns(c.m_valueExpr, add);
}

void GatherPredicateColumns(const ibQueryPredicatePtr& p, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add)
{
	if (p == nullptr) return;
	switch (p->m_kind) {
		case ibQueryPredicateKind::Leaf:   GatherConditionColumns(p->m_leaf, add); break;
		case ibQueryPredicateKind::IsNull: add(p->m_col); GatherColumnExprColumns(p->m_expr, add); break;
		case ibQueryPredicateKind::RefType: add(p->m_col);       break;
		case ibQueryPredicateKind::And:
		case ibQueryPredicateKind::Or:     for (const auto& c : p->m_children) GatherPredicateColumns(c, add); break;
		case ibQueryPredicateKind::Not:    if (!p->m_children.empty()) GatherPredicateColumns(p->m_children.front(), add); break;
	}
}

// --- a computed value (arithmetic / CASE) asked of one row held in memory ----------------------------
// A statement computes it where it reads; what is evaluated here is what comes after the statement — a
// fold's computed figure, a result's computed output (EvalOverResultRow) — off the column-based AST
// (Column / Const / Arith / Case — the CASE reuses the boolean predicate evaluator).

// ⭐⭐ A SCALAR IS STRICT IN ITS OPERANDS — one NULL in, NULL out.
//
// That is SQL's rule and, more to the point, it is what the SERVER does with the same expression: a
// query must not answer differently for having taken the RAM road. Every calendar / string / arithmetic
// node below is strict; CASE is NOT (its arms decide, which is how ISNULL guards anything at all), and
// a leaf has no operands.
//
// 🛑 Without it the operand is READ THROUGH ITS DEFAULT and the answer is a number: an unmatched LEFT
// JOIN row gave `DATEDIFF(A.Period, B.Period, Day)` = **-739616** — the distance from the date zero a
// NULL reads as — and `MAX(x) * 2` over an empty fold gave 0. Both look like figures and neither is
// one (measured 2026-09-05, the first probe that put a scalar over the null-producing side of a join).
bool RamAnyNull(std::initializer_list<const ibValue*> vs)
{
	for (const ibValue* v : vs)
		if (RamIsNullValue(*v)) return true;
	return false;
}

ibValue EvalColumnExprRow(const ibQueryColumnExprPtr& e, const ibQueryRow& row)
{
	if (e == nullptr) return ibValue();
	switch (e->m_kind) {
		case ibQueryColumnExprKind::Column:
			// ⚠ A NAMED FIELD HAS NO MEANING IN RAM, and saying so is the point. A materialised row holds
			// the value WHOLE (one cell per column), never the field spread the DB path writes — so a
			// per-field expression asked here would have to answer with the whole value under a field's
			// name, and the caller would reassemble an object out of several copies of itself. The
			// expression is built per field precisely to be pushed to the server; if one reaches this
			// evaluator, the read took the join / multi-source road and the reading has to be split
			// differently, which is a defect to be told about rather than a case to fake.
			ibASSERT_MSG(e->m_field.IsEmpty(), ibT("a per-field column expression cannot be evaluated over a RAM row"));
			return e->m_col != nullptr && e->m_field.IsEmpty() ? row.Get(e->m_col) : ibValue();
		case ibQueryColumnExprKind::Const:  return e->m_const;
		case ibQueryColumnExprKind::Arith: {
			const ibValue va = EvalColumnExprRow(e->m_lhs, row);
			const ibValue vb = EvalColumnExprRow(e->m_rhs, row);
			if (RamAnyNull({ &va, &vb })) return RamNullValue();   // strict — see above
			if (e->m_arith == ibQueryColumnArithOp::Concat)   // before the numbers: a string is not read as one
				return ibValue(va.GetString() + vb.GetString());
			const ibNumber a = va.GetNumber();
			const ibNumber b = vb.GetNumber();
			switch (e->m_arith) {
				case ibQueryColumnArithOp::Add: return ibValue(a + b);
				case ibQueryColumnArithOp::Concat: break;   // answered above
				case ibQueryColumnArithOp::Sub: return ibValue(a - b);
				case ibQueryColumnArithOp::Mul: return ibValue(a * b);
				// ⭐ A DIVISION BY ZERO IS REFUSED — as the database refuses it (PostgreSQL, Firebird, MS SQL raise) and in the
				// language's own sentence. It answered 0 here: a figure the same query read by the server never gives.
				case ibQueryColumnArithOp::Div:
				case ibQueryColumnArithOp::Mod:
					if (b.IsZero())
						ibBackendCoreException::Error(ibScriptError_DivideByZero);
					return ibValue(e->m_arith == ibQueryColumnArithOp::Div ? a / b : a % b);
			}
			return ibValue();
		}
		case ibQueryColumnExprKind::Case: {
			for (const auto& wt : e->m_cases)
				if (RamEvalPredicate(wt.first, row) == RamTri::True) return EvalColumnExprRow(wt.second, row);
			return e->m_else ? EvalColumnExprRow(e->m_else, row) : ibValue();
		}
		case ibQueryColumnExprKind::PeriodTrunc: {
			// The RAM twin of the SQL truncation. It must agree with the dialect's expression to the
			// day, or a query answers differently depending on whether it happened to co-locate — so
			// this walks the calendar rather than approximating (no "30-day month" arithmetic).
			const ibValue v = EvalColumnExprRow(e->m_lhs, row);
			if (RamAnyNull({ &v })) return RamNullValue();
			return ibValue(v.GetDate().BeginOfPeriod(e->m_periodUnit));
		}
		// ⭐ AND THE REST OF THE CALENDAR, under the same obligation: each of these is the twin of a
		// dialect template, and the two must answer identically or one query gives two numbers
		// depending on the road it took. The arithmetic itself lives in ONE place (ibDateTime, fdatetime.h),
		// which is what makes that promise keepable rather than merely intended.
		case ibQueryColumnExprKind::PeriodEnd: {
			const ibValue v = EvalColumnExprRow(e->m_lhs, row);
			if (RamAnyNull({ &v })) return RamNullValue();
			return ibValue(v.GetDate().EndOfPeriod(e->m_periodUnit));
		}
		case ibQueryColumnExprKind::DateAdd: {
			const ibValue v = EvalColumnExprRow(e->m_lhs, row);
			const ibValue n = e->m_args.empty() ? ibValue(0) : EvalColumnExprRow(e->m_args.front(), row);
			if (RamAnyNull({ &v, &n })) return RamNullValue();
			return ibValue(v.GetDate().AddPeriods(e->m_periodUnit, static_cast<long>(n.GetNumber().ToInt())));
		}
		case ibQueryColumnExprKind::DateDiff: {
			const ibValue a = EvalColumnExprRow(e->m_lhs, row);
			const ibValue b = EvalColumnExprRow(e->m_rhs, row);
			if (RamAnyNull({ &a, &b })) return RamNullValue();
			return ibValue(ibNumber(a.GetDate().PeriodsUntil(b.GetDate(), e->m_periodUnit)));
		}
		case ibQueryColumnExprKind::DatePart: {
			const ibValue v = EvalColumnExprRow(e->m_lhs, row);
			if (RamAnyNull({ &v })) return RamNullValue();
			return ibValue(ibNumber(v.GetDate().GetPart(e->m_datePart)));
		}
		case ibQueryColumnExprKind::Substring: {
			// 1-BASED, like the SQL it mirrors and like the language a person writes — the C++ one
			// underneath counts from zero, and that difference belongs here rather than in anybody's
			// query.
			const ibValue s = EvalColumnExprRow(e->m_lhs, row);
			if (RamAnyNull({ &s })) return RamNullValue();
			const long from = e->m_args.size() > 0 ? static_cast<long>(EvalColumnExprRow(e->m_args[0], row).GetNumber().ToInt()) : 1;
			const long len  = e->m_args.size() > 1 ? static_cast<long>(EvalColumnExprRow(e->m_args[1], row).GetNumber().ToInt()) : 0;
			const ibString text = s.GetString();
			const long start = (from > 0 ? from : 1) - 1;
			if (start >= static_cast<long>(text.length()) || len <= 0)
				return ibValue(ibEmptyString);
			return ibValue(text.Mid(static_cast<size_t>(start), static_cast<size_t>(len)));
		}
		// ⭐ A COLUMN OF THE FINISHED RESULT — asked of the ROW, by the name it was published under.
		// A row that carries such a column answers; one that does not answers empty, which is what a
		// figure that has not been folded yet is. No second evaluator, no substitution pass.
		case ibQueryColumnExprKind::OutputRef:
			return row.Get(e->m_outputName);

		// ⭐⭐ THE QUESTION PUT TO THE VALUE. Asked of the value itself, exactly as the runtime asks it
		// when it shows one — so a query and a form say the same thing about the same reference,
		// because the answer comes from one place (the metatype, through the value).
		case ibQueryColumnExprKind::ValueAsk: {
			const ibValue v = EvalColumnExprRow(e->m_lhs, row);
			switch (e->m_valueAsk) {
			case ibQueryValueAsk::ValueType:
				// The TYPE the value carries. An absent value has none — NULL, not "the empty type".
				// Made through the door in typeDescription.h: this tier does not name the class that
				// carries a type, and reaching into the runtime's value zoo to build one is the leak
				// that door exists to close.
				return (RamIsNullValue(v) || v.IsEmpty()) ? RamNullValue() : ibTypeValueOf(v.GetClassType());
			case ibQueryValueAsk::AsType:
				// The value AS the type a CAST named — through the same door, for the same reason. NULL stays NULL:
				// a missing value converted is still missing, not a zero nobody wrote.
				return RamIsNullValue(v) ? RamNullValue() : ibValueAsType(v, e->m_asType);
			case ibQueryValueAsk::Presentation:
			case ibQueryValueAsk::RefPresentation:
			default:
				// WHAT IT SHOWS. The value answers — a reference through its metatype, a primitive with
				// its own text — and NULL keeps being NULL rather than becoming the word for it.
				return RamIsNullValue(v) ? RamNullValue() : ibValue(v.GetString());
			}
		}

		case ibQueryColumnExprKind::WindowAgg:
			// A WINDOW HAS NO VALUE ON ONE ROW — its value is a fold over the row's PARTITION, and the statement folds it
			// (OVER), on a server and in a base in memory alike. One that reaches a row is a defect to be told about.
			ibFAIL_MSG(ibT("a window aggregate reached a row evaluator - the statement folds it"));
			return ibValue();
	}
	return ibValue();
}

// ⭐ THE ROW AS THE EVALUATOR SEES IT — a reading of the RESULT that came back.
//
// The evaluator asks a row for a column BY ID, and a result answers by COLUMN. So the bridge carries
// the columns the expression reads (gathered once) and matches by id.
class ibResultRow : public ibQueryRow {
public:
	ibResultRow(const ibDataResultSource& src, const std::vector<ibQueryColumnSelect::Input>& inputs)
		: m_src(src), m_inputs(inputs) {}
	// A column the expression names, read the way the STATEMENT put it there: a primitive under its
	// prefix as one field, a reference / enum reassembled from the spread under that prefix. An input
	// with no prefix was never projected under one and is read as the source's own column.
	ibValue Get(ibMetaID id) const override {
		for (const ibQueryColumnSelect::Input& in : m_inputs)
			if (in.m_col != nullptr && in.m_col->GetColumnId() == id) {
				if (in.m_prefix.IsEmpty()) return m_src.Value(in.m_col);
				return in.m_object ? m_src.Column(in.m_prefix, in.m_col) : m_src.Column(in.m_prefix);
			}
		return ibValue();
	}
	// …and by the published name — which is how a result hands out an aggregate's figure.
	ibValue Get(const ibString& name) const override { return m_src.Column(name); }
private:
	const ibDataResultSource&                       m_src;
	const std::vector<ibQueryColumnSelect::Input>&  m_inputs;
};

// (defined below — the evaluator over a RESULT needs the same column gathering the compose does)
void GatherColumnExprColumns(const ibQueryColumnExprPtr& e, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add);

} // namespace ibProvider

// ⭐ THE SAME EXPRESSION, OVER A RESULT — a CALL SITE, not a second evaluator. (Asked by the selection, queryResult.cpp.)
//
// All it does is dress the result as a row and hand it to the one evaluator: the tree is walked once,
// by the same code, with the same semantics. An `OutputRef` inside it is answered by the row itself
// (ibResultRow::Get(name)), so nothing is substituted, copied or pre-folded on the way in.
ibValue EvalOverResultRow(const ibQueryColumnSelect& c, const ibDataResultSource& src)
{
	return c.m_expr ? EvalColumnExprRow(c.m_expr, ibResultRow(src, c.m_inputs)) : ibValue();
}

namespace ibProvider {

// Every source column an expression reads.
void GatherColumnExprColumns(const ibQueryColumnExprPtr& e, const std::function<void(const std::shared_ptr<const ibBackendQueryColumn>&)>& add)
{
	if (e == nullptr) return;
	switch (e->m_kind) {
		case ibQueryColumnExprKind::Column: add(e->m_col); break;
		case ibQueryColumnExprKind::Const:  break;
		case ibQueryColumnExprKind::Arith:  GatherColumnExprColumns(e->m_lhs, add); GatherColumnExprColumns(e->m_rhs, add); break;
		case ibQueryColumnExprKind::Case:
			for (const auto& wt : e->m_cases) { GatherPredicateColumns(wt.first, add); GatherColumnExprColumns(wt.second, add); }
			GatherColumnExprColumns(e->m_else, add);
			break;
		case ibQueryColumnExprKind::PeriodTrunc: GatherColumnExprColumns(e->m_lhs, add); break;
		// The calendar calls read their operands like any other expression, so every column they
		// touch has to ride into the read — a DATEDIFF whose second date nobody projected would come
		// back as a difference from nothing, which is a plausible number and the wrong one.
		case ibQueryColumnExprKind::PeriodEnd:
		case ibQueryColumnExprKind::DatePart:
			GatherColumnExprColumns(e->m_lhs, add);
			break;
		case ibQueryColumnExprKind::DateDiff:
			GatherColumnExprColumns(e->m_lhs, add);
			GatherColumnExprColumns(e->m_rhs, add);
			break;
		case ibQueryColumnExprKind::DateAdd:
		case ibQueryColumnExprKind::Substring:
			GatherColumnExprColumns(e->m_lhs, add);
			for (const ibQueryColumnExprPtr& a : e->m_args)
				GatherColumnExprColumns(a, add);
			break;
		// ⭐ THE PARTITION COUNTS TOO. A window names its input AND the levels it is computed over, and
		// every one of those columns has to reach the read — a partition key nobody projected is a
		// query that cannot be built. (Missing here, the figure would have come back partitioned by
		// nothing, which is a plausible number and the wrong one.)
		case ibQueryColumnExprKind::WindowAgg:
			GatherColumnExprColumns(e->m_lhs, add);
			for (const ibQueryColumnExprPtr& key : e->m_partition)
				GatherColumnExprColumns(key, add);
			break;
		// A published result column is not a source column — the row answers it by name.
		case ibQueryColumnExprKind::OutputRef:
			break;
		// The question reads the value it is asked of. A nested query's PRESENTATION(Field) is offered
		// out here only while Field is: a feature option that hides Field hides that column too.
		case ibQueryColumnExprKind::ValueAsk:
			GatherColumnExprColumns(e->m_lhs, add);
			break;
	}
}

// A LEVEL'S KEY — the values of its fields, in the level's own order. One field is the ordinary
// case and stays a one-element key rather than a shape of its own, so nothing downstream branches
// on how many fields a level has. (A balance keys its readings by one too — see ibAggAcc.)
using ibLevelKey = std::vector<ibValue>;

struct ibLevelKeyHash
{
	std::size_t operator()(const ibLevelKey& key) const
	{
		// Mixed in 64 bits and folded down at the end: on a 32-bit build std::size_t is half the
		// width, and mixing IN it silently throws away the upper half of every value's hash.
		const ibValueHash one;
		unsigned long long h = 0xcbf29ce484222325ull;
		for (const ibValue& value : key) {
			h ^= static_cast<unsigned long long>(one(value));
			h *= 0x100000001b3ull;
		}
		return static_cast<std::size_t>(h ^ (h >> 32));
	}
};

struct ibLevelKeyEqual
{
	bool operator()(const ibLevelKey& left, const ibLevelKey& right) const
	{
		if (left.size() != right.size()) return false;
		const ibValueEqual same;
		for (std::size_t i = 0; i < left.size(); ++i)
			if (!same(left[i], right[i])) return false;
		return true;
	}
};

// ⭐ WHERE A BALANCE READING STANDS — the key it is a reading of and the moment it was taken at (every period
// field, the senior first). Read off the row once, beside the operand, and only for an aggregate that is a
// balance (m_balance).
struct ibBalanceMark
{
	ibLevelKey m_key;
	ibLevelKey m_moment;
};

// EARLIER IN TIME — the period fields compared one after another, the senior first: a later field decides
// only where every one before it is the same.
inline bool ibMomentBefore(const ibLevelKey& left, const ibLevelKey& right)
{
	const ibValueEqual same;
	for (std::size_t i = 0; i < left.size() && i < right.size(); ++i)
		if (!same(left[i], right[i]))
			return left[i] < right[i];
	return false;
}

// ⭐⭐ AN AGGREGATE IS AN ACCUMULATOR — it always was, and holding the rows was how it hid.
//
// SUM, COUNT, AVG, MIN, MAX all fold one row at a time and forget it; the only reason this used to
// take a bucket (a table + a list of row indices) is that the rows happened to be lying there. Said
// as a state that rows are FED into, the same arithmetic serves BOTH folds: the one that already has
// its rows (a composed JOIN result — AggregateOne below feeds them in a loop) and the streaming one,
// which never holds a row longer than the moment it reads it.
//
// One statement of the semantics, and it is the awkward half that makes this matter: every column
// aggregate IGNORES NULL operands (SQL), COUNT(col) counts non-null rows, AVG divides by the
// non-null count, MIN/MAX skip NULL, and DISTINCT folds each different VALUE once — keyed by the
// value's identity, never by its presentation (two references display the same string far more
// often than they are the same reference, and a display-keyed count would quietly merge them).
// Two copies of that is two answers waiting to disagree.
struct ibAggAcc
{
	ibNumber m_sum{ 0 };
	long     m_n     = 0;      // operands folded (non-null, DISTINCT-filtered) — COUNT / the AVG divisor
	long     m_rows  = 0;      // rows fed, null or not — COUNT(*), which counts rows and not values
	ibValue  m_best;
	bool     m_have  = false;
	// DISTINCT only — the one place memory grows with the DATA rather than with the groups, and it
	// grows with the number of DIFFERENT VALUES in a group, which is what DISTINCT means.
	//
	// ⚠ MADE ON THE FIRST DISTINCT VALUE, not with the accumulator. Every node of a fold carries one
	// accumulator per aggregate, and an empty hash set is not free: it allocates its buckets when it is
	// built. The payroll sheet opens 419 thousand nodes, none of them DISTINCT, and building and freeing
	// those sets stood in the stack samples of its fold (MEASURED 2026-09-12, Debug).
	std::unique_ptr<std::unordered_set<ibValue, ibValueHash, ibValueEqual>> m_seen;

	// ⭐⭐ A BALANCE only (a SUM over a `ROLE OPENING` / `ROLE CLOSING` field) — each KEY's reading at its first
	// (opening) or last (closing) moment. A group's balance is the sum of those, never of every row: the opening of a month
	// is not the openings of its thirty days added up. Rows of one key at one moment add up — they are
	// parts of that key the read did not tell apart. Made on the first reading, like the DISTINCT set.
	struct Reading { ibLevelKey m_moment; ibNumber m_sum{ 0 }; };
	std::unique_ptr<std::unordered_map<ibLevelKey, Reading, ibLevelKeyHash, ibLevelKeyEqual>> m_readings;

	// A KEY'S READING, or null — what ibStreamingFold::CarryBalancesByKey asks of a period.
	const Reading* ReadingOf(const ibLevelKey& key) const
	{
		if (!m_readings)
			return nullptr;
		const auto found = m_readings->find(key);
		return found != m_readings->end() ? &found->second : nullptr;
	}

	// …AND A KEY'S BALANCE CARRIED INTO A PERIOD IT DID NOT MOVE IN: a reading with no moment of its own, since
	// the key has no row here to be read at.
	void CarryBalance(const ibLevelKey& key, const ibNumber& balance)
	{
		if (!m_readings)
			m_readings = std::make_unique<std::unordered_map<ibLevelKey, Reading, ibLevelKeyHash, ibLevelKeyEqual>>();
		if (m_readings->emplace(key, Reading{ ibLevelKey(), balance }).second)
			++m_n;
	}

	void Feed(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRow& row)
	{
		if (a.m_balance != ibBalanceRole::None)
			FeedBalance(a, Operand(a, row), MarkOf(a, row));
		else
			FeedOperand(a, Operand(a, row));
	}

	// WHERE THIS ROW'S READING STANDS, for a balance — read once, like the operand.
	static ibBalanceMark MarkOf(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRow& row)
	{
		ibBalanceMark mark;
		mark.m_key.reserve(a.m_balanceKeys.size());
		for (const std::shared_ptr<const ibBackendQueryColumn>& key : a.m_balanceKeys)
			mark.m_key.push_back(row.Get(key));
		mark.m_moment.reserve(a.m_balanceMoments.size());
		for (const std::shared_ptr<const ibBackendQueryColumn>& period : a.m_balanceMoments)
			mark.m_moment.push_back(row.Get(period));
		return mark;
	}

	// ONE ENTRY FOR A FOLD THAT READ THE ROW ITSELF — the operand, and the mark when the aggregate is a balance.
	void FeedRead(const ibDataQueryBuilder::AggregateItem& a, const ibValue& v, const ibBalanceMark& mark)
	{
		if (a.m_balance != ibBalanceRole::None)
			FeedBalance(a, v, mark);
		else
			FeedOperand(a, v);
	}

	void FeedBalance(const ibDataQueryBuilder::AggregateItem& a, const ibValue& v, const ibBalanceMark& mark)
	{
		++m_rows;
		if (RamIsNullValue(v))
			return;
		++m_n;
		if (!m_readings)
			m_readings = std::make_unique<std::unordered_map<ibLevelKey, Reading, ibLevelKeyHash, ibLevelKeyEqual>>();
		const auto found = m_readings->find(mark.m_key);
		if (found == m_readings->end()) {
			m_readings->emplace(mark.m_key, Reading{ mark.m_moment, v.GetNumber() });
			return;
		}
		Reading& reading = found->second;
		if (ibLevelKeyEqual()(mark.m_moment, reading.m_moment))
			reading.m_sum += v.GetNumber();                              // the same moment: another part of it
		else if (a.m_balance == ibBalanceRole::Opening ? ibMomentBefore(mark.m_moment, reading.m_moment)
		                                                : ibMomentBefore(reading.m_moment, mark.m_moment))
			reading = Reading{ mark.m_moment, v.GetNumber() };         // an earlier (opening) / later (closing) one
	}

	// WHAT ONE ROW GIVES THIS AGGREGATE — read off the row once. COUNT(*) — no source column AND no
	// computed input — counts the row itself and has no value to read; a COMPUTED input (SUM(Qty *
	// Price), MAX(CASE …)) evaluates its expression per row; a plain input reads its source column.
	static ibValue Operand(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRow& row)
	{
		if (a.m_col == nullptr && !a.m_expr)
			return ibValue();
		return a.m_expr ? EvalColumnExprRow(a.m_expr, row) : row.Get(a.m_col);
	}

	// ⭐ …AND FED WHAT WAS READ. A streaming fold hands one row to every heading on its path — the grand
	// total, a department, an employee, and a cell across each of them — and it used to ask the row for
	// the operand at every one of them (2026-09-12). Read once by the fold, it is handed down as it is.
	void FeedOperand(const ibDataQueryBuilder::AggregateItem& a, const ibValue& v)
	{
		using Fn = ibDataQueryBuilder::AggregateFn;
		++m_rows;
		if (a.m_col == nullptr && !a.m_expr)
			return;   // COUNT(*) — the row is what it counts
		if (RamIsNullValue(v))
			return;
		if (a.m_distinct) {
			if (!m_seen)
				m_seen = std::make_unique<std::unordered_set<ibValue, ibValueHash, ibValueEqual>>();
			if (!m_seen->insert(v).second)
				return;   // already folded this value
		}
		switch (a.m_fn) {
		case Fn::Count:             ++m_n; break;
		case Fn::Sum: case Fn::Avg: m_sum += v.GetNumber(); ++m_n; break;   // in place — `m_sum + v` copied the sum first
		case Fn::Min: if (!m_have || v < m_best) { m_best = v; m_have = true; } break;
		case Fn::Max: if (!m_have || v > m_best) { m_best = v; m_have = true; } break;
		default: break;
		}
	}

	ibValue Result(const ibDataQueryBuilder::AggregateItem& a) const
	{
		// ⭐⭐ A FOLD THAT SAW NOTHING ANSWERS *NULL*, not an empty value.
		//
		// There is no smallest of no numbers and no average of none — SQL says NULL, and so does every
		// guard a person writes against it. A default `ibValue` is TYPE_EMPTY ("no type chosen"), which
		// answers FALSE to `IS NULL` and reads as zero, so the guard could not fire:
		// `ISNULL(MIN(B.Period), &Till)` over an unmatched LEFT JOIN group substituted NOTHING, and the
		// `DATEDIFF` around it measured to the date zero — 45 days for the row that matched and
		// **-739661** for the row that did not, side by side in one column (measured 2026-09-06).
		//
		// The same distinction an outer join makes for an unmatched row (RamNullValue): a fold with
		// nothing in it and a row that never matched are the same absence, and must read alike.
		//
		// ⚠ SUM OF NO ROWS IS ZERO, ON PURPOSE. SQL would say NULL there too, but this engine answers a
		// no-rows total with zero (MakeZeroAggregateResult, the door's own empty-result shape), and a
		// report that adds up nothing prints 0 rather than a blank.
		//
		// ⭐ …BUT A SUM OF ROWS THAT ALL HAVE NO VALUE IS NULL — and that is the decision about what a total
		// means, taken (Max, 2026-09-16): a figure that is not kept — a quantity on a supplier account,
		// empty on every row of it — adds up to empty, while a kept figure that came to nothing adds up to
		// zero. It is also what the server answers for the same SUM (a computed output that came back NULL
		// is read as NULL, columnCodec.cpp), so the report no longer depends on which road its totals took.
		using Fn = ibDataQueryBuilder::AggregateFn;
		// A BALANCE is the sum of its keys' readings — read the way a SUM is: nothing kept is NULL, nothing fed is zero.
		if (a.m_balance != ibBalanceRole::None) {
			if (m_rows > 0 && m_n == 0)
				return RamNullValue();
			ibNumber total{ 0 };
			if (m_readings)
				for (const auto& reading : *m_readings)
					total += reading.second.m_sum;
			return ibValue(total);
		}
		switch (a.m_fn) {
		case Fn::Count: return ibValue(ibNumber((a.m_col == nullptr && !a.m_expr) ? m_rows : m_n));
		case Fn::Sum: return (m_rows > 0 && m_n == 0) ? RamNullValue() : ibValue(m_sum);
		case Fn::Avg: return m_n > 0 ? ibValue(m_sum / ibNumber(m_n)) : RamNullValue();
		case Fn::Min: case Fn::Max: return m_have ? m_best : RamNullValue();
		default: return ibValue();
		}
	}
};

// One aggregate over the rows of a bucket — the accumulator above, fed the rows the caller already
// holds. (column read by GetColumnId.)
ibValue AggregateOne(const ibDataQueryBuilder::AggregateItem& a, const ibQueryRamTable& TC, const std::vector<long>& idx)
{
	ibAggAcc acc;
	for (long i : idx)
		acc.Feed(a, ibRamTableRow(TC, i));
	return acc.Result(a);
}


// ⭐⭐ DOES THIS AGGREGATE NEED A SLOT OF ITS OWN — i.e. one that is not its source column's?
//
// Two reasons, and they are the same reason seen twice: THERE IS NOTHING TO ROLL IN PLACE INTO.
//
//   * COUNT(*) has no source column at all;
//   * an aggregate with an AREA that did not get a receiver — folded here rather than by the server.
//     Its figure is NOT the column's roll-up: ApplyScopedAggregates keeps it at the named level and
//     CARRIES IT DOWN, so writing it in place overwrites the column under the whole area. A detail
//     row then shows its item's 30 where its own 15 belongs (CI, 2026-08-27), and if a ladder
//     aggregate names that column too, its every subtotal goes the same way.
//
// ⚠ The lowering already re-projects a repeated column so two aggregates do not share a slot — but
// only when it is SCALAR (a reference cannot be re-projected as one column), and only through that
// one road: this seam is public and takes the items it is handed. The rule belongs where the slot is
// decided, not with whoever happened to build the list.
//
// ⚠ ASKED ONCE, so the slot and the COLUMN declared for it (AddSyntheticAggColumns) cannot disagree:
// a figure declared under one name and written under another reads back as nothing at all.
bool AggNeedsOwnSlot(const ibDataQueryBuilder::AggregateItem& agg)
{
	return agg.m_col == nullptr || (agg.m_scopeDepth > 0 && agg.m_receiver == nullptr);
}

// WHERE AN AGGREGATE'S FIGURE LANDS — its OWN column when it rolls in place, its synthetic receiver
// otherwise. Asked in one place because both folds (bucketed and streaming) write the same slots,
// and a second answer to this is a figure that reads back under a different name than it was written.
// ⭐ DERIVED AT THE MOMENT IT IS ASKED FOR, from the aggregate itself — not stored, and not worked
// out a second time somewhere else. Both sides call THIS, over the same list, so they cannot come to
// different answers; the Aggregate kind is what keeps the slot clear of every other minted id
// (Max, 2026-09-06: *"you just re-derive its id at the moment of access"*).
ibMetaID AggSlotId(const std::vector<ibDataQueryBuilder::AggregateItem>& aggs, size_t i)
{
	return AggNeedsOwnSlot(aggs[i])
		? ibSynthId(ibSynthKind::Aggregate, static_cast<ibMetaID>(i))
		: aggs[i].m_col->GetColumnId();
}

// Roll the aggregates over `rows` and write each onto the node: a COLUMN aggregate IN-PLACE into its
// own column (a.m_col) — so it reads as the row value on a leaf and the SUBTOTAL on a group node
// (GetValue(col)); a COUNT(*) into its synthetic receiver (read by GetColumn(alias)).
void ApplyAggregates(ibSelectorTree::Node& node, const ibQueryRamTable& TC, const std::vector<long>& rows,
                     const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i)
		node.m_values[AggSlotId(aggs, i)] = AggregateOne(aggs[i], TC, rows);
}

// ⭐⭐ THE FIGURES THAT BELONG TO ONE LEVEL — `TOTALS SUM(x) OVER Item`.
//
// An aggregate with an area is folded exactly like any other: every node on the path already holds
// its own roll-up of the rows beneath it. What makes it different is only WHICH of those numbers is
// the answer — the one computed at the named level. So this is a single pass over the finished tree
// rather than a second kind of fold:
//
//   * ON that level — keep what is there;
//   * BELOW it — carry that value down, unchanged. Inside the area the figure IS constant, which is
//     what lets a detail row show the share's denominator;
//   * ABOVE it, or in another branch — ERASE it. Several values live up there and any one of them
//     would be a lie. ⚠ Erased, NOT zeroed: a zero joins sums as an addend and divides as a
//     denominator, while an absent value reads back as the runtime's NULL — "no such figure here".
//
// Done after the fold, so both roads (streaming and snapshot) get it from one place, and so a tree
// the DBMS folded would be treated the same the day the push-down learns to bring one.
void ApplyScopedAggregates(ibSelectorTree& tree, const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i) {
		const ibDataQueryBuilder::AggregateItem& agg = aggs[i];
		if (agg.m_scopeDepth <= 0)
			continue;                                   // area comes from the ladder — nothing to fix up
		const ibMetaID slot = AggSlotId(aggs, i);
		const ibString branch = agg.m_scopeBranch ? agg.m_scopeBranch->m_name : ibString();

		// `carried` = the value of the nearest ancestor that WAS the area, or null while above it.
		const std::function<void(ibSelectorTree::Node&, const ibValue*)> walk =
			[&](ibSelectorTree::Node& node, const ibValue* carried) {
				const ibValue* pass = carried;
				if (carried != nullptr) {
					node.m_values[slot] = *carried;     // below the area — the area's own figure
				}
				else if (node.m_level == agg.m_scopeDepth && node.m_branch.IsSameAs(branch, false)
				         && node.m_kind != ibSelectorNodeKind::Branch) {
					const auto it = node.m_values.find(slot);
					if (it != node.m_values.end())
						pass = &it->second;             // this IS the area — keep it, hand it down
				}
				else {
					node.m_values.erase(slot);          // above it, or another branch: no such figure
				}
				for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
					if (child != nullptr)
						walk(*child, pass);
			};
		walk(tree.Root(), nullptr);
	}
}

// Add the synthetic receiver COLUMNS to a tree — one for every aggregate that does not roll into a
// column of its own (AggNeedsOwnSlot: a COUNT(*), or an area folded here for want of windows). An
// aggregate that DOES roll in place needs none: its column is already present. Read by GetColumn(alias).
void AddSyntheticAggColumns(ibSelectorTree& tree, const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
{
	for (size_t i = 0; i < aggs.size(); ++i)
		if (AggNeedsOwnSlot(aggs[i]))
			tree.AddColumn(AggSlotId(aggs, i), aggs[i].m_alias, ibTypeDescription());
}

// ⭐ A NODE AS THE EVALUATOR SEES IT — the figures folded onto it, asked for by the names they were declared under
// (a derived figure's OutputRef). The twin of ibResultRow: the one evaluator walks the expression, and only the row
// it reads is a node of the tree.
class ibTreeNodeRow : public ibQueryRow {
public:
	ibTreeNodeRow(const ibSelectorTree::Node& node, const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
		: m_node(node), m_aggs(aggs) {}
	ibValue Get(ibMetaID id) const override {
		const auto it = m_node.m_values.find(id);
		return it != m_node.m_values.end() ? it->second : ibValue();
	}
	// …and a figure by its name, from the slot it was folded into — the same slot every road writes (AggSlotId).
	ibValue Get(const ibString& name) const override {
		for (size_t i = 0; i < m_aggs.size(); ++i)
			if (m_aggs[i].m_alias.IsSameAs(name, false))
				return Get(AggSlotId(m_aggs, i));
		return ibValue();
	}
private:
	const ibSelectorTree::Node&                           m_node;
	const std::vector<ibDataQueryBuilder::AggregateItem>& m_aggs;
};

// CellKey is GONE. Hierarchy linking keys by the VALUE itself now (ibValueHash / ibValueEqual,
// value.h): a REFERENCE compares by its _RRRef there — a row's own data-reference and a parent-ref
// pointing AT it are the same value, which is what the link needs — and no cell is rendered to text
// to say so. (docs/private/query-language-arc.md §22.1b)

// Context for the recursive hierarchy fold (invariant across the recursion).
// ---------------------------------------------------------------------------
// VALUE-KEYED INDEXES, all of them, in one place.
//
// Every hierarchy / grouping structure below keys by the VALUE itself through ibValueHash +
// ibValueEqual (value.h) — a reference matches by its guid there, which is what these need, and
// nothing is rendered to text per row to say so.
using ibRowsByValue    = std::unordered_map<ibValue, std::vector<long>,    ibValueHash, ibValueEqual>;
using ibRefChildren    = std::unordered_map<ibValue, std::vector<ibValue>, ibValueHash, ibValueEqual>;
// (ibLevelKey and its hash stand above ibAggAcc now — a balance keys its readings by one.)
using ibRowsByLevelKey = std::unordered_map<ibLevelKey, std::vector<long>, ibLevelKeyHash, ibLevelKeyEqual>;
using ibValueSeen      = std::unordered_map<ibValue, char,                 ibValueHash, ibValueEqual>;
using ibValueParentMap = std::unordered_map<ibValue, ibValue,              ibValueHash, ibValueEqual>;

// "No parent" — the row is a root. An absent value, an SQL null, or an empty string: the same three
// the old string key collapsed to "" and tested with .empty().
inline bool IsNoKey(const ibValue& v)
{
	return v.IsEmpty() || v.IsNull()
		|| (v.GetType() == ibValueTypes::TYPE_STRING && v.GetString().IsEmpty());
}

// ⭐ THE TREE A REFERENCE'S TARGET IS ARRANGED IN — read once and whole, for both roads that build a hierarchy
// level: each value's parent, its PLACE in its kind's order, and whether the kind keeps folders apart from items.
// The read is sorted by the target's own key, which the door sorts by the kind's order (a catalog by what it is
// presented by, a chart by its code), so the place is the order the kind's own list shows. Empty when the target
// has no parent column or there is no connection.
struct ibValueHierarchy {
	ibValueParentMap                                             parentOf;
	std::unordered_map<ibValue, long, ibValueHash, ibValueEqual> place;
	bool                                                         foldersFirst = false;
};

ibValueHierarchy ReadValueHierarchy(const std::shared_ptr<const ibBackendQueryable>& target, ibDatabaseConnectionHolder* holder)
{
	ibValueHierarchy tree;
	if (target == nullptr || holder == nullptr)
		return tree;
	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys = target->GetPrimaryKeyColumns();
	std::shared_ptr<const ibBackendQueryColumn> rk = keys.empty() ? nullptr : keys.front();
	std::shared_ptr<const ibBackendQueryColumn> pk = target->GetHierarchyColumn();
	if (rk == nullptr || pk == nullptr)
		return tree;
	tree.foldersFirst = target->GetHierarchyType() == eFolders;
	ibDataQueryBuilder q(holder);
	q.From(target).Select(rk, ibEmptyString).Select(pk, ibEmptyString).OrderBy(rk, /*ascending*/ true);
	ibSelector ts = q.Execute(ibReadPageRequest{}).Select(ibSelectKind::ibSelectKind_Direct);
	long place = 0;
	while (ts.Next()) {
		const ibValue key = ts.GetValue(rk);
		tree.parentOf[key] = ts.GetValue(pk);
		tree.place.emplace(key, place++);
	}
	return tree;
}

// ⭐⭐ THE VALUES OF A HIERARCHY LEVEL, FILED UNDER THEIR PARENTS IN THE ORDER THEIR ROWS ARRIVED — one
// arrangement for both roads that build such a tree (BuildReferenceHierarchy and the dimension combiner).
//
// A value stands where its own row stood — the fold's rule everywhere, so the read's order (a sort by a reference
// is its kind's order: a chart by its code) is the order of the tree. A folder with NO row of its own — every
// account of class 6 posted, the class itself never — stands where its FIRST descendant did. Appended after
// everything instead, a chart's class 6 printed below class 9; and walked from an unordered map, the whole level
// came out in the order of its hashes (Max, 2026-09-29: "the hierarchical output puts class 6 at the bottom").
// A folder that HAS a row keeps its own turn: filed from a child, a folder whose contents sort early jumped ahead
// of its own row.
//
// ⭐ …THAT IS A TREE OF PEERS — an account under an account, each an element in its own right. A catalog of
// FOLDERS AND ITEMS keeps them apart the way its list does: under every parent the folders first, among
// themselves in their kind's order (their place in the read above), then the items as their rows came (Max,
// 2026-09-29: "folders always go on top; subordinate accounts are peers"). A folder is what has children here —
// an item of such a catalog never does.
ibRefChildren ibArrangeUnderParents(const std::vector<ibValue>& keyOrder, const ibValueHierarchy& tree,
                                    const std::function<bool(const ibValue&)>& hasRow)
{
	const ibValueParentMap& parentOf = tree.parentOf;
	ibRefChildren childrenOf;
	ibValueSeen   seen;
	// "No parent" arrives as two values — a key the map does not mention, and an empty reference — and the root
	// is one bucket: both are filed under `ibValue()`, where the roots are looked up.
	const auto parentOfKey = [&parentOf](const ibValue& key) {
		const auto pit = parentOf.find(key);
		const ibValue par = pit != parentOf.end() ? pit->second : ibValue();
		return IsNoKey(par) ? ibValue() : par;
	};
	for (const ibValue& key : keyOrder) {
		if (IsNoKey(key) || seen[key])
			continue;
		seen[key] = 1;
		ibValue par = parentOfKey(key);
		childrenOf[par].push_back(key);
		// …and the folders above it that have no row of their own, at this moment
		while (!IsNoKey(par) && !seen[par] && !hasRow(par)) {
			seen[par] = 1;
			const ibValue up = parentOfKey(par);
			childrenOf[up].push_back(par);
			par = up;
		}
	}
	// Whatever is still unfiled above them follows — an ancestor the walk up stopped at.
	std::function<void(const ibValue&)> chainUp = [&](const ibValue& key) {
		if (IsNoKey(key) || seen[key])
			return;
		seen[key] = 1;
		const ibValue par = parentOfKey(key);
		childrenOf[par].push_back(key);
		chainUp(par);
	};
	for (const ibValue& key : keyOrder)
		chainUp(parentOfKey(key));
	// …the folders first, in their kind's order, where the kind keeps folders and items apart (see above)
	if (tree.foldersFirst) {
		const auto isFolder = [&childrenOf](const ibValue& v) { return childrenOf.contains(v); };
		const auto placeOf  = [&tree](const ibValue& v) {
			const auto it = tree.place.find(v);
			return it != tree.place.end() ? it->second : std::numeric_limits<long>::max();
		};
		for (auto& under : childrenOf) {
			std::vector<ibValue>& kids = under.second;
			const auto items = std::stable_partition(kids.begin(), kids.end(), isFolder);
			std::stable_sort(kids.begin(), items,
				[&placeOf](const ibValue& a, const ibValue& b) { return placeOf(a) < placeOf(b); });
		}
	}
	return childrenOf;
}

struct HierBuildCtx {
	const ibQueryRamTable*                               detail;
	std::shared_ptr<const ibBackendQueryColumn>          rowKeyCol;
	const ibRowsByValue*                                childrenOf;
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDimensionKind                                      mode;
	std::vector<bool>*                                  visited;   // cycle guard (malformed parent-ref)
};

// Attach the subtree rooted at detail-row `rowIdx` under `parent`; returns the subtree's detail-row
// indices (for the parent's subtotal). Sets the node's values (the row copied), m_hasChildren (from
// the data — its key appears as a parent), and a subtotal rolled over the WHOLE subtree (aggregated
// from raw leaves -> COUNT/AVG correct). HierarchyOnly omits a leaf element but still counts it.
std::vector<long> AttachHierNode(const HierBuildCtx& ctx, ibSelectorTree::Node* parent, long rowIdx, int level)
{
	if (rowIdx < 0 || rowIdx >= static_cast<long>(ctx.visited->size()) || (*ctx.visited)[static_cast<size_t>(rowIdx)])
		return {};                                   // cycle / out of range -> stop
	(*ctx.visited)[static_cast<size_t>(rowIdx)] = true;

	const ibValue key = ctx.detail->GetCell(rowIdx, ctx.rowKeyCol->GetColumnId());
	const auto cit = ctx.childrenOf->find(key);
	const bool hasKids = (cit != ctx.childrenOf->end() && !cit->second.empty());

	if (ctx.mode == ibDimensionKind::HierarchyOnly && !hasKids)
		return { rowIdx };                           // a leaf element: not a node, but counts upward

	ibSelectorTree::Node* node = parent->AddChild(level);
	for (const ibQueryRamColumn& col : ctx.detail->Columns())
		node->m_values[col.m_id] = ctx.detail->GetCell(rowIdx, col.m_id);
	node->m_hasChildren = hasKids;

	std::vector<long> subtreeRows = { rowIdx };
	if (hasKids)
		for (long childIdx : cit->second) {
			std::vector<long> kids = AttachHierNode(ctx, node, childIdx, level + 1);
			subtreeRows.insert(subtreeRows.end(), kids.begin(), kids.end());
		}
	ApplyAggregates(*node, *ctx.detail, subtreeRows, *ctx.aggregates);
	return subtreeRows;
}

// (The recursive multi-level totals fold that stood here is gone: it bucketed the rows level by
// level to build what one streaming descent builds — see ibStreamingFold, further down.)

} // namespace ibProvider

ibDataQueryResult ibQueryComposer::ExecuteRead(const ibDataQuerySpec& given, const ibReadPageRequest& page)
{
	const ibDataQuerySpec spec = ReadAsOneStatement(given);
	return ProviderOf(spec).ExecuteRead(spec, page);
}

ibDataQueryResult ibQueryComposer::ExecuteReadCached(const ibDataQuerySpec& given, const ibReadPageRequest& page,
                                                     ibRenderedPageCache& cache, const ibPageSignature& signature)
{
	// A rendered page is one source's text on its own base: a composite's statement is not that source's signature, and
	// a base in memory reads no text.
	const ibDataQuerySpec spec = ReadAsOneStatement(given);
	if (spec.m_queryable != given.m_queryable)
		return ProviderOf(spec).ExecuteRead(spec, page);
	return ProviderOf(spec).ExecuteReadCached(spec, page, cache, signature);
}

// A fold is a statement's GROUP BY on whichever road the spec takes — a composite's, a base in memory's.
ibDataQueryResult ibQueryComposer::ExecuteAggregate(const ibDataQuerySpec& given)
{
	const ibDataQuerySpec spec = ReadAsOneStatement(given);
	return ProviderOf(spec).ExecuteAggregate(spec);
}

namespace ibProvider {

// ⭐ WHAT A LEVEL FIELD CONTRIBUTES TO THE KEY — its cell, or the START OF THE PERIOD containing it
// when the field is read BY PERIODS. `ibDateTime::BeginOfPeriod` is the RAM twin of the expression the SQL
// road groups by, walking the calendar rather than approximating it — so a row lands in the same
// bucket whichever road read it, which is the only way two roads may exist at all.
ibValue LevelKeyValue(const ibTotalField& field, const ibQueryRow& row)
{
	const ibValue v = row.Get(field.m_col);
	if (!field.ByPeriods() || v.GetType() != TYPE_DATE)
		return v;
	return ibValue(v.GetDate().BeginOfPeriod(field.m_periods->m_unit));
}

// ⭐⭐ THE STREAMING FOLD — rows in, tree out, ONE PASS, and nothing kept but the tree.
//
// The recursive fold this replaces bucketed the rows level by level: a vector of row indices per
// group, per level, over a snapshot that was itself the whole detail. It read every row exactly once
// per level and never looked back, which is to say it never needed any of it — the shape was there
// because the rows were.
//
// Fed a row at a time, the same tree comes out of ONE descent: the row's key at each level names the
// node it belongs to (opened on first sight, so groups keep first-seen order exactly as before), and
// every node on the path takes the row into its ACCUMULATORS. Memory is then a function of the
// number of GROUPS — the composition's acceptance criterion — and the detail rows are never all in
// hand at once.
//
// Two nodes are NOT held here: a DETAIL leaf (a level with no fields is one node per row — the
// report asked to print the rows, so it is paying for rows on purpose) and the tree itself.
class ibStreamingFold
{
public:
	// ⭐ ONE LADDER OF LEVELS — the common ones, or one branch's. Everything the fold used to ask of
	// the whole list is asked of this instead, which is what lets a branch be a cross-table of its
	// own: where the columns start and whether the last level is the records are facts about a
	// LADDER, and a branch is one.
	struct Section {
		const ibTotalBranch* m_branch = nullptr;   // null = the common ladder
		std::size_t m_from = 0, m_to = 0;          // its levels, as a half-open range of m_levels
		std::size_t m_across = 0;                  // the first of them that reads ACROSS the page
		std::size_t m_depth  = 1;                  // the DEPTH its first level stands at
		bool        m_details = false;             // its last level is the records…
		bool        m_detailsAcross = false;       // …and they read across rather than down

		// ⚠ DEPTH IS COUNTED WITHIN THE LADDER, not by position in the flat list. Two branches begin
		// at the same place, so their first levels must carry the same number — otherwise the second
		// branch would print one rung deeper than the first for no reason a reader could see.
		int DepthOf(std::size_t li) const { return static_cast<int>(m_depth + (li - m_from)); }
		int DepthOfDetails() const { return static_cast<int>(m_depth + (m_to - m_from) - 1); }
	};

	// ⚠ NO `identity` HERE, AND THAT IS AN ANSWER RATHER THAN AN OMISSION. The snapshot fold reads the
	// row's identity into its `DimCtx` because its headings ARE the rows; this road was taught the same
	// rule, it was RUN, and it was taken out again the next day — the note at the top of `FeedLadder`
	// says what the sheet looked like. It used to be TOLD the identity anyway, and the parameter then
	// sat unread, which reads from the outside exactly like a rule that failed to arrive: a reader
	// coming from the snapshot road found a field nothing touched and a compiler warning agreeing with
	// them. Read `FeedLadder` before adding it back.
	ibStreamingFold(ibSelectorTree& tree, const std::vector<ibTotalLevel>& levels,
	                const std::vector<ibDataQueryBuilder::AggregateItem>& aggs,
	                const std::vector<ibQueryRamColumn>& rowColumns)
		: m_levels(levels), m_aggs(aggs), m_rowColumns(rowColumns)
	{
		// WHERE THE PAGE STOPS AND THE PAGE-WIDTH STARTS — asked of the levels, which say which way
		// they read. Everything from here on is a column key of a cross-table.
		// ⚠ THE DETAIL LEVEL COUNTS HERE TOO. A column axis made of NOTHING BUT records — "the
		// resources laid out across the page, one column per row" — starts at that level, and
		// skipping it because it has no fields would leave the axis unfound.
		// ⭐⭐ THE LEVELS FALL INTO SECTIONS, AND A REPORT WITHOUT `SPLIT` HAS EXACTLY ONE. A section is
		// a ladder: the levels that fold one after another under a shared head. Written without
		// branches — every report there has ever been — the whole list is that one section and every
		// question below is asked of it, so nothing about the old road changes shape.
		//
		// `SPLIT` opens the next one. The common levels come first (they carry no branch), and each
		// branch's levels follow as a section of their own — which is what lets the same rows be
		// folded by characteristic on one side and by series on the other.
		for (std::size_t li = 0; li < m_levels.size(); ++li) {
			const ibTotalBranch* branch = m_levels[li].m_branch.get();
			if (m_sections.empty() || branch != m_sections.back().m_branch)
				m_sections.push_back(Section{ branch, li, li });
			m_sections.back().m_to = li + 1;
		}
		// ⚠ THE COMMON SECTION EXISTS EVEN WHEN NOTHING IS COMMON. A query may fork straight away —
		// `TOTALS SUM(x) BY SPLIT Item SPLIT Unit`, no shared level at all — and there the first
		// section found above is already a BRANCH. Read as the common ladder it would make branch one
		// the trunk and hang branch two INSIDE it, which is the opposite of what was asked. So an
		// empty common section is put in front: every branch then forks from the root, where they all
		// belong. (An empty list — `TOTALS BY OVERALL` — lands here too, and one empty section is
		// exactly right for it.)
		if (m_sections.empty() || m_sections.front().m_branch != nullptr)
			m_sections.insert(m_sections.begin(), Section{ nullptr, 0, 0 });

		// EACH SECTION READS ITSELF THE SAME WAY THE WHOLE LIST USED TO. Where the page stops and the
		// page-width starts, and whether the last level is the records: both are facts about a
		// LADDER, and a branch is a ladder — so a branch may be a cross-table of its own, which is
		// the point of asking this per section rather than once for the query.
		//
		// ⚠ THE DETAIL LEVEL COUNTS TOWARDS THE COLUMN AXIS TOO. A column axis made of NOTHING BUT
		// records — "the resources laid out across the page, one column per row" — starts at that
		// level, and skipping it because it has no fields would leave the axis unfound.
		for (Section& s : m_sections) {
			s.m_across = s.m_to;
			for (std::size_t li = s.m_from; li < s.m_to; ++li)
				if (m_levels[li].m_axis == ibTotalsAxis::Columns) { s.m_across = li; break; }
			s.m_details       = s.m_to > s.m_from && m_levels[s.m_to - 1].m_fields.empty();
			s.m_detailsAcross = s.m_details && m_levels[s.m_to - 1].m_axis == ibTotalsAxis::Columns;
		}
		// EVERY BRANCH BEGINS WHERE THE COMMON LADDER ENDED, so they all start at one depth — the
		// number of common ROW levels plus one. The common section itself starts at 1, which is the
		// number its first level has always carried.
		for (std::size_t si = 1; si < m_sections.size(); ++si)
			m_sections[si].m_depth = 1 + (m_sections.front().m_across - m_sections.front().m_from);
		m_pool.emplace_back(&tree.Root(), NewAccs());   // the root IS the grand total
		m_rowKeys.resize(m_levels.size());
		m_operands.resize(m_aggs.size());
		m_marks.resize(m_aggs.size());
	}

	void Feed(const ibQueryRow& row)
	{
		// ⭐ THE ROW IS READ ONCE, HERE — every level's key and every aggregate's operand — and what follows
		// only hands those on. A row belongs to a heading per level and to a cell across each of them, and
		// each used to ask the row again: the column key once per row heading, the operand once per node
		// on the path (2026-09-12, the payroll sheet: a dozen reads of four cells per row).
		for (std::size_t li = 0; li < m_levels.size(); ++li) {
			ibLevelKey& key = m_rowKeys[li];
			key.clear();
			for (const ibTotalField& field : m_levels[li].m_fields)
				key.push_back(LevelKeyValue(field, row));      // one field is the degenerate one-element key
		}
		for (std::size_t i = 0; i < m_aggs.size(); ++i) {
			m_operands[i] = ibAggAcc::Operand(m_aggs[i], row);
			if (m_aggs[i].m_balance != ibBalanceRole::None)
				m_marks[i] = ibAggAcc::MarkOf(m_aggs[i], row);   // …and where a balance's reading stands, once too
		}

		std::size_t cur = 0;
		FeedNode(cur);                                        // the grand total takes every row

		// ⭐⭐ AND THE COLUMNS HANG UNDER THE GRAND TOTAL TOO — which is what "what does this column
		// add up to" IS. The root is a heading like any other; the only thing that made its cells
		// special was that nothing produced them.
		//
		// 🛑 THEY USED TO BE A SECOND FOLD — the whole output read again, by its column keys alone,
		// with a flag telling the driver which pass it was in. The reason given was that the two sets
		// of subtotals "cannot come out of one tree", and that was true only while the tree was ONE
		// CHAIN: rows-then-columns has no node standing for "the columns alone". Now every heading
		// carries its own column branch, and the root is the heading over everything.
		FeedAcross(cur, row, m_sections.front());

		// THE COMMON LADDER — the levels written before the first `SPLIT`, which in a report that has
		// none is all of them. This is the whole of what Feed used to do.
		cur = FeedLadder(cur, row, m_sections.front());

		// ⭐⭐ …AND THEN EACH BRANCH, FROM THE NODE THE COMMON LADDER ENDED ON. The same row goes down
		// every branch: that is what `SPLIT` says — one read, folded several ways, each with its own
		// order of groupings (Max, 2026-08-27). A report with no branches does not enter this loop.
		for (std::size_t bi = 1; bi < m_sections.size(); ++bi) {
			const std::size_t fork = BranchChild(cur, bi);
			// FED LIKE THE HEADING IT STANDS UNDER, and for the same reason a detail leaf is: a fork
			// covers exactly the rows its parent does, so its figure is the parent's. Left unfed it
			// would report a nought — a total that reconciles to nothing, sitting where a reader who
			// walks into the branch would find it.
			FeedNode(fork);
			FeedLadder(fork, row, m_sections[bi]);
		}
	}

	// ⭐ ONE LADDER, FED FROM `cur` DOWN — the levels of one section, in order, each opening the node
	// its key names and taking the row into it. Returns the node the last ROW level opened, because
	// that is what the records hang under and what a branch forks from.
	std::size_t FeedLadder(std::size_t cur, const ibQueryRow& row, const Section& section)
	{
		// 🛑⭐⭐ "A LEVEL KEYED BY THE ROW'S IDENTITY **IS** THE ROW" WAS TRIED HERE AND TAKEN OUT AGAIN.
		// It stamped the row's fields onto such a heading and stopped the descent under it, and the
		// result is worth remembering: the sheet printed 72 lines that each looked exactly like a
		// detail record, so the groupings became INVISIBLE — a heading that carries a row is not
		// distinguishable from a row (Max, 2026-08-29: *"I set a grouping by reference and one by data
		// version; I expect to see the first, the second, and the third — the detail records. Right
		// now I see nothing except the detail records"*).
		//
		// The ladder is read LITERALLY: what a person names is what they get, one heading per rung and
		// the records at the bottom. A rung that happens to hold one row is still a rung — "it holds
		// one row" is a fact about the data, not permission to delete a level the user configured.
		for (std::size_t li = section.m_from; li < section.m_across; ++li) {
			const ibTotalLevel& level = m_levels[li];
			if (level.m_fields.empty())
				break;   // the detail level — handled below, where the row axis ends
			cur = Child(cur, m_rowKeys[li], level, section.DepthOf(li), /*across*/false);
			FeedNode(cur);


			// ⭐⭐ AND THE COLUMNS HANG UNDER EVERY ROW HEADING, not only under the deepest one.
			//
			// 🛑 THEY USED TO HANG UNDER THE LAST ROW LEVEL ALONE, because the fold was one chain and
			// the column keys were simply its tail. So a table with two row groupings printed its
			// figures against the INNER heading and left the outer one blank (Max, 2026-08-26: "if
			// one more grouping appears, it shows no totals above"). The cells of an outer heading
			// are a fold over a SUBSET of the keys — the row prefix and the columns, skipping what
			// is between — and a subset is not a prefix, so no single chain can carry it.
			//
			// It costs a walk down the column axis per row heading the row belongs to, and holds
			// `headings × column keys` nodes. Memory is still counted in GROUPS, which is the whole
			// criterion of the reports arc.
			FeedAcross(cur, row, section);

			// ⚠ NOTHING IS SKIPPED HERE. Every rung the ladder names is folded, and the records under
			// the last one — which is what the LIST does, one rung per fetch, the next when a node is
			// opened. A sheet that stopped early had rungs the list plainly showed, and the two roads
			// giving one answer is the whole requirement (Max, 2026-08-29: *"I need the result for
			// lists and the result for report tables to be the same"*).
		}

		// ⭐ AND THE ROW ITSELF, under the last ROW heading — not under the last heading of all. In an
		// ordinary report those are the same node and this reads exactly as it always did; in a table
		// the columns stand ACROSS the detail row, which is what makes it a line of the table with
		// cells beside it rather than something hanging inside one cell.
		//
		// Its LEVEL is the one past the last dimension of THIS ladder — the number it has always had
		// in a report with one — so a printer that lays rows out by depth is told nothing new, and no
		// column key shares a number with it.
		//
		// ⚠ THE RECORDS BELONG TO THE BRANCH THAT ASKED FOR THEM. Two branches that both declare a
		// detail level both get the row, each under its own headings — the same fact shown in two
		// cuts, which is what a split IS. A branch that declared none holds no records and pays for
		// none.
		// ⭐⭐ AND THE RECORDS ARE ALWAYS THERE. A list gets them for nothing — it drills to the rows —
		// so a report is the same read with the records level added, unconditionally. Every rule that
		// tried to decide when they were "redundant" produced a second universe: the list showed rows
		// the sheet did not (Max, 2026-08-30: *"for the report the detail records are simply always
		// added"*, and *"you have two universes right now"*).
		if (section.m_details && !section.m_detailsAcross) {
			const std::size_t leaf = AddDetail(cur, row, section.DepthOfDetails(), section);
			if (leaf != kNoNode) {
				// ⚠ AND IT IS FED LIKE A HEADING — one row's worth. Without this its accumulators
				// stay empty and Finish writes a zero into the very total that is supposed to say
				// what this record contributed.
				FeedNode(leaf);
				FeedAcross(leaf, row, section);
			}
		}
		return cur;
	}

	// ⭐ THE FIGURES ARE WRITTEN WHEN THE LAST ROW HAS BEEN READ. An accumulator IS the subtotal
	// while it is still being fed, and asking it earlier would be asking a question whose answer is
	// not in yet. In-place in each aggregate's OWN column, exactly as ApplyAggregates writes them —
	// so a figure reads as the row value on a leaf and as the subtotal on a heading.
	// ⭐⭐ …AND A DETAIL ROW IN A TABLE IS FIGURED LIKE ANY OTHER HEADING. In an ordinary report a
	// record and a measure share ONE column, so the record shows its own value there — `COUNT(Number)`
	// printing 1 where the document's number belongs is the defect that rule exists for. A TABLE has
	// a column per measure and a total at the right, and there a record is "a sub-group of one row"
	// (Max, 2026-08-26): its cells and its row total are FIGURES — COUNT is 1, SUM is the value —
	// not the row's fields spilled into the totals column.
	//
	// The two cases separate themselves and need no flag: a record is only ever POOLED when there is
	// a column axis (see AddDetail), so this loop reaches exactly the records that are laid out as
	// headings and never touches the ones that share their column with a measure.
	void Finish()
	{
		CarryBalancesByKey();
		for (FoldNode& n : m_pool) {
			for (std::size_t i = 0; i < m_aggs.size(); ++i)
				n.m_node->m_values[AggSlotId(m_aggs, i)] = m_accPool[n.m_accs + i].Result(m_aggs[i]);
			n.m_node->m_hasChildren = !n.m_node->m_children.empty();
		}
	}

	// ⭐⭐ A KEY'S BALANCE IN A PERIOD IT DID NOT MOVE IN IS CARRIED, NOT LEFT OUT — the rule PadPeriodChildren keeps
	// for a period nobody moved in, said for one key. A source read by movement (Periodicity = Recorder / Record)
	// has a row only where a key moved, so a month heading added up only the keys that moved that month: a key
	// holding 6 000 on account 90 through a quiet August left August's opening 6 000 short (2026-09-29, the ROLE
	// battery). So under every heading whose children are a PERIOD level, a key missing from a period takes, as
	// its closing AND its opening there, its last closing before it — what stood at the end of its last movement
	// stands through the quiet period; before its first reading, its first opening after is what stood all along.
	// The two edges of one figure are paired in the order TOTALS names them (the first opening with the first
	// closing); a figure with only one edge carries the way PadPeriodChildren does: an opening from after, a
	// closing from before. (A source that has a row for every period a key holds a balance in — a daily or a
	// monthly reading — leaves nothing to carry: a key missing there held nothing.)
	void CarryBalancesByKey()
	{
		std::vector<std::size_t> openings, closings;
		for (std::size_t i = 0; i < m_aggs.size(); ++i) {
			if (m_aggs[i].m_balance == ibBalanceRole::Opening) openings.push_back(i);
			else if (m_aggs[i].m_balance == ibBalanceRole::Closing) closings.push_back(i);
		}
		if (openings.empty() && closings.empty())
			return;

		std::unordered_map<const ibSelectorTree::Node*, std::size_t> accsOf;   // a node's first accumulator
		for (const FoldNode& n : m_pool)
			accsOf[n.m_node] = n.m_accs;

		const std::size_t none = static_cast<std::size_t>(-1);
		for (const FoldNode& n : m_pool) {
			// THE CHILDREN THAT ARE ONE PERIOD LEVEL, in time order
			std::vector<std::pair<ibDateTime, std::size_t>> periods;
			int level = -1;
			for (const std::unique_ptr<ibSelectorTree::Node>& child : n.m_node->m_children) {
				if (child == nullptr || child->m_kind == ibSelectorNodeKind::Detail)
					continue;
				const int li = child->m_level - 1;
				if (li < 0 || static_cast<std::size_t>(li) >= m_levels.size() || m_levels[li].m_fields.empty())
					continue;
				const ibTotalField& head = m_levels[li].m_fields.front();
				if (head.m_col == nullptr
				    || (!head.ByPeriods() && head.m_col->GetBalanceRole() != ibBalanceRole::Moment))
					continue;
				if (level == -1)
					level = child->m_level;
				else if (child->m_level != level)
					continue;
				const auto value = child->m_values.find(head.m_col->GetColumnId());
				const auto accs  = accsOf.find(child.get());
				if (value == child->m_values.end() || value->second.GetType() != TYPE_DATE || accs == accsOf.end())
					continue;
				periods.emplace_back(value->second.GetDate(), accs->second);
			}
			if (periods.size() < 2)
				continue;
			std::stable_sort(periods.begin(), periods.end(),
				[](const auto& a, const auto& b) { return a.first < b.first; });

			const std::size_t pairs = std::max(openings.size(), closings.size());
			for (std::size_t k = 0; k < pairs; ++k) {
				const std::size_t o = k < openings.size() ? openings[k] : none;
				const std::size_t c = k < closings.size() ? closings[k] : none;
				const auto accOf = [&](std::size_t accs, std::size_t agg) -> ibAggAcc* {
					return agg != none ? &m_accPool[accs + agg] : nullptr;
				};

				// every key read in any of these periods, by either edge
				std::unordered_set<ibLevelKey, ibLevelKeyHash, ibLevelKeyEqual> keys;
				for (const auto& period : periods)
					for (const std::size_t agg : { o, c })
						if (const ibAggAcc* acc = accOf(period.second, agg))
							if (acc->m_readings)
								for (const auto& reading : *acc->m_readings)
									keys.insert(reading.first);

				for (const ibLevelKey& key : keys) {
					std::vector<bool> moved(periods.size(), false);
					for (std::size_t p = 0; p < periods.size(); ++p) {
						const ibAggAcc* oa = accOf(periods[p].second, o);
						const ibAggAcc* ca = accOf(periods[p].second, c);
						moved[p] = (oa != nullptr && oa->ReadingOf(key) != nullptr) || (ca != nullptr && ca->ReadingOf(key) != nullptr);
					}
					// FORWARD — the last closing, into every quiet period after it
					if (c != none) {
						bool have = false;
						ibNumber last{ 0 };
						for (std::size_t p = 0; p < periods.size(); ++p) {
							ibAggAcc* ca = accOf(periods[p].second, c);
							if (moved[p]) {
								if (const ibAggAcc::Reading* r = ca->ReadingOf(key)) { have = true; last = r->m_sum; }
								continue;
							}
							if (!have)
								continue;
							ca->CarryBalance(key, last);
							if (ibAggAcc* oa = accOf(periods[p].second, o))
								oa->CarryBalance(key, last);
							moved[p] = true;
						}
					}
					// BACKWARD — the first opening, into every quiet period before it
					if (o != none) {
						bool have = false;
						ibNumber next{ 0 };
						for (std::size_t p = periods.size(); p-- > 0;) {
							ibAggAcc* oa = accOf(periods[p].second, o);
							if (moved[p]) {
								if (const ibAggAcc::Reading* r = oa->ReadingOf(key)) { have = true; next = r->m_sum; }
								continue;
							}
							if (!have)
								continue;
							oa->CarryBalance(key, next);
							if (ibAggAcc* ca = accOf(periods[p].second, c))
								ca->CarryBalance(key, next);
						}
					}
				}
			}
		}
	}

	// THE NUMBER OF GROUPS — what the journal prints beside the number of rows, because that pair is
	// the acceptance criterion stated as two numbers.
	long NodeCount() const { return static_cast<long>(m_pool.size()); }

private:
	// A node under construction: the tree node it already is, one accumulator per aggregate, and the
	// children it has opened, by their level key. Pool indices rather than pointers — the pool grows
	// as the tree does, and an index survives that.
	//
	// ⭐ TWO MAPS, because a heading in a table opens children of two kinds and their keys are values
	// of DIFFERENT fields: "True" as a row key and "True" as a column key are not the same child.
	// One map would have merged them silently — the worst kind of agreement.
	// ⚠ THE MAPS OF CHILDREN ARE MADE WITH THE FIRST CHILD, and the node is built where it will live. Most
	// nodes of a fold never open a child — a record, a cell — yet each carried two hash maps from birth,
	// and an empty one allocates its buckets; the node was built as a temporary, moved into the pool and
	// destroyed again. The payroll sheet opens 419 thousand of them, and building, moving and freeing
	// those maps was the heaviest thing in the stack samples of its fold (MEASURED 2026-09-12, Debug).
	// ⚠ AND ITS ACCUMULATORS LIVE IN ONE POOL BESIDE IT (m_accPool), the node holding where they start.
	// A vector of its own was one more allocation and one more free per node — 419 thousand of each for
	// one payroll sheet, and they stood in the stack samples of its fold and of the fold's end.
	struct FoldNode {
		using ChildMap = std::unordered_map<ibLevelKey, std::size_t, ibLevelKeyHash, ibLevelKeyEqual>;
		FoldNode(ibSelectorTree::Node* node, std::size_t accs) : m_node(node), m_accs(accs) {}
		ibSelectorTree::Node* m_node = nullptr;
		std::size_t           m_accs = 0;   // this node's first accumulator in m_accPool — one per aggregate
		std::unique_ptr<ChildMap> m_children;         // null until the first child is opened
		// …and the cells across a heading, which are FEW — a column per key, three for an employee, seven
		// for a department — so they are kept as a list and found by equality, which for the same
		// reference object is a pointer compare (ibValue::CompareValueLS). A hash map costs its hash and,
		// in a checked build, a global lock per iterator it makes; the fold asked one for every cell of
		// every row (2026-09-12). Past kAcrossListMax the list becomes the map, and stays one.
		// (Made with the first cell, like the maps: most nodes of a fold ARE cells and have none, and an
		// empty vector is an allocation of its own in a checked build.)
		// ⭐ AND IT HOLDS THE CELLS, NOT COPIES OF THEIR KEYS: a cell's key is already in its own values,
		// written there when it was opened, so it is asked of the cell (CellHasKey). A copy per cell was a
		// vector of values built and kept for each of 120 thousand cells of one payroll sheet.
		std::unique_ptr<std::vector<std::size_t>> m_acrossList;
		std::unique_ptr<ChildMap> m_acrossChildren;   // null while the list is enough
		std::size_t           m_acrossCount = 0;   // cells written so far — where the next one is inserted
		// THE CHILD THE LAST ROW WENT TO, down the page. The rows arrive sorted by the levels' keys, so
		// the next row almost always goes to the same department and, two times in three, to the same
		// employee — and asking whether its key is that child's key is one comparison where the map
		// would hash the key and compare it anyway. The key is the map's own (a node's key does not
		// move while the map lives). Not kept across the page: a cell's key changes every row.
		const ibLevelKey*     m_lastKey   = nullptr;
		std::size_t           m_lastChild = 0;
		// THE FORKS THIS HEADING HAS OPENED, by section. Not a map, because a branch is not reached by
		// a KEY — it is named by the query and there is a fixed handful of them. Left NULL until the
		// first fork is opened, so a report without `SPLIT` carries nothing extra per node.
		std::unique_ptr<std::vector<std::size_t>> m_branchChildren;
	};


	static constexpr std::size_t kNoNode = static_cast<std::size_t>(-1);

	// THE CELLS OF ONE HEADING — the column axis walked from the heading down, feeding a node per
	// level so an upper column level keeps its own subtotal exactly as it always did.
	void FeedAcross(std::size_t heading, const ibQueryRow& row, const Section& section)
	{
		std::size_t cur = heading;
		for (std::size_t li = section.m_across; li < section.m_to; ++li) {
			const ibTotalLevel& level = m_levels[li];
			if (level.m_fields.empty()) {
				// ⭐ THE RECORDS, ACROSS THE PAGE — each one a column of its own, which is what a
				// detail level declared on the COLUMN axis asks for. Nothing groups here and nothing
				// follows: a record is the end of an axis whichever way that axis reads.
				if (section.m_detailsAcross) {
					const std::size_t leaf = AddDetail(cur, row, section.DepthOfDetails(), section);
					if (leaf != kNoNode)
						FeedNode(leaf);
				}
				break;
			}
			cur = Child(cur, m_rowKeys[li], level, section.DepthOf(li), /*across*/ li == section.m_across);
			FeedNode(cur);
		}
	}

	// ⭐⭐ THE FORK ITSELF — the node a branch's ladder hangs from, opened on the heading the common
	// levels ended on. It carries no key and no figure: what it says is "from here the rows go
	// another way", and a walk that was not told which way goes through them in order.
	//
	// Opened ONCE per heading and remembered by branch, exactly as a level's child is remembered by
	// its key — a heading meets every row that belongs to it, and the second row must find the fork
	// the first one made rather than start a second copy of it.
	std::size_t BranchChild(std::size_t parent, std::size_t branch)
	{
		std::unique_ptr<std::vector<std::size_t>>& forks = m_pool[parent].m_branchChildren;
		if (!forks)
			forks = std::make_unique<std::vector<std::size_t>>(m_sections.size(), kNoNode);
		if ((*forks)[branch] != kNoNode)
			return (*forks)[branch];

		ibSelectorTree::Node* parentNode = m_pool[parent].m_node;
		// AT THE PARENT'S OWN DEPTH — the fork is not a level, so it must not spend one. What hangs
		// under it is numbered as though it hung under the heading itself, which is what keeps two
		// branches at the SAME depth however many levels stand above them.
		ibSelectorTree::Node* node = parentNode->AddChild(parentNode->m_level);
		node->m_kind   = ibSelectorNodeKind::Branch;
		node->m_values = parentNode->m_values;   // the keys above stay readable inside the branch
		node->m_branch = parentNode->m_branch;   // …and so does the branch above, until this fork names its own
		if (const ibTotalBranch* named = m_sections[branch].m_branch)
			node->m_branch = named->m_name;

		const std::size_t idx = m_pool.size();
		m_pool.emplace_back(node, NewAccs());
		// ⚠ RE-READ THE POOL ENTRY — the emplace above may have reallocated (same trap as Child).
		(*m_pool[parent].m_branchChildren)[branch] = idx;
		return idx;
	}

	// The row in hand — read by Feed — into one node's accumulators.
	void FeedNode(std::size_t idx)
	{
		const std::size_t at = m_pool[idx].m_accs;
		for (std::size_t i = 0; i < m_aggs.size(); ++i)
			m_accPool[at + i].FeedRead(m_aggs[i], m_operands[i], m_marks[i]);
	}

	// A node's accumulators, one per aggregate, taken from the pool — see FoldNode.
	std::size_t NewAccs()
	{
		const std::size_t at = m_accPool.size();
		m_accPool.resize(at + m_aggs.size());
		return at;
	}

	// `key` is the row's, read once for every heading it opens (Feed) — so it is copied only into a map
	// that gains a child, which happens once per GROUP rather than once per row.
	std::size_t Child(std::size_t parent, const ibLevelKey& key, const ibTotalLevel& level, int childLevel, bool across)
	{
		if (!across && m_pool[parent].m_lastKey != nullptr && ibLevelKeyEqual()(*m_pool[parent].m_lastKey, key))
			return m_pool[parent].m_lastChild;   // the same child as the row before — see FoldNode::m_lastKey
		if (across && !m_pool[parent].m_acrossChildren) {
			if (const std::vector<std::size_t>* cells = m_pool[parent].m_acrossList.get()) {
				for (size_t i = 0; i < cells->size(); ++i)   // by index — no iterators to lock for
					if (CellHasKey((*cells)[i], level, key))
						return (*cells)[i];   // the few cells of a heading, walked — see FoldNode::m_acrossList
			}
		}
		else if (const std::unique_ptr<FoldNode::ChildMap>& opened = across ? m_pool[parent].m_acrossChildren
		                                                                    : m_pool[parent].m_children) {
			const auto it = opened->find(key);
			if (it != opened->end()) {
				if (!across) {
					m_pool[parent].m_lastKey   = &it->first;
					m_pool[parent].m_lastChild = it->second;
				}
				return it->second;
			}
		}

		ibSelectorTree::Node* parentNode = m_pool[parent].m_node;
		// THE CELLS FIRST, THE SUB-HEADINGS AFTER — see Node::InsertChild. A walk is pre-order, and
		// that order is the only thing that says which heading a cell stands beside.
		ibSelectorTree::Node* child      = across
			? parentNode->InsertChild(m_pool[parent].m_acrossCount++, childLevel)
			: parentNode->AddChild(childLevel);
		// A SUBGROUP inherits the grouping fields available from the levels above, so a display column
		// that dot-walks an ancestor dimension resolves in the subgroup header too. Only the KEYS are
		// there to inherit at this point — the figures are written at the end, by Finish().
		// (Room for its own key and its figures is made first, and the copy keeps it — the values are
		// one vector, and growing it twice more per node was a reallocation each.)
		child->m_values.reserve(parentNode->m_values.size() + level.m_fields.size() + m_aggs.size());
		child->m_values = parentNode->m_values;
		// ⭐ …AND WHICH BRANCH IT STANDS IN, inherited exactly as the keys above are. A branch is a
		// FACT ABOUT THE NODE — "the rows went this way to reach me" — and not a state of whoever is
		// walking: stamped only on the fork, a walk that had descended past it could no longer tell
		// whose nodes it was looking at, and every output printed every branch's headings (Max, live,
		// 2026-08-27: 125 rows of its own followed by 125 blank ones belonging to the other table).
		// (Outside a SPLIT there is no branch to inherit, and a new node's is already empty.)
		if (!parentNode->m_branch.empty())
			child->m_branch = parentNode->m_branch;
		for (std::size_t i = 0; i < level.m_fields.size() && i < key.size(); ++i)
			child->m_values[level.m_fields[i].m_col->GetColumnId()] = key[i];

		const std::size_t idx = m_pool.size();
		m_pool.emplace_back(child, NewAccs());
		// ⚠ RE-READ THE POOL ENTRY: the emplace above may have reallocated, and a reference taken
		// before it points into the old buffer.
		FoldNode& owner = m_pool[parent];
		if (across && !owner.m_acrossChildren) {
			if (!owner.m_acrossList) {
				owner.m_acrossList = std::make_unique<std::vector<std::size_t>>();
				owner.m_acrossList->reserve(4);   // a column per key: three for an employee, a few more above
			}
			if (owner.m_acrossList->size() < kAcrossListMax) {
				owner.m_acrossList->push_back(idx);
				return idx;
			}
			// …too many to walk: the list becomes the map it stood in for, and the map carries on — keyed
			// by what each cell says it is.
			owner.m_acrossChildren = std::make_unique<FoldNode::ChildMap>();
			for (const std::size_t cell : *owner.m_acrossList)
				owner.m_acrossChildren->emplace(KeyOfCell(cell, level), cell);
			owner.m_acrossList.reset();
		}
		std::unique_ptr<FoldNode::ChildMap>& opened = across ? owner.m_acrossChildren : owner.m_children;
		if (!opened)
			opened = std::make_unique<FoldNode::ChildMap>();
		const auto placed = opened->emplace(key, idx);
		if (!across) {
			owner.m_lastKey   = &placed.first->first;
			owner.m_lastChild = idx;
		}
		return idx;
	}

	// How many cells across one heading are found by walking them before they are put in a map.
	static constexpr std::size_t kAcrossListMax = 16;

	// DOES THIS CELL STAND FOR `key`? Asked of the cell itself: its key went into its own values, under
	// the level's columns, when Child opened it — and nothing is written over them until the fold is
	// finished. Compared value by value, exactly as the map compares whole keys (ibLevelKeyEqual).
	bool CellHasKey(std::size_t cell, const ibTotalLevel& level, const ibLevelKey& key) const
	{
		if (key.size() != level.m_fields.size())
			return false;   // Feed builds one value per field, so this is never the case — but it is not assumed
		const ibSelectorTree::Node* node = m_pool[cell].m_node;
		const ibValueEqual same;
		for (std::size_t i = 0; i < key.size(); ++i) {
			const ibValue* held = node->m_values.find_value(level.m_fields[i].m_col->GetColumnId());
			if (held == nullptr || !same(*held, key[i]))
				return false;
		}
		return true;
	}

	// …and the key itself, read back off the cell — for the rare heading whose cells outgrow the list and
	// move into the map, which does keep keys of its own.
	ibLevelKey KeyOfCell(std::size_t cell, const ibTotalLevel& level) const
	{
		const ibSelectorTree::Node* node = m_pool[cell].m_node;
		ibLevelKey key;
		key.reserve(level.m_fields.size());
		for (const ibTotalField& field : level.m_fields) {
			const ibValue* held = node->m_values.find_value(field.m_col->GetColumnId());
			key.push_back(held != nullptr ? *held : ibValue());
		}
		return key;
	}

	// ⭐ A DETAIL ROW IS A NODE LIKE ANY OTHER NOW — it is returned, because a table hangs its cells
	// under it. It still opens no map of children: what stands across it is opened by FeedAcross,
	// and nothing groups below a row.
	std::size_t AddDetail(std::size_t parent, const ibQueryRow& row, int childLevel, const Section& section)
	{
		ibSelectorTree::Node* parentNode = m_pool[parent].m_node;
		ibSelectorTree::Node* leaf       = parentNode->AddChild(childLevel);
		leaf->m_kind   = ibSelectorNodeKind::Detail;
		// ROOM FOR THE WHOLE ROW AT ONCE — the keys above, the row's columns and the figures filled below. Grown an entry
		// at a time, a five-column record moved its values over and over, for every record the source has (an eighth of
		// folding 103 thousand movements, Debug, 2026-11-16). Reserved before the copy, which then reuses the room.
		leaf->m_values.reserve(parentNode->m_values.size() + m_rowColumns.size() + m_aggs.size());
		leaf->m_values = parentNode->m_values;                 // the group keys above stay readable on the row
		leaf->m_branch = parentNode->m_branch;                 // …and whose branch it is read on (see Child)
		for (const ibQueryRamColumn& col : m_rowColumns)
			leaf->m_values[col.m_id] = row.Get(col.m_id);

		// ⚠ AND NOTHING IS ROLLED ON TOP OF THEM — a detail row's figure is the value it already
		// holds, which the loop above has just written; rolling COUNT over the single row would put a
		// 1 where the document's number belongs. EXCEPT where the row holds nothing to write: COUNT(*)
		// has no input column at all, so its receiver is filled here or comes back blank.
		for (std::size_t i = 0; i < m_aggs.size(); ++i) {
			if (RowCarries(m_aggs[i].m_col))
				continue;
			ibAggAcc one;
			one.Feed(m_aggs[i], row);
			leaf->m_values[AggSlotId(m_aggs, i)] = one.Result(m_aggs[i]);
		}

		// ⚠ IN THE POOL ONLY WHERE SOMETHING HANGS UNDER IT — a table's cells do, an ordinary
		// report's rows do not. A pool entry costs an accumulator per aggregate, and a report reads
		// as many detail rows as the source has: paying that per ROW is exactly the memory the
		// streaming fold exists to not spend (the arc's criterion is groups, not rows).
		if (section.m_across >= section.m_to)
			return kNoNode;
		const std::size_t idx = m_pool.size();
		m_pool.emplace_back(leaf, NewAccs());
		return idx;   // …and from here on it is a heading like any other — see Finish
	}

	bool RowCarries(const std::shared_ptr<const ibBackendQueryColumn>& col) const
	{
		if (col == nullptr)
			return false;
		for (const ibQueryRamColumn& c : m_rowColumns)
			if (c.m_id == col->GetColumnId())
				return true;
		return false;
	}

	const std::vector<ibTotalLevel>&                      m_levels;
	const std::vector<ibDataQueryBuilder::AggregateItem>& m_aggs;
	const std::vector<ibQueryRamColumn>&                  m_rowColumns;   // what a DETAIL row writes
	// ⚠ DEQUES — both grow a node at a time, 160 thousand of them for one payroll sheet, and a vector moved
	// every node and accumulator it held at each doubling (a node's maps and an accumulator's value moved
	// one by one). Indices survive either way; a deque also keeps a reference good while it grows.
	std::deque<FoldNode>                                  m_pool;
	std::deque<ibAggAcc>                                  m_accPool;      // every node's accumulators — see FoldNode
	// THE ROW BEING FED, as the fold needs it: each level's key and each aggregate's operand, read once
	// per row by Feed and handed to every node on its path. Kept between rows so their storage is reused.
	std::vector<ibLevelKey>                               m_rowKeys;
	std::vector<ibValue>                                  m_operands;
	std::vector<ibBalanceMark>                            m_marks;      // a balance's key and moment, per aggregate
	// THE LADDERS THIS FOLD BUILDS — one without `SPLIT`, and then one per branch. What used to be
	// three fields about "the levels" now belongs to each section, because with branches there is no
	// longer a single answer to "where do the columns start".
	std::vector<Section>                                  m_sections;
};

// --- PERIODS: the padding ---------------------------------------------------------------------
// A period nothing happened in has no row to come back, and that missing row is exactly what the
// reader is looking at. So after the fold — EITHER fold, ours or the DBMS's — the periodic levels
// are filled in: every unit between the bounds gets a node, and the ones the data produced stay
// where they are.
//
// It is a pass over NODES, not rows: a year by months adds at most twelve, and it costs the same
// whether they were folded here or on the server. Which is why it lives in one place and both roads
// end with it.

// The figure an EMPTY period reports. A sum or a count of nothing IS nought — that is the number a
// chart needs and the one a reader expects on a quiet month. A MIN / MAX / AVG of nothing is not a
// number at all, and writing 0 there would be inventing a smallest value that never existed.
ibValue EmptyPeriodFigure(const ibDataQueryBuilder::AggregateItem& a)
{
	using Fn = ibDataQueryBuilder::AggregateFn;
	if (a.m_balance != ibBalanceRole::None)
		return ibValue();   // a balance is not nought on a quiet month — it is carried (PadPeriodChildren)
	return (a.m_fn == Fn::Sum || a.m_fn == Fn::Count) ? ibValue(ibNumber(0L)) : ibValue();
}

// Fill in the missing periods among ONE node's children. The children carry the period in `id`;
// bounds that were written win, and where they were not the series runs between the first and the
// last period the data holds — the honest default, since with no bounds and no rows there is no
// series at all.
void PadPeriodChildren(ibSelectorTree::Node& parent, const ibTotalField& field, int childLevel,
                       const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	const ibMetaID id = field.m_col->GetColumnId();
	const ibTotalPeriods& periods = *field.m_periods;

	// ⚠ THE CHILDREN OF THIS LEVEL, and only those. A heading in a CROSS-TABLE stands over children
	// of two kinds — the sub-headings under it and the cells across it — and a cell knows nothing
	// about the period this level pads. Taken by their LEVEL, which is the one thing that says
	// which of them this call is about; without it the first cell answered "not a period level
	// after all" and the quiet months were silently never filled in.
	std::map<ibDateTime, ibSelectorTree::Node*> have;   // ordered by the moment itself
	std::vector<ibSelectorTree::Node*>          others; // …the ones this level does not speak for
	for (const std::unique_ptr<ibSelectorTree::Node>& child : parent.m_children) {
		if (child == nullptr) continue;
		if (child->m_level != childLevel) { others.push_back(child.get()); continue; }
		const auto it = child->m_values.find(id);
		if (it == child->m_values.end() || it->second.GetType() != TYPE_DATE) return;   // not a period level after all
		have[it->second.GetDate()] = child.get();
	}

	if (have.empty() && (periods.m_from.IsEmpty() || periods.m_to.IsEmpty()))
		return;                                        // nothing to pad between — and saying so is not a failure
	const ibDateTime from = !periods.m_from.IsEmpty() ? periods.m_from.GetDate().BeginOfPeriod(periods.m_unit)
	                                                  : have.begin()->first;
	const ibDateTime to   = !periods.m_to.IsEmpty()   ? periods.m_to.GetDate().BeginOfPeriod(periods.m_unit)
	                                                  : have.rbegin()->first;
	if (from > to)
		return;

	// ⚠ THE BOUNDS PAD, THEY DO NOT FILTER. A period OUTSIDE them that the data produced is still a
	// period that happened; dropping it here would turn a display setting into a silent WHERE.
	std::map<ibDateTime, ibSelectorTree::Node*> series = have;
	std::vector<std::unique_ptr<ibSelectorTree::Node>> made;
	for (ibDateTime t = from; t <= to; t = t.BeginOfNextPeriod(periods.m_unit)) {
		if (series.contains(t))
			continue;
		auto node = std::make_unique<ibSelectorTree::Node>();
		node->m_level  = childLevel;
		node->m_values = parent.m_values;              // the keys above stay readable, as on any node
		node->m_values[id] = ibValue(t);
		for (size_t i = 0; i < aggregates.size(); ++i)
			node->m_values[AggSlotId(aggregates, i)] = EmptyPeriodFigure(aggregates[i]);
		series[t] = node.get();
		made.push_back(std::move(node));
	}
	if (made.empty())
		return;

	// ⭐ …AND A BALANCE IS CARRIED INTO A QUIET PERIOD, never zeroed. Nothing moved there, so what stood at
	// its START is what stood at the start of the next period that has rows, and what stands at its END is
	// what stood at the end of the last one before it. Beyond the data on the far side the figure itself has
	// no neighbour to take it from (the other edge would — that is the pair of a balance), and it is left
	// empty there rather than invented.
	for (size_t i = 0; i < aggregates.size(); ++i) {
		const ibDataQueryBuilder::AggregateItem& a = aggregates[i];
		if (a.m_balance == ibBalanceRole::None)
			continue;
		const ibMetaID slot = AggSlotId(aggregates, i);
		const ibValue* carried = nullptr;
		const auto visit = [&](const ibDateTime& at, ibSelectorTree::Node* node) {
			if (have.contains(at)) {
				const auto it = node->m_values.find(slot);
				carried = it != node->m_values.end() ? &it->second : nullptr;
			}
			else
				node->m_values[slot] = carried != nullptr ? *carried : ibValue();
		};
		if (a.m_balance == ibBalanceRole::Opening)
			for (auto it = series.rbegin(); it != series.rend(); ++it) visit(it->first, it->second);
		else
			for (auto it = series.begin(); it != series.end(); ++it) visit(it->first, it->second);
	}

	// Rebuild the child list along the SERIES, keeping the direction the data came in: a report that
	// reads newest-first must not have its filled months arrive ascending in the middle of it.
	const bool descending = parent.m_children.size() > 1 && have.size() > 1
	                     && parent.m_children.front() != nullptr && parent.m_children.back() != nullptr
	                     && parent.m_children.front()->m_values.count(id) && parent.m_children.back()->m_values.count(id)
	                     && parent.m_children.front()->m_values.at(id).GetDate()
	                        > parent.m_children.back()->m_values.at(id).GetDate();

	std::map<ibSelectorTree::Node*, std::unique_ptr<ibSelectorTree::Node>> owned;
	for (std::unique_ptr<ibSelectorTree::Node>& child : parent.m_children)
		if (child != nullptr) { ibSelectorTree::Node* raw = child.get(); owned[raw] = std::move(child); }
	for (std::unique_ptr<ibSelectorTree::Node>& node : made) { ibSelectorTree::Node* raw = node.get(); owned[raw] = std::move(node); }

	parent.m_children.clear();
	parent.m_children.reserve(series.size() + others.size());
	auto take = [&owned, &parent](ibSelectorTree::Node* raw) {
		const auto it = owned.find(raw);
		if (it != owned.end() && it->second)          // an empty slot is not a row — see ibSelector::Next
			parent.m_children.push_back(std::move(it->second));
	};
	// ⚠ THE OTHERS KEEP THEIR PLACE AT THE FRONT — a heading's cells are written before its
	// sub-headings, and a pre-order reader is what that order is for.
	for (ibSelectorTree::Node* raw : others)
		take(raw);
	if (descending) for (auto it = series.rbegin(); it != series.rend(); ++it) take(it->second);
	else            for (auto it = series.begin();  it != series.end();  ++it) take(it->second);
	parent.m_hasChildren = !parent.m_children.empty();
}

// Walk to the nodes whose CHILDREN are the given level, and pad each one's children.
//
// ⚠ BY LEVEL, NOT BY DEPTH. They were the same number while a fold was one chain; in a CROSS-TABLE
// they are not — a column key hangs directly under whichever row heading it belongs to, so a node
// two steps down may be level 3. The node says which level it is, so that is what is asked.
void PadLevel(ibSelectorTree::Node& node, int levelIndex, const ibTotalField& field,
              const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	const int childLevel = levelIndex + 1;
	for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
		if (child != nullptr && child->m_level == childLevel) {
			PadPeriodChildren(node, field, childLevel, aggregates);
			return;   // this node's children ARE the level — nothing deeper here belongs to it
		}
	for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
		if (child != nullptr && child->m_kind != ibSelectorNodeKind::Detail)
			PadLevel(*child, levelIndex, field, aggregates);
}

// A HIERARCHY UNFOLD IS NOT A ONE-PASS FOLD. It arranges the level's VALUES into the target
// catalog's parent chain — a shape known only once every value has been seen, and one that hangs
// the rows themselves under a folder (Hierarchy). So those folds keep reading a whole table, and
// this is the question that says which road a set of levels takes.
bool LevelsUnfoldHierarchy(const std::vector<ibTotalLevel>& levels)
{
	for (const ibTotalLevel& level : levels)
		for (const ibTotalField& field : level.m_fields)
			if (field.m_dim != ibDimensionKind::Elements)
				return true;
	return false;
}

// ⭐⭐ …BUT IT NEEDS EVERY VALUE, NOT EVERY ROW. The unfold arranges the level's VALUES into the parent chain,
// and a heading's figures are its rows folded — so where no line of the sheet IS a row, the rows that agree on
// every key of every level can be folded into one first, while they are read, and the tree built over those.
// A report of a million movements by the goods hierarchy then holds one line per item and warehouse instead of
// the million (it drained them all until 2026-09-29).
//
// Exact only where each figure can be folded again from its parts, and where nothing but the keys and the
// figures is read off a row. So not when:
//   * a level lists the RECORDS (a level with no fields) — the rows are what is printed;
//   * a level is keyed by the ROW'S IDENTITY — its heading carries the row's own cells (AttachDimValue);
//   * a level is read BY PERIODS — padded from its figures by a pass of its own;
//   * an aggregate is DISTINCT (different values are not a sum of parts), has an AREA (OVER — folded over
//     nodes), is a BALANCE (a key's reading at its first or last moment — the rows folded first would have
//     lost the moment they stood at), or shares its slot with another one or with a key (the part would
//     overwrite what the other reads). AVG is folded from two parts — a sum in its slot, a count beside it —
//     and divided once the tree is built (FoldByKeysFirst's `averages`).
bool CanFoldByKeysFirst(const std::vector<ibTotalLevel>& levels,
	const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates, const std::shared_ptr<const ibBackendQueryable>& source)
{
	ibMetaID identity = 0;
	if (source != nullptr) {
		const std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys = source->GetPrimaryKeyColumns();
		if (keys.size() == 1 && keys.front() != nullptr)
			identity = keys.front()->GetColumnId();
	}
	std::vector<ibMetaID> taken;   // the key columns and the slots, each read by one thing only
	for (const ibTotalLevel& level : levels) {
		if (level.m_fields.empty())
			return false;
		for (const ibTotalField& field : level.m_fields) {
			if (field.m_col == nullptr || field.ByPeriods() || (identity != 0 && field.m_col->GetColumnId() == identity))
				return false;
			taken.push_back(field.m_col->GetColumnId());
		}
	}
	for (size_t i = 0; i < aggregates.size(); ++i) {
		const ibDataQueryBuilder::AggregateItem& a = aggregates[i];
		if (a.m_distinct || a.m_scopeDepth > 0 || a.m_balance != ibBalanceRole::None)
			return false;
		const ibMetaID slot = AggSlotId(aggregates, i);
		if (std::find(taken.begin(), taken.end(), slot) != taken.end())
			return false;
		taken.push_back(slot);
	}
	return true;
}

// The fold of CanFoldByKeysFirst: one row per distinct key tuple — the FIRST row read with it, so every group
// stands where its first row stood (the fold's rule), and a level's values come in the order they were first
// met — with each aggregate's PART in the aggregate's own slot. `rolled` folds those parts again: a count's
// parts are ADDED, a computed input is read from its slot; the slots do not move, so the tree's figures land
// exactly where the full fold puts them.
//
// ⭐ AN AVERAGE IS TWO PARTS: the sum in its slot and the count in a cell of its own beside it (an aggregate id
// past the last real one, declared as no column, so it never reaches the tree's columns). Both are added by
// the fold; `averages` names the pairs, and DivideAverages turns each into the average once the tree is built.
// `source` — what the rows are read from, whose application the parts' columns hold values of.
ibQueryRamTable FoldByKeysFirst(ibQueryRowCursor& rows, const std::vector<ibTotalLevel>& levels,
	const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates, const std::shared_ptr<const ibBackendQueryable>& source,
	std::vector<ibDataQueryBuilder::AggregateItem>& rolled,
	std::vector<std::pair<ibMetaID, ibMetaID>>& averages, long& read)
{
	const ibMetaData* const metaData = source != nullptr ? source->GetMetaData() : nullptr;
	ibQueryRamTable table;
	for (const ibQueryRamColumn& c : rows.Columns())
		table.AddColumn(c.m_id, c.m_name, c.m_type, c.m_caption);

	rolled = aggregates;
	for (size_t i = 0; i < aggregates.size(); ++i) {
		ibDataQueryBuilder::AggregateItem& a = rolled[i];
		if (AggNeedsOwnSlot(aggregates[i])) {
			// A COUNT(*) or a computed input has no column to fold in place: its part gets a column under the
			// slot's own id, so the slot is the same one whichever fold wrote it.
			const ibMetaID slot = AggSlotId(aggregates, i);
			table.AddColumn(slot, aggregates[i].m_alias, ibTypeDescription());
			a.m_col = std::make_shared<ibSubqueryExprColumn>(aggregates[i].m_alias, slot, metaData);
		}
		a.m_expr.reset();
		if (a.m_fn == ibDataQueryBuilder::AggregateFn::Count)
			a.m_fn = ibDataQueryBuilder::AggregateFn::Sum;
	}
	std::vector<size_t> averaged;   // which aggregates are averages, in the order their counts were numbered
	for (size_t i = 0; i < aggregates.size(); ++i) {
		if (aggregates[i].m_fn != ibDataQueryBuilder::AggregateFn::Avg)
			continue;
		const ibMetaID counted = ibSynthId(ibSynthKind::Aggregate, static_cast<ibMetaID>(aggregates.size() + averaged.size()));
		rolled[i].m_fn = ibDataQueryBuilder::AggregateFn::Sum;   // the sums of the parts…
		ibDataQueryBuilder::AggregateItem count{ ibDataQueryBuilder::AggregateFn::Sum,
		                                         std::make_shared<ibSubqueryExprColumn>(ibString(), counted, metaData), ibString() };
		rolled.push_back(count);                                  // …and their counts, beside them
		averages.emplace_back(AggSlotId(aggregates, i), counted);
		averaged.push_back(i);
	}

	std::vector<ibMetaID> keyIds;
	for (const ibTotalLevel& level : levels)
		for (const ibTotalField& field : level.m_fields)
			if (std::find(keyIds.begin(), keyIds.end(), field.m_col->GetColumnId()) == keyIds.end())
				keyIds.push_back(field.m_col->GetColumnId());

	struct KeyHash {
		size_t operator()(const std::vector<ibValue>& key) const {
			std::uint64_t h = kIbHashBasis;
			for (const ibValue& v : key)
				h = ibHashCombine(h, static_cast<std::uint64_t>(ibValueHash()(v)));
			return static_cast<size_t>(h);
		}
	};
	struct KeyEqual {
		bool operator()(const std::vector<ibValue>& a, const std::vector<ibValue>& b) const {
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (!ibValueEqual()(a[i], b[i]))
					return false;
			return true;
		}
	};
	struct Group { long m_row; std::vector<ibAggAcc> m_parts; };
	std::unordered_map<std::vector<ibValue>, size_t, KeyHash, KeyEqual> index;
	std::vector<Group> groups;

	while (rows.Next()) {
		++read;
		std::vector<ibValue> key;
		key.reserve(keyIds.size());
		for (const ibMetaID id : keyIds)
			key.push_back(rows.Get(id));
		const auto [found, inserted] = index.try_emplace(std::move(key), groups.size());
		if (inserted) {
			const long r = table.AppendRow();
			for (const ibQueryRamColumn& c : rows.Columns())
				table.SetCell(r, c.m_id, rows.Get(c.m_id));
			groups.push_back(Group{ r, std::vector<ibAggAcc>(aggregates.size()) });
		}
		const size_t g = found->second;
		for (size_t i = 0; i < aggregates.size(); ++i)
			groups[g].m_parts[i].Feed(aggregates[i], rows);
	}

	for (const Group& group : groups) {
		for (size_t i = 0; i < aggregates.size(); ++i)
			if (aggregates[i].m_fn != ibDataQueryBuilder::AggregateFn::Avg)
				table.SetCell(group.m_row, AggSlotId(aggregates, i), group.m_parts[i].Result(aggregates[i]));
		for (size_t k = 0; k < averaged.size(); ++k) {
			const ibAggAcc& part = group.m_parts[averaged[k]];
			table.SetCell(group.m_row, averages[k].first, part.m_n > 0 ? ibValue(part.m_sum) : RamNullValue());
			table.SetCell(group.m_row, averages[k].second, ibValue(ibNumber(part.m_n)));
		}
	}
	return table;
}

// …and each average made from its two parts, on every heading of the finished tree: the sum over the count,
// or NULL where nothing was counted — what an average of no values is (ibAggAcc::Result). The count's cell is
// taken away again; it was never a figure of the report.
void DivideAverages(ibSelectorTree::Node& node, const std::vector<std::pair<ibMetaID, ibMetaID>>& averages)
{
	for (const std::pair<ibMetaID, ibMetaID>& average : averages) {
		const auto counted = node.m_values.find(average.second);
		if (counted == node.m_values.end())
			continue;
		const auto summed = node.m_values.find(average.first);
		const bool any = counted->second > ibValue(ibNumber(0));
		const ibValue value = (any && summed != node.m_values.end() && !RamIsNullValue(summed->second))
			? ibValue(summed->second.GetNumber() / counted->second.GetNumber())
			: RamNullValue();
		node.m_values.erase(average.second);
		node.m_values[average.first] = value;
	}
	for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
		if (child != nullptr)
			DivideAverages(*child, averages);
}

} // namespace ibProvider

// ⭐⭐ FILL IN THE PERIODS NOBODY REPORTED — the second half of what `BY … PERIODS(unit, …)` means.
//
// Both folds end here, and they must: the DBMS returns the periods it HAS, and so does the RAM fold;
// neither can invent the quiet month, because neither is looking at a calendar. This is the pass
// that does, and it works on the finished tree, so there is exactly one statement of what padding
// means whichever road produced the tree.
void ibQueryComposer::PadPeriodLevels(ibSelectorTree& tree, const std::vector<ibTotalLevel>& levels,
                                      const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	for (size_t li = 0; li < levels.size(); ++li) {
		// ONE FIELD, because a period is a scale and a tuple has none: `BY (Period, Warehouse)
		// PERIODS(Month)` would ask for the months of each warehouse, which is a different report
		// (and one the author can write as two levels). Refused where it is written; ignored here.
		if (!levels[li].IsSingleField() || !levels[li].m_fields.front().ByPeriods())
			continue;
		PadLevel(tree.Root(), static_cast<int>(li), levels[li].m_fields.front(), aggregates);
	}
}

// ⭐ A FIGURE OVER FIGURES — `SUM(Accrued) - SUM(Deducted) AS ToPay`. Every road folds its own
// figures (the DBMS, the stream, the snapshot, the hierarchy), and the expression between them is
// computed here, on the finished tree, so there is one statement of it whichever road built the tree.
void ApplyDerivedAggregates(ibSelectorTree& tree, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
                            const std::vector<ibDerivedAggregate>& derived)
{
	for (size_t j = 0; j < derived.size(); ++j) {
		// The selected field's own column — or a slot past every aggregate's own, of the same kind, so it is clear
		// of every other minted id, as theirs are.
		const std::shared_ptr<const ibBackendQueryColumn>& field = derived[j].m_col;
		const ibMetaID slot = field != nullptr ? field->GetColumnId()
			: ibSynthId(ibSynthKind::Aggregate, static_cast<ibMetaID>(aggregates.size() + j));
		// Declared once: a selection builds again on a tree that already has it (a shared read's branches).
		const std::vector<ibQueryRamColumn>& columns = tree.Columns();
		if (std::none_of(columns.begin(), columns.end(), [slot](const ibQueryRamColumn& c) { return c.m_id == slot; }))
			tree.AddColumn(slot, derived[j].m_alias, field != nullptr ? field->GetTypeDesc() : ibTypeDescription());

		// ⚠ EVERY HEADING, a padded period's included: a figure its calls do not reach there (erased above an area)
		// reads as NULL, which is what the evaluator makes of a missing operand — no figure, rather than a wrong one.
		// A fork carries none, and neither does a DETAIL ROW: its calls' slots hold the row's own values, not folds
		// of one row (`COUNT(Number)` there is the number, ibStreamingFold::AddDetail), so an expression over them
		// would be one over readings. The row shows the selected field's own value, or nothing.
		const std::function<void(ibSelectorTree::Node&)> walk = [&](ibSelectorTree::Node& node) {
			if (node.m_kind == ibSelectorNodeKind::Group)
				node.m_values[slot] = EvalColumnExprRow(derived[j].m_expr, ibTreeNodeRow(node, aggregates));
			for (const std::unique_ptr<ibSelectorTree::Node>& child : node.m_children)
				if (child != nullptr)
					walk(*child);
		};
		walk(tree.Root());
	}
}

// totals fold over a combined row set -> a RAW totals TREE (L3's own ibQueryRamTable). L3
// stops here — flat-vs-hierarchical rendering is the runtime's call. Group columns keyed by
// GetColumnId; aggregates by a synthetic id (read by alias). Grand total = the root (level
// 0). Pure (no DB) — exposed so the fold is unit-testable directly. (docs §22.1b)
//
// ⭐ IT TAKES A CURSOR. One pass, no table: every group column is a one-field level, so this is the
// streaming fold with the levels spelled the flat way. (The snapshot overload below is the same
// call with the table handed over as a cursor — see queryRowCursor.h.)
ibSelectorTree BuildTotalsTree(ibQueryRowCursor& rows,
		const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& groupFields,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	ibSelectorTree tree;
	for (const std::shared_ptr<const ibBackendQueryColumn>& g : groupFields)
		tree.AddColumn(g->GetColumnId(), g->GetName(), g->GetTypeDesc());
	for (const ibDataQueryBuilder::AggregateItem& a : aggregates)   // aggregate column = its OWN source column
		if (a.m_col != nullptr) tree.AddColumn(a.m_col->GetColumnId(), a.m_col->GetName(), a.m_col->GetTypeDesc());
	AddSyntheticAggColumns(tree, aggregates);                       // + COUNT(*) receivers

	std::vector<ibTotalLevel> levels;
	levels.reserve(groupFields.size());
	for (const std::shared_ptr<const ibBackendQueryColumn>& g : groupFields)
		levels.push_back(ibTotalLevel::One(g, ibDimensionKind::Elements));

	ibStreamingFold fold(tree, levels, aggregates, rows.Columns());
	long read = 0;
	while (rows.Next()) { fold.Feed(rows); ++read; }
	fold.Finish();
	ibJournalInfo(ibT("query.road"), ibT("STREAM: totals folded %ld rows into %ld nodes (%u levels)"),
	              read, fold.NodeCount(), static_cast<unsigned>(levels.size()));
	return tree;   // raw — the runtime decides flat vs. hierarchical
}

ibSelectorTree BuildTotalsTree(const ibQueryRamTable& detail,
		const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& groupFields,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates)
{
	ibRamTableCursor rows(detail);
	return BuildTotalsTree(rows, groupFields, aggregates);
}

// RECURSIVE hierarchy fold (parent-ref). From a WHOLE materialised detail table -> the Node tree the
// runtime already consumes. The sibling of BuildTotalsTree (fixed columns); here the grouping is by a
// row's parent-ref recursively, unbounded depth. m_hasChildren comes from the data; each node's
// subtotal rolls over its subtree. `mode` = the placement (Elements / Hierarchy / HierarchyOnly).
ibSelectorTree BuildHierarchyTree(const ibQueryRamTable& detail,
		const std::shared_ptr<const ibBackendQueryColumn>& rowKeyCol, const std::shared_ptr<const ibBackendQueryColumn>& parentKeyCol,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates, ibDimensionKind mode)
{
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : detail.Columns())   // aggregates roll IN-PLACE into their own columns
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);              // + COUNT(*) receivers

	const long n = detail.RowCount();

	// No row key or parent key: there is no tree. Select(ByGroups) on a query without
	// TOTALS arrives here with both null, and reading them is a null dereference.
	// The records themselves are the walk — the Elements placement, which does not
	// use the keys. A by-groups walk of a result that named no grouping reads like
	// the direct walk.
	if (rowKeyCol == nullptr || parentKeyCol == nullptr)
		mode = ibDimensionKind::Elements;

	// Elements: every row as a leaf NODE under the root, in order, no nesting. A grand total (if
	// aggregates) on the root. (The tree carries no flat rows — a leaf is a one-level node.)
	if (mode == ibDimensionKind::Elements) {
		std::vector<long> all; all.reserve(static_cast<size_t>(n));
		for (long r = 0; r < n; ++r) {
			ibSelectorTree::Node* leaf = tree.Root().AddChild(1);
			for (const ibQueryRamColumn& col : detail.Columns())
				leaf->m_values[col.m_id] = detail.GetCell(r, col.m_id);
			all.push_back(r);
		}
		ApplyAggregates(tree.Root(), detail, all, aggregates);   // grand total in-place
		tree.Root().m_hasChildren = !tree.Root().m_children.empty();
		return tree;
	}

	// Recursive nest by parent-ref: index children by parent key, identify roots (no / unknown parent).
	ibRowsByValue childrenOf;
	std::unordered_map<ibValue, long, ibValueHash, ibValueEqual> keyToRow;
	for (long r = 0; r < n; ++r) {
		keyToRow[detail.GetCell(r, rowKeyCol->GetColumnId())] = r;
		childrenOf[detail.GetCell(r, parentKeyCol->GetColumnId())].push_back(r);
	}
	std::vector<long> roots;
	for (long r = 0; r < n; ++r) {
		const ibValue pk = detail.GetCell(r, parentKeyCol->GetColumnId());
		if (IsNoKey(pk) || !keyToRow.contains(pk)) roots.push_back(r);
	}

	std::vector<bool> visited(static_cast<size_t>(n), false);
	HierBuildCtx ctx{ &detail, rowKeyCol, &childrenOf, &aggregates, mode, &visited };
	std::vector<long> allRows;
	for (long rootIdx : roots) {
		std::vector<long> rows = AttachHierNode(ctx, &tree.Root(), rootIdx, 1);
		allRows.insert(allRows.end(), rows.begin(), rows.end());
	}
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), detail, allRows, aggregates);   // grand total in-place
	return tree;
}

namespace ibProvider {

// Context for the recursive reference-value hierarchy fold (invariant across the recursion).
// Indexed by the VALUE itself (ibValueHash, value.h). Keying by a rendered string cost a text
// conversion per row — and needed a THIRD map, `valOf`, whose only job was to give the value back
// once you had the string. The value is the key now, so there is nothing to give back.

struct RefHierCtx {
	const ibQueryRamTable*                                detail;
	std::shared_ptr<const ibBackendQueryColumn>           refCol;
	const ibRowsByValue*                                  rowsByVal;    // value -> snapshot rows carrying it
	const ibRefChildren*                                  childrenOf;   // value -> child values (target parent-map)
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDimensionKind                                       dim;
};

// Attach the value-subtree rooted at `valueKey` under `parent`; returns the subtree's snapshot rows
// (for the parent's subtotal). A node = one target-catalog value (folder), carrying the rows whose
// refCol == this value + (recursively) descendant values' rows. Cycle-guarded.
std::vector<long> AttachRefNode(const RefHierCtx& ctx, ibSelectorTree::Node* parent, const ibValue& valueKey,
                                int level, ibValueSeen& visited)
{
	if (IsNoKey(valueKey) || visited[valueKey]) return {};
	visited[valueKey] = 1;

	const auto cit = ctx.childrenOf->find(valueKey);
	const bool hasKids = (cit != ctx.childrenOf->end() && !cit->second.empty());

	std::vector<long> ownRows;
	const auto rit = ctx.rowsByVal->find(valueKey);
	if (rit != ctx.rowsByVal->end()) ownRows = rit->second;   // this value's own rows

	// ⭐⭐ HIERARCHYONLY: A VALUE WITH NOTHING BELOW IT IS NOT A LEVEL. The two readings, stated by
	// the person who owns the word (Max, 2026-09-04): `TOTALS BY Ref HIERARCHY` adds the element's
	// hierarchy INCLUDING the element itself, and it appears in the detail rows as well;
	// `HIERARCHYONLY` adds those rows NOT including the element — it is in the detail rows only.
	//
	// 🛑 ONE WORD, TWO FUNCTIONS, TWO MEANINGS — which is why the modes looked identical. The other
	// builder of this same tree already reads it right (AttachHierNode: "a leaf element: not a node,
	// but counts upward"); here HierarchyOnly only withheld the DETAIL rows and still made a node out
	// of every value, so `TOTALS BY Goods HIERARCHYONLY` came back indistinguishable from HIERARCHY
	// (measured 2026-09-03). The rows still count upward — the subtotal is over the subtree, not over
	// the nodes drawn from it.
	const bool namesNothingBelow = (ctx.dim == ibDimensionKind::HierarchyOnly && !hasKids);
	if (namesNothingBelow)
		return ownRows;

	ibSelectorTree::Node* node = parent->AddChild(level);
	node->m_values[ctx.refCol->GetColumnId()] = valueKey;   // the key IS the value the node shows

	std::vector<long> subtree = ownRows;
	if (hasKids)
		for (const ibValue& childKey : cit->second) {
			std::vector<long> kr = AttachRefNode(ctx, node, childKey, level + 1, visited);
			subtree.insert(subtree.end(), kr.begin(), kr.end());
		}
	// Hierarchy: the value's own detail rows hang under it as leaf elements; HierarchyOnly omits them
	// (folders only). Either way the subtotal counts them.
	if (ctx.dim == ibDimensionKind::Hierarchy)
		for (long r : ownRows) {
			ibSelectorTree::Node* leaf = node->AddChild(level + 1);
			for (const ibQueryRamColumn& col : ctx.detail->Columns())
				leaf->m_values[col.m_id] = ctx.detail->GetCell(r, col.m_id);
		}
	node->m_hasChildren = hasKids || (ctx.dim == ibDimensionKind::Hierarchy && !ownRows.empty());
	ApplyAggregates(*node, *ctx.detail, subtree, *ctx.aggregates);   // subtotal over the value-subtree, in-place
	return subtree;
}

} // namespace ibProvider

ibSelectorTree ibQueryComposer::BuildReferenceHierarchy(const ibQueryRamTable& snapshot,
		const std::shared_ptr<const ibBackendQueryColumn>& refCol, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
		ibDatabaseConnectionHolder* holder, const std::shared_ptr<const ibBackendQueryable>& source, ibDimensionKind dim)
{
	// Resolve the target catalog of the reference field + its hierarchy keys.
	std::shared_ptr<const ibBackendQueryable> target = (source != nullptr) ? source->GetProvider().ResolveReferenceTarget(source, refCol) : nullptr;
	std::shared_ptr<const ibBackendQueryColumn> tRowKey = (target != nullptr && !target->GetPrimaryKeyColumns().empty())
	                                       ? target->GetPrimaryKeyColumns().front() : nullptr;
	std::shared_ptr<const ibBackendQueryColumn> tParent = (target != nullptr) ? target->GetHierarchyColumn() : nullptr;

	// No target / no holder / no parent column → flat group by the value (degrade, not crash).
	if (target == nullptr || holder == nullptr || tRowKey == nullptr || tParent == nullptr)
		return BuildTotalsTree(snapshot, { refCol }, aggregates);

	// The target's tree (value -> parent value, and each one's place in its kind's order), through the door.
	const ibValueHierarchy hierarchy = ReadValueHierarchy(target, holder);

	// Index the snapshot rows by the refCol value; build the value->children map from the tree.
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : snapshot.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);              // + COUNT(*) receivers

	// `valOf` is gone: it existed only to map a rendered key back to the value it came from, and the
	// value is the key now.
	// …the values in the order their rows came, which the map does not keep (see ibArrangeUnderParents).
	ibRowsByValue rowsByVal;
	std::vector<ibValue> keyOrder;
	const long n = snapshot.RowCount();
	for (long r = 0; r < n; ++r) {
		const ibValue key = snapshot.GetCell(r, refCol->GetColumnId());
		const auto [it, inserted] = rowsByVal.try_emplace(key);
		if (inserted)
			keyOrder.push_back(key);
		it->second.push_back(r);
	}

	// Among VALUES PRESENT (+ their ancestors), each where its row stood.
	ibRefChildren childrenOf = ibArrangeUnderParents(keyOrder, hierarchy,
		[&rowsByVal](const ibValue& key) { return rowsByVal.contains(key); });

	RefHierCtx ctx{ &snapshot, refCol, &rowsByVal, &childrenOf, &aggregates, dim };
	ibValueSeen visited;
	std::vector<long> allRows;
	const auto rit = childrenOf.find(ibValue());   // roots = values whose parent is empty/unknown
	if (rit != childrenOf.end())
		for (const ibValue& rootKey : rit->second) {
			std::vector<long> rows = AttachRefNode(ctx, &tree.Root(), rootKey, 1, visited);
			allRows.insert(allRows.end(), rows.begin(), rows.end());
		}
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), snapshot, allRows, aggregates);   // grand total in-place
	return tree;
}

// === GENERAL multi-level dimension combiner =================================================
namespace ibProvider {

struct DimCtx {
	const ibQueryRamTable*                                snapshot;
	const std::vector<ibTotalLevel>*                      levels;
	const std::vector<ibDataQueryBuilder::AggregateItem>* aggregates;
	ibDatabaseConnectionHolder*                           holder;
	std::shared_ptr<const ibBackendQueryable>             source;
	// WHERE THE PAGE STOPS AND THE PAGE-WIDTH STARTS — the same seam the streaming fold reads off
	// the levels (ibStreamingFold). This road is taken when a level unfolds a HIERARCHY, and a
	// cross-table grouped by a hierarchy is still a cross-table.
	size_t                                                across = 0;
	bool                                                  details = false;   // the last level is the rows
	// ⭐ THE COLUMN THAT IS THE ROW'S IDENTITY — the source's reference key, 0 when it has none (a RAM
	// table, a join). A level grouped BY it holds one row per group by construction, so its headings are
	// the rows themselves; see AttachDimValue. Read once, here, rather than per node.
	ibMetaID                                              identity = 0;
	// ⭐ …AND THE TREES, ONE PER LEVEL FIELD, READ ONCE PER FOLD (HierarchyForField). A hierarchy level under another
	// is folded once per heading above it, and every one of those folds read its catalog's whole tree again — all the
	// goods, for each warehouse.
	mutable std::map<std::shared_ptr<const ibBackendQueryColumn>, ibValueHierarchy> trees;
};

// The tree a level field's values stand in (ReadValueHierarchy): the target catalog of the reference field (cross),
// or the source itself when the field is the source's OWN parent column (self) — read once per fold (DimCtx::trees).
// Empty when there is no hierarchy target / no holder.
const ibValueHierarchy& HierarchyForField(const DimCtx& ctx, const std::shared_ptr<const ibBackendQueryColumn>& field)
{
	const auto known = ctx.trees.find(field);
	if (known != ctx.trees.end())
		return known->second;
	std::shared_ptr<const ibBackendQueryable> target = (ctx.source != nullptr) ? ctx.source->GetProvider().ResolveReferenceTarget(ctx.source, field) : nullptr;
	if (target == nullptr && ctx.source != nullptr && field == ctx.source->GetHierarchyColumn())
		target = ctx.source;
	return ctx.trees.emplace(field, ReadValueHierarchy(target, ctx.holder)).first->second;
}

// `across` — this call is building the COLUMN branch of a cross-table: the levels past the seam,
// hung under one row heading. Everything else is the ordinary walk down the page.
// ⭐ `branch` — WHICH LADDER THIS DESCENT IS ON. Null is the common one, which is every report
// without `SPLIT`. Carried rather than read off the node, because a branch has an IDENTITY (the
// shared ibTotalBranch) and two nameless ones must not fold into each other.
// ⭐ `depth` — THE NUMBER THIS LEVEL STANDS AT, carried rather than derived from `levelIdx`. Depth is
// counted WITHIN THE LADDER (the streaming fold says the same in Section::DepthOf): two branches
// begin in the same place, so their first levels must carry the same number — computed from the flat
// list, the second branch printed one rung deeper for no reason a reader could see.
void FoldDimLevel(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows, size_t levelIdx,
                  bool across = false, const ibTotalBranch* branch = nullptr, int depth = 1, int indent = 0);

// ⭐ THE CELLS OF ONE HEADING — the column axis, folded under the heading that is being built. Called
// for EVERY row heading rather than only the deepest, because an outer heading's cells are a fold
// over a SUBSET of the keys (its own prefix and the columns), and a subset is not a prefix of the
// nesting order — no single chain can carry it. Same rule, same reason as ibStreamingFold::FeedAcross.
void FoldAcross(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows)
{
	if (ctx.across < ctx.levels->size())
		FoldDimLevel(ctx, node, rows, ctx.across, /*across*/true);
}

// Attach one value of a Hierarchy level under `parent`: sub-values (hierarchy depth) + the NEXT level
// inside this value's own rows (Hierarchy only). Returns the value-subtree's rows (for the subtotal).
std::vector<long> AttachDimValue(const DimCtx& ctx, ibSelectorTree::Node* parent, size_t levelIdx,
	const ibValue& valueKey,
	const ibRowsByValue& byVal,
	const ibRefChildren& childrenOf, ibValueSeen& visited, bool across = false,
	const ibTotalBranch* branch = nullptr, int depth = 1, int indent = 0)
{
	if (IsNoKey(valueKey) || visited[valueKey]) return {};
	visited[valueKey] = 1;
	const ibTotalLevel& level = (*ctx.levels)[levelIdx];

	// ⭐⭐ HIERARCHYONLY: THE HIERARCHY WITHOUT THE VALUE THAT NAMES IT. Stated by the person who owns
	// the word (Max, 2026-09-04): `TOTALS BY Ref HIERARCHY` adds the element's hierarchy INCLUDING
	// the element itself, and it also stands in the detail records; `HIERARCHYONLY` adds those rows
	// NOT including the element — it appears in the detail records only.
	//
	// So a value with nothing under it is not a rung here: no node, and its rows travel up to the
	// nearest folder that IS one, where they are read as that folder's detail records. A value that
	// names something below it stays a node in both modes — it is a folder, which is what the word
	// keeps.
	//
	// 🛑 THE MODE WAS NOT ASKED AT ALL ON THIS ROAD, so `TOTALS BY Goods HIERARCHYONLY` and `… HIERARCHY`
	// answered identically (measured 2026-09-03) — and the other builder of the same tree had read it
	// right all along (AttachHierNode: "a leaf element: not a node, but counts upward"). One word, two
	// functions, two meanings.
	const auto kidsIt = childrenOf.find(valueKey);
	const bool hasKids = (kidsIt != childrenOf.end() && !kidsIt->second.empty());
	const bool foldersOnly = (level.HeadDim() == ibDimensionKind::HierarchyOnly);

	if (foldersOnly && !hasKids) {
		// ⚠ THE ROWS ARE RETURNED, NEVER DROPPED — the caller hangs them on the folder above (or, at
		// the root, on the level's own remainder). A fold's contract is that every row it was given is
		// somewhere in the tree it returns; see the note on `orphaned` below for what losing them costs.
		const auto own = byVal.find(valueKey);
		return own != byVal.end() ? own->second : std::vector<long>();
	}

	// ⚠ THE LEVEL'S NUMBER, not the depth under the parent: a hierarchy nests VALUES inside one
	// level, so every folder and every item here belongs to the same level and must say so.
	ibSelectorTree::Node* node = parent->AddChild(depth);
	node->m_indent = indent;             // …and how far inside that level it stands — the hierarchy step
	node->m_values = parent->m_values;   // inherit the grouping fields available from the levels above (same rule as the Elements branch)
	node->m_branch = parent->m_branch;   // …and the branch it stands in, inherited exactly as the keys are
	// A hierarchy level is single-field by construction (FoldDimLevel routes it here only then), so
	// its key is the head field's value.
	node->m_values[level.HeadCol()->GetColumnId()] = valueKey;   // the key IS the value

	std::vector<long> ownRows;
	const auto rit = byVal.find(valueKey);
	if (rit != byVal.end()) ownRows = rit->second;

	// ⭐⭐ A LEVEL KEYED BY THE ROW'S OWN IDENTITY **IS** THE ROW, so the node carries the row's fields.
	// Grouping a catalog by its Reference and unfolding the hierarchy is how a TREE is written — the
	// shape of the list on the screen — and there every line is an element, drawn at its own depth. The
	// key determines the row completely (that is what an identity is), so every column it was read with
	// belongs on this node just as it would on a detail record.
	//
	// 🛑 Without it a tree could only be printed by grouping over the PARENT, and that reading loses
	// exactly what it is asked for: a folder holding no elements is nobody's parent and never appears, a
	// folder's own Code and Description are not the key and print blank, and every element needs a second
	// node under its own heading (Max, 2026-08-29: *"the hierarchy has to be output over the Reference"*).
	//
	// Asked of the SOURCE, not of the row count: "this level's key is the identity" is a fact about the
	// query, true for every node of the level at once. A group that merely happens to hold one row is a
	// different thing entirely and is left alone — a field that appears only where a group came out
	// single would be worse than one that never appears.
	//
	// ⚠ AND A NODE WITH NO ROW OF ITS OWN IS CLEARED, not left inheriting. A folder is pulled into the
	// tree by `chainUp` whenever something UNDER it matched, so under a filter it can stand there with no
	// row behind it — and the values it inherited from its parent are that parent's, not a blank. Left
	// alone it would print its parent's Code and Description as if they were its own, which is worse than
	// printing nothing: a wrong line reads as data.
	if (ctx.identity != 0 && ctx.identity == level.HeadCol()->GetColumnId()) {
		for (const ibQueryRamColumn& col : ctx.snapshot->Columns())
			node->m_values[col.m_id] = ownRows.empty()
				? ibValue()
				: ctx.snapshot->GetCell(ownRows.front(), col.m_id);
		node->m_values[level.HeadCol()->GetColumnId()] = valueKey;   // …its own key survives either way
	}

	std::vector<long> subtree = ownRows;
	// …and the rows this node shows as its OWN detail records. They are its own rows, plus — under
	// HIERARCHYONLY — the rows of every element that was folded into it instead of becoming a node.
	// That is where "it appears in the detail records only" is actually delivered.
	std::vector<long> detailRows = ownRows;
	const auto cit = childrenOf.find(valueKey);          // sub-values (deeper in the catalog hierarchy)
	if (cit != childrenOf.end())
		for (const ibValue& childKey : cit->second) {
			// ⭐ A SUB-VALUE KEEPS THE RUNG AND TAKES ONE MORE STEP IN. The level is the ladder's and does
			// not move; the indentation is the tree's and does (Max, 2026-08-29: *"if there is a sub-folder
			// it has to add one"*).
			const auto grandKids = childrenOf.find(childKey);
			const bool childIsAFolder = (grandKids != childrenOf.end() && !grandKids->second.empty());
			std::vector<long> kr = AttachDimValue(ctx, node, levelIdx, childKey, byVal, childrenOf, visited, across, branch, depth, indent + 1);
			subtree.insert(subtree.end(), kr.begin(), kr.end());
			if (foldersOnly && !childIsAFolder)
				detailRows.insert(detailRows.end(), kr.begin(), kr.end());   // no node of its own — its rows are ours
		}
	// A hierarchy nests VALUES inside ONE level, so a sub-value keeps this level's depth; the NEXT
	// level goes one deeper — and stands where this value stands, not where the level began.
	//
	// ⭐⭐ …AND THE NEXT LEVEL GOES INSIDE THIS VALUE'S OWN ROWS — EXCEPT THE RECORDS, UNDER A RUNG
	// WHOSE KEY IS THE ROW ITSELF.
	//
	// 🛑 BOTH WIDER RULES WERE TRIED AND BOTH WERE WRONG, which is why the narrow one is written out:
	//   * stop the whole descent — the second grouping a person set then vanished from the sheet while
	//     the list still reached it by drilling (*"the groupings work in the list, the report does not
	//     see them"*);
	//   * stop nothing — a tree over the reference then printed every element TWICE, bold heading and
	//     plain row, one under the other (*"the hierarchy doubles"*).
	// What is degenerate is only the RECORDS level: this node already IS that row, fields and all. A
	// further GROUPING is a different question and is folded as asked.
	const bool keyIsTheRow = ctx.identity != 0 && level.HeadCol() != nullptr
		&& ctx.identity == level.HeadCol()->GetColumnId();
	const bool nextIsDetails = (levelIdx + 1 >= ctx.levels->size())
		|| (*ctx.levels)[levelIdx + 1].m_fields.empty();
	if (!(keyIsTheRow && nextIsDetails))
		FoldDimLevel(ctx, node, detailRows, levelIdx + 1, across, branch, depth + 1, indent);   // + elements folded in (HIERARCHYONLY)

	// ⭐ AND THE CELLS OF THIS HEADING — over its WHOLE subtree, which is what its own figures are
	// computed from (ApplyAggregates below reads the same rows). A folder of a hierarchy stands for
	// everything under it, so its row of cells has to say the same thing its total does.
	//
	// Built last and inserted FIRST (FoldDimLevel's across pass inserts rather than appends): the
	// subtree is not known until the children have been walked, while a pre-order reader needs the
	// cells before the sub-headings to tell whose they are.
	if (!across)
		FoldAcross(ctx, node, subtree);
	node->m_hasChildren = !node->m_children.empty();
	ApplyAggregates(*node, *ctx.snapshot, subtree, *ctx.aggregates);
	return subtree;
}

// Fold `rows` at `levelIdx` under `node`: group by the level field, then either flat groups (Elements)
// or the field's reference hierarchy (Hierarchy/HierarchyOnly). Recurses the next level inside.
void FoldDimLevel(const DimCtx& ctx, ibSelectorTree::Node* node, const std::vector<long>& rows, size_t levelIdx,
                  bool across, const ibTotalBranch* branch, int depth, int indent)
{
	if (levelIdx >= ctx.levels->size()) return;          // leaf level reached

	// ⭐⭐ WHERE THE LADDER FORKS — the snapshot road's half of `SPLIT`.
	//
	// 🛑 THE TWO ROADS MUST FOLD THE SAME SHAPE (the note over BuildDimensionTree says so), and for a
	// while only one of them knew about branches: the streaming fold cuts the levels into sections
	// and opens a fork per section, while this one walked the flat list and quietly folded every
	// branch as one deeper ladder. It is not only a test's problem — this road is the one a query
	// takes whenever a level UNFOLDS A HIERARCHY, so a report that split its totals and grouped by a
	// hierarchy lost its branches with nothing said (found by CI, 2026-08-27).
	//
	// The rule is the fold's, not the walk's: levels of one branch stand together, a fork carries no
	// key and spends no level, and every branch that starts here is fed THE SAME rows.
	if (!across) {
		const ibTotalBranch* here = (*ctx.levels)[levelIdx].m_branch.get();
		if (here != branch) {
			// Inside a branch and the next level belongs to another one: this section has ended.
			if (branch != nullptr)
				return;
			// On the common ladder and a branch begins: open one fork per section, side by side.
			for (size_t at = levelIdx; at < ctx.levels->size(); ) {
				const ibTotalBranch* section = (*ctx.levels)[at].m_branch.get();
				size_t end = at;
				while (end < ctx.levels->size() && (*ctx.levels)[end].m_branch.get() == section)
					++end;
				if (section == nullptr) {   // a common level after a branch — cannot happen, but do not eat it
					FoldDimLevel(ctx, node, rows, at, across, nullptr, depth, indent);
					break;
				}
				// AT THE PARENT'S OWN DEPTH — same rule as ibStreamingFold::BranchChild, so two
				// branches stand at one depth however many levels stood above them.
				ibSelectorTree::Node* fork = node->AddChild(node->m_level);
				fork->m_indent = indent;              // …and inside whatever hierarchy step it was opened in
				fork->m_kind   = ibSelectorNodeKind::Branch;
				fork->m_values = node->m_values;      // the keys above stay readable inside the branch
				fork->m_branch = section->m_name.IsEmpty() ? node->m_branch : section->m_name;
				// ⭐ EVERY BRANCH STARTS AT THE SAME DEPTH — this one, the number the level would have
				// had without the fork. A fork spends none of its own (it is opened at the parent's
				// depth above), so the branches stand level with each other however many of them there
				// are. Counted from the flat list instead, branch two printed a rung deeper.
				FoldDimLevel(ctx, fork, rows, at, across, section, depth, indent);
				FoldAcross(ctx, fork, rows);
				fork->m_hasChildren = !fork->m_children.empty();
				ApplyAggregates(*fork, *ctx.snapshot, rows, *ctx.aggregates);   // the heading's own figure
				at = end;
			}
			return;
		}
	}

	// ⭐ WHERE THE ROWS END. Down the page the ladder stops at the seam — everything past it reads
	// ACROSS, and those levels are walked by FoldAcross under each heading, not by falling into them
	// here. What still hangs below a heading is the DETAIL level, which is last in the config and
	// reads down the page like every other row level (ibDataQueryBuilder::TotalsDetails).
	if (!across && levelIdx >= ctx.across) {
		if (!ctx.details)
			return;
		levelIdx = ctx.levels->size() - 1;
	}
	const ibTotalLevel& level = (*ctx.levels)[levelIdx];

	// ⭐⭐ A LEVEL WITH NO FIELDS IS THE DETAIL RECORDS. Grouping by nothing does not make one group
	// of everything — that is what OVERALL means, at the other end of the list — it makes NO group
	// at all, so what hangs here is the rows themselves: one node per row, carrying the values it
	// was read with. That is the whole of "a detail record is an empty grouping", and it is one
	// primitive rather than a second kind of level: the settings tree writes an empty grouping node
	// and the fold answers with rows.
	//
	// The rows are ALREADY HERE — this is the snapshot the levels above were folded from — so the
	// detail level costs nodes, not a second read. (It does cost nodes: a report that prints every
	// row holds every row, which is what printing every row means. The server-side fold is refused
	// upstream when details are asked for, since ROLLUP returns no rows to hang.)
	if (level.m_fields.empty()) {
		if (across)
			return;   // nothing groups here, and there are no detail records across the page
		for (long r : rows) {
			// ⚠ NUMBERED PAST THE LAST DIMENSION, not "one deeper than the heading it hangs under".
			// In a table it hangs under the last ROW heading while the column keys are numbered on
			// past it, and two nodes sharing a number is exactly what a printer cannot untangle.
			// ⭐ …AND INSIDE A BRANCH IT IS THE BRANCH'S LAST RUNG, not the flat list's end — the same
			// rule the streaming fold spells as Section::DepthOfDetails(). Outside one the two agree,
			// so the old number is kept where there are no branches at all.
			ibSelectorTree::Node* leaf = node->AddChild(
				branch != nullptr ? depth : static_cast<int>(ctx.levels->size()));
			leaf->m_indent = indent;           // …standing wherever the heading above it stands in its tree
			leaf->m_kind = ibSelectorNodeKind::Detail;
			leaf->m_values = node->m_values;   // the group keys above stay readable on the row
			leaf->m_branch = node->m_branch;   // …and whose branch it is read on
			for (const ibQueryRamColumn& col : ctx.snapshot->Columns())
				leaf->m_values[col.m_id] = ctx.snapshot->GetCell(r, col.m_id);
			// ⭐⭐ AND NOTHING IS ROLLED ON TOP OF THEM. A detail row is the BOTTOM floor of the one
			// column that carries two things: the figure at every heading, the row's own value here.
			// Rolling the resources over this single row overwrote that value with the row's
			// "contribution" — invisible for SUM, where a sum of one row IS the row, and wrong for
			// every other function: `COUNT(Number)` put a 1 where the document's number belongs
			// (Max, 2026-08-22: "the rows returned a number, and it should be the document number").
			//
			// There is nothing to compute here anyway. What a row contributes to the total above it
			// is the value it already holds, which the loop above has just written.
			//
			// ⚠ EXCEPT WHERE THE ROW HOLDS NOTHING. `COUNT(*)` has no input column at all — its
			// receiver is a synthetic id that is not in the snapshot — so the loop above cannot
			// fill it and the parent's value is copied BEFORE the parent's own aggregates run. Left
			// alone it came back blank on every detail row while staying in the schema: a column
			// that is there and empty, which is the shape this whole day was spent removing. Those
			// aggregates, and only those, are still rolled over the single row.
			auto snapshotCarries = [&ctx](const std::shared_ptr<const ibBackendQueryColumn>& col) {
				if (col == nullptr)
					return false;
				for (const ibQueryRamColumn& c : ctx.snapshot->Columns())
					if (c.m_id == col->GetColumnId())
						return true;
				return false;
			};
			// ⭐⭐ IN A TABLE A RECORD IS FIGURED LIKE A HEADING — "a sub-group of one row" (Max,
			// 2026-08-26): every measure is rolled over its single row, so a cell and the row total
			// hold FIGURES (COUNT is 1, SUM is the value). The rule above — a record shows its own
			// value — belongs to the ordinary report, where a record and a measure SHARE one column;
			// a table gives each measure a column of its own and there is nothing to share.
			//
			// Told apart by the same fact everywhere else uses: is there a column axis at all.
			const bool inATable = ctx.across < ctx.levels->size();
			std::vector<ibDataQueryBuilder::AggregateItem> rolled;
			for (const ibDataQueryBuilder::AggregateItem& a : *ctx.aggregates)
				if (inATable || !snapshotCarries(a.m_col))
					rolled.push_back(a);
			if (!rolled.empty())
				ApplyAggregates(*leaf, *ctx.snapshot, std::vector<long>{ r }, rolled);

			// …AND ITS CELLS — its own column groups, exactly as a heading has them.
			FoldAcross(ctx, leaf, std::vector<long>{ r });
			leaf->m_hasChildren = !leaf->m_children.empty();
		}
		return;
	}

	// A HIERARCHY UNFOLD walks ONE parent chain, so it is a single-field level's answer — there is no
	// one chain to walk for a key made of several fields. The lowering refuses that combination where
	// it is written, so this reads the head field and never has to blend the two readings.
	//
	// ⭐⭐ …AND ONLY WHERE THERE IS A CHAIN TO WALK. The unfold is a REQUEST — "read this field down its
	// parent chain" — and a request is answered by what the data can do, not by the word alone. A
	// document has no parent column; a catalog nobody has filled a parent in is the same thing said with
	// data. Either way the chain is one link long, and a level that unfolds a chain of one link IS an
	// ordinary grouping (Max, 2026-08-29: *"there cannot be levels there by definition — it is one level,
	// because there is no hierarchy at all"*).
	//
	// 🛑 Taken on the WORD, the fold went down the hierarchy road expecting a tree and had none: it built
	// roots, walked for children that could not exist, and handed the printer a shape that a flat source
	// cannot have — rows spread across seven levels where one was possible. The mess is not in what it
	// then did; it is in having entered at all (Max: *"you expect a hierarchy and you have a flat one"*).
	//
	// The parent map is read FIRST and its emptiness IS the answer, so this asks the same authority the
	// walk would have asked anyway and costs nothing extra.
	static const ibValueHierarchy kNoHierarchy;
	const ibValueHierarchy& hierarchy = (level.IsSingleField() && level.HeadDim() != ibDimensionKind::Elements)
		? HierarchyForField(ctx, level.HeadCol())
		: kNoHierarchy;
	const ibValueParentMap& parentOf = hierarchy.parentOf;

	if (level.IsSingleField() && level.HeadDim() != ibDimensionKind::Elements && !parentOf.empty()) {
		// Grouped by the VALUE, in first-seen order. `valOf` is gone with the string key — it only ever
		// mapped a rendered key back to the value it was rendered from.
		//
		// 🛑⭐⭐ …AND FIRST-SEEN ORDER HAS TO BE KEPT SOMEWHERE, because the bucket map is UNORDERED. The
		// Elements branch below has always carried its own `order` vector for exactly this reason; this
		// branch walked `byVal` itself, so the tree came out in HASH order and the read's `ORDER BY` — the
		// author's sort, the level keys, everything the query was careful to state — was thrown away at the
		// last step. Nothing said so: the rows were all there, in an order with no name (Max, 2026-08-29:
		// *"a strange sort, but it does print everything now"*).
		//
		// The fold's rule everywhere else is *a group stands where its FIRST ROW stood*, and that is all
		// this restores: the keys in arrival order, and the walk below started from them.
		ibRowsByValue          byVal;
		std::vector<ibValue>   keyOrder;
		keyOrder.reserve(rows.size());
		for (long r : rows) {
			const ibValue key = ctx.snapshot->GetCell(r, level.HeadCol()->GetColumnId());
			const auto [it, inserted] = byVal.try_emplace(key);
			if (inserted)
				keyOrder.push_back(key);
			it->second.push_back(r);
		}

		// Hierarchy / HierarchyOnly — arrange the values into the catalog's parent-ref tree. (`parentOf`
		// was read above the branch: its emptiness is what decides whether this road is taken at all.)
		//
		// ⭐⭐ "NO PARENT" ARRIVES AS TWO DIFFERENT VALUES, AND THE ROOT BUCKET IS ONE — a key the map does not
		// mention, and an empty reference; both are filed under `ibValue()`, where the roots are looked up below.
		// 🛑 Filed under whatever came, a hierarchical catalog lost its whole tree: every top-level element went
		// into the empty-REFERENCE bucket and the root lookup read the empty-VALUE one (Max, 2026-08-29: the
		// hierarchical sheet came out blank, twice).
		// ⭐⭐ …AND A KEY STANDS WHERE ITS OWN ROW STOOD, NOT WHERE ITS FIRST CHILD DID; a folder with no row of
		// its own, where its first descendant did (ibArrangeUnderParents — the other hierarchy road says it too).
		ibRefChildren childrenOf = ibArrangeUnderParents(keyOrder, hierarchy,
			[&byVal](const ibValue& key) { return byVal.contains(key); });
		ibValueSeen visited;
		// Rows of ROOT values that HIERARCHYONLY folded away: an element standing at the top has no
		// folder to be read under, so its rows join the level's remainder below rather than vanishing
		// (the same contract the `orphaned` note states — a row in none of the nodes is a row eaten).
		std::vector<long> foldedAtRoot;
		const auto rit = childrenOf.find(ibValue());
		if (rit != childrenOf.end())
			for (const ibValue& rootKey : rit->second) {
				std::vector<long> rr = AttachDimValue(ctx, node, levelIdx, rootKey, byVal, childrenOf, visited, across, branch, depth, indent);
				const auto rootKids = childrenOf.find(rootKey);
				if (level.HeadDim() == ibDimensionKind::HierarchyOnly
				 && (rootKids == childrenOf.end() || rootKids->second.empty()))
					foldedAtRoot.insert(foldedAtRoot.end(), rr.begin(), rr.end());
			}

		// ⭐⭐ …AND THE ROWS THAT HAVE NO KEY AT THIS LEVEL BELONG TO THE NODE ITSELF. Grouping a catalog by
		// its PARENT makes "no parent" a real group — it means the TOP LEVEL — and the arrangement refuses such a
		// key at the door (it is what ends the walk up the chain), so those rows had no heading to hang
		// under and simply vanished: a catalog printed one folder and lost every element standing beside it
		// (Max, 2026-08-29). They are not a heading either — the top level of a tree has no caption — so
		// they go where they belong, straight under the node this level is being built on, at the same
		// depth. The next level inside them still runs, so a sub-grouping under a top-level row keeps working.
		//
		// ⚠ AND ONE LEVEL SHALLOWER, which is the whole of what "belonging to the node" means here. A
		// heading stands at `depth` and its own rows one deeper; rows that belong to the NODE stand where
		// the headings do. Passed `depth`, they came out alongside a heading's children and the sheet drew
		// them INSIDE the last folder — the indentation said they were its contents (Max, 2026-08-29:
		// *"the levels are broken"*).
		// ⭐⭐ AN EMPTY VALUE IS A VALUE, AND ITS ROWS ARE A GROUP LIKE ANY OTHER — "not set" is what a
		// person calls it, and it gets a heading of its own at this rung.
		//
		// 🛑 THEY USED TO HANG ON THE NODE ITSELF, headingless, and that cost BOTH roads at once. On the
		// sheet seventy documents printed as bare rows under someone else's heading. In the LIST they
		// vanished outright: a fetch returns the nodes OF ITS RUNG, and rows attached to the rung above
		// are numbered past the last dimension — so the list showed one group and lost seventy-one rows
		// (Max, 2026-08-30: *"and the rows disappear altogether"*). Losing rows is the worst answer a
		// list can give, and it came from a special case that need not exist.
		//
		// ⚠ THIS IS NOT THE "NO PARENT" SENTINEL. `IsNoKey` reads two different facts through one
		// spelling: a row whose VALUE at this level is empty (data — a group), and a key with no PARENT
		// (structure — the root of the tree, filed in `childrenOf[ibValue()]` above). Only the first is
		// changed here; the parent-map root bucket is untouched.
		// ⭐⭐ …AND THE REMAINDER GOES ON IT. `visited` says which keys the walk down the roots actually
		// reached; whatever it did not is a row the tree has no place for — a value whose parent chain
		// the map does not reach, or one that simply has no value at this level. Read together they are
		// one thing: everything not hanging anywhere hangs HERE (Max, 2026-08-30: *"you just need to
		// hang the remainder of the values onto the empty element"*).
		//
		// 🛑 THE ALTERNATIVE IS SILENT LOSS, and that is what it was: a fetch returns the nodes of its
		// rung, so rows attached nowhere are rows nobody can reach — a list showed one group and lost
		// seventy-one rows. A fold's contract is that every row it was given is somewhere in the tree it
		// returns; a row that is in none of the nodes is a row the fold ate.
		std::vector<long> orphaned = foldedAtRoot;   // top-level elements HIERARCHYONLY did not draw
		for (const ibValue& key : keyOrder) {
			if (!IsNoKey(key) && visited[key])
				continue;                                   // attached under its own folder chain
			const auto bit = byVal.find(key);
			if (bit != byVal.end())
				orphaned.insert(orphaned.end(), bit->second.begin(), bit->second.end());
		}
		if (!orphaned.empty()) {
			ibSelectorTree::Node* empty = node->AddChild(depth);
			empty->m_indent = indent;
			empty->m_values = node->m_values;   // the rungs above stay readable on this heading
			empty->m_branch = node->m_branch;
			empty->m_values[level.HeadCol()->GetColumnId()] = ibValue();   // …and its own key, which is empty
			FoldDimLevel(ctx, empty, orphaned, levelIdx + 1, across, branch, depth + 1, indent);
			if (!across)
				FoldAcross(ctx, empty, orphaned);
			empty->m_hasChildren = !empty->m_children.empty();
			ApplyAggregates(*empty, *ctx.snapshot, orphaned, *ctx.aggregates);
		}
		return;
	}

	// GROUPED BY THE TUPLE of the level's fields, in first-seen order. One field is the degenerate
	// case of the same walk — a one-element key — so a level of two fields costs one more cell read
	// per row and nothing else.
	ibRowsByLevelKey        byKey;
	std::vector<ibLevelKey> order;
	for (long r : rows) {
		ibLevelKey key;
		key.reserve(level.m_fields.size());
		for (const ibTotalField& field : level.m_fields)
			key.push_back(LevelKeyValue(field, ibRamTableRow(*ctx.snapshot, r)));   // truncated when the field is BY PERIODS

		const auto [it, inserted] = byKey.try_emplace(key);
		if (inserted)
			order.push_back(std::move(key));
		it->second.push_back(r);
	}

	// ⚠ THE LEVEL'S NUMBER, NOT THE DEPTH BELOW THIS NODE. Down the page the two agree; across it
	// they do not — a column key hangs directly under whichever row heading it belongs to, and its
	// number has to say WHICH LEVEL it is so the schema and the tree keep meaning the same thing.
	const int childLevel = depth;   // the depth this level stands at — WITHIN its ladder, see the declaration
	// …and cells are INSERTED before the sub-headings a previous pass may already have added, so a
	// pre-order reader meets a heading's own cells before anything nested under it.
	std::size_t insertAt = 0;

	for (const ibLevelKey& key : order) {
		ibSelectorTree::Node* child = across
			? node->InsertChild(insertAt++, childLevel)
			: node->AddChild(childLevel);
		child->m_indent = indent;   // …inside whatever hierarchy step the level above it stands in
		// A SUBGROUP inherits the grouping fields AVAILABLE from the levels above (Max) — copy the parent
		// group's stamped dimension values down, then add this level's own. So a display column that
		// dot-walks an ANCESTOR dimension's reference (e.g. the parent grouped by Reference, this level by
		// Reference.DataVersion) resolves against the inherited value in the subgroup header, not just at the
		// top level. Aggregates are (re)stamped by ApplyAggregates below, so inheriting them is harmless.
		child->m_values = node->m_values;
		child->m_branch = node->m_branch;   // …and which branch it stands in — a fact about the NODE
		for (std::size_t i = 0; i < level.m_fields.size(); ++i)
			child->m_values[level.m_fields[i].m_col->GetColumnId()] = key[i];   // every field of the key IS a value

		const std::vector<long>& own = byKey[key];

		// ⭐⭐ A LEVEL KEYED BY THE ROW'S OWN IDENTITY **IS** THE ROW — the same rule AttachDimValue
		// states, and it belongs to the LEVEL rather than to either branch. A field can be grouped by
		// with any unfold, and which branch that takes is not the grouping's business: a HIERARCHY
		// unfold over a source with no parent column (a document grouped by its own reference) is an
		// ordinary grouping by the time it gets here, and the heading came out holding nothing but the
		// reference while a record printed the same document again underneath (Max, 2026-08-29, live).
		//
		// ⭐⭐ AND IT IS A RECORD, NOT A HEADING WEARING A RECORD'S FIELDS. A node has ONE STATUS: either it
		// is a GROUPING — a key, a caption drawn across the line, figures — or it is a ROW, which is its own
		// values in its own columns. Stamped onto a Group node the row's fields made it both at once, and a
		// printer that draws both draws them on top of each other: the reference's presentation over the
		// number, in one cell (Max, 2026-08-29: *"you hand back a row that is in two statuses at the same
		// time — hierarchy and grouping"*).
		//
		// ⚠ AND ONLY THE SOURCE'S OWN TABLE IS READ THIS WAY. Grouping a document by a CATALOG it refers to
		// stays an ordinary grouping, hierarchy unfold or not — the catalog's folders are headings and the
		// documents hang under them. It is the MAIN TABLE, grouped by its own reference, that turns into
		// records, and that is what lets a list show every field on the line (Max, 2026-08-29).
		//
		// There is nothing below such a node — the group and the row are one thing here — so the next level
		// is not entered and the figures are not rolled over it: a record shows what it holds, exactly as
		// the detail branch above says.
		// ⭐⭐ A LEVEL KEYED BY THE ROW'S OWN IDENTITY **IS** THE ROW, so the node carries the row's fields.
		// Its key tells one row from another, so every group holds exactly one — and a heading that stands
		// for one row may as well show it. Asked of the SOURCE, not of the row count: a group that merely
		// happens to hold one row is a different thing and is left alone.
		// 🛑⭐⭐ AND IT IS TAKEN OUT AGAIN, FOR THE `Elements` SPELLING. Stamping the row's fields onto
		// such a heading made it indistinguishable from a record, so the groupings a person had set
		// went INVISIBLE — 72 lines that all read as detail rows — and stopping the descent under it
		// dropped every rung below, which the LIST still showed by drilling into the node. Both halves
		// of the rule cost more than they saved (Max, 2026-08-29: *"I set a grouping by reference and
		// one by data version; I expect the first, the second and the third"* and *"the list keeps
		// diverging from the report"*).
		//
		// A rung that happens to hold one row is still a rung: "it holds one row" is a fact about the
		// data, not permission to delete a level somebody configured. The LADDER IS READ LITERALLY —
		// one heading per rung, records at the bottom — which is exactly what the list draws, and one
		// answer for both roads is the requirement.
		//
		// ⚠ AND AN ORDINARY RUNG IS AN ORDINARY RUNG, whatever its key happens to be. The "a level keyed
		// by the row's identity IS the row" rule belongs to the TREE — `AttachDimValue`, where a node
		// genuinely stands for one element and the tree itself is the display — and it was tried here
		// as well, twice. Both times it took away what a person had asked for: a document list grouped
		// by its own reference printed headings and NO detail records (Max, 2026-08-30: *"the problem
		// is that in documents the detail records are not printed"*). A group holding one row is still
		// a group, and the rows under it are still the rows.
		FoldDimLevel(ctx, child, own, levelIdx + 1, across, branch, depth + 1, indent);   // next level inside, same ladder, one deeper
		// ⭐ AND THE CELLS OF THIS HEADING — under EVERY row heading, not only the deepest (FoldAcross).
		if (!across)
			FoldAcross(ctx, child, own);
		child->m_hasChildren = !child->m_children.empty();
		ApplyAggregates(*child, *ctx.snapshot, own, *ctx.aggregates);
	}
}

} // namespace ibProvider

// ⭐⭐ THE COMPOSITION'S OWN FOLD, TAKING A CURSOR. This is the road every report travels, so this is
// where "memory grows with the number of GROUPS" is either true or not.
//
// One pass builds the whole tree when the levels group by VALUES — which is what a report's levels
// do. The exception says its own name: a level that unfolds a reference HIERARCHY arranges its
// values into the target catalog's parent chain, a shape that is not known until every value has
// been seen (and, in Hierarchy, hangs the rows themselves under their folder). That fold reads a
// whole table, so the cursor is drained into one and the road is journalled — the answer is the
// same either way, and the only thing at stake is what it costs.
ibSelectorTree BuildDimensionTree(ibQueryRowCursor& rows,
		const std::vector<ibTotalLevel>& levels, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
		ibDatabaseConnectionHolder* holder, const std::shared_ptr<const ibBackendQueryable>& source)
{
	if (LevelsUnfoldHierarchy(levels)) {
		if (CanFoldByKeysFirst(levels, aggregates, source)) {
			std::vector<ibDataQueryBuilder::AggregateItem> rolled;
			std::vector<std::pair<ibMetaID, ibMetaID>> averages;   // an average's sum slot and its count's
			long read = 0;
			ibQueryRamTable grouped = FoldByKeysFirst(rows, levels, aggregates, source, rolled, averages, read);
			ibJournalInfo(ibT("query.road"), ibT("RAM: folded %ld detail rows into %ld groups by their keys - a level unfolds a reference hierarchy"),
			              read, grouped.RowCount());
			ibSelectorTree tree = BuildDimensionTree(grouped, levels, rolled, holder, source);
			if (!averages.empty())
				DivideAverages(tree.Root(), averages);
			return tree;
		}
		ibQueryRamTable snapshot = ibDrainToRamTable(rows);
		ibJournalInfo(ibT("query.road"), ibT("RAM: drained %ld detail rows - a level unfolds a reference hierarchy"),
		              snapshot.RowCount());
		return BuildDimensionTree(snapshot, levels, aggregates, holder, source);
	}

	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : rows.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);

	// ⚠ AND THE ROW'S IDENTITY IS NOT ASKED FOR ON THIS ROAD — see the note over `ibStreamingFold`'s
	// constructor. The snapshot road asks the source for it (`BuildDimensionTree` below) because its
	// headings ARE the rows; a streamed ladder is read literally, one heading per rung.
	ibStreamingFold fold(tree, levels, aggregates, rows.Columns());
	long read = 0;
	ibJournalStopwatch fetch, feed, finish;
	for (;;) {
		fetch.Resume();
		const bool more = rows.Next();
		fetch.Pause();
		if (!more)
			break;
		feed.Resume();
		fold.Feed(rows);
		feed.Pause();
		++read;
	}
	finish.Resume();
	fold.Finish();
	ibQueryComposer::PadPeriodLevels(tree, levels, aggregates);   // the quiet months, which no fold can produce
	ApplyScopedAggregates(tree, aggregates);     // …and the figures that belong to ONE level (OVER)
	finish.Pause();
	ibJournalInfo(ibT("query.road"), ibT("STREAM: folded %ld rows into %ld nodes (%u levels); ")
	              ibT("fetch %lld ms, feed %lld ms, finish %lld ms"),
	              read, fold.NodeCount(), static_cast<unsigned>(levels.size()), fetch.Ms(), feed.Ms(), finish.Ms());
	return tree;
}

ibSelectorTree BuildDimensionTree(const ibQueryRamTable& snapshot,
		const std::vector<ibTotalLevel>& levels, const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
		ibDatabaseConnectionHolder* holder, const std::shared_ptr<const ibBackendQueryable>& source)
{
	ibSelectorTree tree;
	for (const ibQueryRamColumn& col : snapshot.Columns())
		tree.AddColumn(col.m_id, col.m_name, col.m_type);
	AddSyntheticAggColumns(tree, aggregates);

	std::vector<long> all; all.reserve(static_cast<size_t>(snapshot.RowCount()));
	for (long r = 0; r < snapshot.RowCount(); ++r) all.push_back(r);

	// THE SEAM AND THE ROWS — asked of the levels, exactly as the streaming fold asks (the two roads
	// must fold the same shape; which one a query takes depends only on whether a level unfolds a
	// hierarchy).
	DimCtx ctx{ &snapshot, &levels, &aggregates, holder, source };
	ctx.across = levels.size();
	for (size_t li = 0; li < levels.size(); ++li)
		if (levels[li].m_axis == ibTotalsAxis::Columns) { ctx.across = li; break; }
	ctx.details = !levels.empty() && levels.back().m_fields.empty();
	// …AND WHICH COLUMN IS THE ROW'S IDENTITY, asked of the source once. A level grouped by it holds one
	// row per group by construction, and its headings ARE those rows (AttachDimValue).
	if (source != nullptr) {
		const std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys = source->GetPrimaryKeyColumns();
		if (keys.size() == 1 && keys.front() != nullptr)
			ctx.identity = keys.front()->GetColumnId();
	}

	FoldDimLevel(ctx, &tree.Root(), all, 0);
	// …AND THE ROOT'S OWN CELLS — the column totals, which are the cells of the heading over
	// everything (see ibStreamingFold::Feed for why this replaced a second fold).
	FoldAcross(ctx, &tree.Root(), all);
	tree.Root().m_hasChildren = !tree.Root().m_children.empty();
	ApplyAggregates(tree.Root(), snapshot, all, aggregates);   // grand total in-place
	ibQueryComposer::PadPeriodLevels(tree, levels, aggregates);                 // …and the periods nothing happened in
	ApplyScopedAggregates(tree, aggregates);                   // …and the figures that belong to ONE level
	return tree;
}

// ExecuteGroupLevelPage — the paged single-level group read (door SelectAggregatePage). On a shape the source's
// provider pages (TryPageGroupLevel — the database's: one plain scalar dim over a single source) the level's groups
// run server-side as GROUP BY dim ORDER BY dim [dim </> anchor] LIMIT count — a keyset-paged group read; otherwise
// the unpaged aggregate (all groups). The lowering's single-scalar-dim TOTALS drill routes here instead of the RAM fold.
ibDataQueryResult ibQueryComposer::ExecuteGroupLevelPage(const ibDataQuerySpec& given, const ibReadPageRequest& page)
{
	// A tree pages its groups as the one source it is (ReadAsOneStatement).
	const ibDataQuerySpec spec = ReadAsOneStatement(given);
	// The result has no default state — an empty one is made the way every empty result is.
	ibDataQueryResult paged = ibMakeEmptyQueryResult(spec.m_queryable);
	if (ProviderOf(spec).TryPageGroupLevel(spec, page, paged))
		return paged;
	return ExecuteAggregate(spec);   // not pageable server-side -> unpaged all-groups
}

// ⭐ CAN THE DBMS FOLD THIS TOTALS ITSELF, and if so — fold it. The push-down a totals read takes
// when it can, asked as a QUESTION so a caller that must otherwise read detail rows (the query
// lowering, the door's SelectTotals) can take the cheap road when it exists and the result's fold
// when it does not. Asked of the provider of the primary source, by the same verb. False = nothing
// was read; the caller proceeds exactly as before.
bool ibQueryComposer::TryFoldTotalsInDbms(const ibDataQuerySpec& given, ibSelectorTree& out)
{
	// …only where the rows stand. A spec read in a base in memory is copied there to be read, and a fold declined after
	// the copy would have it copied again for the detail: the result's fold takes the one read.
	if (ReadsInMemorySpace(given))
		return false;
	// A tree folds its levels as the one source it is (OverComposite).
	const ibDataQuerySpec spec = OverComposite(given);
	return ProviderOf(spec).TryFoldTotalsInDbms(spec, out);
}

long ibQueryComposer::ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind)
{
	// Writes always target a single real table — there is no multi-source write.
	return spec.m_queryable->GetProvider().ExecuteWrite(spec, kind);
}

// One ORDER BY key compared for the RAM sort floor. Delegates to ibValue::CompareValueLS — the
// SQL-aligned three-way total order (NULL = smallest) — then applies the sort direction, giving
// NULLS FIRST on ASC / NULLS LAST on DESC deterministically. The NULL rule lives in ONE place
// (ibValue) now, shared with operator< / std::sort / set / map.
//   return <0: a orders before b ; >0: b before a ; 0: equal on this key (try the next key).
int RamSortCompareKey(const ibValue& va, const ibValue& vb, bool ascending)
{
	const int c = va.CompareValueLS(vb);
	return ascending ? c : -c;
}

// ==========================================================================
// THE DOOR'S VERBS (dataQueryBuilder.h) — what each terminal runs with its own spec once the policy has let it
// through. The door declares them and names no provider; the composer answers here.
// ==========================================================================
ibDataQueryResult ibDataQueryBuilder::ExecuteRead(const ibReadPageRequest& page) const
{
	return ibQueryComposer::ExecuteRead(m_spec, page);
}

ibDataQueryResult ibDataQueryBuilder::ExecuteReadCached(const ibReadPageRequest& page, ibRenderedPageCache& cache,
                                                        const ibPageSignature& signature) const
{
	return ibQueryComposer::ExecuteReadCached(m_spec, page, cache, signature);
}

ibDataQueryResult ibDataQueryBuilder::ExecuteAggregate() const
{
	return ibQueryComposer::ExecuteAggregate(m_spec);
}

ibDataQueryResult ibDataQueryBuilder::ExecuteGroupLevelPage(const ibReadPageRequest& page) const
{
	return ibQueryComposer::ExecuteGroupLevelPage(m_spec, page);
}

bool ibDataQueryBuilder::TryFoldTotalsInDbms(ibSelectorTree& out) const
{
	return ibQueryComposer::TryFoldTotalsInDbms(m_spec, out);
}

long ibDataQueryBuilder::ExecuteWrite(WriteKind kind) const
{
	return ibQueryComposer::ExecuteWrite(m_spec, kind);
}

// The inner's own conditions and folds are already in its rows, so the semi-join over the temp carries none. CAN is
// asked BEFORE the rows are computed: without temp tables the caller's join in memory computes the inner again, and
// rows taken first would be read twice (ibTempTableManager::CanMaterialise).
bool ibDataQueryBuilder::TrySemiJoin(const std::shared_ptr<const ibBackendQueryable>& inner,
                                     const std::shared_ptr<const ibBackendQueryColumn>& outerKey, const ibString& innerKey,
                                     ibQueryFilterOp op)
{
	const std::shared_ptr<const ibBackendQueryColumn> innerColumn = inner != nullptr ? inner->ResolveColumnByName(innerKey) : nullptr;
	if (innerColumn == nullptr || !ibTempTableManager::CanMaterialise(m_spec.m_holder))
		return false;
	// Only the key goes into the temp — the one column the semi-join compares; the inner's other outputs (a fold, a
	// subquery's figure) may declare no type, which a temp of this base cannot hold.
	ibQueryRamTable rows = inner->ComputeRows({});
	ibQueryRamTable keys;
	keys.AddColumn(innerColumn->GetColumnId(), innerColumn->GetName(), innerColumn->GetTypeDesc());
	for (long row = 0; row < rows.RowCount(); ++row)
		keys.SetCell(keys.AppendRow(), innerColumn->GetColumnId(), rows.TakeCell(row, innerColumn->GetColumnId()));
	std::shared_ptr<ibTempTableManager> temps = ibTempTableManager::Materialise(m_spec.m_holder, keys, inner->GetMetaData());
	const std::shared_ptr<const ibDbTempTableQueryable> temp = temps != nullptr ? temps->Queryable() : nullptr;
	const std::shared_ptr<const ibBackendQueryColumn> key = temp != nullptr ? temp->ResolveColumnByName(innerKey) : nullptr;
	if (key == nullptr)
		return false;
	ibSemiJoinExists semiJoin;
	semiJoin.m_inner    = temp;
	semiJoin.m_outerKey = outerKey;
	semiJoin.m_innerKey = key;
	semiJoin.m_op       = op;
	AddSemiJoin(semiJoin);
	m_ownedTemps.push_back(std::move(temps));   // lives across Execute and its pages; dropped with the builder
	return true;
}