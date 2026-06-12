// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/button.cpp
// Purpose:     ibButton
// Author:      Vadim Zeitlin
// Created:     14.08.00
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

#include "frontend/uikit/ctrl/button.h"
#include "frontend/uikit/window.h"


#if wxUSE_BUTTON

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/dcscreen.h>
    #include <wx/button.h>
    #include <wx/validate.h>
    #include <wx/settings.h>
#endif

#include <wx/stockitem.h>
// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// default margins around the image
static const wxCoord DEFAULT_BTN_MARGIN_X = 0;  // We should give space for the border, at least.
static const wxCoord DEFAULT_BTN_MARGIN_Y = 0;

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

void ibButton::Init()
{
    m_isPressed =
    m_isDefault = false;
}

bool ibButton::Create(wxWindow *parent,
                      wxWindowID id,
                      const wxBitmapBundle& bitmap,
                      const wxString &lbl,
                      const wxPoint &pos,
                      const wxSize &size,
                      long style,
                      const wxValidator& validator,
                      const wxString &name)
{
    wxString label(lbl);
    if (label.empty() && wxIsStockID(id))
        label = wxGetStockLabel(id);

    long ctrl_style = style & ~wxBU_ALIGN_MASK;
    ctrl_style = ctrl_style & ~wxALIGN_MASK;

    if((style & wxBU_RIGHT) == wxBU_RIGHT)
        ctrl_style |= wxALIGN_RIGHT;
    else if((style & wxBU_LEFT) == wxBU_LEFT)
        ctrl_style |= wxALIGN_LEFT;
    else
        ctrl_style |= wxALIGN_CENTRE_HORIZONTAL;

    if((style & wxBU_TOP) == wxBU_TOP)
        ctrl_style |= wxALIGN_TOP;
    else if((style & wxBU_BOTTOM) == wxBU_BOTTOM)
        ctrl_style |= wxALIGN_BOTTOM;
    else
        ctrl_style |= wxALIGN_CENTRE_VERTICAL;

    if ( !ibControl::Create(parent, id, pos, size, ctrl_style, validator, name) )
        return false;

    SetLabel(label);

    if (bitmap.IsOk())
        SetBitmap(bitmap); // SetInitialSize called by SetBitmap()
    else
        SetInitialSize(size);

    CreateInputHandler(ibINP_HANDLER_BUTTON);

    return true;
}

ibButton::~ibButton()
{
}

// ----------------------------------------------------------------------------
// size management
// ----------------------------------------------------------------------------

/* static */
wxSize ibButton::GetDefaultSize(wxWindow* WXUNUSED(win))
{
    static wxSize s_sizeBtn;

    if ( s_sizeBtn.x == 0 )
    {
        wxScreenDC dc;

        s_sizeBtn.x = dc.GetCharWidth()*10 + 2;

        // SEAM vs univ: the canon used charHeight*11/10 + 2, which came out
        // SMALLER than a text field of the same font (line + 2*(border+pad)
        // = line + 6) — buttons and fields on one form row must be equally
        // tall, so the row height is the maximum of the two formulas
        const wxCoord hChar = dc.GetCharHeight();
        s_sizeBtn.y = wxMax(hChar*11/10 + 2, hChar + 6);
    }

    return s_sizeBtn;
}


// ----------------------------------------------------------------------------
// input processing
// ----------------------------------------------------------------------------

void ibButton::Click()
{
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    InitCommandEvent(event);
    Command(event);
}

// ----------------------------------------------------------------------------
// misc
// ----------------------------------------------------------------------------

wxBitmap ibButton::DoGetBitmap(State WXUNUSED(which)) const
{
    return m_bitmap;
}

void ibButton::DoSetBitmap(const wxBitmapBundle& bitmap, State which)
{
    // we support only one bitmap right now, although this wouldn't be
    // difficult to change
    if ( which == wxAnyButtonBase::State_Normal )
        m_bitmap = bitmap.GetBitmap(wxDefaultSize); // TODO-HIDPI

    DoSetBitmapMargins(DEFAULT_BTN_MARGIN_X, DEFAULT_BTN_MARGIN_Y);
}

void ibButton::DoSetBitmapMargins(wxCoord x, wxCoord y)
{
    m_marginBmpX = x + 2;
    m_marginBmpY = y + 2;

    SetInitialSize(wxDefaultSize);
}

ibWindow *ibButton::SetDefault()
{
    // SEAM vs univ: no native default-button bookkeeping — the flag is ours
    m_isDefault = true;
    Refresh();
    return this;
}

wxIMPLEMENT_DYNAMIC_CLASS(ibButton, ibControl);

#endif // wxUSE_BUTTON

