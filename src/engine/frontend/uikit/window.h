// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/window.h
// Purpose:     ibWindow class which is the base class for all
//              wxUniv port controls, it supports the customization of the
//              window drawing and input processing.
// Author:      Vadim Zeitlin
// Created:     06.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_WINDOW_H_
#define _WX_UNIV_WINDOW_H_

#include "frontend/frontend.h"

#include <wx/bitmap.h>      // for m_bitmapBg

class ibControlRenderer;
class wxEventLoop;

class ibMenu;
class ibMenuBar;

class ibRenderer;

#if wxUSE_SCROLLBAR
    class ibScrollBar;
#endif // wxUSE_SCROLLBAR

#ifdef __WXX11__
#define wxUSE_TWO_WINDOWS 1
#else
#define wxUSE_TWO_WINDOWS 0
#endif

// ----------------------------------------------------------------------------
// ibWindow: derives from the cross-platform wxWindow — the native port
// underneath (MSW / OSX / GTK) supplies the window host, DC and raw input
// (the univ original derived from wxWindowNative for the same effect)
// ----------------------------------------------------------------------------

class FRONTEND_API ibWindow : public wxWindow
{
public:
    // ctors and create functions
    // ---------------------------

    ibWindow() { Init(); }

    ibWindow(wxWindow *parent,
             wxWindowID id,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxString& name = wxASCII_STR(wxPanelNameStr))
        : wxWindow(parent, id, pos, size, style | wxCLIP_CHILDREN, name)
        { Init(); }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxPanelNameStr));

    virtual ~ibWindow();

    // background pixmap support
    // -------------------------

    virtual void SetBackground(const wxBitmap& bitmap,
                               int alignment = wxALIGN_CENTRE,
                               wxStretch stretch = wxSTRETCH_NOT);

    const wxBitmap& GetBackgroundBitmap(int *alignment = nullptr,
                                        wxStretch *stretch = nullptr) const;

    // scrollbars: we (re)implement it ourselves using our own scrollbars
    // instead of the native ones
    // ------------------------------------------------------------------

    virtual void SetScrollbar(int orient,
                              int pos,
                              int page,
                              int range,
                              bool refresh = true ) override;
    virtual void SetScrollPos(int orient, int pos, bool refresh = true) override;
    virtual int GetScrollPos(int orient) const override;
    virtual int GetScrollThumb(int orient) const override;
    virtual int GetScrollRange(int orient) const override;
    virtual void ScrollWindow(int dx, int dy,
                              const wxRect* rect = nullptr) override;

    // take into account the borders here
    virtual wxPoint GetClientAreaOrigin() const override;

    // popup menu support
    // ------------------

    // NB: all menu related functions are implemented in menu.cpp

    // SEAM vs univ: there ibMenu was wxMenu and the native PopupMenu() entry
    // dispatched into DoPopupMenu below; here ibMenu is a separate type, so
    // the popup entry point gets its own overloads (hiding the wxMenu ones —
    // an ib window shows ib menus); bodies in menu.cpp
    bool PopupMenu(ibMenu *menu, const wxPoint& pos = wxDefaultPosition);
    bool PopupMenu(ibMenu *menu, int x, int y);

    // this is wxUniv-specific private method to be used only by ibMenu
    void DismissPopupMenu();

    // miscellaneous other methods
    // ---------------------------

    // get the state information
    virtual bool IsFocused() const;
    virtual bool IsCurrent() const;
    virtual bool IsPressed() const;
    virtual bool IsDefault() const;

    // return all state flags at once (combination of wxCONTROL_XXX values)
    int GetStateFlags() const;

    // set the "highlighted" flag and return true if it changed
    virtual bool WXMakeCurrent(bool doit = true);

#if wxUSE_SCROLLBAR
    // get the scrollbar (may be null) for the given orientation
    ibScrollBar *GetScrollbar(int orient) const
    {
        return orient & wxVERTICAL ? m_scrollbarVert : m_scrollbarHorz;
    }
