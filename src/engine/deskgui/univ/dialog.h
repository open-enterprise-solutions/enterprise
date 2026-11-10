// FORKED from wxWidgets: wxDialogBase's text sizer and buttons (src/common/dlgcmn.cpp) and wxStdDialogButtonSizer
// (include/wx/sizer.h, src/common/sizer.cpp), of the library's static text and button (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/dialog.h, wx/sizer.h
// Purpose:     wxDialogBase class, wxStdDialogButtonSizer
// Author:      Vadim Zeitlin, Robert Roebling
// Created:     29.06.99
// Copyright:   (c) Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_DIALOG_H_
#define _DESKGUI_UNIV_DIALOG_H_

#include "deskgui/deskgui.h"
#include "deskgui/univ/topLevel.h"

#include <wx/dialog.h>
#include <wx/sizer.h>

class ibButton;
class ibStdDialogButtonSizer;
class ibTextSizerWrapper;

// SEAM vs univ: THE WINDOW IS THE NATIVE DIALOG, ITS DECORATIONS THE LIBRARY'S (ibTopLevelWindow) - its modal loop is
// the native dialog's, not the univ dialog's own. The library's part beyond is the door to its buttons: wxDialogBase
// finds a dialog's buttons as wxButton alone - Esc and the title bar's x (EmulateButtonClickIfPresent), the row of
// standard buttons it creates (CreateStdDialogButtonSizer) - and ibButton is not one. A button of wx's own still answers
// through wx's handlers, which run after these.
class DESKGUI_API ibDialog : public ibTopLevelWindow<wxDialog>
{
public:
    ibDialog() = default;

    ibDialog(wxWindow *parent, wxWindowID id,
             const wxString& title,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = wxDEFAULT_DIALOG_STYLE,
             const wxString& name = wxASCII_STR(wxDialogNameStr))
    {
        Create(parent, id, title, pos, size, style, name);
    }

    // wxDialog's, with its defaults - under the drop shadow a menu casts
    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_DIALOG_STYLE,
                const wxString& name = wxASCII_STR(wxDialogNameStr));

    // wxDialogBase's, of the library's static texts: the message wrapped at widthMax, a text per line - the wrapper's
    // texts when one is given (textSizerWrapper.h)
    wxSizer *CreateTextSizer(const wxString& message, int widthMax = -1);
    wxSizer *CreateTextSizer(const wxString& message,
                             ibTextSizerWrapper& wrapper,
                             int widthMax = -1);

    // wxDialogBase's, of the library's buttons; the separated ones under a line of its own
    wxSizer *CreateButtonSizer(long flags);
    wxSizer *CreateSeparatedSizer(wxSizer *sizer);
    wxSizer *CreateSeparatedButtonSizer(long flags);
    ibStdDialogButtonSizer *CreateStdDialogButtonSizer(long flags);

protected:
    // wxDialogBase's: the button of the escape id, or Cancel and then the affirmative one when it is wxID_ANY, is
    // clicked - true if there was one
    bool SendCloseButtonClickEvent();

private:
    void OnCharHook(wxKeyEvent& event);
    void OnCloseWindow(wxCloseEvent& event);

    wxDECLARE_DYNAMIC_CLASS(ibDialog);
    wxDECLARE_EVENT_TABLE();
};

// wxStdDialogButtonSizer of the library's buttons: OK, Cancel, Help... in the platform's order
class DESKGUI_API ibStdDialogButtonSizer : public wxBoxSizer
{
public:
    ibStdDialogButtonSizer();

    // Checks button ID against system IDs and sets one of the pointers below
    // to this button. Does not do any sizer-related things here.
    void AddButton(ibButton *button);

    // Use these if no standard ID can/should be used
    void SetAffirmativeButton(ibButton *button);
    void SetNegativeButton(ibButton *button);
    void SetCancelButton(ibButton *button);

    // All platform-specific code here, checks which buttons exist and add
    // them to the sizer accordingly.
    // Note - one potential hack on Mac we could use here,
    // if m_buttonAffirmative is wxID_SAVE then ensure wxID_SAVE
    // is set to _("Save") and m_buttonNegative is set to _("Don't Save")
    // I wasn't sure whether to add this or not.
    void Realize();

    ibButton *GetAffirmativeButton() const { return m_buttonAffirmative; }
    ibButton *GetApplyButton() const { return m_buttonApply; }
    ibButton *GetNegativeButton() const { return m_buttonNegative; }
    ibButton *GetCancelButton() const { return m_buttonCancel; }
    ibButton *GetHelpButton() const { return m_buttonHelp; }

protected:
    ibButton *m_buttonAffirmative = nullptr;  // wxID_OK, wxID_YES, wxID_SAVE go here
    ibButton *m_buttonApply = nullptr;        // wxID_APPLY
    ibButton *m_buttonNegative = nullptr;     // wxID_NO
    ibButton *m_buttonCancel = nullptr;       // wxID_CANCEL, wxID_CLOSE
    ibButton *m_buttonHelp = nullptr;         // wxID_HELP, wxID_CONTEXT_HELP

private:
    wxDECLARE_NO_COPY_CLASS(ibStdDialogButtonSizer);
};

#endif
    // _DESKGUI_UNIV_DIALOG_H_
