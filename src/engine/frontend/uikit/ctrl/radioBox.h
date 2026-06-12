// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/radiobox.h
// Purpose:     ibRadioBox declaration
// Author:      Vadim Zeitlin
// Created:     11.09.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_RADIOBOX_H_
#define _WX_UNIV_RADIOBOX_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/staticBox.h"

class ibRadioButton;

#include <wx/dynarray.h>

WX_DEFINE_EXPORTED_ARRAY_PTR(ibRadioButton *, wxArrayRadioButtons);

// ----------------------------------------------------------------------------
// ibRadioBox: a box full of radio buttons
// ----------------------------------------------------------------------------

class FRONTEND_API ibRadioBox : public ibStaticBox,
                               public wxRadioBoxBase
{
public:
    // ibRadioBox construction
    ibRadioBox() { Init(); }

    ibRadioBox(wxWindow *parent,
               wxWindowID id,
               const wxString& title,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               int n = 0, const wxString *choices = nullptr,
               int majorDim = 0,
               long style = wxRA_SPECIFY_COLS,
               const wxValidator& val = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxRadioBoxNameStr))
    {
        Init();

        (void)Create(parent, id, title, pos, size, n, choices,
                     majorDim, style, val, name);
    }
    ibRadioBox(wxWindow *parent,
               wxWindowID id,
               const wxString& title,
               const wxPoint& pos,
               const wxSize& size,
               const wxArrayString& choices,
               int majorDim = 0,
               long style = wxRA_SPECIFY_COLS,
               const wxValidator& val = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxRadioBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0, const wxString *choices = nullptr,
                int majorDim = 0,
                long style = wxRA_SPECIFY_COLS,
                const wxValidator& val = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRadioBoxNameStr));
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                int majorDim = 0,
                long style = wxRA_SPECIFY_COLS,
                const wxValidator& val = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRadioBoxNameStr));

    virtual ~ibRadioBox();

    // implement ibRadioBox interface
    virtual void SetSelection(int n) override;
    virtual int GetSelection() const override;

    virtual unsigned int GetCount() const override
        { return (unsigned int)m_buttons.GetCount(); }

    virtual wxString GetString(unsigned int n) const override;
    virtual void SetString(unsigned int n, const wxString& label) override;

    virtual bool Enable(unsigned int n, bool enable = true) override;
    virtual bool Show(unsigned int n, bool show = true) override;

    virtual bool IsItemEnabled(unsigned int n) const override;
    virtual bool IsItemShown(unsigned int n) const override;

    // we also override the ibControl methods to avoid virtual function hiding
    virtual bool Enable(bool enable = true) override;
    virtual bool Show(bool show = true) override;
    virtual wxString GetLabel() const override;
    virtual void SetLabel(const wxString& label) override;

    // we inherit a version always returning false from ibStaticBox, override
    // it to behave normally
    virtual bool AcceptsFocus() const override { return ibControl::AcceptsFocus(); }

#if wxUSE_TOOLTIPS
    virtual void DoSetToolTip( wxToolTip *tip );
#endif // wxUSE_TOOLTIPS

    // wxUniversal-only methods

    // another Append() version
    void Append(int n, const wxString *choices);

    // implementation only: called by ibRadioHookHandler
    void OnRadioButton(wxEvent& event);
    bool OnKeyDown(wxKeyEvent& event);

protected:
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // override the base class methods dealing with window positioning/sizing
    // as we must move/size the buttons as well
    virtual void DoMoveWindow(int x, int y, int width, int height) override;
    virtual wxSize DoGetBestClientSize() const override;

    // generate a radiobutton click event for the current item
    void SendRadioEvent();

    // common part of all ctors
    void Init();

    // calculate the max size of all buttons
    wxSize GetMaxButtonSize() const;

    // the currently selected radio button or -1
    int m_selection;

    // all radio buttons
    wxArrayRadioButtons m_buttons;

    // the event handler which is used to translate radiobutton events into
    // radiobox one
    wxEvtHandler *m_evtRadioHook;

private:
    wxDECLARE_DYNAMIC_CLASS(ibRadioBox);
};

#endif // _WX_UNIV_RADIOBOX_H_
