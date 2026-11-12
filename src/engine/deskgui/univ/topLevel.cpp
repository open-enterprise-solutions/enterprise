#include <wx/wxprec.h>

#include "deskgui/univ/topLevel.h"

#include <type_traits>

#include <wx/app.h>         // wxWakeUpIdle - the window deleted when the application is idle (Destroy)
#include <wx/display.h>     // wxDisplay - the work area a window is kept in (Create)

#ifdef __WXGTK3__
    #include <wx/gtk/private/wrapgtk.h>
    #include <wx/gtk/private/backend.h>     // wxGTKImpl::IsWayland - who places the window (OnMouseMotion)
#endif

#ifdef __WXMSW__
    #include <wx/msw/private.h>
    #include <wx/rawbmp.h>      // wxAlphaPixelData - the last row painted opaque, for DWM (TryBefore)
    #include <dwmapi.h>
#endif

ibWindowShowLocker::ibWindowShowLocker(wxTopLevelWindow *window)
    : m_window(nullptr)
{
#ifdef __WXMSW__
    // off the screen while it is shown: the window cloaked keeps its state - shown, active, sized - and DWM shows none
    // of it; a system without the cloak (before Windows 8) shows it as it always did
    if ( window->IsShown() )
        return;

    BOOL cloak = TRUE;
    if ( SUCCEEDED(::DwmSetWindowAttribute(GetHwndOf(window), DWMWA_CLOAK, &cloak, sizeof(cloak))) )
        m_window = window;
#else
    wxUnusedVar(window);
#endif
}

ibWindowShowLocker::~ibWindowShowLocker()
{
#ifdef __WXMSW__
    if ( m_window == nullptr )
        return;

    // every window in it painted now, each paint pending since it was shown - and the window on the screen whole
    ibMSWPaintPending(m_window);

    BOOL cloak = FALSE;
    ::DwmSetWindowAttribute(GetHwndOf(m_window), DWMWA_CLOAK, &cloak, sizeof(cloak));
#endif
}

#ifdef __WXMSW__

namespace
{

// the system's name of the part of the decorations hit (wxHT_TOPLEVEL_xxx): the title bar and the icon are the
// caption; the border its side or its corner, of a window sized by it; a button is the client area - pressed and
// painted here (ibTopLevelWindow::OnLeftDown): the system's name of it is the system's own button, pressed and painted
// by the system
WXLRESULT GetSystemHit(const wxTopLevelWindow *window, long hit)
{
    if ( hit == wxHT_TOPLEVEL_TITLEBAR || hit == wxHT_TOPLEVEL_ICON )
        return HTCAPTION;

    if ( !(hit & wxHT_TOPLEVEL_ANY_BORDER) )
        return HTCLIENT;

    if ( !window->HasFlag(wxRESIZE_BORDER) )
        return HTBORDER;

    switch ( hit )
    {
        case wxHT_TOPLEVEL_BORDER_N:
            return HTTOP;
        case wxHT_TOPLEVEL_BORDER_S:
            return HTBOTTOM;
        case wxHT_TOPLEVEL_BORDER_W:
            return HTLEFT;
        case wxHT_TOPLEVEL_BORDER_E:
            return HTRIGHT;
        case wxHT_TOPLEVEL_BORDER_NW:
            return HTTOPLEFT;
        case wxHT_TOPLEVEL_BORDER_NE:
            return HTTOPRIGHT;
        case wxHT_TOPLEVEL_BORDER_SW:
            return HTBOTTOMLEFT;
        case wxHT_TOPLEVEL_BORDER_SE:
            return HTBOTTOMRIGHT;
    }
    return HTBORDER;
}

} // anonymous namespace

