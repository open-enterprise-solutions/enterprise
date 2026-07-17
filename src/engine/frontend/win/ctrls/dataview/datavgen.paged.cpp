/////////////////////////////////////////////////////////////////////////////
// Name:        datavgen.paged.cpp
// Purpose:     Paged-mode bootstrap + fetch dispatch for ibDataViewCtrl.
//              Extracted from datavgen.cpp on 2026-05-08 to keep that file
//              from growing past 9.5K LOC.  Methods stay members of
//              ibDataViewCtrl — class definition lives in datavgen.h —
//              this is purely a translation-unit split, no behavioural
//              change.
// Methods:
//   GetPagedInsertParent      — drill-chain insert parent walker
//   SchedulePagedRefresh      — debounced refresh dispatcher
//   PagedRefresh              — wipe + capture restore-state
//   PagedBootstrap            — initial fetch + Tree-mode auto-expand
//                               + RAM focus-by-rawrow restore
//   DispatchPagedFetch        — async forward / backward fetch
//   OnPagedFetchForwardResult — append+trim path
//   OnPagedFetchBackwardResult— prepend path
/////////////////////////////////////////////////////////////////////////////

#include <wx/wxprec.h>

#if wxUSE_DATAVIEWCTRL

#include "dataview.h"
#include "backend/model.h"   // ibValueModel + Features for SetViewMode
#include "datavgen.paged.private.h"   // kBufferSlack + ScopedPagedFreeze
#ifndef WX_PRECOMP
#include <wx/log.h>
#include <wx/vector.h>
#endif

#include <wx/weakref.h>
#include <wx/generic/private/rowheightcache.h>   // HeightCache::Remove

namespace {
// Single point of tree-node child creation across all paged populate
// paths (Bootstrap top-level, Tree-mode auto-expand, fwd/bwd-result
// inserts).  The Container marker (HasChildren = m_branchData != nullptr)
// is queried by the renderer for the folder icon AND by the fork's
// expander rendering — must be set unconditionally for folder items in
// every view mode so the marker survives Bootstrap and matches the
// fwd/bwd result handlers' set; pre-fix asymmetry caused folder-icon
// flicker on refresh in List mode.
ibDataViewTreeNode* MakeChildNode(ibDataViewTreeNode* parent,
                                  const ibDataViewItem& item)
{
	auto* node = new ibDataViewTreeNode(parent, item);
	if (item.IsContainer())
		node->SetHasChildren(true);
	return node;
}
}

int ibDataViewCtrl::FindVisibleRowInTree(const ibDataViewItem& target) const
{
	int row = -1;
	bool found = false;
	// Recursive walker — m_root is implicitly open; deeper nodes must be
	// open AND have children to contribute to the visible row count.
	std::function<void(ibDataViewTreeNode*)> walk =
		[&](ibDataViewTreeNode* node) {
		if (found) return;
		if (!node->HasChildren()) return;
		if (node->GetParent() != nullptr && !node->IsOpen()) return;
		for (auto* c : node->GetChildNodes()) {
			if (found) return;
			if (c->IsHidden()) continue;
			++row;
			if (c->GetItem() == target) { found = true; return; }
			walk(c);
		}
	};
	walk(m_root);
	return found ? row : wxNOT_FOUND;
}

ibDataViewTreeNode* ibDataViewCtrl::GetPagedInsertParent() const
{
	if (m_root == nullptr) return nullptr;
	ibDataViewTreeNode* parent = m_root;
	if (m_viewMode != ibDataViewViewMode::ibDataViewHierarchical
	    || m_topParentChain.IsEmpty())
		return parent;
	for (size_t i = 0; i < m_topParentChain.GetCount(); ++i) {
		const auto& kids = parent->GetChildNodes();
		if (kids.empty()) return parent;   // partial chain — bail safely
		parent = kids[0];
	}
	return parent;
}

void ibDataViewCtrl::SetPagedRestoreSelection(const ibDataViewItem& item)
{
		m_pagedRestoreSelection = item;
	if (item.IsOk()) {
		m_pagedRestoreFocus = item;
		// Programmatic Select means "really select this row" — without
		// this flag, post-Add to a list with no prior selection would
		// land focus only (ChangeCurrentRow) without the highlight
		// (SelectItem) because PagedRefresh captured wasSelected=false
		// from m_selection.IsEmpty() before Select stamped here.
		m_pagedRestoreFocusWasSelected = true;
	}
}

void ibDataViewCtrl::SchedulePagedRefresh(const ibDataViewItem& preferSelection)
{
			// Latest-selection-wins: a series of ItemInserted events (e.g. LoadData
	// repopulating the table) produces successive items; the last one
	// is the most reasonable focus anchor for the user.
	if (preferSelection.IsOk())
		m_pagedRefreshSelection = preferSelection;

	if (m_pagedRefreshScheduled) return;
	m_pagedRefreshScheduled = true;

	// Dispatch through the model's worker channel rather than a
	// host-specific wxTheApp->CallAfter — under web that channel is
	// the per-session worker queue (FIFO, sequential), under desktop
	// it falls back to ibSession::Submit's inline path until a GUI
	// worker pool is wired up.  Either way, all callers in a series
	// land on the same handler and the m_pagedRefreshScheduled flag
	// coalesces them into one PagedRefresh.
	//
	// wxWeakRef guards against the control being destroyed between
	// submit and dispatch (model owned by ibValue, may outlive the
	// control's lifecycle on form close / panel rebuild).
	ibDataViewModel* model = GetModel();
	if (model == nullptr) {
		m_pagedRefreshScheduled = false;
		return;
	}
	wxWeakRef<ibDataViewCtrl> weakSelf(this);
	model->SubmitFetchAsync([weakSelf]{
		auto* self = weakSelf.get();
		if (self == nullptr) return;   // control gone — drop the refresh
		self->m_pagedRefreshScheduled = false;
		ibDataViewItem sel = self->m_pagedRefreshSelection;
		self->m_pagedRefreshSelection = ibDataViewItem();
		self->PagedRefresh(sel);
		// PagedBootstrap fires on the next idle (OnIdleEvent) so the
		// viewport is sampled when layout has settled.  Calling it
		// inline here would lock the batch size to whatever
		// GetCountPerPage() returns at this moment — small under
		// initial form-open or other early-call scenarios.
	});
}

