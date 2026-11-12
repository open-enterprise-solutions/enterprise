// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/winuniv.cpp
// Purpose:     implementation of extra ibWindow methods for wxUniv port
// Author:      Vadim Zeitlin
// Created:     06.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/window.h"
#include "deskgui/univ/toolTip.h"   // the tip of a window: the theme's tooltip shows it

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/scrollBar.h"


#include <wx/window.h>

#ifndef WX_PRECOMP
    #include <wx/app.h>
    #include <wx/dcclient.h>
    #include <wx/dcmemory.h>
    #include <wx/event.h>
    #include <wx/scrolbar.h>
    #include <wx/menu.h>
    #include <wx/frame.h>
    #include <wx/log.h>
    #include <wx/button.h>
#endif // WX_PRECOMP

#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"


#if wxUSE_CARET
    #include <wx/caret.h>
#endif // wxUSE_CARET

// ============================================================================
// implementation
// ============================================================================

#if wxUSE_SCROLLBAR
// ----------------------------------------------------------------------------
// scrollbars class
// ----------------------------------------------------------------------------

// This is scrollbar class used to implement ibWindow's "built-in" scrollbars;
// unlike the standard ibScrollBar class, this one is positioned outside of its
// parent's client area
class ibWindowScrollBar : public ibScrollBar
{
public:
    ibWindowScrollBar(wxWindow *parent,
                      wxWindowID id,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize,
                      long style = wxSB_HORIZONTAL)
        : ibScrollBar(parent, id, pos, size, style)
    {
    }

    virtual bool CanBeOutsideClientArea() const { return true; }
};
#endif // wxUSE_SCROLLBAR


// ----------------------------------------------------------------------------
// event tables
// ----------------------------------------------------------------------------

// we can't use wxWindow here as it won't be expanded inside the macro
#if defined(__WXMSW__)
    wxIMPLEMENT_DYNAMIC_CLASS(ibWindow, wxWindowMSW);
#elif defined(__WXGTK__)
    wxIMPLEMENT_DYNAMIC_CLASS(ibWindow, wxWindowGTK);
#elif defined(__WXOSX__)
    wxIMPLEMENT_DYNAMIC_CLASS(ibWindow, wxWindowMac);
#elif defined(__WXDFB__)
    wxIMPLEMENT_DYNAMIC_CLASS(ibWindow, ibWindowDFB);
#elif defined(__WXX11__)
    wxIMPLEMENT_DYNAMIC_CLASS(ibWindow, wxWindowX11);
#endif

wxBEGIN_EVENT_TABLE(ibWindow, wxWindow)
    EVT_PAINT(ibWindow::OnPaint)
    EVT_MOUSE_CAPTURE_LOST(ibWindow::OnMouseCaptureLost)
    EVT_ERASE_BACKGROUND(ibWindow::OnErase)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

