// FORKED from wxWidgets: wxGenericMessageDialog (src/generic/msgdlgg.cpp) and wxMessageBox (src/common/utilscmn.cpp) -
// see messageDialog.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/msgdlgg.cpp
// Purpose:     wxGenericMessageDialog
// Author:      Julian Smart, Robert Roebling
// Created:     04/01/98
// Copyright:   (c) Julian Smart and Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/messageDialog.h"

#include <wx/artprov.h>
#include <wx/modalhook.h>
#include <wx/settings.h>

#include "deskgui/univ/textSizerWrapper.h"
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/staticBitmap.h"

// ----------------------------------------------------------------------------
// ibTitleTextWrapper: simple class to create wrapped text in "title font"
// ----------------------------------------------------------------------------

class ibTitleTextWrapper : public ibTextSizerWrapper
{
public:
    ibTitleTextWrapper(wxWindow *win)
        : ibTextSizerWrapper(win)
    {
    }

protected:
    virtual wxWindow *OnCreateLine(const wxString& s) override
    {
        wxWindow * const win = ibTextSizerWrapper::OnCreateLine(s);

        win->SetFont(win->GetFont().Larger().MakeBold());

        return win;
    }
};

// ----------------------------------------------------------------------------
// icons
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibMessageDialog, ibDialog)
        EVT_BUTTON(wxID_YES, ibMessageDialog::OnYes)
        EVT_BUTTON(wxID_NO, ibMessageDialog::OnNo)
        EVT_BUTTON(wxID_HELP, ibMessageDialog::OnHelp)
        EVT_BUTTON(wxID_CANCEL, ibMessageDialog::OnCancel)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibMessageDialog, ibDialog);

ibMessageDialog::ibMessageDialog( wxWindow *parent,
                                  const wxString& message,
                                  const wxString& caption,
                                  long style,
                                  const wxPoint& pos)
                      : m_message(message),
                        m_caption(caption),
                        m_pos(pos)
{
    // wxMessageDialogBase's
    m_parent = GetParentForModalDialog(parent, style);
    SetMessageDialogStyle(style);

    m_created = false;
}

wxSizer *ibMessageDialog::CreateMsgDlgButtonSizer()
{
    if ( HasCustomLabels() )
    {
        ibStdDialogButtonSizer * const sizerStd = new ibStdDialogButtonSizer;

        ibButton *btnDef = nullptr;

        if ( m_dialogStyle & wxOK )
        {
            btnDef = new ibButton(this, wxID_OK, GetCustomOKLabel());
            sizerStd->AddButton(btnDef);
        }

        if ( m_dialogStyle & wxCANCEL )
        {
            ibButton * const
                cancel = new ibButton(this, wxID_CANCEL, GetCustomCancelLabel());
            sizerStd->AddButton(cancel);

            if ( m_dialogStyle & wxCANCEL_DEFAULT )
                btnDef = cancel;
        }

        if ( m_dialogStyle & wxYES_NO )
        {
            ibButton * const
                yes = new ibButton(this, wxID_YES, GetCustomYesLabel());
            sizerStd->AddButton(yes);

            ibButton * const
                no = new ibButton(this, wxID_NO, GetCustomNoLabel());
            sizerStd->AddButton(no);
            if ( m_dialogStyle & wxNO_DEFAULT )
                btnDef = no;
            else if ( !btnDef )
                btnDef = yes;
        }

        if ( m_dialogStyle & wxHELP )
        {
            ibButton * const
                help = new ibButton(this, wxID_HELP, GetCustomHelpLabel());
            sizerStd->AddButton(help);
        }

        if ( btnDef )
        {
            btnDef->SetDefault();
            btnDef->SetFocus();
        }

        sizerStd->Realize();

        return CreateSeparatedSizer(sizerStd);
    }

    // Use standard labels for all buttons
    return CreateSeparatedButtonSizer
           (
                m_dialogStyle & (wxOK | wxCANCEL | wxHELP | wxYES_NO |
                                 wxNO_DEFAULT | wxCANCEL_DEFAULT)
           );
}

