////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : DB value-model (L5-1 fetch)
////////////////////////////////////////////////////////////////////////////
//
// The DB half of the value-model, split out of tableInfo.cpp: ibValueModelCursor::RunComposerPage (render the
// settings → SQL → keyset page → walk → COPY nodes; hierarchy drill via GROUPING) + ibValueModel::ResolveAnchorByKey
// (the point lookup a FindRowValue restore stub positions on). A DB model reads dynamically from the cursor — no
// stored rows. The RAM half lives in tableInfoRam.cpp; the shared ibValueModel base + the node lives in tableInfo.cpp.

#include "tableInfo.h"

#include "backend/backend_exception.h"             // ibBackendException — the fetch's catch/restore-scope guard
#include "backend/tableView.h"                     // s_constIgnoreParent / ibDataViewItem
#include "backend/composition/dataComposer.h"      // ibDataDBComposer — render → SQL
#include "backend/composition/listFetchDriver.h"   // ibListFetchDriver — the universal composer-fetch sink
#include "backend/query/dataQueryBuilder.h"        // ibReadPageRequest — the page envelope
#include "backend/system/value/valueType.h"        // ibValueTypeDescription::AdjustValue — typed empty parent ref (hierarchy roots)
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — drilled folder guid
#include "backend/uniqueKey.h"                      // ibUniqueKey — GetItemKey builds the row's reference key


