/////////////////////////////////////////////////////////////////////////////
// Name:        datavgen.paged.cpp
// Purpose:     Paged-mode bootstrap + fetch dispatch for ibDataViewCtrl.
//              Extracted from datavgen.cpp on 2026-05-08 to keep that file
//              from growing past 9.5K LOC.  Methods stay members of
//              ibDataViewCtrl — class definition lives in datavgen.h —
//              this is purely a translation-unit split, no behavioural
//              change.
// Methods (fetch only — the busy indicator moved next to the painting it does,
//          in datavgen.cpp beside ibDataViewMainWindow::OnPaint):
//   GetPagedInsertParent      — drill-chain insert parent walker
//   SchedulePagedRefresh      — debounced refresh dispatcher
//   PagedRefresh              — capture restore-state, arm the Reset portion
//   OnPagedFetchResetComplete — the Reset answer: wipe + fill + auto-expand + restore
//   ReadPagedFetchReset       — the Reset read (free function: model only, no control)
//   DispatchPagedFetch        — THE door: one request, direction inside (Reset /
//                               Forward / Backward), answer back on this thread
//   OnPagedFetchForwardComplete — append+trim path
//   OnPagedFetchBackwardComplete— prepend path
/////////////////////////////////////////////////////////////////////////////

#include <wx/wxprec.h>

#if wxUSE_DATAVIEWCTRL

#include "dataview.h"
#include "backend/appData.h"               // ibApplicationData::GetLogger
#include "backend/backend_exception.h"     // ibBackendException — what a failed portion throws
#include "backend/logger/logger.h"         // a portion that failed leaves a row in the journal
#include "datavgen.window.private.h"  // ibDataViewMainWindow — the rows area this freezes / scrolls
#include "datavgen.paged.private.h"   // kBufferSlack + ScopedPagedFreeze + ibPagedFetch

#include <wx/weakref.h>
#include <wx/generic/private/rowheightcache.h>   // HeightCache::Remove

#include <algorithm>
#include <memory>

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

	// A READ ALREADY OUT WAS ASKED THE OLD QUESTION. Where to position is decided
	// at DISPATCH, so a stamp arriving after that is never read — and worse, the
	// answer clears it on arrival. That is a row created and then not selected,
	// which is exactly what this method exists to prevent.
	//
	// While the bootstrap was synchronous the window did not exist (this always
	// landed before it ran). Now it does, so the in-flight answer is discarded by
	// the generation bump and a fresh Reset is armed — it will be dispatched on the
	// next idle and will position on the row just stamped.
	if (item.IsOk() && IsFetchInFlight()) {
		++m_pagedFetchGen;
		m_pagedNeedsBootstrap = true;
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

	// Postponed on THIS thread, not dispatched: PagedRefresh is control state, so it
	// must not go through the model's fetch door, which for a runtime table is a
	// worker. The flag above coalesces a burst of notifier hooks into one refresh.
	if (wxTheApp == nullptr) {
		m_pagedRefreshScheduled = false;
		return;
	}
	wxWeakRef<ibDataViewCtrl> weakSelf(this);
	wxTheApp->CallAfter([weakSelf]{
		auto* self = weakSelf.get();
		if (self == nullptr) return;   // control gone — drop the refresh
		self->m_pagedRefreshScheduled = false;
		ibDataViewItem sel = self->m_pagedRefreshSelection;
		self->m_pagedRefreshSelection = ibDataViewItem();
		self->PagedRefresh(sel);
	});
}

