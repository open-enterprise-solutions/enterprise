// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically): the top-level window drawing its
// own decorations (src/univ/topluniv.cpp), over a native window of wx's.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/toplevel.h
// Purpose:     Top level window, abstraction of ibFrame and ibDialog
// Author:      Vaclav Slavik
// Copyright:   (c) 2001-2002 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////


#ifndef _DESKGUI_UNIV_TOPLEVEL_H_
#define _DESKGUI_UNIV_TOPLEVEL_H_

#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"

#include <algorithm>
#include <functional>
#include <vector>

#include <wx/toplevel.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/cshelp.h>
#include <wx/image.h>
#include <wx/utils.h>
#include <wx/frame.h>
#include <wx/dialog.h>

//-----------------------------------------------------------------------------
// ibWindowShowLocker
//-----------------------------------------------------------------------------

// A TOP-LEVEL WINDOW SHOWN WHOLE, as wxWindowUpdateLocker holds a window still: kept off the screen while it is shown
// (MSW: DWMWA_CLOAK), painted whole as it leaves the scope - every window in it - and only then on the screen. Shown,
// the system paints a window by parts, one paint each, as the queue gets to them: a white window, then its parts, a
// dialog half painted (Max, 2026-11-10: "the window should appear only once everything has loaded"). A window shown
// already is left as it is
class DESKGUI_API ibWindowShowLocker
{
public:
    explicit ibWindowShowLocker(wxTopLevelWindow *window);
    ~ibWindowShowLocker();

private:
    wxTopLevelWindow *m_window;   // the window kept off the screen - none when it was shown already

    wxDECLARE_NO_COPY_CLASS(ibWindowShowLocker);
};

#ifdef __WXMSW__
// THE SYSTEM MOVES AND SIZES A WINDOW WHOSE DECORATIONS ARE PAINTED (ibTopLevelWindow), as it does a native one: it snaps
// it to the screen's edges and halves, moves it by the keys (Win+arrows), maximizes it dragged by its title bar to the
// top (Max, 2026-11-11: our windows could do none of it - the mouse, captured, moved them). Its native window keeps the
// caption and frame styles that let the system do so and has no non-client area (WM_NCCALCSIZE): the client area is the
// whole window (ibTopLevelWindow::GetFrameRect), the decorations painted in it, and the parts of them are named to the system for what they are
// (WM_NCHITTEST; hitTest - the part at a point of the client area, wxHT_TOPLEVEL_xxx); maximized, the client area is
// the work area, the frame the system puts past its edges off the screen. The system's menu is not kept. True when the
// message is answered, its answer in result
DESKGUI_API bool ibMSWDecorationsProc(wxTopLevelWindow *window,
                                      long (*hitTest)(const wxTopLevelWindow *window, const wxPoint& pt),
                                      WXUINT message, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT *result);

// every paint pending in the window and in its children, painted now - not left to the queue, which gets to them one
// by one (ibWindowShowLocker)
DESKGUI_API void ibMSWPaintPending(wxWindow *window);

// THE SYSTEM'S DROP SHADOW ROUND A POPUP, ITS OWN: the class's style (CS_DROPSHADOW) given for the moment show shows the
// window, and taken back - the class is every wx window's (wxWindowNR), and kept it gave the shadow to every popup shown
// after the first menu (2026-11-11, Max: "it spreads"). The shadow cast stays with the window till it hides. What show
// answers. On GTK3/Cocoa a popup's shadow is the window manager's, the window's own
DESKGUI_API bool ibMSWShowShadowed(wxWindow *window, const std::function<bool()>& show);
#endif // __WXMSW__

//-----------------------------------------------------------------------------
// ibTopLevelWindow
//-----------------------------------------------------------------------------

// SEAM vs univ: THE WINDOW IS wx's OWN, ITS DECORATIONS OURS - as the designer's form card has them (ibInnerFrame): the
// native window is made without its caption and frame, the wx style keeps both for what is painted in their place
// (GetDecorationsStyle) - the theme's title bar, its buttons and the border (ibRenderer::DrawFrameTitleBar) round the
// client area (GetClientAreaOrigin, DoGetClientSize). On MSW the system moves and sizes it, held by the title bar and
// the border (ibMSWDecorationsProc); elsewhere the mouse, captured, does. Not kept: native decorations on demand, the
// full screen, the system menu, the input handler and its actions.
template <class W>
class ibTopLevelWindow : public W
{
public:
    ibTopLevelWindow();

