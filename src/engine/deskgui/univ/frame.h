// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically): the frame laying out its menubar
// and status bar inside the decorations the theme draws (src/univ/framuniv.cpp), over a native frame of wx's.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/frame.h
// Purpose:     ibFrame class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     19.05.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_FRAME_H_
#define _DESKGUI_UNIV_FRAME_H_

#include "deskgui/deskgui.h"
#include "deskgui/univ/menu.h"   // ibMenuBar, ibMenu — the menubar it takes, and its menus
#include "deskgui/univ/topLevel.h"

#include <wx/frame.h>

// ----------------------------------------------------------------------------
// ibFrame
// ----------------------------------------------------------------------------

// SEAM vs univ: THE FRAME IS wx's OWN, ITS DECORATIONS AND ITS MENUBAR OURS - the theme's title bar and border round
// the client area (ibTopLevelWindow), and under the title bar the menubar the theme draws (ibMenuBar, the only kind
// it takes: the native one would stand above the title bar, in the caption the native window no longer has). The
// client area begins below the menubar and ends above the status bar, as the canon's; the status bar, the theme's
// (ibStatusBar, OnCreateStatusBar), is placed as the frame is sized (OnSize, wxFrame::PositionStatusBar) in the client
// area so measured. Not kept: the frame's own tool bar
// (the panes' are AUI's), the menubar's keyboard but its letters and its items' accelerators (Alt+letter opens a menu,
// Ctrl+S clicks Save - OnCharHook, on every port).
class DESKGUI_API ibFrame : public ibTopLevelWindow<wxFrame>
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
    virtual wxSize GetMinSize() const override;

#if wxUSE_STATUSBAR
    // the theme's status bar (ibStatusBar) - the port's own is not the theme's
    virtual wxStatusBar *OnCreateStatusBar(int number, long style, wxWindowID id, const wxString& name) override;
#endif // wxUSE_STATUSBAR

#ifdef __WXMSW__
    // the system's menu keys answered by nothing, as for a window with no menu (WM_SYSCOMMAND SC_KEYMENU - the menu's
    // letters are OnCharHook's)
    virtual WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif // __WXMSW__

protected:
    virtual void DoGetClientSize(int *width, int *height) const override;
    virtual void DoSetClientSize(int width, int height) override;

    // the menubar's window is one of the bars, not the child filling the frame (wxTopLevelWindowBase::DoLayout)
    virtual bool IsOneOfBars(const wxWindow *win) const override;

#if wxUSE_MENUS
    // override to update menu bar position when the frame size changes
    virtual void PositionMenuBar() override;
    virtual void DetachMenuBar() override;
    // the theme's menubar attached as the generic frame attaches one - the native frame's makes its native menu
    virtual void AttachMenuBar(wxMenuBar *menubar) override;
    // …and a popup menu the theme's, an ibMenu (ibWindow::DoPopupMenu) - the native frame's shows the system's
    virtual bool DoPopupMenu(wxMenu *menu, int x, int y) override;
#endif // wxUSE_MENUS

private:
    void OnSize(wxSizeEvent& event);

    // Alt+letter opens the menu whose title has the letter underlined, as a native menubar's
    void OnCharHook(wxKeyEvent& event);

    wxDECLARE_DYNAMIC_CLASS(ibFrame);
};

#endif // _DESKGUI_UNIV_FRAME_H_
