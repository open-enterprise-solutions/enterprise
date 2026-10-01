// ORDER MATTERS AND IT IS NOT COSMETIC. pluginHost.h pulls in wx, which pulls in
// winsock2; simplePluginDLL.h pulls in windows.h, which pulls in the ORIGINAL
// winsock. Whichever arrives second redefines what the first declared, and the
// build dies inside the Windows SDK with fifty redefinition errors that say
// nothing about this file. Platform headers last.
#include "backend/plugin/pluginApi.h"
#include "backend/plugin/pluginHost.h"

#include "simplePluginDLL.h"

// Reference OES plugin. Exports the ABI and shows the ONE thing every plugin
// does at startup: ask the host for what it needs, and refuse to load when it
// is not there.
//
// It asks for two capabilities:
//   * diagnostics — to hear every compile / runtime failure as data;
//   * script      — to compile a text and get those failures back directly.
//
// Together these are the language-service surface: a plugin holding both can
// answer "is this code valid, and if not, where exactly" with no UI, no
// debugger attached and nothing executed. That is what a code-generating
// assistant needs from the platform, and it is why these capabilities exist.

static const ibPluginInfo s_info = {
    IB_PLUGIN_ABI_VERSION,
    "simplePlugin",
    "2.0.0",
    "Reference OES plugin - queries host capabilities and listens to diagnostics.",
    "Open Enterprise Solutions"
};

namespace {

// Lives as long as the plugin does, and is unsubscribed in shutdown BEFORE the
// DLL goes away: the host does not track which sink came from which plugin, and
// a sink that outlives its code is a call into freed memory on the next error.
class ibSimpleSink : public ibDiagnosticSink {
public:
    void OnDiagnostic(const ibDiagnostic& diagnostic) override {
        m_lastMessage = diagnostic.m_message;
        m_count++;
    }

    wxString m_lastMessage;
    int      m_count = 0;
};

ibSimpleSink               s_sink;
const ibPluginDiagnostics* s_diagnostics = nullptr;
const ibPluginScript*      s_script = nullptr;

}   // namespace

extern "C" {

OES_PLUGIN_EXPORT const ibPluginInfo* oes_plugin_info(void)
{
    return &s_info;
}

OES_PLUGIN_EXPORT int oes_plugin_initialize(void* host)
{
    const ibPluginHost* pluginHost = static_cast<const ibPluginHost*>(host);
    if (pluginHost == nullptr || pluginHost->query == nullptr)
        return 1;   // a host older than ABI 2, or one with no lookup — cannot work

    s_diagnostics = static_cast<const ibPluginDiagnostics*>(
        pluginHost->query(pluginHost, ibCapabilityDiagnostics, 1));
    s_script = static_cast<const ibPluginScript*>(
        pluginHost->query(pluginHost, ibCapabilityScript, 1));

    // REFUSE RATHER THAN DEGRADE. A plugin that half-loads fails later, somewhere
    // else, as behaviour nobody can explain.
    if (s_diagnostics == nullptr || s_script == nullptr)
        return 1;

    s_diagnostics->Subscribe(&s_sink);
    return 0;   // 0 = success
}

OES_PLUGIN_EXPORT void oes_plugin_shutdown(void)
{
    if (s_diagnostics != nullptr)
        s_diagnostics->Unsubscribe(&s_sink);

    s_diagnostics = nullptr;
    s_script = nullptr;
}

}   // extern "C"

#ifdef __WXMSW__
BOOL APIENTRY DllMain(HANDLE /*hModule*/, DWORD /*ul_reason_for_call*/, LPVOID /*lpReserved*/)
{
    return TRUE;
}
#endif
