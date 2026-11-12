// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

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

#include "deskgui/univ/menu.h"
#include "deskgui/univ/menuItem.h"
#include "deskgui/univ/window.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/ctrl/control.h"   // ibControl::FindAccelIndex


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
#include <wx/app.h>
#include <wx/weakref.h>

#include <memory>

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/popupWindow.h"   // ibPopupWindow — the menu's window

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
        m_indexAccel = wxControlBase::FindAccelIndex(text, &m_label);

        // calculate the width later, after the menu bar is created
        m_width = 0;
    }

    void SetEnabled(bool enabled = true) { m_isEnabled = enabled; }

    // accessors

    const wxString& GetLabel() const { return m_label; }
    const wxString& GetOriginalLabel() const { return m_originalLabel; }
    bool IsEnabled() const { return m_isEnabled; }
    wxCoord GetWidth(ibMenuBarWindow *window) const
    {
        if ( !m_width )
        {
            wxConstCast(this, ibMenuInfo)->CalcWidth(window);
        }

        return m_width;
    }

    int GetAccelIndex() const { return m_indexAccel; }

private:
    // SEAM vs univ: measured in the bar's window - the bar has none of its own
    void CalcWidth(ibMenuBarWindow *window);

    wxString m_label;
    wxString m_originalLabel;
    wxCoord m_width;
    int m_indexAccel;
    bool m_isEnabled;
};

// ----------------------------------------------------------------------------
// ibPopupMenuWindow: a popup window showing a menu
// ----------------------------------------------------------------------------

// SEAM vs univ: the theme's popup (ibPopupWindow) - top-level, not activated, under the system's drop shadow; a child of
// the desktop, as wx makes a popup, casts none (Max, 2026-11-10: "the menu has no such shading at its edges")
class ibPopupMenuWindow : public ibPopupWindow<wxPopupTransientWindow>
{
public:
    ibPopupMenuWindow(wxWindow *parent, ibMenu *menu);

    virtual ~ibPopupMenuWindow();

    // override the base class version to select the first item initially
    virtual void Popup(wxWindow *focus = nullptr) override;

    // override the base class version to dismiss any open submenus
    virtual void Dismiss() override;

#ifdef __WXMSW__
    // nor focus it, as the code editor's lists refuse it (ibCodeEditorPopupWindow): top-level, wxPopupWindow::SetFocus
    // is live, and wx focuses a clicked window that accepts it - the window that popped the menu lost the focus, its
    // owner moved it on, and the menu shut on the press before the release chose the item (2026-11-11: the tree's
    // Properties and Open did nothing)
    virtual bool AcceptsFocus() const override { return false; }
#endif

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

    // SEAM vs univ: there the popup window was a univ wxWindow and its drawing
    // overrode its paint pipeline; here the base is the native
    // wxPopupTransientWindow, so paint directly with the theme renderer
    void OnPaint(wxPaintEvent& event);

    // draw the menu's items inside this window
    void DrawItems(wxDC& dc, ibRenderer *rend);

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
// ibMenuBarWindow: the window a menubar is drawn in
// ----------------------------------------------------------------------------

// SEAM vs univ: the canon's menubar was this window, its menus' data are the
// bar's (ibMenuBar). It takes neither the focus nor the mouse of its own: a
// menu of it is shown as a popup menu is (ibWindow::DoPopupMenu - the menu
// holding the mouse, the keys redirected to it), and when the mouse moves over
// another title (ProcessMouseEvent) or an arrow key the menu left moves along
// the bar (ProcessKeyDown), the menu shown is dismissed and the next one shown
// in its place (PopupCurrentMenu)
class ibMenuBarWindow : public ibWindow
{
public:
    ibMenuBarWindow(ibMenuBar *menubar, wxWindow *parent);
    virtual ~ibMenuBarWindow();

    // get the rect of the item
    wxRect GetItemRect(size_t pos) const;

    // get the menu from the given point or -1 if none
    int GetMenuFromPoint(const wxPoint& pos) const;

    // refresh the given item
    void RefreshItem(size_t pos);

    // refresh all items after this one (including it)
    void RefreshAllItemsAfter(size_t pos);

    // show the menu of the given item, and the next ones the user moves to
    void PopupMenu(size_t pos);

    // the mouse moved over the bar while a menu of it is shown, the point the
    // bar's: true if another menu is shown in its place
    bool ProcessMouseEvent(const wxPoint& pt);

    // a key the menu shown left: true if done
    bool ProcessKeyDown(int key);

protected:
    virtual wxSize DoGetBestClientSize() const override;

private:
    // "fast" version of GetMenuCount()
    size_t GetCount() const;

    // get the (total) width of the specified menu
    wxCoord GetItemWidth(size_t pos) const;

    // the menu of the current item shown below it, until it is dismissed -
    // and the next one, while the user moves to another (m_next)
    void PopupCurrentMenu();

    // hide the currently shown menu, the next one to show (or -1)
    void DismissMenu(int next);

    // event handlers
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);

    // the bar drawn, which deletes this window as it goes
    ibMenuBar *m_menuBar;

    // the item whose menu is shown or -1
    int m_current;

    // the item whose menu is shown next, as the one shown is dismissed, or -1
    int m_next;

