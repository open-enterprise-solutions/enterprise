// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/spinbutt.h
// Purpose:     universal version of ibSpinButton
// Author:      Vadim Zeitlin
// Created:     21.01.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_SPINBUTT_H_
#define _WX_UNIV_SPINBUTT_H_

#include "frontend/frontend.h"

class ibWindow;

#include "frontend/uikit/ctrl/scrollArrow.h"

// ----------------------------------------------------------------------------
// ibSpinButton
// ----------------------------------------------------------------------------

// actions supported by this control
#define ibACTION_SPIN_INC    wxT("inc")
#define ibACTION_SPIN_DEC    wxT("dec")

#include <wx/spinbutt.h>      // wxSP_* styles, wxSpinEvent
#include "frontend/uikit/ctrl/control.h"

// SEAM vs univ: wxSpinButtonBase sits on the native wxControl in our build
class FRONTEND_API ibSpinButton : public ibControl,
                                 public ibControlWithArrows
{
public:
    ibSpinButton();
    ibSpinButton(wxWindow *parent,
                 wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = wxSP_VERTICAL | wxSP_ARROW_KEYS,
                 const wxString& name = wxSPIN_BUTTON_NAME);

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_VERTICAL | wxSP_ARROW_KEYS,
                const wxString& name = wxSPIN_BUTTON_NAME);

    // implement wxSpinButtonBase methods
    // wxSpinButtonBase shims (see the SEAM note above)
    int GetMin() const { return m_min; }
    int GetMax() const { return m_max; }

    virtual int GetValue() const;
    virtual void SetValue(int val);
    virtual void SetRange(int minVal, int maxVal);

    // implement ibControlWithArrows methods
    virtual ibRenderer *GetRenderer() const override { return m_renderer; }
    virtual ibWindow *GetWindow() override { return this; }
    virtual bool IsVertical() const override { return ((GetWindowStyle() & wxSP_VERTICAL) != 0); }
    virtual int GetArrowState(ibScrollArrows::Arrow arrow) const override;
    virtual void SetArrowFlag(ibScrollArrows::Arrow arrow, int flag, bool set) override;
    virtual bool OnArrow(ibScrollArrows::Arrow arrow) override;
    virtual ibScrollArrows::Arrow HitTestArrow(const wxPoint& pt) const override;

    // for ibStdSpinButtonInputHandler
    const ibScrollArrows& GetArrows() { return m_arrows; }

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = 0,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

protected:
    virtual wxSize DoGetBestClientSize() const override;
    virtual void DoDraw(ibControlRenderer *renderer) override;
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // the common part of all ctors
    void Init();

    // normalize the value to fit into min..max range
    int NormalizeValue(int value) const;

    // change the value by +1/-1 and send the event, return true if value was
    // changed
    bool ChangeValue(int inc);

    // get the rectangles for our 2 arrows
    void CalcArrowRects(wxRect *rect1, wxRect *rect2) const;

    // the current controls value
    int m_value;

private:
    // the object which manages our arrows
    ibScrollArrows m_arrows;

    // the state (combination of wxCONTROL_XXX flags) of the arrows
    int m_arrowsState[ibScrollArrows::Arrow_Max];

    // range state (lived in wxSpinButtonBase before the rebase)
    int m_min;
    int m_max;

    wxDECLARE_DYNAMIC_CLASS(ibSpinButton);
};

// ----------------------------------------------------------------------------
// ibStdSpinButtonInputHandler: manages clicks on them (use arrows like
// ibStdScrollBarInputHandler) and processes keyboard events too
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdSpinButtonInputHandler : public ibStdInputHandler
{
public:
    ibStdSpinButtonInputHandler(ibInputHandler *inphand);

    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed) override;
    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event) override;
    virtual bool HandleMouseMove(ibInputConsumer *consumer,
                                 const wxMouseEvent& event) override;
};

#endif // _WX_UNIV_SPINBUTT_H_

