// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

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

#include "frontend/uikit/frame.h"


#include <wx/frame.h>

#ifndef WX_PRECOMP
    #include <wx/menu.h>
    #include <wx/statusbr.h>
    #include <wx/settings.h>
    #include <wx/toolbar.h>
#endif // WX_PRECOMP

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibFrame, wxFrameBase)
    EVT_SIZE(ibFrame::OnSize)
    EVT_SYS_COLOUR_CHANGED(ibFrame::OnSysColourChanged)
wxEND_EVENT_TABLE()

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
    if ( !ibTopLevelWindow::Create(parent, id, title, pos, size, style, name) )
        return false;

    SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));

    return true;
}

// Responds to colour changes, and passes event on to children.
void ibFrame::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
    Refresh();

    event.Skip();
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
    PositionStatusBar();
#endif // wxUSE_STATUSBAR
#if wxUSE_TOOLBAR
    PositionToolBar();
#endif // wxUSE_TOOLBAR

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

        wxCoord heightTbar = 0;

#if wxUSE_TOOLBAR
        if ( m_frameToolBar )
            heightTbar = m_frameToolBar->GetSize().y;
#endif // wxUSE_TOOLBAR

        m_frameMenuBar->SetSize(0,
                                - (heightMbar + heightTbar),
                                GetClientSize().x, heightMbar);
    }
}

void ibFrame::DetachMenuBar()
{
    wxFrameBase::DetachMenuBar();
    SendSizeEvent();
}

void ibFrame::AttachMenuBar(ibMenuBar *menubar)
{
    wxFrameBase::AttachMenuBar(menubar);
    SendSizeEvent();
}

#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR

void ibFrame::PositionStatusBar()
{
    if ( m_frameStatusBar )
    {
        wxSize size = GetClientSize();
        m_frameStatusBar->SetSize(0, size.y, size.x, wxDefaultCoord);
    }
}

ibStatusBar* ibFrame::CreateStatusBar(int number, long style,
                                      wxWindowID id, const wxString& name)
{
    ibStatusBar *bar = wxFrameBase::CreateStatusBar(number, style, id, name);
    SendSizeEvent();
    return bar;
}

#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR

ibToolBar* ibFrame::CreateToolBar(long style, wxWindowID id, const wxString& name)
{
    if ( wxFrameBase::CreateToolBar(style, id, name) )
    {
        PositionToolBar();
    }

    return m_frameToolBar;
}

void ibFrame::PositionToolBar()
{
    if ( m_frameToolBar )
    {
        wxSize size = GetClientSize();
        int tw, th, tx, ty;

        tx = ty = 0;
        m_frameToolBar->GetSize(&tw, &th);
        if ( m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL )
        {
            tx = -tw;
            th = size.y;
        }
        else
        {
            ty = -th;
            tw = size.x;
        }

        m_frameToolBar->SetSize(tx, ty, tw, th);
    }
}
#endif // wxUSE_TOOLBAR

wxPoint ibFrame::GetClientAreaOrigin() const
{
    wxPoint pt = wxFrameBase::GetClientAreaOrigin();

#if wxUSE_MENUS
    if ( m_frameMenuBar )
    {
        pt.y += m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_TOOLBAR
    if ( m_frameToolBar )
    {
        if ( m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL )
            pt.x += m_frameToolBar->GetSize().x;
        else
            pt.y += m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR

    return pt;
}

void ibFrame::DoGetClientSize(int *width, int *height) const
{
    wxFrameBase::DoGetClientSize(width, height);

#if wxUSE_MENUS
    if ( m_frameMenuBar && height )
    {
        (*height) -= m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    if ( m_frameStatusBar && height )
    {
        (*height) -= m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR
    if ( m_frameToolBar )
    {
        if ( width && (m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL) )
            (*width) -= m_frameToolBar->GetSize().x;
        else if ( height )
            (*height) -= m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR
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
    if ( m_frameStatusBar )
    {
        height += m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR
    if ( m_frameToolBar )
    {
        if ( m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL )
            width += m_frameToolBar->GetSize().x;
        else
            height += m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR

    wxFrameBase::DoSetClientSize(width, height);
}

wxSize ibFrame::GetMinSize() const
{
    wxSize size = wxFrameBase::GetMinSize();

#if wxUSE_MENUS
    if ( m_frameMenuBar )
    {
        const wxSize sizeMenu = m_frameMenuBar->GetBestSize();
        if ( sizeMenu.x > size.x )
            size.x = sizeMenu.x;
        size.y += sizeMenu.y;
    }
#endif // wxUSE_MENUS

#if wxUSE_TOOLBAR
    if ( m_frameToolBar )
    {
        size.y += m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR

#if wxUSE_STATUSBAR
    if ( m_frameStatusBar )
    {
        size.y += m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

    return size;
}

bool ibFrame::Enable(bool enable)
{
    if (!wxFrameBase::Enable(enable))
        return false;
    return true;
}