// ibValueModelCursor::RunComposerPage — the DB fetch: render the composer → SQL → keyset page → walk → COPY nodes;
// hierarchy drill. NO stored row here — the DB reads dynamically from the cursor.
unsigned int ibValueModelCursor::RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibFetchDirection dir, ibDataViewItemArray& out) const
{
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr)
		return 0;

	// A FLAT List view of a hierarchical source passes the ignore-parent SENTINEL (it is "ok" but is NOT a real
	// row node — never GetViewData it): "walk the WHOLE table, no parent scope". Empty parent = top-level.
	const bool flatView = (parent == s_constIgnoreParent);

	// Grouping dimensions (field paths) + their KIND (the VID: Elements / Hierarchy / HierarchyOnly) — read
	// from the COMPOSER (the committed store; set by the ctor / settings-dialog commit), skip empty lines. A
	// Hierarchy/HierarchyOnly dim makes that level unfold the reference's parent tree.
	//
	// The VIEW MODE decides flat-vs-grouped, exactly as the RAM half does (tableInfoRam.cpp): a flat LIST view
	// passes the ignore-parent SENTINEL → grouping is OFF (the user chose "Плоский" → a flat table even with a
	// grouping configured, so the flat toggle always wins over a stored grouping); a TREE / Hierarchical view
	// passes an empty/real parent → grouping is ON. Populating dims in a flat view drove groupLevel=true there,
	// so a flat List of a grouped source ran a TOTALS read that returned the whole tree pre-order → level-0-only
	// trim left zero rows → "everything disappears". Gating on !flatView keeps the flat list flat.
	std::vector<wxString>         dims;
	std::vector<ibQueryDimUnfold> dimKinds;
	if (!flatView) {
		for (size_t i = 0; i < m_composer.GroupCount(); ++i) {
			wxString f; ibQueryDimUnfold k = ibQueryDimUnfold::Elements;
			if (m_composer.GetGroupAt(i, f, k) && !f.IsEmpty()) {
				dims.push_back(f);
				dimKinds.push_back(k);
			}
		}
	}

	// The already-drilled dimension values (the scope) — read from the browsed parent node's group path.
	// GetViewData is a static_cast, so guard on a real, ok parent node.
	std::vector<ibValue> parentPath;
	if (!dims.empty() && parent.IsOk() && !flatView)
		if (const ibComposerNode* pnode = GetViewData<ibComposerNode>(parent))
			parentPath = pnode->GetGroupPath();
	const size_t depth      = parentPath.size();
	const bool   grouping   = !dims.empty();

	// SELF-HIERARCHY is a PROPERTY OF THE QUERYABLE (it carries a hierarchy / parent column): the source is
	// INHERENTLY a tree (a folder catalog / chart of accounts), so with NO user grouping it shows the parent
	// tree, each row a DETAIL folder node whose children are rows parented on it. GROUPING-REPLACES-TREE rule (the
	// user's spec): a user grouping REPLACES the inherent folder tree — the folder tree shows ONLY when no grouping
	// is configured. So a user grouping switches hierarchy OFF (`!grouping`), and groupLevel then drives the flat-scan +
	// TotalBy path a non-hierarchical document takes: the catalog is grouped by the field, folders no longer
	// special. hierarchy is ON only in a Tree/Hierarchical view (not flatView) of a source with a hierarchy
	// column AND with no grouping configured.
	const bool hierarchy = q->GetHierarchyColumn() != nullptr && !flatView && !grouping;
	const bool groupLevel = grouping && !hierarchy && depth < dims.size();

	ibValue hierParentKey;       // the browsed folder's self-reference (scope for its children)
	bool    hierHasParent = false;
	if (hierarchy && parent.IsOk())
		if (const ibComposerNode* pnode = GetViewData<ibComposerNode>(parent)) {
			const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();
			if (!keyCols.empty() && keyCols.front() != nullptr) {
				pnode->GetValue(keyCols.front()->GetColumnId(), hierParentKey);
				hierHasParent = true;
			}
		}

	// KEYSET-vs-DOT-WALK-SORT incompatibility: when a sort field is a dot-walk (reference) path — e.g.
	// "Reference.DataVersion" — the ORDER BY rides the JOINED column, but the keyset predicate rides the main-
	// table PK (BuildAnchorPredicate deliberately skips a joined column, dbTableProvider.cpp) → the two diverge,
	// so `PK > anchorPk` returns rows already shown under the ORDER BY → the page windows OVERLAP (duplicate
	// rows). A joined column can't be a keyset key, so we fetch the WHOLE ordered result in ONE shot: unbounded
	// on the initial (unanchored) read, nothing on any keyset continuation. (Detail-level only — a grouped read
	// runs the TOTALS path.) The trade: a dot-walk sort loads all rows up front (user-chosen, so acceptable).
	bool dotWalkSort = false;
	if (!groupLevel) {
		for (size_t i = 0; i < m_composer.SortCount(); ++i) {
			wxString f; bool a;
			if (m_composer.GetSortAt(i, f, a) && GetColumnIDByName(f) == wxNOT_FOUND) { dotWalkSort = true; break; }
		}
	}
	if (dotWalkSort && anchor.IsOk())
		return 0;   // the one-shot initial read already served the whole ordered result

	// Build the page envelope from (anchor, count, direction) — same shape the
	// dynamic list builds. +1 probe row -> the buffer learns there is more.
	ibReadPageRequest page;
	page.m_direction   = dotWalkSort ? ibFetchDirection::Reset : dir;
	page.m_count       = dotWalkSort ? 0 : ((count > 0 ? count : defaultCountPerPage) + 1);   // 0 = unbounded; else +1 probe
	page.m_reverseSort = (page.m_direction == ibFetchDirection::Backward);
	page.m_flatScan   = true;
	page.m_isTopLevel = true;
	// HIERARCHY = a property of the QUERYABLE: a source with a parent column (GetHierarchyColumn — the default,
	// non-removable hierarchy column) IS hierarchical. In hierarchy view we drive the level fetch through the
	// page envelope's PARENT SCOPE — the provider fetches ONE level (the roots when top-level, a folder's
	// children when scoped) off that column, marks hasChildren, and resolves the empty-parent roots NATIVELY
	// at L3 (no empty reference to construct). This is the mechanism the dynamic list used before the arc; a
	// user grouping (Elements) still layers ON TOP through the composer below; a flat view keeps m_flatScan.
	if (hierarchy && q->GetHierarchyColumn() != nullptr) {
		page.m_parentFilter = true;
		page.m_parentCol    = q->GetHierarchyColumn();
		page.m_flatScan     = false;
		page.m_isTopLevel   = !hierHasParent;
		// DRILL: the browsed folder's OWN guid is the scope — its children = rows whose parent == it. Without
		// it (top level → invalid guid) the provider returns the ROOTS again under every expanded folder (the
		// "doubling"). hierParentKey is the folder's PK self-reference; pull the guid off it.
		if (hierHasParent) {
			ibValueReferenceDataObject* refObj = nullptr;
			if (hierParentKey.ConvertToValue(refObj) && refObj != nullptr)
				page.m_parentGuid = refObj->GetGuid();
		}
	}
	if (anchor.IsOk()) {
		page.m_hasAnchor = true;
		ibValueModel::ibComposerNode* a = GetViewData<ibValueModel::ibComposerNode>(anchor);
		// A real fetched anchor carries its sort+identity values inline. A FindRowValue restore STUB carries ONLY
		// its row-key (the PK) — resolve the rest with ONE point lookup keyed by that PK so the keyset predicate
		// positions the page AT the row. (This is where the old per-list FindRowValue sort pre-reading moved —
		// into L5, source-agnostic, keyed.) Empty resolve → degrades to a top-of-list fetch, never a crash.
		std::map<ibMetaID, ibValue> resolved;
		if (a != nullptr && a->IsKeyOnlyAnchor())
			resolved = ResolveAnchorByKey(a->GetRowKey());
		const auto anchorValue = [&](const ibMetaID col) -> ibValue {
			if (!resolved.empty()) {
				const auto it = resolved.find(col);
				return it != resolved.end() ? it->second : ibValue();
			}
			ibValue v;
			if (a != nullptr) a->GetValue(col, v);
			return v;
		};
		// USER sort values — resolve each sort field NAME → col-id (sort is L5, by name), read off the node.
		for (size_t i = 0; i < m_composer.SortCount(); ++i) {
			wxString field; bool asc;
			if (!m_composer.GetSortAt(i, field, asc)) continue;
			const ibMetaID col = GetColumnIDByName(field);
			if (col == wxNOT_FOUND) continue;
			page.m_anchorSortValues.push_back(anchorValue(col));
		}
		// ... PLUS the row's PRIMARY KEY as the cursor tail (matches EffectiveSort). Pull each key column's value
		// STRAIGHT from the node by its column id — the node carries its key RAW (a catalog/document = one data-
		// reference; a register = recorder+line+period / period+dimensions). No conversion, no stringifying: the
		// keyset (BuildAnchorPredicate) encodes a reference value to its real _RRRef blob by itself. Skip a key
		// column the user already sorts by (mirrors EffectiveSort's dedup).
		for (const ibBackendQueryColumn* pk : q->GetPrimaryKeyColumns()) {
			if (pk == nullptr) continue;
			bool dup = false;
			for (size_t j = 0; j < m_composer.SortCount(); ++j) {
				wxString f; bool a2;
				if (m_composer.GetSortAt(j, f, a2) && GetColumnIDByName(f) == pk->GetColumnId()) { dup = true; break; }
			}
			if (dup) continue;
			page.m_anchorSortValues.push_back(anchorValue(pk->GetColumnId()));
		}
	}

	// Group-level paging within ONE level is a follow-up: the TOTALS read returns the whole level at
	// once, so a continuation (anchored) fetch returns nothing rather than re-emitting it (which would
	// duplicate rows). Detail rows page normally below (parity with RunPage).
	if (groupLevel && anchor.IsOk())
		return 0;

	// ONE composer for THIS fetch (the persistent settings STORE — source + filter + sort + grouping). The fetch
	// does NOT clear it; it overlays the per-fetch DRILL of the browsed parent transiently: take the grouping
	// config OUT, render just the browsed level's TotalBy + the scope filter(s), run, then put it back.
	ibDataDBComposer& composer = m_composer;
	const ibDataComposer::SettingsScope scope = composer.MarkScope();
	const std::vector<std::pair<wxString, ibQueryDimUnfold>> savedGroups = composer.TakeGroups();
	// SELF-HIERARCHY is driven by the page envelope's parent scope (set above) + the provider — NOT by the
	// composer (TOTALS BY Parent HIERARCHY grouped by the parent VALUE: it buried the items at tree level 1
	// with only the dimension, the diagnostic log proved it). So the composer renders a PLAIN level SELECT
	// (TakeGroups dropped the grouping) and the provider scopes it to the parent. Only an EXPLICIT user
	// grouping (Elements) drills through the composer here, layered on top of the base parent-hierarchy.
	if (grouping && !hierarchy) {
		for (size_t k = 0; k < depth && k < dims.size(); ++k)
			composer.Filter(dims[k], wxT("="), parentPath[k]);
		if (groupLevel)
			composer.TotalBy(dims[depth], dimKinds[depth]);   // VID drives Elements / Hierarchy / HierarchyOnly
	}
	// flat: no drill — the persistent filter + sort render as-is.

	// Run the composer onto the generic list-fetch sink — the driver carries the page
	// envelope in and accumulates attribute-keyed rows out.
	ibListFetchDriver driver(page);
	try {
		composer.Run(driver);
	}
	catch (const ibBackendException&) {
		composer.PutGroups(savedGroups);
		composer.RestoreScope(scope);
		throw;
	}

	// Undo the transient drill — the persistent settings are exactly as they were before this fetch.
	composer.PutGroups(savedGroups);
	composer.RestoreScope(scope);

	// Wrap each driver row in a generic composer node, honouring count + direction
	// exactly as RunPage does: trim the extra probe row, reverse on Backward.
	std::vector<ibListFetchDriver::Row>& rows = driver.Rows();
	if (static_cast<int>(rows.size()) > count && count > 0)
		rows.pop_back();                       // drop the +1 probe
	if (dir == ibFetchDirection::Backward)
		std::reverse(rows.begin(), rows.end());

	// At a GROUP level the dimension being grouped is dims[depth]; the DISPLAY column that renders the group
	// header + the drill scope key by its column id. A PLAIN dimension resolves through the model column
	// collection AND the totals result carries its value under that same id — read straight off the row. A
	// DOT-WALK dimension ("Reference.DataVersion") does NOT: the totals lowering groups it under a SYNTHETIC id
	// (so its leaf can't clash with the main table on a self-reference), so the value rides under that synthetic
	// id, NOT the display leaf's real id. So (a) resolve the REAL leaf id by walking the path through the
	// queryable's references (this is the dot-path display column's own model id), and (b) flag it so we re-key
	// the value from the synthetic id to the leaf id per group row below (the composer computed the value; we
	// just route it to where the display + drill read it — no per-row dot re-resolution on a group header).
	ibMetaID groupDimCol   = ibMetaID(wxNOT_FOUND);
	bool     dotWalkDim    = false;
	if (groupLevel) {
		groupDimCol = GetColumnIDByName(dims[depth]);
		if (groupDimCol == wxNOT_FOUND) {
			const ibBackendQueryable* walk = q;
			wxString rest = dims[depth];
			const ibBackendQueryColumn* leaf = nullptr;
			while (walk != nullptr && !rest.IsEmpty()) {
				leaf = walk->ResolveColumnByName(rest.BeforeFirst(wxT('.')));
				rest = rest.AfterFirst(wxT('.'));
				if (leaf == nullptr) break;
				if (!rest.IsEmpty()) walk = walk->ResolveReferenceTarget(leaf);   // hop into the reference target
			}
			if (leaf != nullptr && rest.IsEmpty()) {
				groupDimCol = leaf->GetColumnId();
				dotWalkDim  = true;
			}
		}
	}
	// A dot-walk dimension's grouped value rides under a totals-lowering SYNTHETIC column id (queryLowering.cpp
	// kSyntheticColumnBase). Keep in sync: dim synthetics sit at/above this base (aggregate synthetics sit lower,
	// at 0x40000000), so a single dot-walk group row carries exactly one key here = its dimension value.
	const ibMetaID kDimSyntheticBase = 0x50000000;

	// The source's PRIMARY-KEY columns — stamp each DB-list DETAIL copy's stable identity (m_rowKey) so it
	// survives re-fetch selection AND a guid-keyed FindRowValue stub matches a fetched row by it. (RAM never
	// reaches here — its short-circuit above returns the LIVE storage rows, identified by their stable pointer.)
	const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();

	// The FOLDER-flag column (catalog IsFolder / ЭтоГруппа): a row with it set is a CONTAINER — a folder,
	// shown drillable even when empty (the folder convention) — vs a leaf item. Resolved once; null for an item-only
	// hierarchy or a flat source (then the driver row's hasChildren decides containerness).
	const ibBackendQueryColumn* folderCol = q->GetFolderColumn();

	// `this` is const here (fetch READS + returns rows). The node holds COPIES of the row values and
	// overrides IsAttached() -> true, so there is NO owner pin and NO const_cast on the model state.
	unsigned int fetched = 0;
	out.Alloc(rows.size());
	for (const ibListFetchDriver::Row& r : rows) {
		// LAZY drill: a TOTALS result arrives pre-order over EVERY level, but the paged control wants only the
		// CURRENT scope's TOP level per fetch — deeper rows load when the user drills into a node. The TOP level
		// is NOT the same number for the two shapes: BuildDimensionTree folds from Root (m_level 0), so the first
		// grouping level's group headers sit at m_level == 1 (queryProvider.cpp FoldDimLevel: AddChild(m_level+1));
		// a flat / DETAIL fetch (no TotalBy → hasTotals=false) emits every row at level 0. Filtering `!= 0`
		// therefore dropped EVERY group header (they are level 1) → grouping produced zero rows and the list
		// "disappeared". Keep level 1 for a group fetch, level 0 for a flat/detail one.
		if (r.m_level != (groupLevel ? 1 : 0))
			continue;
		ibComposerNode* node = nullptr;
		if (groupLevel) {
			// A GROUP node: its identity / scope is the parent path + THIS group's own dimension value, and it
			// drills one dimension deeper (or into the detail rows). DRILLABLE container only when the dimension
			// value was stamped into the path (else a drill re-enters at depth 0 → groupLevel fires again →
			// infinite re-grouping).
			std::map<ibMetaID, ibValue> values = r.m_values;
			ibValue dimValue = (groupDimCol != wxNOT_FOUND) ? values[groupDimCol] : ibValue();
			if (dotWalkDim) {
				// The value rides under the synthetic dim id, not the leaf id the display + drill key by. Pull it
				// (the sole synthetic-range key on a group row) and RE-KEY it under the real leaf id, so
				// GetValueByRow(group, dot-path column) reads the composer's grouped value directly.
				for (const auto& kv : r.m_values)
					if (kv.first >= kDimSyntheticBase) { dimValue = kv.second; break; }
				if (groupDimCol != wxNOT_FOUND)
					values[groupDimCol] = dimValue;
			}
			std::vector<ibValue> groupPath = parentPath;
			if (groupDimCol != wxNOT_FOUND)
				groupPath.push_back(dimValue);
			node = new ibComposerNode(values, groupPath, /*container*/ groupDimCol != wxNOT_FOUND);
		}
		else {
			// A DETAIL row (DB-list copy): keeps its PRIMARY-KEY row-key (re-fetch selection survival + FindRowValue
			// stub matching) and renders drillable through the SAME node (no subclass) when it is a CONTAINER: a
			// FOLDER (IsFolder set — drillable even empty, the folder convention) or, for an item-only hierarchy, a row the
			// provider flagged hasChildren. (Read-only — no write-back source; editing a DB list goes via the form.)
			std::vector<ibValue> rowKey;
			rowKey.reserve(keyCols.size());
			for (const ibBackendQueryColumn* kc : keyCols)
				if (kc != nullptr) rowKey.push_back(r.GetValue(kc->GetColumnId()));
			// A grouped DETAIL row is a LEAF. Under the grouping-replaces-tree rule a user grouping REPLACES the inherent folder
			// tree (hierarchy is OFF whenever grouping is on), so a folder that lands inside a group is a plain
			// grouped item — NOT drillable. Marking it a container by folderCol re-offered a folder expander whose
			// drill re-entered RunComposerPage with an empty group path (depth 0) → groupLevel fired again → the
			// whole grouping tree nested under the folder ("infinite" re-grouping). Only a non-grouped hierarchy /
			// flat fetch honours folderCol / hasChildren for containerness.
			const bool isContainer = grouping
				? false
				: (folderCol != nullptr
					? r.GetValue(folderCol->GetColumnId()).GetBoolean()
					: r.m_hasChildren);
			node = new ibComposerNode(r.m_values, isContainer, std::move(rowKey));
		}
		out.Add(ibDataViewItem(node));   // ctor IncRefs to 2
		node->DecRef();                  // -> `out` owns exactly one reference per row
		++fetched;
	}
	for (size_t i = 0; i < out.GetCount(); ++i) SetItemParent(out[i], parent); return fetched;
}