    // W's, its native window without the decorations - on MSW with the system's styles of them (ibMSWDecorationsProc) -
    // the decorations painted again as it is sized (OnSize); with no place given, over the middle of its parent
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos,
                const wxSize& size,
                long style,
                const wxString& name);

    // the part of the window at the point of the client area: wxHT_TOPLEVEL_xxx
    long HitTest(const wxPoint& pt) const;

    // W's, on the screen whole (ibWindowShowLocker) - a dialog's ShowModal shows it here too
    virtual bool Show(bool show = true) override;
    // W's - the window deleted when the application is idle (wxTopLevelWindowBase::Destroy) - and the application woken
    // for it: a person's close comes with the events that wake it, a close from the code may come with none - a signal's
    // (ibWxApp::OnStopSignal), a timer's. On GTK3 the idle is gone while nothing happens and is put back by a widget's
    // event: the window waited to be deleted and the process lived on (codeRunner after its SIGTERM under Xvfb, CI's look
    // step for half an hour, 2026-11-12). On MSW and Cocoa the wake costs nothing
    virtual bool Destroy() override;
    // W's, and the window painted at once after it, every window in it (Update is the one window's): a queue gives the
    // paint after the input, the pointer moved on before it, and the window followed the pointer unpainted - the one
    // place both roads of a size pass, the system's (MSW) and the captured mouse's (OnMouseMotion). Where the children's
    // moves wait for the end of the size (MSW), they are painted there (EndRepositioningChildren)
    virtual bool Layout() override;

    virtual wxPoint GetClientAreaOrigin() const override;
    virtual wxSize GetMinSize() const override;
    virtual void SetTitle(const wxString& title) override;
    // W's, and the icon of the title bar - the canon's (wxTopLevelWindow::SetIcons): of the window's icons the one of
    // the theme's size, or one scaled to it
    virtual void SetIcons(const wxIconBundle& icons) override;

    // …and at every restyle: wxNavigationEnabled toggles wxTAB_TRAVERSAL at the first child, and the native caption
    // came back with it (ibWindow::MSWGetStyle); wxDialog makes its native size grip for wxRESIZE_BORDER and puts it
    // by the client size in the window's corner - inside ours, above the frame
    virtual void SetWindowStyleFlag(long style) override;

#ifdef __WXMSW__
    // W's, a dialog a popup still - wx makes every dialog one (wxTopLevelWindowMSW::CreateDialog), and its styles made
    // again for the caption and the frame (Create, SetWindowStyleFlag) took it off: hidden, an overlapped window does not
    // hand the activation to its owner, the system gave it to the window under it - another program's (2026-11-12,
    // Max: "the last one activates anything - Explorer, the browser"). On GTK3/Cocoa the dialog is the port's own
    virtual WXDWORD MSWGetStyle(long flags, WXDWORD *exstyle = nullptr) const override;
    // the system moves and sizes the window by its decorations (ibMSWDecorationsProc)
    virtual WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
    // W's - the children moved where the size laid them out, all at once (wxMSW defers the moves to the end of the size,
    // wxWindowMSW::HandleSize) - and the window painted at once, every window in it: painted in Layout, they were painted
    // where they stood before the size, and painted again when moved (2026-11-11, measured: 35 ms a step, 43 with it)
    virtual void EndRepositioningChildren() override;
#endif // __WXMSW__

protected:
    // THE DECORATIONS PAINTED BEFORE THE WINDOW'S OWN PAINT, as a control's frame is (ibWindow::TryBefore): the window
    // whole - its colour, the title bar and the border - and the paint goes on to whoever draws inside the client area,
    // the window's own handler. A handler of the paint here took it whole, and one bound after it took it first (the AUI
    // manager's, when it laid out the frame itself - it lays out a window inside it now, ibAuiManager::SetManagedWindow):
    // the title bar was never painted
    virtual bool TryBefore(wxEvent& event) override;

    virtual void DoGetClientSize(int *width, int *height) const override;
    virtual void DoSetClientSize(int width, int height) override;

