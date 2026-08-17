/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/datavgen.cpp
// Purpose:     ibDataViewCtrl generic implementation
// Author:      Robert Roebling
// Modified by: Francesco Montorsi, Guru Kathiresan, Bo Yang
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#if wxUSE_DATAVIEWCTRL

#include "dataview.h"
#ifndef WX_PRECOMP
#ifdef __WXMSW__
#include <wx/app.h>          // GetRegisteredClassName()
#include <wx/msw/private.h>
#include <wx/msw/wrapwin.h>
#include <wx/msw/wrapcctl.h> // include <commctrl.h> "properly"
#endif
#include <wx/sizer.h>
#include <wx/log.h>
#include <wx/dcclient.h>
#include <wx/timer.h>
#include <wx/settings.h>
#include <wx/msgdlg.h>
#include <wx/dcscreen.h>
#include <wx/frame.h>
#include <wx/vector.h>
#endif

#include <wx/stockitem.h>
#include <wx/popupwin.h>
#include <wx/renderer.h>
#include <wx/graphics.h>   // the busy badge's arc (antialiased, alpha)
#include <wx/dcbuffer.h>
#include <wx/icon.h>
#include <wx/itemattr.h>
#include <wx/list.h>
#include <wx/listimpl.cpp>
#include <wx/imaglist.h>
#include <wx/dnd.h>
#include <wx/stopwatch.h>
#include <wx/weakref.h>

#include <wx/generic/private/markuptext.h>

#include <wx/generic/private/rowheightcache.h>
#include <wx/generic/private/widthcalc.h>

#if wxUSE_ACCESSIBILITY
#include <wx/private/markupparser.h>
#endif // wxUSE_ACCESSIBILITY

//-----------------------------------------------------------------------------
// classes
//-----------------------------------------------------------------------------

class ibDataViewColumn;
class ibDataViewHeaderWindow;
class ibDataViewCtrl;

//-----------------------------------------------------------------------------
// constants
//-----------------------------------------------------------------------------

// the cell padding on the left/right
static const int PADDING_RIGHTLEFT = 3;

namespace
{
	// ----------------------------------------------------------------------------
	// helper functions
	// ----------------------------------------------------------------------------

	// Return the expander column or, if it is not set, the first column and also
	// set it as the expander one for the future.
	ibDataViewColumn* GetExpanderColumnOrFirstOne(ibDataViewCtrl* dataview)
	{
		ibDataViewColumn* expander = dataview->GetExpanderColumn();
		if (!expander)
		{
			// TODO-RTL: last column for RTL support
			expander = dataview->GetColumn(0);
			dataview->SetExpanderColumn(expander);
		}

		return expander;
	}

	wxTextCtrl* CreateEditorTextCtrl(wxWindow* parent, const wxRect& labelRect, const wxString& value)
	{
		wxTextCtrl* ctrl = new wxTextCtrl(parent, wxID_ANY, value,
			labelRect.GetPosition(),
			labelRect.GetSize(),
			wxTE_PROCESS_ENTER);

		// Adjust size of wxTextCtrl editor to fit text, even if it means being
		// wider than the corresponding column (this is how Explorer behaves).
		const int fitting = ctrl->GetSizeFromTextSize(ctrl->GetTextExtent(ctrl->GetValue())).x;
		const int current = ctrl->GetSize().x;
		// ⚠ THE ROOM LEFT IN THE PARENT, not the editor's own width minus its own x. That was the
		// bug: for a column far from the left edge the subtraction went NEGATIVE (a 160-wide cell at
		// x=380 gave -220), the editor was sized to it, and an editor with no width is an editor
		// nobody can see. It looked exactly like "this cell refuses to open" — and only ever for
		// columns after the first, which is why the leftmost cell of every grid always worked.
		const int maxwidth = (parent != nullptr ? parent->GetSize().x : ctrl->GetSize().x)
		                     - ctrl->GetPosition().x;

		// Adjust size so that it fits all content. Don't change anything if the
		// allocated space is already larger than needed and don't extend wxDVC's
		// boundaries.
		int width = wxMin(wxMax(current, fitting), maxwidth);

		if (width != current)
			ctrl->SetSize(wxSize(width, -1));

		// select the text in the control an place the cursor at the end
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();

		return ctrl;
	}

} // anonymous namespace

//-----------------------------------------------------------------------------
// ibDataViewColumn
//-----------------------------------------------------------------------------

void ibDataViewColumn::Init(int width, wxAlignment align, int flags)
{
	m_width =
		m_manuallySetWidth = width;
	m_minWidth = 0;
	m_align = align;
	m_flags = flags;
	m_sort = false;
	m_sortAscending = true;
}

int ibDataViewColumn::DoGetEffectiveWidth(int width) const
{
	switch (width)
	{
	case wxCOL_WIDTH_DEFAULT:
		return wxWindow::FromDIP(wxDVC_DEFAULT_WIDTH, GetOwner());

	case wxCOL_WIDTH_AUTOSIZE:
		wxCHECK_MSG(GetOwner(), wxDVC_DEFAULT_WIDTH, "no owner control");
		return GetOwner()->GetBestColumnWidth(const_cast<ibDataViewColumn*>(this));

	default:
		return width;
	}
}

int ibDataViewColumn::GetWidth() const
{
	return DoGetEffectiveWidth(m_width);
}

void ibDataViewColumn::WXOnResize(int width)
{
	m_width =
		m_manuallySetWidth = width;

	if (ibDataViewCtrl* owner = GetOwner()) {
		owner->InvalidateColBestWidth(this);
		owner->OnColumnResized();
	}
}

int ibDataViewColumn::WXGetSpecifiedWidth() const
{
	// Note that we need to return valid value even if no width was initially
	// specified, as otherwise the last column created without any explicit
	// width could be reduced to nothing by UpdateColumnSizes() when the
	// control is shrunk.
	return DoGetEffectiveWidth(m_manuallySetWidth);
}

void ibDataViewColumn::UpdateDisplay()
{
	if (ibDataViewCtrl* owner = GetOwner())
		owner->OnColumnChange(owner->GetColumnIndex(this));
}

void ibDataViewColumn::UpdateWidth()
{
	if (ibDataViewCtrl* owner = GetOwner())
		owner->OnColumnWidthChange(owner->GetColumnIndex(this));
}

void ibDataViewColumn::UnsetAsSortKey()
{
	m_sort = false;

	if (ibDataViewCtrl* owner = GetOwner())
		owner->DontUseColumnForSorting(owner->GetColumnIndex(this));

	UpdateDisplay();
}

void ibDataViewColumn::SetSortOrder(bool ascending)
{
	ibDataViewCtrl* owner = GetOwner();
	if (!owner)
		return;

	const int idx = owner->GetColumnIndex(this);

	// If this column isn't sorted already, mark it as sorted
	if (!m_sort)
	{
		wxASSERT(!owner->IsColumnSorted(idx));

		// Now set this one as the new sort column.
		owner->UseColumnForSorting(idx);
		m_sort = true;
	}

	m_sortAscending = ascending;

	// Call this directly instead of using UpdateDisplay() as we already have
	// the column index, no need to look it up again.
	owner->OnColumnChange(idx);
}

// ----------------------------------------------------------------------------
// ibDataViewColumn — the group it lives under
// ----------------------------------------------------------------------------

// A POINTER, NOT AN EVENT. Whoever changed the membership announces it — the group does,
// through WXColumnTreeChanged, once BOTH sides of the fact are set.
//
// This used to refresh from here, and that was a notification AHEAD OF THE FACT: a column
// inserted into a group is already the nth column of the tree while the header still
// counts n of them, so OnColumnChange(n) ran off the end of the header (the wxCHECK in
// UpdateColumn — it broke opening any form with a table). The tree-changed notification
// drops the geometry anyway, so nothing is lost by keeping this a plain setter.
void ibDataViewColumn::SetParent(ibDataViewColumnGroup* group)
{
	m_group = group;
}

//-----------------------------------------------------------------------------
// ibDataViewHeaderWindow
//-----------------------------------------------------------------------------

class ibDataViewHeaderWindow : public ibHeaderGenericCtrl
{
public:
	ibDataViewHeaderWindow(ibDataViewCtrl* parent)
		: ibHeaderGenericCtrl(parent, wxID_ANY,
			wxDefaultPosition, wxDefaultSize,
			wxHD_DEFAULT_STYLE | wxHD_BITMAP_ON_RIGHT)
	{
	}

	ibDataViewCtrl* GetOwner() const
	{
		return static_cast<ibDataViewCtrl*>(GetParent());
	}

	virtual wxWindow* GetMainWindowOfCompositeControl() wxOVERRIDE
	{
		return GetOwner();
	}

	// Add/Remove additional column to sorting columns
	void ToggleSortByColumn(int column)
	{
		ibDataViewCtrl* const owner = GetOwner();

		if (!owner->IsMultiColumnSortAllowed())
			return;

		ibDataViewColumn* const col = owner->GetColumn(column);
		if (!col->IsSortable())
			return;

		if (owner->IsColumnSorted(column))
		{
			col->UnsetAsSortKey();
			SendEvent(wxEVT_DATAVIEW_COLUMN_SORTED, column);
		}
		else // Do start sortign by it.
		{
			col->SetSortOrder(true);
			SendEvent(wxEVT_DATAVIEW_COLUMN_SORTED, column);
		}
	}

#if wxUSE_ACCESSIBILITY
	virtual wxAccessible* CreateAccessible() wxOVERRIDE
	{
		// Under MSW wxHeadrCtrl is a native control
		// so we just need to pass all requests
		// to the accessibility framework.
		return new wxAccessible(this);
	}
#endif // wxUSE_ACCESSIBILITY

protected:
	// implement/override ibHeaderGenericCtrl functions by forwarding them to the main
	// control
	virtual const wxHeaderColumn& GetColumn(unsigned int idx) const wxOVERRIDE
	{
		return *(GetOwner()->GetColumn(idx));
	}

	virtual bool UpdateColumnWidthToFit(unsigned int idx, int widthTitle) wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		ibDataViewColumn* const column = owner->GetColumn(idx);

		int widthContents = owner->GetBestColumnWidth(column);
		column->SetWidth(wxMax(widthTitle, widthContents));
		owner->OnColumnChange(idx);

		return true;
	}

	// THE HEADER FOLLOWS THE SAME GEOMETRY AS THE CELLS.
	//
	// Both read the control's column layout, so a group title stands exactly over
	// the columns it owns and a stacked column's title sits exactly over its own
	// band — there is no second arithmetic to keep in step with the first. Flat is
	// the degenerate case here (one full-height cell per column), not a separate path.
	virtual void BuildHeaderButtons(std::vector<ibHeaderButton>& cells, int height) const wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		const ibDataViewColumnLayout& layout = owner->GetColumnLayout();

		const int bands = wxMax(layout.GetHeaderBandCount(), 1);

		for (const ibHeaderCell& source : layout.GetHeaderCells()) {

			const ibColumnPlacement& place = source.place;

			// Divide the height we were GIVEN rather than multiplying a nominal band
			// height: the header is sized in whole bands and the last one has to
			// reach the bottom pixel.
			const int top = (height * place.band) / bands;
			const int bottom = (height * (place.band + place.bandSpan)) / bands;

			ibHeaderButton cell;
			cell.rect = wxRect(place.x, top, place.width, bottom - top);

			if (source.group != nullptr) {
				cell.isGroup = true;
				cell.title = source.group->GetTitle();
				cell.align = source.group->GetAlignment();
			}
			else if (place.column != nullptr) {
				const int idx = owner->GetColumnIndex(place.column);
				if (idx == wxNOT_FOUND)
					continue;
				cell.column = (unsigned int)idx;
				cell.title = place.column->GetTitle();
				cell.bitmap = place.column->GetBitmapBundle();
				cell.align = place.column->GetAlignment();
			}
			else {
				continue;
			}

			cells.push_back(cell);
		}
	}

	// Same source for the INTERACTION geometry as for the drawn one: resizing, the
	// reorder drop marker and the refresh rectangle all read a column's x from the
	// layout instead of adding up the widths to its left (which stops being true the
	// moment two columns share an x range).
	virtual int GetColStart(unsigned int idx) const wxOVERRIDE
	{
		ibColumnPlacement place;
		if (!GetColumnHeaderPlacement(idx, place))
			return ibHeaderGenericCtrl::GetColStart(idx);

		return place.x + GetScrollOffset();
	}

	virtual int GetColEnd(unsigned int idx) const wxOVERRIDE
	{
		ibColumnPlacement place;
		if (!GetColumnHeaderPlacement(idx, place))
			return ibHeaderGenericCtrl::GetColEnd(idx);

		return place.x + place.width + GetScrollOffset();
	}

	// Which column a header point belongs to. Stacked columns share an x range, so
	// the BAND under the cursor is what tells them apart — that is the whole reason
	// this override exists.
	virtual unsigned int FindColumnAtPoint(int xPhysical, int yPhysical, bool* onSeparator) const wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		const ibDataViewColumnLayout& layout = owner->GetColumnLayout();

		// Only "no y known" falls back to the base arithmetic — a grouped header needs the
		// band, and a FLAT header is just a header of one band, answered by the same
		// placements it is drawn from (see GetColumnHeaderPlacement).
		if (yPhysical < 0)
			return ibHeaderGenericCtrl::FindColumnAtPoint(xPhysical, yPhysical, onSeparator);

		if (onSeparator != nullptr)
			*onSeparator = false;

		int w, h;
		GetClientSize(&w, &h);

		const int bands = wxMax(layout.GetHeaderBandCount(), 1);
		const int band = h > 0 ? wxMin((yPhysical * bands) / h, bands - 1) : 0;
		const int xLogical = xPhysical - GetScrollOffset();

		const ibHeaderCell* cell = layout.FindHeaderCellAt(xLogical, band);
		if (cell == nullptr)
			return COL_NONE;

		const int separatorClickMargin = FromDIP(8);

		// A DIVIDER BELONGS TO THE CELL ON ITS LEFT, from either side of it. The point
		// lands in the cell whose range contains it, so a pixel to the RIGHT of a
		// divider is already inside the NEXT cell — and asking that cell about its own
		// right edge said "no divider here". Half the grab zone was dead: you could
		// only catch a border by approaching it from the left.
		if (abs(xLogical - cell->place.x) < separatorClickMargin) {
			if (const ibHeaderCell* prev = layout.FindHeaderCellAt(cell->place.x - 1, band))
				cell = prev;
		}

		const int right = cell->place.x + cell->place.width;
		const bool onEdge = abs(xLogical - right) < separatorClickMargin;

		// A GROUP TITLE IS NOT A COLUMN — it cannot be clicked to sort or dragged to
		// reorder. Its EDGE, though, is the edge of the columns under it, and dragging
		// that is how you widen a group: the drag is handed to the last column under it
		// and WXApplyColumnWidth passes the new width to the whole stack.
		ibDataViewColumn* column = cell->place.column;
		if (column == nullptr) {
			if (!onEdge)
				return COL_NONE;
			column = layout.FindLastColumnOf(cell->group);
			if (column == nullptr)
				return COL_NONE;
		}

		const int idx = owner->GetColumnIndex(column);
		if (idx == wxNOT_FOUND)
			return COL_NONE;

		if (onSeparator != nullptr && column->IsResizeable())
			*onSeparator = onEdge;

		return (unsigned int)idx;
	}

	// NOTHING TO MOVE HERE. The generic header keeps an order array of its own; this
	// header's order is the column TREE, and the drag has already moved the member in it
	// (ibDataViewCtrl::ColumnMoved, called from OnEndReorder just above). Permuting the
	// array as well would give two answers to "which column is at position N" — and it is
	// the array that everything else here no longer reads.
	virtual void DoMoveCol(unsigned int WXUNUSED(idx), unsigned int WXUNUSED(pos)) wxOVERRIDE
	{
		Refresh();
	}

private:

	// The column's place in the HEADER grid, by its index in the control.
	bool GetColumnHeaderPlacement(unsigned int idx, ibColumnPlacement& place) const
	{
		// ALWAYS the layout, flat or grouped. It used to answer "no" for a flat table and let
		// the base class add up column widths instead — a SECOND account of where the columns
		// are, beside the one they are DRAWN from. The two disagree as soon as anything (a
		// hidden column, the header's own order array, a stretch just applied) touches one and
		// not the other, and that shows as a resize border you grab to the LEFT of where it is
		// painted. One account, and the cursor cannot be out of step with it.
		ibDataViewCtrl* const owner = GetOwner();
		return owner->GetColumnLayout().GetHeaderPlacement(owner->GetColumn(idx), place);
	}

	void FinishEditing();

	bool SendEvent(wxEventType type, unsigned int n)
	{
		ibDataViewCtrl* const owner = GetOwner();
		ibDataViewEvent event(type, owner, owner->GetColumn(n));

		// for events created by ibDataViewHeaderWindow the
		// row / value fields are not valid
		return owner->ProcessWindowEvent(event);
	}

	void OnClick(ibHeaderGenericCtrlEvent& event)
	{
		FinishEditing();

		const unsigned idx = event.GetColumn();

		if (SendEvent(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, idx))
			return;

		// default handling for the column click is to sort by this column or
		// toggle its sort order
		ibDataViewCtrl* const owner = GetOwner();
		ibDataViewColumn* const col = owner->GetColumn(idx);
		if (!col->IsSortable())
		{
			// no default handling for non-sortable columns
			event.Skip();
			return;
		}

		// Model-side veto — paged models (ibValueModel-derived) deny
		// header-click toggle for system sorts and for columns that
		// have no sort entry at all.  Non-paged native models default
		// to permissive (true) so wx-style lists keep working.
		if (auto* m = owner->GetModel();
		    m != nullptr && !m->IsSortable(col->GetModelColumn()))
		{
			event.Skip();
			return;
		}

		if (col->IsSortKey())
		{
			// already using this column for sorting, just change the order
			col->ToggleSortOrder();
		}
		else // not using this column for sorting yet
		{
			// We will sort by this column only now, so reset all the
			// previously used ones.
			owner->ResetAllSortColumns();

			// Sort the column
			col->SetSortOrder(true);
		}

		ibDataViewModel* const model = owner->GetModel();
		if (model) {
			// Sort change invalidates the paged anchor: GetTopItem()'s
			// cursor key (e.g. uuid + sortValues) was built for the OLD
			// ordering and steers the new SQL into the wrong half of the
			// table — typically returning a single row.  Skip restore
			// capture so PagedRefresh starts a fresh fetch from the top
			// of the new ordering.
			owner->SetPagedSkipRestoreCapture();
			// Direct integration: the paged override rewrites the composer
			// sort and refetches; legacy non-paged models forward to
			// Resort() through the default implementation. NOTE: an OES
			// tablebox consumes wxEVT_DATAVIEW_COLUMN_HEADER_CLICK first
			// (ibValueModelTableBox::OnColumnClick) and commits the sort
			// straight on its concrete model's composer — so this generic
			// path only runs for native (non-tablebox) models.
			(void)model;   // native wx model: header SortOrder state above drives the re-sort; no model sort call
		}

		owner->OnColumnChange(idx);

		SendEvent(wxEVT_DATAVIEW_COLUMN_SORTED, idx);
	}

	void OnRClick(ibHeaderGenericCtrlEvent& event)
	{
		// Event wasn't processed somewhere, use default behaviour
		if (!SendEvent(wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK,
			event.GetColumn()))
		{
			event.Skip();
			ToggleSortByColumn(event.GetColumn());
		}
	}

	// A DRAG BEGINS: the control remembers the widths it starts from, so the whole drag is
	// measured against ONE picture instead of against the previous mouse-move.
	void OnBeginResize(ibHeaderGenericCtrlEvent& event)
	{
		ibDataViewCtrl* const owner = GetOwner();

		owner->WXBeginColumnDrag(owner->GetColumn(event.GetColumn()));
		event.Skip();
	}

	void OnResize(ibHeaderGenericCtrlEvent& event)
	{
		FinishEditing();

		ibDataViewCtrl* const owner = GetOwner();

		const unsigned col = event.GetColumn();

		// Through the control rather than straight at the column: columns STACKED
		// under one vertical group share an x range, so a drag on that edge is a drag
		// on all of them — one keeping its old width would leave the group ragged.
		owner->WXApplyColumnWidth(owner->GetColumn(col), event.GetWidth());
	}

	// The last width of the drag, and then the remembered picture is let go.
	void OnEndResize(ibHeaderGenericCtrlEvent& event)
	{
		OnResize(event);
		GetOwner()->WXEndColumnDrag();
	}

	// WHERE A DROP WOULD PUT THE COLUMN — the group it joins, the place in it, and the
	// rectangle that SHOWS it. One answer, read both while dragging (the hint) and when
	// the button comes up (the move), so what the user is shown is what happens.
	struct ibColumnDrop {
		ibDataViewColumnGroup* holder = nullptr;
		unsigned int at = 0;
		wxRect hint;              // physical, over the header
	};

	// IN THE MIDDLE of a column = into that column's group, beside it. NEAR THE EDGE of a
	// group = out of it, next to the group itself, ONE LEVEL UP — which is how a column
	// gets between two groups, and the only way it can leave the group it is in: every
	// point of a grouped header belongs to somebody, so without an edge zone there is
	// nowhere to drop a column meaning "not inside anything".
	//
	// The hint is shaped after the holder: a bar BETWEEN COLUMNS where they run side by
	// side, and a bar BETWEEN BANDS inside a stack — so "it will go under this one" and
	// "it will go beside this one" look different while the mouse is still down.
	bool FindColumnDrop(int xPhysical, int yPhysical, ibColumnDrop& out) const
	{
		ibDataViewCtrl* const owner = GetOwner();
		const ibDataViewColumnLayout& layout = owner->GetColumnLayout();

		int w, h;
		GetClientSize(&w, &h);

		const int bands = wxMax(layout.GetHeaderBandCount(), 1);
		const int band = (h > 0 && yPhysical >= 0)
			? wxMin((yPhysical * bands) / h, bands - 1) : 0;
		const int xLogical = xPhysical - GetScrollOffset();

		const ibHeaderCell* cell = layout.FindHeaderCellAt(xLogical, band);
		if (cell == nullptr)
			return false;

		// A GROUP's own title cell names the group; a column cell names the column, and
		// then the group is the column's holder.
		ibDataViewColumnGroup* holder = cell->group != nullptr
			? cell->group
			: (cell->place.column != nullptr ? cell->place.column->GetParent() : nullptr);
		if (holder == nullptr)
			return false;

		const int edge = FromDIP(10);
		const int left = cell->place.x, right = cell->place.x + cell->place.width;
		const bool atLeft = abs(xLogical - left) < edge;
		const bool atRight = abs(xLogical - right) < edge;

		// Out of the group only where there IS an out: the root has no holder above it.
		if (holder->GetParent() != nullptr && (atLeft || atRight)) {

			ibDataViewColumnGroup* above = holder->GetParent();
			const int pos = above->GetMemberPosition(holder);

			out.holder = above;
			out.at = pos != wxNOT_FOUND ? (unsigned int)pos : above->GetMemberCount();
			if (atRight)
				out.at++;

			// BETWEEN the groups: the full height of the header, at the group's edge.
			const int x = (atRight ? right : left) + GetScrollOffset();
			out.hint = wxRect(x - kDropMarker / 2, 0, kDropMarker, h);
			return true;
		}

		if (cell->place.column != nullptr) {

			const int pos = holder->GetMemberPosition(cell->place.column);
			const bool after = holder->GetKind() == ibColumnGroupVertical
				? (band >= cell->place.band + cell->place.bandSpan - 1
					&& yPhysical > CellMiddleY(cell->place, h, bands))
				: (xLogical > left + cell->place.width / 2);

			out.holder = holder;
			out.at = pos != wxNOT_FOUND ? (unsigned int)pos + (after ? 1 : 0) : holder->GetMemberCount();

			if (holder->GetKind() == ibColumnGroupVertical) {
				// UNDER (or over) the column it was dropped on — a stack grows downwards,
				// so that is what the hint has to say.
				const int top = (h * cell->place.band) / bands;
				const int bottom = (h * (cell->place.band + cell->place.bandSpan)) / bands;
				const int y = after ? bottom : top;
				out.hint = wxRect(left + GetScrollOffset(), y - kDropMarker / 2,
					cell->place.width, kDropMarker);
			}
			else {
				const int x = (after ? right : left) + GetScrollOffset();
				out.hint = wxRect(x - kDropMarker / 2, 0, kDropMarker, h);
			}
			return true;
		}

		// On a group TITLE, away from its edges: into it, at the end.
		out.holder = holder;
		out.at = holder->GetMemberCount();
		out.hint = wxRect(left + GetScrollOffset(), 0, cell->place.width, h);
		return true;
	}

	virtual void UpdateReorderingMarker(int xPhysical, int yPhysical) wxOVERRIDE
	{
		const ibDataViewColumnLayout& layout = GetOwner()->GetColumnLayout();
		if (layout.IsFlat()) {
			ibHeaderGenericCtrl::UpdateReorderingMarker(xPhysical, yPhysical);
			return;
		}

		ibColumnDrop drop;
		DrawReorderingMarker(xPhysical,
			FindColumnDrop(xPhysical, yPhysical, drop) ? drop.hint : wxRect());
	}

	// THE DROP ITSELF — the same answer the hint was drawn from, so the column lands where
	// the user was shown it would.
	virtual bool EndReordering(int xPhysical, int yPhysical) wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		ibDataViewColumn* const dragged = IsReordering()
			? owner->GetColumn(GetColumnBeingReordered()) : nullptr;

		// The base ends the drag itself — mouse capture, the overlay, and its "did we
		// really move it" answer. The ORDER it would apply is a no-op: DoMoveCol is
		// overridden here, because the tree is the order.
		const bool moved = ibHeaderGenericCtrl::EndReordering(xPhysical, yPhysical);
		if (!moved || dragged == nullptr)
			return moved;

		FinishEditing();

		ibColumnDrop drop;
		if (FindColumnDrop(xPhysical, yPhysical, drop))
			owner->WXMoveColumn(dragged, drop.holder, drop.at);

		return moved;
	}

private:

	// Thickness of the drop hint, and the middle of a cell's band range — both used by
	// FindColumnDrop only.
	static const int kDropMarker = 4;

	static int CellMiddleY(const ibColumnPlacement& place, int height, int bands)
	{
		const int top = (height * place.band) / bands;
		const int bottom = (height * (place.band + place.bandSpan)) / bands;
		return (top + bottom) / 2;
	}

protected:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibDataViewHeaderWindow);
};

wxBEGIN_EVENT_TABLE(ibDataViewHeaderWindow, ibHeaderGenericCtrl)
EVT_HEADER_CLICK(wxID_ANY, ibDataViewHeaderWindow::OnClick)
EVT_HEADER_RIGHT_CLICK(wxID_ANY, ibDataViewHeaderWindow::OnRClick)

EVT_HEADER_BEGIN_RESIZE(wxID_ANY, ibDataViewHeaderWindow::OnBeginResize)
EVT_HEADER_RESIZING(wxID_ANY, ibDataViewHeaderWindow::OnResize)
EVT_HEADER_END_RESIZE(wxID_ANY, ibDataViewHeaderWindow::OnEndResize)

wxEND_EVENT_TABLE()

//-----------------------------------------------------------------------------
// ibDataViewFooterWindow
//-----------------------------------------------------------------------------

class ibDataViewFooterWindow : public ibHeaderGenericCtrl
{
public:

	ibDataViewFooterWindow(ibDataViewCtrl* parent)
		: ibHeaderGenericCtrl(parent, wxID_ANY,
			wxDefaultPosition, wxDefaultSize,
			wxHD_BITMAP_ON_RIGHT)
	{
	}

	ibDataViewCtrl* GetOwner() const
	{
		return static_cast<ibDataViewCtrl*>(GetParent());
	}

	virtual wxWindow* GetMainWindowOfCompositeControl() wxOVERRIDE
	{
		return GetOwner();
	}

#if wxUSE_ACCESSIBILITY
	virtual wxAccessible* CreateAccessible() wxOVERRIDE
	{
		// Under MSW wxHeadrCtrl is a native control
		// so we just need to pass all requests
		// to the accessibility framework.
		return new wxAccessible(this);
	}
#endif // wxUSE_ACCESSIBILITY

protected:
	// implement/override ibHeaderGenericCtrl functions by forwarding them to the main
	// control
	virtual const wxHeaderColumn& GetColumn(unsigned int idx) const wxOVERRIDE
	{
		return GetOwner()->GetColumn(idx)->GetFooterColumn();
	}

	virtual bool UpdateColumnWidthToFit(unsigned int idx, int widthTitle) wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		ibDataViewColumn* const column = owner->GetColumn(idx);

		int widthContents = owner->GetBestColumnWidth(column);
		column->SetWidth(wxMax(widthTitle, widthContents));
		owner->OnColumnChange(idx);

		return true;
	}

	// THE FOOTER FOLLOWS THE CELLS, and so it follows the same layout the header and the
	// cells do. Not the header's geometry, though, but the BODY's: a footer cell is the
	// foot of the column above it, so it takes that column's x, width and BAND — a
	// stacked column's total sits under that column, in its own band, and not in one
	// wide strip pretending the stack is a single column. Group titles have no place
	// here: a group heads its columns, it does not total them.
	virtual void BuildHeaderButtons(std::vector<ibHeaderButton>& cells, int height) const wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		const ibDataViewColumnLayout& layout = owner->GetColumnLayout();

		const int bands = wxMax(layout.GetRowBandCount(), 1);

		for (const ibColumnPlacement& place : layout.GetBodyPlacements()) {

			if (place.column == nullptr)
				continue;

			const int idx = owner->GetColumnIndex(place.column);
			if (idx == wxNOT_FOUND)
				continue;

			// Divided out of the height GIVEN, exactly as the header does it, so the
			// last band reaches the bottom pixel.
			const int top = (height * place.band) / bands;
			const int bottom = (height * (place.band + place.bandSpan)) / bands;

			ibHeaderButton cell;
			cell.rect = wxRect(place.x, top, place.width, bottom - top);
			cell.column = (unsigned int)idx;
			cell.title = place.column->GetFooterTitle();
			cell.bitmap = place.column->GetFooterBitmapBundle();
			cell.align = place.column->GetFooterAlignment();

			cells.push_back(cell);
		}
	}

	// The INTERACTION geometry from the same place as the drawn one — see the header's
	// overrides. The footer reads BODY placements, since that is what it draws.
	virtual int GetColStart(unsigned int idx) const wxOVERRIDE
	{
		ibColumnPlacement place;
		if (!GetColumnBodyPlacement(idx, place))
			return ibHeaderGenericCtrl::GetColStart(idx);

		return place.x + GetScrollOffset();
	}

	virtual int GetColEnd(unsigned int idx) const wxOVERRIDE
	{
		ibColumnPlacement place;
		if (!GetColumnBodyPlacement(idx, place))
			return ibHeaderGenericCtrl::GetColEnd(idx);

		return place.x + place.width + GetScrollOffset();
	}

	// Stacked columns share an x range in the footer as they do everywhere else, so the BAND
	// under the cursor is what tells them apart. A FLAT footer is one band, answered by the
	// same placements it is drawn from — never by a second account of the widths (see the
	// header's GetColumnHeaderPlacement for what that second account cost).
	virtual unsigned int FindColumnAtPoint(int xPhysical, int yPhysical, bool* onSeparator) const wxOVERRIDE
	{
		ibDataViewCtrl* const owner = GetOwner();
		const ibDataViewColumnLayout& layout = owner->GetColumnLayout();

		if (yPhysical < 0)
			return ibHeaderGenericCtrl::FindColumnAtPoint(xPhysical, yPhysical, onSeparator);

		if (onSeparator != nullptr)
			*onSeparator = false;

		int w, h;
		GetClientSize(&w, &h);

		const int bands = wxMax(layout.GetRowBandCount(), 1);
		const int band = h > 0 ? wxMin((yPhysical * bands) / h, bands - 1) : 0;
		const int xLogical = xPhysical - GetScrollOffset();

		ibDataViewColumn* column = layout.FindColumnAt(xLogical, band);
		if (column == nullptr)
			return COL_NONE;

		const int idx = owner->GetColumnIndex(column);
		if (idx == wxNOT_FOUND)
			return COL_NONE;

		ibColumnPlacement place;
		if (!layout.GetBodyPlacement(column, place))
			return (unsigned int)idx;

		// A divider belongs to the cell on its LEFT from either side — the same rule as
		// in the header, and for the same reason (half the grab zone was dead).
		const int separatorClickMargin = FromDIP(8);
		if (abs(xLogical - place.x) < separatorClickMargin) {
			if (ibDataViewColumn* prev = layout.FindColumnAt(place.x - 1, band)) {
				const int prevIdx = owner->GetColumnIndex(prev);
				if (prevIdx != wxNOT_FOUND && layout.GetBodyPlacement(prev, place)) {
					if (onSeparator != nullptr)
						*onSeparator = true;
					return (unsigned int)prevIdx;
				}
			}
		}

		if (onSeparator != nullptr)
			*onSeparator = abs(xLogical - (place.x + place.width)) < separatorClickMargin;

		return (unsigned int)idx;
	}

	void OnResize(ibHeaderGenericCtrlEvent& event)
	{
		ibDataViewCtrl* const owner = GetOwner();

		const unsigned col = event.GetColumn();
		// Through the control, exactly as the header does it: a drag names a width ON
		// SCREEN, and only WXApplyColumnWidth knows what to store for it.
		owner->WXApplyColumnWidth(owner->GetColumn(col), event.GetWidth());
	}

private:

	// The column's rectangle in the BODY grid, by display index.
	bool GetColumnBodyPlacement(unsigned int idx, ibColumnPlacement& place) const
	{
		ibDataViewCtrl* const owner = GetOwner();
		if (idx >= owner->GetColumnCount())
			return false;

		return owner->GetColumnLayout().GetBodyPlacement(owner->GetColumn(idx), place);
	}

protected:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibDataViewFooterWindow);
};

wxBEGIN_EVENT_TABLE(ibDataViewFooterWindow, ibHeaderGenericCtrl)
EVT_HEADER_RESIZING(wxID_ANY, ibDataViewFooterWindow::OnResize)
EVT_HEADER_END_RESIZE(wxID_ANY, ibDataViewFooterWindow::OnResize)
wxEND_EVENT_TABLE()

//-----------------------------------------------------------------------------
// ibDataViewMainWindow
//-----------------------------------------------------------------------------

// The class definition lives in datavgen.window.private.h (a window is not part
// of a portion); the out-of-line wxIMPLEMENT_DYNAMIC_CLASS and event-table impls
// below stay here.
#include "datavgen.window.private.h"

// ---------------------------------------------------------
// ibGenericDataViewModelNotifier
// ---------------------------------------------------------

class ibGenericDataViewModelNotifier : public ibDataViewModelNotifier
{
public:

	ibGenericDataViewModelNotifier(ibDataViewCtrl* mainWindow)
	{
		m_tableAreaWin = mainWindow;
	}

	virtual bool ItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item) wxOVERRIDE
	{
		return m_tableAreaWin->ItemInserted(parent, item);
	}

	virtual bool ItemAppended(const ibDataViewItem& parent, const ibDataViewItem& item) wxOVERRIDE
	{
		return m_tableAreaWin->ItemAppended(parent, item);
	}

	virtual bool ItemDeleted(const ibDataViewItem& parent, const ibDataViewItem& item) wxOVERRIDE
	{
		return m_tableAreaWin->ItemDeleted(parent, item);
	}

	virtual bool ItemChanged(const ibDataViewItem& item) wxOVERRIDE
	{
		return m_tableAreaWin->ItemChanged(item);
	}

	virtual bool ValueChanged(const ibDataViewItem& item, unsigned int col) wxOVERRIDE
	{
		return m_tableAreaWin->ValueChanged(item, col);
	}

	virtual bool Cleared() wxOVERRIDE
	{
		return m_tableAreaWin->Cleared();
	}

	// Freeze the control across the BeforeReset → AfterReset window so
	// any intermediate paint events during the model mutation (vector
	// clear, async refresh state setup) don't show stale or empty
	// rows.  PagedRefresh's own inner Freeze (datavgen.paged.cpp:172)
	// nests safely — wxFreeze is reference-counted, so the outer pair
	// here just brackets the synchronous mutation while the inner pair
	// extends the freeze across the async wipe → bootstrap window.
	virtual bool BeforeReset() wxOVERRIDE
	{
		m_tableAreaWin->Freeze();
		return true;
	}

	virtual bool AfterReset() wxOVERRIDE
	{
		const bool r = m_tableAreaWin->Cleared();
		m_tableAreaWin->Thaw();
		return r;
	}

	virtual void Resort() wxOVERRIDE
	{
		m_tableAreaWin->Resort();
	}

	ibDataViewCtrl* m_tableAreaWin;
};

// ---------------------------------------------------------
// ibDataViewRenderer
// ---------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewRenderer, ibDataViewRendererBase);

ibDataViewRenderer::ibDataViewRenderer(const wxString& varianttype,
	ibDataViewCellMode mode,
	int align) :
	ibDataViewCustomRendererBase(varianttype, mode, align)
{
	m_align = align;
	m_mode = mode;
	m_ellipsizeMode = wxELLIPSIZE_MIDDLE;
	m_dc = NULL;
	m_state = 0;
}

ibDataViewRenderer::~ibDataViewRenderer()
{
	delete m_dc;
}

wxDC* ibDataViewRenderer::GetDC()
{
	if (m_dc == NULL)
	{
		if (GetOwner() == NULL)
			return NULL;
		if (GetOwner()->GetOwner() == NULL)
			return NULL;
		m_dc = new wxClientDC(GetOwner()->GetOwner());
	}

	return m_dc;
}

void ibDataViewRenderer::SetAlignment(int align)
{
	m_align = align;
}

int ibDataViewRenderer::GetAlignment() const
{
	return m_align;
}

// ---------------------------------------------------------
// ibDataViewCustomRenderer
// ---------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewCustomRenderer, ibDataViewRenderer);