void ibDataViewCtrl::PagedRefresh(const ibDataViewItem& preferSelection)
{
		// PagedRefresh runs synchronously inline (e.g. inside OnUpdated /
	// notifier reset).  PagedBootstrap fires later on OnInternalIdle.
	// We DELIBERATELY do not wipe the tree here — wiping eagerly would
	// leave the chrome (header / footer / scrollbar) painting against
	// an empty model between this point and the next idle, which is
	// the visible "flicker" the user reported.  Instead this method
	// only captures restore-state + bumps the cancellation generation
	// + arms the bootstrap flag; the actual DestroyTree + buffer reset
	// is folded into the start of PagedBootstrap so wipe + fill happen
	// in a single freeze window.
	const bool skipCapture = m_pagedSkipRestoreCapture;
	m_pagedSkipRestoreCapture = false;
	const ibDataViewItem topItem = skipCapture
		? ibDataViewItem() : GetTopItem();
	// Explicit prefer (caller asked for a specific focus target —
	// post-Save new row, SetViewMode switch) drives both fetch cursor
	// and focus.  Without explicit prefer, fall back to the pre-refresh
	// currentRow as a focus-only target — fetch cursor stays on the
	// anchor (top) so the viewport doesn't jump to selection.
	ibDataViewItem explicitTarget;
	ibDataViewItem currentFocus;
	// Focus capture runs in both paths — sort (skipCapture) preserves
	// focus on the same business row across the new ordering, drill
	// captures it but the IsEqualTo scan misses silently in
	// PagedBootstrap.  Anchor / explicitTarget are gated below.
	if (m_currentRow != (unsigned)-1)
		currentFocus = GetItemByRow(m_currentRow);
	if (!skipCapture) {
		if (preferSelection.IsOk())
			explicitTarget = preferSelection;
	}
	const int  preSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	const int topRow   = topItem.IsOk()      ? (int)GetRowByItem(topItem)      : -1;
	const int focusRow = currentFocus.IsOk() ? (int)GetRowByItem(currentFocus) : -1;
	const int totalRows = m_root != nullptr ? (int)GetRowCount() : 0;
		if (topRow >= 0 && focusRow >= 0) {
		const int viewport = GetCountPerPage();
		const int dist = focusRow - topRow;
		const bool focusVisible = (dist >= 0 && dist < viewport);
			}
	// NB: currentFocus stays in m_pagedRestoreFocus only — we do NOT
	// promote it to explicitTarget even when it sits outside the
	// viewport.  Promotion forces Bootstrap into selection-cursor
	// mode, which makes viewport jump to focus area; users complained
	// "refresh shows other elements" when selection was off-screen.
	// Keeping anchor-cursor always means viewport stays at saved top
	// and focus is restored ONLY if it lands inside the fetched
	// (post-bootstrap + post-backward) buffer; otherwise selection
	// goes to -1 — viewport preservation wins over focus preservation.

	// Coalesce-safe capture: a second Refresh that lands after the
	// first one already wiped the tree sees GetTopItem() == empty, so
	// blindly assigning would erase the anchor captured by the first
	// click and Bootstrap would land on row 0.  Keep the previous
	// anchor unless we have a real new one.  skipCapture (drill) still
	// resets — it explicitly wants the new context.
	if (skipCapture) {
		// Sort / drill — anchor's cursor key (uuid+sortValues for sort,
		// parent-scoped tree for drill) is invalidated, so PagedBootstrap
		// must start a fresh top-of-window fetch.  Focus, however, is
		// preserved: for sort, IsEqualTo (m_objGuid for catalog,
		// m_nodeKeys for register) re-locates the same business row in
		// the new ordering; for drill old focus from previous folder
		// silently misses the IsEqualTo scan and lands at default
		// position — but we no longer destructively wipe a still-valid
		// focus capture from a concurrent sort path.
		m_pagedRestoreAnchor    = ibDataViewItem();
		m_pagedRestoreSelection = ibDataViewItem();
		if (currentFocus.IsOk()) m_pagedRestoreFocus = currentFocus;
		m_pagedRestoreFocusRow = (m_currentRow != (unsigned)-1)
			? static_cast<long>(m_currentRow) : -1;
		m_pagedRestoreFocusWasSelected = !m_selection.IsEmpty();
	} else {
		if (topItem.IsOk())          m_pagedRestoreAnchor    = topItem;
		if (explicitTarget.IsOk())   m_pagedRestoreSelection = explicitTarget;
		if (explicitTarget.IsOk())   m_pagedRestoreFocus     = explicitTarget;
		else if (currentFocus.IsOk()) m_pagedRestoreFocus    = currentFocus;
		// Capture raw row index (pre-refresh).  Used by
		// PagedBootstrap as the primary focus-restore for
		// RAM-backed paged models where row order is stable.
		m_pagedRestoreFocusRow = (m_currentRow != (unsigned)-1)
			? static_cast<long>(m_currentRow) : -1;
		// Was anything actually selected before refresh?  Drives whether
		// post-bootstrap focus-restore should also re-select the matched
		// row.  Explicit prefer (post-Save / SetViewMode) implies "select
		// this row" regardless; for plain refresh (currentFocus path) we
		// only re-select if user had a real selection going in.
		m_pagedRestoreFocusWasSelected = explicitTarget.IsOk()
			|| !m_selection.IsEmpty();
	}
	++m_pagedFetchGen;            // discard any in-flight async results
	m_pagedNeedsBootstrap = true; // arm wipe + fill at next idle
	}

