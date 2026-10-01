#ifndef __FIREBIRD_BOOTSTRAP_H__
#define __FIREBIRD_BOOTSTRAP_H__

// Firebird runtime bootstrap — points the engine plugin loader at our
// vendored _fb/ subfolder and sets FIREBIRD / FIREBIRD_LOG env vars.
// Idempotent + thread-safe via std::call_once; later calls no-op.
//
// Goals:
//   - User sees only enterprise.exe + a single hidden _fb/ folder
//     next to it; no firebird.conf / firebird.msg / fbclient.dll
//     scattered in the bin root.
//   - fbclient.dll is loaded from _fb/; engine13.dll loads from
//     _fb/plugins/; firebird.msg / intl / icudt resolve through
//     the FIREBIRD env var.
//   - firebird.log writes to _fb/firebird.log — the FB default,
//     just passed explicitly so the path is stable across FB
//     version upgrades that might change resolution rules.
//
// Called early in ibInterfaceFirebird::Init, before any fbclient
// symbol is resolved.

#include "backend/backend.h"

class BACKEND_API ibFirebirdBootstrap {
public:
	// Compute _fb/ path next to the executable, install it as a
	// DLL search directory, set FIREBIRD env var to it, point
	// firebird.log at %LOCALAPPDATA%/OES/logs/.
	//
	// Returns true on success, false if the _fb/ directory isn't
	// where we expect (development build from a non-installed
	// layout — in that case fall back to default Windows DLL
	// search and let the linker find fbclient.dll wherever it is).
	static bool Init();

	// Path to the _fb/ folder once Init has run. Empty string if
	// Init never ran or detection failed.
	static const wxString& GetFbRuntimeDir();
};

#endif // __FIREBIRD_BOOTSTRAP_H__