ibDataViewCustomRenderer::ibDataViewCustomRenderer(const wxString& varianttype,
	ibDataViewCellMode mode, int align) :
	ibDataViewRenderer(varianttype, mode, align)
{
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewCustomRenderer::GetAccessibleDescription() const
{
	wxVariant val;
	GetValue(val);

	wxString strVal;
	if (val.IsType(wxS("bool")))
	{
		/* TRANSLATORS: Name of Boolean true value */
		strVal = val.GetBool() ? _("true")
			/* TRANSLATORS: Name of Boolean false value */
			: _("false");
	}
	else
	{
		strVal = val.MakeString();
	}

	return strVal;
}
#endif // wxUSE_ACCESSIBILITY

// ---------------------------------------------------------
// ibDataViewTextRenderer
// ---------------------------------------------------------

wxIMPLEMENT_CLASS(ibDataViewTextRenderer, ibDataViewRenderer);

ibDataViewTextRenderer::ibDataViewTextRenderer(const wxString& varianttype,
	ibDataViewCellMode mode, int align) :
	ibDataViewRenderer(varianttype, mode, align)
{
#if wxUSE_MARKUP
	m_markupText = NULL;
#endif // wxUSE_MARKUP
}

ibDataViewTextRenderer::~ibDataViewTextRenderer()
{
#if wxUSE_MARKUP
	delete m_markupText;
#endif // wxUSE_MARKUP
}

#if wxUSE_MARKUP
void ibDataViewTextRenderer::EnableMarkup(bool enable)
{
	if (enable)
	{
		if (!m_markupText)
		{
			m_markupText = new wxItemMarkupText(wxString());
		}
	}
	else
	{
		if (m_markupText)
		{
			delete m_markupText;
			m_markupText = NULL;
		}
	}
}
#endif // wxUSE_MARKUP

bool ibDataViewTextRenderer::SetValue(const wxVariant& value)
{
	m_text = value.GetString();

#if wxUSE_MARKUP
	if (m_markupText)
		m_markupText->SetMarkup(m_text);
#endif // wxUSE_MARKUP

	return true;
}

bool ibDataViewTextRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
	return false;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewTextRenderer::GetAccessibleDescription() const
{
#if wxUSE_MARKUP
	if (m_markupText)
		return wxMarkupParser::Strip(m_text);
#endif // wxUSE_MARKUP
	return m_text;
}
#endif // wxUSE_ACCESSIBILITY

bool ibDataViewTextRenderer::HasEditorCtrl() const
{
	return true;
}

wxWindow* ibDataViewTextRenderer::CreateEditorCtrl(wxWindow* parent,
	wxRect labelRect, const wxVariant& value)
{
	return CreateEditorTextCtrl(parent, labelRect, value);
}

bool ibDataViewTextRenderer::GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value)
{
	wxTextCtrl* text = (wxTextCtrl*)editor;
	value = text->GetValue();
	return true;
}

bool ibDataViewTextRenderer::Render(wxRect rect, wxDC* dc, int state)
{
#if wxUSE_MARKUP
	if (m_markupText)
	{
		int flags = 0;
		if (state & wxDATAVIEW_CELL_SELECTED)
			flags |= wxCONTROL_SELECTED;
		m_markupText->Render(GetView(), *dc, rect, flags, GetEllipsizeMode());
	}
	else
#endif // wxUSE_MARKUP
		RenderText(m_text, 0, rect, dc, state);

	return true;
}

wxSize ibDataViewTextRenderer::GetSize() const
{
	if (!m_text.empty())
	{
#if wxUSE_MARKUP
		if (m_markupText)
		{
			ibDataViewCtrl* const view = GetView();
			wxClientDC dc(view);
			if (GetAttr().HasFont())
				dc.SetFont(GetAttr().GetEffectiveFont(view->GetFont()));

			return m_markupText->Measure(dc);
		}
#endif // wxUSE_MARKUP

		return GetTextExtent(m_text);
	}
	else
		return GetView()->FromDIP(wxSize(wxDVC_DEFAULT_RENDERER_SIZE,
			wxDVC_DEFAULT_RENDERER_SIZE));
}

// ---------------------------------------------------------
// ibDataViewBitmapRenderer
// ---------------------------------------------------------

wxIMPLEMENT_CLASS(ibDataViewBitmapRenderer, ibDataViewRenderer);

ibDataViewBitmapRenderer::ibDataViewBitmapRenderer(const wxString& varianttype,
	ibDataViewCellMode mode, int align) :
	ibDataViewRenderer(varianttype, mode, align)
{
}

bool ibDataViewBitmapRenderer::SetValue(const wxVariant& value)
{
	if (value.GetType() == wxT("wxBitmapBundle"))
	{
		m_bitmapBundle << value;
	}
	else if (value.GetType() == wxT("wxBitmap"))
	{
		wxBitmap bitmap;
		bitmap << value;
		m_bitmapBundle = wxBitmapBundle(bitmap);
	}
	else if (value.GetType() == wxT("wxIcon"))
	{
		wxIcon icon;
		icon << value;
		m_bitmapBundle = wxBitmapBundle(icon);
	}
	else
	{
		m_bitmapBundle.Clear();
	}

	return true;
}

bool ibDataViewBitmapRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
	return false;
}

bool
ibDataViewBitmapRenderer::IsCompatibleVariantType(const wxString& variantType) const
{
	// We can accept values of any types checked by SetValue().
	return variantType == wxS("wxBitmapBundle")
		|| variantType == wxS("wxBitmap")
		|| variantType == wxS("wxIcon");
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewBitmapRenderer::GetAccessibleDescription() const
{
	return wxEmptyString;
}
#endif // wxUSE_ACCESSIBILITY

bool ibDataViewBitmapRenderer::Render(wxRect cell, wxDC* dc, int WXUNUSED(state))
{
	if (m_bitmapBundle.IsOk())
	{
		dc->DrawBitmap(m_bitmapBundle.GetBitmapFor(GetView()),
			cell.x, cell.y,
			true /* use mask */);
	}

	return true;
}

wxSize ibDataViewBitmapRenderer::GetSize() const
{
	if (m_bitmapBundle.IsOk())
		return m_bitmapBundle.GetPreferredBitmapSizeFor(GetView());

	return GetView()->FromDIP(wxSize(wxDVC_DEFAULT_RENDERER_SIZE,
		wxDVC_DEFAULT_RENDERER_SIZE));
}

// ---------------------------------------------------------
// ibDataViewToggleRenderer
// ---------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewToggleRenderer, ibDataViewRenderer);

ibDataViewToggleRenderer::ibDataViewToggleRenderer(const wxString& varianttype,
	ibDataViewCellMode mode, int align) :
	ibDataViewRenderer(varianttype, mode, align)
{
	m_toggle = false;
	m_radio = false;
}

bool ibDataViewToggleRenderer::SetValue(const wxVariant& value)
{
	m_toggle = value.GetBool();

	return true;
}

bool ibDataViewToggleRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
	return false;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewToggleRenderer::GetAccessibleDescription() const
{
	/* TRANSLATORS: Checkbox state name */
	return m_toggle ? _("checked")
		/* TRANSLATORS: Checkbox state name */
		: _("unchecked");
}
#endif // wxUSE_ACCESSIBILITY

bool ibDataViewToggleRenderer::Render(wxRect cell, wxDC* dc, int WXUNUSED(state))
{
	int flags = 0;
	if (m_toggle)
		flags |= wxCONTROL_CHECKED;
	if (GetMode() != wxDATAVIEW_CELL_ACTIVATABLE ||
		!(GetOwner()->GetOwner()->IsEnabled() && GetEnabled()))
		flags |= wxCONTROL_DISABLED;

	// Ensure that the check boxes always have at least the minimal required
	// size, otherwise DrawCheckBox() doesn't really work well. If this size is
	// greater than the cell size, the checkbox will be truncated but this is a
	// lesser evil.
	wxSize size = cell.GetSize();
	size.IncTo(GetSize());
	cell.SetSize(size);

	wxRendererNative& renderer = wxRendererNative::Get();
	wxWindow* const win = GetOwner()->GetOwner();
	if (m_radio)
		renderer.DrawRadioBitmap(win, *dc, cell, flags);
	else
		renderer.DrawCheckBox(win, *dc, cell, flags);

	return true;
}

bool ibDataViewToggleRenderer::WXActivateCell(const wxRect& WXUNUSED(cellRect),
	ibDataViewModel* model,
	const ibDataViewItem& item,
	unsigned int col,
	const wxMouseEvent* mouseEvent)
{
	if (mouseEvent)
	{
		// Only react to clicks directly on the checkbox, not elsewhere in the
		// same cell.
		if (!wxRect(GetSize()).Contains(mouseEvent->GetPosition()))
			return false;
	}

	model->ChangeValue(!m_toggle, item, col);
	return true;
}

wxSize ibDataViewToggleRenderer::GetSize() const
{
	return wxRendererNative::Get().GetCheckBoxSize(GetView());
}

// ---------------------------------------------------------
// ibDataViewProgressRenderer
// ---------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewProgressRenderer, ibDataViewRenderer);

ibDataViewProgressRenderer::ibDataViewProgressRenderer(const wxString& label,
	const wxString& varianttype, ibDataViewCellMode mode, int align) :
	ibDataViewRenderer(varianttype, mode, align)
	, m_label(label)
{
	m_value = 0;
}

bool ibDataViewProgressRenderer::SetValue(const wxVariant& value)
{
	m_value = (long)value;

	if (m_value < 0) m_value = 0;
	if (m_value > 100) m_value = 100;

	return true;
}

bool ibDataViewProgressRenderer::GetValue(wxVariant& value) const
{
	value = (long)m_value;
	return true;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewProgressRenderer::GetAccessibleDescription() const
{
	return wxString::Format(wxS("%i %%"), m_value);
}
#endif // wxUSE_ACCESSIBILITY

bool
ibDataViewProgressRenderer::Render(wxRect rect, wxDC* dc, int WXUNUSED(state))
{
	const ibDataViewItemAttr& attr = GetAttr();
	if (attr.HasColour())
		dc->SetBackground(attr.GetColour());

	// This is a hack, but native renderers don't support using custom colours,
	// but typically gauge colour is important (e.g. it's commonly green/red to
	// indicate some qualitative difference), so we fall back to the generic
	// implementation which looks ugly but does support using custom colour.
	wxRendererNative& renderer = attr.HasColour()
		? wxRendererNative::GetGeneric()
		: wxRendererNative::Get();
	renderer.DrawGauge(
		GetOwner()->GetOwner(),
		*dc,
		rect,
		m_value,
		100);

	return true;
}

wxSize ibDataViewProgressRenderer::GetSize() const
{
	// Return -1 width because a progress bar fits any width; unlike most
	// renderers, it doesn't have a "good" width for the content. This makes it
	// grow to the whole column, which is pretty much always the desired
	// behaviour. Keep the height fixed so that the progress bar isn't too fat.
	return GetView()->FromDIP(wxSize(-1, 12));
}

// ---------------------------------------------------------
// ibDataViewIconTextRenderer
// ---------------------------------------------------------

wxIMPLEMENT_CLASS(ibDataViewIconTextRenderer, ibDataViewRenderer);

ibDataViewIconTextRenderer::ibDataViewIconTextRenderer(
	const wxString& varianttype, ibDataViewCellMode mode, int align) :
	ibDataViewRenderer(varianttype, mode, align)
{
	SetMode(mode);
	SetAlignment(align);
}

bool ibDataViewIconTextRenderer::SetValue(const wxVariant& value)
{
	m_value << value;
	return true;
}

bool ibDataViewIconTextRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
	return false;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewIconTextRenderer::GetAccessibleDescription() const
{
	return m_value.GetText();
}
#endif // wxUSE_ACCESSIBILITY

bool ibDataViewIconTextRenderer::Render(wxRect rect, wxDC* dc, int state)
{
	int xoffset = 0;

	const wxBitmapBundle& bb = m_value.GetBitmapBundle();
	if (bb.IsOk())
	{
		wxWindow* const dvc = GetView();
		const wxIcon& icon = bb.GetIconFor(dvc);
		dc->DrawIcon(icon, rect.x, rect.y + (rect.height - icon.GetLogicalHeight()) / 2);
		xoffset = icon.GetLogicalWidth() + dvc->FromDIP(4);
	}

	RenderText(m_value.GetText(), xoffset, rect, dc, state);

	return true;
}

wxSize ibDataViewIconTextRenderer::GetSize() const
{
	wxWindow* const dvc = GetView();

	if (!m_value.GetText().empty())
	{
		wxSize size = GetTextExtent(m_value.GetText());

		const wxBitmapBundle& bb = m_value.GetBitmapBundle();
		if (bb.IsOk())
			size.x += bb.GetPreferredLogicalSizeFor(dvc).x + dvc->FromDIP(4);
		return size;
	}
	return dvc->FromDIP(wxSize(80, 20));
}

wxWindow* ibDataViewIconTextRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
	ibDataViewIconText iconText;
	iconText << value;

	wxString text = iconText.GetText();

	// adjust the label rect to take the width of the icon into account
	const wxBitmapBundle& bb = iconText.GetBitmapBundle();
	if (bb.IsOk())
	{
		wxWindow* const dvc = GetView();

		int w = bb.GetPreferredLogicalSizeFor(dvc).x + dvc->FromDIP(4);
		labelRect.x += w;
		labelRect.width -= w;
	}

	return CreateEditorTextCtrl(parent, labelRect, text);
}

bool ibDataViewIconTextRenderer::GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value)
{
	wxTextCtrl* text = (wxTextCtrl*)editor;

	// The icon can't be edited so get its old value and reuse it.
	wxVariant valueOld;
	ibDataViewColumn* const col = GetOwner();
	GetView()->GetModel()->GetValue(valueOld, m_item, col->GetModelColumn());

	ibDataViewIconText iconText;
	iconText << valueOld;

	// But replace the text with the value entered by user.
	iconText.SetText(text->GetValue());

	value << iconText;
	return true;
}

//-----------------------------------------------------------------------------
// ibDataViewDropTarget
//-----------------------------------------------------------------------------

#if wxUSE_DRAG_AND_DROP

class ibBitmapCanvas : public wxWindow
{
public:
	ibBitmapCanvas(wxWindow* parent, const wxBitmap& bitmap, const wxSize& size) :
		wxWindow(parent, wxID_ANY, wxPoint(0, 0), size)
		, m_bitmap(bitmap)
	{
		Bind(wxEVT_PAINT, &ibBitmapCanvas::OnPaint, this);
	}

	void OnPaint(wxPaintEvent& WXUNUSED(event))
	{
		wxPaintDC dc(this);
		dc.DrawBitmap(m_bitmap, 0, 0);
	}

	wxBitmap m_bitmap;
};

class ibDataViewDropSource : public wxDropSource
{
public:
	ibDataViewDropSource(ibDataViewCtrl* win, unsigned int row) :
		wxDropSource(win)
	{
		m_win = win;
		m_row = row;
		m_hint = NULL;
	}

	~ibDataViewDropSource()
	{
		delete m_hint;
	}

	virtual bool GiveFeedback(wxDragResult WXUNUSED(effect)) wxOVERRIDE
	{
		wxPoint pos = wxGetMousePosition();

		if (!m_hint)
		{
			int liney = m_win->GetLineStart(m_row);
			int linex = 0;
			m_win->CalcUnscrolledPosition(0, liney, NULL, &liney);
			m_win->ClientToScreen(&linex, &liney);
			m_dist_x = pos.x - linex;
			m_dist_y = pos.y - liney;

			int indent = 0;
			wxBitmap ib = m_win->CreateItemBitmap(m_row, indent);
			m_dist_x -= indent;
			m_hint = new wxFrame(m_win, wxID_ANY, wxEmptyString,
				wxPoint(pos.x - m_dist_x, pos.y + 5),
				wxSize(1, 1),
				wxFRAME_TOOL_WINDOW |
				wxFRAME_FLOAT_ON_PARENT |
				wxFRAME_NO_TASKBAR |
				wxNO_BORDER);
			new ibBitmapCanvas(m_hint, ib, ib.GetLogicalSize());
			m_hint->SetClientSize(ib.GetLogicalSize());
			m_hint->SetTransparent(128);
			m_hint->Show();
		}
		else
		{
			m_hint->Move(pos.x - m_dist_x, pos.y + 5);
		}

		return false;
	}

	ibDataViewCtrl* m_win;
	unsigned int            m_row;
	wxFrame* m_hint;
	int m_dist_x, m_dist_y;
};

class ibDataViewDropTarget : public wxDropTarget
{
public:
	ibDataViewDropTarget(wxDataObjectComposite* obj, ibDataViewCtrl* win) :
		wxDropTarget(obj),
		m_obj(obj)
	{
		m_win = win;
	}

	wxDataObjectComposite* GetCompositeDataObject() const { return m_obj; }

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) wxOVERRIDE
	{
		wxDataFormat format = GetMatchingPair();
		if (format == wxDF_INVALID)
			return wxDragNone;
		return m_win->OnDragOver(format, x, y, def);
	}

	virtual bool OnDrop(wxCoord x, wxCoord y) wxOVERRIDE
	{
		wxDataFormat format = GetMatchingPair();
		if (format == wxDF_INVALID)
			return false;
		return m_win->OnDrop(format, x, y);
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) wxOVERRIDE
	{
		wxDataFormat format = GetMatchingPair();
		if (format == wxDF_INVALID)
			return wxDragNone;
		if (!GetData())
			return wxDragNone;
		return m_win->OnData(format, x, y, def);
	}

	virtual void OnLeave() wxOVERRIDE
	{
		m_win->OnLeave();
	}

private:
	wxDataObjectComposite* const m_obj;

	ibDataViewCtrl* m_win;
};

#endif // wxUSE_DRAG_AND_DROP

//-----------------------------------------------------------------------------
// ibDataViewRenameTimer
//-----------------------------------------------------------------------------

ibDataViewRenameTimer::ibDataViewRenameTimer(ibDataViewCtrl* owner)
{
	m_owner = owner;
}

void ibDataViewRenameTimer::Notify()
{
	m_owner->OnRenameTimer();
}

// ----------------------------------------------------------------------------
// ibDataViewTreeNode
// ----------------------------------------------------------------------------

namespace
{

	// Comparator used for sorting the tree nodes using the model-defined sort
	// order and also for performing binary search in our own code.
	class ibGenericTreeModelNodeCmp
	{
	public:
		ibGenericTreeModelNodeCmp(ibDataViewCtrl* window,
			const SortOrder& sortOrder)
			: m_model(window->GetModel()),
			m_sortOrder(sortOrder)
		{
			wxASSERT_MSG(!m_sortOrder.IsNone(), "should have sort order");
		}

		// Return negative, zero or positive value depending on whether the first
		// item is less than, equal to or greater than the second one.
		int Compare(ibDataViewTreeNode* first, ibDataViewTreeNode* second) const
		{
			if (first->HasChildren() != second->HasChildren())
				return first->HasChildren() ? -1 : 1;

			return m_model->Compare(first->GetItem(), second->GetItem(),
				m_sortOrder.GetColumn(),
				m_sortOrder.IsAscending());
		}

		// Return true if the items are (strictly) in order, i.e. the first item is
		// less than the second one. This is used by std::sort().
		bool operator()(ibDataViewTreeNode* first, ibDataViewTreeNode* second) const
		{
			return Compare(first, second) < 0;
		}

	private:
		ibDataViewModel* const m_model;
		const SortOrder m_sortOrder;
	};

} // anonymous namespace

void ibDataViewTreeNode::InsertChild(ibDataViewCtrl* window,
	ibDataViewTreeNode* node, unsigned index)
{
	if (!m_branchData)
		m_branchData = new BranchNodeData;

	const SortOrder sortOrder = window->GetSortOrder();

	// Flag indicating whether we should retain existing sorted list when
	// inserting the child node.
	bool insertSorted = false;

	if (sortOrder.IsNone())
	{
		// We should insert assuming an unsorted list. This will cause the
		// child list to lose the current sort order, if any.
		m_branchData->sortOrder = SortOrder();
	}
	else if (m_branchData->children.empty())
	{
		if (m_branchData->open)
		{
			// We don't need to search for the right place to insert the first
			// item (there is only one), but we do need to remember the sort
			// order to use for the subsequent ones.
			m_branchData->sortOrder = sortOrder;
		}
		else
		{
			// We're inserting the first child of a closed node. We can choose
			// whether to consider this empty child list sorted or unsorted.
			// By choosing unsorted, we postpone comparisons until the parent
			// node is opened in the view, which may be never.
			m_branchData->sortOrder = SortOrder();
		}
	}
	else if (m_branchData->open)
	{
		// For open branches, children should be already sorted.
		wxASSERT_MSG(m_branchData->sortOrder == sortOrder,
			wxS("Logic error in wxDVC sorting code"));

		// We can use fast insertion.
		insertSorted = true;
	}
	else if (m_branchData->sortOrder == sortOrder)
	{
		// The children are already sorted by the correct criteria (because
		// the node must have been opened in the same time in the past). Even
		// though it is closed now, we still insert in sort order to avoid a
		// later resort.
		insertSorted = true;
	}
	else
	{
		// The children of this closed node aren't sorted by the correct
		// criteria, so we just insert unsorted.
		m_branchData->sortOrder = SortOrder();
	}


	if (insertSorted)
	{
		// Use binary search to find the correct position to insert at.
		ibGenericTreeModelNodeCmp cmp(window, sortOrder);
		int lo = 0, hi = m_branchData->children.size();
		while (lo < hi)
		{
			int mid = lo + (hi - lo) / 2;
			int r = cmp.Compare(node, m_branchData->children[mid]);
			if (r < 0)
				hi = mid;
			else if (r > 0)
				lo = mid + 1;
			else
				lo = hi = mid;
		}
		m_branchData->InsertChild(node, lo);
	}
	else
	{
		m_branchData->InsertChild(node, index);
	}
}

void ibDataViewTreeNode::Resort(ibDataViewCtrl* window)
{
	if (!m_branchData)
		return;

	// No reason to sort a closed node.
	if (!m_branchData->open)
		return;

	const SortOrder sortOrder = window->GetSortOrder();

	if (!sortOrder.IsNone())
	{
		ibDataViewTreeNodes& nodes = m_branchData->children;

		// When sorting by column value, we can skip resorting entirely if the
		// same sort order was used previously. However we can't do this when
		// using model-specific sort order, which can change at any time.
		if (m_branchData->sortOrder != sortOrder || !sortOrder.UsesColumn())
		{
			std::sort(m_branchData->children.begin(),
				m_branchData->children.end(),
				ibGenericTreeModelNodeCmp(window, sortOrder));

			m_branchData->sortOrder = sortOrder;
		}

		// There may be open child nodes that also need a resort.
		int len = nodes.size();
		for (int i = 0; i < len; i++)
		{
			if (nodes[i]->HasChildren())
				nodes[i]->Resort(window);
		}
	}
}

void
ibDataViewTreeNode::PutChildInSortOrder(ibDataViewCtrl* window,
	ibDataViewTreeNode* childNode)
{
	// The childNode has changed, and may need to be moved to another location
	// in the sorted child list.

	if (!m_branchData)
		return;
	if (!m_branchData->open)
		return;
	if (m_branchData->sortOrder.IsNone())
		return;

	ibDataViewTreeNodes& nodes = m_branchData->children;

	// This is more than an optimization, the code below assumes that 1 is a
	// valid index.
	if (nodes.size() == 1)
		return;

	// We should already be sorted in the right order.
	wxASSERT(m_branchData->sortOrder == window->GetSortOrder());

	// First find the node in the current child list
	int hi = nodes.size();
	int oldLocation = wxNOT_FOUND;
	for (int index = 0; index < hi; ++index)
	{
		if (nodes[index] == childNode)
		{
			oldLocation = index;
			break;
		}
	}
	wxCHECK_RET(oldLocation >= 0, "not our child?");

	ibGenericTreeModelNodeCmp cmp(window, m_branchData->sortOrder);

	// Check if we actually need to move the node.
	bool locationChanged = false;

	// Compare with next node
	if (oldLocation != hi - 1)
	{
		if (!cmp(childNode, nodes[oldLocation + 1]))
			locationChanged = true;
	}

	// Compare with previous node
	if (!locationChanged && oldLocation > 0)
	{
		if (!cmp(nodes[oldLocation - 1], childNode))
			locationChanged = true;
	}

	if (!locationChanged)
		return;

	// Remove and reinsert the node in the child list
	m_branchData->RemoveChild(oldLocation);
	hi = nodes.size();
	int lo = 0;
	while (lo < hi)
	{
		int mid = lo + (hi - lo) / 2;
		int r = cmp.Compare(childNode, m_branchData->children[mid]);
		if (r < 0)
			hi = mid;
		else if (r > 0)
			lo = mid + 1;
		else
			lo = hi = mid;
	}

	m_branchData->InsertChild(childNode, lo);

	// Make sure the change is actually shown right away
	window->UpdateDisplay();
}

// The tree building helper, declared firstly

static void BuildHierarchicalHelper(ibDataViewCtrl* window,
	const ibDataViewModel* model,
	ibDataViewTreeNode* node);

static void BuildListHelper(ibDataViewCtrl* window,
	const ibDataViewModel* model,
	const ibDataViewItem& item,
	ibDataViewTreeNode* node);

static void BuildTreeHelper(ibDataViewCtrl* window,
	const ibDataViewModel* model,
	const ibDataViewItem& item,
	ibDataViewTreeNode* node);

void ibDataViewCtrl::DrawCellBackground(ibDataViewRenderer* cell, wxDC& dc, const wxRect& rect, ibDataViewTreeNodeViewMode mode)
{
	wxRect rectBg(rect);

	// don't overlap the horizontal rules
	if (HasFlag(wxDV_HORIZ_RULES))
	{
		rectBg.y++;
		rectBg.height--;
	}

	// don't overlap the vertical rules
	if (HasFlag(wxDV_VERT_RULES))
	{
		// same note as in OnPaint handler above
		// NB: Vertical rules are drawn in the last pixel of a column so that
		//     they align perfectly with native MSW ibHeaderGenericCtrl as well as for
		//     consistency with MSW native list control. There's no vertical
		//     rule at the most-left side of the control.
		rectBg.width--;
	}

	if (mode == ibDataViewTreeNodeViewMode::ibDataViewHierarchy)
	{
		const wxColour& colour = wxColour(197, 220, 232);
		wxDCPenChanger changePen(dc, colour);
		wxDCBrushChanger changeBrush(dc, colour);

		dc.DrawRectangle(rect);
		return;
	}

	cell->RenderBackground(&dc, rectBg);
}

void ibDataViewCtrl::OnRenameTimer()
{
	// We have to call this here because changes may just have
	// been made and no screen update taken place.
	if (m_dirty)
	{
		// TODO: use wxTheApp->SafeYieldFor(NULL, wxEVT_CATEGORY_UI) instead
		//       (needs to be tested!)
		wxSafeYield();
	}

	ibDataViewItem item = GetItemByRow(m_currentRow);

	StartEditing(item, m_currentCol);
}

void
ibDataViewCtrl::StartEditing(const ibDataViewItem& item,
	const ibDataViewColumn* col)
{
	ibDataViewRenderer* renderer = col->GetRenderer();
	if (!IsCellEditableInMode(item, col, wxDATAVIEW_CELL_EDITABLE))
		return;

	wxRect itemRect = GetItemRect(item, col);

	if (renderer->StartEditing(item, itemRect))
	{
		renderer->NotifyEditingStarted(item);

		// Save the renderer to be able to finish/cancel editing it later and
		// save the control to be able to detect if we're still editing it.
		m_editorRenderer = renderer;
		m_editorCtrl = renderer->GetEditorCtrl();
	}
}

void ibDataViewCtrl::FinishEditing()
{
	if (m_editorCtrl)
	{
		m_editorRenderer->FinishEditing();
	}
}

void ibDataViewHeaderWindow::FinishEditing()
{
	ibDataViewCtrl* win = static_cast<ibDataViewCtrl*>(GetOwner());
	win->FinishEditing();
}
//-----------------------------------------------------------------------------
// Helper class for do operation on the tree node
//-----------------------------------------------------------------------------

namespace
{
	class DoJob
	{
	public:
		DoJob() {}
		virtual ~DoJob() {}

		// The return value control how the tree-walker tranverse the tree
		enum
		{
			DONE,          // Job done, stop traversing and return
			SKIP_SUBTREE,  // Ignore the current node's subtree and continue
			CONTINUE       // Job not done, continue
		};

		virtual int operator() (ibDataViewTreeNode* node) = 0;
	};

	bool
		Walker(ibDataViewTreeNode* node, DoJob& func, WalkFlags flags = Walk_All)
	{
		wxCHECK_MSG(node, false, "can't walk NULL node");

		switch (func(node))
		{
		case DoJob::DONE:
			return true;
		case DoJob::SKIP_SUBTREE:
			return false;
		case DoJob::CONTINUE:
			break;
		}

		if (node->HasChildren() && (flags != Walk_ExpandedOnly || node->IsOpen()))
		{
			const ibDataViewTreeNodes& nodes = node->GetChildNodes();

			for (ibDataViewTreeNodes::const_iterator i = nodes.begin();
				i != nodes.end();
				++i)
			{
				if ((*i)->IsHidden())
					continue;
				if (Walker(*i, func, flags))
					return true;
			}
		}

		return false;
	}

} // anonymous namespace

bool ibDataViewCtrl::ItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item)
{
	return DoItemInserted(parent, item, wxEVT_DATAVIEW_ITEM_START_INSERTING);
}

bool ibDataViewCtrl::ItemAppended(const ibDataViewItem& parent, const ibDataViewItem& item)
{
	return DoItemInserted(parent, item, wxEVT_DATAVIEW_ITEM_START_ADDING);
}

bool ibDataViewCtrl::DoItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item, wxEventType eventType)
{
	// Send event
	ibDataViewEvent le(eventType, this, GetCurrentColumn(), item);
	if (ProcessWindowEvent(le) || !le.IsAllowed())
		return false;

	// Paged DB-backed path: row order is governed by SQL ORDER BY,
	// the new row's correct visual position needs a re-fetch.
	// SchedulePagedRefresh debounces a burst of N mutations into one
	// PagedRefresh on the next idle tick.  Newly inserted item
	// becomes the restore anchor so the bootstrap positions around
	// it and re-applies focus + selection.
	//
	// Paged RAM-backed path (TabularSection / ibValueTable / register
	// record sets): the row is already in m_nodeValues at a known
	// position when ItemInserted fires.  Falling through to the
	// non-paged tree-insert path below uses GetChildren to locate
	// the insertion index via sibling matching — works as long as
	// the new row is adjacent to a row that's in the loaded buffer
	// (typical copy-row / append scenarios).  Edge case: insertion
	// past the loaded buffer end places the row at an approximate
	// visual position; corrected on next scroll / refresh.  Trade-
	// off: avoids a full tree wipe + bootstrap on every cell-edit
	// derived ItemInserted (the user-visible flicker).
	if (GetModel() != nullptr && GetModel()->IsPagedModel()) {
		// EVERY paged model re-fetches around the new row and lets PagedBootstrap position + select it (centred via
		// the backward fetch), using m_pagedRestoreSelection (stamped by the _START_* handlers' ApplyCurrentLine →
		// Select) as the anchor. Keyed DB and grouped RAM always needed this (SQL ORDER BY / group placement); a
		// PLAIN RAM table (TabularSection / value-table) needs it too — the tree-insert path below can only place a
		// row ADJACENT to the loaded buffer, so a row appended PAST the buffer landed at an approximate position and
		// the standard EnsureVisible left it half-clipped at the fold. Row inserts are discrete (Add / Copy), not
		// per-keystroke, so there is no cell-edit flicker here (those fire ValueChanged → the narrow DoItemChanged).
		SchedulePagedRefresh(item);
		return true;
	}

	if (IsVirtualList())
	{
		ibDataViewVirtualListModel* list_model =
			(ibDataViewVirtualListModel*)GetModel();
		m_countRows = list_model->GetCount();
	}
	else
	{
		// specific position (row) is unclear, so clear whole height cache
		ClearRowHeightCache();

		const FindNodeResult findResult = FindNode(parent);
		ibDataViewTreeNode* parentNode = findResult.m_node;

		// If one of parents is not realized yet (has children but was never
		// expanded). Return as nodes will be initialized in Expand().
		if (!findResult.m_subtreeRealized)
			return true;

		// The parent node was not found.
		if (!parentNode)
			return false;

		// If the parent has not children then just mask it as container and return.
		// Nodes will be initialized in Expand().
		if (!parentNode->HasChildren())
		{
			parentNode->SetHasChildren(true);
			return true;
		}

		// If the parent has children but child nodes was not initialized and
		// the node is collapsed then just return as nodes will be initialized in
		// Expand().
		if (!parentNode->IsOpen() && parentNode->GetChildNodes().empty())
			return true;

		parentNode->SetHasChildren(true);

		ibDataViewTreeNode* itemNode = new ibDataViewTreeNode(parentNode, item);
		itemNode->SetHasChildren(item.IsContainer());

		if (GetSortOrder().IsNone())
		{
			// There's no sorting, so we need to select an insertion position

			ibDataViewItemArray modelSiblings;
			GetModel()->GetFirstFetch(parent, ibDataViewItem(), -1, modelSiblings);
			const int modelSiblingsSize = modelSiblings.size();

			// Pointer-identity search: ibDataViewItemArray::Index uses
			// operator== which after the refcount-aware refactor
			// dispatches to ibDataViewObject::IsEqualTo — for fresh
			// TabularSection rows with default values that equality
			// returns true across value-similar rows, so Index would
			// return the last value-match (often the just-inserted
			// row's wrong neighbour) instead of the actual instance
			// position.  We need INSTANCE identity here.
			int posInModel = wxNOT_FOUND;
			for (int i = modelSiblingsSize - 1; i >= 0; --i) {
				if (modelSiblings[i].GetID() == item.GetID()) {
					posInModel = i;
					break;
				}
			}
			wxCHECK_MSG(posInModel != wxNOT_FOUND, false, "adding non-existent item?");


			const ibDataViewTreeNodes& nodeSiblings = parentNode->GetChildNodes();
			const int nodeSiblingsSize = nodeSiblings.size();

			int nodePos = 0;

			if (posInModel == modelSiblingsSize - 1)
			{
				nodePos = nodeSiblingsSize;
			}
			else if (modelSiblingsSize == nodeSiblingsSize + 1)
			{
				// This is the simple case when our node tree already matches the
				// model and only this one item is missing.
				nodePos = posInModel;
			}
			else
			{
				// It's possible that a larger discrepancy between the model and
				// our realization exists. This can happen e.g. when adding a bunch
				// of items to the model and then calling ItemsAdded() just once
				// afterwards. In this case, we must find the right position by
				// looking at sibling items.

				// append to the end if we won't find a better position:
				nodePos = nodeSiblingsSize;

				for (int nextItemPos = posInModel + 1;
					nextItemPos < modelSiblingsSize;
					nextItemPos++)
				{
					int nextNodePos = parentNode->FindChildByItem(modelSiblings[nextItemPos]);
					if (nextNodePos != wxNOT_FOUND)
					{
						nodePos = nextNodePos;
						break;
					}
				}
			}
			parentNode->ChangeSubTreeCount(+1);
			parentNode->InsertChild(this, itemNode, nodePos);
		}
		else
		{
			// Node list is or will be sorted, so InsertChild do not need insertion position
			parentNode->ChangeSubTreeCount(+1);
			parentNode->InsertChild(this, itemNode, 0);
		}

		InvalidateCount();
	}

	const int newRowIdx = GetRowByItem(item);
		m_selection.OnItemsInserted(newRowIdx, 1);

	// Move focus to the just-added row — user-driven Add / Copy
	// expects to land on the new row.  Use the public Select(item)
	// helper which already does ExpandAncestors + single-sel
	// UnselectAllRows + SelectRow + ChangeCurrentRow.
	Select(item);

	InvalidateColBestWidths();

	UpdateDisplay();

	return true;
}

bool ibDataViewCtrl::ItemDeleted(const ibDataViewItem& parent,
	const ibDataViewItem& item)
{
	// Send event
	ibDataViewEvent le(wxEVT_DATAVIEW_ITEM_START_DELETING, this, GetCurrentColumn(), item);
	if (this->ProcessWindowEvent(le) || !le.IsAllowed())
		return false;

	// DB-backed paged: re-fetch on delete because the cursor (anchor
	// rows around the deleted one, m_pagedHasMore* flags) need
	// resynchronisation against SQL.  RAM-backed paged: the row is
	// already gone from m_nodeValues, falling through to the
	// non-paged remove-from-tree path is correct and avoids the
	// flicker — UNLESS the model groups, where the row's group may now
	// be empty (its header must drop) so a re-fetch is required too.
	if (GetModel() != nullptr && GetModel()->IsPagedModel()
	    && (GetModel()->HasKeyedRows() || GetModel()->IsGroupedModel())) {   // keyed DB, OR grouped RAM
		SchedulePagedRefresh();
		return true;
	}

	if (IsVirtualList())
	{
		ibDataViewVirtualListModel* list_model =
			(ibDataViewVirtualListModel*)GetModel();
		m_countRows = list_model->GetCount();

		m_selection.OnItemDelete(GetRowByItem(item));
	}
	else // general case
	{
		const FindNodeResult findResult = FindNode(parent);
		ibDataViewTreeNode* parentNode = findResult.m_node;

		// One of parents of the parent node has children but was never
		// expanded, so the tree was not built and we have nothing to delete.
		if (!findResult.m_subtreeRealized)
			return true;

		// Notice that it is possible that the item being deleted is not in the
		// tree at all, for example we could be deleting a never shown (because
		// collapsed) item in a tree model. So it's not an error if we don't know
		// about this item, just return without doing anything then.
		if (!parentNode)
			return true;

		wxCHECK_MSG(parentNode->HasChildren(), false, "parent node doesn't have children?");
		const ibDataViewTreeNodes& parentsChildren = parentNode->GetChildNodes();

		// We can't use FindNode() to find 'item', because it was already
		// removed from the model by the time ItemDeleted() is called, so we
		// have to do it manually. We keep track of its position as well for
		// later use.
		//
		// Pointer-identity compare: operator== dispatches to
		// IsEqualTo (value-based) which over default / empty
		// TabularSection rows returns true for unrelated instances —
		// the loop would stop at the first value-match instead of
		// the actual deleted row's tree node.
		int itemPosInNode = 0;
		ibDataViewTreeNode* itemNode = NULL;
		for (ibDataViewTreeNodes::const_iterator i = parentsChildren.begin();
			i != parentsChildren.end();
			++i, ++itemPosInNode)
		{
			if ((*i)->GetItem().GetID() == item.GetID())
			{
				itemNode = *i;
				break;
			}
		}

		// If the parent wasn't expanded, it's possible that we didn't have a
		// node corresponding to 'item' and so there's nothing left to do.
		if (!itemNode)
		{
			// If this was the last child to be removed, it's possible the parent
			// node became a leaf. Let's ask the model about it.
			if (parentNode->GetChildNodes().empty())
				parentNode->SetHasChildren(parent.IsContainer());

			return true;
		}

		if (m_rowHeightCache)
			m_rowHeightCache->Remove(GetRowByItem(parent) + itemPosInNode);

		// Delete the item from ibDataViewTreeNode representation:
		const int itemsDeleted = 1 + itemNode->GetSubTreeCount();

		parentNode->RemoveChild(itemPosInNode);
		delete itemNode;
		parentNode->ChangeSubTreeCount(-itemsDeleted);

		// Make the row number invalid and get a new valid one when user call GetRowCount
		InvalidateCount();

		// If this was the last child to be removed, it's possible the parent
		// node became a leaf. Let's ask the model about it.
		if (parentNode->GetChildNodes().empty())
		{
			bool isContainer = parent.IsContainer();
			parentNode->SetHasChildren(isContainer);
			if (isContainer)
			{
				// If it's still a container, make sure we show "+" icon for it
				// and not "-" one as there is nothing to collapse any more.
				if (parentNode->IsOpen())
					parentNode->ToggleOpen(this);
			}
		}

		// Update selection by removing 'item' and its entire children tree from the selection.
		if (!m_selection.IsEmpty())
		{
			// we can't call GetRowByItem() on 'item', as it's already deleted, so compute it from
			// the parent ('parentNode') and position in its list of children
			int itemRow;
			if (itemPosInNode == 0)
			{
				// 1st child, row number is that of the parent parentNode + 1
				itemRow = GetRowByItem(parentNode->GetItem()) + 1;
			}
			else
			{
				// row number is that of the sibling above 'item' + its subtree if any + 1
				const ibDataViewTreeNode* siblingNode = parentNode->GetChildNodes()[itemPosInNode - 1];

				itemRow = GetRowByItem(siblingNode->GetItem()) +
					siblingNode->GetSubTreeCount() +
					1;
			}

			m_selection.OnItemsDeleted(itemRow, itemsDeleted);

			// Move focus UP: the row above the deleted one becomes
			// active.  If we deleted the topmost (itemRow == 0), fall
			// back to the row that slid up into position 0.  Use the
			// public Select(item) helper.
			const long total = static_cast<long>(GetRowCount());
			long newCurrent = -1;
			if (total > 0) {
				newCurrent = (itemRow > 0) ? (itemRow - 1) : 0;
			}
			if (newCurrent >= 0) {
				const ibDataViewItem newItem = GetItemByRow(static_cast<unsigned>(newCurrent));
				if (newItem.IsOk()) Select(newItem);
			}
		}
	}

	// Change the current row to the last row if the current exceed the max row number
	if (HasCurrentRow() && m_currentRow >= GetRowCount())
		ChangeCurrentRow(m_countRows - 1);

	InvalidateColBestWidths();

	UpdateDisplay();

	return true;
}

