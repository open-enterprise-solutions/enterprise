// FORKED from wxWidgets: wxFindReplaceDialogBase (include/wx/fdrepdlg.h, src/common/fddlgcmn.cpp) and
// wxGenericFindReplaceDialog (include/wx/generic/fdrepdlg.h, src/generic/fdrepdlg.cpp), of the library's dialog, static
// texts, text fields, check boxes, radio box and buttons (wx -> ib prefixes applied). wx's data and events are kept
// (wxFindReplaceData, wxFindDialogEvent, wxEVT_FIND_*): a view finds as it found.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/fdrepdlg.h
// Purpose:     wxGenericFindReplaceDialog class
// Author:      Markus Greither
// Created:     25/05/2001
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_FINDREPLACEDIALOG_H_
#define _DESKGUI_UNIV_FINDREPLACEDIALOG_H_

#include "deskgui/univ/dialog.h"

#include <wx/fdrepdlg.h>   // wxFindReplaceData, wxFindDialogEvent, wxFR_* - the dialog's data and its events, wx's

class ibCheckBox;
class ibRadioBox;
class ibTextCtrl;

// ----------------------------------------------------------------------------
// ibFindReplaceDialog: dialog for searching / replacing text
// ----------------------------------------------------------------------------

// SEAM vs wx: one class of wxFindReplaceDialogBase and wxGenericFindReplaceDialog - the base derives wxDialog, the
// theme's dialog is ibDialog; the data written through its setters (wx's base was its friend)
class DESKGUI_API ibFindReplaceDialog : public ibDialog
{
public:
    ibFindReplaceDialog() { Init(); }

    ibFindReplaceDialog(wxWindow *parent,
                        wxFindReplaceData *data,
                        const wxString& title,
                        int style = 0)
    {
        Init();

        (void)Create(parent, data, title, style);
    }

    bool Create(wxWindow *parent,
                wxFindReplaceData *data,
                const wxString& title,
                int style = 0);

    // get/set the data
    const wxFindReplaceData *GetData() const { return m_FindReplaceData; }
    void SetData(wxFindReplaceData *data) { m_FindReplaceData = data; }

protected:
    void Init();

    void SendEvent(const wxEventType& evtType);
    // the event to the dialog's handlers, its parent's when none takes it (wxFindReplaceDialogBase::Send)
    void Send(wxFindDialogEvent& event);

    void OnFind(wxCommandEvent& event);
    void OnReplace(wxCommandEvent& event);
    void OnReplaceAll(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    void OnUpdateFindUI(wxUpdateUIEvent& event);

    void OnCloseWindow(wxCloseEvent& event);

    wxFindReplaceData *m_FindReplaceData;

    // the last string searched for
    wxString m_lastSearch;

    ibCheckBox *m_chkCase,
               *m_chkWord;

    ibRadioBox *m_radioDir;

    ibTextCtrl *m_textFind,
               *m_textRepl;

private:
    wxDECLARE_DYNAMIC_CLASS(ibFindReplaceDialog);

    wxDECLARE_EVENT_TABLE();
};

#endif // _DESKGUI_UNIV_FINDREPLACEDIALOG_H_