bool ibMSWDecorationsProc(wxTopLevelWindow *window,
                          long (*hitTest)(const wxTopLevelWindow *window, const wxPoint& pt),
                          WXUINT message, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT *result)
{
    switch ( message )
    {
        case WM_NCCALCSIZE:
        {
            // no non-client area: the client area is the whole window, the decorations painted in it. Maximized, the
            // work area, as a native window's: the system puts a maximized window's frame past the work area's edges
            // whatever place it is told (WM_GETMINMAXINFO, WM_WINDOWPOSCHANGING on the change of state - 2026-11-11:
            // -8,-8 1936x1048 over a work area of 1920x1032, the title bar and the border cut off; Max: "the window is
            // cut"), and that frame stays off the screen
            RECT * const rect = wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam)->rgrc[0]
                                       : reinterpret_cast<RECT *>(lParam);
            if ( ::IsZoomed(GetHwndOf(window)) )
            {
                MONITORINFO info;
                info.cbSize = sizeof(info);
                const HMONITOR monitor = ::MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST);
                if ( monitor && ::GetMonitorInfo(monitor, &info) )
                    *rect = info.rcWork;
            }
            *result = 0;
            return true;
        }

        case WM_NCHITTEST:
        {
            // the point on the screen; the native client area is the whole window, the decorations' client area inside it
            POINT pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            ::ScreenToClient(GetHwndOf(window), &pt);
            *result = GetSystemHit(window, hitTest(window, wxPoint(pt.x, pt.y) - window->GetClientAreaOrigin()));
            return true;
        }

        case WM_NCRBUTTONUP:
            // the system's menu, opened by the caption clicked with the right button - not kept
            if ( wParam != HTCAPTION )
                break;
            *result = 0;
            return true;

        case WM_SYSCOMMAND:
            // …and by Alt+Space
            if ( (wParam & 0xFFF0) != SC_KEYMENU || lParam != VK_SPACE )
                break;
            *result = 0;
            return true;

    }

    return false;
}

// MSW's alone: GDI paints a window by window, each paint queued. GTK3 and Cocoa paint a window's tree in one frame
void ibMSWPaintPending(wxWindow *window)
{
    ::RedrawWindow(GetHwndOf(window), nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
}

bool ibMSWShowShadowed(wxWindow *window, const std::function<bool()>& show)
{
    const HWND hWnd = GetHwndOf(window);
    if ( hWnd == nullptr )
        return show();

    // the class's style as it was, given back - a class that cast the shadow of its own keeps it
    const ULONG_PTR style = ::GetClassLongPtr(hWnd, GCL_STYLE);
    ::SetClassLongPtr(hWnd, GCL_STYLE, style | CS_DROPSHADOW);
    const bool shown = show();
    ::SetClassLongPtr(hWnd, GCL_STYLE, style);
    return shown;
}

#endif // __WXMSW__

//-----------------------------------------------------------------------------
// ibTopLevelWindow
//-----------------------------------------------------------------------------

template <class W>
ibTopLevelWindow<W>::ibTopLevelWindow()
    : m_renderer(ibThemeEngine::Get()->GetRenderer())
{
    // the decorations are painted with the client area, all of it at once: nothing erased under the paint
    this->SetBackgroundStyle(wxBG_STYLE_PAINT);

    this->Bind(wxEVT_SIZE, &ibTopLevelWindow::OnSize, this);
    this->Bind(wxEVT_ACTIVATE, &ibTopLevelWindow::OnActivateTitleBar, this);
    this->Bind(wxEVT_SET_CURSOR, &ibTopLevelWindow::OnSetCursor, this);
    this->Bind(wxEVT_MOTION, &ibTopLevelWindow::OnMouseMotion, this);
    this->Bind(wxEVT_LEFT_DOWN, &ibTopLevelWindow::OnLeftDown, this);
    this->Bind(wxEVT_LEFT_UP, &ibTopLevelWindow::OnLeftUp, this);
    this->Bind(wxEVT_LEFT_DCLICK, &ibTopLevelWindow::OnLeftDClick, this);
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &ibTopLevelWindow::OnMouseCaptureLost, this);
}

