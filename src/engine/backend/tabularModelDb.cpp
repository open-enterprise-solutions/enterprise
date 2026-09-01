////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : DB value-model (L5-1 fetch)
////////////////////////////////////////////////////////////////////////////
//
// The DB half of the value-model, split out of tabularModel.cpp: ibValueModelCursor::RunComposerPage (render the
// settings → SQL → keyset page → walk → COPY nodes; hierarchy drill via GROUPING) + ibValueModel::ResolveAnchorByKey
// (the point lookup a FindRowValue restore stub positions on). A DB model reads dynamically from the cursor — no
// stored rows. The RAM half lives in tabularModelRam.cpp; the shared ibValueModel base + the node lives in tabularModel.cpp.

#include "tabularModel.h"

#include "backend/backend_exception.h"             // ibBackendException — the fetch's catch/restore-scope guard
#include "backend/tabularModelView.h"              // s_constIgnoreParent / ibDataViewItem
#include "backend/composition/dataComposer.h"      // ibDataDBComposer — render → SQL
#include "backend/composition/drivers/listFetchDriver.h"   // ibListFetchDriver — the universal composer-fetch sink
#include "backend/query/dataQueryBuilder.h"        // ibReadPageRequest — the page envelope
#include "backend/query/queryProvider.h"           // ibBackendQueryProvider — GetProvider().ResolveReferenceTarget (dot-walk hop)
#include "backend/system/value/valueType.h"        // ibValueTypeDescription::AdjustValue — typed empty parent ref (hierarchy roots)
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — drilled folder guid
#include "backend/uniqueKey.h"                      // ibUniqueKey — GetItemKey builds the row's reference key
// ibValueModelCursor::EnsureSnapshot — DynamicRead OFF: materialise the WHOLE result set into m_snapshot ONCE, then
// every fetch / scroll / group serves from RAM (RunStoragePage). Re-materialises only when the view generation moved
// (a refresh / filter / sort change bumps it; a scroll does not). The SQL read applies the persistent FILTER + SORT
// but drops GROUPING + the page limit (the whole detail set in one shot); grouping is mirrored onto the snapshot
// composer and rebuilt IN RAM. `this` is const — the fill goes through the const-model storage ops, NO const_cast. A
// self-hierarchy source flattens here (RAM has no parent-ref tree — that is a live-cursor feature).
void ibValueModelCursor::EnsureSnapshot() const
{
	if (m_snapshotValid && m_snapshotGen == GetViewGeneration())
		return;                                           // snapshot current — reuse across scroll

	m_snapshot.Clear(this, false);                        // drop the previous materialisation (silent)

	const ibBackendQueryable* q = GetSourceQueryable();
	if (q != nullptr) {
		// Read the WHOLE detail set in ONE unbounded flat SELECT (persistent filter + sort applied; grouping OUT).
		ibReadPageRequest page;
		page.m_direction  = ibFetchDirection::Reset;
		page.m_count      = 0;                            // 0 = unbounded (every row)
		page.m_flatScan   = true;
		page.m_isTopLevel = true;

		ibDataDBComposer& composer = m_composer;
		const ibDataComposer::SettingsScope scope = composer.MarkScope();
		const ibDataComposer::TakenGroups savedGroups = composer.TakeGroups();   // detail read
		ibListFetchDriver driver(page);
		try {
			composer.Run(driver);
		}
		catch (const ibBackendException&) {
			composer.PutGroups(savedGroups);
			composer.RestoreScope(scope);
			throw;
		}
		composer.PutGroups(savedGroups);
		composer.RestoreScope(scope);

		// Mirror the persistent GROUPING onto the snapshot composer — RunStoragePage builds the group tree IN RAM
		// (the SQL read dropped it). Filter/sort are baked into the materialised order, so the snapshot composer
		// keeps none → ComputeOrder returns the rows in their SQL order.
		m_snapshotComposer.PutGroups(savedGroups);

		// One DETAIL copy node per row (same shape as the DB detail fetch), adopted (silent) into the snapshot.
		const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();
		const ibBackendQueryColumn* folderCol = GetFolderDisplayColumn();
		m_snapshot.Reserve(static_cast<long>(driver.Rows().size()));
		for (const ibListFetchDriver::Row& r : driver.Rows()) {
			if (r.m_level != 0)
				continue;                                 // a flat / detail read emits every row at level 0
			std::vector<ibValue> rowKey;
			rowKey.reserve(keyCols.size());
			for (const ibBackendQueryColumn* kc : keyCols)
				if (kc != nullptr) rowKey.push_back(r.GetValue(kc->GetColumnId()));
			// A container = a FOLDER, or ANY element when the source nests element in element, or a row already
			// known to have children. The middle term is the hierarchy KIND, asked of the source.
			const bool isFolderRow = (folderCol != nullptr) && r.GetValue(folderCol->GetColumnId()).GetBoolean();
			const bool isContainer = isFolderRow
			                      || q->GetHierarchyType() == ibHierarchyType::eItems
			                      || r.m_expandable;
			ibComposerNode* node = new ibComposerNode(r.m_values, isContainer, std::move(rowKey));
			m_snapshot.AddValue(this, node, false);       // adopt (silent) — const-model op, no const_cast
		}
	}

	// Capture the generation AFTER the fill — the silent Clear / AddValue bump it per row, so recording it before
	// would leave m_snapshotGen != the current generation and re-materialise on every fetch.
	m_snapshotGen   = GetViewGeneration();
	m_snapshotValid = true;
}

