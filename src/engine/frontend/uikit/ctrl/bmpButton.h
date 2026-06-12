// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

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

#include "frontend/frontend.h"

#include <wx/bmpbndl.h>

#include "frontend/uikit/ctrl/button.h"

// SEAM vs univ: wxBitmapButtonBase rides the native wxButton chain — derive
// from our ibButton; the per-state bitmap storage of wxAnyButtonBase is
// re-implemented via the shims below
class FRONTEND_API ibBitmapButton : public ibButton
{
public:
    // ---- wxAnyButtonBase per-state bitmap shims --------------------------
    enum State
    {
        State_Normal,
        State_Current,
        State_Pressed,
        State_Disabled,
        State_Focused,
        State_Max
    };

    void SetBitmapLabel(const wxBitmapBundle& bitmap)
        { m_bitmaps[State_Normal] = bitmap; OnSetBitmap(); }
    void SetBitmapPressed(const wxBitmapBundle& bitmap)
        { m_bitmaps[State_Pressed] = bitmap; OnSetBitmap(); }
    void SetBitmapFocus(const wxBitmapBundle& bitmap)
        { m_bitmaps[State_Focused] = bitmap; OnSetBitmap(); }
    void SetBitmapDisabled(const wxBitmapBundle& bitmap)
        { m_bitmaps[State_Disabled] = bitmap; OnSetBitmap(); }

    wxBitmap GetBitmapLabel() const
        { return m_bitmaps[State_Normal].GetBitmapFor(this); }
    wxBitmap GetBitmapPressed() const
        { return m_bitmaps[State_Pressed].GetBitmapFor(this); }
    wxBitmap GetBitmapFocus() const
        { return m_bitmaps[State_Focused].GetBitmapFor(this); }
    wxBitmap GetBitmapDisabled() const
        { return m_bitmaps[State_Disabled].GetBitmapFor(this); }
    // ----------------------------------------------------------------------

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

    virtual bool Enable(bool enable = true) override;

    virtual bool WXMakeCurrent(bool doit = true) override;

    virtual void Press() override;
    virtual void Release() override;

protected:
    void OnSetFocus(wxFocusEvent& event);
    void OnKillFocus(wxFocusEvent& event);

    // called when one of the bitmaps is changed by user (the virtual lived
    // in the lost wxAnyButtonBase)
    virtual void OnSetBitmap();

    // set bitmap to the given one if it's ok or to the normal bitmap and
    // return true if the bitmap really changed
    bool ChangeBitmap(const wxBitmap& bmp);

    // per-state bitmaps (lived in the lost wxAnyButtonBase)
    wxBitmapBundle m_bitmaps[State_Max];

private:
    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibBitmapButton);
};

#endif // _WX_UNIV_BMPBUTTN_H_