template <class W>
bool ibTopLevelWindow<W>::Create(wxWindow *parent,
                                 wxWindowID id,
                                 const wxString& title,
                                 const wxPoint& pos,
                                 const wxSize& size,
                                 long style,
                                 const wxString& name)
{
    // the canon's: the native window made without the decorations, the wx style given them back
    if ( !W::Create(parent, id, title, pos, size, GetNativeStyle(style), name) )
        return false;

    const long nativeStyle = this->m_windowStyle;
    this->m_windowStyle = (nativeStyle & ~s_decorations) | (style & s_decorations);
#ifdef __WXMSW__
    // …and the native window given the system's styles of them, the caption and the frame (wx's own, MSWGetStyle),
    // for the system to move and size it (ibMSWDecorationsProc) - once it is made: W made with them, wxDialog makes its
    // native size grip for wxRESIZE_BORDER
    this->MSWUpdateStyle(nativeStyle, this->GetExtraStyle());

    // …and the system's shadow round it, active or not, as round a native window: a window with no non-client area
    // casts none, and the class's drop shadow (CS_DROPSHADOW, the menu's) a window casts only while it is active - gone
    // at a click anywhere else (2026-11-11, Max). DWM's frame taken a pixel into the client area gives it back (the
    // documented custom frame, DwmExtendFrameIntoClientArea) - the window's last row, painted opaque (TryBefore).
    // On GTK3/Cocoa the shadow is the window manager's
    const MARGINS shadow = { 0, 0, 0, 1 };
    ::DwmExtendFrameIntoClientArea(GetHwndOf(this), &shadow);
#endif // __WXMSW__

    // the theme's surface, not the platform's: a native window's default is the system's colour (BTNFACE on MSW, the
    // appearance's on macOS - black there, under Luna's black text), the palette's for a dialog is chrome (FRAME,
    // docs/private/ui-palette.md). Its OWN colour, so the labels on it take it (ibLunaColourScheme::GetBackground)
    // and one set on the window afterwards wins
    if ( !this->UseBgCol() )
        this->SetOwnBackgroundColour(ibThemeEngine::Get()->GetColourScheme()->Get(ibColourScheme::FRAME));

    // no larger than its display's work area, as Cocoa keeps a titled window the user may size (constrainFrameRect) -
    // ours is made without the system's title bar: one larger than the screen had its own off it, Centre keeping the top
    // left on the display and then giving it up for the bottom right (codeRunner's 1100x750 on the 1024x768 screen of
    // CI look-macos, 2026-11-12). The same on every port: the native window is undecorated on all three
    if ( this->HasFlag(wxRESIZE_BORDER) )
    {
        wxSize sizeFit = this->GetSize();
        sizeFit.DecTo(wxDisplay(this).GetClientArea().GetSize());
        if ( sizeFit != this->GetSize() )
            this->SetSize(sizeFit);
    }

    // wx's native dialog with no place given stands by its owner's corner (wxTopLevelWindowMSW::CreateDialog) - centred
    // here, at the size it is made with; one fitted afterwards centres itself again (ibMessageDialog)
    if ( pos == wxDefaultPosition )
        this->Centre();
    return true;
}

template <class W>
long ibTopLevelWindow<W>::HitTest(const wxPoint& pt) const
{
    // the point is the client area's (wxWindowMSW::InitMouseEvent), the frame the whole window's
    return m_renderer->HitTestFrame(GetFrameRect(), pt + GetClientAreaOrigin(), GetDecorationsStyle());
}

template <class W>
wxPoint ibTopLevelWindow<W>::GetClientPoint(const wxMouseEvent& event) const
{
#if defined(__WXOSX__) || defined(__WXGTK__)
    return event.GetPosition() - GetClientAreaOrigin();
#else
    return event.GetPosition();
#endif
}

template <class W>
bool ibTopLevelWindow<W>::Show(bool show)
{
    const ibWindowShowLocker locker(this);
    return W::Show(show);
}

template <class W>
bool ibTopLevelWindow<W>::Destroy()
{
    if ( !W::Destroy() )
        return false;

    wxWakeUpIdle();
    return true;
}

template <class W>
bool ibTopLevelWindow<W>::Layout()
{
    const bool laid = W::Layout();
#ifdef __WXMSW__
    // the children not moved yet - painted when they are (EndRepositioningChildren)
    if ( this->m_hDWP )
        return laid;
#endif // __WXMSW__

    std::vector<wxWindow*> windows{ this };
    while ( !windows.empty() )
    {
        wxWindow * const win = windows.back();
        windows.pop_back();
        win->Update();
        for ( wxWindow *child : win->GetChildren() )
            windows.push_back(child);
    }
    return laid;
}

template <class W>
wxPoint ibTopLevelWindow<W>::GetClientAreaOrigin() const
{
    return m_renderer->GetFrameClientArea(GetFrameRect(), GetDecorationsStyle()).GetPosition();
}

template <class W>
wxSize ibTopLevelWindow<W>::GetMinSize() const
{
    wxSize size = W::GetMinSize();
    size.IncTo(m_renderer->GetFrameMinSize(GetDecorationsStyle()));
    return size;
}

template <class W>
void ibTopLevelWindow<W>::SetTitle(const wxString& title)
{
    W::SetTitle(title);
    RefreshTitleBar();
}

