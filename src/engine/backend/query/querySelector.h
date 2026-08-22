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
	explicit ibSelector(ibQueryRamTable&& snapshot, ibSelectKind kind = ibSelectKind::ibSelectKind_Direct)
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
	// THE OVERALL LEVEL — walk the tree's ROOT as a row, above every dimension. It is a WALK
	// setting, not a fold one: the root's aggregates are already computed either way
	// (BuildDimensionTree — "grand total in-place"), and this only decides whether anyone sees them
	// as a row instead of having to ask GetTotal() for each column by hand.
	ibSelector& WithTotals(const std::vector<ibTotalLevel>& levels,
		const std::vector<ibDataQueryBuilder::AggregateItem>& aggs, bool overall)
	{
		m_totalLevels = levels; m_aggregates = aggs; m_totalsOverall = overall; return *this;
	}

	// ⭐ THE SAME THING, SAID BY THE READER. `BY OVERALL` is how the QUERY asks for the root row; a
	// consumer that always wants it — a report, which prints a grand total at the bottom whatever
	// the text says — asks here instead of having the text rewritten under it. Nothing is folded
	// differently: the root already holds the whole-result aggregates, and this only puts it in the
	// walk.
	ibSelector& WalkOverall(bool on = true) { m_totalsOverall = on; return *this; }

	// EAGER fold: the whole snapshot → the node tree per the TRAVERSAL kind. Subtotals from THIS snapshot.
	//   Direct            → flat: each row a leaf node;
	//   ByGroups / ByGroupsHierarchy → fold by the TotalBy LEVELS (in order). A SINGLE Hierarchy /
	//     HierarchyOnly level on the source's OWN parent column unfolds as a real tree (BuildHierarchyTree,
	//     row-keyed, no extra query — the fast path below); a multi-level or cross-catalog reference-
	//     hierarchy unfold still needs the target catalog's rows, so those levels group by value
	//     (Elements semantics). Manual ByParentRef / ByGroups (no TotalBy levels) is the direct-config /
	//     unit-test path.
	// ⭐ THE TREE MAY ARRIVE ALREADY BUILT — when the DBMS folded it. `GROUP BY ROLLUP` computes
	// every subtotal level server-side and only the aggregated rows travel; there is nothing left
	// to fold here, and folding again would need the detail rows this path exists to avoid reading.
	//
	// Handed over rather than rebuilt, because the two trees are the SAME tree: same levels, same
	// aggregates, same shape. Where they would differ, the server path is not taken at all.
	ibSelector& WithReadyTree(std::shared_ptr<ibSelectorTree> tree)
	{
		m_readyTree = std::move(tree);
		return *this;
	}

	ibSelectorTree Build() const
	{
		// A tree the server already folded is the answer, whatever the walk kind: a flat walk over
		// it still reads its nodes, which is what a caller asking for groups wants either way.
		//
		// ⚠ COPIED, NOT MOVED. The ready tree is SHARED — the result keeps it and hands the same
		// shared_ptr to every selector it makes — so moving out of it emptied the tree for whoever
		// asked second (a result read twice, a drill fetching a node's children).
		if (m_readyTree)
			return m_readyTree->Clone();

		if (m_kind == ibSelectKind::ibSelectKind_Direct)
			return BuildFlat();

		// ⚠ OVERALL ALONE IS STILL A TOTALS FOLD. `TOTALS COUNT(x) BY OVERALL` asks for one row over
		// everything and names no dimension — so the levels are empty, and without this the walk fell
		// through to the manual/unit-test fallback below and folded by a null column. The dimension
		// tree handles it exactly: FoldDimLevel returns at once with nothing to fold, and the root
		// still gets the whole-snapshot aggregates, which IS the row that was asked for.
		if (!m_totalLevels.empty() || m_totalsOverall) {
			// Self-hierarchy fast path: a SINGLE Hierarchy level on the source's OWN parent column is
			// ROW-keyed (each catalog row is a node) — that is BuildHierarchyTree, rowKey from the
			// queryable (already in the snapshot), NO extra query.
			// ONE FIELD, because a hierarchy unfolds a single parent chain: a level keyed by a tuple
			// has no one chain to walk, and it takes the general road below.
			if (m_totalLevels.size() == 1 && m_totalLevels[0].IsSingleField()
			    && m_totalLevels[0].HeadDim() != ibDimensionKind::Elements && m_queryable != nullptr
			    && m_totalLevels[0].HeadCol() == m_queryable->GetHierarchyColumn()) {
				const std::vector<const ibBackendQueryColumn*> keys = m_queryable->GetPrimaryKeyColumns();
				if (!keys.empty() && keys.front() != nullptr)
					return ibQueryComposer::BuildHierarchyTree(m_snapshot, keys.front(), m_totalLevels[0].HeadCol(),
					                                           m_aggregates, m_totalLevels[0].HeadDim());
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
	// ⭐ A VISIT WITHOUT A NODE IS NOT A VISIT. `Next()` answers "is there a row here", and the reader
	// takes that answer literally: it reads the columns straight after. So the cursor must never
	// stand where `Current()` has nothing to return — every column would read NULL and the walk
	// would report a row made entirely of nothing (Max, 2026-08-22: "once the total, and the second
	// time a null node comes"). Skipping is the invariant, not a repair: what the walk hands out is
	// nodes, and an empty slot is not one.
	// ⭐ …AND A REFUSAL DOES NOT MOVE THE CURSOR. `Next()` answering false means "there is no next
	// row", not "you are now nowhere": the selection keeps standing on the last row it handed out,
	// so reading a column after the loop gives that row's values rather than a screenful of NULLs
	// (Max, 2026-08-22: "it should just return false, and the values stay as the last ones").
	// Advancing past the end and reporting it was two answers to one question, and the second was
	// indistinguishable from a real row made of nothing.
	bool Next()
	{
		EnsureWalk();
		for (long probe = m_pos; ++probe < static_cast<long>(m_visits.size()); )
			if (m_visits[static_cast<size_t>(probe)] != nullptr) {
				m_pos = probe;
				return true;
			}
		return false;
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
		const auto it = n->m_values.find(col->GetColumnId());
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
	// A HEADING OR A ROW — asked of the node, never guessed from the depth. A fold with a detail
	// level yields both in one walk, and only the node knows which of the two this visit is.
	ibSelectorNodeKind Kind() const { const ibSelectorTree::Node* n = Current(); return n ? n->m_kind : ibSelectorNodeKind::Group; }

	// GRAND TOTAL — lives IN the selection itself (the root of the folded tree), read without walking.
	// A column / an aggregate alias rolled over the WHOLE result. Absent → runtime NULL.
	ibValue GetTotal(const ibBackendQueryColumn* col) const
	{
		EnsureWalk();
		if (col == nullptr) return ibValue(ibValueTypes::TYPE_NULL);
		const auto it = m_walk->Root().m_values.find(col->GetColumnId());
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
	ibSelector Select(ibSelectKind kind = ibSelectKind::ibSelectKind_Direct) const;

	const ibQueryRamTable& Snapshot() const { return m_snapshot; }
	ibSelectKind           GetKind()  const { return m_kind; }

	// How many nodes this selection will hand out. Forces the fold, which is what the first Next()
	// would have done anyway — so asking costs nothing that was not going to be paid.
	long NodeCount() const { EnsureWalk(); return static_cast<long>(m_visits.size()); }

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
		// THE ROOT IS THE OVERALL ROW, and it comes FIRST — pre-order, and there is nothing above it.
		// Normally the walk starts at the root's CHILDREN: the root is the fold over everything, which
		// is a total, not a dimension value, and a report that did not ask for it would find a
		// mysterious empty first row. Asked for, it is exactly the row the author wanted.
		// ⭐⭐ OVERALL IS A LEVEL, not a row bolted on top. It is the grouping that names NO fields —
		// "as if you had set no dimensions at all" (Max, 2026-08-22) — and a grouping by nothing has
		// exactly ONE node covering the whole result: the tree's root. So when it is asked for, the
		// walk visits that node and nothing else, and the first dimension level is reached the same
		// way every other level is: by descending into the one above it.
		//
		// Said this way it needs no special case anywhere else. It used to be pushed IN FRONT of the
		// first level's nodes, which put two different levels in one cursor and made a descent into
		// the root print everything twice.
		if (m_totalsOverall) {
			m_visits.push_back(&m_walk->Root());
			m_pos = -1;
			return;
		}

		// ⭐⭐ ONE LEVEL, NOT THE WHOLE TREE. A selection visits the DIRECT CHILDREN of what it was
		// made over, and a node's own children are reached by descending into it — `s.Select()`. That
		// is what a grouping IS: three levels are three nested loops, not one loop over everything
		// (Max, 2026-08-22, watching the walk return the group's figure 62 and then 1, 2, 3, 4 in the
		// same cursor: "that is not needed here — the node that unfolds the value sees those").
		//
		// It used to flatten the whole subtree PRE-ORDER, which put every level and every detail row
		// into one cursor: the reader could tell them apart only by asking Level() on each visit, and
		// a script written the way the language reads — loop the groups, loop their rows — saw the
		// rows twice, once mixed into the outer loop.
		//
		// A DIRECT walk is unaffected: its tree is one flat layer of leaves under the root, so its
		// direct children ARE the rows.
		for (const auto& child : m_walk->Root().m_children)
			if (child != nullptr)          // an empty slot is not a row — see Next()
				m_visits.push_back(child.get());
		m_pos = -1;
	}

	ibQueryRamTable m_snapshot;                    // the raw snapshot this Selector traverses
	ibSelectKind    m_kind;                        // AXIS 1 — traversal
	ibDimensionKind m_dimKind = ibDimensionKind::Hierarchy;  // AXIS 2 — dimension unfold (for ByGroupsHierarchy)
	const ibBackendQueryColumn* m_rowKeyCol    = nullptr;   // parent-ref fold
	const ibBackendQueryColumn* m_parentKeyCol = nullptr;
	std::vector<const ibBackendQueryColumn*>       m_groupFields;   // grouping fold (manual ByGroups)
	std::vector<ibTotalLevel>                      m_totalLevels;   // totals dimensions (from the door, via WithTotals)
	bool                                           m_totalsOverall = false;  // walk the ROOT as a row (BY OVERALL)
	std::vector<ibDataQueryBuilder::AggregateItem> m_aggregates;
	// The tree the DBMS folded, when it did (see WithReadyTree). Shared because a selector is copied
	// on its way from the result to the caller, and the tree must not be copied with it.
	std::shared_ptr<ibSelectorTree>                m_readyTree;

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
		q.Where(c.m_col, c.m_op, c.m_value);                        // ONE op — Where carries any ibQueryFilterOp now
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

	// ⭐ THE CHILDREN ARE ALREADY THERE, when the fold built them. Hand over the node's subtree as a
	// tree of its own and let the sub-selection walk ITS direct children — no query, no re-fold, and
	// it works for a fold with no live source behind it, which the re-execution below cannot serve at
	// all. That is the ordinary case for a scripted TOTALS: everything was read once, folded once,
	// and a descent is a move inside what is already in hand.
	//
	// The re-execution road stays for what it was built for: a LAZY drill, where a node is expandable
	// but its children were deliberately not read yet.
	if (n != nullptr && !n->m_children.empty() && m_walk != nullptr) {
		ibSelector child(ibQueryRamTable{}, kind);
		child.WithReadyTree(std::make_shared<ibSelectorTree>(m_walk->SubtreeOf(*n)));
		child.m_baseLevel = m_baseLevel;   // the nodes keep their own levels; Level() stays absolute
		return child;
	}

	if (n == nullptr || m_rowKeyCol == nullptr) return ibSelector(ibQueryRamTable{}, kind);
	const auto it = n->m_values.find(m_rowKeyCol->GetColumnId());
	const ibValue key = (it != n->m_values.end()) ? it->second : ibValue();
	return MakeChild(key, n->m_level + m_baseLevel, kind);          // children continue DEEPER
}

#endif // __QUERY_SELECTOR_H__
