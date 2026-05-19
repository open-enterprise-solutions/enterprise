/////////////////////////////////////////////////////////////////////////////
// metaBridge implementation — see header for the contract.
//
// Phase 3.1 scope: read-only MetaQuery against the live activeMetaData
// tree. Resolves "Catalog.<Name>" / "Document.<Name>" / etc. to the
// matching metaobject under GetCommonMetaObject(), serialises its
// shallow shape (name, synonym, comment, attributes, tabular sections)
// to JSON via nlohmann. Mutation paths land in subsequent commits.
/////////////////////////////////////////////////////////////////////////////

#include "metaBridge.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/compiler/value.h"
#include "backend/plugin/pluginApi.h"
#include "backend/plugin/pluginManager.h"

#include "3rdparty/nlohmann/json.hpp"

#include <functional>
#include <vector>

#include <wx/string.h>
#include <wx/thread.h>
#include <wx/log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace metaBridge {
namespace {

// Map plugin-facing kind label → CLSID. The strings here mirror the
// public XML / JSON serialisation tags (CLAUDE.md §Metadata Object Types)
// so the same vocabulary works for agents, config import/export, and
// human-authored tooling. Keys are stored canonical-case + lower-case so
// case-insensitive lookups land — agent prompts produce mixed case in
// the wild; refusing "catalog" but accepting "Catalog" hurts agent DX.
const std::unordered_map<std::string, unsigned long long>& KindMap()
{
	static const std::unordered_map<std::string, unsigned long long> map = {
		{ "constant",                   g_metaConstantCLSID                   },
		{ "catalog",                    g_metaCatalogCLSID                    },
		{ "document",                   g_metaDocumentCLSID                   },
		{ "enumeration",                g_metaEnumerationCLSID                },
		{ "dataprocessor",              g_metaDataProcessorCLSID              },
		{ "report",                     g_metaReportCLSID                     },
		{ "informationregister",        g_metaInformationRegisterCLSID        },
		{ "accumulationregister",       g_metaAccumulationRegisterCLSID       },
		{ "chartofcharacteristictypes", g_metaChartOfCharacteristicTypesCLSID },
		{ "chartofaccounts",            g_metaChartOfAccountsCLSID            },
		{ "accountingregister",         g_metaAccountingRegisterCLSID         },
	};
	return map;
}

// Canonical-case labels mirrored back in serialised JSON output. Keyed
// by CLSID so we don't have to re-lower-case every emitted entry. Same
// table as KindMap() but with display-case values + CLSID keys.
const std::unordered_map<unsigned long long, std::string>& CLSIDToKindMap()
{
	static const std::unordered_map<unsigned long long, std::string> map = {
		{ g_metaConstantCLSID,                   "Constant"                    },
		{ g_metaCatalogCLSID,                    "Catalog"                     },
		{ g_metaDocumentCLSID,                   "Document"                    },
		{ g_metaEnumerationCLSID,                "Enumeration"                 },
		{ g_metaDataProcessorCLSID,              "DataProcessor"               },
		{ g_metaReportCLSID,                     "Report"                      },
		{ g_metaInformationRegisterCLSID,        "InformationRegister"         },
		{ g_metaAccumulationRegisterCLSID,       "AccumulationRegister"        },
		{ g_metaChartOfCharacteristicTypesCLSID, "ChartOfCharacteristicTypes"  },
		{ g_metaChartOfAccountsCLSID,            "ChartOfAccounts"             },
		{ g_metaAccountingRegisterCLSID,         "AccountingRegister"          },
	};
	return map;
}

// Reverse lookup — O(1) via the precomputed map above.
std::string CLSIDToKindString(unsigned long long clsid)
{
	const auto& map = CLSIDToKindMap();
	auto it = map.find(clsid);
	return it == map.end() ? std::string() : it->second;
}

std::string LowerAscii(const std::string& s)
{
	std::string out(s);
	std::transform(out.begin(), out.end(), out.begin(),
	               [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
	return out;
}

// Allocate `s` via malloc so the plugin caller can free() it per
// the ABI contract. Returns nullptr on OOM (the trampoline will then
// surface -1 to the plugin).
char* StrdupForPlugin(const std::string& s)
{
	const size_t n = s.size() + 1;
	void* mem = std::malloc(n);
	if (mem == nullptr) return nullptr;
	std::memcpy(mem, s.c_str(), n);
	return static_cast<char*>(mem);
}

void SetError(char** errorMsg, const std::string& msg)
{
	if (errorMsg != nullptr) {
		*errorMsg = StrdupForPlugin(msg);
	}
}

// Split "Kind.Name" → (kind, name). Returns false when the input is
// missing the separator or either side is empty.
bool SplitFullName(const char* fullName, std::string& kind, std::string& name)
{
	if (fullName == nullptr || *fullName == '\0') return false;
	const char* dot = std::strchr(fullName, '.');
	if (dot == nullptr || dot == fullName || *(dot + 1) == '\0') return false;
	kind.assign(fullName, dot - fullName);
	name.assign(dot + 1);
	return true;
}

// Build a shallow JSON view of an attribute-like child (Attribute,
// PredefinedAttribute, Dimension, Resource — any metaObject leaf that
// carries name + synonym). We intentionally skip type qualifiers in
// Phase 3.1; the agent's editor primitives will surface those once
// MetaEdit needs them.
nlohmann::json SerializeChildShallow(const ibValueMetaObject& child)
{
	nlohmann::json j;
	j["name"]    = std::string(child.GetName().utf8_str());
	j["synonym"] = std::string(child.GetSynonym().utf8_str());
	const std::string kind = CLSIDToKindString(child.GetClassType());
	if (!kind.empty()) j["kind"] = kind;
	return j;
}

// Build the full JSON document for a single metaobject. Top-level fields
// match the agent prompt vocabulary in docs/sigma-agent-prompt-patterns.md.
nlohmann::json SerializeObject(const ibValueMetaObject& obj,
                                const std::string& fullName,
                                const std::string& kind)
{
	nlohmann::json j;
	j["fullName"] = fullName;
	j["kind"]     = kind;
	j["name"]     = std::string(obj.GetName().utf8_str());
	j["synonym"]  = std::string(obj.GetSynonym().utf8_str());

	// Children are the only nested shape we surface in Phase 3.1. The
	// caller (an LLM agent) walks the array; deeper introspection
	// happens via a subsequent MetaQuery on the child's full name.
	nlohmann::json children = nlohmann::json::array();
	std::vector<ibValueMetaObject*> all = obj.GetAnyArrayObject<>();
	for (const ibValueMetaObject* child : all) {
		if (child == nullptr) continue;
		children.push_back(SerializeChildShallow(*child));
	}
	j["children"] = std::move(children);
	return j;
}

} // namespace

unsigned long long KindStringToCLSID(const char* kind)
{
	if (kind == nullptr || *kind == '\0') return 0;
	const auto& map = KindMap();
	auto it = map.find(LowerAscii(std::string(kind)));
	if (it == map.end()) return 0;
	return it->second;
}

int HostMetaQuery(const char* fullName,
                   const char* fieldsFilter,
                   char**      jsonOut,
                   char**      errorMsg)
{
	if (jsonOut != nullptr) *jsonOut = nullptr;
	if (errorMsg != nullptr) *errorMsg = nullptr;

	// Thread contract — activeMetaData and its child tree may be
	// concurrently mutated by Designer UI events (form edits, paste,
	// debugger session start). Walking the tree from a plugin worker
	// thread would TOCTOU-race those mutations and produce dangling
	// pointer reads. Mirror the wxIsMainThread guard pattern from
	// CallWebPaneRegister; agent calls land on the UI thread already
	// thanks to the WebView message marshal.
	if (!wxIsMainThread()) {
		wxLogWarning(wxT("metaBridge::HostMetaQuery called off main thread — refused"));
		SetError(errorMsg, "MetaQuery: must be called from the UI thread");
		return -1;
	}

	// Phase 3.1 does not yet honour field projection — fail loud so a
	// caller that depends on it sees the gap immediately instead of
	// silently receiving the full payload.
	if (fieldsFilter != nullptr && *fieldsFilter != '\0') {
		SetError(errorMsg, "MetaQuery: fieldsFilter not yet supported (Phase 3.1)");
		return -1;
	}

	std::string kind, name;
	if (!SplitFullName(fullName, kind, name)) {
		SetError(errorMsg, "MetaQuery: fullName must be '<Kind>.<Name>'");
		return -1;
	}

	const unsigned long long clsid = KindStringToCLSID(kind.c_str());
	if (clsid == 0) {
		SetError(errorMsg, "MetaQuery: unknown kind '" + kind + "'");
		return -1;
	}

	if (activeMetaData == nullptr) {
		SetError(errorMsg, "MetaQuery: no configuration loaded");
		return -1;
	}
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) {
		SetError(errorMsg, "MetaQuery: configuration has no root");
		return -1;
	}

	// Single snapshot of root's children → no TOCTOU between membership
	// scan and serialisation. We hold the std::vector locally for the
	// rest of the call; the pointed-to objects remain valid because we
	// are on the UI thread (see guard above) and no recursive call to
	// the Designer reaches here before we return.
	const std::vector<ibValueMetaObject*> topLevel = root->GetAnyArrayObject<>();
	const wxString needle = wxString::FromUTF8(name.c_str());
	ibValueMetaObject* hit = nullptr;
	for (ibValueMetaObject* child : topLevel) {
		if (child == nullptr) continue;
		if (child->GetClassType() != static_cast<ibClassID>(clsid)) continue;
		if (child->GetName() != needle) continue;
		hit = child;
		break;
	}
	if (hit == nullptr) {
		SetError(errorMsg, "MetaQuery: not found '" + std::string(fullName) + "'");
		return -1;
	}

	// Skip the dump+strdup if the caller didn't want the buffer — saves
	// an allocation pair and the JSON serialisation pass entirely.
	if (jsonOut == nullptr) return 0;

	try {
		nlohmann::json j = SerializeObject(*hit, std::string(fullName),
		                                     CLSIDToKindString(static_cast<unsigned long long>(clsid)));
		const std::string dump = j.dump();
		char* buf = StrdupForPlugin(dump);
		if (buf == nullptr) {
			SetError(errorMsg, "MetaQuery: out of memory");
			return -1;
		}
		*jsonOut = buf;
		return 0;
	} catch (const std::exception& e) {
		// Generic error to plugin (no internal library leak); detailed
		// reason to host log for our own debugging.
		wxLogWarning(wxT("metaBridge::HostMetaQuery serialisation threw: %s"),
		             wxString::FromUTF8(e.what()));
		SetError(errorMsg, "MetaQuery: internal serialisation error");
		return -1;
	}
}

} // namespace metaBridge

