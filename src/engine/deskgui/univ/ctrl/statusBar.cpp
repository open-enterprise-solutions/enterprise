// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically): the status bar, over the port's
// own wxStatusBar (statusBar.h).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/statusbr.cpp
// Purpose:     ibStatusBar implementation
// Author:      Vadim Zeitlin
// Created:     14.10.01
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

#include "deskgui/univ/ctrl/statusBar.h"

#if wxUSE_STATUSBAR

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
#endif

#include <wx/dcbuffer.h>

#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"

// ============================================================================
// implementation
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibStatusBar, wxStatusBar);

wxBEGIN_EVENT_TABLE(ibStatusBar, wxStatusBar)
    EVT_PAINT(ibStatusBar::OnPaint)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

bool ibStatusBar::Create(wxWindow *parent,
                         wxWindowID id,
                         long style,
                         const wxString& name)
{
    // SEAM vs univ: the theme's renderer asked for - the bar is the port's wxStatusBar, not an ibWindow
    m_renderer = ibThemeEngine::Get()->GetRenderer();

    // the canon's plain window, not the port's bar (wxStatusBar::Create: the native control on MSW) - painted whole as
    // it is sized, its fields placed by its width (OnPaint). No size grip: the theme draws none, and the port's bar on
    // GTK (wxStatusBarGeneric, whose events this window still gets there) would size the window from the corner
    if ( !wxWindow::Create(parent, id,
                           wxDefaultPosition, wxDefaultSize,
                           (style & ~wxSTB_SIZEGRIP) | wxFULL_REPAINT_ON_RESIZE, name) )
    {
        return false;
    }

    SetBackgroundStyle(wxBG_STYLE_PAINT);

    SetFieldsCount(1);

    SetSize(DoGetBestSize());

    return true;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ibStatusBar::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    // SEAM vs univ: the canon's DoDraw over its control's background - here the background is the bar's own, in the
    // same buffer as its fields (as an ibWindow's, ibThemePaintDC): the theme's chrome, the status bar's
    // (docs/private/ui-palette.md, chrome-light), unless the bar is given its own
    wxAutoBufferedPaintDC dc(this);
    const wxRect client = GetClientRect();
    m_renderer->DrawBackground(dc, UseBgCol() ? GetBackgroundColour() : wxTHEME_COLOUR(TITLEBAR), client, 0);

    // prepare the DC
    dc.SetFont(GetFont());
    dc.SetTextForeground(UseForegroundColour() ? GetForegroundColour() : wxTHEME_COLOUR(CONTROL_TEXT));

    // do draw the fields
    const int flags = IsEnabled() ? 0 : (int)wxCONTROL_DISABLED;
    const wxArrayInt widths = GetFieldWidths();
    wxRect rect = client;
    for ( int n = 0; n < GetFieldsCount(); n++ )
    {
        rect.width = widths[n];

        if ( IsExposed(rect) )
            DrawStatusField(dc, rect, n, flags);

        rect.x += rect.width + m_renderer->GetStatusBarBorderBetweenFields();
    }
}

void ibStatusBar::DrawStatusField(wxDC& dc, const wxRect& rect, int n, int flags)
{
    // SEAM vs univ: the text ellipsized to what its field holds - inside the field's border, if it has one, and the
    // theme's margins (ibRenderer::DrawStatusField) - by the bar's style, as the port's bars do (wxStatusBarGeneric::
    // DrawFieldText, the native one on MSW); the canon clipped it
    const int style = GetStatusStyle(n);
    wxEllipsizeMode mode = wxELLIPSIZE_NONE;
    if ( HasFlag(wxSTB_ELLIPSIZE_START) )
        mode = wxELLIPSIZE_START;
    else if ( HasFlag(wxSTB_ELLIPSIZE_MIDDLE) )
        mode = wxELLIPSIZE_MIDDLE;
    else if ( HasFlag(wxSTB_ELLIPSIZE_END) )
        mode = wxELLIPSIZE_END;

    const wxCoord border = style == wxSB_FLAT ? 0 : m_renderer->GetStatusBarBorders().x;
    const wxCoord width = rect.width - 2*(border + m_renderer->GetStatusBarFieldMargins().x);
    const wxString text = wxControl::Ellipsize(GetStatusText(n), dc, mode, width, wxELLIPSIZE_FLAGS_EXPAND_TABS);

    m_renderer->DrawStatusField(dc, rect, text, flags, style);
}

