/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/private/grid.h
// Purpose:     Private ibGrid structures
// Author:      Michael Bedward (based on code by Julian Smart, Robin Dunn)
// Modified by: Santiago Palacios
// Created:     1/08/1999
// Copyright:   (c) Michael Bedward
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GENERIC_GRID_PRIVATE_H_
#define _WX_GENERIC_GRID_PRIVATE_H_

#include "wx/defs.h"

#if wxUSE_GRID

#include "wx/headerctrl.h"

// for ibGridOperations
#include "gridextsel.h"


// ----------------------------------------------------------------------------
// array classes
// ----------------------------------------------------------------------------

WX_DEFINE_ARRAY_WITH_DECL_PTR(ibGridCellAttr*, wxArrayAttrs,
	class);

WX_DECLARE_HASH_MAP_WITH_DECL(wxLongLong_t, ibGridCellAttr*,
	wxIntegerHash, wxIntegerEqual,
	ibGridCoordsToAttrMap, class);


// ----------------------------------------------------------------------------
// private classes
// ----------------------------------------------------------------------------

// header column providing access to the column information stored in ibGrid
// via wxHeaderColumn interface
class ibGridHeaderColumn : public wxHeaderColumn
{
public:
	ibGridHeaderColumn(ibGrid* grid, int col)
		: m_grid(grid),
		m_col(col)
	{
	}

	virtual wxString GetTitle() const wxOVERRIDE { return m_grid->GetColLabelValue(m_col); }
	virtual wxBitmap GetBitmap() const wxOVERRIDE { return wxNullBitmap; }
	virtual wxBitmapBundle GetBitmapBundle() const wxOVERRIDE { return wxBitmapBundle(); }
	virtual int GetWidth() const wxOVERRIDE { return m_grid->GetColSize(m_col); }
	virtual int GetMinWidth() const wxOVERRIDE { return m_grid->GetColMinimalWidth(m_col); }
	virtual wxAlignment GetAlignment() const wxOVERRIDE
	{
		int horz,
			vert;
		m_grid->GetColLabelAlignment(&horz, &vert);

		return static_cast<wxAlignment>(horz);
	}

	virtual int GetFlags() const wxOVERRIDE
	{
		// we can't know in advance whether we can sort by this column or not
		// with ibGrid API so suppose we can by default
		int flags = wxCOL_SORTABLE;
		if (m_grid->CanDragColSize(m_col))
			flags |= wxCOL_RESIZABLE;
		if (m_grid->CanDragColMove())
			flags |= wxCOL_REORDERABLE;
		if (GetWidth() == 0)
			flags |= wxCOL_HIDDEN;

		return flags;
	}

	virtual bool IsSortKey() const wxOVERRIDE
	{
		return m_grid->IsSortingBy(m_col);
	}

	virtual bool IsSortOrderAscending() const wxOVERRIDE
	{
		return m_grid->IsSortOrderAscending();
	}

private:
	// these really should be const but are not because the column needs to be
	// assignable to be used in a wxVector (in STL build, in non-STL build we
	// avoid the need for this)
	ibGrid* m_grid;
	int m_col;
};

// header control retrieving column information from the grid
class ibGridHeaderCtrl : public wxHeaderCtrl
{
public:
	ibGridHeaderCtrl(ibGrid* owner)
		: wxHeaderCtrl(owner,
			wxID_ANY,
			wxDefaultPosition,
			wxDefaultSize,
			(owner->CanHideColumns() ? wxHD_ALLOW_HIDE : 0) |
			(owner->CanDragColMove() ? wxHD_ALLOW_REORDER : 0))
	{
		m_inResizing = 0;
	}

	// Special method to call from ibGrid::DoSetColSize(), see comments there.
	void UpdateIfNotResizing(unsigned int idx)
	{
		if (!m_inResizing)
			UpdateColumn(idx);
	}

protected:
	virtual const wxHeaderColumn& GetColumn(unsigned int idx) const wxOVERRIDE
	{
		return m_columns[idx];
	}

	ibGrid* GetOwner() const { return static_cast<ibGrid*>(GetParent()); }

private:
	wxMouseEvent GetDummyMouseEvent() const
	{
		// make up a dummy event for the grid event to use -- unfortunately we
		// can't do anything else here
		wxMouseEvent e;
		e.SetState(wxGetMouseState());
		GetOwner()->ScreenToClient(&e.m_x, &e.m_y);
		return e;
	}

	// override the base class method to update our m_columns array
	virtual void OnColumnCountChanging(unsigned int count) wxOVERRIDE
	{
		const unsigned countOld = m_columns.size();
		if (count < countOld)
		{
			// just discard the columns which don't exist any more (notice that
			// we can't use resize() here as it would require the vector
			// value_type, i.e. ibGridHeaderColumn to be default constructible,
			// which it is not)
			m_columns.erase(m_columns.begin() + count, m_columns.end());
		}
		else // new columns added
		{
			// add columns for the new elements
			for (unsigned n = countOld; n < count; n++)
				m_columns.push_back(ibGridHeaderColumn(GetOwner(), n));
		}
	}

	// override to implement column auto sizing
	virtual bool UpdateColumnWidthToFit(unsigned int idx, int WXUNUSED(widthTitle)) wxOVERRIDE
	{
		GetOwner()->HandleColumnAutosize(idx, GetDummyMouseEvent());

		return true;
	}

	// overridden to react to the actions using the columns popup menu
	virtual void UpdateColumnVisibility(unsigned int idx, bool show) wxOVERRIDE
	{
		GetOwner()->SetColSize(idx, show ? wxGRID_AUTOSIZE : 0);

		// as this is done by the user we should notify the main program about
		// it
		GetOwner()->SendGridSizeEvent(wxEVT_GRID_COL_SIZE, idx,
			GetDummyMouseEvent());
	}

	// overridden to react to the columns order changes in the customization
	// dialog
	virtual void UpdateColumnsOrder(const wxArrayInt& order) wxOVERRIDE
	{
		GetOwner()->SetColumnsOrder(order);
	}


	// event handlers forwarding wxHeaderCtrl events to ibGrid
	void OnClick(wxHeaderCtrlEvent& event)
	{
		GetOwner()->SendEvent(wxEVT_GRID_LABEL_LEFT_CLICK,
			-1, event.GetColumn(),
			GetDummyMouseEvent());

		GetOwner()->DoColHeaderClick(event.GetColumn());
	}

	void OnDoubleClick(wxHeaderCtrlEvent& event)
	{
		if (!GetOwner()->SendEvent(wxEVT_GRID_LABEL_LEFT_DCLICK,
			-1, event.GetColumn(),
			GetDummyMouseEvent()))
		{
			event.Skip();
		}
	}

	void OnRightClick(wxHeaderCtrlEvent& event)
	{
		if (!GetOwner()->SendEvent(wxEVT_GRID_LABEL_RIGHT_CLICK,
			-1, event.GetColumn(),
			GetDummyMouseEvent()))
		{
			event.Skip();
		}
	}

