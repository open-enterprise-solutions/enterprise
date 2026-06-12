// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/ctrlrend.cpp
// Purpose:     ibControlRenderer implementation
// Author:      Vadim Zeitlin
// Created:     15.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>


#ifndef WX_PRECOMP
    #include <wx/app.h>
    #include <wx/control.h>
    #include <wx/checklst.h>
    #include <wx/listbox.h>
    #include <wx/scrolbar.h>
    #include <wx/dc.h>
    #include <wx/log.h>
    #include <wx/gauge.h>
    #include <wx/image.h>
#endif // WX_PRECOMP

#include "frontend/uikit/theme.h"
#include "frontend/uikit/renderer.h"
#include "frontend/uikit/window.h"
#include "frontend/uikit/ctrl/control.h"
#include "frontend/uikit/ctrl/scrollBar.h"
#include "frontend/uikit/ctrl/listBox.h"
#include "frontend/uikit/ctrl/checkListBox.h"
#include "frontend/uikit/ctrl/gauge.h"
#include "frontend/uikit/ctrl/listBox.h"
#include "frontend/uikit/colourScheme.h"

// ============================================================================
// implementation
// ============================================================================

ibRenderer::~ibRenderer()
{
}

// ----------------------------------------------------------------------------
// ibControlRenderer
// ----------------------------------------------------------------------------

ibControlRenderer::ibControlRenderer(ibWindow *window,
                                     wxDC& dc,
                                     ibRenderer *renderer)
                : m_dc(dc)
{
    m_window = window;
    m_renderer = renderer;

    wxSize size = m_window->GetClientSize();
    m_rect.x =
    m_rect.y = 0;
    m_rect.width = size.x;
    m_rect.height = size.y;
}

void ibControlRenderer::DrawLabel()
{
    m_dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    m_dc.SetFont(m_window->GetFont());
    m_dc.SetTextForeground(m_window->GetForegroundColour());

    ibControl *ctrl = wxStaticCast(m_window, ibControl);
    wxString label = ctrl->GetLabelText();

    if ( !label.empty() )
    {
        m_renderer->DrawLabel(m_dc,
                              label,
                              m_rect,
                              m_window->GetStateFlags(),
                              ctrl->GetAlignment(),
                              ctrl->GetAccelIndex());
    }
}

void ibControlRenderer::DrawButtonLabel(const wxBitmap& bitmap,
                                        wxCoord marginX, wxCoord marginY)
{
    m_dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    m_dc.SetFont(m_window->GetFont());
    m_dc.SetTextForeground(m_window->GetForegroundColour());

    ibControl *ctrl = wxStaticCast(m_window, ibControl);
    wxString label = ctrl->GetLabelText();

    if ( !label.empty() || bitmap.IsOk() )
    {
        wxRect rectLabel = m_rect;
        if ( bitmap.IsOk() )
        {
            rectLabel.Inflate(-marginX, -marginY);
        }

        m_renderer->DrawButtonLabel(m_dc,
                                    label,
                                    bitmap,
                                    rectLabel,
                                    m_window->GetStateFlags(),
                                    ctrl->GetAlignment(),
                                    ctrl->GetAccelIndex());
    }
}

void ibControlRenderer::DrawFrame()
{
    m_dc.SetFont(m_window->GetFont());
    m_dc.SetTextForeground(m_window->GetForegroundColour());
    m_dc.SetTextBackground(m_window->GetBackgroundColour());

    ibControl *ctrl = wxStaticCast(m_window, ibControl);

    m_renderer->DrawFrame(m_dc,
                          ctrl->GetLabelText(),
                          m_rect,
                          m_window->GetStateFlags(),
                          ctrl->GetAlignment(),
                          ctrl->GetAccelIndex());
}

void ibControlRenderer::DrawButtonBorder()
{
    int flags = m_window->GetStateFlags();

    m_renderer->DrawButtonBorder(m_dc, m_rect, flags, &m_rect);

    // Why do this here?
    // m_renderer->DrawButtonSurface(m_dc, wxTHEME_BG_COLOUR(m_window), m_rect, flags );
}