// ===========================================================================
// Phase 3.2 — MetaCreate / MetaEdit / MetaDelete + undo stack
// ===========================================================================
namespace metaBridge {
namespace {

// Undo stack — Designer Ctrl+Z integration lands in Phase 3.3 via the
// active document's wxCommandProcessor. For now we hold the lambdas
// in-process. Vector, not stack adapter, so tests can introspect size.
// Not thread-safe — by contract all Meta* trampolines are main-thread
// only (asserted via wxIsMainThread elsewhere).
std::vector<std::function<void()>> g_undoStack;

bool RequireMainThread(char** errorMsg)
{
	if (!wxIsMainThread()) {
		wxLogWarning(wxT("metaBridge: mutation called off main thread — refused"));
		SetError(errorMsg, "MetaMutation: must be called from the UI thread");
		return false;
	}
	return true;
}

bool RequireConfiguration(ibValueMetaObject*& rootOut, char** errorMsg)
{
	if (activeMetaData == nullptr) {
		SetError(errorMsg, "MetaMutation: no configuration loaded");
		return false;
	}
	rootOut = activeMetaData->GetCommonMetaObject();
	if (rootOut == nullptr) {
		SetError(errorMsg, "MetaMutation: configuration has no root");
		return false;
	}
	return true;
}

// Centralised policy gate — every Meta* mutation flows through this.
// Returns true on allow; on deny sets errorMsg to a JSON object the
// plugin can re-emit verbatim via chat.error envelope. The envelope
// shape carries everything the plugin needs to render the confirmation
// prompt: op name, current policy state, suggested elevation.
bool GatePolicy(const char* pluginId, const wxString& opName, char** errorMsg)
{
	if (pluginId == nullptr || *pluginId == '\0') {
		SetError(errorMsg, "MetaMutation: pluginId required for policy gate");
		return false;
	}
	if (appData == nullptr) {
		SetError(errorMsg, "MetaMutation: appData not initialised");
		return false;
	}
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) {
		SetError(errorMsg, "MetaMutation: plugin manager not initialised");
		return false;
	}
	const wxString pidW = wxString::FromUTF8(pluginId);
	if (pm->CheckMutationAllowed(pidW, opName)) return true;

