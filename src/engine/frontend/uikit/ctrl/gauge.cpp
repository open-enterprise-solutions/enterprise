// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

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

#include "frontend/uikit/ctrl/gauge.h"


#if wxUSE_GAUGE

#include <wx/gauge.h>

#ifndef WX_PRECOMP
#endif //WX_PRECOMP

#include "frontend/uikit/renderer.h"

// ============================================================================
// implementation
// ============================================================================

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
    if ( IsVertical() )
    {
        size.x = (3*size.y) / 2 + 2;
        size.y = wxDefaultCoord;
    }
    else
    {
        size.y = (3*size.x) / 2 + 2;
        size.x = wxDefaultCoord;
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

void ibGauge::DoDraw(ibControlRenderer *renderer)
{
    renderer->DrawProgressBar(this);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibGauge, ibControl);

#endif // wxUSE_GAUGE
