// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/spinbutt.cpp
// Purpose:     implementation of the universal version of ibSpinButton
// Author:      Vadim Zeitlin
// Created:     21.01.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <wx/wxprec.h>

#include "frontend/uikit/ctrl/spinButton.h"


#ifndef WX_PRECOMP
#endif

#include <wx/spinbutt.h>

#if wxUSE_SPINBTN

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/theme.h"

// ============================================================================
// implementation of ibSpinButton
// ============================================================================

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

#ifdef __VISUALC__
    // warning C4355: 'this' : used in base member initializer list
    #pragma warning(disable:4355)  // so what? disable it...
#endif

ibSpinButton::ibSpinButton()
            : m_arrows(this)
{
    Init();
}

ibSpinButton::ibSpinButton(wxWindow *parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
            : m_arrows(this)
{
    Init();

    (void)Create(parent, id, pos, size, style, name);
}

#ifdef __VISUALC__
    // warning C4355: 'this' : used in base member initializer list
    #pragma warning(default:4355)
#endif

void ibSpinButton::Init()
{
    // wxSpinButtonBase defaults
    m_min = 0;
    m_max = 100;
    for ( size_t n = 0; n < WXSIZEOF(m_arrowsState); n++ )
    {
        m_arrowsState[n] = 0;
    }

    m_value = 0;
}

bool ibSpinButton::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    // the spin buttons never have the border
    style &= ~wxBORDER_MASK;

    if ( !ibControl::Create(parent, id, pos, size, style,
                            wxDefaultValidator, name) )
        return false;

    SetInitialSize(size);

    CreateInputHandler(ibINP_HANDLER_SPINBTN);

    return true;
}

// ----------------------------------------------------------------------------
// value access
// ----------------------------------------------------------------------------

void ibSpinButton::SetRange(int minVal, int maxVal)
{
    m_min = minVal;
    m_max = maxVal;

    // because the arrows disabled state might have changed - we don't check if
    // it really changed or not because SetRange() is called rarely enough and
    // an extra refresh here doesn't really hurt
    Refresh();
}

int ibSpinButton::GetValue() const
{
    return m_value;
}

void ibSpinButton::SetValue(int val)
{
    if ( val != m_value )
    {
        m_value = val;

        Refresh();
    }
}

int ibSpinButton::NormalizeValue(int value) const
{
    if ( value > m_max )
    {
        if ( GetWindowStyleFlag() & wxSP_WRAP )
            value = m_min + (value - m_max - 1) % (m_max - m_min + 1);
        else
            value = m_max;
    }
    else if ( value < m_min )
    {
        if ( GetWindowStyleFlag() & wxSP_WRAP )
            value = m_max - (m_min - value - 1) % (m_max - m_min + 1);
        else
            value = m_min;
    }

    return value;
}

bool ibSpinButton::ChangeValue(int inc)
{
    int valueNew = NormalizeValue(m_value + inc);

    if ( valueNew == m_value )
    {
        // nothing changed - most likely because we are already at min/max
        // value
        return false;
    }

    wxSpinEvent event(inc > 0 ? wxEVT_SCROLL_LINEUP : wxEVT_SCROLL_LINEDOWN,
                      GetId());
    event.SetPosition(valueNew);
    event.SetEventObject(this);

    if ( GetEventHandler()->ProcessEvent(event) && !event.IsAllowed() )
    {
        // program has vetoed the event
        return false;
    }

    m_value = valueNew;

    // send wxEVT_SCROLL_THUMBTRACK as well
    event.SetEventType(wxEVT_SCROLL_THUMBTRACK);
    (void)GetEventHandler()->ProcessEvent(event);

    return true;
}

// ----------------------------------------------------------------------------
// size calculations
// ----------------------------------------------------------------------------

wxSize ibSpinButton::DoGetBestClientSize() const
{
    // a spin button has by default the same size as two scrollbar arrows put
    // together
    wxSize size = m_renderer->GetScrollbarArrowSize();
    if ( IsVertical() )
    {
        size.y *= 2;
    }
    else
    {
        size.x *= 2;
    }

    return size;
}

// ----------------------------------------------------------------------------
// ibControlWithArrows methods
// ----------------------------------------------------------------------------

int ibSpinButton::GetArrowState(ibScrollArrows::Arrow arrow) const
{
    int state = m_arrowsState[arrow];

    // the arrow may also be disabled: either because the control is completely
    // disabled
    bool disabled = !IsEnabled();

    if ( !disabled && !(GetWindowStyleFlag() & wxSP_WRAP) )
    {
        // ... or because we can't go any further - note that this never
        // happens if we just wrap
        if ( IsVertical() )
        {
            if ( arrow == ibScrollArrows::Arrow_First )
                disabled = m_value == m_max;
            else
                disabled = m_value == m_min;
        }
        else // horizontal
        {
            if ( arrow == ibScrollArrows::Arrow_First )
                disabled = m_value == m_min;
            else
                disabled = m_value == m_max;
        }
    }

    if ( disabled )
    {
        state |= wxCONTROL_DISABLED;
    }

    return state;
}