	// Build a JSON-shaped diagnostic the agent UI can render as a
	// confirmation card without a second parse round-trip. The shape
	// mirrors what well-known IDE assistants emit on permission denial
	// so a generic agent renderer Just Works. Use nlohmann::json to
	// auto-escape pluginId/op fields — a hostile or sloppy plugin id
	// containing `"`/`\\`/control bytes would otherwise produce
	// malformed JSON and break the agent's parse.
	const auto policy = pm->GetMutationPolicy(pidW, opName);
	const char* policyName = (policy == ibPluginManager::MutationPolicy::Deny)
		? "Deny"
		: ((policy == ibPluginManager::MutationPolicy::Ask) ? "Ask" : "Allow");
	nlohmann::json env;
	env["code"]     = "permission_denied";
	env["op"]       = std::string(opName.utf8_str());
	env["pluginId"] = std::string(pluginId);
	env["policy"]   = policyName;
	env["hint"]     = "call SetMutationPolicy(pluginId, op, AllowSession|AllowAlways) before retrying";
	SetError(errorMsg, env.dump());
	return false;
}

// Walk top-level children + match (kind, name). Returns (parent, index).
// Phase 3.2 supports only top-level objects; nested mutations (attribute
// on a Catalog, dimension on a Register) land in Phase 3.3.
struct LookupResult {
	ibValueMetaObject* parent = nullptr;
	ibValueMetaObject* hit    = nullptr;
};

LookupResult LookupTopLevel(const std::string& kind, const std::string& name)
{
	LookupResult out;
	if (activeMetaData == nullptr) return out;
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) return out;
	const unsigned long long clsid = KindStringToCLSID(kind.c_str());
	if (clsid == 0) return out;
	const wxString needle = wxString::FromUTF8(name.c_str());
	for (ibValueMetaObject* child : root->GetAnyArrayObject<>()) {
		if (child == nullptr) continue;
		if (child->GetClassType() != static_cast<ibClassID>(clsid)) continue;
		if (child->GetName() != needle) continue;
		out.parent = root;
		out.hit    = child;
		return out;
	}
	out.parent = root;
	return out;
}