bool ibDataViewCtrl::DoItemChanged(const ibDataViewItem& item, int view_column)
{
	const bool paged = (GetModel() != nullptr && GetModel()->IsPagedModel());

	// Paged path used to always SchedulePagedRefresh(item) — full tree
	// wipe + re-fetch — for any ValueChanged signal. That was the safe
	// option while GetRowByItem could mis-locate paged rows (walker
	// stopped at a value-equal sibling instead of the actual instance,
	// so RefreshRow(GetRowByItem(item)) painted the wrong row).  After
	// the walker switch to pointer-identity comparison the narrow path
	// works and the user-visible flicker on every cell edit is gone.
	//
	// `node->PutInSortOrder` and the `m_rowHeightCache->Remove` paths
	// stay non-paged-only because in paged mode the row order comes
	// from SQL ORDER BY (re-position needs a re-fetch, not a wx-tree
	// re-insert) and the row-height cache is keyed off the same paged
	// row index that GetRowByItem returns. Sort-column edits in paged
	// mode therefore land at the now-stale visual position until the
	// next reset; acceptable since most edits aren't on the sort
	// column.  Bulk repopulate paths still call ItemsAdded / Cleared
	// / BeforeReset which go through SchedulePagedRefresh as before.
	if (!paged && !IsVirtualList())
	{
		if (m_rowHeightCache)
			m_rowHeightCache->Remove(GetRowByItem(item));

		// Move this node to its new correct place after it was updated.
		//
		// In principle, we could skip the call to PutInSortOrder() if the modified
		// column is not the sort column, but in real-world applications it's fully
		// possible and likely that custom compare uses not only the selected model
		// column but also falls back to other values for comparison. To ensure
		// consistency it is better to treat a value change as if it was an item
		// change.
		const FindNodeResult findResult = FindNode(item);
		ibDataViewTreeNode* const node = findResult.m_node;
		if (!findResult.m_subtreeRealized)
			return true;
		wxCHECK_MSG(node, false, "invalid item");
		node->PutInSortOrder(this);
	}

	ibDataViewColumn* column;
	if (view_column == wxNOT_FOUND)
	{
		column = NULL;
		InvalidateColBestWidths();
	}
	else
	{
		column = GetColumn(view_column);
		InvalidateColBestWidth(column);
	}

	// Update the displayed value(s).
	RefreshRow(GetRowByItem(item));

	// Send event
	ibDataViewEvent le(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, this, column, item);
	this->ProcessWindowEvent(le);

	return true;
}

bool ibDataViewCtrl::ValueChanged(const ibDataViewItem& item, unsigned int model_column)
{
	int view_column = GetModelColumnIndex(model_column);
	if (view_column == wxNOT_FOUND)
		return false;

	return DoItemChanged(item, view_column);
}

bool ibDataViewCtrl::Cleared()
{
		// Paged path: routed through SchedulePagedRefresh so a series of
	// reset signals (BeforeReset/AfterReset → Cleared, plus following
	// ItemInserted events from bulk-mutation) coalesce into one PagedRefresh on
	// the next idle.  Refcount-aware ibDataViewItem keeps row pointers
	// the control holds alive past the model-side Clear(), so the
	// asynchronous wipe inside PagedRefresh is safe.
	if (GetModel() != nullptr && GetModel()->IsPagedModel()) {
		// Plain refresh: keep the TOP ANCHOR (viewport top), NOT the selection — passing the current row as a
		// prefer-selection would anchor the fetch on the SELECTED row and jump the viewport onto it. The bootstrap
		// re-fetches from m_pagedRestoreAnchor (>= it) so the top row stays put.
		SchedulePagedRefresh();
		return true;
	}

	DestroyTree();
	m_selection.Clear();
	m_currentRow = (unsigned)-1;

	ClearRowHeightCache();

	if (GetModel())
	{
		BuildTree(GetModel());
	}
	else
	{
		m_countRows = 0;
	}

	InvalidateColBestWidths();

	UpdateDisplay();

	return true;
}

void ibDataViewCtrl::Resort()
{
	ClearRowHeightCache();

	if (!IsVirtualList())
	{
		m_root->Resort(this);
	}

	UpdateDisplay();
}

void ibDataViewCtrl::ClearRowHeightCache()
{
	if (m_rowHeightCache)
		m_rowHeightCache->Clear();
}
void ibDataViewCtrl::UpdateDisplay()
{
	m_dirty = true;
	m_underMouse = NULL;
}

void ibDataViewCtrl::RecalculateDisplay()
{
	ibDataViewModel* model = GetModel();
	if (!model)
	{
		m_tableAreaWin->Refresh();

		if (m_tableFrozenRowAreaWin)
			m_tableFrozenRowAreaWin->Refresh();

		if (m_tableFrozenColAreaWin)
			m_tableFrozenColAreaWin->Refresh();

		if (m_tableFrozenCornerAreaWin)
			m_tableFrozenColAreaWin->Refresh();

		return;
	}

	int width = GetEndOfLastCol();
	int height = GetLineStart(GetRowCount());

	wxPoint offset = GetDataViewWindowOffset(m_tableAreaWin);
	width -= offset.x;
	height -= offset.y;

	// preserve (more or less) the previous position
	int x, y;
	GetViewStart(&x, &y);

	// ensure the position is valid for the new scroll ranges
	if (x >= width)
		x = wxMax(width - 1, 0);
	if (y >= height)
		y = wxMax(height - 1, 0);

	m_tableAreaWin->SetVirtualSize(width, height);

	SetScrollRate(FromDIP(10), GetRowHeight());
	UpdateColumnSizes();

	m_tableAreaWin->Refresh();

	if (m_tableFrozenRowAreaWin)
		m_tableFrozenRowAreaWin->Refresh();

	if (m_tableFrozenColAreaWin)
		m_tableFrozenColAreaWin->Refresh();

	if (m_tableFrozenCornerAreaWin)
		m_tableFrozenCornerAreaWin->Refresh();
}

void ibDataViewCtrl::ScrollTo(int rows, int column)
{
	m_underMouse = NULL;

	int x, y;
	GetScrollPixelsPerUnit(&x, &y);

	// Take care to not divide by 0 if we're somehow called before scrolling
	// parameters are initialized.
	int sy = y ? GetLineStart(rows) / y : -1;
	int sx = -1;

	if (column != -1 && x)
		sx = GetColumnStart(column) / x;

	Scroll(sx, sy);
}

int ibDataViewCtrl::GetEndOfLastCol() const
{
	// The width the whole column area takes — which is NOT the sum of the column
	// widths once grouping is in play: columns stacked under a vertical group share
	// one width instead of adding up. The layout knows; ask it.
	const ibDataViewColumnLayout& layout = GetColumnLayout();
	if (!layout.IsFlat())
		return layout.GetTotalWidth();

	int width = 0;
	unsigned int i;
	for (i = 0; i < GetColumnCount(); i++)
	{
		const ibDataViewColumn* c =
			GetColumn(i);

		if (!c->IsHidden())
			width += c->GetWidth();
	}
	return width;
}

int ibDataViewCtrl::GetColumnStart(int column) const
{
	wxASSERT(column >= 0);
	int sx = -1;

	wxRect rect = GetClientRect();
	int colnum = 0;
	int x_start, w = 0;
	int xx, yy, xe;

	CalcUnscrolledPosition(rect.x, rect.y, &xx, &yy);

	const ibDataViewColumnLayout& layout = GetColumnLayout();

	ibColumnPlacement place;
	if (!layout.IsFlat() && column >= 0
		&& layout.GetBodyPlacement(GetColumn((unsigned int)column), place))
	{
		// Grouped: a column's x is not the sum of the widths before it (stacked
		// columns share one), so it is read off the layout.
		x_start = place.x;
		w = place.width;
	}
	else
	{
		for (x_start = 0; colnum < column; colnum++)
		{
			ibDataViewColumn* col = GetColumn(colnum);
			if (col->IsHidden())
				continue;      // skip it!

			w = col->GetWidth();
			x_start += w;
		}
	}

	int x_end = x_start + w;
	xe = xx + rect.width;
	if (x_end > xe)
	{
		sx = (xx + x_end - xe);
	}
	if (x_start < xx)
	{
		sx = x_start;
	}
	return sx;
}

unsigned int ibDataViewCtrl::GetFirstVisibleRow() const
{
	int x = 0;
	int y = 0;

	CalcUnscrolledPosition(x, y, &x, &y);

	// `m_tableAreaWin`'s virtual size has the frozen-row offset
	// subtracted (RecalculateDisplay → SetVirtualSize), so its
	// logical y=0 corresponds to the first NON-frozen row.
	// `GetLineAt(y)` expects y in full-tree coords (row 0 = first
	// row in m_root, frozen or not), so shift by the frozen-area
	// offset before mapping y → row.  Mirrors what DrawTableContent
	// does explicitly at lines ~6812-6818 by passing `gridOffset.y`
	// into CalcDataViewWindowUnscrolledPosition.  Without this shift,
	// scrolling inside a drilled subfolder leaves topRow stuck at
	// row 0 (the topmost crumb) and DerivePagedThumb's dataBehind
	// branch never fires.
	if (m_tableAreaWin) {
		const wxPoint offset = GetDataViewWindowOffset(m_tableAreaWin);
		y += offset.y;
	}

	return GetLineAt(y);
}

unsigned int ibDataViewCtrl::GetLastVisibleRow()
{
	wxSize client_size = GetClientSize();

	// Find row occupying the bottom line of the client area (dimY-1).
	CalcUnscrolledPosition(client_size.x, client_size.y - 1,
		&client_size.x, &client_size.y);

	unsigned int row = GetLineAt(client_size.y);

	return wxMin(GetRowCount() - 1, row);
}

unsigned int ibDataViewCtrl::GetLastFullyVisibleRow()
{
	unsigned int row = GetLastVisibleRow();

	int bottom = GetLineStart(row) + GetLineHeight(row);
	CalcScrolledPosition(-1, bottom, NULL, &bottom);

	if (bottom > GetClientSize().y)
		return wxMax(0, row - 1);
	else
		return row;
}

unsigned int ibDataViewCtrl::GetRowCount() const
{
	if (m_countRows == -1)
	{
		ibDataViewCtrl* const
			self = const_cast<ibDataViewCtrl*>(this);

		self->UpdateCount(RecalculateCount());
		self->UpdateDisplay();
	}

	return m_countRows;
}

void ibDataViewCtrl::ChangeCurrentRow(unsigned int row)
{
	m_currentRow = row;

	// send event
#if wxUSE_ACCESSIBILITY
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_FOCUS, this, wxOBJID_CLIENT, m_currentRow + 1);
#endif // wxUSE_ACCESSIBILITY
}

bool ibDataViewCtrl::UnselectAllRows(unsigned int except)
{
	if (!m_selection.IsEmpty())
	{
				for (unsigned i = GetFirstVisibleRow(); i <= GetLastVisibleRow(); i++)
		{
			if (m_selection.IsSelected(i) && i != except)
				RefreshRow(i);
		}

		if (except != (unsigned int)-1)
		{
			const bool wasSelected = m_selection.IsSelected(except);
			ClearSelection();
			if (wasSelected)
			{
				m_selection.SelectItem(except);

				// The special item is still selected.
				return false;
			}
		}
		else
		{
			ClearSelection();
		}
	}

	// There are no selected items left.
	return true;
}

void ibDataViewCtrl::ClearSelection()
{
		m_selection.SelectRange(0, GetRowCount() - 1, false);
}

void ibDataViewCtrl::SelectRow(unsigned int row, bool on)
{
		if (m_selection.SelectItem(row, on))
		RefreshRow(row);
}

void ibDataViewCtrl::SelectRows(unsigned int from, unsigned int to)
{
		wxArrayInt changed;
	if (m_selection.SelectRange(from, to, true, &changed))
	{
		for (unsigned i = 0; i < changed.size(); i++)
			RefreshRow(changed[i]);
	}
	else // Selection of too many rows has changed.
	{
		RefreshRows(from, to);
	}
}

void ibDataViewCtrl::Select(const wxArrayInt& aSelections)
{
	for (size_t i = 0; i < aSelections.GetCount(); i++)
	{
		int n = aSelections[i];

		if (m_selection.SelectItem(n))
			RefreshRow(n);
	}
}

void ibDataViewCtrl::ReverseRowSelection(unsigned int row)
{
	m_selection.SelectItem(row, !m_selection.IsSelected(row));
	RefreshRow(row);
}

bool ibDataViewCtrl::IsRowSelected(unsigned int row) const
{
	return m_selection.IsSelected(row);
}

void ibDataViewCtrl::SendSelectionChangedEvent(const ibDataViewItem& item)
{
#if wxUSE_ACCESSIBILITY
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_SELECTIONWITHIN, this, wxOBJID_CLIENT, wxACC_SELF);
#endif // wxUSE_ACCESSIBILITY

	ibDataViewEvent le(wxEVT_DATAVIEW_SELECTION_CHANGED, this, item);
	ProcessWindowEvent(le);
}

void ibDataViewCtrl::RefreshRows(unsigned int from, unsigned int to)
{
	wxRect rect = GetLinesRect(from, to);

	CalcScrolledPosition(rect.x, rect.y, &rect.x, &rect.y);

	wxSize client_size = GetClientSize();
	wxRect client_rect(0, 0, client_size.x, client_size.y);
	wxRect intersect_rect = client_rect.Intersect(rect);

	if (!intersect_rect.IsEmpty())
	{
		// Copy rectangle can get scroll offsets..
		int rect_x = intersect_rect.GetX();
		int rect_y = intersect_rect.GetY();

		int rectWidth = intersect_rect.GetWidth();
		int rectHeight = intersect_rect.GetHeight();

		if (m_tableAreaWin)
		{
			wxRect anotherrect(rect_x, rect_y, rectWidth, rectHeight);
			anotherrect.Offset(GetDataViewWindowOffset(m_tableAreaWin));
			m_tableAreaWin->Refresh(true, &anotherrect);
		}

		if (m_tableFrozenRowAreaWin) {
			wxRect anotherrect(rect_x, rect_y, rectWidth, rectHeight);
			anotherrect.Offset(GetDataViewWindowOffset(m_tableFrozenRowAreaWin));
			m_tableFrozenRowAreaWin->Refresh(true, &anotherrect);
		}

		if (m_tableFrozenColAreaWin) {
			wxRect anotherrect(rect_x, rect_y, rectWidth, rectHeight);
			anotherrect.Offset(GetDataViewWindowOffset(m_tableFrozenColAreaWin));
			m_tableFrozenColAreaWin->Refresh(true, &anotherrect);
		}

		if (m_tableFrozenCornerAreaWin) {
			wxRect anotherrect(rect_x, rect_y, rectWidth, rectHeight);
			anotherrect.Offset(GetDataViewWindowOffset(m_tableFrozenCornerAreaWin));
			m_tableFrozenCornerAreaWin->Refresh(true, &anotherrect);
		}
	}
}

void ibDataViewCtrl::RefreshRowsAfter(unsigned int firstRow)
{
	wxSize client_size = GetClientSize();
	int start = GetLineStart(firstRow);
	CalcScrolledPosition(start, 0, &start, NULL);
	if (start > client_size.y) return;

	wxRect rect(0, start, client_size.x, client_size.y - start);

	if (m_tableAreaWin)
	{
		wxRect anotherrect(rect);
		anotherrect.Offset(GetDataViewWindowOffset(m_tableAreaWin));
		m_tableAreaWin->Refresh(true, &anotherrect);
	}

	if (m_tableFrozenRowAreaWin) {
		wxRect anotherrect(rect);
		anotherrect.Offset(GetDataViewWindowOffset(m_tableFrozenRowAreaWin));
		m_tableFrozenRowAreaWin->Refresh(true, &anotherrect);
	}

	if (m_tableFrozenColAreaWin) {
		wxRect anotherrect(rect);
		anotherrect.Offset(GetDataViewWindowOffset(m_tableFrozenColAreaWin));
		m_tableFrozenColAreaWin->Refresh(true, &anotherrect);
	}

	if (m_tableFrozenCornerAreaWin) {
		wxRect anotherrect(rect);
		anotherrect.Offset(GetDataViewWindowOffset(m_tableFrozenCornerAreaWin));
		m_tableFrozenCornerAreaWin->Refresh(true, &anotherrect);
	}
}

wxRect ibDataViewCtrl::GetLinesRect(unsigned int rowFrom, unsigned int rowTo) const
{
	if (rowFrom > rowTo)
		wxSwap(rowFrom, rowTo);

	wxRect rect;
	rect.x = 0;
	rect.y = GetLineStart(rowFrom);
	// Don't calculate exact width of the row, because GetEndOfLastCol() is
	// expensive to call, and controls with rows not spanning entire width rare.
	// It is more efficient to e.g. repaint empty parts of the window needlessly.
	rect.width = INT_MAX;
	if (rowFrom == rowTo)
		rect.height = GetLineHeight(rowFrom);
	else
		rect.height = GetLineStart(rowTo) - rect.y + GetLineHeight(rowTo);

	return rect;
}

int ibDataViewCtrl::GetLineStart(unsigned int row) const
{
	// check for the easy case first
	if (!m_rowHeightCache || !HasFlag(wxDV_VARIABLE_LINE_HEIGHT))
		return row * m_lineHeight;

	int start = 0;
	if (m_rowHeightCache->GetLineStart(row, start))
		return start;

	unsigned int r;
	for (r = 0; r < row; r++)
	{
		int height = 0;
		if (!m_rowHeightCache->GetLineHeight(r, height))
		{
			// row height not in cache -> get it from the renderer...
			ibDataViewItem item = GetItemByRow(r);
			if (!item)
				break;

			height = QueryAndCacheLineHeight(r, item);
		}

		start += height;
	}

	return start;
}

int ibDataViewCtrl::GetLineAt(unsigned int y) const
{
	// check for the easy case first
	if (!m_rowHeightCache || !HasFlag(wxDV_VARIABLE_LINE_HEIGHT))
		return y / m_lineHeight;

	unsigned int row = 0;
	if (m_rowHeightCache->GetLineAt(y, row))
		return row;

	// OnPaint asks GetLineAt for the very last y position and this is always
	// below the last item (--> an invalid item). To prevent iterating over all
	// items, check if y is below the last row.
	// Because this is done very often (for each repaint) its worth to handle
	// this special case separately.
	int height = 0;
	int start = 0;
	unsigned int rowCount = GetRowCount();
	if (rowCount == 0 ||
		(m_rowHeightCache->GetLineInfo(rowCount - 1, start, height) &&
			y >= static_cast<unsigned int>(start + height)))
	{
		return rowCount;
	}

	// sum all item heights until y is reached
	unsigned int yy = 0;
	for (;;)
	{
		height = 0;
		if (!m_rowHeightCache->GetLineHeight(row, height))
		{
			// row height not in cache -> get it from the renderer...
			ibDataViewItem item = GetItemByRow(row);
			if (!item)
			{
				wxASSERT(row >= GetRowCount());
				break;
			}

			height = QueryAndCacheLineHeight(row, item);
		}

		yy += height;
		if (y < yy)
			break;

		row++;
	}

	return row;
}

int ibDataViewCtrl::GetLineHeight(unsigned int row) const
{
	// check for the easy case first
	if (!m_rowHeightCache || !HasFlag(wxDV_VARIABLE_LINE_HEIGHT))
		return m_lineHeight;

	int height = 0;
	if (m_rowHeightCache->GetLineHeight(row, height))
		return height;

	ibDataViewItem item = GetItemByRow(row);
	if (!item)
		return m_lineHeight;

	height = QueryAndCacheLineHeight(row, item);
	return height;
}

int ibDataViewCtrl::QueryAndCacheLineHeight(unsigned int row, ibDataViewItem item) const
{
	const ibDataViewModel* model = GetModel();
	int height = m_lineHeight;
	unsigned int cols = GetColumnCount();
	unsigned int col;
	for (col = 0; col < cols; col++)
	{
		const ibDataViewColumn* column = GetColumn(col);
		if (column->IsHidden())
			continue;      // skip it!

		if (!model->HasValue(item, col))
			continue;      // skip it!

		ibDataViewRenderer* renderer =
			const_cast<ibDataViewRenderer*>(column->GetRenderer());
		if (renderer->PrepareForItem(model, item, column->GetModelColumn()))
			height = wxMax(height, renderer->GetSize().y);
	}

	// ... and store the height in the cache
	m_rowHeightCache->Put(row, height);

	return height;
}

namespace
{
	class RowToTreeNodeJob : public DoJob
	{
	public:
		// Note that we initialize m_current to -1 because the first node passed to
		// our operator() will be the root node, which doesn't appear in the window
		// and so doesn't count as a real row.
		explicit RowToTreeNodeJob(int row)
			: m_row(row), m_current(-1), m_ret(NULL)
		{
		}

		virtual int operator() (ibDataViewTreeNode* node) wxOVERRIDE
		{
			if (m_current == m_row)
			{
				m_ret = node;
				return DoJob::DONE;
			}

			if (node->GetSubTreeCount() + m_current < m_row)
			{
				m_current += node->GetSubTreeCount() + 1;
				return  DoJob::SKIP_SUBTREE;
			}
			else
			{
				// If the current node has only leaf children, we can find the
				// desired node directly. This can speed up finding the node
				// in some cases, and will have a very good effect for list views.
				if (node->HasChildren() &&
					(int)node->GetChildNodes().size() == node->GetSubTreeCount())
				{
					const int index = m_row - m_current - 1;
					m_ret = node->GetChildNodes()[index];
					return DoJob::DONE;
				}

				m_current++;

				return DoJob::CONTINUE;
			}
		}

		ibDataViewTreeNode* GetResult() const
		{
			return m_ret;
		}

	private:
		const int m_row;
		int m_current;
		ibDataViewTreeNode* m_ret;
	};
}

// anonymous namespace
//
ibDataViewTreeNode* ibDataViewCtrl::GetTreeNodeByRow(unsigned int row) const
{
	wxASSERT(!IsVirtualList());

	if (row == (unsigned)-1)
		return NULL;

	RowToTreeNodeJob job(static_cast<int>(row));
	Walker(m_root, job);
	return job.GetResult();
}

ibDataViewItem ibDataViewCtrl::GetItemByRow(unsigned int row) const
{
	ibDataViewItem item;
	if (IsVirtualList())
	{
		if (row < GetRowCount())
			item = ibDataViewItem(wxUIntToPtr(row + 1));
	}
	else
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(row);
		if (node)
			item = node->GetItem();
	}

	return item;
}

bool
ibDataViewCtrl::SendExpanderEvent(wxEventType type,
	const ibDataViewItem& item)
{
#if wxUSE_ACCESSIBILITY
	if (type == wxEVT_DATAVIEW_ITEM_EXPANDED || type == wxEVT_DATAVIEW_ITEM_COLLAPSED)
		wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_REORDER, this, wxOBJID_CLIENT, wxACC_SELF);
#endif // wxUSE_ACCESSIBILITY

	ibDataViewEvent le(type, this, item);
	return !ProcessWindowEvent(le) || le.IsAllowed();
}

bool ibDataViewCtrl::IsExpandedRow(unsigned int row) const
{
	if (IsList())
		return false;

	ibDataViewTreeNode* node = GetTreeNodeByRow(row);
	if (!node)
		return false;

	if (!node->HasChildren())
		return false;

	return node->IsOpen();
}

bool ibDataViewCtrl::HasChildrenRow(unsigned int row) const
{
	if (IsList())
		return false;

	ibDataViewTreeNode* node = GetTreeNodeByRow(row);
	if (!node)
		return false;

	if (!node->HasChildren())
		return false;

	return true;
}

void ibDataViewCtrl::ExpandRow(unsigned int row, bool expandChildren)
{
	if (IsList()) {
				return;
	}

	ibDataViewTreeNode* node = GetTreeNodeByRow(row);
	if (!node) {
				return;
	}

		DoExpand(node, row, expandChildren);
	}

void
ibDataViewCtrl::DoExpand(ibDataViewTreeNode* node,
	unsigned int row,
	bool expandChildren)
{
	if (!node->HasChildren())
		return;


	if (!node->IsOpen())
	{
		if (!SendExpanderEvent(wxEVT_DATAVIEW_ITEM_EXPANDING, node->GetItem()))
		{
			// Vetoed by the event handler.
						return;
		}

		if (m_rowHeightCache)
		{
			// Expand makes new rows visible thus we invalidates all following
			// rows in the height cache
			m_rowHeightCache->Remove(row);
		}

		node->ToggleOpen(this);

		// build the children of current node
		if (node->GetChildNodes().empty())
		{
			::BuildTreeHelper(this, GetModel(), node->GetItem(), node);
		}

		const unsigned countNewRows = node->GetSubTreeCount();

		// Shift all stored indices after this row by the number of newly added
		// rows.
		m_selection.OnItemsInserted(row + 1, countNewRows);
		if (HasCurrentRow() && m_currentRow > row)
			ChangeCurrentRow(m_currentRow + countNewRows);

		if (m_countRows != -1)
			m_countRows += countNewRows;

		// Expanding this item means the previously cached column widths could
		// have become invalid as new items are now visible.
		InvalidateColBestWidths();

		UpdateDisplay();

		// Send the expanded event
		SendExpanderEvent(wxEVT_DATAVIEW_ITEM_EXPANDED, node->GetItem());
	}

	// Note that we have to expand the children when expanding recursively even
	// when this node itself was already open.
	if (expandChildren)
	{
		const ibDataViewTreeNodes& children = node->GetChildNodes();

		for (ibDataViewTreeNodes::const_iterator i = children.begin();
			i != children.end();
			++i)
		{
			ibDataViewTreeNode* const child = *i;

			// Row currently corresponds to the previous item, so increment it
			// first to correspond to this child.
			DoExpand(child, ++row, true);

			// We don't need +1 here because we'll increment the row during the
			// next loop iteration.
			row += child->GetSubTreeCount();
		}
	}
}

void ibDataViewCtrl::CollapseRow(unsigned int row)
{
	if (IsList())
		return;

	ibDataViewTreeNode* node = GetTreeNodeByRow(row);
	if (!node)
		return;

	
	if (!node->HasChildren())
		return;

	if (m_rowHeightCache)
	{
		// Collapse hides rows thus we invalidates all following
		// rows in the height cache
		m_rowHeightCache->Remove(row);
	}

	if (node->IsOpen())
	{
		if (!SendExpanderEvent(wxEVT_DATAVIEW_ITEM_COLLAPSING, node->GetItem()))
		{
			// Vetoed by the event handler.
			return;
		}

		const unsigned countDeletedRows = node->GetSubTreeCount();

		if (m_selection.OnItemsDeleted(row + 1, countDeletedRows))
		{
			SendSelectionChangedEvent(GetItemByRow(row));

			// The event handler for wxEVT_DATAVIEW_SELECTION_CHANGED could
			// have called Collapse() itself, in which case the node would be
			// already closed and we shouldn't try to close it again.
			if (!node->IsOpen())
				return;
		}

		node->ToggleOpen(this);

		// Adjust the current row if necessary.
		if (HasCurrentRow() && m_currentRow > row)
		{
			// If the current row was among the collapsed items, make the
			// parent itself current.
			if (m_currentRow <= row + countDeletedRows)
				ChangeCurrentRow(row);
			else // Otherwise just update the index.
				ChangeCurrentRow(m_currentRow - countDeletedRows);
		}

		if (m_countRows != -1)
			m_countRows -= countDeletedRows;

		InvalidateColBestWidths();

		UpdateDisplay();

		SendExpanderEvent(wxEVT_DATAVIEW_ITEM_COLLAPSED, node->GetItem());
	}
}

ibDataViewCtrl::FindNodeResult
ibDataViewCtrl::FindNode(const ibDataViewItem& item)
{
	FindNodeResult result;
	result.m_node = NULL;
	result.m_subtreeRealized = true;

	const ibDataViewModel* model = GetModel();
	if (model == NULL)
		return result;

	if (!item.IsOk())
	{
		result.m_node = m_root;
		return result;
	}

	// Compose the parent-chain for the item we are looking for
	wxVector<ibDataViewItem> parentChain;
	ibDataViewItem it(item);
	while (it.IsOk())
	{
		parentChain.push_back(it);
		it = it.GetParentItem();
	}

	// Find the item along the parent-chain.
	// This algorithm is designed to speed up the node-finding method
	ibDataViewTreeNode* node = m_root;
	for (unsigned iter = parentChain.size() - 1; ; --iter)
	{
		if (node->HasChildren())
		{
			if (node->GetChildNodes().empty())
			{
				// Even though the item is a container, it doesn't have any
				// child nodes in the control's representation yet.
				result.m_subtreeRealized = false;
				return result;
			}

			const ibDataViewTreeNodes& nodes = node->GetChildNodes();
			bool found = false;

			// Pointer-identity walk — see the same caveat in
			// ItemToRowJob / FindChildByItem.
			for (unsigned i = 0; i < nodes.size(); ++i)
			{
				ibDataViewTreeNode* currentNode = nodes[i];
				if (currentNode->GetItem().GetID() == parentChain[iter].GetID())
				{
					if (currentNode->GetItem().GetID() == item.GetID())
					{
						result.m_node = currentNode;
						return result;
					}

					node = currentNode;
					found = true;
					break;
				}
			}
			if (!found)
				return result;
		}
		else
			return result;

		if (!iter)
			break;
	}
	return result;
}

void ibDataViewCtrl::HitTest(const wxPoint& point, ibDataViewItem& item,
	ibDataViewColumn*& column)
{
	ibDataViewColumn* col = NULL;
	unsigned int cols = GetColumnCount();
	unsigned int colnum = 0;
	int x, y;

	CalcUnscrolledPosition(point.x, point.y, &x, &y);

	const unsigned int row = GetLineAt(y);
	const ibDataViewColumnLayout& layout = GetColumnLayout();

	if (layout.IsFlat())
	{
		for (unsigned x_start = 0; colnum < cols; colnum++)
		{
			col = GetColumn(colnum);
			if (col->IsHidden())
				continue;      // skip it!

			unsigned int w = col->GetWidth();
			if (x_start + w >= (unsigned int)x)
				break;

			x_start += w;
		}
	}
	else
	{
		// Grouped: the point picks a cell, not a stripe — WHICH BAND of the row it
		// falls in decides between columns stacked at the same x.
		const int lineStart = GetLineStart(row);
		const int lineHeight = wxMax(GetLineHeight(row), 1);
		const int bands = wxMax(layout.GetRowBandCount(), 1);
		const int band = wxMin(wxMax((y - lineStart) * bands / lineHeight, 0), bands - 1);

		col = layout.FindColumnAt(x, band);

		// Past the right edge the old behaviour is to answer with the last column
		// rather than nothing — several callers rely on a non-null column.
		if (col == nullptr)
		{
			for (unsigned int pos = 0; pos < cols; pos++)
			{
				ibDataViewColumn* candidate = GetColumn(pos);
				if (candidate != nullptr && !candidate->IsHidden())
					col = candidate;
			}
		}
	}

	column = col;
	item = GetItemByRow(row);
}

wxRect ibDataViewCtrl::GetItemRect(const ibDataViewItem& item,
	const ibDataViewColumn* column)
{
	int xpos = 0;
	int width = 0;
	// Which band of the row the cell occupies — the whole row unless the column is
	// stacked under a vertical group. Filled in from the layout below.
	int bandFirst = 0;
	int bandSpan = 0;

	unsigned int cols = GetColumnCount();
	const ibDataViewColumnLayout& layout = GetColumnLayout();

	// If column is null the loop will compute the combined width of all columns.
	// Otherwise, it will compute the x position of the column we are looking for.
	for (unsigned int i = 0; i < cols; i++)
	{
		ibDataViewColumn* col = GetColumn(i);

		if (col == column)
		{
			ibColumnPlacement place;
			if (layout.GetBodyPlacement(col, place))
			{
				xpos = place.x;
				bandFirst = place.band;
				bandSpan = place.bandSpan;
			}
			break;
		}

		if (col->IsHidden())
			continue;      // skip it!

		xpos += col->GetWidth();
		width += col->GetWidth();
	}

	if (column != 0)
	{
		// If we have a column, we need can get its width directly.
		if (column->IsHidden())
			width = 0;
		else
			width = column->GetWidth();

	}
	else
	{
		// If we have no column, we reset the x position back to zero.
		xpos = 0;
		width = layout.GetTotalWidth() > 0 ? layout.GetTotalWidth() : width;
	}

	const int row = GetRowByItem(item, Walk_ExpandedOnly);
	if (row == -1)
	{
		// This means the row is currently not visible at all.
		return wxRect();
	}

	// we have to take an expander column into account and compute its indentation
	// to get the correct x position where the actual text is
	int indent = 0;
	if (!IsList() && m_viewMode == ibDataViewViewMode::ibDataViewTree &&
		(column == 0 || GetExpanderColumnOrFirstOne(this) == column))
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(row);
		indent = GetIndent() * node->GetIndentLevel();
		indent += wxRendererNative::Get().GetExpanderSize(this).GetWidth();
	}

	const int lineStart  = GetLineStart(row);
	const int lineHeight = GetLineHeight(row);

	// The cell's own slice of the row. Without a band (no column asked for, or a
	// flat grid) this is the row itself — the old rectangle, unchanged.
	int cellTop = lineStart;
	int cellHeight = lineHeight;
	if (bandSpan > 0)
	{
		const int bands = wxMax(layout.GetRowBandCount(), 1);
		const int top = (lineHeight * bandFirst) / bands;
		const int bottom = (lineHeight * (bandFirst + bandSpan)) / bands;
		cellTop = lineStart + top;
		cellHeight = bottom - top;
	}

	wxRect itemRect(xpos + indent, cellTop, width - indent, cellHeight);

	ibDataViewMainWindow* tableWindow = CellToDataViewWindow(item, column);
	const wxPoint winOffset = GetDataViewWindowOffset(tableWindow);
	itemRect.Offset(-winOffset);

	const wxPoint preScroll = itemRect.GetPosition();
	// convert to scrolled coords
	CalcDataViewWindowScrolledPosition(itemRect.x, itemRect.y,
		&itemRect.x, &itemRect.y, tableWindow);

	const char* winType =
		tableWindow == m_tableAreaWin              ? "data" :
		tableWindow == m_tableFrozenRowAreaWin     ? "frozenRow" :
		tableWindow == m_tableFrozenColAreaWin     ? "frozenCol" :
		tableWindow == m_tableFrozenCornerAreaWin  ? "corner" : "?";
	const int clientH = GetClientSize().y;
	const bool offscreen = (itemRect.GetBottom() < 0 || itemRect.GetTop() > clientH);
	
	// Check if the rectangle is completely outside of the currently visible
	// area and, if so, return an empty rectangle to indicate that the item is
	// not visible.
	if (offscreen)
	{
		return wxRect();
	}

	return itemRect;
}

int ibDataViewCtrl::RecalculateCount() const
{
	if (IsVirtualList())
	{
		const ibDataViewVirtualListModel* list_model =
			static_cast<const ibDataViewVirtualListModel*>(GetModel());

		return list_model->GetCount();
	}
	else
	{
		return m_root->GetSubTreeCount();
	}
}

namespace
{

	class ItemToRowJob : public DoJob
	{
	public:
		// As with RowToTreeNodeJob above, we initialize m_current to -1 because
		// the first node passed to our operator() is the root node which is not
		// visible on screen and so we should return 0 for its first child node and
		// not for the root itself.
		ItemToRowJob(const ibDataViewItem& item, wxVector<ibDataViewItem>::reverse_iterator iter)
			: m_item(item), m_iter(iter), m_current(-1)
		{
		}

