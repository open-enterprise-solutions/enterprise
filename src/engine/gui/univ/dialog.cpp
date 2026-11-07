// FORKED from wxWidgets: wxDialogBase's text sizer and buttons (src/common/dlgcmn.cpp) and wxStdDialogButtonSizer
// (src/common/sizer.cpp), of the library's static text and button (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/dlgcmn.cpp, src/common/sizer.cpp
// Purpose:     common (to all ports) wxDialog functions, wxStdDialogButtonSizer
// Author:      Vadim Zeitlin, Robert Roebling
// Created:     28.06.99
// Copyright:   (c) Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "gui/univ/dialog.h"

#include <algorithm>
#include <vector>

#include <wx/settings.h>

#include "gui/univ/textSizerWrapper.h"
#include "gui/univ/window.h"
#include "gui/univ/ctrl/button.h"
#include "gui/univ/ctrl/staticLine.h"

//-----------------------------------------------------------------------------
// ibDialog
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDialog, wxDialog);

wxBEGIN_EVENT_TABLE(ibDialog, wxDialog)
    EVT_CHAR_HOOK(ibDialog::OnCharHook)
    EVT_CLOSE(ibDialog::OnCloseWindow)
wxEND_EVENT_TABLE()

wxSizer *ibDialog::CreateTextSizer(const wxString& message, int widthMax)
{
    ibTextSizerWrapper wrapper(this);

    return CreateTextSizer(message, wrapper, widthMax);
}

wxSizer *ibDialog::CreateTextSizer(const wxString& message,
                                   ibTextSizerWrapper& wrapper,
                                   int widthMax)
{
    // I admit that this is complete bogus, but it makes
    // message boxes work for pda screens temporarily..
    const bool is_pda = wxSystemSettings::GetScreenType() <= wxSYS_SCREEN_PDA;
    if (is_pda)
    {
        widthMax = wxSystemSettings::GetMetric( wxSYS_SCREEN_X, this ) - 25;
    }

    return wrapper.CreateSizer(message, widthMax);
}

wxSizer *ibDialog::CreateButtonSizer(long flags)
{
    return CreateStdDialogButtonSizer(flags);
}

wxSizer *ibDialog::CreateSeparatedSizer(wxSizer *sizer)
{
    // Mac Human Interface Guidelines recommend not to use static lines as
    // grouping elements
#if !defined(__WXMAC__)
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(new ibStaticLine(this),
                   wxSizerFlags().Expand().DoubleBorder(wxBOTTOM));
    topsizer->Add(sizer, wxSizerFlags().Expand());
    sizer = topsizer;
#endif // !__WXMAC__

    return sizer;
}

wxSizer *ibDialog::CreateSeparatedButtonSizer(long flags)
{
    wxSizer *sizer = CreateButtonSizer(flags);
    if ( !sizer )
        return nullptr;

    return CreateSeparatedSizer(sizer);
}

ibStdDialogButtonSizer *ibDialog::CreateStdDialogButtonSizer(long flags)
{
    ibStdDialogButtonSizer *sizer = new ibStdDialogButtonSizer();

    ibButton *ok = nullptr;
    ibButton *yes = nullptr;
    ibButton *no = nullptr;

    if (flags & wxOK)
    {
        ok = new ibButton(this, wxID_OK);
        sizer->AddButton(ok);
    }

    if (flags & wxCANCEL)
    {
        ibButton *cancel = new ibButton(this, wxID_CANCEL);
        sizer->AddButton(cancel);
    }

    if (flags & wxYES)
    {
        yes = new ibButton(this, wxID_YES);
        sizer->AddButton(yes);
    }

    if (flags & wxNO)
    {
        no = new ibButton(this, wxID_NO);
        sizer->AddButton(no);
    }

    if (flags & wxAPPLY)
    {
        ibButton *apply = new ibButton(this, wxID_APPLY);
        sizer->AddButton(apply);
    }

    if (flags & wxCLOSE)
    {
        ibButton *close = new ibButton(this, wxID_CLOSE);
        sizer->AddButton(close);
    }

    if (flags & wxHELP)
    {
        ibButton *help = new ibButton(this, wxID_HELP);
        sizer->AddButton(help);
    }

    if (flags & wxNO_DEFAULT)
    {
        if (no)
        {
            no->SetDefault();
            no->SetFocus();
        }
    }
    else
    {
        if (ok)
        {
            ok->SetDefault();
            ok->SetFocus();
        }
        else if (yes)
        {
            yes->SetDefault();
            yes->SetFocus();
        }
    }

    if (flags & wxOK)
        SetAffirmativeId(wxID_OK);
    else if (flags & wxYES)
        SetAffirmativeId(wxID_YES);
    else if (flags & wxCLOSE)
        SetAffirmativeId(wxID_CLOSE);

    sizer->Realize();

    return sizer;
}