// Detect `force=true` flag in propertiesJson. Used by MetaDelete to
// require an explicit irreversible-op opt-in even when policy is
// AllowAlways — matches the `rm --no-preserve-root` convention.
//
// Parser is lenient: missing JSON / non-object payload / malformed
// input all read as force=false (safe default). nlohmann's
// allow_exceptions=false returns a discarded_value on parse failure;
// .is_object() / .find() on that value are no-throw, so no try/catch
// is needed and adding one would mask future legitimate exception
// paths.
bool ExtractForceFlag(const char* propertiesJson)
{
	if (propertiesJson == nullptr || *propertiesJson == '\0') return false;
	auto j = nlohmann::json::parse(propertiesJson, nullptr, /*allow_exceptions*/ false);
	if (!j.is_object()) return false;
	auto it = j.find("force");
	if (it == j.end()) return false;
	return it->is_boolean() ? it->get<bool>() : false;
}

} // namespace

int HostMetaCreate(const char* pluginId,
                    const char* objectKind,
                    const char* fullName,
                    const char* /*propertiesJson*/,
                    char**      errorMsg)
{
	if (errorMsg != nullptr) *errorMsg = nullptr;
	if (!RequireMainThread(errorMsg)) return -1;
	if (!GatePolicy(pluginId, wxT("meta.create"), errorMsg)) {
		return IB_PLUGIN_PERMISSION_DENIED;
	}

	std::string kind, name;
	if (objectKind != nullptr && *objectKind != '\0') {
		kind = objectKind;
	}
	std::string nameOnly;
	if (!SplitFullName(fullName, nameOnly, name)) {
		// Allow caller to pass kind via objectKind and name via fullName
		// without the dot prefix — useful when the agent already knows
		// the kind from the prompt template.
		if (!kind.empty() && fullName != nullptr && std::strchr(fullName, '.') == nullptr) {
			name = fullName;
		} else {
			SetError(errorMsg, "MetaCreate: fullName must be '<Kind>.<Name>' or kind+name provided separately");
			return -1;
		}
	} else if (kind.empty()) {
		kind = nameOnly;
	}

	const unsigned long long clsid = KindStringToCLSID(kind.c_str());
	if (clsid == 0) {
		SetError(errorMsg, "MetaCreate: unknown kind '" + kind + "'");
		return -1;
	}

	ibValueMetaObject* root = nullptr;
	if (!RequireConfiguration(root, errorMsg)) return -1;

	// Refuse to overwrite an existing object — caller must MetaDelete first.
	if (LookupTopLevel(kind, name).hit != nullptr) {
		SetError(errorMsg, "MetaCreate: object already exists '" + kind + "." + name + "'");
		return -1;
	}

	// Real instantiation via the metaCtor factory: each metaobject class
	// registers itself with METADATA_TYPE_REGISTER at static init time;
	// GetAvailableCtor(clsid) returns the ctor pointer. CreateObject()
	// allocates a default-constructed instance; we downcast and graft it
	// onto the configuration root.
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(static_cast<ibClassID>(clsid));
	if (ctor == nullptr) {
		SetError(errorMsg, "MetaCreate: no factory registered for kind '" + kind + "'");
		return -1;
	}
	ibValue* raw = ctor->CreateObject();
	if (raw == nullptr) {
		SetError(errorMsg, "MetaCreate: factory returned null for kind '" + kind + "'");
		return -1;
	}
	auto* obj = dynamic_cast<ibValueMetaObject*>(raw);
	if (obj == nullptr) {
		delete raw;
		SetError(errorMsg, "MetaCreate: factory product is not an ibValueMetaObject");
		return -1;
	}

	const wxString objName = wxString::FromUTF8(name.c_str());
	obj->SetName(objName);
	obj->SetParent(root);
	root->AddChild(obj);

	// Undo lambda: detach + delete. Designer Ctrl+Z (Phase 3.3 wiring
	// of the wxCommandProcessor) triggers UndoLastAgentMutation which
	// pops this entry. The capture holds raw pointers — safe because
	// the undo runs on the same UI thread that performed the create,
	// and configuration teardown (UnloadAll path) bypasses the agent.
	g_undoStack.emplace_back([root, obj]() {
		root->RemoveChild(obj);
		delete obj;
	});

	wxLogMessage(wxT("[meta] MetaCreate(plugin=%s) -> %s.%s OK"),
	             wxString::FromUTF8(pluginId),
	             wxString::FromUTF8(kind), objName);
	return 0;
}

