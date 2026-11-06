#ifndef __GUI_H__
#define __GUI_H__

#include <wx/wx.h>

#include "core/core.h"   // IB_EXPORT / IB_IMPORT

// THE DESKTOP'S USER INTERFACE LIBRARY — what the desktop's programs draw their windows with: the custom-drawn engine
// (the wxUniversal theme engine forked with ib prefixes, and our Luna theme), the forks of wx's generic controls (the
// grid) and ours, the dock art. All of it is drawn by us; wxWidgets below is a cross-platform host only - window
// handles, DCs, fonts, raw input. The thin client's windows (frmclient) and the launcher place it, and codeRunner is to.
// ⭐ THE AIM (Max, 2026-11-06): nothing outside it sees a widget. The programs ask this root's API, and what draws under
// it - univ/ today, the platform's own API one day - changes without them knowing (docs/private/gui.md).
#if defined(GUI_EXPORTS)
#define GUI_API IB_EXPORT
#else
#define GUI_API IB_IMPORT
#endif

#endif
