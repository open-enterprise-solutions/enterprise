// FORKED from wxWidgets: wxPanel (src/common/panelcmn.cpp) - see panel.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/panelcmn.cpp
// Purpose:     wxPanel common code
// Author:      Julian Smart, Robert Roebling, Vadim Zeitlin
// Created:     04/01/98
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/panel.h"

// SEAM vs wx: no XTI tables - the library's classes are not streamed
wxIMPLEMENT_DYNAMIC_CLASS(ibPanel, ibWindow);

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibPanel creation
// ----------------------------------------------------------------------------

bool ibPanel::Create(wxWindow *parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxString& name)
{
    if ( !ibWindow::Create(parent, id, pos, size, style, name) )
        return false;

    // so that non-solid background renders correctly under GTK+:
    SetThemeEnabled(true);

    return true;
}

void ibPanel::InitDialog()
{
    wxInitDialogEvent event(GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
