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
#include "queryRowCursor.h"     // ibQueryRowCursor — the rows as they arrive (what the group folds read)
#include "querySelectorTree.h"  // ibSelectorTree (the folded tree) + ibSelectorTree::Node
#include "queryProvider.h"      // ibQueryComposer::BuildHierarchyTree / BuildTotalsTree
#include "dataQueryBuilder.h"   // ibDataQueryBuilder + ibSelectKind + ibDimensionKind + AggregateItem

#include <algorithm>           // stable_sort — a level's own order over the nodes it hands out

class ibDatabaseConnectionHolder;   // sub-selection recipe holds it by pointer (Select(kind) only)

// ⭐⭐ THE ORDER ONE LEVEL COMES IN — and it belongs HERE because a selection is what holds a level.
//
// A level's sort is not the query's ORDER BY: the rows were already read and folded, and what is
// being ordered is the HEADINGS of one level — the nodes this selection hands out. Asking the query
// again would order the rows a second time and still leave the headings in fold order.
//
// A node is read two ways (GetValue by column, GetColumn by an aggregate's alias), so a key names
// itself the same two ways: sorting a level by one of its own fields, and sorting it by a figure
// ("the biggest customer first"), are one question with two spellings of the key.
struct ibSelectorSort {
	const ibBackendQueryColumn* m_col   = nullptr;   // a source column…
	wxString                    m_alias;             // …or an aggregate's alias
	bool                        m_ascending = true;
};

class ibSelector
{
public:
	// Takes OWNERSHIP of the snapshot (move-only). `kind` is the TRAVERSAL (AXIS 1), default Direct.
	explicit ibSelector(ibQueryRamTable&& snapshot, ibSelectKind kind = ibSelectKind::ibSelectKind_Direct)
		: m_snapshot(std::move(snapshot)), m_kind(kind) {}

	// ⭐ …OR OF A CURSOR, which is how a READ arrives: result.Select(kind) hands the rows over
	// unread, and a fold that streams (the group folds — see ibQueryComposer::BuildDimensionTree)
	// never materialises them at all. A fold that must address every row drains the cursor once, and
	// then this selection holds a snapshot exactly as it always did.
	explicit ibSelector(std::unique_ptr<ibQueryRowCursor> rows, ibSelectKind kind = ibSelectKind::ibSelectKind_Direct)
		: m_rows(std::move(rows)), m_kind(kind) {}

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

	// ⭐ WALK ONE BRANCH OF THE FORK — `SPLIT … ONTO <name>`. Said the same way OVERALL is, and for
	// the same reason: nothing is folded differently, this only decides WHICH of what was folded
	// this selection hands out. Empty (the default) walks every branch in order.
	ibSelector& WalkBranch(const wxString& branch) { m_branch = branch; return *this; }

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

		// ⭐⭐ THE STREAMING ROAD — the ordinary report, and the reason the rows travel as a cursor.
		// The dimension fold reads them one at a time (it drains the cursor itself, and says so, for
		// the reference-hierarchy levels that cannot be folded in one pass), so nothing here holds the
		// detail. The self-hierarchy fast path below is row-KEYED — a row IS a node there — so it
		// takes the snapshot road, which is the same cost by construction.
		if (m_rows && !SelfHierarchyFold() && (!m_totalLevels.empty() || m_totalsOverall)) {
			ibSelectorTree folded = ibQueryComposer::BuildDimensionTree(*m_rows, m_totalLevels, m_aggregates,
			                                                            m_holder, m_queryable);
			m_rows.reset();   // read once, and a spent cursor must not read back as rows
			return folded;
		}

		EnsureSnapshot();   // every fold below addresses rows by index — drain the cursor once