		// Maybe binary search will help to speed up this process
		virtual int operator() (ibDataViewTreeNode* node) wxOVERRIDE
		{
			// Compare by pointer identity (GetID), NOT by value-equality
			// (operator==).  After the refcount-aware ibDataViewItem
			// refactor, operator== dispatches to ibDataViewObject::
			// IsEqualTo, which on ibComposerNode compares m_nodeValues.
			// For TabularSection rows that share defaults / empty values
			// across multiple rows that's a false positive — the walker
			// would stop at the first row with matching values instead
			// of the actual instance.  We need INSTANCE identity here:
			// "find this row pointer in the tree", not "find any row
			// that looks the same".
			if (node->GetItem().GetID() == m_item.GetID())
			{
				return DoJob::DONE;
			}

			// Is this node the next (grand)parent of the item we're looking for?
			if (node->GetItem().GetID() == m_iter->GetID())
			{
				// Search for the next (grand)parent now and skip this item itself.
				++m_iter;
				++m_current;
				return DoJob::CONTINUE;
			}
			else
			{
				// Skip this node and all its currently visible children.
				m_current += node->GetSubTreeCount() + 1;
				return DoJob::SKIP_SUBTREE;
			}

		}

		int GetResult() const
		{
			return m_current;
		}

	private:
		const ibDataViewItem m_item;
		wxVector<ibDataViewItem>::reverse_iterator m_iter;

		// The row corresponding to the last node seen in our operator().
		int m_current;

	};

} // anonymous namespace

int
ibDataViewCtrl::GetRowByItem(const ibDataViewItem& item,
	WalkFlags flags) const
{
	const ibDataViewModel* model = GetModel();
	if (model == NULL)
		return -1;

	if (IsVirtualList())
	{
		return wxPtrToUInt(item.GetID()) - 1;
	}
	else
	{
		if (!item.IsOk())
			return -1;

		// Compose the parent-chain of the item we are looking for
		wxVector<ibDataViewItem> parentChain;
		ibDataViewItem it(item);
		while (it.IsOk())
		{
			parentChain.push_back(it);
			it = it.GetParentItem();
		}

		// add an 'invalid' item to represent our 'invisible' root node
		parentChain.push_back(ibDataViewItem());

		// the parent chain was created by adding the deepest parent first.
		// so if we want to start at the root node, we have to iterate backwards through the vector
		ItemToRowJob job(item, parentChain.rbegin());
		if (!Walker(m_root, job, flags))
			return -1;

		return job.GetResult();
	}
}

static void BuildHierarchicalHelper(ibDataViewCtrl* window, const ibDataViewModel* model,
	ibDataViewTreeNode* node)
{
	ibDataViewItemArray children;

	ibDataViewItem item  = window->GetDrillHierarchyItem();
	ibDataViewItem child = item;

	while (child.IsOk())
	{
		children.Add(child);
		child = child.GetParentItem();
	}

	ibDataViewTreeNode* parent = node;

	for (unsigned int num = children.Count(); num > 0; num--)
	{
		ibDataViewTreeNode* node = new ibDataViewTreeNode(parent, children[num - 1], ibDataViewTreeNodeViewMode::ibDataViewHierarchy);

		{
			node->SetHasChildren(true);
			node->ToggleOpen(window);
		}

		parent->InsertChild(window, node, 0);

		if (parent->IsOpen())
			parent->ChangeSubTreeCount(1);

		parent = node;
	}

	BuildTreeHelper(window, model,
		item, parent);
}

static void BuildTreeHelper(ibDataViewCtrl* window, const ibDataViewModel* model,
	const ibDataViewItem& item, ibDataViewTreeNode* node)
{
	// Skip only real leaf items — the invisible root passed in by
	// BuildTree() is an empty (Mode::Empty) ibDataViewItem whose
	// IsContainer() returns false unconditionally, so the bare
	// `!item.IsContainer()` check used to bail out before fetching
	// the top-level rows. That left Tree-mode controls empty after
	// Cleared() / AssociateModel re-fires (paths that pass through
	// here with the empty root). Treat the empty root as a container.
	if (item.IsOk() && !item.IsContainer())
		return;

	ibDataViewItemArray children;
	unsigned int num = model->GetFirstFetch(item, ibDataViewItem(), 0, children);

	for (unsigned int index = 0; index < num; index++)
	{
		ibDataViewTreeNode* n = new ibDataViewTreeNode(node, children[index]);

		if (children[index].IsContainer())
			n->SetHasChildren(true);

		node->InsertChild(window, n, index);
	}

	if (node->IsOpen())
		node->ChangeSubTreeCount(+num);
}

void BuildListHelper(ibDataViewCtrl* window, const ibDataViewModel* model,
	const ibDataViewItem& item, ibDataViewTreeNode* node)
{
	ibDataViewItemArray children;
	unsigned int num = model->GetFirstFetch(item, ibDataViewItem(), 0, children);

	for (unsigned int index = 0; index < num; index++)
	{
		ibDataViewTreeNode* n = new ibDataViewTreeNode(node, children[index]);

		node->InsertChild(window, n, index);

		BuildListHelper(window, model, children[index], node);
	}

	if (node->IsOpen())
		node->ChangeSubTreeCount(+num);
}

void ibDataViewCtrl::BuildTree(ibDataViewModel* model)
{
	DestroyTree();

	if (GetModel()->IsVirtualListModel())
	{
		InvalidateCount();
		return;
	}

	m_root = ibDataViewTreeNode::CreateRootNode();

	if (m_viewMode == ibDataViewHierarchical)
	{
		BuildHierarchicalHelper(this, model, m_root);
	}
	else if (m_viewMode == ibDataViewList)
	{
		// First we define a invalid item to fetch the top-level elements
		ibDataViewItem item;
		BuildListHelper(this, model, item, m_root);
	}
	else if (m_viewMode == ibDataViewTree)
	{
		// First we define a invalid item to fetch the top-level elements
		ibDataViewItem item;
		BuildTreeHelper(this, model, item, m_root);
	}

	InvalidateCount();
}


void ibDataViewCtrl::DestroyTree()
{
	if (!IsVirtualList())
	{
		wxDELETE(m_root);
		m_countRows = 0;
	}
}

ibDataViewColumn*
ibDataViewCtrl::FindColumnForEditing(const ibDataViewItem& item, ibDataViewCellMode mode) const
{
	// Edit the current column editable in 'mode'. If no column is focused
	// (typically because the user has full row selected), try to find the
	// first editable column (this would typically be a checkbox for
	// wxDATAVIEW_CELL_ACTIVATABLE and we don't want to force the user to set
	// focus on the checkbox column; or on the only editable text column).

	ibDataViewColumn* candidate = m_currentCol;

	if (candidate && !IsCellEditableInMode(item, candidate, mode))
	{
		if (m_currentColSetByKeyboard)
		{
			// If current column was set by keyboard to something not editable (in
			// 'mode') and the user pressed Space/F2 then do not edit anything
			// because focus is visually on that column and editing
			// something else would be surprising.
			return NULL;
		}
		else
		{
			// But if the current column was set by mouse to something not editable (in
			// 'mode') and the user pressed Space/F2 to edit it, treat the
			// situation as if there was whole-row focus, because that's what is
			// visually indicated and the mouse click could very well be targeted
			// on the row rather than on an individual cell.
			candidate = NULL;
		}
	}

	if (!candidate)
	{
		const unsigned cols = GetColumnCount();
		for (unsigned i = 0; i < cols; i++)
		{
			ibDataViewColumn* c = GetColumn(i);
			if (c->IsHidden())
				continue;

			if (IsCellEditableInMode(item, c, mode))
			{
				candidate = c;
				break;
			}
		}
	}

	// Switch to the first column with value if the current column has no value
	if (candidate && !GetModel()->HasValue(item, candidate->GetModelColumn()))
		candidate = FindFirstColumnWithValue(item);

	if (!candidate)
		return NULL;

	if (!IsCellEditableInMode(item, candidate, mode))
		return NULL;

	return candidate;
}

bool ibDataViewCtrl::IsCellEditableInMode(const ibDataViewItem& item,
	const ibDataViewColumn* col,
	ibDataViewCellMode mode) const
{
	if (col->GetRenderer()->GetMode() != mode)
		return false;

	if (!GetModel()->IsEnabled(item, col->GetModelColumn()))
		return false;

	if (!GetModel()->HasValue(item, col->GetModelColumn()))
		return false;

	return true;
}

void ibDataViewCtrl::GoToRelativeRow(const wxKeyboardState& kbdState, int delta)
{
	// if there is no selection, we cannot move it anywhere
	if (!HasCurrentRow() || IsEmpty())
		return;

	int newRow = (int)m_currentRow + delta;

	// let's keep the new row inside the allowed range
	if (newRow < 0)
		newRow = 0;

	const int rowCount = (int)GetRowCount();
	if (newRow >= rowCount)
		newRow = rowCount - 1;

	GoToRow(kbdState, newRow);
}

void
ibDataViewCtrl::GoToRow(const wxKeyboardState& kbdState,
	unsigned int newCurrent)
{
	unsigned int oldCurrent = m_currentRow;

	if (newCurrent == oldCurrent)
		return;

	// in single selection we just ignore Shift as we can't select several
	// items anyhow
	if (kbdState.ShiftDown() && !IsSingleSel())
	{
		RefreshRow(oldCurrent);

		ChangeCurrentRow(newCurrent);

		// select all the items between the old and the new one
		if (oldCurrent > newCurrent)
		{
			newCurrent = oldCurrent;
			oldCurrent = m_currentRow;
		}

		SelectRows(oldCurrent, newCurrent);

		wxSelectionStore::IterationState cookie;
		const unsigned firstSel = m_selection.GetFirstSelectedItem(cookie);
		if (firstSel != wxSelectionStore::NO_SELECTION)
			SendSelectionChangedEvent(GetItemByRow(firstSel));
	}
	else // !shift
	{
		RefreshRow(oldCurrent);

		// all previously selected items are unselected unless ctrl is held
		if (!kbdState.ControlDown())
			UnselectAllRows();

		ChangeCurrentRow(newCurrent);

		if (!kbdState.ControlDown())
		{
			SelectRow(m_currentRow, true);
			SendSelectionChangedEvent(GetItemByRow(m_currentRow));
		}
		else
			RefreshRow(m_currentRow);
	}

	EnsureVisibleRowCol(m_currentRow, -1);
}

bool ibDataViewCtrl::TryAdvanceCurrentColumn(ibDataViewTreeNode* node, wxKeyEvent& event, bool forward)
{
	if (GetColumnCount() == 0)
		return false;

	if (!m_useCellFocus)
		return false;

	const bool wrapAround = event.GetKeyCode() == WXK_TAB;

	// navigation shouldn't work in nodes with fewer than two columns
	if (node && IsItemSingleValued(node->GetItem()))
		return false;

	if (m_currentCol == NULL || !m_currentColSetByKeyboard)
	{
		if (forward)
		{
			if (node)
			{
				// find first column with value
				m_currentCol = FindFirstColumnWithValue(node->GetItem());
			}
			else
			{
				// in the special "list" case, all columns have values, so just
				// take the first one
				m_currentCol = GetColumn(0);
			}

			m_currentColSetByKeyboard = true;
			RefreshRow(m_currentRow);
			return true;
		}
		else
		{
			if (!wrapAround)
				return false;
		}
	}

	int idx = GetColumnIndex(m_currentCol);
	const unsigned int cols = GetColumnCount();
	for (unsigned int i = 0; i < cols; i++)
	{
		idx += (forward ? +1 : -1);
		if (idx >= (int)GetColumnCount())
		{
			if (!wrapAround)
				return false;

			if (GetCurrentRow() < GetRowCount() - 1)
			{
				// go to the first column of the next row:
				idx = 0;
				GoToRelativeRow(wxKeyboardState()/*dummy*/, +1);
			}
			else
			{
				// allow focus change
				event.Skip();
				return false;
			}
		}
		else if (idx < 0)
		{
			if (!wrapAround)
				return false;

			if (GetCurrentRow() > 0)
			{
				// go to the last column of the previous row:
				idx = (int)GetColumnCount() - 1;
				GoToRelativeRow(wxKeyboardState()/*dummy*/, -1);
			}
			else
			{
				// allow focus change
				event.Skip();
				return false;
			}
		}
		if (!node || GetModel()->HasValue(node->GetItem(), i))
			break;
	}

	EnsureVisibleRowCol(m_currentRow, idx);

	if (idx < 1)
	{
		// We are going to the left of the second column. Reset to whole-row
		// focus (which means first column would be edited).
		m_currentCol = NULL;
		RefreshRow(m_currentRow);
		return true;
	}

	m_currentCol = GetColumn(idx);
	m_currentColSetByKeyboard = true;
	RefreshRow(m_currentRow);
	return true;
}

int ibDataViewCtrl::GetTableAreaWidth() const
{
	return m_tableAreaWin != nullptr ? m_tableAreaWin->GetClientSize().x : 0;
}

// ⭐ THE LAW ON COLUMN WIDTHS, and it has exactly two cases.
//
//   the columns fit    → they are STRETCHED in proportion to what they ask for, filling the
//                        width to the right edge (widening the table widens the columns).
//   they do not fit    → nothing is squeezed. Every column keeps the width it asks for and
//                        the SCROLLBAR carries the overflow — drawn as it always was.
//
// There is NO automatic squeeze, and that is deliberate: squeezing produced "P..R..L..A.."
// (a row of initials, unreadable), and per-column floors invented to stop it then fought the
// manual drag — the drag asked a neighbour for room, the fit pushed it back to its floor,
// and the width came out of the dragged column instead.
//
// Proportion still applies to a DRAG, where it belongs: pull a border and the range to its
// right gives up exactly what this one gains (WXApplyColumnWidth).
//
// The share is always taken from the ASKED-FOR width, never from the current one, so every
// pass starts from the same numbers and the fitting cannot compound. A stack counts ONCE:
// its columns share one x range and their edges have to agree.
// THE WIDTHS HAVE ALL BEEN SET — and now, ONCE, everything that follows from them: the cached
// geometry goes, and the header, footer and rows area are repainted whole. A changed width
// moves every column after it (and under a group, cells above and below), so repainting one
// column's old rectangle leaves the previous dividers standing — the picket fence beside the
// dragged edge. Said per column instead, this ran thirteen times per mouse-move over
// half-applied widths, which is what made the drag crawl.
void ibDataViewCtrl::WXColumnWidthsApplied()
{
	InvalidateColumnLayout();

	if (m_headerAreaWin != nullptr)
		m_headerAreaWin->Refresh();

	if (m_footerAreaWin != nullptr)
		m_footerAreaWin->Refresh();

	if (m_tableAreaWin != nullptr)
		m_tableAreaWin->Refresh();
}

void ibDataViewCtrl::UpdateColumnSizes()
{
	if (GetColumnCount() == 0 || m_tableAreaWin == nullptr)
		return;

	const int room = GetTableAreaWidth();

	// NOT WHILE THE ROWS AREA HAS NO REAL WIDTH YET. On the way up, this is called several
	// times before the windows are laid out — measured: room 18, then 83, 148, 224 — and
	// every one of those says "the columns do not fit", so a freshly opened form started in
	// the scrolling state and only the next resize straightened it out. There is nothing to
	// fit into a strip 18 pixels wide; the layout will call again when there is.
	if (room < FromDIP(64))
		return;

	// A DRAG OWNS THE WIDTHS WHILE IT LASTS — the fit stands aside and only publishes the extent.
	//
	// Its arithmetic already keeps the very invariant this function exists for: while the columns
	// fit, whatever the dragged range takes the ranges to its RIGHT give, so the row stays exactly
	// as wide as the table; once those are at their floors the row grows and the scrollbar with it.
	// So re-deriving the widths here corrects nothing — it only disagrees, and two accounts of one
	// geometry always end the same way. Measured on the 13-column journal: the stretch handed every
	// range 3 px (including the ones LEFT of the drag, which must never move), the next pass took
	// them back, and the two took turns per mouse-move — the dragged column's left edge alternating
	// 504/516 and its border up to 47 px away from the pointer.
	if (WXIsColumnDragActive()) {

		const int taken = GetEndOfLastCol();
		m_tableAreaWin->SetVirtualSize(
			taken > room ? taken : 0, m_tableAreaWin->GetVirtualSize().y);
		return;
	}

	std::vector<ibWidthRange> ranges;
	CollectWidthRanges(ranges);

	int asking = 0;
	for (const ibWidthRange& range : ranges)
		asking += range.specified;

	if (ranges.empty() || asking <= 0 || room <= 0) {
		// Nothing to share out — publish what the columns really take (the layout's total,
		// not the widths added up: under a vertical group they SHARE one width, so adding
		// them claims the table is far wider than it is).
		const int taken = GetEndOfLastCol();
		m_tableAreaWin->SetVirtualSize(
			taken > room ? taken : 0, m_tableAreaWin->GetVirtualSize().y);
		return;
	}

	// DOES NOT FIT: leave every width alone and let the scrollbar do its job.
	if (asking > room) {

		bool moved = false;
		for (const ibWidthRange& range : ranges) {
			const int width = wxMax(range.specified, range.minimum);
			for (ibDataViewColumn* column : range.columns) {
				if (column->GetWidth() != width)
					moved = true;

				column->WXSetShownWidth(width);
			}
		}

		// Same rule as in the FITS branch below: a change nobody made is not announced.
		if (moved)
			WXColumnWidthsApplied();
		m_tableAreaWin->SetVirtualSize(GetEndOfLastCol(), m_tableAreaWin->GetVirtualSize().y);
		return;
	}

	// FITS: stretched in proportion, the rounding remainder going to the LAST range so the
	// columns end exactly on the right edge with no sliver of background beside them.
	int given = 0;
	bool moved = false;   // did any width really change — see the announcement below

	for (size_t idx = 0; idx < ranges.size(); idx++) {

		const bool lastOne = (idx + 1 == ranges.size());
		const int width = lastOne
			? room - given
			: wxMax((int)((wxLongLong_t)room * ranges[idx].specified / asking), ranges[idx].minimum);

		// Set QUIETLY, and not into the asked-for width: an automatic stretch is not a width
		// the user requested, and everything that follows from the new widths is announced
		// once, below — announcing per column rebuilt the layout and repainted the header
		// thirteen times per mouse-move, over half-applied widths (the header filled with
		// leftover dividers and the drag crawled).
		for (ibDataViewColumn* column : ranges[idx].columns) {
			if (column->GetWidth() != width)
				moved = true;

			column->WXSetShownWidth(width);
		}

		given += width;
	}

	// ONLY IF A WIDTH ACTUALLY MOVED. The fit runs far more often than the widths change — a
	// drag alone brings it here twice per mouse-move, the second time from the scrollbar's own
	// size cascade — and announcing a change that did not happen is not free: it drops the
	// geometry, rebuilds it, and repaints all three windows. Measured on one drag: the layout
	// was rebuilt 2–6 times per pixel of mouse travel (generation 9→11→13→19→20→24) and the
	// rows area painted TWICE per pass, the second time a frame behind the header — which is
	// what "the row catches up with the column" looked like on screen.
	if (moved)
		WXColumnWidthsApplied();

	// On screen by construction, so no horizontal scrolling. Forced to 0 to keep the
	// scrollbar from flickering while the window is dragged narrower; idle corrects it.
	m_tableAreaWin->SetVirtualSize(0, m_tableAreaWin->GetVirtualSize().y);
}


//-----------------------------------------------------------------------------
// ibDataViewCtrl
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataViewCtrl, ibDataViewCtrlBase);
wxBEGIN_EVENT_TABLE(ibDataViewCtrl, ibDataViewCtrlBase)
EVT_SIZE(ibDataViewCtrl::OnSize)
EVT_DPI_CHANGED(ibDataViewCtrl::OnDPIChanged)
EVT_SCROLLWIN(ibDataViewCtrl::OnScrollEvent)
// Idle path collapsed onto OnInternalIdle (single entry point: col-
// widths/dirty + paged bootstrap/fill/thaw + external hook).  EVT_IDLE
// on this control is no longer used.
wxEND_EVENT_TABLE()

#include "datavgen.window.private.h"  // ibDataViewMainWindow — the rows area this freezes / scrolls
#include "datavgen.paged.private.h"   // kBufferSlack + ScopedPagedFreeze + ibPagedFetch

// OnIdleEvent body collapsed into OnInternalIdle — single idle entry
// point on the control.  See OnInternalIdle below.

void ibDataViewCtrl::OnScrollEvent(wxScrollWinEvent& event)
{
	// Horizontal scrolling (column overflow) is plain wxScrollHelper
	// territory — pass it straight through to the default handler.  The
	// paged 3-state lying-scrollbar logic below is VERTICAL-only; before
	// the orientation guard it also caught horizontal events, so a paged
	// model swallowed the horizontal thumb drag (THUMBRELEASE returns
	// without event.Skip()) and mis-routed horizontal line/page events
	// into vertical PagedFetch — which is exactly why column scroll broke
	// once the custom scrollbar landed.
	if (event.GetOrientation() != wxVERTICAL) {
		event.Skip();
		return;
	}
	ibDataViewModel* model = GetModel();
	if (model != nullptr && model->IsPagedModel() && m_tableAreaWin != nullptr) {
		const int  countPerPage = GetCountPerPage();
		const long total        = static_cast<long>(GetRowCount());
		// GetFirstVisibleRow() works in drill mode (frozen-offset aware,
		// no round-trip through Item lookup which fails when fetched
		// rows have no model-side parent).
		const long topAdj       = static_cast<long>(GetFirstVisibleRow());
		const long marginFwd    = total - (topAdj + countPerPage);
		const long marginBwd    = topAdj;

		const wxEventType evt = event.GetEventType();
		const short dir =
			(evt == wxEVT_SCROLLWIN_LINEDOWN || evt == wxEVT_SCROLLWIN_PAGEDOWN ||
			 evt == wxEVT_SCROLLWIN_BOTTOM) ? +1 :
			(evt == wxEVT_SCROLLWIN_LINEUP   || evt == wxEVT_SCROLLWIN_PAGEUP   ||
			 evt == wxEVT_SCROLLWIN_TOP) ? -1 : 0;

		// Thumb drag in lying-scrollbar mode: wxScrollHelper would map
		// the fake-range position to the wrong buffer pixel, so we
		// don't event.Skip() here.  Instead, on RELEASE we compare the
		// drop position against the thumb's rest position for the
		// current state; a downward delta triggers a forward fetch,
		// upward delta a backward fetch.  TRACK events during the drag
		// are ignored (only the final position matters).  Snap the
		// thumb back to its rest pose at the end.
		if (IsPagedScrollbarMode() &&
		    (evt == wxEVT_SCROLLWIN_THUMBTRACK ||
		     evt == wxEVT_SCROLLWIN_THUMBRELEASE)) {
			if (evt == wxEVT_SCROLLWIN_THUMBRELEASE) {
				const int viewport2 = GetCountPerPage();
				if (viewport2 > 0) {
					int restPos;
					switch (DerivePagedThumb()) {
					case ibPagedThumb::Top:    restPos = 0;         break;
					case ibPagedThumb::Bottom: restPos = viewport2; break;
					case ibPagedThumb::Middle:
					default:                   restPos = viewport2; break;
					}
					const int dropPos = (int)event.GetPosition();
					const int delta = dropPos - restPos;
										// Snap-to-start: drag landed at the very top of the
					// bar.  Reset to bootstrap with empty anchor so the
					// next idle-fire reloads the head of the dataset.
					if (dropPos == 0 && m_pagedHasMoreBwd) {
						DestroyTree();
						m_root = ibDataViewTreeNode::CreateRootNode();
						m_pagedNeedsBootstrap    = true;
						m_pagedHasMoreFwd        = true;
						m_pagedHasMoreBwd        = false;
						m_pagedFwdAnchor         = ibDataViewItem();
						m_pagedBwdAnchor         = ibDataViewItem();
						m_pagedRestoreAnchor     = ibDataViewItem();
						m_pagedRestoreSelection  = ibDataViewItem();
						++m_pagedFetchGen;
						m_selection.Clear();
						m_currentRow             = (unsigned)-1;
						ClearRowHeightCache();
						InvalidateCount();
						UpdateDisplay();
											}
					else if (delta > 0 && m_pagedHasMoreFwd)
						PagedFetchForward(viewport2);
					else if (delta < 0 && m_pagedHasMoreBwd)
						PagedFetchBackward(viewport2);
					// Snap-to-end: dragging to the bottom of the bar
					// would require N forward fetches looped to reach
					// the dataset end (no model API for "last batch").
					// Deferred until async fetch lands.
				}
			}
			UpdatePagedScrollbar();
			return;
		}

		
		// Edge breach → fetch in the direction the user is actually
		// moving.  Cross-direction fetch (e.g. backward on a forward
		// scroll just because behind < slack from a small initial
		// buffer) caused content jumps when the trim shifted the
		// visible window.  Per-direction guard: a fetch in flight on
		// one side does not block a scroll-triggered fetch on the
		// other side.
		if (dir > 0 && marginFwd < kBufferSlack && m_pagedHasMoreFwd
		    && m_pagedFetchingFwd == 0)
			PagedFetchForward(countPerPage);
		else if (dir < 0 && marginBwd < kBufferSlack && m_pagedHasMoreBwd
		         && m_pagedFetchingBwd == 0)
			PagedFetchBackward(countPerPage);

		// Thumb derives from has-more flags + viewport position; topRow
		// updates after wxScrollHelper's default handler (event.Skip()
		// below).  CallAfter defers the refresh so DerivePagedThumb
		// reads the post-scroll topRow.
		CallAfter([this] { UpdatePagedScrollbar(); });
	}
	event.Skip();
}


ibDataViewCtrl::~ibDataViewCtrl()
{
	// FIRST, before anything else is unwound. Clearing the token is what makes
	// every delivery already posted to the UI queue a no-op — those lambdas hold
	// a copy of it and check it before they touch this object, and by the time
	// they run this object is gone.
	//
	// The reads themselves are not cancelled here and need not be: each is a
	// rented run that ends by construction — it reads its portion, posts, and its
	// session goes with it. A control dying while its form lives (a panel rebuild)
	// leaves a read that finishes, finds the token cleared, and drops its answer.
	if (m_aliveToken)
		*m_aliveToken = false;
	m_busyTimer.Stop();

	// Must do this or ~wxScrollHelper will pop the wrong event handler
	SetTargetWindow(this);

	if (m_notifier)
		GetModel()->RemoveNotifier(m_notifier);

	// EVERYTHING STILL HANGING ON THE TREE IS OURS — columns and groups alike. Whatever was
	// detached along the way (a form control being torn down) belongs to whoever detached it
	// and is already gone.
	DoClearColumns();

	wxDELETE(m_rootGroup);

#if wxUSE_ACCESSIBILITY
	SetAccessible(NULL);
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_DESTROY, this, wxOBJID_CLIENT, wxACC_SELF);
#endif // wxUSE_ACCESSIBILITY

	DestroyTree();

	delete m_renameTimer;
	delete m_rowHeightCache;
}

void ibDataViewCtrl::Init()
{
	m_notifier = NULL;

	m_headerAreaWin = NULL;
	m_tableAreaWin = NULL;
	m_tableFrozenRowAreaWin = NULL;
	m_tableFrozenColAreaWin = NULL;
	m_tableFrozenCornerAreaWin = NULL;
	m_footerAreaWin = NULL;

	m_colsDirty = false;

	// THE COLUMN STORE, BORN WITH THE CONTROL — so every reader can take it without asking
	// whether it exists yet. Its behaviour is decided, not configured: horizontal (columns
	// side by side, the ordinary table) and showing nothing of itself, ever.
	//
	// Made ONCE although Init runs TWICE — the default ctor calls it and so does Create. A
	// second store would orphan whatever the first already held, and leak it.
	if (m_rootGroup == nullptr) {
		m_rootGroup = new ibDataViewColumnGroup(wxEmptyString, ibColumnGroupHorizontal);
		m_rootGroup->SetOwner(this);
	}

	m_allowMultiColumnSort = false;

	m_editorRenderer = NULL;
	m_selectionMode = ibDataViewSelectionMode::ibDataViewSelectRow;

	// Default view mode = flat List.  Tree mode requires per-row
	// IsContainer guards (BuildTreeHelper) that bail on a flat
	// list's invalid root item, leaving the control empty even when
	// the model has rows.  Callers that need tree semantics flip via
	// SetViewMode after construction.
	m_viewMode = ibDataViewViewMode::ibDataViewList;

	m_lastOnSame = false;
	m_renameTimer = new ibDataViewRenameTimer(this);

	// TODO: user better initial values/nothing selected
	m_currentCol = NULL;
	m_currentColSetByKeyboard = false;
	m_useCellFocus = false;
	m_currentRow = (unsigned)-1;
	m_lineHeight = GetDefaultRowHeight();

#if wxUSE_DRAG_AND_DROP
	m_dragCount = 0;
	m_dragStart = wxPoint(0, 0);

	m_dragEnabled = false;
	m_dropItemInfo = DropItemInfo();
#endif // wxUSE_DRAG_AND_DROP

	m_lineLastClicked = (unsigned int)-1;
	m_lineBeforeLastClicked = (unsigned int)-1;
	m_lineSelectSingleOnUp = (unsigned int)-1;

	m_hasFocus = false;

	m_penRule = wxPen(GetRuleColour());

	m_root = ibDataViewTreeNode::CreateRootNode();

	// Make m_countRows = -1 will cause the class recaculate the real displaying number of rows.
	m_countRows = -1;
	m_countFrozenRows = m_countFrozenCols = 0;
	m_countFrozenHierarchicalRows = 0;

	m_underMouse = NULL;
}

bool ibDataViewCtrl::Create(wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxValidator& validator,
	const wxString& name)
{
	Init();

	if (!wxControl::Create(parent, id, pos, size,
		style | wxWANTS_CHARS, validator, name))
		return false;

	SetInitialSize(size);

#if defined(__WXOSX__) && !wxCHECK_VERSION(3, 3, 0)
	MacSetClipChildren(true);
#endif

	m_tableAreaWin = new ibDataViewMainWindow(this, ibDataViewMainWindow::ibDataViewWindowNormal);

	// The busy indicator's tick. Owned by the control so it dies with it — a
	// timer outliving its window fires into freed memory. It is armed only while
	// a read is actually out (UpdateBusyIndicator), so an idle list costs nothing.
	//
	// An OWN id, not wxID_ANY: this control already owns another timer (the rename
	// one), and binding on wxID_ANY means "every timer event reaching this
	// handler" — the two would fire each other's handlers.
	static const int s_busyTimerId = wxWindow::NewControlId();
	m_busyTimer.SetOwner(this, s_busyTimerId);
	Bind(wxEVT_TIMER, &ibDataViewCtrl::OnBusyTimer, this, s_busyTimerId);

	// We use the cursor keys for moving the selection, not scrolling, so call
	// this method to ensure wxScrollHelperEvtHandler doesn't catch all
	// keyboard events forwarded to us from wxListMainWindow.
	DisableKeyboardScrolling();

	if (HasFlag(wxDV_NO_HEADER))
		m_headerAreaWin = NULL;
	else
		m_headerAreaWin = new ibDataViewHeaderWindow(this);

	if (!HasFlag(wxDV_FOOTER))
		m_footerAreaWin = NULL;
	else
		m_footerAreaWin = new ibDataViewFooterWindow(this);

	SetTargetWindow(m_tableAreaWin);

	if (HasFlag(wxDV_VARIABLE_LINE_HEIGHT))
	{
		m_rowHeightCache = new HeightCache();
	}
	else
	{
		m_rowHeightCache = NULL;
	}

	EnableSystemThemeByDefault();

#if wxUSE_ACCESSIBILITY
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_CREATE, this, wxOBJID_CLIENT, wxACC_SELF);
#endif // wxUSE_ACCESSIBILITY

	UpdateDisplay();

	return true;
}

wxWindowList ibDataViewCtrl::GetCompositeWindowParts() const
{
	wxWindowList parts;
	parts.push_back(m_headerAreaWin); // It's ok to add it even if it's null.
	parts.push_back(m_tableAreaWin);
	parts.push_back(m_tableFrozenRowAreaWin);
	parts.push_back(m_tableFrozenColAreaWin);
	parts.push_back(m_tableFrozenCornerAreaWin);
	parts.push_back(m_footerAreaWin); // It's ok to add it even if it's null.
	return parts;
}

wxBorder ibDataViewCtrl::GetDefaultBorder() const
{
	return wxBORDER_THEME;
}

ibHeaderGenericCtrl* ibDataViewCtrl::GenericGetHeader() const
{
	return m_headerAreaWin;
}

void ibDataViewCtrl::InitializeFrozenWindows()
{
	// frozen row windows
	if (wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows) > 0 && !m_tableFrozenRowAreaWin)
	{
		m_tableFrozenRowAreaWin = new ibDataViewMainWindow(this, ibDataViewMainWindow::ibDataViewWindowFrozenRow);
		m_tableFrozenRowAreaWin->SetOwnForegroundColour(m_tableAreaWin->GetForegroundColour());
		m_tableFrozenRowAreaWin->SetOwnBackgroundColour(m_tableAreaWin->GetBackgroundColour());
	}
	else if (wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows) == 0 && m_tableFrozenRowAreaWin)
	{
		delete m_tableFrozenRowAreaWin;
		m_tableFrozenRowAreaWin = NULL;
	}

	// frozen column windows
	if (m_countFrozenCols > 0 && !m_tableFrozenColAreaWin)
	{
		m_tableFrozenColAreaWin = new ibDataViewMainWindow(this, ibDataViewMainWindow::ibDataViewWindowFrozenCol);
		m_tableFrozenColAreaWin->SetOwnForegroundColour(m_tableAreaWin->GetForegroundColour());
		m_tableFrozenColAreaWin->SetOwnBackgroundColour(m_tableAreaWin->GetBackgroundColour());
	}
	else if (m_countFrozenCols == 0 && m_tableFrozenColAreaWin)
	{
		delete m_tableFrozenColAreaWin;
		m_tableFrozenColAreaWin = NULL;
	}

	// frozen corner window
	if (m_countFrozenRows > 0 && m_countFrozenCols > 0 && !m_tableFrozenCornerAreaWin)
	{
		m_tableFrozenCornerAreaWin = new ibDataViewMainWindow(this, ibDataViewMainWindow::ibDataViewWindowFrozenCorner);

		m_tableFrozenCornerAreaWin->SetOwnForegroundColour(m_tableAreaWin->GetForegroundColour());
		m_tableFrozenCornerAreaWin->SetOwnBackgroundColour(m_tableAreaWin->GetBackgroundColour());
	}
	else if ((m_countFrozenRows == 0 || m_countFrozenCols == 0) && m_tableFrozenCornerAreaWin)
	{
		delete m_tableFrozenCornerAreaWin;
		m_tableFrozenCornerAreaWin = NULL;
	}
}

bool ibDataViewCtrl::FreezeTo(int row, int col)
{
	wxCHECK_MSG(row >= 0 && col >= 0, false,
		"Number of rows or cols can't be negative!");

	if (row > 99999)
		row = 0;

	if (col > 99999)
		col = 0;

	m_countFrozenRows = wxMax(row, 0);
	m_countFrozenCols = wxMax(col, 0);

	InitializeFrozenWindows();

	// recompute dimensions
	InvalidateBestSize();

	CalcWindowSizes();

	Refresh();
	return true;
}

bool ibDataViewCtrl::IsFrozen() const
{
	return wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows) || m_countFrozenCols;
}

void ibDataViewCtrl::SetHeaderHeight(int point)
{
	m_userHeaderHeight = wxMax(point, 1);
	ApplyHeaderHeight();
}

int ibDataViewCtrl::GetHeaderHeight() const
{
	if (m_headerAreaWin)
		return m_headerAreaWin->GetColumnHeight();

	return 0;
}

void ibDataViewCtrl::SetFooterHeight(int point)
{
	m_userFooterHeight = wxMax(point, 1);
	ApplyHeaderHeight();
}

// The header is as deep as the DEEPER of the two claims on it: what the form asked for, and
// what the column groups need (one band per group level). Asking for one band cannot flatten
// a two-level header — that would clip the titles it draws.
//
// THE FOOTER IS SIZED BY THE SAME RULE, against the BODY's band count: it draws one cell per
// column, in that column's own band, so a stack of three needs three bands under it or two
// of the three totals have nowhere to be.
void ibDataViewCtrl::ApplyHeaderHeight()
{
	const ibDataViewColumnLayout& layout = GetColumnLayout();

	if (m_headerAreaWin != nullptr)
		m_headerAreaWin->SetColumnHeight(
			wxMax(m_userHeaderHeight, layout.GetHeaderBandCount()));

	if (m_footerAreaWin != nullptr)
		m_footerAreaWin->SetColumnHeight(
			wxMax(m_userFooterHeight, layout.GetRowBandCount()));
}

int ibDataViewCtrl::GetFooterHeight() const
{
	if (m_footerAreaWin)
		return m_footerAreaWin->GetColumnHeight();

	return 0;
}

void ibDataViewCtrl::ShowHeaderWindow(bool e)
{
	int flags = GetWindowStyleFlag();

	if (e)
		flags &= ~wxDV_NO_HEADER;
	else
		flags |= wxDV_NO_HEADER;

	SetWindowStyleFlag(flags);

	if (m_headerAreaWin != NULL && HasFlag(wxDV_NO_HEADER))
	{
		if (m_headerAreaWin)
			m_headerAreaWin->Destroy();

		m_headerAreaWin = NULL;
	}
	else if (m_headerAreaWin == NULL && !HasFlag(wxDV_NO_HEADER))
	{
		m_headerAreaWin = new ibDataViewHeaderWindow(this);
		m_headerAreaWin->SetColumnCount(GetColumnCount());
	}
}

void ibDataViewCtrl::ShowFooterWindow(bool e)
{
	int flags = GetWindowStyleFlag();

	if (!e)
		flags &= ~wxDV_FOOTER;
	else
		flags |= wxDV_FOOTER;

	SetWindowStyleFlag(flags);

	if (m_footerAreaWin != NULL && !HasFlag(wxDV_FOOTER))
	{
		if (m_footerAreaWin)
			m_footerAreaWin->Destroy();

		m_footerAreaWin = NULL;
	}
	else if (m_footerAreaWin == NULL && HasFlag(wxDV_FOOTER))
	{
		m_footerAreaWin = new ibDataViewFooterWindow(this);
		m_footerAreaWin->SetColumnCount(GetColumnCount());
	}
}

#ifdef __WXMSW__
WXLRESULT ibDataViewCtrl::MSWWindowProc(WXUINT nMsg,
	WXWPARAM wParam,
	WXLPARAM lParam)
{
	WXLRESULT rc = ibDataViewCtrlBase::MSWWindowProc(nMsg, wParam, lParam);

	// we need to process arrows ourselves for scrolling
	if (nMsg == WM_GETDLGCODE)
	{
		rc |= DLGC_WANTARROWS;
	}

	return rc;
}
#endif

