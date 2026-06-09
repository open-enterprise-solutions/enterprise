#ifndef __QUERY_SELECTOR_H__
#define __QUERY_SELECTOR_H__

// The L3 read in two steps — RAW data, then a TRAVERSAL the consumer walks as a cursor:
//
//   result    = door.Execute(req)         - run the query; ibDataQueryResult (carries the raw data).
//   selection = result.Select(kind)       - an ibSelector. `kind` (AXIS 1, ibSelectKind) = HOW to walk:
//                                           Direct (rows as-is) / ByGroups (by levels) /
//                                           ByGroupsHierarchy (by levels + dimension hierarchy).
//   while (s.Next()) { s.GetValue(col); s.Level(); ... ; d = s.Select(kind); while (d.Next()) ... }
//                                         - the cursor; s.Select(kind) is the SAME function on the
//                                           CURRENT node -> a sub-selection (selection -> sub-selection),
//                                           inheriting the filter, recursing down on demand.
//
// AXIS 2 (ibDimensionKind: Elements / Hierarchy / HierarchyOnly) lives on the DIMENSION
// (ByParentRef's dim / a TotalBy level): how a reference field unfolds (folders). One read = one
// snapshot; subtotals roll from THAT snapshot (no second query → detail and total can't skew).
// (docs/query-language-arc.md §22.1b)

#include "queryRamTable.h"      // ibQueryRamTable (the snapshot, held BY VALUE — move-only)
#include "querySelectorTree.h"  // ibSelectorTree (the folded tree) + ibSelectorTree::Node
#include "queryProvider.h"      // ibQueryComposer::BuildHierarchyTree / BuildTotalsTree
#include "dataQueryBuilder.h"   // ibDataQueryBuilder + ibSelectKind + ibDimensionKind + AggregateItem

class ibDatabaseConnectionHolder;   // sub-selection recipe holds it by pointer (Select(kind) only)

class ibSelector
{
public:
	// Takes OWNERSHIP of the snapshot (move-only). `kind` is the TRAVERSAL (AXIS 1), default Direct.
	explicit ibSelector(ibQueryRamTable&& snapshot, ibSelectKind kind = ibSelectKind::Direct)
		: m_snapshot(std::move(snapshot)), m_kind(kind) {}

	// Fold by parent-ref (a reference dimension): rowKeyCol = the row's data-reference, parentKeyCol =
	// its parent attribute (link by _RRRef identity). `dim` (AXIS 2) = how the dimension unfolds.
	ibSelector& ByParentRef(const ibBackendQueryColumn* rowKeyCol, const ibBackendQueryColumn* parentKeyCol,
		ibDimensionKind dim = ibDimensionKind::Hierarchy)
	{
		m_rowKeyCol = rowKeyCol; m_parentKeyCol = parentKeyCol; m_dimKind = dim; return *this;
	}

	// Fold by grouping levels (TotalBy / GroupBy): the columns in order (level 0 = first field).
	ibSelector& ByGroups(const std::vector<const ibBackendQueryColumn*>& groupFields)
	{
		m_groupFields = groupFields; return *this;
	}

	// Aggregates rolled at every node from the snapshot (Sum / Count / …). Inputs must be present in
	// the snapshot — the door stamps them as materialise-columns, so result.Select drains them in.
	ibSelector& Aggregating(const std::vector<ibDataQueryBuilder::AggregateItem>& aggs)
	{
		m_aggregates = aggs; return *this;
	}

	// Recipe for sub-selections (selection -> sub-selection): the source a sub-Select re-Executes for a
	// node's child LEVEL, the columns that level materialises, AND the base FILTER (the original Where
	// filter) — so a sub-selection shows the node's children WITH THE SAME filter. Inherited down.
	ibSelector& WithSource(ibDatabaseConnectionHolder* holder, const ibBackendQueryable* queryable,
		const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& selectCols,
		const std::vector<ibQueryCondition>& conditions = {})
	{
		m_holder = holder; m_queryable = queryable; m_selectCols = selectCols; m_conditions = conditions; return *this;
	}

	// Configure the fold from the door's stamped totals config (TotalBy levels + common totals
	// aggregates). result.Select(kind) calls this so the consumer needs no manual fold.
	ibSelector& WithTotals(const std::vector<ibTotalLevel>& levels, const std::vector<ibAggregateItem>& aggs)
	{
		m_totalLevels = levels; m_aggregates = aggs; return *this;
	}