int HostMetaEdit(const char* pluginId,
                  const char* fullName,
                  const char* jsonPatch,
                  char**      errorMsg)
{
	if (errorMsg != nullptr) *errorMsg = nullptr;
	if (!RequireMainThread(errorMsg)) return -1;
	if (!GatePolicy(pluginId, wxT("meta.edit"), errorMsg)) {
		return IB_PLUGIN_PERMISSION_DENIED;
	}

	std::string kind, name;
	if (!SplitFullName(fullName, kind, name)) {
		SetError(errorMsg, "MetaEdit: fullName must be '<Kind>.<Name>'");
		return -1;
	}

	ibValueMetaObject* root = nullptr;
	if (!RequireConfiguration(root, errorMsg)) return -1;

	const LookupResult lookup = LookupTopLevel(kind, name);
	if (lookup.hit == nullptr) {
		SetError(errorMsg, "MetaEdit: not found '" + std::string(fullName) + "'");
		return -1;
	}

	// Phase 3.3 minimal patch: supports a JSON object payload like
	//   {"synonym":"...", "comment":"..."}
	// — straight string replacement on the three top-level common
	// properties. RFC 6902 JSON Patch ops (add/replace/move) on nested
	// attribute paths land in Phase 3.4 once we expose the property
	// vocabulary per metaobject kind.
	if (jsonPatch == nullptr || *jsonPatch == '\0') {
		SetError(errorMsg, "MetaEdit: empty patch payload");
		return -1;
	}
	auto patch = nlohmann::json::parse(jsonPatch, nullptr, /*allow_exceptions*/ false);
	if (!patch.is_object()) {
		SetError(errorMsg, "MetaEdit: patch must be a JSON object (synonym/comment fields)");
		return -1;
	}

	// Snapshot old values for the undo lambda BEFORE we mutate.
	ibValueMetaObject* target = lookup.hit;
	const wxString oldSynonym = target->GetSynonym();
	const wxString oldComment = target->GetComment();
	bool changedSynonym = false;
	bool changedComment = false;

	if (auto it = patch.find("synonym"); it != patch.end() && it->is_string()) {
		target->SetSynonym(wxString::FromUTF8(it->get<std::string>()));
		changedSynonym = true;
	}
	if (auto it = patch.find("comment"); it != patch.end() && it->is_string()) {
		target->SetComment(wxString::FromUTF8(it->get<std::string>()));
		changedComment = true;
	}
	if (!changedSynonym && !changedComment) {
		SetError(errorMsg, "MetaEdit: patch had no recognised fields (synonym/comment supported)");
		return -1;
	}

	g_undoStack.emplace_back([target, oldSynonym, oldComment,
	                           changedSynonym, changedComment]() {
		if (changedSynonym) target->SetSynonym(oldSynonym);
		if (changedComment) target->SetComment(oldComment);
	});

	wxLogMessage(wxT("[meta] MetaEdit(plugin=%s) -> %s synonym=%d comment=%d"),
	             wxString::FromUTF8(pluginId),
	             wxString::FromUTF8(fullName),
	             (int)changedSynonym, (int)changedComment);
	return 0;
}

