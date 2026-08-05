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
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/fileSystem/fs.h"            // ibWriterMemory — the JSON sink
#include "backend/metaData.h"
#include "backend/objCtor.h"                 // ibCtorMetaValueType — full type, for the JSON type resolver
#include "backend/metadataConfiguration.h"
#include "backend/serialize/dataBuilder.h"    // ibDataNode
#include "backend/serialize/jsonProvider.h"

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
		// A LOCAL SINK, not a member: Check may be called from several threads
		// (a language service serving an editor and a build step at once), and
		// a shared collector would mix their answers.
		class Collector : public ibDiagnosticSink {
		public:
			void OnDiagnostic(const ibDiagnostic& diagnostic) override { m_found.push_back(diagnostic); }
			std::vector<ibDiagnostic> m_found;
		} collector;

		ibDiagnostics::Subscribe(&collector);

		try {
			// COMPILED AND DISCARDED. The compiler is constructed here and dies
			// here: nothing is registered, no module is replaced, the open
			// configuration never learns this happened.
			ibCompileCode compiler(moduleName, wxT("check"), false);
			compiler.Compile(text);
		}
		catch (const ibBackendException&) {
			// The refusal is already IN the collector — DoSetError publishes the
			// record and then throws. Nothing to add here.
		}
		catch (...) {
			// Anything else means the compiler failed in a way it does not
			// describe; better an empty answer than a plugin taking the process
			// down through a DLL boundary.
		}

		ibDiagnostics::Unsubscribe(&collector);
		return collector.m_found;
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
		std::vector<wxString> names;

		const ibClassID clsid = ResolveKind(kind);
		if (clsid == 0)
			return names;

		for (const ibValueMetaObject* object : activeMetaData->GetAnyArrayObject<ibValueMetaObject>(clsid))
			if (object != nullptr)
				names.push_back(object->GetName());

		return names;
	}

	wxString Describe(const wxString& kind, const wxString& name) const override
	{
		const ibClassID clsid = ResolveKind(kind);
		if (clsid == 0)
			return wxEmptyString;

		ibValueMetaObject* found = nullptr;
		for (ibValueMetaObject* object : activeMetaData->GetAnyArrayObject<ibValueMetaObject>(clsid)) {
			if (object != nullptr && object->GetName() == name) {
				found = object;
				break;
			}
		}

		if (found == nullptr)
			return wxEmptyString;

		// The SAME node tree the binary format serialises, rendered as JSON:
		// one description of the object, two ways of writing it down. The type
		// resolver turns config-specific class ids back into portable names
		// ("CatalogRef.Номенклатура"), which is the half that makes the answer
		// readable by anything other than this process.
		ibDataNode node;
		if (!found->BuildDataNode(node))
			return wxEmptyString;

		ibJsonProvider provider;
		provider.SetTypeResolver([](ibClassID clsid) -> wxString {
			const ibCtorMetaValueType* typeCtor = activeMetaData != nullptr
				? activeMetaData->GetTypeCtor(clsid) : nullptr;
			return typeCtor != nullptr ? typeCtor->GetClassName() : wxString();
		});

		ibWriterMemory writer;
		if (!provider.Write(node, writer))
			return wxEmptyString;

		return wxString::FromUTF8(reinterpret_cast<const char*>(writer.pointer()), writer.size());
	}

private:

	// KIND NAME → CLASS ID, through the one registry every metatype registers
	// itself in (METADATA_TYPE_REGISTER → ibValue::RegisterCtor). So "Catalog"
	// here is the same "Catalog" a configuration writes, by construction rather
	// than by a table somebody has to keep in step.
	//
	// Returns 0 when there is no configuration open, the name is unknown, or the
	// name belongs to something that is not a metatype (a value class, a
	// control) — asking for "Array" must not return every array in the tree.
	static ibClassID ResolveKind(const wxString& kind)
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen())
			return 0;

		const ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(kind);
		if (ctor == nullptr)
			return 0;
		if (ctor->GetObjectTypeCtor() != ibCtorObjectType::ibCtorObjectType_object_metadata)
			return 0;

		return ctor->GetClassType();
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
