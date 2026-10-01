// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/menu.cpp
// Purpose:     ibMenuItem, ibMenu and ibMenuBar implementation
// Author:      Vadim Zeitlin
// Created:     25.08.00
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

#include "frontend/uikit/menu.h"
#include "frontend/uikit/menuItem.h"
#include "frontend/uikit/window.h"
#include "frontend/uikit/theme.h"
#include "frontend/uikit/colourScheme.h"
#include "frontend/uikit/ctrl/control.h"   // ibControl::FindAccelIndex


#if wxUSE_MENUS

#include <wx/menu.h>
#include <wx/stockitem.h>

#ifndef WX_PRECOMP
    #include <wx/dynarray.h>
    #include <wx/control.h>      // for FindAccelIndex()
    #include <wx/settings.h>
    #include <wx/accel.h>
    #include <wx/log.h>
    #include <wx/frame.h>
    #include <wx/dcclient.h>
#endif // WX_PRECOMP

#include <wx/popupwin.h>
#include <wx/evtloop.h>

#include "frontend/uikit/renderer.h"

#ifdef __WXMSW__
    #include <wx/msw/private.h>
#endif // __WXMSW__

typedef wxMenuItemList::compatibility_iterator ibMenuItemIter;

// SEAM vs univ: the generic wxMenuBase machinery traffics in the native
// wxMenuItem*/wxMenu*; everything inside an ibMenu is created as
// ibMenuItem/ibMenu by the ibMenu factories, so these downcasts are safe
// (single inheritance chain — see the SEAM note in menu.h)
static inline ibMenuItem *ibItem(wxMenuItem *item)
    { return static_cast<ibMenuItem*>(item); }
static inline ibMenu *ibSub(wxMenu *menu)
    { return static_cast<ibMenu*>(menu); }

// ----------------------------------------------------------------------------
// ibMenuInfo contains all extra information about top level menus we need
// ----------------------------------------------------------------------------

#if 0 // UIKIT-REVIVE: menubar-only helper — cut until the univ frame revives

class ibMenuInfo
{
public:
    // ctor
    ibMenuInfo(const wxString& text)
    {
        SetLabel(text);
        SetEnabled();
    }

    // modifiers

    void SetLabel(const wxString& text)
    {
        m_originalLabel = text;

        // remember the accel char (may be -1 if none)
        m_indexAccel = ibControl::FindAccelIndex(text, &m_label);

        // calculate the width later, after the menu bar is created
        m_width = 0;
    }

    void SetEnabled(bool enabled = true) { m_isEnabled = enabled; }

    // accessors

    const wxString& GetLabel() const { return m_label; }
    const wxString& GetOriginalLabel() const { return m_originalLabel; }
    bool IsEnabled() const { return m_isEnabled; }
    wxCoord GetWidth(ibMenuBar *menubar) const
    {
        if ( !m_width )
        {
            wxConstCast(this, ibMenuInfo)->CalcWidth(menubar);
        }

        return m_width;
    }

    int GetAccelIndex() const { return m_indexAccel; }

private:
    void CalcWidth(ibMenuBar *menubar)
    {
        wxSize size;
        wxInfoDC dc(menubar);
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        dc.GetTextExtent(m_label, &size.x, &size.y);

        // adjust for the renderer we use and store the width
        m_width = menubar->GetRenderer()->GetMenuBarItemSize(size).x;
    }

    wxString m_label;
    wxString m_originalLabel;
    wxCoord m_width;
    int m_indexAccel;
    bool m_isEnabled;
};

#endif // UIKIT-REVIVE: menubar-only helper

// ----------------------------------------------------------------------------
// ibPopupMenuWindow: a popup window showing a menu
// ----------------------------------------------------------------------------

class ibPopupMenuWindow : public wxPopupTransientWindow
{
public:
    ibPopupMenuWindow(wxWindow *parent, ibMenu *menu);

    virtual ~ibPopupMenuWindow();

    // override the base class version to select the first item initially
    virtual void Popup(ibWindow *focus = nullptr);

    // override the base class version to dismiss any open submenus
    virtual void Dismiss();

    // called when a submenu is dismissed
    void OnSubmenuDismiss(bool dismissParent);

    // the default wxMSW wxPopupTransientWindow::OnIdle disables the capture
    // when the cursor is inside the popup, which dsables the menu tracking
    // so override it to do nothing
#ifdef __WXMSW__
    void OnIdle(wxIdleEvent& WXUNUSED(event)) { }
#endif

    // get the currently selected item (may be null)
    ibMenuItem *GetCurrentItem() const
    {
        return m_nodeCurrent ? ibItem(m_nodeCurrent->GetData()) : nullptr;
    }

    // find the menu item at given position
    ibMenuItemIter GetMenuItemFromPoint(const wxPoint& pt) const;

    // refresh the given item
    void RefreshItem(ibMenuItem *item);

    // preselect the first item
    void SelectFirst() { SetCurrentItem(m_menu->GetMenuItems().GetFirst()); }

    // process the key event, return true if done
    bool ProcessKeyDown(int key);

    // process mouse move event
    void ProcessMouseMove(const wxPoint& pt);

    // don't dismiss the popup window if the parent menu was clicked
    virtual bool ProcessLeftDown(wxMouseEvent& event);

protected:
    // how did we perform this operation?
    enum InputMethod
    {
        WithKeyboard,
        WithMouse
    };

    // notify the menu when the window disappears from screen
    virtual void OnDismiss();

    // SEAM vs univ: there the popup window was a univ wxWindow and DoDraw
    // overrode its paint pipeline; here the base is the native
    // wxPopupTransientWindow, so paint directly with the theme renderer
    void OnPaint(wxPaintEvent& event);

    // draw the menu inside this window
    void DoDraw(wxDC& dc, ibRenderer *rend);

    // event handlers
    void OnLeftDown(wxMouseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);

    // reset the current item and node
    void ResetCurrent();

    // set the current node and item without refreshing anything
    void SetCurrentItem(ibMenuItemIter node);

    // change the current item refreshing the old and new items
    void ChangeCurrent(ibMenuItemIter node);

    // activate item, i.e. call either ClickItem() or OpenSubmenu() depending
    // on what it is, return true if something was done (i.e. it's not a
    // separator...)
    bool ActivateItem(ibMenuItem *item, InputMethod how = WithKeyboard);

    // send the event about the item click
    void ClickItem(ibMenuItem *item);

    // show the submenu for this item
    void OpenSubmenu(ibMenuItem *item, InputMethod how = WithKeyboard);

    // can this time be opened?
    bool CanOpen(ibMenuItem *item)
    {
        return item && item->IsEnabled() && item->IsSubMenu();
    }

    // dismiss the menu and all parent menus too
    void DismissAndNotify();

    // react to dismissing this menu and also dismiss the parent if
    // dismissParent
    void HandleDismiss(bool dismissParent);

    // do we have an open submenu?
    bool HasOpenSubmenu() const { return m_hasOpenSubMenu; }

    // get previous node after the current one
    ibMenuItemIter GetPrevNode() const;

    // get previous node before the given one, wrapping if it's the first one
    ibMenuItemIter GetPrevNode(ibMenuItemIter node) const;

    // get next node after the current one
    ibMenuItemIter GetNextNode() const;

    // get next node after the given one, wrapping if it's the last one
    ibMenuItemIter GetNextNode(ibMenuItemIter node) const;

private:
    // the menu we show
    ibMenu *m_menu;

    // the menu node corresponding to the current item
    ibMenuItemIter m_nodeCurrent;

    // do we currently have an opened submenu?
    bool m_hasOpenSubMenu;

    wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibMenuKbdRedirector: an event handler which redirects kbd input to ibMenu
// ----------------------------------------------------------------------------

class ibMenuKbdRedirector : public wxEvtHandler
{
public:
    ibMenuKbdRedirector(ibMenu *menu) { m_menu = menu; }

    virtual bool ProcessEvent(wxEvent& event)
    {
        if ( event.GetEventType() == wxEVT_KEY_DOWN )
        {
            return m_menu->ProcessKeyDown(((wxKeyEvent &)event).GetKeyCode());
        }
        else
        {
            // return false;

            return wxEvtHandler::ProcessEvent(event);
        }
    }

private:
    ibMenu *m_menu;
};

// ----------------------------------------------------------------------------
// wxWin macros
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibPopupMenuWindow, wxPopupTransientWindow)
    EVT_PAINT(ibPopupMenuWindow::OnPaint)

    EVT_KEY_DOWN(ibPopupMenuWindow::OnKeyDown)

    EVT_LEFT_DOWN(ibPopupMenuWindow::OnLeftDown)
    EVT_RIGHT_DOWN(ibPopupMenuWindow::OnRightDown)
    EVT_LEFT_UP(ibPopupMenuWindow::OnLeftUp)
    EVT_MOTION(ibPopupMenuWindow::OnMouseMove)
    EVT_LEAVE_WINDOW(ibPopupMenuWindow::OnMouseLeave)
    EVT_MOUSE_CAPTURE_LOST(ibPopupMenuWindow::OnCaptureLost)
#ifdef __WXMSW__
    EVT_IDLE(ibPopupMenuWindow::OnIdle)
#endif
wxEND_EVENT_TABLE()

#if 0 // UIKIT-REVIVE: univ menubar needs univ frame
wxBEGIN_EVENT_TABLE(ibMenuBar, wxMenuBarBase)
    EVT_KILL_FOCUS(ibMenuBar::OnKillFocus)
    EVT_KEY_DOWN(ibMenuBar::OnKeyDown)
    EVT_LEFT_DOWN(ibMenuBar::OnLeftDown)
    EVT_LEFT_UP(ibMenuBar::OnLeftUp)
    EVT_MOTION(ibMenuBar::OnMouseMove)
    EVT_MOUSE_CAPTURE_LOST(ibMenuBar::OnCaptureLost)
