// FORKED from wxWidgets: wxSplitterWindow (src/generic/splitter.cpp) - see splitterWindow.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/splitter.cpp
// Purpose:     wxSplitterWindow implementation
// Author:      Julian Smart
// Created:     01/02/97
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/splitterWindow.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/theme.h"

#include <wx/dcclient.h>
#include <wx/renderer.h>
#include <wx/settings.h>
#include <wx/toplevel.h>

#ifdef __WXOSX__
    #include <wx/osx/private/available.h>
#endif

#include <wx/generic/private/drawresize.h>

#include <stdlib.h>

wxDEFINE_EVENT( ibEVT_SPLITTER_SASH_POS_CHANGED, ibSplitterEvent );
wxDEFINE_EVENT( ibEVT_SPLITTER_SASH_POS_CHANGING, ibSplitterEvent );
wxDEFINE_EVENT( ibEVT_SPLITTER_SASH_POS_RESIZE, ibSplitterEvent);
wxDEFINE_EVENT( ibEVT_SPLITTER_DOUBLECLICKED, ibSplitterEvent );
wxDEFINE_EVENT( ibEVT_SPLITTER_UNSPLIT, ibSplitterEvent );

wxIMPLEMENT_DYNAMIC_CLASS(ibSplitterWindow, ibWindow);

wxIMPLEMENT_DYNAMIC_CLASS(ibSplitterEvent, wxNotifyEvent);

wxBEGIN_EVENT_TABLE(ibSplitterWindow, ibWindow)
    EVT_PAINT(ibSplitterWindow::OnPaint)
    EVT_SIZE(ibSplitterWindow::OnSize)
    EVT_DPI_CHANGED(ibSplitterWindow::OnDPIChanged)
    EVT_MOUSE_EVENTS(ibSplitterWindow::OnMouseEvent)
    EVT_MOUSE_CAPTURE_LOST(ibSplitterWindow::OnMouseCaptureLost)
    EVT_SET_CURSOR(ibSplitterWindow::OnSetCursor)
wxEND_EVENT_TABLE()

bool ibSplitterWindow::Create(wxWindow *parent, wxWindowID id,
                                   const wxPoint& pos,
                                   const wxSize& size,
                                   long style,
                                   const wxString& name)
{
    // allow TABbing from one window to the other
    style |= wxTAB_TRAVERSAL;

    if ( !ibWindow::Create(parent, id, pos, size, style, name) )
        return false;

    m_lastSize = GetClientSize();

    m_permitUnsplitAlways = (style & wxSP_PERMIT_UNSPLIT) != 0;

    // don't erase the splitter background, it's pointless as we overwrite it
    // anyhow
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    return true;
}

void ibSplitterWindow::Init()
{
    m_splitMode = wxSPLIT_VERTICAL;
    m_permitUnsplitAlways = true;
    m_windowOne = nullptr;
    m_windowTwo = nullptr;
    m_dragMode = wxSPLIT_DRAG_NONE;
    m_oldX = 0;
    m_oldY = 0;
    m_sashStart = 0;
    m_sashPosition = 0;
    m_requestedSashPosition = INT_MAX;
    m_sashGravity = 0.0;
    m_minimumPaneSize = 0;
    m_sashCursorWE = wxCursor(wxCURSOR_SIZEWE);
    m_sashCursorNS = wxCursor(wxCURSOR_SIZENS);

    m_needUpdating = false;
}

ibSplitterWindow::~ibSplitterWindow()
{
}

// ----------------------------------------------------------------------------
// entering/leaving sash
// ----------------------------------------------------------------------------

// SEAM vs wx: the sash does not light up under the mouse - it is the background and its grip (DrawSash); the cursor
// shows it
void ibSplitterWindow::OnEnterSash()
{
    SetResizeCursor();
}

void ibSplitterWindow::OnLeaveSash()
{
    SetCursor(*wxSTANDARD_CURSOR);
}

void ibSplitterWindow::SetResizeCursor()
{
    SetCursor(m_splitMode == wxSPLIT_VERTICAL ? m_sashCursorWE
                                              : m_sashCursorNS);
}

// ----------------------------------------------------------------------------
// other event handlers
// ----------------------------------------------------------------------------

void ibSplitterWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dc(this);
#ifdef __WXOSX__
  #if __MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_16
    if ( WX_IS_MACOS_AVAILABLE(10, 16) )
    {
        // Nothing to do: since macOS 10.14, views are layer-backed or using a shared
        // layer and explicitly clearing the background isn't needed. This only
        // started mattering here with macOS 11 (aka 10.16 when built with older SDK),
        // where we must avoid explicitly painting window backgrounds
    }
    else
  #endif
    {
        // as subpanels might have a transparent background we must erase the background
        // at least on OSX, otherwise traces of the sash will remain
        // test with: splitter sample->replace right window
        dc.Clear();
    }
#endif // __WXOSX__

    DrawSash(dc);
}

void ibSplitterWindow::OnInternalIdle()
{
    ibWindow::OnInternalIdle();

    // We may need to update the children sizes if we're in the middle of
    // a live update as indicated by m_needUpdating. The other possible case,
    // when we have a requested but not yet set sash position (as indicated
    // by m_requestedSashPosition having a valid value) is handled by OnSize.
    if ( m_needUpdating )
    {
        m_needUpdating = false;
        SizeWindows();
    }
}

void ibSplitterWindow::OnMouseEvent(wxMouseEvent& event)
{
    int x = (int)event.GetX(),
        y = (int)event.GetY();

    if ( GetWindowStyle() & wxSP_NOSASH )
    {
        event.Skip();
        return;
    }

    // with wxSP_LIVE_UPDATE style the splitter windows are always resized
    // following the mouse movement while it drags the sash, without it we only
    // draw the sash at the new position but only resize the windows when the
    // dragging is finished
    const bool isLive = HasFlag(wxSP_LIVE_UPDATE);

    if (event.LeftDown())
    {
        if ( SashHitTest(x, y) )
        {
            // Start the drag now
            m_dragMode = wxSPLIT_DRAG_DRAGGING;

            // Capture mouse and set the cursor
            CaptureMouse();
            SetResizeCursor();

            if ( !isLive )
            {
                // remember the initial sash position and draw the initial
                // shadow sash
                m_sashPositionCurrent = m_sashPosition;

                m_oldX = (m_splitMode == wxSPLIT_VERTICAL ? m_sashPositionCurrent : x);
                m_oldY = (m_splitMode != wxSPLIT_VERTICAL ? m_sashPositionCurrent : y);
                DrawSashTracker(m_oldX, m_oldY);
            }

            m_ptStart = wxPoint(x,y);
            m_sashStart = m_sashPosition;
            return;
        }
    }
    else if (event.LeftUp() && m_dragMode == wxSPLIT_DRAG_DRAGGING)
    {
        // We can stop dragging now and see what we've got.
        m_dragMode = wxSPLIT_DRAG_NONE;

        // Release mouse and unset the cursor
        ReleaseMouse();
        SetCursor(* wxSTANDARD_CURSOR);

        // exit if unsplit after doubleclick
        if ( !IsSplit() )
        {
            return;
        }

        // Hide sash tracker
        if ( !isLive )
        {
            m_overlay.Reset();
        }

        // the position of the click doesn't exactly correspond to
        // m_sashPosition, rather it changes it by the distance by which the
        // mouse has moved
        int diff = m_splitMode == wxSPLIT_VERTICAL ? x - m_ptStart.x : y - m_ptStart.y;

        int posSashNew = OnSashPositionChanging(m_sashStart + diff);
        if ( posSashNew == -1 )
        {
            // change not allowed
            return;
        }

        if ( m_permitUnsplitAlways || m_minimumPaneSize == 0 )
        {
            // Deal with possible unsplit scenarios
            if ( posSashNew == 0 )
            {
                // We remove the first window from the view
                wxWindow *removedWindow = m_windowOne;
                m_windowOne = m_windowTwo;
                m_windowTwo = nullptr;
                OnUnsplit(removedWindow);
                ibSplitterEvent eventUnsplit(ibEVT_SPLITTER_UNSPLIT, this);
                eventUnsplit.m_data.win = removedWindow;
                (void)ProcessWindowEvent(eventUnsplit);
                SetSashPositionAndNotify(0);
            }
            else if ( posSashNew == GetWindowSize() )
            {
                // We remove the second window from the view
                wxWindow *removedWindow = m_windowTwo;
                m_windowTwo = nullptr;
                OnUnsplit(removedWindow);
                ibSplitterEvent eventUnsplit(ibEVT_SPLITTER_UNSPLIT, this);
                eventUnsplit.m_data.win = removedWindow;
                (void)ProcessWindowEvent(eventUnsplit);
                SetSashPositionAndNotify(0);
            }
            else
            {
                SetSashPositionAndNotify(posSashNew);
            }
        }
        else
        {
            SetSashPositionAndNotify(posSashNew);
        }

        SizeWindows();
    }  // left up && dragging
    else if ((event.Moving() || event.Leaving() || event.Entering()) && (m_dragMode == wxSPLIT_DRAG_NONE))
    {
        if ( event.Leaving() || !SashHitTest(x, y) )
            OnLeaveSash();
        else
            OnEnterSash();
    }
    else if (event.Dragging() && (m_dragMode == wxSPLIT_DRAG_DRAGGING))
    {
        int diff = m_splitMode == wxSPLIT_VERTICAL ? x - m_ptStart.x : y - m_ptStart.y;

        int posSashNew = OnSashPositionChanging(m_sashStart + diff);
        if ( posSashNew == -1 )
        {
            // change not allowed
            return;
        }

        if ( !isLive )
        {
            if ( posSashNew == m_sashPositionCurrent )
                return;

            m_sashPositionCurrent = posSashNew;

            m_oldX = (m_splitMode == wxSPLIT_VERTICAL ? m_sashPositionCurrent : x);
            m_oldY = (m_splitMode != wxSPLIT_VERTICAL ? m_sashPositionCurrent : y);

            // As we captured the mouse, we may get the mouse events from outside
            // our window - for example, negative values in x, y: the tracker stays
            // inside (SEAM vs wx: on every port; wx's MSW kept it from the other
            // side, where its unsigned coordinates had put it)
            if ( m_oldX < 0 )
                m_oldX = 0;
            if ( m_oldY < 0 )
                m_oldY = 0;

            DrawSashTracker(m_oldX, m_oldY);
        }
        else
        {
            if ( posSashNew == m_sashPosition )
                return;

            DoSetSashPosition(posSashNew);

            // in live mode, the new position is the actual sash position, clear requested position!
            m_requestedSashPosition = INT_MAX;
            m_needUpdating = true;
        }
    }
    else if ( event.LeftDClick() && m_windowTwo )
    {
        OnDoubleClickSash(x, y);
    }
    else
    {
        event.Skip();
    }
}