void ibDataViewCtrl::PagedBootstrap()
{
	ibDataViewModel* model = GetModel();
		if (model == nullptr) return;

	// Wipe phase — runs at the start of bootstrap, NOT at the end of
	// PagedRefresh.  Eager wipe in PagedRefresh would leave the chrome
	// (header / footer / scrollbar) painting against an empty model
	// between PagedRefresh and the next idle (visualHost's outer
	// freeze releases at end of UpdateForm, before idle fires).
	// Folding the wipe in here keeps the OLD tree visible up to this
	// point, then Freeze → DestroyTree → Fetch → populate → Thaw
	// happens as one transaction inside the same idle pass.
	//
	// Idempotent: if a previous bootstrap already armed the freeze
	// (m_pagedFrozenForBootstrap = true), skip re-Freeze; we'll Thaw
	// once at the matching point at end of OnInternalIdle.
	const bool needsWipe = (m_root != nullptr && GetRowCount() > 0)
	                       || m_selection.GetSelectedCount() > 0
	                       || m_currentRow != (unsigned)-1;
	// Freeze ONLY the rows area — header keeps painting columns so the
	// user always sees the table chrome even while bootstrap rebuilds.
	if (!m_pagedFrozenForBootstrap) {
		if (m_tableAreaWin) m_tableAreaWin->Freeze();
		m_pagedFrozenForBootstrap = true;
	}
	if (needsWipe) {
		const unsigned prevSelCount = m_selection.GetSelectedCount();
		const unsigned prevCurRow   = m_currentRow;
		DestroyTree();
		m_root              = ibDataViewTreeNode::CreateRootNode();
		m_selection.Clear();
		m_currentRow        = (unsigned)-1;
		ClearRowHeightCache();
		InvalidateCount();
		// Deliberately NOT reset here:
		//   m_pagedHasMoreFwd / m_pagedHasMoreBwd
		//   m_pagedFwdAnchor / m_pagedBwdAnchor
		// Bootstrap below assigns them from the upcoming fetch result a
		// few lines down (see "m_pagedHasMoreFwd = ((int)n >= batch)").
		// Resetting here flips hasFwd to true for one frame, which the
		// lying-scrollbar sees as "rows exist ahead" → scrollbar shows
		// → fetch returns <batch → hasFwd=false → scrollbar hides.
		// Visible flicker on register / catalog lists shorter than one
		// viewport.  Same reason UpdateDisplay/UpdatePagedScrollbar are
		// not called mid-wipe — chrome must not see the zero-row state.
			}

	// Initial / refresh batch == the visible viewport PLUS one slack buffer, fetched in ONE go — so the buffer
	// has room to scroll into (a viewport-exact batch leaves nothing below the fold: the wheel can't scroll, so
	// the scroll-driven forward fetch never fires and rows "don't load until resize"). The extra beyond the
	// buffer still comes in via scroll-driven PagedFetchForward. Loading it here (not via an async idle-fill)
	// also kills the two-step "rows keep getting added on refresh".
	const int viewport = GetCountPerPage();
	const int batch    = (viewport > 0 ? viewport : 100) + static_cast<int>(kBufferSlack);

	const bool restoreFromSelection = m_pagedRestoreSelection.IsOk();
	const bool isTreeMode = (m_viewMode == ibDataViewViewMode::ibDataViewTree);
	const ibDataViewItem savedSelection = m_pagedRestoreSelection;
	// Selection-cursor mode (post-Add to empty folder, post-SetViewMode):
	// Select(item) stamps m_pagedRestoreSelection but not
	// m_pagedRestoreFocus, so the focus-restore loop below would skip
	// (savedFocus empty) even though the new row arrived in the fetch.
	// Fall back to m_pagedRestoreSelection in that case so the new row
	// becomes current.
	const ibDataViewItem savedFocus     = m_pagedRestoreFocus.IsOk()
		? m_pagedRestoreFocus
		: m_pagedRestoreSelection;

	// Tree-mode pre-walk: compute the savedFocus ancestor chain once
	// here so (a) the bootstrap fetch can use chain[last] (topmost
	// ancestor folder) as cursor and guarantee chain head lands in the
	// first batch even when there are more top-level folders than fit,
	// and (b) the walker below reuses the array without a second
	// BuildAncestorBreadcrumb roundtrip.  Empty when not in tree mode,
	// no savedFocus, or selection sits at the dataset root.
	ibDataViewItemArray treeCrumbs;
	if (isTreeMode && savedFocus.IsOk())
		model->BuildAncestorBreadcrumb(savedFocus, treeCrumbs);
	const ibDataViewItem treeChainHead = treeCrumbs.IsEmpty()
		? ibDataViewItem()
		: treeCrumbs[treeCrumbs.GetCount() - 1];

	// Hierarchical-mode auto-drill on selection-restore: target row
	// (e.g. selector form's pre-set value) sits in a subfolder.  Without
	// this, top-level fetch returns siblings of root + target's row
	// is never in the batch → focus-scan misses across all fetched.
	// Same primitive (BuildAncestorBreadcrumb) as Tree mode above; the
	// difference is the Hierarchical view consumes the chain via
	// m_topParentChain (drill state) instead of in-tree expand walker.
	const bool isHierarchical =
		(m_viewMode == ibDataViewViewMode::ibDataViewHierarchical);
	if (isHierarchical && restoreFromSelection
	    && m_topParentChain.IsEmpty()
	    && savedFocus.IsOk()) {
		ibDataViewItemArray hierCrumbs;
		model->BuildAncestorBreadcrumb(savedFocus, hierCrumbs);
		for (size_t i = 0; i < hierCrumbs.GetCount(); ++i)
			m_topParentChain.Add(hierCrumbs[i]);
		m_countFrozenHierarchicalRows =
			static_cast<int>(m_topParentChain.GetCount());
		// Re-init frozen-rows window to match the new chain depth.
		// SetTopParent / SetViewMode call this after mutating chain;
		// without it, chain crumbs render in the scrolling-rows area
		// (visible as extra "empty folder" rows above the data) until
		// the next manual Refresh re-runs the partition.
		InitializeFrozenWindows();
		InvalidateBestSize();
		CalcWindowSizes();
	}

	// Cursor selection:
	//   - Tree mode + restoreFromSelection + chain available →
	//     cursor=chain[last] (topmost ancestor folder).  Composite
	//     cursor predicate <= (isFolder=1, ...) includes ALL folders
	//     plus non-folders ordered before chain[last]; chain head is
	//     guaranteed in the batch so tree-auto-expand finds it at
	//     depth 0.  Backward-prefetch covers folders ordered before
	//     chain[last] when needed.
	//   - Tree mode + restoreFromSelection + no chain (selection at
	//     dataset root) → cursor=empty so the fetch starts from the
	//     top (folders first per `isFolder DESC`).
	//   - Non-tree + restoreFromSelection (post-Save / List or Hier
	//     SetViewMode) → cursor=selection so the new row lands in
	//     the fetched batch and can be centred via PagedFetchBackward.
	//   - Otherwise → cursor=anchor (saved top) so plain Refresh /
	//     sort keeps the viewport at the same row.  Selection-fallback
	//     (currentRow) is held in m_pagedRestoreFocus and applied
	//     only to currentRow / m_selection after the fetch — it
	//     doesn't drive the cursor and so doesn't shift the viewport.
	// Tree mode normally drives cursor by chain head (so chain[last]
	// pins into first batch + auto-expand walker reaches savedFocus).
	// When the chain is empty (selection at dataset root, or a row
	// just-added at top level in an empty folder) the chainHead is
	// empty → fetch starts from the top of the new ordering.  Fall
	// back to selection-as-cursor in that case so the new row lands
	// in the fetched batch (otherwise IsEqualTo scan misses and the
	// row never becomes current).
	//
	// For non-selection path: prefer anchor (saved top) when set so
	// plain Refresh keeps viewport on the same row.  For sort
	// (skipCapture wiped the anchor but preserved focus), fall back
	// to focus-as-cursor so the post-sort fetch positions the batch
	// around the previously-selected row — otherwise a focus that
	// resorts beyond the first batch (e.g. selected row goes from
	// position 5 to 90 after column re-sort) drops out of items[]
	// and the IsEqualTo scan misses, losing the selection.
	const ibDataViewItem restore = restoreFromSelection
		? (isTreeMode
			? (treeChainHead.IsOk() ? treeChainHead : m_pagedRestoreSelection)
			: m_pagedRestoreSelection)
		: (m_pagedRestoreAnchor.IsOk()
			? m_pagedRestoreAnchor
			: m_pagedRestoreFocus);

	const int preBootstrapSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	
	ibDataViewItemArray items;
	// Drill chain front in Hierarchical mode; empty (= top-level) in
	// List, Tree, and non-hierarchical models. A List view passes an
	// empty parent because the composer only scopes by parent when it
	// groups, which a List view never does.
	const ibDataViewItem effectiveParent = GetEffectiveFetchParent();
	unsigned int n = model->GetFirstFetch(effectiveParent, restore, batch, items);
		// Empty-fetch fallback: a cursor-mode fetch (selection / anchor /
	// focus) can return 0 rows when the cursor points at a row that
	// no longer matches the active filter — typical case is post-Add
	// of an element whose key fields fall outside the current filter,
	// which would otherwise leave the table fully empty.  Retry with
	// an empty cursor so the user still sees the filtered top batch.
	if (n == 0 && restore.IsOk()) {
		items.Clear();
		n = model->GetFirstFetch(effectiveParent, ibDataViewItem(), batch, items);
			}
	if (restore.IsOk()) {
		int anchorIdx = -1;
		for (unsigned i = 0; i < n; ++i)
			if (items[i] == restore) { anchorIdx = (int)i; break; }
	}
	// Heuristic: model returning fewer rows than the batch size means
	// it has nothing more to give in that direction.  Saves a wasted
	// round-trip that would otherwise return 0 just to discover this.
	m_pagedHasMoreFwd       = ((int)n >= batch);
	// Restore anchor implies "data may exist before us" only if the
	// forward fetch actually returned rows.  An empty fetch (drill into
	// an empty folder, filtered-out range) leaves no bwd anchor — so
	// PagedFetchBackward would no-op and m_pagedHasMoreBwd would stay
	// true forever, leaving a phantom scrollbar.  Tie hasBwd to n>0.
	m_pagedHasMoreBwd       = restore.IsOk() && n > 0;
	m_pagedNeedsBootstrap = false;
	m_pagedFwdAnchor        = (n > 0) ? items[n - 1] : ibDataViewItem();
	m_pagedBwdAnchor        = (n > 0) ? items[0]     : ibDataViewItem();
	m_pagedRestoreAnchor    = ibDataViewItem();
	m_pagedRestoreSelection = ibDataViewItem();
	// m_pagedRestoreFocus is intentionally NOT cleared here.  When
	// focus sits OUTSIDE the initial Reset batch (focus row < topRow,
	// or far past topRow + batch), the post-bootstrap focus-scan
	// MISSes; the row may still arrive via the queued backward /
	// forward prefetch.  OnPagedFetch{Backward,Forward}Result re-scans
	// the just-inserted items for m_pagedRestoreFocus and restores
	// currentRow + selection on match, then clears the field.  Next
	// PagedRefresh wipes whatever is left.

	if (m_root == nullptr)
		m_root = ibDataViewTreeNode::CreateRootNode();

	// Hierarchical breadcrumb: chain = [current, parent, grandparent,
	// ...].  Walk back-to-front (root ancestor first) so each crumb
	// becomes a child of the previous and the bottom of the chain
	// (front of the array) ends up holding the fetched children.
	ibDataViewTreeNode* insertParent = m_root;
	if (m_viewMode == ibDataViewViewMode::ibDataViewHierarchical
	    && !m_topParentChain.IsEmpty()) {
		for (unsigned int num = m_topParentChain.GetCount(); num > 0; --num) {
			ibDataViewTreeNode* crumb = new ibDataViewTreeNode(
				insertParent, m_topParentChain[num - 1],
				ibDataViewTreeNodeViewMode::ibDataViewHierarchy);
			crumb->SetHasChildren(true);
			crumb->ToggleOpen(this);
			insertParent->InsertChild(this, crumb, 0);
			if (insertParent->IsOpen())
				insertParent->ChangeSubTreeCount(1);
			insertParent = crumb;
		}
	}

	const unsigned int childCount = static_cast<unsigned int>(insertParent->GetChildNodes().size());
	for (unsigned int i = 0; i < n; ++i) {
		insertParent->InsertChild(this, MakeChildNode(insertParent, items[i]),
		                         childCount + i);
	}
	if (n > 0)
		insertParent->ChangeSubTreeCount(static_cast<int>(n));

	bool restoredViaTreeExpand = false;

	// Tree-mode auto-expand to selection: walk the breadcrumb chain
	// top → down, fetching each ancestor's children and toggling it
	// open so the saved selection is reachable in the tree on first
	// paint.  Top-level fetch above already populated m_root's
	// children — the topmost ancestor (chain[last]) lives there
	// because the bootstrap cursor was set to chain[last] (see
	// `restore` calc above), pinning it into the first batch even
	// when there are more top-level folders than fit the viewport.
	// Use savedFocus (= explicit prefer when present, else pre-refresh
	// currentRow item) so plain Refresh in tree mode also re-expands
	// the chain to the focused row.  Pre-fix this only fired on the
	// explicit-prefer path (restoreFromSelection), so plain Refresh in
	// tree mode lost expansion entirely.
	if (m_viewMode == ibDataViewViewMode::ibDataViewTree
	    && savedFocus.IsOk()) {
		// treeCrumbs precomputed above the bootstrap fetch so the
		// cursor could pin chain[last] into the first batch; reused
		// here without a second BuildAncestorBreadcrumb roundtrip.
				if (!treeCrumbs.IsEmpty()) {
			ibDataViewTreeNode* parentNode = m_root;
			for (int i = static_cast<int>(treeCrumbs.GetCount()) - 1; i >= 0; --i) {
				const ibDataViewItem& ancItem = treeCrumbs[i];
				ibDataViewTreeNode* match = nullptr;
				// Tree-restore-after-refresh path: ancItem comes
				// from BuildAncestorBreadcrumb (separate fetch),
				// parentNode.children was filled by current bootstrap
				// — pointers usually differ, so we rely on
				// IsEqualTo (m_objGuid for tree nodes) here.
				for (auto* c : parentNode->GetChildNodes()) {
					if (c->GetItem() == ancItem) { match = c; break; }
				}
				if (match == nullptr) {
										break;
				}
				if (!match->HasChildren()) match->SetHasChildren(true);
				if (!match->IsOpen()) {
					ibDataViewItemArray ancChildren;
					unsigned int cn = model->GetFirstFetch(ancItem,
					    ibDataViewItem(), batch, ancChildren);
					const unsigned int already =
						static_cast<unsigned int>(match->GetChildNodes().size());
					for (unsigned int k = 0; k < cn; ++k) {
						match->InsertChild(this,
						    MakeChildNode(match, ancChildren[k]),
						    already + k);
					}
					match->ToggleOpen(this);   // open + propagate subtree count
									}
				parentNode = match;
			}
			// Locate selection among deepest parent's children.  See
			// FindVisibleRowInTree for the value-eq walker that handles
			// fresh post-fetch pointers (pointer-id always misses).
			int row = FindVisibleRowInTree(savedFocus);
			if (row != wxNOT_FOUND) {
				ChangeCurrentRow(static_cast<unsigned int>(row));
				if (m_pagedRestoreFocusWasSelected)
					m_selection.SelectItem(static_cast<unsigned int>(row), true);
				restoredViaTreeExpand = true;
							} else {
							}
		}
	}

	// If the user had a focused / selected row before the refresh,
	// find the same business row in the new ordering by data-compare
	// (ibDataViewItem::operator== now dispatches to ibDataViewObject's
	// IsEqualTo virtual, which ibComposerNode overrides to compare
	// row values).  Restore focus + selection on the matching index.
	// Tree-mode auto-expand above already handled deep selection; skip
	// the top-level scan when it succeeded.
	//
	// Row-index offset by crumbCount: in Hierarchical mode the
	// breadcrumb nodes occupy the first crumbCount rows of the tree,
	// so items[i] (a child under the deepest crumb) renders at tree
	// row crumbCount + i.  In List / Tree this is zero.  Without the
	// offset, selecting items[0] lands focus on the topmost crumb
	// row instead of the actual data row.
	// Focus restore: anchor-mode (plain Refresh / sort) and
	// selection-mode both want to re-apply currentRow to the row the
	// user had focused before the refresh.  Drive it from
	// m_pagedRestoreFocus (= explicit prefer when present, else
	// pre-refresh currentRow) so anchor-mode also re-selects the row
	// without hijacking the viewport via the fetch cursor.
		if (!restoredViaTreeExpand && n > 0 && savedFocus.IsOk()) {
		const unsigned int crumbCount =
			static_cast<unsigned int>(m_topParentChain.GetCount());
		bool found = false;
		// RAM-backed paged models preserve row order across refresh
		// (m_nodeValues survives, save/reload writes by line number),
		// and IsEqualTo on ibComposerNode value-compares m_nodeValues
		// → for default-valued TabularSection rows that returns true
		// across unrelated rows.  Restore focus by raw row index
		// instead.  DB-backed paged keeps the value-eq scan because
		// rows there have unique GUIDs.
		const bool ramFetch = !model->HasKeyedRows();
		if (ramFetch) {
			// RAM: match by the live storage node's IDENTITY — its pointer IS the stable row id (RAM RunComposerPage
			// returns the same storage nodes). Value-compare mismatches non-unique / default-valued rows (the jump);
			// raw index breaks when the focus sits past the window. The batch was widened above to include the focus
			// node, so it is present here — found wherever it now sits, robust to re-order.
			for (unsigned int i = 0; i < n && !found; ++i) {
				if (items[i].GetID() == savedFocus.GetID()) {
					const unsigned int row = crumbCount + i;
					ChangeCurrentRow(row);
					if (m_pagedRestoreFocusWasSelected)
						m_selection.SelectItem(row, true);
					found = true;
				}
			}
		}
		else {
			// DB (keyed): value-compare scan — rows carry unique guids so IsEqualTo re-locates the same business row.
			for (unsigned int i = 0; i < n && !found; ++i) {
				if (items[i] == savedFocus) {
					const unsigned int row = crumbCount + i;
					ChangeCurrentRow(row);
					if (m_pagedRestoreFocusWasSelected)
						m_selection.SelectItem(row, true);
					found = true;
					break;
				}
			}
		}
				}

	const int preUpdateSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	InvalidateCount();
	UpdateDisplay();
	UpdatePagedScrollbar();
	const int postUpdateSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	const wxSize vsz = m_tableAreaWin ? m_tableAreaWin->GetVirtualSize() : wxSize(0,0);
	
	// Reflect the composer's active sort onto the column-header arrows after the refresh. A header CLICK sets
	// the clicked column's arrow directly (OnColumnClick), but a SETTINGS-DIALOG Filter/Sort/Group commit reaches
	// the columns only here (it just mutates the composer + NotifyReset). Drop every arrow, then let each column
	// re-apply its own from the composer (matching by its bound field — no model-side sort-arrow bridge).
	ResetAllSortColumns();
	for (unsigned int ci = 0; ci < GetColumnCount(); ++ci)
		if (ibDataViewColumn* c = GetColumn(ci))
			c->SyncSortArrowFromModel();

	// Backward fetch only in selection-cursor mode (post-Save new row,
	// SetViewMode switch).  We want the explicit-prefer row in the
	// middle of the window, not pinned at top, so users see context
	// before/after the new row.  In anchor-cursor mode (plain Refresh
	// / sort) items[0] IS the saved top — pulling backward would
	// scroll the viewport above the saved top and the user would see
	// a different top row.  Cold open (no anchor, no selection)
	// reaches here with restore=empty and skips both branches.
	if (restoreFromSelection && m_pagedHasMoreBwd) {
		// Reset scroll-Y to items[0]=focus position BEFORE backward
		// fetch.  wxScroll's sy survived through the wipe (e.g.
		// preSy=374 from before refresh), but the new buffer is
		// smaller — sy=374 in old virt-size 968 maps to a row that
		// doesn't correspond to anything sensible in the new 20-row
		// tree.  Without this reset, bwd-result computes
		// topRowBeforeAdj from the stale sy, and restoreTopRow ends
		// up far past where focus actually is — focus visually lands
		// off-screen.  ScrollTo(crumbCount) puts focus (= items[0])
		// at viewport top; bwd-result then computes restoreTopRow=n
		// (= focus row after prepend) and centres the viewport on
		// focus, which is the "anchor-in-middle" UX intent.
		const int crumbCount = static_cast<int>(m_topParentChain.GetCount());
				ScrollTo(crumbCount, -1);
				PagedFetchBackward(batch);
	}
	else if (!restoreFromSelection && restore.IsOk()) {
		// Anchor-cursor mode (plain Refresh / sort) for List / Tree /
		// Hierarchical.
		//   1. wxScroll's pixel scroll-Y survived the wipe
		//      (DestroyTree doesn't reset scroll position).  In the
		//      new buffer items[0] IS the saved top, so without an
		//      explicit ScrollTo the viewport lands N rows past where
		//      the user was.  ScrollTo(crumbCount) places items[0]
		//      at the top visible row → viewport preserved.
		//   2. Pull rows BEFORE saved top into the loaded buffer so
		//      the user can scroll back without us re-fetching: Reset
		//      cursor only loads rows from saved top onwards, leaving
		//      the head of the dataset unloaded.  Without this the
		//      lying scrollbar reaches Bottom on forward exhaustion
		//      and stops asking for backward batches, so rows before
		//      saved top are visually GONE forever.
		// Tree-auto-expand above (when savedFocus has crumbs) already
		// called ChangeCurrentRow but did not scroll; ScrollTo here
		// is what actually positions the viewport.
		const int crumbCount = static_cast<int>(m_topParentChain.GetCount());
				ScrollTo(crumbCount, -1);
		if (m_pagedHasMoreBwd) {
						PagedFetchBackward(batch);
		}
	}
	else {
			}
	const int finalSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	}