	void OnBeginResize(wxHeaderCtrlEvent& event)
	{
		GetOwner()->DoHeaderStartDragResizeCol(event.GetColumn());

		event.Skip();
	}

	void OnResizing(wxHeaderCtrlEvent& event)
	{
		// Calling ibGrid method results in a call to our own UpdateColumn()
		// because it ends up in ibGrid::SetColSize() which must indeed update
		// the column when it's called by the program -- but in the case where
		// the size change comes from the column itself, it is useless and, in
		// fact, harmful, as it results in extra flicker due to the inefficient
		// implementation of UpdateColumn() in wxMSW wxHeaderCtrl, so skip
		// calling it from our overridden version by setting this flag for the
		// duration of this function execution and checking it in our
		// UpdateIfNotResizing().
		m_inResizing++;

		GetOwner()->DoHeaderDragResizeCol(event.GetWidth());

		m_inResizing--;
	}

	void OnEndResize(wxHeaderCtrlEvent& event)
	{
		GetOwner()->DoHeaderEndDragResizeCol(event.GetWidth());

		event.Skip();
	}

	void OnBeginReorder(wxHeaderCtrlEvent& event)
	{
		GetOwner()->DoStartMoveRowOrCol(event.GetColumn());
	}

	void OnEndReorder(wxHeaderCtrlEvent& event)
	{
		GetOwner()->DoEndMoveCol(event.GetNewOrder());
	}

	wxVector<ibGridHeaderColumn> m_columns;

	// The count of OnResizing() call nesting, 0 if not inside it.
	int m_inResizing;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridHeaderCtrl);
};

// common base class for various grid subwindows
class ibGridSubwindow : public wxWindow
{
public:
	ibGridSubwindow(ibGrid* owner,
		int additionalStyle = 0,
		const wxString& name = wxASCII_STR(wxPanelNameStr))
		: wxWindow(owner, wxID_ANY,
			wxDefaultPosition, wxDefaultSize,
			wxBORDER_NONE | additionalStyle,
			name)
	{
		m_owner = owner;

		// (⛔ NO wxBG_STYLE_PAINT HERE. Tried 2026-08-26 and REVERTED the same minute: it is only
		//  true of the CELL window, which paints its whole rectangle itself (DrawGridSpace). The
		//  label / area / outline strips paint their ITEMS and let the system fill everything around
		//  them — the space below the last row, the strip's own margins. Denied that fill they
		//  showed an uninitialised buffer: black bands across the headings. Whoever tries this again
		//  must give each strip its own Clear() first, in its own colour.)
	}

	virtual wxWindow* GetMainWindowOfCompositeControl() wxOVERRIDE { return m_owner; }

	virtual bool AcceptsFocus() const wxOVERRIDE { return false; }

	ibGrid* GetOwner() { return m_owner; }

	virtual bool IsFrozen() const { return false; }

protected:
	void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

	ibGrid* m_owner;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridSubwindow);
};

class ibGridRowAreaWindow : public ibGridSubwindow
{
public:
	ibGridRowAreaWindow(ibGrid* parent)
		: ibGridSubwindow(parent)
	{
	}

private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridRowAreaWindow);
};

class ibGridRowFrozenAreaWindow : public ibGridRowAreaWindow
{
public:

	ibGridRowFrozenAreaWindow(ibGrid* parent)
		: ibGridRowAreaWindow(parent)
	{
	}

	virtual bool IsFrozen() const wxOVERRIDE { return true; }
};

class ibGridRowLabelWindow : public ibGridSubwindow
{
public:
	ibGridRowLabelWindow(ibGrid* parent)
		: ibGridSubwindow(parent)
	{
	}

private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridRowLabelWindow);
};

class ibGridRowFrozenLabelWindow : public ibGridRowLabelWindow
{
public:
	ibGridRowFrozenLabelWindow(ibGrid* parent)
		: ibGridRowLabelWindow(parent)
	{
	}

	virtual bool IsFrozen() const wxOVERRIDE { return true; }
};

class ibGridColAreaWindow : public ibGridSubwindow
{
public:
	ibGridColAreaWindow(ibGrid* parent)
		: ibGridSubwindow(parent)
	{
	}

private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridColAreaWindow);
};

class ibGridColFrozenAreaWindow : public ibGridColAreaWindow
{
public:
	ibGridColFrozenAreaWindow(ibGrid* parent)
		: ibGridColAreaWindow(parent)
	{
	}

	virtual bool IsFrozen() const wxOVERRIDE { return true; }
};

// Excel-style outline panes. Independent of the existing area windows:
// visibility is driven purely by whether any m_rowAreaAt/m_colAreaAt entry
// has m_level > 0. Row outline sits to the LEFT of the row label; col outline
// sits ABOVE the col label.
class ibGridRowOutlineWindow : public ibGridSubwindow
{
public:
	ibGridRowOutlineWindow(ibGrid* parent) : ibGridSubwindow(parent) {}
private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridRowOutlineWindow);
};

// A FROZEN STRIP IS STILL PART OF THE SHEET, and rows kept in view may perfectly well open a
// group. Without a pane of its own the outline simply stopped where the freeze began: no rail,
// no button and no background at all beside the frozen rows (Max, 2026-08-20: "nothing is
// rendered at the top — even the colour is lost"). Same twin the label and the area panes have.
class ibGridRowFrozenOutlineWindow : public ibGridRowOutlineWindow
{
public:
	ibGridRowFrozenOutlineWindow(ibGrid* parent) : ibGridRowOutlineWindow(parent) {}

	virtual bool IsFrozen() const wxOVERRIDE { return true; }
};

class ibGridColOutlineWindow : public ibGridSubwindow
{
public:
	ibGridColOutlineWindow(ibGrid* parent) : ibGridSubwindow(parent) {}
private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridColOutlineWindow);
};

class ibGridColFrozenOutlineWindow : public ibGridColOutlineWindow
{
public:
	ibGridColFrozenOutlineWindow(ibGrid* parent) : ibGridColOutlineWindow(parent) {}

	virtual bool IsFrozen() const wxOVERRIDE { return true; }
};

// WHERE THE OUTLINE PANE MEETS THE REST OF THE CHROME. The row pane starts below the column
// chrome and the column pane to the right of the row chrome, so the corner between them — and the
// strip each pane runs past — belonged to nobody and came up as bare window background: a white
// notch in the frame (Max, 2026-08-20: "you forgot the corner"). It carries no content, only the
// chrome colour, so it needs no paint handler of its own.
class ibGridOutlineCornerWindow : public ibGridSubwindow
{
public:
	ibGridOutlineCornerWindow(ibGrid* parent) : ibGridSubwindow(parent) {}

