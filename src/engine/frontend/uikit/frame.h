// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/frame.h
// Purpose:     ibFrame class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     19.05.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_FRAME_H_
#define _WX_UNIV_FRAME_H_

#include "frontend/frontend.h"

// ----------------------------------------------------------------------------
// ibFrame
// ----------------------------------------------------------------------------

class FRONTEND_API ibFrame : public wxFrameBase
{
public:
    ibFrame() = default;
    ibFrame(wxWindow *parent,
            wxWindowID id,
            const wxString& title,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxDEFAULT_FRAME_STYLE,
            const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        Create(parent, id, title, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr));

    virtual wxPoint GetClientAreaOrigin() const override;
    virtual bool Enable(bool enable = true) override;

#if wxUSE_STATUSBAR
    virtual ibStatusBar* CreateStatusBar(int number = 1,
                                         long style = wxSTB_DEFAULT_STYLE,
                                         wxWindowID id = 0,
                                         const wxString& name = wxASCII_STR(wxStatusLineNameStr)) override;
#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR
    // create main toolbar bycalling OnCreateToolBar()
    virtual ibToolBar* CreateToolBar(long style = -1,
                                     wxWindowID id = wxID_ANY,
                                     const wxString& name = wxASCII_STR(wxToolBarNameStr)) override;
#endif // wxUSE_TOOLBAR

    virtual wxSize GetMinSize() const override;

protected:
    void OnSize(wxSizeEvent& event);
    void OnSysColourChanged(wxSysColourChangedEvent& event);

    virtual void DoGetClientSize(int *width, int *height) const override;
    virtual void DoSetClientSize(int width, int height) override;

#if wxUSE_MENUS
    // override to update menu bar position when the frame size changes
    virtual void PositionMenuBar() override;
    virtual void DetachMenuBar() override;
    virtual void AttachMenuBar(ibMenuBar *menubar) override;
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    // override to update statusbar position when the frame size changes
    virtual void PositionStatusBar() override;
#endif // wxUSE_MENUS

protected:
#if wxUSE_TOOLBAR
    virtual void PositionToolBar() override;
#endif // wxUSE_TOOLBAR

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibFrame);
};

#endif // _WX_UNIV_FRAME_H_
