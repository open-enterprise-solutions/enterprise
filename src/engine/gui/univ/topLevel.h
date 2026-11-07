// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically): the top-level window drawing its
// own decorations (src/univ/topluniv.cpp), over a native window of wx's.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/toplevel.h
// Purpose:     Top level window, abstraction of ibFrame and ibDialog
// Author:      Vaclav Slavik
// Copyright:   (c) 2001-2002 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////


#ifndef __WX_UNIV_TOPLEVEL_H__
#define __WX_UNIV_TOPLEVEL_H__

#include "gui/univ/renderer.h"
#include "gui/univ/theme.h"

#include <algorithm>
#include <vector>

#include <wx/toplevel.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/cshelp.h>
#include <wx/utils.h>

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// frame decorations type flags used in ibRenderer and ibColourScheme
enum
{
    wxTOPLEVEL_ACTIVE          = 0x00000001,
    wxTOPLEVEL_MAXIMIZED       = 0x00000002,
    wxTOPLEVEL_TITLEBAR        = 0x00000004,
    wxTOPLEVEL_ICON            = 0x00000008,
    wxTOPLEVEL_RESIZEABLE      = 0x00000010,
    wxTOPLEVEL_BORDER          = 0x00000020,
    wxTOPLEVEL_BUTTON_CLOSE    = 0x01000000,
    wxTOPLEVEL_BUTTON_MAXIMIZE = 0x02000000,
    wxTOPLEVEL_BUTTON_ICONIZE =  0x04000000,
    wxTOPLEVEL_BUTTON_RESTORE  = 0x08000000,
    wxTOPLEVEL_BUTTON_HELP     = 0x10000000
};

// frame hit test return values:
enum
{
    wxHT_TOPLEVEL_NOWHERE         = 0x00000000,
    wxHT_TOPLEVEL_CLIENT_AREA     = 0x00000001,
    wxHT_TOPLEVEL_ICON            = 0x00000002,
    wxHT_TOPLEVEL_TITLEBAR        = 0x00000004,

    wxHT_TOPLEVEL_BORDER_N        = 0x00000010,
    wxHT_TOPLEVEL_BORDER_S        = 0x00000020,
    wxHT_TOPLEVEL_BORDER_E        = 0x00000040,
    wxHT_TOPLEVEL_BORDER_W        = 0x00000080,
    wxHT_TOPLEVEL_BORDER_NE       = wxHT_TOPLEVEL_BORDER_N | wxHT_TOPLEVEL_BORDER_E,
    wxHT_TOPLEVEL_BORDER_SE       = wxHT_TOPLEVEL_BORDER_S | wxHT_TOPLEVEL_BORDER_E,
    wxHT_TOPLEVEL_BORDER_NW       = wxHT_TOPLEVEL_BORDER_N | wxHT_TOPLEVEL_BORDER_W,
    wxHT_TOPLEVEL_BORDER_SW       = wxHT_TOPLEVEL_BORDER_S | wxHT_TOPLEVEL_BORDER_W,
    wxHT_TOPLEVEL_ANY_BORDER      = 0x000000F0,

    wxHT_TOPLEVEL_BUTTON_CLOSE    = /*0x01000000*/ wxTOPLEVEL_BUTTON_CLOSE,
    wxHT_TOPLEVEL_BUTTON_MAXIMIZE = /*0x02000000*/ wxTOPLEVEL_BUTTON_MAXIMIZE,
    wxHT_TOPLEVEL_BUTTON_ICONIZE =  /*0x04000000*/ wxTOPLEVEL_BUTTON_ICONIZE,
    wxHT_TOPLEVEL_BUTTON_RESTORE  = /*0x08000000*/ wxTOPLEVEL_BUTTON_RESTORE,
    wxHT_TOPLEVEL_BUTTON_HELP     = /*0x10000000*/ wxTOPLEVEL_BUTTON_HELP,
    wxHT_TOPLEVEL_ANY_BUTTON      =   0x1F000000
};