	virtual bool AcceptsFocus() const wxOVERRIDE { return false; }

	wxDECLARE_NO_COPY_CLASS(ibGridOutlineCornerWindow);
};

class ibGridColLabelWindow : public ibGridSubwindow
{
public:
	ibGridColLabelWindow(ibGrid* parent)
		: ibGridSubwindow(parent)
	{
	}

private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridColLabelWindow);
};

class ibGridColFrozenLabelWindow : public ibGridColLabelWindow
{
public:
	ibGridColFrozenLabelWindow(ibGrid* parent)
		: ibGridColLabelWindow(parent)
	{
	}

	virtual bool IsFrozen() const wxOVERRIDE { return true; }
};

class ibGridCornerLabelWindow : public ibGridSubwindow
{
public:
	ibGridCornerLabelWindow(ibGrid* parent)
		: ibGridSubwindow(parent)
	{
	}

private:
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnPaint(wxPaintEvent& event);

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridCornerLabelWindow);
};

class ibGridWindow : public ibGridSubwindow
{
public:
	// grid window variants for scrolling possibilities
	enum ibGridWindowType
	{
		ibGridWindowNormal = 0,
		ibGridWindowFrozenCol = 1,
		ibGridWindowFrozenRow = 2,
		ibGridWindowFrozenCorner = ibGridWindowFrozenCol |
		ibGridWindowFrozenRow
	};

	ibGridWindow(ibGrid* parent, ibGridWindowType type)
		: ibGridSubwindow(parent,
			wxWANTS_CHARS | wxCLIP_CHILDREN,
			"GridWindow"),
		m_type(type)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
	}


	virtual void ScrollWindow(int dx, int dy, const wxRect* rect) wxOVERRIDE;

	virtual bool AcceptsFocus() const wxOVERRIDE { return true; }

	ibGridWindowType GetType() const { return m_type; }

private:
	const ibGridWindowType m_type;

	void OnPaint(wxPaintEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent&);
	void OnKeyUp(wxKeyEvent&);
	void OnChar(wxKeyEvent&);
	void OnFocus(wxFocusEvent&);

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibGridWindow);
};

// ----------------------------------------------------------------------------
// the internal data representation used by ibGridCellAttrProvider
// ----------------------------------------------------------------------------

// this class stores attributes set for cells
class ibGridCellAttrData
{
public:
	~ibGridCellAttrData();

	void SetAttr(ibGridCellAttr* attr, int row, int col);
	ibGridCellAttr* GetAttr(int row, int col) const;
	void UpdateAttrRows(size_t pos, int numRows);
	void UpdateAttrCols(size_t pos, int numCols);

private:
	// Tries to search for the attr for given cell.
	ibGridCoordsToAttrMap::iterator FindIndex(int row, int col) const;

	mutable ibGridCoordsToAttrMap m_attrs;
};

// this class stores attributes set for rows or columns
class ibGridRowOrColAttrData
{
public:
	// empty ctor to suppress warnings
	ibGridRowOrColAttrData() {}
	~ibGridRowOrColAttrData();

	void SetAttr(ibGridCellAttr* attr, int rowOrCol);
	ibGridCellAttr* GetAttr(int rowOrCol) const;
	void UpdateAttrRowsOrCols(size_t pos, int numRowsOrCols);

private:
	wxArrayInt m_rowsOrCols;
	wxArrayAttrs m_attrs;
};

// NB: this is just a wrapper around 3 objects: one which stores cell
//     attributes, and 2 others for row/col ones
class ibGridCellAttrProviderData
{
public:
	ibGridCellAttrData m_cellAttrs;
	ibGridRowOrColAttrData m_rowAttrs,
		m_colAttrs;
};

// ----------------------------------------------------------------------------
// operations classes abstracting the difference between operating on rows and
// columns
// ----------------------------------------------------------------------------

// This class allows to write a function only once because by using its methods
// it will apply to both columns and rows.
//
// This is an abstract interface definition, the two concrete implementations
// below should be used when working with rows and columns respectively.
class ibGridOperations
{
public:
	// Returns the operations in the other direction, i.e. ibGridRowOperations
	// if this object is a ibGridColumnOperations and vice versa.
	virtual ibGridOperations& Dual() const = 0;

	// returns wxHORIZONTAL or wxVERTICAL for row/col operations
	virtual int GetOrientation() const = 0;

	// return row/col specific cursor modes
	virtual ibGrid::CursorMode GetCursorModeResize() const = 0;
	virtual ibGrid::CursorMode GetCursorModeSelect() const = 0;
	virtual ibGrid::CursorMode GetCursorModeMove() const = 0;

	// Return the total number of rows or columns.
	virtual int GetTotalNumberOfLines(const ibGrid* grid) const = 0;

	// Return the current number of rows or columns of a grid window.
	virtual int GetNumberOfLines(const ibGrid* grid, ibGridWindow* gridWindow) const = 0;

	// Return the first line for this grid type.
	virtual int GetFirstLine(const ibGrid* grid, ibGridWindow* gridWindow) const = 0;

	// Return the selection mode which allows selecting rows or columns.
	virtual ibGrid::ibGridSelectionModes GetSelectionMode() const = 0;

	// Make a ibGridCellCoords from the given components: thisDir is row or
	// column and otherDir is column or row
	virtual ibGridCellCoords MakeCoords(int thisDir, int otherDir) const = 0;

	// Calculate the scrolled position of the given abscissa or ordinate.
	virtual int CalcScrolledPosition(ibGrid* grid, int pos) const = 0;

	// Selects the horizontal or vertical component from the given object.
	virtual int Select(const ibGridCellCoords& coords) const = 0;
	virtual int Select(const wxPoint& pt) const = 0;
	virtual int Select(const wxSize& sz) const = 0;
	virtual int Select(const wxRect& r) const = 0;
	virtual int& Select(wxRect& r) const = 0;

	// Return or set left/top or right/bottom component of a block.
	virtual int SelectFirst(const ibGridBlockCoords& block) const = 0;
	virtual int SelectLast(const ibGridBlockCoords& block) const = 0;
	virtual void SetFirst(ibGridBlockCoords& block, int line) const = 0;
	virtual void SetLast(ibGridBlockCoords& block, int line) const = 0;

	// Returns width or height of the rectangle
	virtual int& SelectSize(wxRect& r) const = 0;

	// Make a wxSize such that Select() applied to it returns first component
	virtual wxSize MakeSize(int first, int second) const = 0;

	// Sets the row or column component of the given cell coordinates
	virtual void Set(ibGridCellCoords& coords, int line) const = 0;


	// Draws a line parallel to the row or column, i.e. horizontal or vertical:
	// pos is the horizontal or vertical position of the line and start and end
	// are the coordinates of the line extremities in the other direction
	virtual void
		DrawParallelLine(wxDC& dc, int start, int end, int pos) const = 0;

