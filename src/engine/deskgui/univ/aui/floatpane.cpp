// FORKED from wxWidgets' AUI (src/aui/floatpane.cpp of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/aui/floatpane.cpp
// Purpose:     wxaui: wx advanced user interface - docking window manager
// Author:      Benjamin I. Williams
// Created:     2005-05-17
// Copyright:   (C) Copyright 2005-2006, Kirix Corporation, All Rights Reserved
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"



#include "deskgui/univ/aui/framemanager.h"
#include "deskgui/univ/aui/floatpane.h"

#ifndef WX_PRECOMP
#endif

#include "wx/private/tlwdrag.h"

#ifdef __WXMSW__
#include "wx/msw/wrapwin.h"
#endif

wxIMPLEMENT_CLASS(ibAuiFloatingFrame, ibFrame);

namespace
{

// Return true if the floating frames shouldn't use any decorations of their
// own but show the caption of the pane inside them instead.
bool UseOwnCaption()
{
    // Currently we must do it when using Wayland TLW drag protocol as when
    // dragging the floating frame by our caption we get the notifications
    // allowing to dock it, while dragging by the standard caption doesn't
    // generate any events at all and hence the pane can't be docked.
    //
    // We might want to use this even in the other cases as our caption may be
    // preferable to wxMiniFrame one, but for now be conservative and don't
    // change the behaviour when using X11 or under the other platforms.
    return wxTLWDragSession::IsAvailable();
}

// Return the style to use for the floating frame showing the given pane.
long GetFloatingFrameStyle(long style, const ibAuiPaneInfo& pane)
{
    if ( !pane.IsFixed() )
        style |= wxRESIZE_BORDER;

    if ( UseOwnCaption() )
    {
        // The caption shown inside the frame provides the close button and
        // allows dragging the frame, so we don't need any decorations.
        style &= ~(wxCAPTION | wxSYSTEM_MENU);
        style |= wxBORDER_NONE;
    }
    else
    {
        if ( pane.HasCloseButton() )
            style |= wxCLOSE_BOX;

        if ( pane.HasMaximizeButton() )
            style |= wxMAXIMIZE_BOX;
    }

    return style;
}

} // anonymous namespace

ibAuiFloatingFrame::ibAuiFloatingFrame(wxWindow* parent,
                ibAuiManager* owner_mgr,
                const ibAuiPaneInfo& pane,
                wxWindowID id /*= wxID_ANY*/,
                long style /*=wxRESIZE_BORDER | wxSYSTEM_MENU | wxCAPTION |
                              wxFRAME_TOOL_WINDOW | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT |
                              wxCLIP_CHILDREN
                           */)
                : ibFrame(parent, id, wxEmptyString,
                        pane.floating_pos, pane.floating_size,
                        GetFloatingFrameStyle(style, pane))
    , m_ownerMgr(owner_mgr)
{
    m_moving = false;
    m_mgr.SetManagedWindow(this);
    m_solidDrag = true;

    // find out if the system supports solid window drag.
    // on non-msw systems, this is assumed to be the case
#ifdef __WXMSW__
    BOOL b = TRUE;
    SystemParametersInfo(38 /*SPI_GETDRAGFULLWINDOWS*/, 0, &b, 0);
    m_solidDrag = b ? true : false;
#endif

    SetExtraStyle(wxWS_EX_PROCESS_IDLE);
}

ibAuiFloatingFrame::~ibAuiFloatingFrame()
{
    CleanUp();
}

void ibAuiFloatingFrame::CleanUp()
{
    // Ensure that the owner manager doesn't have a dangling pointer to us and
    // that we don't notify it about anything any more.
    if (m_ownerMgr)
    {
        if (m_ownerMgr->m_actionWindow == this)
            m_ownerMgr->m_actionWindow = nullptr;

        m_ownerMgr = nullptr;
    }

    m_mgr.UnInit();
}