void ibSplitterWindow::OnMouseCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    if (m_dragMode != wxSPLIT_DRAG_DRAGGING)
        return;

    m_dragMode = wxSPLIT_DRAG_NONE;

    SetCursor(* wxSTANDARD_CURSOR);

    // Erase sash tracker
    // SEAM vs wx: the canon asks HasFlag(wxSP_LIVE_UPDATE) here - the one mode with no tracker; it is drawn without
    // it (OnMouseEvent's !isLive), and a drag cut off (another window took the mouse) left it on the window
    if ( !HasFlag(wxSP_LIVE_UPDATE) )
    {
        m_overlay.Reset();
    }
}

void ibSplitterWindow::OnSize(wxSizeEvent& event)
{
    // only process this message if we're not iconized - otherwise iconizing
    // and restoring a window containing the splitter has a funny side effect
    // of changing the splitter position!
    wxWindow *parent = wxGetTopLevelParent(this);
    bool iconized;

    wxTopLevelWindow *winTop = wxDynamicCast(parent, wxTopLevelWindow);
    if ( winTop )
    {
        iconized = winTop->IsIconized();
    }
    else
    {
        wxFAIL_MSG(wxT("should have a top level parent!"));

        iconized = false;
    }

    if ( iconized )
    {
        m_lastSize = wxSize(0,0);

        event.Skip();

        return;
    }

    const wxSize curSize = event.GetSize();

    // Update the sash position if needed.
    //
    // Notice that we shouldn't do this if the sash position requested by user
    // couldn't be set yet as it would never be taken into account at all if we
    // modified it before this happens.
    if ( m_windowTwo && m_requestedSashPosition == INT_MAX )
    {
        int size = m_splitMode == wxSPLIT_VERTICAL ? curSize.x : curSize.y;

        int old_size = m_splitMode == wxSPLIT_VERTICAL ? m_lastSize.x : m_lastSize.y;

        // Don't do anything if the size didn't really change.
        if ( size != old_size )
        {
            int newPosition = -1;

            // Apply gravity if we use it.
            int delta = (int) ( (size - old_size)*m_sashGravity );

            // If delta == 0 then sash will be set according to the windows min size.
            if ( delta != 0 )
            {
                newPosition = m_sashPosition + delta;
                if( newPosition < m_minimumPaneSize )
                    newPosition = m_minimumPaneSize;
            }

            // Send an event with the newly calculated position. The handler
            // can then override the new position by setting the new position.
            ibSplitterEvent update(ibEVT_SPLITTER_SASH_POS_RESIZE, this);
            update.m_data.resize.pos = newPosition;
            update.m_data.resize.oldSize = old_size;
            update.m_data.resize.newSize = size;

            if ( ProcessWindowEvent(update) )
            {
                if (update.IsAllowed())
                {
                    // If the user set the sashposition to -1
                    // we keep the already calculated value,
                    // otherwise the user provided the new position.
                    int userPos = update.GetSashPosition();
                    if (userPos != -1)
                        newPosition = userPos;
                }
                else
                {
                    // the event handler vetoed the change
                    newPosition = -1;
                }
            }

            // Also check if the second window became too small.
            newPosition = AdjustSashPosition(newPosition == -1
                                                 ? m_sashPosition
                                                 : newPosition);
            if ( newPosition != m_sashPosition )
                SetSashPositionAndNotify(newPosition);
        }
    }

    m_lastSize = curSize;

    SizeWindows();
}

