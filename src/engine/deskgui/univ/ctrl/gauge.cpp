// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/gauge/gauge.cpp
// Purpose:     ibGauge for wxUniversal
// Author:      Vadim Zeitlin
// Created:     20.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/gauge.h"


#if wxUSE_GAUGE

#include <wx/gauge.h>

#ifndef WX_PRECOMP
#endif //WX_PRECOMP

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibGauge, ibControl)
    EVT_PAINT(ibGauge::OnPaint)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// ibGauge creation
// ----------------------------------------------------------------------------

void ibGauge::Init()
{
    m_gaugePos =
    m_rangeMax = 0;
}

bool ibGauge::Create(wxWindow *parent,
                     wxWindowID id,
                     int range,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    if ( !ibControl::Create(parent, id, pos, size, style, validator, name) )
        return false;

    m_rangeMax = range;

    SetInitialSize(size);

    return true;
}

// ----------------------------------------------------------------------------
// ibGauge range/position
// ----------------------------------------------------------------------------

void ibGauge::SetRange(int range)
{
    m_rangeMax = range;
    if ( m_gaugePos > m_rangeMax )
        m_gaugePos = m_rangeMax;

    Refresh();
}

void ibGauge::SetValue(int pos)
{
    // SEAM vs univ: the univ original asserted on pos > range, but the
    // native wxGauge (the host behaviour we mirror) clamps — a +N step
    // over an unaligned range is normal caller code, not an error
    if ( pos < 0 )
        pos = 0;
    else if ( pos > m_rangeMax )
        pos = m_rangeMax;

    m_gaugePos = pos;

    Refresh();
}

// ----------------------------------------------------------------------------
// ibGauge geometry
// ----------------------------------------------------------------------------

wxSize ibGauge::DoGetBestClientSize() const
{
    wxSize size = GetRenderer()->GetProgressBarStep();

    // these adjustments are really ridiculous - they are just done to find the
    // "correct" result under Windows (FIXME)
    // SEAM vs univ: the canon gave no length (wxDefaultCoord), and a gauge in a
    // sizer came out as wide as nothing — the native one's is taken: the Windows
    // HIG's 107 dialog units (wxMSW's wxGauge::DoGetBestSize)
    const int length = ConvertDialogToPixels(wxSize(107, 0)).x;
    if ( IsVertical() )
    {
        size.x = (3*size.y) / 2 + 2;
        size.y = length;
    }
    else
    {
        size.y = (3*size.x) / 2 + 2;
        size.x = length;
    }

    return size;
}

// ----------------------------------------------------------------------------
// ibGauge drawing
// ----------------------------------------------------------------------------

wxBorder ibGauge::GetDefaultBorder() const
{
    return wxBORDER_STATIC;
}

void ibGauge::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);
    const wxRect rectClient(GetClientSize());

    // draw background
    dc.SetBrush(GetBackgroundColour());
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(rectClient);

    int max = GetRange();
    if ( !max )
    {
        // nothing to draw
        return;
    }

    // calc the filled rect
    int pos = GetValue();
    int left = max - pos;

    wxRect rect = rectClient;
    rect.Deflate(1); // FIXME this depends on the border width

    wxColour col = UseFgCol() ? GetForegroundColour() : wxTHEME_COLOUR(GAUGE);
    dc.SetBrush(col);

    if ( IsSmooth() )
    {
        // just draw the rectangle in one go
        if ( IsVertical() )
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

        dc.DrawRectangle(rect);
    }
    else // discrete
    {
        wxSize sizeStep = GetRenderer()->GetProgressBarStep();
        const int step = IsVertical() ? sizeStep.y : sizeStep.x;

        // we divide by it below!
        wxCHECK_RET( step, wxT("invalid ibGauge step") );

        // SEAM vs univ: the canon segment arithmetic systematically left the
        // tail of the bar unpainted at 100% — paint the EXACT filled length,
        // segmented, clamping the last segment instead
        const int lenTotal = IsVertical() ? rect.height : rect.width;
        const int lenFilled = (lenTotal * pos) / max;

        for ( int ofs = 0; ofs < lenFilled; ofs += step )
        {
            const int lenSeg = wxMin(step, lenFilled - ofs);

            wxRect rectSegment;
            if ( IsVertical() )
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

            dc.DrawRectangle(rectSegment);
        }
    }
}

wxIMPLEMENT_DYNAMIC_CLASS(ibGauge, ibControl);

#endif // wxUSE_GAUGE