bool ibDialog::SendCloseButtonClickEvent()
{
    int idCancel = GetEscapeId();
    switch ( idCancel )
    {
        case wxID_NONE:
            // The user doesn't want this dialog to close "implicitly".
            break;

        case wxID_ANY:
            // this value is special: it means translate Esc to wxID_CANCEL
            // but if there is no such button, then fall back to wxID_OK
            if ( ibButton::ClickIfPossible(wxDynamicCast(FindWindow(wxID_CANCEL), ibButton)) )
                return true;
            idCancel = GetAffirmativeId();
            wxFALLTHROUGH;

        default:
            // translate Esc to button press for the button with given id
            if ( ibButton::ClickIfPossible(wxDynamicCast(FindWindow(idCancel), ibButton)) )
                return true;
    }

    return false;
}

void ibDialog::OnCharHook(wxKeyEvent& event)
{
    if ( IsEscapeKey(event) && SendCloseButtonClickEvent() )
    {
        // Skip the call to event.Skip() below, we did handle this key.
        return;
    }

    // wxDialogBase's: a button of wx's own
    event.Skip();
}

void ibDialog::OnCloseWindow(wxCloseEvent& event)
{
    // wxDialogBase's guard: the handler of the button clicked may call Close() itself
    static std::vector<ibDialog *> closing;

    if ( std::find(closing.begin(), closing.end(), this) != closing.end() )
        return;

    closing.push_back(this);

    // When a previously hidden (necessarily modeless) dialog is being closed,
    // we must not perform the usual validation and data transfer steps as they
    // had been already done when it was hidden.
    const bool clicked = IsShown() && SendCloseButtonClickEvent();

    closing.erase(std::find(closing.begin(), closing.end(), this));

    // wxDialogBase's: a button of wx's own, else EndDialog(wxID_CANCEL)
    if ( !clicked )
        event.Skip();
}

//-----------------------------------------------------------------------------
// ibStdDialogButtonSizer
//-----------------------------------------------------------------------------

ibStdDialogButtonSizer::ibStdDialogButtonSizer()
    : wxBoxSizer(wxHORIZONTAL)
{
    bool is_pda = (wxSystemSettings::GetScreenType() <= wxSYS_SCREEN_PDA);
    // If we have a PDA screen, put yes/no button over
    // all other buttons, otherwise on the left side.
    if (is_pda)
        m_orient = wxVERTICAL;
}

void ibStdDialogButtonSizer::AddButton(ibButton *mybutton)
{
    switch (mybutton->GetId())
    {
        case wxID_OK:
        case wxID_YES:
        case wxID_SAVE:
            m_buttonAffirmative = mybutton;
            break;
        case wxID_APPLY:
            m_buttonApply = mybutton;
            break;
        case wxID_NO:
            m_buttonNegative = mybutton;
            break;
        case wxID_CANCEL:
        case wxID_CLOSE:
            m_buttonCancel = mybutton;
            break;
        case wxID_HELP:
        case wxID_CONTEXT_HELP:
            m_buttonHelp = mybutton;
            break;
        default:
            break;
    }
}

void ibStdDialogButtonSizer::SetAffirmativeButton( ibButton *button )
{
    m_buttonAffirmative = button;
}

void ibStdDialogButtonSizer::SetNegativeButton( ibButton *button )
{
    m_buttonNegative = button;
}

void ibStdDialogButtonSizer::SetCancelButton( ibButton *button )
{
    m_buttonCancel = button;
}

