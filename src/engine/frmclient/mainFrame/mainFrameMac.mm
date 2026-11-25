#import <AppKit/AppKit.h>

#include "mainFrame.h"

void ibFrontendMainFrame::Raise()
{
	// A bundle started directly by the studio has not been activated by Launch Services.
	// wx's Raise makes the window key, but leaves its application behind the studio.
	[NSApp activateIgnoringOtherApps:YES];
	ibAuiMDIParentFrame::Raise();
}
