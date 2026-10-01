// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/inphand.cpp
// Purpose:     (trivial) ibInputHandler implementation
// Author:      Vadim Zeitlin
// Created:     18.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>


#ifndef WX_PRECOMP
#endif // WX_PRECOMP

#include "frontend/uikit/inputHandler.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibInputHandler
// ----------------------------------------------------------------------------

bool ibInputHandler::HandleMouseMove(ibInputConsumer * WXUNUSED(consumer),
                                     const wxMouseEvent& WXUNUSED(event))
{
    return false;
}

bool ibInputHandler::HandleFocus(ibInputConsumer *WXUNUSED(consumer),
                                 const wxFocusEvent& WXUNUSED(event))
{
    return false;
}

bool ibInputHandler::HandleActivation(ibInputConsumer *WXUNUSED(consumer),
                                      bool WXUNUSED(activated))
{
    return false;
}

ibInputHandler::~ibInputHandler()
{
}