//-----------------------------------------------------------------------------
// ibTopLevelWindow
//-----------------------------------------------------------------------------

// SEAM vs univ: THE WINDOW IS wx's OWN, ITS DECORATIONS OURS - as the designer's form card has them (ibInnerFrame): the
// native window is made without its caption and frame, the wx style keeps both for what is painted in their place
// (GetDecorationsStyle) - the theme's title bar, its buttons and the border (ibRenderer::DrawFrameTitleBar) round the
// client area (GetClientAreaOrigin, DoGetClientSize). The mouse, captured, moves the window held by the title bar and
// sizes it held by the border. Not kept: native decorations on demand, the full screen, the system menu and the
// keyboard's move, the title bar's icon, the input handler and its actions.
template <class W>
class ibTopLevelWindow : public W
{
public:
    ibTopLevelWindow();

    // W's, its native window without the decorations, painted whole again as it is sized; with no place given, over
    // the middle of its parent
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos,
                const wxSize& size,
                long style,
                const wxString& name);

    // the part of the window at the point of the client area: wxHT_TOPLEVEL_xxx
    long HitTest(const wxPoint& pt) const;

    virtual wxPoint GetClientAreaOrigin() const override;
    virtual wxSize GetMinSize() const override;
    virtual void SetTitle(const wxString& title) override;

    // …and at every restyle: wxNavigationEnabled toggles wxTAB_TRAVERSAL at the first child, and the native caption
    // came back with it (ibWindow::MSWGetStyle); wxDialog makes its native size grip for wxRESIZE_BORDER and puts it
    // by the client size in the window's corner - inside ours, above the frame
    virtual void SetWindowStyleFlag(long style) override;

protected:
    virtual void DoGetClientSize(int *width, int *height) const override;
    virtual void DoSetClientSize(int width, int height) override;

private:
    // wxTOPLEVEL_xxx of the window's style and state
    long GetDecorationsStyle() const;

    // the strips round the client area painted again - not the children inside it
    void RefreshTitleBar();

    void ClickTitleBarButton(long button);

    void OnPaint(wxPaintEvent& event);
    void OnActivateTitleBar(wxActivateEvent& event);
    void OnSetCursor(wxSetCursorEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

    // the native window's style: the decorations taken away - they are painted - the window painted whole, the
    // decorations with it, its children clipped
    static long GetNativeStyle(long style)
    {
        return (style & ~s_decorations) | wxBORDER_NONE | wxCLIP_CHILDREN | wxFULL_REPAINT_ON_RESIZE;
    }

    // the canon's (wxGetResizingCursor): the cursor of the border at the part hit - none for another part
    static bool GetResizingCursor(long hit, wxCursor& cursor);

    // the decorations: in the wx style, not in the native window's
    static constexpr long s_decorations = wxCAPTION | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX | wxSYSTEM_MENU |
                                          wxRESIZE_BORDER | wxBORDER_MASK;

    ibRenderer *m_renderer;
    bool m_isActive = false;
    long m_grip = 0;            // what the mouse holds: the title bar, the border or a button (wxHT_TOPLEVEL_xxx)
    long m_pressedButton = 0;   // the button held, while the mouse is over it
    wxRect m_gripRect;          // the window as it was grabbed, on the screen
    wxPoint m_gripPoint;        // the pointer as it grabbed, on the screen
};

template <class W>
ibTopLevelWindow<W>::ibTopLevelWindow()
    : m_renderer(ibThemeEngine::Get()->GetRenderer())
{
    // the decorations are painted with the client area, all of it at once: nothing erased under the paint
    this->SetBackgroundStyle(wxBG_STYLE_PAINT);

    this->Bind(wxEVT_PAINT, &ibTopLevelWindow::OnPaint, this);
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

    this->m_windowStyle = (this->m_windowStyle & ~s_decorations) | (style & s_decorations);

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
    return m_renderer->HitTestFrame(wxRect(this->GetSize()), pt + GetClientAreaOrigin(), GetDecorationsStyle());
}

