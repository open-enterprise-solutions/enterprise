///////////////////////////////////////////////////////////////////////////////
// Name:        wx/headerctrl.h
// Purpose:     ibHeaderGenericCtrlBase class: interface of ibHeaderGenericCtrl
// Author:      Vadim Zeitlin
// Created:     2008-12-01
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef __WX_HEADERCTRL_H__
#define __WX_HEADERCTRL_H__

#include <wx/control.h>

#if wxUSE_HEADERCTRL

#include <wx/dynarray.h>
#include <wx/vector.h>
#include <wx/overlay.h>

#include <wx/headercol.h>

#include <vector>

// notice that the classes in this header are defined in the core library even
// although currently they're only used by wxGrid which is in wxAdv because we
// plan to use it in wxListCtrl which is in core too in the future
class ibHeaderGenericCtrlEvent;

#include "frontend/frontend.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

enum
{
	// allow column drag and drop
	wxHD_ALLOW_REORDER = 0x0001,

	// allow hiding (and showing back) the columns using the menu shown by
	// right clicking the header
	wxHD_ALLOW_HIDE = 0x0002,

	// force putting column images on right
	wxHD_BITMAP_ON_RIGHT = 0x0004,

	// style used by default when creating the control
	wxHD_DEFAULT_STYLE = wxHD_ALLOW_REORDER
};

extern const char ibHeaderGenericCtrlNameStr[];

// "no column here" — the answer FindColumnAtPoint gives over a gap, and now also over
// a GROUP title (which labels columns rather than being one). Part of that contract,
// so it lives beside it rather than in one .cpp.
const unsigned COL_NONE = (unsigned)-1;

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrlBase defines the interface of a header control
// ----------------------------------------------------------------------------

class ibHeaderGenericCtrlBase : public wxControl
{
public:
	/*
		Derived classes must provide default ctor as well as a ctor and
		Create() function with the following signatures:

	ibHeaderGenericCtrl(wxWindow *parent,
				 wxWindowID winid = wxID_ANY,
				 const wxPoint& pos = wxDefaultPosition,
				 const wxSize& size = wxDefaultSize,
				 long style = wxHD_DEFAULT_STYLE,
				 const wxString& name = wxASCII_STR(ibHeaderGenericCtrlNameStr));

	bool Create(wxWindow *parent,
				wxWindowID winid = wxID_ANY,
				const wxPoint& pos = wxDefaultPosition,
				const wxSize& size = wxDefaultSize,
				long style = wxHD_DEFAULT_STYLE,
				const wxString& name = wxASCII_STR(ibHeaderGenericCtrlNameStr));
	 */

	 // column-related methods
	 // ----------------------

	 // set the number of columns in the control
	 //
	 // this also calls UpdateColumn() for all columns
	void SetColumnCount(unsigned int count);

	// return the number of columns in the control as set by SetColumnCount()
	unsigned int GetColumnCount() const { return DoGetCount(); }

	// return whether the control has any columns
	bool IsEmpty() const { return DoGetCount() == 0; }

	// update the column with the given index
	void UpdateColumn(unsigned int idx)
	{
		wxCHECK_RET(idx < GetColumnCount(), "invalid column index");

		DoUpdate(idx);
	}


	// columns order
	// -------------

	// set the columns order: the array defines the column index which appears
	// the given position, it must have GetColumnCount() elements and contain
	// all indices exactly once
	void SetColumnsOrder(const wxArrayInt& order);
	wxArrayInt GetColumnsOrder() const;

	// get the index of the column at the given display position
	unsigned int GetColumnAt(unsigned int pos) const;

	// get the position at which this column is currently displayed
	unsigned int GetColumnPos(unsigned int idx) const;

	// reset the columns order to the natural one
	void ResetColumnsOrder();

	// helper function used by the generic version of this control and also
	// wxGrid: reshuffles the array of column indices indexed by positions
	// (i.e. using the same convention as for SetColumnsOrder()) so that the
	// column with the given index is found at the specified position
	static void MoveColumnInOrderArray(wxArrayInt& order,
		unsigned int idx,
		unsigned int pos);


	// UI helpers
	// ----------

#if wxUSE_MENUS
	// show the popup menu containing all columns with check marks for the ones
	// which are currently shown and return true if something was done using it
	// (in this case UpdateColumnVisibility() will have been called) or false
	// if the menu was cancelled
	//
	// this is called from the default right click handler for the controls
	// with wxHD_ALLOW_HIDE style
	bool ShowColumnsMenu(const wxPoint& pt, const wxString& title = wxString());

