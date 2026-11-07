// FORKED from wxWidgets: wxSearchCtrl (include/wx/srchctrl.h, wxSearchCtrlBase; include/wx/generic/srchctlg.h,
// src/generic/srchctlg.cpp), on the library's text field (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/srchctlg.h
// Purpose:     generic wxSearchCtrl class
// Author:      Vince Harron
// Created:     2006-02-19
// Copyright:   Vince Harron
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _GUI_UNIV_CTRL_SEARCHCTRL_H_
#define _GUI_UNIV_CTRL_SEARCHCTRL_H_

#include "gui/univ/ctrl/textCtrl.h"         // base class declaration

#include <wx/srchctrl.h>                    // wxEVT_SEARCH, wxEVT_SEARCH_CANCEL, wxSearchCtrlNameStr - wx's

// SEAM vs wx: THE SEARCH CONTROL IS THE LIBRARY'S TEXT FIELD - its frame, surface and focus; the glass and the "×" are
// the field's own buttons (ibTextCtrl::ShowOpenButton, ShowClearButton), after the text. wx's generic one was a
// frameless text beside buttons of its own, on a control of its own: a second field with buttons, which looked and
// behaved like none. Not kept: the search menu (SetMenu) and the user bitmaps (SetSearchBitmap...) - the field's
// buttons are labels.
class GUI_API ibSearchCtrl : public ibTextCtrl
{
public:
    // creation
    // --------

    ibSearchCtrl() {}
    ibSearchCtrl(wxWindow *parent, wxWindowID id,
               const wxString& value = wxEmptyString,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxSearchCtrlNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxSearchCtrlNameStr));

    // get/set search options
    // ----------------------
    void ShowSearchButton( bool show );
    bool IsSearchButtonVisible() const;

    void ShowCancelButton( bool show );
    bool IsCancelButtonVisible() const;

    void SetDescriptiveText(const wxString& text);
    wxString GetDescriptiveText() const;

private:
    // Enter and the glass search (wxEVT_SEARCH), the "×" clears the field (wxEVT_SEARCH_CANCEL)
    void OnTextEnter(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnClear(wxCommandEvent& event);

    wxDECLARE_DYNAMIC_CLASS(ibSearchCtrl);
};

#endif // _GUI_UNIV_CTRL_SEARCHCTRL_H_
