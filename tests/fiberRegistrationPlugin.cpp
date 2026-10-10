// A plugin that registers a fiber local, which the host must refuse.
// Built twice: once so the call is in a static initialiser, once so it
// is in oes_plugin_initialize. The process has to keep running either way.

#include "core/fiber/fiberLocals.h"
#include "backend/plugin/pluginApi.h"

#include <atomic>

namespace {

std::atomic<int> g_saves{ 0 };

void Save(void*)
{
	g_saves.fetch_add(1, std::memory_order_relaxed);
}

void Restore(const void*) {}

#ifndef OES_FIBER_REG_AT_INIT
struct AtLoad {
	AtLoad()
	{
		ibFiberLocals::RegisterTrivial<int>("fiberRegistration.static", &Save, &Restore);
	}
};

AtLoad s_atLoad;
#endif

const ibPluginInfo s_info = {
	IB_PLUGIN_ABI_VERSION,
	"fiberRegistrationPlugin",
	"1",
	"Registers a fiber local. The host must refuse the load.",
	"Open Enterprise Solutions"
};

} // namespace

extern "C" {

OES_PLUGIN_EXPORT const ibPluginInfo* oes_plugin_info(void)
{
	return &s_info;
}

OES_PLUGIN_EXPORT int oes_plugin_initialize(void*)
{
#ifdef OES_FIBER_REG_AT_INIT
	ibFiberLocals::RegisterTrivial<int>("fiberRegistration.init", &Save, &Restore);
#endif
	return 0;
}

OES_PLUGIN_EXPORT int oes_fiber_register_saves(void)
{
	return g_saves.load(std::memory_order_relaxed);
}

} // extern "C"
