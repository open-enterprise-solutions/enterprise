// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

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

#ifndef _DESKGUI_UNIV_WINDOW_H_
#define _DESKGUI_UNIV_WINDOW_H_

#include "deskgui/deskgui.h"

#include <wx/bitmap.h>      // for m_bitmapBg
#include <wx/dcbuffer.h>    // for ibThemePaintDC

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

class DESKGUI_API ibWindow : public wxWindow
{
public:
    // ctors and create functions
    // ---------------------------

    ibWindow() { Init(); }

    // made as Create makes one: the port's constructor went past it - on GTK3 the native window kept its border and was
    // put at the parent's native corner (Create's GTK section), on MSW its native style was the port's (the base
    // constructor's MSWGetStyle, not ours); six windows were made this way (the grid's, the tree list's header, ...)
    ibWindow(wxWindow *parent,
             wxWindowID id,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        Init();
        Create(parent, id, pos, size, style, name);
    }

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

    // ...and wxWindowBase's for the same reason: the id of the item chosen,
    // wxID_NONE when the menu is dismissed
    int GetPopupMenuSelectionFromUser(ibMenu& menu, const wxPoint& pos = wxDefaultPosition);
    int GetPopupMenuSelectionFromUser(ibMenu& menu, int x, int y);

    // SEAM vs univ: THE THEME'S MENU SHOWN OVER ANY WINDOW, at a point of its client area - an ib window's (DoPopupMenu
    // below) and the native frame's (ibFrame::DoPopupMenu): of the window only where it is, its loop, the keys and the
    // focus the menu's own; every menu here is an ibMenu (ibMenu's factories), the body in menu.cpp
    static bool DoPopupMenu(wxWindow *window, wxMenu *menu, int x, int y);

    // this is wxUniv-specific private method to be used only by ibMenu: the popup menu's loop ended
    static void DismissPopupMenu();

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

    // return true if this window's background is the content's (a text field,
    // a list box), not the chrome's (the theme decides its colour)
    virtual bool HasContentBackground() const { return false; }

    // returns the (low level) renderer to use for drawing the control by
    // querying the current theme
    ibRenderer *GetRenderer() const { return m_renderer; }

    // after scrollbars are added or removed they must be refreshed by calling
    // this function
    void RefreshScrollbars();

    // erase part of the control: a window of a transparent background has its parent's drawn under it; a control's
    // OnPaint gets its background with it from the ibThemePaintDC it paints on
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
             child == (wxWindow*)m_scrollbarVert ||
             child == (wxWindow*)m_scrollbarCorner )
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

    // SEAM vs univ: A DISABLED WINDOW KEEPS ITS HANDLE LIVE. Enable keeps the logical state - IsEnabled(), the theme's
    // wxCONTROL_DISABLED - and the native host is not disabled, so the mouse still reaches a disabled control (the
    // designer selects it by a click) and ibInputConsumer refuses its input itself (inputConsumer.cpp).
    virtual void DoEnable(bool WXUNUSED(enable)) override {}

    // SEAM vs univ: the rectangle, in window coordinates, the border is drawn round - the client area and the
    // scrollbars inside it. The whole window, unless a control puts something beside its frame (ibControl's title).
    virtual wxRect GetBorderRect() const { return wxRect(GetSize()); }

    // SEAM vs univ: the non-client part - the border, the corner between the scrollbars - painted in the window's
    // paint, before the window's own handler paints the client area; drawn anew as the window gains or loses the focus.
    // The mouse over a window with a tip, and what ends it, handed to the theme's tooltip (ibToolTipWindow)
    virtual bool TryBefore(wxEvent& event) override;

    // SEAM vs wx: a window's tip is the theme's tooltip's (ibToolTipWindow), kept from the system's tooltip control -
    // wx's own keeps it (wxWindowBase), the platform's is not asked; a tip set or changed under the mouse shows as it is
    virtual void DoSetToolTip(wxToolTip *tip) override;
    virtual void DoSetToolTipText(const wxString& tip) override;

    // the window's frame round rect (GetBorderRect), drawn in the paint before the window's handler (TryBefore): the
    // theme's border of the window's kind (GetBorder) in its state
    virtual void DrawBorder(wxDC& dc, const wxRect& rect);

    // we deal with the scrollbars in these functions
    virtual void DoSetClientSize(int width, int height) override;
    virtual void DoGetClientSize(int *width, int *height) const override;
    virtual wxHitTest DoHitTest(wxCoord x, wxCoord y) const override;

    // event handlers

    // the scrollbars along the new edges, and the frame the size moved painted again - its right and bottom bands where
    // they stood at the size the window was last painted at (m_paintedSize) and where they stand now, what stands beside
    // it (GetBorderRect): the canon's way, its FIXME's (wxFULL_REPAINT_ON_RESIZE for every window, "very expensive") done.
    // Before the window's own size handler (TryBefore), not among them
    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnErase(wxEraseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

    // adjust the size of the window to take into account its borders
    wxSize AdjustSize(const wxSize& size) const;

    // put the scrollbars along the edges of the window
    void PositionScrollbars();

    // the renderer we use
    ibRenderer *m_renderer;

    // background bitmap info
    wxBitmap  m_bitmapBg;
    int       m_alignBgBitmap;
    wxStretch m_stretchBgBitmap;

    // is the mouse currently inside the window?
    bool m_isCurrent:1;

#ifdef __WXMSW__
public:
    // override MSWWindowProc() to process WM_NCHITTEST
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam);

    // SEAM vs univ: the border and the scrollbars are ours - drawn inside the window - and the native window never
    // takes them. The wx style keeps both for our own pipeline, and the native style is made of it here, at Create
    // and at every restyle (MSWUpdateStyle): wxNavigationEnabled's wxTAB_TRAVERSAL, at the first child, turned a
    // panel's wxBORDER_RAISED into a native 3px frame under ours.
    virtual WXDWORD MSWGetStyle(long flags, WXDWORD *exstyle = nullptr) const override;
