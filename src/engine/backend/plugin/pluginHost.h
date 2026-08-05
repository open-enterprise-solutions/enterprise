#ifndef _IB_PLUGIN_HOST_H_
#define _IB_PLUGIN_HOST_H_

/////////////////////////////////////////////////////////////////////////////
// What a plugin can ASK THE HOST FOR — the capabilities behind ibPluginHost.
//
// pluginApi.h is the boundary: plain C, one `query` function, ABI-stable. This
// header is what lies beyond it — ordinary C++ abstract classes, one per
// capability, each with its own name and version so the host can grow (or
// retire) one without disturbing plugins that never asked for it.
//
//   const void* raw = host->query(host, ibCapabilityScript, 1);
//   auto* script = static_cast<const ibPluginScript*>(raw);
//
// WHY CAPABILITIES AND NOT ONE BIG "HOST SERVICES" CLASS. A single class means
// every plugin links against every part of the platform, and adding one method
// changes the layout for all of them. Named capabilities make the dependency
// explicit and the versioning honest: "diagnostics v1" either is what you were
// built against, or you are told NULL and refuse to start.
//
// THE FIRST CONSUMER is the language service (script checking + metadata
// introspection) that an AI assistant talks to — which is why `script` and
// `metadata` are the first two capabilities and why both are read-only.
/////////////////////////////////////////////////////////////////////////////

#include "backend/backend.h"
#include "backend/backend_diagnostic.h"
#include "backend/plugin/pluginApi.h"

#include <vector>

#include <wx/string.h>

// THE HOST ITSELF — one per process, carrying no state, only the capability
// lookup. Declared here rather than in pluginManager.h so the definition in
// pluginHost.cpp sees this declaration: without it the function is compiled
// without the export attribute and nothing outside backend.dll can call it.
BACKEND_API ibPluginHost* ibPluginHostInstance();

// Capability names. Literals rather than an enum: the boundary is C strings,
// and a plugin built a year ago must still be able to ask for "diagnostics"
// without sharing an enumeration with the host.
#define ibCapabilityDiagnostics "diagnostics"
#define ibCapabilityScript      "script"
#define ibCapabilityMetadata    "metadata"

// ---------------------------------------------------------------------------
// diagnostics — listen to failures as data
// ---------------------------------------------------------------------------
class BACKEND_API ibPluginDiagnostics {
public:
	virtual ~ibPluginDiagnostics() = default;

	// Thin wrapper over ibDiagnostics so a plugin does not link the registry
	// directly. Unsubscribe before your DLL is unloaded — the host does not
	// track which sinks came from which plugin.
	virtual void Subscribe(ibDiagnosticSink* sink) const = 0;
	virtual void Unsubscribe(ibDiagnosticSink* sink) const = 0;
};

// ---------------------------------------------------------------------------
// script — compile a module text and report what is wrong with it
// ---------------------------------------------------------------------------
class BACKEND_API ibPluginScript {
public:
	virtual ~ibPluginScript() = default;

	// COMPILE ONLY, NEVER RUN. The text is put through the same compiler the
	// designer uses and thrown away; nothing is stored, no module is replaced,
	// no configuration is touched. Returns the diagnostics produced — empty
	// means the text compiles.
	//
	// `moduleName` is what appears in the messages; pass the name of the module
	// being edited so a report reads the way the designer's would.
	//
	// This is the call that closes the loop for a code-generating assistant: it
	// can find its own mistakes in a language that exists nowhere but here.
	virtual std::vector<ibDiagnostic> Check(const wxString& text,
		const wxString& moduleName = wxT("module")) const = 0;
};

// ---------------------------------------------------------------------------
// metadata — read the shape of the open configuration
// ---------------------------------------------------------------------------
class BACKEND_API ibPluginMetadata {
public:
	virtual ~ibPluginMetadata() = default;

	// True when a configuration is open; everything below answers empty
	// otherwise (a plugin may load long before, or entirely without, one).
	virtual bool IsConfigurationOpen() const = 0;

	// Names of every metaobject of a kind — "Catalog", "Document",
	// "InformationRegister"… Kind names are the ones the metatypes register
	// themselves under, i.e. what a script writes.
	virtual std::vector<wxString> List(const wxString& kind) const = 0;

	// One metaobject as JSON: its properties, attributes with their types,
	// tabular sections. Empty string when not found.
	//
	// JSON because it is what already exists (ibJsonProvider) and because the
	// reader is as likely to be a tool as a person. ⚠ READ ONLY: the JSON view
	// is lossy by design and is not a way back in.
	virtual wxString Describe(const wxString& kind, const wxString& name) const = 0;
};

#endif // _IB_PLUGIN_HOST_H_
