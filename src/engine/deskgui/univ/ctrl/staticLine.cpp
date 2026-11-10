// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/statline.cpp
// Purpose:     ibStaticLine implementation
// Author:      Vadim Zeitlin
// Created:     25.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/staticLine.h"


#if wxUSE_STATLINE

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/validate.h>
#endif

#include <wx/statline.h>

#include "deskgui/univ/renderer.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibStaticLine
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibStaticLine, ibControl)
    EVT_PAINT(ibStaticLine::OnPaint)
wxEND_EVENT_TABLE()

bool ibStaticLine::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint &pos,
                          const wxSize &size,
                          long style,
                          const wxString &name)
{
    if ( !ibControl::Create(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    wxSize sizeReal = AdjustSize(size);
    if ( sizeReal != size )
        SetSize(sizeReal);

    return true;
}

void ibStaticLine::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    const wxSize sz = GetSize();
    if ( IsVertical() )
        GetRenderer()->DrawVerticalLine(dc, 0, 0, sz.y);
    else // horizontal
        GetRenderer()->DrawHorizontalLine(dc, 0, 0, sz.x);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticLine, ibControl);

#endif // wxUSE_STATLINE