void ibAuiFloatingFrame::SetPaneWindow(const ibAuiPaneInfo& pane)
{
    m_paneWindow = pane.window;
    m_paneWindow->Reparent(this);

    ibAuiPaneInfo contained_pane = pane;
    contained_pane.Dock().Center().Show().
                    CaptionVisible(false).
                    PaneBorder(false);

    if ( UseOwnCaption() )
    {
        // Show the caption to allow dragging and closing the pane, but not the
        // other buttons: only "maximize" would make sense but it doesn't
        // currently work and it's not really clear if it's useful.
        contained_pane.CaptionVisible().
                       MaximizeButton(false).
                       MinimizeButton(false).
                       PinButton(false);
    }

    // Carry over the minimum size
    wxSize pane_min_size = pane.window->GetMinSize();

    // if the frame window's max size is greater than the min size
    // then set the max size to the min size as well
    wxSize cur_max_size = GetMaxSize();
    if (cur_max_size.IsFullySpecified() &&
          (cur_max_size.x < pane.min_size.x ||
           cur_max_size.y < pane.min_size.y)
       )
    {
        SetMaxSize(pane_min_size);
    }

    SetMinSize(pane.window->GetMinSize());

    m_mgr.AddPane(m_paneWindow, contained_pane);
    m_mgr.Update();

    const wxSize floatingSize = pane.floating_size;
    const wxSize floatingClientSize = pane.floating_client_size;

    if ( pane.min_size.IsFullySpecified() )
    {
        // because SetSizeHints() calls Fit() too (which sets the window
        // size to its minimum allowed), we keep the size before calling
        // SetSizeHints() and reset it afterwards...
        wxSize tmp = GetSize();
        GetSizer()->SetSizeHints(this);
        SetSize(tmp);
    }

    SetTitle(pane.caption);

    // This code is slightly awkward because we need to reset wxRESIZE_BORDER
    // before calling SetClientSize() below as doing it after setting the
    // client size would actually change it, at least under MSW, where the
    // total window size doesn't change and hence, as the borders size changes,
    // the client size does change.
    //
    // So we must call it first but doing it generates a size event and updates
    // pane.floating_size from inside it so we must also record its original
    // value before doing it.
    const bool hasFloatingSize = floatingSize != wxDefaultSize ||
                                     floatingClientSize != wxDefaultSize;
    if (pane.IsFixed())
    {
        SetWindowStyleFlag(GetWindowStyleFlag() & ~wxRESIZE_BORDER);
    }

    if ( hasFloatingSize )
    {
        // give floating_client_size precedence over floating_size
        if (floatingClientSize != wxDefaultSize)
        {
            SetClientSize(floatingClientSize);
        }
        else
        {
            SetSize(floatingSize);
        }
    }
    else
    {
        wxSize size = pane.best_size;
        if (size == wxDefaultSize)
            size = pane.min_size;
        if (size == wxDefaultSize)
            size = m_paneWindow->GetSize();
        if (m_ownerMgr && pane.HasGripper())
        {
            const int gripperSize = m_ownerMgr->GetMetricForWindow(ibAUI_DOCKART_GRIPPER_SIZE, m_paneWindow);
            if (pane.HasGripperTop())
                size.y += gripperSize;
            else
                size.x += gripperSize;
        }

        // The caption shown inside the frame takes space too, so account for
        // it to avoid making the pane window itself smaller than it should be.
        if (m_ownerMgr && contained_pane.HasCaption())
        {
            size.y += m_ownerMgr->GetMetricForWindow(ibAUI_DOCKART_CAPTION_SIZE, m_paneWindow);
        }

        SetClientSize(size);
    }
}

