// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/button.h
// Purpose:     ibButton for wxUniversal
// Author:      Vadim Zeitlin
// Created:     15.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_BUTTON_H_
#define _WX_UNIV_BUTTON_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/anyButton.h"
#include <wx/anybutton.h>      // wxAnyButtonBase::State

// ----------------------------------------------------------------------------
// the actions supported by this control
// ----------------------------------------------------------------------------
//checkbox.cpp needed it, so not move it to anybutton.h
#define ibACTION_BUTTON_TOGGLE  wxT("toggle")    // press/release the button
#define ibACTION_BUTTON_PRESS   wxT("press")     // press the button
#define ibACTION_BUTTON_RELEASE wxT("release")   // release the button
#define ibACTION_BUTTON_CLICK   wxT("click")     // generate button click event

// ----------------------------------------------------------------------------
// ibButton: a push button
// ----------------------------------------------------------------------------

// SEAM vs univ: wxButtonBase sits on the native wxAnyButton in our build
class FRONTEND_API ibButton : public ibAnyButton
{
public:
    ibButton() { Init(); }
    ibButton(wxWindow *parent,
             wxWindowID id,
             const wxBitmapBundle& bitmap,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        Init();

        Create(parent, id, bitmap, label, pos, size, style, validator, name);
    }

    ibButton(wxWindow *parent,
             wxWindowID id,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        Init();

        Create(parent, id, label, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        return Create(parent, id, wxNullBitmap, label,
                      pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxBitmapBundle& bitmap,
                const wxString& label = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr));

    virtual ~ibButton();

    using State = wxAnyButtonBase::State;

    // wxButtonBase shims
    void SetBitmap(const wxBitmapBundle& bitmap) { DoSetBitmap(bitmap, wxAnyButtonBase::State_Normal); }
    static wxSize GetDefaultSize(wxWindow* win = nullptr);

    // a primary-action button is drawn as the default one (palette accent)
    void SetPrimary(bool primary = true) { m_isDefault = primary; Refresh(); }

    virtual ibWindow *SetDefault();

    virtual bool IsPressed() const override { return m_isPressed; }
    virtual bool IsDefault() const override { return m_isDefault; }

    // ibButton actions
    virtual void Click() override;

    virtual bool CanBeHighlighted() const override { return true; }



protected:
    virtual void DoSetBitmap(const wxBitmapBundle& bitmap, State which);
    virtual wxBitmap DoGetBitmap(State which) const;
    virtual void DoSetBitmapMargins(wxCoord x, wxCoord y);

    // common part of all ctors
    void Init();

private:
    wxDECLARE_DYNAMIC_CLASS(ibButton);
};

#endif // _WX_UNIV_BUTTON_H_