void ibSplitterWindow::OnDPIChanged(wxDPIChangedEvent& event)
{
    // On platforms requiring scaling by DPI factor we need to do it whenever
    // DPI changes, but elsewhere we shouldn't do it as the same logical
    // coordinates are used irrespectively of the current DPI value.
#ifndef wxHAS_DPI_INDEPENDENT_PIXELS
    m_minimumPaneSize = event.ScaleX(m_minimumPaneSize);
    m_sashPosition = event.ScaleX(m_sashPosition);
    m_lastSize = event.Scale(m_lastSize);

    // Any saved or pending positions need to be updated too.
    m_lastSplitPosition = event.Scale(m_lastSplitPosition);

    // But this one should only be scaled if it is really valid.
    if ( m_requestedSashPosition != INT_MAX )
        m_requestedSashPosition = event.ScaleX(m_requestedSashPosition);
#endif // !wxHAS_DPI_INDEPENDENT_PIXELS

    event.Skip();
}

void ibSplitterWindow::SetSashGravity(double gravity)
{
    wxCHECK_RET( gravity >= 0. && gravity <= 1.,
                    wxT("invalid gravity value") );

    m_sashGravity = gravity;
}

bool ibSplitterWindow::SashHitTest(int x, int y)
{
    if ( m_windowTwo == nullptr || m_sashPosition == 0)
        return false; // No sash

    int z = m_splitMode == wxSPLIT_VERTICAL ? x : y;
    int hitMax = m_sashPosition + GetSashSize() - 1;

    return z >= m_sashPosition && z <= hitMax;
}

void ibSplitterWindow::SetSashInvisible(bool invisible)
{
    if ( IsSashInvisible() != invisible )
        ToggleWindowStyle(wxSP_NOSASH);
}

int ibSplitterWindow::GetSashSize() const
{
    return IsSashInvisible() ? 0 : GetDefaultSashSize();
}

int ibSplitterWindow::GetDefaultSashSize() const
{
    return wxRendererNative::Get().GetSplitterParams(this).widthSash;
}

int ibSplitterWindow::GetBorderSize() const
{
    return wxRendererNative::Get().GetSplitterParams(this).border;
}