	// append the entries for all our columns to the given menu, with the
	// currently visible columns being checked
	//
	// this is used by ShowColumnsMenu() but can also be used if you use your
	// own custom columns menu but nevertheless want to show all the columns in
	// it
	//
	// the ids of the items corresponding to the columns are consecutive and
	// start from idColumnsBase
	void AddColumnsItems(wxMenu& menu, int idColumnsBase = 0);
#endif // wxUSE_MENUS

	// show the columns customization dialog and return true if something was
	// changed using it (in which case UpdateColumnVisibility() and/or
	// UpdateColumnsOrder() will have been called)
	//
	// this is called by the control itself from ShowColumnsMenu() (which in
	// turn is only called by the control if wxHD_ALLOW_HIDE style was
	// specified) and if the control has wxHD_ALLOW_REORDER style as well
	bool ShowCustomizeDialog();

	// compute column title width
	int GetColumnTitleWidth(const wxHeaderColumn& col);

	// compute column title width for the column with the given index
	int GetColumnTitleWidth(unsigned int idx)
	{
		return GetColumnTitleWidth(GetColumn(idx));
	}

	// implementation only from now on
	// -------------------------------

	// the user doesn't need to TAB to this control
	virtual bool AcceptsFocusFromKeyboard() const wxOVERRIDE { return false; }

	// this method is only overridden in order to synchronize the control with
	// the main window when it is scrolled, the derived class must implement
	// DoScrollHorz()
	virtual void ScrollWindow(int dx, int dy, const wxRect* rect = NULL) wxOVERRIDE;

protected:
	// this method must be implemented by the derived classes to return the
	// information for the given column
	virtual const wxHeaderColumn& GetColumn(unsigned int idx) const = 0;

	// this method is called from the default EVT_HEADER_SEPARATOR_DCLICK
	// handler to update the fitting column width of the given column, it
	// should return true if the width was really updated
	virtual bool UpdateColumnWidthToFit(unsigned int WXUNUSED(idx),
		int WXUNUSED(widthTitle))
	{
		return false;
	}

	// this method is called from ShowColumnsMenu() and must be overridden to
	// update the internal column visibility (there is no need to call
	// UpdateColumn() from here, this will be done internally)
	virtual void UpdateColumnVisibility(unsigned int WXUNUSED(idx),
		bool WXUNUSED(show))
	{
		wxFAIL_MSG("must be overridden if called");
	}

	// this method is called from ShowCustomizeDialog() to reorder all columns
	// at once and should be implemented for controls using wxHD_ALLOW_REORDER
	// style (there is no need to call SetColumnsOrder() from here, this is
	// done by the control itself)
	virtual void UpdateColumnsOrder(const wxArrayInt& WXUNUSED(order))
	{
		wxFAIL_MSG("must be overridden if called");
	}

	// this method can be overridden in the derived classes to do something
	// (e.g. update/resize some internal data structures) before the number of
	// columns in the control changes
	virtual void OnColumnCountChanging(unsigned int WXUNUSED(count)) {}


	// helper function for the derived classes: update the array of column
	// indices after the number of columns changed
	void DoResizeColumnIndices(wxArrayInt& colIndices, unsigned int count);

protected:
	// this window doesn't look nice with the border so don't use it by default
	virtual wxBorder GetDefaultBorder() const wxOVERRIDE { return wxBORDER_NONE; }

private:
	// methods implementing our public API and defined in platform-specific
	// implementations
	virtual void DoSetCount(unsigned int count) = 0;
	virtual unsigned int DoGetCount() const = 0;
	virtual void DoUpdate(unsigned int idx) = 0;

	virtual void DoScrollHorz(int dx) = 0;

	virtual void DoSetColumnsOrder(const wxArrayInt& order) = 0;
	virtual wxArrayInt DoGetColumnsOrder() const = 0;


	// event handlers
	void OnSeparatorDClick(ibHeaderGenericCtrlEvent& event);
#if wxUSE_MENUS
	void OnRClick(ibHeaderGenericCtrlEvent& event);
#endif // wxUSE_MENUS

	wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// One drawn cell of the header.
//
// Flat headers have exactly one per column, spanning the whole height — that is
// the degenerate case, not a separate code path. A grouped header (see
// datavlayout.h) also carries GROUP titles, which belong to no column and stand
// one band above the columns they own; and a column's own cell then occupies a
// sub-rectangle instead of the full height. Everything the painter needs is in
// here, so the paint loop is the same loop either way.
// ----------------------------------------------------------------------------

struct ibHeaderButton {
	wxRect rect;                       // UNSCROLLED x; the painter adds the scroll offset
	unsigned int column = 0;           // column index (meaningless when isGroup)
	bool isGroup = false;              // a group title: no sort arrow, no hover, no resize
	wxString title;
	wxBitmapBundle bitmap;
	wxAlignment align = wxALIGN_LEFT;
};

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl
// ----------------------------------------------------------------------------

class ibHeaderGenericCtrl : public ibHeaderGenericCtrlBase
{
public:
	ibHeaderGenericCtrl()
	{
		Init();
	}

	ibHeaderGenericCtrl(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxHD_DEFAULT_STYLE,
		const wxString& name = wxASCII_STR(ibHeaderGenericCtrlNameStr))
	{
		Init();

		Create(parent, id, pos, size, style, name);
	}

	bool Create(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxHD_DEFAULT_STYLE,
		const wxString& name = wxASCII_STR(ibHeaderGenericCtrlNameStr));

	virtual ~ibHeaderGenericCtrl();

	void SetColumnHeight(int point) 
	{ 
		InvalidateBestSize();
		m_numHeight = point; 
	}

	int GetColumnHeight() const { return m_numHeight; }

protected:

	virtual wxSize DoGetBestSize() const wxOVERRIDE;

	// The cells to draw, in paint order. The base builds the FLAT header — one
	// full-height cell per column, left to right — and a derived header that knows
	// about column groups (ibDataViewHeaderWindow) replaces the geometry without
	// touching the paint loop that consumes it.
	virtual void BuildHeaderButtons(std::vector<ibHeaderButton>& cells, int height) const;

	// x just past the last cell — where the empty filler on the right starts.
	int GetHeaderButtonsEnd(const std::vector<ibHeaderButton>& cells) const;

	// Column geometry, in PHYSICAL coordinates (scroll offset included). Virtual
	// for the same reason BuildHeaderButtons is: a grouped header does not lay its
	// columns out by accumulating widths, and resizing / reordering / the drop
	// marker all read their x from here.
	virtual int GetColStart(unsigned int idx) const;
	virtual int GetColEnd(unsigned int idx) const;

	// The column at a header point. `yPhysical` matters only where columns are
	// STACKED (a vertical group): several of them then share one x range, and the
	// band under the cursor is what tells them apart. -1 = "no y known", answer by
	// x alone, which is what a flat header always does.
	virtual unsigned int FindColumnAtPoint(int xPhysical, int yPhysical, bool* onSeparator) const;

	unsigned int FindColumnAtPoint(int xPhysical, bool* onSeparator = NULL) const
	{
		return FindColumnAtPoint(xPhysical, -1, onSeparator);
	}

	// Physical x = logical x + this.
	int GetScrollOffset() const { return m_scrollOffset; }

	// ---- REORDERING: THE DROP IS A POINT ------------------------------------
	//
	// Protected rather than private because WHERE a dragged column lands is not always
	// "between two columns": with groups it can land INSIDE one, or come OUT of one, and
	// only a header that knows about groups can say which. ibDataViewHeaderWindow
	// overrides both the decision and the hint that shows it.

	// is a drag reordering operation currently in progress
	bool IsReordering() const;

	// the column being dragged; COL_NONE when none is
	unsigned int GetColumnBeingReordered() const { return m_colBeingReordered; }

	// end the drag: true when the column really moved (a mere click answers false)
	virtual bool EndReordering(int xPhysical, int yPhysical = -1);

	// the hint shown while the column is dragged around
	virtual void UpdateReorderingMarker(int xPhysical, int yPhysical = -1);

	// THE PHANTOM PLUS THE DROP HINT — one overlay, so both are drawn in one go. A derived
	// header says where the hint goes; an empty rect draws the phantom alone.
	void DrawReorderingMarker(int xPhysical, const wxRect& hint);

	// Move the column with given idx to given position (this doesn't generate any events
	// but does refresh the display).
	//
	// Virtual, and protected, because a header whose column order lives ELSEWHERE has
	// nothing to do here: the dataview's columns are ordered by their group TREE, which
	// the drag has already rewritten by the time this would run, and permuting this array
	// as well would leave two answers to "which column is at position N".
	virtual void DoMoveCol(unsigned int idx, unsigned int pos);

private:

	// implement base class pure virtuals
	virtual void DoSetCount(unsigned int count) wxOVERRIDE;
	virtual unsigned int DoGetCount() const wxOVERRIDE;
	virtual void DoUpdate(unsigned int idx) wxOVERRIDE;

	virtual void DoScrollHorz(int dx) wxOVERRIDE;

	virtual void DoSetColumnsOrder(const wxArrayInt& order) wxOVERRIDE;
	virtual wxArrayInt DoGetColumnsOrder() const wxOVERRIDE;

	// common part of all ctors
	void Init();

	// event handlers
	void OnPaint(wxPaintEvent& event);
	void OnMouse(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);

	// (GetColStart / GetColEnd / FindColumnAtPoint moved up to the protected
	//  section — a grouped header overrides them.)

	// refresh the given column [only]; idx must be valid
	void RefreshCol(unsigned int idx);

	// refresh the given column if idx is valid
	void RefreshColIfNotNone(unsigned int idx);

	// refresh all the controls starting from (and including) the given one
	void RefreshColsAfter(unsigned int idx);

	// (FindColumnAtPoint moved up to the protected section — a grouped header
	//  overrides it, since stacked columns share an x range and only the band
	//  under the cursor tells them apart.)

	// return the result of FindColumnAtPoint() if it is a valid column,
	// otherwise the index of the last (rightmost) displayed column
	// The POINT, not just its x: where columns are STACKED several of them share one x
	// range, and only the band under the cursor tells them apart. -1 = "no y known".
	unsigned int FindColumnClosestToPoint(int xPhysical, int yPhysical = -1) const;

	// return true if a drag resizing operation is currently in progress
	bool IsResizing() const;

	// return true if any drag operation is currently in progress
	bool IsDragging() const { return IsResizing() || IsReordering(); }

	// end any drag operation currently in progress (resizing or reordering)
	void EndDragging();

	// cancel the drag operation currently in progress and generate an event
	// about it
	void CancelDragging();

	// start (if m_colBeingResized is -1) or continue resizing the column
	//
	// this generates wxEVT_HEADER_BEGIN_RESIZE/RESIZING events and can
	// cancel the operation if the user handler decides so
	void StartOrContinueResizing(unsigned int col, int xPhysical);

	// end the resizing operation currently in progress and generate an event
	// about it with its cancelled flag set if xPhysical is -1
	void EndResizing(int xPhysical);

	// same functions as above but for column moving/reordering instead of
	// resizing
	void StartReordering(unsigned int col, int xPhysical);

	// constrain the given position to be larger than the start position of the
	// given column plus its minimal width and return the effective width
	int ConstrainByMinWidth(unsigned int col, int& xPhysical);

	// clear any overlaid markers
	void ClearMarkers();

	//number of column height point
	unsigned int m_numHeight;

	// number of columns in the control currently
	unsigned int m_numColumns;

	// index of the column under mouse or -1 if none
	unsigned int m_hover;

	// the column being resized or -1 if there is no resizing operation in
	// progress
	unsigned int m_colBeingResized;

	// the column being moved or -1 if there is no reordering operation in
	// progress
	unsigned int m_colBeingReordered;

	// the distance from the start of m_colBeingReordered and the mouse
	// position when the user started to drag it
	int m_dragOffset;

	// the horizontal scroll offset
	int m_scrollOffset;

	// the overlay display used during the dragging operations
	wxOverlay m_overlay;

	// the indices of the column appearing at the given position on the display
	// (its size is always m_numColumns)
	wxArrayInt m_colIndices;

	bool m_wasSeparatorDClick;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_NO_COPY_CLASS(ibHeaderGenericCtrl);
};

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrlSimple: concrete header control which can be used standalone
// ----------------------------------------------------------------------------

class ibHeaderGenericCtrlSimple : public ibHeaderGenericCtrl
{
public:
	// control creation
	// ----------------

	ibHeaderGenericCtrlSimple() { Init(); }
	ibHeaderGenericCtrlSimple(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxHD_DEFAULT_STYLE,
		const wxString& name = wxASCII_STR(ibHeaderGenericCtrlNameStr))
	{
		Init();

		Create(parent, winid, pos, size, style, name);
	}

	// managing the columns
	// --------------------

	// insert the column at the given position, using GetColumnCount() as
	// position appends it at the end
	void InsertColumn(const wxHeaderColumnSimple& col, unsigned int idx)
	{
		wxCHECK_RET(idx <= GetColumnCount(), "invalid column index");

		DoInsert(col, idx);
	}