bool ibAuiFloatingFrame::Destroy()
{
    // SEAM vs wx: the pane window is in the sizer of the window laid out inside the frame (ibAuiManager::SetManagedWindow),
    // not in the frame's own
    wxWindow* const managed = m_mgr.GetManagedWindow();

    // We are not going to be deleted immediately, but can still get events
    // until then, e.g. macOS may send size events even to the hidden window,
    // so ensure we reset any dangling pointers to avoid crashes while handling
    // such events.
    CleanUp();

    // Delete our sizer to detach the pane window from it and allow adding it
    // to another one.
    //
    // Note that this must be done after m_mgr.UnInit() called from CleanUp().
    if (managed)
        managed->SetSizer(nullptr);

    return ibFrame::Destroy();
}

ibAuiManager* ibAuiFloatingFrame::GetOwnerManager() const
{
    return m_ownerMgr;
}

bool ibAuiFloatingFrame::IsTopNavigationDomain(NavigationKind kind) const
{
    switch ( kind )
    {
        case Navigation_Tab:
            break;

        case Navigation_Accel:
            // Floating frames are often used as tool palettes and it's
            // convenient for the accelerators defined in the parent frame to
            // work in them, so don't block their propagation.
            return false;
    }

    return ibFrame::IsTopNavigationDomain(kind);
}

bool ibAuiFloatingFrame::DockPane()
{
    if ( !m_ownerMgr )
        return false;

    ibAuiPaneInfo& pane = m_ownerMgr->GetPane(m_paneWindow);
    if ( !pane.IsOk() || !pane.IsFloating() ||
            pane.frame != this || !pane.IsDockable() )
        return false;

    if ( m_ownerMgr->m_hasMaximized )
        m_ownerMgr->RestoreMaximizedPane();

    pane.Dock();
    m_ownerMgr->AddPaneToMinDockIfNecessary(pane);
    m_ownerMgr->Update();

    return true;
}

void ibAuiFloatingFrame::OnSize(wxSizeEvent& event)
{
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneResized(m_paneWindow, GetRect());
    }

    // SEAM vs wx: the frame lays out the window its pane is laid out in (ibAuiManager::SetManagedWindow)
    event.Skip();
}

void ibAuiFloatingFrame::OnClose(wxCloseEvent& evt)
{
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneClosed(m_paneWindow, evt);
    }
    if (!evt.GetVeto())
    {
        m_mgr.DetachPane(m_paneWindow);
        Destroy();
    }
}

void ibAuiFloatingFrame::OnLeftDClick(wxMouseEvent& event)
{
    if ( !DockPane() )
        event.Skip();
}

#ifdef __WXMSW__

bool ibAuiFloatingFrame::MSWHandleMessage(WXLRESULT* result,
                                          WXUINT message,
                                          WXWPARAM wParam,
                                          WXLPARAM lParam)
{
    if ( message == WM_NCLBUTTONDBLCLK && wParam == HTCAPTION )
    {
        if ( DockPane() )
        {
            *result = 0;
            return true;
        }
    }

    return ibFrame::MSWHandleMessage(result, message, wParam, lParam);
}

#endif // __WXMSW__