void ibControlRenderer::DrawBitmap(const wxBitmap& bitmap)
{
    int style = m_window->GetWindowStyle();
    DrawBitmap(m_dc, bitmap, m_rect,
               style & wxALIGN_MASK,
               style & wxBI_EXPAND ? wxEXPAND : wxSTRETCH_NOT);
}

/* static */
void ibControlRenderer::DrawBitmap(wxDC &dc,
                                   const wxBitmap& bitmap,
                                   const wxRect& rect,
                                   int alignment,
                                   wxStretch stretch)
{
    // we may change the bitmap if we stretch it
    wxBitmap bmp = bitmap;
    if ( !bmp.IsOk() )
        return;

    int width = bmp.GetWidth(),
        height = bmp.GetHeight();

    wxCoord x = 0,
            y = 0;
    if ( stretch & wxTILE )
    {
        // tile the bitmap
        for ( ; x < rect.width; x += width )
        {
            for ( y = 0; y < rect.height; y += height )
            {
                // no need to use mask here as we cover the entire window area
                dc.DrawBitmap(bmp, x, y);
            }
        }
    }
#if wxUSE_IMAGE
    else if ( stretch & wxEXPAND )
    {
        // stretch bitmap to fill the entire control
        bmp = wxBitmap(wxImage(bmp.ConvertToImage()).Scale(rect.width, rect.height));
    }
#endif // wxUSE_IMAGE
    else // not stretched, not tiled
    {
        if ( alignment & wxALIGN_RIGHT )
        {
            x = rect.GetRight() - width + 1;
        }
        else if ( alignment & wxALIGN_CENTRE )
        {
            x = (rect.GetLeft() + rect.GetRight() - width + 1) / 2;
        }
        else // alignment & wxALIGN_LEFT
        {
            x = rect.GetLeft();
        }

        if ( alignment & wxALIGN_BOTTOM )
        {
            y = rect.GetBottom() - height + 1;
        }
        else if ( alignment & wxALIGN_CENTRE_VERTICAL )
        {
            y = (rect.GetTop() + rect.GetBottom() - height + 1) / 2;
        }
        else // alignment & wxALIGN_TOP
        {
            y = rect.GetTop();
        }
    }

    // do draw it
    dc.DrawBitmap(bmp, x, y, true /* use mask */);
}

#if wxUSE_SCROLLBAR

void ibControlRenderer::DrawScrollbar(const ibScrollBar *scrollbar,
                                      int WXUNUSED(thumbPosOld))
{
    // we will only redraw the parts which must be redrawn and not everything
    wxRegion rgnUpdate = scrollbar->GetUpdateRegion();

    {
#if wxUSE_LOG_TRACE
        wxRect rectUpdate = rgnUpdate.GetBox();
        wxLogTrace(wxT("scrollbar"),
                   wxT("%s redraw: update box is (%d, %d)-(%d, %d)"),
                   scrollbar->IsVertical() ? wxT("vert") : wxT("horz"),
                   rectUpdate.GetLeft(),
                   rectUpdate.GetTop(),
                   rectUpdate.GetRight(),
                   rectUpdate.GetBottom());
#endif // wxUSE_LOG_TRACE

#if 0 //def WXDEBUG_SCROLLBAR
        static bool s_refreshDebug = false;
        if ( s_refreshDebug )
        {
            wxClientDC dc(wxConstCast(scrollbar, ibScrollBar));
            dc.SetBrush(*wxRED_BRUSH);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(rectUpdate);

            // under Unix we use "--sync" X option for this
            #ifdef __WXMSW__
                ::GdiFlush();
                ::Sleep(200);
            #endif // __WXMSW__
        }
#endif // WXDEBUG_SCROLLBAR
    }

    wxOrientation orient = scrollbar->IsVertical() ? wxVERTICAL
                                                   : wxHORIZONTAL;

    // the shaft
    for ( int nBar = 0; nBar < 2; nBar++ )
    {
        ibScrollBar::Element elem =
            (ibScrollBar::Element)(ibScrollBar::Element_Bar_1 + nBar);

        wxRect rectBar = scrollbar->GetScrollbarRect(elem);

        if ( rgnUpdate.Contains(rectBar) )
        {
            wxLogTrace(wxT("scrollbar"),
                       wxT("drawing bar part %d at (%d, %d)-(%d, %d)"),
                       nBar + 1,
                       rectBar.GetLeft(),
                       rectBar.GetTop(),
                       rectBar.GetRight(),
                       rectBar.GetBottom());

            m_renderer->DrawScrollbarShaft(m_dc,
                                           orient,
                                           rectBar,
                                           scrollbar->GetState(elem));
        }
    }

    // arrows
    for ( int nArrow = 0; nArrow < 2; nArrow++ )
    {
        ibScrollBar::Element elem =
            (ibScrollBar::Element)(ibScrollBar::Element_Arrow_Line_1 + nArrow);

        wxRect rectArrow = scrollbar->GetScrollbarRect(elem);
        if ( rgnUpdate.Contains(rectArrow) )
        {
            wxLogTrace(wxT("scrollbar"),
                       wxT("drawing arrow %d at (%d, %d)-(%d, %d)"),
                       nArrow + 1,
                       rectArrow.GetLeft(),
                       rectArrow.GetTop(),
                       rectArrow.GetRight(),
                       rectArrow.GetBottom());

            scrollbar->GetArrows().DrawArrow
            (
                (ibScrollArrows::Arrow)nArrow,
                m_dc,
                rectArrow,
                true // draw a scrollbar arrow, not just an arrow
            );
        }
    }

    // TODO: support for page arrows

    // and the thumb
    ibScrollBar::Element elem = ibScrollBar::Element_Thumb;
    wxRect rectThumb = scrollbar->GetScrollbarRect(elem);
    if ( rectThumb.width && rectThumb.height && rgnUpdate.Contains(rectThumb) )
    {
        wxLogTrace(wxT("scrollbar"),
                   wxT("drawing thumb at (%d, %d)-(%d, %d)"),
                   rectThumb.GetLeft(),
                   rectThumb.GetTop(),
                   rectThumb.GetRight(),
                   rectThumb.GetBottom());

        m_renderer->DrawScrollbarThumb(m_dc,
                                       orient,
                                       rectThumb,
                                       scrollbar->GetState(elem));
    }
}