	// Draw a horizontal or vertical line across the given rectangle
	// (this is implemented in terms of above and uses Select() to extract
	// start and end from the given rectangle)
	void DrawParallelLineInRect(wxDC& dc, const wxRect& rect, int pos) const
	{
		const int posStart = Select(rect.GetPosition());
		DrawParallelLine(dc, posStart, posStart + Select(rect.GetSize()), pos);
	}


	// Return the index of the row or column at the given pixel coordinate.
	virtual int
		PosToLine(const ibGrid* grid, int pos, ibGridWindow* gridWindow, bool clip = false) const = 0;

	// Get the top/left position, in pixels, of the given row or column
	virtual int GetLineStartPos(const ibGrid* grid, int line) const = 0;

	// Get the bottom/right position, in pixels, of the given row or column
	virtual int GetLineEndPos(const ibGrid* grid, int line) const = 0;

	// Get the height/width of the given row/column
	virtual int GetLineSize(const ibGrid* grid, int line) const = 0;

	// Get ibGrid::m_rowBottoms/m_colRights array
	virtual const wxArrayInt& GetLineEnds(const ibGrid* grid) const = 0;

	// Get default height row height or column width
	virtual int GetDefaultLineSize(const ibGrid* grid) const = 0;

	// Return the minimal acceptable row height or column width
	virtual int GetMinimalAcceptableLineSize(const ibGrid* grid) const = 0;

	// Return the minimal row height or column width
	virtual int GetMinimalLineSize(const ibGrid* grid, int line) const = 0;

	// Set the row height or column width
	virtual void SetLineSize(ibGrid* grid, int line, int size) const = 0;

	// Set the row default height or column default width
	virtual void SetDefaultLineSize(ibGrid* grid, int size, bool resizeExisting) const = 0;

	// auto size the row height or column width from the label content
	virtual void HandleLineAutosize(ibGrid* grid, int line, const wxMouseEvent& event) const = 0;

	// Return the index of the line at the given position
	virtual int GetLineAt(const ibGrid* grid, int pos) const = 0;

	// Return the display position of the line with the given index.
	virtual int GetLinePos(const ibGrid* grid, int line) const = 0;

	// Return the index of the line just before the given one or wxNOT_FOUND.
	virtual int GetLineBefore(const ibGrid* grid, int line) const = 0;

	// Get the row or column label window
	virtual wxWindow* GetHeaderWindow(ibGrid* grid) const = 0;

	// Get the width or height of the row or column label window
	virtual int GetHeaderWindowSize(ibGrid* grid) const = 0;

	// Get the row or column frozen grid window
	virtual ibGridWindow* GetFrozenGrid(ibGrid* grid) const = 0;


	// return the value of m_canDragRow/ColMove
	virtual bool CanDragMove(ibGrid* grid) const = 0;

	// call DoEndMoveRow or DoEndMoveColumn
	virtual void DoEndMove(ibGrid* grid, int line) const = 0;

	// return whether the given row/column can be interactively resized
	virtual bool CanDragLineSize(ibGrid* grid, int line) const = 0;

	// call DoEndDragResizeRow or DoEndDragResizeCol
	virtual void DoEndLineResize(ibGrid* grid, const wxMouseEvent& event, ibGridWindow* gridWindow) const = 0;


	// extend current selection block up to given row or column
	virtual bool SelectionExtendCurrentBlock(ibGrid* grid, int line,
		const wxMouseEvent& event,
		wxEventType eventType = wxEVT_GRID_RANGE_SELECTED) const = 0;

	// select or de-select a row or column
	virtual void SelectArea(ibGrid* grid, int line, wxMouseEvent& event) const = 0;
	virtual void SelectLine(ibGrid* grid, int line, wxMouseEvent& event) const = 0;
	virtual void DeselectLine(ibGrid* grid, int line) const = 0;

	// check whether the row or columns first cell is in selected
	virtual bool IsLineInSelection(ibGrid* grid, int line) const = 0;

	//change area name if exist
	virtual void MakeAreaName(ibGrid* grid, int line, wxMouseEvent& event) const = 0;

	// sent a result with row or column and the other value -1
	virtual ibGrid::EventResult SendEvent(ibGrid* grid, wxEventType eventType,
		int line, const wxMouseEvent& event) const = 0;

	// call DrawRowLabel or DrawColumnLabel
	virtual void DrawLineLabel(ibGrid* grid, wxDC& dc, int line) const = 0;

	// helper for Process...MouseEvent for drawing markers on label windows
	void PrepareDCForLabels(ibGrid* grid, wxDC& dc) const;

	// make the specified line visible by doing a minimal amount of scrolling
	virtual void MakeLineVisible(ibGrid* grid, int line) const = 0;

	// set cursor into the first visible cell of the given row or column
	virtual void MakeLineCurrent(ibGrid* grid, int line) const = 0;

	// ------ outline / grouping dispatch (row vs col lives in subclasses) ------
	virtual int  GetOutlineMaxLevel(const ibGrid* grid) const = 0;
	virtual wxRect GetOutlineButtonRect(const ibGrid* grid, int idx) const = 0;
	virtual int  HitTestOutlineButton(const ibGrid* grid, const wxPoint& pt) const = 0;
	virtual bool ToggleOutlineGroup(ibGrid* grid, int idx) const = 0;
	virtual int  GetOutlineAreaCount(const ibGrid* grid) const = 0;
	virtual void DrawOutline(ibGrid* grid, wxDC& dc) const = 0;

	// This class is never used polymorphically but give it a virtual dtor
	// anyhow to suppress g++ complaints about it
	virtual ~ibGridOperations() {}
};

class ibGridRowOperations : public ibGridOperations
{
public:
	virtual ibGridOperations& Dual() const wxOVERRIDE;

	virtual int GetOrientation() const wxOVERRIDE
	{
		return wxVERTICAL;
	}

	virtual ibGrid::CursorMode GetCursorModeResize() const wxOVERRIDE
	{
		return ibGrid::WXGRID_CURSOR_RESIZE_ROW;
	}
	virtual ibGrid::CursorMode GetCursorModeSelect() const wxOVERRIDE
	{
		return ibGrid::WXGRID_CURSOR_SELECT_ROW;
	}
	virtual ibGrid::CursorMode GetCursorModeMove() const wxOVERRIDE
	{
		return ibGrid::WXGRID_CURSOR_MOVE_ROW;
	}

	virtual int GetTotalNumberOfLines(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetNumberRows();
	}

	virtual int GetNumberOfLines(const ibGrid* grid, ibGridWindow* gridWindow) const wxOVERRIDE;

	virtual int GetFirstLine(const ibGrid* grid, ibGridWindow* gridWindow) const wxOVERRIDE;

	virtual ibGrid::ibGridSelectionModes GetSelectionMode() const wxOVERRIDE
	{
		return ibGrid::ibGridSelectRows;
	}

