// FORKED from wx's generic activity indicator (wx -> ib): the implementation class folded into the control, the
// ground the theme's (ibThemePaintDC), the dots the theme's text unless the control is given a colour of its own

///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/activityindicator.cpp
// Purpose:     Generic wxActivityIndicator implementation.
// Author:      Vadim Zeitlin
// Created:     2015-03-06
// Copyright:   (c) 2015 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/activityIndicator.h"

#include <wx/graphics.h>

#include <memory>

#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

namespace
{

// Total number of "running" dots.
const int NUM_DOTS = 8;

// Delay between the consecutive updates in milliseconds.
const int FRAME_DELAY = 150;

} // anonymous namespace

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibActivityIndicator, ibControl)
    EVT_PAINT(ibActivityIndicator::OnPaint)
    EVT_TIMER(wxID_ANY, ibActivityIndicator::OnTimer)
wxEND_EVENT_TABLE()

bool ibActivityIndicator::Create(wxWindow* parent,
                                 wxWindowID winid,
                                 const wxPoint& pos,
                                 const wxSize& size,
                                 long style,
                                 const wxString& name)
{
    // a plain window as wx's (wxWindow::Create): no frame unless one is asked for
    if ( !(style & wxBORDER_MASK) )
        style |= wxBORDER_NONE;

    // painted whole as it is sized: its dots are centred in it, sized by it (ibWindow::OnSize paints again only the
    // frame)
    style |= wxFULL_REPAINT_ON_RESIZE;

    if ( !ibControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name) )
        return false;

    m_timer.SetOwner(this);
    return true;
}

void ibActivityIndicator::Start()
{
    // Avoid restarting the timer if it's already running, this could
    // result in jumps in appearance of the next frame while calling
    // Start() is not supposed to have any effect at all in this case.
    if ( m_timer.IsRunning() )
        return;

    m_timer.Start(FRAME_DELAY);
}

void ibActivityIndicator::Stop()
{
    // Unlike above, it's not a problem to call Stop() even if we're not
    // running.
    m_timer.Stop();
}

bool ibActivityIndicator::IsRunning() const
{
    return m_timer.IsRunning();
}

wxSize ibActivityIndicator::DoGetBestClientSize() const
{
    int size = 0;
    switch ( GetWindowVariant() )
    {
        case wxWINDOW_VARIANT_MAX:
            wxFAIL_MSG(wxS("Invalid window variant"));
            wxFALLTHROUGH;

        case wxWINDOW_VARIANT_NORMAL:
            size = 24;
            break;

        case wxWINDOW_VARIANT_SMALL:
            size = 16;
            break;

        case wxWINDOW_VARIANT_MINI:
            size = 12;
            break;

        case wxWINDOW_VARIANT_LARGE:
            size = 32;
            break;
    }

    wxASSERT_MSG( size, wxS("Unknown window variant") );

    return FromDIP(wxSize(size, size));
}

void ibActivityIndicator::OnTimer(wxTimerEvent& WXUNUSED(event))
{
    if ( ++m_frame == NUM_DOTS )
        m_frame = 0;

    Refresh();
}

void ibActivityIndicator::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    // SEAM vs wx: the theme's ground - wx's painted on the window's own erase, the system's colour
    ibThemePaintDC dc(this);

    std::unique_ptr<wxGraphicsContext> const gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if ( !gc )
        return;

    const wxSize size = GetClientSize();

    // Centre everything.
    gc->Translate(size.x/2., size.y/2.);

    // Radius of 1/10th allows to have reasonably sized dots with a bit of
    // separation between them and so subjectively looks a bit nicer than
    // perhaps more natural 1/8th.
    static const double RADIUS_FACTOR = 10;

    const double r = wxMin(size.x, size.y) / RADIUS_FACTOR;

    // The initial dot touches the top border.
    wxGraphicsPath path = gc->CreatePath();
    path.AddCircle(0, -(RADIUS_FACTOR / 2. - 1.)*r, r);

    // Subsequent dots are rotated by this angle with respect to the
    // previous one.
    const double angle = wxDegToRad(360. / NUM_DOTS);

    // And the animation effect is achieved just by starting to draw from
    // the next position every time.
    gc->Rotate(m_frame*angle);

    // SEAM vs wx: the theme's text, as a gauge takes the theme's colour (ibGauge::DoDraw) - wx's took the window's
    // foreground, the system's
    wxColour colDot = UseFgCol() ? GetForegroundColour() : wxTHEME_COLOUR(CONTROL_TEXT);

    const bool isEnabled = IsThisEnabled();
    for ( int n = 0; n < NUM_DOTS; n++ )
    {
        // Draw all dots uniformly grey when the window is disabled,
        // otherwise each subsequent dot is slightly more opaque.
        const int opacityIndex = isEnabled ? n + 1 : 2;

        // wxALPHA_OPAQUE+1 is used just because it is divisible by the
        // default NUM_DOTS value, and we need -1 because of this to keep
        // it in 0..wxALPHA_OPAQUE range.
        const int opacity = opacityIndex*(wxALPHA_OPAQUE + 1)/NUM_DOTS - 1;

        colDot.Set(colDot.Red(), colDot.Green(), colDot.Blue(), opacity);
        gc->SetBrush(colDot);

        gc->FillPath(path);
        gc->Rotate(angle);
    }
}

wxIMPLEMENT_DYNAMIC_CLASS(ibActivityIndicator, ibControl);