void ibWindow::Init()
{
#if wxUSE_SCROLLBAR
    m_scrollbarVert =
    m_scrollbarHorz = nullptr;
#endif // wxUSE_SCROLLBAR

    m_isCurrent = false;

    m_renderer = ibThemeEngine::Get()->GetRenderer();

    // SEAM vs univ: the canon erased the background in a separate native
    // erase pass (OnErase) and painted the control in OnPaint — two passes
    // flicker. With wxBG_STYLE_PAINT the erase pass is gone and OnPaint
    // draws everything through one back buffer.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

bool ibWindow::Create(wxWindow *parent,
                      wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      long style,
                      const wxString& name)
{
    // Get default border
    wxBorder border = GetBorder(style);
    style &= ~wxBORDER_MASK;
    style |= border;

    long actualStyle = style;

    // we add wxCLIP_CHILDREN to get the same ("natural") behaviour under MSW
    // as under the other platforms
    actualStyle |= wxCLIP_CHILDREN;

    actualStyle &= ~wxVSCROLL;
    actualStyle &= ~wxHSCROLL;

    // SEAM vs univ: not painted whole as it is sized - the canon's wxFULL_REPAINT_ON_RESIZE for every window, its FIXME's
    // stopgap ("very expensive, we need to fix the code in OnSize() instead"), is gone: what the size moved of the frame
    // is painted again (OnSize), what it uncovered the system makes dirty. With it every window of a resized window's
    // tree was painted whole at every step - a container's native class redraws its children whole as it is sized
    // (CS_HREDRAW | CS_VREDRAW): the configuration tree 6.5 ms of a step (2026-11-12, measured). A window whose picture
    // follows its size asks for it itself (ibStatusBar, ibStaticBox). On GTK3 the flag decides it the same way (wxPizza,
    // redraw on allocate); wxOSX reads no flag - a Cocoa view sized is drawn whole, the bands OnSize refreshes with it

#ifdef __WXGTK__
    // SEAM vs wxGTK: the border is ours here too - the native window made without one, as MSWGetStyle and
    // MacGetWXBorderSize keep it off the others: wxGTK insets its native area by a sunken, raised, simple or theme border
    // and draws the platform's frame round it (wxPizza::get_border, draw_border), ours a second one inside. And made at
    // its client area's place in its parent (DoSetSize): wxGTK puts the widget at the place it is made with
    const long nativeStyle = (actualStyle & ~wxBORDER_MASK) | wxBORDER_NONE;
    wxPoint posNative(pos.x == wxDefaultCoord ? 0 : pos.x, pos.y == wxDefaultCoord ? 0 : pos.y);
    if ( parent )
        posNative += parent->GetClientAreaOrigin();

    if ( !wxWindow::Create(parent, id, posNative, size, nativeStyle, name) )
        return false;
#else
    if ( !wxWindow::Create(parent, id, pos, size, actualStyle, name) )
        return false;
#endif // __WXGTK__

    // Set full style again, including those we didn't want present
    // when calling the base window Create().
    wxWindowBase::SetWindowStyleFlag(style);

    // if we allow or should always have a vertical scrollbar, make it
    if ( style & wxVSCROLL || style & wxALWAYS_SHOW_SB )
    {
#if wxUSE_TWO_WINDOWS
        SetInsertIntoMain( true );
#endif
#if wxUSE_SCROLLBAR
        m_scrollbarVert = new ibWindowScrollBar(this, wxID_ANY,
                                                wxDefaultPosition, wxDefaultSize,
                                                wxSB_VERTICAL);
#endif // wxUSE_SCROLLBAR
#if wxUSE_TWO_WINDOWS
        SetInsertIntoMain( false );
#endif
    }

    // if we should allow a horizontal scrollbar, make it
    if ( style & wxHSCROLL )
    {
#if wxUSE_TWO_WINDOWS
        SetInsertIntoMain( true );
#endif
#if wxUSE_SCROLLBAR
        m_scrollbarHorz = new ibWindowScrollBar(this, wxID_ANY,
                                                wxDefaultPosition, wxDefaultSize,
                                                wxSB_HORIZONTAL);
#endif // wxUSE_SCROLLBAR
#if wxUSE_TWO_WINDOWS
        SetInsertIntoMain( false );
#endif
    }

#if wxUSE_SCROLLBAR
    if (m_scrollbarHorz || m_scrollbarVert)
    {
        // position it/them
        PositionScrollbars();
    }
#endif // wxUSE_SCROLLBAR

    return true;
}

ibWindow::~ibWindow()
{
    SendDestroyEvent();

#if wxUSE_SCROLLBAR
    // clear pointers to scrollbar before deleting the children: they are
    // children and so will be deleted by DestroyChildren() call below and if
    // any code using the scrollbars would be called in the process or from
    // ~wxWindowBase, the app would crash:
    m_scrollbarVert = m_scrollbarHorz = nullptr;
#endif

    // we have to destroy our children before we're destroyed because our
    // children suppose that we're of type ibWindow, not just wxWindow,
    // and so bad things may happen if they're deleted from the base class dtor
    // as by then we're not a ibWindow any longer and wxUniv-specific virtual
    // functions can't be called
    DestroyChildren();
}

// ----------------------------------------------------------------------------
// background pixmap
// ----------------------------------------------------------------------------

void ibWindow::SetBackground(const wxBitmap& bitmap,
                              int alignment,
                              wxStretch stretch)
{
    m_bitmapBg = bitmap;
    m_alignBgBitmap = alignment;
    m_stretchBgBitmap = stretch;
}

const wxBitmap& ibWindow::GetBackgroundBitmap(int *alignment,
                                               wxStretch *stretch) const
{
    if ( m_bitmapBg.IsOk() )
    {
        if ( alignment )
            *alignment = m_alignBgBitmap;
        if ( stretch )
            *stretch = m_stretchBgBitmap;
    }

    return m_bitmapBg;
}

// ----------------------------------------------------------------------------
// painting
// ----------------------------------------------------------------------------

// SEAM vs univ: THE NON-CLIENT PART IS PAINTED IN THE WINDOW'S PAINT, on every platform - the border and the square in
// the corner between the two scrollbars here, a control's title (ibControl::TryBefore) - before the window's own handler
// paints its client area. wxEVT_NC_PAINT is wxMSW's alone: wxGTK raises it before the paint with a DC drawing outside
// the paint (nothing on Wayland), wxOSX never for the window itself. The paint DC is the one DC that draws in the paint
// on all three: wxMSW's begins at the client area, wxOSX's is clipped to it too (wxPaintDCImpl), wxGTK's begins at the
// native area's corner (ibThemePaintDC moves it) - the window is ours whole.
// The canon erased the corner (OnErase); with no erase pass (wxBG_STYLE_PAINT) a window painting itself
// (ibTreeCtrl::OnPaint) left it black. The border follows the focus (the theme's accent): the window gaining or losing
// it is drawn anew, whatever handles the focus after.
bool ibWindow::TryBefore(wxEvent& event)
{
    const wxEventType type = event.GetEventType();

#ifdef __WXOSX__
    // SEAM vs wxOSX: a mouse event's place is the client area's, as wxMSW gives it (wxWindowMSW::InitMouseEvent) and
    // GTKProcessEvent makes it on GTK - wxOSX's is the native view's (wxSetupCoordinates), the whole window's with our
    // frame and a control's title in it: a click landed their width right and down of what it hit, and wxOSX has no door
    // of a window to take it at (wxWidgetCocoaImpl::DoHandleMouseEvent calls HandleWindowEvent, not virtual). So the
    // event ends here, and the same event goes again from the client area, as wxOSX sends it (2026-11-12, Max). Once for
    // an occurrence of the mouse: a copy goes through as it comes - this one, and one a window sends on (ibListMainWindow
    // to its list, ibStyledTextCtrl's wheel) carry its time and kind and a place of the client area - as does one made
    // in code (no time). The top-level window's is its own (ibTopLevelWindow::GetClientPoint)
    if ( wxMouseEvent* const mouse = wxDynamicCast(&event, wxMouseEvent) )
    {
        static long s_time = 0;
        static wxEventType s_type = wxEVT_NULL;
        if ( mouse->GetTimestamp() != 0 && (mouse->GetTimestamp() != s_time || type != s_type) )
        {
            s_time = mouse->GetTimestamp();
            s_type = type;

            const wxPoint origin = GetClientAreaOrigin();
            if ( origin != wxPoint(0, 0) )
            {
                wxMouseEvent client(*mouse);
                client.SetPosition(mouse->GetPosition() - origin);
                event.Skip(!HandleWindowEvent(client));
                return true;
            }
        }
    }
#endif // __WXOSX__

    // THE MOUSE OVER A WINDOW WITH A TIP goes to the theme's tooltip, before the window's own handler: a move tracks the
    // tip, a press, a wheel, a key or leaving the window ends it
    if ( GetToolTip() != nullptr )
    {
        if ( type == wxEVT_MOTION )
        {
            if ( ibToolTipWindow *const tip = ibToolTipWindow::Get() )
                tip->Track(this, wxGetMousePosition());
        }
        else if ( type == wxEVT_LEAVE_WINDOW || type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN
               || type == wxEVT_MIDDLE_DOWN || type == wxEVT_MOUSEWHEEL || type == wxEVT_KEY_DOWN )
        {
            if ( ibToolTipWindow *const tip = ibToolTipWindow::Get() )
                tip->Dismiss();
        }
    }

    if ( type == wxEVT_SET_FOCUS || type == wxEVT_KILL_FOCUS )
    {
        // the window whose frame shows the focus - a composite control's, for the window inside it (IsFocused)
        GetMainWindowOfCompositeControl()->Refresh();
    }
    else if ( type == wxEVT_SIZE && event.GetEventObject() == this )
    {
        // SEAM vs univ: THE NON-CLIENT PART IS LAID OUT BEFORE THE WINDOW'S OWN SIZE HANDLER, as it is painted before its
        // paint (below): the scrollbars along the new edges - the client size a handler reads is measured from where they
        // stand (DoGetClientSize) - and the frame the size moved painted again. The canon's size handler ran after a
        // derived one, which read the old client size, and not at all under one that took the event: the property grid's
        // (ibPropertyGrid::OnResize, the canon's own) left its scrollbar at the length it was made with, the down arrow
        // cut off below the window and the frame's bottom line broken at it (2026-11-12, Max)
        OnSize(static_cast<wxSizeEvent&>(event));
    }
    else if ( type == wxEVT_PAINT && m_renderer )
    {
        wxPaintDC dc(this);
        dc.SetDeviceOrigin(0, 0);
        if ( wxGraphicsContext* const gc = dc.GetGraphicsContext() )
            gc->ResetClip();

        // the frame's rect - the window's, unless a control puts something beside its frame (GetBorderRect)
        wxRect rect = GetBorderRect();

#if wxUSE_SCROLLBAR
        // the square between the scrollbars, inside the frame's line: the frame's rect, not the window's - a title
        // beside the frame is not under the scrollbars; the canon measured it from 0 and over the line
        if ( m_scrollbarVert && m_scrollbarHorz )
        {
            wxRect rectClient = GetClientRect(),
                   rectBorder = m_renderer->GetBorderDimensions(GetBorder());

            wxRect rectCorner;
            rectCorner.x = rectClient.GetRight() + 1;
            rectCorner.y = rectClient.GetBottom() + 1;
            rectCorner.SetRight(rect.GetRight() - rectBorder.width);
            rectCorner.SetBottom(rect.GetBottom() - rectBorder.height);

            m_renderer->DrawScrollCorner(dc, rectCorner);
        }

        // if the scrollbars are outside the border, we must adjust the rect to
        // exclude them
        if ( !m_renderer->AreScrollbarsInsideBorder() )
        {
            ibScrollBar *scrollbar = GetScrollbar(wxVERTICAL);
            if ( scrollbar )
                rect.width -= scrollbar->GetSize().x;

            scrollbar = GetScrollbar(wxHORIZONTAL);
            if ( scrollbar )
                rect.height -= scrollbar->GetSize().y;
        }
#endif // wxUSE_SCROLLBAR

        DrawBorder(dc, rect);
        m_paintedSize = GetSize();
    }

    return wxWindow::TryBefore(event);
}

void ibWindow::DoSetToolTip(wxToolTip *tip)
{
    wxWindowBase::DoSetToolTip(tip);

    // set or taken away under the mouse - the code editor's tip of the word the pointer is on - it shows, or goes, now
    if ( ibToolTipWindow *const tipWindow = ibToolTipWindow::Get() )
        tipWindow->Track(this, wxGetMousePosition());
}

void ibWindow::DoSetToolTipText(const wxString& tip)
{
    // wxWindowBase's: the tip there given its new text, or a new one made through DoSetToolTip
    wxWindowBase::DoSetToolTipText(tip);

    if ( ibToolTipWindow *const tipWindow = ibToolTipWindow::Get() )
        tipWindow->Track(this, wxGetMousePosition());
}

void ibWindow::DrawBorder(wxDC& dc, const wxRect& rect)
{
    m_renderer->DrawBorder(dc, GetBorder(), rect, GetStateFlags());
}

// SEAM vs univ: the paint of a window drawing nothing of its own - a panel, a frame: its background. A control paints
// by its own event (its OnPaint on an ibThemePaintDC, the background already in it), its frame drawn before it
// (TryBefore).
// The canon's drawing doors (DoDrawBackground, DoDraw over an ibControlRenderer) are gone.
void ibWindow::OnPaint(wxPaintEvent& event)
{
    if ( !m_renderer )
    {
        // it is a native control which paints itself
        event.Skip();
        return;
    }

    // one buffered pass (the erase pass is disabled by wxBG_STYLE_PAINT in Init()); the buffer covers the client area
    // only (GetClientSize: without the scrollbars), the corner between them is the border's (TryBefore)
    // SEAM vs univ: wxBufferedDC over the paint DC, as wxBufferedPaintDC builds it - not wxAutoBufferedPaintDC, which
    // asserts wxBG_STYLE_PAINT, and a window taking the erase back (wxBG_STYLE_ERASE) with no paint of its own paints
    // here too
    wxPaintDC dcPaint(this);
#ifdef __WXGTK__
    // from the client area's corner and inside it, as ibThemePaintDC's - wxGTK's begins at the native area's, our frame's
    const wxPoint origin = GetClientAreaOrigin();
    dcPaint.SetDeviceOrigin(origin.x, origin.y);
    dcPaint.SetClippingRegion(wxPoint(0, 0), GetClientSize());
#endif // __WXGTK__
#if wxALWAYS_NATIVE_DOUBLE_BUFFER
    wxDC& dc = dcPaint;
#else
    wxBufferedDC dc(&dcPaint, GetClientSize());
    // the window's font and colours, as wxBufferedPaintDC takes them
    dc.GetImpl()->InheritAttributes(this);
#endif

    EraseBackground(dc, wxRect(GetSize()));
}

// the event handler executed when the window background must be painted
// SEAM vs univ: modern wx asserts if a capturing window has no handler for
// wxEVT_MOUSE_CAPTURE_LOST; the std input handlers re-arm on the next click
void ibWindow::OnMouseCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    WXMakeCurrent(false);
}