	virtual ibGridCellCoords MakeCoords(int thisDir, int otherDir) const wxOVERRIDE
	{
		return ibGridCellCoords(thisDir, otherDir);
	}

	virtual int CalcScrolledPosition(ibGrid* grid, int pos) const wxOVERRIDE
	{
		return grid->CalcScrolledPosition(wxPoint(pos, 0)).x;
	}

	virtual int Select(const ibGridCellCoords& c) const wxOVERRIDE { return c.GetRow(); }
	virtual int Select(const wxPoint& pt) const wxOVERRIDE { return pt.x; }
	virtual int Select(const wxSize& sz) const wxOVERRIDE { return sz.x; }
	virtual int Select(const wxRect& r) const wxOVERRIDE { return r.x; }
	virtual int& Select(wxRect& r) const wxOVERRIDE { return r.x; }
	virtual int SelectFirst(const ibGridBlockCoords& block) const wxOVERRIDE
	{
		return block.GetTopRow();
	}
	virtual int SelectLast(const ibGridBlockCoords& block) const wxOVERRIDE
	{
		return block.GetBottomRow();
	}
	virtual void SetFirst(ibGridBlockCoords& block, int line) const wxOVERRIDE
	{
		block.SetTopRow(line);
	}
	virtual void SetLast(ibGridBlockCoords& block, int line) const wxOVERRIDE
	{
		block.SetBottomRow(line);
	}
	virtual int& SelectSize(wxRect& r) const wxOVERRIDE { return r.width; }
	virtual wxSize MakeSize(int first, int second) const wxOVERRIDE
	{
		return wxSize(first, second);
	}
	virtual void Set(ibGridCellCoords& coords, int line) const wxOVERRIDE
	{
		coords.SetRow(line);
	}

	virtual void DrawParallelLine(wxDC& dc, int start, int end, int pos) const wxOVERRIDE
	{
		dc.DrawLine(start, pos, end, pos);
	}

	virtual int PosToLine(const ibGrid* grid, int pos, ibGridWindow* gridWindow, bool clip = false) const wxOVERRIDE
	{
		return grid->YToRow(pos, clip, gridWindow);
	}
	virtual int GetLineStartPos(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetRowTop(line, grid->GetGridZoom());
	}
	virtual int GetLineEndPos(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetRowBottom(line, grid->GetGridZoom());
	}
	virtual int GetLineSize(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetRowHeight(line, grid->GetGridZoom());
	}
	virtual const wxArrayInt& GetLineEnds(const ibGrid* grid) const wxOVERRIDE
	{		
		return grid->GetRowBottoms(grid->GetGridZoom());
	}

	virtual int GetDefaultLineSize(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetDefaultRowSize(grid->GetGridZoom());
	}
	virtual int GetMinimalAcceptableLineSize(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetRowMinimalAcceptableHeight(grid->GetGridZoom());
	}
	virtual int GetMinimalLineSize(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetRowMinimalHeight(line, grid->GetGridZoom());
	}
	virtual void SetLineSize(ibGrid* grid, int line, int size) const wxOVERRIDE
	{
		grid->SetRowSize(line, size, grid->GetGridZoom());
	}
	virtual void SetDefaultLineSize(ibGrid* grid, int size, bool resizeExisting) const wxOVERRIDE
	{
		grid->SetDefaultRowSize(size, grid->GetGridZoom(), resizeExisting);
	}
	virtual void HandleLineAutosize(ibGrid* grid, int line, const wxMouseEvent& event) const wxOVERRIDE
	{
		grid->HandleRowAutosize(line, event);
	}

	virtual int GetLineAt(const ibGrid* grid, int pos) const wxOVERRIDE
	{
		return grid->GetRowAt(pos);
	}
	virtual int GetLinePos(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetRowPos(line);
	}

	virtual int GetLineBefore(const ibGrid* grid, int line) const wxOVERRIDE
	{
		int posBefore = grid->GetRowPos(line) - 1;
		return posBefore >= 0 ? grid->GetRowAt(posBefore) : wxNOT_FOUND;
	}

	virtual wxWindow* GetHeaderWindow(ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetGridRowLabelWindow();
	}
	virtual int GetHeaderWindowSize(ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetRowLabelSize(grid->GetGridZoom());
	}

	virtual ibGridWindow* GetFrozenGrid(ibGrid* grid) const wxOVERRIDE
	{
		return (ibGridWindow*)grid->GetFrozenRowGridWindow();
	}

	virtual bool CanDragMove(ibGrid* grid) const wxOVERRIDE
	{
		return grid->m_canDragRowMove;
	}
	virtual void DoEndMove(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->DoEndMoveRow(line);
	}

	virtual bool CanDragLineSize(ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->CanDragRowSize(line);
	}
	virtual void DoEndLineResize(ibGrid* grid, const wxMouseEvent& event,
		ibGridWindow* gridWindow) const wxOVERRIDE
	{
		grid->DoEndDragResizeRow(event, gridWindow);
	}


	virtual bool SelectionExtendCurrentBlock(ibGrid* grid, int line,
		const wxMouseEvent& event,
		wxEventType eventType = wxEVT_GRID_RANGE_SELECTED) const wxOVERRIDE
	{
		return grid->m_selection->ExtendCurrentBlock
		(
			ibGridCellCoords(grid->m_currentCellCoords.GetRow(), 0),
			ibGridCellCoords(line, grid->GetNumberCols() - 1),
			event,
			eventType
		);
	}

	virtual void SelectArea(ibGrid* grid, int line, wxMouseEvent& event) const
	{
		ibGridCellArea* rowArea = grid->GetRowArea(line);
		if (rowArea != NULL)
		{
			grid->m_selection->ClearSelection();
			grid->m_selection->SelectBlock(rowArea->m_start, 0, rowArea->m_end, grid->GetNumberCols() - 1, event);
		}
	};

	virtual void SelectLine(ibGrid* grid, int line, wxMouseEvent& event) const wxOVERRIDE
	{
		grid->m_selection->SelectRow(line, event);
	};
	virtual void DeselectLine(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->DeselectRow(line);
	}

	virtual bool IsLineInSelection(ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->m_selection->IsInSelection(line, 0);
	}

	virtual void MakeAreaName(ibGrid* grid, int line, wxMouseEvent& event) const wxOVERRIDE
	{
		grid->MakeRowAreaLabel(grid->GetRowArea(line));
	}

	virtual ibGrid::EventResult SendEvent(ibGrid* grid, wxEventType eventType,
		int line, const wxMouseEvent& event) const wxOVERRIDE
	{
		return grid->SendEvent(eventType, line, -1, event);
	}

	virtual void DrawLineLabel(ibGrid* grid, wxDC& dc, int line) const wxOVERRIDE
	{
		grid->DrawRowLabel(dc, line);
	}