	// append the column to the end of the control
	void AppendColumn(const wxHeaderColumnSimple& col)
	{
		DoInsert(col, GetColumnCount());
	}

	// delete the column at the given index
	void DeleteColumn(unsigned int idx)
	{
		wxCHECK_RET(idx < GetColumnCount(), "invalid column index");

		DoDelete(idx);
	}

	// delete all the existing columns
	void DeleteAllColumns();


	// modifying columns
	// -----------------

	// show or hide the column, notice that even when a column is hidden we
	// still account for it when using indices
	void ShowColumn(unsigned int idx, bool show = true)
	{
		wxCHECK_RET(idx < GetColumnCount(), "invalid column index");

		DoShowColumn(idx, show);
	}

	void HideColumn(unsigned int idx)
	{
		ShowColumn(idx, false);
	}

	// indicate that the column is used for sorting
	void ShowSortIndicator(unsigned int idx, bool ascending = true)
	{
		wxCHECK_RET(idx < GetColumnCount(), "invalid column index");

		DoShowSortIndicator(idx, ascending);
	}

	// remove the sort indicator completely
	void RemoveSortIndicator();

protected:
	// implement/override base class methods
	virtual const wxHeaderColumn& GetColumn(unsigned int idx) const wxOVERRIDE;
	virtual bool UpdateColumnWidthToFit(unsigned int idx, int widthTitle) wxOVERRIDE;

	// and define another one to be overridden in the derived classes: it
	// should return the best width for the given column contents or -1 if not
	// implemented, we use it to implement UpdateColumnWidthToFit()
	virtual int GetBestFittingWidth(unsigned int WXUNUSED(idx)) const
	{
		return -1;
	}

	void OnHeaderResizing(ibHeaderGenericCtrlEvent& evt);

private:
	// functions implementing our public API
	void DoInsert(const wxHeaderColumnSimple& col, unsigned int idx);
	void DoDelete(unsigned int idx);
	void DoShowColumn(unsigned int idx, bool show);
	void DoShowSortIndicator(unsigned int idx, bool ascending);

	// common part of all ctors
	void Init();

	// bring the column count in sync with the number of columns we store
	void UpdateColumnCount()
	{
		SetColumnCount(static_cast<int>(m_cols.size()));
	}


	// all our current columns
	typedef wxVector<wxHeaderColumnSimple> Columns;
	Columns m_cols;

	// the column currently used for sorting or -1 if none
	unsigned int m_sortKey;


	wxDECLARE_NO_COPY_CLASS(ibHeaderGenericCtrlSimple);
	wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibHeaderGenericCtrl events
// ----------------------------------------------------------------------------

class ibHeaderGenericCtrlEvent : public wxNotifyEvent
{
public:
	ibHeaderGenericCtrlEvent(wxEventType commandType = wxEVT_NULL, int winid = 0)
		: wxNotifyEvent(commandType, winid),
		m_col(-1),
		m_width(0),
		m_order(static_cast<unsigned int>(-1))
	{
	}

	ibHeaderGenericCtrlEvent(const ibHeaderGenericCtrlEvent& event)
		: wxNotifyEvent(event),
		m_col(event.m_col),
		m_width(event.m_width),
		m_order(event.m_order)
	{
	}

	// the column which this event pertains to: valid for all header events
	int GetColumn() const { return m_col; }
	void SetColumn(int col) { m_col = col; }

	// the width of the column: valid for column resizing/dragging events only
	int GetWidth() const { return m_width; }
	void SetWidth(int width) { m_width = width; }

	// the new position of the column: for end reorder events only
	unsigned int GetNewOrder() const { return m_order; }
	void SetNewOrder(unsigned int order) { m_order = order; }

	virtual wxEvent* Clone() const wxOVERRIDE { return new ibHeaderGenericCtrlEvent(*this); }

protected:
	// the column affected by the event
	int m_col;

	// the current width for the dragging events
	int m_width;

	// the new column position for end reorder event
	unsigned int m_order;

private:

	wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(ibHeaderGenericCtrlEvent);
};


wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_CLICK, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_RIGHT_CLICK, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_MIDDLE_CLICK, ibHeaderGenericCtrlEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_DCLICK, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_RIGHT_DCLICK, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_MIDDLE_DCLICK, ibHeaderGenericCtrlEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_SEPARATOR_DCLICK, ibHeaderGenericCtrlEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_BEGIN_RESIZE, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_RESIZING, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_END_RESIZE, ibHeaderGenericCtrlEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_BEGIN_REORDER, ibHeaderGenericCtrlEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_END_REORDER, ibHeaderGenericCtrlEvent);

wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_HEADER_DRAGGING_CANCELLED, ibHeaderGenericCtrlEvent);