template <class W>
void ibTopLevelWindow<W>::SetIcons(const wxIconBundle& icons)
{
    W::SetIcons(icons);

    // SEAM vs univ: the nearest larger, not the system's size - the frame's icon comes as one of 256x256 (the
    // program's, wxICON), neither the title bar's 16 nor the system's 32, and the frame had none (Max, 2026-11-11).
    // None of an empty bundle, as the canon's (wxTopLevelWindowMSW::SetIcons): GetIcon reads the data it has not - an icon
    // not loaded makes one (wxIconBundle(const wxIcon&)), and every program of the nightly fell at its start (2026-11-12)
    const wxSize size = m_renderer->GetFrameIconSize();
    const wxIcon& icon = !icons.IsEmpty() ? icons.GetIcon(size, wxIconBundle::FALLBACK_NEAREST_LARGER) : wxNullIcon;

    wxBitmap bmp;
    if ( !icon.IsOk() || !bmp.CopyFromIcon(icon) )
        m_titlebarIcon = wxNullIcon;
    else if ( bmp.GetSize() == size )
        m_titlebarIcon = icon;
    else
        m_titlebarIcon.CopyFromBitmap(wxBitmap(bmp.ConvertToImage().Rescale(size.x, size.y, wxIMAGE_QUALITY_HIGH)));

    RefreshTitleBar();
}

template <class W>
void ibTopLevelWindow<W>::SetWindowStyleFlag(long style)
{
    const long decorationsOld = this->m_windowStyle & s_decorations;

    W::SetWindowStyleFlag(GetNativeStyle(style));
    const long nativeStyle = this->m_windowStyle;
    this->m_windowStyle = (nativeStyle & ~s_decorations) | (style & s_decorations);
#ifdef __WXMSW__
    this->MSWUpdateStyle(nativeStyle, this->GetExtraStyle());
#endif // __WXMSW__

    // other decorations, another client area
    if ( (style & s_decorations) != decorationsOld )
    {
        this->SendSizeEvent();
        this->Refresh();
    }
}

#ifdef __WXMSW__

template <class W>
WXDWORD ibTopLevelWindow<W>::MSWGetStyle(long flags, WXDWORD *exstyle) const
{
    WXDWORD msflags = W::MSWGetStyle(flags, exstyle);
    // "all dialogs are popups", the canon's
    if constexpr ( std::is_base_of_v<wxDialog, W> )
        msflags |= WS_POPUP;
    return msflags;
}

template <class W>
WXLRESULT ibTopLevelWindow<W>::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    const auto hitTest = [](const wxTopLevelWindow *window, const wxPoint& pt) -> long
    {
        return static_cast<const ibTopLevelWindow *>(window)->HitTest(pt);
    };

    WXLRESULT result = 0;
    if ( ibMSWDecorationsProc(this, hitTest, message, wParam, lParam, &result) )
        return result;

    return W::MSWWindowProc(message, wParam, lParam);
}

template <class W>
void ibTopLevelWindow<W>::EndRepositioningChildren()
{
    W::EndRepositioningChildren();
    ibMSWPaintPending(this);
}

#endif // __WXMSW__

template <class W>
void ibTopLevelWindow<W>::DoGetClientSize(int *width, int *height) const
{
    const wxRect client = m_renderer->GetFrameClientArea(GetFrameRect(), GetDecorationsStyle());
    if ( width )
        *width = client.width;
    if ( height )
        *height = client.height;
}

template <class W>
void ibTopLevelWindow<W>::DoSetClientSize(int width, int height)
{
    const wxSize size = m_renderer->GetFrameTotalSize(wxSize(width, height), GetDecorationsStyle());
    // the native window's, as the canon's (wxTopLevelWindow::DoSetClientSize, univ) - not W's: wxFrame's, on every port,
    // took the size for its client area again and added its bars to it - the status bar and the client area's origin
    // (the title bar, the menu) counted twice, ibFrame and the renderer had (2026-11-11)
    this->wxTopLevelWindowNative::DoSetClientSize(size.x, size.y);
}

template <class W>
wxRect ibTopLevelWindow<W>::GetFrameRect() const
{
#ifdef __WXMSW__
    // the native client area as wx measures it (wxWindowMSW's - W's own asks GetClientAreaOrigin, which asks this)
    int width = 0;
    int height = 0;
    this->wxWindowMSW::DoGetClientSize(&width, &height);
    return wxRect(wxSize(width, height));
#else
    return wxRect(this->GetSize());
#endif // __WXMSW__
}

