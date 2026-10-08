///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/headerctrlg.cpp
// Purpose:     generic ibHeaderGenericCtrl implementation
// Author:      Vadim Zeitlin
// Created:     2008-12-03
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "headerctrlg.h"

#include <wx/dcbuffer.h>
#include <wx/renderer.h>

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

namespace
{
	static wxColour colour_selection(0x66, 0x66, 0x66);

	static wxPen pen_reorder(colour_selection);
	static wxBrush brush_reorder(colour_selection);

} // anonymous namespace

// ============================================================================
// ibHeaderGenericCtrl implementation
// ============================================================================

void DrawHeaderButton(wxWindow* win,
	wxDC& dc,
	const wxRect& rect,
	int flags = 0,
	wxHeaderSortIconType sortArrow = wxHDR_SORT_ICON_NONE,
	wxHeaderButtonParams* params = NULL)
{
	static wxPen shadowPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));

	dc.SetPen(shadowPen);
	dc.DrawLine(rect.GetRight(), rect.GetTop(),
		rect.GetRight(), rect.GetBottom());
	dc.DrawLine(rect.GetLeft(), rect.GetBottom(),
		rect.GetRight() + 1, rect.GetBottom());

	// As above, don't draw the outer border if the control has its own one.
	int ofs = 0;
	if (win->GetBorder() == wxBORDER_NONE)
	{
		dc.DrawLine(rect.GetLeft(), rect.GetTop(),
			rect.GetRight(), rect.GetTop());

		ofs = 1;
	}

	static wxPen lightPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

	// ⭐ THE OUTER LEFT EDGE IS DRAWN LIKE THE OUTER RIGHT ONE — in SHADOW, not in light.
	//
	// The pair of pens below is the old 3D button look: a shadow line down the right of every
	// column and a light line down its left. Between two columns that reads fine (the shadow is
	// the separator), but at the very first column the light line IS the control's left edge — and
	// against a white header it is invisible. The right-hand edge, being shadow, is not: the grid
	// then looks bordered on the right and cut off on the left (Max, 2026-08-19: "on the right it
	// is fine, on the left something ate it — and it is like that absolutely everywhere").
	//
	// Only the leftmost column is affected; every other column keeps its light line, because there
	// the shadow of the column before it is already the separator and two dark lines side by side
	// would read as a gap.
	const bool outerLeftEdge = rect.GetLeft() <= 0;
	dc.SetPen(outerLeftEdge ? shadowPen : lightPen);
	dc.DrawLine(rect.GetLeft(), rect.GetTop() + ofs,
		rect.GetLeft(), rect.GetBottom());

	// ⚠ AND IT STARTS ONE PIXEL IN AT THE OUTER EDGE. This light line is drawn AFTER the vertical
	// one and from the same x, so at the leftmost column it repainted the top pixel of the dark
	// edge — a single light dot in the corner, which is exactly what it looked like.
	dc.SetPen(lightPen);
	dc.DrawLine(rect.GetLeft() + (outerLeftEdge ? 1 : 0), rect.GetTop() + ofs,
		rect.GetRight(), rect.GetTop() + ofs);

	int labelWidth = 0;

	// Mark this item as selected.  For the generic version we'll just draw an
	// underline
	if (flags & wxCONTROL_SELECTED)
	{
		// draw a line at the bottom of the header button, overlaying the
		// native hot-tracking line (on XP)
		const int penwidth = 3;
		int y = rect.y + rect.height + 1 - penwidth;
		wxColour c = (params && params->m_selectionColour.IsOk()) ?
			params->m_selectionColour : colour_selection;
		wxPen pen(c, penwidth);
		pen.SetCap(wxCAP_BUTT);
		wxDCPenChanger setPen(dc, pen);
		dc.DrawLine(rect.x, y, rect.x + rect.width, y);
	}

	// Draw an up or down arrow
	int arrowSpace = 0;
	if (sortArrow != wxHDR_SORT_ICON_NONE)
	{
		wxRect ar = rect;

		// make a rect for the arrow
		ar.SetSize(wxWindow::FromDIP(wxSize(8, 4), win));
		ar.y += (rect.height - ar.height) / 2;
		ar.x = ar.x + rect.width - 3 * ar.width / 2;
		arrowSpace = 3 * ar.width / 2; // space to preserve when drawing the label

		wxPoint triPt[3];
		if (sortArrow & wxHDR_SORT_ICON_UP)
		{
			triPt[0].x = ar.width / 2;
			triPt[0].y = 0;
			triPt[1].x = ar.width;
			triPt[1].y = ar.height;
			triPt[2].x = 0;
			triPt[2].y = ar.height;
		}
		else
		{
			triPt[0].x = 0;
			triPt[0].y = 0;
			triPt[1].x = ar.width;
			triPt[1].y = 0;
			triPt[2].x = ar.width / 2;
			triPt[2].y = ar.height;
		}

		wxColour c = (params && params->m_arrowColour.IsOk()) ?
			params->m_arrowColour : wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW);

		wxDCPenChanger setPen(dc, c);
		wxDCBrushChanger setBrush(dc, c);

		wxDCClipper clip(dc, rect);
		dc.DrawPolygon(3, triPt, ar.x, ar.y);
	}
	labelWidth += arrowSpace;

	int bmpWidth = 0;

	// draw the bitmap if there is one
	if (params && params->m_labelBitmap.IsOk())
	{
		int w = params->m_labelBitmap.GetLogicalWidth();
		int h = params->m_labelBitmap.GetLogicalHeight();

		const int margin = 1; // an extra pixel on either side of the bitmap

		bmpWidth = w + 2 * margin;
		labelWidth += bmpWidth;

		int x = rect.x + margin;
		const int y = rect.y + wxMax(1, (rect.height - h) / 2);

		const int extraSpace = rect.width - labelWidth;
		if (params->m_labelText.empty() && extraSpace > 0)
		{
			// use the alignment flags
			switch (params->m_labelAlignment)
			{
			default:
			case wxALIGN_LEFT:
				break;

			case wxALIGN_CENTER:
				x += extraSpace / 2;
				break;

			case wxALIGN_RIGHT:
				x += extraSpace;
				break;
			}
		}

		wxDCClipper clip(dc, rect);
		dc.DrawBitmap(params->m_labelBitmap, x, y, true);
	}

	// Draw a label if one is given
	if (params && !params->m_labelText.empty())
	{
		const int margin = 5;   // number of pixels to reserve on either side of the label
		labelWidth += 2 * margin;

		wxFont font = params->m_labelFont.IsOk() ?
			params->m_labelFont : win->GetFont();
		wxColour clr = params->m_labelColour.IsOk() ?
			params->m_labelColour : win->GetForegroundColour();

		if (flags & wxCONTROL_DISABLED)
		{
			clr = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
		}

		wxString label(params->m_labelText);

		wxDCFontChanger setFont(dc, font);
		wxDCTextColourChanger setTextFg(dc, clr);
		wxDCTextBgModeChanger setBgMode(dc, wxBRUSHSTYLE_TRANSPARENT);

		int tw, th;
		dc.GetMultiLineTextExtent(label, &tw, &th);

		int x = rect.x + bmpWidth + margin;
		const int y = rect.y + wxMax(0, (rect.height - th) / 2);

		// truncate and add an ellipsis (...) if the text is too wide.
		const int availWidth = rect.width - labelWidth;

#if wxUSE_CONTROLS
		if (tw > availWidth)
		{
			label = wxControl::Ellipsize(label,
				dc,
				wxELLIPSIZE_END,
				availWidth,
				wxELLIPSIZE_FLAGS_NONE);
			tw = dc.GetTextExtent(label).x;
		}
		else // enough space, we can respect alignment
#endif // wxUSE_CONTROLS
		{
			switch (params->m_labelAlignment)
			{
			default:
			case wxALIGN_LEFT:
				break;

			case wxALIGN_CENTER:
				x += (availWidth - tw) / 2;
				break;

			case wxALIGN_RIGHT:
				x += availWidth - tw;
				break;
			}
		}

		dc.DrawText(label, x, y);

		labelWidth += tw;
	}
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl creation
// ----------------------------------------------------------------------------

