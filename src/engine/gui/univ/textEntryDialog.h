// FORKED from wxWidgets: wxTextEntryDialog and wxPasswordEntryDialog (include/wx/generic/textdlgg.h,
// src/generic/textdlgg.cpp), wxGetTextFromUser and wxGetPasswordFromUser (src/common/utilscmn.cpp), of the library's
// dialog, static text and text control (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/textdlgg.h
// Purpose:     wxTextEntryDialog class
// Author:      Julian Smart
// Created:     01/02/97
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _GUI_UNIV_TEXTENTRYDIALOG_H_
#define _GUI_UNIV_TEXTENTRYDIALOG_H_

#include "gui/univ/dialog.h"

#include <wx/textdlg.h>   // wxTextEntryDialogStyle, wxGetTextFromUserPromptStr - wx's defaults

class ibTextCtrl;
#if wxUSE_VALIDATORS
class ibTextValidator;
#endif

// ----------------------------------------------------------------------------
// ibTextEntryDialog: a dialog with text control, [ok] and [cancel] buttons
// ----------------------------------------------------------------------------

class GUI_API ibTextEntryDialog : public ibDialog
{
public:
    ibTextEntryDialog()
    {
        m_textctrl = nullptr;
        m_dialogStyle = 0;
    }

    ibTextEntryDialog(wxWindow *parent,
                      const wxString& message,
                      const wxString& caption = wxASCII_STR(wxGetTextFromUserPromptStr),
                      const wxString& value = wxEmptyString,
                      long style = wxTextEntryDialogStyle,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize sz = wxDefaultSize)
    {
        Create(parent, message, caption, value, style, pos, sz);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption = wxASCII_STR(wxGetTextFromUserPromptStr),
                const wxString& value = wxEmptyString,
                long style = wxTextEntryDialogStyle,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize sz = wxDefaultSize);

    void SetValue(const wxString& val);
    wxString GetValue() const { return m_value; }

    void SetHint(const wxString &hint);

    void SetMaxLength(unsigned long len);

    void ForceUpper();

#if wxUSE_VALIDATORS
    // SEAM vs wx: the library's text validator - wx's finds its field as a wxTextCtrl only (textValidator.h)
    void SetTextValidator( const ibTextValidator& validator );
    void SetTextValidator( wxTextValidatorStyle style = wxFILTER_NONE );
    ibTextValidator* GetTextValidator();
#endif // wxUSE_VALIDATORS

    virtual bool TransferDataToWindow() override;
    virtual bool TransferDataFromWindow() override;

    // implementation only
    void OnOK(wxCommandEvent& event);

protected:
    ibTextCtrl *m_textctrl;
    wxString    m_value;
    long        m_dialogStyle;

private:
    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibTextEntryDialog);
    wxDECLARE_NO_COPY_CLASS(ibTextEntryDialog);
};

// ----------------------------------------------------------------------------
// ibPasswordEntryDialog: dialog with password control, [ok] and [cancel]
// ----------------------------------------------------------------------------

class GUI_API ibPasswordEntryDialog : public ibTextEntryDialog
{
public:
    ibPasswordEntryDialog() = default;
    ibPasswordEntryDialog(wxWindow *parent,
                      const wxString& message,
                      const wxString& caption = wxASCII_STR(wxGetPasswordFromUserPromptStr),
                      const wxString& value = wxEmptyString,
                      long style = wxTextEntryDialogStyle,
                      const wxPoint& pos = wxDefaultPosition)
    {
        Create(parent, message, caption, value, style, pos);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& caption = wxASCII_STR(wxGetPasswordFromUserPromptStr),
                const wxString& value = wxEmptyString,
                long style = wxTextEntryDialogStyle,
                const wxPoint& pos = wxDefaultPosition);


private:
    wxDECLARE_DYNAMIC_CLASS(ibPasswordEntryDialog);
    wxDECLARE_NO_COPY_CLASS(ibPasswordEntryDialog);
};

// ----------------------------------------------------------------------------
// function to get a string from user
// ----------------------------------------------------------------------------

GUI_API wxString
    ibGetTextFromUser(const wxString& message,
                    const wxString& caption = wxASCII_STR(wxGetTextFromUserPromptStr),
                    const wxString& default_value = wxEmptyString,
                    wxWindow *parent = nullptr,
                    wxCoord x = wxDefaultCoord,
                    wxCoord y = wxDefaultCoord,
                    bool centre = true);

GUI_API wxString
    ibGetTextFromUser(const wxString& message,
                    const wxString& caption,
                    const wxString& default_value,
                    wxWindow *parent,
                    const wxPoint& pos,
                    const wxSize& size = wxDefaultSize,
                    bool centre = true);

GUI_API wxString
    ibGetPasswordFromUser(const wxString& message,
                        const wxString& caption = wxASCII_STR(wxGetPasswordFromUserPromptStr),
                        const wxString& default_value = wxEmptyString,
                        wxWindow *parent = nullptr,
                        wxCoord x = wxDefaultCoord,
                        wxCoord y = wxDefaultCoord,
                        bool centre = true);

#endif // _GUI_UNIV_TEXTENTRYDIALOG_H_
