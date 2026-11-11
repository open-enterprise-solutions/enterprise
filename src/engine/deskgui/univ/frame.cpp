// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically): the frame laying out its menubar
// and status bar inside the decorations the theme draws (frame.h).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/framuniv.cpp
// Purpose:     ibFrame class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     19.05.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
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

#include "deskgui/univ/frame.h"
#include "deskgui/univ/ctrl/statusBar.h"
#include "deskgui/univ/window.h"   // ibWindow::DoPopupMenu — the theme's menu, shown over the frame

#ifndef WX_PRECOMP
    #include <wx/menu.h>
    #include <wx/statusbr.h>
#endif // WX_PRECOMP

#ifdef __WXMSW__
    #include <wx/msw/private.h>
#endif // __WXMSW__

// ============================================================================
// implementation
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibFrame, wxFrame);

// ----------------------------------------------------------------------------
// ctors
// ----------------------------------------------------------------------------

bool ibFrame::Create(wxWindow *parent,
                     wxWindowID id,
                     const wxString& title,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxString& name)
{
    if ( !ibTopLevelWindow<wxFrame>::Create(parent, id, title, pos, size, style, name) )
        return false;

    // SEAM vs univ: the colour is the theme's (ibTopLevelWindow::Create), not the workspace's the canon set
    Bind(wxEVT_SIZE, &ibFrame::OnSize, this);
    Bind(wxEVT_CHAR_HOOK, &ibFrame::OnCharHook, this);
    return true;
}

// ----------------------------------------------------------------------------
// menu support
// ----------------------------------------------------------------------------

void ibFrame::OnSize(wxSizeEvent& event)
{
#if wxUSE_MENUS
    PositionMenuBar();
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    // ...and the status bar, as the canon's: the native frame places it only as a SHOWN frame is sized
    // (wxFrame::HandleSize), and the main window is sized hidden - shown at that size, its bar stood where the frame's
    // first size put it: mid-window in the client, under the panes in the designer until the window was first sized
    // (2026-11-11, Max). The port's own bar got by on a size posted at its creation (wxStatusBar::Create, MSW)
    PositionStatusBar();
#endif // wxUSE_STATUSBAR

    event.Skip();
}

// SEAM vs univ: THE MENU'S LETTERS ARE THE FRAME'S, before the window with the focus has the key - wx hands every key
// to the top window first (wxEVT_CHAR_HOOK, from its keyboard hook). The canon took them as a character in each window
// of its own (wxWindow::OnChar): a window that handles its characters kept the letter, and a character is the
// layout's - Alt+E in the Russian layout is 'у', no title of an English menu has it. The layout's letter is matched
// first (a Russian title, "&Файл"), the key's own then (an English title typed in any layout). Its items' accelerators
// before them, here too: on every port (ibMenuBar::ProcessAccelEvent)
void ibFrame::OnCharHook(wxKeyEvent& event)
{
#if wxUSE_MENUS
    ibMenuBar * const menubar = wxDynamicCast(m_frameMenuBar, ibMenuBar);

#if wxUSE_ACCEL
    if ( menubar && menubar->ProcessAccelEvent(event) )
        return;
#endif // wxUSE_ACCEL

    // AltGr is Ctrl+Alt: a character typed, not a menu asked for
    if ( menubar && event.AltDown() && !event.ControlDown() )
    {
        int pos = wxNOT_FOUND;
        if ( event.GetUnicodeKey() != WXK_NONE )
            pos = menubar->FindNextItemForAccel(-1, event.GetUnicodeKey());
        if ( pos == wxNOT_FOUND )
            pos = menubar->FindNextItemForAccel(-1, event.GetKeyCode());

        if ( pos != wxNOT_FOUND && menubar->IsEnabledTop(pos) )
        {
            // after the key, not in it: the hook that raised the event waits on its return, and the menu's loop
            // would run inside it, every key it reads raising the hook again
            CallAfter([this, pos]()
            {
                ibMenuBar * const bar = wxDynamicCast(m_frameMenuBar, ibMenuBar);
                if ( bar && static_cast<size_t>(pos) < bar->GetMenuCount() )
                    bar->PopupMenu(pos);
            });
            return;
        }
    }
#endif // wxUSE_MENUS

    event.Skip();
}

#if wxUSE_MENUS

void ibFrame::PositionMenuBar()
{
    if ( m_frameMenuBar )
    {
        // the menubar is positioned above the client size, hence the negative
        // y coord
        wxCoord heightMbar = m_frameMenuBar->GetSize().y;

        m_frameMenuBar->SetSize(0,
                                - heightMbar,
                                GetClientSize().x, heightMbar);
    }
}