int HostMetaDelete(const char* pluginId,
                    const char* fullName,
                    const char* propertiesJson,
                    char**      errorMsg)
{
	if (errorMsg != nullptr) *errorMsg = nullptr;
	if (!RequireMainThread(errorMsg)) return -1;
	if (!GatePolicy(pluginId, wxT("meta.delete"), errorMsg)) {
		return IB_PLUGIN_PERMISSION_DENIED;
	}

	// Irreversible-op extra guard. Even when policy is AllowAlways, the
	// caller must opt in to a destructive op by setting `force=true` in
	// the propertiesJson body — same defensive convention as
	// `rm --no-preserve-root`. Without this, an agent that wins
	// AllowAlways for meta.delete once could subsequently wipe arbitrary
	// objects between user prompts.
	if (!ExtractForceFlag(propertiesJson)) {
		SetError(errorMsg, "MetaDelete: destructive op requires propertiesJson {\"force\":true}");
		return -1;
	}

	std::string kind, name;
	if (!SplitFullName(fullName, kind, name)) {
		SetError(errorMsg, "MetaDelete: fullName must be '<Kind>.<Name>'");
		return -1;
	}

	ibValueMetaObject* root = nullptr;
	if (!RequireConfiguration(root, errorMsg)) return -1;

	const LookupResult lookup = LookupTopLevel(kind, name);
	if (lookup.hit == nullptr) {
		SetError(errorMsg, "MetaDelete: not found '" + std::string(fullName) + "'");
		return -1;
	}

	// Detach from parent. We keep ownership in the undo lambda — a Ctrl+Z
	// after delete re-AddChilds the same instance, preserving all its
	// state (properties, child attributes, form blobs). Production
	// configuration validation hook lives in Phase 3.4 (re-run the
	// config validator after each mutation; rollback on failure).
	ibValueMetaObject* parent = lookup.parent;
	ibValueMetaObject* victim = lookup.hit;
	parent->RemoveChild(victim);

	g_undoStack.emplace_back([parent, victim]() {
		victim->SetParent(parent);
		parent->AddChild(victim);
	});

	wxLogMessage(wxT("[meta] MetaDelete(plugin=%s) -> %s OK"),
	             wxString::FromUTF8(pluginId),
	             wxString::FromUTF8(fullName));
	return 0;
}

int UndoLastAgentMutation()
{
	if (!wxIsMainThread()) return -1;
	if (g_undoStack.empty()) return -1;
	auto fn = std::move(g_undoStack.back());
	g_undoStack.pop_back();
	try {
		fn();
		return 0;
	} catch (...) {
		wxLogWarning(wxT("metaBridge: undo lambda threw — partial revert"));
		return -1;
	}
}

void ClearUndoStackForTests()
{
	g_undoStack.clear();
}

} // namespace metaBridge