template <class W>
long ibTopLevelWindow<W>::GetDecorationsStyle() const
{
    const long windowStyle = this->GetWindowStyleFlag();
    long style = 0;

    if ( windowStyle & wxCAPTION )
    {
        style |= wxTOPLEVEL_TITLEBAR;
        if ( windowStyle & wxCLOSE_BOX )
            style |= wxTOPLEVEL_BUTTON_CLOSE;
        if ( windowStyle & wxMINIMIZE_BOX )
            style |= wxTOPLEVEL_BUTTON_ICONIZE;
        if ( windowStyle & wxMAXIMIZE_BOX )
            style |= this->IsMaximized() ? wxTOPLEVEL_BUTTON_RESTORE : wxTOPLEVEL_BUTTON_MAXIMIZE;
#if wxUSE_HELP
        if ( this->GetExtraStyle() & wxWS_EX_CONTEXTHELP )
            style |= wxTOPLEVEL_BUTTON_HELP;
#endif
    }
    if ( (windowStyle & (wxSIMPLE_BORDER | wxNO_BORDER)) == 0 )
        style |= wxTOPLEVEL_BORDER;
    if ( windowStyle & wxRESIZE_BORDER )
        style |= wxTOPLEVEL_RESIZEABLE;

    if ( this->IsMaximized() )
        style |= wxTOPLEVEL_MAXIMIZED;
    if ( m_titlebarIcon.IsOk() )
        style |= wxTOPLEVEL_ICON;
    if ( m_isActive )
        style |= wxTOPLEVEL_ACTIVE;

    return style;
}

template <class W>
void ibTopLevelWindow<W>::RefreshTitleBar()
{
#ifdef __WXOSX__
    // SEAM vs wxOSX: the paint is raised only when what is dirty touches the client area (wxWindowMac::MacDoRedraw) -
    // the strips alone were never painted; the whole window, the decorations painted with it (TryBefore)
    this->Refresh(false);
#else
    const wxSize size = GetFrameRect().GetSize();
    const wxRect client(GetClientAreaOrigin(), this->GetClientSize());

    this->RefreshRect(wxRect(0, 0, size.x, client.y), false);
    this->RefreshRect(wxRect(0, client.y, client.x, size.y - client.y), false);
    this->RefreshRect(wxRect(client.GetRight() + 1, client.y, size.x - client.GetRight() - 1, size.y - client.y), false);
    this->RefreshRect(wxRect(client.x, client.GetBottom() + 1, client.width, size.y - client.GetBottom() - 1), false);
#endif // __WXOSX__
}

template <class W>
void ibTopLevelWindow<W>::ClickTitleBarButton(long button)
{
    switch ( button )
    {
        case wxTOPLEVEL_BUTTON_CLOSE:
            this->Close();
            break;

        case wxTOPLEVEL_BUTTON_ICONIZE:
            this->Iconize();
            break;

        case wxTOPLEVEL_BUTTON_MAXIMIZE:
            this->Maximize();
            break;

        case wxTOPLEVEL_BUTTON_RESTORE:
            this->Restore();
            break;

        case wxTOPLEVEL_BUTTON_HELP:
#if wxUSE_HELP
            {
            wxContextHelp contextHelp(this);
            }
#endif
            break;

        default:
            wxFAIL_MSG(wxT("incorrect button specification"));
    }
}