void ibWindow::OnErase(wxEraseEvent& event)
{
    if ( !m_renderer )
    {
        event.Skip();

        return;
    }

    // the corner between the scrollbars is the border's (TryBefore)
    EraseBackground(*event.GetDC(), wxRect(GetSize()));
}

void ibWindow::EraseBackground(wxDC& dc, const wxRect& rect)
{
    // SEAM vs univ: a transparent window's background is its parent's, drawn under it here (the canon's
    // DoDrawBackground, the hook gone with the drawing doors); the parent may be a native host window (frame/dialog)
    // which has no univ EraseBackground — fall back to drawing ourselves then
    ibWindow * const parent = dynamic_cast<ibWindow*>(GetParent());
    if ( HasTransparentBackground() && !UseBgCol() && parent )
    {
        // DirectFB paints the parent first, then its child windows, so by
        // the time this code is called, parent's background was already
        // drawn and there's no point in (imperfectly!) duplicating the work
        // here:
#ifndef __WXDFB__
        wxASSERT( !IsTopLevel() );

        wxPoint pos = GetPosition();

        AdjustForParentClientOrigin( pos.x, pos.y, 0 );

        // Adjust DC logical origin
        wxCoord org_x, org_y, x, y;
        dc.GetLogicalOrigin( &org_x, &org_y );
        x = org_x + pos.x;
        y = org_y + pos.y;
        dc.SetLogicalOrigin( x, y );

        // Adjust draw rect
        wxRect rectParent = rect;
        rectParent.Offset(pos);

        // Let parent draw the background
        parent->EraseBackground( dc, rectParent );

        // Restore DC logical origin
        dc.SetLogicalOrigin( org_x, org_y );
#endif // !__WXDFB__
        return;
    }

    if ( GetBackgroundBitmap().IsOk() )
    {
        // Get the bitmap and the flags
        int alignment;
        wxStretch stretch;
        wxBitmap bmp = GetBackgroundBitmap(&alignment, &stretch);
        ibRenderer::DrawBitmap(dc, bmp, rect, alignment, stretch);
    }
    else
    {
        // Just fill it with bg colour if no bitmap

        m_renderer->DrawBackground(dc, wxTHEME_BG_COLOUR(this),
                                   rect, GetStateFlags());
    }
}

