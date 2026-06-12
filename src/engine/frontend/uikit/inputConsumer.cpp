// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/inpcons.cpp
// Purpose:     ibInputConsumer: mix-in class for input handling
// Author:      Vadim Zeitlin
// Created:     14.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <wx/wxprec.h>

#include "frontend/uikit/inputConsumer.h"


#ifndef WX_PRECOMP
    #include <wx/window.h>
#endif // WX_PRECOMP

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/theme.h"
#include "frontend/uikit/window.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// focus/activation handling
// ----------------------------------------------------------------------------

void ibInputConsumer::OnFocus(wxFocusEvent& event)
{
    if ( m_inputHandler && m_inputHandler->HandleFocus(this, event) )
        GetInputWindow()->Refresh();
    else
        event.Skip();
}

void ibInputConsumer::OnActivate(wxActivateEvent& event)
{
    if ( m_inputHandler && m_inputHandler->HandleActivation(this, event.GetActive()) )
        GetInputWindow()->Refresh();
    else
        event.Skip();
}

// ----------------------------------------------------------------------------
// input processing
// ----------------------------------------------------------------------------

ibInputHandler *
ibInputConsumer::DoGetStdInputHandler(ibInputHandler * WXUNUSED(handlerDef))
{
    return nullptr;
}

void ibInputConsumer::CreateInputHandler(const wxString& inphandler)
{
    m_inputHandler = ibThemeEngine::Get()->GetInputHandler(inphandler, this);
}

void ibInputConsumer::OnKeyDown(wxKeyEvent& event)
{
    if ( !m_inputHandler || !m_inputHandler->HandleKey(this, event, true) )
        event.Skip();
}

void ibInputConsumer::OnKeyUp(wxKeyEvent& event)
{
    if ( !m_inputHandler || !m_inputHandler->HandleKey(this, event, false) )
        event.Skip();
}

void ibInputConsumer::OnMouse(wxMouseEvent& event)
{
    if ( m_inputHandler )
    {
        if ( event.Moving() || event.Dragging() ||
                event.Entering() || event.Leaving() )
        {
            if ( m_inputHandler->HandleMouseMove(this, event) )
                return;
        }
        else // a click action
        {
            if ( m_inputHandler->HandleMouse(this, event) )
                return;
        }
    }

    event.Skip();
}

// ----------------------------------------------------------------------------
// the actions
// ----------------------------------------------------------------------------

bool ibInputConsumer::PerformAction(const ibControlAction& WXUNUSED(action),
                                    long WXUNUSED(numArg),
                                    const wxString& WXUNUSED(strArg))
{
    return false;
}