template <class W>
wxPoint ibTopLevelWindow<W>::GetClientAreaOrigin() const
{
    return m_renderer->GetFrameClientArea(wxRect(this->GetSize()), GetDecorationsStyle()).GetPosition();
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
void ibTopLevelWindow<W>::SetWindowStyleFlag(long style)
{
    const long decorationsOld = this->m_windowStyle & s_decorations;

    W::SetWindowStyleFlag(GetNativeStyle(style));
    this->m_windowStyle = (this->m_windowStyle & ~s_decorations) | (style & s_decorations);

    // other decorations, another client area
    if ( (style & s_decorations) != decorationsOld )
    {
        this->SendSizeEvent();
        this->Refresh();
    }
}

template <class W>
void ibTopLevelWindow<W>::DoGetClientSize(int *width, int *height) const
{
    const wxRect client = m_renderer->GetFrameClientArea(wxRect(this->GetSize()), GetDecorationsStyle());
    if ( width )
        *width = client.width;
    if ( height )
        *height = client.height;
}

template <class W>
void ibTopLevelWindow<W>::DoSetClientSize(int width, int height)
{
    const wxSize size = m_renderer->GetFrameTotalSize(wxSize(width, height), GetDecorationsStyle());
    W::DoSetClientSize(size.x, size.y);
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
    if ( m_isActive )
        style |= wxTOPLEVEL_ACTIVE;

    return style;
}

template <class W>
void ibTopLevelWindow<W>::RefreshTitleBar()
{
    const wxSize size = this->GetSize();
    const wxRect client(GetClientAreaOrigin(), this->GetClientSize());

    this->RefreshRect(wxRect(0, 0, size.x, client.y), false);
    this->RefreshRect(wxRect(0, client.y, client.x, size.y - client.y), false);
    this->RefreshRect(wxRect(client.GetRight() + 1, client.y, size.x - client.GetRight() - 1, size.y - client.y), false);
    this->RefreshRect(wxRect(client.x, client.GetBottom() + 1, client.width, size.y - client.GetBottom() - 1), false);
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
void ibTopLevelWindow<W>::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dcPaint(this);

    // the paint DC begins at the client area (GetClientAreaOrigin), the decorations are round it: the whole window.
    // Not wxBufferedDC: it blits no more than its DC's size, the client size (wxClientDCImpl::DoGetSize) - the frame's
    // bottom and right were left unpainted, what was behind the window showing there
    dcPaint.SetDeviceOrigin(0, 0);
    const wxSize size = this->GetSize();
    wxBitmap buffer(size);
    wxMemoryDC dc(buffer);

    dc.SetBackground(wxBrush(this->GetBackgroundColour()));
    dc.Clear();
    m_renderer->DrawFrameTitleBar(dc, wxRect(size), this->GetTitle(), wxNullIcon,
                                  GetDecorationsStyle(), m_pressedButton, wxCONTROL_PRESSED);

    dcPaint.Blit(0, 0, size.x, size.y, &dc, 0, 0);
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
        const long pressed = HitTest(event.GetPosition()) == m_grip ? m_grip : 0;
        if ( pressed != m_pressedButton )
        {
            m_pressedButton = pressed;
            RefreshTitleBar();
        }
        return;
    }

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

    // …and painted at once, the window and every window in it (Update is the one window's): a queue gives the paint
    // after the input, the pointer moved on before it, and the window followed the pointer unpainted
    std::vector<wxWindow*> windows{ this };
    while ( !windows.empty() )
    {
        wxWindow * const win = windows.back();
        windows.pop_back();
        win->Update();
        for ( wxWindow *child : win->GetChildren() )
            windows.push_back(child);
    }
}

template <class W>
void ibTopLevelWindow<W>::OnLeftDown(wxMouseEvent& event)
{
    // a button is pressed, the title bar moves the window and the border sizes it
    const long hit = HitTest(event.GetPosition());
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
    if ( button == 0 || HitTest(event.GetPosition()) != wxHT_TOPLEVEL_TITLEBAR )
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

#endif // __WX_UNIV_TOPLEVEL_H__