void ibWindow::Refresh(bool eraseBackground, const wxRect *rect)
{
    wxRect rectClient; // the same rectangle in client coordinates
    wxPoint origin = GetClientAreaOrigin();

    wxSize size = GetClientSize();

    if ( rect )
    {
        // the rectangle passed as argument is in client coordinates
        rectClient = *rect;

        // don't refresh anything beyond the client area (scrollbars for
        // example)
        if ( rectClient.GetRight() > size.x )
            rectClient.SetRight(size.x);
        if ( rectClient.GetBottom() > size.y )
            rectClient.SetBottom(size.y);

    }
    else // refresh the entire client area
    {
        // x,y is already set to 0 by default
        rectClient.SetSize(size);
    }

    // convert refresh rectangle to window coordinates:
    wxRect rectWin(rectClient);
    rectWin.Offset(origin);

    // SEAM vs univ: with no rectangle, the whole window - its non-client part is painted in the paint (TryBefore),
    // where it is dirty, and follows the window's state (enabled, focused, current) as the client area does
    if ( !rect )
        rectWin = wxRect(GetSize());

    wxWindow::Refresh(eraseBackground, &rectWin);

    // Refresh all sub controls if any.
    wxWindowList& children = GetChildren();
    for ( wxWindowList::iterator i = children.begin(); i != children.end(); ++i )
    {
        wxWindow *child = *i;
        // only refresh subcontrols if they are visible:
        if ( child->IsTopLevel() || !child->IsShown() || child->IsFrozen() )
            continue;

        // ...and when the subcontrols are in the update region:
        wxRect childrect(child->GetRect());
        childrect.Intersect(rectClient);
        if ( childrect.IsEmpty() )
            continue;

        // refresh the subcontrol now:
        childrect.Offset(-child->GetPosition());
        // NB: We must call wxWindow version because we need to refresh
        //     the entire control, not just its client area, and this is why we
        //     don't account for child client area origin here either. Also
        //     note that we don't pass eraseBackground to the child, but use
        //     true instead: this is because we can't be sure that
        //     eraseBackground=false is safe for children as well and not only
        //     for the parent.
        child->wxWindow::Refresh(eraseBackground, &childrect);
    }
}

// ----------------------------------------------------------------------------
// state flags
// ----------------------------------------------------------------------------

bool ibWindow::Enable(bool enable)
{
    if ( !wxWindow::Enable(enable) )
        return false;

    // disabled window can't keep focus
    if ( FindFocus() == this && GetParent() != nullptr )
    {
        GetParent()->SetFocus();
    }

    if ( m_renderer )
    {
        // a window with renderer is drawn by ourselves and it has to be
        // refreshed to reflect its new status
        Refresh();
    }

    return true;
}

