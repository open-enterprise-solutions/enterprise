// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/anybutton.h
// Purpose:     ibAnyButton class
// Author:      Vadim Zeitlin
// Created:     2000-08-15 (extracted from button.h)
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_ANYBUTTON_H_
#define _WX_UNIV_ANYBUTTON_H_

#include "frontend/frontend.h"

#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/ctrl/control.h"
// ----------------------------------------------------------------------------
// Common button functionality
// ----------------------------------------------------------------------------

// SEAM vs univ: wxAnyButtonBase sits on the native wxControl in our build
class FRONTEND_API ibAnyButton : public ibControl
{
public:
    ibAnyButton() = default;

    virtual ~ibAnyButton() = default;

    // ibAnyButton actions
    virtual void Toggle();
    virtual void Press();
    virtual void Release();
    virtual void Click(){}

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

public:
    // every button kind highlights on hover/press (lived only on ibButton
    // before — the toggle button never got its pressed background)
    virtual bool CanBeHighlighted() const override { return true; }

protected:
    // choose the default border for this window (drawn by the NC pass —
    // the ONLY frame of the button, see the SEAM note in DoDraw)
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_STATIC; }

    virtual wxSize DoGetBestClientSize() const override;

    virtual bool DoDrawBackground(wxDC& dc) override;
    virtual void DoDraw(ibControlRenderer *renderer) override;
    // current state
    bool m_isPressed,
         m_isDefault;

    // the (optional) image to show and the margins around it
    wxBitmap m_bitmap;
    wxCoord  m_marginBmpX,
             m_marginBmpY;

private:
    wxDECLARE_NO_COPY_CLASS(ibAnyButton);
};

// ----------------------------------------------------------------------------
// ibStdAnyButtonInputHandler: translates SPACE and ENTER keys and the left mouse
// click into button press/release actions
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdAnyButtonInputHandler : public ibStdInputHandler
{
public:
    ibStdAnyButtonInputHandler(ibInputHandler *inphand);

    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed) override;
    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event) override;
    virtual bool HandleMouseMove(ibInputConsumer *consumer,
                                 const wxMouseEvent& event) override;
    virtual bool HandleFocus(ibInputConsumer *consumer,
                             const wxFocusEvent& event) override;
    virtual bool HandleActivation(ibInputConsumer *consumer, bool activated) override;

private:
    // the window (button) which has capture or nullptr and the flag telling if
    // the mouse is inside the button which captured it or not
    ibWindow *m_winCapture;
    bool      m_winHasMouse;
};

#endif // _WX_UNIV_ANYBUTTON_H_