void ibFrame::DetachMenuBar()
{
    // SEAM vs univ: no layout of its own - the frame is laid out once, as the bar it takes (AttachMenuBar, which
    // SetMenuBar always calls after this one, a null bar too). Laid out here as well, a bar swapped for another stood
    // for a moment with none: the panes rose by its height and fell back, and on a tab switched everything jumped
    // (2026-11-11, Max: "the menu jumps like mad")
    wxFrameBase::DetachMenuBar();
}

void ibFrame::AttachMenuBar(wxMenuBar *menubar)
{
    // the theme's: a native one would make its native menu, in the caption the native window has not
    wxASSERT_MSG( !menubar || wxDynamicCast(menubar, ibMenuBar),
                  wxT("a frame of the theme takes the theme's menubar (ibMenuBar)") );

    wxFrameBase::AttachMenuBar(menubar);
    SendSizeEvent();
}

bool ibFrame::DoPopupMenu(wxMenu *menu, int x, int y)
{
    // the theme's, as an ib window shows it (ibWindow::DoPopupMenu): the native frame's was the system's menu, and of an
    // ibMenu's items it had none - they are not in its native menu (a server's popup menu over the main window, 2026-11-11).
    // At the mouse when no point is given (wxWindowBase::PopupMenu's wxDefaultPosition), as the native one. On GTK3/Cocoa
    // the frame is native as well - the same road
    if ( x == wxDefaultCoord && y == wxDefaultCoord )
    {
        const wxPoint mouse = ScreenToClient(wxGetMousePosition());
        x = mouse.x;
        y = mouse.y;
    }

    return ibWindow::DoPopupMenu(this, menu, x, y);
}

#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR

wxStatusBar *ibFrame::OnCreateStatusBar(int number, long style, wxWindowID id, const wxString& name)
{
    // the theme's, as wxFrameBase makes the port's
    ibStatusBar * const statusBar = new ibStatusBar(this, id, style, name);
    statusBar->SetFieldsCount(number);
    return statusBar;
}

#endif // wxUSE_STATUSBAR

bool ibFrame::IsOneOfBars(const wxWindow *win) const
{
#if wxUSE_MENUS
    // the menubar's window, not the bar - it has none of its own (ibMenuBar)
    const ibMenuBar * const menubar = wxDynamicCast(m_frameMenuBar, ibMenuBar);
    if ( menubar && win == menubar->GetWindow() )
        return true;
#endif // wxUSE_MENUS

    return ibTopLevelWindow<wxFrame>::IsOneOfBars(win);
}

wxPoint ibFrame::GetClientAreaOrigin() const
{
    wxPoint pt = ibTopLevelWindow<wxFrame>::GetClientAreaOrigin();

#if wxUSE_MENUS
    if ( m_frameMenuBar )
    {
        pt.y += m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

    return pt;
}

void ibFrame::DoGetClientSize(int *width, int *height) const
{
    ibTopLevelWindow<wxFrame>::DoGetClientSize(width, height);

#if wxUSE_MENUS
    if ( m_frameMenuBar && height )
    {
        (*height) -= m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    // ...as the native frame's: a hidden status bar is not placed
    // (wxFrame::PositionStatusBar)
    if ( m_frameStatusBar && m_frameStatusBar->IsShown() && height )
    {
        (*height) -= m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR
}

void ibFrame::DoSetClientSize(int width, int height)
{
#if wxUSE_MENUS
    if ( m_frameMenuBar )
    {
        height += m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    if ( m_frameStatusBar && m_frameStatusBar->IsShown() )
    {
        height += m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

    ibTopLevelWindow<wxFrame>::DoSetClientSize(width, height);
}

wxSize ibFrame::GetMinSize() const
{
    wxSize size = ibTopLevelWindow<wxFrame>::GetMinSize();

#if wxUSE_MENUS
    if ( m_frameMenuBar )
    {
        const wxSize sizeMenu = m_frameMenuBar->GetBestSize();
        if ( sizeMenu.x > size.x )
            size.x = sizeMenu.x;
        size.y += sizeMenu.y;
    }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    if ( m_frameStatusBar )
    {
        size.y += m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

    return size;
}

#ifdef __WXMSW__

WXLRESULT ibFrame::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    // the native menu's keyboard, sent by the system to a window with no native menu - Alt+letter a window let by
    // (WM_SYSCHAR), Alt or F10 alone: answered by nothing, not by the system's bell. The letters that open a menu are
    // taken before they get here (OnCharHook)
    if ( message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU )
        return 0;

    return ibTopLevelWindow<wxFrame>::MSWWindowProc(message, wParam, lParam);
}

#endif // __WXMSW__