template <class W>
bool ibTopLevelWindow<W>::TryBefore(wxEvent& event)
{
    if ( event.GetEventType() != wxEVT_PAINT )
        return W::TryBefore(event);

#ifdef __WXOSX__
    // wxOSX's paint DC begins at the client area and is clipped to it (wxPaintDCImpl), and its clip is the native
    // context's own, the window's every DC drawing under it: the decorations round the client area were never painted -
    // no title bar, no close button, no frame, the window one with what was behind it. No paint DC here (wxOSX needs
    // none, the view is drawing already), the window DC draws on the same context, the whole window. Buffered natively.
    wxWindowDC dc(this);
    PaintFrame(dc);
#else
    wxPaintDC dcPaint(this);

    // the paint DC begins at the client area (GetClientAreaOrigin), the decorations are round it: the whole window -
    // wxOSX's is clipped to the client area too (wxPaintDCImpl), the clip taken off (ibWindow::TryBefore).
    // Not wxBufferedDC: it blits no more than its DC's size, the client size (wxClientDCImpl::DoGetSize) - the frame's
    // bottom and right were left unpainted, what was behind the window showing there
    dcPaint.SetDeviceOrigin(0, 0);
    if ( wxGraphicsContext* const gc = dcPaint.GetGraphicsContext() )
        gc->ResetClip();
    // the buffer of the part to paint - the box of what is dirty, in the whole window's coordinates - not of the whole
    // window: sized, the window is dirty in its strips (OnSize), and a buffer of all of it cost its every step
    const wxSize size = GetFrameRect().GetSize();
    wxRect box = this->GetUpdateRegion().GetBox();
    box.Intersect(wxRect(size));
    if ( box.IsEmpty() )
        box = wxRect(size);
#ifdef __WXMSW__
    // a DIB of 32 bits - its alpha is copied with it (below)
    wxBitmap buffer(box.GetSize(), 32);
#else
    wxBitmap buffer(box.GetSize());
#endif
    wxMemoryDC dc(buffer);
    dc.SetDeviceOrigin(-box.x, -box.y);
    PaintFrame(dc);
    dc.SetDeviceOrigin(0, 0);

#ifdef __WXMSW__
    // THE WINDOW'S LAST ROW IS DWM'S FRAME, taken into the client area for the shadow (Create): DWM lays what is painted
    // there over its own frame by the pixel's alpha, and GDI leaves the alpha 0 - the border's line was the frame's
    // white, the window with no bottom line (2026-11-12, Max: Windows Server; Windows 11 the same). The row made opaque
    // in the buffer, and the buffer told it has no alpha after: Blit copies it as it is (BitBlt), alpha too, where a
    // bitmap with alpha would be AlphaBlended - the rest of it, transparent, over what is behind. On GTK3/Cocoa: no such
    // row, the shadow is the window manager's
    if ( box.GetBottom() == size.y - 1 )
    {
        dc.SelectObject(wxNullBitmap);
        {
            wxAlphaPixelData pixels(buffer, wxPoint(0, box.height - 1), wxSize(box.width, 1));
            wxAlphaPixelData::Iterator pixel(pixels);
            for ( int x = 0; x < box.width; ++x, ++pixel )
                pixel.Alpha() = wxALPHA_OPAQUE;
        }
        buffer.ResetAlpha();
        dc.SelectObject(buffer);
    }
#endif

    dcPaint.Blit(box.x, box.y, box.width, box.height, &dc, 0, 0);
#endif

    return W::TryBefore(event);
}

template <class W>
void ibTopLevelWindow<W>::PaintFrame(wxDC& dc)
{
    const wxRect rect = GetFrameRect();
    m_paintedSize = rect.GetSize();

    dc.SetBackground(wxBrush(this->GetBackgroundColour()));
    dc.Clear();
    m_renderer->DrawFrameTitleBar(dc, rect, this->GetTitle(), m_titlebarIcon,
                                  GetDecorationsStyle(), m_pressedButton, wxCONTROL_PRESSED);
}

template <class W>
void ibTopLevelWindow<W>::OnSize(wxSizeEvent& event)
{
    event.Skip();

    if ( GetFrameRect().GetSize() == m_paintedSize )
        return;

    // the corner of the client area a resizable window's grip is drawn in (ibLunaRenderer::DrawFrameBorder): the square
    // the native grip takes, a scroll bar's corner box (SBS_SIZEGRIP)
    const wxSize grip = m_renderer->GetScrollbarArrowSize();

    // the right and the bottom strips where they stood, and the grip's corner - none before the first paint
    if ( m_paintedSize.x > 0 && m_paintedSize.y > 0 )
    {
        const wxRect client = m_renderer->GetFrameClientArea(wxRect(m_paintedSize), GetDecorationsStyle());
        this->RefreshRect(wxRect(client.GetRight() + 1, 0, m_paintedSize.x - client.GetRight() - 1, m_paintedSize.y),
                          false);
        this->RefreshRect(wxRect(0, client.GetBottom() + 1, m_paintedSize.x, m_paintedSize.y - client.GetBottom() - 1),
                          false);
        this->RefreshRect(wxRect(client.GetRight() + 1 - grip.x, client.GetBottom() + 1 - grip.y, grip.x, grip.y),
                          false);
    }
    // ...and all of them where they stand now, painted with the window once it is laid out (Layout,
    // EndRepositioningChildren). Painted here at once, before the layout, they shortened nothing the screen showed: a
    // step of the left edge 17.4 ms with it, 17.0 without, as many frames of the old picture read off the screen
    // (2026-11-11, measured)
    RefreshTitleBar();
    const wxRect client(GetClientAreaOrigin(), this->GetClientSize());
    this->RefreshRect(wxRect(client.GetRight() + 1 - grip.x, client.GetBottom() + 1 - grip.y, grip.x, grip.y), false);
}

template <class W>
void ibTopLevelWindow<W>::OnActivateTitleBar(wxActivateEvent& event)
{
    m_isActive = event.GetActive();
    RefreshTitleBar();

    // wx's own: the focus given back to the child that had it
    event.Skip();
}

