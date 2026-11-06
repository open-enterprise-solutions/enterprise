// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/bmpbuttn.h
// Purpose:     ibBitmapButton class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     25.08.00
// Copyright:   (c) Vadim Zeitlin
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_BMPBUTTN_H_
#define _WX_UNIV_BMPBUTTN_H_

#include "gui/gui.h"

#include <wx/bmpbndl.h>

#include "gui/univ/ctrl/button.h"

// SEAM vs univ: wxBitmapButtonBase rides the native wxButton chain — derive from our ibButton. The bitmap of each
// state is ibButton's (it draws the one its state asks for), as wxAnyButtonBase keeps them for wx's own buttons; this
// one is a button of its bitmap alone, as small as it.
class GUI_API ibBitmapButton : public ibButton
{
public:
    ibBitmapButton() = default;

    ibBitmapButton(wxWindow *parent,
                   wxWindowID id,
                   const wxBitmapBundle& bitmap,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = 0,
                   const wxValidator& validator = wxDefaultValidator,
                   const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        Create(parent, id, bitmap, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxBitmapBundle& bitmap,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr));

    // wxBitmapButton's per-state bitmaps
    void SetBitmapLabel(const wxBitmapBundle& bitmap) { DoSetBitmap(bitmap, wxAnyButtonBase::State_Normal); }
    void SetBitmapPressed(const wxBitmapBundle& bitmap) { DoSetBitmap(bitmap, wxAnyButtonBase::State_Pressed); }
    void SetBitmapFocus(const wxBitmapBundle& bitmap) { DoSetBitmap(bitmap, wxAnyButtonBase::State_Focused); }
    void SetBitmapDisabled(const wxBitmapBundle& bitmap) { DoSetBitmap(bitmap, wxAnyButtonBase::State_Disabled); }

    wxBitmap GetBitmapLabel() const { return DoGetBitmap(wxAnyButtonBase::State_Normal); }
    wxBitmap GetBitmapPressed() const { return DoGetBitmap(wxAnyButtonBase::State_Pressed); }
    wxBitmap GetBitmapFocus() const { return DoGetBitmap(wxAnyButtonBase::State_Focused); }
    wxBitmap GetBitmapDisabled() const { return DoGetBitmap(wxAnyButtonBase::State_Disabled); }

private:
    wxDECLARE_DYNAMIC_CLASS(ibBitmapButton);
};

#endif // _WX_UNIV_BMPBUTTN_H_