ibDataViewMainWindow* ibDataViewCtrl::CellToDataViewWindow(const ibDataViewItem& item, const ibDataViewColumn* column) const
{
	// It may happen that we're called during grid creation, when the current
	// cell still has invalid coordinates -- don't return (possibly null)
	// frozen corner window in this case.
	if (!item.IsOk() && column == NULL)
		return m_tableAreaWin;
	else if (GetRowByItem(item) < wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows) && GetColumnIndex(column) < m_countFrozenCols)
		return m_tableFrozenCornerAreaWin;
	else if (GetRowByItem(item) < wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows))
		return m_tableFrozenRowAreaWin;
	// A cell is in the frozen COLUMN area by its COLUMN, not by its row — this read the row
	// against the frozen-COLUMN count, so a cell landed there or not depending on how far
	// down it was.
	else if (GetColumnIndex(column) < m_countFrozenCols)
		return m_tableFrozenColAreaWin;

	return m_tableAreaWin;
}

void ibDataViewCtrl::GetDataViewWindowOffset(const ibDataViewMainWindow* tableWindow, int& x, int& y) const
{
	wxPoint pt = GetDataViewWindowOffset(tableWindow);

	x = pt.x;
	y = pt.y;
}

wxPoint ibDataViewCtrl::GetDataViewWindowOffset(const ibDataViewMainWindow* tableWindow) const
{
	wxPoint pt(0, 0);

	if (tableWindow)
	{
		if (m_tableFrozenRowAreaWin &&
			(tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenRow) == 0)
		{
			pt.y = m_tableFrozenRowAreaWin->GetClientSize().y;
		}

		if (m_tableFrozenColAreaWin &&
			(tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenCol) == 0)
		{
			pt.x = m_tableFrozenColAreaWin->GetClientSize().x;
		}
	}

	return pt;
}

ibDataViewMainWindow* ibDataViewCtrl::DevicePosToDataViewWindow(wxPoint pos) const
{
	return DevicePosToDataViewWindow(pos.x, pos.y);
}

ibDataViewMainWindow* ibDataViewCtrl::DevicePosToDataViewWindow(int x, int y) const
{
	if (m_tableAreaWin->GetRect().Contains(x, y))
		return m_tableAreaWin;
	else if (m_tableFrozenCornerAreaWin && m_tableFrozenCornerAreaWin->GetRect().Contains(x, y))
		return m_tableFrozenCornerAreaWin;
	else if (m_tableFrozenRowAreaWin && m_tableFrozenRowAreaWin->GetRect().Contains(x, y))
		return m_tableFrozenRowAreaWin;
	else if (m_tableFrozenColAreaWin && m_tableFrozenColAreaWin->GetRect().Contains(x, y))
		return m_tableFrozenColAreaWin;

	return NULL;
}

void ibDataViewCtrl::CalcDataViewWindowUnscrolledPosition(int x, int y, int* xx, int* yy,
	const ibDataViewMainWindow* tableWindow) const
{
	CalcUnscrolledPosition(x, y, xx, yy);

	if (tableWindow)
	{
		if (yy && (tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenRow))
			*yy = y;
		if (xx && (tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenCol))
			*xx = x;
	}
}

wxPoint ibDataViewCtrl::CalcDataViewWindowUnscrolledPosition(const wxPoint& pt,
	const ibDataViewMainWindow* tableWindow) const
{
	wxPoint pt2;
	CalcDataViewWindowUnscrolledPosition(pt.x, pt.y, &pt2.x, &pt2.y, tableWindow);
	return pt2;
}

void ibDataViewCtrl::CalcDataViewWindowScrolledPosition(int x, int y, int* xx, int* yy,
	const ibDataViewMainWindow* tableWindow) const
{
	CalcScrolledPosition(x, y, xx, yy);

	if (tableWindow)
	{
		if (yy && (tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenRow))
			*yy = y;
		if (xx && (tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenCol))
			*xx = x;
	}
}

wxPoint ibDataViewCtrl::CalcDataViewWindowScrolledPosition(const wxPoint& pt,
	const ibDataViewMainWindow* tableWindow) const
{
	wxPoint pt2;
	CalcDataViewWindowScrolledPosition(pt.x, pt.y, &pt2.x, &pt2.y, tableWindow);
	return pt2;
}

wxSize ibDataViewCtrl::GetSizeAvailableForScrollTarget(const wxSize& size)
{
	wxPoint offset = GetDataViewWindowOffset(m_tableAreaWin);
	wxSize sizeTableWin(size);

	sizeTableWin.x -= offset.x;
	sizeTableWin.y -= offset.y;

	if (!HasFlag(wxDV_NO_HEADER) && (m_headerAreaWin))
		sizeTableWin.y -= m_headerAreaWin->GetSize().y;

	if (HasFlag(wxDV_FOOTER) && (m_footerAreaWin))
		sizeTableWin.y -= m_footerAreaWin->GetSize().y;

	return sizeTableWin;
}

void ibDataViewCtrl::CalcWindowSizes()
{
	int cw, ch;
	GetClientSize(&cw, &ch);

	if (m_targetWindow != this) // check whether initialisation has been done
	{
		unsigned int freezeAreaHeight = 0,
			freezeAreaWidth = 0;

		// frozen rows and cols windows size
		for (int row = 0; row < wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows); row++)
		{
			freezeAreaHeight += GetLineHeight(row);
		}

		// THE FROZEN EDGE IS A GEOMETRY QUESTION, so the layout answers it: the frozen area
		// reaches the RIGHT EDGE of the last column counted. Adding up the first N widths
		// counted a vertical STACK once per column in it (they share one x range, each
		// holding the stack's full width), which pushed the edge far past where those
		// columns are actually drawn. Reading edges also makes splitting a stack impossible:
		// the first of its columns to be counted carries the whole stack.
		int frozen = 0;
		for (const ibColumnPlacement& place : GetColumnLayout().GetBodyPlacements()) {

			if (frozen == m_countFrozenCols)
				break;

			freezeAreaWidth = (unsigned int)wxMax((int)freezeAreaWidth, place.x + place.width);
			frozen++;
		}

		// We need to override OnSize so that our scrolled
		// window a) does call Layout() to use sizers for
		// positioning the controls but b) does not query
		// the sizer for their size and use that for setting
		// the scrollable area as set that ourselves by
		// calling SetScrollbar() further down.

		int headerAreaHeight = 0, headerAreaFooter = 0;

		if (m_headerAreaWin && m_headerAreaWin->IsShown())
			headerAreaHeight = m_headerAreaWin->GetBestHeight(cw);

		if (m_footerAreaWin && m_footerAreaWin->IsShown())
			headerAreaFooter = m_footerAreaWin->GetBestHeight(cw);

		if (m_headerAreaWin && m_headerAreaWin->IsShown())
			m_headerAreaWin->SetSize(0, 0, cw, headerAreaHeight);

		if (m_tableAreaWin && m_tableAreaWin->IsShown())
			m_tableAreaWin->SetSize(freezeAreaWidth, headerAreaHeight + freezeAreaHeight, cw, ch - (headerAreaHeight + headerAreaFooter + freezeAreaHeight));

		if (m_tableFrozenRowAreaWin && m_tableFrozenRowAreaWin->IsShown())
			m_tableFrozenRowAreaWin->SetSize(freezeAreaWidth, headerAreaHeight, cw - freezeAreaWidth, freezeAreaHeight);

		if (m_tableFrozenColAreaWin && m_tableFrozenColAreaWin->IsShown())
			m_tableFrozenColAreaWin->SetSize(0, headerAreaHeight + freezeAreaHeight, freezeAreaWidth, ch - (headerAreaHeight + headerAreaFooter + freezeAreaHeight));

		if (m_tableFrozenCornerAreaWin && m_tableFrozenCornerAreaWin->IsShown())
			m_tableFrozenCornerAreaWin->SetSize(0, headerAreaHeight, freezeAreaWidth, freezeAreaHeight);

		if (m_footerAreaWin && m_footerAreaWin->IsShown())
			m_footerAreaWin->SetSize(0, ch - headerAreaFooter, cw, headerAreaFooter);

		// Update the last column size to take all the available space. Note that
		// this must be done after calling Layout() to update m_tableAreaWin size.

		// Scroll-rate rationale.  AdjustScrollbars()
		// derives the scrollbar range as virtualSize / pixelsPerLine, so
		// with a 0 rate it emits range == 0 and NO scrollbar appears even when
		// the virtual width overflows the client.  The x-rate was previously
		// only set by RecalculateDisplay(), which fires on m_dirty — for a
		// non-paged control (e.g. a document-form tablebox) that seldom runs
		// after the first layout, so every resize went through CalcWindowSizes
		// → AdjustScrollbars() with a stale 0 x-rate and the horizontal
		// scrollbar never showed despite overflowing columns.  (Diagnosed:
		// virtX=1647 vs clientX=611, yet SetScrollbar range=0.)
		//
		// Set ONLY the x-rate; preserve the current y-rate.  Forcing a y-rate
		// here would enable the vertical scrollbar for controls that never had
		// one (y-rate stays 0 until RecalculateDisplay sets it), making it pop
		// up spuriously — e.g. the small designer tablebox preview where any
		// content overflows the tiny rows area.  Vertical stays owned by
		// RecalculateDisplay (non-paged) / the lying paged scrollbar (paged).
		// Apply synchronously, NOT deferred to idle.  Deferring the scrollbar
		// geometry to OnInternalIdle crashed / glitched designer column-add and
		// resize: the deferred AdjustScrollbars ran a tick later, after the
		// column array / control state had already moved on.  Set ONLY the
		// x-rate; preserve the current y-rate (see rationale above).
		int curXUnit = 0, curYUnit = 0;
		GetScrollPixelsPerUnit(&curXUnit, &curYUnit);
		const int wantXUnit = FromDIP(10);
		if (curXUnit != wantXUnit)
			SetScrollRate(wantXUnit, curYUnit);
		UpdateColumnSizes();
		AdjustScrollbars();

		// We must redraw the headers if their height changed. Normally this
		// shouldn't happen as the control shouldn't let itself be resized beneath
		// its minimal height but avoid the display artefacts that appear if it
		// does happen, e.g. because there is really not enough vertical space.
		if (!HasFlag(wxDV_NO_HEADER) && m_headerAreaWin &&
			m_headerAreaWin->GetSize().y <= m_headerAreaWin->GetBestSize().y)
		{
			m_headerAreaWin->Refresh();
		}

		if (HasFlag(wxDV_FOOTER) && m_footerAreaWin &&
			m_footerAreaWin->GetSize().y <= m_footerAreaWin->GetBestSize().y)
		{
			m_footerAreaWin->Refresh();
		}
	}
}

void ibDataViewCtrl::OnSize(wxSizeEvent& event)
{
	CalcWindowSizes();

	// AND THE COLUMNS FOLLOW THE NEW WIDTH. CalcWindowSizes lays the windows out; the widths
	// are a separate question and the only place that answers it is this one — the geometry
	// cache has to be dropped as well, or the row keeps the positions it had at the old size.
	// (Without this a table simply did not react to the window being resized.)
	InvalidateColumnLayout();
	UpdateColumnSizes();
	SyncHorizontalScrollbar();

	// Top-up fill in OnInternalIdle is condition-driven (loaded < cpp +
	// slack) — it fires whenever the buffer has room, regardless of why.
	// No size-flag needed.

	event.Skip();
}

void ibDataViewCtrl::OnDPIChanged(wxDPIChangedEvent& event)
{
	ClearRowHeightCache();
	SetRowHeight(GetDefaultRowHeight());

	const unsigned int count = GetColumnCount();
	for (unsigned int idx = 0; idx < count; idx++)
	{
		ibDataViewColumn* column = GetColumn(idx);

		int minWidth = column->GetMinWidth();
		if (minWidth > 0)
			minWidth = event.ScaleX(minWidth);
		column->SetMinWidth(minWidth);

		int width = column->WXGetSpecifiedWidth();
		if (width > 0)
			width = event.ScaleX(width);
		column->SetWidth(width);
	}

	event.Skip();
}

void ibDataViewCtrl::SetFocus()
{
	if (m_tableAreaWin)
		m_tableAreaWin->SetFocus();
}

bool ibDataViewCtrl::SetFont(const wxFont& font)
{
	if (!BaseType::SetFont(font))
		return false;

	SetRowHeight(GetDefaultRowHeight());

	if (m_headerAreaWin || m_tableAreaWin || m_tableFrozenRowAreaWin || m_tableFrozenColAreaWin || m_tableFrozenCornerAreaWin)
	{
		InvalidateColBestWidths();
		Layout();
	}

	return true;
}

bool ibDataViewCtrl::SetForegroundColour(const wxColour& colour)
{
	// Previous versions of this class, not using wxCompositeWindow, as well as
	// the native versions of this control, don't change the header foreground
	// when this method is called and this could be more desirable in practice,
	// as well we being more compatible, so skip calling the base class version
	// that would change it as well and change only the main items area colour
	// here too.
	if (!ibDataViewCtrlBase::SetForegroundColour(colour))
		return false;

	if (m_tableAreaWin)
		m_tableAreaWin->SetForegroundColour(colour);

	if (m_tableFrozenRowAreaWin)
		m_tableFrozenRowAreaWin->SetForegroundColour(colour);

	if (m_tableFrozenColAreaWin)
		m_tableFrozenColAreaWin->SetForegroundColour(colour);

	if (m_tableFrozenCornerAreaWin)
		m_tableFrozenCornerAreaWin->SetForegroundColour(colour);

	return true;
}

bool ibDataViewCtrl::SetBackgroundColour(const wxColour& colour)
{
	// See SetForegroundColour() above.
	if (!ibDataViewCtrlBase::SetBackgroundColour(colour))
		return false;

	if (m_tableAreaWin)
		m_tableAreaWin->SetBackgroundColour(colour);

	if (m_tableFrozenRowAreaWin)
		m_tableFrozenRowAreaWin->SetBackgroundColour(colour);

	if (m_tableFrozenColAreaWin)
		m_tableFrozenColAreaWin->SetBackgroundColour(colour);

	if (m_tableFrozenCornerAreaWin)
		m_tableFrozenCornerAreaWin->SetBackgroundColour(colour);

	return true;
}

#if wxUSE_ACCESSIBILITY
bool ibDataViewCtrl::Show(bool show)
{
	bool changed = wxControl::Show(show);
	if (changed)
	{
		wxAccessible::NotifyEvent(show ? wxACC_EVENT_OBJECT_SHOW : wxACC_EVENT_OBJECT_HIDE,
			this, wxOBJID_CLIENT, wxACC_SELF);
	}

	return changed;
}

void ibDataViewCtrl::SetName(const wxString& name)
{
	wxControl::SetName(name);
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_NAMECHANGE, this, wxOBJID_CLIENT, wxACC_SELF);
}

bool ibDataViewCtrl::Reparent(wxWindowBase* newParent)
{
	bool changed = wxControl::Reparent(newParent);
	if (changed)
	{
		wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_PARENTCHANGE, this, wxOBJID_CLIENT, wxACC_SELF);
	}

	return changed;
}
#endif // wxUSE_ACCESIBILITY

bool ibDataViewCtrl::Enable(bool enable)
{
	bool changed = wxControl::Enable(enable);
	if (changed)
	{
#if wxUSE_ACCESSIBILITY
		wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_STATECHANGE, this, wxOBJID_CLIENT, wxACC_SELF);
#endif // wxUSE_ACCESIBILITY
		Refresh();
	}

	return changed;
}

void ibDataViewCtrl::ScrollWindow(int dx, int dy, const wxRect* rect)
{
	m_underMouse = NULL;

	// We must explicitly call wxWindow version to avoid infinite recursion as
	// ibDataViewMainWindow::ScrollWindow() calls this method back.

	if (m_tableAreaWin)
		m_tableAreaWin->wxWindow::ScrollWindow(dx, dy, rect);

	if (m_tableFrozenRowAreaWin)
		m_tableFrozenRowAreaWin->wxWindow::ScrollWindow(dx, 0, rect);
	if (m_tableFrozenColAreaWin)
		m_tableFrozenColAreaWin->wxWindow::ScrollWindow(0, dy, rect);

	if (m_headerAreaWin)
		m_headerAreaWin->ScrollWindow(dx, 0, rect);
	if (m_footerAreaWin)
		m_footerAreaWin->ScrollWindow(dx, 0, rect);
}

void ibDataViewCtrl::Refresh(bool eraseb, const wxRect* rect)
{
	// Refresh to get correct scrolled position:
	BaseType::Refresh(eraseb, rect);

	if (rect)
	{
		m_tableAreaWin->Refresh(eraseb, rect);
		if (m_tableFrozenRowAreaWin)
			m_tableFrozenRowAreaWin->Refresh(eraseb, rect);
		if (m_tableFrozenColAreaWin)
			m_tableFrozenColAreaWin->Refresh(eraseb, rect);
		if (m_headerAreaWin)
			m_headerAreaWin->Refresh(eraseb, rect);
		if (m_footerAreaWin)
			m_footerAreaWin->Refresh(eraseb, rect);
	}
	else
	{
		m_tableAreaWin->Refresh(eraseb, NULL);
		if (m_tableFrozenRowAreaWin)
			m_tableFrozenRowAreaWin->Refresh(eraseb, NULL);
		if (m_tableFrozenColAreaWin)
			m_tableFrozenColAreaWin->Refresh(eraseb, NULL);
		if (m_headerAreaWin)
			m_headerAreaWin->Refresh(eraseb, NULL);
		if (m_footerAreaWin)
			m_footerAreaWin->Refresh(eraseb, NULL);
	}
}

bool ibDataViewCtrl::AssociateModel(ibDataViewModel* model)
{

	if (ibDataViewModel* const oldModel = GetModel())
	{
		// Remove the notifier from the model before calling the base class
		// version which may (or not) delete the model.
		oldModel->RemoveNotifier(m_notifier);
		m_notifier = nullptr;
	}

	if (!ibDataViewCtrlBase::AssociateModel(model))
		return false;

	if (model)
	{
		m_notifier = new ibGenericDataViewModelNotifier(this);
		model->AddNotifier(m_notifier);
	}

	DestroyTree();

	// Paged models start empty.  The control fetches the first batch
	// itself once layout settles (OnIdle bootstrap below), so don't
	// build a tree off whatever GetChildren returns at attach time.
	m_pagedNeedsBootstrap = (model != nullptr && model->IsPagedModel());
	m_pagedHasMoreFwd     = m_pagedNeedsBootstrap;
	m_pagedHasMoreBwd     = false;
	m_pagedFetchingFwd    = 0;
	m_pagedFetchingBwd    = 0;
	m_pagedFwdAnchor      = ibDataViewItem();
	m_pagedBwdAnchor      = ibDataViewItem();
	++m_pagedFetchGen;

	// NOT FROZEN HERE ANY MORE. This used to hold the rows area back so the empty
	// frame between mounting the control and filling it never reached the screen —
	// correct while the fill was synchronous and one idle pass away.
	//
	// It is wrong now: the fill waits for a portion, and a frozen window does not
	// repaint AT ALL, so the whole wait showed as a blank rectangle with no way to
	// say anything on it. The empty frame IS the waiting state, and it has an
	// answer to give — the busy arc. So the area stays live and the indicator does
	// the talking; the wipe + fill that follows is frozen as its own transaction
	// (OnPagedFetchResetComplete), which is where flicker actually mattered.

	if (model && !model->IsPagedModel())
	{
		BuildTree(model);
	}
	else if (model)
	{
		// Make sure tree root exists so ItemInserted paths have a parent.
		m_root = ibDataViewTreeNode::CreateRootNode();
		InvalidateCount();
	}

	// (Header arrows are set per column from the composer on column rebuild — tablebox OnUpdated — and on click.)

	UpdateDisplay();

	return true;
}

#if wxUSE_DRAG_AND_DROP
bool ibDataViewCtrl::EnableDragSource(const wxDataFormat& format)
{
	m_dragFormat = format;
	m_dragEnabled = format != wxDF_INVALID;

	return true;
}

void ibDataViewCtrl::RefreshDropHint()
{
	const unsigned row = m_dropItemInfo.m_row;

	switch (m_dropItemInfo.m_hint)
	{
	case DropHint_None:
		break;

	case DropHint_Inside:
		RefreshRow(row);
		break;

	case DropHint_Above:
		RefreshRows(row == 0 ? 0 : row - 1, row);
		break;

	case DropHint_Below:
		// It's not a problem here if row+1 is out of range, RefreshRows()
		// allows this.
		RefreshRows(row, row + 1);
		break;
	}
}

void ibDataViewCtrl::RemoveDropHint()
{
	RefreshDropHint();

	m_dropItemInfo = DropItemInfo();
}

ibDataViewCtrl::DropItemInfo ibDataViewCtrl::GetDropItemInfo(const wxCoord x, const wxCoord y)
{
	DropItemInfo dropItemInfo;

	int xx = x;
	int yy = y;
	CalcUnscrolledPosition(xx, yy, &xx, &yy);

	unsigned int row = GetLineAt(yy);
	dropItemInfo.m_row = row;

	if (row >= GetRowCount() || xx > GetEndOfLastCol())
		return dropItemInfo;

	if (IsVirtualList())
	{
		dropItemInfo.m_item = GetItemByRow(row);

		if (dropItemInfo.m_item.IsOk())
			dropItemInfo.m_hint = DropHint_Inside;
	}
	else
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(row);
		if (!node)
			return dropItemInfo;

		dropItemInfo.m_item = node->GetItem();

		const int itemStart = GetLineStart(row);
		const int itemHeight = GetLineHeight(row);

		// 15% is an arbitrarily chosen threshold here, which could be changed
		// or made configurable if really needed.
		static const double UPPER_ITEM_PART = 0.15;

		bool insertAbove = yy - itemStart < itemHeight * UPPER_ITEM_PART;
		if (insertAbove)
		{
			// Can be treated as 'insertBelow" with the small difference:
			node = GetTreeNodeByRow(row - 1);   // We need the node from the previous row

			dropItemInfo.m_hint = DropHint_Above;

			if (!node)
			{
				// Seems to be dropped in the root node

				dropItemInfo.m_indentLevel = 0;
				dropItemInfo.m_proposedDropIndex = 0;
				dropItemInfo.m_item = ibDataViewItem();

				return dropItemInfo;
			}

		}

		bool insertBelow = yy - itemStart > itemHeight * (1.0 - UPPER_ITEM_PART);
		if (insertBelow)
			dropItemInfo.m_hint = DropHint_Below;

		if (insertBelow || insertAbove)
		{
			// Insert inside the 'item' or after (below) it. Depends on:
			// 1 - the 'item' is a container
			// 2 - item's child index in it's parent (the last in the parent or not)
			// 3 - expanded (opened) or not
			// 4 - mouse x position

			int xStart = 0;     // Expander column x position start
			ibDataViewColumn* const expander = GetExpanderColumnOrFirstOne(this);
			for (unsigned int i = 0; i < GetColumnCount(); i++)
			{
				ibDataViewColumn* col = GetColumn(i);
				if (col->IsHidden())
					continue;   // skip it!

				if (col == expander)
					break;

				xStart += col->GetWidth();
			}

			const int expanderWidth = wxRendererNative::Get().GetExpanderSize(this).GetWidth();

			int level = node->GetIndentLevel();

			ibDataViewTreeNode* prevAscendNode = node;
			ibDataViewTreeNode* ascendNode = node;
			while (ascendNode != NULL)
			{
				dropItemInfo.m_indentLevel = level + 1;

				if (ascendNode->GetItem().IsContainer())
				{
					// Item can be inserted
					dropItemInfo.m_item = ascendNode->GetItem();

					int itemPosition = ascendNode->FindChildByItem(prevAscendNode->GetItem());
					if (itemPosition == wxNOT_FOUND)
						itemPosition = 0;
					else
						itemPosition++;

					dropItemInfo.m_proposedDropIndex = itemPosition;

					// We must break the loop if the applied node is expanded
					// (opened) and the proposed drop position is not the last
					// in this node.
					if (ascendNode->IsOpen())
					{
						const size_t lastPos = ascendNode->GetChildNodes().size();
						if (static_cast<size_t>(itemPosition) != lastPos)
							break;
					}

					int indent = GetIndent() * level + expanderWidth;

					if (xx >= xStart + indent)
						break;
				}

				prevAscendNode = ascendNode;
				ascendNode = ascendNode->GetParent();
				--level;
			}
		}
		else
		{
			dropItemInfo.m_hint = DropHint_Inside;
		}
	}

	return dropItemInfo;
}

wxDragResult ibDataViewCtrl::OnDragOver(wxDataFormat format, wxCoord x,
	wxCoord y, wxDragResult def)
{
	DropItemInfo nextDropItemInfo = GetDropItemInfo(x, y);

	ibDataViewEvent event(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, this, nextDropItemInfo.m_item);
	event.SetProposedDropIndex(nextDropItemInfo.m_proposedDropIndex);
	event.SetDataFormat(format);
	event.SetDropEffect(def);

	wxDragResult result = def;

	if (HandleWindowEvent(event) && event.IsAllowed())
	{
		// Processing handled event

		result = event.GetDropEffect();
		switch (result)
		{
		case wxDragCopy:
		case wxDragMove:
		case wxDragLink:
			break;

		case wxDragNone:
		case wxDragCancel:
		case wxDragError:
		{
			RemoveDropHint();
			return result;
		}
		}
	}
	else
	{
		RemoveDropHint();
		return wxDragNone;
	}

	if (nextDropItemInfo.m_hint != DropHint_None)
	{
		if (m_dropItemInfo.m_hint != nextDropItemInfo.m_hint ||
			m_dropItemInfo.m_row != nextDropItemInfo.m_row)
		{
			RefreshDropHint();   // refresh previous rows
		}

		m_dropItemInfo.m_hint = nextDropItemInfo.m_hint;
		m_dropItemInfo.m_row = nextDropItemInfo.m_row;

		RefreshDropHint();
	}
	else
	{
		RemoveDropHint();
	}

	m_dropItemInfo = nextDropItemInfo;

	return result;
}

bool ibDataViewCtrl::OnDrop(wxDataFormat format, wxCoord x, wxCoord y)
{
	RemoveDropHint();

	DropItemInfo dropItemInfo = GetDropItemInfo(x, y);

	ibDataViewEvent event(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, this, dropItemInfo.m_item);
	event.SetProposedDropIndex(dropItemInfo.m_proposedDropIndex);
	event.SetDataFormat(format);
	if (!HandleWindowEvent(event) || !event.IsAllowed())
		return false;

	return true;
}

wxDragResult ibDataViewCtrl::OnData(wxDataFormat format, wxCoord x, wxCoord y,
	wxDragResult def)
{
	DropItemInfo dropItemInfo = GetDropItemInfo(x, y);

	ibDataViewDropTarget* const
		target = static_cast<ibDataViewDropTarget*>(GetDropTarget());
	wxDataObjectComposite* const obj = target->GetCompositeDataObject();

	ibDataViewEvent event(wxEVT_DATAVIEW_ITEM_DROP, this, dropItemInfo.m_item);
	event.SetProposedDropIndex(dropItemInfo.m_proposedDropIndex);
	event.InitData(obj, format);
	event.SetDropEffect(def);
	if (!HandleWindowEvent(event) || !event.IsAllowed())
		return wxDragNone;

	return def;
}

void ibDataViewCtrl::OnLeave()
{
	RemoveDropHint();
}

wxBitmap ibDataViewCtrl::CreateItemBitmap(unsigned int row, int& indent)
{
	int height = GetLineHeight(row);
	int width = 0;
	unsigned int cols = GetColumnCount();
	unsigned int col;
	for (col = 0; col < cols; col++)
	{
		ibDataViewColumn* column = GetColumn(col);
		if (column->IsHidden())
			continue;      // skip it!
		width += column->GetWidth();
	}

	indent = 0;
	if (!IsList())
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(row);
		indent = GetIndent() * node->GetIndentLevel();
		indent += wxRendererNative::Get().GetExpanderSize(this).GetWidth();
	}
	width -= indent;

	wxBitmap bitmap;
#ifdef wxHAS_DPI_INDEPENDENT_PIXELS
	bitmap.CreateWithDIPSize(width, height, GetDPIScaleFactor());
#else
	bitmap.Create(width, height);
	bitmap.SetScaleFactor(GetDPIScaleFactor());
#endif
	wxMemoryDC dc(bitmap);
	dc.SetFont(GetFont());
	dc.SetPen(*wxBLACK_PEN);
	dc.SetBrush(*wxWHITE_BRUSH);
	dc.DrawRectangle(0, 0, width, height);

	ibDataViewModel* model = GetModel();

	ibDataViewColumn* const
		expander = GetExpanderColumnOrFirstOne(this);

	int x = 0;
	for (col = 0; col < cols; col++)
	{
		ibDataViewColumn* column = GetColumn(col);
		ibDataViewRenderer* cell = column->GetRenderer();

		if (column->IsHidden())
			continue;       // skip it!

		width = column->GetWidth();

		if (column == expander)
			width -= indent;

		ibDataViewItem item = GetItemByRow(row);
		if (cell->PrepareForItem(model, item, column->GetModelColumn()))
		{
			wxRect item_rect(x, 0, width, height);
			item_rect.Deflate(FromDIP(PADDING_RIGHTLEFT), 0);

			// dc.SetClippingRegion( item_rect );
			cell->WXCallRender(item_rect, &dc, 0);
			// dc.DestroyClippingRegion();
		}

		x += width;
	}

	return bitmap;
}

bool ibDataViewCtrl::DoEnableDropTarget(const wxVector<wxDataFormat>& formats)
{
	ibDataViewDropTarget* dt = NULL;
	if (wxDataObjectComposite* dataObject = CreateDataObject(formats))
	{
		dt = new ibDataViewDropTarget(dataObject, this);
	}

	SetDropTarget(dt);

	return true;
}

#endif // wxUSE_DRAG_AND_DROP
void ibDataViewCtrl::OnColumnResized()
{
	// THE CACHED GEOMETRY GOES FIRST — an interactive drag does not run through
	// OnColumnChange, so without this the layout kept the widths from before the drag and
	// the table would not resize at all.
	InvalidateColumnLayout();

	// AND THE SCROLLBAR FOLLOWS THE DRAG. Publish the width the columns now take, then ask for
	// the bar to be re-derived — ASK, not do, while a drag is in progress. AdjustScrollbars
	// pulls a whole window-geometry cascade behind it and paying that per mouse-move is what
	// made the drag feel sticky; deferring it to the mouse being RELEASED was worse in a
	// different way: crossing the table's edge mid-drag left the rows laid out for the old
	// width, so the bar appeared only on release and the row visibly caught up with the header
	// afterwards. A request is served once per frame, which is exactly the right cadence: one
	// cascade per repaint instead of one per pixel.
	UpdateColumnSizes();

	if (WXIsColumnDragActive())
		RequestScrollbarSync();
	else
		SyncHorizontalScrollbar();

	UpdateDisplay();
}

// THE HORIZONTAL SCROLLBAR, RE-DERIVED.
//
// AdjustScrollbars turns the virtual width into a range by dividing it by the scroll RATE —
// so with a rate of 0 the range is 0 and no scrollbar appears however far the columns
// overflow. The rate used to be set in two unrelated places (a window resize and
// RecalculateDisplay), which is why widening a COLUMN published the right virtual width and
// still showed no scrollbar: nobody re-derived it on that path.
//
// Only the x-rate is touched, deliberately: forcing a y-rate would raise a vertical
// scrollbar on controls that never had one (it stays 0 until RecalculateDisplay sets it) —
// the small tablebox preview in the designer, for one.
// THE BAR ON THE NEXT FRAME, AND ONCE PER FRAME. Whoever changes the widths says so and goes
// on; idle serves the request, so twenty mouse-moves between two repaints cost one cascade.
//
// It also makes the two hosts behave alike. The bar used to appear during a drag in the thick
// client and only on release in the Designer — same control, same table: the client's form was
// getting a size cascade from its own layout that re-derived the bar as a side effect, and the
// Designer's dialog was not. Asking the control itself for the frame depends on nobody's layout.
void ibDataViewCtrl::RequestScrollbarSync()
{
	m_scrollSyncPending = true;
	wxWakeUpIdle();
}

void ibDataViewCtrl::SyncHorizontalScrollbar()
{
	m_scrollSyncPending = false;

	if (m_tableAreaWin == nullptr)
		return;

	int xUnit = 0, yUnit = 0;
	GetScrollPixelsPerUnit(&xUnit, &yUnit);

	if (xUnit <= 0)
		SetScrollRate(1, yUnit);

	AdjustScrollbars();
}

void ibDataViewCtrl::OnColumnWidthChange(unsigned int idx)
{
	InvalidateColBestWidth(GetColumn(idx));

	OnColumnChange(idx);
}

void ibDataViewCtrl::OnColumnChange(unsigned int idx)
{
	if (m_headerAreaWin)
		m_headerAreaWin->UpdateColumn(idx);

	if (m_footerAreaWin)
		m_footerAreaWin->UpdateColumn(idx);

	// AND THE CACHED GEOMETRY GOES. Width, visibility, group — any of them moves every OTHER
	// column too, because a row is laid out as a whole (x AND band). Without this the widths
	// changed while the layout kept the old ones: measured as columns 126 wide and a right
	// edge still at 13×80 — the table drew narrow columns, would not fill its width, and
	// jumped on the next drag. AFTER telling the header, for the same reason as in
	// OnColumnsCountChanged: it keeps its own count, and a paint in between must not meet the
	// two disagreeing.
	InvalidateColumnLayout();

	UpdateDisplay();
}

void ibDataViewCtrl::OnColumnsCountChanged()
{
	// THE HEADER FIRST, THE GEOMETRY AFTER. The header keeps its own count of columns (it
	// draws them) and the layout is read while painting, so telling the header after dropping
	// the geometry would leave a paint in between that walks a count and a tree disagreeing
	// about how many columns there are — it crashed on the first form with a table.
	if (m_headerAreaWin)
		m_headerAreaWin->SetColumnCount(GetColumnCount());

	if (m_footerAreaWin)
		m_footerAreaWin->SetColumnCount(GetColumnCount());

	// A column arrived or left: every position in the row moves, so the cached geometry goes.
	InvalidateColumnLayout();

	int editableCount = 0;

	const unsigned cols = GetColumnCount();
	for (unsigned i = 0; i < cols; i++)
	{
		ibDataViewColumn* c = GetColumn(i);
		if (c->IsHidden())
			continue;
		if (c->GetRenderer()->GetMode() != wxDATAVIEW_CELL_INERT)
			editableCount++;
	}

	m_useCellFocus = (editableCount > 0);

	UpdateDisplay();
}

void ibDataViewCtrl::DoSetExpanderColumn()
{
	InvalidateColBestWidth(GetExpanderColumn());

	UpdateDisplay();
}

void ibDataViewCtrl::DoSetIndent()
{
	UpdateDisplay();
}

bool ibDataViewCtrl::SetRowHeight(int lineHeight)
{
	if (!m_tableAreaWin)
		return false;

	// What is being set is the height of ONE BAND — the row itself is as many of
	// them as the column groups make it (one, when nothing is grouped, which is the
	// old meaning unchanged).
	m_bandHeight = lineHeight;
	m_lineHeight = lineHeight * wxMax(GetRowBandCount(), 1);
	return true;
}

int ibDataViewCtrl::GetRowHeight() const
{
	if (!m_tableAreaWin)
		return 0;

	return m_lineHeight;
}

int ibDataViewCtrl::GetDefaultRowHeight() const
{
	const int SMALL_ICON_HEIGHT = FromDIP(16);

#ifdef __WXMSW__
	// We would like to use the same line height that Explorer uses. This is
	// different from standard ListView control since Vista.
	if (wxGetWinVersion() >= wxWinVersion_Vista)
		return wxMax(SMALL_ICON_HEIGHT, GetCharHeight()) + FromDIP(6);
	else
#endif // __WXMSW__
		return wxMax(SMALL_ICON_HEIGHT, GetCharHeight()) + FromDIP(1);
}

int ibDataViewCtrl::GetModelColumnIndex(unsigned int model_column) const
{
	const int count = GetColumnCount();
	for (int index = 0; index < count; index++)
	{
		ibDataViewColumn* column = GetColumn(index);
		if (column->GetModelColumn() == model_column)
			return index;
	}
	return wxNOT_FOUND;
}

class ibDataViewMaxWidthCalculator : public wxMaxWidthCalculatorBase
{
public:
	ibDataViewMaxWidthCalculator(const ibDataViewCtrl* dvc,
		ibDataViewRenderer* renderer,
		const ibDataViewModel* model,
		size_t model_column,
		int expanderSize)
		: wxMaxWidthCalculatorBase(model_column),
		m_dvc(dvc),
		m_renderer(renderer),
		m_model(model),
		m_expanderSize(expanderSize)
	{
		int index = dvc->GetModelColumnIndex(model_column);
		ibDataViewColumn* column = index == wxNOT_FOUND ? NULL : dvc->GetColumn(index);
		m_isExpanderCol =
			!dvc->IsList() &&
			(column == 0 ||
				GetExpanderColumnOrFirstOne(const_cast<ibDataViewCtrl*>(dvc)) == column);
	}

	virtual void UpdateWithRow(int row) wxOVERRIDE
	{
		int width = 0;
		ibDataViewItem item;

		if (m_isExpanderCol)
		{
			ibDataViewTreeNode* node = m_dvc->GetTreeNodeByRow(row);
			item = node->GetItem();
			width = m_dvc->GetIndent() * node->GetIndentLevel() + m_expanderSize;
		}
		else
		{
			item = m_dvc->GetItemByRow(row);
		}

		if (m_model != NULL && m_model->HasValue(item, GetColumn()))
		{
			if (m_renderer->PrepareForItem(m_model, item, GetColumn()))
				width += m_renderer->GetSize().x;
		}

		UpdateWithWidth(width);
	}

private:
	const ibDataViewCtrl* m_dvc;
	ibDataViewRenderer* m_renderer;
	const ibDataViewModel* m_model;
	bool m_isExpanderCol;
	int m_expanderSize;
};

unsigned int ibDataViewCtrl::GetBestColumnWidth(ibDataViewColumn* column) const
{
	if (column == nullptr)
		return 0;

	if (column->WXBestWidth() != 0)
		return column->WXBestWidth();

	const int count = GetRowCount();
	ibDataViewRenderer* renderer =
		const_cast<ibDataViewRenderer*>(column->GetRenderer());

	ibDataViewMaxWidthCalculator calculator(this, renderer,
		GetModel(), column->GetModelColumn(),
		GetRowHeight());

	calculator.UpdateWithWidth(column->GetMinWidth());

	if (m_headerAreaWin)
		calculator.UpdateWithWidth(m_headerAreaWin->GetColumnTitleWidth(*column));

	if (m_footerAreaWin)
		calculator.UpdateWithWidth(m_footerAreaWin->GetColumnTitleWidth(*column));

	const wxPoint origin = CalcUnscrolledPosition(wxPoint(0, 0));
	calculator.ComputeBestColumnWidth(count,
		GetLineAt(origin.y),
		GetLineAt(origin.y + GetClientSize().y));

	int max_width = calculator.GetMaxWidth();
	if (max_width > 0)
		max_width += 2 * FromDIP(PADDING_RIGHTLEFT);

	column->WXSetBestWidth(max_width);
	return max_width;
}

