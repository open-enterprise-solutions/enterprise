#include "simplePluginDLL.h"
#include "backend/plugin/pluginApi.h"

// Minimal OES plugin. Exports the required ABI so ibPluginManager recognises
// the DLL; otherwise does nothing. Use as a starting template for new plugins.

static const ibPluginInfo s_info = {
    IB_PLUGIN_ABI_VERSION,
    "simplePlugin",
    "1.0.0",
    "Example OES plugin - exports the ABI and logs on init.",
    "Open Enterprise Solutions"
};

extern "C" {

OES_PLUGIN_EXPORT const ibPluginInfo* oes_plugin_info(void)
{
    return &s_info;
}

OES_PLUGIN_EXPORT int oes_plugin_initialize(void* /*hostContext*/)
{
    return 0;   // 0 = success
}

OES_PLUGIN_EXPORT void oes_plugin_shutdown(void)
{
}

}   // extern "C"

#ifdef __WXMSW__
BOOL APIENTRY DllMain(HANDLE /*hModule*/, DWORD /*ul_reason_for_call*/, LPVOID /*lpReserved*/)
{
    return TRUE;
}
#endif