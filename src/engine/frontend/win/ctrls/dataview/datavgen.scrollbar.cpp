/////////////////////////////////////////////////////////////////////////////
// Name:        datavgen.scrollbar.cpp
// Purpose:     3-state lying scrollbar for paged ibDataViewCtrl.  Extracted
//              from datavgen.cpp on 2026-05-08.  Pure translation-unit
//              split — methods stay members of ibDataViewCtrl, declarations
//              live in datavgen.h.
// Methods:
//   IsPagedScrollbarMode    — gate (paged + vertical scroll only)
//   DerivePagedThumb        — top/middle/bottom from has-more flags +
//                             scroll position
//   UpdatePagedScrollbar    — refresh widget through SetScrollbar override
//   AdjustScrollbars        — wxScrollHelper hook + lying re-apply
//   SetScrollPos            — wxScrollHelper override + lying re-apply
//   SetScrollbar            — the actual lie: substitute 3-state values
//                             before passing to wxWindow
//
//   OnScrollEvent stays in datavgen.cpp because it's wired to
//   EVT_SCROLLWIN in the event table.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wxprec.h>

#if wxUSE_DATAVIEWCTRL

#include "dataview.h"
#include "backend/tableInfo.h"
#include "datavgen.paged.private.h"
#ifndef WX_PRECOMP
#include <wx/log.h>
#endif

bool ibDataViewCtrl::IsPagedScrollbarMode() const
{
	const ibDataViewModel* model = GetModel();
	return model != nullptr && model->IsPagedModel();
}

// Derived 3-state thumb position.  No state machine — just look at
// the live has-more flags + viewport position inside the loaded
// buffer.  Top  = nothing data-wise behind viewport  (start of dataset).
// Bottom       = nothing data-wise ahead of viewport (end of dataset).
// Middle       = data on both sides.  Pure function of current state,
// safe to call from any thumb-update path.
ibDataViewCtrl::ibPagedThumb ibDataViewCtrl::DerivePagedThumb() const
{
	// Full rendered row count (includes nested crumbs in Hierarchical
	// mode).  m_root->GetChildNodes() would count only the topmost
	// crumb when drilled into a folder, missing the actual data rows
	// underneath, so the thumb would stay locked at Top forever.
	const long bufferSize = static_cast<long>(GetRowCount());
	const long crumbCount = static_cast<long>(m_topParentChain.GetCount());
	const int  viewport   = GetCountPerPage();

	// Degenerate case: full dataset (no more to fetch) fits the
	// scroll area in one shot.  Treat it as Top — the user has the
	// whole dataset in view; the scrollbar itself is hidden in this
	// case (see SetScrollbar override) so this is just a guard.
	// Pixel comparison mirrors SetScrollbar's hide condition: row-
	// count vs cpp lies when GetCountPerPage rounds up over the
	// header/footer space.
	const int rowsHeightT = static_cast<int>((bufferSize - crumbCount) * m_lineHeight);
	const int areaHeightT = m_tableAreaWin
		? m_tableAreaWin->GetClientSize().y
		: (viewport * m_lineHeight);
	if (!m_pagedHasMoreFwd && !m_pagedHasMoreBwd
	    && rowsHeightT <= areaHeightT)
		return ibPagedThumb::Top;

	// Scroll-position-based decision.  Comparing topRow + viewport vs
	// bufferSize gets confused around the buffer's last visible row
	// (overshoot region in m_tableAreaWin's virtual layout makes
	// `topRow + viewport <= bufferSize` flicker between true/false
	// over multiple scroll units, requiring 2-3 scroll-up actions
	// before the thumb leaves Bottom).  Instead, query the actual
	// scroll Y vs max scroll Y on m_tableAreaWin — true at the very
	// edge, false anywhere else, regardless of how the row<->pixel
	// math lines up at the boundary.
	int sx = 0, sy = 0;
	CalcUnscrolledPosition(0, 0, &sx, &sy);
	const wxSize virt   = m_tableAreaWin ? m_tableAreaWin->GetVirtualSize()
	                                     : wxSize(0, 0);
	const wxSize client = m_tableAreaWin ? m_tableAreaWin->GetClientSize()
	                                     : wxSize(0, 0);
	const int maxScrollY = std::max(0, virt.y - client.y);
	const bool atScrollTop    = (sy <= 0);
	const bool atScrollBottom = (sy >= maxScrollY);

	const bool dataAhead  = m_pagedHasMoreFwd || !atScrollBottom;
	const bool dataBehind = m_pagedHasMoreBwd || !atScrollTop;

	ibPagedThumb result;
	if (!dataAhead && dataBehind) result = ibPagedThumb::Bottom;
	else if (!dataBehind && dataAhead) result = ibPagedThumb::Top;
	else if (dataAhead && dataBehind) result = ibPagedThumb::Middle;
	else result = ibPagedThumb::Top;            // both false: empty / fits in buffer

	const char* label =
		(result == ibPagedThumb::Top)    ? "Top" :
		(result == ibPagedThumb::Bottom) ? "Bottom" : "Middle";
		return result;
}

