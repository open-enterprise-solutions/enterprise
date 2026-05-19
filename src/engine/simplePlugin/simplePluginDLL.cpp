/////////////////////////////////////////////////////////////////////////////
// simplePlugin — ABI v2 demonstration.
//
// Exercises every entry point of the ibHostAPI surface so future plugin
// authors have a concrete reference:
//   - RegisterFunction("PluginEcho", …)
//   - RegisterMenuItem("Plugin Echo Demo", …)
//   - Subscribe("DocumentSaved", …)
//   - Log(...)
/////////////////////////////////////////////////////////////////////////////

#include "simplePluginDLL.h"
#include "backend/plugin/pluginApi.h"

#include <cstdio>
#include <cstring>

namespace {

// Captured for the lifetime of the plugin so callbacks can call back
// into the host without re-receiving the pointer. const-correct against
// the host's ibHostAPI guarantee.
const ibHostAPI* g_host = nullptr;

// BSL/CES builtin: `PluginEcho(<x>)` — returns the first argument
// converted to a string. Demonstrates argument inspection + return
// value via the host marshalling helpers.
int PluginEcho(ibPluginValue** args, int argc, ibPluginValue** ret)
{
    if (g_host == nullptr) return -1;
    if (argc <= 0 || args == nullptr || args[0] == nullptr) {
        *ret = g_host->MakeString("");
        return 0;
    }
    if (g_host->IsNull(args[0])) {
        *ret = g_host->MakeNull();
        return 0;
    }
    const char* s = g_host->GetString(args[0]);
    *ret = g_host->MakeString(s ? s : "");
    return 0;
}

// Designer Tools → Plugins → "Plugin Echo Demo".
void OnMenuClick(void)
{
    if (g_host) g_host->Log("simplePlugin: menu item clicked", 0);
}

// Fires whenever any document is saved.
void OnDocumentSaved(const char* event, ibPluginValue* /*payload*/)
{
    if (g_host == nullptr) return;
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "simplePlugin: event '%s' received", event ? event : "?");
    g_host->Log(buf, 0);
}

} // namespace

static const ibPluginInfo s_info = {
    IB_PLUGIN_ABI_VERSION,
    "simplePlugin",
    "2.0.0",
    "Example OES plugin — registers a builtin, a menu item, and an event.",
    "Open Enterprise Solutions"
};

extern "C" {

OES_PLUGIN_EXPORT const ibPluginInfo* oes_plugin_info(void)
{
    return &s_info;
}

OES_PLUGIN_EXPORT int oes_plugin_initialize(const ibHostAPI* host)
{
    g_host = host;
    if (g_host == nullptr) {
        // Host is ABI v1 or running in a mode that doesn't expose the
        // bridge — plugin can still load passively.
        return 0;
    }

    g_host->Log("simplePlugin: initializing (ABI v2)", 0);
    g_host->RegisterFunction("PluginEcho",        &PluginEcho);
    g_host->RegisterMenuItem("Plugin Echo Demo",  &OnMenuClick);
    g_host->Subscribe       ("DocumentSaved",      &OnDocumentSaved);
    return 0;
}

OES_PLUGIN_EXPORT void oes_plugin_shutdown(void)
{
    if (g_host) g_host->Log("simplePlugin: shutting down", 0);
    g_host = nullptr;
}

}   // extern "C"

#ifdef __WXMSW__
#include <windows.h>
BOOL APIENTRY DllMain(HANDLE /*hModule*/, DWORD /*ul_reason_for_call*/, LPVOID /*lpReserved*/)
{
    return TRUE;
}
#endif