void ibHeaderGenericCtrl::Init()
{
	m_numColumns = 0;
	m_hover =
		m_colBeingResized =
		m_colBeingReordered = COL_NONE;
	m_dragOffset = 0;
	m_scrollOffset = 0;
	m_numHeight = 1;
	m_wasSeparatorDClick = false;
}

bool ibHeaderGenericCtrl::Create(wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	if (!ibHeaderGenericCtrlBase::Create(parent, id, pos, size,
		style, wxDefaultValidator, name))
		return false;

	// tell the system to not paint the background at all to avoid flicker as
	// we paint the entire window area in our OnPaint()
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	return true;
}

ibHeaderGenericCtrl::~ibHeaderGenericCtrl()
{
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl columns manipulation
// ----------------------------------------------------------------------------

void ibHeaderGenericCtrl::DoSetCount(unsigned int count)
{
	// update the column indices order array before changing m_numColumns
	DoResizeColumnIndices(m_colIndices, count);

	m_numColumns = count;

	// don't leave the column index invalid, this would cause a crash later if
	// it is used from OnMouse()
	if (m_hover >= count)
		m_hover = COL_NONE;

	InvalidateBestSize();
	Refresh();
}

unsigned int ibHeaderGenericCtrl::DoGetCount() const
{
	return m_numColumns;
}

void ibHeaderGenericCtrl::DoUpdate(unsigned int idx)
{
	InvalidateBestSize();

	// we need to refresh not only this column but also the ones after it in
	// case it was shown or hidden or its width changed -- it would be nice to
	// avoid doing this unnecessary by storing the old column width (TODO)
	RefreshColsAfter(idx);
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl scrolling
// ----------------------------------------------------------------------------

void ibHeaderGenericCtrl::DoScrollHorz(int dx)
{
	m_scrollOffset += dx;

	// don't call our own version which calls this function!
	wxControl::ScrollWindow(dx, 0);
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl geometry
// ----------------------------------------------------------------------------

wxSize ibHeaderGenericCtrl::DoGetBestSize() const
{
	wxWindow* win = GetParent();
	int height = wxRendererNative::Get().GetHeaderButtonHeight(win) * wxMax(m_numHeight, 1);

	// the vertical size is rather arbitrary but it looks better if we leave
	// some space around the text
	return wxSize(IsEmpty() ? ibHeaderGenericCtrlBase::DoGetBestSize().x
		: GetColEnd(GetColumnCount() - 1),
		height); // (7*GetCharHeight())/4);
}

int ibHeaderGenericCtrl::GetColStart(unsigned int idx) const
{
	int pos = m_scrollOffset;
	for (unsigned n = 0; ; n++)
	{
		const unsigned i = m_colIndices[n];
		if (i == idx)
			break;

		const wxHeaderColumn& col = GetColumn(i);
		if (col.IsShown())
			pos += col.GetWidth();
	}

	return pos;
}

int ibHeaderGenericCtrl::GetColEnd(unsigned int idx) const
{
	int x = GetColStart(idx);

	return x + GetColumn(idx).GetWidth();
}

unsigned int ibHeaderGenericCtrl::FindColumnAtPoint(int xPhysical, int WXUNUSED(yPhysical), bool* onSeparator) const
{
	int pos = 0;
	int xLogical = xPhysical - m_scrollOffset;
	const unsigned count = GetColumnCount();
	for (unsigned n = 0; n < count; n++)
	{
		const unsigned idx = m_colIndices[n];
		const wxHeaderColumn& col = GetColumn(idx);
		if (col.IsHidden())
			continue;

		pos += col.GetWidth();

		// TODO: don't hardcode sensitivity
		const int separatorClickMargin = FromDIP(8);

		// if the column is resizable, check if we're approximatively over the
		// line separating it from the next column
		if (col.IsResizeable() && abs(xLogical - pos) < separatorClickMargin)
		{
			if (onSeparator) 
			{
				bool last_visible_col = false;

				for (unsigned num = n + 1; num < count; num++)
				{
					const wxHeaderColumn& col = GetColumn(m_colIndices[num]);
					if (col.IsHidden())
						continue;

					last_visible_col = true;
					break;
				}

				*onSeparator = last_visible_col;
			}
			
			return idx;
		}

		// inside this column?
		if (xLogical < pos)
		{
			if (onSeparator)
				*onSeparator = false;
			return idx;
		}
	}

	if (onSeparator)
		*onSeparator = false;
	return COL_NONE;
}

unsigned int ibHeaderGenericCtrl::FindColumnClosestToPoint(int xPhysical, int yPhysical) const
{
	const unsigned int colIndexAtPoint = FindColumnAtPoint(xPhysical, yPhysical, nullptr);

	// valid column found?
	if (colIndexAtPoint != COL_NONE)
		return colIndexAtPoint;

	// if not, xPhysical must be beyond the rightmost column, so return its
	// index instead -- if we have it
	const unsigned int count = GetColumnCount();
	if (!count)
		return COL_NONE;

	return m_colIndices[count - 1];
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl repainting
// ----------------------------------------------------------------------------

void ibHeaderGenericCtrl::RefreshCol(unsigned int idx)
{
	wxRect rect = GetClientRect();
	const int start = GetColStart(idx);
	rect.x += start;
	// Between the column's EDGES, not "its own width from its start". The two agree
	// in a flat header and stop agreeing the moment columns are stacked: there a
	// column's cell is as wide as its GROUP, so repainting its own narrower width
	// left the rest of the cell holding whatever was drawn on it before — a hover
	// underline that stayed behind after the mouse had gone.
	rect.width = GetColEnd(idx) - start;

	RefreshRect(rect);
}

void ibHeaderGenericCtrl::RefreshColIfNotNone(unsigned int idx)
{
	if (idx != COL_NONE)
		RefreshCol(idx);
}

void ibHeaderGenericCtrl::RefreshColsAfter(unsigned int idx)
{
	wxRect rect = GetClientRect();
	const int ofs = GetColStart(idx);
	if (ofs >= rect.width)
		return;
	rect.x += ofs;
	rect.width -= ofs;

	RefreshRect(rect);
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl dragging/resizing/reordering
// ----------------------------------------------------------------------------

bool ibHeaderGenericCtrl::IsResizing() const
{
	return m_colBeingResized != COL_NONE;
}

bool ibHeaderGenericCtrl::IsReordering() const
{
	return m_colBeingReordered != COL_NONE;
}

void ibHeaderGenericCtrl::ClearMarkers()
{
	wxClientDC dc(this);

	wxDCOverlay dcover(m_overlay, &dc);
	dcover.Clear();
}

void ibHeaderGenericCtrl::EndDragging()
{
	// THE HIGHLIGHT IS DROPPED WITH THE DRAG. It is remembered as a column INDEX, and the
	// drag has just moved every border — so the cell drawn "under the mouse" afterwards is
	// a cell the mouse is no longer over: a frame left standing around a heading. The next
	// mouse-move sets it again, correctly.
	m_hover = COL_NONE;

	// A REPAINT ENDS EVERY DRAG, resize as well as reorder: the header that was on screen
	// while the mouse was down (a phantom, a selection frame, a half-drawn divider) is not
	// the header that should be on screen after it comes up.
	Refresh();

	// We currently only use markers for reordering, not for resizing
	if (IsReordering())
	{
		// THE OVERLAY IS RELEASED, NOT JUST CLEARED, AND THE HEADER REDRAWN.
		//
		// Clearing paints the overlay's own backing back — which is the header as it was
		// when the drag STARTED. After a reorder the header is no longer that: the column
		// has moved, so the restored pixels are a ghost of the drag (a phantom frame with
		// two half-titles inside it, sitting between the columns it used to be dragged
		// past). Dropping the overlay and asking for a real repaint is the only way the
		// header on screen is the header as it now IS.
		ClearMarkers();
		m_overlay.Reset();
		Refresh();
	}

	// don't use the special dragging cursor any more
	SetCursor(wxNullCursor);
}

void ibHeaderGenericCtrl::CancelDragging()
{
	wxASSERT_MSG(IsDragging(),
		"shouldn't be called if we're not dragging anything");

	EndDragging();

	unsigned int& col = IsResizing() ? m_colBeingResized : m_colBeingReordered;

	ibHeaderGenericCtrlEvent event(wxEVT_HEADER_DRAGGING_CANCELLED, GetId());
	event.SetEventObject(this);
	event.SetColumn(col);

	GetEventHandler()->ProcessEvent(event);

	col = COL_NONE;
}

int ibHeaderGenericCtrl::ConstrainByMinWidth(unsigned int col, int& xPhysical)
{
	const int xStart = GetColStart(col);

	// notice that GetMinWidth() returns 0 if there is no minimal width so it
	// still makes sense to use it even in this case
	const int xMinEnd = xStart + GetColumn(col).GetMinWidth();

	if (xPhysical < xMinEnd)
		xPhysical = xMinEnd;

	return xPhysical - xStart;
}

void ibHeaderGenericCtrl::StartOrContinueResizing(unsigned int col, int xPhysical)
{
	ibHeaderGenericCtrlEvent event(IsResizing() ? wxEVT_HEADER_RESIZING
		: wxEVT_HEADER_BEGIN_RESIZE,
		GetId());
	event.SetEventObject(this);
	event.SetColumn(col);

	event.SetWidth(ConstrainByMinWidth(col, xPhysical));

	if (GetEventHandler()->ProcessEvent(event) && !event.IsAllowed())
	{
		if (IsResizing())
		{
			ReleaseMouse();
			CancelDragging();
		}
		//else: nothing to do -- we just don't start to resize
	}
	else // go ahead with resizing
	{
		if (!IsResizing())
		{
			m_colBeingResized = col;
			SetCursor(wxCursor(wxCURSOR_SIZEWE));
			CaptureMouse();
		}
		//else: we had already done the above when we started

	}
}

void ibHeaderGenericCtrl::EndResizing(int xPhysical)
{
	wxASSERT_MSG(IsResizing(), "shouldn't be called if we're not resizing");

	EndDragging();

	ReleaseMouse();

	ibHeaderGenericCtrlEvent event(wxEVT_HEADER_END_RESIZE, GetId());
	event.SetEventObject(this);
	event.SetColumn(m_colBeingResized);
	event.SetWidth(ConstrainByMinWidth(m_colBeingResized, xPhysical));

	GetEventHandler()->ProcessEvent(event);

	m_colBeingResized = COL_NONE;
}

void ibHeaderGenericCtrl::UpdateReorderingMarker(int xPhysical, int yPhysical)
{
	// BETWEEN TWO COLUMNS — a vertical bar at the edge the column would land on. Where
	// columns are grouped the drop is not always between two of them, and the derived
	// header shapes its own hint (see ibDataViewHeaderWindow).
	const unsigned int col = FindColumnClosestToPoint(xPhysical, yPhysical);
	if (col == COL_NONE) {
		DrawReorderingMarker(xPhysical, wxRect());
		return;
	}

	static const int DROP_MARKER_WIDTH = 4;

	DrawReorderingMarker(xPhysical, wxRect(GetColEnd(col) - DROP_MARKER_WIDTH / 2, 0,
		DROP_MARKER_WIDTH, GetClientSize().y));
}

// THE PHANTOM AND THE HINT TOGETHER, because they share one overlay: drawing either alone
// would rub the other out. A derived header decides WHERE the hint goes and passes it in —
// a bar between columns, or a bar between BANDS when the column would land in a stack.
void ibHeaderGenericCtrl::DrawReorderingMarker(int xPhysical, const wxRect& hint)
{
	wxClientDC dc(this);

	wxDCOverlay dcover(m_overlay, &dc);
	dcover.Clear();

	// ONLY THE HINT — where the column would land. The upstream code also drew a PHANTOM
	// frame of the dragged column following the pointer, and that frame is what read as "the
	// overlay glitches": it is restored from pixels captured when the drag began, so after
	// the columns move it shows a stale header inside a floating box, and any repaint leaves
	// pieces of it behind. The hint alone says everything the phantom did.
	if (hint.IsEmpty())
		return;

	dc.SetPen(colour_selection);
	dc.SetBrush(brush_reorder);
	dc.DrawRectangle(hint);
}

void ibHeaderGenericCtrl::StartReordering(unsigned int col, int xPhysical)
{
	ibHeaderGenericCtrlEvent event(wxEVT_HEADER_BEGIN_REORDER, GetId());
	event.SetEventObject(this);
	event.SetColumn(col);

	if (GetEventHandler()->ProcessEvent(event) && !event.IsAllowed())
	{
		// don't start dragging it, nothing to do otherwise
		return;
	}

	m_dragOffset = xPhysical - GetColStart(col);

	m_colBeingReordered = col;
	SetCursor(wxCursor(wxCURSOR_HAND));
	CaptureMouse();

	// do not call UpdateReorderingMarker() here: we don't want to give
	// feedback for reordering until the user starts to really move the mouse
	// as he might want to just click on the column and not move it at all
}

bool ibHeaderGenericCtrl::EndReordering(int xPhysical, int yPhysical)
{
	wxASSERT_MSG(IsReordering(), "shouldn't be called if we're not reordering");

	EndDragging();

	ReleaseMouse();

	const int colOld = m_colBeingReordered;
	const unsigned colNew = FindColumnClosestToPoint(xPhysical, yPhysical);

	m_colBeingReordered = COL_NONE;

	// mouse drag must be longer than min distance m_dragOffset
	if (xPhysical - GetColStart(colOld) == m_dragOffset)
	{
		return false;
	}

	// cannot proceed without a valid column index
	if (colNew == COL_NONE)
	{
		return false;
	}

	if (static_cast<int>(colNew) != colOld)
	{
		ibHeaderGenericCtrlEvent event(wxEVT_HEADER_END_REORDER, GetId());
		event.SetEventObject(this);
		event.SetColumn(colOld);

		const unsigned pos = GetColumnPos(colNew);
		event.SetNewOrder(pos);

		if (!GetEventHandler()->ProcessEvent(event) || event.IsAllowed())
		{
			// do reorder the columns
			DoMoveCol(colOld, pos);
		}
	}

	// whether we moved the column or not, the user did move the mouse and so
	// did try to do it so return true
	return true;
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl column reordering
// ----------------------------------------------------------------------------

void ibHeaderGenericCtrl::DoSetColumnsOrder(const wxArrayInt& order)
{
	m_colIndices = order;
	Refresh();
}

wxArrayInt ibHeaderGenericCtrl::DoGetColumnsOrder() const
{
	return m_colIndices;
}

void ibHeaderGenericCtrl::DoMoveCol(unsigned int idx, unsigned int pos)
{
	MoveColumnInOrderArray(m_colIndices, idx, pos);

	Refresh();
}

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl event handlers
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibHeaderGenericCtrl, ibHeaderGenericCtrlBase)
EVT_PAINT(ibHeaderGenericCtrl::OnPaint)
EVT_MOUSE_EVENTS(ibHeaderGenericCtrl::OnMouse)
EVT_MOUSE_CAPTURE_LOST(ibHeaderGenericCtrl::OnCaptureLost)
EVT_KEY_DOWN(ibHeaderGenericCtrl::OnKeyDown)
wxEND_EVENT_TABLE()

// The FLAT header: one full-height cell per column, left to right. A header that
// knows about column groups overrides this and hands back a different geometry —
// the paint loop below does not change, which is the point of routing both
// through one description.
void ibHeaderGenericCtrl::BuildHeaderButtons(std::vector<ibHeaderButton>& cells, int height) const
{
	int xpos = 0;
	for (unsigned int i = 0; i < m_numColumns; i++)
	{
		const unsigned idx = m_colIndices[i];
		const wxHeaderColumn& col = GetColumn(idx);
		if (col.IsHidden())
			continue;

		const int colWidth = col.GetWidth();

		ibHeaderButton cell;
		cell.rect = wxRect(xpos, 0, colWidth, height);
		cell.column = idx;
		cell.title = col.GetTitle();
		cell.bitmap = col.GetBitmapBundle();
		cell.align = col.GetAlignment();
		cells.push_back(cell);

		xpos += colWidth;
	}
}

int ibHeaderGenericCtrl::GetHeaderButtonsEnd(const std::vector<ibHeaderButton>& cells) const
{
	int end = 0;
	for (const ibHeaderButton& cell : cells)
		end = wxMax(end, cell.rect.GetRight() + 1);
	return end;
}

void ibHeaderGenericCtrl::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	int w, h;
	GetClientSize(&w, &h);

	wxAutoBufferedPaintDC dc(this);
	dc.Clear();

	std::vector<ibHeaderButton> cells;
	BuildHeaderButtons(cells, h);

	bool firstDrawn = true;
	for (const ibHeaderButton& cell : cells)
	{
		wxRect rect = cell.rect;
		rect.x += m_scrollOffset;

		// Off screen either way — nothing to draw. (Not a `break`: with groups the
		// cells are not handed out in left-to-right order, so a cell past the right
		// edge says nothing about the ones after it.)
		if (rect.GetRight() < 0 || rect.x > w)
			continue;

		wxHeaderSortIconType sortArrow = wxHDR_SORT_ICON_NONE;
		int state = 0;

		if (!IsEnabled())
		{
			state = wxCONTROL_DISABLED;
		}
		else if (!cell.isGroup && cell.column == m_hover)
		{
			// A group title is not hovered / sorted / clicked — it labels columns,
			// it is not one.
			state = wxCONTROL_SELECTED;
		}

		if (!cell.isGroup)
		{
			const wxHeaderColumn& col = GetColumn(cell.column);
			if (col.IsSortKey())
			{
				sortArrow = col.IsSortOrderAscending() ? wxHDR_SORT_ICON_UP
					: wxHDR_SORT_ICON_DOWN;
			}
		}

		if (firstDrawn)
		{
			state |= wxCONTROL_SPECIAL;
			firstDrawn = false;
		}

		wxHeaderButtonParams params;
		params.m_labelText = cell.title;
		params.m_labelBitmap = cell.bitmap.GetBitmapFor(this);
		params.m_labelAlignment = cell.align;

		DrawHeaderButton(this, dc, rect, state, sortArrow, &params);
	}

	const int xend = GetHeaderButtonsEnd(cells) + m_scrollOffset;
	if (xend < w)
	{
		int state = wxCONTROL_NONE;
		if (!IsEnabled())
			state |= wxCONTROL_DISABLED;
		DrawHeaderButton(
			this, dc, wxRect(xend, 0, w - xend, h));
	}
}

void ibHeaderGenericCtrl::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
	if (IsDragging())
		CancelDragging();
}

void ibHeaderGenericCtrl::OnKeyDown(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE)
	{
		if (IsDragging())
		{
			ReleaseMouse();
			CancelDragging();

			return;
		}
	}

	event.Skip();
}

void ibHeaderGenericCtrl::OnMouse(wxMouseEvent& mevent)
{
	const bool wasSeparatorDClick = m_wasSeparatorDClick;
	m_wasSeparatorDClick = false;

	// do this in advance to allow simply returning if we're not interested,
	// we'll undo it if we do handle the event below
	mevent.Skip();

	// account for the control displacement
	const int xPhysical = mevent.GetX();

	// first deal with the [continuation of any] dragging operations in
	// progress
	if (IsResizing())
	{
		if (mevent.LeftUp())
			EndResizing(xPhysical);
		else // update the live separator position
			StartOrContinueResizing(m_colBeingResized, xPhysical);

		return;
	}

	if (IsReordering())
	{
		if (!mevent.LeftUp())
		{
			// update the column position
			UpdateReorderingMarker(xPhysical, mevent.GetY());

			return;
		}

		// finish reordering and continue to generate a click event below if we
		// didn't really reorder anything
		if (EndReordering(xPhysical, mevent.GetY()))
			return;
	}


	// find if the event is over a column at all — the y matters where columns are
	// stacked (a vertical group), several sharing one x range.
	bool onSeparator;
	const unsigned col = mevent.Leaving()
		? (onSeparator = false, COL_NONE)
		: FindColumnAtPoint(xPhysical, mevent.GetY(), &onSeparator);


	// update the highlighted column if it changed
	if (col != m_hover)
	{
		const unsigned hoverOld = m_hover;
		m_hover = col;

		RefreshColIfNotNone(hoverOld);
		RefreshColIfNotNone(m_hover);
	}

	// update mouse cursor as it moves around
	if (mevent.Moving())
	{
		SetCursor(onSeparator ? wxCursor(wxCURSOR_SIZEWE) : wxNullCursor);
		return;
	}

	// all the other events only make sense when they happen over a column
	if (col == COL_NONE)
		return;


	// enter various dragging modes on left mouse press
	if (mevent.LeftDown())
	{
		if (onSeparator)
		{
			// start resizing the column
			wxASSERT_MSG(!IsResizing(), "reentering column resize mode?");
			StartOrContinueResizing(col, xPhysical);
		}
		// on column itself - both header and column must have the appropriate
		// flags to allow dragging the column
		else if (HasFlag(wxHD_ALLOW_REORDER) && GetColumn(col).IsReorderable())
		{

			// start dragging the column
			wxASSERT_MSG(!IsReordering(), "reentering column move mode?");

			StartReordering(col, xPhysical);
		}

		return;
	}

	// determine the type of header event corresponding to click events
	wxEventType evtType = wxEVT_NULL;
	const bool click = mevent.ButtonUp(),
		dblclk = mevent.ButtonDClick();
	if (click || dblclk)
	{
		switch (mevent.GetButton())
		{
		case wxMOUSE_BTN_LEFT:
			// treat left double clicks on separator specially
			if (onSeparator && dblclk)
			{
				evtType = wxEVT_HEADER_SEPARATOR_DCLICK;
				m_wasSeparatorDClick = true;
			}
			else if (!wasSeparatorDClick)
			{
				evtType = click ? wxEVT_HEADER_CLICK
					: wxEVT_HEADER_DCLICK;
			}
			break;

		case wxMOUSE_BTN_RIGHT:
			evtType = click ? wxEVT_HEADER_RIGHT_CLICK
				: wxEVT_HEADER_RIGHT_DCLICK;
			break;

		case wxMOUSE_BTN_MIDDLE:
			evtType = click ? wxEVT_HEADER_MIDDLE_CLICK
				: wxEVT_HEADER_MIDDLE_DCLICK;
			break;

		default:
			// ignore clicks from other mouse buttons
			;
		}
	}

	if (evtType == wxEVT_NULL)
		return;

	ibHeaderGenericCtrlEvent event(evtType, GetId());
	event.SetEventObject(this);
	event.SetColumn(col);

	if (GetEventHandler()->ProcessEvent(event))
		mevent.Skip(false);
}