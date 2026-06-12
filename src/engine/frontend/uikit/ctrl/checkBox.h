// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/checkbox.h
// Purpose:     ibCheckBox declaration
// Author:      Vadim Zeitlin
// Created:     07.09.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_CHECKBOX_H_
#define _WX_UNIV_CHECKBOX_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/control.h"

#include <wx/button.h> // for ibStdButtonInputHandler

// ----------------------------------------------------------------------------
// the actions supported by ibCheckBox
// ----------------------------------------------------------------------------

#define ibACTION_CHECKBOX_CHECK   wxT("check")   // SetValue(true)
#define ibACTION_CHECKBOX_CLEAR   wxT("clear")   // SetValue(false)
#define ibACTION_CHECKBOX_TOGGLE  wxT("toggle")  // toggle the check state

// additionally it accepts ibACTION_BUTTON_PRESS and RELEASE

// ----------------------------------------------------------------------------
// ibCheckBox
// ----------------------------------------------------------------------------

class FRONTEND_API ibCheckBox : public ibControl
{
public:
    // checkbox constants
    enum State
    {
        State_Normal,
        State_Pressed,
        State_Disabled,
        State_Current,
        State_Max
    };

    enum Status
    {
        Status_Checked,
        Status_Unchecked,
        Status_3rdState,
        Status_Max
    };

    // constructors
    ibCheckBox() { Init(); }

    ibCheckBox(wxWindow *parent,
               wxWindowID id,
               const wxString& label,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxCheckBoxNameStr))
    {
        Init();

        Create(parent, id, label, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    // implement the checkbox interface
    virtual void SetValue(bool value);
    virtual bool GetValue() const;

    // wxCheckBoxBase shims (see the SEAM note in control.h)
    bool IsChecked() const { return GetValue(); }
    void Set3StateValue(wxCheckBoxState state) { DoSet3StateValue(state); }
    wxCheckBoxState Get3StateValue() const { return DoGet3StateValue(); }
    bool Is3State() const { return (GetWindowStyle() & wxCHK_3STATE) != 0; }
    bool Is3rdStateAllowedForUser() const {
        return (GetWindowStyle() & wxCHK_ALLOW_3RD_STATE_FOR_USER) != 0;
    }
    static void WXValidateStyle(long* WXUNUSED(style)) {}

    // set/get the bitmaps to use for the checkbox indicator
    void SetBitmap(const wxBitmap& bmp, State state, Status status);
    virtual wxBitmap GetBitmap(State state, Status status) const;

    // ibCheckBox actions
    void Toggle();
    virtual void Press();
    virtual void Release();
    virtual void ChangeValue(bool value);

    // overridden base class virtuals
    virtual bool IsPressed() const override { return m_isPressed; }

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1,
                               const wxString& strArg = wxEmptyString) override;

    virtual bool CanBeHighlighted() const override { return true; }
    virtual ibInputHandler *CreateStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return CreateStdInputHandler(handlerDef);
    }

protected:
    virtual void DoSet3StateValue(wxCheckBoxState state);
    virtual wxCheckBoxState DoGet3StateValue() const;

    virtual void DoDraw(ibControlRenderer *renderer) override;
    virtual wxSize DoGetBestClientSize() const override;

    // get the size of the bitmap using either the current one or the default
    // one (query renderer then)
    virtual wxSize GetBitmapSize() const;

    // common part of all ctors
    void Init();

    // send command event notifying about the checkbox state change
    virtual void SendEvent();

    // called when the checkbox becomes checked - radio button hook
    virtual void OnCheck();

    // get the state corresponding to the flags (combination of wxCONTROL_XXX)
    ibCheckBox::State GetState(int flags) const;

    // directly access the bitmaps array without trying to find a valid bitmap
    // to use as GetBitmap() does
    wxBitmap DoGetBitmap(State state, Status status) const
        { return m_bitmaps[state][status]; }

    // get the current status
    Status GetStatus() const { return m_status; }

private:
    // the current check status
    Status m_status;

    // the bitmaps to use for the different states
    wxBitmap m_bitmaps[State_Max][Status_Max];

    // is the checkbox currently pressed?
    bool m_isPressed;

    wxDECLARE_DYNAMIC_CLASS(ibCheckBox);
};

#endif // _WX_UNIV_CHECKBOX_H_
