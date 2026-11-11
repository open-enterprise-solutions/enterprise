#include <wx/wxprec.h>

#include "deskgui/univ/toolTip.h"

#include <wx/app.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/settings.h>
#include <wx/tooltip.h>

#include "deskgui/univ/window.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/renderer.h"

#ifdef __WXMSW__
    #include <wx/msw/private.h>
    #include "deskgui/univ/topLevel.h"   // ibMSWShowShadowed — the popup's shadow, its own
#endif

namespace {

// how long the mouse rests before the tip shows - the system's tooltip waits as long
constexpr int kRestDelay = 500;

// the margin round the text inside the frame, and the drop below the pointer, both in DIPs
constexpr int kTextMargin = 4;
constexpr int kPointerDrop = 20;

wxWeakRef<ibToolTipWindow> s_window;

} // namespace

ibToolTipWindow::ibToolTipWindow(wxWindow *parent)
    : m_timer(this)
{
    // a top-level popup (wxPU_CONTAINS_CONTROLS), as the shadow needs one (Show)
    Create(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    Bind(wxEVT_TIMER, &ibToolTipWindow::OnTimer, this);
    Bind(wxEVT_PAINT, &ibToolTipWindow::OnPaint, this);
}

#ifdef __WXMSW__

bool ibToolTipWindow::Show(bool show)
{
    if ( !show )
        return wxPopupWindow::Show(false);

    // shown without being activated: the window's state first, then the native show that does not activate
    if ( !wxWindowBase::Show(true) )
        return false;

    const HWND hWnd = GetHwnd();
    ibMSWShowShadowed(this, [hWnd] { ::ShowWindow(hWnd, SW_SHOWNA); return true; });
    ::SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

bool ibToolTipWindow::MSWHandleMessage(WXLRESULT *result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    if ( message == WM_MOUSEACTIVATE )
    {
        *result = MA_NOACTIVATE;
        return true;
    }
    if ( message == WM_NCHITTEST )
    {
        *result = HTTRANSPARENT;
        return true;
    }
    return wxPopupWindow::MSWHandleMessage(result, message, wParam, lParam);
}

#endif // __WXMSW__

void ibToolTipWindow::Track(ibWindow *owner, const wxPoint& at)
{
    const wxToolTip *const tip = owner->GetToolTip();
    const wxString text = tip != nullptr ? tip->GetTip() : wxString();
    if ( text.empty() || !owner->IsShownOnScreen() || !owner->GetScreenRect().Contains(at) )
    {
        if ( m_owner == owner )
            Dismiss();
        return;
    }

    // the same tip: shown, it stays; waited for, the rest starts again where the pointer moved to
    if ( m_owner == owner && text == m_text )
    {
        if ( !IsShown() )
        {
            m_at = at;
            m_timer.StartOnce(kRestDelay);
        }
        return;
    }

    // another window's tip, or another tip of this one: one already shown gives way to it at once
    const bool shown = IsShown();
    m_owner = owner;
    m_text = text;
    m_at = at;
    if ( shown )
        ShowTip();
    else
        m_timer.StartOnce(kRestDelay);
}

void ibToolTipWindow::Dismiss()
{
    m_timer.Stop();
    if ( IsShown() )
        Show(false);
    m_owner = nullptr;
    m_text.clear();
}

void ibToolTipWindow::ShowTip()
{
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    wxCoord width = 0, height = 0;
    dc.GetMultiLineTextExtent(m_text, &width, &height);

    const int margin = FromDIP(kTextMargin) + 1;   // and the frame's line
    wxRect rect(m_at + wxPoint(0, FromDIP(kPointerDrop)), wxSize(width + 2 * margin, height + 2 * margin));

    // on the screen the pointer is on: pushed back from its right and bottom edges, above the pointer when there is no
    // room below it
    const int display = wxDisplay::GetFromPoint(m_at);
    const wxRect area = wxDisplay(display != wxNOT_FOUND ? display : 0).GetClientArea();
    if ( rect.GetRight() > area.GetRight() )
        rect.x = wxMax(area.x, area.GetRight() - rect.width + 1);
    if ( rect.GetBottom() > area.GetBottom() )
        rect.y = wxMax(area.y, m_at.y - rect.height - FromDIP(2));

    SetSize(rect);
    Refresh();
    if ( !IsShown() )
        Show(true);
}

void ibToolTipWindow::OnTimer(wxTimerEvent& WXUNUSED(event))
{
    // the owner gone, or the pointer off it while the rest was waited: nothing to show
    if ( m_owner == nullptr || !m_owner->GetScreenRect().Contains(wxGetMousePosition()) )
    {
        Dismiss();
        return;
    }
    ShowTip();
}

void ibToolTipWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetFont(GetFont());
    ibThemeEngine::Get()->GetRenderer()->DrawToolTip(dc, wxRect(GetClientSize()), m_text);
}

/* static */
ibToolTipWindow *ibToolTipWindow::Get()
{
    if ( s_window == nullptr )
    {
        wxWindow *const top = wxTheApp != nullptr ? wxTheApp->GetTopWindow() : nullptr;
        if ( top == nullptr )
            return nullptr;
        s_window = new ibToolTipWindow(top);
    }
    return s_window;
}
