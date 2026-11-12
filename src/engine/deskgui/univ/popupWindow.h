#ifndef _DESKGUI_UNIV_POPUPWINDOW_H_
#define _DESKGUI_UNIV_POPUPWINDOW_H_

#include "deskgui/deskgui.h"

#include <wx/popupwin.h>
#include <wx/weakref.h>

class wxMenuEvent;

// THE THEME'S POPUP - one way to be shown and one way to go, for every window the library and its programs pop up: a
// menu, a combo's list, the tooltip, a code editor's lists (Max, 2026-11-12: "our own popup, common, inherited - the
// same behaviour everywhere on our theme"). On MSW it casts the system's drop shadow; one that does not take the focus
// is shown without being activated, the window it stands over keeps the activation and the keys. It goes as a menu of
// its frame opens (ibWindow::DoPopupMenu tells the frame, as the native ports do): the menu takes the keys, not the
// focus, so no focus lost tells it - wx's own canon hides a combo's popup so (wxComboFrameEventHandler). W is
// wxPopupWindow, or wxPopupTransientWindow for one that goes on a click outside it
template <class W>
class ibPopupWindow : public W
{
public:
    ibPopupWindow() = default;
    ibPopupWindow(wxWindow *parent, int flags = wxBORDER_NONE) : W(parent, flags) {}

    virtual bool Show(bool show = true) override;

    // gone: a transient window as wx's goes (wxPopupTransientWindow::Dismiss), a plain one hidden; its owner told
    // (OnDismiss). W's own for a transient window, declared here for a plain one - so not marked override
    virtual void Dismiss();

protected:
    // what the popup's owner is told as it goes
    virtual void OnDismiss() {}

#ifdef __WXMSW__
    // nor does a click activate one that does not take the focus
    virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    // a menu of the frame opens: the popup goes
    void OnFrameMenuOpen(wxMenuEvent& event);

    wxWeakRef<wxWindow> m_frame;   // the frame it listens to while shown
};

// MADE ONCE, IN DESKGUI (popupWindow.cpp), as the frame's and the dialog's are (topLevel.h says why)
#ifdef DESKGUI_EXPORTS
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4661)
#endif // _MSC_VER
extern template class ibPopupWindow<wxPopupWindow>;
extern template class ibPopupWindow<wxPopupTransientWindow>;
#ifdef _MSC_VER
    #pragma warning(pop)
#endif // _MSC_VER
#else
extern template class DESKGUI_API ibPopupWindow<wxPopupWindow>;
extern template class DESKGUI_API ibPopupWindow<wxPopupTransientWindow>;
#endif // DESKGUI_EXPORTS

#endif // _DESKGUI_UNIV_POPUPWINDOW_H_
