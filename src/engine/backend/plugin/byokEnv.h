/////////////////////////////////////////////////////////////////////////////
// byokEnv — per-plugin "Bring Your Own Key" secret storage.
//
// Each plugin may have a sibling file <pluginId>.env at
//   $XDG_CONFIG_HOME/OES/plugins/<pluginId>.env    (Linux/macOS)
//   %APPDATA%/OES/plugins/<pluginId>.env           (Windows)
//
// Format: dotenv-style. One KEY=value per line; #-prefixed lines are
// comments; surrounding whitespace trimmed; double-quoted values
// support \n \r \t \" \\ escapes. Other content is ignored.
//
// Permissions enforced on write — mode 0600 on Unix, current-user-only
// ACL on Windows. On read, we WARN (but don't refuse) when permissions
// look too permissive: spec is "tighten it on next save", not
// "refuse to start the host" — agent UX shouldn't break because of a
// file-mode misconfiguration the user can fix from the dialog.
//
// Lookup is keyed by (pluginId, key). Plugins read via the ABI v4
// tail entry ibHostAPI::ReadPluginEnv — the calling pluginId is
// resolved from the host-side thread-local scope (set during
// oes_plugin_initialize + provider Query). Plugins CANNOT read other
// plugins' env files via the public ABI.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_BYOK_ENV_H_
#define _IB_BYOK_ENV_H_

#include "backend/backend.h"

#include <wx/string.h>

#include <string>
#include <unordered_map>

namespace byokEnv {

using KeyMap   = std::unordered_map<std::string, std::string>;
using PluginEnv = std::unordered_map<std::string, KeyMap>; // pluginId → keys

// Absolute path to the env file for the given pluginId.
wxString GetEnvFilePath(const std::string& pluginId);

// Load all <*>.env files in the plugins config dir. Files with too-loose
// permissions log a warning and are loaded anyway (the dialog can
// re-save with correct permissions). Returns the populated map.
PluginEnv LoadAll();

// Write/replace a single plugin's env. Creates parent dirs; enforces
// mode 0600 / current-user-only ACL on the resulting file. Returns
// 0 on success, -1 on error.
int Save(const std::string& pluginId, const KeyMap& keys);

// Convenience: read one key. Returns empty string when absent.
std::string Get(const PluginEnv& env, const std::string& pluginId,
                 const std::string& key);

} // namespace byokEnv

#endif // _IB_BYOK_ENV_H_