// Draw the sash
void ibSplitterWindow::DrawSash(wxDC& dc)
{
    if (HasFlag(wxSP_3DBORDER))
        wxRendererNative::Get().DrawSplitterBorder
                            (
                                this,
                                dc,
                                GetClientRect()
                            );

    // don't draw sash if we're not split
    if ( m_sashPosition == 0 || !m_windowTwo )
        return;

    // nor if we're configured to not show it
    if ( IsSashInvisible() )
        return;

    // SEAM vs wx: the sash is the window's own background, the theme's (ibWindow::EraseBackground) - wxMSW's is the
    // dialog's face too, but the system's (BTNFACE): a light bar between the panes on Luna's chrome, and macOS draws
    // its own divider
    const wxSize size = GetClientSize();
    const wxRect sash = m_splitMode == wxSPLIT_VERTICAL
                            ? wxRect(m_sashPosition, 0, GetSashSize(), size.y)
                            : wxRect(0, m_sashPosition, size.x, GetSashSize());
    EraseBackground(dc, sash);

    // the grip: a row of dots along the sash's middle, the theme's shadow - the background alone hid the sash
    // between two panes of one colour
    const int dot = FromDIP(2), step = FromDIP(4), dots = 5;
    const wxPoint centre(sash.x + sash.width / 2, sash.y + sash.height / 2);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxTHEME_COLOUR(SHADOW_DARK));
    for ( int i = 0; i < dots; ++i )
    {
        const int along = (i - dots / 2) * step - dot / 2;
        if ( m_splitMode == wxSPLIT_VERTICAL )
            dc.DrawRectangle(centre.x - dot / 2, centre.y + along, dot, dot);
        else
            dc.DrawRectangle(centre.x + along, centre.y - dot / 2, dot, dot);
    }
}

// Draw the sash tracker (for whilst moving the sash)
void ibSplitterWindow::DrawSashTracker(int x, int y)
{
    // One of the components of this size will be modified below, the other one
    // will stay unchanged as the tracker goes across the entire window.
    wxSize sizeSash = GetClientSize();

    // One of the components of this position will be modified below, the other
    // one is always 0 as the tracker starts at the left/top of the window.
    wxPoint posSash;

    const int sashTrackerWidth = GetDefaultSashSize();

    if ( m_splitMode == wxSPLIT_VERTICAL )
    {
        posSash.x = wxClip(x, 0, sizeSash.x);
        sizeSash.x = sashTrackerWidth;
    }
    else
    {
        posSash.y = wxClip(y, 0, sizeSash.y);
        sizeSash.y = sashTrackerWidth;
    }

    wxDrawOverlayResizeHint(this, m_overlay, wxRect(posSash, sizeSash));
}

int ibSplitterWindow::GetWindowSize() const
{
    wxSize size = GetClientSize();

    return m_splitMode == wxSPLIT_VERTICAL ? size.x : size.y;
}

int ibSplitterWindow::AdjustSashPosition(int sashPos) const
{
    wxWindow *win;

    win = GetWindow1();
    if ( win )
    {
        // the window shouldn't be smaller than its own minimal size nor
        // smaller than the minimual pane size specified for this splitter
        int minSize = m_splitMode == wxSPLIT_VERTICAL ? win->GetMinWidth()
                                                      : win->GetMinHeight();

        if ( minSize == -1 || m_minimumPaneSize > minSize )
            minSize = m_minimumPaneSize;

        minSize += GetBorderSize();

        if ( sashPos < minSize )
            sashPos = minSize;
    }

    win = GetWindow2();
    if ( win )
    {
        int minSize = m_splitMode == wxSPLIT_VERTICAL ? win->GetMinWidth()
                                                      : win->GetMinHeight();

        if ( minSize == -1 || m_minimumPaneSize > minSize )
            minSize = m_minimumPaneSize;

        int maxSize = GetWindowSize() - minSize - GetBorderSize() - GetSashSize();
        if ( maxSize > 0 && sashPos > maxSize && maxSize >= m_minimumPaneSize)
            sashPos = maxSize;
    }

    return sashPos;
}

bool ibSplitterWindow::DoSetSashPosition(int sashPos)
{
    int newSashPosition = AdjustSashPosition(sashPos);

    if ( newSashPosition == m_sashPosition )
        return false;

    m_sashPosition = newSashPosition;

    return true;
}

void ibSplitterWindow::SetSashPositionAndNotify(int sashPos)
{
    // we must reset the request here, otherwise the sash would be stuck at
    // old position if the user attempted to move the sash after invalid
    // (e.g. smaller than minsize) sash position was requested using
    // SetSashPosition():
    m_requestedSashPosition = INT_MAX;

    // note that we must send the event in any case, i.e. even if the sash
    // position hasn't changed and DoSetSashPosition() returns false because we
    // must generate a CHANGED event at the end of resizing
    DoSetSashPosition(sashPos);

    ibSplitterEvent event(ibEVT_SPLITTER_SASH_POS_CHANGED, this);
    event.m_data.resize.pos = m_sashPosition;

    (void)ProcessWindowEvent(event);
}

