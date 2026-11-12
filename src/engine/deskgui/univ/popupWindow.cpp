#include <wx/wxprec.h>

#include "deskgui/univ/popupWindow.h"

#include <wx/menu.h>

#include <type_traits>

#ifdef __WXMSW__
    #include <wx/msw/private.h>
    #include "deskgui/univ/topLevel.h"   // ibMSWShowShadowed - the popup's shadow, its own
#endif

template <class W>
bool ibPopupWindow<W>::Show(bool show)
{
    if ( !show )
    {
        if ( wxWindow * const frame = m_frame )
            frame->Unbind(wxEVT_MENU_OPEN, &ibPopupWindow::OnFrameMenuOpen, this);
        m_frame = nullptr;
        return W::Show(false);
    }

#ifdef __WXMSW__
    bool shown;
    if ( this->AcceptsFocus() )
    {
        // it takes the keys: shown as wx shows it - activated, and a transient one goes as it loses the activation
        shown = ibMSWShowShadowed(this, [this] { return W::Show(true); });
    }
    else
    {
        // it leaves them to the window it stands over: the window's state first, then the native show that does not
        // activate - topmost, as wx makes a popup (WS_EX_TOPMOST): the shadow is a window of the system's, owned by no
        // one, and among the frame's windows the frame covered it (2026-11-11)
        shown = this->wxWindowBase::Show(true);
        if ( shown )
        {
            const HWND hWnd = reinterpret_cast<HWND>(this->GetHandle());
            ibMSWShowShadowed(this, [hWnd] { ::ShowWindow(hWnd, SW_SHOWNA); return true; });
            ::SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
#else
    const bool shown = W::Show(true);
#endif // __WXMSW__
    if ( !shown )
        return false;

    if ( wxWindow * const frame = wxGetTopLevelParent(this->GetParent()) )
    {
        frame->Bind(wxEVT_MENU_OPEN, &ibPopupWindow::OnFrameMenuOpen, this);
        m_frame = frame;
    }
    return true;
}

template <class W>
void ibPopupWindow<W>::Dismiss()
{
    if constexpr ( std::is_base_of_v<wxPopupTransientWindow, W> )
    {
        W::Dismiss();
    }
    else
    {
        this->Hide();
        OnDismiss();
    }
}

#ifdef __WXMSW__

template <class W>
bool ibPopupWindow<W>::MSWHandleMessage(WXLRESULT *result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    if ( message == WM_MOUSEACTIVATE && !this->AcceptsFocus() )
    {
        *result = MA_NOACTIVATE;
        return true;
    }
    return W::MSWHandleMessage(result, message, wParam, lParam);
}

#endif // __WXMSW__

template <class W>
void ibPopupWindow<W>::OnFrameMenuOpen(wxMenuEvent& event)
{
    // the frame answers too (wxFrameBase::OnMenuOpen) - asked first: the owner told may destroy the popup
    event.Skip();
    Dismiss();
}

template class DESKGUI_API ibPopupWindow<wxPopupWindow>;
template class DESKGUI_API ibPopupWindow<wxPopupTransientWindow>;
