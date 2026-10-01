// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/menu.h
// Purpose:     ibMenu and ibMenuBar classes for wxUniversal
// Author:      Vadim Zeitlin
// Created:     05.05.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_MENU_H_
#define _WX_UNIV_MENU_H_

#include "frontend/frontend.h"

class ibWindow;
class ibMenuItem;

#if wxUSE_ACCEL
    #include <wx/accel.h>
#endif // wxUSE_ACCEL

#include <wx/dynarray.h>
#include <wx/menu.h>

// fwd declarations
class ibMenuInfo;
class ibMenuGeometryInfo;
class ibPopupMenuWindow;
class ibRenderer;

// ----------------------------------------------------------------------------
// ibMenu
// ----------------------------------------------------------------------------

// SEAM vs univ: there wxMenu itself was the univ class. Here we derive from
// the NATIVE wxMenu so that the generic wxMenuBase bookkeeping (item list,
// FindItem, SendEvent, invoking window) keeps working and wxMenuItem* /
// wxMenu* downcast to ibMenuItem* / ibMenu* along a single chain. The native
// HMENU the base creates is idle dead weight: the creating Append/Insert
// virtuals are overridden and the menu is only shown by the univ popup.
class FRONTEND_API ibMenu : public wxMenu
{
public:
    // ctors and dtor
    ibMenu(const wxString& title, long style = 0)
        : wxMenu(title, style) { Init(); }

    ibMenu(long style = 0) : wxMenu(style) { Init(); }

    virtual ~ibMenu();

    // called by ibMenuItem when an item of this menu changes
    void RefreshItem(ibMenuItem *item);

    // does the menu have any items?
    bool IsEmpty() const { return !GetMenuItems().GetFirst(); }

    // SEAM vs univ: the generic creating wxMenuBase::Append/Insert overloads
    // build items through the static wxMenuItem::New factory, i.e. NATIVE
    // wxMenuItem objects without the univ geometry fields. Hide them with
    // factories creating ibMenuItem (bodies in the .cpp — dllimport inlines
    // of a FRONTEND_API class do not instantiate inside the DLL).
    wxMenuItem *Append(int itemid,
                       const wxString& text = wxEmptyString,
                       const wxString& help = wxEmptyString,
                       wxItemKind kind = wxITEM_NORMAL);
    wxMenuItem *AppendSeparator();
    wxMenuItem *AppendCheckItem(int itemid,
                                const wxString& text,
                                const wxString& help = wxEmptyString);
    wxMenuItem *AppendRadioItem(int itemid,
                                const wxString& text,
                                const wxString& help = wxEmptyString);
    wxMenuItem *AppendSubMenu(ibMenu *submenu,
                              const wxString& text,
                              const wxString& help = wxEmptyString);

    // show this menu at the given position (in screen coords) and optionally
    // select its first item
    void Popup(const wxPoint& pos, const wxSize& size,
               bool selectFirst = true);

    // dismiss the menu
    void Dismiss();

    // override the base class methods to connect/disconnect event handlers
    virtual void Attach(wxMenuBarBase *menubar) override;
    virtual void Detach() override;

    // implementation only from here

    // do as if this item were clicked, return true if the resulting event was
    // processed, false otherwise
    bool ClickItem(ibMenuItem *item);

    // process the key event, return true if done
    bool ProcessKeyDown(int key);

#if 0 // UIKIT-REVIVE: the univ accel-table API (Add/Remove/GetMenuItem) does
      // not exist in the native wxAcceleratorTable build
    // find the item for the given accel and generate an event if found
    bool ProcessAccelEvent(const wxKeyEvent& event);
#endif // wxUSE_ACCEL

protected:
    // implement base class virtuals (wxMenuItem* per the generic signatures;
    // every item is created as ibMenuItem by the factories above)
    virtual wxMenuItem* DoAppend(wxMenuItem *item) override;
    virtual wxMenuItem* DoInsert(size_t pos, wxMenuItem *item) override;
    virtual wxMenuItem* DoRemove(wxMenuItem *item) override;

    // common part of DoAppend and DoInsert
    void OnItemAdded(ibMenuItem *item);

    // called by ibPopupMenuWindow when the window is hidden
    void OnDismiss(bool dismissParent);

    // return true if the menu is currently shown on screen
    bool IsShown() const;

    // get the menu geometry info
    const ibMenuGeometryInfo& GetGeometryInfo() const;

    // forget old menu geometry info
    void InvalidateGeometryInfo();

    // return either the menubar or the invoking window, normally never null
    ibWindow *GetRootWindow() const;

    // get the renderer we use for drawing: either the one of the menu bar or
    // the one of the window if we're a popup menu
    ibRenderer *GetRenderer() const;

#if 0 // UIKIT-REVIVE: univ accel-table API not in the native build
    // add/remove accel for the given menu item
    void AddAccelFor(ibMenuItem *item);
    void RemoveAccelFor(ibMenuItem *item);
#endif // wxUSE_ACCEL

private:
    // common part of all ctors
    void Init();

    // terminate the current radio group, if any
    void EndRadioGroup();

