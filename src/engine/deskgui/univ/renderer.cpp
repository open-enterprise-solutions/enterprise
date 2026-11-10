// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/ctrlrend.cpp
// Purpose:     ibRenderer implementation
// Author:      Vadim Zeitlin
// Created:     15.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// SEAM vs univ: the canon's ibControlRenderer - a DC, a rect and the theme bundled for a control's DoDraw, and the
// drawing of a label, a frame, a scroll bar, a list's items, a gauge - is gone with the drawing doors: each control
// draws itself in its own paint (its OnPaint, on an ibThemePaintDC), asking the theme; the theme knows no control.

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>


#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/image.h>
#endif // WX_PRECOMP

#include "deskgui/univ/renderer.h"

// ============================================================================
// implementation
// ============================================================================

ibRenderer::~ibRenderer()
{
}

/* static */
void ibRenderer::DrawBitmap(wxDC &dc,
                            const wxBitmap& bitmap,
                            const wxRect& rect,
                            int alignment,
                            wxStretch stretch)
{
    // we may change the bitmap if we stretch it
    wxBitmap bmp = bitmap;
    if ( !bmp.IsOk() )
        return;

    // SEAM vs univ: the size the DC draws it at - in logical units, not the
    // pixels: a bitmap of scale 2 (OSX Retina) has twice the pixels, centring
    // by them put it half its height above the middle of the rect
    const wxSize sizeBmp = bmp.GetLogicalSize();
    int width = sizeBmp.x,
        height = sizeBmp.y;

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