#endif // wxUSE_SCROLLBAR

    // methods used by ibColourScheme to choose the colours for this window
    // --------------------------------------------------------------------

    // return true if this control can be highlighted when the mouse is over
    // it (the theme decides itself whether it is really highlighted or not)
    virtual bool CanBeHighlighted() const { return false; }

    // return true if we should use the colours/fonts returned by the
    // corresponding GetXXX() methods instead of the default ones
    bool UseFgCol() const { return m_hasFgCol; }
    bool UseFont() const { return m_hasFont; }

    // return true if this window serves as a container for the other windows
    // only and doesn't get any input itself
    virtual bool IsStaticBox() const { return false; }

    // returns the (low level) renderer to use for drawing the control by
    // querying the current theme
    ibRenderer *GetRenderer() const { return m_renderer; }

    // scrolling helper: like ScrollWindow() except that it doesn't refresh the
    // uncovered window areas but returns the rectangle to update (don't call
    // this with both dx and dy non zero)
    wxRect ScrollNoRefresh(int dx, int dy, const wxRect *rect = nullptr);

    // after scrollbars are added or removed they must be refreshed by calling
    // this function
    void RefreshScrollbars();

    // repaint the window border (e.g. on focus change — the border colour
    // follows the state flags)
    void RefreshBorder();

    // erase part of the control
    virtual void EraseBackground(wxDC& dc, const wxRect& rect);

    // overridden base class methods
    // -----------------------------

    // the rect coordinates are, for us, in client coords, but if no rect is
    // specified, the entire window is refreshed
    virtual void Refresh(bool eraseBackground = true,
                         const wxRect *rect = nullptr) override;

    // we refresh the window when it is dis/enabled
    virtual bool Enable(bool enable = true) override;

    // should we use the standard control colours or not?
    virtual bool ShouldInheritColours() const override { return false; }

    virtual bool IsClientAreaChild(const wxWindow *child) const override
    {
#if wxUSE_SCROLLBAR
        if ( child == (wxWindow*)m_scrollbarHorz ||
             child == (wxWindow*)m_scrollbarVert )
            return false;
#endif
        return wxWindow::IsClientAreaChild(child);
    }

    virtual wxSize GetWindowBorderSize() const override;

protected:
    // common part of all ctors
    void Init();

    // SEAM vs univ: no `override` — the wxWindowBase virtual takes wxMenu*,
    // ours takes ibMenu*, so this is an independent overload
    virtual bool DoPopupMenu(ibMenu *menu, int x, int y);

    // we deal with the scrollbars in these functions
    virtual void DoSetClientSize(int width, int height) override;
    virtual void DoGetClientSize(int *width, int *height) const override;
    virtual wxHitTest DoHitTest(wxCoord x, wxCoord y) const override;

    // event handlers
    void OnSize(wxSizeEvent& event);
    void OnNcPaint(wxNcPaintEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnErase(wxEraseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

#if wxUSE_ACCEL // UIKIT-REVIVE: menus cut
    void OnKeyDown(wxKeyEvent& event);
#endif // wxUSE_ACCEL

#if 0 // UIKIT-REVIVE: univ menu not revived yet
    void OnChar(wxKeyEvent& event);
    void OnKeyUp(wxKeyEvent& event);
#endif // wxUSE_MENUS

    // draw the control background, return true if done
    virtual bool DoDrawBackground(wxDC& dc);

    // draw the controls border
    virtual void DoDrawBorder(wxDC& dc, const wxRect& rect);

    // draw the controls contents
    virtual void DoDraw(ibControlRenderer *renderer);

    // adjust the size of the window to take into account its borders
    wxSize AdjustSize(const wxSize& size) const;

    // put the scrollbars along the edges of the window
    void PositionScrollbars();

#if 0 // UIKIT-REVIVE: univ menu not revived yet
    // return the menubar of the parent frame or nullptr
    ibMenuBar *GetParentFrameMenuBar() const;
#endif // wxUSE_MENUS

    // the renderer we use
    ibRenderer *m_renderer;

    // background bitmap info
    wxBitmap  m_bitmapBg;
    int       m_alignBgBitmap;
    wxStretch m_stretchBgBitmap;

    // old size
    wxSize m_oldSize;

    // is the mouse currently inside the window?
    bool m_isCurrent:1;

#ifdef __WXMSW__
public:
    // override MSWWindowProc() to process WM_NCHITTEST
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam);
#endif // __WXMSW__

private:

#if wxUSE_SCROLLBAR
    // the window scrollbars
    ibScrollBar *m_scrollbarHorz,
                *m_scrollbarVert;
#endif // wxUSE_SCROLLBAR

    // the current modal event loop for the popup menu we show or nullptr
    static wxEventLoop *ms_evtLoopPopup;

#if 0 // UIKIT-REVIVE: accel/menubar Alt-navigation needs univ frame
    // the last window over which Alt was pressed (used by OnKeyUp)
    static ibWindow *ms_winLastAltPress;
#endif // wxUSE_MENUS

    wxDECLARE_DYNAMIC_CLASS(ibWindow);
    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_UNIV_WINDOW_H_
