// FORKED from wxWidgets: wxNumberEntryDialog and wxGetNumberFromUser (include/wx/generic/numdlgg.h,
// src/generic/numdlgg.cpp), of the library's dialog, static text and spin control (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/numdlgg.h
// Purpose:     wxNumberEntryDialog class
// Author:      John Labenski
// Created:     07.02.04 (extracted from textdlgg.cpp)
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_NUMBERENTRYDIALOG_H_
#define _DESKGUI_UNIV_NUMBERENTRYDIALOG_H_

#include "deskgui/univ/dialog.h"

class ibSpinCtrl;

// ----------------------------------------------------------------------------
// ibNumberEntryDialog: a dialog with spin control, [ok] and [cancel] buttons
// ----------------------------------------------------------------------------

// SEAM vs wx: the library's spin control, always there - wx's text control in its place (!wxUSE_SPINCTRL) is not kept
class DESKGUI_API ibNumberEntryDialog : public ibDialog
{
public:
    ibNumberEntryDialog()
    {
        m_value = m_min = m_max = 0;
    }

    ibNumberEntryDialog(wxWindow *parent,
                        const wxString& message,
                        const wxString& prompt,
                        const wxString& caption,
                        long value, long min, long max,
                        const wxPoint& pos = wxDefaultPosition)
    {
        Create(parent, message, prompt, caption, value, min, max, pos);
    }

    bool Create(wxWindow *parent,
                const wxString& message,
                const wxString& prompt,
                const wxString& caption,
                long value, long min, long max,
                const wxPoint& pos = wxDefaultPosition);

    long GetValue() const { return m_value; }

    // implementation only
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

protected:

    ibSpinCtrl *m_spinctrl;

    long m_value, m_min, m_max;

private:
    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibNumberEntryDialog);
    wxDECLARE_NO_COPY_CLASS(ibNumberEntryDialog);
};

// ----------------------------------------------------------------------------
// function to get a number from user
// ----------------------------------------------------------------------------

DESKGUI_API long
    ibGetNumberFromUser(const wxString& message,
                        const wxString& prompt,
                        const wxString& caption,
                        long value = 0,
                        long min = 0,
                        long max = 100,
                        wxWindow *parent = nullptr,
                        const wxPoint& pos = wxDefaultPosition);

#endif // _DESKGUI_UNIV_NUMBERENTRYDIALOG_H_