void ibDataViewCtrl::DispatchPagedFetch(PagedFetchDir dir, int batch)
{
	const bool isForward = (dir == PagedFetchDir::Forward);
	const char* dirS = isForward ? "Forward" : "Backward";

	ibDataViewModel* model = GetModel();
	const ibDataViewItem& anchorRef = isForward ? m_pagedFwdAnchor : m_pagedBwdAnchor;
	int& fetchingCounter = isForward ? m_pagedFetchingFwd : m_pagedFetchingBwd;
	if (model == nullptr || m_root == nullptr || !anchorRef.IsOk()) {
				return;
	}
	if (fetchingCounter > 0) {
				return;
	}

	++fetchingCounter;
	const uint64_t  gen      = m_pagedFetchGen;
	const ibDataViewItem anchor = anchorRef;             // refcounted copy
	const ibDataViewItem parentSnapshot = GetEffectiveFetchParent();
	
	struct FetchResult {
		ibDataViewItemArray items;
		unsigned int        n = 0;
	};
	auto result = std::make_shared<FetchResult>();

	// wxWeakRef captures both for the worker-side lambda and the
	// CallAfter-marshalled UI-side lambda.  If the control is
	// destroyed at any point between submit and final dispatch,
	// both lambdas safely no-op.
	wxWeakRef<ibDataViewCtrl> weakSelf(this);
	model->SubmitFetchAsync([model, parentSnapshot, anchor, batch, weakSelf, gen, result, isForward]() {
		// Worker thread: pure DB roundtrip, no UI access.
		result->n = isForward
			? model->GetNextFetch(parentSnapshot, anchor, batch, result->items)
			: model->GetPrevFetch(parentSnapshot, anchor, batch, result->items);
		const char* dirSworker = isForward ? "Forward" : "Backward";
				// Marshal back to UI thread; gen check discards the result if
		// the buffer has been wiped (sort / refresh) since submit.
		auto* self = weakSelf.get();
		if (self == nullptr) return;   // control gone before result
		self->CallAfter([weakSelf, batch, gen, result, isForward]() {
			auto* s = weakSelf.get();
			if (s == nullptr) return;
			if (isForward) --s->m_pagedFetchingFwd;
			else           --s->m_pagedFetchingBwd;
			const char* dirSdisp = isForward ? "Forward" : "Backward";
			if (gen != s->m_pagedFetchGen) {
								return;
			}
						if (isForward) s->OnPagedFetchForwardResult(result->items, result->n, batch);
			else           s->OnPagedFetchBackwardResult(result->items, result->n, batch);
		});
	});
}

