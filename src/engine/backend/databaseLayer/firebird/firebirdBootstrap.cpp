#include "firebirdBootstrap.h"

#include "core/fileSystem.h"
#include "core/startProcess.h"   // ibSetEnv

#ifdef __IBMSW__
#include <windows.h>
#endif

#include <mutex>

namespace {

// Process-wide bootstrap state. `std::call_once` guarantees the
// initialiser body runs at most once across all threads; later callers
// block until the first runner finishes, then read the cached result.
// Without call_once two threads racing into Init could each pass the
// `if (s_initialised)` gate and both run SetDllDirectoryW / ibSetEnv
// (especially nasty on POSIX where setenv() is not thread-safe).
std::once_flag s_initOnce;
bool           s_success = false;
ibString       s_fbRuntimeDir;

// Compute the _fb directory path relative to the running executable.
// Returns an empty string if the directory doesn't exist (caller falls
// back to default behaviour).
ibString DetectFbRuntimeDir() {
	// _fb stands in the program folder: beside the executable, or beside the .app when this
	// process is inside a bundle. The same place as lang/ and backend.conf.
	//
	// Missing is tolerated. During early dev / unit tests the directory may not have been
	// laid out, and the default DLL search still finds fbclient next to the executable.
	const ibString candidate = ibProgramFolder() + ibFILE_SEP_PATH + ibT("_fb");
	if (ibDirExists(candidate))
		return candidate;
	return ibEmptyString;
}

void DoInit() {
	s_fbRuntimeDir = DetectFbRuntimeDir();
	if (s_fbRuntimeDir.IsEmpty()) {
		// _fb/ not laid out — let default DLL search handle it.
		s_success = false;
		return;
	}

#ifdef __IBMSW__
	// SetDllDirectory replaces the standard DLL search current-directory
	// entry with our _fb/ path, so LoadLibrary("fbclient.dll") resolves
	// from there. This also affects subsequent dependent-DLL loads
	// inside fbclient (engine13.dll under _fb/plugins/, intl, ICU).
	SetDllDirectoryW(s_fbRuntimeDir.wc_str());
#endif

	// FIREBIRD env var — engine plugin uses this as the root for
	// firebird.conf / firebird.msg / plugins/ / intl/ lookup.
	// Without it the engine searches relative to fbclient.dll which
	// happens to land in _fb/ anyway, but setting it explicitly is
	// resilient against future FB releases changing the default
	// resolution logic.
	ibSetEnv(ibT("FIREBIRD"), s_fbRuntimeDir);

	// FIREBIRD_LOCK — where FB stores fb_lock_* shared-state files for
	// Classic-mode multi-process coordination. Default is %TEMP%
	// which is fine for our scenarios; left commented out as a
	// reference for future per-installation isolation.
	// ibSetEnv(ibT("FIREBIRD_LOCK"), <per-instance path>);

	// FIREBIRD_LOG — log path = `<_fb>/firebird.log`. Same as FB's
	// default when the env var isn't set (next to fbclient.dll), but
	// passed explicitly so the path is stable across FB version
	// upgrades that might change the default resolution.
	ibSetEnv(ibT("FIREBIRD_LOG"), s_fbRuntimeDir + ibFILE_SEP_PATH + ibT("firebird.log"));

	s_success = true;
}

} // namespace

bool ibFirebirdBootstrap::Init() {
	std::call_once(s_initOnce, DoInit);
	return s_success;
}

const ibString& ibFirebirdBootstrap::GetFbRuntimeDir() {
	return s_fbRuntimeDir;
}
