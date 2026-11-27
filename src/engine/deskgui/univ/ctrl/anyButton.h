// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/anybutton.h
// Purpose:     ibAnyButton class
// Author:      Vadim Zeitlin
// Created:     2000-08-15 (extracted from button.h)
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_ANYBUTTON_H_
#define _DESKGUI_UNIV_ANYBUTTON_H_

#include "deskgui/deskgui.h"

#include "deskgui/univ/inputHandler.h"
#include "deskgui/univ/ctrl/control.h"
// ----------------------------------------------------------------------------
// Common button functionality
// ----------------------------------------------------------------------------

class ibStdAnyButtonInputHandler;

// SEAM vs univ: wxAnyButtonBase sits on the native wxControl in our build
class DESKGUI_API ibAnyButton : public ibControl
{
public:
    ibAnyButton() = default;

    virtual ~ibAnyButton();

    // ibAnyButton actions
    virtual void Toggle();
    virtual void Press();
    virtual void Release();
    virtual void Click(){}

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1,
                               const wxString& strArg = wxEmptyString) override;

    // every button's one handler (and a check box's, whose own hands the mouse on to it): the one that holds the pressed
    static ibStdAnyButtonInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override;

public:
    // every button kind highlights on hover/press (lived only on ibButton
    // before — the toggle button never got its pressed background)
    virtual bool CanBeHighlighted() const override { return true; }

    // the button's face: the theme's surface in its state, or its background bitmap
    virtual void EraseBackground(wxDC& dc, const wxRect& rect) override;

protected:
    // choose the default border for this window (drawn by ibWindow::TryBefore —
    // the ONLY frame of the button, see the SEAM note in OnPaint)
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_STATIC; }

    virtual wxSize DoGetBestClientSize() const override;

    void OnPaint(wxPaintEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);

    // current state
    bool m_isPressed,
         m_isDefault;

    // the (optional) image to show and the margins around it
    wxBitmap m_bitmap;
    wxCoord  m_marginBmpX,
             m_marginBmpY;

private:
    wxDECLARE_NO_COPY_CLASS(ibAnyButton);
    wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibStdAnyButtonInputHandler: translates SPACE and ENTER keys and the left mouse
// click into button press/release actions
// ----------------------------------------------------------------------------

class DESKGUI_API ibStdAnyButtonInputHandler : public ibStdInputHandler
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

    // stop what a press on the window started - its capture, its pressed look: the window lost the mouse or is being
    // destroyed (the OnCaptureLost and the destructor of ibAnyButton and of ibCheckBox, whose handler hands the mouse
    // on to this one). The handler is every button's and holds one at a time: for another one it does nothing
    void StopPressing(ibInputConsumer *consumer);

private:
    // the window (button) which has capture or nullptr and the flag telling if
    // the mouse is inside the button which captured it or not
    ibWindow *m_winCapture;
    bool      m_winHasMouse;
};

#endif // _DESKGUI_UNIV_ANYBUTTON_H_