// Point lookup for a FindRowValue restore STUB — fetch the single source row matching `rowKey` (its PK value(s))
// and return its value map, so RunComposerPage can fill the anchor's sort tuple and the keyset positions the page
// AT the row. Same composer/driver path as the page fetch, run SEQUENTIALLY before it (its own MarkScope /
// RestoreScope), so there is no reentrancy on the persistent store. This is where the old per-list FindRowValue
// sort-value pre-reading lives now — ONCE, source-agnostic, keyed by the row-key.
std::map<ibMetaID, ibValue> ibValueModel::ResolveAnchorByKey(const std::vector<ibValue>& rowKey) const
{
	std::map<ibMetaID, ibValue> out;
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr || rowKey.empty())
		return out;
	const std::vector<const ibBackendQueryColumn*> pk = q->GetPrimaryKeyColumns();
	if (pk.size() != rowKey.size())
		return out;   // shape mismatch — cannot position by this key (degrades to a top-of-list fetch)

	// Transient point query over the SAME store: filter PK == key, drop grouping, fetch one row. MarkScope +
	// TakeGroups out / PutGroups + RestoreScope back, so the persistent filter / sort / grouping are untouched.
	ibDataComposer& composer = GetModelComposer();
	const ibDataComposer::SettingsScope scope = composer.MarkScope();
	const std::vector<std::pair<wxString, ibQueryDimUnfold>> savedGroups = composer.TakeGroups();
	for (size_t i = 0; i < pk.size(); ++i)
		if (pk[i] != nullptr)
			composer.Filter(pk[i]->GetName(), wxT("="), rowKey[i]);

	ibReadPageRequest page;
	page.m_count      = 2;        // 1 row + the probe row RunComposerPage's envelope keeps
	page.m_flatScan   = true;
	page.m_isTopLevel = true;
	ibListFetchDriver driver(page);
	composer.Run(driver);

	composer.PutGroups(savedGroups);
	composer.RestoreScope(scope);

	const std::vector<ibListFetchDriver::Row>& rows = driver.Rows();
	if (!rows.empty())
		out = rows.front().m_values;
	return out;
}