// SEAM vs univ: the focus inside a composite control is the control's (wx's HasFocus: the focused window's
// GetMainWindowOfCompositeControl) - its frame shows it, the window inside holding the focus has none
bool ibWindow::IsFocused() const
{
    return HasFocus();
}

bool ibWindow::IsPressed() const
{
    return false;
}

bool ibWindow::IsDefault() const
{
    return false;
}

bool ibWindow::IsCurrent() const
{
    return m_isCurrent;
}

bool ibWindow::WXMakeCurrent(bool doit)
{
    if ( doit == m_isCurrent )
        return false;

    m_isCurrent = doit;

    if ( CanBeHighlighted() )
        Refresh();

    return true;
}

int ibWindow::GetStateFlags() const
{
    int flags = 0;
    if ( !IsEnabled() )
        flags |= wxCONTROL_DISABLED;

    // the following states are only possible if our application is active - if
    // it is not, even our default/focused controls shouldn't appear as such
    if ( wxTheApp->IsActive() )
    {
        if ( IsCurrent() )
            flags |= wxCONTROL_CURRENT;
        if ( IsFocused() )
            flags |= wxCONTROL_FOCUSED;
        if ( IsPressed() )
            flags |= wxCONTROL_PRESSED;
        if ( IsDefault() )
            flags |= wxCONTROL_ISDEFAULT;
    }

    return flags;
}

// ----------------------------------------------------------------------------
// size
// ----------------------------------------------------------------------------

void ibWindow::OnSize(wxSizeEvent& WXUNUSED(event))
{
#if wxUSE_SCROLLBAR
    if ( m_scrollbarVert || m_scrollbarHorz )
    {
        PositionScrollbars();
    }
#endif // wxUSE_SCROLLBAR

    // nothing painted yet, or nothing moved - a sizer sends the size at every layout, changed or not
    const wxSize size = GetSize();
    if ( !m_renderer || m_paintedSize.x <= 0 || m_paintedSize.y <= 0 || size == m_paintedSize )
        return;

    // the frame's right and bottom bands - its line, the corner between the scrollbars - where they stood at the size
    // the window was last painted at and where they stand now; what stands beside the frame (a control's title, centred
    // on the window's height) whole. In window coordinates: wxWindow's refresh, not ours, which keeps to the client area
    const wxRect frame = GetBorderRect();
    const wxRect border = m_renderer->GetBorderDimensions(GetBorder());
    int right = border.width,
        bottom = border.height;
#if wxUSE_SCROLLBAR
    if ( m_scrollbarVert )
        right += m_scrollbarVert->GetSize().x;
    if ( m_scrollbarHorz )
        bottom += m_scrollbarHorz->GetSize().y;
#endif // wxUSE_SCROLLBAR

    const int frameRight = frame.GetRight() + 1,
              frameBottom = frame.GetBottom() + 1,
              paintedRight = frameRight - (size.x - m_paintedSize.x),
              paintedBottom = frameBottom - (size.y - m_paintedSize.y);
    if ( right > 0 && paintedRight != frameRight )
    {
        const int left = wxMin(frameRight, paintedRight) - right;
        wxRect band(left, 0, wxMin(wxMax(frameRight, paintedRight), size.x) - left, size.y);
        wxWindow::Refresh(false, &band);
    }
    if ( bottom > 0 && paintedBottom != frameBottom )
    {
        const int top = wxMin(frameBottom, paintedBottom) - bottom;
        wxRect band(frame.x, top, frame.width, wxMin(wxMax(frameBottom, paintedBottom), size.y) - top);
        wxWindow::Refresh(false, &band);
    }
    if ( frame.x > 0 && size.y != m_paintedSize.y )
    {
        wxRect beside(0, 0, frame.x, size.y);
        wxWindow::Refresh(false, &beside);
    }
    if ( frameRight < size.x )
    {
        wxRect beside(frameRight, 0, size.x - frameRight, size.y);
        wxWindow::Refresh(false, &beside);
    }
}

wxSize ibWindow::GetWindowBorderSize() const
{
    // the border, and what stands beside the frame (GetBorderRect)
    return AdjustSize(wxSize(0, 0)) + (GetSize() - GetBorderRect().GetSize());
}

wxSize ibWindow::AdjustSize(const wxSize& size) const
{
    wxSize sz = size;
    if ( m_renderer )
        m_renderer->AdjustSize(&sz, this);
    return sz;
}

wxPoint ibWindow::GetClientAreaOrigin() const
{
    wxPoint pt = wxWindowBase::GetClientAreaOrigin();

#if wxUSE_TWO_WINDOWS
#else
    if ( m_renderer )
        pt += GetBorderRect().GetPosition() + m_renderer->GetBorderDimensions(GetBorder()).GetPosition();
#endif

    return pt;
}

void ibWindow::DoGetClientSize(int *width, int *height) const
{
    // if it is a native window, we assume it handles the scrollbars itself
    // too - and if it doesn't, there is not much we can do
    if ( !m_renderer )
    {
        wxWindow::DoGetClientSize(width, height);

        return;
    }

    // SEAM vs univ: the frame's size, not the native client's (the whole window: the host has no border of its own) -
    // a title beside the frame is not in the client area
    const wxRect rectFrame = GetBorderRect();
    int w = rectFrame.width,
        h = rectFrame.height;

    // we assume that the scrollbars are positioned correctly (by a previous
    // call to PositionScrollbars()) here

    wxRect rectBorder;
    if ( m_renderer )
        rectBorder = m_renderer->GetBorderDimensions(GetBorder());

    if ( width )
    {
#if wxUSE_SCROLLBAR
        // in any case, take account of the scrollbar
        if ( m_scrollbarVert )
            w -= m_scrollbarVert->GetSize().x;
#endif // wxUSE_SCROLLBAR

        // account for the left and right borders
        *width = w - rectBorder.x - rectBorder.width;

        // we shouldn't return invalid width
        if ( *width < 0 )
            *width = 0;
    }

    if ( height )
    {
#if wxUSE_SCROLLBAR
        if ( m_scrollbarHorz )
            h -= m_scrollbarHorz->GetSize().y;
#endif // wxUSE_SCROLLBAR

        *height = h - rectBorder.y - rectBorder.height;

        // we shouldn't return invalid height
        if ( *height < 0 )
            *height = 0;
    }
}

