/////////////////////////////////////////////////////////////////////////////
// pluginInstaller — extract / install / uninstall plugin .zip bundles.
//
// Bundle layout:
//   manifest.json         { pluginId, version, abiVersion, binary }
//   <binary>              e.g. template-provider.dylib
//   webview/…             optional WebView HTML/JS for the AI chat pane
//
// manifest.json schema:
//   {
//     "pluginId":   "template-provider",   // required, must match real plugin id
//     "version":    "0.3.1",               // required, dotted-decimal
//     "abiVersion": 4,                     // required, integer
//     "binary":     "template-provider.dylib" // required, relative to bundle root
//   }
//
// Install target:
//   GetPluginsDir() / <pluginId> / <version> /  ← extracted contents
//   GetPluginsDir() / <pluginId> / current   →  symlink/copy of <version>
//
// Atomicity: extracts into a temp sibling dir first, then renames to the
// versioned dir. An interrupted install leaves the existing version
// intact. On macOS / Linux `current` is a relative symlink; on Windows
// we recopy because junction-point support is inconsistent across NTFS
// configurations.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_INSTALLER_H_
#define _IB_PLUGIN_INSTALLER_H_

#include "backend/backend.h"

#include <wx/string.h>

#include <string>

namespace pluginInstaller {

struct Manifest {
	std::string pluginId;
	std::string version;
	int         abiVersion = 0;
	std::string binary;
};

enum class InstallResult {
	OK,
	BadZip,         // file isn't a valid zip / no manifest.json inside
	BadManifest,    // manifest.json missing required fields or wrong shape
	AbiUnsupported, // manifest.abiVersion outside the host's supported range
	IoError,        // disk full / permission denied during extract
	AlreadyExists,  // same version already installed; caller can prompt overwrite
};

// Read the manifest.json from a .zip into out_manifest. Returns false
// when the file isn't a zip or the manifest is malformed. errorMsg
// (optional) carries a diagnostic.
bool ReadManifest(const wxString& zipPath, Manifest& out_manifest,
                   wxString* errorMsg = nullptr);

// Install a .zip. Honours allowOverwrite for the same-version case.
// Returns OK on success, otherwise the failure tier — caller renders
// the right diagnostic. errorMsg (optional) carries the underlying
// reason text.
InstallResult Install(const wxString& zipPath, bool allowOverwrite,
                       wxString* errorMsg = nullptr);

// Uninstall a plugin: removes <pluginsDir>/<pluginId> entirely + drops
// the matching .env (if any). Does NOT touch plugins.json5 — caller
// removes the entry there separately so policy history can survive a
// short-lived uninstall + reinstall.
// Returns 0 on success, -1 on partial / total failure (with log lines
// covering each step).
int Uninstall(const std::string& pluginId);

} // namespace pluginInstaller

#endif // _IB_PLUGIN_INSTALLER_H_