// Lowest / highest pinned row indices — the focused row + any rows in
// the selection store.  Trim caps to these so no pinned row is evicted
// from the loaded buffer.  Returns -1 in either field when no pin
// exists on that side.  O(n) over loaded rows; the buffer is small
// (~30-50) so this is cheap.
//
// Sanity guard: wxSelectionStore reports IsSelected(i) == true for ALL
// rows when its internal m_defaultState flag is set (e.g. transient
// state during SelectAll / item-count grow).  Skip the iteration in
// that case — pinning every row would freeze the trim entirely.
void ibDataViewCtrl::GetPagedPinRange(long& minRow, long& maxRow) const
{
	minRow = -1;
	maxRow = -1;
	if (m_currentRow != (unsigned)-1) {
		minRow = static_cast<long>(m_currentRow);
		maxRow = static_cast<long>(m_currentRow);
	}
	if (m_root == nullptr || m_selection.IsEmpty()) return;
	const long count = static_cast<long>(m_root->GetChildNodes().size());
	if (count == 0) return;
	// Bail if the store reports both ends selected: that's the
	// "default-on" state, not a real user selection.
	if (m_selection.IsSelected(0) &&
	    m_selection.IsSelected(static_cast<unsigned>(count - 1))) {
		return;
	}
	for (long i = 0; i < count; ++i) {
		if (!m_selection.IsSelected(static_cast<unsigned>(i))) continue;
		if (minRow < 0 || i < minRow) minRow = i;
		if (maxRow < 0 || i > maxRow) maxRow = i;
	}
}

// GetPagedInsertParent / SchedulePagedRefresh / PagedRefresh /
// PagedBootstrap / DispatchPagedFetch / OnPagedFetch{Forward,Backward}Result
// — extracted to datavgen.paged.cpp on 2026-05-08.

void ibDataViewCtrl::UpdatePagedScrollbar()
{
	// SetScrollbar override substitutes the lying 3-state values for
	// vertical paged regardless of the parameters passed; just trigger
	// a refresh through the override so the widget picks up the latest
	// topRow / has-more state.
	if (!IsPagedScrollbarMode()) return;
	SetScrollbar(wxVERTICAL, 0, 0, 0, true);
}

void ibDataViewCtrl::AdjustScrollbars()
{
	wxScrollHelper::AdjustScrollbars();
	// Re-apply the lie immediately after wxScrollHelper has pushed the
	// real virtual-size-derived values to the widget.
	UpdatePagedScrollbar();
}

void ibDataViewCtrl::SetScrollPos(int orient, int pos, bool refresh)
{
	// In paged scrollbar mode the vertical thumb position is owned by
	// UpdatePagedScrollbar — block wxScrollHelper from clobbering it on
	// scroll events / wheel / programmatic Scroll calls.
	if (orient == wxVERTICAL && IsPagedScrollbarMode())
		return;
	wxCompositeWindow<ibDataViewCtrlBase>::SetScrollPos(orient, pos, refresh);
}

void ibDataViewCtrl::SetScrollbar(int orient, int pos, int thumbSize, int range,
	bool refresh)
{
	// Any wxScrollHelper / wxWindow path that pushes vertical scrollbar
	// state in paged mode is intercepted here and the parameters are
	// substituted with the 3-state lying values derived from current
	// has-more + viewport state.  Recursion-safe: substituted call goes
	// to the base class.
	if (orient == wxVERTICAL && IsPagedScrollbarMode()) {
		const int viewport = GetCountPerPage();
		if (viewport > 0) {
			// "Fits in buffer" — no fwd/bwd to fetch AND the loaded
			// data rows fit ENTIRELY within the rows area in pixels.
			// Hide the scrollbar (range=0) only then.
			//
			// Pixel comparison, not row count: GetCountPerPage uses
			// the OUTER ctrl's client size (header + rows + footer),
			// so a 19-row register against cpp=20 reports "fits" while
			// the rows area itself is shorter than 19 row-heights —
			// wheel-scroll still moves the contents because virtual
			// size > actual rows-area client size, but the lying
			// scrollbar is hidden ("there are more items, no scrollbar,
			// yet it still scrolls").
			//
			// Subtract crumbCount: in Hierarchical drill mode crumbs
			// are FROZEN at top in a separate window and don't take
			// rows-area space.
			const long renderedRows = static_cast<long>(GetRowCount());
			const long crumbs       = static_cast<long>(m_topParentChain.GetCount());
			const int rowsHeight    = static_cast<int>((renderedRows - crumbs) * m_lineHeight);
			const int areaHeight    = m_tableAreaWin
				? m_tableAreaWin->GetClientSize().y
				: (viewport * m_lineHeight);
			if (!m_pagedHasMoreFwd && !m_pagedHasMoreBwd
			    && rowsHeight <= areaHeight) {
				wxCompositeWindow<ibDataViewCtrlBase>::SetScrollbar(
					orient, 0, 0, 0, refresh);
				return;
			}
			int fakePos, fakeRange;
			switch (DerivePagedThumb()) {
			case ibPagedThumb::Top:
				fakeRange = 2 * viewport;
				fakePos   = 0;
				break;
			case ibPagedThumb::Bottom:
				fakeRange = 2 * viewport;
				fakePos   = viewport;
				break;
			case ibPagedThumb::Middle:
			default:
				fakeRange = 3 * viewport;
				fakePos   = viewport;
				break;
			}
			wxCompositeWindow<ibDataViewCtrlBase>::SetScrollbar(
				orient, fakePos, viewport, fakeRange, refresh);
			return;
		}
	}
	wxCompositeWindow<ibDataViewCtrlBase>::SetScrollbar(orient, pos, thumbSize,
		range, refresh);
}


#endif // wxUSE_DATAVIEWCTRL