	// EAGER fold: the whole snapshot → the node tree per the TRAVERSAL kind. Subtotals from THIS snapshot.
	//   Direct            → flat: each row a leaf node;
	//   ByGroups / ByGroupsHierarchy → fold by the TotalBy LEVELS (in order). A Hierarchy / HierarchyOnly
	//     level's reference-hierarchy unfold needs the target catalog's rows — a later layer; for now the
	//     levels group by value (Elements semantics). Manual ByParentRef / ByGroups (no TotalBy levels) is
	//     the direct-config / unit-test path.
	ibSelectorTree Build() const
	{
		if (m_kind == ibSelectKind::Direct)
			return BuildFlat();

		if (!m_totalLevels.empty()) {
			// Self-hierarchy fast path: a SINGLE Hierarchy level on the source's OWN parent column is
			// ROW-keyed (each catalog row is a node) — that is BuildHierarchyTree, rowKey from the
			// queryable (already in the snapshot), NO extra query.
			if (m_totalLevels.size() == 1 && m_totalLevels[0].m_dim != ibDimensionKind::Elements && m_queryable != nullptr
			    && m_totalLevels[0].m_col == m_queryable->GetParentColumn()) {
				const std::vector<const ibBackendQueryColumn*> keys = m_queryable->GetPrimaryKeyColumns();
				if (!keys.empty() && keys.front() != nullptr)
					return ibQueryComposer::BuildHierarchyTree(m_snapshot, keys.front(), m_totalLevels[0].m_col,
					                                           m_aggregates, m_totalLevels[0].m_dim);
			}
			// Everything else — Elements / cross-catalog ref-hierarchy / multi-level (in order, with
			// per-level dimension unfold) → the general value-keyed combiner.
			return ibQueryComposer::BuildDimensionTree(m_snapshot, m_totalLevels, m_aggregates, m_holder, m_queryable);
		}

		// direct-config / unit-test fallback
		if (!m_groupFields.empty())
			return ibQueryComposer::BuildTotalsTree(m_snapshot, m_groupFields, m_aggregates);
		return ibQueryComposer::BuildHierarchyTree(m_snapshot, m_rowKeyCol, m_parentKeyCol, m_aggregates, m_dimKind);
	}

	// --- iteration: selection = a CURSOR over the traversal -----------------------------------------
	// s = result.Select(kind); while (s.Next()) { s.GetValue(col); s.Level(); … }
	// The folded tree is walked PRE-ORDER (a folder, then its subtree). Built lazily on the first Next.
	bool Next()
	{
		EnsureWalk();
		return ++m_pos < static_cast<long>(m_visits.size());
	}

	// Rewind to the start — walk the SAME tree again (the folded tree is kept; only the position
	// resets, no re-query, no re-fold).
	void Reset() { m_pos = -1; }

	// Current visit's value for a column (s.<col>) / for an aggregate alias. A column NOT carried at
	// this level (a detail field on an upper grouping node — not yet unfolded) yields the runtime's
	// explicit NULL, not a default-empty value, so "no value at this level" reads as NULL.
	ibValue GetValue(const ibBackendQueryColumn* col) const
	{
		const ibSelectorTree::Node* n = Current();
		if (n == nullptr || col == nullptr) return ibValue(ibValueTypes::TYPE_NULL);
		const auto it = n->m_values.find(col->GetModelID());
		return it != n->m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
	}
	ibValue GetColumn(const wxString& alias) const
	{
		const ibSelectorTree::Node* n = Current();
		if (n == nullptr || !m_walk) return ibValue(ibValueTypes::TYPE_NULL);
		for (const ibQueryRamColumn& c : m_walk->Columns())
			if (c.m_name == alias) {
				const auto it = n->m_values.find(c.m_id);
				return it != n->m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
			}
		return ibValue(ibValueTypes::TYPE_NULL);
	}

	int  Level()       const { const ibSelectorTree::Node* n = Current(); return n ? n->m_level + m_baseLevel : 0; }  // absolute depth
	bool HasChildren() const { const ibSelectorTree::Node* n = Current(); return n ? n->m_hasChildren : false; }      // expandable (folder)

	// GRAND TOTAL — lives IN the selection itself (the root of the folded tree), read without walking.
	// A column / an aggregate alias rolled over the WHOLE result. Absent → runtime NULL.
	ibValue GetTotal(const ibBackendQueryColumn* col) const
	{
		EnsureWalk();
		if (col == nullptr) return ibValue(ibValueTypes::TYPE_NULL);
		const auto it = m_walk->Root().m_values.find(col->GetModelID());
		return it != m_walk->Root().m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
	}
	ibValue GetTotalColumn(const wxString& alias) const
	{
		EnsureWalk();
		for (const ibQueryRamColumn& c : m_walk->Columns())
			if (c.m_name == alias) {
				const auto it = m_walk->Root().m_values.find(c.m_id);
				return it != m_walk->Root().m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
			}
		return ibValue(ibValueTypes::TYPE_NULL);
	}

	// LAZY sub-selection — "selection -> sub-selection": Execute the CURRENT visit's DIRECT children as
	// their own result (parent == the current node's row-key), with the inherited filter + the
	// fold/recipe, walked with the given `kind`. So `d = s.Select(kind); while (d.Next()) …` recurses
	// the tree on demand — each sub-level its own query, Level() continuing DEEPER. Requires
	// WithSource() + a parent-ref fold; empty Selector otherwise (no DB hit). Defined below.
	ibSelector Select(ibSelectKind kind = ibSelectKind::Direct) const;

	const ibQueryRamTable& Snapshot() const { return m_snapshot; }
	ibSelectKind           GetKind()  const { return m_kind; }

private:
	const ibSelectorTree::Node* Current() const
	{
		return (m_pos >= 0 && m_pos < static_cast<long>(m_visits.size())) ? m_visits[static_cast<size_t>(m_pos)] : nullptr;
	}

