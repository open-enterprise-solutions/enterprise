/////////////////////////////////////////////////////////////////////////////
// Plugin manager implementation.
/////////////////////////////////////////////////////////////////////////////

#include "pluginManager.h"
#include "pluginHost.h"   // ibPluginHostInstance — what initialize() receives

#include "core/fileSystem.h"

#ifdef __IBMSW__
  #include <windows.h>
#endif

namespace {
// RAII: on Windows, silence the OS-level "missing DLL" modal that LoadLibrary
// otherwise pops up for every broken plugin dependency. Other platforms are
// no-ops.
struct ScopedSilenceLoadErrors {
#ifdef __IBMSW__
	UINT m_prev;
	ScopedSilenceLoadErrors()  { m_prev = ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX); }
	~ScopedSilenceLoadErrors() { ::SetErrorMode(m_prev); }
#else
	ScopedSilenceLoadErrors()  = default;
	~ScopedSilenceLoadErrors() = default;
#endif
};
} // namespace

ibString ibPluginManager::GetPluginsDir()
{
	return ibProgramFolder() + ibFILE_SEP_PATH + ibT("plugins");
}

size_t ibPluginManager::LoadAll()
{
	UnloadAll();

	const ibString dir = GetPluginsDir();
	if (!ibDirExists(dir))
		return 0;

	// The platform's dynamic-library suffix — avoids
	// hard-coding ".dll" so the same code works on Linux (.so) / macOS (.dylib).
	const ibString pattern = ibT("*") + ibDynamicLibrary::Extension();

	std::vector<ibString> files;
	ibDir::GetAllFiles(dir, &files, pattern, ibDIR_FILES);

	// RAII, not a value: it silences the loader for its whole scope and restores on the way out.
	// Named only so it HAS a scope — hence maybe_unused rather than a deletion.
	[[maybe_unused]] ScopedSilenceLoadErrors silence;

	for (const ibString& path : files) {

		auto lib = std::make_unique<ibDynamicLibrary>();
		if (!lib->Load(path)) {
			// Quiet in the UI, never quiet altogether. A stray DLL in the folder
			// legitimately fails to load and must not disturb anyone — but so does
			// OUR plugin when a dependency is missing or a symbol went unresolved,
			// and that one used to vanish without a trace. Same file, same branch;
			// only the log tells them apart afterwards.
			ibJournalInfo(ibT("plugin"),"Plugin candidate '%s' did not load - missing dependency or "
				"unresolved symbol; skipped.", path);
			continue;
		}

		// Every unrelated DLL in the folder legitimately does not export our entry point.
		const auto info_fn = reinterpret_cast<ibPluginInfoFn>(lib->GetSymbol(ibT("oes_plugin_info")));
		if (info_fn == nullptr)
			continue; // not one of ours

		const ibPluginInfo* info = info_fn();
		if (info == nullptr || info->abi_version != IB_PLUGIN_ABI_VERSION) {
			ibJournalInfo(ibT("plugin"),"Skipping plugin '%s': ABI mismatch (got %d, expected %d)",
				path, info ? info->abi_version : -1, IB_PLUGIN_ABI_VERSION);
			continue;
		}

		// Optional initialise hook — a non-zero return aborts the load.
		const auto init_fn = reinterpret_cast<ibPluginInitializeFn>(lib->GetSymbol(ibT("oes_plugin_initialize")));
		// THE HOST, not NULL (ABI 2). One C struct with a `query` function; the
		// plugin asks it for the capabilities it needs — see pluginHost.h. A
		// plugin that wanted something the host does not offer is expected to
		// return non-zero here, and it is then unloaded without shutdown being
		// called: it never finished starting, so it has nothing to tear down.
		if (init_fn && init_fn(ibPluginHostInstance()) != 0) {
			ibJournalInfo(ibT("plugin"),"Plugin '%s' initialize() failed", path);
			continue;
		}

		LoadedPlugin p;
		p.m_path = path;
		p.m_lib  = std::move(lib);
		p.m_info = info;
		p.m_shutdown = reinterpret_cast<ibPluginShutdownFn>(p.m_lib->GetSymbol(ibT("oes_plugin_shutdown")));
		ibJournalInfo(ibT("plugin"),"Loaded plugin: %s %s", info->name ? info->name : "<unnamed>",
			info->version ? info->version : "");

		m_plugins.push_back(std::move(p));
	}

	return m_plugins.size();
}

void ibPluginManager::UnloadAll()
{
	// Reverse order — last-loaded first, matches LIFO expectations for any
	// dependencies between plugins.
	for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
		if (it->m_shutdown) {
			try { it->m_shutdown(); } catch (...) { /* plugin bug — don't take the host down */ }
		}
		// m_lib's dtor unloads the DLL.
	}
	m_plugins.clear();
}