		// ⚠ OVERALL ALONE IS STILL A TOTALS FOLD. `TOTALS COUNT(x) BY OVERALL` asks for one row over
		// everything and names no dimension — so the levels are empty, and without this the walk fell
		// through to the manual/unit-test fallback below and folded by a null column. The dimension
		// tree handles it exactly: FoldDimLevel returns at once with nothing to fold, and the root
		// still gets the whole-snapshot aggregates, which IS the row that was asked for.
		if (!m_totalLevels.empty() || m_totalsOverall) {
			// Self-hierarchy fast path: a SINGLE Hierarchy level on the source's OWN parent column is
			// ROW-keyed (each catalog row is a node) — that is BuildHierarchyTree, rowKey from the
			// queryable (already in the snapshot), NO extra query.
			if (SelfHierarchyFold()) {
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

	// ⭐ THE ORDER THIS SELECTION HANDS ITS NODES OUT IN — a level's own sort, and nothing deeper.
	//
	// NOT inherited by a descent. `s.Select()` makes the selection over a node's children, and those
	// children are the NEXT level, which has a sort of its own (or none). Carrying this one down
	// would order a level by a key its author set for the one above it — the same class of mistake as
	// a filter that leaks a level down, and the caller states each level's order as it descends.
	//
	// Stated after the walk was built, it re-orders what is already there rather than refolding: the
	// nodes are the fold's, this only decides the sequence they are visited in.
	void OrderBy(std::vector<ibSelectorSort> keys)
	{
		m_order = std::move(keys);
		if (!m_visits.empty())
			ApplyOrder();
		m_pos = -1;   // a new order means the walk starts over — a position in the old one means nothing
	}

	// ⭐ READ THE ROWS NOW, and be done with the cursor.
	//
	// The fold is otherwise lazy — the first Next() builds it — and a selection that nobody has
	// walked yet is sitting on an OPEN read while the caller goes off and does the next thing on the
	// same connection. That is a state this house has paid for before (docs: the read-state arc), and
	// it is not what the streaming fold is for: the point is not to read LATER, it is not to KEEP.
	// So result.Select() folds here, in one pass, and releases the rows.
	void ReadRows() const { EnsureWalk(); }

	// ⭐ …AND THE TREE IT FOLDED THEM INTO, handed back so it can be folded ONCE for several walks.
	// A cursor is spent by the first scan, so a second selection over the same read has nothing left
	// to fetch — which is exactly what the branches of a shared read are (ibDataQueryResult::Select
	// (kind, branch)): one query, one fold, one tree, and each branch walking its own part of it.
	std::shared_ptr<ibSelectorTree> FoldedTree() const { EnsureWalk(); return m_walk; }

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
		const auto it = WalkRoot().m_values.find(col->GetColumnId());
		return it != WalkRoot().m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
	}
	ibValue GetTotalColumn(const wxString& alias) const
	{
		EnsureWalk();
		for (const ibQueryRamColumn& c : m_walk->Columns())
			if (c.m_name == alias) {
				const auto it = WalkRoot().m_values.find(c.m_id);
				return it != WalkRoot().m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
			}
		return ibValue(ibValueTypes::TYPE_NULL);
	}

	// LAZY sub-selection — "selection -> sub-selection": Execute the CURRENT visit's DIRECT children as
	// their own result (parent == the current node's row-key), with the inherited filter + the
	// fold/recipe, walked with the given `kind`. So `d = s.Select(kind); while (d.Next()) …` recurses
	// the tree on demand — each sub-level its own query, Level() continuing DEEPER. Requires
	// WithSource() + a parent-ref fold; empty Selector otherwise (no DB hit). Defined below.
	ibSelector Select(ibSelectKind kind = ibSelectKind::ibSelectKind_Direct) const;

	// ⭐ …AND ONE BRANCH OF WHAT IS UNDER IT — `SPLIT … ONTO ByCharacteristic`, asked for by that
	// name. The SAME walk, narrowed to one fork: everything else about the descent is unchanged,
	// which is why this is an argument and not a second kind of selection. Named nothing, the walk
	// goes through every branch in order — see CollectVisits.
	ibSelector Select(ibSelectKind kind, const wxString& branch) const;

	// THE ROWS AS A TABLE — draining the cursor first if nothing has needed them all at once yet.
	// Holding the whole detail is what the cursor exists to avoid, so this is for the callers that
	// genuinely want the table (a test, a fold that addresses rows by index) and it pays for it here.
	const ibQueryRamTable& Snapshot() const { EnsureSnapshot(); return m_snapshot; }
	ibSelectKind           GetKind()  const { return m_kind; }

	// How many nodes this selection will hand out. Forces the fold, which is what the first Next()
	// would have done anyway — so asking costs nothing that was not going to be paid.
	long NodeCount() const { EnsureWalk(); return static_cast<long>(m_visits.size()); }

private:
	const ibSelectorTree::Node* Current() const
	{
		return (m_pos >= 0 && m_pos < static_cast<long>(m_visits.size())) ? m_visits[static_cast<size_t>(m_pos)] : nullptr;
	}

	// ⭐ IS THIS THE ROW-KEYED SELF-HIERARCHY FOLD? A SINGLE Hierarchy / HierarchyOnly level on the
	// source's OWN parent column, where each row of the source IS a node. ONE FIELD, because a
	// hierarchy unfolds a single parent chain: a level keyed by a tuple has no one chain to walk.
	// Asked in one place — the fold below reads it, and so does the choice of road above it.
	bool SelfHierarchyFold() const
	{
		return m_totalLevels.size() == 1 && m_totalLevels[0].IsSingleField()
		    && m_totalLevels[0].HeadDim() != ibDimensionKind::Elements && m_queryable != nullptr
		    && m_totalLevels[0].HeadCol() == m_queryable->GetHierarchyColumn();
	}

	// THE ROWS, HOWEVER THIS SELECTION HOLDS THEM — a live cursor when one was handed over, the
	// snapshot read as one otherwise. Everything below reads rows through this, so no fold has to
	// know which of the two it got.
	void EnsureSnapshot() const
	{
		if (m_rows) {
			m_snapshot = ibDrainToRamTable(*m_rows);
			m_rows.reset();
		}
	}

	// Direct fold — each row becomes a leaf node under the root (no subtotals, no nesting). Straight
	// off the cursor when there is one: a flat walk holds a node per row either way, and draining
	// into a table first would hold every row TWICE.
	ibSelectorTree BuildFlat() const
	{
		ibRamTableCursor own(m_snapshot);
		ibQueryRowCursor& rows = m_rows ? *m_rows : static_cast<ibQueryRowCursor&>(own);

		ibSelectorTree t;
		for (const ibQueryRamColumn& c : rows.Columns()) t.AddColumn(c.m_id, c.m_name, c.m_type);
		while (rows.Next()) {
			ibSelectorTree::Node* leaf = t.Root().AddChild(1);
			for (const ibQueryRamColumn& c : rows.Columns())
				leaf->m_values[c.m_id] = rows.Get(c.m_id);
		}
		m_rows.reset();   // the rows are in the tree now — a spent cursor must not read back as rows
		return t;
	}

	// Sub-selection core: re-Execute the children of `parentKeyValue` with the inherited filter +
	// fold/recipe, the new Selector walked with `kind`, Level() based at `childBaseLevel`.
	ibSelector MakeChild(const ibValue& parentKeyValue, int childBaseLevel, ibSelectKind kind) const;

	// WHERE THIS SELECTION STARTS: the tree's own root, or the node a descent was made over. One
	// place answers it, so nothing below has to remember which kind of selection it is in.
	const ibSelectorTree::Node& WalkRoot() const
	{
		return m_viewNode != nullptr ? *m_viewNode : m_walk->Root();
	}

	// A NODE'S VALUE FOR A SORT KEY — the same two readings the walker uses (GetValue / GetColumn),
	// asked of a node that is not the current one. Absent = the runtime's NULL, so a level whose
	// nodes do not carry the key sorts as "all equal" rather than as an error: the key may name a
	// figure this level does not roll up, and a report with a stray sort line still prints.
	ibValue NodeValueFor(const ibSelectorTree::Node* node, const ibSelectorSort& key) const
	{
		if (node == nullptr)
			return ibValue(ibValueTypes::TYPE_NULL);
		ibMetaID id = 0;
		if (key.m_col != nullptr)
			id = key.m_col->GetColumnId();
		else if (m_walk) {
			for (const ibQueryRamColumn& c : m_walk->Columns())
				if (c.m_name == key.m_alias) { id = c.m_id; break; }
		}
		if (id == 0)
			return ibValue(ibValueTypes::TYPE_NULL);

		// ⚠ A HEADING CARRIES ITS KEY AND ITS FIGURES — AND NOTHING ELSE. Every other column of the
		// rows lives on the DETAIL nodes, and a report that never declared a details level has none
		// of those at all. So this answers for the key of the level and for an aggregate, and says
		// NULL for anything else — which is why a sort by an ordinary FIELD does not come here: it is
		// stated as the order the detail is READ in, and the group then stands where its first row
		// does (dataComposer, AppendSettingsClauses).
		const auto it = node->m_values.find(id);
		return it != node->m_values.end() ? it->second : ibValue(ibValueTypes::TYPE_NULL);
	}

	// ⭐ ONE COMPARISON FOR THE WHOLE HOUSE. The keys are compared by the same function a RAM ORDER BY
	// compares rows with (ibQueryComposer::RamSortCompareKey) — a second answer to "which of these two
	// values comes first" is how a level's headings and the rows under them start disagreeing.
	//
	// STABLE, so nodes the sort cannot tell apart keep the order the fold gave them — which is the
	// order the rows arrived in, and the only one anybody can predict.
	void ApplyOrder() const
	{
		if (m_order.empty() || m_visits.size() < 2)
			return;
		std::stable_sort(m_visits.begin(), m_visits.end(),
			[this](const ibSelectorTree::Node* a, const ibSelectorTree::Node* b) {
				for (const ibSelectorSort& key : m_order) {
					const int c = ibQueryComposer::RamSortCompareKey(
						NodeValueFor(a, key), NodeValueFor(b, key), key.m_ascending);
					if (c != 0) return c < 0;
				}
				return false;
			});
	}

	void EnsureWalk() const
	{
		// A VIEW is already built — it is somebody else's tree, entered at a node. Nothing to fold,
		// nothing to copy: the visits are that node's children, read where they already live.
		if (m_viewNode != nullptr) {
			if (!m_visits.empty() || m_pos != -1)
				return;                    // already prepared
			CollectVisits(*m_viewNode);
			ApplyOrder();
			m_pos = -1;
			return;
		}

		if (m_walk) return;
		m_walk = std::make_shared<ibSelectorTree>(Build());
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
			m_visits.push_back(&WalkRoot());
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
		CollectVisits(WalkRoot());
		ApplyOrder();
		m_pos = -1;
	}

	// ⭐⭐ THE CHILDREN OF ONE NODE — and a FORK is walked THROUGH, not walked ON.
	//
	// `SPLIT` puts a branch node between a heading and the levels under it. Asked for what is under
	// the heading without naming a branch, this goes through the forks in order: the first branch
	// whole, then the next (Max, 2026-08-27). So a reader that never heard of branches — every
	// script and every driver written before them — still sees every group there is, rather than
	// stopping at a node that carries no key and no figure of its own.
	//
	// Name one and it is the only one this selection walks: `Select(ByGroups, "ByCharacteristic")`
	// is the same walk, narrowed. A name nobody answers to yields an EMPTY selection rather than
	// quietly falling back to all of them — asking for a branch that is not there is a mistake worth
	// seeing, and the empty walk is where it shows.
	void CollectVisits(const ibSelectorTree::Node& parent) const
	{
		for (const auto& child : parent.m_children) {
			if (child == nullptr)          // an empty slot is not a row — see Next()
				continue;
			if (child->m_kind == ibSelectorNodeKind::Branch) {
				if (!m_branch.IsEmpty() && !child->m_branch.IsSameAs(m_branch, false))
					continue;              // a branch was named, and this is not it
				CollectVisits(*child);     // the fork itself is not a visit — what is under it is
				continue;
			}
			// ⚠ NAMED A BRANCH, SO ONLY THAT BRANCH. Where a heading holds both records of its own
			// and forks under them, asking for one branch must not also hand back the records that
			// stand beside it — they belong to the ladder above, not to the branch.
			//
			// ⭐ ASKED OF THE NODE, which carries the branch it stands in (the fold stamps it down
			// the whole subtree, not on the fork alone). So this reads the same at any depth: the
			// walk that has already descended past a fork still knows whose nodes these are, which
			// is what a DESCENT inside a branch needs — and it is the ordinary case, since a report
			// that prints a grand total enters at the root and walks down from there.
			if (m_branch.IsEmpty() || child->m_branch.IsSameAs(m_branch, false))
				m_visits.push_back(child.get());
		}
	}

	// THE ROWS — as a live cursor while nobody has needed them all at once, as a snapshot after a
	// fold that does. Mutable for the same reason m_walk is: both are the lazy build, and Build() is
	// const because ASKING a selection what it holds must not read as changing it.
	mutable std::unique_ptr<ibQueryRowCursor> m_rows;
	mutable ibQueryRamTable m_snapshot;            // the raw snapshot this Selector traverses (drained on demand)
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
	// ⭐ SHARED, not owned alone — a descent is a VIEW of this tree, not a copy of part of it. See
	// ibSelector::Select: the child holds this same tree plus the node it starts at, so entering a
	// heading costs a pointer instead of a deep copy of everything under it. Heap either way, because
	// Node addresses must stay stable across a move of the selector.
	mutable std::shared_ptr<ibSelectorTree>          m_walk;

	// A SUB-SELECTION'S STARTING NODE, inside `m_walk` above. Null = this selection walks the tree's
	// own root; set = it walks that node's children, and its "grand total" is that node's figures.
	// The tree is kept alive by the shared pointer beside it, so the parent may be gone.
	const ibSelectorTree::Node*                      m_viewNode = nullptr;
	// WHICH BRANCH THIS SELECTION WALKS — empty = all of them, in order, the forks walked through.
	// It is state of the SELECTION and not of the tree: two readers may descend into one heading and
	// take different branches of it, and the tree they share knows nothing about either.
	wxString                                         m_branch;
	mutable std::vector<const ibSelectorTree::Node*>  m_visits;  // pre-order visit order
	mutable long                                      m_pos = -1;
	// THE LEVEL'S OWN ORDER — empty = fold order, which is the order the rows arrived in. Not mutable
	// and not lazy: it is stated, not derived, and a descent starts without one (see OrderBy).
	std::vector<ibSelectorSort>                       m_order;
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
	child.ReadRows();   // configured — so fold it now and let go of the read (see ReadRows)
	return child;
}

inline ibSelector ibSelector::Select(ibSelectKind kind) const
{
	const ibSelectorTree::Node* n = Current();

	// ⭐ THE CHILDREN ARE ALREADY THERE, when the fold built them. The sub-selection is a VIEW of this
	// same tree, entered at this node — no query, no re-fold, and no copy. That is the ordinary case
	// for a scripted TOTALS: everything was read once, folded once, and a descent is a move inside
	// what is already in hand.
	//
	// ⚠ A VIEW, NOT A SUBTREE COPY, and the difference is the difference between a report that runs
	// and one that does not. The first shape of this copied the node's subtree and then Build()
	// cloned it again — two deep copies per heading, each carrying every node's value map. A report
	// that descends into every group over a hundred thousand rows would copy hundreds of thousands of
	// nodes, where the old flat walk copied none. Sharing the tree costs a pointer, and the shared
	// pointer is what makes it safe: the child keeps the tree alive whether or not the parent does.
	//
	// The re-execution road stays for what it was built for: a LAZY drill, where a node is expandable
	// but its children were deliberately not read yet.
	if (n != nullptr && !n->m_children.empty() && m_walk != nullptr) {
		ibSelector child(ibQueryRamTable{}, kind);
		child.m_walk     = m_walk;      // the same tree, kept alive by the pointer
		child.m_viewNode = n;           // …entered here
		child.m_baseLevel = m_baseLevel;   // the nodes keep their own levels; Level() stays absolute
		// ⭐ AND IT STAYS IN THE SAME BRANCH. A descent goes DEEPER along the road it is already on —
		// walking one branch and then descending into everything would be two different walks under
		// one name. Dropped here, a report's every table printed every table's headings.
		child.m_branch   = m_branch;
		return child;
	}

	if (n == nullptr || m_rowKeyCol == nullptr) return ibSelector(ibQueryRamTable{}, kind);
	const auto it = n->m_values.find(m_rowKeyCol->GetColumnId());
	const ibValue key = (it != n->m_values.end()) ? it->second : ibValue();
	return MakeChild(key, n->m_level + m_baseLevel, kind);          // children continue DEEPER
}

// ONE BRANCH OF THE DESCENT. The descent itself is the one above — the branch is carried on the
// selection it produces and read where the visits are gathered, so nothing here duplicates it.
//
// ⚠ IT REACHES THE VIEW ROAD ONLY, and honestly so: a LAZY drill re-executes a query for a node's
// children, and a query that has not been run has no branches to choose between yet. Branches come
// out of a fold, so they are addressable exactly where a fold has happened.
inline ibSelector ibSelector::Select(ibSelectKind kind, const wxString& branch) const
{
	ibSelector child = Select(kind);
	child.m_branch = branch;
	return child;
}

#endif // __QUERY_SELECTOR_H__