// THE DRAG ENDS HERE: the member moves, and everyone who cares is told once.
//
// `holder` and `at` come from the header, which is the only place that knows WHERE the
// mouse let go — inside a group, or at its edge, which means beside it one level up. The
// tree IS the order, so moving the member is the whole of the move; there is no display
// order to record it in a second time.
void ibDataViewCtrl::WXMoveColumn(ibDataViewColumn* column, ibDataViewColumnGroup* holder, unsigned int at)
{
	if (column == nullptr || holder == nullptr)
		return;

	holder->InsertColumn(at, column);

	InvalidateColumnLayout();
	UpdateDisplay();

	ibDataViewEvent event(wxEVT_DATAVIEW_COLUMN_REORDERED, this, column);
	event.SetColumn(GetColumnIndex(column));
	ProcessWindowEvent(event);
}
// MEMBERSHIP IS OWNERSHIP, so this frees what is still hanging on the tree, and only
// the destructor calls it: everything detached along the way has already been handed
// back to whoever detached it.
void ibDataViewCtrl::DoClearColumns()
{
	const unsigned int count = m_rootGroup->GetColumnCount();
	wxVector<ibDataViewColumn*> owned;
	owned.reserve(count);
	for (unsigned int idx = 0; idx < count; idx++)
		owned.push_back(m_rootGroup->GetColumn(idx));

	// The columns are taken out of the TREE first and only then freed — and they are
	// forgotten one by one (SetParent(nullptr)) rather than handed back to the root the
	// way ungrouping does it, because there is nothing here to hand them to: this runs
	// while the control is dying.
	FreeGroupsUnder(m_rootGroup);
	m_rootGroup->RemoveAllMembers();

	for (ibDataViewColumn* column : owned) {
		if (column != nullptr) {
			column->SetParent(nullptr);
			delete column;
		}
	}
}

void ibDataViewCtrl::InvalidateColBestWidth(ibDataViewColumn* column)
{
	if (column == nullptr)
		return;

	column->WXSetBestWidthDirty();
	m_colsDirty = true;
}

void ibDataViewCtrl::InvalidateColBestWidths()
{
	// mark all columns as dirty:
	const unsigned int count = GetColumnCount();
	for (unsigned int idx = 0; idx < count; idx++)
		GetColumn(idx)->WXSetBestWidthDirty();

	m_colsDirty = true;
}

void ibDataViewCtrl::UpdateColWidths()
{
	m_colsDirty = false;

	if (!m_headerAreaWin && !m_footerAreaWin)
		return;

	const unsigned int count = GetColumnCount();
	for (unsigned int idx = 0; idx < count; idx++)
	{
		ibDataViewColumn* column = GetColumn(idx);

		// Note that we have to have an explicit 'dirty' flag here instead of
		// checking if the width==0, as is done in GetBestColumnWidth().
		//
		// Testing width==0 wouldn't work correctly if some code called
		// GetWidth() after col. width invalidation but before
		// ibDataViewCtrl::UpdateColWidths() was called at idle time. This
		// would result in the header's column width getting out of sync with
		// the control itself.
		if (column->WXIsBestWidthDirty())
		{
			if (m_headerAreaWin)
				m_headerAreaWin->UpdateColumn(idx);
			if (m_footerAreaWin)
				m_footerAreaWin->UpdateColumn(idx);

			column->WXSetBestWidthDirty(false);
		}
	}
}

void ibDataViewCtrl::OnInternalIdle()
{
	ibDataViewCtrlBase::OnInternalIdle();


	if (m_colsDirty)
		UpdateColWidths();

	// The frame a width change asked for (see RequestScrollbarSync) — before the display is
	// recalculated, since the scroll range is one of the things it is recalculated from.
	if (m_scrollSyncPending)
		SyncHorizontalScrollbar();

	if (m_dirty)
	{
		RecalculateDisplay();
		m_dirty = false;
	}

	// Bootstrap path: AssociateModel cannot fetch yet because the
	// control's height (and therefore the desired batch size) isn't
	// known until layout settles.  We wait for the first idle after
	// the size is real, then ask the model for the initial window.
	if (m_pagedNeedsBootstrap && m_tableAreaWin != nullptr
	    && GetCountPerPage() > 0) {
		PagedBootstrap();
	}

	// Sample whether the model is still waiting on data. Here rather than in a
	// handler of its own because idle already runs on every pass, and the answer
	// is only interesting between frames.
	UpdateBusyIndicator();

	// Top-up fill: keep buf at target size whenever forward data is
	// available.  Driven by the always-checked condition `loaded <
	// cpp + slack`, NOT gated on m_pagedSizeChanged — Windows starves
	// idle events during a resize-drag, so the flag may be consumed
	// (or never even read) before the user releases the mouse.  If
	// the size flag is set we clear it (legacy hint), but the actual
	// fetch decision is condition-driven so resize→refresh-immediately
	// flows still load the missing tail rows.
	if (m_tableAreaWin != nullptr && !m_pagedNeedsBootstrap) {
		const int cpp = GetCountPerPage();
		const int loaded = (m_root != nullptr) ? GetRowCount() : 0;
		// Fill ONLY to the visible viewport, never a slack over-fetch: a refresh that already filled the
		// viewport must NOT eagerly pull the rest of the list down (that is what looked like "rows keep
		// getting ADDED on refresh" instead of loading on scroll). The tail loads on a real scroll-down
		// (OnScroll: dir>0 && marginFwd<slack -> PagedFetchForward). This still fills a resize / partial
		// bootstrap where the buffer is genuinely shorter than the viewport.
		if (cpp > 0 && loaded < cpp
		    && m_pagedHasMoreFwd && m_pagedFetchingFwd == 0
		    && m_pagedFetchingBwd == 0) {
			// Skip if a backward fetch is in flight: anchor-cursor
			// Bootstrap dispatches both directions, the backward
			// result arrives shortly and brings the buffer to target
			// size on its own.  Firing forward here too would race
			// the backward result, push the buffer past target, and
			// then trigger an aggressive trim inside
			// OnPagedFetchForwardComplete that wipes the just-loaded
			// backward rows — visually "rows disappear" near the
			// saved top.
						const int batch = cpp - loaded;   // fill to the viewport exactly — no slack over-fetch
			PagedFetchForward(batch);
					}
	}

	// Anti-flicker Thaw — THE WATCHDOG COPY of it.
	//
	// The freeze covers the REBUILD and only the rebuild: wipe → build → focus →
	// scroll, armed where the portion lands (OnPagedFetchResetComplete) and dropped
	// at the end of it. Nothing in between is worth showing — the half-built tree is
	// not an answer — and painting it would cost a pass per step. That is the freeze
	// paying for itself twice: no intermediate frames, and no work drawing them.
	//
	// What it must NOT cover is the wait before the portion and the settle after it.
	// Before: the old rows are still there, still the best answer available, and the
	// arc spins over them from its own window (which no freeze touches — that is why
	// it has one). After: top-ups and the backward pull move the viewport in the
	// open, deliberately. A freeze held over the settle hides real defects — a focus
	// landing on the wrong row, a selection jumping — behind a clean picture, which
	// is exactly how those stayed unnoticed while it did.
	//
	// This copy is what ends the freeze when the rebuild does not: a delivery that
	// returned early on a failed read, or one whose control was gone by then.
	if (!m_pagedNeedsBootstrap && !IsFetchInFlight())
		EndBootstrapFreeze();
}

void ibDataViewCtrl::EndBootstrapFreeze()
{
	if (!m_pagedFrozenForBootstrap)
		return;

	// Bootstrap and any forward/backward fetches flipped m_dirty=true via
	// UpdateDisplay() but did not call RecalculateDisplay synchronously — that ran
	// on the EMPTY tree (just before bootstrap), so virtual size is still 0 when we
	// reach Thaw.  Without this, wx paints the freshly-populated rows against the
	// stale (empty) virtual size first, then the next idle pass re-runs
	// RecalculateDisplay and triggers a second paint — visible flicker.  Fold the
	// recalc in here so Thaw releases a single composite paint with the correct
	// virtual size.
	if (m_dirty) {
		RecalculateDisplay();
		m_dirty = false;
	}
	m_pagedFrozenForBootstrap = false;
	// Match the rows-area-only Freeze in OnPagedFetchResetComplete /
	// AssociateModel — outer ctrl was never frozen, no Thaw needed.
	if (m_tableAreaWin) m_tableAreaWin->Thaw();
}

ibDataViewColumn* ibDataViewCtrl::GetSortingColumn() const
{
	if (m_sortingColumnIdxs.empty())
		return NULL;

	return GetColumn(m_sortingColumnIdxs.front());
}

wxVector<ibDataViewColumn*> ibDataViewCtrl::GetSortingColumns() const
{
	wxVector<ibDataViewColumn*> out;

	for (wxVector<int>::const_iterator it = m_sortingColumnIdxs.begin(),
		end = m_sortingColumnIdxs.end();
		it != end;
		++it)
	{
		out.push_back(GetColumn(*it));
	}

	return out;
}

ibDataViewItem ibDataViewCtrl::DoGetCurrentItem() const
{
	return GetItemByRow(GetCurrentRow());
}

void ibDataViewCtrl::DoSetCurrentItem(const ibDataViewItem& item)
{
	const int row = GetRowByItem(item);

	const unsigned oldCurrent = GetCurrentRow();
	if (static_cast<unsigned>(row) != oldCurrent)
	{
		ChangeCurrentRow(row);
		RefreshRow(oldCurrent);
		RefreshRow(row);
	}
}

ibDataViewColumn* ibDataViewCtrl::GetCurrentColumn() const
{
	return m_currentCol;
}

int ibDataViewCtrl::GetSelectedItemsCount() const
{
	return GetSelections().GetSelectedCount();
}

ibDataViewItem ibDataViewCtrl::GetTopItem() const
{
	unsigned int item = GetFirstVisibleRow();
	ibDataViewTreeNode* node = NULL;
	ibDataViewItem dataitem;

	if (!IsVirtualList())
	{
		node = GetTreeNodeByRow(item);
		if (node == NULL) return ibDataViewItem();

		dataitem = node->GetItem();
	}
	else
	{
		dataitem = ibDataViewItem(wxUIntToPtr(item + 1));
	}

	return dataitem;
}

int ibDataViewCtrl::GetCountPerPage() const
{
	wxSize size = GetClientSize();
	return size.y / m_lineHeight;
}

int ibDataViewCtrl::GetSelections(ibDataViewItemArray& sel) const
{
	sel.Empty();
	const wxSelectionStore& selections = GetSelections();

	wxSelectionStore::IterationState cookie;
	for (unsigned row = selections.GetFirstSelectedItem(cookie);
		row != wxSelectionStore::NO_SELECTION;
		row = selections.GetNextSelectedItem(cookie))
	{
		ibDataViewItem item = GetItemByRow(row);
		if (item.IsOk())
		{
			sel.Add(item);
		}
		else
		{
			wxFAIL_MSG("invalid item in selection - bad internal state");
		}
	}

	return sel.size();
}

void ibDataViewCtrl::SetSelections(const ibDataViewItemArray& sel)
{
	ClearSelection();

	if (sel.empty())
		return;

	ibDataViewItem last_parent;

	for (size_t i = 0; i < sel.size(); i++)
	{
		ibDataViewItem item = sel[i];
		ibDataViewItem parent = item.GetParentItem();
		if (parent)
		{
			if (parent != last_parent)
				ExpandAncestors(item);
		}

		last_parent = parent;
		int row = GetRowByItem(item);
		if (row >= 0)
			SelectRow(static_cast<unsigned int>(row), true);
	}

	// Also make the last item as current item
	DoSetCurrentItem(sel.Last());
}

void ibDataViewCtrl::Select(const ibDataViewItem& item)
{
	ExpandAncestors(item);

	// Paged path: stamp item into the bootstrap-restore channel so a
	// pending PagedBootstrap matches the (possibly stub) item via
	// ibDataViewObject::IsEqualTo against the freshly-fetched batch.
	// For non-paged or already-bootstrapped paged the GetRowByItem
	// branch below handles the wxSelectionStore + ChangeCurrentRow
	// path; restore-selection then sits unused (cleared on next
	// PagedRefresh).  Single Select call covers both timings.
	if (GetModel() != nullptr && GetModel()->IsPagedModel())
		SetPagedRestoreSelection(item);

	int row = GetRowByItem(item);
	if (row >= 0)
	{
		// Unselect all rows before select another in the single select mode
		if (IsSingleSel())
			UnselectAllRows();

		SelectRow(row, true);

		// Also set focus to the selected item
		ChangeCurrentRow(row);
	}
}

void ibDataViewCtrl::Unselect(const ibDataViewItem& item)
{
	int row = GetRowByItem(item);
	if (row >= 0)
		SelectRow(row, false);
}

bool ibDataViewCtrl::IsSelected(const ibDataViewItem& item) const
{
	int row = GetRowByItem(item);
	if (row >= 0)
	{
		return IsRowSelected(row);
	}
	return false;
}

bool ibDataViewCtrl::SetHeaderAttr(const wxItemAttr& attr)
{
	if (!m_headerAreaWin && !m_footerAreaWin)
		return false;

	// Call all functions unconditionally to reset the previously set
	// attributes, if any.
	if (m_headerAreaWin)
	{
		m_headerAreaWin->SetForegroundColour(attr.GetTextColour());
		m_headerAreaWin->SetBackgroundColour(attr.GetBackgroundColour());
		m_headerAreaWin->SetFont(attr.GetFont());
	}

	if (m_footerAreaWin)
	{
		m_footerAreaWin->SetForegroundColour(attr.GetTextColour());
		m_footerAreaWin->SetBackgroundColour(attr.GetBackgroundColour());
		m_footerAreaWin->SetFont(attr.GetFont());
	}

	// If the font has changed, the size of the header might need to be
	// updated.
	Layout();

	return true;
}

bool ibDataViewCtrl::SetAlternateRowColour(const wxColour& colour)
{
	m_alternateRowColour = colour;
	return true;
}

void ibDataViewCtrl::SelectAll()
{
	m_selection.SelectRange(0, GetRowCount() - 1);

	m_tableAreaWin->Refresh();

	if (m_tableFrozenRowAreaWin)
		m_tableFrozenRowAreaWin->Refresh();

	if (m_tableFrozenColAreaWin)
		m_tableFrozenColAreaWin->Refresh();

	if (m_tableFrozenCornerAreaWin)
		m_tableFrozenCornerAreaWin->Refresh();
}

void ibDataViewCtrl::UnselectAll()
{
	UnselectAllRows();
}

void ibDataViewCtrl::SetSelectionMode(ibDataViewSelectionMode selmode)
{
	if (m_selectionMode != selmode)
	{
		m_selectionMode = selmode;

		UpdateDisplay();
	}
}

ibDataViewSelectionMode ibDataViewCtrl::GetSelectionMode() const
{
	return m_selectionMode;
}

// (SyncColumnArrowsFromModel DELETED — the header arrow is a pure FRONT concern now. The OES tablebox sets it
//  on the clicked column in OnColumnClick, and each tablebox column re-reads the composer's active sort on
//  rebuild (ibValueModelTableBoxColumn OnUpdated), matching by its OWN bound field name. No model-side bridge.)

ibDataViewItem ibDataViewCtrl::GetEffectiveFetchParent() const
{
	// Drill-into-folder always wins: if the user is inside a folder,
	// fetch its children regardless of the view mode (Hierarchical's
	// usual case, but legal in any mode).
	if (!m_topParentChain.IsEmpty())
		return m_topParentChain[0];

	// A FLAT List view passes the ignore-parent SENTINEL → the model walks the WHOLE table (every row, no
	// parent scope) in one ORDER BY. A Tree view passes an EMPTY parent → the model returns top-level rows
	// only (the roots), and the expand walk fetches each folder's children. This is how the FRONTEND view
	// mode tells the model flat-vs-tree — the hierarchy itself is the queryable's inherent property.
	if (m_viewMode == ibDataViewViewMode::ibDataViewList)
		return s_constIgnoreParent;
	return ibDataViewItem();
}

void ibDataViewCtrl::SetViewMode(ibDataViewViewMode viewMode)
{
	const bool modeChanged = (m_viewMode != viewMode);
	m_viewMode = viewMode;

	if (modeChanged)
	{
		const ibDataViewItem& selection =
			GetItemByRow(m_currentRow);

		// Freeze across the whole switch — InitializeFrozenWindows
		// can show/hide m_tableFrozenRowAreaWin, CalcWindowSizes
		// re-lays out the children, Cleared() schedules an async
		// PagedRefresh that wipes m_root and rebuilds from a fresh
		// fetch.  Without freeze the user sees flickering empty
		// rows + sequential repaints between each step.
		ScopedPagedFreeze freeze(this);

		if (m_viewMode == ibDataViewViewMode::ibDataViewHierarchical) {

			ibDataViewModel* model = GetModel();

			// Entering Hierarchical from List/Tree with a selection
			// inside folders: ask the model to materialise the
			// breadcrumb chain (direct parent → topmost ancestor) so
			// the user sees the selected row sitting under its full
			// folder path.  m_topParentChain[0] = deepest folder
			// (selection's direct parent); index N-1 = topmost.
			//
			// If selection is at the root, or the model is non-
			// hierarchical, BuildAncestorBreadcrumb returns empty —
			// chain stays empty and the view shows top-level rows.
			//
			// We don't preserve the previous Hierarchical drill state
			// across mode switches: every entry into Hierarchical
			// re-anchors on the current selection so the user lands
			// where they're looking.
			m_topParentChain.Clear();
			if (model != NULL && selection.IsOk()) {
				model->BuildAncestorBreadcrumb(selection, m_topParentChain);
			}
			m_countFrozenHierarchicalRows =
				static_cast<int>(m_topParentChain.GetCount());
		}
		else
		{
			// Leaving Hierarchical — drop the drill chain so the next
			// fetch goes against the top level (or against the flat-
			// scan sentinel for List view of a hierarchical model).
			m_topParentChain.Clear();
			m_countFrozenHierarchicalRows = 0;
		}

		InitializeFrozenWindows();

		// recompute dimensions
		InvalidateBestSize();

		CalcWindowSizes();

		// View-mode switch in paged mode is async: SchedulePagedRefresh
		// dispatches PagedRefresh through ibSession::Submit (inline on
		// desktop), which wipes m_root and kicks PagedBootstrap to
		// GetFirstFetch the new layout.  We pass the captured row as
		// preferSelection — PagedRefresh stamps it into
		// m_pagedRestoreSelection, PagedBootstrap passes it as anchor
		// to GetFirstFetch and (post-fetch) finds the matching row by
		// ibDataViewObject::IsEqualTo (logical equality on m_objGuid /
		// m_nodeValues), restoring focus + selection in the new layout.
		//
		// We deliberately do NOT call Cleared() here.  Cleared() in
		// paged mode is just `SchedulePagedRefresh()` with an empty
		// preferSelection; under inline Submit the first refresh fires
		// synchronously and clears m_currentRow, so the second
		// SchedulePagedRefresh inside Cleared() would re-fire
		// PagedRefresh with no selection and wipe our stamp.  For
		// non-paged native models the path is unreachable today (every
		// ibValueModel-derived model is paged); add an explicit branch
		// here when that changes.
		SchedulePagedRefresh(selection);

		// Send the view-mode-set event to ibDataViewCtrl itself so
		// listeners (column-header arrows, status-bar widgets) can
		// react to the new mode.  Selection-restore is async — by the
		// time PagedBootstrap finishes, m_currentRow / m_selection are
		// already updated by the bootstrap's own restore logic.
		ibDataViewEvent cache_event(wxEVT_DATAVIEW_VIEW_SET, this, selection);
		cache_event.SetViewMode(viewMode);
		ProcessWindowEvent(cache_event);

		// TODO Tree-mode deep-ancestor expand: ExpandAncestors used to
		// walk the parent chain from `selection` up and ToggleOpen each
		// node so the focused row was reachable on initial render.  In
		// paged mode the tree is empty at this point — ancestor expand
		// has to happen after each lazy fetch.  Deferred until
		// hierarchical drill positioning is solid.
	}  // ScopedPagedFreeze auto-Thaws here
}

ibDataViewViewMode ibDataViewCtrl::GetViewMode() const
{
	return m_viewMode;
}

void ibDataViewCtrl::SetTopParent(const ibDataViewItem& item)
{
		if ((!IsList()) && m_viewMode == ibDataViewViewMode::ibDataViewHierarchical)
	{
		ibDataViewModel* model = GetModel();

		// Drill flow:
		//   click on a crumb already in m_topParentChain → drill UP past
		//     it (pop chain[0..i] so chain[i+1] becomes new top).
		//   click on a folder NOT in chain → drill INTO that folder.
		// Drill state lives on the control (m_topParentChain); the model
		// is stateless and just receives the front-of-chain as parent.
		// Locating clicked in chain detects both chain[0] (current top)
		// and ancestor crumbs (chain[1+]); without this an ancestor
		// crumb falls through to drill-INTO and Insert duplicates it.
		int chainIdx = -1;
		if (model != NULL && item.IsOk()) {
			for (int i = 0; i < (int)m_topParentChain.GetCount(); ++i) {
				if (m_topParentChain[i] == item) { chainIdx = i; break; }
			}
		}
		if (model != NULL && chainIdx >= 0)
		{
			// Drill UP — pop chain[0..chainIdx] inclusive.  i=0 case
			// (click current top) pops chain[0] only; i>0 (click ancestor
			// crumb) pops everything down to and including the clicked
			// crumb so its parent becomes the new top.
			for (int i = 0; i <= chainIdx && !m_topParentChain.IsEmpty(); ++i)
				m_topParentChain.RemoveAt(0);
			m_countFrozenHierarchicalRows =
				static_cast<int>(m_topParentChain.GetCount());

			// Freeze paints across Cleared → bootstrap so the user
			// sees one transition (old folder → new folder) instead
			// of an empty table between Cleared and the deferred
			// PagedBootstrap fire on next idle.
			//
			// Drill changes the parent context — flag the upcoming
			// async PagedRefresh to NOT capture the OLD folder's top
			// / selection as restore anchors (PagedRefresh runs after
			// Cleared scheduled it and would otherwise overwrite any
			// reset we did synchronously here).
			m_pagedSkipRestoreCapture = true;
						{
				ScopedPagedFreeze freeze(this);
				// PagedRefresh sync (bypassing Cleared's async
				// SchedulePagedRefresh) — without this the bootstrap
				// runs only on the next OnInternalIdle, AFTER this
				// freeze block has Thawed → stale folder paints
				// briefly.  Direct call arms m_pagedNeedsBootstrap and
				// the sync PagedBootstrap fires inside the freeze.
				PagedRefresh();
				if (m_pagedNeedsBootstrap && m_tableAreaWin != nullptr
				    && GetCountPerPage() > 0) {
					// DISPATCHES, does not fill: the new folder's rows
					// arrive on the UI thread when the read comes back
					// (OnPagedFetchResetComplete), inside a freeze of its own.
					// So this freeze no longer spans the rebuild — the
					// PREVIOUS folder stays on screen under the busy badge
					// until there is something to replace it with, which
					// is the same bargain every other paged read makes.
					PagedBootstrap();
				}
				// PagedBootstrap rebuilds m_root but only flips
				// m_dirty; the m_tableAreaWin virtual size still reflects
				// the pre-drill row count, so wxScrollHelper computes a
				// scroll range that fits one viewport and the thumb stays
				// pinned at top.  Force the virtual-size recompute now so
				// the scrollbar matches the freshly-built tree.
				RecalculateDisplay();
			}  // ScopedPagedFreeze auto-Thaws here

			InitializeFrozenWindows();

			// recompute dimensions
			InvalidateBestSize();

			CalcWindowSizes();
		}
		else if (model != NULL && item.IsContainer())
		{
			// Drill INTO — prepend the new folder onto the existing
			// chain.  Fetched rows have no backend tree-parent (model
			// is stateless), so the chain accumulates on the control.
			m_topParentChain.Insert(item, 0);
			m_countFrozenHierarchicalRows =
				static_cast<int>(m_topParentChain.GetCount());

			m_pagedSkipRestoreCapture = true;
						{
				ScopedPagedFreeze freeze(this);
				// Sync PagedRefresh (see drill UP branch above for
				// rationale — async Cleared path leaves stale folder
				// painting briefly between freeze.~Thaw and idle).
				PagedRefresh();
				if (m_pagedNeedsBootstrap && m_tableAreaWin != nullptr
				    && GetCountPerPage() > 0)
					PagedBootstrap();
				RecalculateDisplay();
			}  // ScopedPagedFreeze auto-Thaws here

			InitializeFrozenWindows();

			// recompute dimensions
			InvalidateBestSize();

			CalcWindowSizes();
		}
	}
}

void ibDataViewCtrl::EnsureVisibleRowCol(int row, int column)
{
	if (row < 0)
		row = 0;
	if (row > (int) GetRowCount())
		row = GetRowCount();

	int first = GetFirstVisibleRow();
	int last = GetLastFullyVisibleRow();

	if (row <= first)
	{
		ScrollTo(row, column);
	}
	else if (row > last)
	{
		if (!HasFlag(wxDV_VARIABLE_LINE_HEIGHT))
		{
			// Simple case as we can directly find the item to scroll to.
			ScrollTo(row - last + first, column);
		}
		else
		{
			// calculate scroll position based on last visible item
			const int itemStart = GetLineStart(row);
			const int itemHeight = GetLineHeight(row);
			const int clientHeight = GetSize().y;
			int scrollX, scrollY;
			GetScrollPixelsPerUnit(&scrollX, &scrollY);
			int scrollPosY =
				(itemStart + itemHeight - clientHeight + scrollY - 1) / scrollY;
			int scrollPosX = -1;
			if (column != -1 && scrollX)
				scrollPosX = GetColumnStart(column) / scrollX;
			Scroll(scrollPosX, scrollPosY);
		}
	}
}

void ibDataViewCtrl::EnsureVisible(const ibDataViewItem& item, const ibDataViewColumn* column)
{
	ExpandAncestors(item);

	RecalculateDisplay();

	int row = GetRowByItem(item);
	if (row >= 0)
	{
		if (column == NULL)
			EnsureVisibleRowCol(row, -1);
		else
			EnsureVisibleRowCol(row, GetColumnIndex(column));
	}
}

void ibDataViewCtrl::HitTest(const wxPoint& point, ibDataViewItem& item,
	ibDataViewColumn*& column) const
{
	// Convert from ibDataViewCtrl coordinates to ibDataViewCtrl coordinates.
	// (They can be different due to the presence of the header.).
	const wxPoint clientPt = ScreenToClient(ClientToScreen(point));
	HitTest(clientPt, item, column);
}

wxRect ibDataViewCtrl::GetItemRect(const ibDataViewItem& item,
	const ibDataViewColumn* column) const
{
	// Convert position from the main window coordinates to the control coordinates.
	// (They can be different due to the presence of the header.).
	wxRect r = GetItemRect(item, column);
	if (r.width || r.height)
	{
		const wxPoint ctrlPos = ScreenToClient(ClientToScreen(r.GetPosition()));
		r.SetPosition(ctrlPos);
	}
	return r;
}

void ibDataViewCtrl::DoExpand(const ibDataViewItem& item, bool expandChildren)
{
	int row = GetRowByItem(item);
	if (row != -1)
		ExpandRow(row, expandChildren);
}

void ibDataViewCtrl::Collapse(const ibDataViewItem& item)
{
	int row = GetRowByItem(item);
	if (row != -1)
		CollapseRow(row);
}

bool ibDataViewCtrl::IsExpanded(const ibDataViewItem& item) const
{
	int row = GetRowByItem(item);
	if (row != -1)
		return IsExpandedRow(row);
	return false;
}

void ibDataViewCtrl::EditItem(const ibDataViewItem& item, const ibDataViewColumn* column)
{
	wxCHECK_RET(item.IsOk(), "invalid item");
	wxCHECK_RET(column, "no column provided");

	StartEditing(item, column);
}

void ibDataViewCtrl::ResetAllSortColumns()
{
	// Must make copy, because unsorting will remove it from original vector
	wxVector<int> const copy(m_sortingColumnIdxs);
	for (wxVector<int>::const_iterator it = copy.begin(),
		end = copy.end();
		it != end;
		++it)
	{
		GetColumn(*it)->UnsetAsSortKey();
	}

	wxASSERT(m_sortingColumnIdxs.empty());
}

bool ibDataViewCtrl::AllowMultiColumnSort(bool allow)
{
	if (m_allowMultiColumnSort == allow)
		return true;

	m_allowMultiColumnSort = allow;

	// If disabling, must disable any multiple sort that are active
	if (!allow)
	{
		ResetAllSortColumns();

		if (ibDataViewModel* model = GetModel())
			model->Resort();
	}

	return true;
}


bool ibDataViewCtrl::IsColumnSorted(int idx) const
{
	for (wxVector<int>::const_iterator it = m_sortingColumnIdxs.begin(),
		end = m_sortingColumnIdxs.end();
		it != end;
		++it)
	{
		if (*it == idx)
			return true;
	}

	return false;
}

void ibDataViewCtrl::UseColumnForSorting(int idx)
{
	m_sortingColumnIdxs.push_back(idx);
}

void ibDataViewCtrl::DontUseColumnForSorting(int idx)
{
	for (wxVector<int>::iterator it = m_sortingColumnIdxs.begin(),
		end = m_sortingColumnIdxs.end();
		it != end;
		++it)
	{
		if (*it == idx)
		{
			m_sortingColumnIdxs.erase(it);
			return;
		}
	}

	wxFAIL_MSG("Column is not used for sorting");
}

void ibDataViewCtrl::ToggleSortByColumn(int column)
{
	m_headerAreaWin->ToggleSortByColumn(column);
}

void ibDataViewCtrl::DoEnableSystemTheme(bool enable, wxWindow* window)
{
	typedef wxSystemThemedControl<wxControl> Base;
	Base::DoEnableSystemTheme(enable, window);
	Base::DoEnableSystemTheme(enable, m_tableAreaWin);
	if (m_tableFrozenRowAreaWin)
		Base::DoEnableSystemTheme(enable, m_tableFrozenRowAreaWin);
	if (m_tableFrozenRowAreaWin)
		Base::DoEnableSystemTheme(enable, m_tableFrozenColAreaWin);
	if (m_tableFrozenCornerAreaWin)
		Base::DoEnableSystemTheme(enable, m_tableFrozenCornerAreaWin);
	if (m_headerAreaWin)
		Base::DoEnableSystemTheme(enable, m_headerAreaWin);
	if (m_footerAreaWin)
		Base::DoEnableSystemTheme(enable, m_footerAreaWin);
}

void ibDataViewCtrl::DrawTableContent(wxDC& dc, ibDataViewMainWindow* tableWindow)
{
	ibDataViewModel* model = GetModel();

	int cw, ch;
	tableWindow->GetClientSize(&cw, &ch);

	dc.SetBrush(GetBackgroundColour());
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(0, 0, cw, ch);

	if (tableWindow->GetType() == ibDataViewMainWindow::ibDataViewWindowNormal && IsEmpty())
	{
		// No items to draw.
		return;
	}
	else if (tableWindow->GetType() == ibDataViewMainWindow::ibDataViewWindowFrozenRow && wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows) == 0)
	{
		// No items to draw.
		return;
	}
	else if (tableWindow->GetType() == ibDataViewMainWindow::ibDataViewWindowFrozenCol && m_countFrozenCols == 0)
	{
		// No items to draw.
		return;
	}
	else if (tableWindow->GetType() == ibDataViewMainWindow::ibDataViewWindowFrozenCorner && m_countFrozenCols == 0 && wxMax(m_countFrozenRows, m_countFrozenHierarchicalRows) == 0)
	{
		// No items to draw.
		return;
	}

	// prepare the DC
	ibDataViewCtrl::PrepareDC(dc);

	wxPoint dcOrigin = dc.GetDeviceOrigin() - GetDataViewWindowOffset(tableWindow);

	if (tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenRow)
		dcOrigin.y = 0;

	if (tableWindow->GetType() & ibDataViewMainWindow::ibDataViewWindowFrozenCol)
		dcOrigin.x = 0;

	dc.SetDeviceOrigin(dcOrigin.x, dcOrigin.y);
	dc.SetFont(GetFont());

	int top, bottom, left, right;

	wxPoint gridOffset = GetDataViewWindowOffset(tableWindow);

	CalcDataViewWindowUnscrolledPosition(gridOffset.x, gridOffset.y, &left, &top, tableWindow);
	CalcDataViewWindowUnscrolledPosition(cw + gridOffset.x, ch + gridOffset.y, &right, &bottom, tableWindow);

	// compute which items needs to be redrawn
	unsigned int item_start = GetLineAt(wxMax(0, top));
	unsigned int item_count =
		wxMin((int)(GetLineAt(wxMax(0, top + bottom)) - item_start + 1),
			(int)(GetRowCount() - item_start));
	unsigned int item_last = item_start + item_count;

	// Per-paint cycle: log only the Normal area (frozen-row / col /
	// corner are derived from the same buffer, no need to spam).
	if (tableWindow->GetType() == ibDataViewMainWindow::ibDataViewWindowNormal) {
			}

	// Send the event to ibDataViewCtrl itself.
	ibDataViewEvent cache_event(wxEVT_DATAVIEW_CACHE_HINT, this, NULL);
	cache_event.SetCache(item_start, item_last - 1);

	ProcessWindowEvent(cache_event);

	// compute which columns needs to be redrawn
	unsigned int cols = GetColumnCount();
	if (!cols)
	{
		// we assume that we have at least one column below and painting an
		// empty control is unnecessary anyhow
		return;
	}

	// THE geometry of the row: which column sits where, across AND down. Flat is
	// the common case and keeps the old left-to-right arithmetic; once a column
	// group is in play a column is a rectangle in an (x, band) grid and the
	// clipping shortcut below no longer holds — every column is walked and the DC
	// clips what falls outside.
	const ibDataViewColumnLayout& columnLayout = GetColumnLayout();

	unsigned int col_start = 0;
	unsigned int col_last = 0;
	unsigned int x_start = 0;
	unsigned int x_last = 0;

	if (columnLayout.IsFlat())
	{
		for (x_start = 0; col_start < cols; col_start++)
		{
			ibDataViewColumn* col = GetColumn(col_start);
			if (col->IsHidden())
				continue;      // skip it!

			unsigned int w = col->GetWidth();
			if (x_start + w >= (unsigned int)left)
				break;

			x_start += w;
		}

		col_last = col_start;
		x_last = x_start;
		for (; col_last < cols; col_last++)
		{
			ibDataViewColumn* col = GetColumn(col_last);
			if (col->IsHidden())
				continue;      // skip it!

			if (x_last > (unsigned int)right)
				break;

			x_last += col->GetWidth();
		}
	}
	else
	{
		col_last = cols;
		x_last = (unsigned int)wxMax(columnLayout.GetTotalWidth(), 0);
	}

	// Instead of calling GetLineStart() for each line from the first to the
	// last one, we will compute the starts of the lines as we iterate over
	// them starting from this one, as this is much more efficient when using
	// wxDV_VARIABLE_LINE_HEIGHT (and doesn't really change anything when not
	// using it, so there is no need to use two different approaches).
	const unsigned int first_line_start = GetLineStart(item_start);

	// Draw background of alternate rows specially if required
	if (HasFlag(wxDV_ROW_LINES))
	{
		wxColour altRowColour = m_alternateRowColour;
		if (!altRowColour.IsOk())
		{
			// Determine the alternate rows colour automatically from the
			// background colour.
			const wxColour bgColour = GetBackgroundColour();

			// Depending on the background, alternate row color
			// will be 3% more dark or 50% brighter.
			int alpha = bgColour.GetRGB() > 0x808080 ? 97 : 150;
			altRowColour = bgColour.ChangeLightness(alpha);
		}

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(altRowColour));

		// We only need to draw the visible part, so limit the rectangle to it.
		const int xRect = CalcDataViewWindowUnscrolledPosition(wxPoint(0, 0), tableWindow).x;
		const int widthRect = cw;
		unsigned int cur_line_start = first_line_start;
		for (unsigned int item = item_start; item < item_last; item++)
		{
			const int h = GetLineHeight(item);
			// PHASE, not raw index. The buffer is a window that slides: a backward
			// portion prepends rows, a trim drops them off the front, a refresh
			// rebuilds it outright — and every one of those shifts every index by
			// an arbitrary amount. Striping on `item % 2` therefore repainted the
			// SAME business row in the other shade each time, which reads as the
			// whole list flickering. The phase is corrected wherever the buffer
			// shifts, so a row keeps its stripe (see m_stripePhase).
			if ((item + m_stripePhase) % 2)
			{
				dc.DrawRectangle(xRect, cur_line_start, widthRect, h);
			}
			cur_line_start += h;
		}
	}

	// Draw horizontal rules if required
	if (HasFlag(wxDV_HORIZ_RULES))
	{
		dc.SetPen(m_penRule);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		unsigned int cur_line_start = first_line_start;
		for (unsigned int i = item_start; i <= item_last; i++)
		{
			const int h = GetLineHeight(i);
			dc.DrawLine(x_start, cur_line_start, x_last, cur_line_start);
			cur_line_start += h;
		}
	}

	// Draw vertical rules if required
	if (HasFlag(wxDV_VERT_RULES))
	{
		dc.SetPen(m_penRule);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		// NB: Vertical rules are drawn in the last pixel of a column so that
		//     they align perfectly with native MSW ibHeaderGenericCtrl as well as for
		//     consistency with MSW native list control. There's no vertical
		//     rule at the most-left side of the control.

		const int line_last = GetLineStart(item_last);

		if (columnLayout.IsFlat())
		{
			int x = x_start - 1;
			for (unsigned int i = col_start; i < col_last; i++)
			{
				ibDataViewColumn* col = GetColumn(i);
				if (col->IsHidden())
					continue;       // skip it

				x += col->GetWidth();

				dc.DrawLine(x, first_line_start,
					x, line_last);
			}
		}
		else
		{
			// Grouped: the rule belongs to the CELL, not to a full-height stripe —
			// a column stacked under another one must not draw a line through its
			// neighbour's band.
			for (unsigned int i = col_start; i < col_last; i++)
			{
				ibColumnPlacement place;
				if (!columnLayout.GetBodyPlacement(GetColumn(i), place) || place.width <= 0)
					continue;

				// Merged into the cell on its right (an in-cell group): the two are one
				// cell showing two values, and a rule through it would deny that.
				if (place.mergedRight)
					continue;

				int line_top = first_line_start;
				for (unsigned int item = item_start; item < item_last; item++)
				{
					const int lh = GetLineHeight(item);
					wxRect ruleRect;
					if (GetColumnCellRect(GetColumn(i), line_top, lh, ruleRect))
					{
						const int rx = ruleRect.x + ruleRect.width - 1;
						dc.DrawLine(rx, ruleRect.y, rx, ruleRect.y + ruleRect.height);
					}
					line_top += lh;
				}
			}
		}
	}

	ibDataViewColumn* selectedCol = m_currentCol;

	// Per-paint selection visualisation summary: log highlighted rows
	// in the [item_start, item_last) range so we can diff what the
	// model thinks is selected vs what gets painted.
	if (tableWindow->GetType() == ibDataViewMainWindow::ibDataViewWindowNormal) {
		wxString selRows;
		int selDrawn = 0;
		for (unsigned int item = item_start; item < item_last; item++) {
			if (m_selection.IsSelected(item) || item == m_currentRow) {
				if (!selRows.IsEmpty()) selRows += ",";
				selRows += wxString::Format("%u", item);
				++selDrawn;
			}
		}
			}

	// redraw the background for the items which are selected/current
	unsigned int cur_line_start = first_line_start;
	for (unsigned int item = item_start; item < item_last; item++)
	{
		bool selected = m_selection.IsSelected(item);
		const int line_height = GetLineHeight(item);

		if (selected || item == m_currentRow)
		{
			wxRect rowRect(x_start, cur_line_start,
				x_last - x_start, line_height);

			bool renderColumnFocus = false;

			int flags = wxCONTROL_SELECTED;
			if (m_hasFocus)
				flags |= wxCONTROL_FOCUSED;

			// draw keyboard focus rect if applicable
			if (item == m_currentRow && m_hasFocus)
			{
				if (m_useCellFocus && m_currentCol && m_currentColSetByKeyboard)
				{
					renderColumnFocus = true;

					// If there is just a single value, render full-row focus:
					if (!IsList())
					{
						ibDataViewTreeNode* node = GetTreeNodeByRow(item);
						if (IsItemSingleValued(node->GetItem()))
							renderColumnFocus = false;
					}
				}

				if (renderColumnFocus)
				{
					wxRect colRect(rowRect);

					for (unsigned int i = col_start; i < col_last; i++)
					{
						ibDataViewColumn* col = GetColumn(i);
						if (col->IsHidden())
							continue;

						// The cell's OWN rectangle — its band inside the row when the column
						// lives under a vertical group, the full row height otherwise.
						if (!GetColumnCellRect(col, cur_line_start, line_height, colRect))
							continue;

						if (col == m_currentCol)
						{
							// Draw selection rect left of column
							{
								wxRect clipRect(rowRect);
								clipRect.width = colRect.x;

								wxDCClipper clip(dc, clipRect);
								wxRendererNative::Get().DrawItemSelectionRect
								(
									this,
									dc,
									rowRect,
									flags
								);
							}

							// Draw selection rect right of column
							{
								wxRect clipRect(rowRect);
								clipRect.x = colRect.x + colRect.width;
								clipRect.width = rowRect.width - clipRect.x;

								wxDCClipper clip(dc, clipRect);
								wxRendererNative::Get().DrawItemSelectionRect
								(
									this,
									dc,
									rowRect,
									flags
								);
							}

							// Draw column selection rect
							wxRendererNative::Get().DrawItemSelectionRect
							(
								this,
								dc,
								colRect,
								flags | wxCONTROL_CURRENT | wxCONTROL_CELL
							);

							break;
						}
					}
				}
				else // Not using column focus.
				{
					flags |= wxCONTROL_CURRENT | wxCONTROL_FOCUSED;

					// We still need to show the current item if it's not
					// selected.
					if (!selected)
					{
						wxRendererNative::Get().DrawFocusRect
						(
							this,
							dc,
							rowRect,
							flags
						);
					}
					//else: The current item is selected, will be drawn below.
				}
			}

			// draw selection and whole-item focus:
			if (selected && !renderColumnFocus)
			{
				if (m_selectionMode == ibDataViewSelectionMode::ibDataViewSelectRow)
				{
					wxRendererNative::Get().DrawItemSelectionRect
					(
						this,
						dc,
						rowRect,
						flags
					);
				}
				else if (m_selectionMode == ibDataViewSelectionMode::ibDataViewSelectCell)
				{
					// Faint row hint — show which row contains the
					// focused cell without competing with the strong
					// cell highlight below.  Blend system-highlight
					// 15% over the background so the tint is just
					// noticeable on light themes and barely there on
					// dark.
					{
						const wxColour hl = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
						const wxColour bg = GetBackgroundColour();
						const wxColour faint(
							(hl.Red()   * 15 + bg.Red()   * 85) / 100,
							(hl.Green() * 15 + bg.Green() * 85) / 100,
							(hl.Blue()  * 15 + bg.Blue()  * 85) / 100);
						wxDCBrushChanger  brushSaver(dc, wxBrush(faint));
						wxDCPenChanger    penSaver  (dc, *wxTRANSPARENT_PEN);
						dc.DrawRectangle(rowRect);
					}

					wxRect colRect(rowRect);

					for (unsigned int i = col_start; i < col_last; i++)
					{
						ibDataViewColumn* col = GetColumn(i);
						if (col->IsHidden())
							continue;

						// Same (x, band) rectangle the cell itself will be drawn in —
						// the highlight has to sit ON the cell, not on a full-height
						// stripe through the other bands of the row.
						if (!GetColumnCellRect(col, cur_line_start, line_height, colRect))
							continue;

						if (col == m_currentCol || m_currentCol == nullptr)
						{
							// Strong native selection rect on the
							// focused cell only.
							wxRendererNative::Get().DrawItemSelectionRect
							(
								this,
								dc,
								colRect,
								flags
							);

							selectedCol = col;
							break;
						}
					}
				}
				else
				{
					wxRendererNative::Get().DrawItemSelectionRect
					(
						this,
						dc,
						rowRect,
						flags
					);
				}
			}
		}
		cur_line_start += line_height;
	}