// The resize cursors are the border's own, set over it alone: a window's cursor (SetCursor) is its children's too - the
// form card gives its children the arrow back.
template <class W>
void ibTopLevelWindow<W>::OnSetCursor(wxSetCursorEvent& event)
{
    wxCursor cursor;
    if ( m_grip == 0 && this->HasFlag(wxRESIZE_BORDER) )
    {
        const wxPoint pt = wxGetMousePosition() - this->GetScreenPosition() - GetClientAreaOrigin();
        if ( GetResizingCursor(HitTest(pt), cursor) )
        {
            event.SetCursor(cursor);
            return;
        }
    }

    event.Skip();
}

template <class W>
void ibTopLevelWindow<W>::OnMouseMotion(wxMouseEvent& event)
{
    if ( m_grip == 0 )
    {
        event.Skip();
        return;
    }

    // the button held is pressed while the mouse is over it
    if ( m_grip & wxHT_TOPLEVEL_ANY_BUTTON )
    {
        const long pressed = HitTest(GetClientPoint(event)) == m_grip ? m_grip : 0;
        if ( pressed != m_pressedButton )
        {
            m_pressedButton = pressed;
            RefreshTitleBar();
        }
        return;
    }

#ifdef __WXGTK3__
    // SEAM vs the grip below: under Wayland a window cannot place itself - Move, and the place SetSize is given, do
    // nothing there. The compositor moves and sizes it, asked as GTK asks for its own title bar (GtkWindow, once the
    // pointer held moves - a double click still maximizes) and as wx's mini frame does (gtk_window_begin_move_drag).
    // X11 and Cocoa place it themselves, below; MSW's system does it from the hit test (ibMSWDecorationsProc)
    if ( wxGTKImpl::IsWayland(gtk_widget_get_window(this->m_widget)) )
    {
        const long grip = m_grip;
        m_grip = 0;
        wxEndBusyCursor();
        this->ReleaseMouse();

        GdkEvent* const motion = gtk_get_current_event();
        if ( motion == nullptr )
            return;

        gdouble x = 0, y = 0;
        gdk_event_get_root_coords(motion, &x, &y);
        GtkWindow* const window = GTK_WINDOW(this->m_widget);
        if ( grip == wxHT_TOPLEVEL_TITLEBAR )
        {
            gtk_window_begin_move_drag(window, GDK_BUTTON_PRIMARY, int(x), int(y), gdk_event_get_time(motion));
        }
        else
        {
            GdkWindowEdge edge = GDK_WINDOW_EDGE_SOUTH_EAST;
            switch ( grip )
            {
                case wxHT_TOPLEVEL_BORDER_N:  edge = GDK_WINDOW_EDGE_NORTH;      break;
                case wxHT_TOPLEVEL_BORDER_S:  edge = GDK_WINDOW_EDGE_SOUTH;      break;
                case wxHT_TOPLEVEL_BORDER_E:  edge = GDK_WINDOW_EDGE_EAST;       break;
                case wxHT_TOPLEVEL_BORDER_W:  edge = GDK_WINDOW_EDGE_WEST;       break;
                case wxHT_TOPLEVEL_BORDER_NE: edge = GDK_WINDOW_EDGE_NORTH_EAST; break;
                case wxHT_TOPLEVEL_BORDER_NW: edge = GDK_WINDOW_EDGE_NORTH_WEST; break;
                case wxHT_TOPLEVEL_BORDER_SW: edge = GDK_WINDOW_EDGE_SOUTH_WEST; break;
            }
            gtk_window_begin_resize_drag(window, edge, GDK_BUTTON_PRIMARY, int(x), int(y), gdk_event_get_time(motion));
        }
        gdk_event_free(motion);
        return;
    }
#endif // __WXGTK3__

    // the window follows the pointer: held by the title bar, all of it; by the border, the edges held move and the
    // others stay - never smaller than its minimum
    const wxPoint diff = wxGetMousePosition() - m_gripPoint;
    if ( m_grip == wxHT_TOPLEVEL_TITLEBAR )
    {
        this->Move(m_gripRect.GetPosition() + diff);
        return;
    }

    const wxSize min = GetMinSize();
    wxRect rect = m_gripRect;
    if ( m_grip & wxHT_TOPLEVEL_BORDER_W )
    {
        rect.width = std::max(m_gripRect.width - diff.x, min.x);
        rect.x = m_gripRect.GetRight() + 1 - rect.width;
    }
    else if ( m_grip & wxHT_TOPLEVEL_BORDER_E )
        rect.width = std::max(m_gripRect.width + diff.x, min.x);
    if ( m_grip & wxHT_TOPLEVEL_BORDER_N )
    {
        rect.height = std::max(m_gripRect.height - diff.y, min.y);
        rect.y = m_gripRect.GetBottom() + 1 - rect.height;
    }
    else if ( m_grip & wxHT_TOPLEVEL_BORDER_S )
        rect.height = std::max(m_gripRect.height + diff.y, min.y);
    this->SetSize(rect);
}

