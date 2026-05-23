#include "firebirdBootstrap.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

#include <mutex>

namespace {

// Process-wide bootstrap state. `std::call_once` guarantees the
// initialiser body runs at most once across all threads; later callers
// block until the first runner finishes, then read the cached result.
// Without call_once two threads racing into Init could each pass the
// `if (s_initialised)` gate and both run SetDllDirectoryW / wxSetEnv
// (especially nasty on POSIX where setenv() is not thread-safe).
std::once_flag s_initOnce;
bool           s_success = false;
wxString       s_fbRuntimeDir;

// Compute the _fb directory path relative to the running executable.
// Returns empty wxString if the directory doesn't exist (caller falls
// back to default behaviour).
wxString DetectFbRuntimeDir() {
	const wxString exePath = wxStandardPaths::Get().GetExecutablePath();
	wxFileName fn(exePath);
	fn.AppendDir(wxT("_fb"));
	const wxString candidate = fn.GetPath();

	// Verify it exists — during early dev / unit tests the build
	// output may not have been laid out yet, in which case the
	// default DLL search still finds fbclient.dll next to the exe
	// (legacy layout). Tolerate that.
	if (!wxDirExists(candidate))
		return wxEmptyString;

	return candidate;
}

void DoInit() {
	s_fbRuntimeDir = DetectFbRuntimeDir();
	if (s_fbRuntimeDir.IsEmpty()) {
		// _fb/ not laid out — let default DLL search handle it.
		s_success = false;
		return;
	}

#ifdef __WXMSW__
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
	wxSetEnv(wxT("FIREBIRD"), s_fbRuntimeDir);

	// FIREBIRD_LOCK — where FB stores fb_lock_* shared-state files for
	// Classic-mode multi-process coordination. Default is %TEMP%
	// which is fine for our scenarios; left commented out as a
	// reference for future per-installation isolation.
	// wxSetEnv(wxT("FIREBIRD_LOCK"), <per-instance path>);

	// FIREBIRD_LOG — log path = `<_fb>/firebird.log`. Same as FB's
	// default when the env var isn't set (next to fbclient.dll), but
	// passed explicitly so the path is stable across FB version
	// upgrades that might change the default resolution.
	{
		wxFileName logFile(s_fbRuntimeDir, wxT("firebird.log"));
		wxSetEnv(wxT("FIREBIRD_LOG"), logFile.GetFullPath());
	}

	s_success = true;
}

} // namespace

bool ibFirebirdBootstrap::Init() {
	std::call_once(s_initOnce, DoInit);
	return s_success;
}

const wxString& ibFirebirdBootstrap::GetFbRuntimeDir() {
	return s_fbRuntimeDir;
}
