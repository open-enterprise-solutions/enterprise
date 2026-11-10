// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/radiobut.cpp
// Purpose:     ibRadioButton implementation
// Author:      Vadim Zeitlin
// Created:     10.09.00
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

#include "deskgui/univ/ctrl/radioButton.h"


#if wxUSE_RADIOBTN

#include <wx/radiobut.h>

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/validate.h>
#endif

#include "deskgui/univ/theme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/inputHandler.h"
#include "deskgui/univ/colourScheme.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibRadioButton
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibRadioButton, ibCheckBox)
    EVT_PAINT(ibRadioButton::OnPaint)
wxEND_EVENT_TABLE()

bool ibRadioButton::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString &label,
                        const wxPoint &pos,
                        const wxSize &size,
                        long style,
                        const wxValidator& validator,
                        const wxString &name)
{
    if ( !ibCheckBox::Create(parent, id, label, pos, size, style,
                             validator, name) )
    {
        return false;
    }

    // A GROUP ALWAYS HAS ONE CHECKED, and the first one is it until somebody says otherwise — the native button's
    // rule. The fork dropped it, so a dialog written against the native one opened with none checked while its
    // fields already named the first (Max, 2026-11-09, the form wizard's kinds).
    if ( GetFirstInGroup() == this )
        SetValue(true);

    return true;
}

// ----------------------------------------------------------------------------
// group walking (wxRadioButtonBase shims)
// ----------------------------------------------------------------------------

ibRadioButton *ibRadioButton::GetFirstInGroup() const
{
    wxWindow *parent = GetParent();
    if ( !parent )
        return const_cast<ibRadioButton *>(this);

    // walk backwards from this control to the radio that starts the group
    ibRadioButton *first = const_cast<ibRadioButton *>(this);
    wxWindowList::compatibility_iterator node =
        parent->GetChildren().Find(const_cast<ibRadioButton *>(this));
    while ( node )
    {
        ibRadioButton *radio = dynamic_cast<ibRadioButton *>(node->GetData());
        if ( radio )
        {
            first = radio;
            if ( radio->GetWindowStyle() & wxRB_GROUP )
                break;
        }
        node = node->GetPrevious();
    }
    return first;
}

ibRadioButton *ibRadioButton::GetNextInGroup() const
{
    wxWindow *parent = GetParent();
    if ( !parent )
        return nullptr;

    wxWindowList::compatibility_iterator node =
        parent->GetChildren().Find(const_cast<ibRadioButton *>(this));
    if ( node )
        node = node->GetNext();
    while ( node )
    {
        ibRadioButton *radio = dynamic_cast<ibRadioButton *>(node->GetData());
        if ( radio )
        {
            // the next group starts here — we were the last of ours
            return (radio->GetWindowStyle() & wxRB_GROUP) ? nullptr : radio;
        }
        node = node->GetNext();
    }
    return nullptr;
}

ibRadioButton *ibRadioButton::GetLastInGroup() const
{
    ibRadioButton *last = const_cast<ibRadioButton *>(this);
    for ( ibRadioButton *radio = last->GetNextInGroup();
          radio;
          radio = radio->GetNextInGroup() )
    {
        last = radio;
    }
    return last;
}
// ----------------------------------------------------------------------------
// radio button methods
// ----------------------------------------------------------------------------

void ibRadioButton::OnCheck()
{
    // clear all the other radio buttons in our group
    ibRadioButton* const last = GetLastInGroup();
    for ( ibRadioButton* radio = GetFirstInGroup();
          radio;
          radio = radio->GetNextInGroup() )
    {
        if ( radio != this )
            radio->ClearValue();

        if ( radio == last )
            break;
    }
}

void ibRadioButton::ChangeValue(bool value)
{
    if ( value == IsChecked() )
        return;

    if ( !IsChecked() )
    {
        ibCheckBox::ChangeValue(value);
    }
    else // attempt to clear a radio button - this can't be done
    {
        // but still refresh as our PRESSED flag changed
        Refresh();
    }
}

void ibRadioButton::ClearValue()
{
    if ( IsChecked() )
    {
        SetValue(false);
    }
}

void ibRadioButton::SendEvent()
{
    wxCommandEvent event(wxEVT_RADIOBUTTON, GetId());
    InitCommandEvent(event);
    event.SetInt(IsChecked());
    Command(event);
}

// ----------------------------------------------------------------------------
// overridden ibCheckBox methods
// ----------------------------------------------------------------------------

wxSize ibRadioButton::GetBitmapSize() const
{
    wxBitmap bmp = GetBitmap(State_Normal, Status_Checked);
    return bmp.IsOk() ? wxSize(bmp.GetWidth(), bmp.GetHeight())
                    : GetRenderer()->GetRadioBitmapSize();
}

void ibRadioButton::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());

    int flags = GetStateFlags();
    Status status = GetStatus();
    if ( status == Status_Checked )
        flags |= wxCONTROL_CHECKED;

    GetRenderer()->
        DrawRadioButton(dc,
                        GetLabel(),
                        GetBitmap(GetState(flags), status),
                        wxRect(GetClientSize()),
                        flags,
                        GetWindowStyle() & wxALIGN_RIGHT ? wxALIGN_RIGHT
                                                         : wxALIGN_LEFT,
                        GetAccelIndex());
}

wxIMPLEMENT_DYNAMIC_CLASS(ibRadioButton, ibCheckBox);

#endif // wxUSE_RADIOBTN