void ibMessageDialog::DoCreateMsgdialog()
{
    ibDialog::Create(m_parent, wxID_ANY, m_caption, m_pos, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

    wxBoxSizer *topsizer = new wxBoxSizer( wxVERTICAL );

    wxBoxSizer *icon_text = new wxBoxSizer( wxHORIZONTAL );

    // 1) icon
    if (m_dialogStyle & wxICON_MASK)
    {
        ibStaticBitmap *icon = new ibStaticBitmap
                                   (
                                    this,
                                    wxID_ANY,
                                    wxArtProvider::GetMessageBoxIcon(m_dialogStyle)
                                   );
        if ( wxSystemSettings::GetScreenType() <= wxSYS_SCREEN_PDA )
            topsizer->Add( icon, 0, wxTOP|wxLEFT|wxRIGHT | wxALIGN_LEFT, FromDIP(10) );
        else
            icon_text->Add(icon, wxSizerFlags().Top().Border(wxRIGHT, FromDIP(20)));
    }

    // 2) text

    wxBoxSizer * const textsizer = new wxBoxSizer(wxVERTICAL);

    // To prevent clipping
    int maxWidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X, this) - FromDIP(25);

    // To enhance readability
    maxWidth = wxMin( maxWidth, GetCharWidth() * 70 );

    // We want to show the main message in a different font to make it stand
    // out if the extended message is used as well. This looks better and is
    // more consistent with the native dialogs under MSW and GTK.
    wxString lowerMessage;
    if ( !m_extendedMessage.empty() )
    {
        ibTitleTextWrapper titleWrapper(this);
        textsizer->Add(CreateTextSizer(GetMessage(), titleWrapper, maxWidth),
                       wxSizerFlags().Border(wxBOTTOM, FromDIP(20)));

        lowerMessage = GetExtendedMessage();
    }
    else // no extended message
    {
        lowerMessage = GetMessage();
    }

    textsizer->Add(CreateTextSizer(lowerMessage, maxWidth));

    icon_text->Add(textsizer, 0, wxALIGN_CENTER, 10);
    topsizer->Add( icon_text, 1, wxLEFT|wxRIGHT|wxTOP, 10 );

    // 3) optional checkbox and detailed text
    AddMessageDialogCheckBox( topsizer );
    AddMessageDialogDetails( topsizer );

    // 4) buttons
    wxSizer *sizerBtn = CreateMsgDlgButtonSizer();
    if ( sizerBtn )
        topsizer->Add(sizerBtn, 0, wxEXPAND | wxALL, 10 );

    SetSizer( topsizer );

    topsizer->SetSizeHints( this );
    wxSize size( GetSize() );
    if (size.x < size.y*3/2)
    {
        size.x = size.y*3/2;
        SetSize( size );
    }

    Centre( wxBOTH | wxCENTER_FRAME);
}

void ibMessageDialog::OnYes(wxCommandEvent& WXUNUSED(event))
{
    EndModal( wxID_YES );
}

void ibMessageDialog::OnNo(wxCommandEvent& WXUNUSED(event))
{
    EndModal( wxID_NO );
}

void ibMessageDialog::OnHelp(wxCommandEvent& WXUNUSED(event))
{
    EndModal( wxID_HELP );
}

void ibMessageDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
    // Allow cancellation via ESC/Close button except if
    // only YES and NO are specified.
    const long style = GetMessageDialogStyle();
    if ( (style & wxYES_NO) != wxYES_NO || (style & wxCANCEL) )
    {
        EndModal( wxID_CANCEL );
    }
}

int ibMessageDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    if ( !m_created )
    {
        m_created = true;
        DoCreateMsgdialog();
    }

    return ibDialog::ShowModal();
}

// ----------------------------------------------------------------------------
// ibMessageBox (wx's src/common/utilscmn.cpp)
// ----------------------------------------------------------------------------

int ibMessageBox(const wxString& message, const wxString& caption, long style,
                 wxWindow *parent, int WXUNUSED(x), int WXUNUSED(y) )
{
    // add the appropriate icon unless this was explicitly disabled by use of
    // wxICON_NONE
    if ( !(style & wxICON_NONE) && !(style & wxICON_MASK) )
    {
        style |= style & wxYES ? wxICON_QUESTION : wxICON_INFORMATION;
    }

    ibMessageDialog dialog(parent, message, caption, style);

    int ans = dialog.ShowModal();
    switch ( ans )
    {
        case wxID_OK:
            return wxOK;
        case wxID_YES:
            return wxYES;
        case wxID_NO:
            return wxNO;
        case wxID_CANCEL:
            return wxCANCEL;
        case wxID_HELP:
            return wxHELP;
    }

    wxFAIL_MSG( wxT("unexpected return code from ibMessageDialog") );

    return wxCANCEL;
}

// ----------------------------------------------------------------------------
// ibLogGui
// ----------------------------------------------------------------------------

void ibLogGui::DoShowSingleLogMessage(const wxString& message,
                                      const wxString& title,
                                      int style)
{
    ibMessageBox(message, title, style);
}

void ibLogGui::DoShowMultipleLogMessages(const wxArrayString& messages,
                                         const wxArrayInt& WXUNUSED(severities),
                                         const wxArrayLong& WXUNUSED(times),
                                         const wxString& title,
                                         int style)
{
    // the latest first, no more lines than a box holds - wxLogGui's own count without its log dialog (src/generic/logg.cpp)
    const size_t lineWidth = 156;
    wxString text;
    size_t lines = 0;
    for ( size_t n = messages.size(); n > 0; n-- )
    {
        lines += (messages[n - 1].length() + lineWidth - 1) / lineWidth;
        if ( lines > 25 )
            break;

        text << messages[n - 1] << wxT("\n");
    }

    ibMessageBox(text, title, style);
}
