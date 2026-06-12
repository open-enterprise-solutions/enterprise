// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/choice.cpp
// Purpose:     ibChoice implementation
// Author:      Vadim Zeitlin
// Created:     15.12.00
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

#include "frontend/uikit/ctrl/choice.h"


#if wxUSE_CHOICE

#include <wx/choice.h>

#ifndef WX_PRECOMP
    #include <wx/arrstr.h>
#endif

wxBEGIN_EVENT_TABLE(ibChoice, ibComboBox)
    EVT_COMBOBOX(wxID_ANY, ibChoice::OnComboBox)
wxEND_EVENT_TABLE()

ibChoice::ibChoice(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   const wxArrayString& choices,
                   long style,
                   const wxValidator& validator,
                   const wxString& name)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

bool ibChoice::Create(wxWindow *parent, wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      const wxArrayString& choices,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    wxCArrayString chs(choices);

    return Create(parent, id, pos, size, chs.GetCount(), chs.GetStrings(),
                  style, validator, name);
}

bool ibChoice::Create(wxWindow *parent, wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      int n, const wxString choices[],
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    wxString value;
    if ( n )
        value = choices[0];
    return ibComboBox::Create(parent, id, value,
                                 pos, size, n, choices,
                                 wxCB_READONLY | style, validator, name);
}


void ibChoice::OnComboBox(wxCommandEvent& event)
{
    if ( event.GetId() == GetId() )
    {
        event.SetEventType(wxEVT_CHOICE);
        event.Skip();
        GetEventHandler()->ProcessEvent(event);
    }
    else
        event.Skip();
}

wxIMPLEMENT_DYNAMIC_CLASS(ibChoice, ibComboBox);

#endif // wxUSE_CHOICE
