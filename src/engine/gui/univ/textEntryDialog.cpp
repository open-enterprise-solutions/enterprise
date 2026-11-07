// FORKED from wxWidgets: wxTextEntryDialog (src/generic/textdlgg.cpp) and wxGetTextFromUser (src/common/utilscmn.cpp)
// - see textEntryDialog.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/textdlgg.cpp
// Purpose:     wxTextEntryDialog
// Author:      Julian Smart
// Created:     04/01/98
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "gui/univ/textEntryDialog.h"

#include "gui/univ/ctrl/textCtrl.h"
#include "gui/univ/textValidator.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

static const int wxID_TEXT = 3000;

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibTextEntryDialog
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibTextEntryDialog, ibDialog)
    EVT_BUTTON(wxID_OK, ibTextEntryDialog::OnOK)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibTextEntryDialog, ibDialog);

bool ibTextEntryDialog::Create(wxWindow *parent,
                                     const wxString& message,
                                     const wxString& caption,
                                     const wxString& value,
                                     long style,
                                     const wxPoint& pos,
                                     const wxSize sz)
{
    // Do not pass style to GetParentForModalDialog() as wxDIALOG_NO_PARENT
    // has the same numeric value as wxTE_MULTILINE and so attempting to create
    // a dialog for editing multiline text would also prevent it from having a
    // parent which is undesirable. As it is, we can't create a text entry
    // dialog without a parent which is not ideal either but is a much less
    // important problem.
    if ( !ibDialog::Create(GetParentForModalDialog(parent, 0),
                           wxID_ANY, caption,
                           pos, sz,
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) )
    {
        return false;
    }

    SetMinSize( sz );

    m_dialogStyle = style;
    m_value = value;

    wxBoxSizer *topsizer = new wxBoxSizer( wxVERTICAL );
    // 1) text message
    topsizer->Add(CreateTextSizer(message), wxSizerFlags().DoubleBorder());

    // 2) text ctrl: create it with wxTE_RICH2 style to allow putting more than
    // 64KiB of text into it
    if ( style & wxTE_MULTILINE )
        style |= wxTE_RICH2;

    m_textctrl = new ibTextCtrl(this, wxID_TEXT, value,
                                wxDefaultPosition, wxSize(300, wxDefaultCoord),
                                style & ~wxTextEntryDialogStyle);

    topsizer->Add(m_textctrl,
                  wxSizerFlags(style & wxTE_MULTILINE ? 1 : 0).
                    Expand().
                    TripleBorder(wxLEFT | wxRIGHT));

    // 3) buttons if any
    wxSizer *buttonSizer = CreateSeparatedButtonSizer(style & (wxOK | wxCANCEL));
    if ( buttonSizer )
    {
        topsizer->Add(buttonSizer, wxSizerFlags().Expand().DoubleBorder());
    }

    SetSizer( topsizer );

    // Size returned by Fit() is the minimum size needed to fit the contents.
    const wxSize minSize = topsizer->Fit( this );

    // Prevent the dialog from being resized smaller than the minimum size.
    SetMinSize(minSize);

    // For single-line entry, also constrain max height to prevent vertical expansion
    if ( !(style & wxTE_MULTILINE) )
    {
        SetMaxSize(wxSize(-1, minSize.GetHeight()));
    }

    if ( style & wxCENTRE )
        Centre( wxBOTH );

    return true;
}

bool ibTextEntryDialog::TransferDataToWindow()
{
    if ( m_textctrl )
    {
        m_textctrl->SetValue(m_value);
        m_textctrl->SetFocus();
    }

    return ibDialog::TransferDataToWindow();
}

bool ibTextEntryDialog::TransferDataFromWindow()
{
    if ( m_textctrl )
    {
        m_value = m_textctrl->GetValue();
    }

    return ibDialog::TransferDataFromWindow();
}

void ibTextEntryDialog::OnOK(wxCommandEvent& WXUNUSED(event) )
{
    if ( Validate() && TransferDataFromWindow() )
    {
        EndModal( wxID_OK );
    }
}