#if wxUSE_DRAG_AND_DROP
	wxRect dropItemRect;

	if (m_dropItemInfo.m_hint == DropHint_Inside)
	{
		int rect_y = GetLineStart(m_dropItemInfo.m_row);
		int rect_h = GetLineHeight(m_dropItemInfo.m_row);
		wxRect rect(x_start, rect_y, x_last - x_start, rect_h);

		wxRendererNative::Get().DrawItemSelectionRect(this, dc, rect, wxCONTROL_SELECTED | wxCONTROL_FOCUSED);
	}
#endif // wxUSE_DRAG_AND_DROP

	ibDataViewColumn* const
		expander = GetExpanderColumnOrFirstOne(this);

	// GROUP-caption geometry (hierarchical output): a group row draws its dimension value as ONE caption
	// that STARTS at the expander column's text and FLOWS RIGHT across columns with no value of their own — so a
	// grouping whose dimension has no bound column still shows, while in-scope dimension columns keep their values
	// (each empty cell paints its own slice of the caption; a valued cell paints its value instead — no cross-cell
	// erase, since every cell draws its own background then its own content in column order). Precompute the
	// expander column's content x; the per-row indent + expander width are added at the draw site below.
	int grpCaptionColX = 0;
	{
		const unsigned int expIdx = GetColumnIndex(expander);
		for (unsigned int c = 0; c < expIdx; c++) {
			ibDataViewColumn* cc = GetColumn(c);
			if (cc != nullptr && !cc->IsHidden())
				grpCaptionColX += cc->GetWidth();
		}
	}
	const int grpExpanderWidth = wxRendererNative::Get().GetExpanderSize(this).GetWidth();

	// redraw all cells for all rows which must be repainted and all columns
	wxRect cell_rect;

	for (unsigned int i = col_start; i < col_last; i++)
	{
		ibDataViewColumn* col = GetColumn(i);
		if (col->IsHidden())
			continue;       // skip it!

		ibDataViewRenderer* cell = col->GetRenderer();

		// Where the column sits INSIDE a row: x / width across, band down. Only the
		// row's top moves from here on — the rest of the rectangle is the same for
		// every row, which is why it is asked once per column.
		ibColumnPlacement place;
		if (!columnLayout.GetBodyPlacement(col, place) || place.width <= 0)
			continue;

		int line_top = first_line_start;

		for (unsigned int item = item_start; item < item_last; item++)
		{
			// get the cell value and set it into the renderer
			ibDataViewTreeNode* node = NULL;
			ibDataViewItem dataitem;
			const int line_height = GetLineHeight(item);

			ibDataViewTreeNodeViewMode datatype =
				ibDataViewTreeNodeViewMode::ibDataViewCell;

			if (!IsVirtualList())
			{
				node = GetTreeNodeByRow(item);
				if (node == NULL)
				{
					line_top += line_height;
					continue;
				}

				dataitem = node->GetItem();
				datatype = node->GetViewMode();
			}
			else
			{
				dataitem = ibDataViewItem(wxUIntToPtr(item + 1));
			}

			// update cell_rect — the band of THIS row the column occupies
			GetColumnCellRect(col, line_top, line_height, cell_rect);

			bool selected = m_selectionMode == ibDataViewSelectCell
				? m_selection.IsSelected(item) && (col == selectedCol) : m_selection.IsSelected(item);

			int state = 0;
			if (selected)
				state |= wxDATAVIEW_CELL_SELECTED;

			cell->SetState(state);
			const bool hasValue = cell->PrepareForItem(model, dataitem, col->GetModelColumn());

			// draw the background
			if (!selected)
				DrawCellBackground(cell, dc, cell_rect, datatype);

			// deal with the expander
			int indent = 0;
			if ((!IsList()) && (col == expander))
			{
				// Calculate the indent first
				if (m_viewMode == ibDataViewViewMode::ibDataViewTree)
					indent = GetIndent() * node->GetIndentLevel();

				// Get expander size
				wxSize expSize = wxRendererNative::Get().GetExpanderSize(this);

				// draw expander if needed
				if (node->HasChildren())
				{
					wxRect rect = cell_rect;
					rect.x += indent;
					rect.y += (cell_rect.GetHeight() - expSize.GetHeight()) / 2; // center vertically
					rect.width = expSize.GetWidth();
					rect.height = expSize.GetHeight();

					int flag = 0;
					if (m_underMouse == node)
						flag |= wxCONTROL_CURRENT;
					if (node->IsOpen())
						flag |= wxCONTROL_EXPANDED;

					// ensure that we don't overflow the cell (which might
					// happen if the column is very narrow)
					wxDCClipper clip(dc, cell_rect);

					wxRendererNative::Get().DrawTreeItemButton(this, dc, rect, flag);
				}

				indent += expSize.GetWidth();

				// force the expander column to VERTICAL-center, but PRESERVE the renderer's HORIZONTAL
				// alignment: SetValue picks it per value type (a numeric line-number column right-aligns),
				// and a bare SetAlignment(wxALIGN_CENTER_VERTICAL) dropped the horizontal flag → every
				// expander-column cell fell back to LEFT, so the tabular-section line number stopped
				// indenting right in Tree/Hierarchical view ("the flag does not get through").
				int expAlign = cell->GetAlignment();
				if (expAlign == wxDVR_DEFAULT_ALIGNMENT)
					expAlign = wxALIGN_LEFT;
				cell->SetAlignment((expAlign & (wxALIGN_RIGHT | wxALIGN_CENTER_HORIZONTAL)) | wxALIGN_CENTER_VERTICAL);

#if wxUSE_DRAG_AND_DROP
				if (item == m_dropItemInfo.m_row)
				{
					dropItemRect = cell_rect;
					dropItemRect.x += expSize.GetWidth();
					dropItemRect.width -= expSize.GetWidth();
					if (m_dropItemInfo.m_indentLevel >= 0)
					{
						int hintIndent = GetIndent() * m_dropItemInfo.m_indentLevel;
						dropItemRect.x += hintIndent;
						dropItemRect.width -= hintIndent;
					}
				}
#endif
			}

			wxRect item_rect = cell_rect;
			item_rect.Deflate(FromDIP(PADDING_RIGHTLEFT), 0);

			// account for the tree indent (harmless if we're not indented)
			item_rect.x += indent;
			item_rect.width -= indent;

			if (item_rect.width <= 0)
			{
				line_top += line_height;
				continue;
			}

			// TODO: it would be much more efficient to create a clipping
			//       region for the entire column being rendered (in the OnPaint
			//       of ibDataViewCtrl) instead of a single clip region for
			//       each cell. However it would mean that each renderer should
			//       respect the given wxRect's top & bottom coords, eventually
			//       violating only the left & right coords - however the user can
			//       make its own renderer and thus we cannot be sure of that.
			// A GROUP row renders its VALUED cells here as usual (an in-scope dimension / its dot-walk, an aggregate);
			// its dimension-value CAPTION is drawn ONCE as a continuous span in a dedicated pass AFTER this loop
			// (below), so column rules / per-cell padding never break it into stripes.
			wxDCClipper clip(dc, item_rect);

			if (hasValue)
				cell->WXCallRender(item_rect, &dc, state);

			line_top += line_height;
		}
	}

	// GROUP-row captions — drawn LAST, as ONE continuous span per row, so nothing (column rules, per-cell padding,
	// backgrounds) breaks the text into stripes. The caption flows from the expander across the columns with NO
	// value of their own and STOPS at the first column that carries a value (an in-scope dimension / its dot-walk),
	// which keeps that value visible. The presentation string comes off the node itself (item.GetGroupCaption).
	{
		unsigned int grp_line = first_line_start;
		for (unsigned int item = item_start; item < item_last; item++)
		{
			const int lh = GetLineHeight(item);
			ibDataViewTreeNode* gnode = IsVirtualList() ? NULL : GetTreeNodeByRow(item);
			wxString grpCaption;
			if (gnode != NULL && gnode->GetItem().GetGroupCaption(grpCaption))
			{
				const ibDataViewItem gitem = gnode->GetItem();
				const int capX = grpCaptionColX + FromDIP(PADDING_RIGHTLEFT)
					+ GetIndent() * gnode->GetIndentLevel() + grpExpanderWidth;
				// Right edge = the LEFT x of the first column (after the expander) that resolves a value of its own.
				int rightX = GetEndOfLastCol();
				int cx = 0;
				for (unsigned int i = 0; i < GetColumnCount(); i++)
				{
					ibDataViewColumn* c = GetColumn(i);
					if (c->IsHidden()) continue;
					if (c != expander && cx >= capX && c->GetRenderer() != NULL
					    && c->GetRenderer()->PrepareForItem(model, gitem, c->GetModelColumn()))
					{
						rightX = cx;
						break;
					}
					cx += c->GetWidth();
				}
				if (rightX > capX)
				{
					const bool sel = m_selection.IsSelected(item);
					// Draw through the SAME path a data cell uses (ibDataViewCustomRendererBase::WXCallRender ->
					// RenderText): set the DC colour + font like the cell does (row attribute wins, else the control's
					// own foreground / font — NOT the system default, which the form may override), then paint via the
					// NATIVE wxRendererNative::DrawItemText with the SELECTED flag — it owns the selection-colour logic
					// (theme-aware), so a highlighted group header matches a highlighted detail cell exactly, rather
					// than a hand-picked HIGHLIGHTTEXT. The DC font is set explicitly (a cell above may have left a
					// different one on the DC).
					ibDataViewItemAttr attr;
					GetModel()->GetAttr(gitem, expander->GetModelColumn(), attr);
					const wxColour fg = sel ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT)
					                  : attr.HasColour() ? attr.GetColour()
					                  : GetForegroundColour();
					int flags = 0;
					if (sel)          flags |= wxCONTROL_SELECTED;
					if (!IsEnabled()) flags |= wxCONTROL_DISABLED;
					const wxRect capRect(capX, grp_line, rightX - capX, lh);
					wxDCClipper     clip(dc, capRect);
					wxDCFontChanger fontChg(dc, attr.HasFont() ? attr.GetEffectiveFont(GetFont()) : GetFont());
					dc.SetTextForeground(fg);
					wxRendererNative::Get().DrawItemText(this, dc, grpCaption, capRect,
						wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, flags, wxELLIPSIZE_END);
				}
			}
			grp_line += lh;
		}
	}

#if wxUSE_DRAG_AND_DROP
	if (m_dropItemInfo.m_hint == DropHint_Below || m_dropItemInfo.m_hint == DropHint_Above)
	{
		const int insertLineHeight = 2;     // TODO: setup (should be even)

		int rect_y = dropItemRect.y - insertLineHeight / 2;     // top insert
		if (m_dropItemInfo.m_hint == DropHint_Below)
			rect_y += dropItemRect.height;                    // bottom insert

		wxRect rect(dropItemRect.x, rect_y, dropItemRect.width, insertLineHeight);
		wxRendererNative::Get().DrawItemSelectionRect(this, dc, rect, wxCONTROL_SELECTED);
	}
#endif // wxUSE_DRAG_AND_DROP
}

void ibDataViewCtrl::ProcessTableCharEvent(wxKeyEvent& event, ibDataViewMainWindow* tableWin)
{
	// no item -> nothing to do
	if (!HasCurrentRow())
	{
		event.Skip();
		return;
	}

	switch (event.GetKeyCode())
	{
	case WXK_RETURN:
		if (event.HasModifiers())
		{
			event.Skip();
			break;
		}
		else
		{
			// Enter activates the item, i.e. sends wxEVT_DATAVIEW_ITEM_ACTIVATED to
			// it. Only if that event is not handled do we activate column renderer (which
			// is normally done by Space) or even inline editing.

			const ibDataViewItem item = GetItemByRow(m_currentRow);

			ibDataViewEvent le(wxEVT_DATAVIEW_ITEM_ACTIVATED, this, item);
			if (ProcessWindowEvent(le))
				break;
			// else: fall through to WXK_SPACE handling
		}
		wxFALLTHROUGH;

	case WXK_SPACE:
		if (event.HasModifiers())
		{
			event.Skip();
			break;
		}
		else
		{
			// Space toggles activatable items or -- if not activatable --
			// starts inline editing (this is normally done using F2 on
			// Windows, but Space is common everywhere else, so use it too
			// for greater cross-platform compatibility).

			const ibDataViewItem item = GetItemByRow(m_currentRow);

			// Activate the current activatable column. If not column is focused (typically
			// because the user has full row selected), try to find the first activatable
			// column (this would typically be a checkbox and we don't want to force the user
			// to set focus on the checkbox column).
			ibDataViewColumn* activatableCol = FindColumnForEditing(item, wxDATAVIEW_CELL_ACTIVATABLE);

			if (activatableCol)
			{
				const unsigned colIdx = activatableCol->GetModelColumn();
				const wxRect cell_rect = GetItemRect(item, activatableCol);

				ibDataViewRenderer* cell = activatableCol->GetRenderer();
				cell->PrepareForItem(GetModel(), item, colIdx);
				cell->WXActivateCell(cell_rect, GetModel(), item, colIdx, NULL);

				break;
			}
			// else: fall through to WXK_F2 handling
			wxFALLTHROUGH;
		}

	case WXK_F2:
		if (event.HasModifiers())
		{
			event.Skip();
			break;
		}
		else
		{
			if (!m_selection.IsEmpty())
			{
				// Mimic Windows 7 behavior: edit the item that has focus
				// if it is selected and the first selected item if focus
				// is out of selection.
				unsigned sel;
				if (m_selection.IsSelected(m_currentRow))
				{
					sel = m_currentRow;
				}
				else // Focused item is not selected.
				{
					wxSelectionStore::IterationState cookie;
					sel = m_selection.GetFirstSelectedItem(cookie);
				}


				const ibDataViewItem item = GetItemByRow(sel);

				// Edit the current column. If no column is focused
				// (typically because the user has full row selected), try
				// to find the first editable column.
				ibDataViewColumn* editableCol = FindColumnForEditing(item, wxDATAVIEW_CELL_EDITABLE);

				if (editableCol)
					EditItem(item, editableCol);
			}
		}
		break;

	case WXK_UP:
		GoToRelativeRow(event, -1);
		break;

	case WXK_DOWN:
		GoToRelativeRow(event, +1);
		break;

	case '+':
	case WXK_ADD:
		ExpandRow(m_currentRow);
		break;

	case '*':
	case WXK_MULTIPLY:
		if (!IsExpandedRow(m_currentRow))
		{
			ExpandRow(m_currentRow, true /* recursively */);
			break;
		}
		//else: fall through to Collapse()
		wxFALLTHROUGH;

	case '-':
	case WXK_SUBTRACT:
		CollapseRow(m_currentRow);
		break;

	case WXK_LEFT:
		ProcessTableCharLeftKeyEvent(event);
		break;

	case WXK_RIGHT:
		ProcessTableCharRightKeyEvent(event);
		break;

	case WXK_END:
		GoToRelativeRow(event, +(int)GetRowCount());
		break;

	case WXK_HOME:
		GoToRelativeRow(event, -(int)GetRowCount());
		break;

	case WXK_PAGEUP:
		GoToRelativeRow(event, -(GetCountPerPage() - 1));
		break;

	case WXK_PAGEDOWN:
		GoToRelativeRow(event, +(GetCountPerPage() - 1));
		break;

	default:
		event.Skip();
	}
}

void ibDataViewCtrl::ProcessTableCharHookEvent(wxKeyEvent& event, ibDataViewMainWindow* tableWin)
{
	if (m_editorCtrl)
	{
		// Handle any keys special for the in-place editor and return without
		// calling Skip() below.
		switch (event.GetKeyCode())
		{
		case WXK_ESCAPE:
			m_editorRenderer->CancelEditing();
			return;

		case WXK_RETURN:
			// Shift-Enter is not special either.
			if (event.ShiftDown())
				break;
			wxFALLTHROUGH;

		case WXK_TAB:
			// Ctrl/Alt-Tab or Enter could be used for something else, so
			// don't handle them here.
			if (event.HasModifiers())
				break;

			m_editorRenderer->FinishEditing();
			return;
		}
	}
	else if (m_useCellFocus)
	{
		if (event.GetKeyCode() == WXK_TAB && !event.HasModifiers())
		{
			if (event.ShiftDown())
				ProcessTableCharLeftKeyEvent(event);
			else
				ProcessTableCharRightKeyEvent(event);
			return;
		}
	}

	event.Skip();
}

void ibDataViewCtrl::ProcessTableCharLeftKeyEvent(wxKeyEvent& event)
{
	if (IsList())
	{
		TryAdvanceCurrentColumn(NULL, event, /*forward=*/false);
	}
	else
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(m_currentRow);
		if (!node)
			return;

		if (TryAdvanceCurrentColumn(node, event, /*forward=*/false))
			return;

		const bool dontCollapseNodes = event.GetKeyCode() == WXK_TAB;
		if (dontCollapseNodes)
		{
			m_currentCol = NULL;
			// allow focus change
			event.Skip();
			return;
		}

		// Because TryAdvanceCurrentColumn() return false, we are at the first
		// column or using whole-row selection. In this situation, we can use
		// the standard TreeView handling of the left key.
		if (node->HasChildren() && node->IsOpen())
		{
			CollapseRow(m_currentRow);
		}
		else
		{
			// if the node is already closed, we move the selection to its parent
			ibDataViewTreeNode* parent_node = node->GetParent();

			if (parent_node)
			{
				int parent = GetRowByItem(parent_node->GetItem());
				if (parent >= 0)
				{
					GoToRow(event, parent);
				}
			}
		}
	}
}

void ibDataViewCtrl::ProcessTableCharRightKeyEvent(wxKeyEvent& event)
{
	if (IsList())
	{
		TryAdvanceCurrentColumn(NULL, event, /*forward=*/true);
	}
	else
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(m_currentRow);
		if (!node)
			return;

		if (node->HasChildren())
		{
			if (!node->IsOpen())
			{
				ExpandRow(m_currentRow);
			}
			else
			{
				// if the node is already open, we move the selection to the first child
				GoToRelativeRow(event, +1);
			}
		}
		else
		{
			TryAdvanceCurrentColumn(node, event, /*forward=*/true);
		}
	}
}

void ibDataViewCtrl::ProcessTableMouseEvent(wxMouseEvent& event, ibDataViewMainWindow* tableWin)
{
	// store position, before it's modified in the next step
	const wxPoint posEvent = event.GetPosition();

	event.SetPosition(posEvent + GetDataViewWindowOffset(tableWin));

	const wxPoint unscrolledPos = CalcDataViewWindowUnscrolledPosition(event.GetPosition(), tableWin);

	ibDataViewColumn* col = NULL;

	int xpos = 0;
	unsigned int cols = GetColumnCount();
	unsigned int i;
	for (i = 0; i < cols; i++)
	{
		ibDataViewColumn* c = GetColumn(i);
		if (c->IsHidden())
			continue;      // skip it!

		if (unscrolledPos.x < xpos + c->GetWidth())
		{
			col = c;
			break;
		}
		xpos += c->GetWidth();
	}

	ibDataViewModel* const model = GetModel();

	const unsigned int current = GetLineAt(unscrolledPos.y);
	const ibDataViewItem item = GetItemByRow(current);

	if (event.ButtonDown())
	{
		// Not skipping button down events would prevent the system from
		// setting focus to this window as most (all?) of them do by default,
		// so skip it to enable default handling.
		event.Skip();

		// Also stop editing if any mouse button is pressed: this is not really
		// necessary for the left button, as it would result in a focus loss
		// that would make the editor close anyhow, but we do need to do it for
		// the other ones and it does no harm to do it for the left one too.
		FinishEditing();
	}

	// Handle right clicking here, before everything else as context menu
	// events should be sent even when we click outside of any item, unlike all
	// the other ones.
	if (event.RightUp())
	{
		ibDataViewEvent le(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, this, col, item);
		int xx = event.GetX();
		int yy = event.GetY();
		ClientToScreen(&xx, &yy);
		ScreenToClient(&xx, &yy);
		le.SetPosition(xx, yy);
		ProcessWindowEvent(le);
	}

#if wxUSE_DRAG_AND_DROP
	if (event.Dragging() || ((m_dragCount > 0) && event.Leaving()))
	{
		if (m_dragCount == 0)
		{
			// we have to report the raw, physical coords as we want to be
			// able to call HitTest(event.m_pointDrag) from the user code to
			// get the item being dragged
			m_dragStart = event.GetPosition();
		}

		m_dragCount++;
		if ((m_dragCount < 3) && (event.Leaving()))
			m_dragCount = 3;
		else if (m_dragCount != 3)
			return;

		if (event.LeftIsDown())
		{

			CalcUnscrolledPosition(m_dragStart.x, m_dragStart.y,
				&m_dragStart.x, &m_dragStart.y);

			unsigned int drag_item_row = GetLineAt(m_dragStart.y);
			if (drag_item_row >= GetRowCount() || m_dragStart.x > GetEndOfLastCol())
				return;

			ibDataViewItem itemDragged = GetItemByRow(drag_item_row);

			// Notify cell about drag
			ibDataViewEvent evt(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, this, itemDragged);
			if (!HandleWindowEvent(evt))
				return;

			if (!evt.IsAllowed())
				return;

			wxDataObject* obj = evt.GetDataObject();
			if (!obj)
				return;

			ibDataViewDropSource drag(this, drag_item_row);
			drag.SetData(*obj);
			/* wxDragResult res = */ drag.DoDragDrop(evt.GetDragFlags());
			delete obj;
		}
		return;
	}
	else
	{
		m_dragCount = 0;
	}
#endif // wxUSE_DRAG_AND_DROP

	// Check if we clicked outside the item area.
	if ((current >= GetRowCount()) || !col)
	{
		// wx upstream cleared selection here per Windows file-explorer
		// convention.  OES forms use selection as the row-context for
		// subsequent actions (Edit / Delete / drill / Add-with-parent),
		// so a stray click into the empty area below the last row must
		// NOT drop selection — UnselectAll cleared m_selection but
		// m_currentRow / m_pagedRestoreFocus survived, so the next
		// refresh restored the highlight, giving the user a confusing
		// "selection blinked" effect.  Persistent selection wins.
		event.Skip();
		return;
	}

	ibDataViewRenderer* cell = col->GetRenderer();
	ibDataViewColumn* const
		expander = GetExpanderColumnOrFirstOne(this);

	// Test whether the mouse is hovering over the expander (a.k.a tree "+"
	// button) and also determine the offset of the real cell start, skipping
	// the indentation and the expander itself.
	bool hoverOverExpander = false;
	int itemOffset = 0;
	if ((!IsList()) && (expander == col) && m_viewMode == ibDataViewViewMode::ibDataViewTree)
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(current);

		int indent = node->GetIndentLevel();
		itemOffset = GetIndent() * indent;
		const int expWidth = wxRendererNative::Get().GetExpanderSize(this).GetWidth();

		if (node->HasChildren())
		{
			// we make the rectangle we are looking in a bit bigger than the actual
			// visual expander so the user can hit that little thing reliably
			wxRect rect(xpos + itemOffset,
				GetLineStart(current) + (GetLineHeight(current) - m_lineHeight) / 2,
				expWidth, m_lineHeight);

			if (rect.Contains(unscrolledPos.x, unscrolledPos.y))
			{
				// So the mouse is over the expander
				hoverOverExpander = true;
				if (m_underMouse && m_underMouse != node)
				{
					// wxLogMessage("Undo the row: %d", GetRowByItem(m_underMouse->GetItem()));
					RefreshRow(GetRowByItem(m_underMouse->GetItem()));
				}
				if (m_underMouse != node)
				{
					// wxLogMessage("Do the row: %d", current);
					RefreshRow(current);
				}
				m_underMouse = node;
			}
		}

		// Account for the expander as well, even if this item doesn't have it,
		// its parent does so it still counts for the offset.
		itemOffset += expWidth;
	}

	if (!hoverOverExpander)
	{
		if (m_underMouse != NULL)
		{
			// wxLogMessage("Undo the row: %d", GetRowByItem(m_underMouse->GetItem()));
			RefreshRow(GetRowByItem(m_underMouse->GetItem()));
			m_underMouse = NULL;
		}
	}

	bool simulateClick = false;

	if (event.ButtonDClick())
	{
		m_renameTimer->Stop();
		m_lastOnSame = false;

		if ((!IsList()) && m_viewMode == ibDataViewViewMode::ibDataViewHierarchical)
		{
			ibDataViewTreeNode* node = GetTreeNodeByRow(current);
			if (node->HasChildren())
			{
				SetTopParent(node->GetItem());

				return;
			}
		}
	}

	bool ignore_other_columns =
		(expander != col) &&
		(!model->HasValue(item, col->GetModelColumn()));

	if (event.LeftDClick())
	{
		if (!hoverOverExpander && (current == m_lineLastClicked))
		{
			ibDataViewEvent le(wxEVT_DATAVIEW_ITEM_ACTIVATED, this, col, item);
			if (ProcessWindowEvent(le))
			{
				// Item activation was handled from the user code.
				return;
			}
		}

		// Either it was a double click over the expander, or the second click
		// happened on another item than the first one or it was a bona fide
		// double click which was unhandled. In all these cases we continue
		// processing this event as a simple click, e.g. to select the item or
		// activate the renderer.
		simulateClick = true;
	}

	if (event.LeftUp() && !hoverOverExpander)
	{
		if (m_lineSelectSingleOnUp != (unsigned int)-1)
		{
			// select single line
			if (UnselectAllRows(m_lineSelectSingleOnUp))
			{
				SelectRow(m_lineSelectSingleOnUp, true);
			}

			SendSelectionChangedEvent(GetItemByRow(m_lineSelectSingleOnUp));
		}

		// If the user click the expander, we do not do editing even if the column
		// with expander are editable
		//
		if (m_lastOnSame && !ignore_other_columns)
		{
			if ((col == m_currentCol) && (current == m_currentRow) &&
				IsCellEditableInMode(item, col, wxDATAVIEW_CELL_EDITABLE))
			{
				m_renameTimer->Start(100, true);
			}
		}

		m_lastOnSame = false;
		m_lineSelectSingleOnUp = (unsigned int)-1;
	}
	else if (!event.LeftUp())
	{
		// This is necessary, because after a DnD operation in
		// from and to ourself, the up event is swallowed by the
		// DnD code. So on next non-up event (which means here and
		// now) m_lineSelectSingleOnUp should be reset.
		m_lineSelectSingleOnUp = (unsigned int)-1;
	}

	if (event.RightDown())
	{
		m_lineBeforeLastClicked = m_lineLastClicked;
		m_lineLastClicked = current;

		// If the item is already selected, do not update the selection.
		// Multi-selections should not be cleared if a selected item is clicked.
		if (!IsRowSelected(current))
		{
			UnselectAllRows();

			const unsigned oldCurrent = m_currentRow;
			ChangeCurrentRow(current);
			SelectRow(m_currentRow, true);
			RefreshRow(oldCurrent);
			SendSelectionChangedEvent(GetItemByRow(m_currentRow));
		}
	}
	else if (event.MiddleDown())
	{
	}

	if ((event.LeftDown() || simulateClick) && hoverOverExpander)
	{
		ibDataViewTreeNode* node = GetTreeNodeByRow(current);

		// hoverOverExpander being true tells us that our node must be
		// valid and have children.
		// So we don't need any extra checks.
		if (node->IsOpen())
			CollapseRow(current);
		else
			ExpandRow(current);

	}
	else if (((event.LeftDown() || event.RightDown()) || simulateClick) && !hoverOverExpander)
	{
		m_lineBeforeLastClicked = m_lineLastClicked;
		m_lineLastClicked = current;

		unsigned int oldCurrentRow = m_currentRow;
		bool oldWasSelected = IsRowSelected(m_currentRow);

		bool cmdModifierDown = event.CmdDown();
		if (IsSingleSel() || !(cmdModifierDown || event.ShiftDown()))
		{
			if (IsSingleSel() || !IsRowSelected(current))
			{
				ChangeCurrentRow(current);
				if (UnselectAllRows(current))
				{
					SelectRow(m_currentRow, true);
					SendSelectionChangedEvent(GetItemByRow(m_currentRow));
				}
			}
			else // multi sel & current is highlighted & no mod keys
			{
				m_lineSelectSingleOnUp = current;
				ChangeCurrentRow(current); // change focus
			}
		}
		else // multi sel & either ctrl or shift is down
		{
			if (cmdModifierDown)
			{
				ChangeCurrentRow(current);
				ReverseRowSelection(m_currentRow);
				SendSelectionChangedEvent(GetItemByRow(m_currentRow));
			}
			else if (event.ShiftDown())
			{
				ChangeCurrentRow(current);

				unsigned int lineFrom = oldCurrentRow,
					lineTo = current;

				if (lineFrom == static_cast<unsigned>(-1))
				{
					// If we hadn't had any current row before, treat this as a
					// simple click and select the new row only.
					lineFrom = current;
				}

				if (lineTo < lineFrom)
				{
					lineTo = lineFrom;
					lineFrom = m_currentRow;
				}

				SelectRows(lineFrom, lineTo);

				wxSelectionStore::IterationState cookie;
				const unsigned firstSel = m_selection.GetFirstSelectedItem(cookie);
				if (firstSel != wxSelectionStore::NO_SELECTION)
					SendSelectionChangedEvent(GetItemByRow(firstSel));
			}
			else // !ctrl, !shift
			{
				// test in the enclosing if should make it impossible
				wxFAIL_MSG(wxT("how did we get here?"));
			}
		}

		if (m_currentRow != oldCurrentRow)
			RefreshRow(oldCurrentRow);

		ibDataViewColumn* oldCurrentCol = m_currentCol;

		// Update selection here...
		m_currentCol = col;
		m_currentColSetByKeyboard = false;

		if (col != oldCurrentCol)
			UpdateDisplay();

		// This flag is used to decide whether we should start editing the item
		// label. We do it if the user clicks twice (but not double clicks,
		// i.e. simulateClick is false) on the same item but not if the click
		// was used for something else already, e.g. selecting the item (so it
		// must have been already selected) or giving the focus to the control
		// (so it must have had focus already).
		m_lastOnSame = !simulateClick && ((col == oldCurrentCol) &&
			(current == oldCurrentRow)) && oldWasSelected &&
			HasFocus();

		// Call ActivateCell() after everything else as under GTK+
		if (IsCellEditableInMode(item, col, wxDATAVIEW_CELL_ACTIVATABLE))
		{
			// notify cell about click

			wxRect cell_rect(xpos + itemOffset,
				GetLineStart(current),
				col->GetWidth() - itemOffset,
				GetLineHeight(current));

			// Note that PrepareForItem() should be called after GetLineStart()
			// call in cell_rect initialization above as GetLineStart() calls
			// PrepareForItem() for other items from inside it.
			cell->PrepareForItem(model, item, col->GetModelColumn());

			// Report position relative to the cell's custom area, i.e.
			// not the entire space as given by the control but the one
			// used by the renderer after calculation of alignment etc.
			//
			// Notice that this results in negative coordinates when clicking
			// in the upper left corner of a centre-aligned cell which doesn't
			// fill its column entirely so this is somewhat surprising, but we
			// do it like this for compatibility with the native GTK+ version,
			// see #12270.

			// adjust the rectangle ourselves to account for the alignment
			const int align = cell->GetEffectiveAlignment();

			wxRect rectItem = cell_rect;
			const wxSize size = cell->GetSize();
			if (size.x >= 0 && size.x < cell_rect.width)
			{
				if (align & wxALIGN_CENTER_HORIZONTAL)
					rectItem.x += (cell_rect.width - size.x) / 2;
				else if (align & wxALIGN_RIGHT)
					rectItem.x += cell_rect.width - size.x;
				// else: wxALIGN_LEFT is the default
			}

			if (size.y >= 0 && size.y < cell_rect.height)
			{
				if (align & wxALIGN_CENTER_VERTICAL)
					rectItem.y += (cell_rect.height - size.y) / 2;
				else if (align & wxALIGN_BOTTOM)
					rectItem.y += cell_rect.height - size.y;
				// else: wxALIGN_TOP is the default
			}

			wxMouseEvent event2(event);
			event2.m_x -= rectItem.x;
			event2.m_y -= rectItem.y;
			CalcUnscrolledPosition(event2.m_x, event2.m_y, &event2.m_x, &event2.m_y);

			/* ignore ret */ cell->WXActivateCell
			(
				cell_rect,
				model,
				item,
				col->GetModelColumn(),
				&event2
			);
		}
	}
}