void ibAuiFloatingFrame::OnMoveEvent(wxMoveEvent& event)
{
    // Always sync pane's floating_pos with frame's position
    if (m_ownerMgr)
    {
        m_ownerMgr->GetPane(m_paneWindow).
            floating_pos = GetRect().GetPosition();
    }

    if (!m_solidDrag)
    {
        // systems without solid window dragging need to be
        // handled slightly differently, due to the lack of
        // the constant stream of EVT_MOVING events
        if (!isMouseDown())
            return;
        OnMoveStart();
        OnMoving(event.GetRect(), wxNORTH);
        m_moving = true;
        return;
    }


    wxRect winRect = GetRect();

    if (winRect == m_lastRect)
        return;

    // skip the first move event
    if (m_lastRect.IsEmpty())
    {
        m_lastRect = winRect;
        return;
    }

    // as on OSX moving windows are not getting all move events, only sporadically, this difference
    // is almost always big on OSX, so avoid this early exit opportunity
#ifndef __WXOSX__
    // skip if moving too fast to avoid massive redraws and
    // jumping hint windows
    // TODO: Should 3x3px threshold increase on Retina displays?
    if ((abs(winRect.x - m_lastRect.x) > 3) ||
        (abs(winRect.y - m_lastRect.y) > 3))
    {
        m_last3Rect = m_last2Rect;
        m_last2Rect = m_lastRect;
        m_lastRect = winRect;

        return;
    }
#endif

    // prevent frame redocking during resize
    if (m_lastRect.GetSize() != winRect.GetSize())
    {
        m_last3Rect = m_last2Rect;
        m_last2Rect = m_lastRect;
        m_lastRect = winRect;
        return;
    }

    wxDirection dir = wxALL;

    int horiz_dist = abs(winRect.x - m_last3Rect.x);
    int vert_dist = abs(winRect.y - m_last3Rect.y);

    if (vert_dist >= horiz_dist)
    {
        if (winRect.y < m_last3Rect.y)
            dir = wxNORTH;
        else
            dir = wxSOUTH;
    }
    else
    {
        if (winRect.x < m_last3Rect.x)
            dir = wxWEST;
        else
            dir = wxEAST;
    }

    m_last3Rect = m_last2Rect;
    m_last2Rect = m_lastRect;
    m_lastRect = winRect;

    if (!isMouseDown())
        return;

    if (!m_moving)
    {
        OnMoveStart();
        m_moving = true;
    }

    if (m_last3Rect.IsEmpty())
        return;

    if ( event.GetEventType() == wxEVT_MOVING )
        OnMoving(event.GetRect(), dir);
    else
        OnMoving(wxRect(event.GetPosition(),GetSize()), dir);
}

void ibAuiFloatingFrame::OnIdle(wxIdleEvent& event)
{
    if (m_moving)
    {
        if (!isMouseDown())
        {
            m_moving = false;
            OnMoveFinished();
        }
        else
        {
            event.RequestMore();
        }
    }
}

void ibAuiFloatingFrame::OnMoveStart()
{
    // notify the owner manager that the pane has started to move
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneMoveStart(m_paneWindow);
    }
}

void ibAuiFloatingFrame::OnMoving(const wxRect& WXUNUSED(window_rect), wxDirection dir)
{
    // notify the owner manager that the pane is moving
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneMoving(m_paneWindow, dir);
    }
    m_lastDirection = dir;
}

void ibAuiFloatingFrame::OnMoveFinished()
{
    // notify the owner manager that the pane has finished moving
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneMoved(m_paneWindow, m_lastDirection);
    }
}

void ibAuiFloatingFrame::OnActivate(wxActivateEvent& event)
{
    if (m_ownerMgr && event.GetActive())
    {
        m_ownerMgr->OnFloatingPaneActivated(m_paneWindow);
    }
}

// utility function which determines the state of the mouse button
// (independent of having a wxMouseEvent handy) - utimately a better
// mechanism for this should be found (possibly by adding the
// functionality to wxWidgets itself)
bool ibAuiFloatingFrame::isMouseDown()
{
    return wxGetMouseState().LeftIsDown();
}


wxBEGIN_EVENT_TABLE(ibAuiFloatingFrame, ibFrame)
    EVT_SIZE(ibAuiFloatingFrame::OnSize)
    EVT_MOVE(ibAuiFloatingFrame::OnMoveEvent)
    EVT_MOVING(ibAuiFloatingFrame::OnMoveEvent)
    EVT_LEFT_DCLICK(ibAuiFloatingFrame::OnLeftDClick)
    EVT_CLOSE(ibAuiFloatingFrame::OnClose)
    EVT_IDLE(ibAuiFloatingFrame::OnIdle)
    EVT_ACTIVATE(ibAuiFloatingFrame::OnActivate)
wxEND_EVENT_TABLE()


