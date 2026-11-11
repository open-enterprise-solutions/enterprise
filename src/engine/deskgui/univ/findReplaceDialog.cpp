// FORKED from wxWidgets: the find/replace dialog (src/generic/fdrepdlg.cpp, src/common/fddlgcmn.cpp) - see
// findReplaceDialog.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/fdrepdlg.cpp
// Purpose:     Find/Replace dialogs
// Author:      Markus Greither and Vadim Zeitlin
// Created:     05/25/01
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/findReplaceDialog.h"

#include <wx/sizer.h>
#include <wx/settings.h>

#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/checkBox.h"
#include "deskgui/univ/ctrl/radioBox.h"
#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/textCtrl.h"

// ============================================================================
// implementation
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibFindReplaceDialog, ibDialog);

wxBEGIN_EVENT_TABLE(ibFindReplaceDialog, ibDialog)
    EVT_BUTTON(wxID_FIND, ibFindReplaceDialog::OnFind)
    EVT_BUTTON(wxID_REPLACE, ibFindReplaceDialog::OnReplace)
    EVT_BUTTON(wxID_REPLACE_ALL, ibFindReplaceDialog::OnReplaceAll)
    EVT_BUTTON(wxID_CANCEL, ibFindReplaceDialog::OnCancel)

    EVT_UPDATE_UI(wxID_FIND, ibFindReplaceDialog::OnUpdateFindUI)
    EVT_UPDATE_UI(wxID_REPLACE, ibFindReplaceDialog::OnUpdateFindUI)
    EVT_UPDATE_UI(wxID_REPLACE_ALL, ibFindReplaceDialog::OnUpdateFindUI)

    EVT_CLOSE(ibFindReplaceDialog::OnCloseWindow)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// ibFindReplaceDialog
// ----------------------------------------------------------------------------

void ibFindReplaceDialog::Init()
{
    m_FindReplaceData = nullptr;

    m_chkWord =
    m_chkCase = nullptr;

    m_radioDir = nullptr;

    m_textFind =
    m_textRepl = nullptr;
}