    // the exact menu geometry is defined by a struct derived from this one
    // which is opaque and defined by the renderer
    ibMenuGeometryInfo *m_geometry;

    // the menu shown on screen or nullptr if not currently shown
    ibPopupMenuWindow *m_popupMenu;

#if 0 // UIKIT-REVIVE: univ accel-table API not in the native build
    // the accel table for this menu
    wxAcceleratorTable m_accelTable;
#endif // wxUSE_ACCEL

    // the position of the first item in the current radio group or -1
    int m_startRadioGroup;

    // it calls out OnDismiss()
    friend class ibPopupMenuWindow;
    wxDECLARE_DYNAMIC_CLASS(ibMenu);
};

// ----------------------------------------------------------------------------
// ibMenuBar
// ----------------------------------------------------------------------------

#if 0 // UIKIT-REVIVE: univ menubar needs the univ frame (Attach(ibFrame*),
      // DoDraw, the whole top-level wiring) — cut until the frame revives

class FRONTEND_API ibMenuBar : public wxMenuBarBase
{
public:
    // ctors and dtor
    ibMenuBar(long WXUNUSED(style) = 0);
    ibMenuBar(size_t n, ibMenu *menus[], const wxString titles[], long style = 0);
    virtual ~ibMenuBar();

    // implement base class virtuals
    virtual bool Append( ibMenu *menu, const wxString &title ) override;
    virtual bool Insert(size_t pos, ibMenu *menu, const wxString& title) override;
    virtual ibMenu *Replace(size_t pos, ibMenu *menu, const wxString& title) override;
    virtual ibMenu *Remove(size_t pos) override;

    virtual void EnableTop(size_t pos, bool enable) override;
    virtual bool IsEnabledTop(size_t pos) const override;

    virtual void SetMenuLabel(size_t pos, const wxString& label) override;
    virtual wxString GetMenuLabel(size_t pos) const override;

    virtual void Attach(ibFrame *frame) override;
    virtual void Detach() override;

    // get the next item for the givan accel letter (used by ibFrame), return
    // -1 if none
    //
    // if unique is not null, filled with true if there is only one item with
    // this accel, false if two or more
    int FindNextItemForAccel(int idxStart,
                             int keycode,
                             bool *unique = nullptr) const;

    // called by ibFrame to set focus to or open the given menu
    void SelectMenu(size_t pos);
    void PopupMenu(size_t pos);

#if wxUSE_ACCEL
    // find the item for the given accel and generate an event if found
    bool ProcessAccelEvent(const wxKeyEvent& event);
#endif // wxUSE_ACCEL

    // called by ibMenu when it is dismissed
    void OnDismissMenu(bool dismissMenuBar = false);

protected:
    // common part of all ctors
    void Init();

    // event handlers
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnKillFocus(wxFocusEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);

    // process the mouse move event, return true if we did, false to continue
    // processing as usual
    //
    // the coordinates are client coordinates of menubar, convert if necessary
    bool ProcessMouseEvent(const wxPoint& pt);

    // called when the menu bar loses mouse capture - it is not hidden unlike
    // menus, but it doesn't have modal status any longer
    void OnDismiss();

    // draw the menubar
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // menubar geometry
    virtual wxSize DoGetBestClientSize() const override;

    // has the menubar been created already?
    bool IsCreated() const { return m_frameLast != nullptr; }

    // "fast" version of GetMenuCount()
    size_t GetCount() const;

    // get the (total) width of the specified menu
    wxCoord GetItemWidth(size_t pos) const;

    // get the rect of the item
    wxRect GetItemRect(size_t pos) const;

    // get the menu from the given point or -1 if none
    int GetMenuFromPoint(const wxPoint& pos) const;

    // refresh the given item
    void RefreshItem(size_t pos);

    // refresh all items after this one (including it)
    void RefreshAllItemsAfter(size_t pos);

    // hide the currently shown menu and show this one
    void DoSelectMenu(size_t pos);

    // popup the currently selected menu
    void PopupCurrentMenu(bool selectFirst = true);

    // hide the currently selected menu
    void DismissMenu();

    // do we show a menu currently?
    bool IsShowingMenu() const { return m_menuShown != nullptr; }

    // we don't want to have focus except while selecting from menu
    void GiveAwayFocus();

    // Release the mouse capture if we have it
    bool ReleaseMouseCapture();

    // the array containing extra menu info we need
    std::vector<ibMenuInfo> m_menuInfos;

    // the current item (only used when menubar has focus)
    int m_current;

private:
    // the last frame to which we were attached, nullptr initially
    ibFrame *m_frameLast;

    // the currently shown menu or nullptr
    ibMenu *m_menuShown;

    // should be showing the menu? this is subtly different from m_menuShown !=
    // nullptr as the menu which should be shown may be disabled in which case we
    // don't show it - but will do as soon as the focus shifts to another menu
    bool m_shouldShowMenu;

    // it calls out ProcessMouseEvent()
    friend class ibPopupMenuWindow;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibMenuBar);
};

#endif // UIKIT-REVIVE: univ menubar needs univ frame

#endif // _WX_UNIV_MENU_H_
