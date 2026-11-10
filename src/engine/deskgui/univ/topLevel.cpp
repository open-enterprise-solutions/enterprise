#include <wx/wxprec.h>

#include "deskgui/univ/topLevel.h"

#ifdef __WXMSW__
    #include <wx/msw/private.h>
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
    const HWND hWnd = GetHwndOf(m_window);
    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);

    BOOL cloak = FALSE;
    ::DwmSetWindowAttribute(hWnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
#endif
}