// Position and size subwindows.
// Note that the border size applies to each subwindow, not
// including the edges next to the sash.
void ibSplitterWindow::SizeWindows()
{
    // check if we have delayed setting the real sash position
    if ( m_requestedSashPosition != INT_MAX )
    {
        int newSashPosition = ConvertSashPosition(m_requestedSashPosition);
        if ( newSashPosition != m_sashPosition )
        {
            DoSetSashPosition(newSashPosition);
        }

        if ( newSashPosition <= m_sashPosition
            && newSashPosition >= m_sashPosition - GetBorderSize() )
        {
            // don't update it any more
            m_requestedSashPosition = INT_MAX;
        }
    }

    int w, h;
    GetClientSize(&w, &h);

    if ( GetWindow1() && !GetWindow2() )
    {
        GetWindow1()->SetSize(GetBorderSize(), GetBorderSize(),
                              w - 2*GetBorderSize(), h - 2*GetBorderSize());
    }
    else if ( GetWindow1() && GetWindow2() )
    {
        const int border = GetBorderSize(),
                  sash = GetSashSize();

        int size1 = GetSashPosition() - border,
            size2 = GetSashPosition() + sash;

        int x2, y2, w1, h1, w2, h2;
        if ( GetSplitMode() == wxSPLIT_VERTICAL )
        {
            w1 = size1;
            w2 = w - 2*border - sash - w1;
            if (w2 < 0)
                w2 = 0;
            h2 = h - 2*border;
            if (h2 < 0)
                h2 = 0;
            h1 = h2;
            x2 = size2;
            y2 = border;
        }
        else // horz splitter
        {
            w2 = w - 2*border;
            if (w2 < 0)
                w2 = 0;
            w1 = w2;
            h1 = size1;
            h2 = h - 2*border - sash - h1;
            if (h2 < 0)
                h2 = 0;
            x2 = border;
            y2 = size2;
        }

        GetWindow2()->SetSize(x2, y2, w2, h2);
        GetWindow1()->SetSize(border, border, w1, h1);
    }

    // SEAM vs wx: the sash at its new place is the paint's (OnPaint, DrawSash) - wx drew it straight onto a client DC,
    // which Cocoa and GTK3 under Wayland do not draw through. Its strip alone: the panes repaint what their sizes moved
    const wxSize size = GetClientSize();
    if ( HasFlag(wxSP_3DBORDER) )
        Refresh(false);
    else
        RefreshRect(m_splitMode == wxSPLIT_VERTICAL ? wxRect(m_sashPosition, 0, GetSashSize(), size.y)
                                                    : wxRect(0, m_sashPosition, size.x, GetSashSize()),
                    false);
}

// Set pane for unsplit window
void ibSplitterWindow::Initialize(wxWindow *window)
{
    wxASSERT_MSG( (!window || window->GetParent() == this),
                  wxT("windows in the splitter should have it as parent!") );

    if (window && !window->IsShown())
        window->Show();

    m_windowOne = window;
    m_windowTwo = nullptr;
    DoSetSashPosition(0);
}

// Associates the given window with window 2, drawing the appropriate sash
// and changing the split mode.
// Does nothing and returns false if the window is already split.
bool ibSplitterWindow::DoSplit(wxSplitMode mode,
                               wxWindow *window1, wxWindow *window2,
                               int sashPosition)
{
    if ( IsSplit() )
        return false;

    wxCHECK_MSG( window1 && window2, false,
                 wxT("cannot split with null window(s)") );

    wxCHECK_MSG( window1->GetParent() == this && window2->GetParent() == this, false,
                  wxT("windows in the splitter should have it as parent!") );

    if (! window1->IsShown())
        window1->Show();
    if (! window2->IsShown())
        window2->Show();

    m_splitMode = mode;
    m_windowOne = window1;
    m_windowTwo = window2;


    SetSashPosition(sashPosition, true);
    return true;
}