void ibStatusBar::DoUpdateStatusText(int i)
{
    wxRect rect;
    if ( GetFieldRect(i, rect) )
    {
        RefreshRect(rect);
    }
}

// ----------------------------------------------------------------------------
// fields count/widths
// ----------------------------------------------------------------------------

void ibStatusBar::SetFieldsCount(int number, const int *widths)
{
    // SEAM vs univ: the base's, not the port's bar's (the native one sets its parts); the fields placed anew
    wxStatusBarBase::SetFieldsCount(number, widths);

    Refresh();
}

void ibStatusBar::SetStatusWidths(int n, const int widths[])
{
    wxStatusBarBase::SetStatusWidths(n, widths);

    Refresh();
}

void ibStatusBar::SetStatusStyles(int n, const int styles[])
{
    wxStatusBarBase::SetStatusStyles(n, styles);

    Refresh();
}

wxArrayInt ibStatusBar::GetFieldWidths() const
{
    // the total width for the fields doesn't include the borders between them
    return CalculateAbsWidths(GetClientSize().x -
                              m_renderer->GetStatusBarBorderBetweenFields() * (GetFieldsCount() - 1));
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

bool ibStatusBar::GetFieldRect(int n, wxRect& rect) const
{
    wxCHECK_MSG( n >= 0 && n < GetFieldsCount(), false,
                 wxT("invalid field index in GetFieldRect()") );

    rect = DoGetFieldRect(n);

    return true;
}

wxRect ibStatusBar::DoGetFieldRect(int n) const
{
    const wxArrayInt widths = GetFieldWidths();

    wxRect rect = GetClientRect();
    for ( int i = 0; i <= n; i++ )
    {
        rect.width = widths[i];

        if ( i < n )
            rect.x += rect.width + m_renderer->GetStatusBarBorderBetweenFields();
    }

    return rect;
}

wxCoord ibStatusBar::GetHeight() const
{
    return GetCharHeight() + 2*GetBorderY();
}

wxSize ibStatusBar::DoGetBestSize() const
{
    return wxSize(100, GetHeight());
}

void ibStatusBar::DoSetSize(int x, int y,
                            int width, int WXUNUSED(height),
                            int sizeFlags)
{
    wxStatusBar::DoSetSize(x, y, width, GetHeight(), sizeFlags);
}

// ----------------------------------------------------------------------------
// misc
// ----------------------------------------------------------------------------

void ibStatusBar::SetMinHeight(int WXUNUSED(height))
{
    // nothing to do here, we don't support it - and why would we?
}

int ibStatusBar::GetBorderX() const
{
    return m_renderer->GetStatusBarBorders().x +
           m_renderer->GetStatusBarFieldMargins().x;
}

int ibStatusBar::GetBorderY() const
{
    return m_renderer->GetStatusBarBorders().y +
           m_renderer->GetStatusBarFieldMargins().y;
}

#ifdef __WXMSW__

WXLRESULT ibStatusBar::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    return wxStatusBarBase::MSWWindowProc(message, wParam, lParam);
}

WXDWORD ibStatusBar::MSWGetStyle(long flags, WXDWORD *exstyle) const
{
    return wxStatusBarBase::MSWGetStyle(flags, exstyle);
}

void ibStatusBar::DoMoveWindow(int x, int y, int width, int height)
{
    wxStatusBarBase::DoMoveWindow(x, y, width, height);
}

#endif // __WXMSW__

#endif // wxUSE_STATUSBAR