void ibDataViewCtrl::OnPagedFetchForwardResult(ibDataViewItemArray& items,
	unsigned int n, int batch)
{
	const int entrySy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
		if (n == 0) {
				m_pagedHasMoreFwd = false;
				UpdatePagedScrollbar();
		return;
	}

	// Freeze paints across append + trim + ScrollTo so the user sees
	// only the final, consistent frame.  Without this the selection
	// flickers at an intermediate position between trim (which shifts
	// row indices) and ScrollTo (which corrects the scroll-Y).
	ScopedPagedFreeze freeze(this);
	m_pagedFwdAnchor = items[n - 1];
	// Heuristic: model returned fewer rows than requested → no more
	// data forward.  Flip exhaustion now instead of waiting for the
	// next fetch to come back with n=0.
	if ((int)n < batch) {
				m_pagedHasMoreFwd = false;
	}

	// Append n new rows at the end of the loaded window.  In drill
	// mode, "loaded window" means the data rows under the deepest
	// crumb — NOT under m_root (which has only the topmost crumb as a
	// child).  Inserting under m_root would render new rows as
	// siblings of the breadcrumb and break the indent shape.
	ibDataViewModel* model = GetModel();
	ibDataViewTreeNode* const insertParent = GetPagedInsertParent();
	if (insertParent == nullptr) return;  // freeze auto-Thaws via dtor
	const long crumbCount = static_cast<long>(m_topParentChain.GetCount());
	const unsigned int childCount = static_cast<unsigned int>(insertParent->GetChildNodes().size());
	for (unsigned int i = 0; i < n; ++i) {
		insertParent->InsertChild(this, MakeChildNode(insertParent, items[i]),
		                         childCount + i);
	}
	insertParent->ChangeSubTreeCount(static_cast<int>(n));   // propagates up to m_root

	// Trim front to keep buffer at viewport + 2*kBufferSlack — but
	// never inside the visible viewport.  wxDVC's scroll position is
	// pixel-based; trimming N rows from front shifts every remaining
	// row's index by -N, so the item under the user's scroll pixel
	// changes → visual JUMP.  Cap the trim at "rows above viewport
	// minus slack reserve" so the visible window and a small backward
	// buffer stay intact.  If we can't reach target this turn, leave
	// the buffer slightly oversized; the next forward scroll past
	// kBufferSlack will trim opportunistically.
	//
	// In drill mode `over` measures the data window only (children of
	// the deepest crumb); the crumb chain itself isn't counted toward
	// the buffer budget.  Selection-store / currentRow indices are
	// flat-tree positions, so trim shifts must use crumbCount as the
	// base offset for the data area.
	const long target = static_cast<long>(GetCountPerPage()) + 2 * kBufferSlack;
	const long over   = static_cast<long>(insertParent->GetChildNodes().size()) - target;
	long trimmed = 0;
	long restoreTopRow = -1;     // -1 = keep current scroll
	if (over > 0) {
		const long topRow       = static_cast<long>(GetRowByItem(GetTopItem()));
		const long topRowAdj    = topRow >= 0 ? topRow : 0;
		// topRow is a flat-tree index; the data area starts at crumbCount.
		// `dataTop` is how many data rows sit above the visible viewport.
		const long dataTop      = std::max(0L, topRowAdj - crumbCount);
		const long maxTrimFront = std::max(0L, dataTop - kBufferSlack);
		long actualTrim         = std::min(over, maxTrimFront);
		// Pin the focused row + any selected rows: never trim through
		// the lowest pinned row index.  Selection (single or multi)
		// survives until the user clicks somewhere else.  pinMin is a
		// flat-tree index → translate to the data-area index for cap.
		long pinMin, pinMax;
		GetPagedPinRange(pinMin, pinMax);
		if (pinMin >= 0) {
			const long pinMinData = pinMin - crumbCount;
			if (pinMinData >= 0 && pinMinData < actualTrim)
				actualTrim = pinMinData;
		}
		if (actualTrim > 0) {
			auto& kids = insertParent->GetChildNodes();
			for (long i = 0; i < actualTrim; ++i) {
				ibDataViewTreeNode* node = kids[0];
				if (m_rowHeightCache) m_rowHeightCache->Remove(static_cast<unsigned>(crumbCount));
				insertParent->RemoveChild(0);
				delete node;
			}
			insertParent->ChangeSubTreeCount(-static_cast<int>(actualTrim));
			// Selection / currentRow speak in flat-tree indices.  The
			// trimmed range is [crumbCount, crumbCount + actualTrim).
			if (!m_selection.IsEmpty()) {
								m_selection.OnItemsDeleted(static_cast<unsigned>(crumbCount),
				                           static_cast<unsigned>(actualTrim));
							}
			if (m_currentRow != (unsigned)-1) {
				const long cur = static_cast<long>(m_currentRow);
				if (cur < crumbCount) {
					// Crumb row — unchanged
				} else if (cur < crumbCount + actualTrim) {
										m_currentRow = (unsigned)-1;          // inside trimmed range
				} else {
					m_currentRow -= static_cast<unsigned>(actualTrim);
				}
			}
			m_pagedHasMoreBwd = true;
			if (!kids.empty())
				m_pagedBwdAnchor = kids.front()->GetItem();
			trimmed = actualTrim;
			// Same items the user was viewing are now at lower indices
			// (every remaining row shifted by -actualTrim).  Re-scroll
			// to keep the same item at the visible top — without this
			// wxDVC's pixel scroll position doesn't change, but its
			// row<->pixel mapping does, so the user would see a jump.
			restoreTopRow = topRowAdj - actualTrim;
		}
	}

	
	// Late focus restoration: if the saved focus didn't appear in the
	// initial Reset batch (focus past the first viewport-sized window
	// from anchor), it may now be in the rows we just appended.  Scan
	// the new items[] and restore currentRow / selection on match.
	// Only when no current selection — don't override an explicit user
	// click that happened between bootstrap and now.
	if (m_pagedRestoreFocus.IsOk() && m_currentRow == (unsigned)-1
	    && m_selection.IsEmpty()) {
		const long crumbCountFR = static_cast<long>(m_topParentChain.GetCount());
		const long dataInsertOffset = static_cast<long>(insertParent->GetChildNodes().size())
		                              - static_cast<long>(n);
		// RAM matches the off-screen focus by its live node IDENTITY (pointer) — value-compare drops a selection
		// that scrolled out of the fetched batch when rows are non-unique. Keyed DB keeps the value-eq scan.
		const bool keyedFR = GetModel() && GetModel()->HasKeyedRows();
		for (unsigned int i = 0; i < n; ++i) {
			if (keyedFR ? (items[i] == m_pagedRestoreFocus)
			            : (items[i].GetID() == m_pagedRestoreFocus.GetID())) {
				const unsigned row =
					static_cast<unsigned>(crumbCountFR + dataInsertOffset)
					+ i;
				ChangeCurrentRow(row);
				if (m_pagedRestoreFocusWasSelected)
					m_selection.SelectItem(row, true);
				m_pagedRestoreFocus = ibDataViewItem();
								break;
			}
		}
	}

	InvalidateCount();
	RecalculateDisplay();
	const int afterRecalcSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	const wxSize afterRecalcVsz = m_tableAreaWin ?
		m_tableAreaWin->GetVirtualSize() : wxSize(0,0);
		if (restoreTopRow >= 0) {
				ScrollTo(static_cast<int>(restoreTopRow), -1);
	}
	UpdatePagedScrollbar();
	const int exitSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	}  // ScopedPagedFreeze auto-Thaws here

