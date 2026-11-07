// FORKED from wxWidgets: wxNumberEntryDialog (src/generic/numdlgg.cpp) - see numberEntryDialog.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/numdlgg.cpp
// Purpose:     wxGetNumberFromUser implementation
// Author:      Vadim Zeitlin
// Created:     23.07.99
// Copyright:   (c) Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "gui/univ/numberEntryDialog.h"

#include "gui/univ/ctrl/spinCtrl.h"
#include "gui/univ/ctrl/staticText.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibNumberEntryDialog
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibNumberEntryDialog, ibDialog)
    EVT_BUTTON(wxID_OK, ibNumberEntryDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, ibNumberEntryDialog::OnCancel)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibNumberEntryDialog, ibDialog);

bool ibNumberEntryDialog::Create(wxWindow *parent,
                                         const wxString& message,
                                         const wxString& prompt,
                                         const wxString& caption,
                                         long value,
                                         long min,
                                         long max,
                                         const wxPoint& pos)
{
    if ( !ibDialog::Create(GetParentForModalDialog(parent, 0),
                           wxID_ANY, caption,
                           pos, wxDefaultSize) )
    {
        return false;
    }

    m_value = value;
    m_max = max;
    m_min = min;

    wxBoxSizer *topsizer = new wxBoxSizer( wxVERTICAL );
    // 1) text message
    topsizer->Add( CreateTextSizer( message ), wxSizerFlags().DoubleBorder() );

    // 2) prompt and text ctrl
    wxBoxSizer *inputsizer = new wxBoxSizer( wxHORIZONTAL );

    // prompt if any
    if (!prompt.empty())
        inputsizer->Add( new ibStaticText( this, wxID_ANY, prompt ),
                         wxSizerFlags().Center().DoubleBorder(wxLEFT) );

    // spin ctrl
    wxString valStr;
    valStr.Printf(wxT("%ld"), m_value);
    m_spinctrl = new ibSpinCtrl(this, wxID_ANY, valStr, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, (int)m_min, (int)m_max, (int)m_value);
    inputsizer->Add( m_spinctrl, wxSizerFlags(1).Center().DoubleBorder(wxLEFT | wxRIGHT));
    // add both
    topsizer->Add( inputsizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT));

    // 3) buttons if any
    wxSizer *buttonSizer = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if ( buttonSizer )
    {
        topsizer->Add(buttonSizer, wxSizerFlags().Expand().DoubleBorder());
    }

    SetSizer( topsizer );

    topsizer->SetSizeHints( this );

    Centre( wxBOTH );

    m_spinctrl->SetSelection(-1, -1);
    m_spinctrl->SetFocus();

    return true;
}

void ibNumberEntryDialog::OnOK(wxCommandEvent& WXUNUSED(event))
{
    m_value = m_spinctrl->GetValue();
    if ( m_value < m_min || m_value > m_max )
    {
        // not a number or out of range
        m_value = -1;
        EndModal(wxID_CANCEL);
    }

    EndModal(wxID_OK);
}

void ibNumberEntryDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
    EndModal(wxID_CANCEL);
}

// ----------------------------------------------------------------------------
// global functions
// ----------------------------------------------------------------------------

// ibGetTextFromUser is in textEntryDialog.cpp

long ibGetNumberFromUser(const wxString& msg,
                         const wxString& prompt,
                         const wxString& title,
                         long value,
                         long min,
                         long max,
                         wxWindow *parent,
                         const wxPoint& pos)
{
    ibNumberEntryDialog dialog(parent, msg, prompt, title,
                               value, min, max, pos);
    if (dialog.ShowModal() == wxID_OK)
        return dialog.GetValue();

    return -1;
}