#endif // wxUSE_SCROLLBAR

void ibControlRenderer::DrawLine(wxCoord x1, wxCoord y1, wxCoord x2, wxCoord y2)
{
    wxASSERT_MSG( x1 == x2 || y1 == y2,
                  wxT("line must be either horizontal or vertical") );

    if ( x1 == x2 )
        m_renderer->DrawVerticalLine(m_dc, x1, y1, y2);
    else // horizontal
        m_renderer->DrawHorizontalLine(m_dc, y1, x1, x2);
}

#if 1 // wxUSE_LISTBOX — revived

void ibControlRenderer::DrawItems(const ibListBox *lbox,
                                  size_t itemFirst, size_t itemLast)
{
    DoDrawItems(lbox, itemFirst, itemLast);
}

void ibControlRenderer::DoDrawItems(const ibListBox *lbox,
                                    size_t itemFirst, size_t itemLast,
                                    bool isCheckLbox)
{
    // prepare for the drawing: calc the initial position
    wxCoord lineHeight = lbox->GetLineHeight();

    // note that SetClippingRegion() needs the physical (unscrolled)
    // coordinates while we use the logical (scrolled) ones for the drawing
    // itself
    wxRect rect;
    wxSize size = lbox->GetClientSize();
    rect.width = size.x;
    rect.height = size.y;

    // keep the text inside the client rect or we will overwrite the vertical
    // scrollbar for the long strings
    m_dc.SetClippingRegion(rect.x, rect.y, rect.width + 1, rect.height + 1);

    // adjust the rect position now
    lbox->CalcScrolledPosition(rect.x, rect.y, &rect.x, &rect.y);
    rect.y += itemFirst*lineHeight;
    rect.height = lineHeight;

    // the rect should go to the right visible border so adjust the width if x
    // is shifted (rightmost point should stay the same)
    rect.width -= rect.x;

    // we'll keep the text colour unchanged
    m_dc.SetTextForeground(lbox->GetForegroundColour());

    // an item should have the focused rect only when the lbox has focus, so
    // make sure that we never set wxCONTROL_FOCUSED flag if it doesn't
    int itemCurrent = ibWindow::FindFocus() == (ibWindow *)lbox // cast needed
                        ? lbox->GetCurrentItem()
                        : -1;
    for ( size_t n = itemFirst; n < itemLast; n++ )
    {
        int flags = 0;
        if ( (int)n == itemCurrent )
            flags |= wxCONTROL_FOCUSED;
        if ( lbox->IsSelected(n) )
            flags |= wxCONTROL_SELECTED;

#if 1 // wxUSE_CHECKLISTBOX — revived
        if ( isCheckLbox )
        {
            const ibCheckListBox *checklstbox = wxStaticCast(lbox, ibCheckListBox);
            if ( checklstbox->IsChecked(n) )
                flags |= wxCONTROL_CHECKED;

            m_renderer->DrawCheckItem(m_dc, lbox->GetString(n),
                                      wxNullBitmap,
                                      rect,
                                      flags);
        }
        else
#endif // wxUSE_CHECKLISTBOX
        {
            m_renderer->DrawItem(m_dc, lbox->GetString(n), rect, flags);
        }

        rect.y += lineHeight;
    }
}

