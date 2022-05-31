
#include "keymonitortextctrl.h"
#include "keybinder.h"

IMPLEMENT_CLASS(KeyMonitorTextCtrl, wxTextCtrl)

BEGIN_EVENT_TABLE(KeyMonitorTextCtrl, wxTextCtrl)
    EVT_KEY_DOWN(KeyMonitorTextCtrl::OnKey)
    EVT_KEY_UP(KeyMonitorTextCtrl::OnKey)
END_EVENT_TABLE()

void KeyMonitorTextCtrl::OnKey(wxKeyEvent &event)
{

    // backspace cannot be used as shortcut key...
    if (event.GetKeyCode() == WXK_BACK)
    {

        // this text ctrl contains something and the user pressed backspace...
        // we must delete the keypress...
        Clear();
        return;
    }

    if (event.GetEventType() == wxEVT_KEY_DOWN ||
        (event.GetEventType() == wxEVT_KEY_UP && !IsValidKeyComb()))
    {

        // the user pressed some key combination which must be displayed
        // in this text control.... or he has just stopped pressing a
        // modifier key like shift, ctrl or alt without adding any
        // other alphanumeric char, thus generating an invalid keystroke
        // which must be cleared out...

        KeyBinder::Key key;
        key.code    = event.GetKeyCode();
        key.flags   = event.GetModifiers();

        SetValue(KeyBinder::GetKeyBindingAsText(key));
        SetInsertionPointEnd();

    }

}