#if wxUSE_ACCESSIBILITY
wxAccessible* ibDataViewCtrl::CreateAccessible()
{
	return new ibDataViewCtrlAccessible(this);
}
#endif // wxUSE_ACCESSIBILITY

#if wxUSE_ACCESSIBILITY

//-----------------------------------------------------------------------------
// ibDataViewCtrlAccessible
//-----------------------------------------------------------------------------

ibDataViewCtrlAccessible::ibDataViewCtrlAccessible(ibDataViewCtrl* win)
	: wxWindowAccessible(win)
{
}

// Can return either a child object, or an integer
// representing the child element, starting from 1.
wxAccStatus ibDataViewCtrlAccessible::HitTest(const wxPoint& pt,
	int* childId, wxAccessible** childObject)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	ibDataViewItem item;
	ibDataViewColumn* col;
	const wxPoint posCtrl = dvCtrl->ScreenToClient(pt);
	dvCtrl->HitTest(posCtrl, item, col);
	if (item.IsOk())
	{
		*childId = dvCtrl->GetRowByItem(item) + 1;
		*childObject = NULL;
	}
	else
	{
		if (((wxWindow*)dvCtrl)->HitTest(posCtrl) == wxHT_WINDOW_INSIDE)
		{
			// First check if provided point belongs to the header
			// because header control handles accesibility requestes on its own.
			ibHeaderGenericCtrl* dvHdr = dvCtrl->GenericGetHeader();
			if (dvHdr)
			{
				const wxPoint posHdr = dvHdr->ScreenToClient(pt);
				if (dvHdr->HitTest(posHdr) == wxHT_WINDOW_INSIDE)
				{
					*childId = wxACC_SELF;
					*childObject = dvHdr->GetOrCreateAccessible();
					return wxACC_OK;
				}
			}

			*childId = wxACC_SELF;
			*childObject = this;
		}
		else
		{
			*childId = wxACC_SELF;
			*childObject = NULL;
		}
	}

	return wxACC_OK;
}

// Returns the rectangle for this object (id = 0) or a child element (id > 0).
wxAccStatus ibDataViewCtrlAccessible::GetLocation(wxRect& rect, int elementId)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);
	ibDataViewCtrl* dvWnd = wxDynamicCast(dvCtrl->GetMainWindow(), ibDataViewCtrl);

	if (elementId == wxACC_SELF)
	{
		// Header accesibility requestes are handled separately
		// so header is excluded from effective client area
		// and hence only main window area is reported.
		rect = dvWnd->GetScreenRect();
	}
	else
	{
		ibDataViewItem item = dvWnd->GetItemByRow(elementId - 1);
		if (!item.IsOk())
		{
			return wxACC_NOT_IMPLEMENTED;
		}

		rect = dvWnd->GetItemRect(item, NULL);
		// Indentation and expander column should be included here and therefore
		// reported row width should by the same as the width of the client area.
		rect.width += rect.x;
		rect.x = 0;
		wxPoint posScreen = dvWnd->ClientToScreen(rect.GetPosition());
		rect.SetPosition(posScreen);
	}

	return wxACC_OK;
}

// Navigates from fromId to toId/toObject.
wxAccStatus ibDataViewCtrlAccessible::Navigate(wxNavDir navDir, int fromId,
	int* toId, wxAccessible** toObject)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);
	ibDataViewCtrl* dvWnd = wxDynamicCast(dvCtrl->GetMainWindow(), ibDataViewCtrl);

	const int numRows = (int)dvWnd->GetRowCount();

	if (fromId == wxACC_SELF)
	{
		switch (navDir)
		{
		case wxNAVDIR_FIRSTCHILD:
			if (numRows > 0)
			{
				*toId = 1;
				*toObject = NULL;
				return wxACC_OK;
			}
			return wxACC_FALSE;
		case wxNAVDIR_LASTCHILD:
			if (numRows > 0)
			{
				*toId = numRows;
				*toObject = NULL;
				return wxACC_OK;
			}
			return wxACC_FALSE;
		case wxNAVDIR_DOWN:
			wxFALLTHROUGH;
		case wxNAVDIR_NEXT:
			wxFALLTHROUGH;
		case wxNAVDIR_UP:
			wxFALLTHROUGH;
		case wxNAVDIR_PREVIOUS:
			wxFALLTHROUGH;
		case wxNAVDIR_LEFT:
			wxFALLTHROUGH;
		case wxNAVDIR_RIGHT:
			// Standard wxWindow navigation is applicable here.
			return wxWindowAccessible::Navigate(navDir, fromId, toId, toObject);
		}
	}
	else
	{
		switch (navDir)
		{
		case wxNAVDIR_FIRSTCHILD:
			return wxACC_FALSE;
		case wxNAVDIR_LASTCHILD:
			return wxACC_FALSE;
		case wxNAVDIR_LEFT:
			return wxACC_FALSE;
		case wxNAVDIR_RIGHT:
			return wxACC_FALSE;
		case wxNAVDIR_DOWN:
			wxFALLTHROUGH;
		case wxNAVDIR_NEXT:
			if (fromId < numRows)
			{
				*toId = fromId + 1;
				*toObject = NULL;
				return wxACC_OK;
			}
			return wxACC_FALSE;
		case wxNAVDIR_PREVIOUS:
			wxFALLTHROUGH;
		case wxNAVDIR_UP:
			if (fromId > 1)
			{
				*toId = fromId - 1;
				*toObject = NULL;
				return wxACC_OK;
			}
			return wxACC_FALSE;
		}
	}

	// Let the framework handle the other cases.
	return wxACC_NOT_IMPLEMENTED;
}

// Gets the name of the specified object.
wxAccStatus ibDataViewCtrlAccessible::GetName(int childId, wxString* name)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	if (childId == wxACC_SELF)
	{
		*name = dvCtrl->GetName();
	}
	else
	{
		ibDataViewItem item = dvCtrl->GetItemByRow(childId - 1);
		if (!item.IsOk())
		{
			return wxACC_NOT_IMPLEMENTED;
		}

		// Name is the value in the first textual column
		// plus the name of this column:
		// Column1: Value1
		wxString itemName;

		ibDataViewModel* model = dvCtrl->GetModel();
		const unsigned int numCols = dvCtrl->GetColumnCount();
		for (unsigned int col = 0; col < numCols; col++)
		{
			ibDataViewColumn* dvCol = dvCtrl->GetColumn(col);
			if (dvCol->IsHidden())
				continue; // skip it

			wxVariant value;
			model->GetValue(value, item, dvCol->GetModelColumn());
			if (value.IsNull() || value.IsType(wxS("bool")))
				continue; // Skip non-textual items

			ibDataViewRenderer* r = dvCol->GetRenderer();
			if (!r->PrepareForItem(model, item, dvCol->GetModelColumn()))
				continue;

			wxString vs = r->GetAccessibleDescription();
			if (!vs.empty())
			{
				itemName = vs;
				break;
			}
		}

		if (itemName.empty())
		{
			// Return row number if no textual column found.
			// Rows are numbered from 1.
			*name = wxString::Format(_("Row %i"), childId);
		}
		else
		{
			*name = itemName;
		}
	}

	return wxACC_OK;
}

// Gets the number of children.
wxAccStatus ibDataViewCtrlAccessible::GetChildCount(int* childCount)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);
	ibDataViewCtrl* dvWnd = wxDynamicCast(dvCtrl->GetMainWindow(), ibDataViewCtrl);

	*childCount = (int)dvWnd->GetRowCount();
	return wxACC_OK;
}

// Gets the specified child (starting from 1).
// If *child is NULL and return value is wxACC_OK,
// this means that the child is a simple element and
// not an accessible object.
wxAccStatus ibDataViewCtrlAccessible::GetChild(int childId, wxAccessible** child)
{
	*child = (childId == wxACC_SELF) ? this : NULL;
	return wxACC_OK;
}

// Performs the default action. childId is 0 (the action for this object)
// or > 0 (the action for a child).
// Return wxACC_NOT_SUPPORTED if there is no default action for this
// window (e.g. an edit control).
wxAccStatus ibDataViewCtrlAccessible::DoDefaultAction(int childId)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	if (childId != wxACC_SELF)
	{
		if (!dvCtrl->IsList())
		{
			const unsigned int row = childId - 1;
			ibDataViewTreeNode* node = dvCtrl->GetTreeNodeByRow(row);
			if (node)
			{
				if (node->HasChildren())
				{
					// Expand or collapse the node.
					if (node->IsOpen())
						dvCtrl->CollapseRow(row);
					else
						dvCtrl->ExpandRow(row);
					
					return wxACC_OK;
				}
			}
		}
	}

	return wxACC_NOT_SUPPORTED;
}

// Gets the default action for this object (0) or > 0 (the action for a child).
// Return wxACC_OK even if there is no action. actionName is the action, or the empty
// string if there is no action.
// The retrieved string describes the action that is performed on an object,
// not what the object does as a result. For example, a toolbar button that prints
// a document has a default action of "Press" rather than "Prints the current document."
wxAccStatus ibDataViewCtrlAccessible::GetDefaultAction(int childId, wxString* actionName)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	wxString action;
	if (childId != wxACC_SELF)
	{
		if (!dvCtrl->IsList())
		{
			ibDataViewTreeNode* node = dvCtrl->GetTreeNodeByRow(childId - 1);
			if (node)
			{
				if (node->HasChildren())
				{
					if (node->IsOpen())
						/* TRANSLATORS: Action for manipulating a tree control */
						action = _("Collapse");
					else
						/* TRANSLATORS: Action for manipulating a tree control */
						action = _("Expand");
				}
			}
		}
	}

	*actionName = action;
	return wxACC_OK;
}

// Returns the description for this object or a child.
wxAccStatus ibDataViewCtrlAccessible::GetDescription(int childId, wxString* description)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	if (childId == wxACC_SELF)
	{
		*description = wxString::Format(_("%s (%d items)"),
			dvCtrl->GetName().c_str(), dvCtrl->GetRowCount());
	}
	else
	{
		ibDataViewItem item = dvCtrl->GetItemByRow(childId - 1);
		if (!item.IsOk())
		{
			return wxACC_NOT_IMPLEMENTED;
		}

		// Description is concatenation of the contents of items in all columns:
		// Column1: Value1, Column2: Value2, ...
		// First textual item should be skipped because it is returned
		// as a Name property.
		wxString itemDesc;

		bool firstTextSkipped = false;
		ibDataViewModel* model = dvCtrl->GetModel();
		const unsigned int numCols = dvCtrl->GetColumnCount();
		for (unsigned int col = 0; col < numCols; col++)
		{
			if (!model->HasValue(item, col))
				continue; // skip it

			ibDataViewColumn* dvCol = dvCtrl->GetColumn(col);
			if (dvCol->IsHidden())
				continue; // skip it

			wxVariant value;
			model->GetValue(value, item, dvCol->GetModelColumn());

			ibDataViewRenderer* r = dvCol->GetRenderer();
			if (!r->PrepareForItem(model, item, dvCol->GetModelColumn()))
				continue;

			wxString valStr = r->GetAccessibleDescription();
			// Skip first textual item
			if (!firstTextSkipped && !value.IsNull() && !value.IsType(wxS("bool")) && !valStr.empty())
			{
				firstTextSkipped = true;
				continue;
			}

			if (!valStr.empty())
			{
				wxString colName = dvCol->GetTitle();
				// If column has no label then present its index.
				if (colName.empty())
				{
					// Columns are numbered from 1.
					colName = wxString::Format(_("Column %u"), col + 1);
				}

				if (!itemDesc.empty())
					itemDesc.Append(wxS(", "));
				itemDesc.Append(colName);
				itemDesc.Append(wxS(": "));
				itemDesc.Append(valStr);
			}
		}

		*description = itemDesc;
	}

	return wxACC_OK;
}

// Returns help text for this object or a child, similar to tooltip text.
wxAccStatus ibDataViewCtrlAccessible::GetHelpText(int childId, wxString* helpText)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

#if wxUSE_HELP
	if (childId == wxACC_SELF)
	{
		*helpText = dvCtrl->GetHelpText();
	}
	else
	{
		ibDataViewItem item = dvCtrl->GetItemByRow(childId - 1);
		if (item.IsOk())
		{
			wxRect rect = dvCtrl->GetItemRect(item, NULL);
			*helpText = dvCtrl->GetHelpTextAtPoint(rect.GetPosition(), wxHelpEvent::Origin_Keyboard);
		}
		else
		{
			helpText->clear();
		}
	}
	return wxACC_OK;
#else
	(void)childId;
	(void)helpText;
	return wxACC_NOT_IMPLEMENTED;
#endif
}

// Returns the keyboard shortcut for this object or child.
// Return e.g. ALT+K
wxAccStatus ibDataViewCtrlAccessible::GetKeyboardShortcut(int childId, wxString* shortcut)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	if (childId != wxACC_SELF)
	{
		if (!dvCtrl->IsList())
		{
			ibDataViewTreeNode* node = dvCtrl->GetTreeNodeByRow(childId - 1);
			if (node)
			{
				if (node->HasChildren())
				{
					if (node->IsOpen())
						/* TRANSLATORS: Keystroke for manipulating a tree control */
						*shortcut = _("Left");
					else
						/* TRANSLATORS: Keystroke for manipulating a tree control */
						*shortcut = _("Right");

					return wxACC_OK;
				}
			}
		}
	}

	return wxACC_FALSE;
}

// Returns a role constant.
wxAccStatus ibDataViewCtrlAccessible::GetRole(int childId, wxAccRole* role)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	if (childId == wxACC_SELF)
		*role = dvCtrl->IsList() ? wxROLE_SYSTEM_LIST : wxROLE_SYSTEM_OUTLINE;
	else
		*role = dvCtrl->IsList() ? wxROLE_SYSTEM_LISTITEM : wxROLE_SYSTEM_OUTLINEITEM;

	return wxACC_OK;
}

// Returns a state constant.
wxAccStatus ibDataViewCtrlAccessible::GetState(int childId, long* state)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);
	ibDataViewCtrl* dvWnd = wxDynamicCast(dvCtrl->GetMainWindow(), ibDataViewCtrl);

	long st = 0;
	// State flags common to the object and its children.
	if (!dvWnd->IsEnabled())
		st |= wxACC_STATE_SYSTEM_UNAVAILABLE;
	if (!dvWnd->IsShown())
		st |= wxACC_STATE_SYSTEM_INVISIBLE;

	if (childId == wxACC_SELF)
	{
		if (dvWnd->IsFocusable())
			st |= wxACC_STATE_SYSTEM_FOCUSABLE;
		if (dvWnd->HasFocus())
			st |= wxACC_STATE_SYSTEM_FOCUSED;
	}
	else
	{
		const unsigned int rowNum = childId - 1;

		if (dvWnd->IsFocusable())
			st |= wxACC_STATE_SYSTEM_FOCUSABLE | wxACC_STATE_SYSTEM_SELECTABLE;
		if (!dvWnd->IsSingleSel())
			st |= wxACC_STATE_SYSTEM_MULTISELECTABLE | wxACC_STATE_SYSTEM_EXTSELECTABLE;

		if (rowNum < dvWnd->GetFirstVisibleRow() || rowNum > dvWnd->GetLastFullyVisibleRow())
			st |= wxACC_STATE_SYSTEM_OFFSCREEN;
		if (dvWnd->GetCurrentRow() == rowNum)
			st |= wxACC_STATE_SYSTEM_FOCUSED;
		if (dvWnd->IsRowSelected(rowNum))
			st |= wxACC_STATE_SYSTEM_SELECTED;

		if (!dvWnd->IsList())
		{
			ibDataViewTreeNode* node = dvWnd->GetTreeNodeByRow(rowNum);
			if (node)
			{
				if (node->HasChildren())
				{
					if (node->IsOpen())
						st |= wxACC_STATE_SYSTEM_EXPANDED;
					else
						st |= wxACC_STATE_SYSTEM_COLLAPSED;
				}
			}
		}
	}
	*state = st;
	return wxACC_OK;
}

// Returns a localized string representing the value for the object
// or child.
wxAccStatus ibDataViewCtrlAccessible::GetValue(int childId, wxString* strValue)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	wxString val;

	if (childId != wxACC_SELF)
	{
		if (!dvCtrl->IsList())
		{
			// In the tree view each item within the control has a zero-based value
			// that represents its level within the hierarchy and this value
			// is returned as a Value property.
			ibDataViewTreeNode* node = dvCtrl->GetTreeNodeByRow(childId - 1);
			if (node)
			{
				val = wxString::Format(wxS("%i"), node->GetIndentLevel());
			}
		}
	}

	*strValue = val;
	return wxACC_OK;
}

// Selects the object or child.
wxAccStatus ibDataViewCtrlAccessible::Select(int childId, wxAccSelectionFlags selectFlags)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	if (childId == wxACC_SELF)
	{
		if (selectFlags == wxACC_SEL_TAKEFOCUS)
		{
			dvCtrl->SetFocus();
		}
		else if (selectFlags != wxACC_SEL_NONE)
		{
			wxFAIL_MSG(wxS("Invalid selection flag"));
			return wxACC_INVALID_ARG;
		}
	}
	else
	{
		// These flags are not allowed in the single-selection mode:
		if (dvCtrl->IsSingleSel() &&
			selectFlags & (wxACC_SEL_EXTENDSELECTION | wxACC_SEL_ADDSELECTION | wxACC_SEL_REMOVESELECTION))
		{
			wxFAIL_MSG(wxS("Invalid selection flag"));
			return wxACC_INVALID_ARG;
		}

		const int row = childId - 1;

		if (selectFlags == wxACC_SEL_TAKEFOCUS)
		{
			dvCtrl->ChangeCurrentRow(row);
		}
		else if (selectFlags & wxACC_SEL_TAKESELECTION)
		{
			// This flag must not be combined with the following flags:
			if (selectFlags & (wxACC_SEL_EXTENDSELECTION | wxACC_SEL_ADDSELECTION | wxACC_SEL_REMOVESELECTION))
			{
				wxFAIL_MSG(wxS("Invalid selection flag"));
				return wxACC_INVALID_ARG;
			}

			dvCtrl->UnselectAllRows();
			dvCtrl->SelectRow(row, true);
			if (selectFlags & wxACC_SEL_TAKEFOCUS || dvCtrl->IsSingleSel())
			{
				dvCtrl->ChangeCurrentRow(row);
			}
		}
		else if (selectFlags & wxACC_SEL_EXTENDSELECTION)
		{
			// This flag must not be combined with the following flag:
			if (selectFlags & wxACC_SEL_TAKESELECTION)
			{
				wxFAIL_MSG(wxS("Invalid selection flag"));
				return wxACC_INVALID_ARG;
			}
			// These flags cannot be set together:
			if ((selectFlags & (wxACC_SEL_ADDSELECTION | wxACC_SEL_REMOVESELECTION))
				== (wxACC_SEL_ADDSELECTION | wxACC_SEL_REMOVESELECTION))
			{
				wxFAIL_MSG(wxS("Invalid selection flag"));
				return wxACC_INVALID_ARG;
			}

			// We have to have a focused object as a selection anchor.
			unsigned int focusedRow = dvCtrl->GetCurrentRow();
			if (focusedRow == (unsigned int)-1)
			{
				wxFAIL_MSG(wxS("No selection anchor"));
				return wxACC_INVALID_ARG;
			}

			bool doSelect;
			if (selectFlags & wxACC_SEL_ADDSELECTION)
				doSelect = true;
			else if (selectFlags & wxACC_SEL_REMOVESELECTION)
				doSelect = false;
			else
				// If the anchor object is selected, the selection is extended.
				// If the anchor object is not selected, all objects are unselected.
				doSelect = dvCtrl->IsRowSelected(focusedRow);

			if (doSelect)
			{
				dvCtrl->SelectRows(focusedRow, row);
			}
			else
			{
				for (int r = focusedRow; r <= row; r++)
					dvCtrl->SelectRow(r, false);
			}

			if (selectFlags & wxACC_SEL_TAKEFOCUS)
			{
				dvCtrl->ChangeCurrentRow(row);
			}
		}
		else if (selectFlags & wxACC_SEL_ADDSELECTION)
		{
			// This flag must not be combined with the following flags:
			if (selectFlags & (wxACC_SEL_TAKESELECTION | wxACC_SEL_REMOVESELECTION))
			{
				wxFAIL_MSG(wxS("Invalid selection flag"));
				return wxACC_INVALID_ARG;
			}

			// Combination with wxACC_SEL_EXTENDSELECTION is already handled
			// (see wxACC_SEL_EXTENDSELECTION block).
			dvCtrl->SelectRow(row, true);
			if (selectFlags & wxACC_SEL_TAKEFOCUS)
			{
				dvCtrl->ChangeCurrentRow(row);
			}
		}
		else if (selectFlags & wxACC_SEL_REMOVESELECTION)
		{
			// This flag must not be combined with the following flags:
			if (selectFlags & (wxACC_SEL_TAKESELECTION | wxACC_SEL_ADDSELECTION))
			{
				wxFAIL_MSG(wxS("Invalid selection flag"));
				return wxACC_INVALID_ARG;
			}

			// Combination with wxACC_SEL_EXTENDSELECTION is already handled
			// (see wxACC_SEL_EXTENDSELECTION block).
			dvCtrl->SelectRow(row, false);
			if (selectFlags & wxACC_SEL_TAKEFOCUS)
			{
				dvCtrl->ChangeCurrentRow(row);
			}
		}
	}

	return wxACC_OK;
}

// Gets the window with the keyboard focus.
// If childId is 0 and child is NULL, no object in
// this subhierarchy has the focus.
// If this object has the focus, child should be 'this'.
wxAccStatus ibDataViewCtrlAccessible::GetFocus(int* childId, wxAccessible** child)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	const unsigned int row = dvCtrl->GetCurrentRow();
	if (row != (unsigned int)*childId - 1)
	{
		*childId = row + 1;
		*child = NULL;
	}
	else
	{
		// First check if header is focused because header control
		// handles accesibility requestes on its own.
		ibHeaderGenericCtrl* dvHdr = dvCtrl->GenericGetHeader();
		if (dvHdr)
		{
			if (dvHdr->HasFocus())
			{
				*childId = wxACC_SELF;
				*child = dvHdr->GetOrCreateAccessible();
				return wxACC_OK;
			}
		}

		if (dvCtrl->HasFocus())
		{
			*childId = wxACC_SELF;
			*child = this;
		}
		else
		{
			*childId = 0;
			*child = NULL;
		}
	}

	return wxACC_OK;
}

// Gets a variant representing the selected children
// of this object.
// Acceptable values:
// - a null variant (IsNull() returns true)
// - a "void*" pointer to a wxAccessible child object
// - an integer representing the selected child element,
//   or 0 if this object is selected (GetType() == wxT("long"))
// - a list variant (GetType() == wxT("list"))
wxAccStatus ibDataViewCtrlAccessible::GetSelections(wxVariant* selections)
{
	ibDataViewCtrl* dvCtrl = wxDynamicCast(GetWindow(), ibDataViewCtrl);
	wxCHECK(dvCtrl, wxACC_FAIL);

	ibDataViewItemArray sel;
	dvCtrl->GetSelections(sel);
	if (sel.IsEmpty())
	{
		selections->MakeNull();
	}
	else
	{
		wxVariantList tempList;
		wxVariant v(tempList);

		for (size_t i = 0; i < sel.GetCount(); i++)
		{
			int row = dvCtrl->GetRowByItem(sel[i]);
			v.Append(wxVariant((long)row + 1));
		}

		// Don't return the list if one child is selected.
		if (v.GetCount() == 1)
			*selections = wxVariant(v[0].GetLong());
		else
			*selections = v;
	}

	return wxACC_OK;
}
#endif // wxUSE_ACCESSIBILITY

//-----------------------------------------------------------------------------
// ibDataViewMainWindow
//-----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewMainWindow, wxWindow);

wxBEGIN_EVENT_TABLE(ibDataViewMainWindow, wxWindow)
EVT_PAINT(ibDataViewMainWindow::OnPaint)
EVT_MOUSE_EVENTS(ibDataViewMainWindow::OnMouse)
EVT_SET_FOCUS(ibDataViewMainWindow::OnSetFocus)
EVT_KILL_FOCUS(ibDataViewMainWindow::OnKillFocus)
EVT_CHAR_HOOK(ibDataViewMainWindow::OnCharHook)
EVT_CHAR(ibDataViewMainWindow::OnChar)
wxEND_EVENT_TABLE()

void ibDataViewMainWindow::ScrollWindow(int dx, int dy, const wxRect* rect)
{
	m_owner->ScrollWindow(dx, dy, rect);
}

void ibDataViewMainWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);
	m_owner->DrawTableContent(dc, this);
}

// ---------------------------------------------------------------------------
// The busy indicator — "the data is on its way".
// ---------------------------------------------------------------------------

namespace {
// TWO numbers, and both are about the eye, not about the data.
//
// How long a read may take before it is worth telling anybody. Under this the
// list simply updates and no arc appears at all — the common case on a local base.
constexpr long kBusyShowDelayMs = 200;

// And once it IS up, how long it stays at the very least. Without this an answer
// landing just past the delay puts the arc on screen and takes it off in the same
// breath, which reads as a flicker — the exact thing the delay was meant to spare
// the user. It does NOT delay the rows: they are already there, this only keeps
// the arc legible over them.
constexpr long kBusyMinShowMs = 250;

// Animation cadence. Fast enough to look continuous, slow enough that an idle
// desktop is not repainting a list twenty times a second for decoration.
constexpr int  kBusyTickMs = 60;

// One turn of the arc = kBusySteps ticks (~1.4 s at the cadence above).
constexpr int  kBusySteps = 24;

// Spelled out rather than M_PI: that one is not standard C++ and needs a define
// before <cmath> on MSVC, which is a build-order dependency for one constant.
constexpr double kTwoPi = 6.283185307179586;

// The badge's side, in DIP — big enough to read as a spinner, small enough that
// it covers a row and a half and nothing more.
constexpr int  kBusyBadgeSide = 44;
}   // namespace

// THE ARC IN A WINDOW OF ITS OWN.
//
// It used to be painted at the tail of the rows' OnPaint, which is less code and
// was wrong for one reason: `Freeze()` silences a WINDOW. The rows area is frozen
// across the blind stretch — from the moment the old rows are wiped to the moment
// the portion lands — and everything painted INSIDE it went silent with it, so the
// wait showed as a blank rectangle with nothing to say on it. Freeze or spinner,
// pick one. A sibling window is frozen by nobody, so both hold.
class ibDataViewBusyWindow : public wxWindow
{
public:
	explicit ibDataViewBusyWindow(ibDataViewCtrl* owner)
		: wxWindow(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		           wxBORDER_NONE | wxTRANSPARENT_WINDOW)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &ibDataViewBusyWindow::OnPaint, this);
		// The badge is decoration: it must not erase (flicker), must not take the
		// focus away from the list, and must not eat a click meant for a row.
		Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
	}

	bool AcceptsFocus() const wxOVERRIDE             { return false; }
	bool AcceptsFocusFromKeyboard() const wxOVERRIDE { return false; }

	void SetPhase(int phase) { m_phase = phase; }

private:
	void OnPaint(wxPaintEvent& WXUNUSED(event));

	int m_phase = 0;
};

void ibDataViewBusyWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);

	const wxSize size = GetClientSize();
	if (size.x <= 0 || size.y <= 0)
		return;

	// A plate in the list's own background colour, not a translucent veil: this
	// window has no idea what is underneath it (that is the rows' window, and on
	// the blind stretch it is frozen), so there is nothing to blend with.
	const wxColour plate = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	dc.SetBackground(wxBrush(plate));
	dc.Clear();

	// wxGraphicsContext::Create takes a CONCRETE dc, and wxAutoBufferedPaintDC
	// resolves to wxBufferedPaintDC (a wxMemoryDC) where double buffering is
	// emulated and to wxPaintDC (a wxWindowDC) where the platform buffers
	// natively. So ask for both.
	wxGraphicsContext* raw = nullptr;
	if (auto* mem = dynamic_cast<wxMemoryDC*>(static_cast<wxDC*>(&dc)))
		raw = wxGraphicsContext::Create(*mem);
	else if (auto* win = dynamic_cast<wxWindowDC*>(static_cast<wxDC*>(&dc)))
		raw = wxGraphicsContext::Create(*win);

	std::unique_ptr<wxGraphicsContext> gc(raw);
	if (!gc)
		return;                            // no graphics backend — skip, never fail a paint

	const wxColour frame = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
	gc->SetBrush(wxBrush(plate));
	gc->SetPen(wxPen(wxColour(frame.Red(), frame.Green(), frame.Blue(), 90), 1));
	gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, 6.0);

	const double cx = size.x / 2.0;
	const double cy = size.y / 2.0;
	const double radius = wxMin(size.x, size.y) / 2.0 - 8.0;
	if (radius < 4.0)
		return;                            // too small to read as a spinner

	// The arc: a fixed-length sweep rotated by the phase. Drawn as a thick stroked
	// path rather than a rotating bitmap so it scales with the DPI for free.
	const double turn  = (kTwoPi * m_phase) / kBusySteps;
	const double sweep = kTwoPi * 0.75;

	wxColour accent = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
	gc->SetPen(wxPen(wxColour(accent.Red(), accent.Green(), accent.Blue(), 210),
	                 wxMax(2, static_cast<int>(radius / 4))));
	gc->SetBrush(*wxTRANSPARENT_BRUSH);

	wxGraphicsPath path = gc->CreatePath();
	path.AddArc(cx, cy, radius, turn, turn + sweep, true);
	gc->StrokePath(path);
}

// Centred over the ROWS, not over the control: the header keeps painting through
// the whole wait, and a badge sitting half on it would read as part of the chrome.
void ibDataViewCtrl::PositionBusyWindow()
{
	if (m_busyWin == nullptr)
		return;

	wxWindow* area = (m_tableAreaWin != nullptr)
	                 ? static_cast<wxWindow*>(m_tableAreaWin)
	                 : static_cast<wxWindow*>(this);

	const wxSize side = FromDIP(wxSize(kBusyBadgeSide, kBusyBadgeSide));
	const wxPoint at  = (area == this) ? wxPoint(0, 0) : area->GetPosition();
	const wxSize  span = area->GetClientSize();

	const wxRect want(at.x + (span.x - side.x) / 2,
	                  at.y + (span.y - side.y) / 2,
	                  side.x, side.y);
	if (m_busyWin->GetRect() != want)
		m_busyWin->SetSize(want);
}

void ibDataViewCtrl::ShowBusyWindow(bool show)
{
	if (!show) {
		if (m_busyWin != nullptr && m_busyWin->IsShown())
			m_busyWin->Hide();
		return;
	}

	if (m_busyWin == nullptr)
		m_busyWin = new ibDataViewBusyWindow(this);

	PositionBusyWindow();
	m_busyWin->SetPhase(m_busyPhase);
	if (!m_busyWin->IsShown())
		m_busyWin->Show();
	m_busyWin->Raise();
	m_busyWin->Refresh();
	// Paint NOW rather than on the next idle: the arc's whole job is to appear
	// while the UI thread is between two pieces of work, and an idle pass is
	// exactly what it may not get.
	m_busyWin->Update();
}

// THE WHOLE RULE, and it is two lines of it:
//
//   the answer is in  → the arc goes, at once. No hold, no fade: the rows are
//                       there, so there is nothing left to say.
//   the read is slow  → the arc appears. "Slow" is the only thing that needs a
//                       number (kBusyShowDelayMs): a read that answers before an
//                       eye can catch it must paint NOTHING, or every scrolled
//                       portion flickers.
//
// So the delay guards the appearance only. It is not a grace period for the
// answer — the answer never waits.
void ibDataViewCtrl::UpdateBusyIndicator()
{
	// WAITING STARTS WHEN THE ROWS GO, not when the read leaves. Between the two
	// there is a whole idle pass: the buffer is already wiped and the portion is
	// only dispatched from the next OnInternalIdle. That gap is exactly the blank
	// white rectangle the user sees, and "no read in flight yet" is why nothing was
	// painted over it.
	//
	// A PENDING BOOTSTRAP COUNTS ONLY IF IT CAN ACTUALLY GO — the same condition
	// the idle pass dispatches on. The flag alone is not a wait: a control too
	// short to hold a single row (a table box in the FORM EDITOR, sized to a couple
	// of pixels of preview) never reaches a batch size, so its bootstrap stands
	// armed for the life of the window. Read as waiting, that painted an arc that
	// could never be taken off — a spinner over a form nobody is loading.
	const bool bootstrapCanRun = m_pagedNeedsBootstrap
	                          && m_tableAreaWin != nullptr
	                          && GetCountPerPage() > 0;
	const bool waiting = IsFetchInFlight() || bootstrapCanRun;

	if (!waiting) {
		// STILL LEGIBLE FIRST. The rows are already on screen — this only decides
		// when the arc over them goes. If it has not been up long enough to be
		// read, leave it and let the tick take it off; the tick is still running,
		// which is why nothing here stops it on this path.
		if (m_busyShown
		    && (wxGetUTCTimeMillis() - m_busyShownMs).ToLong() < kBusyMinShowMs)
			return;

		m_busySinceMs = 0;
		if (m_busyTimer.IsRunning())
			m_busyTimer.Stop();
		if (m_busyShown) {
			m_busyShown = false;
			ShowBusyWindow(false);           // take it off the finished rows
		}
		return;
	}

	if (m_busySinceMs == 0) {
		// A read just went out. Note the moment and arm the tick — the tick both
		// turns the arc and re-checks, so a read that dies without delivering
		// still ends the spinner.
		m_busySinceMs = wxGetUTCTimeMillis();
		m_busyPhase   = 0;
	}
	if (!m_busyTimer.IsRunning())
		m_busyTimer.Start(kBusyTickMs);

	// THE DELAY APPLIES TO EVERY READ, including the one with nothing on screen yet.
	//
	// An empty table used to raise the arc on the first pass, on the reasoning that
	// a blank rectangle says nothing and the wait is worth naming. In practice it
	// named waits that were not waits: a RAM table answers in microseconds, so what
	// the user got on every one of them was an arc appearing and then held for its
	// minimum-show — a flash for a read that had already finished. Meanwhile the
	// slow reads it was written for cross 200 ms anyway and show regardless.
	//
	// So there is ONE rule and no exception to it: a read that answers before an eye
	// can catch it paints nothing at all. 200 ms of blank on a cold open is the
	// cheaper of the two, and the rows usually beat it.
	//
	// Checked here as well as in the tick: on the tick alone nothing could appear
	// sooner than one cadence after the delay, which made the delay meaningless
	// for anything shorter.
	if (!m_busyShown
	    && (wxGetUTCTimeMillis() - m_busySinceMs).ToLong() >= kBusyShowDelayMs) {
		m_busyShown  = true;
		m_busyShownMs = wxGetUTCTimeMillis();   // the minimum above counts from HERE
		ShowBusyWindow(true);
	}
}

void ibDataViewCtrl::OnBusyTimer(wxTimerEvent& WXUNUSED(event))
{
	// THE TICK IS ALSO THE WATCHDOG. Re-checking here is what ends the spinner
	// when a read dies without delivering — its worker gone, the pool stopped,
	// the session torn down. A spinner that only ever stops on success is a
	// spinner that can spin forever, which is the one failure to avoid.
	//
	// What it CANNOT catch is a read wedged in a socket: the counter stays up
	// because nothing ever came back. Cancellation is cooperative and a blocking
	// read does not see the flag, so that case belongs to the driver's timeout,
	// not here. The form stays usable throughout either way.
	if (!IsFetchInFlight()) {
		UpdateBusyIndicator();
		return;
	}

	if (!m_busyShown) {
		if ((wxGetUTCTimeMillis() - m_busySinceMs).ToLong() < kBusyShowDelayMs)
			return;                       // still inside the grace period
		m_busyShown   = true;             // crossing the delay — start painting
		m_busyShownMs = wxGetUTCTimeMillis();
	}

	m_busyPhase = (m_busyPhase + 1) % kBusySteps;
	ShowBusyWindow(true);
}

void ibDataViewMainWindow::OnChar(wxKeyEvent& event)
{
	// propagate the char event upwards
	wxKeyEvent eventForParent(event);
	eventForParent.SetEventObject(m_owner);
	if (m_owner->ProcessWindowEvent(eventForParent))
		return;

	if (m_owner->HandleAsNavigationKey(event))
		return;

	m_owner->ProcessTableCharEvent(event, this);
}

void ibDataViewMainWindow::OnMouse(wxMouseEvent& event)
{
	if (event.GetEventType() == wxEVT_MOUSEWHEEL)
	{
		// set one always one scroll
		event.m_linesPerAction = event.m_columnsPerAction = 1;

		// let the base handle mouse wheel events.
		if (!m_owner->ProcessWindowEvent(event))
			event.Skip();

		return;
	}

	m_owner->ProcessTableMouseEvent(event, this);
}

void ibDataViewMainWindow::OnSetFocus(wxFocusEvent& event)
{
	m_owner->m_hasFocus = true;

	// Make the control usable from keyboard once it gets focus by ensuring
	// that it has a current row, if at all possible.
	//
	// EXCEPT WHILE A RESTORE IS STILL COMING. After a sort or a filter the row the
	// user was on is looked for in the fetched page, and when it is not in the
	// FIRST page it arrives with the backward portion a moment later. Stamping row
	// 0 as current in that gap paints a highlight at the top that then jumps away —
	// the phantom selection. The restore knows which row it wants; this only has to
	// keep out of its way.
	if (!m_owner->HasCurrentRow() && !m_owner->IsEmpty()
	    && !m_owner->m_pagedRestoreFocus.IsOk()
	    && !m_owner->m_skipFocusRowOnNextSetFocus)
	{
		m_owner->ChangeCurrentRow(0);
	}
	m_owner->m_skipFocusRowOnNextSetFocus = false;

	if (m_owner->HasCurrentRow())
	{
		Refresh();
	}
#if wxUSE_ACCESSIBILITY
	else
	{
		wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_FOCUS, m_owner, wxOBJID_CLIENT, wxACC_SELF);
	}
#endif // wxUSE_ACCESSIBILITY

	event.Skip();
}

void ibDataViewMainWindow::OnKillFocus(wxFocusEvent& event)
{
	m_owner->m_hasFocus = false;

	if (m_owner->HasCurrentRow())
		Refresh();

	event.Skip();
}

void ibDataViewMainWindow::OnCharHook(wxKeyEvent& event)
{
	m_owner->ProcessTableCharHookEvent(event, this);
}

#endif // !wxUSE_DATAVIEWCTRL