wxEND_EVENT_TABLE()
#endif // UIKIT-REVIVE

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibPopupMenuWindow
// ----------------------------------------------------------------------------

ibPopupMenuWindow::ibPopupMenuWindow(wxWindow *parent, ibMenu *menu)
{
    m_menu = menu;
    m_hasOpenSubMenu = false;

    ResetCurrent();

    // SEAM vs univ: wxBORDER_RAISED here is the NATIVE 3-D edge; the flat
    // look draws its own 1px palette border in OnPaint instead
    (void)Create(parent, wxBORDER_NONE);

    SetCursor(wxCURSOR_ARROW);

    // SEAM vs univ: the univ window erased itself with the scheme colour;
    // the native base needs it set explicitly
    SetBackgroundColour(wxSCHEME_COLOUR(ibThemeEngine::Get()->GetColourScheme(),
                                        CONTROL));
}

ibPopupMenuWindow::~ibPopupMenuWindow()
{
    // When m_popupMenu in ibMenu is deleted because it
    // is a child of an old menu bar being deleted (note: it does
    // not get destroyed by the ibMenu destructor, but
    // by DestroyChildren()), m_popupMenu should be reset to nullptr.

    m_menu->m_popupMenu = nullptr;
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow current item/node handling
// ----------------------------------------------------------------------------

void ibPopupMenuWindow::ResetCurrent()
{
    SetCurrentItem(ibMenuItemIter());
}

void ibPopupMenuWindow::SetCurrentItem(ibMenuItemIter node)
{
    m_nodeCurrent = node;
}

void ibPopupMenuWindow::ChangeCurrent(ibMenuItemIter node)
{
    if ( !m_nodeCurrent || !node || (node != m_nodeCurrent) )
    {
        ibMenuItemIter nodeOldCurrent = m_nodeCurrent;

        m_nodeCurrent = node;

        if ( nodeOldCurrent )
        {
            ibMenuItem *item = ibItem(nodeOldCurrent->GetData());
            wxCHECK_RET( item, wxT("no current item?") );

            // if it was the currently opened menu, close it
            if ( node && item->IsSubMenu() && ibSub(item->GetSubMenu())->IsShown() )
            {
                ibSub(item->GetSubMenu())->Dismiss();
                OnSubmenuDismiss( false );
            }

            RefreshItem(item);
        }

        if ( m_nodeCurrent )
        {
            ibMenuItem *item = ibItem(m_nodeCurrent->GetData());
            if ( item && ibSub(item->GetMenu())->IsShown() )
            {
                RefreshItem(item);
            }
        }
    }
}

ibMenuItemIter ibPopupMenuWindow::GetPrevNode() const
{
    // return the last node if there had been no previously selected one
    return m_nodeCurrent ? GetPrevNode(m_nodeCurrent)
                         : ibMenuItemIter(m_menu->GetMenuItems().GetLast());
}

ibMenuItemIter
ibPopupMenuWindow::GetPrevNode(ibMenuItemIter node) const
{
    if ( node )
    {
        node = node->GetPrevious();
        if ( !node )
        {
            node = m_menu->GetMenuItems().GetLast();
        }
    }
    //else: the menu is empty

    return node;
}

ibMenuItemIter ibPopupMenuWindow::GetNextNode() const
{
    // return the first node if there had been no previously selected one
    return m_nodeCurrent ? GetNextNode(m_nodeCurrent)
                         : ibMenuItemIter(m_menu->GetMenuItems().GetFirst());
}

ibMenuItemIter
ibPopupMenuWindow::GetNextNode(ibMenuItemIter node) const
{
    if ( node )
    {
        node = node->GetNext();
        if ( !node )
        {
            node = m_menu->GetMenuItems().GetFirst();
        }
    }
    //else: the menu is empty

    return node;
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow popup/dismiss
// ----------------------------------------------------------------------------

void ibPopupMenuWindow::Popup(ibWindow *focus)
{
    // check that the current item had been properly reset before
    wxASSERT_MSG( !m_nodeCurrent ||
                  m_nodeCurrent == m_menu->GetMenuItems().GetFirst(),
                  wxT("menu current item preselected incorrectly") );

    wxPopupTransientWindow::Popup(focus);

    // the base class no-longer captures the mouse automatically when Popup
    // is called, so do it here to allow the menu tracking to work
    if ( !HasCapture() )
        CaptureMouse();

#ifdef __WXMSW__
    // ensure that this window is really on top of everything: without using
    // SetWindowPos() it can be covered by its parent menu which is not
    // really what we want
    ibMenu *menuParent = ibSub(m_menu->GetParent());
    if ( menuParent )
    {
        ibPopupMenuWindow *win = menuParent->m_popupMenu;

        // if we're shown, the parent menu must be also shown
        wxCHECK_RET( win, wxT("parent menu is not shown?") );

        if ( !::SetWindowPos(GetHwnd(), GetHwndOf(win),
                             0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW) )
        {
            wxLogLastError(wxT("SetWindowPos(HWND_TOP)"));
        }

        Refresh();
    }
#endif // __WXMSW__
}

void ibPopupMenuWindow::Dismiss()
{
    if ( HasOpenSubmenu() )
    {
        ibMenuItem *item = GetCurrentItem();
        wxCHECK_RET( item && item->IsSubMenu(), wxT("where is our open submenu?") );

        ibPopupMenuWindow *win = ibSub(item->GetSubMenu())->m_popupMenu;
        wxCHECK_RET( win, wxT("opened submenu is not opened?") );

        win->Dismiss();
        OnSubmenuDismiss( false );
    }

    if ( HasCapture() )
        ReleaseMouse();

    wxPopupTransientWindow::Dismiss();

    ResetCurrent();
}

void ibPopupMenuWindow::OnDismiss()
{
    // when we are dismissed because the user clicked elsewhere or we lost
    // focus in any other way, hide the parent menu as well
    HandleDismiss(true);
}

void ibPopupMenuWindow::OnSubmenuDismiss(bool WXUNUSED(dismissParent))
{
    m_hasOpenSubMenu = false;
}

void ibPopupMenuWindow::HandleDismiss(bool dismissParent)
{
    m_menu->OnDismiss(dismissParent);
}

void ibPopupMenuWindow::DismissAndNotify()
{
    Dismiss();
    HandleDismiss(true);
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow geometry
// ----------------------------------------------------------------------------

ibMenuItemIter
ibPopupMenuWindow::GetMenuItemFromPoint(const wxPoint& pt) const
{
    // we only use the y coord normally, but still check x in case the point is
    // outside the window completely
    if ( HitTest(pt) == wxHT_WINDOW_INSIDE )
    {
        wxCoord y = 0;
        for ( ibMenuItemIter node = m_menu->GetMenuItems().GetFirst();
              node;
              node = node->GetNext() )
        {
            ibMenuItem *item = ibItem(node->GetData());
            y += item->GetHeight();
            if ( y > pt.y )
            {
                // found
                return node;
            }
        }
    }

    return ibMenuItemIter();
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow drawing
// ----------------------------------------------------------------------------

void ibPopupMenuWindow::RefreshItem(ibMenuItem *item)
{
    wxCHECK_RET( item, wxT("can't refresh null item") );

    wxASSERT_MSG( IsShown(), wxT("can't refresh menu which is not shown") );

    // FIXME: -1 here because of SetLogicalOrigin(1, 1) in DoDraw()
    RefreshRect(wxRect(0, item->GetPosition() - 1,
                m_menu->GetGeometryInfo().GetSize().x, item->GetHeight()));
}

void ibPopupMenuWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dc(this);

    // flat 1px border around the popup (replaces the native raised edge);
    // DoDraw() then shifts the logical origin by (1, 1) to clear it
    dc.SetPen(wxPen(wxSCHEME_COLOUR(ibThemeEngine::Get()->GetColourScheme(),
                                    SHADOW_IN)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(wxRect(GetClientSize()));

    DoDraw(dc, m_menu->GetRenderer());
}

void ibPopupMenuWindow::DoDraw(wxDC& dc, ibRenderer *rend)
{
    // no clipping so far - do we need it? I don't think so as the menu is
    // never partially covered as it is always on top of everything

    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // FIXME: this should be done in the renderer, however when it is fixed
    //        ibPopupMenuWindow::RefreshItem() should be changed too!
    dc.SetLogicalOrigin(1, 1);

    wxCoord y = 0;
    const ibMenuGeometryInfo& gi = m_menu->GetGeometryInfo();
    for ( ibMenuItemIter node = m_menu->GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        ibMenuItem *item = ibItem(node->GetData());

        if ( item->IsSeparator() )
        {
            rend->DrawMenuSeparator(dc, y, gi);
        }
        else // not a separator
        {
            int flags = 0;
            if ( item->IsCheckable() )
            {
                flags |= wxCONTROL_CHECKABLE;

                if ( item->IsChecked() )
                {
                    flags |= wxCONTROL_CHECKED;
                }
            }

            if ( !item->IsEnabled() )
                flags |= wxCONTROL_DISABLED;

            if ( item->IsSubMenu() )
                flags |= wxCONTROL_ISSUBMENU;

            if ( item == GetCurrentItem() )
                flags |= wxCONTROL_SELECTED;

            wxBitmap bmp;

            if ( !item->IsEnabled() )
            {
                bmp = item->GetDisabledBitmap();
            }

            if ( !bmp.IsOk() )
            {
                // strangely enough, for unchecked item we use the
                // "checked" bitmap because this is the default one - this
                // explains this strange boolean expression
                bmp = item->GetBitmap(!item->IsCheckable() || item->IsChecked());
            }

            rend->DrawMenuItem
                  (
                     dc,
                     y,
                     gi,
                     item->GetItemLabelText(),
                     item->GetAccelString(),
                     bmp,
                     flags,
                     item->GetAccelIndex()
                  );
        }

        y += item->GetHeight();
    }
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow actions
// ----------------------------------------------------------------------------

void ibPopupMenuWindow::ClickItem(ibMenuItem *item)
{
    wxCHECK_RET( item, wxT("can't click null item") );

    wxASSERT_MSG( !item->IsSeparator() && !item->IsSubMenu(),
                  wxT("can't click this item") );

    ibMenu* menu = m_menu;

    // close all menus
    DismissAndNotify();

    menu->ClickItem(item);
}

void ibPopupMenuWindow::OpenSubmenu(ibMenuItem *item, InputMethod how)
{
    wxCHECK_RET( item, wxT("can't open null submenu") );

    ibMenu *submenu = ibSub(item->GetSubMenu());
    wxCHECK_RET( submenu, wxT("can only open submenus!") );

    // FIXME: should take into account the border width
    submenu->Popup(ClientToScreen(wxPoint(0, item->GetPosition())),
                   wxSize(m_menu->GetGeometryInfo().GetSize().x, 0),
                   how == WithKeyboard /* preselect first item then */);

    m_hasOpenSubMenu = true;
}

bool ibPopupMenuWindow::ActivateItem(ibMenuItem *item, InputMethod how)
{
    // don't activate disabled items
    if ( !item || !item->IsEnabled() )
    {
        return false;
    }

    // normal menu items generate commands, submenus can be opened and
    // the separators don't do anything
    if ( item->IsSubMenu() )
    {
        OpenSubmenu(item, how);
    }
    else if ( !item->IsSeparator() )
    {
        ClickItem(item);
    }
    else // separator, can't activate
    {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow input handling
// ----------------------------------------------------------------------------

bool ibPopupMenuWindow::ProcessLeftDown(wxMouseEvent& event)
{
    // wxPopupWindowHandler dismisses the window when the mouse is clicked
    // outside it which is usually just fine, but there is one case when we
    // don't want to do it: if the mouse was clicked on the parent submenu item
    // which opens this menu, so check for it

    wxPoint pos = event.GetPosition();
    if ( HitTest(pos.x, pos.y) == wxHT_WINDOW_OUTSIDE )
    {
        ibMenu *menu = ibSub(m_menu->GetParent());
        if ( menu )
        {
            ibPopupMenuWindow *win = menu->m_popupMenu;

            wxCHECK_MSG( win, false, wxT("parent menu not shown?") );

            pos = ClientToScreen(pos);
            if ( win->GetMenuItemFromPoint(win->ScreenToClient(pos)) )
            {
                // eat the event
                return true;
            }
            //else: it is outside the parent menu as well, do dismiss this one
        }
    }

    return false;
}

// SEAM vs univ: with the mouse captured a right click lands here and the
// canon ignored it — the menu looked stuck. Match the native behaviour:
// a right click outside dismisses the menu and immediately re-opens the
// context menu of the window under the cursor.
// the reopen request must NOT be queued while the menu modal loop is still
// running — the loop pumps pending events and the new DoPopupMenu would
// re-enter before ms_evtLoopPopup is cleared (assert); remember the spot
// and let DoPopupMenu fire it AFTER the loop exits
static bool s_pendingContextReopen = false;
static wxPoint s_pendingContextPos;

void ibPopupMenuWindow::OnRightDown(wxMouseEvent& event)
{
    if ( HitTest(event.GetPosition()) == wxHT_WINDOW_OUTSIDE )
    {
        s_pendingContextPos = ClientToScreen(event.GetPosition());
        s_pendingContextReopen = true;

        DismissAndNotify();
        return;
    }

    event.Skip();
}

void ibPopupMenuWindow::OnLeftDown(wxMouseEvent& event)
{
#if 0 // UIKIT-REVIVE: menubar forwarding needs univ frame
    ibMenuBar* menubar = m_menu->GetMenuBar();
    if ( menubar && !GetMenuItemFromPoint(event.GetPosition()) && !ProcessLeftDown(event) )
    {
        wxPoint pos = event.GetPosition();
        wxPoint posScreen = ClientToScreen(pos);
        event.SetPosition(menubar->ScreenToClient(posScreen));
        menubar->ProcessEvent(event);
    }
#endif // UIKIT-REVIVE

    // SEAM vs univ: there the generic wxPopupWindowHandler (pushed by the
    // generic Popup()) dismissed the menu on outside clicks; the MSW popup
    // relies on activation changes instead, which never happen while we hold
    // the mouse capture — so dismiss here ourselves
    if ( !ProcessLeftDown(event) &&
         HitTest(event.GetPosition()) == wxHT_WINDOW_OUTSIDE )
    {
        DismissAndNotify();
        return;
    }

    event.Skip();
}

void ibPopupMenuWindow::OnLeftUp(wxMouseEvent& event)
{
    ibMenuItemIter node = GetMenuItemFromPoint(event.GetPosition());
    if ( node )
    {
        ActivateItem(ibItem(node->GetData()), WithMouse);
    }
#if 0 // UIKIT-REVIVE: menubar forwarding needs univ frame
    else
    {
        ibMenuBar* menubar = m_menu->GetMenuBar();
        if ( menubar && !ProcessLeftDown(event) )
        {
            wxPoint pos = event.GetPosition();
            wxPoint posScreen = ClientToScreen(pos);
            event.SetPosition(menubar->ScreenToClient(posScreen));
            menubar->ProcessEvent(event);
        }
    }
#endif // UIKIT-REVIVE
}

void ibPopupMenuWindow::OnMouseMove(wxMouseEvent& event)
{
    const wxPoint pt = event.GetPosition();

    // we need to ignore extra mouse events: example when this happens is when
    // the mouse is on the menu and we open a submenu from keyboard - Windows
    // then sends us a dummy mouse move event, we (correctly) determine that it
    // happens in the parent menu and so immediately close the just opened
    // submenu!
#ifdef __WXMSW__
    static wxPoint s_ptLast;
    wxPoint ptCur = ClientToScreen(pt);
    if ( ptCur == s_ptLast )
    {
        return;
    }

    s_ptLast = ptCur;
#endif // __WXMSW__

    ProcessMouseMove(pt);

    event.Skip();
}

void ibPopupMenuWindow::ProcessMouseMove(const wxPoint& pt)
{
    ibMenuItemIter node = GetMenuItemFromPoint(pt);

    // don't reset current to nullptr here, we only do it when the mouse leaves
    // the window (see below)
    if ( node )
    {
        if ( !m_nodeCurrent || (node != m_nodeCurrent) )
        {
            ChangeCurrent(node);

            ibMenuItem *item = GetCurrentItem();
            if ( CanOpen(item) )
            {
                OpenSubmenu(item, WithMouse);
            }
        }
        //else: same item, nothing to do
    }
    else // not on an item
    {
        // the last open submenu forwards the mouse move messages to its
        // parent, so if the mouse moves to another item of the parent menu,
        // this menu is closed and this other item is selected - in the similar
        // manner, the top menu forwards the mouse moves to the menubar which
        // allows to select another top level menu by just moving the mouse

        // we need to translate our client coords to the client coords of the
        // window we forward this event to
        wxPoint ptScreen = ClientToScreen(pt);

        // if the mouse is outside this menu, let the parent one to
        // process it
        ibMenu *menuParent = ibSub(m_menu->GetParent());
        if ( menuParent )
        {
            ibPopupMenuWindow *win = menuParent->m_popupMenu;

            // if we're shown, the parent menu must be also shown
            wxCHECK_RET( win, wxT("parent menu is not shown?") );

            win->ProcessMouseMove(win->ScreenToClient(ptScreen));
        }
#if 0 // UIKIT-REVIVE: menubar forwarding needs univ frame
        else // no parent menu
        {
            ibMenuBar *menubar = m_menu->GetMenuBar();
            if ( menubar )
            {
                if ( menubar->ProcessMouseEvent(
                            menubar->ScreenToClient(ptScreen)) )
                {
                    // menubar has closed this menu and opened another one, probably
                    return;
                }
            }
        }
#endif // UIKIT-REVIVE
        //else: top level popup menu, no other processing to do
    }
}

void ibPopupMenuWindow::OnMouseLeave(wxMouseEvent& event)
{
    // due to the artefact of mouse events generation under MSW, we actually
    // may get the mouse leave event after the menu had been already dismissed
    // and calling ChangeCurrent() would then assert, so don't do it
    if ( IsShown() )
    {
        // we shouldn't change the current them if our submenu is opened and
        // mouse moved there, in this case the submenu is responsable for
        // handling it
        bool resetCurrent;
        if ( HasOpenSubmenu() )
        {
            ibMenuItem *item = GetCurrentItem();
            wxCHECK_RET( CanOpen(item), wxT("where is our open submenu?") );

            ibPopupMenuWindow *win = ibSub(item->GetSubMenu())->m_popupMenu;
            wxCHECK_RET( win, wxT("submenu is opened but not shown?") );

            // only handle this event if the mouse is not inside the submenu
            wxPoint pt = ClientToScreen(event.GetPosition());
            resetCurrent =
                win->HitTest(win->ScreenToClient(pt)) == wxHT_WINDOW_OUTSIDE;
        }
        else
        {
            // this menu is the last opened
            resetCurrent = true;
        }

        if ( resetCurrent )
        {
            ChangeCurrent(ibMenuItemIter());
        }
    }

    event.Skip();
}

void ibPopupMenuWindow::OnKeyDown(wxKeyEvent& event)
{
#if 0 // UIKIT-REVIVE: menubar forwarding needs univ frame
    ibMenuBar *menubar = m_menu->GetMenuBar();

    if ( menubar )
    {
        menubar->ProcessEvent(event);
    }
    else
#endif // UIKIT-REVIVE
    if ( !ProcessKeyDown(event.GetKeyCode()) )
    {
        event.Skip();
    }
}

bool ibPopupMenuWindow::ProcessKeyDown(int key)
{
    ibMenuItem *item = GetCurrentItem();

    // first let the opened submenu to have it (no test for IsEnabled() here,
    // the keys navigate even in a disabled submenu if we had somehow managed
    // to open it inspit of this)
    if ( HasOpenSubmenu() )
    {
        wxCHECK_MSG( CanOpen(item), false,
                     wxT("has open submenu but another item selected?") );

        if ( ibSub(item->GetSubMenu())->ProcessKeyDown(key) )
            return true;
    }

    bool processed = true;

    // handle the up/down arrows, home, end, esc and return here, pass the
    // left/right arrows to the menu bar except when the right arrow can be
    // used to open a submenu
    switch ( key )
    {
        case WXK_LEFT:
            // if we're not a top level menu, close us, else leave this to the
            // menubar
            if ( !m_menu->GetParent() )
            {
                processed = false;
                break;
            }

            // fall through

        case WXK_ESCAPE:
            // close just this menu
            Dismiss();
            HandleDismiss(false);
            break;

        case WXK_RETURN:
            processed = ActivateItem(item);
            break;

        case WXK_HOME:
            ChangeCurrent(m_menu->GetMenuItems().GetFirst());
            break;

        case WXK_END:
            ChangeCurrent(m_menu->GetMenuItems().GetLast());
            break;

        case WXK_UP:
        case WXK_DOWN:
            {
                bool up = key == WXK_UP;

                ibMenuItemIter nodeStart = up ? GetPrevNode() : GetNextNode(),
                               node = nodeStart;
                while ( node && node->GetData()->IsSeparator() )
                {
                    node = up ? GetPrevNode(node) : GetNextNode(node);

                    if ( node == nodeStart )
                    {
                        // nothing but separators and disabled items in this
                        // menu, break out
                        node = ibMenuItemIter();
                    }
                }

                if ( node )
                {
                    ChangeCurrent(node);
                }
                else
                {
                    processed = false;
                }
            }
            break;

        case WXK_RIGHT:
            // don't try to reopen an already opened menu
            if ( !HasOpenSubmenu() && CanOpen(item) )
            {
                OpenSubmenu(item);
            }
            else
            {
                processed = false;
            }
            break;

        default:
            // look for the menu item starting with this letter
            if ( wxIsalnum((wxChar)key) )
            {
                // we want to start from the item after this one because
                // if we're already on the item with the given accel we want to
                // go to the next one, not to stay in place
                ibMenuItemIter nodeStart = GetNextNode();

                // do we have more than one item with this accel?
                bool notUnique = false;

                // translate everything to lower case before comparing
                wxChar chAccel = (wxChar)wxTolower(key);

                // loop through all items searching for the item with this
                // accel
                ibMenuItemIter nodeFound,
                               node = nodeStart;
                for ( ;; )
                {
                    item = ibItem(node->GetData());

                    int idxAccel = item->GetAccelIndex();
                    if ( idxAccel != -1 &&
                         (wxChar)wxTolower(item->GetItemLabelText()[(size_t)idxAccel])
                            == chAccel )
                    {
                        // ok, found an item with this accel
                        if ( !nodeFound )
                        {
                            // store it but continue searching as we need to
                            // know if it's the only item with this accel or if
                            // there are more
                            nodeFound = node;
                        }
                        else // we already had found such item
                        {
                            notUnique = true;

                            // no need to continue further, we won't find
                            // anything we don't already know
                            break;
                        }
                    }

                    // we want to iterate over all items wrapping around if
                    // necessary
                    node = GetNextNode(node);
                    if ( node == nodeStart )
                    {
                        // we've seen all nodes
                        break;
                    }
                }

                if ( nodeFound )
                {
                    item = ibItem(nodeFound->GetData());

                    // go to this item anyhow
                    ChangeCurrent(nodeFound);

                    if ( !notUnique && item->IsEnabled() )
                    {
                        // unique item with this accel - activate it
                        processed = ActivateItem(item);
                    }
                    //else: just select it but don't activate as the user might
                    //      have wanted to activate another item

                    // skip "processed = false" below
                    break;
                }
            }

            processed = false;
    }

    return processed;
}

void ibPopupMenuWindow::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    DismissAndNotify();
}

// ----------------------------------------------------------------------------
// ibMenu
// ----------------------------------------------------------------------------

void ibMenu::Init()
{
    m_geometry = nullptr;

    m_popupMenu = nullptr;

    m_startRadioGroup = -1;
}

ibMenu::~ibMenu()
{
    delete m_geometry;
    delete m_popupMenu;
}

// ----------------------------------------------------------------------------
// ibMenu and ibMenuGeometryInfo
// ----------------------------------------------------------------------------

// ibMenuGeometryInfo::~ibMenuGeometryInfo() is defined inline in renderer.h

const ibMenuGeometryInfo& ibMenu::GetGeometryInfo() const
{
    if ( !m_geometry )
    {
        if ( m_popupMenu )
        {
            wxConstCast(this, ibMenu)->m_geometry =
                GetRenderer()->GetMenuGeometry(m_popupMenu, *this);
        }
        else
        {
            wxFAIL_MSG( wxT("can't get geometry without window") );
        }
    }

    return *m_geometry;
}

void ibMenu::InvalidateGeometryInfo()
{
    wxDELETE(m_geometry);
}

// ----------------------------------------------------------------------------
// ibMenu adding/removing items
// ----------------------------------------------------------------------------

void ibMenu::OnItemAdded(ibMenuItem *item)
{
    InvalidateGeometryInfo();

#if 0 // UIKIT-REVIVE: univ accel-table API not in the native build
    AddAccelFor(item);
#else
    wxUnusedVar(item);
#endif // wxUSE_ACCEL
}

void ibMenu::EndRadioGroup()
{
    // we're not inside a radio group any longer
    m_startRadioGroup = -1;
}

// the creating factories: the only places where items are born, so the
// ibItem()/ibSub() downcasts everywhere else stay safe
wxMenuItem *ibMenu::Append(int itemid, const wxString& text,
                           const wxString& help, wxItemKind kind)
{
    return wxMenuBase::Append(new ibMenuItem(this, itemid, text, help, kind));
}

wxMenuItem *ibMenu::AppendSeparator()
{
    return wxMenuBase::Append(new ibMenuItem(this, wxID_SEPARATOR));
}

wxMenuItem *ibMenu::AppendCheckItem(int itemid, const wxString& text,
                                    const wxString& help)
{
    return wxMenuBase::Append(new ibMenuItem(this, itemid, text, help,
                                             wxITEM_CHECK));
}

wxMenuItem *ibMenu::AppendRadioItem(int itemid, const wxString& text,
                                    const wxString& help)
{
    return wxMenuBase::Append(new ibMenuItem(this, itemid, text, help,
                                             wxITEM_RADIO));
}

wxMenuItem *ibMenu::AppendSubMenu(ibMenu *submenu, const wxString& text,
                                  const wxString& help)
{
    return wxMenuBase::Append(new ibMenuItem(this, wxID_ANY, text, help,
                                             wxITEM_NORMAL, submenu));
}

wxMenuItem* ibMenu::DoAppend(wxMenuItem *itemBase)
{
    ibMenuItem *item = ibItem(itemBase);

    if ( item->GetKind() == wxITEM_RADIO )
    {
        int count = GetMenuItemCount();

        if ( m_startRadioGroup == -1 )
        {
            // start a new radio group
            m_startRadioGroup = count;

            // for now it has just one element
            item->SetAsRadioGroupStart();
            item->SetRadioGroupEnd(m_startRadioGroup);

            if ( !wxMenuBase::DoAppend(item) )
                return nullptr;

            item->Check(true);
        }
        else // extend the current radio group
        {
            // we need to update its end item
            item->SetRadioGroupStart(m_startRadioGroup);
            ibMenuItemIter node = GetMenuItems().Item(m_startRadioGroup);

            if ( node )
            {
                ibItem(node->GetData())->SetRadioGroupEnd(count);
            }
            else
            {
                wxFAIL_MSG( wxT("where is the radio group start item?") );
            }

            if ( !wxMenuBase::DoAppend(item) )
                return nullptr;
        }
    }
    else // not a radio item
    {
        EndRadioGroup();

        if ( !wxMenuBase::DoAppend(item) )
            return nullptr;
    }

    OnItemAdded(item);

    return item;
}

wxMenuItem* ibMenu::DoInsert(size_t pos, wxMenuItem *itemBase)
{
    ibMenuItem *item = ibItem(itemBase);

    if ( item->GetKind() == wxITEM_RADIO )
    {
        unsigned int start, end;
        ibMenuItemIter firstRadio;

        if ( m_startRadioGroup == -1 )
        {
            // start a new radio group
            m_startRadioGroup = pos;

            // set this element as the first of radio group
            item->SetAsRadioGroupStart();
            item->SetRadioGroupEnd(m_startRadioGroup);

            if ( !wxMenuBase::DoInsert(pos, item) )
                return nullptr;

            item->Check(true);
        }
        else // extend the current radio group
        {
            // get current first radio item in radio group
            firstRadio = GetMenuItems().Item(m_startRadioGroup);

            // get current radio group range
            start = ibItem(firstRadio->GetData())->GetRadioGroupStart();
            end = ibItem(firstRadio->GetData())->GetRadioGroupEnd();

            if ( pos <= start )
            {
                // Item is inserted in the beginning of the range
                // we need to update its end item
                m_startRadioGroup = pos;
                item->SetAsRadioGroupStart();
                item->SetRadioGroupEnd(end + 1);
            }
            else if ( (pos >= start) && (pos <= end) )
            {
                // we need to update its end item
                item->SetRadioGroupStart(m_startRadioGroup);

                // Item is inserted in the middle of this range or immediately
                // after it in which case it extends this range so make it span
                // one more item in any case.
                if ( firstRadio )
                {
                    ibItem(firstRadio->GetData())->SetRadioGroupEnd(end + 1);
                }
                else
                {
                    wxFAIL_MSG( wxT("where is the radio group start item?") );
                }
            }

            if ( !wxMenuBase::DoInsert(pos, item) )
                return nullptr;
        }
    }
    else
    {
        if ( !wxMenuBase::DoInsert(pos, item) )
            return nullptr;
    }

    OnItemAdded(item);

    return item;
}

wxMenuItem *ibMenu::DoRemove(wxMenuItem *item)
{
    wxMenuItem *itemOld = wxMenuBase::DoRemove(item);

    if ( itemOld )
    {
        InvalidateGeometryInfo();

#if 0 // UIKIT-REVIVE: univ accel-table API not in the native build
        RemoveAccelFor(ibItem(item));
#endif // wxUSE_ACCEL
    }

    return itemOld;
}

// ----------------------------------------------------------------------------
// ibMenu attaching/detaching
// ----------------------------------------------------------------------------

void ibMenu::Attach(wxMenuBarBase *menubar)
{
    wxMenuBase::Attach(menubar);

    wxCHECK_RET( m_menuBar, wxT("menubar can't be null after attaching") );

    // unfortunately, we can't use m_menuBar->GetEventHandler() here because,
    // if the menubar is currently showing a menu, its event handler is a
    // temporary one installed by wxPopupWindow and so will disappear soon and
    // any attempts to use it from the newly attached menu would result in a
    // crash
    //
    // so we use the menubar itself, even if it's a pity as it means we can't
    // redirect all menu events by changing the menubar handler (FIXME)
    SetNextHandler(m_menuBar);
}

void ibMenu::Detach()
{
    // After the menu is detached from the menu bar, it shouldn't send its
    // events to it.
    SetNextHandler(nullptr);

    wxMenuBase::Detach();
}

// ----------------------------------------------------------------------------
// ibMenu misc functions
// ----------------------------------------------------------------------------

ibWindow *ibMenu::GetRootWindow() const
{
    // SEAM vs univ: the menubar branch waits for the univ frame; GetWindow()
    // walks up the submenu chain to the root menu's invoking window, which
    // our ibWindow::PopupMenu(ibMenu*) entry guarantees to be an ibWindow
    return wxDynamicCast(GetWindow(), ibWindow);
}

ibRenderer *ibMenu::GetRenderer() const
{
    // SEAM vs univ: there the popup window was a univ wxWindow carrying a
    // renderer; here take it from the invoking ibWindow (theme fallback)
    ibWindow *win = GetRootWindow();
    return win ? win->GetRenderer() : ibThemeEngine::Get()->GetRenderer();
}

void ibMenu::RefreshItem(ibMenuItem *item)
{
    // the item geometry changed, so our might have changed as well
    InvalidateGeometryInfo();

    if ( IsShown() )
    {
        // this would be a bug in IsShown()
        wxCHECK_RET( m_popupMenu, wxT("must have popup window if shown!") );

        // recalc geometry to update the item height and such
        (void)GetGeometryInfo();

        m_popupMenu->RefreshItem(item);
    }
}

// ----------------------------------------------------------------------------
// ibMenu showing and hiding
// ----------------------------------------------------------------------------

bool ibMenu::IsShown() const
{
    return m_popupMenu && m_popupMenu->IsShown();
}

void ibMenu::OnDismiss(bool dismissParent)
{
    if ( m_menuParent )
    {
        // always notify the parent about submenu disappearance
        ibPopupMenuWindow *win = ibSub(m_menuParent)->m_popupMenu;
        if ( win )
        {
            win->OnSubmenuDismiss( true );
        }
        else
        {
            wxFAIL_MSG( wxT("parent menu not shown?") );
        }

        // and if we dismiss everything, propagate to parent
        if ( dismissParent )
        {
            // dismissParent is recursive
            ibSub(m_menuParent)->Dismiss();
            ibSub(m_menuParent)->OnDismiss(true);
        }
    }
    else // no parent menu
    {
#if 0 // UIKIT-REVIVE: univ menubar needs univ frame
        // notify the menu bar if we're a top level menu
        if ( m_menuBar )
        {
            m_menuBar->OnDismissMenu(dismissParent);
        }
        else // popup menu
#else
        wxUnusedVar(dismissParent);
#endif // UIKIT-REVIVE
        {
            ibWindow * const win = wxDynamicCast(GetInvokingWindow(), ibWindow);
            wxCHECK_RET( win, wxT("what kind of menu is this?") );

            win->DismissPopupMenu();
        }
    }
}

void ibMenu::Popup(const wxPoint& pos, const wxSize& size, bool selectFirst)
{
    // create the popup window if not done yet
    if ( !m_popupMenu )
    {
        m_popupMenu = new ibPopupMenuWindow(GetRootWindow(), this);
    }

    // select the first item unless disabled
    if ( selectFirst )
    {
        m_popupMenu->SelectFirst();
    }

    // the geometry might have changed since the last time we were shown, so
    // always resize
    m_popupMenu->SetClientSize(GetGeometryInfo().GetSize());

    // position it as specified
    m_popupMenu->Position(pos, size);

    // the menu can't have the focus itself (it is a Windows limitation), so
    // always keep the focus at the originating window
    ibWindow *focus = GetRootWindow();

    wxASSERT_MSG( focus, wxT("no window to keep focus on?") );

    // and show it
    m_popupMenu->Popup(focus);
}

void ibMenu::Dismiss()
{
    wxCHECK_RET( IsShown(), wxT("can't dismiss hidden menu") );

    m_popupMenu->Dismiss();
}

// ----------------------------------------------------------------------------
// ibMenu event processing
// ----------------------------------------------------------------------------

bool ibMenu::ProcessKeyDown(int key)
{
    wxCHECK_MSG( m_popupMenu, false,
                 wxT("can't process key events if not shown") );

    return m_popupMenu->ProcessKeyDown(key);
}

bool ibMenu::ClickItem(ibMenuItem *item)
{
    int isChecked;
    if ( item->IsCheckable() )
    {
        // update the item state
        isChecked = !item->IsChecked();

        item->Check(isChecked != 0);
    }
    else
    {
        // not applicable
        isChecked = -1;
    }

    return SendEvent(item->GetId(), isChecked);
}

// ----------------------------------------------------------------------------
// ibMenu accel support
// ----------------------------------------------------------------------------

#if 0 // UIKIT-REVIVE: the univ accel-table API (Add/Remove/GetMenuItem) does
      // not exist in the native wxAcceleratorTable build

bool ibMenu::ProcessAccelEvent(const wxKeyEvent& event)
{
    // do we have an item for this accel?
    ibMenuItem *item = m_accelTable.GetMenuItem(event);
    if ( item && item->IsEnabled() )
    {
        return ClickItem(item);
    }

    // try our submenus
    for ( ibMenuItemIter node = GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        const ibMenuItem *item = node->GetData();
        if ( item->IsSubMenu() && item->IsEnabled() )
        {
            // try its elements
            if ( item->GetSubMenu()->ProcessAccelEvent(event) )
            {
                return true;
            }
        }
    }

    return false;
}

void ibMenu::AddAccelFor(ibMenuItem *item)
{
    wxAcceleratorEntry *accel = item->GetAccel();
    if ( accel )
    {
        accel->SetMenuItem(item);

        m_accelTable.Add(*accel);

        delete accel;
    }
}

void ibMenu::RemoveAccelFor(ibMenuItem *item)
{
    wxAcceleratorEntry *accel = item->GetAccel();
    if ( accel )
    {
        m_accelTable.Remove(*accel);

        delete accel;
    }
}

#endif // wxUSE_ACCEL

// ----------------------------------------------------------------------------
// ibMenuItem construction
// ----------------------------------------------------------------------------

ibMenuItem::ibMenuItem(ibMenu *parentMenu,
                       int id,
                       const wxString& text,
                       const wxString& help,
                       wxItemKind kind,
                       ibMenu *subMenu)
          : wxMenuItem(parentMenu, id, text, help, kind, subMenu)
{
    m_posY =
    m_height = wxDefaultCoord;

    m_radioGroup.start = -1;
    m_isRadioGroupStart = false;

    UpdateAccelInfo();
}

ibMenuItem::~ibMenuItem()
{
}

// ----------------------------------------------------------------------------
// wxMenuItemBase methods implemented here
// ----------------------------------------------------------------------------

#if 0 // SEAM vs univ: the native wxMenuItem::New factory already exists in
      // the host build; ibMenu's creating factories construct ibMenuItem
      // directly, so this univ definition is cut
/* static */
ibMenuItem *wxMenuItemBase::New(ibMenu *parentMenu,
                                int id,
                                const wxString& name,
                                const wxString& help,
                                wxItemKind kind,
                                ibMenu *subMenu)
{
    return new ibMenuItem(parentMenu, id, name, help, kind, subMenu);
}
#endif // SEAM

// ----------------------------------------------------------------------------
// ibMenuItem operations
// ----------------------------------------------------------------------------

void ibMenuItem::NotifyMenu()
{
    ibSub(m_parentMenu)->RefreshItem(this);
}

void ibMenuItem::UpdateAccelInfo()
{
    m_indexAccel = wxControlBase::FindAccelIndex(m_text);

    // will be empty if the text contains no TABs - ok
    m_strAccel = m_text.AfterFirst(wxT('\t'));
}

void ibMenuItem::SetItemLabel(const wxString& text)
{
    if ( text != m_text )
    {
        // first call the base class version to change m_text
        // (and also check if we don't have a stock menu item)
        wxMenuItemBase::SetItemLabel(text);

        UpdateAccelInfo();

        NotifyMenu();
    }
}

void ibMenuItem::SetCheckable(bool checkable)
{
    if ( checkable != IsCheckable() )
    {
        wxMenuItemBase::SetCheckable(checkable);

        NotifyMenu();
    }
}

void ibMenuItem::SetBitmaps(const wxBitmapBundle& bmpChecked,
                            const wxBitmapBundle& bmpUnchecked)
{
    m_bitmap = bmpChecked;
    m_bmpUnchecked = bmpUnchecked;

    NotifyMenu();
}

void ibMenuItem::Enable(bool enable)
{
    if ( enable != m_isEnabled )
    {
        wxMenuItemBase::Enable(enable);

        NotifyMenu();
    }
}

void ibMenuItem::Check(bool check)
{
    wxCHECK_RET( IsCheckable(), wxT("only checkable items may be checked") );

    if ( m_isChecked == check )
        return;

    if ( GetKind() == wxITEM_RADIO )
    {
        // it doesn't make sense to uncheck a radio item - what would this do?
        if ( !check )
            return;

        // get the index of this item in the menu
        const wxMenuItemList& items = m_parentMenu->GetMenuItems();
        int pos = items.IndexOf(this);
        wxCHECK_RET( pos != wxNOT_FOUND,
                     wxT("menuitem not found in the menu items list?") );

        // get the radio group range
        int start,
            end;

        if ( m_isRadioGroupStart )
        {
            // we already have all information we need
            start = pos;
            end = m_radioGroup.end;
        }
        else // next radio group item
        {
            // get the radio group end from the start item
            start = m_radioGroup.start;
            end = ibItem(items.Item(start)->GetData())->m_radioGroup.end;
        }

        // also uncheck all the other items in this radio group
        ibMenuItemIter node = items.Item(start);
        for ( int n = start; n <= end && node; n++ )
        {
            if ( n != pos )
            {
                ibItem(node->GetData())->m_isChecked = false;
            }
            node = node->GetNext();
        }
    }

    wxMenuItemBase::Check(check);

    NotifyMenu();
}

// radio group stuff
// -----------------

void ibMenuItem::SetAsRadioGroupStart()
{
    m_isRadioGroupStart = true;
}

void ibMenuItem::SetRadioGroupStart(int start)
{
    wxASSERT_MSG( !m_isRadioGroupStart,
                  wxT("should only be called for the next radio items") );

    m_radioGroup.start = start;
}

void ibMenuItem::SetRadioGroupEnd(int end)
{
    wxASSERT_MSG( m_isRadioGroupStart,
                  wxT("should only be called for the first radio item") );

    m_radioGroup.end = end;
}

int ibMenuItem::GetRadioGroupStart()
{
    return m_radioGroup.start;
}

int ibMenuItem::GetRadioGroupEnd()
{
    return m_radioGroup.end;
}

// ----------------------------------------------------------------------------
// wxWin RTTI (the univ build had these in the port-specific files)
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibMenu, wxMenu);
wxIMPLEMENT_DYNAMIC_CLASS(ibMenuItem, wxMenuItem);

// ----------------------------------------------------------------------------
// ibMenuBar creation
// ----------------------------------------------------------------------------

#if 0 // UIKIT-REVIVE: univ menubar needs the univ frame — the whole
      // implementation block is cut until ibFrame revives

void ibMenuBar::Init()
{
    m_frameLast = nullptr;

    m_current = -1;

    m_menuShown = nullptr;

    m_shouldShowMenu = false;
}

ibMenuBar::ibMenuBar(long WXUNUSED(style))
{
    Init();
}

ibMenuBar::ibMenuBar(size_t n, ibMenu *menus[], const wxString titles[], long WXUNUSED(style))
{
    Init();

    for (size_t i = 0; i < n; ++i )
        Append(menus[i], titles[i]);
}

void ibMenuBar::Attach(ibFrame *frame)
{
    // maybe you really wanted to call Detach()?
    wxCHECK_RET( frame, wxT("ibMenuBar::Attach(nullptr) called") );

    wxMenuBarBase::Attach(frame);

    if ( IsCreated() )
    {
        // reparent if necessary
        if ( m_frameLast != frame )
        {
            Reparent(frame);
        }

        // show it back - was hidden by Detach()
        Show();
    }
    else // not created yet, do it now
    {
        // we have no way to return the error from here anyhow :-(
        (void)Create(frame, wxID_ANY);

        SetCursor(wxCURSOR_ARROW);

        SetFont(wxSystemSettings::GetFont(wxSYS_SYSTEM_FONT));

        // calculate and set our height (it won't be changed any more)
        SetSize(wxDefaultCoord, GetBestSize().y);
    }

    // remember the last frame which had us to avoid unnecessarily reparenting
    // above
    m_frameLast = frame;
}

void ibMenuBar::Detach()
{
    // don't delete the window because we may be reattached later, just hide it
    if ( m_frameLast )
    {
        Hide();
    }

    wxMenuBarBase::Detach();
}

ibMenuBar::~ibMenuBar()
{
}

// ----------------------------------------------------------------------------
// ibMenuBar adding/removing items
// ----------------------------------------------------------------------------

bool ibMenuBar::Append(ibMenu *menu, const wxString& title)
{
    return Insert(GetCount(), menu, title);
}

bool ibMenuBar::Insert(size_t pos, ibMenu *menu, const wxString& title)
{
    if ( !wxMenuBarBase::Insert(pos, menu, title) )
        return false;

    menu->SetTitle( title );

    m_menuInfos.insert(m_menuInfos.begin() + pos, ibMenuInfo(title));

    RefreshAllItemsAfter(pos);

    return true;
}

ibMenu *ibMenuBar::Replace(size_t pos, ibMenu *menu, const wxString& title)
{
    ibMenu *menuOld = wxMenuBarBase::Replace(pos, menu, title);

    if ( menuOld )
    {
        ibMenuInfo& info = m_menuInfos[pos];

        info.SetLabel(title);

        // even if the old menu was disabled, the new one is not any more
        info.SetEnabled();

        // even if we change only this one, the new label has different width,
        // so we need to refresh everything beyond this item as well
        RefreshAllItemsAfter(pos);
    }

    return menuOld;
}

ibMenu *ibMenuBar::Remove(size_t pos)
{
    ibMenu *menuOld = wxMenuBarBase::Remove(pos);

    if ( menuOld )
    {
        m_menuInfos.erase(m_menuInfos.begin() + pos);

        // this doesn't happen too often, so don't try to be too smart - just
        // refresh everything
        Refresh();
    }

    return menuOld;
}

// ----------------------------------------------------------------------------
// ibMenuBar top level menus access
// ----------------------------------------------------------------------------

size_t ibMenuBar::GetCount() const
{
    return m_menuInfos.size();
}

wxCoord ibMenuBar::GetItemWidth(size_t pos) const
{
    return m_menuInfos[pos].GetWidth(wxConstCast(this, ibMenuBar));
}

void ibMenuBar::EnableTop(size_t pos, bool enable)
{
    wxCHECK_RET( pos < GetCount(), wxT("invalid index in EnableTop") );

    if ( enable != m_menuInfos[pos].IsEnabled() )
    {
        m_menuInfos[pos].SetEnabled(enable);

        RefreshItem(pos);
    }
    //else: nothing to do
}

bool ibMenuBar::IsEnabledTop(size_t pos) const
{
    wxCHECK_MSG( pos < GetCount(), false, wxT("invalid index in IsEnabledTop") );

    return m_menuInfos[pos].IsEnabled();
}

void ibMenuBar::SetMenuLabel(size_t pos, const wxString& label)
{
    wxCHECK_RET( pos < GetCount(), wxT("invalid index in SetMenuLabel") );

    if ( label != m_menuInfos[pos].GetOriginalLabel() )
    {
        m_menuInfos[pos].SetLabel(label);

        RefreshItem(pos);
    }
    //else: nothing to do
}

wxString ibMenuBar::GetMenuLabel(size_t pos) const
{
    wxCHECK_MSG( pos < GetCount(), wxEmptyString, wxT("invalid index in GetMenuLabel") );

    return m_menuInfos[pos].GetOriginalLabel();
}

// ----------------------------------------------------------------------------
// ibMenuBar drawing
// ----------------------------------------------------------------------------

void ibMenuBar::RefreshAllItemsAfter(size_t pos)
{
    if ( !IsCreated() )
    {
        // no need to refresh if nothing is shown yet
        return;
    }

    wxRect rect = GetItemRect(pos);
    rect.width = GetClientSize().x - rect.x;
    RefreshRect(rect);
}

void ibMenuBar::RefreshItem(size_t pos)
{
    wxCHECK_RET( pos != (size_t)-1,
                 wxT("invalid item in ibMenuBar::RefreshItem") );

    if ( !IsCreated() )
    {
        // no need to refresh if nothing is shown yet
        return;
    }

    RefreshRect(GetItemRect(pos));
}

void ibMenuBar::DoDraw(ibControlRenderer *renderer)
{
    wxDC& dc = renderer->GetDC();
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // redraw only the items which must be redrawn

    // we don't have to use GetUpdateClientRect() here because our client rect
    // is the same as total one
    wxRect rectUpdate = GetUpdateRegion().GetBox();

    int flagsMenubar = GetStateFlags();

    wxRect rect;
    rect.y = 0;
    rect.height = GetClientSize().y;

    wxCoord x = 0;
    size_t count = GetCount();
    for ( size_t n = 0; n < count; n++ )
    {
        if ( x > rectUpdate.GetRight() )
        {
            // all remaining items are to the right of rectUpdate
            break;
        }

        rect.x = x;
        rect.width = GetItemWidth(n);
        x += rect.width;
        if ( x < rectUpdate.x )
        {
            // this item is still to the left of rectUpdate
            continue;
        }

        int flags = flagsMenubar;
        if ( m_current != -1 && n == (size_t)m_current )
        {
            flags |= wxCONTROL_SELECTED;
        }

        if ( !IsEnabledTop(n) )
        {
            flags |= wxCONTROL_DISABLED;
        }

        GetRenderer()->DrawMenuBarItem
                       (
                            dc,
                            rect,
                            m_menuInfos[n].GetLabel(),
                            flags,
                            m_menuInfos[n].GetAccelIndex()
                       );
    }
}

// ----------------------------------------------------------------------------
// ibMenuBar geometry
// ----------------------------------------------------------------------------

wxRect ibMenuBar::GetItemRect(size_t pos) const
{
    wxASSERT_MSG( pos < GetCount(), wxT("invalid menu bar item index") );
    wxASSERT_MSG( IsCreated(), wxT("can't call this method yet") );

    wxRect rect;
    rect.x =
    rect.y = 0;
    rect.height = GetClientSize().y;

    for ( size_t n = 0; n < pos; n++ )
    {
        rect.x += GetItemWidth(n);
    }

    rect.width = GetItemWidth(pos);

    return rect;
}

wxSize ibMenuBar::DoGetBestClientSize() const
{
    wxSize size;
    if ( GetMenuCount() > 0 )
    {
        wxInfoDC dc(wxConstCast(this, ibMenuBar));
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        dc.GetTextExtent(GetMenuLabel(0), &size.x, &size.y);

        // adjust for the renderer we use
        size = GetRenderer()->GetMenuBarItemSize(size);
    }
    else // empty menubar
    {
        size.x =
        size.y = 0;
    }

    // the width is arbitrary, of course, for horizontal menubar
    size.x = 100;

    return size;
}

int ibMenuBar::GetMenuFromPoint(const wxPoint& pos) const
{
    if ( pos.x < 0 || pos.y < 0 || pos.y > GetClientSize().y )
        return -1;

    // do find it
    wxCoord x = 0;
    size_t count = GetCount();
    for ( size_t item = 0; item < count; item++ )
    {
        x += GetItemWidth(item);

        if ( x > pos.x )
        {
            return item;
        }
    }

    // to the right of the last menu item
    return -1;
}

// ----------------------------------------------------------------------------
// ibMenuBar menu operations
// ----------------------------------------------------------------------------

void ibMenuBar::SelectMenu(size_t pos)
{
    SetFocus();
    wxLogTrace(wxT("mousecapture"), wxT("Capturing mouse from ibMenuBar::SelectMenu"));
    CaptureMouse();

    DoSelectMenu(pos);
}

void ibMenuBar::DoSelectMenu(size_t pos)
{
    wxCHECK_RET( pos < GetCount(), wxT("invalid menu index in DoSelectMenu") );

    int posOld = m_current;

    m_current = pos;

    if ( posOld != -1 )
    {
        // close the previous menu
        if ( IsShowingMenu() )
        {
            // restore m_shouldShowMenu flag after DismissMenu() which resets
            // it to false
            bool old = m_shouldShowMenu;

            DismissMenu();

            m_shouldShowMenu = old;
        }

        RefreshItem((size_t)posOld);
    }

    RefreshItem(pos);
}

void ibMenuBar::PopupMenu(size_t pos)
{
    wxCHECK_RET( pos < GetCount(), wxT("invalid menu index in PopupCurrentMenu") );

    SetFocus();
    DoSelectMenu(pos);
    PopupCurrentMenu();
}

// ----------------------------------------------------------------------------
// ibMenuBar input handing
// ----------------------------------------------------------------------------

/*
   Note that ibMenuBar doesn't use ibInputHandler but handles keyboard and
   mouse in the same way under all platforms. This is because it doesn't derive
   from ibControl (which works with input handlers) but directly from ibWindow.

   Also, menu bar input handling is rather simple, so maybe it's not really
   worth making it themeable - at least I've decided against doing it now as it
   would merging the changes back into trunk more difficult. But it still could
   be done later if really needed.
 */

void ibMenuBar::OnKillFocus(wxFocusEvent& event)
{
    if ( m_current != -1 )
    {
        RefreshItem((size_t)m_current);

        m_current = -1;
    }

    event.Skip();
}

void ibMenuBar::OnLeftDown(wxMouseEvent& event)
{
    if ( HasCapture() )
    {
        OnDismiss();
        event.Skip();
    }
    else // we didn't have mouse capture, capture it now
    {
        int item = GetMenuFromPoint(event.GetPosition());
        if ( item == -1 )
        {
            if ( IsShowingMenu() )
            {
                DismissMenu(); // event outside menubar - dismiss
                ReleaseMouseCapture(); // we could get capture back from popup window so release it
                RefreshItem((size_t)m_current);
                m_current = -1;
            }
        }
        else if ( item == m_current && IsShowingMenu() )
        {
            // double-click
            DismissMenu();
            ReleaseMouseCapture();
            RefreshItem((size_t)m_current);
            m_current = -1;
        }
        else // on item
        {
            m_current = item;

            wxLogTrace(wxT("mousecapture"), wxT("Capturing mouse from ibMenuBar::OnLeftDown"));
            CaptureMouse();

            // show it as selected
            RefreshItem((size_t)m_current);

            // show the menu
            PopupCurrentMenu(false /* don't select first item - as Windows does */);
        }
    }
}

void ibMenuBar::OnLeftUp(wxMouseEvent& event)
{
    if ( !HasCapture() && IsShowingMenu() && GetMenuFromPoint(event.GetPosition()) == -1 )
    {
        DismissMenu();
        ReleaseMouseCapture();
        RefreshItem((size_t)m_current);
        m_current = -1;
    }
}

void ibMenuBar::OnMouseMove(wxMouseEvent& event)
{
    if ( HasCapture() )
    {
        (void)ProcessMouseEvent(event.GetPosition());
    }
    else
    {
        event.Skip();
    }
}

bool ibMenuBar::ProcessMouseEvent(const wxPoint& pt)
{
    // a hack to ignore the extra mouse events MSW sends us: this is similar to
    // wxUSE_MOUSEEVENT_HACK in wxWin itself but it isn't enough for us here as
    // we get the messages from different windows (old and new popup menus for
    // example)
#ifdef __WXMSW__
    static wxPoint s_ptLast;
    if ( pt == s_ptLast )
    {
        return false;
    }

    s_ptLast = pt;
#endif // __WXMSW__

    int currentNew = GetMenuFromPoint(pt);
    if ( (currentNew == -1) || (currentNew == m_current) )
    {
        return false;
    }

    // select the new active item
    DoSelectMenu(currentNew);

    // show the menu if we know that we should, even if we hadn't been showing
    // it before (this may happen if the previous menu was disabled)
    if ( m_shouldShowMenu && !m_menuShown)
    {
        // open the new menu if the old one we closed had been opened
        PopupCurrentMenu(false /* don't select first item - as Windows does */);
    }

    return true;
}

void ibMenuBar::OnKeyDown(wxKeyEvent& event)
{
    // ensure that we have a current item - we might not have it if we're
    // given the focus with Alt or F10 press (and under GTK+ the menubar
    // somehow gets the keyboard events even when it doesn't have focus...)
    if ( m_current == -1 )
    {
        if ( !HasCapture() )
        {
            SelectMenu(0);
        }
        else // we do have capture
        {
            // we always maintain a valid current item while we're in modal
            // state (i.e. have the capture)
            wxFAIL_MSG( wxT("how did we manage to lose current item?") );

            return;
        }
    }

    int key = event.GetKeyCode();

    // first let the menu have it
    if ( IsShowingMenu() && m_menuShown->ProcessKeyDown(key) )
    {
        return;
    }

    // cycle through the menu items when left/right arrows are pressed and open
    // the menu when up/down one is
    switch ( key )
    {
        case WXK_ALT:
            // Alt must be processed at ibWindow level too
            event.Skip();
            // fall through

        case WXK_ESCAPE:
            // remove the selection and give the focus away
            if ( m_current != -1 )
            {
                if ( IsShowingMenu() )
                {
                    DismissMenu();
                }

                OnDismiss();
            }
            break;

        case WXK_LEFT:
        case WXK_RIGHT:
            {
                size_t count = GetCount();
                if ( count == 1 )
                {
                    // the item won't change anyhow
                    break;
                }
                //else: otherwise, it will

                // remember if we were showing a menu - if we did, we should
                // show the new menu after changing the item
                bool wasMenuOpened = IsShowingMenu();
                if ( wasMenuOpened )
                {
                    DismissMenu();
                }

                // cast is safe as we tested for -1 above
                size_t currentNew = (size_t)m_current;

                if ( key == WXK_LEFT )
                {
                    if ( currentNew-- == 0 )
                        currentNew = count - 1;
                }
                else // right
                {
                    if ( ++currentNew == count )
                        currentNew = 0;
                }

                DoSelectMenu(currentNew);

                if ( wasMenuOpened )
                {
                    PopupCurrentMenu();
                }
            }
            break;

        case WXK_DOWN:
        case WXK_UP:
        case WXK_RETURN:
            // open the menu
            PopupCurrentMenu();
            break;

        default:
            // letters open the corresponding menu
            {
                bool unique;
                int idxFound = FindNextItemForAccel(m_current, key, &unique);

                if ( idxFound != -1 )
                {
                    if ( IsShowingMenu() )
                    {
                        DismissMenu();
                    }

                    DoSelectMenu((size_t)idxFound);

                    // if the item is not unique, just select it but don't
                    // activate as the user might have wanted to activate
                    // another item
                    //
                    // also, don't try to open a disabled menu
                    if ( unique && IsEnabledTop((size_t)idxFound) )
                    {
                        // open the menu
                        PopupCurrentMenu();
                    }

                    // skip the "event.Skip()" below
                    break;
                }
            }

            event.Skip();
    }
}

void ibMenuBar::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    if ( IsShowingMenu() )
    {
        DismissMenu();
    }
}

// ----------------------------------------------------------------------------
// ibMenuBar accel handling
// ----------------------------------------------------------------------------

int ibMenuBar::FindNextItemForAccel(int idxStart, int key, bool *unique) const
{
    if ( !wxIsalnum((wxChar)key) )
    {
        // we only support letters/digits as accels
        return -1;
    }

    // do we have more than one item with this accel?
    if ( unique )
        *unique = true;

    // translate everything to lower case before comparing
    wxChar chAccel = (wxChar)wxTolower(key);

    // the index of the item with this accel
    int idxFound = -1;

    // loop through all items searching for the item with this
    // accel starting at the item after the current one
    int count = GetCount();
    int n = idxStart == -1 ? 0 : idxStart + 1;

    if ( n == count )
    {
        // wrap
        n = 0;
    }

    idxStart = n;
    for ( ;; )
    {
        const ibMenuInfo& info = m_menuInfos[n];

        int idxAccel = info.GetAccelIndex();
        if ( idxAccel != -1 &&
             (wxChar)wxTolower(info.GetLabel()[(size_t)idxAccel]) == chAccel )
        {
            // ok, found an item with this accel
            if ( idxFound == -1 )
            {
                // store it but continue searching as we need to
                // know if it's the only item with this accel or if
                // there are more
                idxFound = n;
            }
            else // we already had found such item
            {
                if ( unique )
                    *unique = false;

                // no need to continue further, we won't find
                // anything we don't already know
                break;
            }
        }

        // we want to iterate over all items wrapping around if
        // necessary
        if ( ++n == count )
        {
            // wrap
            n = 0;
        }

        if ( n == idxStart )
        {
            // we've seen all items
            break;
        }
    }

    return idxFound;
}

#if wxUSE_ACCEL

bool ibMenuBar::ProcessAccelEvent(const wxKeyEvent& event)
{
    size_t n = 0;
    for ( ibMenuList::compatibility_iterator node = m_menus.GetFirst();
          node;
          node = node->GetNext(), n++ )
    {
        // accels of the items in the disabled menus shouldn't work
        if ( m_menuInfos[n].IsEnabled() )
        {
            if ( node->GetData()->ProcessAccelEvent(event) )
            {
                // menu processed it
                return true;
            }
        }
    }

    // not found
    return false;
}

#endif // wxUSE_ACCEL

// ----------------------------------------------------------------------------
// ibMenuBar menus showing
// ----------------------------------------------------------------------------

void ibMenuBar::PopupCurrentMenu(bool selectFirst)
{
    wxCHECK_RET( m_current != -1, wxT("no menu to popup") );

    // forgot to call DismissMenu()?
    wxASSERT_MSG( !m_menuShown, wxT("shouldn't show two menus at once!") );

    // in any case, we should show it - even if we won't
    m_shouldShowMenu = true;

    if ( IsEnabledTop(m_current) )
    {
        // remember the menu we show
        m_menuShown = GetMenu(m_current);

        // we don't show the menu at all if it has no items
        if ( !m_menuShown->IsEmpty() )
        {
            // position it correctly: note that we must use screen coords and
            // that we pass 0 as width to position the menu exactly below the
            // item, not to the right of it
            wxRect rectItem = GetItemRect(m_current);

            m_menuShown->Popup(ClientToScreen(rectItem.GetPosition()),
                               wxSize(0, rectItem.GetHeight()),
                               selectFirst);
        }
        else
        {
            // reset it back as no menu is shown
            m_menuShown = nullptr;
        }
    }
    //else: don't show disabled menu
}

void ibMenuBar::DismissMenu()
{
    wxCHECK_RET( m_menuShown, wxT("can't dismiss menu if none is shown") );

    m_menuShown->Dismiss();
    OnDismissMenu();
}

void ibMenuBar::OnDismissMenu(bool dismissMenuBar)
{
    m_shouldShowMenu = false;
    m_menuShown = nullptr;
    if ( dismissMenuBar )
    {
        OnDismiss();
    }
}

void ibMenuBar::OnDismiss()
{
    if ( ReleaseMouseCapture() )
    {
        wxLogTrace(wxT("mousecapture"), wxT("Releasing mouse from ibMenuBar::OnDismiss"));
    }

    if ( m_current != -1 )
    {
        size_t current = m_current;
        m_current = -1;

        RefreshItem(current);
        Update();
    }

    GiveAwayFocus();
}

bool ibMenuBar::ReleaseMouseCapture()
{
#ifdef __WXX11__
    // With wxX11, when a menu is closed by clicking away from it, a control
    // under the click will still get an event, even though the menu has the
    // capture (bug?). So that control may already have taken the capture by
    // this point, preventing us from releasing the menu's capture. So to work
    // around this, we release both captures, then put back the control's
    // capture.
    ibWindow *capture = GetCapture();
    if ( capture )
    {
        capture->ReleaseMouse();

        if ( capture == this )
            return true;

        bool had = HasCapture();

        if ( had )
            ReleaseMouse();

        capture->CaptureMouse();

        return had;
    }
#else
    if ( HasCapture() )
    {
        ReleaseMouse();
        return true;
    }
#endif
    return false;
}

void ibMenuBar::GiveAwayFocus()
{
    GetFrame()->SetFocus();
}

#endif // UIKIT-REVIVE: univ menubar needs univ frame

// ----------------------------------------------------------------------------
// popup menu support
// ----------------------------------------------------------------------------

wxEventLoop *ibWindow::ms_evtLoopPopup = nullptr;

// SEAM vs univ: there the native wxWindowBase::PopupMenu(wxMenu*) entry
// dispatched into the univ DoPopupMenu; our ibMenu is a separate type, so
// the entry overloads live here (repeating the generic protocol: invoking
// window + UpdateUI before showing)
bool ibWindow::PopupMenu(ibMenu *menu, const wxPoint& pos)
{
    const wxPoint pt = pos == wxDefaultPosition
        ? ScreenToClient(wxGetMousePosition())
        : pos;

    return PopupMenu(menu, pt.x, pt.y);
}

bool ibWindow::PopupMenu(ibMenu *menu, int x, int y)
{
    wxCHECK_MSG( menu, false, wxT("can't popup null menu") );

    wxMenuInvokingWindowSetter setInvokingWin(*menu, this);

    menu->UpdateUI();

    return DoPopupMenu(menu, x, y);
}

bool ibWindow::DoPopupMenu(ibMenu *menu, int x, int y)
{
    wxCHECK_MSG( !ms_evtLoopPopup, false,
                 wxT("can't show more than one popup menu at a time") );

#ifdef __WXMSW__
    // we need to change the cursor before showing the menu as, apparently, no
    // cursor changes took place while the mouse is captured
    wxCursor cursorOld = GetCursor();
    SetCursor(wxCURSOR_ARROW);
#endif // __WXMSW__

#if 0
    // flash any delayed log messages before showing the menu, otherwise it
    // could be dismissed (because it would lose focus) immediately after being
    // shown
    wxLog::FlushActive();

    // some controls update themselves from OnIdle() call - let them do it
    wxTheApp->ProcessIdle();

    // if the window hadn't been refreshed yet, the menu can adversely affect
    // its next OnPaint() handler execution - i.e. scrolled window refresh
    // logic breaks then as it scrolls part of the menu which hadn't been there
    // when the update event was generated into view
    Update();
#endif // 0

    menu->Popup(ClientToScreen(wxPoint(x, y)), wxSize(0,0));

    // SEAM vs univ: the canon warped the mouse pointer to the menu here
    // ("Windows itself doesn't do it, but IMHO this is nice") — it is not:
    // the cursor visibly jumps on every popup, so the warp is gone

    // we have to redirect all keyboard input to the menu temporarily
    PushEventHandler(new ibMenuKbdRedirector(menu));

    // enter the local modal loop
    ms_evtLoopPopup = new wxEventLoop;
    ms_evtLoopPopup->Run();

    wxDELETE(ms_evtLoopPopup);

    // remove the handler
    PopEventHandler(true /* delete it */);

    // a right click outside the menu asked to reopen the context menu of
    // the window under the cursor — now the modal loop is gone it is safe
    if ( s_pendingContextReopen )
    {
        s_pendingContextReopen = false;

        wxWindow* target = wxFindWindowAtPoint(s_pendingContextPos);
        if ( target )
        {
            wxContextMenuEvent* evt = new wxContextMenuEvent(
                wxEVT_CONTEXT_MENU, target->GetId(), s_pendingContextPos);
            evt->SetEventObject(target);
            target->GetEventHandler()->QueueEvent(evt);
        }
    }

#ifdef __WXMSW__
    SetCursor(cursorOld);
#endif // __WXMSW__

    return true;
}

void ibWindow::DismissPopupMenu()
{
    wxCHECK_RET( ms_evtLoopPopup, wxT("no popup menu shown") );

    ms_evtLoopPopup->Exit();
}

#endif // wxUSE_MENUS