void ibWindow::DoSetClientSize(int width, int height)
{
    // take into account the borders
    wxRect rectBorder = m_renderer->GetBorderDimensions(GetBorder());
    width += rectBorder.x;
    height += rectBorder.y;

    // and the scrollbars (as they may be offset into the border, use the
    // scrollbar position, not size - this supposes that PositionScrollbars()
    // had been called before) - measured from the frame's far edge
    const wxRect rectFrame = GetBorderRect();
#if wxUSE_SCROLLBAR
    if ( m_scrollbarVert )
        width += rectFrame.GetRight() + 1 - m_scrollbarVert->GetPosition().x;
#endif // wxUSE_SCROLLBAR
    width += rectBorder.width;

#if wxUSE_SCROLLBAR
    if ( m_scrollbarHorz )
        height += rectFrame.GetBottom() + 1 - m_scrollbarHorz->GetPosition().y;
#endif // wxUSE_SCROLLBAR
    height += rectBorder.height;

    // SEAM vs univ: and what stands beside the frame (a title, GetBorderRect)
    const wxSize beside = GetSize() - rectFrame.GetSize();
    wxWindow::DoSetClientSize(width + beside.x, height + beside.y);
}

wxHitTest ibWindow::DoHitTest(wxCoord x, wxCoord y) const
{
    wxHitTest ht = wxWindow::DoHitTest(x, y);

#if wxUSE_SCROLLBAR
    if ( ht == wxHT_WINDOW_INSIDE )
    {
        if ( m_scrollbarVert && x >= m_scrollbarVert->GetPosition().x )
        {
            // it can still be changed below because it may also be the corner
            ht = wxHT_WINDOW_VERT_SCROLLBAR;
        }

        if ( m_scrollbarHorz && y >= m_scrollbarHorz->GetPosition().y )
        {
            ht = ht == wxHT_WINDOW_VERT_SCROLLBAR ? wxHT_WINDOW_CORNER
                                                  : wxHT_WINDOW_HORZ_SCROLLBAR;
        }
    }
#endif // wxUSE_SCROLLBAR

    return ht;
}

// ----------------------------------------------------------------------------
// scrolling: we implement it entirely ourselves except for ScrollWindow()
// function which is supposed to be (efficiently) implemented by the native
// window class
// ----------------------------------------------------------------------------

void ibWindow::RefreshScrollbars()
{
#if wxUSE_SCROLLBAR
    if ( m_scrollbarHorz )
        m_scrollbarHorz->Refresh();

    if ( m_scrollbarVert )
        m_scrollbarVert->Refresh();
#endif // wxUSE_SCROLLBAR
}

void ibWindow::PositionScrollbars()
{
#if wxUSE_SCROLLBAR
    // do not use GetClientSize/Rect as it relies on the scrollbars being
    // correctly positioned

    // SEAM vs univ: along the frame's edges, not the window's (GetBorderRect)
    const wxRect rectFrame = GetBorderRect();
    wxSize size = rectFrame.GetSize();
    wxBorder border = GetBorder();
    wxRect rectBorder = m_renderer->GetBorderDimensions(border);
    bool inside = m_renderer->AreScrollbarsInsideBorder();

    int height = m_scrollbarHorz ? m_scrollbarHorz->GetSize().y : 0;
    int width = m_scrollbarVert ? m_scrollbarVert->GetSize().x : 0;

    // SEAM vs univ: the scrollbars painted again when one of them moved - a size that moved nothing left them as they
    // were painted: a sizer sends the size to each of its windows at every layout, changed or not (wxSizerItem::
    // SetDimension, wxSIZE_FORCE_EVENT), and a frame sized by its left edge painted the tree's scrollbar whole at every
    // step, 6 ms of its 35 (2026-11-11, measured)
    const wxRect placedVert = m_scrollbarVert ? m_scrollbarVert->GetRect() : wxRect(),
                 placedHorz = m_scrollbarHorz ? m_scrollbarHorz->GetRect() : wxRect();

    wxRect rectBar;
    if ( m_scrollbarVert )
    {
        rectBar.x = rectFrame.x + size.x - width;
        if ( inside )
           rectBar.x -= rectBorder.width;
        rectBar.width = width;
        rectBar.y = rectFrame.y;
        if ( inside )
            rectBar.y += rectBorder.y;
        rectBar.height = size.y - height;
        if ( inside )
            rectBar.height -= rectBorder.y + rectBorder.height;

        m_scrollbarVert->SetSize(rectBar, wxSIZE_NO_ADJUSTMENTS);
    }

    if ( m_scrollbarHorz )
    {
        rectBar.y = rectFrame.y + size.y - height;
        if ( inside )
            rectBar.y -= rectBorder.height;
        rectBar.height = height;
        rectBar.x = rectFrame.x;
        if ( inside )
            rectBar.x += rectBorder.x;
        rectBar.width = size.x - width;
        if ( inside )
            rectBar.width -= rectBorder.x + rectBorder.width;

        m_scrollbarHorz->SetSize(rectBar, wxSIZE_NO_ADJUSTMENTS);
    }

    if ( (m_scrollbarVert && m_scrollbarVert->GetRect() != placedVert) ||
         (m_scrollbarHorz && m_scrollbarHorz->GetRect() != placedHorz) )
        RefreshScrollbars();
#endif // wxUSE_SCROLLBAR
}