	virtual void MakeLineVisible(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->MakeCellVisible(line, -1);
	}
	virtual void MakeLineCurrent(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->SetCurrentCell(line, grid->GetFirstFullyVisibleCol());
	}

	// ------ outline dispatch for rows ------
	virtual int  GetOutlineMaxLevel(const ibGrid* grid) const wxOVERRIDE
	{ return grid->GetMaxRowGroupLevel(); }
	virtual wxRect GetOutlineButtonRect(const ibGrid* grid, int idx) const wxOVERRIDE
	{ return grid->GetRowGroupButtonRect(idx); }
	virtual int  HitTestOutlineButton(const ibGrid* grid, const wxPoint& pt) const wxOVERRIDE
	{ return grid->HitTestRowOutlineButton(pt); }
	virtual bool ToggleOutlineGroup(ibGrid* grid, int idx) const wxOVERRIDE
	{ return grid->ToggleRowGroup(idx); }
	virtual int  GetOutlineAreaCount(const ibGrid* grid) const wxOVERRIDE
	{ return grid->GetRowGroupCount(); }
	virtual void DrawOutline(ibGrid* grid, wxDC& dc) const wxOVERRIDE
	{ grid->DrawRowOutline(dc); }
};

class ibGridColumnOperations : public ibGridOperations
{

public:
	virtual ibGridOperations& Dual() const wxOVERRIDE;

	virtual int GetOrientation() const wxOVERRIDE
	{
		return wxHORIZONTAL;
	}

	virtual ibGrid::CursorMode GetCursorModeResize() const wxOVERRIDE
	{
		return ibGrid::WXGRID_CURSOR_RESIZE_COL;
	}
	virtual ibGrid::CursorMode GetCursorModeSelect() const wxOVERRIDE
	{
		return ibGrid::WXGRID_CURSOR_SELECT_COL;
	}
	virtual ibGrid::CursorMode GetCursorModeMove() const wxOVERRIDE
	{
		return ibGrid::WXGRID_CURSOR_MOVE_COL;
	}

	virtual int GetTotalNumberOfLines(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetNumberCols();
	}

	virtual int GetNumberOfLines(const ibGrid* grid, ibGridWindow* gridWindow) const wxOVERRIDE;

	virtual int GetFirstLine(const ibGrid* grid, ibGridWindow* gridWindow) const wxOVERRIDE;

	virtual ibGrid::ibGridSelectionModes GetSelectionMode() const wxOVERRIDE
	{
		return ibGrid::ibGridSelectColumns;
	}

	virtual ibGridCellCoords MakeCoords(int thisDir, int otherDir) const wxOVERRIDE
	{
		return ibGridCellCoords(otherDir, thisDir);
	}

	virtual int CalcScrolledPosition(ibGrid* grid, int pos) const wxOVERRIDE
	{
		return grid->CalcScrolledPosition(wxPoint(0, pos)).y;
	}

	virtual int Select(const ibGridCellCoords& c) const wxOVERRIDE { return c.GetCol(); }
	virtual int Select(const wxPoint& pt) const wxOVERRIDE { return pt.y; }
	virtual int Select(const wxSize& sz) const wxOVERRIDE { return sz.y; }
	virtual int Select(const wxRect& r) const wxOVERRIDE { return r.y; }
	virtual int& Select(wxRect& r) const wxOVERRIDE { return r.y; }
	virtual int SelectFirst(const ibGridBlockCoords& block) const wxOVERRIDE
	{
		return block.GetLeftCol();
	}
	virtual int SelectLast(const ibGridBlockCoords& block) const wxOVERRIDE
	{
		return block.GetRightCol();
	}
	virtual void SetFirst(ibGridBlockCoords& block, int line) const wxOVERRIDE
	{
		block.SetLeftCol(line);
	}
	virtual void SetLast(ibGridBlockCoords& block, int line) const wxOVERRIDE
	{
		block.SetRightCol(line);
	}
	virtual int& SelectSize(wxRect& r) const wxOVERRIDE { return r.height; }
	virtual wxSize MakeSize(int first, int second) const wxOVERRIDE
	{
		return wxSize(second, first);
	}
	virtual void Set(ibGridCellCoords& coords, int line) const wxOVERRIDE
	{
		coords.SetCol(line);
	}

	virtual void DrawParallelLine(wxDC& dc, int start, int end, int pos) const wxOVERRIDE
	{
		dc.DrawLine(pos, start, pos, end);
	}

	virtual int PosToLine(const ibGrid* grid, int pos, ibGridWindow* gridWindow, bool clip = false) const wxOVERRIDE
	{
		return grid->XToCol(pos, clip, gridWindow);
	}
	virtual int GetLineStartPos(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetColLeft(line, grid->GetGridZoom());
	}
	virtual int GetLineEndPos(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetColRight(line, grid->GetGridZoom());
	}
	virtual int GetLineSize(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetColWidth(line, grid->GetGridZoom());
	}
	virtual const wxArrayInt& GetLineEnds(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetColRights(grid->GetGridZoom());
	}
	virtual int GetDefaultLineSize(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetDefaultColSize(grid->GetGridZoom());
	}
	virtual int GetMinimalAcceptableLineSize(const ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetColMinimalAcceptableWidth(grid->GetGridZoom());
	}
	virtual int GetMinimalLineSize(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetColMinimalWidth(line, grid->GetGridZoom());
	}
	virtual void SetLineSize(ibGrid* grid, int line, int size) const wxOVERRIDE
	{
		grid->SetColSize(line, size, grid->GetGridZoom());
	}
	virtual void SetDefaultLineSize(ibGrid* grid, int size, bool resizeExisting) const wxOVERRIDE
	{
		grid->SetDefaultColSize(size, grid->GetGridZoom(), resizeExisting);
	}
	virtual void HandleLineAutosize(ibGrid* grid, int line, const wxMouseEvent& event) const wxOVERRIDE
	{
		grid->HandleColumnAutosize(line, event);
	}

	virtual int GetLineAt(const ibGrid* grid, int pos) const wxOVERRIDE
	{
		return grid->GetColAt(pos);
	}
	virtual int GetLinePos(const ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->GetColPos(line);
	}

	virtual int GetLineBefore(const ibGrid* grid, int line) const wxOVERRIDE
	{
		int posBefore = grid->GetColPos(line) - 1;
		return posBefore >= 0 ? grid->GetColAt(posBefore) : wxNOT_FOUND;
	}

	virtual wxWindow* GetHeaderWindow(ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetGridColLabelWindow();
	}
	virtual int GetHeaderWindowSize(ibGrid* grid) const wxOVERRIDE
	{
		return grid->GetColLabelSize(grid->GetGridZoom());
	}

	virtual ibGridWindow* GetFrozenGrid(ibGrid* grid) const wxOVERRIDE
	{
		return (ibGridWindow*)grid->GetFrozenColGridWindow();
	}

