// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

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

#include "deskgui/univ/ctrl/choice.h"
#include "deskgui/univ/ctrl/textCtrl.h"


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
    // BORN WITH NOTHING CHOSEN, as the native choice is — the value shown is the selection. The univ original showed
    // the first item while selecting none, so a cell's editor opened on an unset row showed `Filter.Ref`, gave back
    // nothing, and the row came out empty (Max, 2026-11-09, the choice parameter links). A caller wanting an item
    // shown selects it.
    const wxString value;
    return ibComboBox::Create(parent, id, value,
                                 pos, size, n, choices,
                                 wxCB_READONLY | style, validator, name);
}


wxSize ibChoice::DoGetBestClientSize() const
{
    // the combo's: the field, the margin, the button (the square of the field's height)
    const wxSize best = ibComboBox::DoGetBestClientSize();

    const ibTextCtrl* const text = GetTextCtrl();
    if ( text == nullptr )
        return best;

    // the canon's: an empty list as wide as 80 DIP
    int widest = 0;
    for ( unsigned int n = 0; n < GetCount(); n++ )
        widest = wxMax(widest, text->GetTextExtent(GetString(n)).x);
    if ( widest == 0 )
        widest = FromDIP(80);

    // the combo's own, its field as wide as the widest item
    return wxSize(best.x - text->GetBestSize().x + text->GetSizeFromTextSize(widest).x, best.y);
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
