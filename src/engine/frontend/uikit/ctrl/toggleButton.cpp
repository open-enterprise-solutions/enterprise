// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/tglbtn.cpp
// Purpose:     ibToggleButton
// Author:      Vadim Zeitlin
// Modified by: David Bjorkevik
// Created:     16.05.06
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#include <wx/wxprec.h>

#include "frontend/uikit/ctrl/toggleButton.h"


#if wxUSE_TOGGLEBTN

#include <wx/tglbtn.h>
#include "frontend/uikit/renderer.h"
#include "frontend/uikit/colourScheme.h"
#include "frontend/uikit/theme.h"

#include <wx/stockitem.h>

// wxEVT_TOGGLEBUTTON is already defined by the native wx core

wxIMPLEMENT_DYNAMIC_CLASS(ibToggleButton, ibAnyButton);

ibToggleButton::ibToggleButton()
{
    Init();
}

ibToggleButton::ibToggleButton(wxWindow *parent,
                       wxWindowID id,
                       const wxString& label,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    Init();
    Create(parent, id, label, pos, size, style, validator, name);
}

bool ibToggleButton::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxString& lbl,
                            const wxPoint& pos,
                            const wxSize& size, long style,
                            const wxValidator& validator,
                            const wxString& name)
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
    {
        wxFAIL_MSG(wxT("ibToggleButton creation failed"));
        return false;
    }
    SetLabel(label);
    CreateInputHandler(ibINP_HANDLER_BUTTON);
    return true;
}

void ibToggleButton::Init()
{
    m_isPressed = false;
    m_value = false;
}

void ibToggleButton::Toggle()
{
    if ( m_isPressed )
        Release();
    else
        Press();

    if ( !m_isPressed )
    {
        // releasing button after it had been pressed generates a click event
        // and toggles value
        m_value = !m_value;
        Click();
    }
}

void ibToggleButton::Click()
{
    wxCommandEvent event(wxEVT_TOGGLEBUTTON, GetId());
    InitCommandEvent(event);
    event.SetInt(GetValue());
    Command(event);
}

void ibToggleButton::SetValue(bool state)
{
    m_value = state;
    Refresh();
}

#endif // wxUSE_TOGGLEBTN
