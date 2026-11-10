// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/anybutton.cpp
// Purpose:     ibAnyButton
// Author:      Vadim Zeitlin
// Created:     2014-03-26 (extracted from button.cpp and tglbtn.cpp)
// Copyright:   (c) 2014
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/anyButton.h"
#include "deskgui/univ/ctrl/button.h"



#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
#endif

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibAnyButton, ibControl)
    EVT_PAINT(ibAnyButton::OnPaint)
wxEND_EVENT_TABLE()

void ibAnyButton::Press()
{
    if ( !m_isPressed )
    {
        m_isPressed = true;

        Refresh();
    }
}

void ibAnyButton::Release()
{
    if ( m_isPressed )
    {
        m_isPressed = false;

        Refresh();
    }
}

void ibAnyButton::Toggle()
{
    if ( m_isPressed )
        Release();
    else
        Press();

    if ( !m_isPressed )
    {
        // releasing button after it had been pressed generates a click event
        Click();
    }
    Refresh();
}

bool ibAnyButton::PerformAction(const ibControlAction& action,
                             long numArg,
                             const wxString& strArg)
{
    if ( action == ibACTION_BUTTON_TOGGLE )
        Toggle();
    else if ( action == ibACTION_BUTTON_CLICK )
        Click();
    else if ( action == ibACTION_BUTTON_PRESS )
        Press();
    else if ( action == ibACTION_BUTTON_RELEASE )
        Release();
    else
        return ibControl::PerformAction(action, numArg, strArg);

    return true;
}

/* static */
ibInputHandler *ibAnyButton::GetStdInputHandler(ibInputHandler *handlerDef)
{
    static ibStdAnyButtonInputHandler s_handlerBtn(handlerDef);

    return &s_handlerBtn;
}

// ----------------------------------------------------------------------------
// size management
// ----------------------------------------------------------------------------

wxSize ibAnyButton::DoGetBestClientSize() const
{
    wxInfoDC dc(wxConstCast(this, ibAnyButton));
    wxCoord width, height;
    dc.GetMultiLineTextExtent(GetLabel(), &width, &height);

    if ( m_bitmap.IsOk() )
    {
        // allocate extra space for the bitmap
        wxCoord heightBmp = m_bitmap.GetHeight() + 2*m_marginBmpY;
        if ( height < heightBmp )
            height = heightBmp;

        width += m_bitmap.GetWidth() + 2*m_marginBmpX;
    }

    // SEAM vs univ (sizing rules per the design directive):
    //  - the HEIGHT of every default-sized button is the standard one — the
    //    uniform control-row height;
    //  - the WIDTH follows the label (plus breathing room) and is never less
    //    than the standard one, as univ's and wxMSW's; a picture alone keeps
    //    its own width
    if ( !(GetWindowStyle() & wxBU_EXACTFIT) )
    {
        const wxSize szDef = ibButton::GetDefaultSize();

        if ( width > 0 )
            width += 2*dc.GetCharWidth();   // label padding
        if ( width < szDef.x && !(m_bitmap.IsOk() && GetLabel().empty()) )
            width = szDef.x;                // the standard width at least

        if ( height < szDef.y )
            height = szDef.y;
    }

    return wxSize(width, height);
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ibAnyButton::EraseBackground(wxDC& dc, const wxRect& rect)
{
    if ( GetBackgroundBitmap().IsOk() )
    {
        // get the bitmap and the flags
        int alignment;
        wxStretch stretch;
        wxBitmap bmp = GetBackgroundBitmap(&alignment, &stretch);
        ibRenderer::DrawBitmap(dc, bmp, rect, alignment, stretch);
    }
    else
    {
        m_renderer->DrawButtonSurface(dc, wxTHEME_BG_COLOUR(this),
                                      rect, GetStateFlags());
    }
}

void ibAnyButton::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    // SEAM vs univ: the canon drew DrawButtonBorder here ON TOP of the
    // STATIC window border (ibWindow::TryBefore) — two flat 1px frames side by
    // side read as one fat border. The window border is the only frame now
    // (the theme highlights it for the default/focused button itself).
    const wxString label = GetLabelText();
    if ( label.empty() && !m_bitmap.IsOk() )
        return;

    wxRect rectLabel(GetClientSize());
    if ( m_bitmap.IsOk() )
        rectLabel.Inflate(-m_marginBmpX, -m_marginBmpY);

    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());
    GetRenderer()->DrawButtonLabel(dc, label, m_bitmap, rectLabel, GetStateFlags(), GetAlignment(), GetAccelIndex());
}

