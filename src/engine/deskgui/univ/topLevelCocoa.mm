// The theme's top-level window on Cocoa (topLevel.cpp): what only the native window can be told.

#include "wx/wxprec.h"

#include "wx/osx/private.h"

// A TITLE BAR THE SYSTEM HAS AND NOBODY SEES. The window is made without one, the theme painting its own
// (GetNativeStyle) - and a window without one is not what the Mac's buttons ask of it: miniaturize: and zoom: did
// nothing, and there was no full screen (2026-11-15, Max's brother: "the buttons do not do what a Mac user expects").
// The system's given back as the window's style has it - titled, closable, miniaturizable, resizable - under the
// content (full size), the bar clear, its title and its three buttons hidden: the theme's are painted where they stood.
// Moved by the theme's title bar alone (OnMouseMotion), the system not dragging it as well
void ibOSXSetTitleBarHidden(wxWindow* window)
{
    NSWindow* const native = window->MacGetTopLevelWindowRef();
    const long style = window->GetWindowStyleFlag();
    if ( native == nil || !(style & wxCAPTION) )
        return;

    NSWindowStyleMask mask = ([native styleMask] & ~(NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
        NSWindowStyleMaskResizable)) | NSWindowStyleMaskTitled | NSWindowStyleMaskFullSizeContentView;
    if ( style & wxCLOSE_BOX )
        mask |= NSWindowStyleMaskClosable;
    if ( style & wxMINIMIZE_BOX )
        mask |= NSWindowStyleMaskMiniaturizable;
    if ( style & (wxMAXIMIZE_BOX | wxRESIZE_BORDER) )
        mask |= NSWindowStyleMaskResizable;
    [native setStyleMask:mask];

    [native setTitlebarAppearsTransparent:YES];
    [native setTitleVisibility:NSWindowTitleHidden];
    for ( const NSWindowButton button : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton } )
        [[native standardWindowButton:button] setHidden:YES];
    [native setMovable:NO];
}
