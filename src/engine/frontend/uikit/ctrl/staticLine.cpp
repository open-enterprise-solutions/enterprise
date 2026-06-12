// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

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

#include "frontend/uikit/ctrl/staticLine.h"


#if wxUSE_STATLINE

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/validate.h>
#endif

#include <wx/statline.h>

#include "frontend/uikit/renderer.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibStaticLine
// ----------------------------------------------------------------------------

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

void ibStaticLine::DoDraw(ibControlRenderer *renderer)
{
    // we never have a border, so don't call the base class version whcih draws
    // it
    wxSize sz = GetSize();
    wxCoord x2, y2;
    if ( IsVertical() )
    {
        x2 = 0;
        y2 = sz.y;
    }
    else // horizontal
    {
        x2 = sz.x;
        y2 = 0;
    }

    renderer->DrawLine(0, 0, x2, y2);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticLine, ibControl);

#endif // wxUSE_STATLINE

