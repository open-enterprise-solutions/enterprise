// The theme's popup on Cocoa (popupWindow.cpp): what only the native window can be told.

#include "wx/wxprec.h"

#include "wx/osx/private.h"

// A popup that does not take the focus leaves the key window where it was: its panel becomes the key window only for
// a view that needs it (a text field does) - wx orders it front without making it key (wxPopupWindow::Show), but a
// click made the panel key, the window it stands over resigned, the view there lost the focus (windowDidResignKey) and
// a menu shut on the press before the release chose the item (2026-11-13, the metadata tree's Add on the Mac)
void ibOSXSetPopupNonActivating(wxWindow* popup)
{
    NSWindow* const window = popup->MacGetTopLevelWindowRef();
    if ( [window isKindOfClass:[NSPanel class]] )
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
}