// ibValueModelCursor::RunComposerPage — the DB fetch: render the composer → SQL → keyset page → walk → COPY nodes;
// hierarchy drill. NO stored row here — the DB reads dynamically from the cursor. When IsDynamicRead() is false it
// short-circuits to the whole-list RAM snapshot instead.
unsigned int ibValueModelCursor::RunComposerPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibFetchDirection dir, ibDataViewItemArray& out) const
{
	// DynamicRead OFF (the safety fallback): serve the WHOLE list from the RAM snapshot — materialise it once, then
	// page + group it IN MEMORY (RunStoragePage), bypassing the keyset cursor entirely.
	if (!IsDynamicRead()) {
		// A RESET IS A RE-READ, and the snapshot must obey it. Its cheap re-check keys off the VIEW GENERATION,
		// which only a filter / sort / explicit RefetchAll bumps — a save does not touch it. So the one path that
		// matters most, "an object form wrote a row and the list re-pulls" (UpdateForm -> SchedulePagedRefresh ->
		// a Reset fetch), found the snapshot still valid and served the PRE-SAVE rows: with DynamicRead off a
		// created element never appeared and an edited one kept its old cells until the user happened to sort or
		// filter. Forward / Backward keep reusing the materialisation — a scroll is not a re-read, and paging in
		// RAM is the whole point of this mode.
		if (dir == ibFetchDirection::Reset)
			m_snapshotValid = false;
		EnsureSnapshot();
		return RunStoragePage(m_snapshot, m_snapshotComposer, parent, anchor, count, dir, out);
	}

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
	// The VIEW MODE decides flat-vs-grouped, exactly as the RAM half does (tabularModelRam.cpp): a flat LIST view
	// passes the ignore-parent SENTINEL → grouping is OFF (the user chose the Flat view → a flat table even with a
	// grouping configured, so the flat toggle always wins over a stored grouping); a TREE / Hierarchical view
	// passes an empty/real parent → grouping is ON. Populating dims in a flat view drove groupLevel=true there,
	// so a flat List of a grouped source ran a TOTALS read that returned the whole tree pre-order → level-0-only
	// trim left zero rows → "everything disappears". Gating on !flatView keeps the flat list flat.
	// ⭐⭐ THE RUNGS COME OFF THE LADDER — THE SAME ONE THE SHEET IS PRINTED BY (Max, 2026-08-29:
	// *"bring the two roads together"*). They used to be read straight off `GroupCount()`, which is the
	// SETTING, while the sheet read a LADDER built somewhere else — one state, two constructions, and
	// every difference downstream followed from that. `BuildPrintLevels` is now the only place a ladder
	// is made, and both roads ask it.
	//
	// ⚠ ONLY THE TREE BRANCH IS ASKED FOR HERE. Its flat branch CLEARS the reader's grouping, which is
	// right for a copy printed once and destructive on the live composer a list reads through — so a
	// flat view simply takes no rungs, exactly as it did before.
	std::vector<wxString>         dims;
	std::vector<ibQueryDimUnfold> dimKinds;
	if (!flatView) {
		// DERIVED, SO DERIVED AFRESH. The ladder is a reading of the setting, and the setting changes under a
		// list all the time — a person edits the grouping and fetches again. `BuildPrintLevels` leaves an
		// existing ladder alone (an author's report declares its own), so without the trim the first fetch's
		// ladder would outlive the setting it was read from and answer for a grouping nobody has any more.
		m_composer.TrimLevels(0);
		m_composer.BuildPrintLevels(true, q);
		for (const ibDataComposer::GroupNode& level : m_composer.LevelChain()) {
			// The records level ends the rungs — it names no field, and what stands under the last
			// heading is rows, not another level.
			if (level.m_settings.m_group.m_lines.empty())
				break;
			const ibGroupLineDescription& line = level.m_settings.m_group.m_lines.front();
			if (line.m_path.IsEmpty())
				break;
			dims.push_back(line.m_path);
			dimKinds.push_back(line.m_kind);
		}
	}

	// The already-drilled dimension values (the scope) — read from the browsed parent node's group path.
	// GetViewData is a static_cast, so guard on a real, ok parent node.
	std::vector<ibValue> parentPath;
	// ⭐⭐ …AND WHERE IT STANDS INSIDE THAT RUNG. A rung that unfolds a hierarchy recurses WITHIN itself:
	// a folder and its contents are the SAME rung and differ only by a step inwards. So "which rung" and
	// "how deep in its tree" are two questions, and the browsed node answers both separately.
	std::vector<ibValue> parentSub;
	if (!dims.empty() && parent.IsOk() && !flatView)
		if (const ibComposerNode* pnode = GetViewData<ibComposerNode>(parent)) {
			parentPath = pnode->GetGroupPath();
			parentSub  = pnode->GetSubPath();
		}
	const size_t depth      = parentPath.size();
	const bool   grouping   = !dims.empty();

	// ⭐ WALKING A TREE IS ASKED OF THE ARRANGEMENT, not of the parent column. `GetHierarchyColumn()`
	// answers "is there a parent to walk up", which THREE arrangements say yes to — including
	// `eSubordination`, where the parent is ordinary data and the list is meant to stay flat. Asked
	// through the column, a chart of accounts grew a tree it never declared: an account nested under
	// its parent account, with a twisty to expand, over a list that is by declaration a flat one.
	const ibHierarchyType arrangement = q->GetHierarchyType();
	const bool browsable = arrangement == ibHierarchyType::eItems
	                    || arrangement == ibHierarchyType::eFoldersAndItems;

	// ⭐⭐ WHICH RUNG IS BROWSED, AND HOW A PAGE OF IT IS SERVED — asked of the LADDER, not of two
	// mutually-exclusive flags.
	//
	// 🛑 THE PAIR IT REPLACES CARRIED A RULE THAT IS NOW WRONG: *"a user grouping REPLACES the inherent
	// folder tree — the tree shows ONLY when no grouping is configured"* (`hierarchy = … && !grouping`).
	// So "group by a field, and inside each group show the catalog's own tree" was not merely unbuilt,
	// it was UNSAYABLE — and the sheet, which builds a ladder, said it happily. That is the divergence
	// itself (Max, 2026-08-29: *"the list shows the hierarchy and the report does not — that is
	// nonsense"*).
	//
	// A rung unfolds a tree when it SAYS SO — `Hierarchy` / `HierarchyOnly` — over the row's own
	// identity. Anything else is an ordinary grouping rung. Nothing is mutually exclusive any more:
	// the tree is one rung among the others, and it may stand under them.
	//
	// ⚠ AND OVER THE IDENTITY, not over any reference a person happened to pick: the parent-scope
	// reader below walks the SOURCE's own hierarchy column, so a `Hierarchy` rung over some other
	// reference is not something it can serve — it stays an ordinary grouping rung.
	wxString identity;
	{
		const std::vector<const ibBackendQueryColumn*> key = q->GetPrimaryKeyColumns();
		if (key.size() == 1 && key.front() != nullptr)
			identity = key.front()->GetName();
	}
	const bool atRung = depth < dims.size();
	const bool treeRung = atRung && !identity.IsEmpty() && dims[depth] == identity
	                   && (dimKinds[depth] == ibQueryDimUnfold::Hierarchy
	                    || dimKinds[depth] == ibQueryDimUnfold::HierarchyOnly);

	const bool hierarchy  = treeRung && browsable && q->GetHierarchyColumn() != nullptr && !flatView;
	const bool groupLevel = atRung && !hierarchy;

	ibValue hierParentKey;       // the browsed folder's self-reference (scope for its children)
	bool    hierHasParent = false;
	// ⚠ …AND ONLY FROM A ROW. Entering a GROUP heading puts the reader at the TOP of the tree inside that
	// group, not inside a folder — the heading has no reference of its own to scope children by, and reading
	// one off it would scope the tree by a dimension value. Drilling a FOLDER is the other case, and now that
	// a folder carries its group's scope the two are told apart by what the node IS.
	if (hierarchy && parent.IsOk())
		if (const ibComposerNode* pnode = GetViewData<ibComposerNode>(parent); pnode != nullptr && !pnode->IsGroup()) {
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
		page.m_hierarchyFilter = true;
		page.m_hierarchyCol    = q->GetHierarchyColumn();
		page.m_flatScan        = false;
		page.m_isTopLevel      = !hierHasParent;
		// DRILL scope = the browsed folder's own PK reference (its children = rows whose parent == it). Pass the
		// KEY value WHOLE — the provider encodes it (a reference → its pure _RRRef guid blob). Empty
		// when top-level → the provider filters the empty-parent roots. (Was: decompose to a bare guid + rebuild
		// the blob from the queryable's table id — a crutch that assumed guid-keying AND a self-hierarchy.)
		if (hierHasParent)
			page.m_hierarchyKey = hierParentKey;
	}
	if (anchor.IsOk()) {
		page.m_hasAnchor = true;
		ibValueModel::ibComposerNode* a = GetViewData<ibValueModel::ibComposerNode>(anchor);
		// A GROUP-LEVEL keyset page is ordered by the level's DIM alone -> the single anchor value is the anchor
		// group's OWN dim value (the tail of its group path). The server GROUP-BY page keysets dim >/< this value;
		// the detail sort/PK tail (else) is a detail-read cursor. (docs: group-level paging)
		if (groupLevel) {
			// ⭐ AND ON A NESTED RUNG THE ANCHOR IS ITS STEP, NOT ITS RUNG. A folder INSIDE a rung does not
			// extend the rung's path — its place is the sub-chain — so looking for it in the group path
			// found nothing, the window restarted at the top and the first page came back a second time
			// (Max, 2026-08-30: *"almost, but it doubles"*). Two facts, and the question must be put to
			// the one that holds the answer.
			if (a != nullptr && !a->GetSubPath().empty())
				page.m_anchorSortValues.push_back(a->GetSubPath().back());
			else if (a != nullptr && !a->GetGroupPath().empty())
				page.m_anchorSortValues.push_back(a->GetGroupPath().back());
		}
		else {
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
	}

	// Group-level paging: a TOTALS read returns the WHOLE level at once (ExecuteTotals ignores the page envelope —
	// the fold is eager), so this level windows on the CLIENT by the browsed anchor group below, the SAME rule the
	// RAM half pages a group level by (ibComputePageWindow). (Was: an anchored group fetch returned 0 to avoid
	// re-emitting the level — but the first page's probe-trim then dropped the tail group with no continuation to
	// re-fetch it, silently losing the last group of any level with more groups than a page.) Detail rows still
	// keyset-page in SQL below.

	// ONE composer for THIS fetch (the persistent settings STORE — source + filter + sort + grouping). The fetch
	// does NOT clear it; it overlays the per-fetch DRILL of the browsed parent transiently: take the grouping
	// config OUT, render just the browsed level's TotalBy + the scope filter(s), run, then put it back.
	ibDataDBComposer& composer = m_composer;
	const ibDataComposer::SettingsScope scope = composer.MarkScope();

	const ibDataComposer::TakenGroups savedGroups = composer.TakeGroups();
	// SELF-HIERARCHY is driven by the page envelope's parent scope (set above) + the provider — NOT by the
	// composer (TOTALS BY Parent HIERARCHY grouped by the parent VALUE: it buried the items at tree level 1
	// with only the dimension, the diagnostic log proved it). So the composer renders a PLAIN level SELECT
	// (TakeGroups dropped the grouping) and the provider scopes it to the parent. Only an EXPLICIT user
	// grouping (Elements) drills through the composer here, layered on top of the base parent-hierarchy.
	if (grouping && !hierarchy) {
		// THE MODEL SAYS WHERE THE READER STANDS — this rung, this value. (What that means for the read
		// is the engine's question; the operator is not chosen here.)
		for (size_t k = 0; k < depth && k < dims.size(); ++k)
			composer.ScopeTo(dims[k], wxT("="), parentPath[k]);
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

	// Wrap each driver row in a generic composer node, honouring count + direction exactly as RunPage does. A
	// DETAIL / flat level was keyset-sized by the SQL page — trim the +1 probe, flip a Backward page to display
	// order. A GROUP level is NOT page-sized (TOTALS fold the whole level, ignoring the envelope), so leave every
	// row: it windows on the client below, where ibComputePageWindow handles the direction and count itself.
	std::vector<ibListFetchDriver::Row>& rows = driver.Rows();
	// ⚠ A WINDOWED READ WAS NEVER PAGE-SIZED — the fold is eager, so there is no +1 probe to trim and no
	// reversed page to flip; doing either would drop a real row off the level. (That the whole result is
	// read to serve one level is the honest cost of this step, and the next one — the fold taking a
	// count — is what pays it back.)
	if (!groupLevel) {
		if (static_cast<int>(rows.size()) > count && count > 0)
			rows.pop_back();                       // drop the +1 probe
		if (dir == ibFetchDirection::Backward)
			std::reverse(rows.begin(), rows.end());
	}

	// At a GROUP level the dimension being grouped is dims[depth]; the DISPLAY column that renders the group
	// header + the drill scope key by its column id. A PLAIN dimension resolves through the model column
	// collection AND the totals result carries its value under that same id — read straight off the row. A
	// DOT-WALK dimension ("Reference.DataVersion") does NOT: the totals lowering groups it under a SYNTHETIC id
	// (so its leaf can't clash with the main table on a self-reference), so the value rides under that synthetic
	// id, NOT the display leaf's real id. So (a) resolve the REAL leaf id by walking the path through the
	// queryable's references (this is the dot-path display column's own model id), and (b) flag it so we re-key
	// the value from the synthetic id to the leaf id per group row below (the composer computed the value; we
	// just route it to where the display + drill read it — no per-row dot re-resolution on a group header).
	// Resolve a possibly-dotted dimension field NAME to its leaf column id by walking the queryable's references;
	// wxNOT_FOUND when it is not a resolvable dot-walk path. (A plain field is resolved by GetColumnIDByName.)
	auto resolveDotWalkLeaf = [&](const wxString& field) -> ibMetaID {
		const ibBackendQueryable* walk = q;
		wxString rest = field;
		const ibBackendQueryColumn* leaf = nullptr;
		while (walk != nullptr && !rest.IsEmpty()) {
			leaf = walk->ResolveColumnByName(rest.BeforeFirst(wxT('.')));
			rest = rest.AfterFirst(wxT('.'));
			if (leaf == nullptr) break;
			if (!rest.IsEmpty()) walk = walk->GetProvider().ResolveReferenceTarget(walk, leaf);   // hop into the reference target
		}
		return (leaf != nullptr && rest.IsEmpty()) ? leaf->GetColumnId() : ibMetaID(wxNOT_FOUND);
	};

	ibMetaID groupDimCol   = ibMetaID(wxNOT_FOUND);
	bool     dotWalkDim    = false;
	int      dwOrdinal     = 0;   // index of dims[depth] among the dot-walk dimensions — selects the matching synthetic key
	if (groupLevel) {
		// Each single-source scalar dot-walk dimension consumed one synthetic id in lowering order (queryLowering
		// nextSynthId++, in m_totalsBy order), so dims[depth]'s grouped value rides under the dwOrdinal-th synthetic
		// key on the row — NOT simply the lowest. Count the dot-walk dimensions strictly before this level.
		for (size_t k = 0; k < depth && k < dims.size(); ++k)
			if (GetColumnIDByName(dims[k]) == wxNOT_FOUND && resolveDotWalkLeaf(dims[k]) != ibMetaID(wxNOT_FOUND))
				++dwOrdinal;

		groupDimCol = GetColumnIDByName(dims[depth]);
		if (groupDimCol == wxNOT_FOUND) {
			const ibMetaID leafId = resolveDotWalkLeaf(dims[depth]);
			if (leafId != ibMetaID(wxNOT_FOUND)) {
				groupDimCol = leafId;
				dotWalkDim  = true;
			}
		}
	}
	// A dot-walk dimension's grouped value rides under a totals-lowering SYNTHETIC column id (queryLowering.cpp
	// kSyntheticColumnBase). Keep in sync: dim synthetics sit at/above this base (aggregate synthetics sit lower,
	// at 0x40000000). Several dot-walk dimensions each take their OWN synthetic id, ASCENDING in lowering order
	// (dim0, dim1, … then measures), so a group row at depth D reads the dwOrdinal-th synthetic key (below).
	const ibMetaID kDimSyntheticBase = 0x50000000;

	// The source's PRIMARY-KEY columns — stamp each DB-list DETAIL copy's stable identity (m_rowKey) so it
	// survives re-fetch selection AND a guid-keyed FindRowValue stub matches a fetched row by it. (RAM never
	// reaches here — its short-circuit above returns the LIVE storage rows, identified by their stable pointer.)
	const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();

	// FOLDER-flag container-marking: a folder row renders as a DRILLABLE container even when EMPTY (the folder
	// convention). The DB level-fetch reports hasChildren=false for every row (dataComposer's flat path emits
	// OnRow(0,false,…)), so this is the ONLY folder signal the tree has. The column is the LIST's display column
	// (GetDataIsFolder, handed in at ibCreateHierarchyList) — NOT a queryable accessor; folders are a display
	// concern of the hierarchical list, ordering is the folder-first SORT. Null on a flat / non-folder list.
	const ibBackendQueryColumn* folderCol = GetFolderDisplayColumn();

	// The hierarchy KIND, asked of the source once per fetch: where an element nests inside an element (a
	// chart of accounts) EVERY row is enterable; where it nests inside a folder, only a folder is.
	const bool itemHierarchy = (q != nullptr) && q->GetHierarchyType() == ibHierarchyType::eItems;

	// `this` is const here (fetch READS + returns rows). The node holds COPIES of the row values and
	// overrides IsAttached() -> true, so there is NO owner pin and NO const_cast on the model state.
	unsigned int fetched = 0;
	// A GROUP level is collected WHOLE first (TOTALS ignore the page), then windowed by the anchor group below; a
	// DETAIL / flat level is already page-sized, so it emits straight to `out`. groupNodes holds the ctor ref until
	// the window transfers the kept ones into `out` (and frees the rest).
	std::vector<ibComposerNode*> groupNodes;
	out.Alloc(rows.size());

	// ⭐⭐⭐ A HIERARCHY RUNG IS READ BY ITS STEP, NOT ONLY BY ITS RUNG. The fold nests a folder and its
	// contents at ONE level and separates them by `m_indent`; a reader that filters by level alone gets
	// the folder and everything inside it as SIBLINGS — which is exactly what the list showed while the
	// sheet, which honours the indent, drew the tree correctly (Max, 2026-08-30: *"hierarchical catalog
	// values are not linked to each other"*, with both windows side by side).
	//
	// So this level takes the rows one step in from where the reader stands, and only those under the
	// branch they opened. `hasKids` is read by LOOKING AHEAD — a node has sub-values when the next row
	// steps further in — because that is the one thing a pre-order stream says for free.
	// (⚠ NOT `treeRung` — that one is the SOURCE's own tree, served by the parent-scope reader. This is
	//  any rung whose unfold nests values, including one over another catalog's reference.)
	const bool nestedRung = groupLevel && depth < dimKinds.size()
		&& (dimKinds[depth] == ibQueryDimUnfold::Hierarchy
		 || dimKinds[depth] == ibQueryDimUnfold::HierarchyOnly);
	std::vector<bool> hasKids(rows.size(), false);
	if (nestedRung)
		for (size_t i = 0; i + 1 < rows.size(); ++i)
			hasKids[i] = rows[i + 1].m_indent > rows[i].m_indent;
	std::vector<ibValue> subChain;      // the folder chain of this rung, rebuilt as the pre-order goes by
	size_t rowAt = static_cast<size_t>(-1);

	for (const ibListFetchDriver::Row& r : rows) {
		++rowAt;
		// LAZY drill: a TOTALS result arrives pre-order over EVERY level, but the paged control wants only the
		// CURRENT scope's TOP level per fetch — deeper rows load when the user drills into a node. The TOP level
		// is NOT the same number for the two shapes: BuildDimensionTree folds from Root (m_level 0), so the first
		// grouping level's group headers sit at m_level == 1 (queryProvider.cpp FoldDimLevel: AddChild(m_level+1));
		// a flat / DETAIL fetch (no TotalBy → hasTotals=false) emits every row at level 0. Filtering `!= 0`
		// therefore dropped EVERY group header (they are level 1) → grouping produced zero rows and the list
		// "disappeared". Keep level 1 for a group fetch, level 0 for a flat/detail one.
		if (r.m_level != (groupLevel ? 1 : 0))
			continue;

		// …AND ON A TREE RUNG, THE STEP DECIDES TOO: the chain is rebuilt as the pre-order passes, and a
		// row is this level's only when it stands one step in from the reader and under their branch.
		bool ownKids = false;
		if (nestedRung) {
			const size_t step = r.m_indent > 0 ? static_cast<size_t>(r.m_indent) : 0;
			ibValue own = (groupDimCol != ibMetaID(wxNOT_FOUND)) ? r.GetValue(groupDimCol) : ibValue();
			if (subChain.size() > step)
				subChain.resize(step);
			subChain.push_back(own);
			if (step != parentSub.size())
				continue;                       // another step of the same rung
			bool under = true;
			for (size_t k = 0; k < parentSub.size() && k < subChain.size(); ++k)
				if (!(subChain[k] == parentSub[k])) { under = false; break; }
			if (!under)
				continue;                       // another branch of this rung
			ownKids = rowAt < hasKids.size() && hasKids[rowAt];
		}

		ibComposerNode* node = nullptr;
		if (groupLevel) {
			// A GROUP node: its identity / scope is the parent path + THIS group's own dimension value, and it
			// drills one dimension deeper (or into the detail rows). DRILLABLE container only when the dimension
			// value was stamped into the path (else a drill re-enters at depth 0 → groupLevel fires again →
			// infinite re-grouping).
			std::map<ibMetaID, ibValue> values = r.m_values;
			ibValue dimValue = (groupDimCol != wxNOT_FOUND) ? values[groupDimCol] : ibValue();
			if (dotWalkDim) {
				// The value rides under a synthetic dim id, not the leaf id the display + drill key by. The synthetic
				// keys iterate in ASCENDING order (dim0, dim1, … then measures); this level's value is the
				// dwOrdinal-th one (each earlier dot-walk dimension consumed one). RE-KEY it under the real leaf id,
				// so GetValueByRow(group, dot-path column) reads the composer's grouped value directly. (A dot-walk
				// dimension that lowered to an expanded LEFT-join instead of a scalar synthetic — multi-source or a
				// non-scalar leaf — takes no synthetic id; a mix of the two on one grouping would skew this ordinal.
				// Pure single-source scalar dot-walk dimensions, the common case, map 1:1 to the leading synthetics.)
				int seen = 0;
				for (const auto& kv : r.m_values) {
					if (kv.first < kDimSyntheticBase) continue;
					if (seen++ == dwOrdinal) { dimValue = kv.second; break; }
				}
				if (groupDimCol != wxNOT_FOUND)
					values[groupDimCol] = dimValue;
			}
			// ANCESTOR-dimension SCOPE on the group header (nested grouping). The per-level drill FILTERS each
			// already-drilled ancestor dimension to its browsed value (parentPath[k]) but does NOT project it, so
			// ONLY this level's dimension rides in r.m_values. A display column that dot-walks an ANCESTOR
			// dimension's reference — e.g. grouped by Warehouse then Product, a column `Warehouse.Region` shown on
			// the Product header — resolves its FIRST hop off the node by that dimension's column id (the front's
			// GetValueByPath), which is absent here → the cell read blank. Stamp each ancestor dimension's value
			// under its column id (the SAME id the current level's dimension already sits under), so the front walks
			// the ancestor reference exactly as on a detail row — the scope the SELECTOR's group fold inherits down
			// (FoldDimLevel: child->m_values = node->m_values). parentPath[k] aligns with dims[k]; a dot-walk
			// ancestor dimension keys by its leaf id. emplace keeps r.m_values authoritative (never overwrites).
			for (size_t k = 0; k < depth && k < dims.size() && k < parentPath.size(); ++k) {
				ibMetaID ancId = GetColumnIDByName(dims[k]);
				if (ancId == ibMetaID(wxNOT_FOUND))
					ancId = resolveDotWalkLeaf(dims[k]);
				if (ancId != ibMetaID(wxNOT_FOUND))
					values.emplace(ancId, parentPath[k]);
			}
			std::vector<ibValue> groupPath = parentPath;
			std::vector<ibValue> subPath;
			if (nestedRung && ownKids) {
				// ⭐ A FOLDER OF THIS RUNG — opening it stays on the rung and steps one further in, so the
				// rung's scope does NOT grow and the step does. Grown instead, the drill would have asked
				// for the NEXT grouping and the folder's contents would never be reached.
				subPath = subChain;
			}
			else if (groupDimCol != wxNOT_FOUND) {
				// A LEAF of this rung (or an ordinary grouping value) — opening it moves to what stands
				// under this rung: the next grouping, or the records.
				groupPath.push_back(dimValue);
			}
			groupNodes.push_back(new ibComposerNode(values, groupPath,
				/*container*/ groupDimCol != ibMetaID(wxNOT_FOUND), subPath));
			continue;   // collected — the client window (below) transfers the on-page groups into `out`
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
			// A grouped DETAIL row is a LEAF: a folder that lands inside a grouping is a plain grouped item and
			// is NOT drillable. Marking it a container by folderCol re-offered a folder expander whose drill
			// re-entered RunComposerPage with an empty group path (depth 0) → groupLevel fired again → the whole
			// grouping tree nested under the folder ("infinite" re-grouping).
			//
			// ⭐ ASKED OF THE RUNG BEING SERVED, not of "is anything grouped". The tree is now a RUNG of the
			// ladder, so `grouping` is true whenever a catalog shows its tree — and read the old way that turned
			// every folder in every catalog into a leaf. What decides is which rung produced this row: a TREE
			// rung's rows are folders and items, a grouping's are leaves.
			bool isFolderRow = false;
			if (folderCol != nullptr)
				isFolderRow = r.GetValue(folderCol->GetColumnId()).GetBoolean();
			const bool isContainer = (hierarchy || !grouping)
				? (isFolderRow || itemHierarchy || r.m_expandable) : false;
			// ⭐ AND IT CARRIES WHERE IT STANDS. A row fetched inside a group belongs to that group, and
			// drilling it must re-enter at the rung it was found on — not at the top. Without the scope the
			// drill came back with an empty path, depth 0, and the whole grouping tree re-grew under the folder.
			node = new ibComposerNode(r.m_values, isContainer, std::move(rowKey), parentPath);
		}
		out.Add(ibDataViewItem(node));   // ctor IncRefs to 2
		node->DecRef();                  // -> `out` owns exactly one reference per row
		++fetched;
	}

	// GROUP-level client window: TOTALS folded the WHOLE level (the page envelope does not size a totals read), so
	// window the collected groups by the browsed anchor group — the SAME rule the RAM half pages a group level by.
	// Locate the anchor group by its own last group-path value, take `count` groups after / before it per the
	// direction (the anchor stays in a Reset page so the viewport does not drift). Then drop OUR ctor ref on every
	// group node: `out` took its own via the item ctor's IncRef, so the on-page groups survive with one reference
	// and the off-page ones free.
	if (groupLevel) {
		long p = -1;
		if (anchor.IsOk())
			if (const ibComposerNode* anode = GetViewData<ibComposerNode>(anchor)) {
				// …asked of whichever fact holds this node's place: its step inside the rung when it has
				// one (a folder), its rung path otherwise. Asked only of the path, a folder was never
				// found here either and the window restarted — the same doubling, one place further on.
				const bool byStep = !anode->GetSubPath().empty();
				const std::vector<ibValue>& apath = byStep ? anode->GetSubPath() : anode->GetGroupPath();
				if (!apath.empty())
					for (size_t k = 0; k < groupNodes.size(); ++k) {
						const std::vector<ibValue>& gp = byStep
							? groupNodes[k]->GetSubPath() : groupNodes[k]->GetGroupPath();
						if (!gp.empty() && gp.back() == apath.back()) { p = static_cast<long>(k); break; }
					}
			}
		const std::vector<long> win = ibComputePageWindow(static_cast<long>(groupNodes.size()), p, dir,
			count > 0 ? count : defaultCountPerPage);
		for (const long pos : win) {
			out.Add(ibDataViewItem(groupNodes[pos]));   // ctor IncRefs to 2 (node holds our ctor ref = 1)
			++fetched;
		}
		for (ibComposerNode* gn : groupNodes)
			gn->DecRef();   // drop the build ref: on-page -> 1 (out owns), off-page -> 0 (freed)
	}

	for (size_t i = 0; i < out.GetCount(); ++i) SetItemParent(out[i], parent);

	return fetched;
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
	const ibDataComposer::TakenGroups savedGroups = composer.TakeGroups();
	for (size_t i = 0; i < pk.size(); ++i)
		if (pk[i] != nullptr)
			composer.ScopeTo(pk[i]->GetName(), wxT("="), rowKey[i]);

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

	const ibBackendQueryable* q = GetSourceQueryable();
	const std::vector<const ibBackendQueryColumn*> keyCols =
		q != nullptr ? q->GetPrimaryKeyColumns() : std::vector<const ibBackendQueryColumn*>();

	// THE ROW'S OWN IDENTITY COLUMN, needed by BOTH shapes below — the folder walk keys its point
	// lookups by it, and the grouped shape has to be able to RECOGNISE it (see there).
	const ibMetaID rowKeyCol = (!keyCols.empty() && keyCols.front() != nullptr)
		? keyCols.front()->GetColumnId() : wxNOT_FOUND;

	// GROUPED (grouping is on): the ancestors are the GROUP levels, NOT folders — the folder tree is replaced. Build
	// one group crumb per dimension, each carrying the group PATH root→this (the row's value for dims[0..i]); deepest
	// last built, emitted FIRST so out[0] is the immediate (innermost) group the row sits directly in.
	if (m_composer.GroupCount() > 0) {

		// ⭐ A RESTORE STUB HAS NO CELLS, so asking it for a dimension answers TYPE_EMPTY every time —
		// and an empty crumb is not merely blank, it MOVES THE READ: the page is fetched under it, the
		// scope comes back `<dim> = <nothing>`, and the list shows one empty folder row and no data.
		// The folder walk below has always resolved a stub by its key; this shape never learned to,
		// which is the same state reached by two roads with only one of them taught. Resolve ONCE,
		// here, and every dimension below reads from a row that can answer. (2026-09-01: adding an
		// element at the root of a hierarchical catalog — the one moment a list positions on a
		// key-only node.)
		std::map<ibMetaID, ibValue> resolved;
		if (node->IsKeyOnlyAnchor() && !node->GetRowKey().empty())
			resolved = ResolveAnchorByKey(node->GetRowKey());

		std::vector<ibValue> groupPath;
		std::vector<ibComposerNode*> crumbs;
		for (size_t i = 0; i < m_composer.GroupCount(); ++i) {
			wxString field; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (!m_composer.GetGroupAt(i, field, kind) || field.IsEmpty()) continue;
			const ibMetaID dimCol = GetColumnIDByName(field);
			if (dimCol == wxNOT_FOUND) continue;

			// ⭐⭐ A FOLD ON THE ROW'S OWN KEY IS THE HIERARCHY, NOT A GROUPING. `GroupCount()` answers
			// from the composition's LEVEL CHAIN when no user grouping is set, and a hierarchical list's
			// level is the self-hierarchy fold — its field is `Ref`, the row's own identity. Read as an
			// ordinary grouping it produces a crumb meaning "drill into this row", which is not an
			// ancestor of anything: a row is not its own parent. The ancestors of a self-hierarchy row
			// are its FOLDERS, and the walk below is what answers that — so this contributes nothing and
			// lets it. (Skipped rather than refused: a list may genuinely group by other dimensions too,
			// and those crumbs stay valid.)
			if (rowKeyCol != wxNOT_FOUND && dimCol == rowKeyCol) continue;

			ibValue dimVal;
			node->GetValue(dimCol, dimVal);
			if (dimVal.IsEmpty()) {
				const auto it = resolved.find(dimCol);
				if (it != resolved.end()) dimVal = it->second;
			}

			groupPath.push_back(dimVal);
			std::map<ibMetaID, ibValue> vals;
			vals[dimCol] = dimVal;                                       // the group's own label cell
			crumbs.push_back(new ibComposerNode(vals, groupPath, /*container*/true));   // groupPath copied
		}
		if (!crumbs.empty()) {
			for (size_t i = crumbs.size(); i > 0; --i) {                // deepest → shallowest into out
				out.Add(ibDataViewItem(crumbs[i - 1]));
				crumbs[i - 1]->DecRef();
			}
			return;
		}
		// Nothing here was a grouping after all — fall through and answer as a folder hierarchy.
	}

	if (q == nullptr)
		return;
	const ibBackendQueryColumn* hierCol = q->GetHierarchyColumn();
	if (hierCol == nullptr)
		return;   // not a hierarchical source — no ancestors to walk
	if (rowKeyCol == wxNOT_FOUND)
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
