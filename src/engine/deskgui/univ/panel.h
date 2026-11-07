// FORKED from wxWidgets: wxPanel (include/wx/panel.h, src/common/panelcmn.cpp), on the library's window
// (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/panel.h
// Purpose:     Base header for wxPanel
// Author:      Julian Smart
// Copyright:   (c) Julian Smart
//              (c) 2011 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows Licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PANEL_H_
#define _DESKGUI_UNIV_PANEL_H_

#include "deskgui/univ/window.h"

#include <wx/containr.h>

// ----------------------------------------------------------------------------
// ibPanel contains other controls and implements TAB traversal between them
// ----------------------------------------------------------------------------

// SEAM vs wx: the window under the navigation is the library's, so the panel draws with the theme and an ib menu
// shows over it (ibWindow::PopupMenu) - wx's panel had no native part to keep
class DESKGUI_API ibPanel : public wxNavigationEnabled<ibWindow>
{
public:
    ibPanel() = default;

    ibPanel(wxWindow *parent,
            wxWindowID winid = wxID_ANY,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxTAB_TRAVERSAL | wxNO_BORDER,
            const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        Create(parent, winid, pos, size, style, name);
    }

    // Pseudo ctor
    bool Create(wxWindow *parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxTAB_TRAVERSAL | wxNO_BORDER,
                const wxString& name = wxASCII_STR(wxPanelNameStr));


    // implementation from now on
    // --------------------------

    virtual void InitDialog() override;

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibPanel);
};

#endif // _DESKGUI_UNIV_PANEL_H_
