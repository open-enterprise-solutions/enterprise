///////////////////////////////////////////////////////////////////////////
// Name:        src/generic/grid.cpp
// Purpose:     ibGrid and related classes
// Author:      Michael Bedward (based on code by Julian Smart, Robin Dunn)
// Modified by: Robin Dunn, Vadim Zeitlin, Santiago Palacios
// Created:     1/08/1999
// Copyright:   (c) Michael Bedward (mbedward@ozemail.com.au)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

/*
	TODO:

	- Make Begin/EndBatch() the same as the generic Freeze/Thaw()
	- Review the column reordering code, it's a mess.
 */

 // For compilers that support precompilation, includes "wx/wx.h>.
#include <wx/wxprec.h>

#if wxUSE_GRID

#include "gridext.h"

#ifndef WX_PRECOMP
#include <wx/utils.h>
#include <wx/dcclient.h>
#include <wx/settings.h>
#include <wx/log.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/valtext.h>
#include <wx/intl.h>
#include <wx/math.h>
#include <wx/listbox.h>
#endif

#include <wx/dcbuffer.h>
#include <wx/textfile.h>
#include <wx/spinctrl.h>
#include <wx/tokenzr.h>
#include <wx/renderer.h>
#include <wx/headerctrl.h>
#include <wx/hashset.h>

#if wxUSE_CLIPBOARD
#include <wx/clipbrd.h>
#endif // wxUSE_CLIPBOARD

#include "gridextsel.h"
#include "gridextctrl.h"
#include "gridexteditors.h"
#include "gridextprivate.h"

#include <wx/arrimpl.cpp>

// Required for wxIs... functions
#include <ctype.h>

const char ibGridNameStr[] = "gridext";

WX_DECLARE_HASH_SET_WITH_DECL_PTR(int, wxIntegerHash, wxIntegerEqual,
	ibGridFixedIndicesSet, class);

// ----------------------------------------------------------------------------
// globals
// ----------------------------------------------------------------------------

namespace
{

	//#define DEBUG_ATTR_CACHE
#ifdef DEBUG_ATTR_CACHE
	static size_t gs_nAttrCacheHits = 0;
	static size_t gs_nAttrCacheMisses = 0;
#endif

	// this struct simply combines together the default header renderers
	//
	// as the renderers ctors are trivial, there is no problem with making them
	// globals
	struct DefaultHeaderRenderers
	{
		ibGridColumnHeaderRendererDefault colRenderer;
		ibGridRowHeaderRendererDefault rowRenderer;
		ibGridCornerHeaderRendererDefault cornerRenderer;
	} gs_defaultHeaderRenderers;

} // anonymous namespace

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

const wxArrayString wxEmptyArrayGridString;

ibGridCellCoords ibGridNoCellCoords(-1, -1);
ibGridBlockCoords ibGridNoBlockCoords(-1, -1, -1, -1);
wxRect ibGridNoCellRect(-1, -1, -1, -1);

namespace
{

	// scroll line size
	const size_t GRID_SCROLL_LINE_X = 15;
	const size_t GRID_SCROLL_LINE_Y = GRID_SCROLL_LINE_X;

	// the size of hash tables used a bit everywhere (the max number of elements
	// in these hash tables is the number of rows/columns)
	const int GRID_HASH_SIZE = 100;

	// the minimal distance in pixels the mouse needs to move to start a drag
	// operation
	const int DRAG_SENSITIVITY = 3;

	// the space between the cell edge and the checkbox mark
	const int GRID_CELL_CHECKBOX_MARGIN = 2;

	// the margin between a cell vertical line and a cell text
	const int GRID_TEXT_MARGIN = 1;

} // anonymous namespace


WX_DEFINE_OBJARRAY(ibGridCellCoordsArray)
WX_DEFINE_OBJARRAY(ibGridCellCacheArray)
WX_DEFINE_OBJARRAY(ibGridCellAreaArray)

// ----------------------------------------------------------------------------
// events
// ----------------------------------------------------------------------------

wxDEFINE_EVENT(wxEVT_GRID_CELL_LEFT_CLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_CELL_RIGHT_CLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_CELL_LEFT_DCLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_CELL_RIGHT_DCLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_CELL_BEGIN_DRAG, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_LABEL_LEFT_CLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_LABEL_RIGHT_CLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_LABEL_LEFT_DCLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_LABEL_RIGHT_DCLICK, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_SIZE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_MODIFIED, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_AUTO_SIZE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_SIZE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_MODIFIED, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_AUTO_SIZE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_MOVE, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_MOVE, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_SORT, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_RANGE_SELECTING, ibGridRangeSelectEvent);
wxDEFINE_EVENT(wxEVT_GRID_RANGE_SELECTED, ibGridRangeSelectEvent);
wxDEFINE_EVENT(wxEVT_GRID_CELL_CHANGING, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_CELL_CHANGED, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_TABLE_MODIFIED, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_TABLE_ATTR_MODIFIED, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_SELECT_CELL, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_EDITOR_SHOWN, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_EDITOR_HIDDEN, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_EDITOR_CREATED, ibGridEditorCreatedEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_BRAKE_ADD, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_BRAKE_SET, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_BRAKE_DELETE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_BRAKE_ADD, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_BRAKE_SET, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_BRAKE_DELETE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_AREA_CREATE, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_AREA_DELETE, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_AREA_SIZE, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_AREA_NAME, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_AREA_CREATE, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_AREA_DELETE, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_AREA_SIZE, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_AREA_NAME, ibGridAreaEvent);
wxDEFINE_EVENT(wxEVT_GRID_ROW_FREEZE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_COL_FREEZE, ibGridSizeEvent);
wxDEFINE_EVENT(wxEVT_GRID_ZOOM, ibGridEvent);
wxDEFINE_EVENT(wxEVT_GRID_TABBING, ibGridEvent);

// ============================================================================
// implementation
// ============================================================================

namespace
{

	// Helper function for consistent cell span determination based on cell size.
	ibGrid::CellSpan GetCellSpan(int numRows, int numCols)
	{
		if (numRows == 1 && numCols == 1)
			return ibGrid::CellSpan_None; // just a normal cell

		if (numRows < 0 || numCols < 0)
			return ibGrid::CellSpan_Inside; // covered by a multi-span cell

		// this cell spans multiple cells to its right/bottom
		return ibGrid::CellSpan_Main;
	}

} // anonymous namespace

wxIMPLEMENT_ABSTRACT_CLASS(ibGridCellEditorEvtHandler, wxEvtHandler);

wxBEGIN_EVENT_TABLE(ibGridCellEditorEvtHandler, wxEvtHandler)
EVT_KILL_FOCUS(ibGridCellEditorEvtHandler::OnKillFocus)
EVT_KEY_DOWN(ibGridCellEditorEvtHandler::OnKeyDown)
EVT_CHAR(ibGridCellEditorEvtHandler::OnChar)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ibGridHeaderCtrl, wxHeaderCtrl)
EVT_HEADER_CLICK(wxID_ANY, ibGridHeaderCtrl::OnClick)
EVT_HEADER_DCLICK(wxID_ANY, ibGridHeaderCtrl::OnDoubleClick)
EVT_HEADER_RIGHT_CLICK(wxID_ANY, ibGridHeaderCtrl::OnRightClick)

EVT_HEADER_BEGIN_RESIZE(wxID_ANY, ibGridHeaderCtrl::OnBeginResize)
EVT_HEADER_RESIZING(wxID_ANY, ibGridHeaderCtrl::OnResizing)
EVT_HEADER_END_RESIZE(wxID_ANY, ibGridHeaderCtrl::OnEndResize)

EVT_HEADER_BEGIN_REORDER(wxID_ANY, ibGridHeaderCtrl::OnBeginReorder)
EVT_HEADER_END_REORDER(wxID_ANY, ibGridHeaderCtrl::OnEndReorder)
wxEND_EVENT_TABLE()

ibGridOperations& ibGridRowOperations::Dual() const
{
	static ibGridColumnOperations s_colOper;

	return s_colOper;
}

ibGridOperations& ibGridColumnOperations::Dual() const
{
	static ibGridRowOperations s_rowOper;

	return s_rowOper;
}

int ibGridRowOperations::GetNumberOfLines(const ibGrid* grid, ibGridWindow* gridWindow) const
{
	if (!gridWindow)
		return grid->GetNumberRows();

	if (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow)
		return grid->GetNumberFrozenRows();

	return grid->GetNumberRows() - grid->GetNumberFrozenRows();
}

int ibGridColumnOperations::GetNumberOfLines(const ibGrid* grid, ibGridWindow* gridWindow) const
{
	if (!gridWindow)
		return grid->GetNumberCols();

	if (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol)
		return grid->GetNumberFrozenCols();

	return grid->GetNumberCols() - grid->GetNumberFrozenCols();
}

int ibGridRowOperations::GetFirstLine(const ibGrid* grid, ibGridWindow* gridWindow) const
{
	if (!gridWindow || gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow)
		return 0;

	return grid->GetNumberFrozenRows();
}

int ibGridColumnOperations::GetFirstLine(const ibGrid* grid, ibGridWindow* gridWindow) const
{
	if (!gridWindow || gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol)
		return 0;

	return grid->GetNumberFrozenCols();
}

void ibGridOperations::PrepareDCForLabels(ibGrid* grid, wxDC& dc) const
{
	// The grid can be scrolled in both directions and so grid->DoPrepareDC
	// could offset the device context in both directions.
	// For row and col labels, we want only one direction and so
	// we reset the other direction to the original, unscrolled value.
	wxPoint dcOriginBefore = dc.GetDeviceOrigin();
	grid->DoPrepareDC(dc);
	wxPoint dcOrigin = dc.GetDeviceOrigin();

	if (GetOrientation() == wxVERTICAL)
		dcOrigin.x = dcOriginBefore.x;
	else if (GetOrientation() == wxHORIZONTAL)
		dcOrigin.y = dcOriginBefore.y;

	dc.SetDeviceOrigin(dcOrigin.x, dcOrigin.y);
}

// ----------------------------------------------------------------------------
// ibGridCellWorker is an (almost) empty common base class for
// ibGridCellRenderer and ibGridCellEditor managing ref counting
// ----------------------------------------------------------------------------

ibGridCellWorker::ibGridCellWorker(const ibGridCellWorker& other)
	: wxRefCounter()
{
	CopyClientDataContainer(other);
}

void ibGridCellWorker::SetParameters(const wxString& WXUNUSED(params))
{
	// nothing to do
}

ibGridCellWorker::~ibGridCellWorker()
{
}

// ----------------------------------------------------------------------------
// ibGridHeaderLabelsRenderer and related classes
// ----------------------------------------------------------------------------

void ibGridHeaderLabelsRenderer::DrawLabel(const ibGrid& grid,
	wxDC& dc,
	const wxString& value,
	const wxRect& rect,
	int horizAlign,
	int vertAlign,
	int textOrientation) const
{
	dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
	dc.SetFont(grid.GetLabelFont(grid.GetGridZoom()));

	// Draw text in a different colour and with a shadow when the control
	// is disabled.
	//
	// Note that the colours used here are consistent with wxGenericStaticText
	// rather than our own ibGridCellStringRenderer::SetTextColoursAndFont()
	// because this results in a better disabled appearance for the default
	// bold font used for the labels.
	wxColour colText;
	if (!grid.IsEnabled())
	{
		colText = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNHIGHLIGHT);
		dc.SetTextForeground(colText);

		wxRect rectShadow = rect;
		rectShadow.Offset(1, 1);
		grid.DrawTextRectangle(dc, value, rectShadow,
			horizAlign, vertAlign, textOrientation);

		colText = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
	}
	else
	{
		colText = grid.GetLabelTextColour();
	}

	dc.SetTextForeground(colText);

	grid.DrawTextRectangle(dc, value, rect, horizAlign, vertAlign, textOrientation);
}


void ibGridRowHeaderRendererDefault::DrawBorder(const ibGrid& grid,
	wxDC& dc,
	wxRect& rect) const
{
	static wxPen shadowPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));

	dc.SetPen(shadowPen);
	dc.DrawLine(rect.GetRight(), rect.GetTop(),
		rect.GetRight(), rect.GetBottom());

	dc.DrawLine(rect.GetLeft(), rect.GetBottom(),
		rect.GetRight() + 1, rect.GetBottom());

	// Only draw the external borders when the containing control doesn't have
	// any border, otherwise they would compound with the outer border which
	// looks bad.
	int ofs = 0;
	if (grid.GetBorder() == wxBORDER_NONE)
	{
		dc.DrawLine(rect.GetLeft(), rect.GetTop(),
			rect.GetLeft(), rect.GetBottom());

		ofs = 1;
	}

	static wxPen lightPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

	dc.SetPen(lightPen);
	dc.DrawLine(rect.GetLeft() + ofs, rect.GetTop(),
		rect.GetLeft() + ofs, rect.GetBottom());
	dc.DrawLine(rect.GetLeft() + ofs, rect.GetTop(),
		rect.GetRight(), rect.GetTop());

	rect.Deflate(1 + ofs);
}

void ibGridColumnHeaderRendererDefault::DrawBorder(const ibGrid& grid,
	wxDC& dc,
	wxRect& rect) const
{
	static wxPen shadowPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));

	dc.SetPen(shadowPen);
	dc.DrawLine(rect.GetRight(), rect.GetTop(),
		rect.GetRight(), rect.GetBottom());
	dc.DrawLine(rect.GetLeft(), rect.GetBottom(),
		rect.GetRight() + 1, rect.GetBottom());

	// As above, don't draw the outer border if the control has its own one.
	int ofs = 0;
	if (grid.GetBorder() == wxBORDER_NONE)
	{
		dc.DrawLine(rect.GetLeft(), rect.GetTop(),
			rect.GetRight(), rect.GetTop());

		ofs = 1;
	}

	static wxPen lightPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

	dc.SetPen(lightPen);
	dc.DrawLine(rect.GetLeft(), rect.GetTop() + ofs,
		rect.GetLeft(), rect.GetBottom());
	dc.DrawLine(rect.GetLeft(), rect.GetTop() + ofs,
		rect.GetRight(), rect.GetTop() + ofs);

	rect.Deflate(1 + ofs);
}

void ibGridCornerHeaderRendererDefault::DrawBorder(const ibGrid& grid,
	wxDC& dc,
	wxRect& rect) const
{
	static wxPen shadowPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));

	dc.SetPen(shadowPen);
	dc.DrawLine(rect.GetRight() - 1, rect.GetBottom() - 1,
		rect.GetRight() - 1, rect.GetTop());
	dc.DrawLine(rect.GetRight() - 1, rect.GetBottom() - 1,
		rect.GetLeft(), rect.GetBottom() - 1);

	// As above, don't draw either of outer border if there is already a border
	// around the entire window.
	int ofs = 0;
	if (grid.GetBorder() == wxBORDER_NONE)
	{
		dc.DrawLine(rect.GetLeft(), rect.GetTop(),
			rect.GetRight(), rect.GetTop());
		dc.DrawLine(rect.GetLeft(), rect.GetTop(),
			rect.GetLeft(), rect.GetBottom());

		ofs = 1;
	}

	static wxPen lightPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));

	dc.SetPen(lightPen);
	dc.DrawLine(rect.GetLeft() + 1, rect.GetTop() + ofs,
		rect.GetRight() - 1, rect.GetTop() + ofs);
	dc.DrawLine(rect.GetLeft() + ofs, rect.GetTop() + ofs,
		rect.GetLeft() + ofs, rect.GetBottom() - 1);

	rect.Deflate(1 + ofs);
}

// ----------------------------------------------------------------------------
// ibGridCellAttr
// ----------------------------------------------------------------------------

void ibGridCellAttr::Init(ibGridCellAttr* attrDefault)
{
	m_isReadOnly = Unset;

	m_renderer = NULL;
	m_editor = NULL;

	m_attrkind = ibGridCellAttr::Cell;

	m_orientText = -1;
	m_sizeRows = m_sizeCols = 1;

	SetDefAttr(attrDefault);
}

ibGridCellAttr* ibGridCellAttr::Clone() const
{
	ibGridCellAttr* attr = new ibGridCellAttr(m_defGridAttr);

	if (HasTextColour())
		attr->SetTextColour(GetTextColour());
	if (HasTextOrient())
		attr->SetTextOrient(GetTextOrient());
	if (HasBackgroundColour())
		attr->SetBackgroundColour(GetBackgroundColour());
	if (HasFont())
		attr->SetFont(GetFont());
	if (HasAlignment())
		attr->SetAlignment(m_hAlign, m_vAlign);

	if (HasBorderLeft())
		attr->SetBorderLeft(m_borderLeft.m_style, m_borderLeft.m_colour, m_borderLeft.m_width);
	if (HasBorderRight())
		attr->SetBorderRight(m_borderRight.m_style, m_borderRight.m_colour, m_borderRight.m_width);
	if (HasBorderTop())
		attr->SetBorderTop(m_borderTop.m_style, m_borderTop.m_colour, m_borderTop.m_width);
	if (HasBorderBottom())
		attr->SetBorderBottom(m_borderTop.m_style, m_borderTop.m_colour, m_borderBottom.m_width);

	attr->SetSize(m_sizeRows, m_sizeCols);

	if (m_renderer)
	{
		attr->SetRenderer(m_renderer);
		m_renderer->IncRef();
	}
	if (m_editor)
	{
		attr->SetEditor(m_editor);
		m_editor->IncRef();
	}

	attr->CopyClientDataContainer(*this);

	if (IsReadOnly())
		attr->SetReadOnly();

	attr->m_fitMode = m_fitMode;
	attr->SetKind(m_attrkind);

	return attr;
}

void ibGridCellAttr::MergeWith(ibGridCellAttr* mergefrom)
{
	if (!HasTextColour() && mergefrom->HasTextColour())
		SetTextColour(mergefrom->GetTextColour());
	if (!HasTextOrient() && mergefrom->HasTextOrient())
		SetTextOrient(mergefrom->GetTextOrient());
	if (!HasBackgroundColour() && mergefrom->HasBackgroundColour())
		SetBackgroundColour(mergefrom->GetBackgroundColour());
	if (!HasFont() && mergefrom->HasFont())
		SetFont(mergefrom->GetFont());
	if (!HasAlignment() && mergefrom->HasAlignment())
	{
		int hAlign, vAlign;
		mergefrom->GetAlignment(&hAlign, &vAlign);
		SetAlignment(hAlign, vAlign);
	}

	if (!HasBorderLeft() && mergefrom->HasBorderLeft())
		SetBorderLeft(mergefrom->m_borderLeft.m_style, mergefrom->m_borderLeft.m_colour, mergefrom->m_borderLeft.m_width);
	if (!HasBorderRight() && mergefrom->HasBorderRight())
		SetBorderRight(mergefrom->m_borderRight.m_style, mergefrom->m_borderRight.m_colour, mergefrom->m_borderRight.m_width);
	if (!HasBorderTop() && mergefrom->HasBorderTop())
		SetBorderTop(mergefrom->m_borderTop.m_style, mergefrom->m_borderTop.m_colour, mergefrom->m_borderTop.m_width);
	if (!HasBorderBottom() && mergefrom->HasBorderBottom())
		SetBorderBottom(mergefrom->m_borderTop.m_style, mergefrom->m_borderTop.m_colour, mergefrom->m_borderBottom.m_width);

	if (!HasSize() && mergefrom->HasSize())
		mergefrom->GetSize(&m_sizeRows, &m_sizeCols);

	// Directly access member functions as GetRender/Editor don't just return
	// m_renderer/m_editor
	//
	// Maybe add support for merge of Render and Editor?
	if (!HasRenderer() && mergefrom->HasRenderer())
	{
		m_renderer = mergefrom->m_renderer;
		m_renderer->IncRef();
	}
	if (!HasEditor() && mergefrom->HasEditor())
	{
		m_editor = mergefrom->m_editor;
		m_editor->IncRef();
	}
	if (!HasClientDataContainer() && mergefrom->HasClientDataContainer())
	{
		CopyClientDataContainer(*mergefrom);
	}
	if (!HasReadWriteMode() && mergefrom->HasReadWriteMode())
		SetReadOnly(mergefrom->IsReadOnly());

	if (!HasOverflowMode() && mergefrom->HasOverflowMode())
		SetOverflow(mergefrom->GetOverflow());

	SetDefAttr(mergefrom->m_defGridAttr);
}

void ibGridCellAttr::SetSize(int num_rows, int num_cols)
{
	// The size of a cell is normally 1,1

	// If this cell is larger (2,2) then this is the top left cell
	// the other cells that will be covered (lower right cells) must be
	// set to negative or zero values such that
	// row + num_rows of the covered cell points to the larger cell (this cell)
	// same goes for the col + num_cols.

	// Size of 0,0 is NOT valid, neither is <=0 and any positive value

	wxASSERT_MSG((!((num_rows > 0) && (num_cols <= 0)) ||
		!((num_rows <= 0) && (num_cols > 0)) ||
		!((num_rows == 0) && (num_cols == 0))),
		wxT("ibGridCellAttr::SetSize only takes two positive values or negative/zero values"));

	m_sizeRows = num_rows;
	m_sizeCols = num_cols;
}

void ibGridCellAttr::SetBorderLeft(wxPenStyle style, const wxColour& colour, int width)
{
	m_borderLeft.m_style = style;
	m_borderLeft.m_colour = colour;
	m_borderLeft.m_width = width;
}

void ibGridCellAttr::SetBorderRight(wxPenStyle style, const wxColour& colour, int width)
{
	m_borderRight.m_style = style;
	m_borderRight.m_colour = colour;
	m_borderRight.m_width = width;
}

void ibGridCellAttr::SetBorderTop(wxPenStyle style, const wxColour& colour, int width)
{
	m_borderTop.m_style = style;
	m_borderTop.m_colour = colour;
	m_borderTop.m_width = width;
}

void ibGridCellAttr::SetBorderBottom(wxPenStyle style, const wxColour& colour, int width)
{
	m_borderBottom.m_style = style;
	m_borderBottom.m_colour = colour;
	m_borderBottom.m_width = width;
}

const wxColour& ibGridCellAttr::GetTextColour() const
{
	if (HasTextColour())
	{
		return m_colText;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetTextColour();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return wxNullColour;
	}
}

const int ibGridCellAttr::GetTextOrient() const
{
	if (HasTextOrient())
	{
		return m_orientText;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetTextOrient();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return -1;
	}
}

const wxColour& ibGridCellAttr::GetBackgroundColour() const
{
	if (HasBackgroundColour())
	{
		return m_colBack;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetBackgroundColour();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return wxNullColour;
	}
}

const wxFont& ibGridCellAttr::GetFont(float scale) const
{
	if (HasFont())
	{
		if (scale == 1.0f)
			return m_font;

		const double fractionalPointSize = m_scaledFont.GetFractionalPointSize();
		const float fractionalScale = fractionalPointSize / m_font.GetPointSize();

		if (fractionalScale == scale)
			return m_scaledFont;

		m_scaledFont = m_font.Scaled(scale);
		return m_scaledFont;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetFont(scale);
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return wxNullFont;
	}
}

void ibGridCellAttr::GetAlignment(int* hAlign, int* vAlign) const
{
	if (HasAlignment())
	{
		if (hAlign)
			*hAlign = m_hAlign;
		if (vAlign)
			*vAlign = m_vAlign;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		m_defGridAttr->GetAlignment(hAlign, vAlign);
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
	}
}

void ibGridCellAttr::GetNonDefaultAlignment(int* hAlign, int* vAlign) const
{
	// The logic here is tricky but necessary to handle all the cases: if we
	// have non-default alignment on input, we should only override it if this
	// attribute specifies a non-default alignment. However if the input
	// alignment is invalid, we need to always initialize it, using the default
	// attribute if necessary.

	// First of all, never dereference null pointer.
	if (hAlign)
	{
		if (this != m_defGridAttr && m_hAlign != wxALIGN_INVALID)
		{
			// This attribute has its own alignment, which should always
			// override the input alignment value.
			*hAlign = m_hAlign;
		}
		else if (*hAlign == wxALIGN_INVALID)
		{
			// No input alignment specified, fill it with the default alignment
			// (note that we know that this attribute itself doesn't have any
			// specific alignment or is the same as the default one anyhow in
			// in this "else" branch, so we don't need to check m_hAlign here).
			*hAlign = m_defGridAttr->m_hAlign;
		}
		//else: Input alignment is valid but ours one isn't, nothing to do.
	}

	// This is exactly the same logic as above.
	if (vAlign)
	{
		if (this != m_defGridAttr && m_vAlign != wxALIGN_INVALID)
			*vAlign = m_vAlign;
		else if (*vAlign == wxALIGN_INVALID)
			*vAlign = m_defGridAttr->m_vAlign;
	}
}

ibGridCellBorder ibGridCellAttr::GetBorderLeft() const
{
	if (HasBorderLeft())
	{
		return m_borderLeft;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetBorderLeft();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return ibGridCellBorder();
	}
}

ibGridCellBorder ibGridCellAttr::GetBorderRight() const
{
	if (HasBorderRight())
	{
		return m_borderRight;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetBorderRight();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return ibGridCellBorder();
	}
}

ibGridCellBorder ibGridCellAttr::GetBorderTop() const
{
	if (HasBorderTop())
	{
		return m_borderTop;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetBorderTop();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return ibGridCellBorder();
	}
}

ibGridCellBorder ibGridCellAttr::GetBorderBottom() const
{
	if (HasBorderBottom())
	{
		return m_borderBottom;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetBorderBottom();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return ibGridCellBorder();
	}
}

void ibGridCellAttr::GetSize(int* num_rows, int* num_cols) const
{
	if (num_rows)
		*num_rows = m_sizeRows;
	if (num_cols)
		*num_cols = m_sizeCols;
}

ibGridFitMode ibGridCellAttr::GetFitMode() const
{
	if (m_fitMode.IsSpecified())
	{
		return m_fitMode;
	}
	else if (m_defGridAttr && m_defGridAttr != this)
	{
		return m_defGridAttr->GetFitMode();
	}
	else
	{
		wxFAIL_MSG(wxT("Missing default cell attribute"));
		return ibGridFitMode();
	}
}

bool ibGridCellAttr::CanOverflow() const
{
	// If overflow is disabled anyhow, we definitely can't overflow.
	if (!GetOverflow())
		return false;

	// But if it is enabled, we still don't use it for right-aligned or
	// centered cells because it's not really clear how it should work for
	// them.
	int hAlign = wxALIGN_LEFT;
	GetNonDefaultAlignment(&hAlign, NULL);

	return hAlign == wxALIGN_LEFT;
}

// GetRenderer and GetEditor use a slightly different decision path about
// which attribute to use.  If a non-default attr object has one then it is
// used, otherwise the default editor or renderer is fetched from the grid and
// used.  It should be the default for the data type of the cell.  If it is
// NULL (because the table has a type that the grid does not have in its
// registry), then the grid's default editor or renderer is used.

ibGridCellRenderer* ibGridCellAttr::GetRenderer(const ibGrid* grid, int row, int col) const
{
	ibGridCellRenderer* renderer = NULL;

	if (m_renderer && this != m_defGridAttr)
	{
		// use the cells renderer if it has one
		renderer = m_renderer;
		renderer->IncRef();
	}
	else // no non-default cell renderer
	{
		// get default renderer for the data type
		if (grid)
		{
			// GetDefaultRendererForCell() will do IncRef() for us
			renderer = grid->GetDefaultRendererForCell(row, col);
		}

		if (renderer == NULL)
		{
			if ((m_defGridAttr != NULL) && (m_defGridAttr != this))
			{
				// if we still don't have one then use the grid default
				// (no need for IncRef() here either)
				renderer = m_defGridAttr->GetRenderer(NULL, 0, 0);
			}
			else // default grid attr
			{
				// use m_renderer which we had decided not to use initially
				renderer = m_renderer;
				if (renderer)
					renderer->IncRef();
			}
		}
	}

	// we're supposed to always find something
	wxASSERT_MSG(renderer, wxT("Missing default cell renderer"));

	return renderer;
}

// same as above, except for s/renderer/editor/g
ibGridCellEditor* ibGridCellAttr::GetEditor(const ibGrid* grid, int row, int col) const
{
	ibGridCellEditor* editor = NULL;

	if (m_editor && this != m_defGridAttr)
	{
		// use the cells editor if it has one
		editor = m_editor;
		editor->IncRef();
	}
	else // no non default cell editor
	{
		// get default editor for the data type
		if (grid)
		{
			// GetDefaultEditorForCell() will do IncRef() for us
			editor = grid->GetDefaultEditorForCell(row, col);
		}

		if (editor == NULL)
		{
			if ((m_defGridAttr != NULL) && (m_defGridAttr != this))
			{
				// if we still don't have one then use the grid default
				// (no need for IncRef() here either)
				editor = m_defGridAttr->GetEditor(NULL, 0, 0);
			}
			else // default grid attr
			{
				// use m_editor which we had decided not to use initially
				editor = m_editor;
				if (editor)
					editor->IncRef();
			}
		}
	}

	// we're supposed to always find something
	wxASSERT_MSG(editor, wxT("Missing default cell editor"));

	return editor;
}

// ----------------------------------------------------------------------------
// ibGridCellAttrData
// ----------------------------------------------------------------------------

namespace
{

	// Helper functions to convert grid coords to a key for the attr map, and
	// vice versa.

	ibGridCoordsToAttrMap::key_type CoordsToKey(int row, int col)
	{
		// Treat both row and col as unsigned to not cause havoc with (unsupported)
		// negative coords.
		return (static_cast<wxULongLong_t>(row) << 32) + static_cast<wxUint32>(col);
	}

	void KeyToCoords(ibGridCoordsToAttrMap::key_type key, int* pRow, int* pCol)
	{
		*pRow = key >> 32;
		*pCol = key & wxUINT32_MAX;
	}

} // anonymous namespace

ibGridCellAttrData::~ibGridCellAttrData()
{
	for (ibGridCoordsToAttrMap::iterator it = m_attrs.begin();
		it != m_attrs.end();
		++it)
	{
		it->second->DecRef();
	}

	m_attrs.clear();
}

void ibGridCellAttrData::SetAttr(ibGridCellAttr* attr, int row, int col)
{
	ibGridCoordsToAttrMap::iterator it = FindIndex(row, col);
	if (it == m_attrs.end())
	{
		if (attr)
		{
			// add the attribute
			m_attrs[CoordsToKey(row, col)] = attr;
		}
		//else: nothing to do
	}
	else // we already have an attribute for this cell
	{
		// See note near DecRef() in ibGridRowOrColAttrData::SetAttr for why
		// this also works when old and new attribute are the same.
		it->second->DecRef();

		// Change or remove the attribute.
		if (attr)
			it->second = attr;
		else
			m_attrs.erase(it);
	}
}

ibGridCellAttr* ibGridCellAttrData::GetAttr(int row, int col) const
{
	ibGridCellAttr* attr = NULL;

	ibGridCoordsToAttrMap::iterator it = FindIndex(row, col);
	if (it != m_attrs.end())
	{
		attr = it->second;
		attr->IncRef();
	}

	return attr;
}

namespace
{

	void UpdateCellAttrRowsOrCols(ibGridCoordsToAttrMap& attrs, int editPos,
		int editRowCount, int editColCount)
	{
		wxASSERT(!editRowCount || !editColCount);

		const bool isEditingRows = (editRowCount != 0);
		const int editCount = (isEditingRows ? editRowCount : editColCount);

		// Copy updated attributes to a new map instead of attempting to edit attrs
		// map in-place. This requires more memory but greatly simplifies the code
		// and any attempt with in-place editing would likely also require a lot
		// more attr lookups.
		// The updated copy will contain an attrs map with, if applicable: adjusted
		// coords and cell size, newly inserted attributes (for multicells), and
		// without now deleted attributes.
		ibGridCoordsToAttrMap newAttrs;

		for (ibGridCoordsToAttrMap::iterator it = attrs.begin();
			it != attrs.end();
			++it)
		{
			const ibGridCoordsToAttrMap::key_type oldCoords = it->first;
			ibGridCellAttr* cellAttr = it->second;

			int cellRows, cellCols;
			cellAttr->GetSize(&cellRows, &cellCols);

			int cellRow, cellCol;
			KeyToCoords(oldCoords, &cellRow, &cellCol);

			const int cellPos = isEditingRows ? cellRow : cellCol;

			if (cellPos < editPos)
			{
				// This cell's coords aren't influenced by the editing, however
				// do adjust a multicell's main size, if needed.
				if (GetCellSpan(cellRows, cellCols) == ibGrid::CellSpan_Main)
				{
					int mainSize = isEditingRows ? cellRows : cellCols;
					if (cellPos + mainSize > editPos)
					{
						// Multicell is within affected range:
						// Adjust its size.

						if (editCount >= 0)
						{
							mainSize += editCount;
						}
						else
						{
							// Reduce multicell size by number of deletions, but
							// never more than the multicell's size minus one:
							// cellPos (the main cell) is always less than editPos
							// at this point, then with the below code a multicell
							// with size 7 is at most reduced by:
							// cellPos + 7 - (cellPos + 1) = 7 - 1 = 6.
							mainSize -= wxMin(-editCount,
								cellPos + mainSize - editPos);
							/*
							The above was derived from:
							first_del = edit
							last_del = min(edit - count - 1, cell + size - 1)
							size -= (last_del + 1 - first_del)

							eliminating the 1's:

							first_del = edit
							last_del = min(edit - count, cell + size)
							size -= (last_del - first_del)

							reducing each by edit:

							first_del = 0
							last_del_plus_1 = min(0 - count, cell + size - edit)
							size -= (last_del_plus_1 - 0)

							after eliminating the 0's and substitution, leaving:

							size -= min(-count, cell + size - edit)

							E.g. with a multicell of size 7 and at 2 positions
							after the main cell 100 positions are deleted then
							the size will not (/can't) be reduced by 100 cells
							but by:

							cellPos + 7 - editPos =       # editPos = cellPos + 2
							cellPos + 7 - (cellPos + 2) = # eliminate cellPos
							7 - 2 =
							5 cells, making the final size 7 - 5 = 2.
							*/
						}

						cellAttr->SetSize(isEditingRows ? mainSize : cellRows,
							isEditingRows ? cellCols : mainSize);
					}
				}

				// Set attribute at old/unmodified coords.
				newAttrs[oldCoords] = cellAttr;

				continue;
			}

			if (editCount < 0 && cellPos < editPos - editCount)
			{
				// This row/col is deleted and the cell doesn't exist any longer:
				// Remove the attribute.
				cellAttr->DecRef();

				continue;
			}

			const ibGridCoordsToAttrMap::key_type newCoords
				= CoordsToKey(cellRow + editRowCount, cellCol + editColCount);

			if (GetCellSpan(cellRows, cellCols) != ibGrid::CellSpan_Inside)
			{
				// Rows/cols inserted or deleted (and this cell still exists):
				// Adjust cell coords.
				newAttrs[newCoords] = cellAttr;

				// Nothing more to do: cell is not an inside cell of a multicell.
				continue;
			}

			// Handle inside cell's existence, coords, and size.

			const int mainPos = cellPos + (isEditingRows ? cellRows : cellCols);

			if (editCount < 0
				&& mainPos >= editPos && mainPos < editPos - editCount)
			{
				// On a position that still exists after deletion but main cell
				// of multicell is within deletion range so the multicell is gone:
				// Remove the attribute.
				cellAttr->DecRef();

				continue;
			}

			// Rows/cols inserted or deleted (and this inside cell still exists):
			// Adjust (inside) cell coords.
			newAttrs[newCoords] = cellAttr;

			if (mainPos >= editPos)
			{
				// Nothing more to do: the multicell that this inside cell is part
				// of is moving its main cell as well so offsets to the main cell
				// don't change and there are no edits changing the multicell size.
				continue;
			}

			if (editCount > 0 && cellPos == editPos)
			{
				// At an (old) position that is newly inserted: this is the only
				// opportunity to add required inside cells that point to
				// the main cell. E.g. with a 2x1 multicell that increases in size
				// there's only one inside cell that will be visited while there
				// can be multiple insertions.
				for (int i = 0; i < editCount; ++i)
				{
					const int adjustRows = i * isEditingRows,
						adjustCols = i * !isEditingRows;

					ibGridCellAttr* attr = new ibGridCellAttr;
					attr->SetSize(cellRows - adjustRows, cellCols - adjustCols);

					const int row = cellRow + adjustRows,
						col = cellCol + adjustCols;
					newAttrs[CoordsToKey(row, col)] = attr;
				}
			}

			// Let this inside cell's size point to the main cell of the multicell.
			cellAttr->SetSize(cellRows - editRowCount, cellCols - editColCount);
		}

		attrs = newAttrs;
	}

} // anonymous namespace

void ibGridCellAttrData::UpdateAttrRows(size_t pos, int numRows)
{
	UpdateCellAttrRowsOrCols(m_attrs, static_cast<int>(pos), numRows, 0);
}

void ibGridCellAttrData::UpdateAttrCols(size_t pos, int numCols)
{
	UpdateCellAttrRowsOrCols(m_attrs, static_cast<int>(pos), 0, numCols);
}

ibGridCoordsToAttrMap::iterator
ibGridCellAttrData::FindIndex(int row, int col) const
{
	return m_attrs.find(CoordsToKey(row, col));
}

// ----------------------------------------------------------------------------
// ibGridRowOrColAttrData
// ----------------------------------------------------------------------------

ibGridRowOrColAttrData::~ibGridRowOrColAttrData()
{
	size_t count = m_attrs.GetCount();
	for (size_t n = 0; n < count; n++)
	{
		m_attrs[n]->DecRef();
	}
}

ibGridCellAttr* ibGridRowOrColAttrData::GetAttr(int rowOrCol) const
{
	ibGridCellAttr* attr = NULL;

	int n = m_rowsOrCols.Index(rowOrCol);
	if (n != wxNOT_FOUND)
	{
		attr = m_attrs[(size_t)n];
		attr->IncRef();
	}

	return attr;
}

void ibGridRowOrColAttrData::SetAttr(ibGridCellAttr* attr, int rowOrCol)
{
	int i = m_rowsOrCols.Index(rowOrCol);
	if (i == wxNOT_FOUND)
	{
		if (attr)
		{
			// store the new attribute, taking its ownership
			m_rowsOrCols.Add(rowOrCol);
			m_attrs.Add(attr);
		}
		// nothing to remove
	}
	else // we have an attribute for this row or column
	{
		size_t n = (size_t)i;

		// notice that this code works correctly even when the old attribute is
		// the same as the new one: as we own of it, we must call DecRef() on
		// it in any case and this won't result in destruction of the new
		// attribute if it's the same as old one because it must have ref count
		// of at least 2 to be passed to us while we keep a reference to it too
		m_attrs[n]->DecRef();

		if (attr)
		{
			// replace the attribute with the new one
			m_attrs[n] = attr;
		}
		else // remove the attribute
		{
			m_rowsOrCols.RemoveAt(n);
			m_attrs.RemoveAt(n);
		}
	}
}

void ibGridRowOrColAttrData::UpdateAttrRowsOrCols(size_t pos, int numRowsOrCols)
{
	size_t count = m_attrs.GetCount();
	for (size_t n = 0; n < count; n++)
	{
		int& rowOrCol = m_rowsOrCols[n];
		if ((size_t)rowOrCol >= pos)
		{
			if (numRowsOrCols > 0)
			{
				// If rows or cols inserted, increment row/col counter where necessary
				rowOrCol += numRowsOrCols;
			}
			else if (numRowsOrCols < 0)
			{
				// If rows/cols deleted, either decrement row/col counter (if row/col still exists)
				if ((size_t)rowOrCol >= pos - numRowsOrCols)
					rowOrCol += numRowsOrCols;
				else
				{
					m_rowsOrCols.RemoveAt(n);
					m_attrs[n]->DecRef();
					m_attrs.RemoveAt(n);
					n--;
					count--;
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// ibGridCellAttrProvider
// ----------------------------------------------------------------------------

ibGridCellAttrProvider::ibGridCellAttrProvider()
{
	m_data = NULL;
}

ibGridCellAttrProvider::~ibGridCellAttrProvider()
{
	delete m_data;
}

void ibGridCellAttrProvider::InitData()
{
	m_data = new ibGridCellAttrProviderData;
}

ibGridCellAttr* ibGridCellAttrProvider::GetAttr(int row, int col,
	ibGridCellAttr::wxAttrKind  kind) const
{
	ibGridCellAttr* attr = NULL;
	if (m_data)
	{
		switch (kind)
		{
		case (ibGridCellAttr::Any):
			// Get cached merge attributes.
			// Currently not used as no cache implemented as not mutable
			// attr = m_data->m_mergeAttr.GetAttr(row, col);
			if (!attr)
			{
				// Basically implement old version.
				// Also check merge cache, so we don't have to re-merge every time..
				ibGridCellAttr* attrcell = m_data->m_cellAttrs.GetAttr(row, col);
				ibGridCellAttr* attrrow = m_data->m_rowAttrs.GetAttr(row);
				ibGridCellAttr* attrcol = m_data->m_colAttrs.GetAttr(col);

				if ((attrcell != attrrow) && (attrrow != attrcol) && (attrcell != attrcol))
				{
					// Two or more are non NULL
					attr = new ibGridCellAttr;
					attr->SetKind(ibGridCellAttr::Merged);

					// Order is important..
					if (attrcell)
					{
						attr->MergeWith(attrcell);
						attrcell->DecRef();
					}
					if (attrcol)
					{
						attr->MergeWith(attrcol);
						attrcol->DecRef();
					}
					if (attrrow)
					{
						attr->MergeWith(attrrow);
						attrrow->DecRef();
					}

					// store merge attr if cache implemented
					//attr->IncRef();
					//m_data->m_mergeAttr.SetAttr(attr, row, col);
				}
				else
				{
					// one or none is non null return it or null.
					if (attrrow)
						attr = attrrow;
					if (attrcol)
					{
						if (attr)
							attr->DecRef();
						attr = attrcol;
					}
					if (attrcell)
					{
						if (attr)
							attr->DecRef();
						attr = attrcell;
					}
				}
			}
			break;

		case (ibGridCellAttr::Cell):
			attr = m_data->m_cellAttrs.GetAttr(row, col);
			break;

		case (ibGridCellAttr::Col):
			attr = m_data->m_colAttrs.GetAttr(col);
			break;

		case (ibGridCellAttr::Row):
			attr = m_data->m_rowAttrs.GetAttr(row);
			break;

		default:
			// unused as yet...
			// (ibGridCellAttr::Default):
			// (ibGridCellAttr::Merged):
			break;
		}
	}

	return attr;
}

void ibGridCellAttrProvider::SetAttr(ibGridCellAttr* attr,
	int row, int col)
{
	if (!m_data)
		InitData();

	m_data->m_cellAttrs.SetAttr(attr, row, col);
}

void ibGridCellAttrProvider::SetRowAttr(ibGridCellAttr* attr, int row)
{
	if (!m_data)
		InitData();

	m_data->m_rowAttrs.SetAttr(attr, row);
}

void ibGridCellAttrProvider::SetColAttr(ibGridCellAttr* attr, int col)
{
	if (!m_data)
		InitData();

	m_data->m_colAttrs.SetAttr(attr, col);
}

void ibGridCellAttrProvider::UpdateAttrRows(size_t pos, int numRows)
{
	if (m_data)
	{
		m_data->m_cellAttrs.UpdateAttrRows(pos, numRows);
		m_data->m_rowAttrs.UpdateAttrRowsOrCols(pos, numRows);
	}
}

void ibGridCellAttrProvider::UpdateAttrCols(size_t pos, int numCols)
{
	if (m_data)
	{
		m_data->m_cellAttrs.UpdateAttrCols(pos, numCols);
		m_data->m_colAttrs.UpdateAttrRowsOrCols(pos, numCols);
	}
}

const ibGridColumnHeaderRenderer&
ibGridCellAttrProvider::GetColumnHeaderRenderer(int WXUNUSED(col))
{
	return gs_defaultHeaderRenderers.colRenderer;
}

const ibGridRowHeaderRenderer&
ibGridCellAttrProvider::GetRowHeaderRenderer(int WXUNUSED(row))
{
	return gs_defaultHeaderRenderers.rowRenderer;
}

const ibGridCornerHeaderRenderer& ibGridCellAttrProvider::GetCornerRenderer()
{
	return gs_defaultHeaderRenderers.cornerRenderer;
}

// ----------------------------------------------------------------------------
// ibGridBlockCoords
// ----------------------------------------------------------------------------

ibGridBlockDiffResult
ibGridBlockCoords::Difference(const ibGridBlockCoords& other,
	int splitOrientation) const
{
	ibGridBlockDiffResult result;

	// Check whether the blocks intersect.
	if (!Intersects(other))
	{
		result.m_parts[0] = *this;
		return result;
	}

	// Split the block in up to 4 new parts, that don't contain the other
	// block, like this (for wxHORIZONTAL):
	// |-----------------------------|
	// |                             |
	// |           part[0]           |
	// |                             |
	// |-----------------------------|
	// | part[2] |  other  | part[3] |
	// |-----------------------------|
	// |                             |
	// |           part[1]           |
	// |                             |
	// |-----------------------------|
	// And for wxVERTICAL:
	// |-----------------------------|
	// |         |         |         |
	// |         | part[2] |         |
	// |         |         |         |
	// |         |---------|         |
	// | part[0] |  other  | part[1] |
	// |         |---------|         |
	// |         |         |         |
	// |         | part[3] |         |
	// |         |         |         |
	// |-----------------------------|

	if (splitOrientation == wxHORIZONTAL)
	{
		// Part[0].
		if (m_topRow < other.m_topRow)
			result.m_parts[0] =
			ibGridBlockCoords(m_topRow, m_leftCol,
				other.m_topRow - 1, m_rightCol);

		// Part[1].
		if (m_bottomRow > other.m_bottomRow)
			result.m_parts[1] =
			ibGridBlockCoords(other.m_bottomRow + 1, m_leftCol,
				m_bottomRow, m_rightCol);

		const int maxTopRow = wxMax(m_topRow, other.m_topRow);
		const int minBottomRow = wxMin(m_bottomRow, other.m_bottomRow);

		// Part[2].
		if (m_leftCol < other.m_leftCol)
			result.m_parts[2] =
			ibGridBlockCoords(maxTopRow, m_leftCol,
				minBottomRow, other.m_leftCol - 1);

		// Part[3].
		if (m_rightCol > other.m_rightCol)
			result.m_parts[3] =
			ibGridBlockCoords(maxTopRow, other.m_rightCol + 1,
				minBottomRow, m_rightCol);
	}
	else // wxVERTICAL
	{
		// Part[0].
		if (m_leftCol < other.m_leftCol)
			result.m_parts[0] =
			ibGridBlockCoords(m_topRow, m_leftCol,
				m_bottomRow, other.m_leftCol - 1);

		// Part[1].
		if (m_rightCol > other.m_rightCol)
			result.m_parts[1] =
			ibGridBlockCoords(m_topRow, other.m_rightCol + 1,
				m_bottomRow, m_rightCol);

		const int maxLeftCol = wxMax(m_leftCol, other.m_leftCol);
		const int minRightCol = wxMin(m_rightCol, other.m_rightCol);

		// Part[2].
		if (m_topRow < other.m_topRow)
			result.m_parts[2] =
			ibGridBlockCoords(m_topRow, maxLeftCol,
				other.m_topRow - 1, minRightCol);

		// Part[3].
		if (m_bottomRow > other.m_bottomRow)
			result.m_parts[3] =
			ibGridBlockCoords(other.m_bottomRow + 1, maxLeftCol,
				m_bottomRow, minRightCol);
	}

	return result;
}

ibGridBlockDiffResult
ibGridBlockCoords::SymDifference(const ibGridBlockCoords& other) const
{
	ibGridBlockDiffResult result;

	// Check whether the blocks intersect.
	if (!Intersects(other))
	{
		result.m_parts[0] = *this;
		result.m_parts[1] = other;
		return result;
	}

	// Possible result blocks:
	// |------------------|
	// |                  |            minUpper->m_topRow
	// |      part[0]     |
	// |                  |            maxUpper->m_topRow - 1
	// |-----------------------------|
	// |         |         |         | maxUpper->m_topRow
	// | part[2] |    x    | part[3] |
	// |         |         |         | minLower->m_bottomRow
	// |-----------------------------|
	//           |                   | minLower->m_bottomRow + 1
	//           |      part[1]      |
	//           |                   | maxLower->m_bottomRow
	//           |-------------------|
	//
	// The x marks the intersection of the blocks, which is not a part
	// of the result.

	// Part[0].
	int maxUpperRow;
	if (m_topRow != other.m_topRow)
	{
		const bool block1Min = m_topRow < other.m_topRow;
		const ibGridBlockCoords& minUpper = block1Min ? *this : other;
		const ibGridBlockCoords& maxUpper = block1Min ? other : *this;
		maxUpperRow = maxUpper.m_topRow;

		result.m_parts[0] = ibGridBlockCoords(minUpper.m_topRow,
			minUpper.m_leftCol,
			maxUpper.m_topRow - 1,
			minUpper.m_rightCol);
	}
	else
	{
		maxUpperRow = m_topRow;
	}

	// Part[1].
	int minLowerRow;
	if (m_bottomRow != other.m_bottomRow)
	{
		const bool block1Min = m_bottomRow < other.m_bottomRow;
		const ibGridBlockCoords& minLower = block1Min ? *this : other;
		const ibGridBlockCoords& maxLower = block1Min ? other : *this;
		minLowerRow = minLower.m_bottomRow;

		result.m_parts[1] = ibGridBlockCoords(minLower.m_bottomRow + 1,
			maxLower.m_leftCol,
			maxLower.m_bottomRow,
			maxLower.m_rightCol);
	}
	else
	{
		minLowerRow = m_bottomRow;
	}

	// Part[2].
	if (m_leftCol != other.m_leftCol)
	{
		result.m_parts[2] = ibGridBlockCoords(maxUpperRow,
			wxMin(m_leftCol,
				other.m_leftCol),
			minLowerRow,
			wxMax(m_leftCol,
				other.m_leftCol) - 1);
	}

	// Part[3].
	if (m_rightCol != other.m_rightCol)
	{
		result.m_parts[3] = ibGridBlockCoords(maxUpperRow,
			wxMin(m_rightCol,
				other.m_rightCol) + 1,
			minLowerRow,
			wxMax(m_rightCol,
				other.m_rightCol));
	}

	return result;
}

// ----------------------------------------------------------------------------
// ibGridTableBase
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibGridTableBase, wxObject);

ibGridTableBase::ibGridTableBase()
{
	m_view = NULL;
	m_attrProvider = NULL;
}

ibGridTableBase::~ibGridTableBase()
{
	delete m_attrProvider;
}

void ibGridTableBase::SetAttrProvider(ibGridCellAttrProvider* attrProvider)
{
	delete m_attrProvider;
	m_attrProvider = attrProvider;
}

bool ibGridTableBase::CanHaveAttributes()
{
	if (!GetAttrProvider())
	{
		// use the default attr provider by default
		SetAttrProvider(new ibGridCellAttrProvider);
	}

	return true;
}

ibGridCellAttr* ibGridTableBase::GetAttr(int row, int col, ibGridCellAttr::wxAttrKind  kind)
{
	if (m_attrProvider)
		return m_attrProvider->GetAttr(row, col, kind);
	else
		return NULL;
}

void ibGridTableBase::SetAttr(ibGridCellAttr* attr, int row, int col)
{
	if (m_attrProvider)
	{
		if (attr)
			attr->SetKind(ibGridCellAttr::Cell);
		m_attrProvider->SetAttr(attr, row, col);
	}
	else
	{
		// as we take ownership of the pointer and don't store it, we must
		// free it now
		wxSafeDecRef(attr);
	}
}

void ibGridTableBase::SetRowAttr(ibGridCellAttr* attr, int row)
{
	if (m_attrProvider)
	{
		if (attr)
			attr->SetKind(ibGridCellAttr::Row);
		m_attrProvider->SetRowAttr(attr, row);
	}
	else
	{
		// as we take ownership of the pointer and don't store it, we must
		// free it now
		wxSafeDecRef(attr);
	}
}

void ibGridTableBase::SetColAttr(ibGridCellAttr* attr, int col)
{
	if (m_attrProvider)
	{
		if (attr)
			attr->SetKind(ibGridCellAttr::Col);
		m_attrProvider->SetColAttr(attr, col);
	}
	else
	{
		// as we take ownership of the pointer and don't store it, we must
		// free it now
		wxSafeDecRef(attr);
	}
}

bool ibGridTableBase::InsertRows(size_t WXUNUSED(pos),
	size_t WXUNUSED(numRows))
{
	wxFAIL_MSG(wxT("Called grid table class function InsertRows\nbut your derived table class does not override this function"));

	return false;
}

bool ibGridTableBase::AppendRows(size_t WXUNUSED(numRows))
{
	wxFAIL_MSG(wxT("Called grid table class function AppendRows\nbut your derived table class does not override this function"));

	return false;
}

bool ibGridTableBase::DeleteRows(size_t WXUNUSED(pos),
	size_t WXUNUSED(numRows))
{
	wxFAIL_MSG(wxT("Called grid table class function DeleteRows\nbut your derived table class does not override this function"));

	return false;
}

bool ibGridTableBase::InsertCols(size_t WXUNUSED(pos),
	size_t WXUNUSED(numCols))
{
	wxFAIL_MSG(wxT("Called grid table class function InsertCols\nbut your derived table class does not override this function"));

	return false;
}

bool ibGridTableBase::AppendCols(size_t WXUNUSED(numCols))
{
	wxFAIL_MSG(wxT("Called grid table class function AppendCols\nbut your derived table class does not override this function"));

	return false;
}

bool ibGridTableBase::DeleteCols(size_t WXUNUSED(pos),
	size_t WXUNUSED(numCols))
{
	wxFAIL_MSG(wxT("Called grid table class function DeleteCols\nbut your derived table class does not override this function"));

	return false;
}

wxString ibGridTableBase::GetRowLabelValue(int row)
{
	wxString s;

	// RD: Starting the rows at zero confuses users,
	// no matter how much it makes sense to us geeks.
	s << row + 1;

	return s;
}

wxString ibGridTableBase::GetColLabelValue(int col)
{
	// default col labels are:
	//   cols 0 to 25   : A-Z
	//   cols 26 to 675 : AA-ZZ
	//   etc.

	wxString s;
	unsigned int i, n;
	for (n = 1; ; n++)
	{
		s += (wxChar)(wxT('A') + (wxChar)(col % 26));
		col = col / 26 - 1;
		if (col < 0)
			break;
	}

	// reverse the string...
	wxString s2;
	for (i = 0; i < n; i++)
	{
		s2 += s[n - i - 1];
	}

	return s2;
}

wxString ibGridTableBase::GetCornerLabelValue() const
{
	return wxString();
}

bool ibGridTableBase::GetTypeName(int WXUNUSED(row), int WXUNUSED(col), wxString& typeName)
{
	static wxString typeCacheName = wxGRID_VALUE_STRING;
	if (!typeCacheName.IsSameAs(typeName))
		typeName = typeCacheName;
	return true;
}

bool ibGridTableBase::CanGetValueAs(int WXUNUSED(row), int WXUNUSED(col),
	const wxString& typeName)
{
	return typeName == wxGRID_VALUE_STRING;
}

bool ibGridTableBase::CanSetValueAs(int row, int col, const wxString& typeName)
{
	return CanGetValueAs(row, col, typeName);
}

long ibGridTableBase::GetValueAsLong(int WXUNUSED(row), int WXUNUSED(col))
{
	return 0;
}

double ibGridTableBase::GetValueAsDouble(int WXUNUSED(row), int WXUNUSED(col))
{
	return 0.0;
}

bool ibGridTableBase::GetValueAsBool(int WXUNUSED(row), int WXUNUSED(col))
{
	return false;
}

wxVariant ibGridTableBase::GetValueAsVariant(int WXUNUSED(row), int WXUNUSED(col))
{
	return wxVariant();
}

void ibGridTableBase::SetValueAsLong(int WXUNUSED(row), int WXUNUSED(col),
	long WXUNUSED(value))
{
}

void ibGridTableBase::SetValueAsDouble(int WXUNUSED(row), int WXUNUSED(col),
	double WXUNUSED(value))
{
}

void ibGridTableBase::SetValueAsBool(int WXUNUSED(row), int WXUNUSED(col),
	bool WXUNUSED(value))
{
}

void ibGridTableBase::SetValueAsVariant(int WXUNUSED(row), int WXUNUSED(col),
	wxVariant WXUNUSED(value))
{
}

void* ibGridTableBase::GetValueAsCustom(int WXUNUSED(row), int WXUNUSED(col),
	const wxString& WXUNUSED(typeName))
{
	return NULL;
}

void  ibGridTableBase::SetValueAsCustom(int WXUNUSED(row), int WXUNUSED(col),
	const wxString& WXUNUSED(typeName),
	void* WXUNUSED(value))
{
}

//////////////////////////////////////////////////////////////////////

void ibGridTableBase::OnGridTableNotify(int row, int col, const wxString& s)
{
	if (GetView())
	{
		GetView()->SendEvent(wxEVT_GRID_TABLE_MODIFIED, row, col, s);
	}
}

//////////////////////////////////////////////////////////////////////
//
// Message class for the grid table to send requests and notifications
// to the grid view
//

ibGridTableMessage::ibGridTableMessage()
{
	m_table = NULL;
	m_id = -1;
	m_comInt1 = -1;
	m_comInt2 = -1;
}

ibGridTableMessage::ibGridTableMessage(ibGridTableBase* table, int id,
	int commandInt1, int commandInt2)
{
	m_table = table;
	m_id = id;
	m_comInt1 = commandInt1;
	m_comInt2 = commandInt2;
}

//////////////////////////////////////////////////////////////////////
//
// A basic grid table for string data. An object of this class will
// created by ibGrid if you don't specify an alternative table class.
//

WX_DEFINE_OBJARRAY(ibGridStringArray)

wxIMPLEMENT_DYNAMIC_CLASS(ibGridStringTable, ibGridTableBase);

ibGridStringTable::ibGridStringTable()
	: ibGridTableBase()
{
	m_numCols = 0;
}

ibGridStringTable::ibGridStringTable(int numRows, int numCols)
	: ibGridTableBase()
{
	m_numCols = numCols;

	m_data.Alloc(numRows);

	wxArrayString sa;
	sa.Alloc(numCols);
	sa.Add(wxEmptyString, numCols);

	m_data.Add(sa, numRows);
}

void ibGridStringTable::GetValue(int row, int col, wxString& value)
{
	wxCHECK2_MSG((row >= 0 && row < GetNumberRows()) &&
		(col >= 0 && col < GetNumberCols()),
		,
		wxT("invalid row or column index in ibGridStringTable"));

	value = m_data[row][col];
}

void ibGridStringTable::SetValue(int row, int col, const wxString& value)
{
	wxCHECK_RET((row >= 0 && row < GetNumberRows()) &&
		(col >= 0 && col < GetNumberCols()),
		wxT("invalid row or column index in ibGridStringTable"));

	m_data[row][col] = value;

	OnGridTableNotify(row, col, value);
}

void ibGridStringTable::Clear()
{
	//int numRows;
	//numRows = m_data.GetCount();
	//if (numRows > 0)
	//{
	//	int numCols;
	//	numCols = m_data[0].GetCount();

	//	int row;
	//	for (row = 0; row < numRows; row++)
	//	{
	//		int col;
	//		for (col = 0; col < numCols; col++)
	//		{
	//			m_data[row][col].clear();
	//		}
	//	}
	//}

	m_data.Clear();
}

bool ibGridStringTable::InsertRows(size_t pos, size_t numRows)
{
	if (pos >= m_data.size())
	{
		return AppendRows(numRows);
	}

	wxArrayString sa;
	sa.Alloc(m_numCols);
	sa.Add(wxEmptyString, m_numCols);
	m_data.Insert(sa, pos, numRows);

	if (GetView())
	{
		ibGridTableMessage msg(this,
			wxGRIDTABLE_NOTIFY_ROWS_INSERTED,
			pos,
			numRows);

		GetView()->ProcessTableMessage(msg);
	}

	return true;
}

bool ibGridStringTable::AppendRows(size_t numRows)
{
	wxArrayString sa;
	if (m_numCols > 0)
	{
		sa.Alloc(m_numCols);
		sa.Add(wxEmptyString, m_numCols);
	}

	m_data.Add(sa, numRows);

	if (GetView())
	{
		ibGridTableMessage msg(this,
			wxGRIDTABLE_NOTIFY_ROWS_APPENDED,
			numRows);

		GetView()->ProcessTableMessage(msg);
	}

	return true;
}

bool ibGridStringTable::DeleteRows(size_t pos, size_t numRows)
{
	size_t curNumRows = m_data.GetCount();

	if (pos >= curNumRows)
	{
		wxFAIL_MSG(wxString::Format
		(
			wxT("Called ibGridStringTable::DeleteRows(pos=%lu, N=%lu)\nPos value is invalid for present table with %lu rows"),
			(unsigned long)pos,
			(unsigned long)numRows,
			(unsigned long)curNumRows
		));

		return false;
	}

	if (numRows > curNumRows - pos)
	{
		numRows = curNumRows - pos;
	}

	if (numRows >= curNumRows)
	{
		m_data.Clear();
	}
	else
	{
		m_data.RemoveAt(pos, numRows);
	}

	if (GetView())
	{
		ibGridTableMessage msg(this,
			wxGRIDTABLE_NOTIFY_ROWS_DELETED,
			pos,
			numRows);

		GetView()->ProcessTableMessage(msg);
	}

	return true;
}

bool ibGridStringTable::InsertCols(size_t pos, size_t numCols)
{
	if (pos >= static_cast<size_t>(m_numCols))
	{
		return AppendCols(numCols);
	}

	if (!m_colLabels.IsEmpty())
	{
		m_colLabels.Insert(wxEmptyString, pos, numCols);

		for (size_t i = pos; i < pos + numCols; i++)
			m_colLabels[i] = ibGridTableBase::GetColLabelValue(i);
	}

	for (size_t row = 0; row < m_data.size(); row++)
	{
		for (size_t col = pos; col < pos + numCols; col++)
		{
			m_data[row].Insert(wxEmptyString, col);
		}
	}

	m_numCols += numCols;

	if (GetView())
	{
		ibGridTableMessage msg(this,
			wxGRIDTABLE_NOTIFY_COLS_INSERTED,
			pos,
			numCols);

		GetView()->ProcessTableMessage(msg);
	}

	return true;
}

bool ibGridStringTable::AppendCols(size_t numCols)
{
	for (size_t row = 0; row < m_data.size(); row++)
	{
		m_data[row].Add(wxEmptyString, numCols);
	}

	m_numCols += numCols;

	if (GetView())
	{
		ibGridTableMessage msg(this,
			wxGRIDTABLE_NOTIFY_COLS_APPENDED,
			numCols);

		GetView()->ProcessTableMessage(msg);
	}

	return true;
}

bool ibGridStringTable::DeleteCols(size_t pos, size_t numCols)
{
	size_t row;

	size_t curNumRows = m_data.GetCount();
	size_t curNumCols = m_numCols;

	if (pos >= curNumCols)
	{
		wxFAIL_MSG(wxString::Format
		(
			wxT("Called ibGridStringTable::DeleteCols(pos=%lu, N=%lu)\nPos value is invalid for present table with %lu cols"),
			(unsigned long)pos,
			(unsigned long)numCols,
			(unsigned long)curNumCols
		));
		return false;
	}

	int colID;
	if (GetView())
		colID = GetView()->GetColAt(pos);
	else
		colID = pos;

	if (numCols > curNumCols - colID)
	{
		numCols = curNumCols - colID;
	}

	if (!m_colLabels.IsEmpty())
	{
		// m_colLabels stores just as many elements as it needs, e.g. if only
		// the label of the first column had been set it would have only one
		// element and not numCols, so account for it
		int numRemaining = m_colLabels.size() - colID;
		if (numRemaining > 0)
			m_colLabels.RemoveAt(colID, wxMin(numCols, numRemaining));
	}

	if (numCols >= curNumCols)
	{
		for (row = 0; row < curNumRows; row++)
		{
			m_data[row].Clear();
		}

		m_numCols = 0;
	}
	else // something will be left
	{
		for (row = 0; row < curNumRows; row++)
		{
			m_data[row].RemoveAt(colID, numCols);
		}

		m_numCols -= numCols;
	}

	if (GetView())
	{
		ibGridTableMessage msg(this,
			wxGRIDTABLE_NOTIFY_COLS_DELETED,
			pos,
			numCols);

		GetView()->ProcessTableMessage(msg);
	}

	return true;
}

wxString ibGridStringTable::GetRowLabelValue(int row)
{
	if (row > (int)(m_rowLabels.GetCount()) - 1)
	{
		// using default label
		//
		return ibGridTableBase::GetRowLabelValue(row);
	}
	else
	{
		return m_rowLabels[row];
	}
}

wxString ibGridStringTable::GetColLabelValue(int col)
{
	if (col > (int)(m_colLabels.GetCount()) - 1)
	{
		// using default label
		//
		return ibGridTableBase::GetColLabelValue(col);
	}
	else
	{
		return m_colLabels[col];
	}
}

void ibGridStringTable::SetRowLabelValue(int row, const wxString& value)
{
	if (row > (int)(m_rowLabels.GetCount()) - 1)
	{
		int n = m_rowLabels.GetCount();
		int i;

		for (i = n; i <= row; i++)
		{
			m_rowLabels.Add(ibGridTableBase::GetRowLabelValue(i));
		}
	}

	m_rowLabels[row] = value;
}

void ibGridStringTable::SetColLabelValue(int col, const wxString& value)
{
	if (col > (int)(m_colLabels.GetCount()) - 1)
	{
		int n = m_colLabels.GetCount();
		int i;

		for (i = n; i <= col; i++)
		{
			m_colLabels.Add(ibGridTableBase::GetColLabelValue(i));
		}
	}

	m_colLabels[col] = value;
}

void ibGridStringTable::SetCornerLabelValue(const wxString& value)
{
	m_cornerLabel = value;
}

wxString ibGridStringTable::GetCornerLabelValue() const
{
	return m_cornerLabel;
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibGridSubwindow, wxWindow)
EVT_MOUSE_CAPTURE_LOST(ibGridSubwindow::OnMouseCaptureLost)
wxEND_EVENT_TABLE()

void ibGridSubwindow::OnMouseCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
	m_owner->CancelMouseCapture();
}

wxBEGIN_EVENT_TABLE(ibGridRowAreaWindow, ibGridSubwindow)
EVT_PAINT(ibGridRowAreaWindow::OnPaint)
EVT_MOUSEWHEEL(ibGridRowAreaWindow::OnMouseWheel)
EVT_MOUSE_EVENTS(ibGridRowAreaWindow::OnMouseEvent)
wxEND_EVENT_TABLE()

void ibGridRowAreaWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);

	// NO - don't do this because it will set both the x and y origin
	// coords to match the parent scrolled window and we just want to
	// set the y coord  - MB
	//
	// m_owner->PrepareDC( dc );

	int x, y;
	ibGridWindow* gridWindow = IsFrozen() ? m_owner->m_frozenRowGridWin :
		m_owner->m_gridWin;
	m_owner->GetGridWindowOffset(gridWindow, x, y);
	m_owner->CalcGridWindowUnscrolledPosition(x, y, &x, &y, gridWindow);
	wxPoint pt = dc.GetDeviceOrigin();
	dc.SetDeviceOrigin(pt.x, pt.y - y);

	wxArrayInt rows = m_owner->CalcRowLabelsExposed(GetUpdateRegion(), gridWindow);
	m_owner->DrawRowAreas(dc, rows);
}

void ibGridRowAreaWindow::OnMouseEvent(wxMouseEvent& event)
{
	m_owner->ProcessRowColAreaMouseEvent(ibGridRowOperations(), event, this);
}

void ibGridRowAreaWindow::OnMouseWheel(wxMouseEvent& event)
{
	m_owner->Freeze();

	if (event.ControlDown())
		m_owner->SetGridZoom(event.m_wheelRotation > 0 ? 1 : -1);
	else if (!m_owner->ProcessWindowEvent(event))
		event.Skip();

	m_owner->Thaw();
}

//////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibGridRowLabelWindow, ibGridSubwindow)
EVT_PAINT(ibGridRowLabelWindow::OnPaint)
EVT_MOUSEWHEEL(ibGridRowLabelWindow::OnMouseWheel)
EVT_MOUSE_EVENTS(ibGridRowLabelWindow::OnMouseEvent)
wxEND_EVENT_TABLE()

void ibGridRowLabelWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);

	// NO - don't do this because it will set both the x and y origin
	// coords to match the parent scrolled window and we just want to
	// set the y coord  - MB
	//
	// m_owner->PrepareDC( dc );

	int x, y;
	ibGridWindow* gridWindow = IsFrozen() ? m_owner->m_frozenRowGridWin :
		m_owner->m_gridWin;
	m_owner->GetGridWindowOffset(gridWindow, x, y);
	m_owner->CalcGridWindowUnscrolledPosition(x, y, &x, &y, gridWindow);
	wxPoint pt = dc.GetDeviceOrigin();
	dc.SetDeviceOrigin(pt.x, pt.y - y);

	wxArrayInt rows = m_owner->CalcRowLabelsExposed(GetUpdateRegion(), gridWindow);
	m_owner->DrawRowLabels(dc, rows);

	if (IsFrozen())
		m_owner->DrawLabelFrozenBorder(dc, this, true);
}

void ibGridRowLabelWindow::OnMouseEvent(wxMouseEvent& event)
{
	m_owner->ProcessRowColLabelMouseEvent(ibGridRowOperations(), event, this);
}

void ibGridRowLabelWindow::OnMouseWheel(wxMouseEvent& event)
{
	m_owner->Freeze();

	if (event.ControlDown())
		m_owner->SetGridZoom(event.m_wheelRotation > 0 ? 1 : -1);
	else if (!m_owner->ProcessWindowEvent(event))
		event.Skip();

	m_owner->Thaw();
}

//////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibGridColAreaWindow, ibGridSubwindow)
EVT_PAINT(ibGridColAreaWindow::OnPaint)
EVT_MOUSEWHEEL(ibGridColAreaWindow::OnMouseWheel)
EVT_MOUSE_EVENTS(ibGridColAreaWindow::OnMouseEvent)
wxEND_EVENT_TABLE()

void ibGridColAreaWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);

	// NO - don't do this because it will set both the x and y origin
	// coords to match the parent scrolled window and we just want to
	// set the x coord  - MB
	//
	// m_owner->PrepareDC( dc );

	// Column indices become invalid when the grid is empty, so avoid doing
	// anything at all in this case.
	if (m_owner->GetNumberCols() == 0)
		return;

	int x, y;
	ibGridWindow* gridWindow = IsFrozen() ? m_owner->m_frozenColGridWin :
		m_owner->m_gridWin;
	m_owner->GetGridWindowOffset(gridWindow, x, y);
	m_owner->CalcGridWindowUnscrolledPosition(x, y, &x, &y, gridWindow);
	wxPoint pt = dc.GetDeviceOrigin();
	dc.SetDeviceOrigin(pt.x - x, pt.y);

	wxArrayInt cols = m_owner->CalcColLabelsExposed(GetUpdateRegion(), gridWindow);
	m_owner->DrawColAreas(dc, cols);

	if (IsFrozen())
		m_owner->DrawLabelFrozenBorder(dc, this, false);
}

void ibGridColAreaWindow::OnMouseEvent(wxMouseEvent& event)
{
	m_owner->ProcessRowColAreaMouseEvent(ibGridColumnOperations(), event, this);
}

void ibGridColAreaWindow::OnMouseWheel(wxMouseEvent& event)
{
	m_owner->Freeze();

	if (event.ControlDown())
		m_owner->SetGridZoom(event.m_wheelRotation > 0 ? 1 : -1);
	else if (!m_owner->ProcessWindowEvent(event))
		event.Skip();

	m_owner->Thaw();
}

wxBEGIN_EVENT_TABLE(ibGridColLabelWindow, ibGridSubwindow)
EVT_PAINT(ibGridColLabelWindow::OnPaint)
EVT_MOUSEWHEEL(ibGridColLabelWindow::OnMouseWheel)
EVT_MOUSE_EVENTS(ibGridColLabelWindow::OnMouseEvent)
wxEND_EVENT_TABLE()

void ibGridColLabelWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);

	// NO - don't do this because it will set both the x and y origin
	// coords to match the parent scrolled window and we just want to
	// set the x coord  - MB
	//
	// m_owner->PrepareDC( dc );

	// Column indices become invalid when the grid is empty, so avoid doing
	// anything at all in this case.
	if (m_owner->GetNumberCols() == 0)
		return;

	int x, y;
	ibGridWindow* gridWindow = IsFrozen() ? m_owner->m_frozenColGridWin :
		m_owner->m_gridWin;
	m_owner->GetGridWindowOffset(gridWindow, x, y);
	m_owner->CalcGridWindowUnscrolledPosition(x, y, &x, &y, gridWindow);
	wxPoint pt = dc.GetDeviceOrigin();
	dc.SetDeviceOrigin(pt.x - x, pt.y);

	wxArrayInt cols = m_owner->CalcColLabelsExposed(GetUpdateRegion(), gridWindow);
	m_owner->DrawColLabels(dc, cols);

	if (IsFrozen())
		m_owner->DrawLabelFrozenBorder(dc, this, false);
}

void ibGridColLabelWindow::OnMouseEvent(wxMouseEvent& event)
{
	m_owner->ProcessRowColLabelMouseEvent(ibGridColumnOperations(), event, this);
}

void ibGridColLabelWindow::OnMouseWheel(wxMouseEvent& event)
{
	m_owner->Freeze();

	if (event.ControlDown())
		m_owner->SetGridZoom(event.m_wheelRotation > 0 ? 1 : -1);
	else if (!m_owner->ProcessWindowEvent(event))
		event.Skip();

	m_owner->Thaw();
}

//////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibGridCornerLabelWindow, ibGridSubwindow)
EVT_MOUSEWHEEL(ibGridCornerLabelWindow::OnMouseWheel)
EVT_MOUSE_EVENTS(ibGridCornerLabelWindow::OnMouseEvent)
EVT_PAINT(ibGridCornerLabelWindow::OnPaint)
wxEND_EVENT_TABLE()

void ibGridCornerLabelWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);

	m_owner->DrawCornerLabel(dc);
}

void ibGridCornerLabelWindow::OnMouseEvent(wxMouseEvent& event)
{
	m_owner->ProcessCornerLabelMouseEvent(event);
}

void ibGridCornerLabelWindow::OnMouseWheel(wxMouseEvent& event)
{
	m_owner->Freeze();

	if (event.ControlDown())
		m_owner->SetGridZoom(event.m_wheelRotation > 0 ? 1 : -1);
	else if (!m_owner->ProcessWindowEvent(event))
		event.Skip();

	m_owner->Thaw();
}

//////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibGridWindow, ibGridSubwindow)
EVT_PAINT(ibGridWindow::OnPaint)
EVT_MOUSEWHEEL(ibGridWindow::OnMouseWheel)
EVT_MOUSE_EVENTS(ibGridWindow::OnMouseEvent)
EVT_KEY_DOWN(ibGridWindow::OnKeyDown)
EVT_KEY_UP(ibGridWindow::OnKeyUp)
EVT_CHAR(ibGridWindow::OnChar)
EVT_SET_FOCUS(ibGridWindow::OnFocus)
EVT_KILL_FOCUS(ibGridWindow::OnFocus)
wxEND_EVENT_TABLE()

void ibGridWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);
	m_owner->PrepareDCFor(dc, this);
	wxRegion reg = GetUpdateRegion();

	ibGridCellCoordsArray dirtyCells;

	m_owner->CalcCellsExposed(reg, dirtyCells, this);

	ibGridCellCacheArray storage;

	m_owner->DrawGridSpace(dc, this);
	m_owner->DrawGridCellArea(dc, dirtyCells, storage);
	m_owner->DrawAllGridWindowLines(dc, reg, this);

	if (m_type != ibGridWindow::ibGridWindowNormal)
		m_owner->DrawFrozenBorder(dc, this);

	m_owner->DrawBorder(dc, storage);
	m_owner->DrawHighlight(dc, dirtyCells);
}

void ibGrid::Render(wxDC& dc,
	const wxPoint& position,
	const wxSize& size,
	const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight,
	int style)
{
	wxCHECK_RET(bottomRight.GetCol() < GetNumberCols(),
		"Invalid right column");
	wxCHECK_RET(bottomRight.GetRow() < GetNumberRows(),
		"Invalid bottom row");

	// store user settings and reset later

	// remove grid selection, don't paint selection colour
	// unless we have wxGRID_DRAW_SELECTION
	ibGridSelection* selectionOrig = NULL;
	if (m_selection && !(style & wxGRID_DRAW_SELECTION))
	{
		// remove the selection temporarily, it will be restored below
		selectionOrig = m_selection;
		m_selection = NULL;
	}

	// store user device origin
	wxCoord userOriginX, userOriginY;
	dc.GetDeviceOrigin(&userOriginX, &userOriginY);

	// store user scale
	double scaleUserX, scaleUserY;
	dc.GetUserScale(&scaleUserX, &scaleUserY);

	// set defaults if necessary
	ibGridCellCoords leftTop(topLeft), rightBottom(bottomRight);
	if (leftTop.GetCol() < 0)
		leftTop.SetCol(0);
	if (leftTop.GetRow() < 0)
		leftTop.SetRow(0);
	if (rightBottom.GetCol() < 0)
		rightBottom.SetCol(GetNumberCols() - 1);
	if (rightBottom.GetRow() < 0)
		rightBottom.SetRow(GetNumberRows() - 1);

	// get grid offset, size and cell parameters
	wxPoint pointOffSet;
	wxSize sizeGrid;
	ibGridCellCoordsArray renderCells;
	wxArrayInt arrayCols;
	wxArrayInt arrayRows;

	GetRenderSizes(leftTop, rightBottom,
		pointOffSet, sizeGrid,
		renderCells,
		arrayCols, arrayRows);

	// add headers/labels to dimensions
	if (style & wxGRID_DRAW_ROWS_HEADER)
		sizeGrid.x += GetRowLabelSize();
	if (style & wxGRID_DRAW_COLS_HEADER)
		sizeGrid.y += GetColLabelSize();

	// get render start position in logical units
	wxPoint positionRender = GetRenderPosition(dc, position);

	wxCoord originX = dc.LogicalToDeviceX(positionRender.x);
	wxCoord originY = dc.LogicalToDeviceY(positionRender.y);

	dc.SetDeviceOrigin(originX, originY);

	SetRenderScale(dc, positionRender, size, sizeGrid);

	// draw row headers at specified origin
	if (GetRowLabelSize() > 0 && (style & wxGRID_DRAW_ROWS_HEADER))
	{
		if (style & wxGRID_DRAW_COLS_HEADER)
		{
			DrawCornerLabel(dc); // do only if both col and row labels drawn
			originY += dc.LogicalToDeviceYRel(GetColLabelSize());
		}

		originY -= dc.LogicalToDeviceYRel(pointOffSet.y);
		dc.SetDeviceOrigin(originX, originY);

		DrawRowLabels(dc, arrayRows);

		// reset for columns
		if (style & wxGRID_DRAW_COLS_HEADER)
			originY -= dc.LogicalToDeviceYRel(GetColLabelSize());

		originY += dc.LogicalToDeviceYRel(pointOffSet.y);
		// X offset so we don't overwrite row labels
		originX += dc.LogicalToDeviceXRel(GetRowLabelSize());
	}

	// subtract col offset where startcol > 0
	originX -= dc.LogicalToDeviceXRel(pointOffSet.x);
	// no y offset for col labels, they are at the Y origin

	// draw column labels
	if (style & wxGRID_DRAW_COLS_HEADER)
	{
		dc.SetDeviceOrigin(originX, originY);
		DrawColLabels(dc, arrayCols);
		// don't overwrite the labels, increment originY
		originY += dc.LogicalToDeviceYRel(GetColLabelSize());
	}

	// set device origin to draw grid cells and lines
	originY -= dc.LogicalToDeviceYRel(pointOffSet.y);
	dc.SetDeviceOrigin(originX, originY);

	// draw cell area background
	dc.SetBrush(GetDefaultCellBackgroundColour());
	dc.SetPen(*wxTRANSPARENT_PEN);
	// subtract headers from grid area dimensions
	wxSize sizeCells(sizeGrid);
	if (style & wxGRID_DRAW_ROWS_HEADER)
		sizeCells.x -= GetRowLabelSize();
	if (style & wxGRID_DRAW_COLS_HEADER)
		sizeCells.y -= GetColLabelSize();

	dc.DrawRectangle(pointOffSet, sizeCells);

	// draw cells
	{
		wxDCClipper clipper(dc, wxRect(pointOffSet, sizeCells));
		DrawGridCellArea(dc, renderCells);
	}

	// draw grid lines
	if (style & wxGRID_DRAW_CELL_LINES)
	{
		wxRegion regionClip(pointOffSet.x, pointOffSet.y,
			sizeCells.x, sizeCells.y);

		DrawRangeGridLines(dc, regionClip, renderCells[0], renderCells.Last());
	}

	// draw render rectangle bounding lines
	DoRenderBox(dc, style,
		pointOffSet, sizeCells,
		leftTop, rightBottom);

	// restore user setings
	dc.SetDeviceOrigin(userOriginX, userOriginY);
	dc.SetUserScale(scaleUserX, scaleUserY);

	if (selectionOrig)
	{
		m_selection = selectionOrig;
	}
}

void
ibGrid::SetRenderScale(wxDC& dc,
	const wxPoint& pos, const wxSize& size,
	const wxSize& sizeGrid)
{
	double scaleX, scaleY;
	wxSize sizeTemp;

	if (size.GetWidth() != wxDefaultSize.GetWidth()) // size.x was specified
		sizeTemp.SetWidth(size.GetWidth());
	else
		sizeTemp.SetWidth(dc.DeviceToLogicalXRel(dc.GetSize().GetWidth())
			- pos.x);

	if (size.GetHeight() != wxDefaultSize.GetHeight()) // size.y was specified
		sizeTemp.SetHeight(size.GetHeight());
	else
		sizeTemp.SetHeight(dc.DeviceToLogicalYRel(dc.GetSize().GetHeight())
			- pos.y);

	scaleX = (double)((double)sizeTemp.GetWidth() / (double)sizeGrid.GetWidth());
	scaleY = (double)((double)sizeTemp.GetHeight() / (double)sizeGrid.GetHeight());

	dc.SetUserScale(wxMin(scaleX, scaleY), wxMin(scaleX, scaleY));
}

// get grid rendered size, origin offset and fill cell arrays
void ibGrid::GetRenderSizes(const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight,
	wxPoint& pointOffSet, wxSize& sizeGrid,
	ibGridCellCoordsArray& renderCells,
	wxArrayInt& arrayCols, wxArrayInt& arrayRows) const
{
	pointOffSet.x = 0;
	pointOffSet.y = 0;
	sizeGrid.SetWidth(0);
	sizeGrid.SetHeight(0);

	int col, row;

	ibGridSizesInfo sizeinfo = GetColSizes();
	for (col = 0; col <= bottomRight.GetCol(); col++)
	{
		if (col < topLeft.GetCol())
		{
			pointOffSet.x += sizeinfo.GetSize(col);
		}
		else
		{
			for (row = topLeft.GetRow(); row <= bottomRight.GetRow(); row++)
			{
				renderCells.Add(ibGridCellCoords(row, col));
				arrayRows.Add(row); // column labels rendered in DrawColLabels
			}
			arrayCols.Add(col); // row labels rendered in DrawRowLabels
			sizeGrid.x += sizeinfo.GetSize(col);
		}
	}

	sizeinfo = GetRowSizes();
	for (row = 0; row <= bottomRight.GetRow(); row++)
	{
		if (row < topLeft.GetRow())
			pointOffSet.y += sizeinfo.GetSize(row);
		else
			sizeGrid.y += sizeinfo.GetSize(row);
	}
}

// get render start position
// if position not specified use dc draw extents MaxX and MaxY
wxPoint ibGrid::GetRenderPosition(wxDC& dc, const wxPoint& position)
{
	wxPoint positionRender(position);

	if (!positionRender.IsFullySpecified())
	{
		if (positionRender.x == wxDefaultPosition.x)
			positionRender.x = dc.MaxX();

		if (positionRender.y == wxDefaultPosition.y)
			positionRender.y = dc.MaxY();
	}

	return positionRender;
}

// draw render rectangle bounding lines
// useful where there is multi cell row or col clipping and no cell border
void ibGrid::DoRenderBox(wxDC& dc, const int& style,
	const wxPoint& pointOffSet,
	const wxSize& sizeCells,
	const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight)
{
	if (!(style & wxGRID_DRAW_BOX_RECT))
		return;

	int bottom = pointOffSet.y + sizeCells.GetY(),
		right = pointOffSet.x + sizeCells.GetX() - 1;

	// horiz top line if we are not drawing column header/labels
	if (!(style & wxGRID_DRAW_COLS_HEADER))
	{
		int left = pointOffSet.x;
		left += (style & wxGRID_DRAW_COLS_HEADER)
			? -GetRowLabelSize() : 0;
		dc.SetPen(GetRowGridLinePen(topLeft.GetRow()));
		dc.DrawLine(left,
			pointOffSet.y,
			right,
			pointOffSet.y);
	}

	// horiz bottom line
	dc.SetPen(GetRowGridLinePen(bottomRight.GetRow()));
	dc.DrawLine(pointOffSet.x, bottom - 1, right, bottom - 1);

	// left vertical line if we are not drawing row header/labels
	if (!(style & wxGRID_DRAW_ROWS_HEADER))
	{
		int top = pointOffSet.y;
		top += (style & wxGRID_DRAW_COLS_HEADER)
			? -GetColLabelSize() : 0;
		dc.SetPen(GetColGridLinePen(topLeft.GetCol()));
		dc.DrawLine(pointOffSet.x - 1,
			top,
			pointOffSet.x - 1,
			bottom - 1);
	}

	// right vertical line
	dc.SetPen(GetColGridLinePen(bottomRight.GetCol()));
	dc.DrawLine(right, pointOffSet.y, right, bottom - 1);
}

void ibGridWindow::ScrollWindow(int dx, int dy, const wxRect* rect)
{
	m_owner->ScrollWindow(dx, dy, rect);
}

void ibGrid::ScrollWindow(int dx, int dy, const wxRect* rect)
{
	// We must explicitly call wxWindow version to avoid infinite recursion as
	// ibGridWindow::ScrollWindow() calls this method back.
	m_gridWin->wxWindow::ScrollWindow(dx, dy, rect);

	if (m_frozenColGridWin)
		m_frozenColGridWin->wxWindow::ScrollWindow(0, dy, rect);
	if (m_frozenRowGridWin)
		m_frozenRowGridWin->wxWindow::ScrollWindow(dx, 0, rect);

	m_rowAreaWin->ScrollWindow(0, dy, rect);
	m_rowLabelWin->ScrollWindow(0, dy, rect);
	m_colAreaWin->ScrollWindow(dx, 0, rect);
	m_colLabelWin->ScrollWindow(dx, 0, rect);
}

void ibGridWindow::OnMouseEvent(wxMouseEvent& event)
{
	if (event.ButtonDown(wxMOUSE_BTN_LEFT) && FindFocus() != this)
		SetFocus();

	m_owner->ProcessGridCellMouseEvent(event, this);
}

void ibGridWindow::OnMouseWheel(wxMouseEvent& event)
{
	m_owner->Freeze();

	if (event.ControlDown())
		m_owner->SetGridZoom(event.m_wheelRotation > 0 ? 1 : -1);
	else if (!m_owner->ProcessWindowEvent(event))
		event.Skip();

	m_owner->Thaw();
}

// This seems to be required for wxMotif/wxGTK otherwise the mouse
// cursor must be in the cell edit control to get key events
//
void ibGridWindow::OnKeyDown(wxKeyEvent& event)
{
	if (!m_owner->ProcessWindowEvent(event))
		event.Skip();
}

void ibGridWindow::OnKeyUp(wxKeyEvent& event)
{
	if (!m_owner->ProcessWindowEvent(event))
		event.Skip();
}

void ibGridWindow::OnChar(wxKeyEvent& event)
{
	if (!m_owner->ProcessWindowEvent(event))
		event.Skip();
}

void ibGridWindow::OnFocus(wxFocusEvent& event)
{
	// and if we have any selection, it has to be repainted, because it
	// uses different colour when the grid is not focused:
	if (m_owner->IsSelection())
	{
		Refresh();
	}
	else
	{
		// NB: Note that this code is in "else" branch only because the other
		//     branch refreshes everything and so there's no point in calling
		//     Refresh() again, *not* because it should only be done if
		//     !IsSelection(). If the above code is ever optimized to refresh
		//     only selected area, this needs to be moved out of the "else"
		//     branch so that it's always executed.

		// current cell cursor {dis,re}appears on focus change:
		const ibGridCellCoords cursorCoords(m_owner->GetGridCursorRow(),
			m_owner->GetGridCursorCol());
		const wxRect cursor =
			m_owner->BlockToDeviceRect(cursorCoords, cursorCoords, this);
		if (cursor != ibGridNoCellRect)
			Refresh(true, &cursor);
	}

	if (!m_owner->ProcessWindowEvent(event))
		event.Skip();
}

// Unlike XToCol() and YToRow() these macros always return a valid column/row,
// so their results don't need to be checked, while the results of the public
// functions always must be.
#define internalXToCol(x, gridWindowPtr) XToCol(x, true, gridWindowPtr)
#define internalYToRow(y, gridWindowPtr) YToRow(y, true, gridWindowPtr)

/////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibGrid, wxScrolledCanvas)
EVT_SIZE(ibGrid::OnSize)
EVT_DPI_CHANGED(ibGrid::OnDPIChanged)
EVT_KEY_DOWN(ibGrid::OnKeyDown)
EVT_CHAR(ibGrid::OnChar)
wxEND_EVENT_TABLE()

bool ibGrid::Create(wxWindow* parent, wxWindowID id,
	const wxPoint& pos, const wxSize& size,
	long style, const wxString& name)
{
	if (!wxScrolledCanvas::Create(parent, id, pos, size,
		style | wxWANTS_CHARS, name))
		return false;

	m_colMinWidths = wxLongToLongHashMap(GRID_HASH_SIZE);
	m_rowMinHeights = wxLongToLongHashMap(GRID_HASH_SIZE);

	Create();
	SetInitialSize(size);
	CalcDimensions();

	return true;
}

ibGrid::~ibGrid()
{
	if (m_winCapture)
		m_winCapture->ReleaseMouse();

	// Ensure that the editor control is destroyed before the grid is,
	// otherwise we crash later when the editor tries to do something with the
	// half destroyed grid
	HideCellEditControl();

	// Must do this or ~wxScrollHelper will pop the wrong event handler
	SetTargetWindow(this);
	ClearAttrCache();
	wxSafeDecRef(m_defaultCellAttr);

#ifdef DEBUG_ATTR_CACHE
	size_t total = gs_nAttrCacheHits + gs_nAttrCacheMisses;
	wxPrintf(wxT("ibGrid attribute cache statistics: "
		"total: %u, hits: %u (%u%%)\n"),
		total, gs_nAttrCacheHits,
		total ? (gs_nAttrCacheHits * 100) / total : 0);
#endif

	// if we own the table, just delete it, otherwise at least don't leave it
	// with dangling view pointer
	if (m_ownTable)
		delete m_table;
	else if (m_table && m_table->GetView() == this)
		m_table->SetView(NULL);

	delete m_typeRegistry;
	delete m_selection;

	delete m_setFixedRows;
	delete m_setFixedCols;
}

//
// ----- internal init and update functions
//

// NOTE: If using the default visual attributes works everywhere then this can
// be removed as well as the #else cases below.
#define _USE_VISATTR 0

void ibGrid::Create()
{
	// create the type registry
	m_typeRegistry = new ibGridTypeRegistry;

	m_cellEditCtrlEnabled = false;

	m_defaultCellAttr = new ibGridCellAttr();

	// Set default cell attributes
	m_defaultCellAttr->SetDefAttr(m_defaultCellAttr);
	m_defaultCellAttr->SetKind(ibGridCellAttr::Default);
	m_defaultCellAttr->SetFont(GetFont());
	m_defaultCellAttr->SetAlignment(wxALIGN_LEFT, wxALIGN_TOP);
	m_defaultCellAttr->SetRenderer(new ibGridCellStringRenderer);
	m_defaultCellAttr->SetEditor(new ibGridCellTextEditor);
	m_defaultCellAttr->SetFitMode(ibGridFitMode::Overflow());

#if _USE_VISATTR
	wxVisualAttributes gva = wxListBox::GetClassDefaultAttributes();
	wxVisualAttributes lva = wxPanel::GetClassDefaultAttributes();

	m_defaultCellAttr->SetTextColour(gva.colFg);
	m_defaultCellAttr->SetBackgroundColour(gva.colBg);

#else
	m_defaultCellAttr->SetTextColour(
		wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	m_defaultCellAttr->SetBackgroundColour(
		wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

	m_defaultCellAttr->SetTextOrient(wxHORIZONTAL);

	m_numRows = 0;
	m_numCols = 0;
	m_numFrozenRows = 0;
	m_numFrozenCols = 0;
	m_currentCellCoords = ibGridNoCellCoords;

	// subwindow components that make up the ibGrid
#pragma region area
	m_rowAreaWin = new ibGridRowAreaWindow(this);
#pragma endregion 
	m_rowLabelWin = new ibGridRowLabelWindow(this);

	CreateColumnWindow();
	m_cornerLabelWin = new ibGridCornerLabelWindow(this);
	m_gridWin = new ibGridWindow(this, ibGridWindow::ibGridWindowNormal);

	SetTargetWindow(m_gridWin);

#if _USE_VISATTR
	wxColour gfg = gva.colFg;
	wxColour gbg = gva.colBg;
	wxColour lfg = lva.colFg;
	wxColour lbg = lva.colBg;
#else
	wxColour gfg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	wxColour gbg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	wxColour lfg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	wxColour lbg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
#endif

	m_cornerLabelWin->SetOwnForegroundColour(lfg);
	m_cornerLabelWin->SetOwnBackgroundColour(lbg);

	m_rowAreaWin->SetOwnForegroundColour(lfg);
	m_rowAreaWin->SetOwnBackgroundColour(lbg);
	m_rowLabelWin->SetOwnForegroundColour(lfg);
	m_rowLabelWin->SetOwnBackgroundColour(lbg);
	m_colAreaWin->SetOwnForegroundColour(lfg);
	m_colAreaWin->SetOwnBackgroundColour(lbg);
	m_colLabelWin->SetOwnForegroundColour(lfg);
	m_colLabelWin->SetOwnBackgroundColour(lbg);

	m_gridWin->SetOwnForegroundColour(gfg);
	m_gridWin->SetOwnBackgroundColour(gbg);

	m_labelBackgroundColour = m_rowLabelWin->GetBackgroundColour();
	m_labelTextColour = m_rowLabelWin->GetForegroundColour();

	InitPixelFields();
}

void ibGrid::InitPixelFields()
{
	m_defaultRowHeight = m_gridWin->GetCharHeight();
#if defined(__WXMOTIF__) || defined(__WXGTK__) || defined(__WXQT__)  // see also text ctrl sizing in ShowCellEditControl()
	m_defaultRowHeight += 8;
#else
	m_defaultRowHeight += 4;
#endif

	// Don't change the value when called from OnDPIChanged() later if the
	// corresponding label window is hidden, these values should remain zeroes
	// then.

	if (m_rowAreaWin->IsShown())
		m_rowAreaWidth = FromDIP(WXGRID_DEFAULT_ROW_LABEL_WIDTH);

	if (m_rowLabelWin->IsShown())
		m_rowLabelWidth = FromDIP(WXGRID_DEFAULT_ROW_LABEL_WIDTH);

	if (m_colAreaWin->IsShown())
		m_colAreaHeight = FromDIP(WXGRID_DEFAULT_COL_LABEL_HEIGHT);

	if (m_colLabelWin->IsShown())
		m_colLabelHeight = FromDIP(WXGRID_DEFAULT_COL_LABEL_HEIGHT);

	m_defaultColWidth = FromDIP(WXGRID_DEFAULT_COL_WIDTH);

	m_minAcceptableColWidth = FromDIP(WXGRID_MIN_COL_WIDTH);
	m_minAcceptableRowHeight = FromDIP(WXGRID_MIN_ROW_HEIGHT);
}

void ibGrid::CreateColumnWindow()
{
	if (m_useNativeHeader)
	{
		m_colAreaWin = new ibGridColAreaWindow(this);
		m_colAreaHeight = m_colAreaWin->GetBestSize().y;
		m_colLabelWin = new ibGridHeaderCtrl(this);
		m_colLabelHeight = m_colLabelWin->GetBestSize().y;
	}
	else // draw labels ourselves
	{
		m_colAreaWin = new ibGridColAreaWindow(this);
		m_colAreaHeight = FromDIP(WXGRID_DEFAULT_COL_LABEL_HEIGHT);
		m_colLabelWin = new ibGridColLabelWindow(this);
		m_colLabelHeight = FromDIP(WXGRID_DEFAULT_COL_LABEL_HEIGHT);
	}
}

bool ibGrid::CreateGrid(int numRows, int numCols,
	ibGridSelectionModes selmode)
{
	wxCHECK_MSG(!m_created,
		false,
		wxT("ibGrid::CreateGrid or ibGrid::SetTable called more than once"));

	return SetTable(new ibGridStringTable(numRows, numCols), true, selmode);
}

void ibGrid::SetSelectionMode(ibGridSelectionModes selmode)
{
	wxCHECK_RET(m_created,
		wxT("Called ibGrid::SetSelectionMode() before calling CreateGrid()"));

	m_selection->SetSelectionMode(selmode);
}

ibGrid::ibGridSelectionModes ibGrid::GetSelectionMode() const
{
	wxCHECK_MSG(m_created, ibGridSelectCells,
		wxT("Called ibGrid::GetSelectionMode() before calling CreateGrid()"));

	return m_selection->GetSelectionMode();
}

bool
ibGrid::SetTable(ibGridTableBase* table,
	bool takeOwnership,
	ibGrid::ibGridSelectionModes selmode)
{
	if (m_created)
	{
		// stop all processing
		m_created = false;

		if (m_table)
		{
			// We can't leave the in-place control editing the data of the
			// table alive, as it would try to use the table object that we
			// don't have any more later otherwise, so hide it manually.
			//
			// Notice that we can't call DisableCellEditControl() from here
			// which would try to save the current editor value into the table
			// which might be half-deleted by now, so we have to manually mark
			// the edit control as being disabled.
			HideCellEditControl();
			m_cellEditCtrlEnabled = false;

			// Don't hold on to attributes cached from the old table
			ClearAttrCache();

			m_table->SetView(NULL);
			if (m_ownTable)
				delete m_table;
			m_table = NULL;
		}

		m_undoStack.clear();
		m_redoStack.clear();

		wxDELETE(m_selection);

		m_ownTable = false;
		m_numRows = 0;
		m_numCols = 0;
		m_numFrozenRows = 0;
		m_numFrozenCols = 0;

		// kill row and column size arrays
		m_colWidths.Empty();
		m_rowHeights.Empty();
	}

	if (table)
	{
		m_numRows = table->GetNumberRows();
		m_numCols = table->GetNumberCols();

		m_table = table;
		m_table->SetView(this);
		m_ownTable = takeOwnership;

		// Notice that this must be called after setting m_table as it uses it
		// indirectly, via ibGrid::GetColLabelValue().
		if (m_useNativeHeader)
			SetNativeHeaderColCount();

		m_selection = new ibGridSelection(this, selmode);
		CalcDimensions();

		m_created = true;
	}

	InvalidateBestSize();

	UpdateCurrentCellOnRedim();

	return m_created;
}

void ibGrid::AssignTable(ibGridTableBase* table, ibGridSelectionModes selmode)
{
	wxCHECK_RET(table, wxS("Table pointer must be valid"));
	wxCHECK_RET(!m_created, wxS("ibGrid already has a table"));

	SetTable(table, true /* take ownership */, selmode);
}

void ibGrid::Init()
{
	m_created = false;

	m_cornerLabelWin = NULL;
	m_rowAreaWin = NULL;
	m_rowFrozenAreaWin = NULL;
	m_rowLabelWin = NULL;
	m_rowFrozenLabelWin = NULL;
	m_colAreaWin = NULL;
	m_colFrozenAreaWin = NULL;
	m_colLabelWin = NULL;
	m_colFrozenLabelWin = NULL;
	m_gridWin = NULL;
	m_frozenColGridWin = NULL;
	m_frozenRowGridWin = NULL;
	m_frozenCornerGridWin = NULL;

	m_table = NULL;
	m_ownTable = false;

	m_selection = NULL;
	m_defaultCellAttr = NULL;
	m_typeRegistry = NULL;

	m_setFixedRows =
		m_setFixedCols = NULL;

	// init attr cache
	m_attrCache.row = -1;
	m_attrCache.col = -1;
	m_attrCache.attr = NULL;

	m_labelFont = GetFont();
	//m_labelFont.SetWeight(wxFONTWEIGHT_BOLD);

	m_rowLabelHorizAlign = wxALIGN_CENTRE;
	m_rowLabelVertAlign = wxALIGN_CENTRE;

	m_colLabelHorizAlign = wxALIGN_CENTRE;
	m_colLabelVertAlign = wxALIGN_CENTRE;
	m_colLabelTextOrientation = wxHORIZONTAL;

	m_cornerLabelHorizAlign = wxALIGN_CENTRE;
	m_cornerLabelVertAlign = wxALIGN_CENTRE;
	m_cornerLabelTextOrientation = wxHORIZONTAL;

	// All these fields require a valid window, so are initialized in Create().
	m_rowAreaWidth =
		m_rowLabelWidth = m_colAreaHeight = m_colLabelHeight = 0;

	m_defaultColWidth =
		m_defaultRowHeight = 0;

	m_minAcceptableColWidth =
		m_minAcceptableRowHeight = 0;

	m_zoomScale = 1.0f;

	m_gridLineColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
	m_gridLinesEnabled = true;
	m_gridLinesClipHorz =
		m_gridLinesClipVert = true;
	m_cellHighlightColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	m_cellHighlightPenWidth = 2;
	m_cellHighlightROPenWidth = 1;
	m_gridFrozenBorderColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE); //wxSystemSettings::SelectLightDark(*wxBLACK, *wxWHITE);
	m_gridFrozenBorderPenWidth = 0;

	m_canDragRowMove = false;
	m_canDragColMove = false;
	m_canHideColumns = true;

	m_cursorMode = WXGRID_CURSOR_SELECT_CELL;
	m_winCapture = NULL;
	m_canDragRowSize = true;
	m_canDragColSize = true;
	m_canDragGridSize = true;
	m_canDragCell = false;
	m_dragMoveRowOrCol = -1;
	m_dragLastPos = -1;
	m_dragLastColour = NULL;
	m_dragRowOrCol = -1;
	m_dragRowOrColOldSize = -1;
	m_isDragging = false;
	m_cancelledDragging = false;
	m_startDragPos = wxDefaultPosition;
	m_lastMousePos = wxDefaultPosition;

	m_sortCol = wxNOT_FOUND;
	m_sortIsAscending = true;

	m_useNativeHeader =
		m_nativeColumnLabels = false;

	m_waitForSlowClick = false;

	m_rowResizeCursor = wxCursor(wxCURSOR_SIZENS);
	m_colResizeCursor = wxCursor(wxCURSOR_SIZEWE);

	m_currentCellCoords = ibGridNoCellCoords;

	m_selectionBackground = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
	m_selectionForeground = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);

	m_editable = true;  // default for whole grid
	m_areaEnabled = true;  // default for area grid

	m_batchCount = 0;

	m_extraWidth =
		m_extraHeight = 0;

	// we can't call SetScrollRate() as the window isn't created yet but OTOH
	// we don't need to call it either as the scroll position is (0, 0) right
	// now anyhow, so just set the parameters directly
	m_xScrollPixelsPerLine = GRID_SCROLL_LINE_X;
	m_yScrollPixelsPerLine = GRID_SCROLL_LINE_Y;

	m_tabBehaviour = Tab_Stop;
}

// ----------------------------------------------------------------------------
// the idea is to call these functions only when necessary because they create
// quite big arrays which eat memory mostly unnecessary - in particular, if
// default widths/heights are used for all rows/columns, we may not use these
// arrays at all
//
// with some extra code, it should be possible to only store the widths/heights
// different from default ones (resulting in space savings for huge grids) but
// this is not done currently
// ----------------------------------------------------------------------------

void ibGrid::InitRowHeights()
{
	m_rowHeights.Empty();
	m_rowHeights.Alloc(m_numRows);
	m_rowHeights.Add(m_defaultRowHeight, m_numRows);
}

void ibGrid::InitColWidths()
{
	m_colWidths.Empty();
	m_colWidths.Alloc(m_numCols);
	m_colWidths.Add(m_defaultColWidth, m_numCols);
}

int ibGrid::GetColWidth(int col, float scale) const
{
	if (m_colWidths.IsEmpty())
		return ibCalcGridScale(m_defaultColWidth, scale);

	const int width = m_colWidths[col];

	// a negative width indicates a hidden column
	return width > 0 ? ibCalcGridScale(width, scale) : 0;
}

int ibGrid::GetColLeft(int col, float scale) const
{
	if (m_colWidths.IsEmpty())
		return GetColPos(col) * ibCalcGridScale(m_defaultColWidth, scale);

	int total_width = 0;

	for (int idx = 0; idx < col; idx++)
	{
		const int& width = *(m_colWidths.begin() + idx);
		if (width > 0) total_width += ibCalcGridScale(width, scale);
	}

	return total_width;
}

int ibGrid::GetColRight(int col, float scale) const
{
	if (m_colWidths.IsEmpty())
		return (GetColPos(col) + 1) * ibCalcGridScale(m_defaultColWidth, scale);

	int total_width = 0;

	for (int idx = 0; idx <= col; idx++)
	{
		const int& width = *(m_colWidths.begin() + idx);
		if (width > 0) total_width += ibCalcGridScale(width, scale);
	}

	return total_width;
}

int ibGrid::GetRowHeight(int row, float scale) const
{
	// no custom heights / hidden rows
	if (m_rowHeights.IsEmpty())
		return ibCalcGridScale(m_defaultRowHeight, scale);

	const int height = m_rowHeights[row];

	// a negative height indicates a hidden row
	return height > 0 ? ibCalcGridScale(height, scale) : 0;
}

int ibGrid::GetRowTop(int row, float scale) const
{
	if (m_rowHeights.IsEmpty())
		return GetRowPos(row) * ibCalcGridScale(m_defaultRowHeight, scale);

	int total_height = 0;

	for (int idx = 0; idx < row; idx++)
	{
		const int& height = *(m_rowHeights.begin() + idx);
		if (height > 0) total_height += ibCalcGridScale(height, scale);
	}

	return total_height;
}

int ibGrid::GetRowBottom(int row, float scale) const
{
	if (m_rowHeights.IsEmpty())
		return (GetRowPos(row) + 1) * ibCalcGridScale(m_defaultRowHeight, scale);

	int total_height = 0;

	for (int idx = 0; idx <= row; idx++)
	{
		const int& height = *(m_rowHeights.begin() + idx);
		if (height > 0) total_height += ibCalcGridScale(height, scale);
	}

	return total_height;
}

void ibGrid::CalcDimensions()
{
	// Wait until the window is thawed if it's currently frozen.
	if (GetBatchCount())
		return;

	// if our OnSize() hadn't been called (it would if we have scrollbars), we
	// still must reposition the children
	CalcWindowSizes();

	// compute the size of the scrollable area
	int w = m_numCols > 0 ? GetColRight(GetColAt(m_numCols - 1), GetGridZoom()) : 0;
	int h = m_numRows > 0 ? GetRowBottom(m_numRows - 1, GetGridZoom()) : 0;

	w += m_extraWidth;
	h += m_extraHeight;

	// take into account editor if shown
	if (IsCellEditControlShown())
	{
		const wxRect rect = GetCurrentCellEditorPtr()->GetWindow()->GetRect();
		if (rect.GetRight() > w)
			w = rect.GetRight();
		if (rect.GetBottom() > h)
			h = rect.GetBottom();
	}

	wxPoint offset = GetGridWindowOffset(m_gridWin);
	w -= offset.x;
	h -= offset.y;

	// preserve (more or less) the previous position
	int x, y;
	GetViewStart(&x, &y);

	// ensure the position is valid for the new scroll ranges
	if (x >= w)
		x = wxMax(w - 1, 0);
	if (y >= h)
		y = wxMax(h - 1, 0);

	// update the virtual size and refresh the scrollbars to reflect it
	m_gridWin->SetVirtualSize(w, h);
	Scroll(x, y);
	AdjustScrollbars();
}

wxSize ibGrid::GetSizeAvailableForScrollTarget(const wxSize& size)
{
	wxPoint offset = GetGridWindowOffset(m_gridWin);
	wxSize sizeGridWin(size);
	sizeGridWin.x -= (GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + offset.x;
	sizeGridWin.y -= (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) + offset.y;

	return sizeGridWin;
}

void ibGrid::CalcWindowSizes()
{
	// escape if the window is has not been fully created yet
	if (m_cornerLabelWin == NULL)
		return;

	int cw, ch;
	GetClientSize(&cw, &ch);

	// frozen rows and cols windows size
	int fgw = 0, fgh = 0;

	for (int i = 0; i < m_numFrozenRows; i++)
		fgh += GetRowHeight(i, GetGridZoom());

	for (int i = 0; i < m_numFrozenCols; i++)
		fgw += GetColWidth(i, GetGridZoom());

	const int rowAreaWidth =
		(GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0);

	const int colAreaHeight =
		(GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0);

	// the grid may be too small to have enough space for the labels yet, don't
	// size the windows to negative sizes in this case
	int gw = cw - ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) - fgw;
	int gh = ch - ibCalcGridScale(m_colLabelHeight, GetGridZoom()) - fgh;

	if (gw < 0)
		gw = 0;
	if (gh < 0)
		gh = 0;

	if (m_frozenCornerGridWin && m_frozenCornerGridWin->IsShown())
		m_frozenCornerGridWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()), fgw, fgh);

	if (m_cornerLabelWin && m_cornerLabelWin->IsShown())
		m_cornerLabelWin->SetSize(rowAreaWidth, colAreaHeight, ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), ibCalcGridScale(m_colLabelHeight, GetGridZoom()));

	if (m_colAreaWin && m_colAreaWin->IsShown())
		m_colAreaWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + fgw, 0, gw, colAreaHeight);

	if (m_colFrozenAreaWin && m_colFrozenAreaWin->IsShown())
		m_colFrozenAreaWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), 0, rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + fgw, colAreaHeight);

	if (m_colFrozenLabelWin && m_colFrozenLabelWin->IsShown())
		m_colFrozenLabelWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), colAreaHeight, fgw, ibCalcGridScale(m_colLabelHeight, GetGridZoom()));

	if (m_frozenColGridWin && m_frozenColGridWin->IsShown())
		m_frozenColGridWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) + fgh, fgw, gh);

	if (m_colLabelWin && m_colLabelWin->IsShown())
		m_colLabelWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + fgw, colAreaHeight, gw, ibCalcGridScale(m_colLabelHeight, GetGridZoom()));

	if (m_rowAreaWin && m_rowAreaWin->IsShown())
		m_rowAreaWin->SetSize(0, colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) + fgh, rowAreaWidth, gh);

	if (m_rowFrozenAreaWin && m_rowFrozenAreaWin->IsShown())
		m_rowFrozenAreaWin->SetSize(0, colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()), rowAreaWidth, fgh);

	if (m_rowFrozenLabelWin && m_rowFrozenLabelWin->IsShown())
		m_rowFrozenLabelWin->SetSize(rowAreaWidth, colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()), ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), fgh);

	if (m_rowLabelWin && m_rowLabelWin->IsShown())
		m_rowLabelWin->SetSize(rowAreaWidth, colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) + fgh, ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), gh);

	if (m_frozenRowGridWin && m_frozenRowGridWin->IsShown())
		m_frozenRowGridWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + fgw, colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()), gw, fgh);

	if (m_gridWin && m_gridWin->IsShown())
		m_gridWin->SetSize(rowAreaWidth + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + fgw, colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) + fgh, gw, gh);
}

// this is called when the grid table sends a message
// to indicate that it has been redimensioned
//
bool ibGrid::Redimension(ibGridTableMessage& msg)
{
	int i;
	bool result = false;

	// Clear the attribute cache as the attribute might refer to a different
	// cell than stored in the cache after adding/removing rows/columns.
	ClearAttrCache();

	// By the same reasoning, the editor should be dismissed if columns are
	// added or removed. And for consistency, it should IMHO always be
	// removed, not only if the cell "underneath" it actually changes.
	// For now, I intentionally do not save the editor's content as the
	// cell it might want to save that stuff to might no longer exist.
	HideCellEditControl();

	switch (msg.GetId())
	{
	case wxGRIDTABLE_NOTIFY_ROWS_INSERTED:
	{
		size_t pos = msg.GetCommandInt();
		int numRows = msg.GetCommandInt2();
		m_numRows += numRows;

		if (!m_rowAt.IsEmpty())
		{
			//Shift the row IDs
			for (i = 0; i < m_numRows - numRows; i++)
			{
				if (m_rowAt[i] >= (int)pos)
					m_rowAt[i] += numRows;
			}

			m_rowAt.Insert(pos, pos, numRows);

			//Set the new rows' positions
			for (i = pos + 1; i < (int)pos + numRows; i++)
			{
				m_rowAt[i] = i;
			}
		}

		if (!m_rowHeights.IsEmpty())
			m_rowHeights.Insert(m_defaultRowHeight, pos, numRows);

		UpdateCurrentCellOnRedim();

		if (m_selection)
			m_selection->UpdateRows(pos, numRows);
		ibGridCellAttrProvider* attrProvider = m_table->GetAttrProvider();
		if (attrProvider)
			attrProvider->UpdateAttrRows(pos, numRows);

		CalcDimensions();

		if (ShouldRefresh()) {
			m_rowAreaWin->Refresh();
			m_rowLabelWin->Refresh();
		}
	}
	result = true;
	break;

	case wxGRIDTABLE_NOTIFY_ROWS_APPENDED:
	{
		int numRows = msg.GetCommandInt();
		int oldNumRows = m_numRows;
		m_numRows += numRows;

		if (!m_rowAt.IsEmpty())
		{
			m_rowAt.Add(0, numRows);

			//Set the new rows' positions
			for (i = oldNumRows; i < m_numRows; i++)
			{
				m_rowAt[i] = i;
			}
		}

		if (!m_rowHeights.IsEmpty())
			m_rowHeights.Add(m_defaultRowHeight, numRows);

		UpdateCurrentCellOnRedim();

		CalcDimensions();

		if (ShouldRefresh()) {
			m_rowAreaWin->Refresh();
			m_rowLabelWin->Refresh();
		}
	}
	result = true;
	break;

	case wxGRIDTABLE_NOTIFY_ROWS_DELETED:
	{
		size_t pos = msg.GetCommandInt();
		int numRows = msg.GetCommandInt2();
		m_numRows -= numRows;

		if (!m_rowAt.IsEmpty())
		{
			int rowID = GetRowAt(pos);

			m_rowAt.RemoveAt(pos, numRows);

			//Shift the row IDs
			int rowPos;
			for (rowPos = 0; rowPos < m_numRows; rowPos++)
			{
				if (m_rowAt[rowPos] > rowID)
					m_rowAt[rowPos] -= numRows;
			}
		}

		if (!m_rowHeights.IsEmpty())
			m_rowHeights.RemoveAt(pos, numRows);

		UpdateCurrentCellOnRedim();

		if (m_selection)
			m_selection->UpdateRows(pos, -((int)numRows));
		ibGridCellAttrProvider* attrProvider = m_table->GetAttrProvider();
		if (attrProvider)
		{
			attrProvider->UpdateAttrRows(pos, -((int)numRows));

			// ifdef'd out following patch from Paul Gammans
#if 0
				// No need to touch column attributes, unless we
				// removed _all_ rows, in this case, we remove
				// all column attributes.
				// I hate to do this here, but the
				// needed data is not available inside UpdateAttrRows.
			if (!GetNumberRows())
				attrProvider->UpdateAttrCols(0, -GetNumberCols());
#endif
		}

		CalcDimensions();

		if (ShouldRefresh()) {
			m_rowAreaWin->Refresh();
			m_rowLabelWin->Refresh();
		}
	}
	result = true;
	break;

	case wxGRIDTABLE_NOTIFY_COLS_INSERTED:
	{
		size_t pos = msg.GetCommandInt();
		int numCols = msg.GetCommandInt2();
		m_numCols += numCols;

		if (!m_colAt.IsEmpty())
		{
			//Shift the column IDs
			for (i = 0; i < m_numCols - numCols; i++)
			{
				if (m_colAt[i] >= (int)pos)
					m_colAt[i] += numCols;
			}

			m_colAt.Insert(pos, pos, numCols);

			//Set the new columns' positions
			for (i = pos + 1; i < (int)pos + numCols; i++)
			{
				m_colAt[i] = i;
			}
		}

		if (!m_colWidths.IsEmpty())
			m_colWidths.Insert(m_defaultColWidth, pos, numCols);

		// See comment for wxGRIDTABLE_NOTIFY_COLS_APPENDED case explaining
		// why this has to be done here and not before.
		if (m_useNativeHeader)
			GetGridColHeader()->SetColumnCount(m_numCols);

		UpdateCurrentCellOnRedim();

		if (m_selection)
			m_selection->UpdateCols(pos, numCols);
		ibGridCellAttrProvider* attrProvider = m_table->GetAttrProvider();
		if (attrProvider)
			attrProvider->UpdateAttrCols(pos, numCols);

		CalcDimensions();

		if (ShouldRefresh()) {
			m_colAreaWin->Refresh();
			m_colLabelWin->Refresh();
		}
	}
	result = true;
	break;

	case wxGRIDTABLE_NOTIFY_COLS_APPENDED:
	{
		int numCols = msg.GetCommandInt();
		int oldNumCols = m_numCols;
		m_numCols += numCols;

		if (!m_colAt.IsEmpty())
		{
			m_colAt.Add(0, numCols);

			//Set the new columns' positions
			for (i = oldNumCols; i < m_numCols; i++)
			{
				m_colAt[i] = i;
			}
		}

		if (!m_colWidths.IsEmpty())
			m_colWidths.Add(m_defaultColWidth, numCols);

		// Notice that this must be called after updating m_colWidths above
		// as the native grid control will check whether the new columns
		// are shown which results in accessing m_colWidths array.
		if (m_useNativeHeader)
			GetGridColHeader()->SetColumnCount(m_numCols);

		UpdateCurrentCellOnRedim();

		CalcDimensions();

		if (ShouldRefresh()) {
			m_colAreaWin->Refresh();
			m_colLabelWin->Refresh();
		}
	}
	result = true;
	break;

	case wxGRIDTABLE_NOTIFY_COLS_DELETED:
	{
		size_t pos = msg.GetCommandInt();
		int numCols = msg.GetCommandInt2();
		m_numCols -= numCols;

		if (!m_colAt.IsEmpty())
		{
			int colID = GetColAt(pos);

			m_colAt.RemoveAt(pos, numCols);

			//Shift the column IDs
			int colPos;
			for (colPos = 0; colPos < m_numCols; colPos++)
			{
				if (m_colAt[colPos] > colID)
					m_colAt[colPos] -= numCols;
			}
		}

		if (!m_colWidths.IsEmpty())
			m_colWidths.RemoveAt(pos, numCols);

		// See comment for wxGRIDTABLE_NOTIFY_COLS_APPENDED case explaining
		// why this has to be done here and not before.
		if (m_useNativeHeader)
			GetGridColHeader()->SetColumnCount(m_numCols);

		UpdateCurrentCellOnRedim();

		if (m_selection)
			m_selection->UpdateCols(pos, -((int)numCols));
		ibGridCellAttrProvider* attrProvider = m_table->GetAttrProvider();
		if (attrProvider)
		{
			attrProvider->UpdateAttrCols(pos, -((int)numCols));

			// ifdef'd out following patch from Paul Gammans
#if 0
				// No need to touch row attributes, unless we
				// removed _all_ columns, in this case, we remove
				// all row attributes.
				// I hate to do this here, but the
				// needed data is not available inside UpdateAttrCols.
			if (!GetNumberCols())
				attrProvider->UpdateAttrRows(0, -GetNumberRows());
#endif
		}

		CalcDimensions();

		if (ShouldRefresh()) {
			m_colAreaWin->Refresh();
			m_colLabelWin->Refresh();
		}
	}
	result = true;
	break;
	}

	InvalidateBestSize();

	if (result && ShouldRefresh())
		Refresh();

	return result;
}

wxArrayInt ibGrid::CalcRowLabelsExposed(const wxRegion& reg, ibGridWindow* gridWindow) const
{
	wxRect r;

	wxArrayInt  rowlabels;

	int top, bottom;
	for (wxRegionIterator iter(reg); iter; ++iter)
	{
		r = iter.GetRect();
		r.Offset(GetGridWindowOffset(gridWindow));

		// TODO: remove this when we can...
		// There is a bug in wxMotif that gives garbage update
		// rectangles if you jump-scroll a long way by clicking the
		// scrollbar with middle button.  This is a work-around
		//
#if defined(__WXMOTIF__)
		int cw, ch;
		m_gridWin->GetClientSize(&cw, &ch);
		if (r.GetTop() > ch)
			r.SetTop(0);
		r.SetBottom(wxMin(r.GetBottom(), ch));
#endif

		// logical bounds of update region
		//
		int dummy;
		CalcGridWindowUnscrolledPosition(0, r.GetTop(), &dummy, &top, gridWindow);
		CalcGridWindowUnscrolledPosition(0, r.GetBottom(), &dummy, &bottom, gridWindow);

		// find the row labels within these bounds
		const int rowFirst = internalYToRow(top, gridWindow);
		if (rowFirst == wxNOT_FOUND)
			continue;

		int rowPos = GetRowPos(rowFirst);

		rowlabels.Alloc(m_numRows - rowPos);

		for (; rowPos < m_numRows; rowPos++)
		{
			int row;
			row = GetRowAt(rowPos);

			if (GetRowBottom(row, GetGridZoom()) < top)
				continue;

			if (GetRowTop(row, GetGridZoom()) > bottom)
				break;

			rowlabels.Add(row);
		}
	}

	return rowlabels;
}

wxArrayInt ibGrid::CalcColLabelsExposed(const wxRegion& reg, ibGridWindow* gridWindow) const
{
	wxRect r;

	wxArrayInt colLabels;

	int left, right;
	for (wxRegionIterator iter(reg); iter; ++iter)
	{
		r = iter.GetRect();
		r.Offset(GetGridWindowOffset(gridWindow));

		// TODO: remove this when we can...
		// There is a bug in wxMotif that gives garbage update
		// rectangles if you jump-scroll a long way by clicking the
		// scrollbar with middle button.  This is a work-around
		//
#if defined(__WXMOTIF__)
		int cw, ch;
		m_gridWin->GetClientSize(&cw, &ch);
		if (r.GetLeft() > cw)
			r.SetLeft(0);
		r.SetRight(wxMin(r.GetRight(), cw));
#endif

		// logical bounds of update region
		//
		int dummy;
		CalcGridWindowUnscrolledPosition(r.GetLeft(), 0, &left, &dummy, gridWindow);
		CalcGridWindowUnscrolledPosition(r.GetRight(), 0, &right, &dummy, gridWindow);

		// find the cells within these bounds
		//
		const int colFirst = internalXToCol(left, gridWindow);
		if (colFirst == wxNOT_FOUND)
			continue;

		int colPos = GetColPos(colFirst);

		colLabels.Alloc(m_numCols - colPos);

		for (; colPos < m_numCols; colPos++)
		{
			int col;
			col = GetColAt(colPos);

			if (GetColRight(col, GetGridZoom()) < left)
				continue;

			if (GetColLeft(col, GetGridZoom()) > right)
				break;

			colLabels.Add(col);
		}
	}

	return colLabels;
}

void ibGrid::CalcCellsExposed(const wxRegion& reg,
	ibGridCellCoordsArray& cellsExposed, ibGridWindow* gridWindow) const
{
	wxRect r;

	int left, top, right, bottom;
	for (wxRegionIterator iter(reg); iter; ++iter)
	{
		r = iter.GetRect();
		r.Offset(GetGridWindowOffset(gridWindow));

		// Skip 0-height cells, they're invisible anyhow, don't waste time
		// getting their rectangles and so on.
		if (!r.GetHeight())
			continue;

		// TODO: remove this when we can...
		// There is a bug in wxMotif that gives garbage update
		// rectangles if you jump-scroll a long way by clicking the
		// scrollbar with middle button.  This is a work-around
		//
#if defined(__WXMOTIF__)
		if (gridWindow)
		{
			int cw, ch;
			gridWindow->GetClientSize(&cw, &ch);
			if (r.GetTop() > ch) r.SetTop(0);
			if (r.GetLeft() > cw) r.SetLeft(0);
			r.SetRight(wxMin(r.GetRight(), cw));
			r.SetBottom(wxMin(r.GetBottom(), ch));
		}
#endif

		// logical bounds of update region
		//
		CalcGridWindowUnscrolledPosition(r.GetLeft(), r.GetTop(), &left, &top, gridWindow);
		CalcGridWindowUnscrolledPosition(r.GetRight(), r.GetBottom(), &right, &bottom, gridWindow);

		// find the cells within these bounds
		//
		const int rowFirst = internalYToRow(top, gridWindow);
		if (rowFirst == wxNOT_FOUND)
			continue;

		wxArrayInt cols;

		for (int rowPos = GetRowPos(rowFirst); rowPos < m_numRows; rowPos++)
		{
			const int row = GetRowAt(rowPos);

			if (GetRowBottom(row, GetGridZoom()) <= top)
				continue;

			if (GetRowTop(row, GetGridZoom()) > bottom)
				break;

			// add all dirty cells in this row: notice that the columns which
			// are dirty don't depend on the row so we compute them only once
			// for the first dirty row and then reuse for all the next ones
			if (cols.empty())
			{
				int pos = XToPos(left, gridWindow);
				int end = XToPos(right, gridWindow);

				cols.Alloc(end - pos);

				// do determine the dirty columns
				for (; pos <= end; pos++)
					cols.push_back(GetColAt(pos));

				// if there are no dirty columns at all, nothing to do
				if (cols.empty())
					break;
			}

			const size_t count = cols.size();
			cellsExposed.Alloc(count);

			for (size_t n = 0; n < count; n++) {
				cellsExposed.Add(ibGridCellCoords(row, cols[n]));
			}
		}
	}
}

void ibGrid::PrepareDCFor(wxDC& dc, ibGridWindow* gridWindow)
{
	wxScrolledCanvas::PrepareDC(dc);

	wxPoint dcOrigin = dc.GetDeviceOrigin() - GetGridWindowOffset(gridWindow);

	if (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol)
		dcOrigin.x = 0;
	if (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow)
		dcOrigin.y = 0;

	dc.SetDeviceOrigin(dcOrigin.x, dcOrigin.y);
}

void ibGrid::CheckDoDragScroll(ibGridSubwindow* eventGridWindow, ibGridSubwindow* gridWindow,
	wxPoint posEvent, int direction)
{
	// helper for Process{Row|Col}LabelMouseEvent, ProcessGridCellMouseEvent:
	//  scroll when at the edges or outside the window w. respect to direction
	// eventGridWindow: the window that received the mouse event
	// gridWindow: the same or the corresponding non-frozen window

	if (!m_isDragging)
	{
		// drag is just starting
		m_lastMousePos = posEvent;
		return;
	}

	int w, h;
	eventGridWindow->GetSize(&w, &h);
	// ViewStart is scroll position in scroll units
	wxPoint scrollPos = GetViewStart();
	wxPoint newScrollPos = wxPoint(wxDefaultCoord, wxDefaultCoord);

	if (direction & wxHORIZONTAL)
	{
		// check x direction
		if (eventGridWindow->IsFrozen() && posEvent.x < w)
		{
			// in the frozen window, moving left?
			if (scrollPos.x > 0 && posEvent.x < m_lastMousePos.x)
				newScrollPos.x = scrollPos.x - 1;
		}
		else if (eventGridWindow->IsFrozen() && posEvent.x >= w)
		{
			// frozen window was left, add the width of the non-frozen window
			w += gridWindow->GetSize().x;
		}

		if (posEvent.x < 0 && scrollPos.x > 0)
			newScrollPos.x = scrollPos.x - 1;
		else if (posEvent.x >= w)
			newScrollPos.x = scrollPos.x + 1;
	}

	if (direction & wxVERTICAL)
	{
		// check y direction
		if (eventGridWindow->IsFrozen() && posEvent.y < h)
		{
			// in the frozen window, moving upward?
			if (scrollPos.y && posEvent.y < m_lastMousePos.y)
				newScrollPos.y = scrollPos.y - 1;
		}
		else if (eventGridWindow->IsFrozen() && posEvent.y >= h)
		{
			// frozen window was left, add the height of the non-frozen window
			h += gridWindow->GetSize().y;
		}

		if (posEvent.y < 0 && scrollPos.y > 0)
			newScrollPos.y = scrollPos.y - 1;
		else if (posEvent.y >= h)
			newScrollPos.y = scrollPos.y + 1;
	}

	if (newScrollPos.x != wxDefaultCoord || newScrollPos.y != wxDefaultCoord)
		Scroll(newScrollPos);

	m_lastMousePos = posEvent;
}

bool ibGrid::CheckIfDragCancelled(wxMouseEvent* event)
{
	// helper for Process{Row|Col}LabelMouseEvent, ProcessGridCellMouseEvent:
	// block re-triggering m_isDragging
	if (!m_cancelledDragging)
		return false;

	if (event->LeftIsDown())
		return true;

	m_cancelledDragging = false;

	return false;
}

bool ibGrid::CheckIfAtDragSourceLine(const ibGridOperations& oper, int coord)
{
	// check whether coord on the dragged line or at max half of a line away
	// (the sizes of the lines before/after can be 0, if they are hidden)
	int minCoord = oper.GetLineStartPos(this, m_dragMoveRowOrCol);
	int maxCoord = minCoord + oper.GetLineSize(this, m_dragMoveRowOrCol);

	int lineBefore = oper.GetLineBefore(this, m_dragMoveRowOrCol);
	if (lineBefore == -1 && coord < maxCoord)
		return true;
	if (lineBefore != -1)
		minCoord -= oper.GetLineSize(this, lineBefore) / 2;

	int posAfter = oper.GetLinePos(this, m_dragMoveRowOrCol) + 1;
	int lineAfter = posAfter < oper.GetTotalNumberOfLines(this) ?
		oper.GetLineAt(this, posAfter) : -1;

	if (lineAfter == -1 && coord >= minCoord)
		return true;
	if (lineAfter != -1)
		maxCoord += oper.GetLineSize(this, lineAfter) / 2;

	if (coord >= minCoord && coord < maxCoord)
		return true;
	return false;
}

void ibGrid::ProcessRowColLabelMouseEvent(const ibGridOperations& oper, wxMouseEvent& event, ibGridSubwindow* labelWin)
{
	const ibGridOperations& dual = oper.Dual();

	ibGridSubwindow* headerWin = (ibGridSubwindow*)oper.GetHeaderWindow(this);
	ibGridWindow* gridWindow = labelWin->IsFrozen() ? oper.GetFrozenGrid(this) :
		m_gridWin;

	// store position, before it's modified in the next step
	const wxPoint posEvent = event.GetPosition();

	event.SetPosition(posEvent + GetGridWindowOffset(gridWindow));

	// for drag, we could be moving from the window sending the event to the other
	if (labelWin->IsFrozen() &&
		dual.Select(event.GetPosition()) > dual.Select(labelWin->GetClientSize()))
		gridWindow = m_gridWin;

	const wxPoint unscrolledPos = CalcGridWindowUnscrolledPosition(event.GetPosition(), gridWindow);

	// find y or x mouse coordinate for row / col label window
	int coord = dual.Select(unscrolledPos);
	// index into data rows/cols;  wxNOT_FOUND if outside label window
	int line = oper.PosToLine(this, coord, NULL);

	// wxNOT_FOUND if not near a line edge;  otherwise index into data rows/cols
	int lineEdge = PosToEdgeOfLine(coord, oper);


	// these are always valid, even if the mouse is outside the row/col range:
	// index into displayed rows/cols
	int posAt = PosToLinePos(coord, true /* clip */, oper, NULL);
	// index into data rows/cols
	int lineAt = oper.GetLineAt(this, posAt);

	if (CheckIfDragCancelled(&event))
		return;

	if (event.Dragging() && (m_winCapture == labelWin))
	{
		// scroll when at the edges or outside the window
		CheckDoDragScroll(labelWin, headerWin, posEvent, oper.GetOrientation());
	}

	if (event.Dragging())
	{
		bool callForceRefresh = !m_isDragging;

		if (!m_isDragging)
		{
			m_isDragging = true;

			if (m_cursorMode == oper.GetCursorModeMove() && line != -1)
				DoStartMoveRowOrCol(line);
		}

		if (event.LeftIsDown())
		{
			if (m_cursorMode == oper.GetCursorModeResize())
			{
				DoGridDragResize(event.GetPosition(), oper, gridWindow);
			}
			else if (m_cursorMode == oper.GetCursorModeSelect() && line >= 0)
			{
				// We can't extend the selection from non-selected row / col,
				// which may happen if we Ctrl-clicked it initially.
				// Therefore check m_selection->IsInSelection(m_currentCellCoords)

				if (m_selection && m_numRows && m_numCols &&
					m_selection->IsInSelection(m_currentCellCoords))
				{
					oper.SelectionExtendCurrentBlock(this, line, event, wxEVT_GRID_RANGE_SELECTING);
				}
			}
			else if (m_cursorMode == oper.GetCursorModeMove())
			{
				// determine the y or x position of the drop marker
				int markerPos;
				if (coord >= oper.GetLineStartPos(this, lineAt) + (oper.GetLineSize(this, lineAt) / 2))
					markerPos = oper.GetLineEndPos(this, lineAt);
				else
					markerPos = oper.GetLineStartPos(this, lineAt);

				const wxColour* markerColour;
				// Moving to the same place? Draw a grey marker.
				if (CheckIfAtDragSourceLine(oper, coord))
					markerColour = wxLIGHT_GREY;
				else
					markerColour = wxBLUE;

				if (markerPos != m_dragLastPos || markerColour != m_dragLastColour)
				{
					wxClientDC dc(headerWin);
					oper.PrepareDCForLabels(this, dc);

					int markerLength = oper.Select(headerWin->GetClientSize());

					// Clean up the last indicator if position has changed
					if (m_dragLastPos >= 0 && markerPos != m_dragLastPos)
					{
						wxPen pen(headerWin->GetBackgroundColour(), 2);
						dc.SetPen(pen);
						oper.DrawParallelLine(dc, 0, markerLength, m_dragLastPos + 1);
						dc.SetPen(wxNullPen);

						int lastLine = oper.PosToLine(this, m_dragLastPos, NULL, false);

						if (lastLine != -1)
							oper.DrawLineLabel(this, dc, lastLine);
					}

					// Draw the marker
					wxPen pen(*markerColour, 2);
					dc.SetPen(pen);
					oper.DrawParallelLine(dc, 0, markerLength, markerPos + 1);
					dc.SetPen(wxNullPen);

					m_dragLastPos = markerPos;
					m_dragLastColour = markerColour;
				}
			}

			if (callForceRefresh)
				ForceRefresh();
		}
		return;
	}

	if (m_isDragging && (event.Entering() || event.Leaving()))
		return;

	// ------------ Entering or leaving the window
	//
	if (event.Entering() || event.Leaving())
	{
		ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL, labelWin);
	}

	// ------------ Left button pressed
	//
	else if (event.LeftDown())
	{
		if (lineEdge != wxNOT_FOUND && oper.CanDragLineSize(this, lineEdge))
		{
			DoStartResizeRowOrCol(lineEdge, oper.GetLineSize(this, lineEdge));
			ChangeCursorMode(oper.GetCursorModeResize(), labelWin);
		}
		else
		{
			// not a request to start resizing
			if (line >= 0 &&
				oper.SendEvent(this, wxEVT_GRID_LABEL_LEFT_CLICK, line, event) == Event_Unhandled)
			{
				if (oper.CanDragMove(this))
				{
					ChangeCursorMode(oper.GetCursorModeMove(), headerWin);

					// Show button as pressed
					m_dragMoveRowOrCol = line;
					wxClientDC dc(headerWin);
					oper.PrepareDCForLabels(this, dc);
					oper.DrawLineLabel(this, dc, line);
				}
				else
				{
					// Check if row/col selection is possible and allowed, before doing
					// anything else, including changing the cursor mode to "select
					// row"/"select col".
					if (m_selection && m_numRows > 0 && m_numCols > 0 &&
						m_selection->GetSelectionMode() != dual.GetSelectionMode())
					{
						bool selectNewLine = false,
							makeLineCurrent = false;

						if (event.ShiftDown() && !event.CmdDown())
						{
							// Continue editing the current selection and don't
							// move the grid cursor.
							oper.SelectionExtendCurrentBlock(this, line, event);
							oper.MakeLineVisible(this, line);
						}
						else if (event.CmdDown() && !event.ShiftDown())
						{
							if (oper.IsLineInSelection(this, line))
							{
								oper.DeselectLine(this, line);
								makeLineCurrent = true;
							}
							else
							{
								makeLineCurrent =
									selectNewLine = true;
							}
						}
						else
						{
							ClearSelection();
							makeLineCurrent =
								selectNewLine = true;
						}

						if (selectNewLine)
							oper.SelectLine(this, line, event);

						if (makeLineCurrent)
							oper.MakeLineCurrent(this, line);

						ChangeCursorMode(oper.GetCursorModeSelect(), labelWin);
					}
				}
			}
		}
	}

	// ------------ Left double click
	//
	else if (event.LeftDClick())
	{
		if (lineEdge != wxNOT_FOUND && oper.CanDragLineSize(this, line))
		{
			// adjust row or column size depending on label text
			oper.HandleLineAutosize(this, lineEdge, event);

			ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL, labelWin);
			m_dragLastPos = -1;
		}
		else // not on line separator or it's not resizable
		{
			if (line >= 0 &&
				oper.SendEvent(this, wxEVT_GRID_LABEL_LEFT_DCLICK, line, event) == Event_Unhandled)
			{
				// no default action at the moment
			}
		}
	}

	// ------------ Left button released
	//
	else if (event.LeftUp())
	{
		if (m_cursorMode == oper.GetCursorModeResize())
		{
			oper.DoEndLineResize(this, event, gridWindow);
		}
		else if (m_cursorMode == oper.GetCursorModeMove())
		{
			if (CheckIfAtDragSourceLine(oper, coord))
			{
				// the line didn't actually move anywhere, "unpress" the label
				if (oper.GetOrientation() == wxVERTICAL && line != -1)
					DoColHeaderClick(line);
				oper.GetHeaderWindow(this)->Refresh();
			}
			else
			{
				// find the target position: start with current mouse position
				// (posAt and lineAt are always valid indices, not wxNOT_FOUND)
				int posNew = posAt;

				// adjust for the row being dragged itself
				if (posAt < oper.GetLinePos(this, m_dragMoveRowOrCol))
					posNew++;

				// if mouse is on the near part of the target row,
				// insert it before it, not after
				if (posNew > 0 &&
					(coord <= oper.GetLineStartPos(this, lineAt) +
						oper.GetLineSize(this, lineAt) / 2))
					posNew--;

				oper.DoEndMove(this, posNew);
			}
		}
		else if (oper.GetOrientation() == wxHORIZONTAL && line != -1 &&
			(m_cursorMode == WXGRID_CURSOR_SELECT_CELL ||
				m_cursorMode == WXGRID_CURSOR_SELECT_COL))
		{
			DoColHeaderClick(line);
		}
		EndDraggingIfNecessary();
	}

	// ------------ Right button down
	//
	else if (event.RightDown())
	{
		if (line < 0 ||
			oper.SendEvent(this, wxEVT_GRID_LABEL_RIGHT_CLICK, line, event) == Event_Unhandled)
		{
			// no default action at the moment
			event.Skip();
		}
	}

	// ------------ Right double click
	//
	else if (event.RightDClick())
	{
		if (line < 0 ||
			oper.SendEvent(this, wxEVT_GRID_LABEL_RIGHT_DCLICK, line, event) == Event_Unhandled)
		{
			// no default action at the moment
			event.Skip();
		}
	}

	// ------------ No buttons down and mouse moving
	//
	else if (event.Moving())
	{
		if (lineEdge != wxNOT_FOUND)
		{
			if (m_cursorMode == WXGRID_CURSOR_SELECT_CELL)
			{
				if (oper.CanDragLineSize(this, lineEdge))
				{
					ChangeCursorMode(oper.GetCursorModeResize(), labelWin, false);
				}
			}
		}
		else if (m_cursorMode != WXGRID_CURSOR_SELECT_CELL)
		{
			ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL, labelWin, false);
		}
	}

	// Don't consume the remaining events (e.g. right up).
	else
	{
		event.Skip();
	}
}

void ibGrid::UpdateCurrentCellOnRedim()
{
	if (m_currentCellCoords == ibGridNoCellCoords)
	{
		// We didn't have any valid selection before, which can only happen
		// if the grid was empty.
		// Check if this is still the case and ensure we do have valid
		// selection if the grid is not empty any more.
		if (m_numCols > 0 && m_numRows > 0)
		{
			SetCurrentCell(0, 0);
		}
	}
	else
	{
		if (m_numCols == 0 || m_numRows == 0)
		{
			// We have to reset the selection, as it must either use validate
			// coordinates otherwise, but there are no valid coordinates for
			// the grid cells any more now that it is empty.
			m_currentCellCoords = ibGridNoCellCoords;
		}
		else
		{
			// Check if the current cell coordinates are still valid.
			ibGridCellCoords updatedCoords = m_currentCellCoords;
			if (updatedCoords.GetCol() >= m_numCols)
				updatedCoords.SetCol(m_numCols - 1);
			if (updatedCoords.GetRow() >= m_numRows)
				updatedCoords.SetRow(m_numRows - 1);

			// And change them if they're not.
			if (updatedCoords != m_currentCellCoords)
			{
				// Prevent SetCurrentCell() from redrawing the previous current
				// cell whose coordinates are invalid now.
				m_currentCellCoords = ibGridNoCellCoords;
				SetCurrentCell(updatedCoords);
			}
		}
	}
}

void ibGrid::UpdateColumnSortingIndicator(int col)
{
	wxCHECK_RET(col != wxNOT_FOUND, "invalid column index");

	if (m_useNativeHeader) {
		m_colAreaWin->Refresh();
		GetGridColHeader()->UpdateColumn(col);
	}
	else if (m_nativeColumnLabels) {
		m_colAreaWin->Refresh();
		m_colLabelWin->Refresh();
	}
	//else: sorting indicator display not yet implemented in grid version
}

void ibGrid::SetSortingColumn(int col, bool ascending)
{
	if (col == m_sortCol)
	{
		// we are already using this column for sorting (or not sorting at all)
		// but we might still change the sorting order, check for it
		if (m_sortCol != wxNOT_FOUND && ascending != m_sortIsAscending)
		{
			m_sortIsAscending = ascending;

			UpdateColumnSortingIndicator(m_sortCol);
		}
	}
	else // we're changing the column used for sorting
	{
		const int sortColOld = m_sortCol;

		// change it before updating the column as we want GetSortingColumn()
		// to return the correct new value
		m_sortCol = col;

		if (sortColOld != wxNOT_FOUND)
			UpdateColumnSortingIndicator(sortColOld);

		if (m_sortCol != wxNOT_FOUND)
		{
			m_sortIsAscending = ascending;
			UpdateColumnSortingIndicator(m_sortCol);
		}
	}
}

void ibGrid::DoColHeaderClick(int col)
{
	// we consider that the grid was resorted if this event is processed and
	// not vetoed
	if (SendEvent(wxEVT_GRID_COL_SORT, -1, col) == Event_Handled)
	{
		SetSortingColumn(col, IsSortingBy(col) ? !m_sortIsAscending : true);
		Refresh();
	}
}

void ibGrid::DoStartResizeRowOrCol(int col, int size)
{
	// Hide the editor if it's currently shown to avoid any weird interactions
	// with it while dragging the row/column separator.
	AcceptCellEditControlIfShown();

	m_dragRowOrCol = col;
	m_dragRowOrColOldSize = ibRestoreGridScale(size, GetGridZoom());
}

void ibGrid::ProcessRowColAreaMouseEvent(const ibGridOperations& oper, wxMouseEvent& event, ibGridSubwindow* areaWin)
{
	const ibGridOperations& dual = oper.Dual();

	ibGridSubwindow* headerWin = (ibGridSubwindow*)oper.GetHeaderWindow(this);
	ibGridWindow* gridWindow = areaWin->IsFrozen() ? oper.GetFrozenGrid(this) :
		m_gridWin;

	// store position, before it's modified in the next step
	const wxPoint posEvent = event.GetPosition();

	event.SetPosition(posEvent + GetGridWindowOffset(gridWindow));

	// for drag, we could be moving from the window sending the event to the other
	if (areaWin->IsFrozen() &&
		dual.Select(event.GetPosition()) > dual.Select(areaWin->GetClientSize()))
		gridWindow = m_gridWin;

	const wxPoint unscrolledPos = CalcGridWindowUnscrolledPosition(event.GetPosition(), gridWindow);

	// find y or x mouse coordinate for row / col label window
	int coord = dual.Select(unscrolledPos);

	// index into data rows/cols;  wxNOT_FOUND if outside label window
	int line = oper.PosToLine(this, coord, NULL);

	if (event.LeftDown())
	{
		// indicate corner label by having both row and
		// col args == -1
		//
		if (SendEvent(wxEVT_GRID_LABEL_LEFT_CLICK, -1, -1, event) == Event_Unhandled) {
			oper.SelectArea(this, line, event);
		}
	}
	else if (event.LeftDClick())
	{
		SendEvent(wxEVT_GRID_LABEL_LEFT_DCLICK, -1, -1, event);
		oper.MakeAreaName(this, line, event);
	}
	else if (event.RightDown())
	{
		if (SendEvent(wxEVT_GRID_LABEL_RIGHT_CLICK, -1, -1, event) == Event_Unhandled)
		{
			// no default action at the moment
			event.Skip();
		}
	}
	else if (event.RightDClick())
	{
		if (SendEvent(wxEVT_GRID_LABEL_RIGHT_DCLICK, -1, -1, event) == Event_Unhandled)
		{
			// no default action at the moment
			event.Skip();
		}
	}
	else
	{
		event.Skip();
	}
}

void ibGrid::ProcessCornerLabelMouseEvent(wxMouseEvent& event)
{
	if (event.LeftDown())
	{
		// indicate corner label by having both row and
		// col args == -1
		//
		if (SendEvent(wxEVT_GRID_LABEL_LEFT_CLICK, -1, -1, event) == Event_Unhandled)
		{
			SelectAll();
		}
	}
	else if (event.LeftDClick())
	{
		SendEvent(wxEVT_GRID_LABEL_LEFT_DCLICK, -1, -1, event);
	}
	else if (event.RightDown())
	{
		if (SendEvent(wxEVT_GRID_LABEL_RIGHT_CLICK, -1, -1, event) == Event_Unhandled)
		{
			// no default action at the moment
			event.Skip();
		}
	}
	else if (event.RightDClick())
	{
		if (SendEvent(wxEVT_GRID_LABEL_RIGHT_DCLICK, -1, -1, event) == Event_Unhandled)
		{
			// no default action at the moment
			event.Skip();
		}
	}
	else
	{
		event.Skip();
	}
}

void ibGrid::HandleRowAutosize(int row, const wxMouseEvent& event)
{
	// adjust row height depending on label text
	//
	// TODO: generate RESIZING event, see #10754
	if (!SendGridSizeEvent(wxEVT_GRID_ROW_AUTO_SIZE, row, event))
		AutoSizeRowLabelSize(row);

	SendGridSizeEvent(wxEVT_GRID_ROW_SIZE, row, event);
}

void ibGrid::HandleColumnAutosize(int col, const wxMouseEvent& event)
{
	// adjust column width depending on label text
	//
	// TODO: generate RESIZING event, see #10754
	if (!SendGridSizeEvent(wxEVT_GRID_COL_AUTO_SIZE, col, event))
		AutoSizeColLabelSize(col);

	SendGridSizeEvent(wxEVT_GRID_COL_SIZE, col, event);
}

void ibGrid::CancelMouseCapture()
{
	// cancel operation currently in progress, whatever it is
	if (m_winCapture)
	{
		if (m_cursorMode == WXGRID_CURSOR_MOVE_COL ||
			m_cursorMode == WXGRID_CURSOR_MOVE_ROW)
			m_winCapture->Refresh();
		DoAfterDraggingEnd();
	}
}

void ibGrid::DoAfterDraggingEnd()
{
	if (m_isDragging &&
		(m_cursorMode == WXGRID_CURSOR_SELECT_CELL ||
			m_cursorMode == WXGRID_CURSOR_SELECT_ROW ||
			m_cursorMode == WXGRID_CURSOR_SELECT_COL))
	{
		m_selection->EndSelecting();
	}

	m_isDragging = false;
	m_startDragPos = wxDefaultPosition;
	m_lastMousePos = wxDefaultPosition;
	// from drag moving row/col
	m_dragMoveRowOrCol = -1;
	m_dragLastPos = -1;
	m_dragLastColour = NULL;

	m_cursorMode = WXGRID_CURSOR_SELECT_CELL;
	m_winCapture->SetCursor(*wxSTANDARD_CURSOR);
	m_winCapture = NULL;
}

void ibGrid::EndDraggingIfNecessary()
{
	if (m_winCapture)
	{
		m_winCapture->ReleaseMouse();

		DoAfterDraggingEnd();
	}
}

void ibGrid::ChangeCursorMode(CursorMode mode,
	wxWindow* win,
	bool captureMouse)
{
#if wxUSE_LOG_TRACE
	static const wxChar* const cursorModes[] =
	{
		wxT("SELECT_CELL"),
		wxT("RESIZE_ROW"),
		wxT("RESIZE_COL"),
		wxT("SELECT_ROW"),
		wxT("SELECT_COL"),
		wxT("MOVE_ROW"),
		wxT("MOVE_COL"),
	};

	wxLogTrace(wxT("grid"),
		wxT("ibGrid cursor mode (mouse capture for %s): %s -> %s"),
		win == m_colLabelWin ? wxT("colLabelWin")
		: win ? wxT("rowLabelWin")
		: wxT("gridWin"),
		cursorModes[m_cursorMode], cursorModes[mode]);
#endif // wxUSE_LOG_TRACE

	if (mode == m_cursorMode &&
		win == m_winCapture &&
		captureMouse == (m_winCapture != NULL))
		return;

	if (!win)
	{
		// by default use the grid itself
		win = m_gridWin;
	}

	EndDraggingIfNecessary();

	m_cursorMode = mode;

	switch (m_cursorMode)
	{
	case WXGRID_CURSOR_RESIZE_ROW:
		win->SetCursor(m_rowResizeCursor);
		break;

	case WXGRID_CURSOR_RESIZE_COL:
		win->SetCursor(m_colResizeCursor);
		break;

	case WXGRID_CURSOR_MOVE_ROW:
	case WXGRID_CURSOR_MOVE_COL:
		win->SetCursor(wxCursor(wxCURSOR_HAND));
		break;

	case WXGRID_CURSOR_SELECT_CELL:
		// Mouse is captured in ProcessGridCellMouseEvent() in this mode.
		captureMouse = false;
		wxFALLTHROUGH;

	case WXGRID_CURSOR_SELECT_ROW:
	case WXGRID_CURSOR_SELECT_COL:
		win->SetCursor(*wxSTANDARD_CURSOR);
		break;
	}

	if (captureMouse)
	{
		win->CaptureMouse();
		m_winCapture = win;
	}
}

// ----------------------------------------------------------------------------
// grid mouse event processing
// ----------------------------------------------------------------------------

bool
ibGrid::DoGridCellDrag(wxMouseEvent& event,
	const ibGridCellCoords& coords,
	bool isFirstDrag)
{
	if (coords == ibGridNoCellCoords)
		return false; // we're outside any valid cell

	if (isFirstDrag)
	{
		// Hide the edit control, so it won't interfere with drag-shrinking.
		AcceptCellEditControlIfShown();

		switch (event.GetModifiers())
		{
		case wxMOD_CONTROL:
			// Ctrl-dragging is special, because we could have started it
			// by Ctrl-clicking a previously selected cell, which has the
			// effect of deselecting it and in this case we can't start
			// drag-selection from it because the selection anchor should
			// always be selected itself.
			if (!m_selection->IsInSelection(m_currentCellCoords))
				return false;
			break;

		case wxMOD_NONE:
			if (CanDragCell())
			{
				// if event is handled by user code, no further processing
				return SendEvent(wxEVT_GRID_CELL_BEGIN_DRAG, coords, event) == Event_Unhandled;
			}
			break;

			//default: In all the other cases, we don't have anything special
			//         to do and we'll just extend the selection below.
		}
	}

	// Note that we don't need to check the modifiers here, it doesn't matter
	// which keys are pressed for the current event, as pressing or releasing
	// Ctrl later can't change the dragging behaviour. Only the initial state
	// of the modifier keys matters.
	if (m_selection)
	{
		const int rowStart = m_currentCellCoords.GetRow() < coords.GetRow() ?
			m_currentCellCoords.GetRow() : coords.GetRow();
		const int colStart = m_currentCellCoords.GetCol() < coords.GetCol() ?
			m_currentCellCoords.GetCol() : coords.GetCol();

		const int rowEnd = m_currentCellCoords.GetRow() > coords.GetRow() ?
			m_currentCellCoords.GetRow() : coords.GetRow();
		const int colEnd = m_currentCellCoords.GetCol() > coords.GetCol() ?
			m_currentCellCoords.GetCol() : coords.GetCol();

		ibGridCellCoords blockStart(rowStart, colStart);
		ibGridCellCoords blockEnd(rowEnd, colEnd);

		for (int row = rowStart; row <= rowEnd; row++)
		{
			for (int col = colStart; col <= colEnd; col++)
			{
				int cell_rows, cell_cols;
				if (GetCellSize(row, col, &cell_rows, &cell_cols) != CellSpan::CellSpan_Inside)
				{
					int end_row = row + cell_rows - 1;
					int end_col = col + cell_cols - 1;

					if (end_row > blockEnd.GetRow())
						blockEnd.SetRow(end_row);

					if (end_col > blockEnd.GetCol())
						blockEnd.SetCol(end_col);

					int cell_start_rows, cell_start_cols;
					GetCellSize(end_row, end_col, &cell_start_rows, &cell_start_cols);

					int start_row = end_row + cell_start_rows;
					int start_col = end_col + cell_start_cols;

					if (start_row < blockStart.GetRow())
						blockStart.SetRow(start_row);

					if (start_col < blockStart.GetCol())
						blockStart.SetCol(start_col);
				}
			}
		}

		m_selection->ExtendCurrentBlock(blockStart,
			blockEnd,
			event,
			wxEVT_GRID_RANGE_SELECTING);
	}

	return true;
}

bool ibGrid::DoGridDragEvent(wxMouseEvent& event,
	const ibGridCellCoords& coords,
	bool isFirstDrag,
	ibGridWindow* gridWindow)
{
	switch (m_cursorMode)
	{
	case WXGRID_CURSOR_SELECT_CELL:
		return DoGridCellDrag(event, coords, isFirstDrag);

	case WXGRID_CURSOR_RESIZE_ROW:
		if (m_dragRowOrCol != -1)
			DoGridDragResize(event.GetPosition(), ibGridRowOperations(), gridWindow);
		break;

	case WXGRID_CURSOR_RESIZE_COL:
		if (m_dragRowOrCol != -1)
			DoGridDragResize(event.GetPosition(), ibGridColumnOperations(), gridWindow);
		break;

	default:
		event.Skip();
	}

	return true;
}

void
ibGrid::DoGridCellLeftDown(wxMouseEvent& event,
	const ibGridCellCoords& coords,
	const wxPoint& pos)
{
	if (SendEvent(wxEVT_GRID_CELL_LEFT_CLICK, coords, event) != Event_Unhandled)
	{
		// event handled by user code, no need to do anything here
		return;
	}

	// Process the mouse down event depending on the current cursor mode. Note
	// that this assumes m_cursorMode was set in the mouse move event hendler.
	switch (m_cursorMode)
	{
	case WXGRID_CURSOR_RESIZE_ROW:
	case WXGRID_CURSOR_RESIZE_COL:
	{
		int dragRowOrCol = wxNOT_FOUND;
		if (m_cursorMode == WXGRID_CURSOR_RESIZE_COL)
		{
			dragRowOrCol = XToEdgeOfCol(pos.x);
			DoStartResizeRowOrCol(dragRowOrCol, GetColSize(dragRowOrCol));
		}
		else
		{
			dragRowOrCol = YToEdgeOfRow(pos.y);
			DoStartResizeRowOrCol(dragRowOrCol, GetRowSize(dragRowOrCol));
		}
		wxCHECK_RET(dragRowOrCol != -1, "Can't determine row or column in resizing mode");
	}
	break;

	case WXGRID_CURSOR_SELECT_CELL:
	case WXGRID_CURSOR_SELECT_ROW:
	case WXGRID_CURSOR_SELECT_COL:
		DisableCellEditControl();
		MakeCellVisible(coords);

		if (event.ShiftDown() && !event.CmdDown())
		{
			if (m_selection)
			{
				m_selection->ExtendCurrentBlock(m_currentCellCoords, coords, event);
			}
		}
		else
		{
			if (event.CmdDown() && !event.ShiftDown())
			{
				if (m_selection)
				{
					if (!m_selection->IsInSelection(coords))
					{
						// If the cell is not selected, select it.
						m_selection->SelectBlock(coords.GetRow(), coords.GetCol(),
							coords.GetRow(), coords.GetCol(),
							event);
					}
					else
					{
						// Otherwise deselect it.
						m_selection->DeselectBlock(
							ibGridBlockCoords(coords.GetRow(), coords.GetCol(),
								coords.GetRow(), coords.GetCol()),
							event);
					}
				}
			}
			else
			{
				ClearSelection();

				if (m_selection)
				{
					// In row or column selection mode just clicking on the cell
					// should select the row or column containing it: this is more
					// convenient for the kinds of controls that use such selection
					// mode and is compatible with 2.8 behaviour (see #12062).
					switch (m_selection->GetSelectionMode())
					{
					case ibGridSelectNone:
					case ibGridSelectCells:
					case ibGridSelectRowsOrColumns:
						// nothing to do in these cases
						break;

					case ibGridSelectRows:
						m_selection->SelectRow(coords.GetRow());
						break;

					case ibGridSelectColumns:
						m_selection->SelectCol(coords.GetCol());
						break;
					}
				}

				m_waitForSlowClick = m_currentCellCoords == coords &&
					coords != ibGridNoCellCoords;
			}

			SetCurrentCell(coords);
		}
		break;

	case WXGRID_CURSOR_MOVE_ROW:
	case WXGRID_CURSOR_MOVE_COL:
		// Nothing to do here for this case.
		break;
	}
}

void
ibGrid::DoGridCellLeftDClick(wxMouseEvent& event,
	const ibGridCellCoords& coords,
	const wxPoint& pos)
{
	if (XToEdgeOfCol(pos.x) < 0 && YToEdgeOfRow(pos.y) < 0)
	{
		if (SendEvent(wxEVT_GRID_CELL_LEFT_DCLICK, coords, event) == Event_Unhandled)
		{
			// we want double click to select a cell and start editing
			// (i.e. to behave in same way as sequence of two slow clicks):
			m_waitForSlowClick = true;
		}
	}
}

void
ibGrid::DoGridCellLeftUp(wxMouseEvent& event,
	const ibGridCellCoords& coords,
	ibGridWindow* gridWindow)
{
	if (m_cursorMode == WXGRID_CURSOR_SELECT_CELL)
	{
		if (coords == m_currentCellCoords && m_waitForSlowClick && CanEnableCellControl())
		{
			ClearSelection();

			if (DoEnableCellEditControl(ibGridActivationSource::From(event)))
				GetCurrentCellEditorPtr()->StartingClick();

			m_waitForSlowClick = false;
		}
	}
	else if (m_cursorMode == WXGRID_CURSOR_RESIZE_ROW)
	{
		ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL);
		if (m_dragRowOrCol != -1)
			DoEndDragResizeRow(event, gridWindow);
	}
	else if (m_cursorMode == WXGRID_CURSOR_RESIZE_COL)
	{
		ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL);
		if (m_dragRowOrCol != -1)
			DoEndDragResizeCol(event, gridWindow);
	}

	m_dragLastPos = -1;
}

void
ibGrid::DoGridMouseMoveEvent(wxMouseEvent& WXUNUSED(event),
	const ibGridCellCoords& coords,
	const wxPoint& pos,
	ibGridWindow* gridWindow)
{
	if (coords.GetRow() < 0 || coords.GetCol() < 0)
	{
		// out of grid cell area
		ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL);
		return;
	}

	int dragRow = YToEdgeOfRow(pos.y);
	int dragCol = XToEdgeOfCol(pos.x);

	// Dragging on the corner of a cell to resize in both directions is not
	// implemented, so choose to resize the column when the cursor is over the
	// cell corner, as this is a more common operation.
	if (dragCol >= 0 && CanDragGridColEdges() && CanDragColSize(dragCol))
	{
		if (m_cursorMode != WXGRID_CURSOR_RESIZE_COL)
		{
			ChangeCursorMode(WXGRID_CURSOR_RESIZE_COL, gridWindow, false);
		}
	}
	else if (dragRow >= 0 && CanDragGridRowEdges() && CanDragRowSize(dragRow))
	{
		if (m_cursorMode != WXGRID_CURSOR_RESIZE_ROW)
		{
			ChangeCursorMode(WXGRID_CURSOR_RESIZE_ROW, gridWindow, false);
		}
	}
	else // Neither on a row or col edge
	{
		if (m_cursorMode != WXGRID_CURSOR_SELECT_CELL)
		{
			ChangeCursorMode(WXGRID_CURSOR_SELECT_CELL, gridWindow, false);
		}
	}
}

void ibGrid::ProcessGridCellMouseEvent(wxMouseEvent& event, ibGridWindow* eventGridWindow)
{
	// the window receiving the event might not be the same as the one under
	// the mouse (e.g. in the case of a dragging event started in one window,
	// but continuing over another one)

	if (CheckIfDragCancelled(&event))
		return;

	ibGridWindow* gridWindow =
		DevicePosToGridWindow(event.GetPosition() + eventGridWindow->GetPosition());

	if (!gridWindow)
		gridWindow = eventGridWindow;

	// store position, before it's modified in the next step
	const wxPoint posEvent = event.GetPosition();

	event.SetPosition(posEvent + eventGridWindow->GetPosition() -
		wxPoint((GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom())));

	wxPoint pos = CalcGridWindowUnscrolledPosition(event.GetPosition(), gridWindow);

	// coordinates of the cell under mouse
	ibGridCellCoords coords = XYToCell(pos, gridWindow);

	int cell_rows, cell_cols;
	if (GetCellSize(coords.GetRow(), coords.GetCol(), &cell_rows, &cell_cols)
		== CellSpan_Inside)
	{
		coords.SetRow(coords.GetRow() + cell_rows);
		coords.SetCol(coords.GetCol() + cell_cols);
	}

	// Releasing the left mouse button must be processed in any case, so deal
	// with it first.
	if (event.LeftUp())
	{
		// Note that we must call this one first, before resetting the
		// drag-related data, as it relies on m_cursorMode being still set and
		// EndDraggingIfNecessary() resets it.
		DoGridCellLeftUp(event, coords, gridWindow);

		EndDraggingIfNecessary();
		return;
	}

	const bool isDraggingWithLeft = event.Dragging() && event.LeftIsDown();

	if (isDraggingWithLeft && (m_winCapture == eventGridWindow))
	{
		// scroll when at the edges or outside the window
		CheckDoDragScroll(eventGridWindow, m_gridWin, posEvent,
			wxHORIZONTAL | wxVERTICAL);
	}

	// While dragging the mouse, only releasing the left mouse button, which
	// cancels the drag operation, is processed (above) and any other events
	// are just ignored while it's in progress.
	if (m_isDragging)
	{
		if (isDraggingWithLeft)
			DoGridDragEvent(event, coords, false /* not first drag */, gridWindow);

		if (m_winCapture != gridWindow)
		{
			if (m_winCapture)
				m_winCapture->ReleaseMouse();

			m_winCapture = gridWindow;
			m_winCapture->CaptureMouse();
		}
		return;
	}

	// Now check if we're starting a drag operation (if it had been already
	// started, m_isDragging would be true above).
	if (isDraggingWithLeft)
	{
		// To avoid accidental drags, don't start doing anything until the
		// mouse has been dragged far enough.
		const wxPoint& pt = event.GetPosition();
		if (m_startDragPos == wxDefaultPosition)
		{
			m_startDragPos = pt;
			return;
		}

		if (abs(m_startDragPos.x - pt.x) <= DRAG_SENSITIVITY &&
			abs(m_startDragPos.y - pt.y) <= DRAG_SENSITIVITY)
			return;

		if (DoGridDragEvent(event, coords, true /* first drag */, gridWindow))
		{
			wxASSERT_MSG(!m_winCapture, "shouldn't capture the mouse twice");

			m_winCapture = gridWindow;
			m_winCapture->CaptureMouse();

			m_isDragging = true;
		}

		return;
	}

	// If we're not dragging, cancel any dragging operation which could have
	// been in progress.
	EndDraggingIfNecessary();

	bool handled = false;

	// deal with various button presses
	if (event.IsButton())
	{
		if (coords != ibGridNoCellCoords)
		{
			DisableCellEditControl();

			if (event.LeftDown())
				handled = (DoGridCellLeftDown(event, coords, pos), true);
			else if (event.LeftDClick())
				handled = (DoGridCellLeftDClick(event, coords, pos), true);
			else if (event.RightDown())
				handled = SendEvent(wxEVT_GRID_CELL_RIGHT_CLICK, coords, event) != Event_Unhandled;
			else if (event.RightDClick())
				handled = SendEvent(wxEVT_GRID_CELL_RIGHT_DCLICK, coords, event) != Event_Unhandled;
		}
	}
	else if (event.Moving())
	{
		DoGridMouseMoveEvent(event, coords, pos, gridWindow);
		handled = true;
	}

	if (!handled)
	{
		event.Skip();
	}
}

void ibGrid::DoGridDragResize(const wxPoint& position,
	const ibGridOperations& oper,
	ibGridWindow* gridWindow)
{
	wxCHECK_RET(m_dragRowOrCol != -1,
		"shouldn't be called when not drag resizing");

	// Get the logical position from the physical one we're passed.
	const wxPoint
		logicalPos = CalcGridWindowUnscrolledPosition(position, gridWindow);

	// Size of the row/column is determined by the mouse coordinates in the
	// orthogonal direction.
	const int linePos = oper.Dual().Select(logicalPos);

	const int lineStart = oper.GetLineStartPos(this, m_dragRowOrCol);
	oper.SetLineSize(this, m_dragRowOrCol,
		wxMax(linePos - lineStart,
			oper.GetMinimalLineSize(this, m_dragRowOrCol)));

	// TODO: generate RESIZING event, see #10754, if the size has changed.
}

wxPoint ibGrid::GetPositionForResizeEvent(int width) const
{
	wxCHECK_MSG(m_dragRowOrCol != -1, wxPoint(),
		"shouldn't be called when not drag resizing");

	// Note that we currently always use m_gridWin here as using
	// ibGridHeaderCtrl is incompatible with using frozen rows/columns.
	// This would need to be changed if they're allowed to be used together.
	int x;
	CalcGridWindowScrolledPosition(GetColLeft(m_dragRowOrCol, GetGridZoom()) + width, 0,
		&x, NULL,
		m_gridWin);

	return wxPoint(x, 0);
}

void ibGrid::DoEndDragResizeRow(const wxMouseEvent& event, ibGridWindow* gridWindow)
{
	DoGridDragResize(event.GetPosition(), ibGridRowOperations(), gridWindow);

	SendGridSizeEvent(wxEVT_GRID_ROW_SIZE, m_dragRowOrCol, event);

	PushCommand<ibGridCommandRowSize>(m_dragRowOrCol,
		GetRowHeight(m_dragRowOrCol), m_dragRowOrColOldSize);

	SendGridSizeEvent(wxEVT_GRID_ROW_MODIFIED, m_dragRowOrCol, event);

	m_dragRowOrCol = -1;
}

void ibGrid::DoEndDragResizeCol(const wxMouseEvent& event, ibGridWindow* gridWindow)
{
	DoGridDragResize(event.GetPosition(), ibGridColumnOperations(), gridWindow);

	SendGridSizeEvent(wxEVT_GRID_COL_SIZE, m_dragRowOrCol, event);

	PushCommand<ibGridCommandColSize>(m_dragRowOrCol,
		GetColWidth(m_dragRowOrCol), m_dragRowOrColOldSize);

	SendGridSizeEvent(wxEVT_GRID_COL_MODIFIED, m_dragRowOrCol, event);

	m_dragRowOrCol = -1;
}

void ibGrid::DoHeaderStartDragResizeCol(int col)
{
	DoStartResizeRowOrCol(col, GetColSize(col));
}

void ibGrid::DoHeaderDragResizeCol(int width)
{
	DoGridDragResize(GetPositionForResizeEvent(width),
		ibGridColumnOperations(),
		m_gridWin);
}

void ibGrid::DoHeaderEndDragResizeCol(int width)
{
	// We can sometimes be called even when we're not resizing any more,
	// although it's rather difficult to reproduce: one way to do it is to
	// double click the column separator line while pressing Esc at the same
	// time. There seems to be some kind of check for Esc inside the native
	// header control and so an extra "end resizing" message gets generated.
	if (m_dragRowOrCol == -1)
		return;

	// Unfortunately we need to create a dummy mouse event here to avoid
	// modifying too much existing code. Note that only position and keyboard
	// state parts of this event object are actually used, so the rest
	// (even including some crucial parts, such as event type) can be left
	// uninitialized.
	wxMouseEvent e;
	e.SetState(wxGetMouseState());
	e.SetPosition(GetPositionForResizeEvent(width));

	DoEndDragResizeCol(e, m_gridWin);
}

void ibGrid::DoStartMoveRowOrCol(int col)
{
	m_dragMoveRowOrCol = col;
}

void ibGrid::DoEndMoveRow(int pos)
{
	wxASSERT_MSG(m_dragMoveRowOrCol != -1, "no matching DoStartMoveRow?");

	if (SendEvent(wxEVT_GRID_ROW_MOVE, m_dragMoveRowOrCol, -1) != Event_Vetoed)
		SetRowPos(m_dragMoveRowOrCol, pos);

	m_dragMoveRowOrCol = -1;
}

void ibGrid::RefreshAfterRowPosChange()
{
	// and make the changes visible
	m_rowAreaWin->Refresh();
	m_rowLabelWin->Refresh();
	m_gridWin->Refresh();
}

void ibGrid::SetRowsOrder(const wxArrayInt& order)
{
	m_rowAt = order;

	RefreshAfterRowPosChange();
}

void ibGrid::SetRowPos(int idx, int pos)
{
	// we're going to need m_rowAt now, initialize it if needed
	if (m_rowAt.empty())
	{
		m_rowAt.reserve(m_numRows);
		for (int i = 0; i < m_numRows; i++)
			m_rowAt.push_back(i);
	}

	// from wxHeaderCtrl::MoveRowInOrderArray:
	int posOld = m_rowAt.Index(idx);
	wxASSERT_MSG(posOld != wxNOT_FOUND, "invalid index");

	if (pos != posOld)
	{
		m_rowAt.RemoveAt(posOld);
		m_rowAt.Insert(idx, pos);
	}

	RefreshAfterRowPosChange();
}

int ibGrid::GetRowPos(int idx) const
{
	wxASSERT_MSG(idx >= 0 && idx < m_numRows, "invalid row index");

	if (m_rowAt.IsEmpty())
		return idx;

	int pos = m_rowAt.Index(idx);
	wxASSERT_MSG(pos != wxNOT_FOUND, "invalid row index");

	return pos;
}

void ibGrid::ResetRowPos()
{
	m_rowAt.clear();

	RefreshAfterRowPosChange();
}

bool ibGrid::EnableDragRowMove(bool enable)
{
	if (m_canDragRowMove == enable ||
		(enable && m_rowFrozenLabelWin))
		return false;

	m_canDragRowMove = enable;

	// we use to call ResetRowPos() from here if !enable but this doesn't seem
	// right as it would mean there would be no way to "freeze" the current
	// rows order by disabling moving them after putting them in the desired
	// order, whereas now you can always call ResetRowPos() manually if needed
	return true;
}

void ibGrid::DoEndMoveCol(int pos)
{
	wxASSERT_MSG(m_dragMoveRowOrCol != -1, "no matching DoStartMoveCol?");

	if (SendEvent(wxEVT_GRID_COL_MOVE, -1, m_dragMoveRowOrCol) != Event_Vetoed)
		SetColPos(m_dragMoveRowOrCol, pos);

	m_dragMoveRowOrCol = -1;
}

void ibGrid::RefreshAfterColPosChange()
{
	// and make the changes visible
	if (m_useNativeHeader)
	{
		m_colAreaWin->Refresh();
		SetNativeHeaderColOrder();
	}
	else
	{
		m_colAreaWin->Refresh();
		m_colLabelWin->Refresh();
	}

	m_gridWin->Refresh();
}

void ibGrid::SetColumnsOrder(const wxArrayInt& order)
{
	m_colAt = order;

	RefreshAfterColPosChange();
}

void ibGrid::SetColPos(int idx, int pos)
{
	// we're going to need m_colAt now, initialize it if needed
	if (m_colAt.empty())
	{
		m_colAt.reserve(m_numCols);
		for (int i = 0; i < m_numCols; i++)
			m_colAt.push_back(i);
	}

	wxHeaderCtrl::MoveColumnInOrderArray(m_colAt, idx, pos);

	RefreshAfterColPosChange();
}

int ibGrid::GetColPos(int idx) const
{
	wxASSERT_MSG(idx >= 0 && idx < m_numCols, "invalid column index");

	if (m_colAt.IsEmpty())
		return idx;

	int pos = m_colAt.Index(idx);
	wxASSERT_MSG(pos != wxNOT_FOUND, "invalid column index");

	return pos;
}

void ibGrid::ResetColPos()
{
	m_colAt.clear();

	RefreshAfterColPosChange();
}

bool ibGrid::EnableDragColMove(bool enable)
{
	if (m_canDragColMove == enable ||
		(enable && m_colFrozenLabelWin))
		return false;

	if (m_useNativeHeader)
	{
		wxHeaderCtrl* header = GetGridColHeader();
		long setFlags = header->GetWindowStyleFlag();

		if (enable)
			header->SetWindowStyleFlag(setFlags | wxHD_ALLOW_REORDER);
		else
			header->SetWindowStyleFlag(setFlags & ~wxHD_ALLOW_REORDER);
	}

	m_canDragColMove = enable;

	// we use to call ResetColPos() from here if !enable but this doesn't seem
	// right as it would mean there would be no way to "freeze" the current
	// columns order by disabling moving them after putting them in the desired
	// order, whereas now you can always call ResetColPos() manually if needed
	return true;
}

bool ibGrid::EnableHidingColumns(bool enable)
{
	if (m_canHideColumns == enable || !m_useNativeHeader)
		return false;

	GetGridColHeader()->ToggleWindowStyle(wxHD_ALLOW_HIDE);

	m_canHideColumns = enable;

	return true;
}

void ibGrid::InitializeFrozenWindows()
{
	// frozen row windows
	if (m_numFrozenRows > 0 && !m_frozenRowGridWin)
	{
		m_frozenRowGridWin = new ibGridWindow(this, ibGridWindow::ibGridWindowFrozenRow);
		m_frozenRowGridWin->SetOwnForegroundColour(m_gridWin->GetForegroundColour());
		m_frozenRowGridWin->SetOwnBackgroundColour(m_gridWin->GetBackgroundColour());

		m_rowFrozenAreaWin = new ibGridRowFrozenAreaWindow(this);
		m_rowFrozenAreaWin->SetOwnForegroundColour(m_labelTextColour);
		m_rowFrozenAreaWin->SetOwnBackgroundColour(m_labelBackgroundColour);

		m_rowFrozenLabelWin = new ibGridRowFrozenLabelWindow(this);
		m_rowFrozenLabelWin->SetOwnForegroundColour(m_labelTextColour);
		m_rowFrozenLabelWin->SetOwnBackgroundColour(m_labelBackgroundColour);
	}
	else if (m_numFrozenRows == 0 && m_frozenRowGridWin)
	{
		delete m_frozenRowGridWin;
		delete m_rowFrozenAreaWin;
		delete m_rowFrozenLabelWin;
		m_frozenRowGridWin = NULL;
		m_rowFrozenAreaWin = NULL;
		m_rowFrozenLabelWin = NULL;
	}

	// frozen column windows
	if (m_numFrozenCols > 0 && !m_frozenColGridWin)
	{
		m_frozenColGridWin = new ibGridWindow(this, ibGridWindow::ibGridWindowFrozenCol);
		m_frozenColGridWin->SetOwnForegroundColour(m_gridWin->GetForegroundColour());
		m_frozenColGridWin->SetOwnBackgroundColour(m_gridWin->GetBackgroundColour());

		m_colFrozenAreaWin = new ibGridColFrozenAreaWindow(this);
		m_colFrozenAreaWin->SetOwnForegroundColour(m_labelTextColour);
		m_colFrozenAreaWin->SetOwnBackgroundColour(m_labelBackgroundColour);

		m_colFrozenLabelWin = new ibGridColFrozenLabelWindow(this);
		m_colFrozenLabelWin->SetOwnForegroundColour(m_labelTextColour);
		m_colFrozenLabelWin->SetOwnBackgroundColour(m_labelBackgroundColour);
	}
	else if (m_numFrozenCols == 0 && m_frozenColGridWin)
	{
		delete m_frozenColGridWin;
		delete m_colFrozenAreaWin;
		delete m_colFrozenLabelWin;

		m_frozenColGridWin = NULL;
		m_colFrozenAreaWin = NULL;
		m_colFrozenLabelWin = NULL;
	}

	// frozen corner window
	if (m_numFrozenRows > 0 && m_numFrozenCols > 0 && !m_frozenCornerGridWin)
	{
		m_frozenCornerGridWin = new ibGridWindow(this, ibGridWindow::ibGridWindowFrozenCorner);

		m_frozenCornerGridWin->SetOwnForegroundColour(m_gridWin->GetForegroundColour());
		m_frozenCornerGridWin->SetOwnBackgroundColour(m_gridWin->GetBackgroundColour());
	}
	else if ((m_numFrozenRows == 0 || m_numFrozenCols == 0) && m_frozenCornerGridWin)
	{
		delete m_frozenCornerGridWin;
		m_frozenCornerGridWin = NULL;
	}
}

bool ibGrid::FreezeTo(int row, int col)
{
	wxCHECK_MSG(row >= 0 && col >= 0, false,
		"Number of rows or cols can't be negative!");

	if (row >= m_numRows || col >= m_numCols ||
		!m_rowAt.empty() || m_canDragRowMove ||
		!m_colAt.empty() || m_canDragColMove || m_useNativeHeader)
		return false;

	// freeze
	if (row > m_numFrozenRows || col > m_numFrozenCols)
	{
		// check that it fits in client size
		//int cw, ch;
		//GetClientSize(&cw, &ch);

		//cw -= (GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom());
		//ch -= (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom());

		//if ((row > 0 && GetRowBottom(row - 1, GetGridZoom()) >= ch) ||
		//	(col > 0 && GetColRight(col - 1, GetGridZoom()) >= cw))
		//	return false;

		// check all involved cells for merged ones
		//int cell_rows, cell_cols;

		//for (int i = m_numFrozenRows; i < row; i++)
		//{
		//	for (int j = 0; j < m_numCols; j++)
		//	{
		//		GetCellSize(GetRowAt(i), GetColAt(j), &cell_rows, &cell_cols);

		//		if ((cell_rows > 1) || (cell_cols > 1))
		//			return false;
		//	}
		//}

		//for (int i = m_numFrozenCols; i < col; i++)
		//{
		//	for (int j = 0; j < m_numRows; j++)
		//	{
		//		GetCellSize(GetRowAt(j), GetColAt(i), &cell_rows, &cell_cols);

		//		if ((cell_rows > 1) || (cell_cols > 1))
		//			return false;
		//	}
		//}
	}

	m_numFrozenRows = row;
	m_numFrozenCols = col;

	HideCellEditControl();

	InitializeFrozenWindows();

	// recompute dimensions
	InvalidateBestSize();

	CalcDimensions();

	if (ShouldRefresh())
		Refresh();

	// make up a dummy event for the grid event to use -- unfortunately we
	// can't do anything else here
	wxMouseEvent e;
	e.SetState(wxGetMouseState());
	ScreenToClient(&e.m_x, &e.m_y);

	SendGridSizeEvent(wxEVT_GRID_ROW_FREEZE, row, e);
	SendGridSizeEvent(wxEVT_GRID_COL_FREEZE, col, e);
	return true;
}

bool ibGrid::IsFrozen() const
{
	return m_numFrozenRows || m_numFrozenCols;
}

void ibGrid::UpdateGridWindows() const
{
	m_gridWin->Update();

	if (m_frozenCornerGridWin)
		m_frozenCornerGridWin->Update();

	if (m_frozenRowGridWin)
		m_frozenRowGridWin->Update();

	if (m_frozenColGridWin)
		m_frozenColGridWin->Update();
}

//
// ------ interaction with data model
//
bool ibGrid::ProcessTableMessage(ibGridTableMessage& msg)
{
	switch (msg.GetId())
	{
	case wxGRIDTABLE_NOTIFY_ROWS_INSERTED:
	case wxGRIDTABLE_NOTIFY_ROWS_APPENDED:
	case wxGRIDTABLE_NOTIFY_ROWS_DELETED:
	case wxGRIDTABLE_NOTIFY_COLS_INSERTED:
	case wxGRIDTABLE_NOTIFY_COLS_APPENDED:
	case wxGRIDTABLE_NOTIFY_COLS_DELETED:
		return Redimension(msg);

	default:
		return false;
	}
}

// The behaviour of this function depends on the grid table class
// Clear() function. For the default ibGridStringTable class the
// behaviour is to replace all cell contents with wxEmptyString.
//
void ibGrid::ClearGrid()
{
	if (m_table)
	{
		DisableCellEditControl();
		m_cellEditCtrlEnabled = false;

		ibGridSelectionModes selmode = m_selection ?
			m_selection->GetSelectionMode() : ibGridSelectionModes::ibGridSelectRowsOrColumns;

		wxDELETE(m_selection);

		//Clear cell/col/row attributes 
		m_table->Clear();
		m_table->SetAttrProvider(NULL);

		// Don't hold on to attributes cached from the old table
		ClearAttrCache();

		m_undoStack.clear();
		m_redoStack.clear();

		m_numRows = 0;
		m_numCols = 0;
		m_numFrozenRows = 0;
		m_numFrozenCols = 0;

		//Row and column positions
		m_rowAt.Clear();
		m_colAt.Clear();

		//Area row, col positions
		m_rowAreaAt.Clear();
		m_colAreaAt.Clear();

		// kill row and column size arrays
		m_rowHeights.Empty();
		m_colWidths.Empty();

		m_rowBrakeAt.Clear();
		m_colBrakeAt.Clear();

		m_selection = new ibGridSelection(this, selmode);
		CalcDimensions();

		if (ShouldRefresh())
			m_gridWin->Refresh();
	}
}

bool
ibGrid::DoModifyLines(bool (ibGridTableBase::* funcModify)(size_t, size_t),
	int pos, int num, bool WXUNUSED(updateLabels))
{
	wxCHECK_MSG(m_created, false, "must finish creating the grid first");

	if (!m_table)
		return false;

	DisableCellEditControl();

	return (m_table->*funcModify)(pos, num);

	// the table will have sent the results of the insert row
	// operation to this view object as a grid table message
}

bool
ibGrid::DoAppendLines(bool (ibGridTableBase::* funcAppend)(size_t),
	int num, bool WXUNUSED(updateLabels))
{
	wxCHECK_MSG(m_created, false, "must finish creating the grid first");

	if (!m_table)
		return false;

	return (m_table->*funcAppend)(num);
}

// ----------------------------------------------------------------------------
// event generation helpers
// ----------------------------------------------------------------------------

bool
ibGrid::SendGridAreaEvent(wxEventType type, int rowOrColPos, const ibGridCellArea& area)
{
	ibGridAreaEvent gridEvt(GetId(),
		type,
		this,
		rowOrColPos,
		area);

	return ProcessWindowEvent(gridEvt);
}

bool
ibGrid::SendGridSizeEvent(wxEventType type,
	int rowOrCol,
	const wxMouseEvent& mouseEv)
{
	ibGridSizeEvent gridEvt(GetId(),
		type,
		this,
		rowOrCol,
		mouseEv.GetX() + ibCalcGridScale(GetRowLabelSize(), GetGridZoom()),
		mouseEv.GetY() + ibCalcGridScale(GetColLabelSize(), GetGridZoom()),
		mouseEv);

	return ProcessWindowEvent(gridEvt);
}

ibGrid::EventResult ibGrid::DoSendEvent(ibGridEvent& gridEvt)
{
	const bool claimed = ProcessWindowEvent(gridEvt);

	// A Veto'd event may not be `claimed' so test this first
	if (!gridEvt.IsAllowed())
		return Event_Vetoed;

	// Detect the special case in which the event cell was deleted, as this
	// allows to have checks in several functions that generate an event and
	// then proceed doing something by default with the selected cell: this
	// shouldn't be done if the user-defined handler deleted this cell.
	if (gridEvt.GetRow() >= GetNumberRows() ||
		gridEvt.GetCol() >= GetNumberCols())
		return Event_CellDeleted;

	return claimed ? Event_Handled : Event_Unhandled;
}

// Generate a grid event based on a mouse event and call DoSendEvent() with it.
ibGrid::EventResult
ibGrid::SendEvent(wxEventType type,
	int row, int col,
	const wxMouseEvent& mouseEv)
{

	if (type == wxEVT_GRID_LABEL_LEFT_CLICK ||
		type == wxEVT_GRID_LABEL_LEFT_DCLICK ||
		type == wxEVT_GRID_LABEL_RIGHT_CLICK ||
		type == wxEVT_GRID_LABEL_RIGHT_DCLICK)
	{
		wxPoint pos = mouseEv.GetPosition();

		if (mouseEv.GetEventObject() == GetGridRowLabelWindow())
			pos.y += ibCalcGridScale(GetColLabelSize(), GetGridZoom());
		if (mouseEv.GetEventObject() == GetGridColLabelWindow())
			pos.x += ibCalcGridScale(GetRowLabelSize(), GetGridZoom());

		ibGridEvent gridEvt(GetId(),
			type,
			this,
			row, col,
			pos.x,
			pos.y,
			false,
			mouseEv);

		return DoSendEvent(gridEvt);
	}
	else
	{
		ibGridEvent gridEvt(GetId(),
			type,
			this,
			row, col,
			mouseEv.GetX() + ibCalcGridScale(GetRowLabelSize(), GetGridZoom()),
			mouseEv.GetY() + ibCalcGridScale(GetColLabelSize(), GetGridZoom()),
			false,
			mouseEv);

		if (type == wxEVT_GRID_CELL_BEGIN_DRAG)
		{
			// by default the dragging is not supported, the user code must
			// explicitly allow the event for it to take place
			gridEvt.Veto();
		}

		return DoSendEvent(gridEvt);
	}
}

// Generate a grid event of specified type, return value same as above
//
ibGrid::EventResult
ibGrid::SendEvent(wxEventType type, int row, int col, const wxString& s)
{
	ibGridEvent gridEvt(GetId(), type, this, row, col);
	gridEvt.SetString(s);

	return DoSendEvent(gridEvt);
}

void ibGrid::Refresh(bool eraseb, const wxRect* rect)
{
	// Don't do anything if between Begin/EndBatch...
	// EndBatch() will do all this on the last nested one anyway.
	if (m_created && ShouldRefresh())
	{
		// Refresh to get correct scrolled position:
		wxScrolledCanvas::Refresh(eraseb, rect);

		if (rect)
		{
			int rect_x, rect_y, rectWidth, rectHeight;
			int width_label, width_cell, height_label, height_cell;
			int x, y;

			// Copy rectangle can get scroll offsets..
			rect_x = rect->GetX();
			rect_y = rect->GetY();
			rectWidth = rect->GetWidth();
			rectHeight = rect->GetHeight();

			width_label = (GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) - rect_x;
			if (width_label > rectWidth)
				width_label = rectWidth;

			height_label = (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) - rect_y;
			if (height_label > rectHeight)
				height_label = rectHeight;

			if (rect_x > (GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()))
			{
				x = rect_x - (GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom());
				width_cell = rectWidth;
			}
			else
			{
				x = 0;
				width_cell = rectWidth - ((GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) - rect_x);
			}

			if (rect_y > (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom()))
			{
				y = rect_y - m_colAreaHeight + ibCalcGridScale(m_colLabelHeight, GetGridZoom());
				height_cell = rectHeight;
			}
			else
			{
				y = 0;
				height_cell = rectHeight - ((GridColAreaEnabled() ? (m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) - rect_y);
			}

			// Paint corner label part intersecting rect.
			if (width_label > 0 && height_label > 0)
			{
				wxRect anotherrect(rect_x, rect_y, width_label, height_label);
				m_cornerLabelWin->Refresh(eraseb, &anotherrect);
			}

			// Paint col area part intersecting rect.
			if (width_cell > 0 && height_label > 0)
			{
				wxRect anotherrect(x, rect_y, width_cell, height_label);
				m_colAreaWin->Refresh(eraseb, &anotherrect);
			}

			// Paint col labels part intersecting rect.
			if (width_cell > 0 && height_label > 0)
			{
				wxRect anotherrect(x, rect_y, width_cell, height_label);
				m_colLabelWin->Refresh(eraseb, &anotherrect);
			}

			// Paint row area part intersecting rect.
			if (width_label > 0 && height_cell > 0)
			{
				wxRect anotherrect(rect_x, y, width_label, height_cell);
				m_rowAreaWin->Refresh(eraseb, &anotherrect);
			}

			// Paint row labels part intersecting rect.
			if (width_label > 0 && height_cell > 0)
			{
				wxRect anotherrect(rect_x, y, width_label, height_cell);
				m_rowLabelWin->Refresh(eraseb, &anotherrect);
			}

			// Paint cell area part intersecting rect.
			if (width_cell > 0 && height_cell > 0)
			{
				wxRect anotherrect(x, y, width_cell, height_cell);
				m_gridWin->Refresh(eraseb, &anotherrect);
			}
		}
		else
		{
			m_cornerLabelWin->Refresh(eraseb, NULL);
			m_colAreaWin->Refresh(eraseb, NULL);
			m_colLabelWin->Refresh(eraseb, NULL);
			m_rowAreaWin->Refresh(eraseb, NULL);
			m_rowLabelWin->Refresh(eraseb, NULL);
			m_gridWin->Refresh(eraseb, NULL);

			if (m_frozenColGridWin)
			{
				m_frozenColGridWin->Refresh(eraseb, NULL);
				m_colFrozenAreaWin->Refresh(eraseb, NULL);
				m_colFrozenLabelWin->Refresh(eraseb, NULL);
			}
			if (m_frozenRowGridWin)
			{
				m_frozenRowGridWin->Refresh(eraseb, NULL);
				m_rowFrozenAreaWin->Refresh(eraseb, NULL);
				m_rowFrozenLabelWin->Refresh(eraseb, NULL);
			}
			if (m_frozenCornerGridWin)
				m_frozenCornerGridWin->Refresh(eraseb, NULL);
		}
	}
}

void ibGrid::RefreshBlock(const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight)
{
	RefreshBlock(topLeft.GetRow(), topLeft.GetCol(),
		bottomRight.GetRow(), bottomRight.GetCol());
}

void ibGrid::RefreshBlock(int topRow, int leftCol,
	int bottomRow, int rightCol)
{
	// Note that it is valid to call this function with ibGridNoCellCoords as
	// either or even both arguments, but we can't have a mix of valid and
	// invalid columns/rows for each corner coordinates.
	const bool noTopLeft = topRow == -1 || leftCol == -1;
	const bool noBottomRight = bottomRow == -1 || rightCol == -1;

	if (noTopLeft)
	{
		// So check that either both or none of the components are valid.
		wxASSERT(topRow == -1 && leftCol == -1);

		// And specifying bottom right corner when the top left one is not
		// specified doesn't make sense either.
		wxASSERT(noBottomRight);

		return;
	}

	if (noBottomRight)
	{
		wxASSERT(bottomRow == -1 && rightCol == -1);

		bottomRow = topRow;
		rightCol = leftCol;
	}


	int row = topRow;
	int col = leftCol;

	// corner grid
	if (GetRowPos(topRow) < m_numFrozenRows && GetColPos(leftCol) < m_numFrozenCols && m_frozenCornerGridWin)
	{
		row = wxMin(bottomRow, m_numFrozenRows - 1);
		col = wxMin(rightCol, m_numFrozenCols - 1);

		wxRect rect = BlockToDeviceRect(ibGridCellCoords(topRow, leftCol),
			ibGridCellCoords(row, col),
			m_frozenCornerGridWin);
		m_frozenCornerGridWin->Refresh(false, &rect);
		row++; col++;
	}

	// frozen cols grid
	if (GetColPos(leftCol) < m_numFrozenCols && GetRowPos(bottomRow) >= m_numFrozenRows && m_frozenColGridWin)
	{
		col = wxMin(rightCol, m_numFrozenCols - 1);

		wxRect rect = BlockToDeviceRect(ibGridCellCoords(row, leftCol),
			ibGridCellCoords(bottomRow, col),
			m_frozenColGridWin);
		m_frozenColGridWin->Refresh(false, &rect);
		col++;
	}

	// frozen rows grid
	if (GetRowPos(topRow) < m_numFrozenRows && GetColPos(rightCol) >= m_numFrozenCols && m_frozenRowGridWin)
	{
		row = wxMin(bottomRow, m_numFrozenRows - 1);

		wxRect rect = BlockToDeviceRect(ibGridCellCoords(topRow, col),
			ibGridCellCoords(row, rightCol),
			m_frozenRowGridWin);
		m_frozenRowGridWin->Refresh(false, &rect);
		row++;
	}

	// main grid
	if (GetRowPos(bottomRow) >= m_numFrozenRows && GetColPos(rightCol) >= m_numFrozenCols)
	{
		const wxRect rect = BlockToDeviceRect(ibGridCellCoords(row, col),
			ibGridCellCoords(bottomRow, rightCol),
			m_gridWin);
		if (!rect.IsEmpty())
			m_gridWin->Refresh(false, &rect);
	}
}

void ibGrid::OnSize(wxSizeEvent& event)
{
	if (m_targetWindow != this) // check whether initialisation has been done
	{
		// reposition our children windows
		CalcWindowSizes();
		event.Skip();
	}
}

void ibGrid::OnDPIChanged(wxDPIChangedEvent& event)
{
	InitPixelFields();

	// If we have any non-default row sizes, we need to scale them (default
	// ones will be scaled due to the reinitialization of m_defaultRowHeight
	// inside InitPixelFields() above).
	if (!m_rowHeights.empty())
	{
		for (unsigned i = 0; i < m_rowHeights.size(); ++i)
		{
			int height = m_rowHeights[i];

			// Skip hidden rows.
			if (height <= 0)
				continue;

			m_rowHeights[i] = event.ScaleY(height);
		}
	}

	// Similarly for columns, except that here we need to update the native
	// control even if none of the widths had been changed, as it's not going
	// to do it on its own when redisplayed.
	wxHeaderCtrl* const
		colHeader = m_useNativeHeader ? GetGridColHeader() : NULL;
	if (!m_colWidths.empty())
	{
		int total = 0;
		for (unsigned i = 0; i < m_colWidths.size(); ++i)
		{
			int width = m_colWidths[i];

			if (width <= 0)
				continue;

			m_colWidths[i] = event.ScaleX(width);

			if (colHeader)
				colHeader->UpdateColumn(i);
		}
	}
	else if (colHeader)
	{
		for (int i = 0; i < m_numCols; ++i)
		{
			colHeader->UpdateColumn(i);
		}
	}

	InvalidateBestSize();

	CalcDimensions();

	event.Skip();
}

void ibGrid::OnKeyDown(wxKeyEvent& event)
{
	// propagate the event up and see if it gets processed
	//wxWindow* parent = GetParent();
	//wxKeyEvent keyEvt(event);
	//keyEvt.SetEventObject(parent);

	//if (!parent->ProcessWindowEvent(keyEvt))
	{
		if (GetLayoutDirection() == wxLayout_RightToLeft)
		{
			if (event.GetKeyCode() == WXK_RIGHT)
				event.m_keyCode = WXK_LEFT;
			else if (event.GetKeyCode() == WXK_LEFT)
				event.m_keyCode = WXK_RIGHT;
		}

		// try local handlers
		switch (event.GetKeyCode())
		{
		case WXK_UP:
			DoMoveCursorFromKeyboard
			(
				event,
				ibGridBackwardOperations(this, ibGridRowOperations())
			);
			break;

		case WXK_DOWN:
			DoMoveCursorFromKeyboard
			(
				event,
				ibGridForwardOperations(this, ibGridRowOperations())
			);
			break;

		case WXK_LEFT:
			DoMoveCursorFromKeyboard
			(
				event,
				ibGridBackwardOperations(this, ibGridColumnOperations())
			);
			break;

		case WXK_RIGHT:
			DoMoveCursorFromKeyboard
			(
				event,
				ibGridForwardOperations(this, ibGridColumnOperations())
			);
			break;

		case WXK_RETURN:
		case WXK_NUMPAD_ENTER:
			if (event.ControlDown())
			{
				event.Skip();  // to let the edit control have the return
			}
			else
			{
				// We want to accept the changes in the editor when Enter
				// is pressed in any case, so do it (note that in many
				// cases this would be done by MoveCursorDown() itself, but
				// not always, e.g. it wouldn't do it when editing the
				// cells in the last row or when using Shift-Enter).
				DisableCellEditControl();

				MoveCursorDown(event.ShiftDown());
			}
			break;

		case WXK_ESCAPE:
			if (m_isDragging && m_winCapture)
			{
				switch (m_cursorMode)
				{
				case WXGRID_CURSOR_MOVE_COL:
				case WXGRID_CURSOR_MOVE_ROW:
					// end row/column moving
					m_winCapture->Refresh();
					m_dragLastPos = -1;
					m_dragLastColour = NULL;
					break;

				case WXGRID_CURSOR_RESIZE_ROW:
				case WXGRID_CURSOR_RESIZE_COL:
					// reset to size from before dragging
					(m_cursorMode == WXGRID_CURSOR_RESIZE_ROW
						? static_cast<const ibGridOperations&>(ibGridRowOperations())
						: static_cast<const ibGridOperations&>(ibGridColumnOperations())
						).SetLineSize(this, m_dragRowOrCol, m_dragRowOrColOldSize);

					m_dragRowOrCol = -1;
					break;

				case WXGRID_CURSOR_SELECT_CELL:
				case WXGRID_CURSOR_SELECT_ROW:
				case WXGRID_CURSOR_SELECT_COL:
					if (m_selection)
						m_selection->CancelSelecting();
					break;
				}
				EndDraggingIfNecessary();

				// ensure that a new drag operation is only started after a LeftUp
				m_cancelledDragging = true;
			}
			else
			{
				ClearSelection();
			}
			break;

		case WXK_TAB:
		{
			// send an event to the grid's parents for custom handling
			ibGridEvent gridEvt(GetId(), wxEVT_GRID_TABBING, this,
				GetGridCursorRow(), GetGridCursorCol(),
				-1, -1, false, event);
			if (ProcessWindowEvent(gridEvt))
			{
				// the event has been handled so no need for more processing
				break;
			}
		}
		DoGridProcessTab(event);
		break;

		case WXK_HOME:
		case WXK_END:
			if (m_currentCellCoords != ibGridNoCellCoords)
			{
				const bool goToBeginning = event.GetKeyCode() == WXK_HOME;

				// Find the first or last visible row if we need to go to it
				// (without Control, we keep the current row).
				int row;
				if (event.ControlDown())
				{
					if (goToBeginning)
					{
						for (row = 0; row < m_numRows; ++row)
						{
							if (IsRowShown(row))
								break;
						}
					}
					else
					{
						for (row = m_numRows - 1; row >= 0; --row)
						{
							if (IsRowShown(row))
								break;
						}
					}
				}
				else
				{
					// If we're extending the selection, try to continue in
					// the same row, which may well be different from the
					// one in which we started selecting.
					if (m_selection && event.ShiftDown())
					{
						row = m_selection->GetExtensionAnchor().GetRow();
					}
					else // Just use the current row.
					{
						row = m_currentCellCoords.GetRow();
					}
				}

				// Also find the last or first visible column in any case.
				int col;
				if (goToBeginning)
				{
					for (col = 0; col < m_numCols; ++col)
					{
						if (IsColShown(GetColAt(col)))
							break;
					}
				}
				else
				{
					for (col = m_numCols - 1; col >= 0; --col)
					{
						if (IsColShown(GetColAt(col)))
							break;
					}
				}

				if (event.ShiftDown())
				{
					if (m_selection)
						m_selection->ExtendCurrentBlock(
							m_currentCellCoords,
							ibGridCellCoords(row, col),
							event);
					MakeCellVisible(row, col);
				}
				else
				{
					ClearSelection();
					GoToCell(row, GetColAt(col));
				}
			}
			break;

		case WXK_PAGEUP:
			DoMoveCursorByPage
			(
				event,
				ibGridBackwardOperations(this, ibGridRowOperations())
			);
			break;

		case WXK_PAGEDOWN:
			DoMoveCursorByPage
			(
				event,
				ibGridForwardOperations(this, ibGridRowOperations())
			);
			break;

		case WXK_SPACE:
			// Ctrl-Space selects the current column or, if there is a
			// selection, all columns containing the selected cells,
			// Shift-Space -- the current row (or all selection rows) and
			// Ctrl-Shift-Space -- everything.
		{
			ibGridCellCoords selStart, selEnd;
			switch (m_selection ? event.GetModifiers() : wxMOD_NONE)
			{
			case wxMOD_CONTROL:
				selStart.Set(0, m_currentCellCoords.GetCol());
				selEnd.Set(m_numRows - 1,
					m_selection->GetExtensionAnchor().GetCol());
				break;

			case wxMOD_SHIFT:
				selStart.Set(m_currentCellCoords.GetRow(), 0);
				selEnd.Set(m_selection->GetExtensionAnchor().GetRow(),
					m_numCols - 1);
				break;

			case wxMOD_CONTROL | wxMOD_SHIFT:
				selStart.Set(0, 0);
				selEnd.Set(m_numRows - 1, m_numCols - 1);
				break;

			case wxMOD_NONE:
				if (!IsEditable())
				{
					MoveCursorRight(false);
					break;
				}
				wxFALLTHROUGH;

			default:
				event.Skip();
			}

			if (selStart != ibGridNoCellCoords)
				m_selection->ExtendCurrentBlock(selStart, selEnd, event);
		}
		break;

#if wxUSE_CLIPBOARD
		case WXK_INSERT:
		case 'C':
			if (event.GetModifiers() == wxMOD_CONTROL)
			{
				// Coordinates of the selected block to copy to clipboard.
				ibGridBlockCoords sel;

				// Check if we have any selected blocks and if we don't
				// have too many of them.
				const ibGridBlocks blocks = GetSelectedBlocks();
				ibGridBlocks::iterator iter = blocks.begin();
				if (iter == blocks.end())
				{
					// No selection, copy just the current cell.
					if (m_currentCellCoords == ibGridNoCellCoords)
					{
						// But we don't even have it -- nothing to do then.
						event.Skip();
						break;
					}

					sel = ibGridBlockCoords(GetGridCursorRow(),
						GetGridCursorCol(),
						GetGridCursorRow(),
						GetGridCursorCol());
				}
				else // We do have at least one selected block.
				{
					sel = *blocks.begin();

					if (++iter != blocks.end())
					{
						// As we use simple text format, we can't copy more
						// than one block to clipboard.
						wxLogWarning
						(
							_("Copying more than one selected block "
								"to clipboard is not supported.")
						);
						break;
					}
				}

				wxClipboardLocker lockClipboard;
				if (!lockClipboard)
				{
					// Error message should have been already given and we
					// don't have much to add.
					break;
				}

				wxString buf;
				for (int row = sel.GetTopRow(); row <= sel.GetBottomRow(); row++)
				{
					bool first = true;

					for (int col = sel.GetLeftCol(); col <= sel.GetRightCol(); col++)
					{
						if (first)
							first = false;
						else
							buf += '\t';

						buf += GetCellValue(row, col);
					}

					buf += wxTextFile::GetEOL();
				}

				wxTheClipboard->SetData(new wxTextDataObject(buf));
				break;
			}
			wxFALLTHROUGH;
#endif // wxUSE_CLIPBOARD

		default:
			event.Skip();
			break;
		}
	}
}

void ibGrid::OnKeyUp(wxKeyEvent& WXUNUSED(event))
{
	// This function is unused and not connected to the corresponding event in
	// the event table, it is only kept to prevent changing ABI in this branch
	// and doesn't exist at all in the later wxWidgets versions.
}

void ibGrid::OnChar(wxKeyEvent& event)
{
	// is it possible to edit the current cell at all?
	if (!IsCellEditControlEnabled() && CanEnableCellControl())
	{
		// yes, now check whether the cells editor accepts the key
		ibGridCellEditorPtr editor = GetCurrentCellEditorPtr();

		// <F2> is special and will always start editing, for
		// other keys - ask the editor itself
		const bool specialEditKey = event.GetKeyCode() == WXK_F2 &&
			!event.HasModifiers();
		if (specialEditKey || editor->IsAcceptedKey(event))
		{
			// ensure cell is visble
			MakeCellVisible(m_currentCellCoords);

			if (DoEnableCellEditControl(ibGridActivationSource::From(event))
				&& !specialEditKey)
				editor->StartingKey(event);
		}
		else
		{
			event.Skip();
		}
	}
	else
	{
		event.Skip();
	}
}

void ibGrid::DoGridProcessTab(wxKeyboardState& kbdState)
{
	const bool isForwardTab = !kbdState.ShiftDown();

	// TAB processing only changes when we are at the borders of the grid, so
	// let's first handle the common behaviour when we are not at the border.
	if (isForwardTab)
	{
		if (GetGridCursorCol() < GetNumberCols() - 1)
		{
			MoveCursorRight(false);
			return;
		}
	}
	else // going back
	{
		if (GetGridCursorCol())
		{
			MoveCursorLeft(false);
			return;
		}
	}


	// We only get here if the cursor is at the border of the grid, apply the
	// configured behaviour.
	switch (m_tabBehaviour)
	{
	case Tab_Stop:
		// Nothing special to do, we remain at the current cell.
		break;

	case Tab_Wrap:
		// Go to the beginning of the next or the end of the previous row.
		if (isForwardTab)
		{
			if (GetGridCursorRow() < GetNumberRows() - 1)
			{
				GoToCell(GetGridCursorRow() + 1, 0);
				return;
			}
		}
		else
		{
			if (GetGridCursorRow() > 0)
			{
				GoToCell(GetGridCursorRow() - 1, GetNumberCols() - 1);
				return;
			}
		}
		break;

	case Tab_Leave:
		if (Navigate(isForwardTab ? wxNavigationKeyEvent::IsForward
			: wxNavigationKeyEvent::IsBackward))
			return;
		break;
	}

	// If we remain in this cell, stop editing it if we were doing so.
	DisableCellEditControl();
}

bool ibGrid::SetCurrentCell(const ibGridCellCoords& coords)
{
	switch (SendEvent(wxEVT_GRID_SELECT_CELL, coords))
	{
	case Event_Vetoed:
	case Event_CellDeleted:
		// We shouldn't do anything if the event was vetoed and can't do
		// anything if the cell doesn't exist any longer.
		return false;

	case Event_Unhandled:
	case Event_Handled:
		// But it doesn't matter here if the event was skipped or not.
		break;
	}

	if (m_currentCellCoords != ibGridNoCellCoords)
	{
		DisableCellEditControl();

		if (IsVisible(m_currentCellCoords, false))
		{
#if defined(__WXOSX__) || defined(__WXGTK3__)
			RefreshBlock(m_currentCellCoords, m_currentCellCoords);
#else
			ibGridWindow* prevGridWindow = CellToGridWindow(m_currentCellCoords);
			wxRect r;
			r = BlockToDeviceRect(m_currentCellCoords, m_currentCellCoords, prevGridWindow);
			if (!m_gridLinesEnabled)
			{
				r.x--;
				r.y--;
				r.width++;
				r.height++;
			}

			ibGridCellCoordsArray cells;
			CalcCellsExposed(r, cells, prevGridWindow);

			// Otherwise refresh redraws the highlight!
			m_currentCellCoords = coords;

			wxClientDC prevDc(prevGridWindow);
			PrepareDCFor(prevDc, prevGridWindow);

			DrawGridCellArea(prevDc, cells);
			DrawAllGridWindowLines(prevDc, r, prevGridWindow);

			if (prevGridWindow->GetType() != ibGridWindow::ibGridWindowNormal)
				DrawFrozenBorder(prevDc, prevGridWindow);
#endif
		}
	}

	m_currentCellCoords = coords;

#if defined(__WXOSX__) || defined(__WXGTK3__)
	RefreshBlock(coords, coords);
#else
	if (ShouldRefresh())
	{
		ibGridCellAttrPtr attr = GetCellAttrPtr(coords);
		ibGridWindow* currentGridWindow = CellToGridWindow(coords);
		wxClientDC dc(currentGridWindow);
		PrepareDCFor(dc, currentGridWindow);
		DrawCellHighlight(dc, coords.GetRow(), coords.GetCol(), attr.get());
	}
#endif

	return true;
}

// Note - this function only draws cells that are in the list of
// exposed cells (usually set from the update region by
// CalcExposedCells)
//
void ibGrid::DrawGridCellArea(wxDC& dc, const ibGridCellCoordsArray& cells, ibGridCellCacheArray storage)
{
	if (!m_numRows || !m_numCols)
		return;

	int i, numCells = cells.GetCount();
	ibGridCellCoordsArray redrawCells;

	storage.Alloc(numCells);

	for (i = numCells - 1; i >= 0; i--)
	{
		int row, col, cell_rows, cell_cols;
		row = cells[i].GetRow();
		col = cells[i].GetCol();

		// If this cell is part of a multicell block, find owner for repaint
		if (GetCellSize(row, col, &cell_rows, &cell_cols) == CellSpan_Inside)
		{
			ibGridCellCoords cell(row + cell_rows, col + cell_cols);
			bool marked = false;
			for (int j = 0; j < numCells; j++)
			{
				if (cell == cells[j])
				{
					marked = true;
					break;
				}
			}

			if (!marked)
			{
				int count = redrawCells.GetCount();
				for (int j = 0; j < count; j++)
				{
					if (cell == redrawCells[j])
					{
						marked = true;
						break;
					}
				}

				if (!marked)
					redrawCells.Add(cell);
			}

			// don't bother drawing this cell
			continue;
		}

		// If this cell is empty, find cell to left that might want to overflow
		if (m_table && m_table->IsEmptyCell(row, col))
		{
			for (int l = 0; l < cell_rows; l++)
			{
				// find a cell in this row to leave already marked for repaint
				int left = col;
				for (int k = 0; k < int(redrawCells.GetCount()); k++)
					if ((redrawCells[k].GetCol() < left) &&
						(redrawCells[k].GetRow() == row))
					{
						left = redrawCells[k].GetCol();
					}

				if (left == col)
					left = 0; // oh well

				for (int j = col - 1; j >= left; j--)
				{
					if (!m_table->IsEmptyCell(row + l, j))
					{
						ibGridCellAttrPtr attr = GetCellAttrPtr(row + l, j);
						int numRows, numCols;
						attr->GetSize(&numRows, &numCols);
						if (GetCellSpan(numRows, numCols)
							== ibGrid::CellSpan_Inside)
							// As above: don't bother drawing inside cells.
							continue;

						if (attr->CanOverflow())
						{
							ibGridCellCoords cell(row + l, j);
							bool marked = false;

							for (int k = 0; k < numCells; k++)
							{
								if (cell == cells[k])
								{
									marked = true;
									break;
								}
							}

							if (!marked)
							{
								int count = redrawCells.GetCount();
								for (int k = 0; k < count; k++)
								{
									if (cell == redrawCells[k])
									{
										marked = true;
										break;
									}
								}
								if (!marked)
									redrawCells.Add(cell);
							}
						}
						break;
					}
				}
			}
		}

		ibGridCellCache entry;
		DrawCell(dc, cells[i], entry);
		storage.Add(entry);
	}

	numCells = redrawCells.GetCount();

	for (i = numCells - 1; i >= 0; i--)
	{
		ibGridCellCache entry;
		DrawCell(dc, redrawCells[i], entry);
		storage.Add(entry);
	}
}

void ibGrid::DrawGridSpace(wxDC& dc, ibGridWindow* gridWindow)
{
	int cw, ch;
	gridWindow->GetClientSize(&cw, &ch);

	int right, bottom;
	wxPoint offset = GetGridWindowOffset(gridWindow);
	CalcGridWindowUnscrolledPosition(cw + offset.x, ch + offset.y, &right, &bottom, gridWindow);

	int rightCol = m_numCols > 0 ? GetColRight(GetColAt(m_numCols - 1), GetGridZoom()) : 0;
	int bottomRow = m_numRows > 0 ? GetRowBottom(GetRowAt(m_numRows - 1), GetGridZoom()) : 0;

	if (right > rightCol || bottom > bottomRow)
	{
		int left, top;
		CalcGridWindowUnscrolledPosition(offset.x, offset.y, &left, &top, gridWindow);

		dc.SetBrush(GetDefaultCellBackgroundColour());
		dc.SetPen(*wxTRANSPARENT_PEN);

		if (right > rightCol)
		{
			dc.DrawRectangle(rightCol, top, right - rightCol, ch);
		}

		if (bottom > bottomRow)
		{
			dc.DrawRectangle(left, bottomRow, cw, bottom - bottomRow);
		}
	}
}

void ibGrid::DrawCell(wxDC& dc, const ibGridCellCoords& coords, ibGridCellCache cache)
{
	int row = coords.GetRow();
	int col = coords.GetCol();

	cache.m_coords = coords;

	if (GetColWidth(col) <= 0 || GetRowHeight(row) <= 0)
		return;

	bool isCurrent = coords == m_currentCellCoords;

	// we draw the cell border ourselves
	cache.m_attr = GetCellAttrPtr(row, col);
	cache.m_rect = CellToRect(row, col);

	// if the editor is shown, we should use it and not the renderer
	// Note: However, only if it is really _shown_, i.e. not hidden!
	if (isCurrent && IsCellEditControlShown())
	{
		cache.m_attr->GetEditorPtr(this, row, col)->PaintBackground(dc, cache.m_rect, *cache.m_attr);
	}
	else
	{
		// but all the rest is drawn by the cell renderer and hence may be customized
		cache.m_attr->GetRendererPtr(this, row, col)
			->Draw(*this, *cache.m_attr, dc, cache.m_rect, row, col, IsInSelection(coords));
	}
}

void ibGrid::DrawCellBorder(wxDC& dc, const ibGridCellCoords& coords, const wxRect& rect, const ibGridCellAttr* attr)
{
	if (GetColWidth(coords.GetCol()) <= 0 || GetRowHeight(coords.GetRow()) <= 0)
		return;

	//draw border  
	ibGridCellBorder borderLeft = attr->GetBorderLeft();
	if (borderLeft.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT)
	{
		dc.SetPen(wxPen(borderLeft.m_colour, borderLeft.m_width, borderLeft.m_style));

		if (m_gridLinesEnabled)
			dc.DrawLine(rect.GetLeft() - 1, rect.GetTop() - 1,
				rect.GetLeft() - 1, rect.GetBottom() + 1);
		else
			dc.DrawLine(rect.GetLeft(), rect.GetTop(),
				rect.GetLeft(), rect.GetBottom() + 1);
	}

	ibGridCellBorder borderRight = attr->GetBorderRight();
	if (borderRight.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT)
	{
		dc.SetPen(wxPen(borderLeft.m_colour, borderRight.m_width, borderRight.m_style));
		if (m_gridLinesEnabled)
			dc.DrawLine(rect.GetRight() + 1, rect.GetTop() - 1,
				rect.GetRight() + 1, rect.GetBottom() + 1);
		else
			dc.DrawLine(rect.GetRight() + 1, rect.GetTop(),
				rect.GetRight() + 1, rect.GetBottom() + 1);
	}

	ibGridCellBorder borderTop = attr->GetBorderTop();
	if (borderTop.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT)
	{
		dc.SetPen(wxPen(borderTop.m_colour, borderTop.m_width, borderTop.m_style));
		if (m_gridLinesEnabled)
			dc.DrawLine(rect.GetLeft() - 1, rect.GetTop() - 1,
				rect.GetRight() + 1, rect.GetTop() - 1);
		else
			dc.DrawLine(rect.GetLeft(), rect.GetTop(),
				rect.GetRight() + 1, rect.GetTop());
	}

	ibGridCellBorder borderBottom = attr->GetBorderBottom();
	if (borderBottom.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT)
	{
		dc.SetPen(wxPen(borderBottom.m_colour, borderBottom.m_width, borderBottom.m_style));
		if (m_gridLinesEnabled)
			dc.DrawLine(rect.GetLeft() - 1, rect.GetBottom() + 1,
				rect.GetRight() + 1, rect.GetBottom() + 1);
		else
			dc.DrawLine(rect.GetLeft(), rect.GetBottom() + 1,
				rect.GetRight() + 1, rect.GetBottom() + 1);
	}
}

void ibGrid::DrawCellHighlight(wxDC& dc, int row, int col, const ibGridCellAttr* attr)
{
	if (GetColWidth(col) <= 0 || GetRowHeight(row) <= 0)
		return;

	// hmmm... what could we do here to show that the cell is disabled?
	// for now, I just draw a thinner border than for the other ones, but
	// it doesn't look really good

	wxRect rect = CellToRect(row, col);

	////////////////////////////////////////////////////////////////////////
	// Draw selected lines 

	if (!IsInSelection(row, col - 1))
	{
		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rect.GetLeft(), rect.GetTop() - 1,
			rect.GetLeft(), rect.GetBottom());
	}

	if (!IsInSelection(row, col + 1)) // !!!
	{
		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rect.GetRight() + 1, rect.GetTop() - 1,
			rect.GetRight() + 1, rect.GetBottom());
	}

	if (!IsInSelection(row - 1, col))
	{
		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rect.GetLeft() - 1, rect.GetTop(),
			rect.GetRight() + 1, rect.GetTop());
	}

	if (!IsInSelection(row + 1, col)) // !!!
	{
		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rect.GetLeft() - 1, rect.GetBottom(),
			rect.GetRight() + 1, rect.GetBottom());
	}

}

wxPen ibGrid::GetDefaultGridLinePen()
{
	return wxPen(GetGridLineColour());
}

wxPen ibGrid::GetRowGridLinePen(int WXUNUSED(row))
{
	return GetDefaultGridLinePen();
}

wxPen ibGrid::GetColGridLinePen(int WXUNUSED(col))
{
	return GetDefaultGridLinePen();
}

void ibGrid::DrawBorder(wxDC& dc, const ibGridCellCacheArray& storage)
{
	// if the active cell was repainted, repaint its highlight too because it
	// might have been damaged by the grid lines
	size_t count = storage.GetCount();
	for (size_t n = 0; n < count; n++)
	{
		const ibGridCellCache& cache = storage[n];
		const ibGridCellAttrPtr& attr = cache.m_attr;

		if (attr && attr->HasAnyBorder())
		{
			DrawCellBorder(dc, cache.m_coords, cache.m_rect, attr.get());
		}
	}
}

void ibGrid::DrawHighlight(wxDC& dc, const ibGridCellCoordsArray& cells)
{
	// This if block was previously in wxGrid::OnPaint but that doesn't
	// seem to get called under wxGTK - MB
	//
	if (m_currentCellCoords == ibGridNoCellCoords &&
		m_numRows && m_numCols)
	{
		m_currentCellCoords.Set(0, 0);
	}

	if (IsCellEditControlShown())
	{
		// don't show highlight when the edit control is shown
		return;
	}

	// if the active cell was repainted, repaint its highlight too because it
	// might have been damaged by the grid lines
	size_t count = cells.GetCount();
	for (size_t n = 0; n < count; n++)
	{
		const ibGridCellCoords& cell = cells[n];
		if (m_currentCellCoords == cell || (m_selection && m_selection->IsInSelection(cell)))
		{
			ibGridCellAttrPtr attr = GetCellAttrPtr(cell);
			DrawCellHighlight(dc, cell.GetRow(), cell.GetCol(), attr.get());
		}
	}
}

void ibGrid::DrawFrozenBorder(wxDC& dc, ibGridWindow* gridWindow)
{
	if (gridWindow && m_numCols && m_numRows)
	{
		int top, bottom, left, right;
		int cw, ch;
		wxPoint gridOffset = GetGridWindowOffset(gridWindow);
		gridWindow->GetClientSize(&cw, &ch);
		CalcGridWindowUnscrolledPosition(gridOffset.x, gridOffset.y, &left, &top, gridWindow);
		CalcGridWindowUnscrolledPosition(cw + gridOffset.x, ch + gridOffset.y, &right, &bottom, gridWindow);

		if (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow)
		{
			right = wxMin(right, GetColRight(m_numCols - 1, GetGridZoom()));

			dc.SetPen(wxPen(m_gridFrozenBorderColour,
				m_gridFrozenBorderPenWidth));
			dc.DrawLine(left, bottom, right, bottom);
		}

		if (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol)
		{
			bottom = wxMin(bottom, GetRowBottom(m_numRows - 1, GetGridZoom()));

			dc.SetPen(wxPen(m_gridFrozenBorderColour,
				m_gridFrozenBorderPenWidth));
			dc.DrawLine(right, top, right, bottom);
		}
	}
}

void ibGrid::DrawLabelFrozenBorder(wxDC& dc, wxWindow* window, bool isRow)
{
	if (window)
	{
		int cw, ch;
		window->GetClientSize(&cw, &ch);

		dc.SetPen(wxPen(m_gridFrozenBorderColour,
			m_gridFrozenBorderPenWidth));

		if (isRow)
			dc.DrawLine(0, ch, cw, ch);
		else
			dc.DrawLine(cw, 0, cw, ch);
	}
}

// Used by ibGrid::Render() to draw the grid lines only for the cells in the
// specified range.
void
ibGrid::DrawRangeGridLines(wxDC& dc,
	const wxRegion& reg,
	const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight)
{
	if (!m_gridLinesEnabled)
		return;

	int top, left, width, height;
	reg.GetBox(left, top, width, height);

	// create a clipping region
	wxRegion clippedcells(dc.LogicalToDeviceX(left),
		dc.LogicalToDeviceY(top),
		dc.LogicalToDeviceXRel(width),
		dc.LogicalToDeviceYRel(height));

	// subtract multi cell span area from clipping region for lines
	wxRect rect;
	for (int row = topLeft.GetRow(); row <= bottomRight.GetRow(); row++)
	{
		for (int col = topLeft.GetCol(); col <= bottomRight.GetCol(); col++)
		{
			int cell_rows, cell_cols;
			switch (GetCellSize(row, col, &cell_rows, &cell_cols))
			{
			case CellSpan_Main: // multi cell
				rect = CellToRect(row, col);
				// cater for scaling
				// device origin already set in ::Render() for x, y
				rect.x = dc.LogicalToDeviceX(rect.x);
				rect.y = dc.LogicalToDeviceY(rect.y);
				rect.width = dc.LogicalToDeviceXRel(rect.width);
				rect.height = dc.LogicalToDeviceYRel(rect.height) - 1;
				clippedcells.Subtract(rect);
				break;

			case CellSpan_Inside: // part of multicell
				rect = CellToRect(row + cell_rows, col + cell_cols);
				rect.x = dc.LogicalToDeviceX(rect.x);
				rect.y = dc.LogicalToDeviceY(rect.y);
				rect.width = dc.LogicalToDeviceXRel(rect.width);
				rect.height = dc.LogicalToDeviceYRel(rect.height) - 1;
				clippedcells.Subtract(rect);
				break;

			case CellSpan_None:
				// Nothing special to do.
				break;
			}
		}
	}

	dc.SetDeviceClippingRegion(clippedcells);

	DoDrawGridLines(dc,
		top, left, top + height, left + width,
		topLeft.GetRow(), topLeft.GetCol(),
		bottomRight.GetRow(), bottomRight.GetCol());

	dc.DestroyClippingRegion();

	DoDrawGridNonClippingRegionLines(dc,
		top, left, top + height, left + width,
		topLeft.GetRow(), topLeft.GetCol(),
		bottomRight.GetRow(), bottomRight.GetCol());
}

// This is used to redraw all grid lines e.g. when the grid line colour
// has been changed
//
void ibGrid::DrawAllGridWindowLines(wxDC& dc, const wxRegion& WXUNUSED(reg), ibGridWindow* gridWindow)
{
	if (!m_gridLinesEnabled || !gridWindow)
		return;

	int top, bottom, left, right;

	wxPoint gridOffset = GetGridWindowOffset(gridWindow);

	int cw, ch;
	gridWindow->GetClientSize(&cw, &ch);
	CalcGridWindowUnscrolledPosition(gridOffset.x, gridOffset.y, &left, &top, gridWindow);
	CalcGridWindowUnscrolledPosition(cw + gridOffset.x, ch + gridOffset.y, &right, &bottom, gridWindow);

	// avoid drawing grid lines past the last row and col
	if (m_gridLinesClipHorz)
	{
		if (!m_numCols)
			return;

		const int lastColRight = GetColRight(GetColAt(m_numCols - 1), GetGridZoom());
		if (right > lastColRight)
			right = lastColRight;
	}

	if (m_gridLinesClipVert)
	{
		if (!m_numRows)
			return;

		const int lastRowBottom = GetRowBottom(GetRowAt(m_numRows - 1), GetGridZoom());
		if (bottom > lastRowBottom)
			bottom = lastRowBottom;
	}

	// no gridlines inside multicells, clip them out
	int leftCol = GetColPos(internalXToCol(left, gridWindow));
	int topRow = GetRowPos(internalYToRow(top, gridWindow));
	int rightCol = GetColPos(internalXToCol(right, gridWindow));
	int bottomRow = GetRowPos(internalYToRow(bottom, gridWindow));

	if (gridWindow != NULL)
	{
		wxRegion clippedcells(0, 0, cw, ch);

		int cell_rows, cell_cols;
		wxRect rect;

		for (int j = topRow; j <= bottomRow; j++)
		{
			for (int colPos = leftCol; colPos <= rightCol; colPos++)
			{
				int i = GetColAt(colPos);

				switch (GetCellSize(j, i, &cell_rows, &cell_cols))
				{
				case CellSpan_Main:
					rect = CellToRect(j, i);
					rect.Offset(-gridOffset);
					CalcGridWindowScrolledPosition(rect.x, rect.y, &rect.x, &rect.y, gridWindow);
					clippedcells.Subtract(rect);
					break;

				case CellSpan_Inside:
					rect = CellToRect(j + cell_rows, i + cell_cols);
					rect.Offset(-gridOffset);
					CalcGridWindowScrolledPosition(rect.x, rect.y, &rect.x, &rect.y, gridWindow);
					clippedcells.Subtract(rect);
					break;

				case CellSpan_None:
					// Nothing special to do.
					break;
				}
			}
		}

		dc.SetDeviceClippingRegion(clippedcells);
	}

	DoDrawGridLines(dc,
		top, left, bottom, right,
		topRow, leftCol, m_numRows, m_numCols);

	dc.DestroyClippingRegion();

	DoDrawGridNonClippingRegionLines(dc,
		top, left, bottom, right,
		topRow, leftCol, m_numRows, m_numCols);
}

void ibGrid::DrawAllGridLines()
{
	if (m_gridWin)
	{
		wxClientDC dc(m_gridWin);
		PrepareDCFor(dc, m_gridWin);

		DrawAllGridWindowLines(dc, wxRegion(), m_gridWin);
	}

	if (m_frozenRowGridWin)
	{
		wxClientDC dc(m_frozenRowGridWin);
		PrepareDCFor(dc, m_frozenRowGridWin);

		DrawAllGridWindowLines(dc, wxRegion(), m_frozenRowGridWin);
	}

	if (m_frozenColGridWin)
	{
		wxClientDC dc(m_frozenColGridWin);
		PrepareDCFor(dc, m_frozenColGridWin);

		DrawAllGridWindowLines(dc, wxRegion(), m_frozenColGridWin);
	}

	if (m_frozenCornerGridWin)
	{
		wxClientDC dc(m_frozenCornerGridWin);
		PrepareDCFor(dc, m_frozenCornerGridWin);

		DrawAllGridWindowLines(dc, wxRegion(), m_frozenCornerGridWin);
	}
}

void
ibGrid::DoDrawGridLines(wxDC& dc,
	int top, int left,
	int bottom, int right,
	int topRow, int leftCol,
	int bottomRow, int rightCol)
{
	// horizontal grid lines
	for (int rowPos = topRow; rowPos < bottomRow; rowPos++)
	{
		int i = GetRowAt(rowPos);
		int bot = GetRowBottom(i, GetGridZoom()) - 1;

		if (bot > bottom)
			break;

		if (bot >= top)
		{
			dc.SetPen(GetRowGridLinePen(i));
			dc.DrawLine(left, bot, right, bot);
		}
	}

	// vertical grid lines
	for (int colPos = leftCol; colPos < rightCol; colPos++)
	{
		int i = GetColAt(colPos);

		int colRight = GetColRight(i, GetGridZoom());

#if defined(__WXGTK__) || defined(__WXQT__)
		if (GetLayoutDirection() != wxLayout_RightToLeft)
#endif
			colRight--;

		if (colRight > right)
			break;

		if (colRight >= left)
		{
			dc.SetPen(GetColGridLinePen(i));
			dc.DrawLine(colRight, top, colRight, bottom);
		}
	}
}

void
ibGrid::DoDrawGridNonClippingRegionLines(wxDC& dc,
	int top, int left,
	int bottom, int right,
	int topRow, int leftCol,
	int bottomRow, int rightCol)
{
	// horizontal grid lines
	for (int rowPos = topRow; rowPos < bottomRow; rowPos++)
	{
		int i = GetRowAt(rowPos);
		int bot = GetRowBottom(i, GetGridZoom()) - 1;

		//draw col print brake
		if (IsRowBrake(i))
		{
			dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_SHORT_DASH));
			dc.DrawLine(left, bot, right, bot);
		}

		if (GridRowAreaEnabled())
		{
			//draw area 
			const int flagRowSection = GetRowAreaValue(i);

			// top line
			if (flagRowSection & 2) {
				int rowTop = GetRowTop(i, GetGridZoom()) - 1;
				dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
				dc.DrawLine(left, rowTop, right, rowTop);
			}

			// bottom line
			if (flagRowSection & 4) {
				dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
				dc.DrawLine(left, bot, right, bot);
			}
		}
	}

	// vertical grid lines
	for (int colPos = leftCol; colPos < rightCol; colPos++)
	{
		int i = GetColAt(colPos);

		int colRight = GetColRight(i, GetGridZoom());

#if defined(__WXGTK__) || defined(__WXQT__)
		if (GetLayoutDirection() != wxLayout_RightToLeft)
#endif
			colRight--;

		//draw col print brake
		if (IsColBrake(i))
		{
			dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_SHORT_DASH));
			dc.DrawLine(colRight, top, colRight, bottom);
		}

		if (GridColAreaEnabled())
		{
			const int flagColSection = GetColAreaValue(i);

			// left hand line
			if (flagColSection & 2) {
				int colLeft = GetColLeft(i, GetGridZoom()) - 1;
				dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
				dc.DrawLine(colLeft, top, colLeft, bottom);
			}

			// right hand line
			if (flagColSection & 4) {
				dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
				dc.DrawLine(colRight, top, colRight, bottom);
			}
		}
	}
}

void ibGrid::DrawRowAreas(wxDC& dc, const wxArrayInt& rows)
{
	if (!m_numRows)
		return;

	const size_t numLabels = rows.GetCount();
	for (size_t i = 0; i < numLabels; i++)
	{
		DrawRowArea(dc, rows[i]);
	}

	ibGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;

	int hAlign, vAlign;
	ibGrid::GetRowLabelAlignment(&hAlign, &vAlign);

	for (size_t pos = 0; pos < m_rowAreaAt.size(); pos++) {

		size_t rowHeight = 0;

		const ibGridCellArea& entry = m_rowAreaAt[pos];
		for (int row = entry.m_start; row <= entry.m_end; row++) {
			rowHeight += ibGrid::GetRowHeight(row, GetGridZoom());
		}

		// notice that an explicit static_cast is needed to avoid a compilation
		// error with VC7.1 which, for some reason, tries to instantiate (abstract)
		// ibGridRowHeaderRenderer class without it
		const ibGridRowHeaderRenderer&
			rend = attrProvider ? attrProvider->GetRowHeaderRenderer(entry.m_start)
			: static_cast<const ibGridRowHeaderRenderer&>
			(gs_defaultHeaderRenderers.rowRenderer);

		wxRect rect(0, GetRowTop(entry.m_start, GetGridZoom()), ((GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0)), rowHeight);
		rend.DrawLabel(*this, dc, entry.m_areaLabel,
			rect, hAlign, vAlign, wxHORIZONTAL);
	}
}

void ibGrid::DrawRowArea(wxDC& dc, int row)
{
	if (m_rowAreaWin != NULL) {

		static wxPen sectionPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));

		wxRect rect(0, GetRowTop(row, GetGridZoom()), (GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0), GetRowHeight(row, GetGridZoom()));
		const int flagSection = GetRowAreaValue(row);

		if (flagSection & 2) {
			dc.SetPen(sectionPen);
			dc.DrawLine(rect.GetLeft(), rect.GetTop() == 0 ? 0 : rect.GetTop() - 1, rect.GetRight() + 1, rect.GetTop() == 0 ? 0 : rect.GetTop() - 1);
		}

		if (flagSection & 3) {
			dc.SetPen(sectionPen);
			dc.DrawLine(rect.GetLeft(), rect.GetTop() == 0 ? 0 : rect.GetTop() - 1, rect.GetLeft(), rect.GetBottom() + 1);
		}

		if (flagSection & 4) {
			dc.SetPen(sectionPen);
			dc.DrawLine(rect.GetLeft(), rect.GetBottom(), rect.GetRight() + 1, rect.GetBottom());
		}
	}
}

void ibGrid::DrawRowLabels(wxDC& dc, const wxArrayInt& rows)
{
	if (!m_numRows)
		return;

	const size_t numLabels = rows.GetCount();
	for (size_t i = 0; i < numLabels; i++)
	{
		DrawRowLabel(dc, rows[i]);
	}
}

void ibGrid::DrawRowLabel(wxDC& dc, int row)
{
	if (GetRowHeight(row) <= 0 || m_rowLabelWidth <= 0)
		return;

	ibGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;

	// notice that an explicit static_cast is needed to avoid a compilation
	// error with VC7.1 which, for some reason, tries to instantiate (abstract)
	// ibGridRowHeaderRenderer class without it
	const ibGridRowHeaderRenderer&
		rend = attrProvider ? attrProvider->GetRowHeaderRenderer(row)
		: static_cast<const ibGridRowHeaderRenderer&>
		(gs_defaultHeaderRenderers.rowRenderer);

	wxRect rect(0, GetRowTop(row, GetGridZoom()), ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), GetRowHeight(row, GetGridZoom()));

	if (m_cursorMode == WXGRID_CURSOR_MOVE_ROW)
	{
		// clear the background:
		// when called from ProcessRowColLabelMouseEvent the background is not
		// cleared at this point
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(m_colLabelWin->GetBackgroundColour());
		dc.DrawRectangle(rect);
	}

	// draw a border if the row is not being drag-moved
	// (in that case it's omitted to have a 'pressed' appearance)
	if (m_cursorMode != WXGRID_CURSOR_MOVE_ROW || row != m_dragMoveRowOrCol)
		rend.DrawBorder(*this, dc, rect);
	else
	{
		// just highlight the current row
		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
		dc.DrawRectangle(rect);
		rect.Deflate(GetBorder() == wxBORDER_NONE ? 2 : 1);
	}

	int hAlign, vAlign;
	GetRowLabelAlignment(&hAlign, &vAlign);

	rend.DrawLabel(*this, dc, GetRowLabelValue(row),
		rect, hAlign, vAlign, wxHORIZONTAL);
}

bool ibGrid::UseNativeColHeader(bool native)
{
	if (native == m_useNativeHeader)
		return true;

	// Using native control doesn't work if any columns are frozen currently.
	if (native && m_numFrozenCols)
		return false;

	delete m_colLabelWin;
	m_useNativeHeader = native;

	CreateColumnWindow();

	if (m_useNativeHeader)
	{
		SetNativeHeaderColCount();

		wxHeaderCtrl* const colHeader = GetGridColHeader();
		colHeader->SetBackgroundColour(GetLabelBackgroundColour());
		colHeader->SetForegroundColour(GetLabelTextColour());
		colHeader->SetFont(GetLabelFont(GetGridZoom()));
	}

	CalcWindowSizes();

	return true;
}

void ibGrid::SetUseNativeColLabels(bool native)
{
	wxASSERT_MSG(!m_useNativeHeader,
		"doesn't make sense when using native header");

	m_nativeColumnLabels = native;
	if (native)
	{
		int height = wxRendererNative::Get().GetHeaderButtonHeight(this);
		SetColLabelSize(height);
	}

	GetColLabelWindow()->Refresh();
	m_cornerLabelWin->Refresh();
}

void ibGrid::DrawColAreas(wxDC& dc, const wxArrayInt& cols)
{
	if (!m_numCols)
		return;

	const size_t numLabels = cols.GetCount();
	for (size_t i = 0; i < numLabels; i++)
	{
		DrawColArea(dc, cols[i]);
	}

	if (m_colAreaWin != NULL) {

		ibGridCellAttrProvider* const
			attrProvider = m_table ? m_table->GetAttrProvider() : NULL;

		int hAlign, vAlign;
		ibGrid::GetColLabelAlignment(&hAlign, &vAlign);

		for (size_t pos = 0; pos < m_colAreaAt.size(); pos++) {

			size_t colWidth = 0;

			const ibGridCellArea& entry = m_colAreaAt[pos];
			for (int col = entry.m_start; col <= entry.m_end; col++) {
				colWidth += ibGrid::GetColWidth(col, GetGridZoom());
			}

			const ibGridColumnHeaderRenderer&
				rend = attrProvider ? attrProvider->GetColumnHeaderRenderer(entry.m_start)
				: static_cast<ibGridColumnHeaderRenderer&>
				(gs_defaultHeaderRenderers.colRenderer);

			wxRect rect(GetColLeft(entry.m_start, GetGridZoom()), 0, colWidth, (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0));
			rend.DrawLabel(*this, dc, entry.m_areaLabel,
				rect, hAlign, vAlign, wxHORIZONTAL);
		}
	}
}

void ibGrid::DrawColArea(wxDC& dc, int col)
{
	if (m_colAreaWin != NULL) {

		static wxPen sectionPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW));

		wxRect rect(GetColLeft(col, GetGridZoom()), 0, GetColWidth(col, GetGridZoom()), (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0));
		const int flagSection = GetColAreaValue(col);

		if (flagSection & 2) {
			dc.SetPen(sectionPen);
			dc.DrawLine(rect.GetLeft() == 0 ? 0 : rect.GetLeft() - 1, rect.GetBottom() + 1, rect.GetLeft() == 0 ? 0 : rect.GetLeft() - 1, rect.GetTop());
		}

		if (flagSection & 3) {
			dc.SetPen(sectionPen);
			dc.DrawLine(rect.GetLeft() == 0 ? 0 : rect.GetLeft() - 1, rect.GetTop(), rect.GetRight(), rect.GetTop());
		}

		if (flagSection & 4) {
			dc.SetPen(sectionPen);
			dc.DrawLine(rect.GetRight(), rect.GetTop(), rect.GetRight(), rect.GetBottom() + 1);
		}
	}
}

void ibGrid::DrawColLabels(wxDC& dc, const wxArrayInt& cols)
{
	if (!m_numCols)
		return;

	const size_t numLabels = cols.GetCount();
	for (size_t i = 0; i < numLabels; i++)
	{
		DrawColLabel(dc, cols[i]);
	}
}

void ibGrid::DrawCornerLabel(wxDC& dc)
{
	wxRect rect(wxSize(ibCalcGridScale(m_rowLabelWidth, GetGridZoom()), ibCalcGridScale(m_colLabelHeight, GetGridZoom())));

	ibGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;
	const ibGridCornerHeaderRenderer&
		rend = attrProvider ? attrProvider->GetCornerRenderer()
		: static_cast<ibGridCornerHeaderRenderer&>
		(gs_defaultHeaderRenderers.cornerRenderer);

	if (m_nativeColumnLabels)
	{
		rect.Deflate(1);

		wxRendererNative::Get().DrawHeaderButton(m_cornerLabelWin, dc, rect, 0);
	}
	else
	{
		rect.width++;
		rect.height++;

		rend.DrawBorder(*this, dc, rect);
	}

	wxString label = GetCornerLabelValue();
	if (!label.IsEmpty())
	{
		int hAlign, vAlign;
		GetCornerLabelAlignment(&hAlign, &vAlign);
		const int orient = GetCornerLabelTextOrientation();

		rend.DrawLabel(*this, dc, label, rect, hAlign, vAlign, orient);
	}
}

void ibGrid::DrawColLabel(wxDC& dc, int col)
{
	if (GetColWidth(col) <= 0 || m_colLabelHeight <= 0)
		return;

	int colLeft = GetColLeft(col, GetGridZoom());

	wxRect rect(colLeft, 0, GetColWidth(col, GetGridZoom()), ibCalcGridScale(m_colLabelHeight, GetGridZoom()));

	ibGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;
	const ibGridColumnHeaderRenderer&
		rend = attrProvider ? attrProvider->GetColumnHeaderRenderer(col)
		: static_cast<ibGridColumnHeaderRenderer&>
		(gs_defaultHeaderRenderers.colRenderer);

	if (m_nativeColumnLabels)
	{
		wxRendererNative::Get().DrawHeaderButton
		(
			GetColLabelWindow(),
			dc,
			rect,
			0,
			IsSortingBy(col)
			? IsSortOrderAscending()
			? wxHDR_SORT_ICON_UP
			: wxHDR_SORT_ICON_DOWN
			: wxHDR_SORT_ICON_NONE
		);
		rect.Deflate(2);
	}
	else
	{

		if (m_cursorMode == WXGRID_CURSOR_MOVE_COL)
		{
			// clear the background:
			// when called from ProcessRowColLabelMouseEvent the background
			// is not cleared at this point
			wxDCBrushChanger setBrush(dc, m_colLabelWin->GetBackgroundColour());
			wxDCPenChanger setPen(dc, *wxTRANSPARENT_PEN);
			dc.DrawRectangle(rect);
		}

		// draw a border if the column is not being drag-moved
		// (in that case it's omitted to have a 'pressed' appearance)
		if (m_cursorMode != WXGRID_CURSOR_MOVE_COL || col != m_dragMoveRowOrCol)
			rend.DrawBorder(*this, dc, rect);
		else
		{
			// just highlight the current column
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
			dc.DrawRectangle(rect);
			rect.Deflate(GetBorder() == wxBORDER_NONE ? 2 : 1);
		}
	}

	int hAlign, vAlign;
	GetColLabelAlignment(&hAlign, &vAlign);
	const int orient = GetColLabelTextOrientation();

	rend.DrawLabel(*this, dc, GetColLabelValue(col), rect, hAlign, vAlign, orient);
}

// TODO: these 2 functions should be replaced with wxDC::DrawLabel() to which
//       we just have to add textOrientation support
void ibGrid::DrawTextRectangle(wxDC& dc,
	const wxString& value,
	const wxRect& rect,
	int horizAlign,
	int vertAlign,
	int textOrientation)
{
	static wxArrayString lines;
	ParseLines(value, lines);
	DrawTextRectangle(dc, lines, rect, horizAlign, vertAlign, textOrientation);
}

void ibGrid::DrawTextRectangle(wxDC& dc,
	const wxArrayString& lines,
	const wxRect& rect,
	int horizAlign,
	int vertAlign,
	int textOrientation)
{
	if (lines.empty())
		return;

	wxDCClipper clip(dc, rect);

	long textWidth,
		textHeight;

	static wxArrayInt arrRow, arrCol;

	if (textOrientation == wxHORIZONTAL)
		GetTextBoxSize(dc, lines, &arrRow, &arrCol, &textWidth, &textHeight);
	else
		GetTextBoxSize(dc, lines, &arrRow, &arrCol, &textHeight, &textWidth);

	int x = 0,
		y = 0;
	switch (vertAlign)
	{
	case wxALIGN_BOTTOM:
		if (textOrientation == wxHORIZONTAL)
			y = rect.y + (rect.height - textHeight - GRID_TEXT_MARGIN);
		else
			x = rect.x + (rect.width - textWidth - GRID_TEXT_MARGIN);
		break;

	case wxALIGN_CENTRE:
		if (textOrientation == wxHORIZONTAL)
			y = rect.y + ((rect.height - textHeight) / 2);
		else
			x = rect.x + ((rect.width - textWidth) / 2);
		break;

	case wxALIGN_TOP:
	default:
		if (textOrientation == wxHORIZONTAL)
			y = rect.y + GRID_TEXT_MARGIN;
		else
			x = rect.x + GRID_TEXT_MARGIN;
		break;
	}

	// Align each line of a multi-line label
	size_t nLines = lines.GetCount();
	for (size_t l = 0; l < nLines; l++)
	{
		const wxString& line = lines[l];

		if (line.empty())
		{
			*(textOrientation == wxHORIZONTAL ? &y : &x) += dc.GetCharHeight();
			continue;
		}

		wxCoord lineWidth = arrRow.Item(l),
			lineHeight = arrCol.Item(l);

		//dc.GetTextExtent(line, &lineWidth, &lineHeight);

		switch (horizAlign)
		{
		case wxALIGN_RIGHT:
			if (textOrientation == wxHORIZONTAL)
				x = rect.x + (rect.width - lineWidth - GRID_TEXT_MARGIN);
			else
				y = rect.y + lineWidth + GRID_TEXT_MARGIN;
			break;

		case wxALIGN_CENTRE:
			if (textOrientation == wxHORIZONTAL)
				x = rect.x + ((rect.width - lineWidth) / 2);
			else
				y = rect.y + rect.height - ((rect.height - lineWidth) / 2);
			break;

		case wxALIGN_LEFT:
		default:
			if (textOrientation == wxHORIZONTAL)
				x = rect.x + GRID_TEXT_MARGIN;
			else
				y = rect.y + rect.height - GRID_TEXT_MARGIN;
			break;
		}

		if (textOrientation == wxHORIZONTAL)
		{
			dc.DrawText(line, x, y);
			y += lineHeight;
		}
		else
		{
			dc.DrawRotatedText(line, x, y, 90.0);
			x += lineHeight;
		}
	}
}

void ibGrid::DrawTextRectangle(wxDC& dc,
	const wxString& text,
	const wxRect& rect,
	const ibGridCellAttr& attr,
	int hAlign,
	int vAlign)
{
	attr.GetNonDefaultAlignment(&hAlign, &vAlign);

	const wxEllipsizeMode mode = attr.GetFitMode().GetEllipsizeMode();
	if (mode != wxEllipsizeMode::wxELLIPSIZE_NONE) {

		// This does nothing if there is no need to ellipsize.
		const wxString& ellipsizedText = wxControl::Ellipsize
		(
			text,
			dc,
			attr.GetFitMode().GetEllipsizeMode(),
			rect.GetWidth() - 2 * GRID_TEXT_MARGIN,
			wxELLIPSIZE_FLAGS_NONE
		);

		DrawTextRectangle(dc, ellipsizedText, rect, hAlign, vAlign, attr.GetTextOrient());
	}
	else {

		DrawTextRectangle(dc, text, rect, hAlign, vAlign, attr.GetTextOrient());
	}
}

// Split multi-line text up into an array of strings.
// Any existing contents of the string array are preserved.
//
// TODO: refactor wxTextFile::Read() and reuse the same code from here
void ibGrid::ParseLines(const wxString& value, wxArrayString& lines)
{
	lines.Empty();

	if (!value.IsEmpty()) {

		static wxString strRaw;
		static bool bInQuote = false;

		for (const auto& c : value.ToStdWstring()) {
			strRaw += c;
			if (c == wxT('\'')) {
				bInQuote = !bInQuote;
			}
			else if (c == wxT('\n') && !bInQuote) {

				if (!strRaw.IsEmpty())
					lines.Add(strRaw);

				strRaw.Clear();
			}
		}

		if (!strRaw.IsEmpty())
			lines.Add(strRaw);

		strRaw.Clear();
	}
}

void ibGrid::GetTextBoxSize(const wxDC& dc,
	const wxArrayString& lines,
	wxArrayInt* arrRow, wxArrayInt* arrCol,
	long* width, long* height)
{
	wxCoord w = 0;
	wxCoord h = 0;
	wxCoord lineW = 0, lineH = 0;

	size_t i;
	size_t count = lines.GetCount();

	if (arrRow)
	{
		arrRow->SetCount(0);
		arrRow->Alloc(count);
	}

	if (arrCol)
	{
		arrCol->SetCount(0);
		arrCol->Alloc(count);
	}

	for (i = 0; i < count; i++)
	{
		if (lines[i].empty())
		{
			const int char_height = dc.GetCharHeight();

			// GetTextExtent() would return 0 for empty lines, but we still
			// need to account for their height.
			h += char_height;

			if (arrRow) arrRow->Add(0);
			if (arrCol) arrCol->Add(char_height);
		}
		else
		{
			dc.GetTextExtent(lines[i], &lineW, &lineH);
			w = wxMax(w, lineW);
			h += lineH;

			if (arrRow) arrRow->Add(w);
			if (arrCol) arrCol->Add(lineH);
		}
	}

	*width = w;
	*height = h;
}

//
// ------ Batch processing.
//
void ibGrid::EndBatch()
{
	if (m_batchCount > 0)
	{
		m_batchCount--;
		if (!m_batchCount)
		{
			CalcDimensions();
			Refresh();
		}
	}
}

// Use this, rather than wxWindow::Refresh(), to force an immediate
// repainting of the grid. Has no effect if you are already inside a
// BeginBatch / EndBatch block.
//
void ibGrid::ForceRefresh()
{
	BeginBatch();
	EndBatch();
}

void ibGrid::DoEnable(bool enable)
{
	wxScrolledCanvas::DoEnable(enable);

	Refresh(false /* don't erase background */);
}

//
// ------ Edit control functions
//

void ibGrid::EnableEditing(bool edit)
{
	if (edit != m_editable)
	{
		if (!edit)
			EnableCellEditControl(edit);
		m_editable = edit;
	}
}

void ibGrid::EnableCellEditControl(bool enable)
{
	if (!m_editable)
		return;

	if (enable != m_cellEditCtrlEnabled)
	{
		if (enable)
		{
			// this should be checked by the caller!
			wxCHECK_RET(CanEnableCellControl(), wxT("can't enable editing for this cell!"));

			DoEnableCellEditControl(ibGridActivationSource::FromProgram());
		}
		else
		{
			DoDisableCellEditControl();
		}
	}
}

bool ibGrid::DoEnableCellEditControl(const ibGridActivationSource& actSource)
{
	switch (SendEvent(wxEVT_GRID_EDITOR_SHOWN))
	{
	case Event_Vetoed:
	case Event_CellDeleted:
		// We shouldn't do anything if the event was vetoed and can't do
		// anything if the cell doesn't exist any longer.
		return false;

	case Event_Unhandled:
	case Event_Handled:
		// But it doesn't matter here if the event was skipped or not.
		break;
	}

	if (!DoShowCellEditControl(actSource))
	{
		// We have to send the HIDDEN event matching the SHOWN one above as the
		// user code may reasonably expect always getting them in pairs, so do
		// it even if the editor hadn't really been shown at all.
		SendEvent(wxEVT_GRID_EDITOR_HIDDEN);

		return false;
	}

	return true;
}

void ibGrid::DoDisableCellEditControl()
{
	SendEvent(wxEVT_GRID_EDITOR_HIDDEN);

	DoAcceptCellEditControl();
}

bool ibGrid::IsCurrentCellReadOnly() const
{
	return const_cast<ibGrid*>(this)->
		GetCellAttrPtr(m_currentCellCoords)->IsReadOnly();
}

bool ibGrid::CanEnableCellControl() const
{
	return m_editable && (m_currentCellCoords != ibGridNoCellCoords) &&
		!IsCurrentCellReadOnly();
}

bool ibGrid::IsCellEditControlShown() const
{
	bool isShown = false;

	if (m_cellEditCtrlEnabled)
	{
		if (ibGridCellEditorPtr editor = GetCurrentCellEditorPtr())
		{
			if (editor->IsCreated())
			{
				isShown = editor->GetWindow()->IsShown();
			}
		}
	}

	return isShown;
}

void ibGrid::ShowCellEditControl()
{
	if (IsCellEditControlEnabled())
	{
		if (!IsVisible(m_currentCellCoords, false))
		{
			m_cellEditCtrlEnabled = false;
			return;
		}

		DoShowCellEditControl(ibGridActivationSource::FromProgram());
	}
}

bool ibGrid::DoShowCellEditControl(const ibGridActivationSource& actSource)
{
	wxRect rect = CellToRect(m_currentCellCoords);
	int row = m_currentCellCoords.GetRow();
	int col = m_currentCellCoords.GetCol();

	ibGridCellAttrPtr attr = GetCellAttrPtr(row, col);
	ibGridCellEditorPtr editor = attr->GetEditorPtr(this, row, col);

	const ibGridActivationResult&
		res = editor->TryActivate(row, col, this, actSource);
	switch (res.GetAction())
	{
	case ibGridActivationResult::Change:
		// This is somewhat similar to what DoSaveEditControlValue() does.
		// but we don't allow vetoing CHANGED event here as this code is
		// new and shouldn't have to support this obsolete usage.
		switch (SendEvent(wxEVT_GRID_CELL_CHANGING, res.GetNewValue()))
		{
		case Event_Vetoed:
		case Event_CellDeleted:
			break;

		case Event_Unhandled:
		case Event_Handled:
			const wxString& oldval = GetCellValue(m_currentCellCoords);

			editor->DoActivate(row, col, this);

			// Show the new cell value.
			RefreshBlock(m_currentCellCoords, m_currentCellCoords);

			if (SendEvent(wxEVT_GRID_CELL_CHANGED, oldval) == Event_Vetoed)
			{
				wxFAIL_MSG("Vetoing wxEVT_GRID_CELL_CHANGED is ignored");
			}
			break;
		}
		wxFALLTHROUGH;

	case ibGridActivationResult::Ignore:
		// In any case, don't start editing normally.
		return false;

	case ibGridActivationResult::ShowEditor:
		// Continue normally.
		break;
	}

	// It's not enabled just yet, but will be soon, and we need to set it
	// before generating any events in case their user-defined handlers decide
	// to call EnableCellEditControl() to avoid reentrancy problems.
	m_cellEditCtrlEnabled = true;

	ibGridWindow* gridWindow = CellToGridWindow(row, col);

	// if this is part of a multicell, find owner (topleft)
	int cell_rows, cell_cols;
	if (GetCellSize(row, col, &cell_rows, &cell_cols) == CellSpan_Inside)
	{
		row += cell_rows;
		col += cell_cols;
		m_currentCellCoords.SetRow(row);
		m_currentCellCoords.SetCol(col);
	}

	rect.Offset(-GetGridWindowOffset(gridWindow));

	// convert to scrolled coords
	CalcGridWindowScrolledPosition(rect.x, rect.y, &rect.x, &rect.y, gridWindow);

#ifdef __WXQT__
	// Substract 1 pixel in every dimension to fit in the cell area.
	// If not, Qt will draw the control outside the cell.
	// TODO: Check offsets under Qt.
	rect.Deflate(1, 1);
#endif

	if (!editor->IsCreated())
	{
		editor->Create(gridWindow, wxID_ANY,
			new ibGridCellEditorEvtHandler(this, editor.get()));

		// Ensure the editor window has wxWANTS_CHARS flag, so that it
		// gets Tab, Enter and Esc keys, which need to be processed
		// specially by ibGridCellEditorEvtHandler.
		wxWindow* const editorWindow = editor->GetWindow();
		if (editorWindow)
		{
			editorWindow->SetWindowStyle(editorWindow->GetWindowStyle()
				| wxWANTS_CHARS);
		}

		ibGridEditorCreatedEvent evt(GetId(),
			wxEVT_GRID_EDITOR_CREATED,
			this,
			row,
			col,
			editorWindow);

		ProcessWindowEvent(evt);
	}
	else if (editor->GetWindow() &&
		editor->GetWindow()->GetParent() != gridWindow)
	{
		editor->GetWindow()->Reparent(gridWindow);
	}


	// resize editor to overflow into righthand cells if allowed
	int maxWidth = rect.width, maxHeight = rect.height;
	wxString value = GetCellValue(row, col);
	if (!value.empty() && attr->GetOverflow())
	{
		static wxMemoryDC dc;

		dc.GetMultiLineTextExtent(value, &maxWidth, &maxHeight, NULL, &attr->GetFont(GetGridZoom()));

		if (maxWidth < rect.width)
			maxWidth = rect.width;

		if (maxHeight < rect.height)
			maxHeight = rect.height;
	}

	if ((maxWidth > rect.width) && (col < m_numCols) && m_table)
	{
		GetCellSize(row, col, &cell_rows, &cell_cols);

		// may have changed earlier
		for (int i = col + cell_cols; i < m_numCols; i++)
		{
			int c_rows, c_cols;
			GetCellSize(row, i, &c_rows, &c_cols);

			// looks weird going over a multicell
			if (m_table->IsEmptyCell(row, i) &&
				(rect.width < maxWidth) && (c_rows == 1))
			{
				rect.width += GetColWidth(i, GetGridZoom());
			}
			else
				break;
		}
	}

	if ((maxHeight > rect.height) && (row < m_numRows) && m_table)
	{
		GetCellSize(row, col, &cell_rows, &cell_cols);

		// may have changed earlier
		for (int i = row + cell_rows; i < m_numRows; i++)
		{
			int c_rows, c_cols;
			GetCellSize(i, col, &c_rows, &c_cols);

			// looks weird going over a multicell
			if (m_table->IsEmptyCell(i, col) &&
				(rect.height < maxHeight) && (c_cols == 1))
			{
				rect.height += GetRowHeight(i, GetGridZoom());
			}
			else
				break;
		}
	}

	editor->SetCellAttr(attr.get());
	editor->SetSize(rect);


	// Note that the actual rectangle used by the editor could be
	// different from the one we proposed.
	rect = editor->GetWindow()->GetRect();

	// Ensure that the edit control fits into the visible part of the
	// window by shifting it if necessary: we don't want to truncate
	// any part of it by trying to position it too far to the left or
	// top and we definitely don't want to start showing scrollbars by
	// positioning it too far to the right or bottom.
	const wxSize sizeMax = gridWindow->GetClientSize();
	if (!wxRect(sizeMax).Contains(rect))
	{
		if (rect.x < 0)
			rect.x = 0;
		if (rect.y < 0)
			rect.y = 0;
		if (rect.x > sizeMax.x - rect.width)
			rect.x = sizeMax.x - rect.width;
		if (rect.y > sizeMax.y - rect.height)
			rect.y = sizeMax.y - rect.height;

		editor->GetWindow()->Move(rect.x, rect.y);
	}

	editor->Show(true, attr.get(), GetGridZoom());

	// recalc dimensions in case we need to
	// expand the scrolled window to account for editor
	CalcDimensions();

	editor->BeginEdit(row, col, this);
	editor->SetCellAttr(NULL);

	return true;
}

void ibGrid::HideCellEditControl()
{
	if (IsCellEditControlEnabled())
	{
		DoHideCellEditControl();
	}
}

void ibGrid::DoHideCellEditControl()
{
	ibGridCellEditorPtr editor = GetCurrentCellEditorPtr();
	const bool editorHadFocus = editor->GetWindow()->IsDescendant(FindFocus());

	if (editor->GetWindow()->GetParent() != m_gridWin)
		editor->GetWindow()->Reparent(m_gridWin);

	editor->Show(false);

	ibGridWindow* gridWindow = CellToGridWindow(m_currentCellCoords);
	// return the focus to the grid itself if the editor had it
	//
	// note that we must not do this unconditionally to avoid stealing
	// focus from the window which just received it if we are hiding the
	// editor precisely because we lost focus
	if (editorHadFocus)
		gridWindow->SetFocus();

	// refresh whole row to the right
	wxRect rect(CellToRect(m_currentCellCoords));
	rect.Offset(-GetGridWindowOffset(gridWindow));
	CalcGridWindowScrolledPosition(rect.x, rect.y, &rect.x, &rect.y, gridWindow);
	rect.width = gridWindow->GetClientSize().GetWidth() - rect.x;

#ifdef __WXOSX__
	// ensure that the pixels under the focus ring get refreshed as well
	rect.Inflate(10, 10);
#endif

	gridWindow->Refresh(false, &rect);

	// refresh also the grid to the right
	ibGridWindow* rightGridWindow = NULL;
	if (gridWindow->GetType() == ibGridWindow::ibGridWindowFrozenCorner)
		rightGridWindow = m_frozenRowGridWin;
	else if (gridWindow->GetType() == ibGridWindow::ibGridWindowFrozenCol)
		rightGridWindow = m_gridWin;

	if (rightGridWindow)
	{
		rect.x = 0;
		rect.width = rightGridWindow->GetClientSize().GetWidth();
		rightGridWindow->Refresh(false, &rect);
	}
}

void ibGrid::AcceptCellEditControlIfShown()
{
	if (IsCellEditControlShown())
	{
		DoAcceptCellEditControl();
	}
}

void ibGrid::DoAcceptCellEditControl()
{
	// Reset it first to avoid any problems with recursion via
	// DisableCellEditControl() if it's called from the user-defined event
	// handlers.
	m_cellEditCtrlEnabled = false;

	DoHideCellEditControl();

	DoSaveEditControlValue();
}

void ibGrid::SaveEditControlValue()
{
	if (IsCellEditControlEnabled())
	{
		DoSaveEditControlValue();
	}
}

void ibGrid::DoSaveEditControlValue()
{
	int row = m_currentCellCoords.GetRow();
	int col = m_currentCellCoords.GetCol();

	wxString oldval = GetCellValue(m_currentCellCoords);

	ibGridCellEditorPtr editor = GetCurrentCellEditorPtr();

	wxString newval;
	if (!editor->EndEdit(row, col, this, oldval, &newval))
		return;

	switch (SendEvent(wxEVT_GRID_CELL_CHANGING, newval))
	{
	case Event_Vetoed:
	case Event_CellDeleted:
		break;

	case Event_Unhandled:
	case Event_Handled:

		editor->ApplyEdit(row, col, this);

		// for compatibility reasons dating back to wx 2.8 when this event
		// was called wxEVT_GRID_CELL_CHANGE and wxEVT_GRID_CELL_CHANGING
		// didn't exist we allow vetoing this one too
		if (SendEvent(wxEVT_GRID_CELL_CHANGED, oldval) == Event_Vetoed)
		{
			// Event has been vetoed, set the data back.
			//
			// Note that we must use row and col here, which are sure to
			// not have been changed, while m_currentCellCoords could have
			// been changed by the event handler.
			SetCellValue(row, col, oldval);
			return;
		}

		// set new brake pos
		SetRowBrake(row);
		SetColBrake(col);
	}
}

//
// ------ Grid location functions
//  Note that all of these functions work with the logical coordinates of
//  grid cells and labels so you will need to convert from device
//  coordinates for mouse events etc.
//

ibGridCellCoords ibGrid::XYToCell(int x, int y, ibGridWindow* gridWindow) const
{
	int row = YToRow(y, false, gridWindow);
	int col = XToCol(x, false, gridWindow);

	return row == -1 || col == -1 ? ibGridNoCellCoords
		: ibGridCellCoords(row, col);
}

// compute row or column from some (unscrolled) coordinate value, using either
// m_defaultRowHeight/m_defaultColWidth or binary search on array of
// m_rowBottoms/m_colRights to do it quickly in O(log n) time.
// NOTE: This may not work correctly for reordered columns.
int ibGrid::PosToLinePos(int coord,
	bool clipToMinMax,
	const ibGridOperations& oper,
	ibGridWindow* gridWindow) const
{
	const int numLines = oper.GetNumberOfLines(this, gridWindow);

	if (coord < 0)
		return clipToMinMax && numLines > 0 ? 0 : wxNOT_FOUND;

	const int defaultLineSize = oper.GetDefaultLineSize(this);
	wxCHECK_MSG(defaultLineSize, -1, "can't have 0 default line size");

	int maxPos = coord / defaultLineSize;
	int minPos = oper.GetFirstLine(this, gridWindow);

	// check for the simplest case: if we have no explicit line sizes
	// configured, then we already know the line this position falls in
	const wxArrayInt& lineEnds = oper.GetLineEnds(this);
	if (lineEnds.empty())
	{
		if (maxPos < (numLines + minPos))
			return maxPos;

		return clipToMinMax ? numLines + minPos - 1 : -1;
	}

	// binary search is quite efficient and we can't really make any assumptions
	// on where to start here since row and columns could be of size 0 if they are
	// hidden. While this could be made more efficient, some profiling will be
	// necessary to determine if it really is a performance bottleneck
	maxPos = numLines + minPos - 1;

	// check if the position is beyond the last column
	const int lineAtMaxPos = oper.GetLineAt(this, maxPos);
	if (coord >= lineEnds[lineAtMaxPos])
		return clipToMinMax ? maxPos : wxNOT_FOUND;

	// or before the first one
	const int lineAt0 = oper.GetLineAt(this, minPos);
	if (coord < oper.GetLineStartPos(this, lineAt0))
		return clipToMinMax ? minPos : wxNOT_FOUND;
	else if (coord < lineEnds[lineAt0])
		return minPos;

	// finally do perform the binary search
	while (minPos < maxPos)
	{
		wxCHECK_MSG(lineEnds[oper.GetLineAt(this, minPos)] <= coord &&
			coord < lineEnds[oper.GetLineAt(this, maxPos)],
			-1,
			"ibGrid: internal error in PosToLinePos()");

		if (coord >= lineEnds[oper.GetLineAt(this, maxPos - 1)])
			return maxPos;
		else
			maxPos--;

		const int median = minPos + (maxPos - minPos + 1) / 2;
		if (coord < lineEnds[oper.GetLineAt(this, median)])
			maxPos = median;
		else
			minPos = median;
	}

	return maxPos;
}

int
ibGrid::PosToLine(int coord,
	bool clipToMinMax,
	const ibGridOperations& oper,
	ibGridWindow* gridWindow) const
{
	int pos = PosToLinePos(coord, clipToMinMax, oper, gridWindow);

	return pos == wxNOT_FOUND ? wxNOT_FOUND : oper.GetLineAt(this, pos);
}

int ibGrid::YToRow(int y, bool clipToMinMax, ibGridWindow* gridWindow) const
{
	return PosToLine(y, clipToMinMax, ibGridRowOperations(), gridWindow);
}

int ibGrid::XToCol(int x, bool clipToMinMax, ibGridWindow* gridWindow) const
{
	return PosToLine(x, clipToMinMax, ibGridColumnOperations(), gridWindow);
}

int ibGrid::YToPos(int y, ibGridWindow* gridWindow) const
{
	return PosToLinePos(y, true /* clip */, ibGridRowOperations(), gridWindow);
}

int ibGrid::XToPos(int x, ibGridWindow* gridWindow) const
{
	return PosToLinePos(x, true /* clip */, ibGridColumnOperations(), gridWindow);
}

// return the row/col number such that the pos is near the edge of, or -1 if
// not near an edge.
//
// notice that position can only possibly be near an edge if the row/column is
// large enough to still allow for an "inner" area that is _not_ near the edge
// (i.e., if the height/width is smaller than WXGRID_LABEL_EDGE_ZONE, pos will
// _never_ be considered to be near the edge).
int ibGrid::PosToEdgeOfLine(int pos, const ibGridOperations& oper) const
{
	// Get the bottom or rightmost line that could match.
	int line = oper.PosToLine(this, pos, NULL, true);

	if (line == wxNOT_FOUND)
		return -1;

	const int edge = FromDIP(WXGRID_LABEL_EDGE_ZONE);

	if (oper.GetLineSize(this, line) > edge)
	{
		// We know that we are in this line, test whether we are close enough
		// to start or end border, respectively.
		if (abs(oper.GetLineEndPos(this, line) - pos) < edge)
			return line;
		else if (line > 0 && pos - oper.GetLineStartPos(this, line) < edge)
		{
			// We need to find the previous visible line, so skip all the
			// hidden (of size 0) ones.
			do
			{
				line = oper.GetLineBefore(this, line);
			} while (line >= 0 && oper.GetLineSize(this, line) == 0);

			// It can possibly be -1 here.
			return line;
		}
	}

	return -1;
}

int ibGrid::YToEdgeOfRow(int y) const
{
	return PosToEdgeOfLine(y, ibGridRowOperations());
}

int ibGrid::XToEdgeOfCol(int x) const
{
	return PosToEdgeOfLine(x, ibGridColumnOperations());
}

wxRect ibGrid::CellToRect(int row, int col) const
{
	wxRect rect(-1, -1, -1, -1);

	if (row >= 0 && row < m_numRows &&
		col >= 0 && col < m_numCols)
	{
		int i, cell_rows, cell_cols;
		rect.width = rect.height = 0;
		if (GetCellSize(row, col, &cell_rows, &cell_cols) == CellSpan_Inside)
		{
			row += cell_rows;
			col += cell_cols;
			GetCellSize(row, col, &cell_rows, &cell_cols);
		}

		rect.x = GetColLeft(col, GetGridZoom());
		rect.y = GetRowTop(row, GetGridZoom());

		for (i = col; i < col + cell_cols; i++)
			rect.width += GetColWidth(i, GetGridZoom());
		for (i = row; i < row + cell_rows; i++)
			rect.height += GetRowHeight(i, GetGridZoom());

#ifndef __WXQT__
		// if grid lines are enabled, then the area of the cell is a bit smaller
		if (m_gridLinesEnabled)
		{
			rect.width -= 1;
			rect.height -= 1;
		}
#endif
	}

	return rect;
}

ibGridWindow* ibGrid::CellToGridWindow(int row, int col) const
{
	// It may happen that we're called during grid creation, when the current
	// cell still has invalid coordinates -- don't return (possibly null)
	// frozen corner window in this case.
	if (row == -1 && col == -1)
		return m_gridWin;
	else if (GetRowPos(row) < m_numFrozenRows && GetColPos(col) < m_numFrozenCols)
		return m_frozenCornerGridWin;
	else if (GetRowPos(row) < m_numFrozenRows)
		return m_frozenRowGridWin;
	else if (GetColPos(col) < m_numFrozenCols)
		return m_frozenColGridWin;

	return m_gridWin;
}

void ibGrid::GetGridWindowOffset(const ibGridWindow* gridWindow, int& x, int& y) const
{
	wxPoint pt = GetGridWindowOffset(gridWindow);

	x = pt.x;
	y = pt.y;
}

wxPoint ibGrid::GetGridWindowOffset(const ibGridWindow* gridWindow) const
{
	wxPoint pt(0, 0);

	if (gridWindow)
	{
		if (m_frozenRowGridWin &&
			(gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow) == 0)
		{
			pt.y = m_frozenRowGridWin->GetClientSize().y;
		}

		if (m_frozenColGridWin &&
			(gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol) == 0)
		{
			pt.x = m_frozenColGridWin->GetClientSize().x;
		}
	}

	return pt;
}

ibGridWindow* ibGrid::DevicePosToGridWindow(wxPoint pos) const
{
	return DevicePosToGridWindow(pos.x, pos.y);
}

ibGridWindow* ibGrid::DevicePosToGridWindow(int x, int y) const
{
	if (m_gridWin->GetRect().Contains(x, y))
		return m_gridWin;
	else if (m_frozenCornerGridWin && m_frozenCornerGridWin->GetRect().Contains(x, y))
		return m_frozenCornerGridWin;
	else if (m_frozenRowGridWin && m_frozenRowGridWin->GetRect().Contains(x, y))
		return m_frozenRowGridWin;
	else if (m_frozenColGridWin && m_frozenColGridWin->GetRect().Contains(x, y))
		return m_frozenColGridWin;

	return NULL;
}

void ibGrid::CalcGridWindowUnscrolledPosition(int x, int y, int* xx, int* yy,
	const ibGridWindow* gridWindow) const
{
	CalcUnscrolledPosition(x, y, xx, yy);

	if (gridWindow)
	{
		if (yy && (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow))
			*yy = y;
		if (xx && (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol))
			*xx = x;
	}
}

wxPoint ibGrid::CalcGridWindowUnscrolledPosition(const wxPoint& pt,
	const ibGridWindow* gridWindow) const
{
	wxPoint pt2;
	CalcGridWindowUnscrolledPosition(pt.x, pt.y, &pt2.x, &pt2.y, gridWindow);
	return pt2;
}

void ibGrid::CalcGridWindowScrolledPosition(int x, int y, int* xx, int* yy,
	const ibGridWindow* gridWindow) const
{
	CalcScrolledPosition(x, y, xx, yy);

	if (gridWindow)
	{
		if (yy && (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenRow))
			*yy = y;
		if (xx && (gridWindow->GetType() & ibGridWindow::ibGridWindowFrozenCol))
			*xx = x;
	}
}

wxPoint ibGrid::CalcGridWindowScrolledPosition(const wxPoint& pt,
	const ibGridWindow* gridWindow) const
{
	wxPoint pt2;
	CalcGridWindowScrolledPosition(pt.x, pt.y, &pt2.x, &pt2.y, gridWindow);
	return pt2;
}

bool ibGrid::IsVisible(int row, int col, bool wholeCellVisible) const
{
	// get the cell rectangle in logical coords
	//
	wxRect r(CellToRect(row, col));

	ibGridWindow* gridWindow = CellToGridWindow(row, col);
	r.Offset(-GetGridWindowOffset(gridWindow));

	// convert to device coords
	//
	int left, top, right, bottom;
	CalcGridWindowScrolledPosition(r.GetLeft(), r.GetTop(), &left, &top, gridWindow);
	CalcGridWindowScrolledPosition(r.GetRight(), r.GetBottom(), &right, &bottom, gridWindow);

	// check against the client area of the grid window
	int cw, ch;
	gridWindow->GetClientSize(&cw, &ch);

	if (wholeCellVisible)
	{
		// is the cell wholly visible ?
		return (left >= 0 && right <= cw &&
			top >= 0 && bottom <= ch);
	}
	else
	{
		// is the cell partly visible ?
		//
		return (((left >= 0 && left < cw) || (right > 0 && right <= cw)) &&
			((top >= 0 && top < ch) || (bottom > 0 && bottom <= ch)));
	}
}

// make the specified cell location visible by doing a minimal amount
// of scrolling
//
void ibGrid::MakeCellVisible(int row, int col)
{
	int xpos = -1, ypos = -1;

	if (row < -1 || row >= m_numRows ||
		col < -1 || col >= m_numCols)
		return;

	// We don't scroll in the corresponding direction if "pixels per line" is 0.
	const bool processRow = row != -1 && m_yScrollPixelsPerLine != 0;
	const bool processCol = col != -1 && m_xScrollPixelsPerLine != 0;

	// Get the cell rectangle in logical coords.
	wxRect r;
	ibGridWindow* gridWindow;

	if (processRow && processCol)
	{
		r = CellToRect(row, col);
		gridWindow = CellToGridWindow(row, col);
	}
	else if (processRow)
	{
		r.SetTop(GetRowTop(row, GetGridZoom()));
		r.SetHeight(GetRowHeight(row, GetGridZoom()));
		gridWindow = row < m_numFrozenRows
			? m_frozenRowGridWin
			: m_gridWin;
	}
	else if (processCol)
	{
		r.SetLeft(GetColLeft(col, GetGridZoom()));
		r.SetWidth(GetColWidth(col, GetGridZoom()));
		gridWindow = col < m_numFrozenCols
			? m_frozenColGridWin
			: m_gridWin;
	}
	else
	{
		return;
	}

	wxPoint gridOffset = GetGridWindowOffset(gridWindow);

	if (processRow)
	{
		// Convert to device coords.
		int top, bottom;
		CalcGridWindowScrolledPosition(0, r.GetTop(), NULL, &top, gridWindow);
		CalcGridWindowScrolledPosition(0, r.GetBottom(), NULL, &bottom, gridWindow);

		int ch;
		gridWindow->GetClientSize(NULL, &ch);

		if (top < gridOffset.y)
		{
			ypos = r.GetTop() - gridOffset.y;
		}
		else if (bottom > (ch + gridOffset.y))
		{
			int h = r.GetHeight();

			ypos = r.GetTop() - gridOffset.y;

			int i;
			for (i = row - 1; i >= 0; i--)
			{
				int rowHeight = GetRowHeight(i, GetGridZoom());
				if (h + rowHeight > ch)
					break;

				h += rowHeight;
				ypos -= rowHeight;
			}

			// we divide it later by GRID_SCROLL_LINE, make sure that we don't
			// have rounding errors (this is important, because if we do,
			// we might not scroll at all and some cells won't be redrawn)
			//
			// Sometimes GRID_SCROLL_LINE / 2 is not enough,
			// so just add a full scroll unit...
			ypos += m_yScrollPixelsPerLine;
		}
	}

	if (processCol)
	{
		// Convert to device coords.
		int left, right;
		CalcGridWindowScrolledPosition(r.GetLeft(), 0, &left, NULL, gridWindow);
		CalcGridWindowScrolledPosition(r.GetRight(), 0, &right, NULL, gridWindow);

		int cw;
		gridWindow->GetClientSize(&cw, NULL);

		// special handling for wide cells - show always left part of the cell!
		// Otherwise, e.g. when stepping from row to row, it would jump between
		// left and right part of the cell on every step!
//      if ( left < 0 )
		if (left < gridOffset.x || (right - left) >= cw)
		{
			xpos = r.GetLeft() - gridOffset.x;
		}
		else if (right > (cw + gridOffset.x))
		{
			// position the view so that the cell is on the right
			int x0, y0;
			CalcGridWindowUnscrolledPosition(0, 0, &x0, &y0, gridWindow);
			xpos = x0 + (right - cw);

			// see comment for ypos above
			xpos += m_xScrollPixelsPerLine;
		}
	}

	if (xpos == -1 && ypos == -1)
		return;

	if (xpos != -1)
		xpos /= m_xScrollPixelsPerLine;
	if (ypos != -1)
		ypos /= m_yScrollPixelsPerLine;
	Scroll(xpos, ypos);
	AdjustScrollbars();
}

int ibGrid::GetFirstFullyVisibleRow() const
{
	if (m_numRows == 0)
		return -1;

	int row;
	if (GetNumberFrozenRows() > 0)
	{
		row = 0;
	}
	else
	{
		int y;
		CalcGridWindowUnscrolledPosition(0, 0,
			NULL, &y,
			m_gridWin);

		row = internalYToRow(y, m_gridWin);

		// If the row is not fully visible (if only 2 pixels is hidden
		// the row still looks fully visible).
		if (GetRowTop(row, GetGridZoom()) + 2 < y)
		{
			// Use the next visible row.
			for (;; )
			{
				// But we can't go beyond the last row anyhow.
				if (row == m_numRows - 1)
					break;

				if (IsRowShown(++row))
					break;
			}
		}
	}

	return row;
}

int ibGrid::GetFirstFullyVisibleCol() const
{
	if (m_numCols == 0)
		return -1;

	int col;
	if (GetNumberFrozenCols() > 0)
	{
		col = 0;
	}
	else
	{
		int x;
		CalcGridWindowUnscrolledPosition(0, 0,
			&x, NULL,
			m_gridWin);

		col = internalXToCol(x, m_gridWin);

		// If the column is not fully visible.
		if (GetColLeft(col, GetGridZoom()) < x)
		{
			// Use the next visible column.
			for (;; )
			{
				if (col == m_numCols - 1)
					break;

				if (IsColShown(GetColAt(++col)))
					break;
			}
		}
	}

	return col;
}

//
// ------ Grid cursor movement functions
//

namespace
{

	// Helper function creating dummy wxKeyboardState object corresponding to the
	// value of "expandSelection" flag specified to our public MoveCursorXXX().
	// This function only exists to avoid breaking compatibility in the public API,
	// all internal code should use the real, not dummy, wxKeyboardState.
	inline
		wxKeyboardState DummyKeyboardState(bool expandSelection)
	{
		// Normally "expandSelection" is set depending on whether Shift is pressed
		// or not, but here we reconstruct the keyboard state from this flag.
		return wxKeyboardState(false /* control */, expandSelection /* shift */);
	}

} // anonymous namespace

void
ibGrid::DoMoveCursorFromKeyboard(const wxKeyboardState& kbdState,
	const ibGridDirectionOperations& diroper)
{
	if (kbdState.ControlDown())
		DoMoveCursorByBlock(kbdState, diroper);
	else
		DoMoveCursor(kbdState, diroper);
}

bool
ibGrid::DoMoveCursor(const wxKeyboardState& kbdState,
	const ibGridDirectionOperations& diroper)
{
	if (m_currentCellCoords == ibGridNoCellCoords)
		return false;

	// Expand selection if Shift is pressed.
	if (kbdState.ShiftDown())
	{
		if (!m_selection)
			return false;

		ibGridCellCoords coords(m_selection->GetExtensionAnchor());
		if (!diroper.TryToAdvance(coords))
			return false;

		if (m_selection->ExtendCurrentBlock(m_currentCellCoords,
			coords,
			kbdState))
		{
			// We want to show a line (a row or a column), not the end of
			// the selection block. And do it only if the selection block
			// was actually changed.
			MakeCellVisible(diroper.MakeWholeLineCoords(coords));
		}
	}
	else // don't expand selection
	{
		ClearSelection();

		ibGridCellCoords coords = m_currentCellCoords;
		if (!diroper.TryToAdvance(coords))
			return false;

		GoToCell(coords);
	}

	return true;
}

bool ibGrid::MoveCursorUp(bool expandSelection)
{
	return DoMoveCursor(DummyKeyboardState(expandSelection),
		ibGridBackwardOperations(this, ibGridRowOperations()));
}

bool ibGrid::MoveCursorDown(bool expandSelection)
{
	return DoMoveCursor(DummyKeyboardState(expandSelection),
		ibGridForwardOperations(this, ibGridRowOperations()));
}

bool ibGrid::MoveCursorLeft(bool expandSelection)
{
	return DoMoveCursor(DummyKeyboardState(expandSelection),
		ibGridBackwardOperations(this, ibGridColumnOperations()));
}

bool ibGrid::MoveCursorRight(bool expandSelection)
{
	return DoMoveCursor(DummyKeyboardState(expandSelection),
		ibGridForwardOperations(this, ibGridColumnOperations()));
}

bool
ibGrid::AdvanceByPage(ibGridCellCoords& coords,
	const ibGridDirectionOperations& diroper)
{
	if (diroper.IsAtBoundary(coords))
		return false;

	const int oldRow = coords.GetRow();
	coords.SetRow(diroper.MoveByPixelDistance(oldRow, m_gridWin->GetClientSize().y));
	if (coords.GetRow() == oldRow)
		diroper.Advance(coords);

	return true;
}

bool
ibGrid::DoMoveCursorByPage(const wxKeyboardState& kbdState,
	const ibGridDirectionOperations& diroper)
{
	if (m_currentCellCoords == ibGridNoCellCoords)
		return false;

	// We don't handle Ctrl-PageUp/Down, it's not really clear what are they
	// supposed to do, so don't do anything for now.
	if (kbdState.ControlDown())
		return false;

	if (kbdState.ShiftDown())
	{
		if (!m_selection)
			return false;

		ibGridCellCoords coords = m_selection->GetExtensionAnchor();
		if (!AdvanceByPage(coords, diroper))
			return false;

		if (m_selection->ExtendCurrentBlock(m_currentCellCoords, coords, kbdState))
			MakeCellVisible(diroper.MakeWholeLineCoords(coords));
	}
	else
	{
		ibGridCellCoords coords(m_currentCellCoords);
		if (!AdvanceByPage(coords, diroper))
			return false;

		ClearSelection();
		GoToCell(coords);
	}

	return true;
}

bool ibGrid::MovePageUp()
{
	return DoMoveCursorByPage(DummyKeyboardState(false),
		ibGridBackwardOperations(this, ibGridRowOperations()));
}

bool ibGrid::MovePageDown()
{
	return DoMoveCursorByPage(DummyKeyboardState(false),
		ibGridForwardOperations(this, ibGridRowOperations()));
}

// helper of DoMoveCursorByBlock(): advance the cell coordinates using diroper
// until we find a non-empty cell or reach the grid end
void
ibGrid::AdvanceToNextNonEmpty(ibGridCellCoords& coords,
	const ibGridDirectionOperations& diroper)
{
	while (!diroper.IsAtBoundary(coords))
	{
		diroper.Advance(coords);
		if (!m_table->IsEmpty(coords))
			break;
	}
}

bool
ibGrid::AdvanceByBlock(ibGridCellCoords& coords,
	const ibGridDirectionOperations& diroper)
{
	if (m_table->IsEmpty(coords))
	{
		// we are in an empty cell: find the next block of non-empty cells
		AdvanceToNextNonEmpty(coords, diroper);
	}
	else // current cell is not empty
	{
		if (!diroper.TryToAdvance(coords))
			return false;

		if (m_table->IsEmpty(coords))
		{
			// we started at the end of a block, find the next one
			AdvanceToNextNonEmpty(coords, diroper);
		}
		else // we're in a middle of a block
		{
			// go to the end of it, i.e. find the last cell before the next
			// empty one
			while (!diroper.IsAtBoundary(coords))
			{
				ibGridCellCoords coordsNext(coords);
				diroper.Advance(coordsNext);
				if (m_table->IsEmpty(coordsNext))
					break;

				coords = coordsNext;
			}
		}
	}

	return true;
}

bool
ibGrid::DoMoveCursorByBlock(const wxKeyboardState& kbdState,
	const ibGridDirectionOperations& diroper)
{
	if (!m_table)
		return false;

	ibGridCellCoords coords(m_currentCellCoords);
	if (kbdState.ShiftDown())
	{
		if (!m_selection)
			return false;

		// Extending selection by block is tricky for several reasons. First of
		// all, we need to combine the coordinates of the current cell and the
		// anchor point of selection to find our starting point.
		//
		// To explain why we need to do this, consider the example of moving by
		// rows (i.e. vertically). In this case, it's the contents of column
		// containing the current cell which should determine the destination
		// of Shift-Ctrl-Up/Down movement, just as it does for Ctrl-Up/Down. In
		// fact, the column containing the selection anchor might not even be
		// visible at all, e.g. if a whole row is selected and that column is
		// the last one, so we definitely don't want to use the contents of
		// this column to determine the destination row.
		//
		// So instead of using the anchor itself here, use only its component
		// component in "our" direction with the current cell component.
		const ibGridCellCoords anchor = m_selection->GetExtensionAnchor();

		// This is a really ugly hack that we use to check if we're moving by
		// rows or columns here, but it's not worth adding a specific method
		// just for this, so just check if MakeWholeLineCoords() fixes the
		// column or the row as -1 to determine the direction we're moving in.
		const bool byRow = diroper.MakeWholeLineCoords(coords).GetCol() == -1;

		if (byRow)
			coords.SetRow(anchor.GetRow());
		else
			coords.SetCol(anchor.GetCol());

		if (!AdvanceByBlock(coords, diroper))
			return false;

		// Second, now that we've found the destination, we need to copy the
		// other component, i.e. the one we didn't modify, from the anchor to
		// do as if we started from the anchor originally.
		//
		// Again, to understand why this is necessary, consider the same
		// example as above: if we didn't do this, we would lose the column of
		// the original anchor completely and end up with a single column
		// selection block even if we had started with the full row selection.
		if (byRow)
			coords.SetCol(anchor.GetCol());
		else
			coords.SetRow(anchor.GetRow());

		if (m_selection->ExtendCurrentBlock(m_currentCellCoords,
			coords,
			kbdState))
		{
			// We want to show a line (a row or a column), not the end of
			// the selection block. And do it only if the selection block
			// was actually changed.
			MakeCellVisible(diroper.MakeWholeLineCoords(coords));
		}
	}
	else
	{
		if (!AdvanceByBlock(coords, diroper))
			return false;

		ClearSelection();
		GoToCell(coords);
	}

	return true;
}

bool ibGrid::MoveCursorUpBlock(bool expandSelection)
{
	return DoMoveCursorByBlock(
		DummyKeyboardState(expandSelection),
		ibGridBackwardOperations(this, ibGridRowOperations())
	);
}

bool ibGrid::MoveCursorDownBlock(bool expandSelection)
{
	return DoMoveCursorByBlock(
		DummyKeyboardState(expandSelection),
		ibGridForwardOperations(this, ibGridRowOperations())
	);
}

bool ibGrid::MoveCursorLeftBlock(bool expandSelection)
{
	return DoMoveCursorByBlock(
		DummyKeyboardState(expandSelection),
		ibGridBackwardOperations(this, ibGridColumnOperations())
	);
}

bool ibGrid::MoveCursorRightBlock(bool expandSelection)
{
	return DoMoveCursorByBlock(
		DummyKeyboardState(expandSelection),
		ibGridForwardOperations(this, ibGridColumnOperations())
	);
}

//
// ------ Label values and formatting
//

void ibGrid::GetRowLabelAlignment(int* horiz, int* vert) const
{
	if (horiz)
		*horiz = m_rowLabelHorizAlign;
	if (vert)
		*vert = m_rowLabelVertAlign;
}

void ibGrid::GetColLabelAlignment(int* horiz, int* vert) const
{
	if (horiz)
		*horiz = m_colLabelHorizAlign;
	if (vert)
		*vert = m_colLabelVertAlign;
}

int ibGrid::GetColLabelTextOrientation() const
{
	return m_colLabelTextOrientation;
}

void ibGrid::GetCornerLabelAlignment(int* horiz, int* vert) const
{
	if (horiz)
		*horiz = m_cornerLabelHorizAlign;
	if (vert)
		*vert = m_cornerLabelVertAlign;
}

int ibGrid::GetCornerLabelTextOrientation() const
{
	return m_cornerLabelTextOrientation;
}

int ibGrid::GetRowAreaValue(int row, wxString* areaLabel) const
{
	int result = 0;

	for (unsigned int n = 0; n < m_rowAreaAt.size(); n++) {

		const ibGridCellArea& entry = m_rowAreaAt[n];

		if (row >= entry.m_start && row <= entry.m_end)
			result += 1;
		if (row == entry.m_start)
			result += 2;
		if (row == entry.m_end)
			result += 4;

		if (result > 0) {
			if (areaLabel)
				*areaLabel = entry.m_areaLabel;
			return result;
		}
	}

	return result;
}

wxString ibGrid::GetRowLabelValue(int row) const
{
	if (m_table)
	{
		return m_table->GetRowLabelValue(row);
	}
	else
	{
		wxString s;
		s << row;
		return s;
	}
}

int ibGrid::GetColAreaValue(int col, wxString* areaLabel) const
{
	int result = 0;

	for (unsigned int n = 0; n < m_colAreaAt.size(); n++) {

		const ibGridCellArea& entry = m_colAreaAt[n];

		if (col >= entry.m_start && col <= entry.m_end)
			result += 1;
		if (col == entry.m_start)
			result += 2;
		if (col == entry.m_end)
			result += 4;

		if (result > 0) {
			if (areaLabel)
				*areaLabel = entry.m_areaLabel;
			return result;
		}
	}

	return result;
}

wxString ibGrid::GetColLabelValue(int col) const
{
	if (m_table)
	{
		return m_table->GetColLabelValue(col);
	}
	else
	{
		wxString s;
		s << col;
		return s;
	}
}

wxString ibGrid::GetCornerLabelValue() const
{
	if (m_table)
	{
		return m_table->GetCornerLabelValue();
	}
	else
	{
		return wxString();
	}
}

void ibGrid::SetRowLabelSize(int width)
{
	wxASSERT(width >= 0 || width == wxGRID_AUTOSIZE);

	if (width == wxGRID_AUTOSIZE)
	{
		width = CalcColOrRowLabelAreaMinSize(wxGRID_ROW);
	}

	if (width != ibCalcGridScale(m_rowLabelWidth, GetGridZoom()))
	{
		if (width == 0)
		{
			m_rowAreaWin->Show(false);
			m_rowLabelWin->Show(false);
			m_cornerLabelWin->Show(false);
		}
		else if (ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) == 0)
		{
			if (GridRowAreaEnabled())
				m_rowAreaWin->Show(true);
			m_rowLabelWin->Show(true);
			if (m_colLabelHeight > 0)
				m_cornerLabelWin->Show(true);
		}

		m_rowLabelWidth = width;
		InvalidateBestSize();
		CalcWindowSizes();
		wxScrolledCanvas::Refresh(true);
	}
}

void ibGrid::SetColLabelSize(int height)
{
	wxASSERT(height >= 0 || height == wxGRID_AUTOSIZE);

	if (height == wxGRID_AUTOSIZE)
	{
		height = CalcColOrRowLabelAreaMinSize(wxGRID_COLUMN);
	}

	if (height != m_colLabelHeight)
	{
		if (height == 0)
		{
			m_colAreaWin->Show(false);
			m_colLabelWin->Show(false);
			m_cornerLabelWin->Show(false);
		}
		else if (m_colLabelHeight == 0)
		{
			if (GridColAreaEnabled())
				m_colAreaWin->Show(true);

			m_colLabelWin->Show(true);

			if (ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) > 0)
				m_cornerLabelWin->Show(true);
		}

		m_colLabelHeight = height;
		InvalidateBestSize();
		CalcWindowSizes();
		wxScrolledCanvas::Refresh(true);
	}
}

void ibGrid::SetLabelBackgroundColour(const wxColour& colour)
{
	if (m_labelBackgroundColour != colour)
	{
		m_labelBackgroundColour = colour;
		m_rowAreaWin->SetBackgroundColour(colour);
		m_rowLabelWin->SetBackgroundColour(colour);
		m_colAreaWin->SetBackgroundColour(colour);
		m_colLabelWin->SetBackgroundColour(colour);
		m_cornerLabelWin->SetBackgroundColour(colour);

		if (m_rowFrozenAreaWin)
			m_rowFrozenAreaWin->SetBackgroundColour(colour);
		if (m_rowFrozenLabelWin)
			m_rowFrozenLabelWin->SetBackgroundColour(colour);
		if (m_colFrozenAreaWin)
			m_colFrozenAreaWin->SetBackgroundColour(colour);
		if (m_colFrozenLabelWin)
			m_colFrozenLabelWin->SetBackgroundColour(colour);

		if (ShouldRefresh())
		{
			m_rowAreaWin->Refresh();
			m_rowLabelWin->Refresh();
			m_colAreaWin->Refresh();
			m_colLabelWin->Refresh();
			m_cornerLabelWin->Refresh();

			if (m_rowFrozenAreaWin)
				m_rowFrozenAreaWin->Refresh();
			if (m_rowFrozenLabelWin)
				m_rowFrozenLabelWin->Refresh();
			if (m_colFrozenAreaWin)
				m_colFrozenAreaWin->Refresh();
			if (m_colFrozenLabelWin)
				m_colFrozenLabelWin->Refresh();
		}
	}
}

void ibGrid::SetLabelTextColour(const wxColour& colour)
{
	if (m_labelTextColour != colour)
	{
		m_labelTextColour = colour;

		if (IsUsingNativeHeader())
			GetGridColHeader()->SetForegroundColour(colour);

		if (ShouldRefresh())
		{
			m_rowAreaWin->Refresh();
			m_rowLabelWin->Refresh();
			m_colAreaWin->Refresh();
			m_colLabelWin->Refresh();
		}
	}
}

void ibGrid::SetLabelFont(const wxFont& font)
{
	m_labelFont = font;

	if (IsUsingNativeHeader())
		GetGridColHeader()->SetFont(font);

	if (ShouldRefresh())
	{
		m_rowAreaWin->Refresh();
		m_rowLabelWin->Refresh();
		m_colAreaWin->Refresh();
		m_colLabelWin->Refresh();
	}
}

void ibGrid::SetRowLabelAlignment(int horiz, int vert)
{
	// allow old (incorrect) defs to be used
	switch (horiz)
	{
	case wxLEFT:   horiz = wxALIGN_LEFT; break;
	case wxRIGHT:  horiz = wxALIGN_RIGHT; break;
	case wxCENTRE: horiz = wxALIGN_CENTRE; break;
	}

	switch (vert)
	{
	case wxTOP:    vert = wxALIGN_TOP;    break;
	case wxBOTTOM: vert = wxALIGN_BOTTOM; break;
	case wxCENTRE: vert = wxALIGN_CENTRE; break;
	}

	if (horiz == wxALIGN_LEFT || horiz == wxALIGN_CENTRE || horiz == wxALIGN_RIGHT)
	{
		m_rowLabelHorizAlign = horiz;
	}

	if (vert == wxALIGN_TOP || vert == wxALIGN_CENTRE || vert == wxALIGN_BOTTOM)
	{
		m_rowLabelVertAlign = vert;
	}

	if (ShouldRefresh())
	{
		m_rowAreaWin->Refresh();
		m_rowLabelWin->Refresh();
	}
}

void ibGrid::SetColLabelAlignment(int horiz, int vert)
{
	// allow old (incorrect) defs to be used
	switch (horiz)
	{
	case wxLEFT:   horiz = wxALIGN_LEFT; break;
	case wxRIGHT:  horiz = wxALIGN_RIGHT; break;
	case wxCENTRE: horiz = wxALIGN_CENTRE; break;
	}

	switch (vert)
	{
	case wxTOP:    vert = wxALIGN_TOP;    break;
	case wxBOTTOM: vert = wxALIGN_BOTTOM; break;
	case wxCENTRE: vert = wxALIGN_CENTRE; break;
	}

	if (horiz == wxALIGN_LEFT || horiz == wxALIGN_CENTRE || horiz == wxALIGN_RIGHT)
	{
		m_colLabelHorizAlign = horiz;
	}

	if (vert == wxALIGN_TOP || vert == wxALIGN_CENTRE || vert == wxALIGN_BOTTOM)
	{
		m_colLabelVertAlign = vert;
	}

	if (ShouldRefresh())
	{
		m_colAreaWin->Refresh();
		m_colLabelWin->Refresh();
	}
}

void ibGrid::SetCornerLabelAlignment(int horiz, int vert)
{
	// allow old (incorrect) defs to be used
	switch (horiz)
	{
	case wxLEFT:   horiz = wxALIGN_LEFT; break;
	case wxRIGHT:  horiz = wxALIGN_RIGHT; break;
	case wxCENTRE: horiz = wxALIGN_CENTRE; break;
	}

	switch (vert)
	{
	case wxTOP:    vert = wxALIGN_TOP;    break;
	case wxBOTTOM: vert = wxALIGN_BOTTOM; break;
	case wxCENTRE: vert = wxALIGN_CENTRE; break;
	}

	if (horiz == wxALIGN_LEFT || horiz == wxALIGN_CENTRE || horiz == wxALIGN_RIGHT)
	{
		m_cornerLabelHorizAlign = horiz;
	}

	if (vert == wxALIGN_TOP || vert == wxALIGN_CENTRE || vert == wxALIGN_BOTTOM)
	{
		m_cornerLabelVertAlign = vert;
	}

	if (ShouldRefresh())
	{
		m_cornerLabelWin->Refresh();
	}
}

void ibGrid::SetGridZoom(int point)
{
	const float zoomScale = m_zoomScale + (point * 0.25f);

	if (zoomScale <= 2.25f && zoomScale >= 0.5f)
	{
		// By the same reasoning, the editor should be dismissed if columns are
		// added or removed. And for consistency, it should IMHO always be
		// removed, not only if the cell "underneath" it actually changes.
		// For now, I intentionally do not save the editor's content as the
		// cell it might want to save that stuff to might no longer exist.
		HideCellEditControl();

		m_zoomScale = zoomScale;

		InvalidateBestSize();
		CalcDimensions();

		SendEvent(wxEVT_GRID_ZOOM);
	}
}

// Note: under MSW, the default column label font must be changed because it
//       does not support vertical printing
//
// Example:
//      pGrid->SetLabelFont(wxFontInfo(9).Family(wxFONTFAMILY_SWISS));
//      pGrid->SetColLabelTextOrientation(wxVERTICAL);
//
void ibGrid::SetColLabelTextOrientation(int textOrientation)
{
	if (textOrientation == wxHORIZONTAL || textOrientation == wxVERTICAL)
		m_colLabelTextOrientation = textOrientation;

	if (ShouldRefresh())
	{
		m_colAreaWin->Refresh();
		m_colLabelWin->Refresh();
	}
}

void ibGrid::SetCornerLabelTextOrientation(int textOrientation)
{
	if (textOrientation == wxHORIZONTAL || textOrientation == wxVERTICAL)
		m_cornerLabelTextOrientation = textOrientation;

	if (ShouldRefresh())
		m_cornerLabelWin->Refresh();
}

void ibGrid::SetRowLabelValue(int row, const wxString& s)
{
	if (m_table)
	{
		m_table->SetRowLabelValue(row, s);
		if (ShouldRefresh())
		{
			wxRect rect = CellToRect(row, 0);
			if (rect.height > 0)
			{
				CalcScrolledPosition(0, rect.y, &rect.x, &rect.y);

				rect.x = 0;
				rect.width = ibCalcGridScale(m_rowLabelWidth, GetGridZoom());

				m_rowAreaWin->Refresh(true, &rect);
				m_rowLabelWin->Refresh(true, &rect);
			}
		}
	}
}

void ibGrid::SetColLabelValue(int col, const wxString& s)
{
	if (m_table)
	{
		m_table->SetColLabelValue(col, s);
		if (ShouldRefresh())
		{
			if (m_useNativeHeader)
			{
				GetGridColHeader()->UpdateColumn(col);
			}
			else
			{
				wxRect rect = CellToRect(0, col);
				if (rect.width > 0)
				{
					CalcScrolledPosition(rect.x, 0, &rect.x, &rect.y);
					rect.y = 0;
					rect.height = (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom());
					GetColLabelWindow()->Refresh(true, &rect);
				}
			}
		}
	}
}

void ibGrid::SetCornerLabelValue(const wxString& s)
{
	if (m_table)
	{
		m_table->SetCornerLabelValue(s);
		if (ShouldRefresh())
		{
			wxRect rect = m_cornerLabelWin->GetRect();
			m_cornerLabelWin->Refresh(true, &rect);
		}
	}
}

void ibGrid::SetGridLineColour(const wxColour& colour)
{
	if (m_gridLineColour != colour)
	{
		m_gridLineColour = colour;

		if (GridLinesEnabled())
			RedrawGridLines();
	}
}

void ibGrid::SetCellHighlightColour(const wxColour& colour)
{
	if (m_cellHighlightColour != colour)
	{
		m_cellHighlightColour = colour;

		RefreshBlock(m_currentCellCoords, m_currentCellCoords);
	}
}

void ibGrid::SetCellHighlightPenWidth(int width)
{
	if (m_cellHighlightPenWidth != width)
	{
		m_cellHighlightPenWidth = width;

		// Just redrawing the cell highlight is not enough since that won't
		// make any visible change if the thickness is getting smaller.
		int row = m_currentCellCoords.GetRow();
		int col = m_currentCellCoords.GetCol();
		if (row == -1 || col == -1 || GetColWidth(col) <= 0 || GetRowHeight(row) <= 0)
			return;

		wxRect rect = CellToRect(row, col);
		ibGridWindow* gridWindow = CellToGridWindow(row, col);
		gridWindow->Refresh(true, &rect);
	}
}

void ibGrid::SetCellHighlightROPenWidth(int width)
{
	if (m_cellHighlightROPenWidth != width)
	{
		m_cellHighlightROPenWidth = width;

		// Just redrawing the cell highlight is not enough since that won't
		// make any visible change if the thickness is getting smaller.
		int row = m_currentCellCoords.GetRow();
		int col = m_currentCellCoords.GetCol();
		if (row == -1 || col == -1 ||
			GetColWidth(col) <= 0 || GetRowHeight(row) <= 0)
			return;

		wxRect rect = CellToRect(row, col);
		ibGridWindow* gridWindow = CellToGridWindow(row, col);
		gridWindow->Refresh(true, &rect);
	}
}

void ibGrid::SetGridFrozenBorderColour(const wxColour& colour)
{
	if (m_gridFrozenBorderColour != colour)
	{
		m_gridFrozenBorderColour = colour;

		if (ShouldRefresh())
		{
			if (m_frozenRowGridWin)
				m_frozenRowGridWin->Refresh();
			if (m_frozenColGridWin)
				m_frozenColGridWin->Refresh();
		}
	}
}

void ibGrid::SetGridFrozenBorderPenWidth(int width)
{
	if (m_gridFrozenBorderPenWidth != width)
	{
		m_gridFrozenBorderPenWidth = width;

		if (ShouldRefresh())
		{
			if (m_frozenRowGridWin)
				m_frozenRowGridWin->Refresh();
			if (m_frozenColGridWin)
				m_frozenColGridWin->Refresh();
		}
	}
}

void ibGrid::RedrawGridLines()
{
	// the lines will be redrawn when the window is thawed or shown
	if (!ShouldRefresh())
		return;

	m_gridWin->Refresh();

	if (m_frozenColGridWin)
		m_frozenColGridWin->Refresh();
	if (m_frozenRowGridWin)
		m_frozenRowGridWin->Refresh();
	if (m_frozenCornerGridWin)
		m_frozenCornerGridWin->Refresh();
}

void ibGrid::EnableGridLines(bool enable)
{
	if (enable != m_gridLinesEnabled)
	{
		m_gridLinesEnabled = enable;

		RedrawGridLines();
	}
}

void ibGrid::DoClipGridLines(bool& var, bool clip)
{
	if (clip != var)
	{
		var = clip;

		if (GridLinesEnabled())
			RedrawGridLines();
	}
}

int ibGrid::GetDefaultRowSize(float scale) const
{
	return ibCalcGridScale(m_defaultRowHeight, scale);
}

int ibGrid::GetRowSize(int row, float scale) const
{
	wxCHECK_MSG(row >= 0 && row < m_numRows, 0, wxT("invalid row index"));

	return GetRowHeight(row, scale);
}

int ibGrid::GetDefaultColSize(float scale) const
{
	return ibCalcGridScale(m_defaultColWidth, scale);
}

int ibGrid::GetColSize(int col, float scale) const
{
	wxCHECK_MSG(col >= 0 && col < m_numCols, 0, wxT("invalid column index"));

	return GetColWidth(col, scale);
}

// ============================================================================
// access to the grid attributes: each of them has a default value in the grid
// itself and may be overidden on a per-cell basis
// ============================================================================

// ----------------------------------------------------------------------------
// setting default attributes
// ----------------------------------------------------------------------------

void ibGrid::SetDefaultCellBackgroundColour(const wxColour& col)
{
	m_defaultCellAttr->SetBackgroundColour(col);
#if defined(__WXGTK__) || defined(__WXQT__)
	m_gridWin->SetBackgroundColour(col);
#endif
}

void ibGrid::SetDefaultCellTextColour(const wxColour& col)
{
	m_defaultCellAttr->SetTextColour(col);
}

void ibGrid::SetDefaultCellTextOrient(const int& orient)
{
	m_defaultCellAttr->SetTextOrient(orient);
}

void ibGrid::SetDefaultCellAlignment(int horiz, int vert)
{
	m_defaultCellAttr->SetAlignment(horiz, vert);
}

void ibGrid::SetDefaultCellFitMode(ibGridFitMode fitMode)
{
	m_defaultCellAttr->SetFitMode(fitMode);
}

void ibGrid::SetDefaultCellFont(const wxFont& font)
{
	m_defaultCellAttr->SetFont(font);
}

void ibGrid::SetDefaultCellBorderLeft(wxPenStyle style, const wxColour& colour, int width)
{
	m_defaultCellAttr->SetBorderLeft(style, colour, width);
}

void ibGrid::SetDefaultCellBorderRight(wxPenStyle style, const wxColour& colour, int width)
{
	m_defaultCellAttr->SetBorderRight(style, colour, width);
}

void ibGrid::SetDefaultCellBorderTop(wxPenStyle style, const wxColour& colour, int width)
{
	m_defaultCellAttr->SetBorderTop(style, colour, width);
}

void ibGrid::SetDefaultCellBorderBottom(wxPenStyle style, const wxColour& colour, int width)
{
	m_defaultCellAttr->SetBorderBottom(style, colour, width);
}

// For editors and renderers the type registry takes precedence over the
// default attr, so we need to register the new editor/renderer for the string
// data type in order to make setting a default editor/renderer appear to
// work correctly.

void ibGrid::SetDefaultRenderer(ibGridCellRenderer* renderer)
{
	RegisterDataType(wxGRID_VALUE_STRING,
		renderer,
		GetDefaultEditorForType(wxGRID_VALUE_STRING));
}

void ibGrid::SetDefaultEditor(ibGridCellEditor* editor)
{
	RegisterDataType(wxGRID_VALUE_STRING,
		GetDefaultRendererForType(wxGRID_VALUE_STRING),
		editor);
}

// ----------------------------------------------------------------------------
// access to the default attributes
// ----------------------------------------------------------------------------

wxColour ibGrid::GetDefaultCellBackgroundColour() const
{
	return m_defaultCellAttr->GetBackgroundColour();
}

wxColour ibGrid::GetDefaultCellTextColour() const
{
	return m_defaultCellAttr->GetTextColour();
}

wxFont ibGrid::GetDefaultCellFont() const
{
	return m_defaultCellAttr->GetFont();
}

void ibGrid::GetDefaultCellAlignment(int* horiz, int* vert) const
{
	m_defaultCellAttr->GetAlignment(horiz, vert);
}

ibGridCellBorder ibGrid::GetDefaultCellBorderLeft() const
{
	return m_defaultCellAttr->GetBorderLeft();
}

ibGridCellBorder ibGrid::GetDefaultCellBorderRight() const
{
	return m_defaultCellAttr->GetBorderRight();
}

ibGridCellBorder ibGrid::GetDefaultCellBorderTop() const
{
	return m_defaultCellAttr->GetBorderTop();
}

ibGridCellBorder ibGrid::GetDefaultCellBorderBottom() const
{
	return m_defaultCellAttr->GetBorderBottom();
}

ibGridFitMode ibGrid::GetDefaultCellFitMode() const
{
	return m_defaultCellAttr->GetFitMode();
}

ibGridCellRenderer* ibGrid::GetDefaultRenderer() const
{
	return m_defaultCellAttr->GetRenderer(NULL, 0, 0);
}

ibGridCellEditor* ibGrid::GetDefaultEditor() const
{
	return m_defaultCellAttr->GetEditor(NULL, 0, 0);
}

// ----------------------------------------------------------------------------
// access to cell attributes
// ----------------------------------------------------------------------------

wxColour ibGrid::GetCellBackgroundColour(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetBackgroundColour();
}

wxColour ibGrid::GetCellTextColour(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetTextColour();
}

int ibGrid::GetCellTextOrient(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetTextOrient();
}

wxFont ibGrid::GetCellFont(int row, int col, float scale) const
{
	return GetCellAttrPtr(row, col)->GetFont(scale);
}

void ibGrid::GetCellAlignment(int row, int col, int* horiz, int* vert) const
{
	return  GetCellAttrPtr(row, col)->GetAlignment(horiz, vert);
}

ibGridCellBorder ibGrid::GetCellBorderLeft(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetBorderLeft();
}

ibGridCellBorder ibGrid::GetCellBorderRight(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetBorderRight();
}

ibGridCellBorder ibGrid::GetCellBorderTop(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetBorderTop();
}

ibGridCellBorder ibGrid::GetCellBorderBottom(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetBorderBottom();
}

ibGridFitMode ibGrid::GetCellFitMode(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetFitMode();
}

ibGridCellArea* ibGrid::GetRowArea(int row) const
{
	for (size_t pos = 0; pos < m_rowAreaAt.size(); pos++) {
		ibGridCellArea& entry = m_rowAreaAt[pos];
		if (entry.m_start <= row && entry.m_end >= row)
			return &entry;
	}

	return NULL;
}

ibGridCellArea* ibGrid::GetColArea(int col) const
{
	for (size_t pos = 0; pos < m_colAreaAt.size(); pos++) {
		ibGridCellArea& entry = m_colAreaAt[pos];
		if (entry.m_start <= col && entry.m_end >= col)
			return &entry;
	}

	return NULL;
}

ibGrid::CellSpan
ibGrid::GetCellSize(int row, int col, int* num_rows, int* num_cols) const
{
	GetCellAttrPtr(row, col)->GetSize(num_rows, num_cols);

	return GetCellSpan(*num_rows, *num_cols);
}

ibGridCellRenderer* ibGrid::GetCellRenderer(int row, int col) const
{
	return  GetCellAttrPtr(row, col)->GetRenderer(this, row, col);
}

ibGridCellEditor* ibGrid::GetCellEditor(int row, int col) const
{
	return GetCellAttrPtr(row, col)->GetEditor(this, row, col);
}

bool ibGrid::IsCellReadOnly(int row, int col) const
{
	return GetCellAttrPtr(row, col)->IsReadOnly();
}

// ----------------------------------------------------------------------------
// area support: cache, automatic creation, ...
// ----------------------------------------------------------------------------

void ibGrid::InsertRowArea(size_t index, ibGridCellArea& entry)
{
	m_rowAreaAt.Insert(entry, index);

	//support printing
	SetRowBrake(entry.m_end);

	CalcDimensions();
}

void ibGrid::InsertColArea(size_t index, ibGridCellArea& entry)
{
	m_colAreaAt.Insert(entry, index);

	//support printing
	SetColBrake(entry.m_end);

	CalcDimensions();
}

void ibGrid::SetRowAreaStartSize(int idx, int w)
{
	if (idx > (int)m_rowAreaAt.Count())
		return;

	m_rowAreaAt[idx].m_start = w;

	CalcDimensions();
}

void ibGrid::SetRowAreaEndSize(int idx, int w)
{
	if (idx > (int)m_rowAreaAt.Count())
		return;

	m_rowAreaAt[idx].m_end = w;

	CalcDimensions();
}

int ibGrid::GetRowAreaStartSize(int idx)
{
	if (idx > (int)m_rowAreaAt.Count())
		return -1;

	return m_rowAreaAt[idx].m_start;
}

int ibGrid::GetRowAreaEndSize(int idx)
{
	if (idx > (int)m_rowAreaAt.Count())
		return -1;

	return m_rowAreaAt[idx].m_end;
}

void ibGrid::SetColAreaStartSize(int idx, int w)
{
	if (idx > (int)m_colAreaAt.Count())
		return;

	m_colAreaAt[idx].m_start = w;

	CalcDimensions();
}

void ibGrid::SetColAreaEndSize(int idx, int w)
{
	if (idx > (int)m_colAreaAt.Count())
		return;

	m_colAreaAt[idx].m_end = w;

	CalcDimensions();
}

int ibGrid::GetColAreaStartSize(int idx)
{
	if (idx > (int)m_colAreaAt.Count())
		return -1;

	return m_colAreaAt[idx].m_start;
}

int ibGrid::GetColAreaEndSize(int idx)
{
	if (idx > (int)m_colAreaAt.Count())
		return -1;

	return m_colAreaAt[idx].m_end;
}

void ibGrid::SetRowAreaLabel(int idx, const wxString& label)
{
	if (idx > (int)m_rowAreaAt.Count())
		return;

	m_rowAreaAt[idx].m_areaLabel = label;

	CalcDimensions();
}

void ibGrid::SetColAreaLabel(int idx, const wxString& label)
{
	if (idx > (int)m_colAreaAt.Count())
		return;

	m_colAreaAt[idx].m_areaLabel = label;

	CalcDimensions();
}

wxString ibGrid::GetRowAreaLabel(int idx)
{
	if (idx > (int)m_rowAreaAt.Count())
		return wxT("");

	return m_rowAreaAt[idx].m_areaLabel;
}

wxString ibGrid::GetColAreaLabel(int idx)
{
	if (idx > (int)m_colAreaAt.Count())
		return wxT("");

	return m_colAreaAt[idx].m_areaLabel;
}

void ibGrid::CreateArea()
{
	if (m_selection != NULL)
	{
		int row1 = m_numRows, col1 = m_numCols,
			row2 = 0, col2 = 0; bool hasBlocks = false;

		for (const auto coords : GetSelectedBlocks()) {
			if (row1 > coords.GetTopRow()) row1 = coords.GetTopRow();
			if (col1 > coords.GetLeftCol()) col1 = coords.GetLeftCol();
			if (row2 < coords.GetBottomRow()) row2 = coords.GetBottomRow();
			if (col2 < coords.GetRightCol()) col2 = coords.GetRightCol();
			hasBlocks = true;
		}

		if (!hasBlocks) {
			if (row1 > GetGridCursorRow()) row1 = GetGridCursorRow();
			if (col1 > GetGridCursorCol()) col1 = GetGridCursorCol();
			if (row2 < GetGridCursorRow()) row2 = GetGridCursorRow();
			if (col2 < GetGridCursorCol()) col2 = GetGridCursorCol();
		}

		if (col2 == m_numCols - 1 && row2 != m_numRows - 1) {
			DoRowAreaCreate(row1, row2); //horzontal
		}
		else if (col2 != m_numCols - 1 && row2 == m_numRows - 1) {
			DoColAreaCreate(col1, col2); //vertical
		}
	}
}

void ibGrid::DeleteRowArea(size_t index)
{
	if (index > m_rowAreaAt.GetCount())
		return;

	m_rowAreaAt.Detach(index);

	CalcDimensions();
}

void ibGrid::DeleteColArea(size_t index)
{
	if (index > m_colAreaAt.GetCount())
		return;

	m_colAreaAt.Detach(index);

	CalcDimensions();
}

void ibGrid::DeleteArea()
{
	if (m_selection != NULL)
	{
		int row1 = m_numRows, col1 = m_numCols,
			row2 = 0, col2 = 0; bool hasBlocks = false;

		for (const auto coords : GetSelectedBlocks()) {
			if (row1 > coords.GetTopRow()) row1 = coords.GetTopRow();
			if (col1 > coords.GetLeftCol()) col1 = coords.GetLeftCol();
			if (row2 < coords.GetBottomRow()) row2 = coords.GetBottomRow();
			if (col2 < coords.GetRightCol()) col2 = coords.GetRightCol();
			hasBlocks = true;
		}

		if (!hasBlocks) {
			if (row1 > GetGridCursorRow()) row1 = GetGridCursorRow();
			if (col1 > GetGridCursorCol()) col1 = GetGridCursorCol();
			if (row2 < GetGridCursorRow()) row2 = GetGridCursorRow();
			if (col2 < GetGridCursorCol()) col2 = GetGridCursorCol();
		}

		if (col2 == m_numCols - 1 && row2 != m_numRows - 1) {
			DoRowAreaDelete(row1, row2); //horzontal
		}
		else if (col2 != m_numCols - 1 && row2 == m_numRows - 1) {
			DoColAreaDelete(col1, col2); //vertical
		}
	}
}

//make new name
bool ibGrid::MakeRowAreaLabel(ibGridCellArea* rowArea)
{
	if (rowArea != NULL)
	{
		ibGridDialogInputArea dlg(this);
		dlg.SetAreaLabel(rowArea->m_areaLabel);

		if (dlg.ShowModal() == wxID_OK)
		{
			const wxString& areaLabel = dlg.GetAreaLabel();

			for (unsigned int idx = 0; idx < m_rowAreaAt.size(); idx++)
			{
				if (rowArea != &m_rowAreaAt[idx] && areaLabel == m_rowAreaAt[idx].m_areaLabel)
				{
					wxMessageBox(_("Wrong area name!"), _("Grid editor"));
					return false;
				}
			}

			bool valid_area = true;

			for (const auto c : areaLabel)
			{
				if (c == wxT('_') || iswalpha(c) || isdigit(c))
					continue;

				valid_area = false;
				break;
			}

			if (valid_area)
			{
				const wxString newValue = areaLabel;
				const wxString oldValue = rowArea->m_areaLabel;

				rowArea->m_areaLabel = areaLabel;

				const int index = m_rowAreaAt.Index(*rowArea);
				if (index >= 0)
				{
					SendGridAreaEvent(wxEVT_GRID_ROW_AREA_NAME, index, *rowArea);

					PushCommand<ibGridCommandAreaName>(index, ibGridCommandAreaName::AreaRow, newValue, oldValue);
				}

				return true;
			}

			wxMessageBox(_("Wrong area name!"), _("Grid editor"));
		}
	}

	return false;
}

bool ibGrid::MakeColAreaLabel(ibGridCellArea* colArea)
{
	if (colArea != NULL)
	{
		ibGridDialogInputArea dlg(this);
		dlg.SetAreaLabel(colArea->m_areaLabel);

		if (dlg.ShowModal() == wxID_OK)
		{
			const wxString& areaLabel = dlg.GetAreaLabel();

			for (unsigned int idx = 0; idx < m_colAreaAt.size(); idx++)
			{
				if (colArea != &m_colAreaAt[idx] && areaLabel == m_colAreaAt[idx].m_areaLabel)
				{
					wxMessageBox(_("Wrong area name!"), _("Grid editor"), wxOK | wxCENTRE, this);
					return false;
				}
			}

			bool valid_area = true;

			for (const auto c : areaLabel)
			{
				if (c == wxT('_') || iswalpha(c) || isdigit(c))
					continue;

				valid_area = false;
				break;
			}

			if (valid_area)
			{
				const wxString newValue = areaLabel;
				const wxString oldValue = colArea->m_areaLabel;

				colArea->m_areaLabel = areaLabel;

				const int index = m_colAreaAt.Index(*colArea);
				if (index >= 0)
				{
					SendGridAreaEvent(wxEVT_GRID_COL_AREA_NAME, index, *colArea);

					PushCommand<ibGridCommandAreaName>(index, ibGridCommandAreaName::AreaCol, newValue, oldValue);
				}

				return true;
			}

			wxMessageBox(_("Wrong area name!"), _("Grid editor"), wxOK | wxCENTRE, this);
		}
	}

	return false;
}

void ibGrid::AddRowBrake(int row)
{
	int index = m_rowBrakeAt.Index(row);
	if (index != -1)
		return;

	m_rowBrakeAt.Add(row);

	// make up a dummy event for the grid event to use -- unfortunately we
	// can't do anything else here
	wxMouseEvent e;
	e.SetState(wxGetMouseState());
	ScreenToClient(&e.m_x, &e.m_y);

	SendGridSizeEvent(wxEVT_GRID_ROW_BRAKE_ADD, row, e);
}

void ibGrid::SetRowBrake(int row)
{
	if (m_table)
	{
		const int maxRowBrake = GetMaxRowBrake();

		if (!m_rowBrakeAt.IsEmpty())
			m_rowBrakeAt[m_rowBrakeAt.Count() - 1] = wxMax(wxMin(m_rowBrakeAt.Last(), m_table->GetRowsCount() - 1), row);
		else
			m_rowBrakeAt.Add(row);

		if (maxRowBrake != GetMaxRowBrake())
		{
			// make up a dummy event for the grid event to use -- unfortunately we
			// can't do anything else here
			wxMouseEvent e;
			e.SetState(wxGetMouseState());
			ScreenToClient(&e.m_x, &e.m_y);

			SendGridSizeEvent(wxEVT_GRID_ROW_BRAKE_SET, row, e);
		}
	}
}

void ibGrid::AddColBrake(int col)
{
	int index = m_colBrakeAt.Index(col);
	if (index != -1)
		return;

	m_colBrakeAt.Add(col);

	// make up a dummy event for the grid event to use -- unfortunately we
	// can't do anything else here
	wxMouseEvent e;
	e.SetState(wxGetMouseState());
	ScreenToClient(&e.m_x, &e.m_y);

	SendGridSizeEvent(wxEVT_GRID_COL_BRAKE_ADD, col, e);
}

void ibGrid::DeleteRowBrake(int row)
{
	int index = m_rowBrakeAt.Index(row);
	if (index == -1)
		return;

	m_rowBrakeAt.Remove(row);

	// make up a dummy event for the grid event to use -- unfortunately we
	// can't do anything else here
	wxMouseEvent e;
	e.SetState(wxGetMouseState());
	ScreenToClient(&e.m_x, &e.m_y);

	SendGridSizeEvent(wxEVT_GRID_ROW_BRAKE_DELETE, row, e);
}

void ibGrid::DeleteColBrake(int col)
{
	int index = m_colBrakeAt.Index(col);
	if (index == -1)
		return;

	m_colBrakeAt.Remove(col);

	// make up a dummy event for the grid event to use -- unfortunately we
	// can't do anything else here
	wxMouseEvent e;
	e.SetState(wxGetMouseState());
	ScreenToClient(&e.m_x, &e.m_y);

	SendGridSizeEvent(wxEVT_GRID_COL_BRAKE_DELETE, col, e);

}

void ibGrid::SetColBrake(int col)
{
	if (m_table)
	{
		const int maxColBrake = GetMaxColBrake();

		if (!m_colBrakeAt.IsEmpty())
			m_colBrakeAt[m_colBrakeAt.Count() - 1] = wxMax(wxMin(m_colBrakeAt.Last(), m_table->GetColsCount() - 1), col);
		else
			m_colBrakeAt.Add(col);

		if (maxColBrake != GetMaxColBrake())
		{
			// make up a dummy event for the grid event to use -- unfortunately we
			// can't do anything else here
			wxMouseEvent e;
			e.SetState(wxGetMouseState());
			ScreenToClient(&e.m_x, &e.m_y);

			SendGridSizeEvent(wxEVT_GRID_COL_BRAKE_SET, col, e);
		}
	}
}

// ----------------------------------------------------------------------------
// attribute support: cache, automatic provider creation, ...
// ----------------------------------------------------------------------------

bool ibGrid::CanHaveAttributes() const
{
	if (!m_table)
	{
		return false;
	}

	return m_table->CanHaveAttributes();
}

void ibGrid::ClearAttrCache()
{
	if (m_attrCache.row != -1)
	{
		ibGridCellAttr* oldAttr = m_attrCache.attr;
		m_attrCache.attr = NULL;
		m_attrCache.row = -1;
		// wxSafeDecRec(...) might cause event processing that accesses
		// the cached attribute, if one exists (e.g. by deleting the
		// editor stored within the attribute). Therefore it is important
		// to invalidate the cache  before calling wxSafeDecRef!
		wxSafeDecRef(oldAttr);
	}
}

void ibGrid::RefreshAttr(int row, int col)
{
	if (m_attrCache.row == row && m_attrCache.col == col)
		ClearAttrCache();
}


void ibGrid::CacheAttr(int row, int col, ibGridCellAttr* attr) const
{
	if (attr != NULL)
	{
		ibGrid* const self = const_cast<ibGrid*>(this);

		self->ClearAttrCache();
		self->m_attrCache.row = row;
		self->m_attrCache.col = col;
		self->m_attrCache.attr = attr;
		wxSafeIncRef(attr);
	}
}

bool ibGrid::LookupAttr(int row, int col, ibGridCellAttr** attr) const
{
	if (row == m_attrCache.row && col == m_attrCache.col)
	{
		*attr = m_attrCache.attr;
		wxSafeIncRef(m_attrCache.attr);

#ifdef DEBUG_ATTR_CACHE
		gs_nAttrCacheHits++;
#endif

		return true;
	}
	else
	{
#ifdef DEBUG_ATTR_CACHE
		gs_nAttrCacheMisses++;
#endif

		return false;
	}
}

ibGridCellAttr* ibGrid::GetCellAttr(int row, int col) const
{
	ibGridCellAttr* attr = NULL;
	// Additional test to avoid looking at the cache e.g. for
	// wxNoCellCoords, as this will confuse memory management.
	if (row >= 0)
	{
		if (!LookupAttr(row, col, &attr))
		{
			attr = m_table ? m_table->GetAttr(row, col, ibGridCellAttr::Any)
				: NULL;
			CacheAttr(row, col, attr);
		}
	}

	if (attr)
	{
		attr->SetDefAttr(m_defaultCellAttr);
	}
	else
	{
		attr = m_defaultCellAttr;
		attr->IncRef();
	}

	return attr;
}

ibGridCellAttr* ibGrid::GetOrCreateCellAttr(int row, int col) const
{
	ibGridCellAttr* attr = NULL;
	const bool canHave = CanHaveAttributes();

	wxCHECK_MSG(canHave, attr, wxT("Cell attributes not allowed"));
	wxCHECK_MSG(m_table, attr, wxT("must have a table"));

	attr = m_table->GetAttr(row, col, ibGridCellAttr::Cell);
	if (!attr)
	{
		attr = new ibGridCellAttr(m_defaultCellAttr);

		// artificially inc the ref count to match DecRef() in caller
		attr->IncRef();
		m_table->SetAttr(attr, row, col);
	}

	return attr;
}

// ----------------------------------------------------------------------------
// setting column attributes (wrappers around SetColAttr)
// ----------------------------------------------------------------------------

void ibGrid::SetColFormatBool(int col)
{
	SetColFormatCustom(col, wxGRID_VALUE_BOOL);
}

void ibGrid::SetColFormatNumber(int col)
{
	SetColFormatCustom(col, wxGRID_VALUE_NUMBER);
}

void ibGrid::SetColFormatFloat(int col, int width, int precision)
{
	wxString typeName = wxGRID_VALUE_FLOAT;
	if ((width != -1) || (precision != -1))
	{
		typeName << wxT(':') << width << wxT(',') << precision;
	}

	SetColFormatCustom(col, typeName);
}

void ibGrid::SetColFormatDate(int col, const wxString& format)
{
	wxString typeName = wxGRID_VALUE_DATE;
	if (!format.empty())
	{
		typeName << ':' << format;
	}
	SetColFormatCustom(col, typeName);
}

void ibGrid::SetColFormatCustom(int col, const wxString& typeName)
{
	ibGridCellAttr* attr = m_table->GetAttr(-1, col, ibGridCellAttr::Col);
	if (!attr)
		attr = new ibGridCellAttr;
	ibGridCellRenderer* renderer = GetDefaultRendererForType(typeName);
	attr->SetRenderer(renderer);
	ibGridCellEditor* editor = GetDefaultEditorForType(typeName);
	attr->SetEditor(editor);

	SetColAttr(col, attr);

}

// ----------------------------------------------------------------------------
// setting cell attributes: this is forwarded to the table
// ----------------------------------------------------------------------------

void ibGrid::SetAttr(int row, int col, ibGridCellAttr* attr)
{
	if (CanHaveAttributes())
	{
		m_table->SetAttr(attr, row, col);
		ClearAttrCache();
	}
	else
	{
		wxSafeDecRef(attr);
	}
}

void ibGrid::SetRowAttr(int row, ibGridCellAttr* attr)
{
	if (CanHaveAttributes())
	{
		m_table->SetRowAttr(attr, row);
		ClearAttrCache();
	}
	else
	{
		wxSafeDecRef(attr);
	}

}

void ibGrid::SetColAttr(int col, ibGridCellAttr* attr)
{
	if (CanHaveAttributes())
	{
		m_table->SetColAttr(attr, col);
		ClearAttrCache();
	}
	else
	{
		wxSafeDecRef(attr);
	}

}

void ibGrid::SetCellBackgroundColour(int row, int col, const wxColour& colour, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrBackgroundColour>(row, col, colour, GetCellBackgroundColour(row, col));

		attr->SetBackgroundColour(colour);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellBackgroundColour(const ibGridBlockCoords& coords, const wxColour& colour, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrBackgroundColour>>(this, coords, colour);

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetBackgroundColour(colour);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellTextColour(int row, int col, const wxColour& colour, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrTextColour>(row, col, colour, attr->GetTextColour());

		attr->SetTextColour(colour);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellTextColour(const ibGridBlockCoords& coords, const wxColour& colour, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrTextColour>>(this, coords, colour);

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetTextColour(colour);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellTextOrient(int row, int col, const int& orient, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrTextOrient>(row, col, orient, attr->GetTextOrient());

		attr->SetTextOrient(orient);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellTextOrient(const ibGridBlockCoords& coords, const int& orient, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrTextOrient>>(this, coords, orient);

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetTextOrient(orient);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellFont(int row, int col, const wxFont& font, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrFont>(row, col, font, attr->GetFont());

		attr->SetFont(font);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellFont(const ibGridBlockCoords& coords, const wxFont& font, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrFont>>(this, coords, font);

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetFont(font);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellAlignment(int row, int col, int horiz, int vert, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		wxSize alignment; attr->GetAlignment(&alignment.x, &alignment.y);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrAlignment>(row, col, wxSize{ horiz, vert }, alignment);

		attr->SetAlignment(horiz, vert);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellAlignment(const ibGridBlockCoords& coords, int horiz, int vert, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrAlignment>>(this, coords, wxSize{ horiz, vert });

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetAlignment(horiz, vert);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellBorderLeft(int row, int col, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrBorderLeft>(row, col, ibGridCellBorder{ style, colour, width }, attr->GetBorderLeft());

		attr->SetBorderLeft(style, colour, width);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellBorderLeft(const ibGridBlockCoords& coords, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrBorderLeft>>(this, coords, ibGridCellBorder{ style, colour, width });

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetBorderLeft(style, colour, width);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellBorderRight(int row, int col, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrBorderRight>(row, col, ibGridCellBorder{ style, colour, width }, attr->GetBorderRight());

		attr->SetBorderRight(style, colour, width);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellBorderRight(const ibGridBlockCoords& coords, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrBorderRight>>(this, coords, ibGridCellBorder{ style, colour, width });

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetBorderRight(style, colour, width);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellBorderTop(int row, int col, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrBorderTop>(row, col, ibGridCellBorder{ style, colour, width }, attr->GetBorderTop());

		attr->SetBorderTop(style, colour, width);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellBorderTop(const ibGridBlockCoords& coords, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrBorderTop>>(this, coords, ibGridCellBorder{ style, colour, width });

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetBorderTop(style, colour, width);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellBorderBottom(int row, int col, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrBorderBottom>(row, col, ibGridCellBorder{ style, colour, width }, attr->GetBorderBottom());

		attr->SetBorderBottom(style, colour, width);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellBorderBottom(const ibGridBlockCoords& coords, wxPenStyle style, const wxColour& colour, int width, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrBorderBottom>>(this, coords, ibGridCellBorder{ style, colour, width });

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetBorderBottom(style, colour, width);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellFitMode(int row, int col, ibGridFitMode fitMode, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrFitMode>(row, col, fitMode, attr->GetFitMode());

		attr->SetFitMode(fitMode);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellFitMode(const ibGridBlockCoords& coords, ibGridFitMode fitMode, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrFitMode>>(this, coords, fitMode);

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetFitMode(fitMode);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

void ibGrid::SetCellSize(int row, int col, int num_rows, int num_cols, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		int cell_rows, cell_cols;

		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);
		attr->GetSize(&cell_rows, &cell_cols);
		attr->SetSize(num_rows, num_cols);

		// Cannot set the size of a cell to 0 or negative values
		// While it is perfectly legal to do that, this function cannot
		// handle all the possibilies, do it by hand by getting the CellAttr.
		// You can only set the size of a cell to 1,1 or greater with this fn
		wxASSERT_MSG(!((cell_rows < 1) || (cell_cols < 1)),
			wxT("ibGrid::SetCellSize setting cell size that is already part of another cell"));
		wxASSERT_MSG(!((num_rows < 1) || (num_cols < 1)),
			wxT("ibGrid::SetCellSize setting cell size to < 1"));

		// if this was already a multicell then "turn off" the other cells first
		if ((cell_rows > 1) || (cell_cols > 1))
		{
			int i, j;
			for (j = row; j < row + cell_rows; j++)
			{
				for (i = col; i < col + cell_cols; i++)
				{
					if ((i != col) || (j != row))
					{
						GetOrCreateCellAttrPtr(j, i)->SetSize(1, 1);

						SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, j, i);
					}
				}
			}
		}

		// mark the cells that will be covered by this cell to
		// negative or zero values to point back at this cell
		if (((num_rows > 1) || (num_cols > 1)) && (num_rows >= 1) && (num_cols >= 1))
		{
			int i, j;
			for (j = row; j < row + num_rows; j++)
			{
				for (i = col; i < col + num_cols; i++)
				{
					if ((i != col) || (j != row))
					{
						GetOrCreateCellAttrPtr(j, i)->SetSize(row - j, col - i);

						SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, j, i);
					}
				}
			}
		}

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrSize>(row, col,
				wxSize{ num_rows, num_cols }, wxSize{ cell_rows, cell_cols });

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row + num_rows - 1);
		SetColBrake(col + num_cols - 1);
	}
}

void ibGrid::SetCellRenderer(int row, int col, ibGridCellRenderer* renderer)
{
	if (CanHaveAttributes())
	{
		GetOrCreateCellAttrPtr(row, col)->SetRenderer(renderer);
	}
}

void ibGrid::SetCellEditor(int row, int col, ibGridCellEditor* editor)
{
	if (CanHaveAttributes())
	{
		GetOrCreateCellAttrPtr(row, col)->SetEditor(editor);
	}
}

void ibGrid::SetCellReadOnly(int row, int col, bool isReadOnly, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);

		if (sendUndoCommand)
			PushCommand<ibGridCommandAttrReadOnly>(row, col, isReadOnly, attr->IsReadOnly());

		attr->SetReadOnly(isReadOnly);

		SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);

		//support printing
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellReadOnly(const ibGridBlockCoords& coords, bool isReadOnly, bool sendUndoCommand)
{
	if (CanHaveAttributes())
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCompositeAttr<ibGridCommandAttrReadOnly>>(this, coords, isReadOnly);

		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
		{
			for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
			{
				GetOrCreateCellAttrPtr(row, col)->SetReadOnly(isReadOnly);

				SendEvent(wxEVT_GRID_TABLE_ATTR_MODIFIED, row, col);
			}
		}

		//support printing
		SetRowBrake(coords.GetBottomRow());
		SetColBrake(coords.GetRightCol());
	}
}

// ----------------------------------------------------------------------------
// Data type registration
// ----------------------------------------------------------------------------

void ibGrid::RegisterDataType(const wxString& typeName,
	ibGridCellRenderer* renderer,
	ibGridCellEditor* editor)
{
	m_typeRegistry->RegisterDataType(typeName, renderer, editor);
}

ibGridCellEditor* ibGrid::GetDefaultEditorForCell(int row, int col) const
{
	if (!m_table)
		return NULL;

	static wxString typeName;
	if (!m_table->GetTypeName(row, col, typeName))
		return NULL;
	return GetDefaultEditorForType(typeName);
}

ibGridCellRenderer* ibGrid::GetDefaultRendererForCell(int row, int col) const
{
	if (!m_table)
		return NULL;

	static wxString typeName;
	if (!m_table->GetTypeName(row, col, typeName))
		return NULL;

	return GetDefaultRendererForType(typeName);
}

ibGridCellEditor* ibGrid::GetDefaultEditorForType(const wxString& typeName) const
{
	int index = m_typeRegistry->FindOrCloneDataType(typeName);
	if (index == wxNOT_FOUND)
	{
		wxFAIL_MSG(wxString::Format(wxT("Unknown data type name [%s]"), typeName.c_str()));

		return NULL;
	}

	return m_typeRegistry->GetEditor(index);
}

ibGridCellRenderer* ibGrid::GetDefaultRendererForType(const wxString& typeName) const
{
	int index = m_typeRegistry->FindOrCloneDataType(typeName);
	if (index == wxNOT_FOUND)
	{
		wxFAIL_MSG(wxString::Format(wxT("Unknown data type name [%s]"), typeName.c_str()));

		return NULL;
	}

	return m_typeRegistry->GetRenderer(index);
}

// ----------------------------------------------------------------------------
// row/col size
// ----------------------------------------------------------------------------

void ibGrid::DoDisableLineResize(int line, ibGridFixedIndicesSet*& setFixed)
{
	if (!setFixed)
	{
		setFixed = new ibGridFixedIndicesSet;
	}

	setFixed->insert(line);
}

bool
ibGrid::DoCanResizeLine(int line, const ibGridFixedIndicesSet* setFixed) const
{
	return !setFixed || !setFixed->count(line);
}

void ibGrid::EnableDragRowSize(bool enable)
{
	m_canDragRowSize = enable;
}

void ibGrid::EnableDragColSize(bool enable)
{
	m_canDragColSize = enable;
}

void ibGrid::EnableDragGridSize(bool enable)
{
	m_canDragGridSize = enable;
}

void ibGrid::EnableDragCell(bool enable)
{
	m_canDragCell = enable;
}

void ibGrid::SetDefaultRowSize(int height, float scale, bool resizeExistingRows)
{
	m_defaultRowHeight = wxMax(ibRestoreGridScale(height, scale), m_minAcceptableRowHeight);

	if (resizeExistingRows)
	{
		// since we are resizing all rows to the default row size,
		// we can simply clear the row heights and row bottoms
		// arrays (which also allows us to take advantage of
		// some speed optimisations)
		m_rowHeights.Empty();

		CalcDimensions();
	}
}

namespace
{

	// This is a common part of SetRowSize() and SetColSize() which takes care of
	// updating the height/width of a row/column depending on its current value and
	// the new one.
	//
	// Returns the difference between the new and the old size.
	int UpdateRowOrColSize(int& sizeCurrent, int sizeNew)
	{
		// On input here sizeCurrent can be negative if it's currently hidden (the
		// real size is its absolute value then). And sizeNew can be 0 to indicate
		// that the row/column should be hidden or -1 to indicate that it should be
		// shown again.

		if (sizeNew < 0)
		{
			// We're showing back a previously hidden row/column.
			wxASSERT_MSG(sizeNew == -1, wxS("New size must be positive or -1."));

			// If it's already visible, simply do nothing.
			if (sizeCurrent >= 0)
				return 0;

			// Otherwise show it by restoring its old size.
			sizeCurrent = -sizeCurrent;

			// This is positive which is correct.
			return sizeCurrent;
		}
		else if (sizeNew == 0)
		{
			// We're hiding a row/column.

			// If it's already hidden, simply do nothing.
			if (sizeCurrent <= 0)
				return 0;

			// Otherwise hide it and also remember the shown size to be able to
			// restore it later.
			sizeCurrent = -sizeCurrent;

			// This is negative which is correct.
			return sizeCurrent;
		}
		else // We're just changing the row/column size.
		{
			// Here it could have been hidden or not previously.
			const int sizeOld = sizeCurrent < 0 ? 0 : sizeCurrent;

			sizeCurrent = sizeNew;

			return sizeCurrent - sizeOld;
		}
	}

} // anonymous namespace

void ibGrid::SetRowSize(int row, int height, float scale, bool sendUndoCommand)
{
	height = ibRestoreGridScale(height, scale);

	// See comment in SetColSize
	if (height > 0 && height < GetRowMinimalAcceptableHeight())
		return;

	// The value of -1 is special and means to fit the height to the row label.
	// As with the columns, ignore attempts to auto-size the hidden rows.
	if (height == -1 && GetRowHeight(row) != 0)
	{
		long w, h;
		wxArrayString lines;
		wxClientDC dc(m_rowLabelWin);
		dc.SetFont(GetLabelFont());
		ParseLines(GetRowLabelValue(row), lines);
		GetTextBoxSize(dc, lines, &w, &h);

		// As with the columns, don't make the row smaller than minimal height.
		height = wxMax(h, GetRowMinimalHeight(row));
	}

	const int size = GetRowSize(row);

	DoSetRowSize(row, height);

	if (!sendUndoCommand || m_dragRowOrCol != -1 || height == size)
		return;

	PushCommand<ibGridCommandRowSize>(row,
		height, size);
}

void ibGrid::HideRow(int row)
{
	const int size = GetRowSize(row);

	DoSetRowSize(row, 0);

	if (m_dragRowOrCol != -1)
		return;

	PushCommand<ibGridCommandRowSize>(row, 0, size);
}

void ibGrid::ShowRow(int row)
{
	const int size = GetRowSize(row);

	DoSetRowSize(row, -1);

	if (m_dragRowOrCol != -1)
		return;

	PushCommand<ibGridCommandRowSize>(row, -1, size);
}

void ibGrid::DoSetRowSize(int row, int height)
{
	wxCHECK_RET(row >= 0 && row < m_numRows, wxT("invalid row index"));

	if (m_rowHeights.IsEmpty())
	{
		// need to really create the array
		InitRowHeights();
	}

	const int diff = UpdateRowOrColSize(m_rowHeights[row], height);
	if (!diff)
		return;


	InvalidateBestSize();

	CalcDimensions();

	if (ShouldRefresh())
	{
		// We need to check the size of all the currently visible cells and
		// decrease the row to cover the start of the multirow cells, if any,
		// because we need to refresh such cells entirely when resizing.
		int topRow = row;

		// Note that we don't care about the cells in frozen windows here as
		// they can't have multiple rows currently.
		const wxRect rect = m_gridWin->GetRect();
		int left, right;
		CalcUnscrolledPosition(rect.GetLeft(), 0, &left, NULL);
		CalcUnscrolledPosition(rect.GetRight(), 0, &right, NULL);

		const int posLeft = XToPos(left, m_gridWin);
		const int posRight = XToPos(right, m_gridWin);
		for (int pos = posLeft; pos <= posRight; ++pos)
		{
			int col = GetColAt(pos);

			int numRows, numCols;
			if (GetCellSize(row, col, &numRows, &numCols) == CellSpan_Inside)
			{
				// Notice that numRows here is negative.
				if (row + numRows < topRow)
					topRow = row + numRows;
			}
		}

		// Helper object to refresh part of the window below the given position
		// (in physical coordinates).
		class LowerWindowPartRefresher
		{
		public:
			explicit LowerWindowPartRefresher(int top)
				: m_top(top)
			{
			}

			void operator()(wxWindow* w) const
			{
				wxSize size = w->GetClientSize();
				if (size.y > m_top)
				{
					size.y -= m_top;
					w->RefreshRect(wxRect(wxPoint(0, m_top), size));
				}
				//else: the area to refresh is not in view anyhow
			}

		private:
			const int m_top;
		};

		int y;
		CalcScrolledPosition(0, GetRowTop(topRow, GetGridZoom()), NULL, &y);

		if (topRow < m_numFrozenRows)
		{
			// This row is frozen, refresh the frozen windows.
			LowerWindowPartRefresher refreshLowerPart(y);

			refreshLowerPart(m_rowFrozenAreaWin);
			refreshLowerPart(m_rowFrozenLabelWin);
			refreshLowerPart(m_frozenRowGridWin);

			// If there are any frozen columns as well, there is one more
			// window to refresh.
			if (m_frozenCornerGridWin)
				refreshLowerPart(m_frozenCornerGridWin);
		}
		else // This row is not frozen.
		{
			// If we have any frozen rows, all the windows we're refreshing
			// here are offset by their height.
			if (m_rowFrozenLabelWin)
				y -= m_rowFrozenLabelWin->GetSize().y;

			LowerWindowPartRefresher refreshLowerPart(y);

			refreshLowerPart(m_rowAreaWin);
			refreshLowerPart(m_rowLabelWin);
			refreshLowerPart(m_gridWin);

			if (m_frozenColGridWin)
				refreshLowerPart(m_frozenColGridWin);
		}
	}

	if (m_dragRowOrCol == -1)
	{
		// make up a dummy event for the grid event to use -- unfortunately we
		// can't do anything else here
		wxMouseEvent e;
		e.SetState(wxGetMouseState());
		ScreenToClient(&e.m_x, &e.m_y);

		SendGridSizeEvent(wxEVT_GRID_ROW_MODIFIED, row, e);
	}

	//support printing
	SetRowBrake(row);
}

void ibGrid::SetDefaultColSize(int width, float scale, bool resizeExistingCols)
{
	// we dont allow zero default column width
	m_defaultColWidth = wxMax(wxMax(ibRestoreGridScale(width, scale), m_minAcceptableColWidth), 1);

	if (resizeExistingCols)
	{
		// since we are resizing all columns to the default column size,
		// we can simply clear the col widths and col rights
		// arrays (which also allows us to take advantage of
		// some speed optimisations)
		m_colWidths.Empty();

		CalcDimensions();
	}
}

void ibGrid::SetColSize(int col, int width, float scale, bool sendUndoCommand)
{
	width = ibRestoreGridScale(width, scale);

	// we intentionally don't test whether the width is less than
	// GetColMinimalWidth() here but we do compare it with
	// GetColMinimalAcceptableWidth() as otherwise things currently break (see
	// #651) -- and we also always allow the width of 0 as it has the special
	// sense of hiding the column
	if (width > 0 && width < GetColMinimalAcceptableWidth())
		return;

	// The value of -1 is special and means to fit the width to the column label.
	//
	// Notice that we currently don't support auto-sizing hidden columns (we
	// could, but it's not clear whether this is really needed and it would
	// make the code more complex), and for them passing -1 simply means to
	// show the column back using its old size.
	if (width == -1 && GetColWidth(col) != 0)
	{
		if (m_useNativeHeader)
		{
			width = GetGridColHeader()->GetColumnTitleWidth(col);
		}
		else
		{
			long w, h;
			wxArrayString lines;
			wxClientDC dc(m_colLabelWin);
			dc.SetFont(GetLabelFont());
			ParseLines(GetColLabelValue(col), lines);
			if (GetColLabelTextOrientation() == wxHORIZONTAL)
				GetTextBoxSize(dc, lines, &w, &h);
			else
				GetTextBoxSize(dc, lines, &h, &w);
			width = w + 6;
		}

		// Check that it is not less than the minimal width and do use the
		// possibly greater than minimal-acceptable-width minimal-width itself
		// here as we shouldn't become too small when auto-sizing, otherwise
		// the column could be resized to be too small by double clicking its
		// divider line (which ends up in a call to this function) even though
		// it couldn't be resized to this size by dragging it.
		width = wxMax(width, GetColMinimalWidth(col));
	}

	const int size = GetColSize(col);

	DoSetColSize(col, width);

	if (!sendUndoCommand || m_dragRowOrCol != -1 || width == size)
		return;

	PushCommand<ibGridCommandColSize>(col,
		width, size);
}

void ibGrid::HideCol(int col)
{
	const int size = GetColSize(col);

	DoSetColSize(col, 0);

	if (m_dragRowOrCol != -1)
		return;

	PushCommand<ibGridCommandColSize>(col,
		0, size);
}

void ibGrid::ShowCol(int col)
{
	const int size = GetColSize(col);

	DoSetColSize(col, -1);

	if (m_dragRowOrCol != -1)
		return;

	PushCommand<ibGridCommandColSize>(col,
		-1, size);
}

void ibGrid::DoSetColSize(int col, int width)
{
	wxCHECK_RET(col >= 0 && col < m_numCols, wxT("invalid column index"));

	if (m_colWidths.IsEmpty())
	{
		// need to really create the array
		InitColWidths();
	}

	const int diff = UpdateRowOrColSize(m_colWidths[col], width);
	if (!diff)
		return;

	if (m_useNativeHeader)
	{
		// We have to update the native control if we're called from the
		// program (directly or indirectly, e.g. via AutoSizeColumn()), but we
		// want to avoid doing it when the column is being resized
		// interactively, as this is unnecessary and results in very visible
		// flicker, so take care to call the special method of our header
		// control checking for whether it's being resized interactively
		// instead of the usual UpdateColumn().
		static_cast<ibGridHeaderCtrl*>(m_colLabelWin)->UpdateIfNotResizing(col);
	}
	//else: will be refreshed when the header is redrawn

	InvalidateBestSize();

	CalcDimensions();

	if (ShouldRefresh())
	{
		// This code is symmetric with DoSetRowSize(), see there for more
		// comments.

		int leftCol = col;

		const wxRect rect = m_gridWin->GetRect();
		int top, bottom;
		CalcUnscrolledPosition(0, rect.GetTop(), NULL, &top);
		CalcUnscrolledPosition(0, rect.GetBottom(), NULL, &bottom);

		const int posTop = YToPos(top, m_gridWin);
		const int posBottom = YToPos(bottom, m_gridWin);
		for (int pos = posTop; pos <= posBottom; ++pos)
		{
			int row = GetRowAt(pos);

			int numRows, numCols;
			if (GetCellSize(row, col, &numRows, &numCols) == CellSpan_Inside)
			{
				if (col + numCols < leftCol)
					leftCol = col + numCols;
			}
		}

		// This is supposed to be the equivalent of LowerWindowPartRefresher
		// for the rows, but there is no real counterpart to "lower" in
		// horizontal direction, so use the clumsy "further" as the least bad
		// alternative.
		class FurtherWindowPartRefresher
		{
		public:
			explicit FurtherWindowPartRefresher(int left)
				: m_left(left)
			{
			}

			void operator()(wxWindow* w) const
			{
				wxSize size = w->GetClientSize();
				if (size.x > m_left)
				{
					size.x -= m_left;
					w->RefreshRect(wxRect(wxPoint(m_left, 0), size));
				}
			}

		private:
			const int m_left;
		};

		int x;
		CalcScrolledPosition(GetColLeft(leftCol, GetGridZoom()), 0, &x, NULL);

		if (leftCol < m_numFrozenCols)
		{
			FurtherWindowPartRefresher refreshFurtherPart(x);

			refreshFurtherPart(m_colFrozenLabelWin);
			refreshFurtherPart(m_frozenColGridWin);

			if (m_frozenCornerGridWin)
				refreshFurtherPart(m_frozenCornerGridWin);
		}
		else
		{
			if (m_colFrozenLabelWin)
				x -= m_colFrozenLabelWin->GetSize().x;

			FurtherWindowPartRefresher refreshFurtherPart(x);

			// Refreshing the native header is unnecessary, as it updates
			// itself correctly anyhow, and just results in extra flicker.
			refreshFurtherPart(m_colAreaWin);
			if (!IsUsingNativeHeader())
				refreshFurtherPart(m_colLabelWin);

			refreshFurtherPart(m_gridWin);

			if (m_frozenRowGridWin)
				refreshFurtherPart(m_frozenRowGridWin);
		}
	}

	//support printing
	SetColBrake(col);

	if (m_dragRowOrCol == -1)
	{
		// make up a dummy event for the grid event to use -- unfortunately we
		// can't do anything else here
		wxMouseEvent e;
		e.SetState(wxGetMouseState());
		ScreenToClient(&e.m_x, &e.m_y);

		SendGridSizeEvent(wxEVT_GRID_COL_MODIFIED, col, e);
	}
}

void ibGrid::SetColMinimalWidth(int col, int width, float scale)
{
	if (width > GetColMinimalAcceptableWidth())
	{
		wxLongToLongHashMap::key_type key = (wxLongToLongHashMap::key_type)col;
		m_colMinWidths[key] = ibRestoreGridScale(width, scale);
	}
}

void ibGrid::SetRowMinimalHeight(int row, int width, float scale)
{
	if (width > GetRowMinimalAcceptableHeight())
	{
		wxLongToLongHashMap::key_type key = (wxLongToLongHashMap::key_type)row;
		m_rowMinHeights[key] = ibRestoreGridScale(width, scale);
	}
}

int ibGrid::GetColMinimalWidth(int col, float scale) const
{
	wxLongToLongHashMap::key_type key = (wxLongToLongHashMap::key_type)col;
	wxLongToLongHashMap::const_iterator it = m_colMinWidths.find(key);

	return it != m_colMinWidths.end() ? ibCalcGridScale(it->second, scale) : ibCalcGridScale(m_minAcceptableColWidth, scale);
}

int ibGrid::GetRowMinimalHeight(int row, float scale) const
{
	wxLongToLongHashMap::key_type key = (wxLongToLongHashMap::key_type)row;
	wxLongToLongHashMap::const_iterator it = m_rowMinHeights.find(key);

	return it != m_rowMinHeights.end() ? ibCalcGridScale(it->second, scale) : ibCalcGridScale(m_minAcceptableRowHeight, scale);
}

void ibGrid::SetColMinimalAcceptableWidth(int width, float scale)
{
	// We do allow a width of 0 since this gives us
	// an easy way to temporarily hiding columns.
	if (width >= 0)
		m_minAcceptableColWidth = ibRestoreGridScale(width, scale);
}

void ibGrid::SetRowMinimalAcceptableHeight(int height, float scale)
{
	// We do allow a height of 0 since this gives us
	// an easy way to temporarily hiding rows.
	if (height >= 0)
		m_minAcceptableRowHeight = ibRestoreGridScale(height, scale);
}

int  ibGrid::GetColMinimalAcceptableWidth(float scale) const
{
	return ibCalcGridScale(m_minAcceptableColWidth, scale);
}

int  ibGrid::GetRowMinimalAcceptableHeight(float scale) const
{
	return ibCalcGridScale(m_minAcceptableRowHeight, scale);
}

void ibGrid::SetNativeHeaderColCount()
{
	wxASSERT_MSG(m_useNativeHeader, "no column header window");

	GetGridColHeader()->SetColumnCount(m_numCols);

	SetNativeHeaderColOrder();
}

void ibGrid::SetNativeHeaderColOrder()
{
	wxASSERT_MSG(m_useNativeHeader, "no column header window");

	if (!m_colAt.empty())
		GetGridColHeader()->SetColumnsOrder(m_colAt);
	else
		GetGridColHeader()->ResetColumnsOrder();
}

// ----------------------------------------------------------------------------
// auto sizing
// ----------------------------------------------------------------------------

void
ibGrid::AutoSizeColOrRow(int colOrRow, bool setAsMin, ibGridDirection direction)
{
	const bool column = direction == wxGRID_COLUMN;

	// We don't support auto-sizing hidden rows or columns, this doesn't seem
	// to make much sense.
	if (column)
	{
		if (GetColWidth(colOrRow) == 0)
			return;
	}
	else
	{
		if (GetRowHeight(colOrRow) == 0)
			return;
	}

	wxClientDC dc(m_gridWin);

	AcceptCellEditControlIfShown();

	// initialize both of them just to avoid compiler warnings even if only
	// really needs to be initialized here
	int row,
		col;
	if (column)
	{
		row = -1;
		col = colOrRow;
	}
	else
	{
		row = colOrRow;
		col = -1;
	}

	// If possible, reuse the same attribute and renderer for all cells: this
	// is an important optimization (resulting in up to 80% speed up of
	// AutoSizeColumns()) as finding the attribute and renderer for the cell
	// are very slow operations, due to the number of steps involved in them.
	const bool canReuseAttr = column && m_table->CanMeasureColUsingSameAttr(col);
	ibGridCellAttrPtr attr;
	ibGridCellRendererPtr renderer;

	wxCoord extent, extentMax = 0;
	int max = column ? m_numRows : m_numCols;
	for (int rowOrCol = 0; rowOrCol < max; rowOrCol++)
	{
		if (column)
		{
			if (!IsRowShown(rowOrCol))
				continue;

			row = rowOrCol;
			col = colOrRow;
		}
		else
		{
			if (!IsColShown(rowOrCol))
				continue;

			row = colOrRow;
			col = rowOrCol;
		}

		// we need to account for the cells spanning multiple columns/rows:
		// while they may need a lot of space, they don't need all of it in
		// this column/row
		int numRows, numCols;
		const CellSpan span = GetCellSize(row, col, &numRows, &numCols);
		if (span == CellSpan_Inside)
		{
			// we need to get the size of the main cell, not of a cell hidden
			// by it
			row += numRows;
			col += numCols;

			// get the size of the main cell too
			GetCellSize(row, col, &numRows, &numCols);
		}

		// get cell ( main cell if CellSpan_Inside ) renderer best size
		if (!canReuseAttr || !attr)
		{
			attr = GetCellAttrPtr(row, col);
			renderer = attr->GetRendererPtr(this, row, col);

			if (canReuseAttr)
			{
				// Try to get the best width for the entire column at once, if
				// it's supported by the renderer.
				extent = renderer->GetMaxBestSize(*this, *attr, dc).x;

				if (extent != wxDefaultCoord)
				{
					extentMax = extent;

					// No need to check all the values.
					break;
				}
			}
		}

		if (renderer)
		{
			extent = column
				? renderer->GetBestWidth(*this, *attr, dc, row, col,
					GetRowHeight(row, GetGridZoom()))
				: renderer->GetBestHeight(*this, *attr, dc, row, col,
					GetColWidth(col, GetGridZoom()));

			if (span != CellSpan_None)
			{
				// we spread the size of a spanning cell over all the cells it
				// covers evenly -- this is probably not ideal but we can't
				// really do much better here
				//
				// notice that numCols and numRows are never 0 as they
				// correspond to the size of the main cell of the span and not
				// of the cell inside it
				extent /= column ? numCols : numRows;
			}

			if (extent > extentMax)
				extentMax = extent;
		}
	}

	// now also compare with the column label extent
	wxCoord extentLabel;
	dc.SetFont(GetLabelFont(GetGridZoom()));

	// We add some margin around text for better readability.
	const int margin = FromDIP(column ? 10 : 6);

	if (column)
	{
		if (m_useNativeHeader)
		{
			extentLabel = GetGridColHeader()->GetColumnTitleWidth(colOrRow);

			// Note that GetColumnTitleWidth already adds margins internally,
			// so we don't need to add them here.
		}
		else
		{
			const wxSize
				size = dc.GetMultiLineTextExtent(GetColLabelValue(colOrRow));
			extentLabel = GetColLabelTextOrientation() == wxVERTICAL
				? size.y
				: size.x;

			// Add some margins around text for better readability.
			extentLabel += margin;
		}
	}
	else
	{
		extentLabel = dc.GetMultiLineTextExtent(GetRowLabelValue(colOrRow)).y;

		// As above, add some margins for readability, although a smaller one
		// in vertical direction.
		extentLabel += margin;
	}


	// Finally determine the suitable extent fitting both the cells contents
	// and the label.
	if (!extentMax)
	{
		// Special case: all the cells are empty, use the label extent.
		extentMax = extentLabel;
		if (!extentMax)
		{
			// But if the label is empty too, use the default width/height.
			extentMax = column ? m_defaultColWidth : m_defaultRowHeight;
		}
	}
	else // We have some data in the column cells.
	{
		// Ensure we have the same margin around the cells text as we use
		// around the label.
		extentMax += margin;

		// And increase it to fit the label if necessary.
		if (extentLabel > extentMax)
			extentMax = extentLabel;
	}


	if (column)
	{
		// Ensure automatic width is not less than minimal width. See the
		// comment in SetColSize() for explanation of why this isn't done
		// in SetColSize().
		if (!setAsMin)
			extentMax = wxMax(extentMax, GetColMinimalWidth(colOrRow));

		SetColSize(colOrRow, extentMax);
		if (ShouldRefresh())
		{
			if (m_useNativeHeader)
			{
				GetGridColHeader()->UpdateColumn(colOrRow);
			}
			else
			{
				int cw, ch, dummy;
				m_gridWin->GetClientSize(&cw, &ch);
				wxRect rect(CellToRect(0, colOrRow));
				rect.y = 0;
				CalcScrolledPosition(rect.x, 0, &rect.x, &dummy);
				rect.width = cw - rect.x;
				rect.height = (GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom());
				GetColLabelWindow()->Refresh(true, &rect);
			}
		}
	}
	else
	{
		// Ensure automatic width is not less than minimal height. See the
		// comment in SetColSize() for explanation of why this isn't done
		// in SetRowSize().
		if (!setAsMin)
			extentMax = wxMax(extentMax, GetRowMinimalHeight(colOrRow));

		SetRowSize(colOrRow, extentMax);
		if (ShouldRefresh())
		{
			int cw, ch, dummy;
			m_gridWin->GetClientSize(&cw, &ch);
			wxRect rect(CellToRect(colOrRow, 0));
			rect.x = 0;
			CalcScrolledPosition(0, rect.y, &dummy, &rect.y);
			rect.width = ibCalcGridScale(m_rowLabelWidth, GetGridZoom());
			rect.height = ch - rect.y;

			m_rowAreaWin->Refresh(true, &rect);
			m_rowLabelWin->Refresh(true, &rect);
		}
	}

	if (setAsMin)
	{
		if (column)
			SetColMinimalWidth(colOrRow, extentMax);
		else
			SetRowMinimalHeight(colOrRow, extentMax);
	}
}

wxCoord ibGrid::CalcColOrRowLabelAreaMinSize(ibGridDirection direction)
{
	// calculate size for the rows or columns?
	const bool calcRows = direction == wxGRID_ROW;

	wxClientDC dc(calcRows ? GetGridRowLabelWindow()
		: GetGridColLabelWindow());
	dc.SetFont(GetLabelFont());

	// which dimension should we take into account for calculations?
	//
	// for columns, the text can be only horizontal so it's easy but for rows
	// we also have to take into account the text orientation
	const bool
		useWidth = calcRows || (GetColLabelTextOrientation() == wxVERTICAL);

	wxArrayString lines;
	wxCoord extentMax = 0;

	const int numRowsOrCols = calcRows ? m_numRows : m_numCols;
	for (int rowOrCol = 0; rowOrCol < numRowsOrCols; rowOrCol++)
	{
		lines.Clear();

		wxString label = calcRows ? GetRowLabelValue(rowOrCol)
			: GetColLabelValue(rowOrCol);
		ParseLines(label, lines);

		long w, h;
		GetTextBoxSize(dc, lines, &w, &h);

		const wxCoord extent = useWidth ? w : h;
		if (extent > extentMax)
			extentMax = extent;
	}

	if (!extentMax)
	{
		// empty column - give default extent (notice that if extentMax is less
		// than default extent but != 0, it's OK)
		extentMax = calcRows ? GetDefaultRowLabelSize()
			: GetDefaultColLabelSize();
	}

	// leave some space around text (taken from AutoSizeColOrRow)
	if (calcRows)
		extentMax += 10;
	else
		extentMax += 6;

	return extentMax;
}

void ibGrid::AutoSizeColumns(bool setAsMin)
{
	ibGridUpdateLocker locker(this);

	for (int col = 0; col < m_numCols; col++)
		AutoSizeColumn(col, setAsMin);
}

void ibGrid::AutoSizeRows(bool setAsMin)
{
	ibGridUpdateLocker locker(this);

	for (int row = 0; row < m_numRows; row++)
		AutoSizeRow(row, setAsMin);
}

void ibGrid::AutoSize()
{
	ibGridUpdateLocker locker(this);

	AutoSizeColumns();
	AutoSizeRows();

	// we know that we're not going to have scrollbars so disable them now to
	// avoid trouble in SetClientSize() which can otherwise set the correct
	// client size but also leave space for (not needed any more) scrollbars
	SetScrollbars(m_xScrollPixelsPerLine, m_yScrollPixelsPerLine,
		0, 0, 0, 0, true);

	SetSize(DoGetBestSize());
}

void ibGrid::AutoSizeRowLabelSize(int row)
{
	// Hide the edit control, so it
	// won't interfere with drag-shrinking.
	AcceptCellEditControlIfShown();

	// autosize row height depending on label text
	SetRowSize(row, -1);

	ForceRefresh();
}

void ibGrid::AutoSizeColLabelSize(int col)
{
	// Hide the edit control, so it
	// won't interfere with drag-shrinking.
	AcceptCellEditControlIfShown();

	// autosize column width depending on label text
	SetColSize(col, -1);

	ForceRefresh();
}

wxSize ibGrid::DoGetBestSize() const
{
	wxSize size((GridRowAreaEnabled() ? ibCalcGridScale(m_rowAreaWidth, GetGridZoom()) : 0) + ibCalcGridScale(m_rowLabelWidth, GetGridZoom()) + m_extraWidth,
		(GridColAreaEnabled() ? ibCalcGridScale(m_colAreaHeight, GetGridZoom()) : 0) + ibCalcGridScale(m_colLabelHeight, GetGridZoom()) + m_extraHeight);

	if (m_colWidths.empty())
	{
		size.x += ibCalcGridScale(m_defaultColWidth, GetGridZoom()) * m_numRows * m_numCols;
	}
	else
	{
		for (int col = 0; col < m_numCols; col++)
			size.x += GetColWidth(col, GetGridZoom());
	}

	if (m_rowHeights.empty())
	{
		size.y += ibCalcGridScale(m_defaultRowHeight, GetGridZoom()) * m_numRows;
	}
	else
	{
		for (int row = 0; row < m_numRows; row++)
			size.y += GetRowHeight(row, GetGridZoom());
	}

	return size + GetWindowBorderSize();
}

void ibGrid::Fit()
{
	AutoSize();
}

void ibGrid::SetFocus()
{
	m_gridWin->SetFocus();
}

const wxArrayInt& ibGrid::GetRowBottoms(float scale) const
{
	m_rowBottoms.Alloc(m_rowHeights.Count());
	m_rowBottoms.SetCount(0);

	int total_height = 0;
	for (const int& height : m_rowHeights) {
		const int scaled_height = ibCalcGridScale(height, scale);
		if (height > 0)
		{
			total_height += scaled_height;
			m_rowBottoms.Add(total_height);
		}
		else
		{
			//total_height += scaled_height;
			m_rowBottoms.Add(total_height);
		}
	}

	return m_rowBottoms;
}

const wxArrayInt& ibGrid::GetColRights(float scale) const
{
	m_colRights.Alloc(m_colWidths.Count());
	m_colRights.SetCount(0);

	int total_width = 0;
	for (const int& width : m_colWidths) {
		const int scaled_width = ibCalcGridScale(width, scale);
		if (width > 0)
		{
			total_width += scaled_width;
			m_colRights.Add(total_width);
		}
		else
		{
			//total_width += scaled_width;
			m_colRights.Add(total_width);
		}
	}

	return m_colRights;
}

bool ibGrid::Undo()
{
	if (!m_undoStack.empty()) {
		wxSharedPtr<ibGridCommand> command = m_undoStack.back();
		m_undoStack.pop_back();
		command->Restore(this);
		m_redoStack.push_back(command);
	}

	ForceRefresh();
	return true;
}

bool ibGrid::Redo()
{
	if (!m_redoStack.empty()) {
		wxSharedPtr<ibGridCommand> command = m_redoStack.back();
		m_redoStack.pop_back();
		command->Execute(this);
		m_undoStack.push_back(command);
	}
	
	ForceRefresh();
	return true;
}

bool ibGrid::CanUndo() const
{
	return (!m_undoStack.empty());
}

bool ibGrid::CanRedo() const
{
	return (!m_redoStack.empty());
}

// ----------------------------------------------------------------------------
// cell value accessor functions
// ----------------------------------------------------------------------------

void ibGrid::SetCellValue(int row, int col, const wxString& newValue, bool sendUndoCommand)
{
	const wxString& oldValue = GetCellValue(row, col);

	if (oldValue == newValue)
	{
		// Avoid flicker by not doing anything in this case.
		return;
	}

	if (m_table)
	{
		if (sendUndoCommand)
			PushCommand<ibGridCommandCellValue>(row, col, newValue, oldValue);

		m_table->SetValue(row, col, newValue);

		if (ShouldRefresh())
		{
			int dummy;
			wxRect rect(CellToRect(row, col));
			rect.x = 0;
			rect.width = m_gridWin->GetClientSize().GetWidth();
			CalcScrolledPosition(0, rect.y, &dummy, &rect.y);
			m_gridWin->Refresh(false, &rect);
		}

		if (m_currentCellCoords.GetRow() == row &&
			m_currentCellCoords.GetCol() == col &&
			IsCellEditControlShown())
			// Note: If we are using IsCellEditControlEnabled,
			// this interacts badly with calling SetCellValue from
			// an EVT_GRID_CELL_CHANGE handler.
		{
			HideCellEditControl();
			ShowCellEditControl(); // will reread data from table
		}

		// set new brake pos
		SetRowBrake(row);
		SetColBrake(col);
	}
}

void ibGrid::SetCellValue(const ibGridBlockCoords& coords, const wxString& newValue, bool sendUndoCommand)
{
	if (sendUndoCommand)
		PushCommand<ibGridCommandCompositeCell<ibGridCommandCellValue>>(this, coords, newValue);

	for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++)
	{
		for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++)
		{
			const wxString& oldValue = GetCellValue(row, col);
			if (oldValue == newValue)
			{
				// Avoid flicker by not doing anything in this case.
				continue;
			}

			if (m_table)
			{
				m_table->SetValue(row, col, newValue);

				if (ShouldRefresh())
				{
					int dummy;
					wxRect rect(CellToRect(row, col));
					rect.x = 0;
					rect.width = m_gridWin->GetClientSize().GetWidth();
					CalcScrolledPosition(0, rect.y, &dummy, &rect.y);
					m_gridWin->Refresh(false, &rect);
				}

				if (m_currentCellCoords.GetRow() == row &&
					m_currentCellCoords.GetCol() == col &&
					IsCellEditControlShown())
					// Note: If we are using IsCellEditControlEnabled,
					// this interacts badly with calling SetCellValue from
					// an EVT_GRID_CELL_CHANGE handler.
				{
					HideCellEditControl();
					ShowCellEditControl(); // will reread data from table
				}
			}
		}
	}

	// set new brake pos
	SetRowBrake(coords.GetBottomRow());
	SetColBrake(coords.GetRightCol());
}

// ----------------------------------------------------------------------------
// block, row and column selection
// ----------------------------------------------------------------------------

void ibGrid::SelectRow(int row, bool addToSelected)
{
	if (!m_selection)
		return;

	if (!addToSelected)
		ClearSelection();

	m_selection->SelectRow(row);
}

void ibGrid::SelectCol(int col, bool addToSelected)
{
	if (!m_selection)
		return;

	if (!addToSelected)
		ClearSelection();

	m_selection->SelectCol(col);
}

void ibGrid::SelectBlock(int topRow, int leftCol, int bottomRow, int rightCol,
	bool addToSelected)
{
	if (!m_selection)
		return;

	if (!addToSelected)
		ClearSelection();

	m_selection->SelectBlock(topRow, leftCol, bottomRow, rightCol);
}

void ibGrid::SelectAll()
{
	if (m_selection)
		m_selection->SelectAll();
}

// ----------------------------------------------------------------------------
// cell, row and col deselection
// ----------------------------------------------------------------------------

void ibGrid::DeselectRow(int row)
{
	wxCHECK_RET(row >= 0 && row < m_numRows, wxT("invalid row index"));

	if (m_selection)
		m_selection->DeselectBlock(ibGridBlockCoords(row, 0, row, m_numCols - 1));
}

void ibGrid::DeselectCol(int col)
{
	wxCHECK_RET(col >= 0 && col < m_numCols, wxT("invalid column index"));

	if (m_selection)
		m_selection->DeselectBlock(ibGridBlockCoords(0, col, m_numRows - 1, col));
}

void ibGrid::DeselectCell(int row, int col)
{
	wxCHECK_RET(row >= 0 && row < m_numRows &&
		col >= 0 && col < m_numCols,
		wxT("invalid cell coords"));

	if (m_selection)
		m_selection->DeselectBlock(ibGridBlockCoords(row, col, row, col));
}

bool ibGrid::IsSelection() const
{
	return m_selection && m_selection->IsSelection();
}

bool ibGrid::IsInSelection(int row, int col) const
{
	return m_selection && m_selection->IsInSelection(row, col);
}

ibGridBlocks ibGrid::GetSelectedBlocks() const
{
	if (!m_selection)
		return ibGridBlocks();

	const wxVectorGridBlockCoords& blocks = m_selection->GetBlocks();
	return ibGridBlocks(blocks.begin(), blocks.end());
}

static
ibGridBlockCoordsVector
DoGetRowOrColBlocks(ibGridBlocks blocks, const ibGridOperations& oper)
{
	ibGridBlockCoordsVector res;

	for (ibGridBlocks::iterator it = blocks.begin(); it != blocks.end(); ++it)
	{
		const int firstNew = oper.SelectFirst(*it);
		const int lastNew = oper.SelectLast(*it);

		// Check if this block intersects any of the existing ones.
		//
		// We use simple linear search because we assume there are only a few
		// blocks in all, and it's not worth complicating the code to use
		// anything more advanced, but this definitely could be improved to use
		// the fact that the vector is always sorted.
		for (size_t n = 0;; )
		{
			if (n == res.size())
			{
				// We didn't find any overlapping blocks, so add this one to
				// the end.
				res.push_back(*it);
				break;
			}

			ibGridBlockCoords& block = res[n];
			const int firstThis = oper.SelectFirst(block);
			const int lastThis = oper.SelectLast(block);

			if (lastNew < firstThis)
			{
				// Not only it doesn't overlap this block, but it won't overlap
				// any subsequent ones either, so insert it here and stop.
				res.insert(res.begin() + n, *it);
				break;
			}

			if (lastThis < firstNew)
			{
				// It doesn't overlap this one, but continue checking.
				n++;
				continue;
			}

			// The blocks overlap, combine them by adjusting the bounds of the
			// current block.

			// The first bound is simple as we know that firstNew must be
			// strictly greater than the last coordinate of all the previous
			// elements, otherwise we would have combined it with them earlier.
			if (firstNew < firstThis)
				oper.SetFirst(block, firstNew);

			// But for the last one, we need to find the last element it
			// overlaps (which may be this block itself). We call its index n2
			// to avoid confusion with "last" used for the block component.
			size_t n2 = n;
			for (;; )
			{
				const ibGridBlockCoords& block2 = res[n2];
				if (lastNew < oper.SelectFirst(block2))
				{
					oper.SetLast(block, lastNew);
					break;
				}

				// Do it here as we'll need to remove the current block if it's
				// the last overlapping one and we break just below.
				n2++;

				if (lastNew < oper.SelectLast(block2))
				{
					oper.SetLast(block, oper.SelectLast(block2));
					break;
				}

				if (n2 == res.size())
				{
					oper.SetLast(block, lastNew);
					break;
				}
			}

			if (n2 > n + 1)
				res.erase(res.begin() + n + 1, res.begin() + n2);

			break;
		}
	}

	// This is another inefficiency: it would be also possible to do everything
	// in one pass, combining the adjacent ranges in the loop above. But this
	// is more complicated and doesn't seem to be worth it, for the arrays of
	// small sizes that we work with here, so do an extra path combining the
	// adjacent ranges.
	for (size_t n = 0;; )
	{
		if (n + 1 >= res.size())
			break;

		if (oper.SelectFirst(res[n + 1]) == oper.SelectLast(res[n]) + 1)
		{
			// The ranges touch, combine them.
			oper.SetLast(res[n], oper.SelectLast(res[n + 1]));

			// And erase the subsumed range.
			res.erase(res.begin() + n + 1, res.begin() + n + 2);
		}
		else // Just go to the next one.
		{
			n++;
		}
	}

	return res;
}

ibGridBlockCoordsVector ibGrid::GetSelectedRowBlocks() const
{
	if (!m_selection || m_selection->GetSelectionMode() != ibGridSelectRows)
		return ibGridBlockCoordsVector();

	return DoGetRowOrColBlocks(GetSelectedBlocks(), ibGridRowOperations());
}

ibGridBlockCoordsVector ibGrid::GetSelectedColBlocks() const
{
	if (!m_selection || m_selection->GetSelectionMode() != ibGridSelectColumns)
		return ibGridBlockCoordsVector();

	return DoGetRowOrColBlocks(GetSelectedBlocks(), ibGridColumnOperations());
}

ibGridBlockCoords ibGrid::GetSelectedCellRange() const
{
	int row1 = m_numRows, col1 = m_numCols,
		row2 = 0, col2 = 0; bool hasBlocks = false;

	for (const auto& coords : ibGrid::GetSelectedBlocks()) {
		if (row1 > coords.GetTopRow()) row1 = coords.GetTopRow();
		if (col1 > coords.GetLeftCol()) col1 = coords.GetLeftCol();
		if (row2 < coords.GetBottomRow()) row2 = coords.GetBottomRow();
		if (col2 < coords.GetRightCol()) col2 = coords.GetRightCol();
		hasBlocks = true;
	}

	if (!hasBlocks) {

		if (row1 > ibGrid::GetGridCursorRow() && ibGrid::GetGridCursorRow() >= 0) row1 = ibGrid::GetGridCursorRow(); else row1 = 0;
		if (col1 > ibGrid::GetGridCursorCol() && ibGrid::GetGridCursorCol() >= 0) col1 = ibGrid::GetGridCursorCol(); else col1 = 0;
		if (row2 < ibGrid::GetGridCursorRow()) row2 = ibGrid::GetGridCursorRow();
		if (col2 < ibGrid::GetGridCursorCol()) col2 = ibGrid::GetGridCursorCol();
	}

	return ibGridBlockCoords(row1, col1, row2, col2);
}

ibGridCellCoordsArray ibGrid::GetSelectedCells() const
{
	if (!m_selection)
	{
		ibGridCellCoordsArray a;
		return a;
	}

	return m_selection->GetCellSelection();
}

ibGridCellCoordsArray ibGrid::GetSelectionBlockTopLeft() const
{
	if (!m_selection)
	{
		ibGridCellCoordsArray a;
		return a;
	}

	return m_selection->GetBlockSelectionTopLeft();
}

ibGridCellCoordsArray ibGrid::GetSelectionBlockBottomRight() const
{
	if (!m_selection)
	{
		ibGridCellCoordsArray a;
		return a;
	}

	return m_selection->GetBlockSelectionBottomRight();
}

wxArrayInt ibGrid::GetSelectedRows() const
{
	if (!m_selection)
	{
		wxArrayInt a;
		return a;
	}

	return m_selection->GetRowSelection();
}

wxArrayInt ibGrid::GetSelectedCols() const
{
	if (!m_selection)
	{
		wxArrayInt a;
		return a;
	}

	return m_selection->GetColSelection();
}

void ibGrid::ClearSelection()
{
	if (m_selection)
		m_selection->ClearSelection();
}

// This function returns the rectangle that encloses the given block
// in device coords clipped to the client size of the grid window.
//
wxRect ibGrid::BlockToDeviceRect(const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight,
	const ibGridWindow* gridWindow) const
{
	wxRect resultRect;
	wxRect tempCellRect = CellToRect(topLeft);
	if (tempCellRect != ibGridNoCellRect)
	{
		resultRect = tempCellRect;
	}
	else
	{
		resultRect = wxRect(0, 0, 0, 0);
	}

	tempCellRect = CellToRect(bottomRight);
	if (tempCellRect != ibGridNoCellRect)
	{
		resultRect += tempCellRect;
	}
	else
	{
		// If both inputs were "ibGridNoCellRect," then there's nothing to do.
		return ibGridNoCellRect;
	}

	// Ensure that left/right and top/bottom pairs are in order.
	int left = resultRect.GetLeft();
	int top = resultRect.GetTop();
	int right = resultRect.GetRight();
	int bottom = resultRect.GetBottom();

	int leftCol = topLeft.GetCol();
	int topRow = topLeft.GetRow();
	int rightCol = bottomRight.GetCol();
	int bottomRow = bottomRight.GetRow();

	if (left > right)
	{
		int tmp = left;
		left = right;
		right = tmp;

		tmp = leftCol;
		leftCol = rightCol;
		rightCol = tmp;
	}

	if (top > bottom)
	{
		int tmp = top;
		top = bottom;
		bottom = tmp;

		tmp = topRow;
		topRow = bottomRow;
		bottomRow = tmp;
	}

	if (!gridWindow)
		gridWindow = m_gridWin;

	int cw, ch;
	gridWindow->GetClientSize(&cw, &ch);
	wxPoint offset = GetGridWindowOffset(gridWindow);

	// The following loop is ONLY necessary to detect and handle merged cells.
	if (gridWindow == m_gridWin)
	{
		// Get the origin coordinates: notice that they will be negative if the
		// grid is scrolled downwards/to the right.
		int gridOriginX = offset.x;
		int gridOriginY = offset.y;
		CalcScrolledPosition(gridOriginX, gridOriginY, &gridOriginX, &gridOriginY);

		int onScreenLeftmostCol = internalXToCol(-gridOriginX, m_gridWin);
		int onScreenUppermostRow = internalYToRow(-gridOriginY, m_gridWin);

		int onScreenRightmostCol = internalXToCol(-gridOriginX + cw, m_gridWin);
		int onScreenBottommostRow = internalYToRow(-gridOriginY + ch, m_gridWin);

		// Bound our loop so that we only examine the portion of the selected block
		// that is shown on screen. Therefore, we compare the Top-Left block values
		// to the Top-Left screen values, and the Bottom-Right block values to the
		// Bottom-Right screen values, choosing appropriately.
		const int visibleTopRow = wxMax(topRow, onScreenUppermostRow);
		const int visibleBottomRow = wxMin(bottomRow, onScreenBottommostRow);
		const int visibleLeftCol = wxMax(leftCol, onScreenLeftmostCol);
		const int visibleRightCol = wxMin(rightCol, onScreenRightmostCol);

		for (int j = visibleTopRow; j <= visibleBottomRow; j++)
		{
			for (int i = visibleLeftCol; i <= visibleRightCol; i++)
			{
				if ((j == visibleTopRow) || (j == visibleBottomRow) ||
					(i == visibleLeftCol) || (i == visibleRightCol))
				{
					tempCellRect = CellToRect(j, i);

					if (tempCellRect.x < left)
						left = tempCellRect.x;
					if (tempCellRect.y < top)
						top = tempCellRect.y;
					if (tempCellRect.x + tempCellRect.width > right)
						right = tempCellRect.x + tempCellRect.width;
					if (tempCellRect.y + tempCellRect.height > bottom)
						bottom = tempCellRect.y + tempCellRect.height;
				}
				else
				{
					i = visibleRightCol; // jump over inner cells.
				}
			}
		}
	}

	// Convert to scrolled coords
	CalcGridWindowScrolledPosition(left - offset.x, top - offset.y, &left, &top, gridWindow);
	CalcGridWindowScrolledPosition(right - offset.x, bottom - offset.y, &right, &bottom, gridWindow);

	if (right < 0 || bottom < 0 || left > cw || top > ch)
		return wxRect(0, 0, 0, 0);

	resultRect.SetLeft(wxMax(0, left));
	resultRect.SetTop(wxMax(0, top));
	resultRect.SetRight(wxMin(cw, right));
	resultRect.SetBottom(wxMin(ch, bottom));

	return resultRect;
}

void ibGrid::DoSetSizes(const ibGridSizesInfo& sizeInfo,
	const ibGridOperations& oper)
{
	BeginBatch();
	oper.SetDefaultLineSize(this, sizeInfo.m_sizeDefault, true);
	const int numLines = oper.GetNumberOfLines(this, NULL);
	for (int i = 0; i < numLines; i++)
	{
		int size = sizeInfo.GetSize(i);
		if (size != sizeInfo.m_sizeDefault)
			oper.SetLineSize(this, i, size);
	}
	EndBatch();
}

void ibGrid::SetColSizes(const ibGridSizesInfo& sizeInfo)
{
	DoSetSizes(sizeInfo, ibGridColumnOperations());
}

void ibGrid::SetRowSizes(const ibGridSizesInfo& sizeInfo)
{
	DoSetSizes(sizeInfo, ibGridRowOperations());
}

ibGridSizesInfo::ibGridSizesInfo(int defSize, const wxArrayInt& allSizes)
{
	m_sizeDefault = defSize;
	for (size_t i = 0; i < allSizes.size(); i++)
	{
		if (allSizes[i] != defSize)
			m_customSizes[i] = allSizes[i];
	}
}

int ibGridSizesInfo::GetSize(unsigned pos) const
{
	wxUnsignedToIntHashMap::const_iterator it = m_customSizes.find(pos);

	// if it's not found return the default
	if (it == m_customSizes.end())
		return m_sizeDefault;

	// otherwise return 0 if it's hidden, currently there is no way to get
	// its size before it had been hidden
	if (it->second < 0)
		return 0;

	return it->second;
}

void ibGrid::DoRowAreaCreate(int start, int end)
{
	for (unsigned int idx = 0; idx < m_rowAreaAt.size(); idx++) {

		ibGridCellArea& item = m_rowAreaAt[idx];

		if (start >= item.m_start && start <= item.m_end) {
			if (end > item.m_end) { //expand the bottom border

				PushCommand<ibGridCommandAreaSize>(idx,
					ibGridCommandAreaSize::AreaRow, item.m_end - end, ibGridCommandAreaSize::FromEndArea);

				item.m_end = end;

				SendGridAreaEvent(wxEVT_GRID_ROW_AREA_SIZE, idx, item);

				//support printing
				SetRowBrake(end);
				return;
			}
		}
		else if (end >= item.m_start && end <= item.m_end) {

			if (start < item.m_start) { //extending the upper bound

				PushCommand<ibGridCommandAreaSize>(idx,
					ibGridCommandAreaSize::AreaRow, item.m_start - start, ibGridCommandAreaSize::FromStartArea);

				item.m_start = start;

				SendGridAreaEvent(wxEVT_GRID_ROW_AREA_SIZE, idx, item);
				return;
			}
		}

		if (start >= item.m_start && start <= item.m_end)
			return;

		if (end >= item.m_start && end <= item.m_end)
			return;
	}

	unsigned int countRec = 1;

	//adding a new section
	ibGridCellArea entry;

	entry.m_start = start;
	entry.m_end = end;
	entry.m_areaLabel = wxString::Format(wxT("%s%d"), wxT("Area"), countRec);

	while (countRec > 0)
	{

		bool foundedName = false;

		for (unsigned int idx = 0; idx < m_rowAreaAt.Count(); idx++)
		{
			if (entry.m_areaLabel == m_rowAreaAt[idx].m_areaLabel)
			{
				foundedName = true;
				break;
			}
		}

		if (!foundedName)
			break;

		entry.m_areaLabel = wxString::Format(wxT("%s%d"), wxT("Area"), ++countRec);
	}

	if (MakeRowAreaLabel(&entry))
	{
		PushCommand<ibGridCommandArea>(m_rowAreaAt.Count(),
			ibGridCommandArea::AreaRow, ibGridCommandArea::AddArea, entry);

		AddRowArea(entry);

		SendGridAreaEvent(wxEVT_GRID_ROW_AREA_CREATE, m_rowAreaAt.Count() - 1, entry);
	}
}

void ibGrid::DoColAreaCreate(int start, int end)
{
	for (unsigned int idx = 0; idx < m_colAreaAt.size(); idx++) {

		ibGridCellArea& item = m_colAreaAt[idx];

		if (start >= item.m_start && start <= item.m_end) {
			if (end > item.m_end) {//expand the bottom border

				PushCommand<ibGridCommandAreaSize>(idx,
					ibGridCommandAreaSize::AreaCol, item.m_end - end, ibGridCommandAreaSize::FromEndArea);

				item.m_end = end;

				SendGridAreaEvent(wxEVT_GRID_COL_AREA_SIZE, idx, item);

				//support printing
				SetColBrake(end);
				return;
			}
		}
		else if (end >= item.m_start && end <= item.m_end) {

			if (start < item.m_start) { //extending the upper bound

				PushCommand<ibGridCommandAreaSize>(idx,
					ibGridCommandAreaSize::AreaCol, item.m_start - start, ibGridCommandAreaSize::FromStartArea);

				item.m_start = start;

				SendGridAreaEvent(wxEVT_GRID_COL_AREA_SIZE, idx, item);
				return;
			}
		}

		if (start >= item.m_start && start <= item.m_end)
			return;

		if (end >= item.m_start && end <= item.m_end)
			return;
	}

	unsigned int countRec = 1;

	//adding a new section
	ibGridCellArea entry;

	entry.m_start = start;
	entry.m_end = end;
	entry.m_areaLabel = wxString::Format(wxT("%s%d"), wxT("Area"), countRec);

	while (countRec > 0) {

		bool foundedName = false;

		for (unsigned int idx = 0; idx < m_colAreaAt.Count(); idx++)
		{
			if (entry.m_areaLabel == m_colAreaAt[idx].m_areaLabel)
			{
				foundedName = true;
				break;
			}
		}

		if (!foundedName)
			break;

		entry.m_areaLabel = wxString::Format(wxT("%s%d"), wxT("Area"), ++countRec);
	}

	if (MakeColAreaLabel(&entry))
	{
		PushCommand<ibGridCommandArea>(m_colAreaAt.Count(),
			ibGridCommandArea::AreaCol, ibGridCommandArea::AddArea, entry);

		AddColArea(entry);

		SendGridAreaEvent(wxEVT_GRID_COL_AREA_CREATE, m_colAreaAt.Count() - 1, entry);
	}
}

void ibGrid::DoRowAreaDelete(int start, int end)
{
	for (unsigned int idx = 0; idx < m_rowAreaAt.size(); idx++)
	{
		ibGridCellArea& item = m_rowAreaAt[idx];

		if (start == item.m_start && end == item.m_end) { //deleting a section

			PushCommand<ibGridCommandArea>(idx,
				ibGridCommandArea::AreaRow, ibGridCommandArea::DeleteArea, item);

			SendGridAreaEvent(wxEVT_GRID_ROW_AREA_DELETE, idx, item);

			m_rowAreaAt.Detach(idx);
		}
		else if (start >= item.m_start && start <= item.m_end) {
			if (end >= item.m_end) { //removing the bottom border		
				item.m_end = start - 1;
			}
		}
		else if (end >= item.m_start && end <= item.m_end) {
			if (start <= item.m_start) { //removing the top border
				item.m_start = end + 1;
			}
		}
	}

	CalcDimensions();
}

void ibGrid::DoColAreaDelete(int start, int end)
{
	for (unsigned int idx = 0; idx < m_colAreaAt.size(); idx++)
	{
		ibGridCellArea& item = m_colAreaAt[idx];

		if (start == item.m_start && end == item.m_end) { //deleting a section

			PushCommand<ibGridCommandArea>(idx,
				ibGridCommandArea::AreaCol, ibGridCommandArea::DeleteArea, item);

			SendGridAreaEvent(wxEVT_GRID_COL_AREA_DELETE, idx, item);

			m_colAreaAt.Detach(idx);
		}
		else if (start >= item.m_start && start <= item.m_end) {
			if (end >= item.m_end) { //removing the bottom border		
				item.m_end = start - 1;
			}
		}
		else if (end >= item.m_start && end <= item.m_end) {
			if (start <= item.m_start) { //removing the top border
				item.m_start = end + 1;
			}
		}
	}

	CalcDimensions();
}

// ----------------------------------------------------------------------------
// drop target
// ----------------------------------------------------------------------------

#if wxUSE_DRAG_AND_DROP

// this allow setting drop target directly on ibGrid
void ibGrid::SetDropTarget(wxDropTarget* dropTarget)
{
	GetGridWindow()->SetDropTarget(dropTarget);
}

#endif // wxUSE_DRAG_AND_DROP

// ----------------------------------------------------------------------------
// grid event classes
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibGridEvent, wxNotifyEvent);

ibGridEvent::ibGridEvent(int id, wxEventType type, wxObject* obj,
	int row, int col, int x, int y, bool sel,
	bool control, bool shift, bool alt, bool meta)
	: wxNotifyEvent(type, id),
	wxKeyboardState(control, shift, alt, meta)
{
	Init(row, col, x, y, sel);

	SetEventObject(obj);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibGridSizeEvent, wxNotifyEvent);

ibGridSizeEvent::ibGridSizeEvent(int id, wxEventType type, wxObject* obj,
	int rowOrCol, int x, int y,
	bool control, bool shift, bool alt, bool meta)
	: wxNotifyEvent(type, id),
	wxKeyboardState(control, shift, alt, meta)
{
	Init(rowOrCol, x, y);

	SetEventObject(obj);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibGridRangeSelectEvent, wxNotifyEvent);

ibGridRangeSelectEvent::ibGridRangeSelectEvent(int id, wxEventType type, wxObject* obj,
	const ibGridCellCoords& topLeft,
	const ibGridCellCoords& bottomRight,
	bool sel, bool control,
	bool shift, bool alt, bool meta)
	: wxNotifyEvent(type, id),
	wxKeyboardState(control, shift, alt, meta)
{
	Init(topLeft, bottomRight, sel);

	SetEventObject(obj);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibGridEditorCreatedEvent, wxCommandEvent);

ibGridEditorCreatedEvent::ibGridEditorCreatedEvent(int id, wxEventType type,
	wxObject* obj, int row,
	int col, wxWindow* window)
	: wxCommandEvent(type, id)
{
	SetEventObject(obj);
	m_row = row;
	m_col = col;
	m_window = window;
}

wxIMPLEMENT_DYNAMIC_CLASS(ibGridAreaEvent, wxNotifyEvent);

ibGridAreaEvent::ibGridAreaEvent(int id, wxEventType type,
	wxObject* obj, int rowOrColPos, const ibGridCellArea& area)
	: wxNotifyEvent(type, id)
{
	SetEventObject(obj);

	m_rowOrColPos = rowOrColPos;
	m_area = area;
}

// ----------------------------------------------------------------------------
// ibGridTypeRegistry
// ----------------------------------------------------------------------------

ibGridTypeRegistry::~ibGridTypeRegistry()
{
	size_t count = m_typeinfo.GetCount();
	for (size_t i = 0; i < count; i++)
		delete m_typeinfo[i];
}

void ibGridTypeRegistry::RegisterDataType(const wxString& typeName,
	ibGridCellRenderer* renderer,
	ibGridCellEditor* editor)
{
	ibGridDataTypeInfo* info = new ibGridDataTypeInfo(typeName, renderer, editor);

	// is it already registered?
	int loc = FindRegisteredDataType(typeName);
	if (loc != wxNOT_FOUND)
	{
		delete m_typeinfo[loc];
		m_typeinfo[loc] = info;
	}
	else
	{
		m_typeinfo.Add(info);
	}
}

int ibGridTypeRegistry::FindRegisteredDataType(const wxString& typeName)
{
	size_t count = m_typeinfo.GetCount();
	for (size_t i = 0; i < count; i++)
	{
		if (typeName == m_typeinfo[i]->m_typeName)
		{
			return i;
		}
	}

	return wxNOT_FOUND;
}

int ibGridTypeRegistry::FindDataType(const wxString& typeName)
{
	int index = FindRegisteredDataType(typeName);
	if (index == wxNOT_FOUND)
	{
		// check whether this is one of the standard ones, in which case
		// register it "on the fly"
#if wxUSE_TEXTCTRL
		if (typeName == wxGRID_VALUE_STRING)
		{
			RegisterDataType(wxGRID_VALUE_STRING,
				new ibGridCellStringRenderer,
				new ibGridCellTextEditor);
		}
		else
#endif // wxUSE_TEXTCTRL
#if wxUSE_CHECKBOX
			if (typeName == wxGRID_VALUE_BOOL)
			{
				RegisterDataType(wxGRID_VALUE_BOOL,
					new ibGridCellBoolRenderer,
					new ibGridCellBoolEditor);
			}
			else
#endif // wxUSE_CHECKBOX
#if wxUSE_TEXTCTRL
				if (typeName == wxGRID_VALUE_NUMBER)
				{
					RegisterDataType(wxGRID_VALUE_NUMBER,
						new ibGridCellNumberRenderer,
						new ibGridCellNumberEditor);
				}
				else if (typeName == wxGRID_VALUE_FLOAT)
				{
					RegisterDataType(wxGRID_VALUE_FLOAT,
						new ibGridCellFloatRenderer,
						new ibGridCellFloatEditor);
				}
				else
#endif // wxUSE_TEXTCTRL
#if wxUSE_COMBOBOX
					if (typeName == wxGRID_VALUE_CHOICE)
					{
						RegisterDataType(wxGRID_VALUE_CHOICE,
							new ibGridCellChoiceRenderer,
							new ibGridCellChoiceEditor);
					}
					else
#endif // wxUSE_COMBOBOX
#if wxUSE_DATEPICKCTRL
						if (typeName == wxGRID_VALUE_DATE)
						{
							RegisterDataType(wxGRID_VALUE_DATE,
								new ibGridCellDateRenderer,
								new ibGridCellDateEditor);
						}
						else
#endif // wxUSE_DATEPICKCTRL
						{
							return wxNOT_FOUND;
						}

		// we get here only if just added the entry for this type, so return
		// the last index
		index = m_typeinfo.GetCount() - 1;
	}

	return index;
}

int ibGridTypeRegistry::FindOrCloneDataType(const wxString& typeName)
{
	int index = FindDataType(typeName);
	if (index == wxNOT_FOUND)
	{
		// the first part of the typename is the "real" type, anything after ':'
		// are the parameters for the renderer
		index = FindDataType(typeName.BeforeFirst(wxT(':')));
		if (index == wxNOT_FOUND)
		{
			return wxNOT_FOUND;
		}

		ibGridCellRenderer* const
			renderer = ibGridCellRendererPtr(GetRenderer(index))->Clone();

		ibGridCellEditor* const
			editor = ibGridCellEditorPtr(GetEditor(index))->Clone();

		// do it even if there are no parameters to reset them to defaults
		wxString params = typeName.AfterFirst(wxT(':'));
		renderer->SetParameters(params);
		editor->SetParameters(params);

		// register the new typename
		RegisterDataType(typeName, renderer, editor);

		// we just registered it, it's the last one
		index = m_typeinfo.GetCount() - 1;
	}

	return index;
}

ibGridCellRenderer* ibGridTypeRegistry::GetRenderer(int index)
{
	ibGridCellRenderer* renderer = m_typeinfo[index]->m_renderer;
	if (renderer)
		renderer->IncRef();

	return renderer;
}

ibGridCellEditor* ibGridTypeRegistry::GetEditor(int index)
{
	ibGridCellEditor* editor = m_typeinfo[index]->m_editor;
	if (editor)
		editor->IncRef();

	return editor;
}

wxRect
wxGetContentRect(wxSize contentSize,
	const wxRect& cellRect,
	int hAlign,
	int vAlign)
{
	// Keep square aspect ratio for the checkbox, but ensure that it fits into
	// the available space, even if it's smaller than the standard size.
	const wxCoord minSize = wxMin(cellRect.width, cellRect.height);
	if (contentSize.x >= minSize || contentSize.y >= minSize)
	{
		// It must still have positive size, however.
		const int fittingSize = wxMax(1, minSize - 2 * GRID_CELL_CHECKBOX_MARGIN);

		contentSize.x =
			contentSize.y = fittingSize;
	}

	wxRect contentRect(contentSize);

	if (hAlign & wxALIGN_CENTER_HORIZONTAL)
	{
		contentRect = contentRect.CentreIn(cellRect, wxHORIZONTAL);
	}
	else if (hAlign & wxALIGN_RIGHT)
	{
		contentRect.SetX(cellRect.x + cellRect.width
			- contentSize.x - GRID_CELL_CHECKBOX_MARGIN);
	}
	else // ( hAlign == wxALIGN_LEFT ) and invalid alignment value
	{
		contentRect.SetX(cellRect.x + GRID_CELL_CHECKBOX_MARGIN);
	}

	if (vAlign & wxALIGN_CENTER_VERTICAL)
	{
		contentRect = contentRect.CentreIn(cellRect, wxVERTICAL);
	}
	else if (vAlign & wxALIGN_BOTTOM)
	{
		contentRect.SetY(cellRect.y + cellRect.height
			- contentSize.y - GRID_CELL_CHECKBOX_MARGIN);
	}
	else // wxALIGN_TOP
	{
		contentRect.SetY(cellRect.y + GRID_CELL_CHECKBOX_MARGIN);
	}

	return contentRect;
}

#endif // wxUSE_GRID
