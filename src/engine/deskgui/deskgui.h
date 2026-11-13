#ifndef __GUI_H__
#define __GUI_H__

#include <wx/wx.h>

#include "core/core.h"   // IB_EXPORT / IB_IMPORT
#include "deskgui/strvararg.h"   // an ibString in wx's varargs is a `%s`

// THE DESKTOP'S USER INTERFACE LIBRARY — what the desktop's programs draw their windows with: the custom-drawn engine
// (the wxUniversal theme engine forked with ib prefixes, and our Luna theme), the forks of wx's generic controls (the
// grid) and ours, the dock art. All of it is drawn by us; wxWidgets below is a cross-platform host only - window
// handles, DCs, fonts, raw input. The thin client's windows (frmclient) and the launcher place it, and codeRunner is to.
// ⭐ THE AIM (Max, 2026-11-06): nothing outside it sees a widget. The programs ask this root's API, and what draws under
// it - univ/ today, the platform's own API one day - changes without them knowing (docs/private/deskgui.md).
#if defined(DESKGUI_EXPORTS)
#define DESKGUI_API IB_EXPORT
#else
#define DESKGUI_API IB_IMPORT
#endif

// A WINDOW'S TEXT IS wx's (Max, 2026-11-10): the core's _() answers an ibString (core/localization/dictionary.h), and
// what draws windows hands its text to wx — formats it, logs it, picks it in a ternary, as the forks of wx's code do.
// So under this library _() is the same translation as wx's string; the literal-only check stays (ibTranslateLiteral).
#undef _
#define _(s) ibTranslateLiteral((s)).ToWxString()

#endif
