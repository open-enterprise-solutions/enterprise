#include "appEnv.h"

// Implementation TU is the only consumer of the heavyweight appData.h
// — keeps the include direction inverted (callers pull appEnv.h,
// which is thin). Subsystem accessors here are out-of-line so a class
// signature change in appData (private field rename, ownership swap)
// doesn't ripple to every callsite.
#include "appData.h"

namespace appEnv {

ibSessionRegistry* SessionRegistry()
{
	return ibApplicationData::GetSessionRegistry();
}

ibConnectionPool* ConnectionPool()
{
	return ibApplicationData::GetConnectionPool();
}

ibLockManager* LockManager()
{
	return ibApplicationData::GetLockManager();
}

ibLogger* Logger()
{
	return ibApplicationData::GetLogger();
}

ibPluginManager* PluginManager()
{
	// No public static accessor on appData yet — pluginManager has only
	// been used from inside appData itself. When an outside caller
	// emerges, promote ibApplicationData::GetPluginManager() and route
	// here. For now we return nullptr defensively (the function exists
	// so the namespace surface is symmetric, not because anyone calls it).
	return nullptr;
}

ibMetaDataConfigurationBase* ActiveMetaData()
{
	return ibApplicationData::GetActiveMetaData();
}

ibDebuggerServer* DebugServer()
{
	// Chained accessor: debugger lives on metadata (no metadata → no
	// debugger). When metadata is moved under appData ownership the
	// chain collapses to one short-circuit-on-null line.
	//
	// TODO: switch to `auto* md = ActiveMetaData(); return md ?
	// md->GetDebugServer() : nullptr;` once metadata refactor lands
	// and ibMetaDataConfigurationBase exposes GetDebugServer().
	return nullptr;
}

ibDebuggerClient* DebugClient()
{
	// Same chain as DebugServer — designer's debug client also lives
	// on metadata. TODO: chain through ActiveMetaData()->GetDebugClient().
	return nullptr;
}

} // namespace appEnv