void ibDataViewCtrl::PagedRefresh(const ibDataViewItem& preferSelection)
{
		// PagedRefresh runs synchronously inline (e.g. inside OnUpdated /
	// notifier reset). The Reset portion is dispatched later, from OnInternalIdle.
	// We DELIBERATELY do not wipe the tree here — wiping eagerly would
	// leave the chrome (header / footer / scrollbar) painting against
	// an empty model between this point and the next idle, which is
	// the visible "flicker" the user reported.  Instead this method
	// only captures restore-state + bumps the cancellation generation
	// + arms the bootstrap flag; the actual DestroyTree + buffer reset
	// happens when the Reset portion ARRIVES (OnPagedFetchResetComplete), so wipe + fill
	// land in a single freeze window.
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
	// WHERE THE USER WAS LOOKING, kept as a NUMBER rather than as a row. The sort
	// path throws the anchor ITEM away on purpose — its cursor key means nothing in
	// the new ordering — and this distance is all that is left of the viewport
	// afterwards: put the focused row back this many rows below the top and the
	// screen reads the same, re-ordered. Read from the live top even under
	// skipCapture, because the ITEM is what is invalid there, not the position.
	const int visibleTopRow = (int)GetRowByItem(GetTopItem());
	// Which shade the row at the top is wearing right now. The rebuild puts that
	// same business row back at the head of the buffer, and the stripe phase is set
	// from this so it keeps it — see m_stripePhase.
	m_stripeTopParity = (visibleTopRow >= 0) ? (visibleTopRow & 1) : -1;
	const int focusRow      = currentFocus.IsOk() ? (int)GetRowByItem(currentFocus) : -1;
	int focusOffset = -1;
	if (visibleTopRow >= 0 && focusRow >= visibleTopRow) {
		// Only when the focus was actually ON SCREEN. One scrolled out of view has
		// no offset worth preserving — restoring it would land the sorted list
		// where the user was not looking either.
		const int dist = focusRow - visibleTopRow;
		if (dist < GetCountPerPage())
			focusOffset = dist;
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
		m_pagedRestoreFocusWasSelected = !m_selection.IsEmpty();
		// The only path that needs it: no anchor survives here, so the offset is
		// what puts the viewport back. Drill sets it too and simply never
		// consumes it — its focus belongs to the folder we just left, so the
		// scan below misses and the offset is dropped with it.
		m_pagedRestoreFocusOffset = focusOffset;
	} else {
		if (topItem.IsOk())          m_pagedRestoreAnchor    = topItem;
		if (explicitTarget.IsOk())   m_pagedRestoreSelection = explicitTarget;
		// WHAT SOMEBODY ASKED FOR BEATS WHERE THE CURSOR HAPPENS TO BE. An explicit
		// prefer wins; then a programmatic stamp (SetPagedRestoreSelection — a row
		// just created and told to be selected), which may have landed BEFORE this
		// refresh, since the refresh is posted onto the event loop; and only if
		// neither is present does the pre-refresh current row stand in.
		//
		// Without the middle case the second created row lost: the first one had no
		// prior selection so the stamp survived, the second found the previous row
		// as the current one and this line wrote it over the stamp — so the list
		// re-selected what was there before.
		if (explicitTarget.IsOk())            m_pagedRestoreFocus = explicitTarget;
		else if (m_pagedRestoreSelection.IsOk()) { /* keep the stamped row */ }
		else if (currentFocus.IsOk())         m_pagedRestoreFocus = currentFocus;
		// Capture raw row index (pre-refresh).  Used by
		// PagedBootstrap as the primary focus-restore for
		// RAM-backed paged models where row order is stable.
		// Was anything actually selected before refresh?  Drives whether
		// post-bootstrap focus-restore should also re-select the matched
		// row.  Explicit prefer (post-Save / SetViewMode) implies "select
		// this row" regardless; for plain refresh (currentFocus path) we
		// only re-select if user had a real selection going in.
		// …or a programmatic Select has already stamped a row. That stamp MEANS
		// "select it", and it can arrive before this refresh rather than after it —
		// the refresh is posted onto the event loop, so a post-Save Select lands
		// first. Recomputing the flag from an empty selection store then threw the
		// intent away: the new row was positioned to, and not highlighted.
		m_pagedRestoreFocusWasSelected = explicitTarget.IsOk()
			|| !m_selection.IsEmpty()
			|| m_pagedRestoreSelection.IsOk();
		// The anchor survived here, and it is the stronger answer: it names the
		// row the user had at the top, not a distance from it. Nothing to
		// preserve by offset, and a stale one must not leak into this path.
		m_pagedRestoreFocusOffset = -1;
	}
	++m_pagedFetchGen;            // discard any in-flight async results
	m_pagedNeedsBootstrap = true; // arm wipe + fill at next idle
	}