void ibStdDialogButtonSizer::Realize()
{
    // Helper used to move the most recently added button after the previously
    // added one in TAB order.
    class TabOrderUpdater
    {
    public:
        TabOrderUpdater()
            : m_lastAdded(nullptr)
        {
        }

        void Add(ibButton* btn)
        {
            if ( m_lastAdded )
            {
                // Button should follow previous one in the keyboard navigation
                // order.
                btn->MoveAfterInTabOrder(m_lastAdded);
            }

            m_lastAdded = btn;
        }

    private:
        ibButton* m_lastAdded;
    };

    TabOrderUpdater tabOrder;

#ifdef __WXMAC__
        Add(0, 0, 0, wxLEFT, 6);
        if (m_buttonHelp)
        {
            Add((wxWindow*)m_buttonHelp, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 6);
            tabOrder.Add(m_buttonHelp);
        }

        if (m_buttonNegative){
            // HIG POLICE BULLETIN - destructive buttons need extra padding
            // 24 pixels on either side
            Add((wxWindow*)m_buttonNegative, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 12);
            tabOrder.Add(m_buttonNegative);
        }

        // extra whitespace between help/negative and cancel/ok buttons
        Add(0, 0, 1, wxEXPAND, 0);

        if (m_buttonCancel){
            Add((wxWindow*)m_buttonCancel, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 6);
            // Cancel or help should be default
            // m_buttonCancel->SetDefaultButton();

            tabOrder.Add(m_buttonCancel);
        }

        // Ugh, Mac doesn't really have apply dialogs, so I'll just
        // figure the best place is between Cancel and OK
        if (m_buttonApply)
        {
            Add((wxWindow*)m_buttonApply, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 6);
            tabOrder.Add(m_buttonApply);
        }

        if (m_buttonAffirmative){
            Add((wxWindow*)m_buttonAffirmative, 0, wxALIGN_CENTRE | wxLEFT, 6);

            if (m_buttonAffirmative->GetId() == wxID_SAVE){
                // these buttons have set labels under Mac so we should use them
                m_buttonAffirmative->SetLabel(_("Save"));
                if (m_buttonNegative)
                    m_buttonNegative->SetLabel(_("Don't Save"));
            }
            tabOrder.Add(m_buttonAffirmative);
        }

        // Extra space around and at the right
        Add(12, 40);
#elif defined(__WXGTK__)
        // http://library.gnome.org/devel/hig-book/stable/windows-alert.html.en
        // says that the correct button order is
        //
        //      [Help]                  [Alternative] [Cancel] [Affirmative]

        // Flags ensuring that margins between the buttons are 6 pixels.
        const wxSizerFlags
            flagsBtn = wxSizerFlags().Centre().Border(wxLEFT | wxRIGHT, 3);

        // Margin around the entire sizer button should be 12.
        AddSpacer(9);

        if (m_buttonHelp)
        {
            Add(m_buttonHelp, flagsBtn);
            tabOrder.Add(m_buttonHelp);
        }

        // Align the rest of the buttons to the right.
        AddStretchSpacer();

        if (m_buttonNegative)
        {
            Add(m_buttonNegative, flagsBtn);
            tabOrder.Add(m_buttonNegative);
        }

        if (m_buttonApply)
        {
            Add(m_buttonApply, flagsBtn);
            tabOrder.Add(m_buttonApply);
        }

        if (m_buttonCancel)
        {
            Add(m_buttonCancel, flagsBtn);
            tabOrder.Add(m_buttonCancel);
        }

        if (m_buttonAffirmative)
        {
            Add(m_buttonAffirmative, flagsBtn);
            tabOrder.Add(m_buttonAffirmative);
        }

        // Ensure that the right margin is 12 as well.
        AddSpacer(9);
#elif defined(__WXMSW__)
        // Windows

        // right-justify buttons
        Add(0, 0, 1, wxEXPAND, 0);

        if (m_buttonAffirmative){
            Add((wxWindow*)m_buttonAffirmative, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonAffirmative->ConvertDialogToPixels(wxSize(2, 0)).x);
            tabOrder.Add(m_buttonAffirmative);
        }

        if (m_buttonNegative){
            Add((wxWindow*)m_buttonNegative, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonNegative->ConvertDialogToPixels(wxSize(2, 0)).x);
            tabOrder.Add(m_buttonNegative);
        }

        if (m_buttonCancel){
            Add((wxWindow*)m_buttonCancel, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonCancel->ConvertDialogToPixels(wxSize(2, 0)).x);
            tabOrder.Add(m_buttonCancel);
        }

        if (m_buttonApply)
        {
            Add((wxWindow*)m_buttonApply, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonApply->ConvertDialogToPixels(wxSize(2, 0)).x);
            tabOrder.Add(m_buttonApply);
        }

        if (m_buttonHelp)
        {
            Add((wxWindow*)m_buttonHelp, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonHelp->ConvertDialogToPixels(wxSize(2, 0)).x);
            tabOrder.Add(m_buttonHelp);
        }
#else
        // GTK+1 and any other platform

        // Add(0, 0, 0, wxLEFT, 5); // Not sure what this was for but it unbalances the dialog
        if (m_buttonHelp)
        {
            Add((wxWindow*)m_buttonHelp, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonHelp->ConvertDialogToPixels(wxSize(4, 0)).x);
            tabOrder.Add(m_buttonHelp);
        }

        // extra whitespace between help and cancel/ok buttons
        Add(0, 0, 1, wxEXPAND, 0);

        if (m_buttonApply)
        {
            Add((wxWindow*)m_buttonApply, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonApply->ConvertDialogToPixels(wxSize(4, 0)).x);
            tabOrder.Add(m_buttonApply);
        }

        if (m_buttonAffirmative){
            Add((wxWindow*)m_buttonAffirmative, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonAffirmative->ConvertDialogToPixels(wxSize(4, 0)).x);
            tabOrder.Add(m_buttonAffirmative);
        }

        if (m_buttonNegative){
            Add((wxWindow*)m_buttonNegative, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonNegative->ConvertDialogToPixels(wxSize(4, 0)).x);
            tabOrder.Add(m_buttonNegative);
        }

        if (m_buttonCancel){
            Add((wxWindow*)m_buttonCancel, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, m_buttonCancel->ConvertDialogToPixels(wxSize(4, 0)).x);
            // Cancel or help should be default
            // m_buttonCancel->SetDefaultButton();
            tabOrder.Add(m_buttonCancel);
        }

#endif
}
