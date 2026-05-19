/////////////////////////////////////////////////////////////////////////////
// Plugin manager — scans <exe-dir>/plugins for .dll/.so files and loads any
// that expose the OES plugin ABI (see pluginApi.h).
//
// ABI v2 brings the ibHostAPI bridge: registered BSL builtins, menu items,
// lifecycle event subscribers, and log messages all flow through the
// manager. Host code drives broadcasts through FireEvent("<name>", payload)
// and consumes the registered set via the getters below.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_MANAGER_H_
#define _IB_PLUGIN_MANAGER_H_

#include "backend/backend.h"
#include "backend/plugin/pluginApi.h"

#include <wx/dynlib.h>
#include <wx/string.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class BACKEND_API ibPluginManager {
public:
	ibPluginManager() = default;
	ibPluginManager(const ibPluginManager&) = delete;
	ibPluginManager& operator=(const ibPluginManager&) = delete;

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

	// Registered BSL/CES builtin — name + function pointer. The system
	// manager iterates this set after plugin load and binds each as a
	// callable identifier inside the script tokenizer.
	struct RegisteredFunction {
		std::string         m_name;
		int                 m_paramCount = -1;
		ibPluginFunctionFn  m_fn = nullptr;
	};

	// Registered Designer Tools → Plugins submenu item. The Designer
	// reads this list after plugin load and appends a menu entry per
	// element with the supplied handler.
	struct RegisteredMenuItem {
		std::string         m_label;
		ibPluginMenuFn      m_handler = nullptr;
	};

	// Scan <exe-dir>/plugins/ and load every DLL that exports the OES
	// ABI. Returns how many plugins were successfully loaded.
	size_t LoadAll();

	// Call each plugin's shutdown export (if any) and unload the library.
	void UnloadAll();

	const std::vector<LoadedPlugin>&        Loaded()       const { return m_plugins;        }
	const std::vector<RegisteredFunction>&  Functions()    const { return m_functions;      }
	const std::vector<RegisteredMenuItem>&  MenuItems()    const { return m_menuItems;      }

	// Fire a lifecycle event. The host calls this from the relevant
	// broadcaster (docManager.OnDocumentSaved, debugServer events, etc).
	// `payload` may be null. Dispatches to every plugin that
	// subscribed to `name` via ibHostAPI::Subscribe. Failures inside
	// plugin callbacks are swallowed — a misbehaving plugin must not
	// take the host process down.
	void FireEvent(const wxString& name, ibPluginValue* payload = nullptr);

	// Directory that will be scanned (<exe-dir>/plugins).
	static wxString GetPluginsDir();

	~ibPluginManager() { UnloadAll(); }

	// Internal — registration entry points invoked through ibHostAPI
	// callbacks. Public so the host-bridge translation unit can call
	// them; not part of the plugin-facing surface.
	int  HostRegisterFunction(const char* name, int paramCount, ibPluginFunctionFn fn);
	int  HostRegisterMenuItem(const char* label, ibPluginMenuFn handler);
	int  HostSubscribe(const char* event, ibPluginEventFn cb);

	// Dispatch a registered plugin function from the script call site.
	// Sets up the arena scope so Make* / GetString calls inside the
	// plugin callback see a live ibPluginCallScope; marshals args by
	// adopting each ibValue into the arena; copies the plugin's return
	// value out before the arena tears down. Returns false when the
	// plugin's callback returns non-zero (mapped to a script error).
	bool CallFunction(const RegisteredFunction& fn,
	                   class ibValue& retOut,
	                   class ibValue** paParams,
	                   long lSizeArray);

	// Same trampoline for menu items — sets up an empty arena so the
	// handler can still call host->Log / MakeString (rare but legal).
	void CallMenuHandler(const RegisteredMenuItem& item);

private:
	std::vector<LoadedPlugin>        m_plugins;
	std::vector<RegisteredFunction>  m_functions;
	std::vector<RegisteredMenuItem>  m_menuItems;
	std::unordered_map<std::string,
	    std::vector<ibPluginEventFn>> m_subscribers;
};

#endif // _IB_PLUGIN_MANAGER_H_
