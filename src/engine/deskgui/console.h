#ifndef _DESKGUI_CONSOLE_H_
#define _DESKGUI_CONSOLE_H_

// ibOesConsoleBoot — RAII boot helper for OES console-class binaries.
//
// webserver and appserver don't have a wxApp — they're
// plain `int main(argc, argv)` that uses wx primitives (wxString,
// wx file ops) via wxInitializer. The boot sequence is
// identical in every such binary:
//
//   wxInitializer init(argc, argv);
//   if (!init.IsOk()) { ... return 1; }
//   ibCrashGuard::Install(exeName);
//
// Wrapped here so future console exes (next health-check daemon,
// stress-test driver, etc.) wire in two lines instead of duplicating
// the three. The programs' wx side, beside the window programs' base (app.h), in the desktop's library
// (2026-11-13, Max) - and header-only, so a server that includes it links no deskgui: only the wx base it
// links already, and the core's guard. A stay: once the servers take nothing of wx (the command line,
// wxString, wxFileName), what is left of it is ibCrashGuard::Install, said in their main.
//
// Usage:
//   int main(int argc, char** argv) {
//       ibOesConsoleBoot boot(wxT("appserver"), argc, argv);
//       if (!boot.IsOk()) {
//           std::fprintf(stderr, "wxWidgets failed to initialise\n");
//           return 1;
//       }
//       // ... application body
//   }
//
// Destructor lets wxInitializer's own dtor clean up wx state on exit.

#include "core/diagnostics/crashGuard.h"

#include <wx/init.h>

class ibOesConsoleBoot {
public:
	ibOesConsoleBoot(const ibString& exeName, int argc, char** argv)
		: m_wxInit(argc, argv)
	{
		if (!m_wxInit.IsOk()) return;
		// Install AFTER wx is up so the crash guard's wxString /
		// wxFileName / wxDateTime usage in its handlers (terminate
		// log path, dump filename) has its wx prerequisites ready.
		ibCrashGuard::Install(exeName);
	}

	bool IsOk() const { return m_wxInit.IsOk(); }

	ibOesConsoleBoot(const ibOesConsoleBoot&)            = delete;
	ibOesConsoleBoot& operator=(const ibOesConsoleBoot&) = delete;

private:
	wxInitializer m_wxInit;
};

#endif // _DESKGUI_CONSOLE_H_