void ibWindow::SetScrollbar(int orient,
                            int pos,
                            int pageSize,
                            int range,
                            bool refresh)
{
#if wxUSE_SCROLLBAR
    wxASSERT_MSG( (range == -1 || pageSize <= range),
                    wxT("page size can't be greater than range") );

    bool hasClientSizeChanged = false;
    ibScrollBar *scrollbar = GetScrollbar(orient);
    if ( range && (pageSize < range) )
    {
        if ( !scrollbar )
        {
            // create it
#if wxUSE_TWO_WINDOWS
            SetInsertIntoMain( true );
#endif
            scrollbar = new ibWindowScrollBar(this, wxID_ANY,
                                              wxDefaultPosition, wxDefaultSize,
                                              orient & wxVERTICAL ? wxSB_VERTICAL
                                                                  : wxSB_HORIZONTAL);
#if wxUSE_TWO_WINDOWS
            SetInsertIntoMain( false );
#endif
            if ( orient & wxVERTICAL )
                m_scrollbarVert = scrollbar;
            else
                m_scrollbarHorz = scrollbar;

            // the client area diminished as we created a scrollbar
            hasClientSizeChanged = true;

            PositionScrollbars();
        }
        else if ( GetWindowStyle() & wxALWAYS_SHOW_SB )
        {
            // we might have disabled it before
            scrollbar->Enable();
        }

        scrollbar->SetScrollbar(pos, pageSize, range, pageSize, refresh);
    }
    else // no range means no scrollbar
    {
        if ( scrollbar )
        {
            // wxALWAYS_SHOW_SB only applies to the vertical scrollbar
            if ( range == -1 || ((orient & wxVERTICAL) && (GetWindowStyle() & wxALWAYS_SHOW_SB)) )
            {
                // just disable the scrollbar
                scrollbar->SetScrollbar(pos, pageSize, range == -1 ? 0 : range, pageSize, refresh);
                scrollbar->Disable();
            }
            else // really remove the scrollbar
            {
                // SEAM vs univ: hidden now, destroyed once the events are done (ScheduleForDestruction) - not deleted
                // here: a scrollbar goes away inside a size, where wxMSW defers the children's moves to its end
                // (DeferWindowPos), and a window of that batch destroyed before its end makes the system drop the whole
                // batch, silently: a form whose first layout made the scrollbar and second took it away left its table
                // at its old size till a resize (2026-11-11, measured; the home page's lists, a document's table part).
                // On GTK3/Cocoa nothing is deferred; the scrollbar is destroyed a moment later there as well
                scrollbar->Hide();
                wxTheApp->ScheduleForDestruction(scrollbar);

                if ( orient & wxVERTICAL )
                    m_scrollbarVert = nullptr;
                else
                    m_scrollbarHorz = nullptr;

                // the client area increased as we removed a scrollbar
                hasClientSizeChanged = true;

                // the size of the remaining scrollbar must be adjusted
                if ( m_scrollbarHorz || m_scrollbarVert )
                {
                    PositionScrollbars();
                }
            }
        }
    }

    // give the window a chance to relayout
    if ( hasClientSizeChanged )
    {
#if wxUSE_TWO_WINDOWS
        wxWindow::SetSize( GetSize() );
#else
        wxSizeEvent event(GetSize());
        (void)GetEventHandler()->ProcessEvent(event);
#endif
    }
#else
    wxUnusedVar(orient);
    wxUnusedVar(pos);
    wxUnusedVar(pageSize);
    wxUnusedVar(range);
    wxUnusedVar(refresh);
#endif // wxUSE_SCROLLBAR
}

void ibWindow::SetScrollPos(int orient, int pos, bool WXUNUSED(refresh))
{
#if wxUSE_SCROLLBAR
    ibScrollBar *scrollbar = GetScrollbar(orient);

    if (scrollbar)
        scrollbar->SetThumbPosition(pos);

    // VZ: I think we can safely ignore this as we always refresh it
    //     automatically whenever the value chanegs
#if 0
    if ( refresh )
        Refresh();
#endif
#else
    wxUnusedVar(orient);
    wxUnusedVar(pos);
#endif // wxUSE_SCROLLBAR
}

int ibWindow::GetScrollPos(int orient) const
{
#if wxUSE_SCROLLBAR
    ibScrollBar *scrollbar = GetScrollbar(orient);
    return scrollbar ? scrollbar->GetThumbPosition() : 0;
#else
    wxUnusedVar(orient);
    return 0;
#endif // wxUSE_SCROLLBAR
}

int ibWindow::GetScrollThumb(int orient) const
{
#if wxUSE_SCROLLBAR
    ibScrollBar *scrollbar = GetScrollbar(orient);
    return scrollbar ? scrollbar->GetThumbSize() : 0;
#else
    wxUnusedVar(orient);
    return 0;
#endif // wxUSE_SCROLLBAR
}

int ibWindow::GetScrollRange(int orient) const
{
#if wxUSE_SCROLLBAR
    ibScrollBar *scrollbar = GetScrollbar(orient);
    return scrollbar ? scrollbar->GetRange() : 0;
#else
    wxUnusedVar(orient);
    return 0;
#endif // wxUSE_SCROLLBAR
}