#endif // wxUSE_LISTBOX

#if 1 // wxUSE_CHECKLISTBOX — revived

void ibControlRenderer::DrawCheckItems(const ibCheckListBox *lbox,
                                       size_t itemFirst, size_t itemLast)
{
    DoDrawItems(lbox, itemFirst, itemLast, true);
}

#endif // wxUSE_CHECKLISTBOX

#if 1 // wxUSE_GAUGE — revived

void ibControlRenderer::DrawProgressBar(const ibGauge *gauge)
{
    // draw background
    m_dc.SetBrush(m_window->GetBackgroundColour());
    m_dc.SetPen(*wxTRANSPARENT_PEN);
    m_dc.DrawRectangle(m_rect);

    int max = gauge->GetRange();
    if ( !max )
    {
        // nothing to draw
        return;
    }

    // calc the filled rect
    int pos = gauge->GetValue();
    int left = max - pos;

    wxRect rect = m_rect;
    rect.Deflate(1); // FIXME this depends on the border width

    wxColour col = m_window->UseFgCol() ? m_window->GetForegroundColour()
                                        : wxTHEME_COLOUR(GAUGE);
    m_dc.SetBrush(col);

    if ( gauge->IsSmooth() )
    {
        // just draw the rectangle in one go
        if ( gauge->IsVertical() )
        {
            // vert bars grow from bottom to top
            wxCoord dy = ((rect.height - 1) * left) / max;
            rect.y += dy;
            rect.height -= dy;
        }
        else // horizontal
        {
            // grow from left to right
            rect.width -= ((rect.width - 1) * left) / max;
        }

        m_dc.DrawRectangle(rect);
    }
    else // discrete
    {
        wxSize sizeStep = m_renderer->GetProgressBarStep();
        const int step = gauge->IsVertical() ? sizeStep.y : sizeStep.x;

        // we divide by it below!
        wxCHECK_RET( step, wxT("invalid ibGauge step") );

        // SEAM vs univ: the canon segment arithmetic systematically left the
        // tail of the bar unpainted at 100% — paint the EXACT filled length,
        // segmented, clamping the last segment instead
        const int lenTotal = gauge->IsVertical() ? rect.height : rect.width;
        const int lenFilled = (lenTotal * pos) / max;

        for ( int ofs = 0; ofs < lenFilled; ofs += step )
        {
            const int lenSeg = wxMin(step, lenFilled - ofs);

            wxRect rectSegment;
            if ( gauge->IsVertical() )
            {
                // vert bars grow from bottom to top
                rectSegment.x = rect.x;
                rectSegment.width = rect.width;
                rectSegment.y = rect.y + rect.height - ofs - lenSeg;
                rectSegment.height = lenSeg;
                rectSegment.Deflate(0, 1);
            }
            else // horizontal
            {
                rectSegment.x = rect.x + ofs;
                rectSegment.y = rect.y;
                rectSegment.width = lenSeg;
                rectSegment.height = rect.height;
                rectSegment.Deflate(1, 0);
            }

            if ( rectSegment.width <= 0 || rectSegment.height <= 0 )
                continue;

            m_dc.DrawRectangle(rectSegment);
        }
    }
}

#endif // wxUSE_GAUGE