private:
    // the rectangle the decorations are painted round the client area in, the paint DC's (0, 0) at its corner: the
    // whole window - on MSW its native client area, the part of a maximized window on the work area
    // (ibMSWDecorationsProc)
    wxRect GetFrameRect() const;
    // wxTOPLEVEL_xxx of the window's style and state
    long GetDecorationsStyle() const;

    // the mouse event's point in the client area, as HitTest takes it: wxMSW gives it so (wxWindowMSW::InitMouseEvent
    // takes the client area's origin off), wxOSX gives it in the native view's coordinates - the whole window's, the
    // decorations' too (wxSetupCoordinates) - and the title bar's x was hit a title bar's height below it; wxGTK in its
    // native area's, the whole window's as well (an ibWindow's is taken off in ibWindow::GTKProcessEvent)
    wxPoint GetClientPoint(const wxMouseEvent& event) const;

    // the strips round the client area painted again - not the children inside it
    void RefreshTitleBar();
    // the window whole on the DC whose (0, 0) is the window's corner: its colour, the decorations round the client area
    void PaintFrame(wxDC& dc);

    void ClickTitleBarButton(long button);

    // the decorations the size moved painted again - where they stood at the size they were last painted at
    // (m_paintedSize), inside the window grown, and where they stand now: the canon's way for a border painted in the
    // client area (wxWindow::OnSize, univ - its wxFULL_REPAINT_ON_RESIZE is the FIXME's stopgap, "very expensive")
    void OnSize(wxSizeEvent& event);
    void OnActivateTitleBar(wxActivateEvent& event);
    void OnSetCursor(wxSetCursorEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

    // the native window's style, as the canon's (wxTopLevelWindow::Create, univ): the decorations taken away - they are
    // painted - its children clipped. Not painted whole again as it is sized (wxFULL_REPAINT_ON_RESIZE), the decorations
    // the size moved are (OnSize): with it, on MSW the system kept nothing of what the window showed and DWM held the
    // old picture where it stood on the SCREEN until each part was painted again - sized by its left edge, the menu, the
    // tool bar and the status bar stood a step to the right in the window grown - and every child was painted again at
    // every step: 52-60 ms a step, 37-40 without (2026-11-11, read off the window's surface during a step; Max: "it was
    // reflected on what is on the right")
    static long GetNativeStyle(long style)
    {
        return (style & ~s_decorations) | wxBORDER_NONE | wxCLIP_CHILDREN;
    }

    // the canon's (wxGetResizingCursor): the cursor of the border at the part hit - none for another part
    static bool GetResizingCursor(long hit, wxCursor& cursor);

    // the decorations: in the wx style, not in the native window's
    static constexpr long s_decorations = wxCAPTION | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX | wxSYSTEM_MENU |
                                          wxRESIZE_BORDER | wxBORDER_MASK;

    ibRenderer *m_renderer;
    wxIcon m_titlebarIcon;      // the window's icon at the size the title bar draws it
    wxSize m_paintedSize;       // the window's size its decorations were last painted at (PaintFrame)
    bool m_isActive = false;
    long m_grip = 0;            // what the mouse holds: the title bar, the border or a button (wxHT_TOPLEVEL_xxx)
    long m_pressedButton = 0;   // the button held, while the mouse is over it
    wxRect m_gripRect;          // the window as it was grabbed, on the screen
    wxPoint m_gripPoint;        // the pointer as it grabbed, on the screen
};

// MADE ONCE, IN DESKGUI (topLevel.cpp), for the two windows it is made for - its body there, not in every file that sees
// the frame or the dialog: a change of it rebuilt them all (Max, 2026-11-11). In deskgui's own files unmarked: MSVC
// refuses an export on an instantiation declared extern (C4910) - the one in topLevel.cpp exports it - and warns there
// of every member whose body it does not see (C4661, "can be ignored if defined elsewhere": topLevel.cpp's)
#ifdef DESKGUI_EXPORTS
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4661)
#endif // _MSC_VER
extern template class ibTopLevelWindow<wxFrame>;
extern template class ibTopLevelWindow<wxDialog>;
#ifdef _MSC_VER
    #pragma warning(pop)
#endif // _MSC_VER
#else
extern template class DESKGUI_API ibTopLevelWindow<wxFrame>;
extern template class DESKGUI_API ibTopLevelWindow<wxDialog>;
#endif // DESKGUI_EXPORTS

#endif // _DESKGUI_UNIV_TOPLEVEL_H_