// SEAM vs univ: SCROLLED BY A REPAINT, on every port. The canon moved the pixels that stay through a client DC
// (ScrollNoRefresh) and repainted what they uncovered - a DC Cocoa and GTK3 under Wayland do not draw through, and the
// buffered paint here met stale rows behind it on MSW too (the text control and the list box scrolled by a repaint
// already). Not the native scroll: it moves the whole window, the border and the scrollbars with it (wxGTK ignores the
// rectangle). The paint is buffered (ibThemePaintDC)
void ibWindow::ScrollWindow(int dx, int dy, const wxRect *rect)
{
    if ( !dx && !dy )
        return;

    Refresh(true /* erase bkgnd */, rect);

    // scroll children accordingly:
    wxPoint offset(dx, dy);

    for (wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
         node; node = node->GetNext())
    {
        wxWindow *child = node->GetData();
#if wxUSE_SCROLLBAR
        if ( child == m_scrollbarVert || child == m_scrollbarHorz )
            continue;
#endif // wxUSE_SCROLLBAR

        // VS: Scrolling children has non-trivial semantics. If rect=nullptr then
        //     it is easy: we scroll all children. Otherwise it gets
        //     complicated:
        //       1. if scrolling in one direction only, scroll only
        //          those children that intersect shaft defined by the rectangle
        //          and scrolling direction
        //       2. if scrolling in both axes, scroll all children

        bool shouldMove = false;

        if ( rect && (dx * dy == 0 /* moving in only one of x, y axis */) )
        {
            wxRect childRect = child->GetRect();
            if ( dx == 0 && (childRect.GetLeft() <= rect->GetRight() ||
                             childRect.GetRight() >= rect->GetLeft()) )
            {
                shouldMove = true;
            }
            else if ( dy == 0 && (childRect.GetTop() <= rect->GetBottom() ||
                                  childRect.GetBottom() >= rect->GetTop()) )
            {
                shouldMove = true;
            }
            // else: child outside of scrolling shaft, don't move
        }
        else // scrolling in both axes or rect=nullptr
        {
            shouldMove = true;
        }

        if ( shouldMove )
            child->Move(child->GetPosition() + offset, wxSIZE_ALLOW_MINUS_ONE);
    }
}

// ----------------------------------------------------------------------------
// accelerators and menu hot keys
// ----------------------------------------------------------------------------

// SEAM vs univ: NO KEYS OF ITS OWN FOR THE MENUS - the canon's window looked for the accelerators and the menubar's
// letters in every key it had (OnKeyDown, OnChar, OnKeyUp). Here the accelerators and the menubar's letters are the
// frame's, before a window has the key, on every port (ibFrame::OnCharHook, ibMenuBar::ProcessAccelEvent), Return the
// dialog's default button (ibDialog). Not kept: Alt or F10 alone selecting the first title.

// ----------------------------------------------------------------------------
// MSW-specific section
// ----------------------------------------------------------------------------

#ifdef __WXMSW__

#include <wx/msw/private.h>

WXLRESULT ibWindow::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    if ( message == WM_NCHITTEST )
    {
        // the windows which contain the other windows should let the mouse
        // events through, otherwise a window inside a static box would
        // never get any events at all
        if ( IsStaticBox() )
        {
            return HTTRANSPARENT;
        }
    }

    // SEAM vs MSW: the border is ours - drawn inside the window (TryBefore), the client area measured inside it
    // (DoGetClientSize). MSWGetStyle keeps it off the native style, but wxMSW's themed border reads the wx style,
    // which keeps it: WM_NCCALCSIZE took 2px a side off the native client - the children went past its edge and the
    // scroll blit pulled the frame in, a line a row - and WM_NCPAINT drew a second frame round ours.
    if ( m_renderer && (message == WM_NCCALCSIZE || message == WM_NCPAINT) )
        return MSWDefWindowProc(message, wParam, lParam);

    return wxWindow::MSWWindowProc(message, wParam, lParam);
}

WXDWORD ibWindow::MSWGetStyle(long flags, WXDWORD *exstyle) const
{
    flags &= ~(wxBORDER_MASK | wxVSCROLL | wxHSCROLL);
    return wxWindow::MSWGetStyle(flags | wxBORDER_NONE, exstyle);
}

#endif // __WXMSW__

// ----------------------------------------------------------------------------
// OSX-specific section
// ----------------------------------------------------------------------------

#ifdef __WXOSX__

long ibWindow::MacGetWXBorderSize() const
{
    // a native control (no renderer) keeps the platform's frame
    return m_renderer ? 0 : wxWindow::MacGetWXBorderSize();
}

#endif // __WXOSX__

// ----------------------------------------------------------------------------
// GTK-specific section
// ----------------------------------------------------------------------------

#ifdef __WXGTK__

bool ibWindow::GTKProcessEvent(wxEvent& event) const
{
    wxMouseEvent * const mouse = wxDynamicCast(&event, wxMouseEvent);
    if ( !mouse )
        return wxWindow::GTKProcessEvent(event);

    const wxPoint position = mouse->GetPosition();
    mouse->SetPosition(position - GetClientAreaOrigin());

    const bool processed = wxWindow::GTKProcessEvent(event);

    mouse->SetPosition(position);
    return processed;
}

void ibWindow::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    // the place kept read as the client area's, then made the native area's - as wxMSW does (wxWindowMSW::DoSetSize)
    if ( !IsTopLevel() )
    {
        if ( x == wxDefaultCoord && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE) )
            x = GetPosition().x;
        if ( y == wxDefaultCoord && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE) )
            y = GetPosition().y;

        AdjustForParentClientOrigin(x, y, sizeFlags);
        sizeFlags |= wxSIZE_ALLOW_MINUS_ONE;
    }

    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}

void ibWindow::DoGetPosition(int *x, int *y) const
{
    wxWindow::DoGetPosition(x, y);

    // ...and the native place given back as the client area's (wxWindowMSW::DoGetPosition)
    const wxWindow * const parent = GetParent();
    if ( parent && !IsTopLevel() )
    {
        const wxPoint origin = parent->GetClientAreaOrigin();
        if ( x )
            *x -= origin.x;
        if ( y )
            *y -= origin.y;
    }
}

#endif // __WXGTK__