	virtual bool CanDragMove(ibGrid* grid) const wxOVERRIDE
	{
		return grid->m_canDragColMove;
	}
	virtual void DoEndMove(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->DoEndMoveCol(line);
	}

	virtual bool CanDragLineSize(ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->CanDragColSize(line);
	}
	virtual void DoEndLineResize(ibGrid* grid, const wxMouseEvent& event,
		ibGridWindow* gridWindow) const wxOVERRIDE
	{
		grid->DoEndDragResizeCol(event, gridWindow);
	}

	virtual bool SelectionExtendCurrentBlock(ibGrid* grid, int line,
		const wxMouseEvent& event,
		wxEventType eventType = wxEVT_GRID_RANGE_SELECTED) const wxOVERRIDE
	{
		return grid->m_selection->ExtendCurrentBlock
		(
			ibGridCellCoords(0, grid->m_currentCellCoords.GetCol()),
			ibGridCellCoords(grid->GetNumberRows() - 1, line),
			event,
			eventType
		);
	}

	virtual void SelectArea(ibGrid* grid, int line, wxMouseEvent& event) const
	{
		ibGridCellArea* colArea = grid->GetColArea(line);
		if (colArea != NULL)
		{
			grid->m_selection->ClearSelection();
			grid->m_selection->SelectBlock(0, colArea->m_start, grid->GetNumberRows() - 1, colArea->m_end, event);
		}
	};

	virtual void SelectLine(ibGrid* grid, int line, wxMouseEvent& event) const wxOVERRIDE
	{
		grid->m_selection->SelectCol(line, event);
	};

	virtual void DeselectLine(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->DeselectCol(line);
	}

	virtual bool IsLineInSelection(ibGrid* grid, int line) const wxOVERRIDE
	{
		return grid->m_selection->IsInSelection(0, line);
	}

	virtual void MakeAreaName(ibGrid* grid, int line, wxMouseEvent& event) const wxOVERRIDE
	{
		grid->MakeColAreaLabel(grid->GetColArea(line));
	}

	virtual ibGrid::EventResult SendEvent(ibGrid* grid, wxEventType eventType,
		int line, const wxMouseEvent& event) const wxOVERRIDE
	{
		return grid->SendEvent(eventType, -1, line, event);
	}

	virtual void DrawLineLabel(ibGrid* grid, wxDC& dc, int line) const wxOVERRIDE
	{
		grid->DrawColLabel(dc, line);
	}

	virtual void MakeLineVisible(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->MakeCellVisible(-1, line);
	}
	virtual void MakeLineCurrent(ibGrid* grid, int line) const wxOVERRIDE
	{
		grid->SetCurrentCell(grid->GetFirstFullyVisibleRow(), line);
	}

	// ------ outline dispatch for columns ------
	virtual int  GetOutlineMaxLevel(const ibGrid* grid) const wxOVERRIDE
	{ return grid->GetMaxColGroupLevel(); }
	virtual wxRect GetOutlineButtonRect(const ibGrid* grid, int idx) const wxOVERRIDE
	{ return grid->GetColGroupButtonRect(idx); }
	virtual int  HitTestOutlineButton(const ibGrid* grid, const wxPoint& pt) const wxOVERRIDE
	{ return grid->HitTestColOutlineButton(pt); }
	virtual bool ToggleOutlineGroup(ibGrid* grid, int idx) const wxOVERRIDE
	{ return grid->ToggleColGroup(idx); }
	virtual int  GetOutlineAreaCount(const ibGrid* grid) const wxOVERRIDE
	{ return grid->GetColGroupCount(); }
	virtual void DrawOutline(ibGrid* grid, wxDC& dc) const wxOVERRIDE
	{ grid->DrawColOutline(dc); }
};

// This class abstracts the difference between operations going forward
// (down/right) and backward (up/left) and allows to use the same code for
// functions which differ only in the direction of grid traversal.
//
// Notice that all operations in this class work with display positions and not
// internal indices which can be different if the columns were reordered.
//
// Like ibGridOperations it's an ABC with two concrete subclasses below. Unlike
// it, this is a normal object and not just a function dispatch table and has a
// non-default ctor.
//
// Note: the explanation of this discrepancy is the existence of (very useful)
// Dual() method in ibGridOperations which forces us to make ibGridOperations a
// function dispatcher only.
class ibGridDirectionOperations
{
public:
	// The oper parameter to ctor selects whether we work with rows or columns
	ibGridDirectionOperations(ibGrid* grid, const ibGridOperations& oper)
		: m_grid(grid),
		m_oper(oper)
	{
	}

	// Check if the component of this point in our direction is at the
	// boundary, i.e. is the first/last row/column
	virtual bool IsAtBoundary(const ibGridCellCoords& coords) const = 0;

	// Check if the component of this point in our direction is
	// valid, i.e. not -1
	bool IsValid(const ibGridCellCoords& coords) const
	{
		return m_oper.Select(coords) != -1;
	}

	// Make the coordinates with the other component value of -1.
	ibGridCellCoords MakeWholeLineCoords(const ibGridCellCoords& coords) const
	{
		return m_oper.MakeCoords(m_oper.Select(coords), -1);
	}

	// Increment the component of this point in our direction
	//
	// Note that this can't be called if IsAtBoundary() is true, use
	// TryToAdvance() if this might be the case.
	virtual void Advance(ibGridCellCoords& coords) const = 0;

	// Try to advance in our direction, return true if succeeded or false
	// otherwise, i.e. if the coordinates are already at the grid boundary.
	bool TryToAdvance(ibGridCellCoords& coords) const
	{
		if (IsAtBoundary(coords))
			return false;

		Advance(coords);

		return true;
	}

	// Find the line at the given distance, in pixels, away from this one
	// (this uses clipping, i.e. anything after the last line is counted as the
	// last one and anything before the first one as 0)
	//
	// TODO: Implementation of this method currently doesn't support column
	//       reordering as it mixes up indices and positions. But this doesn't
	//       really matter as it's only called for rows (Page Up/Down only work
	//       vertically) and row reordering is not currently supported. We'd
	//       need to fix it if this ever changes however.
	virtual int MoveByPixelDistance(int line, int distance) const = 0;

	// This class is never used polymorphically but give it a virtual dtor
	// anyhow to suppress g++ complaints about it
	virtual ~ibGridDirectionOperations() {}

protected:
	// Get the position of the row or column from the given coordinates pair.
	//
	// This is just a shortcut to avoid repeating m_oper and m_grid multiple
	// times in the derived classes code.
	int GetLinePos(const ibGridCellCoords& coords) const
	{
		return m_oper.GetLinePos(m_grid, m_oper.Select(coords));
	}

	// Get the index of the row or column from the position.
	int GetLineAt(int pos) const
	{
		return m_oper.GetLineAt(m_grid, pos);
	}

	// Check if the given line is visible, i.e. has non 0 size.
	bool IsLineVisible(int line) const
	{
		return m_oper.GetLineSize(m_grid, line) != 0;
	}


