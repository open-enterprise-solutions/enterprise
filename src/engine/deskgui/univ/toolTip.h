#ifndef _DESKGUI_UNIV_TOOLTIP_H_
#define _DESKGUI_UNIV_TOOLTIP_H_

#include "deskgui/deskgui.h"

#include <wx/popupwin.h>
#include <wx/timer.h>
#include <wx/weakref.h>

class ibWindow;

// THE THEME'S TOOLTIP - one popup for the application, showing the tip of the library's window under the mouse: the
// window keeps its tip from the system's tooltip (ibWindow::DoSetToolTip) and hands the mouse over it here
// (ibWindow::TryBefore). Shown once the mouse rests, painted by the theme (ibRenderer::DrawToolTip), under the drop
// shadow a menu casts (Max, 2026-11-10: "the tooltip can be taken too")
class DESKGUI_API ibToolTipWindow : public wxPopupWindow
{
public:
#ifdef __WXMSW__
    // shown as the menu is (ibPopupMenuWindow::Show): a top-level popup that does not activate, under the shadow
    virtual bool Show(bool show = true) override;

    // the mouse goes through it: neither a click on it activates it nor is it a window the mouse can be over
    virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    // the mouse at `at` on the screen, over `owner` or just gone from it: the owner's tip shown once the mouse rests -
    // at once when another tip is shown - and gone when the owner has none, or the mouse is not over it
    void Track(ibWindow *owner, const wxPoint& at);

    // the mouse pressed or wheeled, a key gone down, the owner left: the tip goes, the rest is waited anew
    void Dismiss();

    // the application's one, made over its top window; none while there is no top window
    static ibToolTipWindow *Get();

private:
    explicit ibToolTipWindow(wxWindow *parent);

    // the rest waited out: the tip laid under the pointer, on the screen it is on
    void ShowTip();

    void OnTimer(wxTimerEvent& event);
    void OnPaint(wxPaintEvent& event);

    wxTimer m_timer;
    wxWeakRef<wxWindow> m_owner;   // the window whose tip is waited for or shown
    wxString m_text;               // its tip, as it was when tracked
    wxPoint m_at;                  // the pointer on the screen, as it was when it rested
};

#endif // _DESKGUI_UNIV_TOOLTIP_H_
