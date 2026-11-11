// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/menu.h
// Purpose:     ibMenu and ibMenuBar classes for wxUniversal
// Author:      Vadim Zeitlin
// Created:     05.05.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_MENU_H_
#define _DESKGUI_UNIV_MENU_H_

#include "deskgui/deskgui.h"

class ibWindow;
class ibMenuItem;

#if wxUSE_ACCEL
    #include <wx/accel.h>
#endif // wxUSE_ACCEL

#include <wx/dynarray.h>
#include <wx/menu.h>

#include <vector>

// fwd declarations
class ibMenuInfo;
class ibMenuBarWindow;
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
class DESKGUI_API ibMenu : public wxMenu
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
    // of a DESKGUI_API class do not instantiate inside the DLL).
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

#if wxUSE_ACCEL
    // find the item for the given accel and generate an event if found - an
    // enabled one of this menu or of its submenus (ibMenuBar::ProcessAccelEvent)
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

    // return either the menubar's window or the invoking window - an ib window
    // or the native frame (ibFrame::DoPopupMenu), normally never null
    wxWindow *GetRootWindow() const;

    // get the renderer we use for drawing: the theme's, as every ib window's
    ibRenderer *GetRenderer() const;

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

    // the position of the first item in the current radio group or -1
    int m_startRadioGroup;

    // it calls out OnDismiss()
    friend class ibPopupMenuWindow;
    wxDECLARE_DYNAMIC_CLASS(ibMenu);
};

// ----------------------------------------------------------------------------
// ibMenuBar
// ----------------------------------------------------------------------------

// SEAM vs univ: there wxMenuBar itself was the univ window. Here, as ibMenu
// from the native wxMenu, we derive from the NATIVE wxMenuBar: the frame, the
// doc manager and AUI's MDI frame keep speaking to a wxMenuBar*. Its native
// menu is never made - no HMENU, no ::SetMenu (ibFrame::AttachMenuBar) - and
// the native methods that would touch it are overridden; its accelerators are
// its items', answered by it on every port (ProcessAccelEvent, asked by the
// frame's key hook) - the native table was MSW's alone, GTK and Cocoa had none
// (2026-11-12, Max). The native menubar has no
// window of its own - the system draws it in the frame's caption; this one is
// drawn by the theme in its window (ibMenuBarWindow, an ibWindow, child of the
// frame it is attached to), whose size and place are the bar's, as the frame
// lays them out (ibFrame), and which shows the bar's menus as a popup menu is
// shown (ibWindow::DoPopupMenu)
class DESKGUI_API ibMenuBar : public wxMenuBar
{
public:
    // ctors and dtor
    ibMenuBar(long style = 0);
    virtual ~ibMenuBar();

    // implement base class virtuals, the native menu left out
    virtual bool Append(wxMenu *menu, const wxString& title) override;
    virtual bool Insert(size_t pos, wxMenu *menu, const wxString& title) override;
    virtual wxMenu *Replace(size_t pos, wxMenu *menu, const wxString& title) override;
    virtual wxMenu *Remove(size_t pos) override;

    virtual void EnableTop(size_t pos, bool enable) override;
    virtual bool IsEnabledTop(size_t pos) const override;

    virtual void SetMenuLabel(size_t pos, const wxString& label) override;
    virtual wxString GetMenuLabel(size_t pos) const override;

    virtual void Attach(wxFrame *frame) override;
    virtual void Detach() override;

#if wxUSE_ACCEL
    // the item whose accelerator the key is, clicked - in an enabled menu of
    // the bar (used by ibFrame); true if there was one
    bool ProcessAccelEvent(const wxKeyEvent& event);
#endif // wxUSE_ACCEL

    // get the next item for the givan accel letter (used by ibFrame), return
    // -1 if none
    //
    // if unique is not null, filled with true if there is only one item with
    // this accel, false if two or more
    int FindNextItemForAccel(int idxStart,
                             int keycode,
                             bool *unique = nullptr) const;

    // called by ibFrame to open the given menu
    void PopupMenu(size_t pos);

protected:
    // the window's (the bar has none of its own)
    virtual void DoGetSize(int *width, int *height) const override;
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;
    virtual wxSize DoGetBestSize() const override;

private:
    // the window drawing the bar - none before it is first attached
    wxWindow *GetWindow() const;

    // the array containing extra menu info we need
    std::vector<ibMenuInfo> m_menuInfos;

    // the window drawing the bar, made as it is first attached - none before
    ibMenuBarWindow *m_window;

    // they reach the window: the menus' root (ibMenu::GetRootWindow), the
    // mouse moved over the bar while a menu is shown
    // (ibPopupMenuWindow::ProcessMouseMove), the frame laying out its bars
    // (ibFrame::IsOneOfBars); and the window the menus' infos
    friend class ibMenu;
    friend class ibPopupMenuWindow;
    friend class ibFrame;
    friend class ibMenuBarWindow;

    wxDECLARE_DYNAMIC_CLASS(ibMenuBar);
};

#endif // _DESKGUI_UNIV_MENU_H_