void ibTextEntryDialog::SetMaxLength(unsigned long len)
{
    if ( m_textctrl )
    {
        m_textctrl->SetMaxLength(len);
    }
}

void ibTextEntryDialog::SetValue(const wxString& val)
{
    m_value = val;

    if ( m_textctrl )
    {
        m_textctrl->SetValue(val);
    }
}

void ibTextEntryDialog::SetHint(const wxString& hint)
{
    if ( m_textctrl )
    {
        m_textctrl->SetHint(hint);
    }
}

void ibTextEntryDialog::ForceUpper()
{
    if ( m_textctrl )
    {
        m_textctrl->ForceUpper();
    }
}

#if wxUSE_VALIDATORS

void ibTextEntryDialog::SetTextValidator( wxTextValidatorStyle style )
{
    SetTextValidator(ibTextValidator(style));
}

void ibTextEntryDialog::SetTextValidator( const ibTextValidator& validator )
{
    if ( m_textctrl )
    {
        m_textctrl->SetValidator( validator );
    }
}

ibTextValidator* ibTextEntryDialog::GetTextValidator()
{
    return (ibTextValidator*)m_textctrl->GetValidator();
}

#endif // wxUSE_VALIDATORS

// ----------------------------------------------------------------------------
// ibPasswordEntryDialog
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibPasswordEntryDialog, ibTextEntryDialog);

bool ibPasswordEntryDialog::Create(wxWindow *parent,
                                   const wxString& message,
                                   const wxString& caption,
                                   const wxString& value,
                                   long style,
                                   const wxPoint& pos)
{
    // Only change from ibTextEntryDialog is the password style
    return ibTextEntryDialog::Create(parent, message, caption, value,
                                     style | wxTE_PASSWORD, pos);
}

// ----------------------------------------------------------------------------
// text entry functions (wx's src/common/utilscmn.cpp)
// ----------------------------------------------------------------------------

namespace {

// Common part of ibGetTextFromUser() and ibGetPasswordFromUser(): check the
// position, resetting it if it is invalid, and return the dialog style to use.
long PrepareTextEntryDialog(wxPoint& pos, bool centre)
{
    if ( pos != wxDefaultPosition && !pos.IsFullySpecified() )
    {
        wxFAIL_MSG("Either both x and y must be valid or none of them.");
        pos = wxDefaultPosition;
    }

    long style = wxTextEntryDialogStyle;

    // Explicitly specified position takes precedence over centring.
    if ( !centre || pos != wxDefaultPosition )
        style &= ~wxCENTRE;

    return style;
}

} // namespace

wxString ibGetTextFromUser(const wxString& message, const wxString& caption,
                        const wxString& defaultValue, wxWindow *parent,
                        wxCoord x, wxCoord y, bool centre )
{
    return ibGetTextFromUser(message, caption, defaultValue, parent,
                             wxPoint(x, y), wxDefaultSize, centre);
}

wxString ibGetTextFromUser(const wxString& message, const wxString& caption,
                        const wxString& defaultValue, wxWindow *parent,
                        const wxPoint& posOrig, const wxSize& size, bool centre )
{
    wxPoint pos = posOrig;
    const long style = PrepareTextEntryDialog(pos, centre);

    wxString str;
    ibTextEntryDialog dialog(parent, message, caption, defaultValue, style,
                             pos, size);

    if (dialog.ShowModal() == wxID_OK)
    {
        str = dialog.GetValue();
    }

    return str;
}

wxString ibGetPasswordFromUser(const wxString& message,
                               const wxString& caption,
                               const wxString& defaultValue,
                               wxWindow *parent,
                               wxCoord x, wxCoord y, bool centre )
{
    wxPoint pos(x, y);
    const long style = PrepareTextEntryDialog(pos, centre);

    wxString str;
    ibPasswordEntryDialog dialog(parent, message, caption, defaultValue,
                                 style, pos);
    if ( dialog.ShowModal() == wxID_OK )
    {
        str = dialog.GetValue();
    }

    return str;
}