// ibValueModelCursor::GetItemKey — the DB row's identity key = its primary-key REFERENCE (guid). Read the item's
// node, take the first PK column's value (the row's own-reference cell) and pull its guid. Empty when there is no
// node / no keyed source / the cell is not a reference (a register's composite-PK row lands here too, but the
// register subclass overrides GetItemKey to build the dimension composite instead of a single guid).
ibUniqueKey ibValueModelCursor::GetItemKey(const ibDataViewItem& item) const
{
	return ibUniqueKey();
}

// ibValueModelCursor::BuildAncestorBreadcrumb — the ancestor chain (immediate parent → … → root) of the row, so a
// Hierarchical / Tree view drills to a selection sitting inside a sub-level after a view-mode switch (the base is a
// stub, so a DB catalog lost such a selection: crumb=0 → the fetch showed the top level, not the level the row
// lives in). out[0] = the immediate parent (= GetEffectiveFetchParent's fetch scope); out.back() = the root.
//
// Two shapes, matching the two list layouts (a user grouping REPLACES the folder tree):
//   • GROUPED: the row's ancestors are its GROUP nodes — one per grouping dimension, keyed by the row's own value
//     for that dim. A crumb's group PATH matches the fetched group node's, so the drill scopes into the group.
//   • FOLDER hierarchy (no grouping): walk the queryable's parent-ref column UP, one point lookup per level.
void ibValueModelCursor::BuildAncestorBreadcrumb(const ibDataViewItem& fromRow, ibDataViewItemArray& out) const
{
	const ibComposerNode* node = fromRow.IsOk() ? GetViewData<ibComposerNode>(fromRow) : nullptr;
	if (node == nullptr)
		return;

	// GROUPED (grouping is on): the ancestors are the GROUP levels, NOT folders — the folder tree is replaced. Build
	// one group crumb per dimension, each carrying the group PATH root→this (the row's value for dims[0..i]); deepest
	// last built, emitted FIRST so out[0] is the immediate (innermost) group the row sits directly in.
	if (m_composer.GroupCount() > 0) {
		std::vector<ibValue> groupPath;
		std::vector<ibComposerNode*> crumbs;
		for (size_t i = 0; i < m_composer.GroupCount(); ++i) {
			wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (!m_composer.GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
			const ibMetaID dimCol = GetColumnIDByName(field);
			if (dimCol == wxNOT_FOUND) continue;
			ibValue dimVal;
			node->GetValue(dimCol, dimVal);
			groupPath.push_back(dimVal);
			std::map<ibMetaID, ibValue> vals;
			vals[dimCol] = dimVal;                                       // the group's own label cell
			crumbs.push_back(new ibComposerNode(vals, groupPath, /*container*/true));   // groupPath copied
		}
		for (size_t i = crumbs.size(); i > 0; --i) {                    // deepest → shallowest into out
			out.Add(ibDataViewItem(crumbs[i - 1]));
			crumbs[i - 1]->DecRef();
		}
		return;
	}

	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr)
		return;
	const ibBackendQueryColumn* hierCol = q->GetHierarchyColumn();
	if (hierCol == nullptr)
		return;   // not a hierarchical source — no ancestors to walk
	const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();
	if (keyCols.empty() || keyCols.front() == nullptr)
		return;
	const ibMetaID hCol = hierCol->GetColumnId();

	// The row's immediate parent reference. Prefer the node's own fetched value; if the hierarchy column is not in
	// the projection (or the node is a key-only restore stub), resolve the row by its key to read it.
	ibValue parentRef;
	node->GetValue(hCol, parentRef);
	if (parentRef.IsEmpty() && !node->GetRowKey().empty()) {
		const std::map<ibMetaID, ibValue> vals = ResolveAnchorByKey(node->GetRowKey());
		const auto it = vals.find(hCol);
		if (it != vals.end()) parentRef = it->second;
	}

	int guard = 0;
	while (!parentRef.IsEmpty() && guard++ < 256) {
		std::map<ibMetaID, ibValue> vals = ResolveAnchorByKey(std::vector<ibValue>{ parentRef });
		if (vals.empty())
			break;   // the ref points nowhere (root passed / broken) — stop
		ibComposerNode* crumb = new ibComposerNode(vals, /*container*/true, std::vector<ibValue>{ parentRef });
		out.Add(ibDataViewItem(crumb));   // ctor IncRefs to 2
		crumb->DecRef();                  // -> `out` owns exactly one reference
		const auto it = vals.find(hCol);
		parentRef = (it != vals.end()) ? it->second : ibValue();
	}
}
