// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/radiobut.h
// Purpose:     ibRadioButton declaration
// Author:      Vadim Zeitlin
// Created:     10.09.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_RADIOBUT_H_
#define _WX_UNIV_RADIOBUT_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/checkBox.h"

// ----------------------------------------------------------------------------
// ibRadioButton
// ----------------------------------------------------------------------------

class FRONTEND_API ibRadioButton : public ibCheckBox
{
public:
    // constructors
    ibRadioButton() { Init(); }

    ibRadioButton(wxWindow *parent,
                  wxWindowID id,
                  const wxString& label,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = 0,
                  const wxValidator& validator = wxDefaultValidator,
                  const wxString& name = wxASCII_STR(wxRadioButtonNameStr))
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
                const wxString& name = wxASCII_STR(wxRadioButtonNameStr));

    // (re)implement pure virtuals from wxRadioButtonBase
    virtual void SetValue(bool value) override { return ibCheckBox::SetValue(value); }
    virtual bool GetValue() const override { return ibCheckBox::GetValue(); }

    // override some base class methods
    virtual void ChangeValue(bool value) override;

protected:
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // implement our own drawing
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // we use the radio button bitmaps for size calculation
    virtual wxSize GetBitmapSize() const override;

    // the radio button can only be cleared using this method, not
    // ChangeValue() above - and it is protected as it can only be called by
    // another radiobutton
    void ClearValue();

    // called when the radio button becomes checked: we clear all the buttons
    // in the same group with us here
    virtual void OnCheck() override;

    // send event about radio button selection
    virtual void SendEvent() override;

    // wxRadioButtonBase shims: group walking by the wxRB_GROUP convention
    // among the parent's children (the wx common helpers are bound to the
    // native wxRadioButton type)
    ibRadioButton *GetFirstInGroup() const;
    ibRadioButton *GetNextInGroup() const;
    ibRadioButton *GetLastInGroup() const;

private:
    wxDECLARE_DYNAMIC_CLASS(ibRadioButton);
};

#endif // _WX_UNIV_RADIOBUT_H_
