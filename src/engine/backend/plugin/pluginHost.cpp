////////////////////////////////////////////////////////////////////////////
//	Description : the host side of the plugin boundary — capabilities
////////////////////////////////////////////////////////////////////////////
//
// One C struct crosses the DLL boundary (ibPluginHost, pluginApi.h); everything
// a plugin actually uses is requested from it by name. The implementations live
// here, as file-static singletons: they hold no state of their own, they are
// pure doors onto subsystems that already exist.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/plugin/pluginHost.h"
#include "backend/plugin/pluginApi.h"   // ibPluginHost — the C struct this file fills in

#include "backend/backend_diagnostic.h"
#include "backend/compiler/scriptCheck.h"           // ibCheckScript — the door this window opens
#include "backend/metaData.h"
#include "backend/metaCollection/metaIntrospect.h"  // ibListMetaObjectNames / ibDescribeMetaObject
#include "backend/metadataConfiguration.h"

#include <cstring>

namespace {

//---------------------------------------------------------------------------
// diagnostics
//---------------------------------------------------------------------------
class ibPluginDiagnosticsImpl : public ibPluginDiagnostics {
public:
	void Subscribe(ibDiagnosticSink* sink)   const override { ibDiagnostics::Subscribe(sink); }
	void Unsubscribe(ibDiagnosticSink* sink) const override { ibDiagnostics::Unsubscribe(sink); }
};

//---------------------------------------------------------------------------
// script — compile and report, never run
//---------------------------------------------------------------------------
class ibPluginScriptImpl : public ibPluginScript {
public:
	std::vector<ibDiagnostic> Check(const wxString& text, const wxString& moduleName) const override
	{
		// A WINDOW, NOT THE MECHANISM. The body used to live here, which made
		// this boundary the owner of the compile-and-report door; it has a
		// second consumer now (the MCP server in the core), so it moved next to
		// the compiler and both call the one door.
		return ibCheckScript(text, moduleName);
	}
};

//---------------------------------------------------------------------------
// metadata — read the shape of what is open
//---------------------------------------------------------------------------
class ibPluginMetadataImpl : public ibPluginMetadata {
public:
	bool IsConfigurationOpen() const override
	{
		return activeMetaData != nullptr && activeMetaData->IsConfigOpen();
	}

	std::vector<wxString> List(const wxString& kind) const override
	{
		// The plugin has no configuration of its own to name, so the window
		// answers about the active one — see metaCollection/metaIntrospect.h for
		// why the mechanism itself refuses to make that assumption.
		return ibListMetaObjectNames(activeMetaData, kind);
	}

	wxString Describe(const wxString& kind, const wxString& name) const override
	{
		return ibDescribeMetaObject(activeMetaData, kind, name);
	}
};

//---------------------------------------------------------------------------
// the boundary itself
//---------------------------------------------------------------------------

const ibPluginDiagnosticsImpl s_diagnostics;
const ibPluginScriptImpl      s_script;
const ibPluginMetadataImpl    s_metadata;

const void* HostQuery(const ibPluginHost* /*self*/, const char* capability, int version)
{
	if (capability == nullptr)
		return nullptr;

	// VERSION IS CHECKED PER CAPABILITY, not globally: the point of naming them
	// is that "metadata" can reach v2 while "diagnostics" stays v1, and a plugin
	// that only ever asked for the second keeps working untouched.
	if (std::strcmp(capability, ibCapabilityDiagnostics) == 0 && version == 1)
		return static_cast<const ibPluginDiagnostics*>(&s_diagnostics);
	if (std::strcmp(capability, ibCapabilityScript) == 0 && version == 1)
		return static_cast<const ibPluginScript*>(&s_script);
	if (std::strcmp(capability, ibCapabilityMetadata) == 0 && version == 1)
		return static_cast<const ibPluginMetadata*>(&s_metadata);

	// Unknown name or wrong version — NULL, and the plugin decides what that
	// means for it. Guessing on the host side would hand a plugin a layout it
	// was not built against, which is the one failure nobody could debug.
	return nullptr;
}

// NOT "s_host": winsock2.h defines s_host as a macro (in_addr::S_un.S_un_b.s_b2),
// and wx drags winsock in. The identifier compiles into someone else's struct
// member and the errors point at the Windows SDK, not at this line.
ibPluginHost s_pluginHost = { IB_PLUGIN_ABI_VERSION, &HostQuery };

} // namespace

ibPluginHost* ibPluginHostInstance()
{
	return &s_pluginHost;
}
