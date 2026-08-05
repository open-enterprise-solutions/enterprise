/////////////////////////////////////////////////////////////////////////////
// Plugin manager — scans <exe-dir>/plugins for .dll/.so files and loads any
// that expose the OES plugin ABI (see pluginApi.h).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_MANAGER_H_
#define _IB_PLUGIN_MANAGER_H_

#include "backend/backend.h"
#include "backend/appDataCtorToken.h"
#include "backend/plugin/pluginApi.h"


#include <wx/dynlib.h>
#include <memory>
#include <vector>

class BACKEND_API ibPluginManager {
public:
	ibPluginManager(const ibPluginManager&) = delete;
	ibPluginManager& operator=(const ibPluginManager&) = delete;

	// Construction restricted to ibApplicationData via the
	// ib::AppDataCtorToken gate — same pattern as the other
	// appData-owned subsystems. Production callers reach the plugin
	// manager through `ibApplicationData::m_pluginManager` (no public
	// static accessor today — appData uses it directly during startup).
	explicit ibPluginManager(ib::AppDataCtorToken) {}


	struct LoadedPlugin {
		wxString               m_path;
		std::unique_ptr<wxDynamicLibrary> m_lib;
		const ibPluginInfo*    m_info = nullptr;
		ibPluginShutdownFn     m_shutdown = nullptr;

		LoadedPlugin() = default;
		LoadedPlugin(LoadedPlugin&&) = default;
		LoadedPlugin& operator=(LoadedPlugin&&) = default;
		LoadedPlugin(const LoadedPlugin&) = delete;
		LoadedPlugin& operator=(const LoadedPlugin&) = delete;
	};

	// Scan <exe-dir>/plugins/ and load every DLL that exports the OES ABI.
	// Returns how many plugins were successfully loaded.
	size_t LoadAll();

	// Call each plugin's shutdown export (if any) and unload the library.
	void UnloadAll();

	const std::vector<LoadedPlugin>& Loaded() const { return m_plugins; }

	// Directory that will be scanned (<exe-dir>/plugins). Exposed so UI can
	// display it and a user can be told where to drop new plugins.
	static wxString GetPluginsDir();

	~ibPluginManager() { UnloadAll(); }

private:
	std::vector<LoadedPlugin> m_plugins;
};

#endif // _IB_PLUGIN_MANAGER_H_