void ibDataViewCtrl::OnPagedFetchResetComplete(ibPagedFetch& req)
{
	ibDataViewModel* model = GetModel();
	if (model == nullptr) return;

	// Names the rest of this method already used, now answered by the parcel.
	const int  batch                = req.m_batch;
	const bool restoreFromSelection = req.m_restoreFromSelection;
	const ibDataViewItem& restore    = req.m_cursor;
	const ibDataViewItem& savedFocus = req.m_savedFocus;
	ibDataViewItemArray&  items      = req.m_items;
	ibDataViewItemArray&  treeCrumbs = req.m_treeCrumbs;
	const unsigned int    n          = req.m_n;

	// WIPE AND FILL AS ONE TRANSACTION, and only now that there is something to
	// fill with. The rows area has been frozen since the wipe, so what is still on
	// screen is the last finished frame; from here to EndBootstrapFreeze below the
	// control is frozen too, and what the user sees next is the next finished frame
	// — never the zero-row state in between, and never a paint per step of the
	// rebuild.
	ScopedPagedFreeze freeze(this);

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
	// (m_pagedFrozenForBootstrap = true), skip re-Freeze; EndBootstrapFreeze
	// at the end of this method drops it once, whoever armed it.
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

	// The drill chain the read walked, applied now — this is the UI half of the
	// hierarchical auto-drill: the read only asked WHERE the row lives (and fetched
	// the page under that folder); the chain, the frozen-rows partition and the
	// geometry belong here.
	if (req.m_hierAutoDrill && !req.m_hierCrumbs.IsEmpty()) {
		for (size_t i = 0; i < req.m_hierCrumbs.GetCount(); ++i)
			m_topParentChain.Add(req.m_hierCrumbs[i]);
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

	// Heuristic: model returning fewer rows than the batch size means
	// it has nothing more to give in that direction.  Saves a wasted
	// round-trip that would otherwise return 0 just to discover this.
	// Counting the FORWARD part only: the backfilled rows came from above the
	// cursor and say nothing about what is left below it. Including them made a
	// short page look full, which cost an extra empty round-trip — and an extra
	// portion is an extra frame.
	m_pagedHasMoreFwd       = ((int)(n - req.m_backfilled) >= batch);
	// Restore anchor implies "data may exist before us" only if the
	// forward fetch actually returned rows.  An empty fetch (drill into
	// an empty folder, filtered-out range) leaves no bwd anchor — so
	// PagedFetchBackward would no-op and m_pagedHasMoreBwd would stay
	// true forever, leaving a phantom scrollbar.  Tie hasBwd to n>0.
	m_pagedHasMoreBwd       = restore.IsOk() && n > 0;
	// UNLESS THE BACKFILL ALREADY HIT THE TOP. It asked for a fixed number of rows
	// above the cursor and got fewer, which is the same "nothing more that way"
	// the forward heuristic reads from a short page — and leaving the flag up
	// costs a backward portion that comes back empty, one more frame for nothing.
	if (req.m_backfill > 0 && (int)req.m_backfilled < req.m_backfill)
		m_pagedHasMoreBwd = false;
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

	// The row that was at the top is back at the head of the data area — give it
	// the shade it had, or the whole list repaints in the other phase and the
	// refresh reads as a flicker. Nothing captured (cold open) leaves the phase be.
	if (m_stripeTopParity >= 0) {
		const int topDataRow = static_cast<int>(m_topParentChain.GetCount());
		m_stripePhase = (m_stripeTopParity ^ (topDataRow & 1)) & 1;
		m_stripeTopParity = -1;
	}

	bool restoredViaTreeExpand = false;

	// Tree-mode auto-expand to selection: walk the breadcrumb chain
	// top → down, planting each ancestor's children (read with the page,
	// never here) and toggling it open so the saved selection is
	// reachable in the tree on first
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
				if (!match->IsOpen()
				    && static_cast<size_t>(i) < req.m_crumbChildren.size()) {
					// This level's children came back with the page — the read
					// fetched every crumb's children in one go, so the rebuild
					// asks the model for nothing.
					ibDataViewItemArray& ancChildren =
						req.m_crumbChildren[static_cast<size_t>(i)];
					const unsigned int cn = ancChildren.GetCount();
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

	InvalidateCount();
	UpdateDisplay();
	UpdatePagedScrollbar();
	
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
	if (restoreFromSelection) {
		// Selection-cursor mode centres the row itself (below), which is a
		// stronger intent than "where the user was looking" — the offset would
		// fight it. Drop it before either branch runs.
		m_pagedRestoreFocusOffset = -1;
	}
	// ALREADY CENTRED — the rows above the cursor came back with the page
	// (m_backfill), so the row is sitting mid-viewport in this very frame. Pulling
	// a backward portion on top of that would prepend rows a second time and slide
	// the list again, which is the jump this fold was written to remove.
	if (restoreFromSelection && req.m_backfilled > 0) {
		// The cursor sits at crumbCount + backfilled: everything read above it came
		// in front. Put HALF a viewport of that above the fold and the row lands in
		// the middle, with context either side — which is the whole point of
		// selection-cursor mode. Where the ordering ends at the cursor there is
		// nothing below to scroll into, so the scroll clamps and the window fills
		// from above instead: the same single frame, no growing afterwards.
		const int crumbCount = static_cast<int>(m_topParentChain.GetCount());
		const int viewport   = GetCountPerPage();
		const int above      = static_cast<int>(req.m_backfilled);
		const int top        = crumbCount + wxMax(0, above - (viewport > 1 ? viewport / 2 : 0));
		ScrollTo(top, -1);
	}
	else if (restoreFromSelection && m_pagedHasMoreBwd) {
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
		//
		// The rows the READ pulled in above the cursor are already in this buffer
		// (ibPagedFetch::m_backfill), so scrolling to its head puts the cursor row
		// back at the distance from the top it had — in ONE frame. Nothing arrives
		// later to slide it, which is what used to read as the list jumping up and
		// then settling.
		const int crumbCount = static_cast<int>(m_topParentChain.GetCount());
				ScrollTo(crumbCount, -1);
		if (m_pagedHasMoreBwd) {
						PagedFetchBackward(batch);
		}
		else {
			// Nothing above the first fetched row, so there is nowhere to put the
			// saved screen offset — the focus IS the top of the ordering. Dropped
			// here rather than left standing, or the next ordinary backward fetch
			// would consume a sort's answer and scroll for no reason.
			m_pagedRestoreFocusOffset = -1;
		}
	}
	else {
		// NOTHING TO POSITION ON — no saved top, no selection to centre. The
		// buffer was just rebuilt starting at the FIRST row of the ordering, so
		// the viewport belongs at the top of it.
		//
		// Leaving the scroll alone is not "keep the user where they were":
		// wxScroll's scroll-Y is a PIXEL and it survives DestroyTree, while the
		// row that pixel used to name does not. A sort with nothing focused
		// arrives here — the anchor is dropped on purpose (its cursor key means
		// nothing in the new ordering) and there is no focus to fall back to — and
		// the old pixel then points into the middle of a list the user has just
		// re-ordered, or past its end. That is the jump. Cold open reaches here
		// too and is already at zero, so this costs it nothing.
		const int crumbCount = static_cast<int>(m_topParentChain.GetCount());
		ScrollTo(crumbCount, -1);
	}

	// THE BLIND STRETCH ENDS HERE. The tree stands, the focus is placed, the
	// viewport is positioned — the frame is finished, so the rows area goes back on
	// air. The freeze taken at the wipe drops to the ScopedPagedFreeze above, whose
	// scope exit then releases ONE composite paint of the whole thing.
	//
	// Deliberately not held any longer than this. The backward top-up dispatched
	// just above lands later and moves the viewport; hiding that under a freeze
	// would also hide a focus landing on the wrong row, which is precisely how such
	// defects stayed invisible while the freeze spanned the settle.
	EndBootstrapFreeze();
	}

// THE READ, and nothing else. A free function rather than a method so it cannot
// reach the control even by accident: everything it needs is in the parcel, and
// every ibDataViewItem it produces is fresh — no node anybody else holds has its
// (non-atomic) refcount touched here.
static void ReadPagedFetchReset(ibDataViewModel* model, ibPagedFetch& req)
{
	// Tree-mode pre-walk: the savedFocus ancestor chain, computed BEFORE the page
	// so (a) the fetch can use chain[last] (the topmost ancestor folder) as its
	// cursor and guarantee the chain head lands in the first batch even when there
	// are more top-level folders than fit, and (b) the walker in apply reuses the
	// array without a second BuildAncestorBreadcrumb roundtrip.
	if (req.m_isTreeMode && req.m_savedFocus.IsOk())
		model->BuildAncestorBreadcrumb(req.m_savedFocus, req.m_treeCrumbs);

	// Hierarchical-mode auto-drill on selection-restore: the target row (a selector
	// form's pre-set value) sits in a subfolder, so a top-level fetch would return
	// its siblings and the row would never be in the batch. Same primitive as Tree
	// above; the difference is that the Hierarchical view consumes the chain as
	// drill state (m_topParentChain, applied on the UI thread) instead of expanding
	// in-tree — and that the page itself must be asked for UNDER the deepest crumb.
	if (req.m_hierAutoDrill) {
		model->BuildAncestorBreadcrumb(req.m_savedFocus, req.m_hierCrumbs);
		if (!req.m_hierCrumbs.IsEmpty())
			req.m_parent = req.m_hierCrumbs[0];
	}

	const ibDataViewItem treeChainHead = req.m_treeCrumbs.IsEmpty()
		? ibDataViewItem()
		: req.m_treeCrumbs[req.m_treeCrumbs.GetCount() - 1];

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
	//   - Otherwise → cursor=anchor (saved top) so plain Refresh keeps
	//     the viewport at the same row.  For a SORT the anchor was
	//     dropped on purpose (its key means nothing in the new
	//     ordering), so this falls back to focus-as-cursor: a row that
	//     re-sorts far down (position 5 → 90) would otherwise drop out
	//     of the batch and the IsEqualTo scan would lose the selection.
	req.m_cursor = req.m_restoreFromSelection
		? (req.m_isTreeMode
			? (treeChainHead.IsOk() ? treeChainHead : req.m_restoreSelection)
			: req.m_restoreSelection)
		: (req.m_restoreAnchor.IsOk()
			? req.m_restoreAnchor
			: req.m_savedFocus);

	req.m_n = model->GetFirstFetch(req.m_parent, req.m_cursor, req.m_batch, req.m_items);

	// Empty-fetch fallback: a cursor-mode fetch (selection / anchor / focus) can
	// return 0 rows when the cursor points at a row that no longer matches the
	// active filter — typically after adding an element whose key fields fall
	// outside it — which would otherwise leave the table blank. Retry from the top.
	// The cursor is CLEARED with it: the page now starts at the first row of the
	// ordering, so there is nothing before it to prefetch and nothing to scroll to.
	if (req.m_n == 0 && req.m_cursor.IsOk()) {
		req.m_items.Clear();
		req.m_cursor = ibDataViewItem();
		req.m_n = model->GetFirstFetch(req.m_parent, req.m_cursor, req.m_batch, req.m_items);
	}

	// THE ROWS ABOVE, in the same pass — see ibPagedFetch::m_backfill. The page
	// starts AT the row the user was on, so on its own it would put that row at the
	// very top of the viewport and a later backward portion would slide it down:
	// two frames, and the second one reads as the list jumping. Read them here and
	// glue them in FRONT, and the control gets one portion and paints once.
	if (req.m_backfill > 0 && req.m_n > 0) {
		ibDataViewItemArray before;
		const unsigned int got = model->GetPrevFetch(req.m_parent, req.m_items[0],
		                                             req.m_backfill, before);
		if (got > 0) {
			ibDataViewItemArray glued;
			for (unsigned int i = 0; i < got; ++i)      glued.Add(before[i]);
			for (unsigned int i = 0; i < req.m_n; ++i)  glued.Add(req.m_items[i]);
			req.m_items      = glued;
			req.m_n          = req.m_n + got;
			req.m_backfilled = got;
		}
	}

	// Every tree level the auto-expand walk will need, fetched HERE. The walk used
	// to ask per level while inserting nodes — on the UI thread, in the middle of
	// the rebuild. The tree is wiped before apply runs, so no crumb is open and
	// every level is genuinely needed.
	if (req.m_isTreeMode && !req.m_treeCrumbs.IsEmpty()) {
		req.m_crumbChildren.resize(req.m_treeCrumbs.GetCount());
		for (size_t i = 0; i < req.m_treeCrumbs.GetCount(); ++i) {
			model->GetFirstFetch(req.m_treeCrumbs[i], ibDataViewItem(),
			                     req.m_batch, req.m_crumbChildren[i]);
		}
	}
}

// THE ONE POINT OF DEPARTURE — one request with the direction in it (Reset = the
// first page and every reload after a sort or a filter; Forward / Backward = what a
// scroll asks for). Decide here, hand the work to the model's own door, take the
// answer back here. The direction matters again only at the far end.
void ibDataViewCtrl::DispatchPagedFetch(ibFetchDirection dir, int batch)
{
	ibDataViewModel* model = GetModel();
	if (model == nullptr) {
		// NO MODEL, NO FIRST FETCH — so disarm, or the flag stands for the life of
		// the window naming a read that can never be dispatched. That is not
		// hypothetical: the FORM EDITOR deliberately associates no model (a preview
		// must not issue SQL against a metadata table still being designed), while
		// the control's own refresh path arms the flag regardless. Left standing it
		// reads as "waiting" — and the busy arc it raised had nothing that could
		// ever take it off.
		m_pagedNeedsBootstrap = false;
		return;
	}

	const bool isReset   = (dir == ibFetchDirection::Reset);
	const bool isForward = (dir == ibFetchDirection::Forward);

	// Every item the read needs travels in the parcel, released on this thread:
	// ibDataViewItem's refcount is NOT atomic. It also carries where the answer goes
	// — the control plus an alive token (a wxWeakRef would write into the control as
	// it dies, which races a copy held off-thread).
	auto req = std::make_shared<ibPagedFetch>();
	req->m_dir   = dir;
	req->m_self  = this;
	req->m_alive = m_aliveToken;
	req->m_gen   = m_pagedFetchGen;

	if (isReset) {
		// Initial / refresh batch == the visible viewport PLUS one slack buffer, fetched in ONE go — so the buffer
		// has room to scroll into (a viewport-exact batch leaves nothing below the fold: the wheel can't scroll, so
		// the scroll-driven forward fetch never fires and rows "don't load until resize"). The extra beyond the
		// buffer still comes in via scroll-driven PagedFetchForward.
		const int viewport = GetCountPerPage();
		req->m_batch = (batch > 0)
			? batch
			: (viewport > 0 ? viewport : 100) + static_cast<int>(kBufferSlack);

		req->m_restoreFromSelection = m_pagedRestoreSelection.IsOk();
		req->m_isTreeMode           = (m_viewMode == ibDataViewViewMode::ibDataViewTree);
		req->m_restoreSelection     = m_pagedRestoreSelection;
		req->m_restoreAnchor        = m_pagedRestoreAnchor;
		// Selection-cursor mode (post-Add to empty folder, post-SetViewMode):
		// Select(item) stamps m_pagedRestoreSelection but not m_pagedRestoreFocus, so
		// the focus-restore would skip (savedFocus empty) even though the new row
		// arrived in the fetch. Fall back to the selection so it becomes current.
		req->m_savedFocus = m_pagedRestoreFocus.IsOk()
			? m_pagedRestoreFocus
			: m_pagedRestoreSelection;
		req->m_hierAutoDrill =
			(m_viewMode == ibDataViewViewMode::ibDataViewHierarchical)
			&& req->m_restoreFromSelection
			&& m_topParentChain.IsEmpty()
			&& req->m_savedFocus.IsOk();

		// The rows above the cursor come back WITH the page — see m_backfill. The
		// saved screen offset is spent here, in one read, instead of by a second
		// portion that would arrive later and slide the list under the user's eye.
		req->m_backfill = (m_pagedRestoreFocusOffset > 0) ? m_pagedRestoreFocusOffset : 0;
		m_pagedRestoreFocusOffset = -1;

		// SELECTION-CURSOR MODE WANTS THE ROW IN THE MIDDLE — a record was just
		// written, and the point of centring it is the context above and below.
		// That context is rows BEFORE the cursor, so it is the same backfill.
		//
		// A WHOLE VIEWPORT of it, not half. Half is what centring needs when the
		// ordering continues below the row; when it does not — a new record sorted
		// LAST, which is the ordinary case after a save — the page below the cursor
		// is one row, and half a screen of backfill leaves the other half blank.
		// Reading a viewport's worth costs one query either way and fills the
		// window in both cases; the scroll below decides where the row sits.
		//
		// Without this the centring took a second portion, and what the user saw on
		// every save was a short list appearing and then growing under the cursor a
		// moment later — the rows sliding, the stripes re-shading and the scrollbar
		// jumping as the buffer changed height.
		if (req->m_backfill == 0 && req->m_restoreFromSelection && viewport > 1)
			req->m_backfill = viewport;

		// ⚠⚠ ANY PAGE THAT STARTS AT A CURSOR MUST READ WHAT IS ABOVE IT. Not "an anchored refresh",
		// not "a selection restore" — ANY of them, because nothing else ever will.
		//
		// The page begins at the cursor, so the rows before it are simply not in the buffer, and the
		// only retry below fires when the fetch returns NOTHING — this one returns a full screen.
		// The list then quietly lacks its beginning, refreshing does not help (keeping the position
		// is the whole point of the cursor), and only something that rebuilds the model — a
		// restructuring, or restarting — brings the rows back.
		//
		// ⚠ AND THE CURSOR IS NOT ALWAYS THE ANCHOR. It is selection, else anchor, else SAVED FOCUS
		// (see where m_cursor is computed). Guarding on the anchor alone left the commonest case
		// open — switching the view mode, where there is no anchor and the focus becomes the cursor:
		// a list of ten showed seven, missing exactly the three above the focused row. The rule has
		// to name the CONDITION (a page starting somewhere other than the top) rather than one of
		// the reasons for it.
		//
		// One backward page, once, per such fetch.
		if (req->m_backfill == 0 && viewport > 1
		    && (req->m_restoreAnchor.IsOk() || req->m_savedFocus.IsOk() || req->m_restoreSelection.IsOk()))
			req->m_backfill = viewport;

		// ARMED, SO DISARM NOW. The flag goes down at dispatch, not when the answer
		// lands: idle fires every pass and would otherwise start a second reset over
		// the first. What discards an OVERTAKEN answer is the generation stamp.
		m_pagedNeedsBootstrap = false;

		// BOTH counters: a reset replaces the whole buffer, so neither end of it may
		// be extended while the replacement is out — a backward portion dispatched
		// from a scroll meanwhile would come back AFTER the wipe and prepend rows
		// from an ordering that no longer exists.
		++m_pagedFetchingFwd;
		++m_pagedFetchingBwd;
	}
	else {
		const ibDataViewItem& anchorRef = isForward ? m_pagedFwdAnchor : m_pagedBwdAnchor;
		int& fetchingCounter = isForward ? m_pagedFetchingFwd : m_pagedFetchingBwd;
		if (m_root == nullptr || !anchorRef.IsOk())
			return;
		if (fetchingCounter > 0) {
			// One is already out, and this one would be anchored on the same row — the
			// same portion twice. Remember the question instead; the delivery re-asks it
			// once the anchor has moved. See m_pagedFetchAgainFwd.
			(isForward ? m_pagedFetchAgainFwd : m_pagedFetchAgainBwd) = true;
			return;
		}
		++fetchingCounter;

		req->m_batch  = batch;
		req->m_anchor = anchorRef;
	}

	// Drill chain front in Hierarchical mode; empty (= top-level) in List, Tree and
	// non-hierarchical models. A List view passes an empty parent because the
	// composer only scopes by parent when it groups, which a List view never does.
	req->m_parent = GetEffectiveFetchParent();

	auto readWork = [model, req]() mutable {
		// Off this control's thread: the model calls, and nothing else.
		//
		// A PORTION THAT FAILED IS STILL A PORTION THAT ENDED. The read may throw —
		// a dropped connection, a query the source cannot answer — and the form must
		// not go down with it: it keeps the rows it already has, and the reason goes
		// to the journal where it can be read afterwards. But the answer has to come
		// back either way, because the counter that says "a fetch is out" is lowered
		// on delivery; swallowing the throw here would leave that counter raised and
		// the list would never ask for anything again.
		try {
			switch (req->m_dir) {
			case ibFetchDirection::Forward:
				req->m_n = model->GetNextFetch(req->m_parent, req->m_anchor, req->m_batch, req->m_items);
				break;
			case ibFetchDirection::Backward:
				req->m_n = model->GetPrevFetch(req->m_parent, req->m_anchor, req->m_batch, req->m_items);
				break;
			default:
				ReadPagedFetchReset(model, *req);   // Reset — the whole first page
				break;
			}
		}
		catch (const ibBackendException& err) {
			req->m_ok = false;
			if (ibLogger* const log = ibApplicationData::GetLogger())
				log->Error(wxT("list"), wxT("fetch"), err.GetErrorDescription());
		}
		catch (...) {
			req->m_ok = false;
			if (ibLogger* const log = ibApplicationData::GetLogger())
				log->Error(wxT("list"), wxT("fetch"), _("unknown exception"));
		}

		// Marshal back to the UI thread. Through wxTheApp rather than the
		// control: posting is safe from any thread, but READING the control to
		// ask it to post is not — the object may already be gone. The app is a
		// process singleton and outlives every form.
		if (wxTheApp == nullptr)
			return;
		wxTheApp->CallAfter([req]() {
			if (!*req->m_alive) return;   // form closed while the read was out
			ibDataViewCtrl* const s = req->m_self;
			const bool isReset   = (req->m_dir == ibFetchDirection::Reset);
			const bool isForward = (req->m_dir == ibFetchDirection::Forward);

			// THE VEIL COMES OFF HERE — the moment the portion is in and the rows
			// are on screen, not on the next idle pass or timer tick, which is a
			// frame or two later and reads as the wheel lingering over data that is
			// already there. On the way out of EVERY exit below, including the
			// dropped and failed ones, because "the read ended" is what it reports.
			// If the delivery re-asks (a scroll burst), the new portion raises the
			// counter again before this runs and the wheel simply stays up.
			struct ibBusyOff {
				ibDataViewCtrl* m_ctrl;
				~ibBusyOff() { m_ctrl->UpdateBusyIndicator(); }
			} busyOff{ s };
			// NEVER BELOW ZERO. AssociateModel zeroes these outright, so a portion
			// still in flight at that moment would drive them negative on arrival —
			// and a negative counter reads as "nothing in flight": no busy badge ever
			// again, and the de-duplication gate stops holding.
			const auto lower = [](int& c) { if (c > 0) --c; };
			if (isReset)        { lower(s->m_pagedFetchingFwd); lower(s->m_pagedFetchingBwd); }
			else if (isForward) { lower(s->m_pagedFetchingFwd); }
			else                { lower(s->m_pagedFetchingBwd); }

			// A BACKWARD PORTION THAT NEVER LANDS is also the one that would have
			// spent the sort's saved screen offset. Drop it on both exits below, or
			// it waits — and the next ordinary scroll-up consumes a sort's answer
			// and jumps the viewport for no reason.
			//
			// The generation check discards the result if the buffer has been wiped
			// (sort / filter / refresh) since the dispatch — the answer is to a
			// question nobody is asking any more.
			//
			// A RESET ANSWERS FOR BOTH DIRECTIONS, so it clears both re-asks: a
			// scroll that latched one while the whole buffer was being replaced is
			// asking about an ordering that no longer exists.
			if (isReset)
				s->m_pagedFetchAgainFwd = s->m_pagedFetchAgainBwd = false;
			bool& again = isForward ? s->m_pagedFetchAgainFwd : s->m_pagedFetchAgainBwd;
			if (req->m_gen != s->m_pagedFetchGen) {
				if (!isForward) s->m_pagedRestoreFocusOffset = -1;
				again = false;   // asked under an ordering that no longer exists
				return;
			}
			// A failed read is NOT an empty one: reporting zero rows here would
			// flip the exhaustion flag and the list would decide there is nothing
			// further to load. Leaving the flags alone means the next scroll asks
			// again, which is what a transient failure deserves. A failed RESET
			// leaves the list exactly as it was — nothing was wiped, which is the
			// whole point of wiping only on arrival.
			if (!req->m_ok) {
				if (!isForward) s->m_pagedRestoreFocusOffset = -1;
				again = false;
				return;
			}

			// THE THREE ANSWERS — the only place the direction still matters: fill
			// from scratch, append at the tail, prepend at the head.
			if (isReset)        { s->OnPagedFetchResetComplete(*req); return; }
			else if (isForward) s->OnPagedFetchForwardComplete(req->m_items, req->m_n, req->m_batch);
			else                s->OnPagedFetchBackwardComplete(req->m_items, req->m_n, req->m_batch);

			// ASKED AGAIN WHILE THIS ONE WAS OUT — now the anchor has moved, so the
			// question means a different portion. Re-ask it: a burst of three scrolls
			// lands three portions in turn. The exhaustion flag the result just set is
			// what ends the chain, so it cannot run away.
			if (again) {
				again = false;
				const bool more = isForward ? s->m_pagedHasMoreFwd : s->m_pagedHasMoreBwd;
				if (more)
					s->DispatchPagedFetch(req->m_dir, req->m_batch);
			}
		});

		// AND LET GO HERE, on this side of the hop. The delivery lambda now holds
		// the parcel, so this drops a reference the UI thread does not need us to
		// drop later — and "later" is the danger: the parcel pins refcounted row
		// nodes with a NON-atomic counter, so if this copy outlived the delivery
		// the last release would land on a worker while the tree owns the nodes.
		req.reset();
	};

	// ONE CODE POINT, EVERY KIND OF TABLE — the same door the bootstrap uses. The
	// MODEL answers where its read runs: a runtime model rents a background session
	// (its own connection, the caller's policy borrowed, used up and gone), a plain
	// in-memory one runs it here. The control never asks which kind it is holding.
	model->SubmitFetchAsync(readWork);
}

// (There is no per-fetch handle to hold or cancel. A rented run ends by
//  construction, and what the control still owns is the alive token — which is
//  what makes a delivery already posted to the UI queue a no-op when it lands on a
//  control that is gone.)

void ibDataViewCtrl::OnPagedFetchForwardComplete(ibDataViewItemArray& items,
	unsigned int n, int batch)
{
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
			// Dropping rows off the FRONT shifts every remaining index — move the
			// stripe phase with them so nothing changes shade (see m_stripePhase).
			if (actualTrim & 1) m_stripePhase ^= 1;
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
		if (restoreTopRow >= 0) {
				ScrollTo(static_cast<int>(restoreTopRow), -1);
	}
	UpdatePagedScrollbar();
	}  // ScopedPagedFreeze auto-Thaws here

void ibDataViewCtrl::OnPagedFetchBackwardComplete(ibDataViewItemArray& items,
	unsigned int n, int batch)
{
		if (n == 0) {
		m_pagedHasMoreBwd = false;
		// Nothing came back, so nothing can be scrolled into: the saved screen
		// offset has no home and must not wait for an unrelated fetch to use it.
		m_pagedRestoreFocusOffset = -1;
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
	ibDataViewTreeNode* const insertParent = GetPagedInsertParent();
	if (insertParent == nullptr) return;  // freeze auto-Thaws via dtor
	const long crumbCount = static_cast<long>(m_topParentChain.GetCount());
	for (unsigned int i = 0; i < n; ++i) {
		insertParent->InsertChild(this, MakeChildNode(insertParent, items[i]), i);
	}
	insertParent->ChangeSubTreeCount(static_cast<int>(n));   // propagates up
	// Every row below just moved down by n — keep their stripes by moving the phase
	// with them (see m_stripePhase).
	if (n & 1) m_stripePhase ^= 1;
	// Selection/currentRow are flat-tree indices.  Inserts happened at
	// flat range [crumbCount, crumbCount + n) — rows below shift by +n.
	if (!m_selection.IsEmpty()) {
				m_selection.OnItemsInserted(static_cast<unsigned>(crumbCount), n);
			}
	if (m_currentRow != (unsigned)-1
	    && static_cast<long>(m_currentRow) >= crumbCount) {
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
	long restoreTopRow = topRowBeforeAdj + static_cast<long>(n);

	// THE SORT'S ONE ANSWER, and this is the first moment it can be given. The
	// bootstrap after a sort fetches FROM the focused row, so the focus starts at
	// the top of the buffer with nothing above it; only now, with a batch
	// prepended, is there room to put it back where it sat on screen. Drive it off
	// m_currentRow (already shifted by the prepend above) rather than off the saved
	// index — if the focus-scan missed, m_currentRow is -1 and the offset is simply
	// dropped instead of scrolling to a row nobody found.
	if (m_pagedRestoreFocusOffset >= 0) {
		if (m_currentRow != (unsigned)-1) {
			restoreTopRow = std::max<long>(crumbCount,
				static_cast<long>(m_currentRow) - m_pagedRestoreFocusOffset);
		}
		m_pagedRestoreFocusOffset = -1;   // one-shot
	}

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
		}
	}

	
	InvalidateCount();
	RecalculateDisplay();
		ScrollTo(static_cast<int>(restoreTopRow), -1);
	UpdatePagedScrollbar();
	}  // ScopedPagedFreeze auto-Thaws here

#endif // wxUSE_DATAVIEWCTRL
