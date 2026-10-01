// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/tglbtn.h
// Purpose:     ibToggleButton for wxUniversal
// Author:      Vadim Zeitlin
// Modified by: David Bjorkevik
// Created:     16.05.06
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_TGLBTN_H_
#define _WX_UNIV_TGLBTN_H_

#include "frontend/frontend.h"

#include <wx/tglbtn.h>        // wxEVT_TOGGLEBUTTON lives in the native core
#include "frontend/uikit/ctrl/anyButton.h"

// ----------------------------------------------------------------------------
// ibToggleButton: a push button
// ----------------------------------------------------------------------------

// SEAM vs univ: wxToggleButtonBase sits on the native control chain
class FRONTEND_API ibToggleButton: public ibAnyButton
{
public:
    ibToggleButton();

    ibToggleButton(wxWindow *parent,
             wxWindowID id,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    // Create the control
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& lbl = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    virtual bool IsPressed() const override { return m_isPressed || m_value; }

    // ibToggleButton actions
    virtual void Toggle() override;
    virtual void Click() override;

    // Get/set the value
    void SetValue(bool state);
    bool GetValue() const { return m_value; }

protected:
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // the current value
    bool m_value;

private:
    // common part of all ctors
    void Init();

    wxDECLARE_DYNAMIC_CLASS(ibToggleButton);
};

#endif // _WX_UNIV_TGLBTN_H_
