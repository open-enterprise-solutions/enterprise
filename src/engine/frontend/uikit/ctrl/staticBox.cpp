// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/statbox.cpp
// Purpose:     ibStaticBox implementation
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

#include "frontend/uikit/ctrl/staticBox.h"


#if wxUSE_STATBOX

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/statbox.h>
    #include <wx/validate.h>
#endif

#include "frontend/uikit/renderer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticBox, ibControl);

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibStaticBox
// ----------------------------------------------------------------------------

bool ibStaticBox::Create(wxWindow *parent,
                         wxWindowID id,
                         const wxString &label,
                         const wxPoint &pos,
                         const wxSize &size,
                         long style,
                         const wxString &name)
{
    // FIXME refresh just the right/bottom parts affected in OnSize
    style |= wxFULL_REPAINT_ON_RESIZE;

    if ( !ibControl::Create(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    SetLabel(label);

    return true;
}

void ibStaticBox::DoDraw(ibControlRenderer *renderer)
{
    // we never have a border, so don't call the base class version which draws
    // it
    renderer->DrawFrame();
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxRect ibStaticBox::GetBorderGeometry() const
{
    // FIXME should use the renderer here
    wxRect rect;
    rect.width =
    rect.x = GetCharWidth() / 2 + 1;
    rect.y = GetCharHeight() + 1;
    rect.height = rect.y / 2;

    return rect;
}

wxPoint ibStaticBox::GetBoxAreaOrigin() const
{
    wxPoint pt = ibControl::GetClientAreaOrigin();
    wxRect rect = GetBorderGeometry();
    pt.x += rect.x;
    pt.y += rect.y;

    return pt;
}

#if 0
void ibStaticBox::DoSetClientSize(int width, int height)
{
    wxRect rect = GetBorderGeometry();

    ibControl::DoSetClientSize(width + rect.x + rect.width,
                               height + rect.y + rect.height);
}

void ibStaticBox::DoGetClientSize(int *width, int *height) const
{
    ibControl::DoGetClientSize(width, height);

    wxRect rect = GetBorderGeometry();
    if ( width )
        *width -= rect.x + rect.width;
    if ( height )
        *height -= rect.y + rect.height;
}

#endif // 0

#endif // wxUSE_STATBOX