typedef void (wxEvtHandler::* ibHeaderGenericCtrlEventFunction)(ibHeaderGenericCtrlEvent&);

#define ibHeaderGenericCtrlEventHandler(func) \
    wxEVENT_HANDLER_CAST(ibHeaderGenericCtrlEventFunction, func)

#define wx__DECLARE_HEADER_EVT(evt, id, fn) \
    wx__DECLARE_EVT1(wxEVT_HEADER_ ## evt, id, ibHeaderGenericCtrlEventHandler(fn))

#define EVT_HEADER_CLICK(id, fn) wx__DECLARE_HEADER_EVT(CLICK, id, fn)
#define EVT_HEADER_RIGHT_CLICK(id, fn) wx__DECLARE_HEADER_EVT(RIGHT_CLICK, id, fn)
#define EVT_HEADER_MIDDLE_CLICK(id, fn) wx__DECLARE_HEADER_EVT(MIDDLE_CLICK, id, fn)

#define EVT_HEADER_DCLICK(id, fn) wx__DECLARE_HEADER_EVT(DCLICK, id, fn)
#define EVT_HEADER_RIGHT_DCLICK(id, fn) wx__DECLARE_HEADER_EVT(RIGHT_DCLICK, id, fn)
#define EVT_HEADER_MIDDLE_DCLICK(id, fn) wx__DECLARE_HEADER_EVT(MIDDLE_DCLICK, id, fn)

#define EVT_HEADER_SEPARATOR_DCLICK(id, fn) wx__DECLARE_HEADER_EVT(SEPARATOR_DCLICK, id, fn)

#define EVT_HEADER_BEGIN_RESIZE(id, fn) wx__DECLARE_HEADER_EVT(BEGIN_RESIZE, id, fn)
#define EVT_HEADER_RESIZING(id, fn) wx__DECLARE_HEADER_EVT(RESIZING, id, fn)
#define EVT_HEADER_END_RESIZE(id, fn) wx__DECLARE_HEADER_EVT(END_RESIZE, id, fn)

#define EVT_HEADER_BEGIN_REORDER(id, fn) wx__DECLARE_HEADER_EVT(BEGIN_REORDER, id, fn)
#define EVT_HEADER_END_REORDER(id, fn) wx__DECLARE_HEADER_EVT(END_REORDER, id, fn)

#define EVT_HEADER_DRAGGING_CANCELLED(id, fn) wx__DECLARE_HEADER_EVT(DRAGGING_CANCELLED, id, fn)

// old wxEVT_COMMAND_* constants
#define wxEVT_COMMAND_HEADER_CLICK                wxEVT_HEADER_CLICK
#define wxEVT_COMMAND_HEADER_RIGHT_CLICK          wxEVT_HEADER_RIGHT_CLICK
#define wxEVT_COMMAND_HEADER_MIDDLE_CLICK         wxEVT_HEADER_MIDDLE_CLICK
#define wxEVT_COMMAND_HEADER_DCLICK               wxEVT_HEADER_DCLICK
#define wxEVT_COMMAND_HEADER_RIGHT_DCLICK         wxEVT_HEADER_RIGHT_DCLICK
#define wxEVT_COMMAND_HEADER_MIDDLE_DCLICK        wxEVT_HEADER_MIDDLE_DCLICK
#define wxEVT_COMMAND_HEADER_SEPARATOR_DCLICK     wxEVT_HEADER_SEPARATOR_DCLICK
#define wxEVT_COMMAND_HEADER_BEGIN_RESIZE         wxEVT_HEADER_BEGIN_RESIZE
#define wxEVT_COMMAND_HEADER_RESIZING             wxEVT_HEADER_RESIZING
#define wxEVT_COMMAND_HEADER_END_RESIZE           wxEVT_HEADER_END_RESIZE
#define wxEVT_COMMAND_HEADER_BEGIN_REORDER        wxEVT_HEADER_BEGIN_REORDER
#define wxEVT_COMMAND_HEADER_END_REORDER          wxEVT_HEADER_END_REORDER
#define wxEVT_COMMAND_HEADER_DRAGGING_CANCELLED   wxEVT_HEADER_DRAGGING_CANCELLED

#endif // wxUSE_HEADERCTRL

#endif // _WX_HEADERCTRL_H_
