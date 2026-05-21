/////////////////////////////////////////////////////////////////////////////
// pluginsConfig — persistent per-plugin settings stored at
// $XDG_CONFIG_HOME/oes/plugins/plugins.json5 (Linux/macOS) or
// %APPDATA%/OES/plugins/plugins.json5 (Windows).
//
// Schema (JSON5-compatible JSON — we only parse the JSON subset):
//
//   {
//     "plugins": {
//       "pugi-oes-bridge": {
//         "enabled":  true,
//         "endpoint": "https://app.pugi.io/api/v1/oes-mcp/invoke",
//         "byokRef":  "pugi.env"          // optional, see byokEnv.h
//       },
//       "simplePlugin": { "enabled": false }
//     },
//     "policy": {
//       "pugi-oes-bridge": {
//         "*":            "AllowAlways",   // wildcard
//         "meta.delete":  "Ask"            // override per op
//       }
//     }
//   }
//
// Mutation policies persist here (AllowAlways tier of the Phase 3.2
// 4-mode permission gate). Session-scoped allowances stay in RAM.
//
// Phase 4.1 ships the loader only; the saver lands with the Tools →
// Plugins dialog (Phase 4.3).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGINS_CONFIG_H_
#define _IB_PLUGINS_CONFIG_H_

#include "backend/backend.h"
#include "backend/plugin/pluginManager.h"

#include <wx/string.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace pluginsConfig {

struct PluginEntry {
	bool      enabled  = true;
	wxString  endpoint;
	wxString  byokRef;
};

struct PolicyEntry {
	// Map op name → policy. "*" matches the wildcard tier.
	std::unordered_map<std::string, ibPluginManager::MutationPolicy> ops;
};

struct SlashCommandEntry {
	wxString name;
	wxString description;
	wxString prompt;
};

struct Snapshot {
	// pluginId → PluginEntry
	std::unordered_map<std::string, PluginEntry> plugins;
	// pluginId → PolicyEntry
	std::unordered_map<std::string, PolicyEntry> policies;
	// User-defined slash commands shown by the native chat pane.
	std::vector<SlashCommandEntry> slashCommands;
};

// Absolute path to plugins.json5 (creates parent dirs if absent —
// pure utility, doesn't touch the file). Honours OES_PLUGIN_CONFIG_DIR
// environment override for tests / offline installs.
wxString GetConfigPath();

// Load the file. Returns an empty Snapshot when the file is absent,
// malformed, or unreadable — a fresh install reads as "no overrides,
// everything default-enabled".
Snapshot Load();

// Apply a loaded snapshot to a plugin manager: skips disabled plugins
// during LoadAll + writes policies via SetMutationPolicy. Called from
// ibPluginManager::LoadAll() before the DLL scan.
void Apply(const Snapshot& snap, ibPluginManager& pm);

// Test hook — checks whether the given pluginId is enabled per snapshot.
// Absent entries treat as enabled (opt-out default).
bool IsEnabled(const Snapshot& snap, const std::string& pluginId);

// Serialise + persist a snapshot back to plugins.json5. Creates parent
// dirs as needed. Atomic via temp-file + rename so an interrupted write
// can't corrupt the live config. Returns 0 on success, -1 on error.
int Save(const Snapshot& snap);

} // namespace pluginsConfig

#endif // _IB_PLUGINS_CONFIG_H_