template <class W>
void ibTopLevelWindow<W>::OnLeftDown(wxMouseEvent& event)
{
    // a button is pressed, the title bar moves the window and the border sizes it
    const long hit = HitTest(GetClientPoint(event));
    const bool button = (hit & wxHT_TOPLEVEL_ANY_BUTTON) != 0;
    const bool move = hit == wxHT_TOPLEVEL_TITLEBAR && !this->IsMaximized();
    const bool size = (hit & wxHT_TOPLEVEL_ANY_BORDER) != 0 && this->HasFlag(wxRESIZE_BORDER);
    if ( !button && !move && !size )
    {
        event.Skip();
        return;
    }

    if ( button )
    {
        m_pressedButton = hit;
        RefreshTitleBar();
    }
    m_grip = hit;
    m_gripRect = this->GetRect();
    m_gripPoint = wxGetMousePosition();
    this->CaptureMouse();

    // …and the pointer shows what it does while it holds, as the canon's (wxTopLevelWindow::InteractiveMove): the
    // move's cursor, the border's
    if ( !button )
    {
        wxCursor cursor(wxCURSOR_SIZING);
        GetResizingCursor(hit, cursor);
        wxBeginBusyCursor(&cursor);
    }
}

template <class W>
void ibTopLevelWindow<W>::OnLeftUp(wxMouseEvent& event)
{
    if ( m_grip == 0 )
    {
        event.Skip();
        return;
    }

    // a button is clicked let go over it - the last thing here: closing, the window may be gone
    const long clicked = m_pressedButton;
    if ( !(m_grip & wxHT_TOPLEVEL_ANY_BUTTON) )
        wxEndBusyCursor();
    m_grip = 0;
    m_pressedButton = 0;
    this->ReleaseMouse();
    if ( clicked != 0 )
    {
        RefreshTitleBar();
        ClickTitleBarButton(clicked);
    }
}

// The title bar double-clicked is its maximize or restore button clicked, as the canon's (wxWin32FrameInputHandler) -
// for a window that has the button: the canon's maximized any.
template <class W>
void ibTopLevelWindow<W>::OnLeftDClick(wxMouseEvent& event)
{
    const long button = GetDecorationsStyle() & (wxTOPLEVEL_BUTTON_MAXIMIZE | wxTOPLEVEL_BUTTON_RESTORE);
    if ( button == 0 || HitTest(GetClientPoint(event)) != wxHT_TOPLEVEL_TITLEBAR )
    {
        event.Skip();
        return;
    }

    ClickTitleBarButton(button);
}

template <class W>
void ibTopLevelWindow<W>::OnMouseCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    if ( m_pressedButton != 0 )
        RefreshTitleBar();
    if ( m_grip != 0 && !(m_grip & wxHT_TOPLEVEL_ANY_BUTTON) )
        wxEndBusyCursor();
    m_grip = 0;
    m_pressedButton = 0;
}

template <class W>
bool ibTopLevelWindow<W>::GetResizingCursor(long hit, wxCursor& cursor)
{
    switch ( hit )
    {
        case wxHT_TOPLEVEL_BORDER_N:
        case wxHT_TOPLEVEL_BORDER_S:
            cursor = wxCursor(wxCURSOR_SIZENS);
            return true;
        case wxHT_TOPLEVEL_BORDER_W:
        case wxHT_TOPLEVEL_BORDER_E:
            cursor = wxCursor(wxCURSOR_SIZEWE);
            return true;
        case wxHT_TOPLEVEL_BORDER_NE:
        case wxHT_TOPLEVEL_BORDER_SW:
            cursor = wxCursor(wxCURSOR_SIZENESW);
            return true;
        case wxHT_TOPLEVEL_BORDER_NW:
        case wxHT_TOPLEVEL_BORDER_SE:
            cursor = wxCursor(wxCURSOR_SIZENWSE);
            return true;
    }
    return false;
}

// the two windows it is made for, made here once (topLevel.h)
template class DESKGUI_API ibTopLevelWindow<wxFrame>;
template class DESKGUI_API ibTopLevelWindow<wxDialog>;
