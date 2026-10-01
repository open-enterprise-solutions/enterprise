// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/statbmp.cpp
// Purpose:     ibStaticBitmap implementation
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

#include "frontend/uikit/ctrl/staticBitmap.h"


#if wxUSE_STATBMP

#include <wx/statbmp.h>

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/icon.h>
    #include <wx/validate.h>
#endif

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/theme.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticBitmap, ibControl);

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibStaticBitmap
// ----------------------------------------------------------------------------

bool ibStaticBitmap::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxBitmapBundle &label,
                            const wxPoint &pos,
                            const wxSize &size,
                            long style,
                            const wxString &name)
{
    if ( !ibControl::Create(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    // set bitmap first
    SetBitmap(label);

    // and adjust our size to fit it after this
    SetInitialSize(size);

    return true;
}

// ----------------------------------------------------------------------------
// bitmap/icon setting/getting and converting between
// ----------------------------------------------------------------------------

void ibStaticBitmap::SetBitmap(const wxBitmapBundle& bitmap)
{
    m_bitmapBundle = bitmap;

    InvalidateBestSize();
    SetSize(GetBestSize());
    Refresh();
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ibStaticBitmap::DoDraw(ibControlRenderer *renderer)
{
    ibControl::DoDraw(renderer);
    renderer->DrawBitmap(GetBitmap());
}

#endif // wxUSE_STATBMP