#endif // __WXMSW__

#ifdef __WXOSX__
public:
    // SEAM vs OSX: the border is ours here too. wxOSX insets the native view by the theme's frame for a sunken or
    // simple border and moves the DC back over it, but not a refresh: the rect a control invalidates (a glyph typed,
    // ibTextCtrl::RefreshPixelRange) landed the frame's width right and down of where it paints - the left of every
    // glyph typed was never painted again. No frame of wx's own: the view is the whole window, as the MSW one is.
    virtual long MacGetWXBorderSize() const override;
#endif // __WXOSX__

#ifdef __WXGTK__
public:
    // SEAM vs wxGTK: a mouse event's place is the client area's, as wxMSW gives it (wxWindowMSW::InitMouseEvent: "our
    // client coords are not quite the same as Windows ones") - wxGTK's is the native area's, our frame's corner: a click
    // landed the frame's width right and down of what it hit. Given back after, for what wxGTK does with it next (the
    // context menu's place, ClientToScreen)
    virtual bool GTKProcessEvent(wxEvent& event) const override;

protected:
    // SEAM vs wxGTK: a child stands in its parent's client area, as wxMSW and wxOSX place it (AdjustForParentClientOrigin
    // in their DoSetSize, taken back in DoGetPosition) - wxGTK places it from the native area's corner, and ours begins
    // inside it: the frame, a frame's title bar and menu bar are drawn there. On Linux the tool bar stood over the title
    // bar and the menu bar, every child of the frame as much higher, the last one short of the status bar by as much
    // (CI, look-linux, 2026-11-12)
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
    virtual void DoGetPosition(int *x, int *y) const override;
#endif // __WXGTK__

private:

#if wxUSE_SCROLLBAR
    // the window scrollbars
    ibScrollBar *m_scrollbarHorz,
                *m_scrollbarVert;
    // ...and the square between them, a window beside them (PositionScrollbars)
    ibWindow *m_scrollbarCorner;
#endif // wxUSE_SCROLLBAR

    wxSize m_paintedSize;       // the window's size its frame was last painted at (TryBefore)

    // the current modal event loop for the popup menu we show or nullptr
    static wxEventLoop *ms_evtLoopPopup;

    wxDECLARE_DYNAMIC_CLASS(ibWindow);
    wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibThemePaintDC: the DC a control paints itself with in its OnPaint, as a wx control does with wxPaintDC - buffered,
// the window's background (EraseBackground: the theme's, or the parent's under a transparent window) already in it;
// its frame is drawn before the handler (ibWindow::TryBefore)
//
// SEAM vs wx: on what wxAutoBufferedPaintDC stands on (wxPaintDC where the port buffers itself, wxBufferedPaintDC on
// MSW), without its assert of wxBG_STYLE_PAINT. A window's background is the paint's (ibWindow::Init), but wxOSX's
// Create makes a window with a transparent background (HasTransparentBackground: a check box, a radio button, a static
// bitmap) a transparent view, for good - wxBG_STYLE_TRANSPARENT cannot be unset - and every paint of one asserted
// (2026-11-13, CI look-macos: a message box's icon). The surface under it is ours either way: EraseBackground paints the
// parent's. On MSW and GTK3 the style stays the paint's
// ----------------------------------------------------------------------------

class ibThemePaintDC : public wxAutoBufferedPaintDCBase
{
public:
    explicit ibThemePaintDC(ibWindow *window)
        : wxAutoBufferedPaintDCBase(window)
    {
        wxASSERT_MSG(window->GetBackgroundStyle() == wxBG_STYLE_PAINT
                     || window->GetBackgroundStyle() == wxBG_STYLE_TRANSPARENT,
                     "a window painted with the theme draws its whole background itself (ibWindow::Init)");
#ifdef __WXGTK__
        // SEAM vs wxGTK: drawn from the client area's corner and kept inside it, as wxMSW's paint DC begins there
        // (wxClientDCImpl::InitDC) and wxOSX's is clipped to it - wxGTK's begins at the native area's corner, where our
        // frame is (the canon's univ did it in its wxClientDCImpl; on GTK3 wxAutoBufferedPaintDC is wxPaintDC itself)
        const wxPoint origin = window->GetClientAreaOrigin();
        SetDeviceOrigin(origin.x, origin.y);
        SetClippingRegion(wxPoint(0, 0), window->GetClientSize());
#endif // __WXGTK__
        window->EraseBackground(*this, wxRect(window->GetSize()));
    }
};

#endif // _DESKGUI_UNIV_WINDOW_H_
