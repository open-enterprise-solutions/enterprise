// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/bmpbuttn.cpp
// Purpose:     ibBitmapButton implementation
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

#include "frontend/uikit/ctrl/bmpButton.h"


#if wxUSE_BMPBUTTON

#include <wx/bmpbuttn.h>

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/validate.h>
#endif

#include "frontend/uikit/renderer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibBitmapButton, ibButton);

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibBitmapButton, ibButton)
    EVT_SET_FOCUS(ibBitmapButton::OnSetFocus)
    EVT_KILL_FOCUS(ibBitmapButton::OnKillFocus)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// ibBitmapButton
// ----------------------------------------------------------------------------

bool ibBitmapButton::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxBitmapBundle& bitmap,
                            const wxPoint &pos,
                            const wxSize &size,
                            long style,
                            const wxValidator& validator,
                            const wxString &name)
{
    // we add wxBU_EXACTFIT because the bitmap buttons are not the standard
    // ones and so shouldn't be forced to be of the standard size which is
    // typically too big for them
    if ( !ibButton::Create(parent, id, bitmap, wxEmptyString,
                           pos, size, style | wxBU_EXACTFIT, validator, name) )
        return false;

    m_bitmaps[State_Normal] = bitmap;

    return true;
}

void ibBitmapButton::OnSetBitmap()
{
    wxBitmap bmp;
    if ( !IsEnabled() )
    {
        bmp = GetBitmapDisabled();
    }
    else if ( IsPressed() )
    {
        bmp = GetBitmapPressed();
    }
    else if ( IsFocused() )
    {
        bmp = GetBitmapFocus();
    }
    //else: just leave it invalid, this means "normal" anyhow in ChangeBitmap()

    ChangeBitmap(bmp);
}

bool ibBitmapButton::ChangeBitmap(const wxBitmap& bmp)
{
    wxBitmap bitmap = bmp.IsOk() ? bmp : GetBitmapLabel();
    if ( bitmap.IsSameAs(m_bitmap) )
        return false;

    m_bitmap = bitmap;
    SetInitialSize(bitmap.GetSize());

    return true;
}

bool ibBitmapButton::Enable(bool enable)
{
    if ( !ibButton::Enable(enable) )
        return false;

    if ( !enable && ChangeBitmap(GetBitmapDisabled()) )
        Refresh();

    return true;
}

bool ibBitmapButton::WXMakeCurrent(bool doit)
{
    ChangeBitmap(doit ? GetBitmapFocus() : GetBitmapLabel());

    return ibButton::WXMakeCurrent(doit);
}

void ibBitmapButton::OnSetFocus(wxFocusEvent& event)
{
    if ( ChangeBitmap(GetBitmapFocus()) )
        Refresh();

    event.Skip();
}

void ibBitmapButton::OnKillFocus(wxFocusEvent& event)
{
    if ( ChangeBitmap(GetBitmapLabel()) )
        Refresh();

    event.Skip();
}

void ibBitmapButton::Press()
{
    ChangeBitmap(GetBitmapPressed());

    ibButton::Press();
}

void ibBitmapButton::Release()
{
    ChangeBitmap(IsFocused() ? GetBitmapFocus() : GetBitmapLabel());

    ibButton::Release();
}

#endif // wxUSE_BMPBUTTON