	ibGrid* const m_grid;
	const ibGridOperations& m_oper;
};

class ibGridBackwardOperations : public ibGridDirectionOperations
{
public:
	ibGridBackwardOperations(ibGrid* grid, const ibGridOperations& oper)
		: ibGridDirectionOperations(grid, oper)
	{
	}

	virtual bool IsAtBoundary(const ibGridCellCoords& coords) const wxOVERRIDE
	{
		wxASSERT_MSG(m_oper.Select(coords) >= 0, "invalid row/column");

		int pos = GetLinePos(coords);
		while (pos)
		{
			// Check the previous line.
			int line = GetLineAt(--pos);
			if (IsLineVisible(line))
			{
				// There is another visible line before this one, hence it's
				// not at boundary.
				return false;
			}
		}

		// We reached the boundary without finding any visible lines.
		return true;
	}

	virtual void Advance(ibGridCellCoords& coords) const wxOVERRIDE
	{
		int pos = GetLinePos(coords);
		for (;; )
		{
			// This is not supposed to happen if IsAtBoundary() returned false.
			wxCHECK_RET(pos, "can't advance when already at boundary");

			int line = GetLineAt(--pos);
			if (IsLineVisible(line))
			{
				m_oper.Set(coords, line);
				break;
			}
		}
	}

	virtual int MoveByPixelDistance(int line, int distance) const wxOVERRIDE
	{
		int pos = m_oper.GetLineStartPos(m_grid, line);
		return m_oper.PosToLine(m_grid, pos - distance + 1, NULL, true);
	}
};

// Please refer to the comments above when reading this class code, it's
// absolutely symmetrical to ibGridBackwardOperations.
class ibGridForwardOperations : public ibGridDirectionOperations
{
public:
	ibGridForwardOperations(ibGrid* grid, const ibGridOperations& oper)
		: ibGridDirectionOperations(grid, oper),
		m_numLines(oper.GetTotalNumberOfLines(grid))
	{
	}

	virtual bool IsAtBoundary(const ibGridCellCoords& coords) const wxOVERRIDE
	{
		wxASSERT_MSG(m_oper.Select(coords) < m_numLines, "invalid row/column");

		int pos = GetLinePos(coords);
		while (pos < m_numLines - 1)
		{
			int line = GetLineAt(++pos);
			if (IsLineVisible(line))
				return false;
		}

		return true;
	}

	virtual void Advance(ibGridCellCoords& coords) const wxOVERRIDE
	{
		int pos = GetLinePos(coords);
		for (;; )
		{
			wxCHECK_RET(pos < m_numLines - 1,
				"can't advance when already at boundary");

			int line = GetLineAt(++pos);
			if (IsLineVisible(line))
			{
				m_oper.Set(coords, line);
				break;
			}
		}
	}

	virtual int MoveByPixelDistance(int line, int distance) const wxOVERRIDE
	{
		int pos = m_oper.GetLineStartPos(m_grid, line);
		return m_oper.PosToLine(m_grid, pos + distance, NULL, true);
	}

private:
	const int m_numLines;
};

// ----------------------------------------------------------------------------
// data structures used for the data type registry
// ----------------------------------------------------------------------------

struct ibGridDataTypeInfo
{
	ibGridDataTypeInfo(const wxString& typeName,
		ibGridCellRenderer* renderer,
		ibGridCellEditor* editor)
		: m_typeName(typeName), m_renderer(renderer), m_editor(editor)
	{
	}

	~ibGridDataTypeInfo()
	{
		wxSafeDecRef(m_renderer);
		wxSafeDecRef(m_editor);
	}

	wxString            m_typeName;
	ibGridCellRenderer* m_renderer;
	ibGridCellEditor* m_editor;

	wxDECLARE_NO_COPY_CLASS(ibGridDataTypeInfo);
};


WX_DEFINE_ARRAY_WITH_DECL_PTR(ibGridDataTypeInfo*, ibGridDataTypeInfoArray,
	class);


class ibGridTypeRegistry
{
public:
	ibGridTypeRegistry() {}
	~ibGridTypeRegistry();

	void RegisterDataType(const wxString& typeName,
		ibGridCellRenderer* renderer,
		ibGridCellEditor* editor);

	// find one of already registered data types
	int FindRegisteredDataType(const wxString& typeName);

	// try to FindRegisteredDataType(), if this fails and typeName is one of
	// standard typenames, register it and return its index
	int FindDataType(const wxString& typeName);

	// try to FindDataType(), if it fails see if it is not one of already
	// registered data types with some params in which case clone the
	// registered data type and set params for it
	int FindOrCloneDataType(const wxString& typeName);

	ibGridCellRenderer* GetRenderer(int index);
	ibGridCellEditor* GetEditor(int index);

private:
	ibGridDataTypeInfoArray m_typeinfo;
};

// Returns the rectangle for showing something of the given size in a cell with
// the given alignment.
//
// The function is used by ibGridCellBoolEditor and ibGridCellBoolRenderer to
// draw a check mark and position wxCheckBox respectively.
wxRect
wxGetContentRect(wxSize contentSize,
	const wxRect& cellRect,
	int hAlign,
	int vAlign);

namespace ibGridPrivate
{

#if wxUSE_DATETIME

	// This is used as TryGetValueAsDate() parameter.
	class DateParseParams
	{
	public:
		// Unfortunately we have to provide the default ctor (and also make the
		// members non-const) because we use these objects as out-parameters as
		// they are not fully declared in the public headers. The factory functions
		// below must be used to create a really usable object.
		DateParseParams() : fallbackParseDate(false) {}

		// Use these functions to really initialize the object.
		static DateParseParams WithFallback(const wxString& format)
		{
			return DateParseParams(format, true);
		}

		static DateParseParams WithoutFallback(const wxString& format)
		{
			return DateParseParams(format, false);
		}

		// The usual format, e.g. "%x" or "%Y-%m-%d".
		wxString format;

		// Whether fall back to ParseDate() is allowed.
		bool fallbackParseDate;

	private:
		DateParseParams(const wxString& format_, bool fallbackParseDate_)
			: format(format_),
			fallbackParseDate(fallbackParseDate_)
		{
		}
	};

	// Helper function trying to get a date from the given cell: if possible, get
	// the date value from the table directly, otherwise get the string value for
	// this cell and try to parse it using the specified date format and, if this
	// doesn't work and fallbackParseDate is true, try using ParseDate() as a
	// fallback. If this still fails, returns false.
	bool
		TryGetValueAsDate(wxDateTime& result,
			const DateParseParams& params,
			const ibGrid& grid,
			int row, int col);

#endif // wxUSE_DATETIME

} // namespace ibGridPrivate

#endif // wxUSE_GRID
#endif // _WX_GENERIC_GRID_PRIVATE_H_