	// Direct fold — each snapshot row becomes a leaf node under the root (no subtotals, no nesting).
	ibSelectorTree BuildFlat() const
	{
		ibSelectorTree t;
		for (const ibQueryRamColumn& c : m_snapshot.Columns()) t.AddColumn(c.m_id, c.m_name, c.m_type);
		const long n = m_snapshot.RowCount();
		for (long r = 0; r < n; ++r) {
			ibSelectorTree::Node* leaf = t.Root().AddChild(1);
			for (const ibQueryRamColumn& c : m_snapshot.Columns())
				leaf->m_values[c.m_id] = m_snapshot.GetCell(r, c.m_id);
		}
		return t;
	}

	// Sub-selection core: re-Execute the children of `parentKeyValue` with the inherited filter +
	// fold/recipe, the new Selector walked with `kind`, Level() based at `childBaseLevel`.
	ibSelector MakeChild(const ibValue& parentKeyValue, int childBaseLevel, ibSelectKind kind) const;

	void EnsureWalk() const
	{
		if (m_walk) return;
		m_walk = std::make_unique<ibSelectorTree>(Build());
		m_visits.clear();
		FlattenPreOrder(m_walk->Root(), m_visits);
		m_pos = -1;
	}
	static void FlattenPreOrder(const ibSelectorTree::Node& n, std::vector<const ibSelectorTree::Node*>& out)
	{
		for (const auto& c : n.m_children) { out.push_back(c.get()); FlattenPreOrder(*c, out); }
	}

	ibQueryRamTable m_snapshot;                    // the raw snapshot this Selector traverses
	ibSelectKind    m_kind;                        // AXIS 1 — traversal
	ibDimensionKind m_dimKind = ibDimensionKind::Hierarchy;  // AXIS 2 — dimension unfold (for ByGroupsHierarchy)
	const ibBackendQueryColumn* m_rowKeyCol    = nullptr;   // parent-ref fold
	const ibBackendQueryColumn* m_parentKeyCol = nullptr;
	std::vector<const ibBackendQueryColumn*>       m_groupFields;   // grouping fold (manual ByGroups)
	std::vector<ibTotalLevel>                      m_totalLevels;   // totals dimensions (from the door, via WithTotals)
	std::vector<ibDataQueryBuilder::AggregateItem> m_aggregates;

	// sub-selection recipe (Select(kind) only)
	ibDatabaseConnectionHolder* m_holder    = nullptr;
	const ibBackendQueryable*   m_queryable = nullptr;
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> m_selectCols;
	std::vector<ibQueryCondition>                                 m_conditions;   // inherited filter
	int                                                           m_baseLevel = 0;  // Level() offset

	// iteration state (lazy walk over the folded tree; mutable — built on the first Next)
	mutable std::unique_ptr<ibSelectorTree>           m_walk;    // heap → stable Node addresses across a move
	mutable std::vector<const ibSelectorTree::Node*>  m_visits;  // pre-order visit order
	mutable long                                      m_pos = -1;
};

// --- out-of-line inline bodies (need ibDataQueryResult::Select complete) --------------------------

inline ibSelector ibSelector::MakeChild(const ibValue& parentKeyValue, int childBaseLevel, ibSelectKind kind) const
{
	if (m_holder == nullptr || m_queryable == nullptr || m_rowKeyCol == nullptr || m_parentKeyCol == nullptr)
		return ibSelector(ibQueryRamTable{}, kind);

	ibDataQueryBuilder q(m_holder);
	q.From(m_queryable);
	for (const ibQueryCondition& c : m_conditions)                 // inherited filter re-applied
		if (c.m_explicitOp) q.WhereCompare(c.m_col, c.m_op, c.m_value);
		else                q.Where(c.m_col, c.m_comparison, c.m_value);
	q.Where(m_parentKeyCol, parentKeyValue);                       // direct children of this node
	for (const auto& sc : m_selectCols) q.Select(sc.first, sc.second);
	for (const ibDataQueryBuilder::AggregateItem& a : m_aggregates) q.Aggregate(a.m_fn, a.m_col, a.m_alias);

	ibSelector child = q.Execute(ibReadPageRequest{}).Select(kind);   // result.Select(kind) → sub-selection
	child.ByParentRef(m_rowKeyCol, m_parentKeyCol, m_dimKind)
	     .ByGroups(m_groupFields)
	     .Aggregating(m_aggregates)
	     .WithSource(m_holder, m_queryable, m_selectCols, m_conditions);
	child.m_baseLevel = childBaseLevel;
	return child;
}

inline ibSelector ibSelector::Select(ibSelectKind kind) const
{
	const ibSelectorTree::Node* n = Current();
	if (n == nullptr || m_rowKeyCol == nullptr) return ibSelector(ibQueryRamTable{}, kind);
	const auto it = n->m_values.find(m_rowKeyCol->GetModelID());
	const ibValue key = (it != n->m_values.end()) ? it->second : ibValue();
	return MakeChild(key, n->m_level + m_baseLevel, kind);          // children continue DEEPER
}

#endif // __QUERY_SELECTOR_H__
