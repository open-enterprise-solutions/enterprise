// FORKED from wxWidgets: wxSearchCtrl (src/generic/srchctlg.cpp) - see searchCtrl.h.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/srchctlg.cpp
// Purpose:     implements wxSearchCtrl as a composite control
// Author:      Vince Harron
// Created:     2006-02-19
// Copyright:   Vince Harron
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "gui/univ/ctrl/searchCtrl.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibSearchCtrl, ibTextCtrl);

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibSearchCtrl creation
// ----------------------------------------------------------------------------

ibSearchCtrl::ibSearchCtrl(wxWindow *parent, wxWindowID id,
           const wxString& value,
           const wxPoint& pos,
           const wxSize& size,
           long style,
           const wxValidator& validator,
           const wxString& name)
{
    Create(parent, id, value, pos, size, style, validator, name);
}

bool ibSearchCtrl::Create(wxWindow *parent, wxWindowID id,
            const wxString& value,
            const wxPoint& pos,
            const wxSize& size,
            long style,
            const wxValidator& validator,
            const wxString& name)
{
    // Enter searches, as in wx's text inside the search control
    if ( !ibTextCtrl::Create(parent, id, value, pos, size,
                             style | wxTE_PROCESS_ENTER, validator, name) )
    {
        return false;
    }

    SetHint(_("Search"));

    // the search button is there from the start, the cancel one is shown on demand
    ShowOpenButton();

    Bind(wxEVT_TEXT_ENTER, &ibSearchCtrl::OnTextEnter, this);
    Bind(wxEVT_CONTROL_BUTTON_OPEN, &ibSearchCtrl::OnOpen, this);
    Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibSearchCtrl::OnClear, this);
    return true;
}

// search control specific interfaces

void ibSearchCtrl::ShowSearchButton( bool show )
{
    ShowOpenButton(show);
}

bool ibSearchCtrl::IsSearchButtonVisible() const
{
    return IsOpenButtonVisible();
}

void ibSearchCtrl::ShowCancelButton( bool show )
{
    ShowClearButton(show);
}

bool ibSearchCtrl::IsCancelButtonVisible() const
{
    return IsClearButtonVisible();
}

void ibSearchCtrl::SetDescriptiveText(const wxString& text)
{
    SetHint(text);
}

wxString ibSearchCtrl::GetDescriptiveText() const
{
    return GetHint();
}

// ----------------------------------------------------------------------------
// the field's keys and buttons
// ----------------------------------------------------------------------------

void ibSearchCtrl::OnTextEnter(wxCommandEvent& WXUNUSED(event))
{
    if ( !IsEmpty() )
    {
        wxCommandEvent event(wxEVT_SEARCH, GetId());
        event.SetEventObject(this);
        event.SetString(GetValue());

        ProcessWindowEvent(event);
    }
}

void ibSearchCtrl::OnOpen(wxCommandEvent& WXUNUSED(event))
{
    // it's convenient to have the string to search for directly in the
    // event instead of having to retrieve it from the control in the
    // event handler code later, so provide it here
    wxCommandEvent event(wxEVT_SEARCH, GetId());
    event.SetEventObject(this);
    event.SetString(GetValue());

    ProcessWindowEvent(event);
}

void ibSearchCtrl::OnClear(wxCommandEvent& WXUNUSED(event))
{
    Clear();

    wxCommandEvent event(wxEVT_SEARCH_CANCEL, GetId());
    event.SetEventObject(this);

    ProcessWindowEvent(event);
}