int ibSplitterWindow::ConvertSashPosition(int sashPosition) const
{
    if ( sashPosition > 0 )
    {
        return sashPosition;
    }
    else if ( sashPosition < 0 )
    {
        // It's negative so adding is subtracting
        return GetWindowSize() + sashPosition;
    }
    else // sashPosition == 0
    {
        // Use last split position if we have it.
        if ( m_splitMode == wxSPLIT_VERTICAL )
        {
            if ( m_lastSplitPosition.x )
                return m_lastSplitPosition.x;
        }
        else if ( m_splitMode == wxSPLIT_HORIZONTAL )
        {
            if ( m_lastSplitPosition.y )
                return m_lastSplitPosition.y;
        }

        // default, put it in the centre
        return GetWindowSize() / 2;
    }
}

// Remove the specified (or second) window from the view
// Doesn't actually delete the window.
bool ibSplitterWindow::Unsplit(wxWindow *toRemove)
{
    if ( ! IsSplit() )
        return false;

    wxWindow *win;
    if ( toRemove == nullptr || toRemove == m_windowTwo)
    {
        win = m_windowTwo ;
        m_windowTwo = nullptr;
    }
    else if ( toRemove == m_windowOne )
    {
        win = m_windowOne ;
        m_windowOne = m_windowTwo;
        m_windowTwo = nullptr;
    }
    else
    {
        wxFAIL_MSG(wxT("splitter: attempt to remove a non-existent window"));

        return false;
    }

    if (m_splitMode == wxSPLIT_VERTICAL)
    {
        m_lastSplitPosition.x = m_sashPosition;
    }
    else if (m_splitMode == wxSPLIT_HORIZONTAL)
    {
        m_lastSplitPosition.y = m_sashPosition;
    }

    OnUnsplit(win);
    DoSetSashPosition(0);
    SizeWindows();

    return true;
}

// Replace a window with another one
bool ibSplitterWindow::ReplaceWindow(wxWindow *winOld, wxWindow *winNew)
{
    wxCHECK_MSG( winOld, false, wxT("use one of Split() functions instead") );
    wxCHECK_MSG( winNew, false, wxT("use Unsplit() functions instead") );

    if ( winOld == m_windowTwo )
    {
        m_windowTwo = winNew;
    }
    else if ( winOld == m_windowOne )
    {
        m_windowOne = winNew;
    }
    else
    {
        wxFAIL_MSG(wxT("splitter: attempt to replace a non-existent window"));

        return false;
    }

    SizeWindows();

    return true;
}

void ibSplitterWindow::SetMinimumPaneSize(int min)
{
    m_minimumPaneSize = min;
    int pos = m_requestedSashPosition != INT_MAX ? m_requestedSashPosition : m_sashPosition;
    SetSashPosition(pos); // re-check limits
}

void ibSplitterWindow::SetSashPosition(int position, bool redraw)
{
    // remember the sash position we want to set for later if we can't set it
    // right now (e.g. because the window is too small)
    m_requestedSashPosition = position;

    DoSetSashPosition(ConvertSashPosition(position));

    if ( redraw )
    {
        SizeWindows();
    }
}

// Make sure the child window sizes are updated. This is useful
// for reducing flicker by updating the sizes before a
// window is shown, if you know the overall size is correct.
void ibSplitterWindow::UpdateSize()
{
    SizeWindows();
}

wxSize ibSplitterWindow::DoGetBestSize() const
{
    // get best sizes of subwindows
    wxSize size1, size2;
    if ( m_windowOne )
        size1 = m_windowOne->GetEffectiveMinSize();
    if ( m_windowTwo )
        size2 = m_windowTwo->GetEffectiveMinSize();

    // sum them
    //
    // pSash points to the size component to which sash size must be added
    int *pSash;
    wxSize sizeBest;
    if ( m_splitMode == wxSPLIT_VERTICAL )
    {
        sizeBest.y = wxMax(size1.y, size2.y);
        sizeBest.x = wxMax(size1.x, m_minimumPaneSize) +
                        wxMax(size2.x, m_minimumPaneSize);

        pSash = &sizeBest.x;
    }
    else // wxSPLIT_HORIZONTAL
    {
        sizeBest.x = wxMax(size1.x, size2.x);
        sizeBest.y = wxMax(size1.y, m_minimumPaneSize) +
                        wxMax(size2.y, m_minimumPaneSize);

        pSash = &sizeBest.y;
    }

    // account for the sash if the window is actually split
    if ( m_windowOne && m_windowTwo )
        *pSash += GetSashSize();

    // account for the border too
    int border = 2*GetBorderSize();
    sizeBest.x += border;
    sizeBest.y += border;

    return sizeBest;
}