    // the currently shown menu or nullptr
    ibMenu *m_menuShown;

    // it forgets the window as it goes (~ibMenuBar)
    friend class ibMenuBar;

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

wxBEGIN_EVENT_TABLE(ibMenuBarWindow, ibWindow)
    EVT_PAINT(ibMenuBarWindow::OnPaint)
    EVT_LEFT_DOWN(ibMenuBarWindow::OnLeftDown)
wxEND_EVENT_TABLE()

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
    // look draws its own 1px palette border in OnPaint instead. A top-level
    // popup (wxPU_CONTAINS_CONTROLS), as the shadow needs one (Show)
    (void)Create(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS);

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

void ibPopupMenuWindow::Popup(wxWindow *focus)
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

        // SEAM vs univ: not activated (SWP_NOACTIVATE) - a menu never takes the activation from the window it is in.
        // Activated, it deactivated the frame, the focus left the view, the view another took its place while the menu
        // was open, and the menu bar built for that one deleted this one's under this very call (2026-11-11, a crash
        // at the Refresh below). And on top, as said above (HWND_TOP): univ put it after its parent (the window given
        // is the one it goes behind), under the parent menu it was to stand over
        if ( !::SetWindowPos(GetHwnd(), HWND_TOP,
                             0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE) )
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
        // SEAM vs univ: the item's own place, as the menu laid it out (ibMenu::GetGeometryInfo) - not the heights
        // summed from the top, which knew of no empty strip above the first item
        (void)m_menu->GetGeometryInfo();
        for ( ibMenuItemIter node = m_menu->GetMenuItems().GetFirst();
              node;
              node = node->GetNext() )
        {
            ibMenuItem *item = ibItem(node->GetData());
            if ( pt.y >= item->GetPosition() && pt.y < item->GetPosition() + item->GetHeight() )
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

    // FIXME: -1 here because of SetLogicalOrigin(1, 1) in DrawItems()
    RefreshRect(wxRect(0, item->GetPosition() - 1,
                m_menu->GetGeometryInfo().GetSize().x, item->GetHeight()));
}

void ibPopupMenuWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dc(this);

