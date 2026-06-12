// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/dialog.h
// Purpose:     ibDialog class
// Author:      Vaclav Slavik
// Created:     2001/09/16
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_DIALOG_H_
#define _WX_UNIV_DIALOG_H_

#include "frontend/frontend.h"

extern WXDLLIMPEXP_DATA_CORE(const char) wxDialogNameStr[];
class ibWindowDisabler;
class wxEventLoop;

// Dialog boxes
class FRONTEND_API ibDialog : public wxDialogBase
{
public:
    ibDialog() { Init(); }

    // Constructor with no modal flag - the new convention.
    ibDialog(wxWindow *parent, wxWindowID id,
             const wxString& title,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = wxDEFAULT_DIALOG_STYLE,
             const wxString& name = wxASCII_STR(wxDialogNameStr))
    {
        Init();
        Create(parent, id, title, pos, size, style, name);
    }

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_DIALOG_STYLE,
                const wxString& name = wxASCII_STR(wxDialogNameStr));

    virtual ~ibDialog();

    // is the dialog in modal state right now?
    virtual bool IsModal() const override;

    // For now, same as Show(true) but returns return code
    virtual int ShowModal() override;

    // may be called to terminate the dialog with the given return code
    virtual void EndModal(int retCode) override;

    // returns true if we're in a modal loop
    bool IsModalShowing() const;

    virtual bool Show(bool show = true) override;

    // implementation only from now on
    // -------------------------------

    // event handlers
    void OnCloseWindow(wxCloseEvent& event);
    void OnOK(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

protected:
    // common part of all ctors
    void Init();

private:
    // while we are showing a modal dialog we disable the other windows using
    // this object
    ibWindowDisabler *m_windowDisabler;

    // modal dialog runs its own event loop
    wxEventLoop *m_eventLoop;

    // is modal right now?
    bool m_isShowingModal;

    wxDECLARE_DYNAMIC_CLASS(ibDialog);
    wxDECLARE_EVENT_TABLE();
};

#endif
    // _WX_UNIV_DIALOG_H_