// ---------------------------------------------------------------------------
// ibSplitterWindow virtual functions: they now just generate the events
// ---------------------------------------------------------------------------

bool ibSplitterWindow::OnSashPositionChange(int WXUNUSED(newSashPosition))
{
    // always allow by default
    return true;
}

int ibSplitterWindow::OnSashPositionChanging(int newSashPosition)
{
    // If within UNSPLIT_THRESHOLD from edge, set to edge to cause closure.
    const int UNSPLIT_THRESHOLD = 4;

    // first of all, check if OnSashPositionChange() doesn't forbid this change
    if ( !OnSashPositionChange(newSashPosition) )
    {
        // it does
        return -1;
    }

    // Obtain relevant window dimension for bottom / right threshold check
    int window_size = GetWindowSize();

    bool unsplit_scenario = false;
    if ( m_permitUnsplitAlways || m_minimumPaneSize == 0 )
    {
        // Do edge detection if unsplit premitted
        if ( newSashPosition <= UNSPLIT_THRESHOLD )
        {
            // threshold top / left check
            newSashPosition = 0;
            unsplit_scenario = true;
        }
        if ( newSashPosition >= window_size - UNSPLIT_THRESHOLD )
        {
            // threshold bottom/right check
            newSashPosition = window_size;
            unsplit_scenario = true;
        }
    }

    if ( !unsplit_scenario )
    {
        // If resultant pane would be too small, enlarge it
        newSashPosition = AdjustSashPosition(newSashPosition);

        // If the result is out of bounds it means minimum size is too big,
        // so split window in half as best compromise.
        if ( newSashPosition < 0 || newSashPosition > window_size )
            newSashPosition = window_size / 2;
    }

    // now let the event handler have it
    //
    // FIXME: shouldn't we do it before the adjustments above so as to ensure
    //        that the sash position is always reasonable?
    ibSplitterEvent event(ibEVT_SPLITTER_SASH_POS_CHANGING, this);
    event.m_data.resize.pos = newSashPosition;

    if ( ProcessWindowEvent(event) )
    {
        if (event.IsAllowed())
        {
            // it could have been changed by it
            newSashPosition = event.GetSashPosition();
        }
        else
        {
            // the event handler vetoed the change
            newSashPosition = -1;
        }
    }

    return newSashPosition;
}

// Called when the sash is double-clicked. The default behaviour is to remove
// the sash if the minimum pane size is zero.
void ibSplitterWindow::OnDoubleClickSash(int x, int y)
{
    wxCHECK_RET(m_windowTwo, wxT("splitter: no window to remove"));

    // new code should handle events instead of using the virtual functions
    ibSplitterEvent event(ibEVT_SPLITTER_DOUBLECLICKED, this);
    event.m_data.pt.x = x;
    event.m_data.pt.y = y;
    if ( !ProcessWindowEvent(event) || event.IsAllowed() )
    {
        if ( GetMinimumPaneSize() == 0 || m_permitUnsplitAlways )
        {
            wxWindow* win = m_windowTwo;
            if ( Unsplit(win) )
            {
                ibSplitterEvent unsplitEvent(ibEVT_SPLITTER_UNSPLIT, this);
                unsplitEvent.m_data.win = win;
                (void)ProcessWindowEvent(unsplitEvent);
            }
        }
    }
    //else: blocked by user
}

void ibSplitterWindow::OnUnsplit(wxWindow *winRemoved)
{
    // call this before calling the event handler which may delete the window
    winRemoved->Show(false);
}

wxPoint ibSplitterWindow::GetLastSplitPosition() const
{
    return m_lastSplitPosition;
}

void ibSplitterWindow::SetLastSplitPosition(const wxPoint& pos)
{
    m_lastSplitPosition = pos;
}


// SEAM vs wx: on every port (wx: MSW and macOS) - wx's own event, wherever the port sends it
void ibSplitterWindow::OnSetCursor(wxSetCursorEvent& event)
{
    // if we don't do it, the resizing cursor might be set for child window:
    // and like this we explicitly say that our cursor should not be used for
    // children windows which overlap us

    if ( SashHitTest(event.GetX(), event.GetY()) )
    {
        // default processing is ok
        event.Skip();
    }
    //else: do nothing, in particular, don't call Skip()
}