    // flat 1px border around the popup (replaces the native raised edge);
    // DrawItems() then shifts the logical origin by (1, 1) to clear it
    dc.SetPen(wxPen(wxSCHEME_COLOUR(ibThemeEngine::Get()->GetColourScheme(),
                                    SHADOW_IN)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(wxRect(GetClientSize()));

    DrawItems(dc, m_menu->GetRenderer());
}

void ibPopupMenuWindow::DrawItems(wxDC& dc, ibRenderer *rend)
{
    // no clipping so far - do we need it? I don't think so as the menu is
    // never partially covered as it is always on top of everything

    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // FIXME: this should be done in the renderer, however when it is fixed
    //        ibPopupMenuWindow::RefreshItem() should be changed too!
    dc.SetLogicalOrigin(1, 1);

    const ibMenuGeometryInfo& gi = m_menu->GetGeometryInfo();
    for ( ibMenuItemIter node = m_menu->GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        ibMenuItem *item = ibItem(node->GetData());

        // SEAM vs univ: at its own place, as the menu laid it out (ibMenu::GetGeometryInfo) and the mouse finds it
        // (GetMenuItemFromPoint) - not the heights summed from the top
        const wxCoord y = item->GetPosition();

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
    }
}

// ----------------------------------------------------------------------------
// ibPopupMenuWindow actions
// ----------------------------------------------------------------------------

// SEAM vs univ: the item clicked is done AFTER the menu's modal loop, as a native menu's command comes once
// TrackPopupMenu has returned - DoPopupMenu does it. Done inside the loop, whatever the command showed modally ran in
// it, and no menu could be shown there: a dialog opened from the code editor's menu (the query constructor) answered
// no right click, DoPopupMenu refusing a second loop without a word
static ibMenu *s_pendingClickMenu = nullptr;
static ibMenuItem *s_pendingClickItem = nullptr;

void ibPopupMenuWindow::ClickItem(ibMenuItem *item)
{
    wxCHECK_RET( item, wxT("can't click null item") );

    wxASSERT_MSG( !item->IsSeparator() && !item->IsSubMenu(),
                  wxT("can't click this item") );

    ibMenu* menu = m_menu;

    // close all menus
    DismissAndNotify();

    s_pendingClickMenu = menu;
    s_pendingClickItem = item;
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
// a right click outside dismisses the menu and the click goes on to the
// window under the cursor (DoPopupMenu).
// the click must NOT be queued while the menu modal loop is still
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
    // SEAM vs univ: there the generic wxPopupWindowHandler (pushed by the
    // generic Popup()) dismissed the menu on outside clicks; the MSW popup
    // relies on activation changes instead, which never happen while we hold
    // the mouse capture — so dismiss here ourselves. A menubar's menu too: a
    // click on its title shuts it, as a native one (the canon passed the click
    // on to the bar)
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
        else // no parent menu
        {
            ibMenuBar *menubar = wxDynamicCast(m_menu->GetMenuBar(), ibMenuBar);
            if ( menubar && menubar->m_window )
            {
                ibMenuBarWindow * const win = menubar->m_window;
                if ( win->ProcessMouseEvent(win->ScreenToClient(ptScreen)) )
                {
                    // menubar has closed this menu and opened another one, probably
                    return;
                }
            }
        }
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
    // SEAM vs univ: there a menubar's menu passed its keys to the bar; here the
    // keys are redirected to the menu shown (ibMenuKbdRedirector), which leaves
    // the bar those it does not take (ibMenu::ProcessKeyDown)
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

// SEAM vs univ: the menu lays itself out from the theme's metrics (FORKED from wxWin32Renderer::GetMenuGeometry, the
// canon's theme laid it out itself from the menu's items): every item gets its place (SetGeometry; GetPosition and
// GetHeight assert otherwise), and the labels and the accel strings are aligned for all of them
const ibMenuGeometryInfo& ibMenu::GetGeometryInfo() const
{
    if ( !m_geometry )
    {
        if ( m_popupMenu )
        {
            const ibRenderer * const renderer = GetRenderer();

            // the items' text is drawn with the system font (ibPopupMenuWindow::DrawItems)
            wxInfoDC dc(m_popupMenu);
            dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

            const wxCoord heightText = dc.GetCharHeight();
            const wxSize margin = renderer->GetMenuMargin();

            // the total height, from below the empty strip at the top
            wxCoord height = margin.y;

            // the max length of label and accel strings: the menu width is the sum of them, even if they're for
            // different items (as the accels should be aligned)
            wxCoord widthLabelMax = 0,
                    widthAccelMax = 0,
                    widthBmpMax = 0;

            for ( ibMenuItemIter node = GetMenuItems().GetFirst(); node; node = node->GetNext() )
            {
                ibMenuItem * const item = ibItem(node->GetData());

                wxCoord h;
                if ( item->IsSeparator() )
                {
                    h = renderer->GetMenuSeparatorHeight();
                }
                else // not separator
                {
                    h = renderer->GetMenuItemHeight(heightText);

                    wxCoord widthLabel;
                    dc.GetTextExtent(item->GetItemLabelText(), &widthLabel, nullptr);
                    widthLabelMax = wxMax(widthLabelMax, widthLabel);

                    wxCoord widthAccel;
                    dc.GetTextExtent(item->GetAccelString(), &widthAccel, nullptr);
                    widthAccelMax = wxMax(widthAccelMax, widthAccel);

                    // logical: the width it is drawn at (scale 2 on OSX Retina)
                    const wxBitmap& bmp = item->GetBitmap();
                    if ( bmp.IsOk() )
                        widthBmpMax = wxMax(widthBmpMax, bmp.GetLogicalSize().x);
                }

                // remember the item position and height
                item->SetGeometry(height, h);

                height += h;
            }

            ibMenuGeometryInfo * const gi = new ibMenuGeometryInfo;

            gi->m_ofsLabel = margin.x + renderer->GetMenuBitmapWidth(widthBmpMax);
            gi->m_ofsAccel = gi->m_ofsLabel + widthLabelMax;
            if ( widthAccelMax > 0 )
            {
                // if we actually have any accels, add a margin
                gi->m_ofsAccel += renderer->GetMenuAccelMargin();
            }

            gi->m_heightItem = renderer->GetMenuItemHeight(heightText);

            gi->m_size.x = gi->m_ofsAccel + widthAccelMax + renderer->GetMenuSubmenuWidth() + margin.x;
            gi->m_size.y = height + margin.y;

            wxConstCast(this, ibMenu)->m_geometry = gi;
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

    // its accelerator is its label's, asked when a key comes (ProcessAccelEvent)
    wxUnusedVar(item);
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

// SEAM vs univ: an item made by the native factory (wxMenuItem::New) - the
// creating Append, AppendSeparator, AppendSubMenu or Insert of wxMenuBase
// called through a wxMenu* (the doc manager's file history, a menu copied
// item by item) - is made again as the theme's, what it was given carried
// over, and the native one goes: the creating calls return the new one
// (DoAppend's). A caller that made the item itself must not touch it again
static ibMenuItem *ibThemeItem(ibMenu *menu, wxMenuItem *item)
{
    if ( ibMenuItem * const themed = wxDynamicCast(item, ibMenuItem) )
        return themed;

    wxMenu * const submenu = item->GetSubMenu();
    wxASSERT_MSG( !submenu || wxDynamicCast(submenu, ibMenu),
                  wxT("a submenu of the theme's menu is the theme's (ibMenu)") );

    // the new item holds the submenu - the native one would delete it
    item->SetSubMenu(nullptr);

    ibMenuItem * const themed = new ibMenuItem(menu, item->GetId(),
                                               item->GetItemLabel(),
                                               item->GetHelp(),
                                               item->GetKind(),
                                               ibSub(submenu));
    if ( item->GetBitmapBundle().IsOk() )
        themed->SetBitmap(item->GetBitmapBundle());
    if ( !item->IsEnabled() )
        themed->Enable(false);
    if ( item->GetKind() == wxITEM_CHECK && item->IsChecked() )
        themed->Check(true);

    delete item;
    return themed;
}

wxMenuItem* ibMenu::DoAppend(wxMenuItem *itemBase)
{
    ibMenuItem *item = ibThemeItem(this, itemBase);

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
    ibMenuItem *item = ibThemeItem(this, itemBase);

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

wxWindow *ibMenu::GetRootWindow() const
{
    // SEAM vs univ: the root menu's - a menubar's menu the window the bar is
    // drawn in (GetWindow() would give its frame), a popup one the window it
    // was invoked by: an ib window (ibWindow::PopupMenu) or the native frame
    // (ibFrame::DoPopupMenu)
    const wxMenuBase *root = this;
    while ( root->GetParent() )
        root = root->GetParent();

    if ( ibMenuBar *menubar = wxDynamicCast(root->GetMenuBar(), ibMenuBar) )
        return menubar->m_window;

    return root->GetInvokingWindow();
}

ibRenderer *ibMenu::GetRenderer() const
{
    // SEAM vs univ: there the popup window was a univ wxWindow carrying a
    // renderer; here the theme's, the one every ib window takes (ibWindow::Init)
    // - the window it is shown over may be the native frame
    return ibThemeEngine::Get()->GetRenderer();
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
        // SEAM vs univ: a menubar's menu is shown as a popup one (ibMenuBarWindow),
        // so its loop ends alike - the window that runs it is told nothing more
        wxUnusedVar(dismissParent);

        wxCHECK_RET( GetRootWindow(), wxT("what kind of menu is this?") );

        ibWindow::DismissPopupMenu();
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
    wxWindow *focus = GetRootWindow();

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

    if ( m_popupMenu->ProcessKeyDown(key) )
        return true;

    // SEAM vs univ: there the bar had a menubar's menu's keys first
    // (ibPopupMenuWindow::OnKeyDown); here the menu does, and the bar has
    // those it leaves - the arrows moving to the next menu
    ibMenuBar * const menubar = wxDynamicCast(GetMenuBar(), ibMenuBar);
    return menubar && menubar->m_window && menubar->m_window->ProcessKeyDown(key);
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

#if wxUSE_ACCEL

// SEAM vs univ: there the menu kept a table of its items' accelerators, made as
// they came (AddAccelFor) - the univ table's API the native wxAcceleratorTable
// has not. Here the item's label is asked when the key comes: the accelerator
// is its text after the tab, and the key is matched as wx's generic table
// matches it (wxAcceleratorTable::GetEntry - the code, then Ctrl, Shift, Alt
// each as the key has it; Ctrl is Cmd on the Mac). The items are updated first,
// as a native menu's are when its accelerator is translated (MSW sends
// WM_INITMENUPOPUP): a disabled one leaves the key to the window with the
// focus. The click comes after the key, not in it, as the menu asked for by its
// letter does (ibFrame::OnCharHook) - the hook that raised the event waits on
// its return, and a dialog the command opens would run its loop inside it. On
// GTK3/Cocoa the same: the key comes to the frame first there too
// (wxEVT_CHAR_HOOK), and the native menubar's accelerators these ports had are
// not made - the bar is the theme's (2026-11-12, Max)
bool ibMenu::ProcessAccelEvent(const wxKeyEvent& event)
{
    for ( ibMenuItemIter node = GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        ibMenuItem * const item = ibItem(node->GetData());

        if ( item->IsSubMenu() )
        {
            // try its elements
            if ( item->IsEnabled() &&
                 ibSub(item->GetSubMenu())->ProcessAccelEvent(event) )
                return true;
            continue;
        }

        if ( item->GetAccelString().empty() )
            continue;

        const std::unique_ptr<wxAcceleratorEntry> accel(item->GetAccel());
        if ( !accel || accel->GetKeyCode() != event.GetKeyCode() )
            continue;

        const int flags = accel->GetFlags();
        if ( ((flags & wxACCEL_CTRL) != 0) != event.ControlDown() ||
             ((flags & wxACCEL_SHIFT) != 0) != event.ShiftDown() ||
             ((flags & wxACCEL_ALT) != 0) != event.AltDown() )
            continue;

        UpdateUI();
        if ( !item->IsEnabled() )
            return false;

        const int id = item->GetId();
        CallAfter([this, id]()
        {
            ibMenuItem * const clicked = ibItem(FindChildItem(id));
            if ( clicked && clicked->IsEnabled() )
                ClickItem(clicked);
        });
        return true;
    }

    return false;
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
wxIMPLEMENT_DYNAMIC_CLASS(ibMenuBar, wxMenuBar);

// ----------------------------------------------------------------------------
// ibMenuBar creation
// ----------------------------------------------------------------------------

ibMenuBar::ibMenuBar(long style)
         : wxMenuBar(style)
{
    m_window = nullptr;
}

ibMenuBar::~ibMenuBar()
{
    // its window goes with it, once the event it may be in is over: the bar
    // may go in a command chosen in its menu (ibMenuBarWindow::PopupCurrentMenu)
    if ( m_window )
    {
        m_window->m_menuBar = nullptr;
        m_window->Hide();
        if ( wxTheApp )
            wxTheApp->ScheduleForDestruction(m_window);
        else
            delete m_window;
    }
}

void ibMenuBar::Attach(wxFrame *frame)
{
    // maybe you really wanted to call Detach()?
    wxCHECK_RET( frame, wxT("ibMenuBar::Attach(nullptr) called") );

    // the native bar's: its frame; its accelerators' table stays empty - the
    // menus make none (ibMenu::ProcessAccelEvent)
    wxMenuBar::Attach(frame);

    if ( m_window )
    {
        // reparent if necessary
        if ( m_window->GetParent() != frame )
        {
            m_window->Reparent(frame);
        }

        // show it back - was hidden by Detach()
        m_window->Show();
    }
    else // not created yet, do it now
    {
        m_window = new ibMenuBarWindow(this, frame);
    }
}

void ibMenuBar::Detach()
{
    // don't delete the window because we may be reattached later, just hide it
    if ( m_window )
    {
        m_window->Hide();
    }

    wxMenuBar::Detach();
}

// ----------------------------------------------------------------------------
// ibMenuBar adding/removing items
// ----------------------------------------------------------------------------

bool ibMenuBar::Append(wxMenu *menu, const wxString& title)
{
    return Insert(GetMenuCount(), menu, title);
}

bool ibMenuBar::Insert(size_t pos, wxMenu *menu, const wxString& title)
{
    // the theme shows it (ibMenu::Popup): a native one has nothing to show it with
    wxCHECK_MSG( wxDynamicCast(menu, ibMenu), false,
                 wxT("a menu of the theme's menubar is the theme's (ibMenu)") );

    if ( !wxMenuBarBase::Insert(pos, menu, title) )
        return false;

    // the base's, as the native bar does: the native menu's SetTitle puts a
    // title item into its native menu
    menu->wxMenuBase::SetTitle(title);

    m_menuInfos.insert(m_menuInfos.begin() + pos, ibMenuInfo(title));

    if ( m_window )
        m_window->RefreshAllItemsAfter(pos);

    return true;
}

wxMenu *ibMenuBar::Replace(size_t pos, wxMenu *menu, const wxString& title)
{
    wxCHECK_MSG( wxDynamicCast(menu, ibMenu), nullptr,
                 wxT("a menu of the theme's menubar is the theme's (ibMenu)") );

    wxMenu *menuOld = wxMenuBarBase::Replace(pos, menu, title);

    if ( menuOld )
    {
        menu->wxMenuBase::SetTitle(title);

        ibMenuInfo& info = m_menuInfos[pos];

        info.SetLabel(title);

        // even if the old menu was disabled, the new one is not any more
        info.SetEnabled();

        // even if we change only this one, the new label has different width,
        // so we need to refresh everything beyond this item as well
        if ( m_window )
            m_window->RefreshAllItemsAfter(pos);
    }

    return menuOld;
}

wxMenu *ibMenuBar::Remove(size_t pos)
{
    wxMenu *menuOld = wxMenuBarBase::Remove(pos);

    if ( menuOld )
    {
        m_menuInfos.erase(m_menuInfos.begin() + pos);

        // this doesn't happen too often, so don't try to be too smart - just
        // refresh everything
        if ( m_window )
            m_window->Refresh();
    }

    return menuOld;
}

// ----------------------------------------------------------------------------
// ibMenuBar top level menus access
// ----------------------------------------------------------------------------

void ibMenuBar::EnableTop(size_t pos, bool enable)
{
    wxCHECK_RET( pos < GetMenuCount(), wxT("invalid index in EnableTop") );

    if ( enable != m_menuInfos[pos].IsEnabled() )
    {
        m_menuInfos[pos].SetEnabled(enable);

        if ( m_window )
            m_window->RefreshItem(pos);
    }
    //else: nothing to do
}

bool ibMenuBar::IsEnabledTop(size_t pos) const
{
    wxCHECK_MSG( pos < GetMenuCount(), false, wxT("invalid index in IsEnabledTop") );

    return m_menuInfos[pos].IsEnabled();
}

void ibMenuBar::SetMenuLabel(size_t pos, const wxString& label)
{
    wxCHECK_RET( pos < GetMenuCount(), wxT("invalid index in SetMenuLabel") );

    if ( label != m_menuInfos[pos].GetOriginalLabel() )
    {
        m_menuInfos[pos].SetLabel(label);

        // the new label has a width of its own
        if ( m_window )
            m_window->RefreshAllItemsAfter(pos);
    }
    //else: nothing to do
}

wxString ibMenuBar::GetMenuLabel(size_t pos) const
{
    wxCHECK_MSG( pos < GetMenuCount(), wxEmptyString, wxT("invalid index in GetMenuLabel") );

    return m_menuInfos[pos].GetOriginalLabel();
}

// ----------------------------------------------------------------------------
// ibMenuBar geometry: its window's
// ----------------------------------------------------------------------------

void ibMenuBar::DoGetSize(int *width, int *height) const
{
    const wxSize size = m_window ? m_window->GetSize() : wxSize(0, 0);
    if ( width )
        *width = size.x;
    if ( height )
        *height = size.y;
}

void ibMenuBar::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    if ( m_window )
        m_window->SetSize(x, y, width, height, sizeFlags);
}

wxSize ibMenuBar::DoGetBestSize() const
{
    return m_window ? m_window->GetBestSize() : wxSize(0, 0);
}

wxWindow *ibMenuBar::GetWindow() const
{
    return m_window;
}

// ----------------------------------------------------------------------------
// ibMenuBar menus showing
// ----------------------------------------------------------------------------

void ibMenuBar::PopupMenu(size_t pos)
{
    wxCHECK_RET( m_window, wxT("can't show a menu of a menubar never attached") );

    m_window->PopupMenu(pos);
}

// ----------------------------------------------------------------------------
// ibMenuBar accel handling
// ----------------------------------------------------------------------------

#if wxUSE_ACCEL

bool ibMenuBar::ProcessAccelEvent(const wxKeyEvent& event)
{
    for ( size_t n = 0; n < GetMenuCount(); n++ )
    {
        // accels of the items in the disabled menus shouldn't work
        if ( IsEnabledTop(n) && ibSub(GetMenu(n))->ProcessAccelEvent(event) )
            return true;
    }

    return false;
}

#endif // wxUSE_ACCEL

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
    int count = (int)m_menuInfos.size();
    if ( count == 0 )
    {
        return -1;
    }

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

// ----------------------------------------------------------------------------
// ibMenuInfo
// ----------------------------------------------------------------------------

void ibMenuInfo::CalcWidth(ibMenuBarWindow *window)
{
    wxSize size;
    wxInfoDC dc(window);
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    dc.GetTextExtent(m_label, &size.x, &size.y);

    // adjust for the renderer we use and store the width
    m_width = window->GetRenderer()->GetMenuBarItemSize(size).x;
}

// ----------------------------------------------------------------------------
// ibMenuBarWindow
// ----------------------------------------------------------------------------

ibMenuBarWindow::ibMenuBarWindow(ibMenuBar *menubar, wxWindow *parent)
{
    m_menuBar = menubar;
    m_current = -1;
    m_next = -1;
    m_menuShown = nullptr;

    // the canon's ibMenuBar::Attach
    (void)Create(parent, wxID_ANY);

    SetCursor(wxCURSOR_ARROW);

    // calculate and set our height (it won't be changed any more)
    SetSize(wxDefaultCoord, GetBestSize().y);
}

ibMenuBarWindow::~ibMenuBarWindow()
{
    // gone before its bar - a child of the frame, which went first: the bar
    // draws nowhere until it is attached again
    if ( m_menuBar )
        m_menuBar->m_window = nullptr;
}

size_t ibMenuBarWindow::GetCount() const
{
    return m_menuBar ? m_menuBar->m_menuInfos.size() : 0;
}

wxCoord ibMenuBarWindow::GetItemWidth(size_t pos) const
{
    return m_menuBar->m_menuInfos[pos].GetWidth(wxConstCast(this, ibMenuBarWindow));
}

// ----------------------------------------------------------------------------
// ibMenuBarWindow drawing
// ----------------------------------------------------------------------------

void ibMenuBarWindow::RefreshAllItemsAfter(size_t pos)
{
    wxRect rect = GetItemRect(pos);
    rect.width = GetClientSize().x - rect.x;
    RefreshRect(rect);
}

void ibMenuBarWindow::RefreshItem(size_t pos)
{
    wxCHECK_RET( pos != (size_t)-1,
                 wxT("invalid item in ibMenuBarWindow::RefreshItem") );

    RefreshRect(GetItemRect(pos));
}

void ibMenuBarWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

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

        if ( !m_menuBar->IsEnabledTop(n) )
        {
            flags |= wxCONTROL_DISABLED;
        }

        GetRenderer()->DrawMenuBarItem
                       (
                            dc,
                            rect,
                            m_menuBar->m_menuInfos[n].GetLabel(),
                            flags,
                            m_menuBar->m_menuInfos[n].GetAccelIndex()
                       );
    }
}

// ----------------------------------------------------------------------------
// ibMenuBarWindow geometry
// ----------------------------------------------------------------------------

wxRect ibMenuBarWindow::GetItemRect(size_t pos) const
{
    wxASSERT_MSG( pos < GetCount(), wxT("invalid menu bar item index") );

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

wxSize ibMenuBarWindow::DoGetBestClientSize() const
{
    // SEAM vs univ: the height of the font's line, not of the first menu's
    // label - a bar attached empty and filled afterwards keeps the height it
    // was made with (the ctor)
    wxInfoDC dc(wxConstCast(this, ibMenuBarWindow));
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // adjust for the renderer we use
    wxSize size = GetRenderer()->GetMenuBarItemSize(wxSize(0, dc.GetCharHeight()));

    // the width is arbitrary, of course, for horizontal menubar
    size.x = 100;

    return size;
}

int ibMenuBarWindow::GetMenuFromPoint(const wxPoint& pos) const
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
// ibMenuBarWindow menus showing
// ----------------------------------------------------------------------------

void ibMenuBarWindow::OnLeftDown(wxMouseEvent& event)
{
    int item = GetMenuFromPoint(event.GetPosition());
    if ( item == -1 )
    {
        event.Skip();
        return;
    }

    PopupMenu(item);
}

void ibMenuBarWindow::PopupMenu(size_t pos)
{
    wxCHECK_RET( pos < GetCount(), wxT("invalid menu index in PopupMenu") );

    // one is shown already: its keys are its own (ibMenuKbdRedirector), but a
    // menu asked for by its letter comes after the key (ibFrame::OnCharHook)
    if ( m_menuShown )
        return;

    m_current = pos;

    // show it as selected
    RefreshItem(pos);

    PopupCurrentMenu();
}

void ibMenuBarWindow::PopupCurrentMenu()
{
    // a command chosen in the menu is done as its loop ends
    // (ibWindow::DoPopupMenu), and one may take the bar away, this window with
    // it - nothing of it is touched then
    const wxWeakRef<wxWindow> self(this);

    while ( m_current != -1 )
    {
        m_next = -1;

        // we don't show the menu at all if it is disabled or has no items -
        // its title is selected all the same
        ibMenu *menu = ibSub(m_menuBar->GetMenu(m_current));
        if ( m_menuBar->IsEnabledTop(m_current) && !menu->IsEmpty() )
        {
            // position it correctly: below the item
            const wxRect rectItem = GetItemRect(m_current);

            // its items as they are now, the frame's handlers answering as the
            // menu opens (DoPopupMenu, wxFrameBase::OnMenuOpen)
            m_menuShown = menu;
            DoPopupMenu(menu, rectItem.x, rectItem.GetBottom() + 1);
            // ...the bar taken away from its window at once, the window only scheduled to go (~ibMenuBar): still here,
            // it has no bar - its item refreshed then read the menus through none (2026-11-11, a command chosen in the
            // menu that changed the view, designer-thin_24996_t29884_20261111T172053_0.dmp)
            if ( !self || !m_menuBar )
                return;
            m_menuShown = nullptr;
        }

        // the next one the user moved to (DismissMenu), or none
        const size_t current = m_current;
        m_current = m_next;
        m_next = -1;

        RefreshItem(current);
        if ( m_current != -1 )
            RefreshItem(m_current);
    }
}

void ibMenuBarWindow::DismissMenu(int next)
{
    wxCHECK_RET( m_menuShown, wxT("can't dismiss menu if none is shown") );

    // the menu shown goes - its loop ends (PopupCurrentMenu), and the next one
    // is shown in its place
    m_next = next;
    m_menuShown->Dismiss();
    DismissPopupMenu();
}

bool ibMenuBarWindow::ProcessMouseEvent(const wxPoint& pt)
{
    if ( !m_menuShown )
        return false;

    int currentNew = GetMenuFromPoint(pt);
    if ( (currentNew == -1) || (currentNew == m_current) )
    {
        return false;
    }

    // show the menu under the mouse in place of the one shown
    DismissMenu(currentNew);

    return true;
}

bool ibMenuBarWindow::ProcessKeyDown(int key)
{
    if ( !m_menuShown )
        return false;

    // cycle through the menu items when left/right arrows are pressed - the
    // menu shown is closed, the next one opened in its place
    switch ( key )
    {
        case WXK_LEFT:
        case WXK_RIGHT:
            {
                size_t count = GetCount();
                if ( count == 1 )
                {
                    // the item won't change anyhow
                    return true;
                }
                //else: otherwise, it will

                // cast is safe as a menu is shown
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

                DismissMenu((int)currentNew);
            }
            return true;
    }

    return false;
}

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

// SEAM vs wx: wxWindowBase's DoGetPopupMenuSelectionFromUser, its selection
// kept here rather than in wincmn.cpp's static - the item's event caught on
// its way to this window, the update events stopped here as there (a menu of
// made-up ids is not updated by handlers elsewhere)
int ibWindow::GetPopupMenuSelectionFromUser(ibMenu& menu, const wxPoint& pos)
{
    int selection = wxID_NONE;
    const auto onMenu = [&selection](wxCommandEvent& event) { selection = event.GetId(); };
    const auto onUpdate = [](wxUpdateUIEvent& WXUNUSED(event)) {};

    Bind(wxEVT_MENU, onMenu);
    Bind(wxEVT_UPDATE_UI, onUpdate);

    PopupMenu(&menu, pos);

    Unbind(wxEVT_UPDATE_UI, onUpdate);
    Unbind(wxEVT_MENU, onMenu);

    return selection;
}

int ibWindow::GetPopupMenuSelectionFromUser(ibMenu& menu, int x, int y)
{
    return GetPopupMenuSelectionFromUser(menu, wxPoint(x, y));
}

bool ibWindow::DoPopupMenu(ibMenu *menu, int x, int y)
{
    return DoPopupMenu(this, menu, x, y);
}

bool ibWindow::DoPopupMenu(wxWindow *window, wxMenu *popup, int x, int y)
{
    ibMenu * const menu = ibSub(popup);

    wxCHECK_MSG( !ms_evtLoopPopup, false,
                 wxT("can't show more than one popup menu at a time") );

    // SEAM vs univ: a menu of no items shows nothing and waits for nothing, as
    // a native one: the canon ran its loop round a popup of no size, unseen,
    // and the next click anywhere went to end it (a tool bar's overflow with
    // every tool in sight, 2026-11-11)
    if ( menu->IsEmpty() )
        return false;

    // we need to change the cursor before showing the menu as, apparently, no
    // cursor changes took place while the mouse is captured - on every port
    // (the canon's was MSW's alone; GTK3 and Cocoa take the arrow too, as the
    // window's own cursor, given back after the loop)
    const wxCursor cursorOld = window->GetCursor();
    window->SetCursor(wxCURSOR_ARROW);

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

    // SEAM vs univ: the focus given back where it was as the menu goes (below),
    // as a native menu leaves it: wx's popup on GTK and macOS gives it to this
    // window (wxPopupTransientWindow::Popup), and a menubar's is not where the
    // user typed; wxMSW's gives it to none
    const wxWeakRef<wxWindow> focused(FindFocus());

    // SEAM vs univ: the window told that the menu opens and, after its loop, that it closed, as every native port tells
    // it (wxMSW's WM_INITMENUPOPUP, wxGTK's "map", wxOSX's menuWillOpen) and the canon's never did: through the menu, its
    // bar or this window, and the frame last (wxMenu::ProcessMenuEvent). The frame updates a menubar's items there
    // (wxFrameBase::OnMenuOpen), and a window shuts the popup it keeps open over itself: the menu takes the keys, not the
    // focus - no focus lost tells it (a code editor's completion list stood over the menu, 2026-11-12, Max)
    const int menuId = menu->IsAttached() ? 0 : wxID_ANY;
    wxMenuEvent open(wxEVT_MENU_OPEN, menuId, menu);
    wxMenu::ProcessMenuEvent(menu, open, window);

    menu->Popup(window->ClientToScreen(wxPoint(x, y)), wxSize(0,0));

    // SEAM vs univ: the canon warped the mouse pointer to the menu here
    // ("Windows itself doesn't do it, but IMHO this is nice") — it is not:
    // the cursor visibly jumps on every popup, so the warp is gone

    // SEAM vs univ: the keys are the menu's while it is shown, taken at the
    // window they come to - the one with the focus once the menu is up. The
    // canon took them at this window, its popup having focused it; wxMSW's
    // focuses none, and a menubar's menu, its bar never focused, heard no key:
    // Down, Esc, Left and Right went to the frame (2026-11-11)
    const wxWeakRef<wxWindow> keys(FindFocus() ? FindFocus() : window);
    keys->PushEventHandler(new ibMenuKbdRedirector(menu));

    // SEAM vs univ: its frame closed while the menu is shown (Alt+F4, the task bar) shuts the menu first, as a native
    // one is, and the frame goes after the loop. Closed inside it, the frame was destroyed there - the menu's window
    // with the capture and its handlers on it (wx asserts), and the loop with nothing left to end it: no window, and
    // the process stayed (2026-11-11, a designer closed with a menu open)
    const wxWeakRef<wxWindow> frame(wxGetTopLevelParent(window));
    const auto onFrameClose = [menu](wxCloseEvent& event)
    {
        menu->Dismiss();
        DismissPopupMenu();
        event.Skip();
    };
    if ( frame )
        frame->Bind(wxEVT_CLOSE_WINDOW, onFrameClose);

    // enter the local modal loop
    ms_evtLoopPopup = new wxEventLoop;
    ms_evtLoopPopup->Run();

    wxDELETE(ms_evtLoopPopup);

    if ( frame )
        frame->Unbind(wxEVT_CLOSE_WINDOW, onFrameClose);

    // remove the handler
    if ( keys )
        keys->PopEventHandler(true /* delete it */);

    // …and the focus where it was, before the command chosen runs (below)
    if ( focused && focused != FindFocus() )
        focused->SetFocus();

    wxMenuEvent close(wxEVT_MENU_CLOSE, menuId, menu);
    wxMenu::ProcessMenuEvent(menu, close, window);

    // a right click outside the menu dismissed it - now the modal loop is gone
    // the click is the window's under the cursor, as a native menu leaves it:
    // its own right press (a tree, a list, a grid select there and pop their
    // menus), the release that follows its own too (the context menu event
    // of the windows asking for that one); a context menu event sent instead
    // reached none of the first ones - the menu shut, no new one opened
    if ( s_pendingContextReopen )
    {
        s_pendingContextReopen = false;

        wxWindow* target = wxFindWindowAtPoint(s_pendingContextPos);
        if ( target )
        {
            wxMouseEvent* evt = new wxMouseEvent(wxEVT_RIGHT_DOWN);
            evt->SetPosition(target->ScreenToClient(s_pendingContextPos));
            evt->SetRightDown(true);
            evt->SetId(target->GetId());
            evt->SetEventObject(target);
            target->GetEventHandler()->QueueEvent(evt);
        }
    }

    window->SetCursor(cursorOld);

    // the item clicked, now the loop is gone (ibPopupMenuWindow::ClickItem)
    if ( s_pendingClickItem )
    {
        ibMenu *clicked = s_pendingClickMenu;
        ibMenuItem *item = s_pendingClickItem;
        s_pendingClickMenu = nullptr;
        s_pendingClickItem = nullptr;

        clicked->ClickItem(item);
    }

    return true;
}

void ibWindow::DismissPopupMenu()
{
    wxCHECK_RET( ms_evtLoopPopup, wxT("no popup menu shown") );

    ms_evtLoopPopup->Exit();
}

#endif // wxUSE_MENUS