bool ibFindReplaceDialog::Create(wxWindow *parent,
                                 wxFindReplaceData *data,
                                 const wxString& title,
                                 int style)
{
    parent = GetParentForModelessDialog(parent, style);

    if ( !ibDialog::Create(parent, wxID_ANY, title,
                           wxDefaultPosition, wxDefaultSize,
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
                           | style) )
    {
        return false;
    }

    SetData(data);

    wxCHECK_MSG( m_FindReplaceData, false,
                 wxT("can't create dialog without data") );

    bool isPda = (wxSystemSettings::GetScreenType() <= wxSYS_SCREEN_PDA);

    wxBoxSizer *leftsizer = new wxBoxSizer( wxVERTICAL );

    // 3 columns because there is a spacer in the middle
    wxFlexGridSizer *sizer2Col = new wxFlexGridSizer(3);
    sizer2Col->AddGrowableCol(2);

    sizer2Col->Add(new ibStaticText(this, wxID_ANY, _("Search for:"),
                                    wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                                    0,
                                    wxALIGN_CENTRE_VERTICAL | wxALIGN_RIGHT);

    sizer2Col->Add(10, 0);

    m_textFind = new ibTextCtrl(this, wxID_ANY, m_FindReplaceData->GetFindString());
    sizer2Col->Add(m_textFind, 1, wxALIGN_CENTRE_VERTICAL | wxEXPAND);

    if ( style & wxFR_REPLACEDIALOG )
    {
        sizer2Col->Add(new ibStaticText(this, wxID_ANY, _("Replace with:"),
                                        wxDefaultPosition, wxSize(80, wxDefaultCoord)),
                                        0,
                                        wxALIGN_CENTRE_VERTICAL |
                                        wxALIGN_RIGHT | wxTOP, 5);

        sizer2Col->Add(isPda ? 2 : 10, 0);

        m_textRepl = new ibTextCtrl(this, wxID_ANY,
                                    m_FindReplaceData->GetReplaceString());
        sizer2Col->Add(m_textRepl, 1,
                       wxALIGN_CENTRE_VERTICAL | wxEXPAND | wxTOP, 5);
    }

    leftsizer->Add(sizer2Col, 0, wxEXPAND | wxALL, 5);

    wxBoxSizer *optsizer = new wxBoxSizer( isPda ? wxVERTICAL : wxHORIZONTAL );

    wxBoxSizer *chksizer = new wxBoxSizer( wxVERTICAL);

    m_chkWord = new ibCheckBox(this, wxID_ANY, _("Whole word"));
    chksizer->Add(m_chkWord, 0, wxALL, 3);

    m_chkCase = new ibCheckBox(this, wxID_ANY, _("Match case"));
    chksizer->Add(m_chkCase, 0, wxALL, 3);

    optsizer->Add(chksizer, 0, wxALL, 10);

    static const wxString searchDirections[] = {_("Up"), _("Down")};
    int majorDimension = 0;
    int rbStyle ;
    if (isPda)
        rbStyle = wxRA_SPECIFY_ROWS;
    else
        rbStyle = wxRA_SPECIFY_COLS;

    m_radioDir = new ibRadioBox(this, wxID_ANY, _("Search direction"),
                                wxDefaultPosition, wxDefaultSize,
                                WXSIZEOF(searchDirections), searchDirections,
                                majorDimension, rbStyle);

    optsizer->Add(m_radioDir, 0, wxALL, isPda ? 5 : 10);

    leftsizer->Add(optsizer);

    wxBoxSizer *bttnsizer = new wxBoxSizer(wxVERTICAL);

    ibButton* btn = new ibButton(this, wxID_FIND);
    btn->SetDefault();
    bttnsizer->Add(btn, 0, wxALL, 3);

    bttnsizer->Add(new ibButton(this, wxID_CANCEL), 0, wxALL, 3);

    if ( style & wxFR_REPLACEDIALOG )
    {
        bttnsizer->Add(new ibButton(this, wxID_REPLACE, _("&Replace")),
                                    0, wxALL, 3);

        bttnsizer->Add(new ibButton(this, wxID_REPLACE_ALL, _("Replace &all")),
                                    0, wxALL, 3);
    }

    wxBoxSizer *topsizer = new wxBoxSizer( wxHORIZONTAL );

    topsizer->Add(leftsizer, 1, wxALL, isPda ? 0 : 5);
    topsizer->Add(bttnsizer, 0, wxALL, isPda ? 0 : 5);

    int flags = m_FindReplaceData->GetFlags();

    if ( flags & wxFR_MATCHCASE )
        m_chkCase->SetValue(true);

    if ( flags & wxFR_WHOLEWORD )
        m_chkWord->SetValue(true);

    m_radioDir->SetSelection( flags & wxFR_DOWN );

    if ( style & wxFR_NOMATCHCASE )
        m_chkCase->Enable(false);

    if ( style & wxFR_NOWHOLEWORD )
        m_chkWord->Enable(false);

    if ( style & wxFR_NOUPDOWN)
        m_radioDir->Enable(false);

    SetSizer( topsizer );

    topsizer->SetSizeHints( this );

    Centre( wxBOTH );

    m_textFind->SetFocus();

    return true;
}

// ----------------------------------------------------------------------------
// send the notification event
// ----------------------------------------------------------------------------

void ibFindReplaceDialog::SendEvent(const wxEventType& evtType)
{
    wxFindDialogEvent event(evtType, GetId());
    event.SetEventObject(this);
    event.SetFindString(m_textFind->GetValue());
    if ( HasFlag(wxFR_REPLACEDIALOG) )
    {
        event.SetReplaceString(m_textRepl->GetValue());
    }

    int flags = 0;

    if ( m_chkCase->GetValue() )
        flags |= wxFR_MATCHCASE;

    if ( m_chkWord->GetValue() )
        flags |= wxFR_WHOLEWORD;

    if ( !m_radioDir || m_radioDir->GetSelection() == 1 )
    {
        flags |= wxFR_DOWN;
    }

    event.SetFlags(flags);

    Send(event);
}

void ibFindReplaceDialog::Send(wxFindDialogEvent& event)
{
    // we copy the data to dialog->GetData() as well

    m_FindReplaceData->SetFlags(event.GetFlags());
    m_FindReplaceData->SetFindString(event.GetFindString());
    if ( HasFlag(wxFR_REPLACEDIALOG) &&
         (event.GetEventType() == wxEVT_FIND_REPLACE ||
          event.GetEventType() == wxEVT_FIND_REPLACE_ALL) )
    {
        m_FindReplaceData->SetReplaceString(event.GetReplaceString());
    }

    // translate wxEVT_FIND_NEXT to wxEVT_FIND if needed
    if ( event.GetEventType() == wxEVT_FIND_NEXT )
    {
        if ( m_FindReplaceData->GetFindString() != m_lastSearch )
        {
            event.SetEventType(wxEVT_FIND);

            m_lastSearch = m_FindReplaceData->GetFindString();
        }
    }

    if ( !ProcessWindowEvent(event) )
    {
        // the event is not propagated upwards to the parent automatically
        // because the dialog is a top level window, so do it manually as
        // in 9 cases out of 10 the message must be processed by the dialog
        // owner and not the dialog itself
        if ( GetParent() )
            (void)GetParent()->ProcessWindowEvent(event);
    }
}

// ----------------------------------------------------------------------------
// event handlers
// ----------------------------------------------------------------------------

void ibFindReplaceDialog::OnFind(wxCommandEvent& WXUNUSED(event))
{
    SendEvent(wxEVT_FIND_NEXT);
}

void ibFindReplaceDialog::OnReplace(wxCommandEvent& WXUNUSED(event))
{
    SendEvent(wxEVT_FIND_REPLACE);
}

void ibFindReplaceDialog::OnReplaceAll(wxCommandEvent& WXUNUSED(event))
{
    SendEvent(wxEVT_FIND_REPLACE_ALL);
}

void ibFindReplaceDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
    SendEvent(wxEVT_FIND_CLOSE);

    Show(false);
}

void ibFindReplaceDialog::OnUpdateFindUI(wxUpdateUIEvent &event)
{
    // we can't search for empty strings
    event.Enable( !m_textFind->GetValue().empty() );
}

void ibFindReplaceDialog::OnCloseWindow(wxCloseEvent &)
{
    SendEvent(wxEVT_FIND_CLOSE);
}