void ibSpinButton::SetArrowFlag(ibScrollArrows::Arrow arrow, int flag, bool set)
{
    int state = m_arrowsState[arrow];
    if ( set )
        state |= flag;
    else
        state &= ~flag;

    if ( state != m_arrowsState[arrow] )
    {
        m_arrowsState[arrow] = state;
        Refresh();
    }
}

bool ibSpinButton::OnArrow(ibScrollArrows::Arrow arrow)
{
    int valueOld = GetValue();

    ibControlAction action;
    if ( arrow == ibScrollArrows::Arrow_First )
        action = IsVertical() ? ibACTION_SPIN_INC : ibACTION_SPIN_DEC;
    else
        action = IsVertical() ? ibACTION_SPIN_DEC : ibACTION_SPIN_INC;

    PerformAction(action);

    // did we scroll to the end?
    return GetValue() != valueOld;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ibSpinButton::DoDraw(ibControlRenderer *renderer)
{
    wxRect rectArrow1, rectArrow2;
    CalcArrowRects(&rectArrow1, &rectArrow2);

    wxDC& dc = renderer->GetDC();
    m_arrows.DrawArrow(ibScrollArrows::Arrow_First, dc, rectArrow1);
    m_arrows.DrawArrow(ibScrollArrows::Arrow_Second, dc, rectArrow2);
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

void ibSpinButton::CalcArrowRects(wxRect *rect1, wxRect *rect2) const
{
    // calculate the rectangles for both arrows: note that normally the 2
    // arrows are adjacent to each other but if the total control width/height
    // is odd, we can have 1 pixel between them
    wxRect rectTotal = GetClientRect();

    *rect1 =
    *rect2 = rectTotal;
    if ( IsVertical() )
    {
        rect1->height /= 2;
        rect2->height /= 2;

        rect2->y += rect1->height;
        if ( rectTotal.height % 2 )
            rect2->y++;
    }
    else // horizontal
    {
        rect1->width /= 2;
        rect2->width /= 2;

        rect2->x += rect1->width;
        if ( rectTotal.width % 2 )
            rect2->x++;
    }
}

ibScrollArrows::Arrow ibSpinButton::HitTestArrow(const wxPoint& pt) const
{
    wxRect rectArrow1, rectArrow2;
    CalcArrowRects(&rectArrow1, &rectArrow2);

    if ( rectArrow1.Contains(pt) )
        return ibScrollArrows::Arrow_First;
    else if ( rectArrow2.Contains(pt) )
        return ibScrollArrows::Arrow_Second;
    else
        return ibScrollArrows::Arrow_None;
}

// ----------------------------------------------------------------------------
// input processing
// ----------------------------------------------------------------------------

bool ibSpinButton::PerformAction(const ibControlAction& action,
                                 long numArg,
                                 const wxString& strArg)
{
    if ( action == ibACTION_SPIN_INC )
        ChangeValue(+1);
    else if ( action == ibACTION_SPIN_DEC )
        ChangeValue(-1);
    else
        return ibControl::PerformAction(action, numArg, strArg);

    return true;
}

/* static */
ibInputHandler *ibSpinButton::GetStdInputHandler(ibInputHandler *handlerDef)
{
    static ibStdSpinButtonInputHandler s_handler(handlerDef);

    return &s_handler;
}

// ----------------------------------------------------------------------------
// ibStdSpinButtonInputHandler
// ----------------------------------------------------------------------------

ibStdSpinButtonInputHandler::
ibStdSpinButtonInputHandler(ibInputHandler *inphand)
    : ibStdInputHandler(inphand)
{
}

bool ibStdSpinButtonInputHandler::HandleKey(ibInputConsumer *consumer,
                                            const wxKeyEvent& event,
                                            bool pressed)
{
    if ( pressed )
    {
        ibControlAction action;
        switch ( event.GetKeyCode() )
        {
            case WXK_DOWN:
            case WXK_RIGHT:
                action = ibACTION_SPIN_DEC;
                break;

            case WXK_UP:
            case WXK_LEFT:
                action = ibACTION_SPIN_INC;
                break;
        }

        if ( !action.IsEmpty() )
        {
            consumer->PerformAction(action);

            return true;
        }
    }

    return ibStdInputHandler::HandleKey(consumer, event, pressed);
}

bool ibStdSpinButtonInputHandler::HandleMouse(ibInputConsumer *consumer,
                                              const wxMouseEvent& event)
{
    ibSpinButton *spinbtn = wxStaticCast(consumer->GetInputWindow(), ibSpinButton);

    if ( spinbtn->GetArrows().HandleMouse(event) )
    {
        // don't refresh, everything is already done
        return false;
    }

    return ibStdInputHandler::HandleMouse(consumer, event);
}

bool ibStdSpinButtonInputHandler::HandleMouseMove(ibInputConsumer *consumer,
                                                  const wxMouseEvent& event)
{
    ibSpinButton *spinbtn = wxStaticCast(consumer->GetInputWindow(), ibSpinButton);

    if ( spinbtn->GetArrows().HandleMouseMove(event) )
    {
        // processed by the arrows
        return false;
    }

    return ibStdInputHandler::HandleMouseMove(consumer, event);
}


wxIMPLEMENT_DYNAMIC_CLASS(ibSpinButton, ibControl);

#endif // wxUSE_SPINBTN
