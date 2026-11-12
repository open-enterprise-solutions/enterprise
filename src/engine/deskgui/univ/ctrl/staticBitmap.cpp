// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

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

#include "deskgui/univ/ctrl/staticBitmap.h"


#if wxUSE_STATBMP

#include <wx/statbmp.h>

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/icon.h>
    #include <wx/validate.h>
#endif

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticBitmap, ibControl);

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibStaticBitmap, ibControl)
    EVT_PAINT(ibStaticBitmap::OnPaint)
wxEND_EVENT_TABLE()

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
    // painted whole as it is sized: its picture is aligned or stretched in it (ibWindow::OnSize paints again only the
    // frame)
    style |= wxFULL_REPAINT_ON_RESIZE;

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

void ibStaticBitmap::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    const int style = GetWindowStyle();
    ibRenderer::DrawBitmap(dc, GetBitmap(), wxRect(GetClientSize()), style & wxALIGN_MASK,
                           style & wxBI_EXPAND ? wxEXPAND : wxSTRETCH_NOT);
}

#endif // wxUSE_STATBMP
