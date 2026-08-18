/////////////////////////////////////////////////////////////////////////////
// Name:        datavgen.paged.private.h
// Purpose:     What a PORTION is made of, shared by the datavgen translation
//              units: the buffer-slack constant, the RAII freeze guard, and the
//              parcel one fetch travels in.
//
//              The inner table window that used to live here moved to
//              datavgen.window.private.h — a window is not part of a portion.
//
//              Private to those .cpp files. Do not include from a public header.
/////////////////////////////////////////////////////////////////////////////

#ifndef OES_DATAVGEN_PAGED_PRIVATE_H
#define OES_DATAVGEN_PAGED_PRIVATE_H

#include "dataview.h"

#include <memory>
#include <vector>

// Slack rows kept on each side of the viewport.  Buffer target =
// viewport + 2*kBufferSlack, so a +/-kBufferSlack scroll within the
// loaded window does not trigger any fetch.  Once the visible margin
// to either edge drops below this, fetch the next batch and trim the
// far side back to target.
static constexpr long kBufferSlack = 5;

// WHAT ONE BOOTSTRAP IS — a first page, carried between the three steps it takes.
//
// The bootstrap is the read with nothing to fall back on: the first page of a list,
// and every reload after a sort or a filter change. It is therefore the one that was
// FELT — the whole rebuild ran on the UI thread, so the window stopped answering for
// the length of a query. The three steps are split by WHO MAY TOUCH WHAT:
//
//   decide (UI thread)  — what to ask for: batch, cursor, view mode, and the items
//                         the restore state names. Reads the control, calls no model.
//   read   (worker)     — the model calls, and nothing else. It is a free function
//                         precisely so it cannot reach the control at all.
//   apply  (UI thread)  — wipe, build, focus, scroll. Touches the tree; asks the
//                         model for nothing, because every answer is already here.
//
// The items live in this parcel rather than in a capture list, for the reason spelled
// out at DispatchPagedFetch: an ibDataViewItem pins its node through a NON-atomic
// refcount, so a node must never be released on a worker while the UI thread holds
// nodes of the same tree. Both hops hold the parcel; the UI hop outlives the worker's
// copy, so it is destroyed here, always.
struct ibPagedFetch {
	// --- what is being asked, and where the answer goes (every direction) ---
	ibFetchDirection m_dir = ibFetchDirection::Reset;
	ibDataViewCtrl*  m_self = nullptr;        // where the answer goes
	std::shared_ptr<bool> m_alive;            // …if it is still there
	uint64_t         m_gen = 0;               // …and still the question being asked
	bool             m_ok  = true;            // the read came back with an answer
	// WHY the read has no answer, carried back so the delivery can SAY it. The journal line is
	// written where the throw is caught; this is the copy a person gets to see, and without it
	// a first page that never ran is indistinguishable on screen from a source with no rows.
	wxString         m_error;
	ibDataViewItem   m_anchor;                // Forward / Backward: the row to page from

	// RESET ONLY — how many rows to pull in BEFORE the cursor, so the row the user
	// was on comes back at the same distance from the top of the viewport it had.
	// The cursor page starts AT that row, which would put it at the very top; the
	// rows above are what puts it back where the eye expects it. Read in the same
	// pass and glued in front of the page, so the control receives ONE portion and
	// paints ONE frame — a second, later portion is a visible jump.
	int              m_backfill   = 0;
	unsigned int     m_backfilled = 0;        // how many actually came back

	// --- decided on the UI thread ---
	int  m_batch                = 0;
	bool m_restoreFromSelection = false;
	bool m_isTreeMode           = false;
	// Hierarchical mode with a selection to restore and no drill chain yet: the row
	// sits in a subfolder, so the chain has to be walked BEFORE the page can be asked
	// for — the fetch's parent is the deepest crumb.
	bool m_hierAutoDrill        = false;
	ibDataViewItem m_savedFocus;
	ibDataViewItem m_restoreSelection;
	ibDataViewItem m_restoreAnchor;
	ibDataViewItem m_parent;        // the drill chain's front; empty in List / Tree

	// --- filled by the read ---
	ibDataViewItemArray m_treeCrumbs;   // tree mode: the focus's ancestor chain
	ibDataViewItemArray m_hierCrumbs;   // auto-drill: the same, for the drill chain
	ibDataViewItem      m_cursor;       // what the page was actually positioned by
	ibDataViewItemArray m_items;        // the page
	unsigned int        m_n = 0;
	// Children of each tree crumb, index-aligned with m_treeCrumbs. Read here so the
	// auto-expand walk in apply has every level in hand — it used to fetch per level,
	// on the UI thread, in the middle of rebuilding the tree.
	std::vector<ibDataViewItemArray> m_crumbChildren;
};

namespace {
// RAII pair-Freeze for ibDataViewCtrl + its main table window.  Most
// paged-mode operations need both windows frozen across a buffer
// mutation + scroll restore (otherwise wxScrollHelper's pixel<->row
// mapping flickers between intermediate states).  Auto-Thaws on scope
// exit — early returns are safe.
class ScopedPagedFreeze {
public:
	explicit ScopedPagedFreeze(ibDataViewCtrl* ctrl)
		: m_ctrl(ctrl), m_table(ctrl ? ctrl->GetMainWindow() : nullptr)
	{
		if (m_ctrl)  m_ctrl->Freeze();
		if (m_table) m_table->Freeze();
	}
	~ScopedPagedFreeze() {
		if (m_table) m_table->Thaw();
		if (m_ctrl)  m_ctrl->Thaw();
	}
	ScopedPagedFreeze(const ScopedPagedFreeze&) = delete;
	ScopedPagedFreeze& operator=(const ScopedPagedFreeze&) = delete;
private:
	ibDataViewCtrl* m_ctrl;
	wxWindow*       m_table;
};
} // anonymous namespace

#endif // OES_DATAVGEN_PAGED_PRIVATE_H