// ============================================================================
// ibStdAnyButtonInputHandler
// ============================================================================

ibStdAnyButtonInputHandler::ibStdAnyButtonInputHandler(ibInputHandler *handler)
                       : ibStdInputHandler(handler)
{
    m_winCapture = nullptr;
    m_winHasMouse = false;
}

bool ibStdAnyButtonInputHandler::HandleKey(ibInputConsumer *consumer,
                                        const wxKeyEvent& event,
                                        bool pressed)
{
    int keycode = event.GetKeyCode();
    if ( keycode == WXK_SPACE || keycode == WXK_RETURN || keycode == WXK_NUMPAD_ENTER )
    {
        // SEAM vs univ: the key presses and its release clicks, as the mouse does. The canon toggled on both, so a
        // held key's repeats clicked over and over, and a check box (whose handler falls back to this one) was
        // checked on the key's down and unchecked on its up.
        if ( pressed )
            consumer->PerformAction(ibACTION_BUTTON_PRESS);
        else if ( consumer->GetInputWindow()->IsPressed() )
            consumer->PerformAction(ibACTION_BUTTON_TOGGLE);

        return true;
    }

    return ibStdInputHandler::HandleKey(consumer, event, pressed);
}

bool ibStdAnyButtonInputHandler::HandleMouse(ibInputConsumer *consumer,
                                          const wxMouseEvent& event)
{
    // the button has 2 states: pressed and normal with the following
    // transitions between them:
    //
    //      normal -> left down -> capture mouse and go to pressed state
    //      pressed -> left up inside -> generate click -> go to normal
    //                         outside ------------------>
    //
    // the other mouse buttons are ignored
    if ( event.Button(1) )
    {
        if ( event.LeftDown() || event.LeftDClick() )
        {
            m_winCapture = consumer->GetInputWindow();
            m_winCapture->CaptureMouse();
            m_winHasMouse = true;

            consumer->PerformAction(ibACTION_BUTTON_PRESS);
        }
        else if ( event.LeftUp() )
        {
            if ( m_winCapture )
            {
                m_winCapture->ReleaseMouse();
                m_winCapture = nullptr;
            }

            if ( m_winHasMouse )
            {
                // this will generate a click event
                consumer->PerformAction(ibACTION_BUTTON_TOGGLE);

                return true;
            }
            //else: the mouse was released outside the window, this doesn't
            //      count as a click
        }
        //else: don't do anything special about the double click
    }

    return ibStdInputHandler::HandleMouse(consumer, event);
}

bool ibStdAnyButtonInputHandler::HandleMouseMove(ibInputConsumer *consumer,
                                              const wxMouseEvent& event)
{
    // we only have to do something when the mouse leaves/enters the pressed
    // button and don't care about the other ones
    if ( event.GetEventObject() == m_winCapture )
    {
        // leaving the button should remove its pressed state
        if ( event.Leaving() )
        {
            // remember that the mouse is now outside
            m_winHasMouse = false;

            // we do have a pressed button, so release it
            consumer->GetInputWindow()->WXMakeCurrent(false);
            consumer->PerformAction(ibACTION_BUTTON_RELEASE);

            return true;
        }
        // and entering it back should make it pressed again if it had been
        // pressed
        else if ( event.Entering() )
        {
            // the mouse is (back) inside the button
            m_winHasMouse = true;

            // we did have a pressed button which we released when leaving the
            // window, press it again
            consumer->GetInputWindow()->WXMakeCurrent(true);
            consumer->PerformAction(ibACTION_BUTTON_PRESS);

            return true;
        }
    }

    return ibStdInputHandler::HandleMouseMove(consumer, event);
}

bool ibStdAnyButtonInputHandler::HandleFocus(ibInputConsumer * WXUNUSED(consumer),
                                          const wxFocusEvent& WXUNUSED(event))
{
    // buttons change appearance when they get/lose focus, so return true to
    // refresh
    return true;
}

bool ibStdAnyButtonInputHandler::HandleActivation(ibInputConsumer *consumer,
                                               bool WXUNUSED(activated))
{
    // the default button changes appearance when the app is [de]activated, so
    // return true to refresh
    return wxStaticCast(consumer->GetInputWindow(), ibAnyButton)->IsDefault();
}