void ibDataViewCtrl::OnPagedFetchBackwardResult(ibDataViewItemArray& items,
	unsigned int n, int batch)
{
	const int entrySy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
		if (n == 0) {
		m_pagedHasMoreBwd = false;
				UpdatePagedScrollbar();
		return;
	}
	m_pagedBwdAnchor = items[0];

	// Freeze paints across prepend + trim + ScrollTo so the user sees
	// only the final, consistent frame (avoids selection flicker).
	ScopedPagedFreeze freeze(this);
	// Heuristic: model returned fewer rows than requested → no more
	// data backward.  Flip exhaustion now.
	if ((int)n < batch)
		m_pagedHasMoreBwd = false;

	// Capture the user's visible top BEFORE the prepend so we can
	// re-scroll after the row indices shift.
	const long topRowBefore = static_cast<long>(GetRowByItem(GetTopItem()));
	const long topRowBeforeAdj = topRowBefore >= 0 ? topRowBefore : 0;

	// Prepend n new rows at the front of the data window — under the
	// deepest crumb in drill mode, m_root in flat mode.
	ibDataViewModel* model = GetModel();
	ibDataViewTreeNode* const insertParent = GetPagedInsertParent();
	if (insertParent == nullptr) return;  // freeze auto-Thaws via dtor
	const long crumbCount = static_cast<long>(m_topParentChain.GetCount());
	for (unsigned int i = 0; i < n; ++i) {
		insertParent->InsertChild(this, MakeChildNode(insertParent, items[i]), i);
	}
	insertParent->ChangeSubTreeCount(static_cast<int>(n));   // propagates up
	// Selection/currentRow are flat-tree indices.  Inserts happened at
	// flat range [crumbCount, crumbCount + n) — rows below shift by +n.
	if (!m_selection.IsEmpty()) {
				m_selection.OnItemsInserted(static_cast<unsigned>(crumbCount), n);
			}
	if (m_currentRow != (unsigned)-1
	    && static_cast<long>(m_currentRow) >= crumbCount) {
		const unsigned oldCurr = m_currentRow;
		m_currentRow += n;
			}

	// Late focus restoration: if savedFocus was BEFORE the bootstrap
	// anchor (focusRow < topRow at refresh time), it didn't appear in
	// the initial Reset batch and focus-scan MISSed.  The just-
	// prepended backward batch may now contain it.  Scan items[]
	// (which are the new prepended rows at flat indices [crumbCount,
	// crumbCount+n) ) and restore currentRow / selection on match.
	// Skipped if the user already has a selection (don't override).
	if (m_pagedRestoreFocus.IsOk() && m_currentRow == (unsigned)-1
	    && m_selection.IsEmpty()) {
		// RAM matches the off-screen focus by live node IDENTITY (value-compare drops a non-unique row); keyed DB
		// keeps the value-eq scan on its unique guids.
		const bool keyedBR = GetModel() && GetModel()->HasKeyedRows();
		for (unsigned int i = 0; i < n; ++i) {
			if (keyedBR ? (items[i] == m_pagedRestoreFocus)
			            : (items[i].GetID() == m_pagedRestoreFocus.GetID())) {
				const unsigned row = static_cast<unsigned>(crumbCount) + i;
				ChangeCurrentRow(row);
				if (m_pagedRestoreFocusWasSelected)
					m_selection.SelectItem(row, true);
				m_pagedRestoreFocus = ibDataViewItem();
								break;
			}
		}
	}

	// Same items the user was viewing are now at higher indices
	// (every existing row shifted by +n after prepend).  We re-scroll
	// at the very end of the function — after RecalculateDisplay has
	// updated the virtual size, otherwise ScrollTo can clamp.
	const long restoreTopRow = topRowBeforeAdj + static_cast<long>(n);

	// Trim tail to keep buffer at viewport + 2*kBufferSlack — mirror
	// of PagedFetchForward's front guard: never trim into the visible
	// viewport.  Note: GetRowByItem(GetTopItem()) here would return
	// the PRE-prepend pixel→row index (wxScroll's pixel position is
	// unchanged by the InsertChild calls), pointing at one of the
	// freshly-prepended rows instead of the user's actual viewing top.
	// Use the already-shifted logical top (topRowBeforeAdj + n) so the
	// trim guard reflects where the user's view will land after
	// ScrollTo(restoreTopRow) below.
	//
	// `over`, `lastIdx` measure the data window only (insertParent's
	// children).  `bottomRow` is a flat-tree index, so we translate
	// through crumbCount when comparing to the data-area lastIdx.
	const long target = static_cast<long>(GetCountPerPage()) + 2 * kBufferSlack;
	const long over   = static_cast<long>(insertParent->GetChildNodes().size()) - target;
	long trimmed = 0;
	if (over > 0) {
		const long topRowAdj    = restoreTopRow;
		const long bottomRow    = topRowAdj + GetCountPerPage();
		const long bottomData   = bottomRow - crumbCount;
		const long lastDataIdx  = static_cast<long>(insertParent->GetChildNodes().size()) - 1;
		const long maxTrimBack  = std::max(0L, lastDataIdx - bottomData - kBufferSlack);
		long actualTrim         = std::min(over, maxTrimBack);
		// Pin the focused row + any selected rows: never trim past the
		// highest pinned row index from the tail.  pinMax is a flat-
		// tree index; translate to the data-area index for cap.
		long pinMin, pinMax;
		GetPagedPinRange(pinMin, pinMax);
		if (pinMax >= 0) {
			const long pinMaxData = pinMax - crumbCount;
			const long maxAllowed = lastDataIdx - pinMaxData;
			if (maxAllowed >= 0 && actualTrim > maxAllowed)
				actualTrim = maxAllowed;
		}
		if (actualTrim > 0) {
			auto& kids = insertParent->GetChildNodes();
			for (long i = 0; i < actualTrim; ++i) {
				const unsigned idx = static_cast<unsigned>(kids.size() - 1);
				ibDataViewTreeNode* node = kids[idx];
				// Row-height cache is keyed by flat-tree index — translate.
				if (m_rowHeightCache)
					m_rowHeightCache->Remove(static_cast<unsigned>(crumbCount + idx));
				insertParent->RemoveChild(idx);
				delete node;
			}
			insertParent->ChangeSubTreeCount(-static_cast<int>(actualTrim));
			// Flat-tree size after trim = crumbs + remaining data rows.
			const unsigned newDataSize = static_cast<unsigned>(insertParent->GetChildNodes().size());
			const unsigned newFlatSize = static_cast<unsigned>(crumbCount) + newDataSize;
			// Trimmed flat range = [newFlatSize, newFlatSize + actualTrim).
			if (!m_selection.IsEmpty()) {
								m_selection.OnItemsDeleted(newFlatSize, static_cast<unsigned>(actualTrim));
							}
			if (m_currentRow != (unsigned)-1 && m_currentRow >= newFlatSize) {
								m_currentRow = (unsigned)-1;
			}
			m_pagedHasMoreFwd = true;
			if (!kids.empty())
				m_pagedFwdAnchor = kids.back()->GetItem();
			trimmed = actualTrim;
		}
	}

	
	InvalidateCount();
	RecalculateDisplay();
	const int afterRecalcSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
		ScrollTo(static_cast<int>(restoreTopRow), -1);
	UpdatePagedScrollbar();
	const int exitSy = m_tableAreaWin ?
		CalcUnscrolledPosition(wxPoint(0, 0)).y : 0;
	}  // ScopedPagedFreeze auto-Thaws here

#endif // wxUSE_DATAVIEWCTRL
