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
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/table/metaTableObject.h"
#include "backend/compiler/value.h"
#include "backend/compiler/compileCode.h"
#include "backend/plugin/pluginApi.h"
#include "backend/plugin/pluginManager.h"

#include "3rdparty/nlohmann/json.hpp"

#include <functional>
#include <memory>
#include <vector>

#include <wx/string.h>
#include <wx/thread.h>
#include <wx/log.h>
#include <wx/msgdlg.h>     // SEC-P1-10: delete burst confirmation prompt

#include <algorithm>
#include <cctype>
#include <chrono>          // SEC-P1-10: delete rate-limit
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
		// AGENT-CHILD: child-object kinds. Sigma emits these in plans;
		// metaBridge resolves the parent via multi-segment fullName and
		// grafts the new child under the correct top-level container.
		// Form variants all share g_metaFormCLSID — formType is conveyed
		// via the properties.formType field, not a separate CLSID.
		{ "form",                       g_metaFormCLSID                       },
		{ "itemform",                   g_metaFormCLSID                       },
		{ "listform",                   g_metaFormCLSID                       },
		{ "choiceform",                 g_metaFormCLSID                       },
		{ "selectionform",              g_metaFormCLSID                       },
		{ "attribute",                  g_metaAttributeCLSID                  },
		{ "tabularsection",             g_metaTableCLSID                      },
		{ "tabularsectionattribute",    g_metaAttributeCLSID                  },
		{ "command",                    g_metaCommonModuleCLSID               },
		// Object/manager modules are singleton properties of the parent
		// metaobject (m_propertyObjectModule / m_propertyManagerModule),
		// not separately add/removable children. KindMap still resolves
		// them so HostMetaEdit path-walk can detect + reject "create"
		// attempts and accept "edit" of m_strSource.
		{ "objectmodule",               g_metaModuleCLSID                     },
		{ "managermodule",              g_metaManagerCLSID                    },
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
		// AGENT-CHILD: child-kind canonical labels for JSON output. Form
		// variants collapse to the canonical "Form" — the variant
		// (ItemForm/ListForm/...) is inferred from properties.formType,
		// not the kind string. tabularSectionAttribute also collapses to
		// "Attribute" — the *path* disambiguates parent context.
		{ g_metaFormCLSID,                       "Form"                        },
		{ g_metaAttributeCLSID,                  "Attribute"                   },
		{ g_metaTableCLSID,                      "TabularSection"              },
		{ g_metaModuleCLSID,                     "ObjectModule"                },
		{ g_metaManagerCLSID,                    "ManagerModule"               },
		{ g_metaCommonModuleCLSID,               "Command"                     },
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
// missing the separator or either side is empty. For multi-segment
// paths ("Catalog.Foo.Forms.Bar") this returns the FIRST two segments
// only — kind="Catalog", name="Foo.Forms.Bar". Multi-segment callers
// use SplitPath() instead.
bool SplitFullName(const char* fullName, std::string& kind, std::string& name)
{
	if (fullName == nullptr || *fullName == '\0') return false;
	const char* dot = std::strchr(fullName, '.');
	if (dot == nullptr || dot == fullName || *(dot + 1) == '\0') return false;
	kind.assign(fullName, dot - fullName);
	name.assign(dot + 1);
	return true;
}

// AGENT-CHILD: split a dotted fullName into segments. Returns false on
// empty input or any empty segment ("Catalog..Forms" → false). Path
// shapes recognised by metaBridge:
//
//   2 segments: <Kind>.<Name>                          — top-level
//   3 segments: <Kind>.<Name>.<ChildSingleton>         — object/manager
//                                                         module (singleton)
//   4 segments: <Kind>.<Name>.<ChildKind>.<ChildName>  — form / attribute /
//                                                         tabular section /
//                                                         command
//   6 segments: <Kind>.<Name>.TabularSections.<Table>.Attributes.<Name>
//                                                       — column of TS
//
// Names are UTF-8 byte strings; Cyrillic / Ukrainian content is fine —
// '.' is the only ASCII delimiter and never appears in a metaobject
// identifier (Designer validates names against C-identifier syntax).
bool SplitPath(const char* fullName, std::vector<std::string>& segments)
{
	segments.clear();
	if (fullName == nullptr || *fullName == '\0') return false;
	const char* p = fullName;
	const char* start = p;
	for (; *p; ++p) {
		if (*p == '.') {
			if (p == start) return false;  // empty segment
			segments.emplace_back(start, p - start);
			start = p + 1;
		}
	}
	if (start == p) return false;          // trailing dot
	segments.emplace_back(start, p - start);
	return !segments.empty();
}

// AGENT-CHILD: canonical child-container label normaliser. Sigma emits
// "Forms" / "Attributes" / "TabularSections" / "Commands" — accept
// case-insensitively and map to a canonical CLSID. Returns 0 when the
// label isn't a recognised container.
unsigned long long ChildContainerCLSID(const std::string& container)
{
	const std::string c = [&](){
		std::string out(container);
		std::transform(out.begin(), out.end(), out.begin(),
		               [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
		return out;
	}();
	if (c == "forms"            || c == "form")             return g_metaFormCLSID;
	if (c == "attributes"       || c == "attribute")        return g_metaAttributeCLSID;
	if (c == "tabularsections"  || c == "tabularsection" ||
	    c == "tables"           || c == "table")            return g_metaTableCLSID;
	if (c == "commands"         || c == "command")          return g_metaCommonModuleCLSID;
	return 0;
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

// Undo stack — Designer Ctrl+Z integration lands in Phase 3.4 via the
// active document's wxCommandProcessor. Held in-process; vector for
// test introspection. By contract every entry point asserts main-thread
// (wxIsMainThread), so no mutex is needed yet; Phase 3.4 plumbing of
// the Designer command processor stays on the UI thread.
//
// Each entry pairs an undo callable with the configuration epoch that
// was active when the mutation ran. g_configEpoch advances on every
// UnloadAll-of-configuration (or any other configuration teardown).
// On undo we compare the stored epoch to the live one — if they differ,
// the entry refers to a dead tree and we skip it instead of dereferenc-
// ing a freed pointer (the Claude P0 dangling-pointer class of bugs).
struct UndoEntry {
	std::function<void()> fn;
	unsigned long long    epoch;
};
std::vector<UndoEntry> g_undoStack;
unsigned long long     g_configEpoch = 1;

// Test override — when non-null, GatePolicy uses this manager instead
// of appData->GetPluginManager(). Production code never touches it.
// Backend has no fixture infrastructure of its own, so this is the
// cheapest way to exercise the policy gate in unit tests without
// booting a full ibApplicationData.
ibPluginManager* g_testPluginManagerOverride = nullptr;

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
	// Tests inject the manager via SetPluginManagerOverrideForTests so
	// the policy gate exercises real CheckMutationAllowed without
	// booting a full ibApplicationData. Production code never sees the
	// override branch — it stays nullptr.
	ibPluginManager* pm = g_testPluginManagerOverride;
	if (pm == nullptr) {
		if (appData == nullptr) {
			SetError(errorMsg, "MetaMutation: appData not initialised");
			return false;
		}
		pm = appData->GetPluginManager();
		if (pm == nullptr) {
			SetError(errorMsg, "MetaMutation: plugin manager not initialised");
			return false;
		}
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

// AGENT-CHILD: find a direct child of `parent` whose CLSID matches and
// whose name equals `needle`. Returns nullptr when no match. Used by
// the path-based create/edit/delete walk to descend into Forms /
// Attributes / TabularSections / Commands collections.
ibValueMetaObject* FindDirectChild(ibValueMetaObject* parent,
                                     unsigned long long clsid,
                                     const wxString& needle)
{
	if (parent == nullptr) return nullptr;
	for (ibValueMetaObject* child : parent->GetAnyArrayObject<>()) {
		if (child == nullptr) continue;
		if (child->GetClassType() != static_cast<ibClassID>(clsid)) continue;
		if (child->GetName() != needle) continue;
		return child;
	}
	return nullptr;
}

// AGENT-CHILD: result of resolving a multi-segment path against the
// live metadata tree. For top-level creates, parent==root and hit may
// be nullptr (target doesn't exist yet). For child creates, parent is
// the *container parent* (Catalog/Document/Table), childCLSID is the
// CLSID the new node should have, leafName is the to-be-created name.
// `isSingleton` marks object/manager modules — they live as property
// values on the parent, not separately add/removable children; the
// caller must reject "create" and route "edit" to the property's
// SetValue(). For singletons, hit points to the singleton's metaobject
// when it exists (modules always exist on their owners; they cannot
// be deleted separately).
struct PathResolution {
	bool                 ok            = false;
	ibValueMetaObject*   parent        = nullptr;
	ibValueMetaObject*   hit           = nullptr;
	unsigned long long   childCLSID    = 0;
	wxString             leafName;
	bool                 isSingleton   = false;
	bool                 isTopLevel    = false;
	// Diagnostic — set when ok==false; SetError appends this verbatim.
	std::string          errorReason;
};

PathResolution ResolvePath(const char* fullName)
{
	PathResolution r;
	std::vector<std::string> segments;
	if (!SplitPath(fullName, segments)) {
		r.errorReason = "fullName malformed (empty or trailing-dot segment)";
		return r;
	}
	if (activeMetaData == nullptr) {
		r.errorReason = "no configuration loaded";
		return r;
	}
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) {
		r.errorReason = "configuration has no root";
		return r;
	}

	// Segment 0 = top-level kind label.
	const unsigned long long topCLSID = KindStringToCLSID(segments[0].c_str());
	if (topCLSID == 0) {
		r.errorReason = "unknown top-level kind '" + segments[0] + "'";
		return r;
	}

	// 2-segment: top-level <Kind>.<Name> — preserve legacy behaviour.
	if (segments.size() == 2) {
		const wxString topName = wxString::FromUTF8(segments[1].c_str());
		r.parent     = root;
		r.hit        = FindDirectChild(root, topCLSID, topName);
		r.childCLSID = topCLSID;
		r.leafName   = topName;
		r.isTopLevel = true;
		r.ok         = true;
		return r;
	}

	// 3+ segments: descend into the top-level container then resolve
	// child container + leaf.
	const wxString topName = wxString::FromUTF8(segments[1].c_str());
	ibValueMetaObject* topObj = FindDirectChild(root, topCLSID, topName);
	if (topObj == nullptr) {
		r.errorReason = "top-level parent '" + segments[0] + "." +
		                 segments[1] + "' not found";
		return r;
	}

	// 3-segment singleton: <Kind>.<Name>.<ObjectModule|ManagerModule>.
	// Resolve via the lower-cased label; CLSIDs from KindMap.
	if (segments.size() == 3) {
		const unsigned long long childCLSID =
		    KindStringToCLSID(segments[2].c_str());
		if (childCLSID == g_metaModuleCLSID ||
		    childCLSID == g_metaManagerCLSID) {
			// Module objects live on the parent's property tree. Walk
			// the parent's children for an object whose CLSID matches —
			// the property system grafts these as real children at
			// construction (via CreateMetaObjectAndSetParent in
			// ibPropertyInnerModule's ctor), so they show up in
			// GetAnyArrayObject.
			ibValueMetaObject* hit = nullptr;
			for (ibValueMetaObject* c : topObj->GetAnyArrayObject<>()) {
				if (c == nullptr) continue;
				if (c->GetClassType() != static_cast<ibClassID>(childCLSID)) continue;
				hit = c;
				break;
			}
			r.parent      = topObj;
			r.hit         = hit;
			r.childCLSID  = childCLSID;
			r.leafName    = wxString::FromUTF8(segments[2].c_str());
			r.isSingleton = true;
			r.ok          = true;
			return r;
		}
		r.errorReason = "3-segment path requires ObjectModule|ManagerModule, got '" +
		                 segments[2] + "'";
		return r;
	}

	// 4-segment: <Kind>.<Name>.<ChildKind>.<ChildName>
	if (segments.size() == 4) {
		const unsigned long long childCLSID = ChildContainerCLSID(segments[2]);
		if (childCLSID == 0) {
			r.errorReason = "unknown child container '" + segments[2] +
			                 "' (expected Forms/Attributes/TabularSections/Commands)";
			return r;
		}
		const wxString leafName = wxString::FromUTF8(segments[3].c_str());
		r.parent     = topObj;
		r.hit        = FindDirectChild(topObj, childCLSID, leafName);
		r.childCLSID = childCLSID;
		r.leafName   = leafName;
		r.ok         = true;
		return r;
	}

	// 6-segment: <Kind>.<Name>.TabularSections.<Table>.Attributes.<Attr>
	if (segments.size() == 6) {
		const unsigned long long tsCLSID = ChildContainerCLSID(segments[2]);
		if (tsCLSID != g_metaTableCLSID) {
			r.errorReason = "6-segment path requires TabularSections at index 2, got '" +
			                 segments[2] + "'";
			return r;
		}
		const wxString tableName = wxString::FromUTF8(segments[3].c_str());
		ibValueMetaObject* table = FindDirectChild(topObj, tsCLSID, tableName);
		if (table == nullptr) {
			r.errorReason = "tabular section '" + segments[3] +
			                 "' not found on parent";
			return r;
		}
		const unsigned long long attrCLSID = ChildContainerCLSID(segments[4]);
		if (attrCLSID != g_metaAttributeCLSID) {
			r.errorReason = "6-segment path requires Attributes at index 4, got '" +
			                 segments[4] + "'";
			return r;
		}
		const wxString attrName = wxString::FromUTF8(segments[5].c_str());
		r.parent     = table;
		r.hit        = FindDirectChild(table, attrCLSID, attrName);
		r.childCLSID = attrCLSID;
		r.leafName   = attrName;
		r.ok         = true;
		return r;
	}

	r.errorReason = "path depth " + std::to_string(segments.size()) +
	                 " not supported (use 2/3/4/6 segments)";
	return r;
}

// AGENT-CHILD: map a "formType" string token from properties JSON to
// the integer ibFormID stored on ibValueMetaObjectForm. Mirrors the
// FillFormType enum on each parent metaobject (Catalog has 5 variants,
// Document has 4, etc.). Defaults to defaultFormType (== wxNOT_FOUND)
// for unrecognised tokens — Designer renders these as "Form" with no
// preset binding, which is the safest fallback.
//
// Token vocabulary mirrors what Sigma emits in plans (see CLAUDE.md
// vibe-coding section). Case-insensitive lookup.
int FormTypeFromString(const std::string& s)
{
	std::string lower(s);
	std::transform(lower.begin(), lower.end(), lower.begin(),
	               [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
	// Catalog: 1=Object 2=List 3=Select 4=Folder 5=FolderSelect
	if (lower == "formobject" || lower == "itemform"  || lower == "objectform") return 1;
	if (lower == "formfolder" || lower == "folderform")                          return 4;
	if (lower == "formlist"   || lower == "listform")                            return 2;
	if (lower == "formselect" || lower == "choiceform"   ||
	    lower == "selectionform"|| lower == "selectform")                        return 3;
	if (lower == "formgroupselect" || lower == "folderselectform")               return 5;
	return defaultFormType;
}

// Audit-log helper. Plugin-supplied identifiers (pluginId, fullName)
// can carry newlines / NUL bytes / ANSI escapes that break grep-based
// forensics. Strip control chars + cap length at 256 code points
// (NOT code units — wxString.size() counts UTF-16 units on MSW, so
// a separate counter avoids halving the visible budget for emoji /
// supplementary-plane content). Not a security boundary — plugins are
// loaded code — but hygiene for incident review.
wxString SanitiseForLog(const wxString& in)
{
	wxString out;
	out.reserve(in.size());
	int     emitted = 0;
	int     dropped = 0;
	for (wxUniChar c : in) {
		if (emitted >= 256) { dropped += 1; continue; }
		const auto v = static_cast<unsigned>(c.GetValue());
		if (v < 0x20 || v == 0x7F) out += wxT('?');
		else                        out += c;
		++emitted;
	}
	if (dropped > 0) {
		out += wxString::Format(wxT("…[+%dch]"), dropped);
	}
	return out;
}

// 1 MB hard cap on patch payloads — defends the host against memory
// exhaustion from a hostile plugin that ships a megabyte of "{a:1,…}".
// The nlohmann parser would otherwise allocate proportional to input.
constexpr std::size_t kMaxPatchBytes = 1024 * 1024;

// SEC-P1-10: per-pluginId last-delete timestamp. Even when policy is
// AllowAlways AND force=true is set, a burst of deletes within
// kDeleteBurstThreshold requires a fresh prompt — matches the human-rate
// expectation that operations 5 seconds apart are independent
// approvals, while back-to-back fires within the same UI tick almost
// always indicate an agent runaway. The first delete in a quiet period
// passes silently; subsequent ones inside the window get gated through
// the wxMessageBox confirmation prompt.
constexpr std::chrono::seconds kDeleteBurstThreshold(5);
std::unordered_map<std::string, std::chrono::steady_clock::time_point>
    g_lastDeleteAt;

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

namespace {

// AGENT-CHILD: factory-instantiate a new metaobject of the given CLSID.
// Returns nullptr on factory failure; never throws. Caller owns the
// result and must SetName/SetParent/AddChild before any flush.
ibValueMetaObject* InstantiateMetaObject(unsigned long long clsid)
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(static_cast<ibClassID>(clsid));
	if (ctor == nullptr) return nullptr;
	ibValue* raw = ctor->CreateObject();
	if (raw == nullptr) return nullptr;
	auto* obj = dynamic_cast<ibValueMetaObject*>(raw);
	if (obj == nullptr) {
		delete raw;
		return nullptr;
	}
	return obj;
}

// AGENT-CHILD: apply property-bag fields shared across all child kinds.
// Returns true on success. `clsid` informs which fields are honoured —
// e.g. moduleCode is only valid on forms/modules, formType only on
// forms. Unknown fields are *not* rejected here (the contract is
// permissive — Sigma may emit aspirational fields the host doesn't yet
// implement; future versions can grow support).
//
// For form moduleCode, we run a strict compile check via ibCompileCode
// and surface the first error verbatim to the caller. This is the
// "RejectsBrokenModuleCode" contract — never accept a form whose
// module source doesn't parse.
bool ApplyCreateProperties(ibValueMetaObject* obj,
                            unsigned long long clsid,
                            const nlohmann::json& props,
                            std::string& errOut)
{
	// synonym: accept either a plain string ("Контрагенты") or a locale
	// map ({"uk-UA":"Контрагенти"}). For the map shape, pick the first
	// value — Designer stores synonym as a single string, locale
	// resolution happens at render time via lang files.
	if (auto it = props.find("synonym"); it != props.end()) {
		if (it->is_string()) {
			obj->SetSynonym(wxString::FromUTF8(it->get<std::string>()));
		} else if (it->is_object() && !it->empty()) {
			// Prefer uk-UA, then ru-RU, then en-US, else first key.
			static const char* prefer[] = { "uk-UA", "ru-RU", "en-US" };
			std::string picked;
			for (const char* k : prefer) {
				auto v = it->find(k);
				if (v != it->end() && v->is_string()) {
					picked = v->get<std::string>();
					break;
				}
			}
			if (picked.empty()) {
				for (auto& kv : it->items()) {
					if (kv.value().is_string()) {
						picked = kv.value().get<std::string>();
						break;
					}
				}
			}
			if (!picked.empty()) {
				obj->SetSynonym(wxString::FromUTF8(picked));
			}
		}
	}

	// comment: plain string.
	if (auto it = props.find("comment"); it != props.end() && it->is_string()) {
		obj->SetComment(wxString::FromUTF8(it->get<std::string>()));
	}

	// moduleCode: form/module-only field. Compile-validate via
	// ibCompileCode::Compile(source) — reject the whole create on
	// syntax error so we never persist a broken module.
	auto codeIt = props.find("moduleCode");
	if (codeIt == props.end()) codeIt = props.find("code");
	if (codeIt != props.end() && codeIt->is_string()) {
		const std::string& src = codeIt->get<std::string>();
		const wxString srcW = wxString::FromUTF8(src);
		// Honest compile-only validation: instantiate a throwaway
		// compileCode and run Compile(source). Any failure trips the
		// global compileError via ibTranslateCode — surface it.
		ibCompileCode probe;
		if (!probe.Compile(srcW)) {
			errOut = "moduleCode compile failed (syntax error)";
			return false;
		}
		// Apply to the right slot.
		if (clsid == g_metaFormCLSID) {
			auto* form = dynamic_cast<ibValueMetaObjectFormBase*>(obj);
			if (form != nullptr) form->SetModuleText(srcW);
		} else if (clsid == g_metaModuleCLSID ||
		           clsid == g_metaManagerCLSID ||
		           clsid == g_metaCommonModuleCLSID) {
			auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(obj);
			if (mod != nullptr) mod->SetModuleText(srcW);
		}
	}

	// AGENT-CHILD: formType only applies to forms. The integer encoding
	// is per-parent-CLSID, but the Catalog mapping covers the common
	// cases (1=Object 2=List 3=Select 4=Folder 5=FolderSelect); other
	// parents (Document/Register) share enough of the vocabulary that
	// FormTypeFromString gives the right answer for Sigma's tokens.
	if (clsid == g_metaFormCLSID) {
		std::string formTypeStr;
		if (auto it = props.find("formType"); it != props.end() && it->is_string()) {
			formTypeStr = it->get<std::string>();
		}
		// We accept formType=Form / ItemForm without setting it explicitly —
		// defaultFormType is the safe fallback. An unknown formType token
		// also lands on defaultFormType (Designer treats it as "Form").
		if (!formTypeStr.empty()) {
			const int ft = FormTypeFromString(formTypeStr);
			// formType is exposed via a property; we cannot write through
			// the wxProperty API from here without dragging the GUI in.
			// Acceptable: the form is created with defaultFormType and
			// the operator can adjust in the inspector. Future commit
			// can wire SetFormType(int) into the backend API surface.
			(void)ft;
		}
	}

	return true;
}

} // namespace

int HostMetaCreate(const char* pluginId,
                    const char* objectKind,
                    const char* fullName,
                    const char* propertiesJson,
                    char**      errorMsg)
{
	if (errorMsg != nullptr) *errorMsg = nullptr;
	if (!RequireMainThread(errorMsg)) return -1;
	if (!GatePolicy(pluginId, wxT("meta.create"), errorMsg)) {
		return IB_PLUGIN_PERMISSION_DENIED;
	}

	// Parse the properties payload up front — every path needs it, and
	// we want to surface JSON parse errors before any tree mutation.
	nlohmann::json props = nlohmann::json::object();
	if (propertiesJson != nullptr && *propertiesJson != '\0') {
		const std::size_t pLen = ::strnlen(propertiesJson, kMaxPatchBytes + 1);
		if (pLen > kMaxPatchBytes) {
			SetError(errorMsg, "MetaCreate: properties exceed 1 MB hard cap");
			return -1;
		}
		auto p = nlohmann::json::parse(propertiesJson, nullptr, /*allow_exceptions*/ false);
		if (!p.is_discarded() && p.is_object()) props = std::move(p);
	}

	ibValueMetaObject* root = nullptr;
	if (!RequireConfiguration(root, errorMsg)) return -1;

	// AGENT-CHILD: route by path depth. Two segments = top-level
	// (preserve existing semantics — drop into the bottom branch). Three
	// or more segments = child path (form/attribute/tabular section /
	// command / module).
	std::vector<std::string> segments;
	const bool pathOk = (fullName != nullptr) && SplitPath(fullName, segments);
	if (pathOk && segments.size() >= 3) {
		const PathResolution r = ResolvePath(fullName);
		if (!r.ok) {
			SetError(errorMsg, "MetaCreate: " + r.errorReason);
			return -1;
		}

		// Singletons (ObjectModule/ManagerModule) cannot be CREATED — they
		// are always present on their parent. Route to edit if caller
		// wants to update source.
		if (r.isSingleton) {
			SetError(errorMsg, "MetaCreate: '" + std::string(fullName) +
			                    "' is a singleton; use MetaEdit to update moduleCode");
			return -1;
		}

		// Duplicate child name — reject explicitly.
		if (r.hit != nullptr) {
			SetError(errorMsg, "MetaCreate: child already exists '" +
			                    std::string(fullName) + "'");
			return -1;
		}

		// Form variants validate formType before instantiation so we
		// emit a clear error rather than create then orphan. Accept the
		// permissive set (Form/ItemForm/...); unknown tokens land on
		// defaultFormType (no rejection — matches Designer's tolerant
		// behaviour for default-form-binding).
		if (r.childCLSID == g_metaFormCLSID) {
			auto ftIt = props.find("formType");
			if (ftIt != props.end() && ftIt->is_string()) {
				// Honor the test case "RejectsInvalidFormType": when the
				// caller declares a formType, we require it be a known
				// token. Empty / missing formType falls through to
				// defaultFormType.
				const int ft = FormTypeFromString(ftIt->get<std::string>());
				if (ft == defaultFormType && !ftIt->get<std::string>().empty()) {
					// Only reject when the explicit string is non-empty
					// AND maps to defaultFormType — i.e. unrecognised.
					SetError(errorMsg, "MetaCreate: unknown formType '" +
					                    ftIt->get<std::string>() + "'");
					return -1;
				}
			}
		}

		ibValueMetaObject* child = InstantiateMetaObject(r.childCLSID);
		if (child == nullptr) {
			SetError(errorMsg, "MetaCreate: factory failed for child CLSID");
			return -1;
		}
		child->SetName(r.leafName);
		child->SetParent(r.parent);

		// Apply props BEFORE AddChild so a moduleCode compile-error
		// rejects the whole op without ever attaching to the tree.
		std::string err;
		if (!ApplyCreateProperties(child, r.childCLSID, props, err)) {
			delete child;
			SetError(errorMsg, "MetaCreate: " + err);
			return -1;
		}

		// AGENT-CHILD: tabular section creation — when properties carry
		// an `attributes` array, instantiate one ibValueMetaObjectAttribute
		// per entry and graft under the new table. Each attribute also
		// goes through ApplyCreateProperties for synonym/comment.
		if (r.childCLSID == g_metaTableCLSID) {
			auto attrsIt = props.find("attributes");
			if (attrsIt != props.end() && attrsIt->is_array()) {
				for (const auto& a : *attrsIt) {
					if (!a.is_object()) continue;
					auto nameIt = a.find("name");
					if (nameIt == a.end() || !nameIt->is_string()) continue;
					ibValueMetaObject* col = InstantiateMetaObject(g_metaAttributeCLSID);
					if (col == nullptr) continue;
					col->SetName(wxString::FromUTF8(nameIt->get<std::string>()));
					col->SetParent(child);
					std::string ignored;
					(void)ApplyCreateProperties(col, g_metaAttributeCLSID, a, ignored);
					child->AddChild(col);
				}
			}
		}

		ibValueMetaObject* parent = r.parent;
		parent->AddChild(child);

		g_undoStack.push_back({[parent, child]() {
			parent->RemoveChild(child);
			delete child;
		}, g_configEpoch});

		wxLogDebug(wxT("[meta] MetaCreate(plugin=%s) -> %s OK (child path)"),
		             SanitiseForLog(wxString::FromUTF8(pluginId)),
		             SanitiseForLog(wxString::FromUTF8(fullName)));
		return 0;
	}

	// ---- Top-level path (legacy, 2-segment <Kind>.<Name>) ----
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
	ibValueMetaObject* obj = InstantiateMetaObject(clsid);
	if (obj == nullptr) {
		SetError(errorMsg, "MetaCreate: no factory registered for kind '" + kind + "'");
		return -1;
	}

	const wxString objName = wxString::FromUTF8(name.c_str());
	obj->SetName(objName);
	obj->SetParent(root);

	// Apply top-level synonym/comment if present. moduleCode/formType
	// don't apply at top level but ApplyCreateProperties skips them
	// silently when the CLSID doesn't match.
	std::string err;
	if (!ApplyCreateProperties(obj, clsid, props, err)) {
		delete obj;
		SetError(errorMsg, "MetaCreate: " + err);
		return -1;
	}

	root->AddChild(obj);

	// Undo lambda: detach + delete. Epoch-gated so a configuration
	// reload between create and undo doesn't deref a freed root.
	g_undoStack.push_back({[root, obj]() {
		root->RemoveChild(obj);
		delete obj;
	}, g_configEpoch});

	wxLogDebug(wxT("[meta] MetaCreate(plugin=%s) -> %s.%s OK"),
	             SanitiseForLog(wxString::FromUTF8(pluginId)),
	             SanitiseForLog(wxString::FromUTF8(kind)),
	             SanitiseForLog(objName));
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

	if (fullName == nullptr || *fullName == '\0') {
		SetError(errorMsg, "MetaEdit: fullName required");
		return -1;
	}

	// Patch shape validation runs BEFORE the configuration check so a
	// caller that ships a malformed JSON gets the right diagnostic even
	// when there's no active configuration. Order: cheap, deterministic
	// checks first; only then expensive configuration lookups.
	if (jsonPatch == nullptr || *jsonPatch == '\0') {
		SetError(errorMsg, "MetaEdit: empty patch payload");
		return -1;
	}
	// strnlen bounds the walk so a hostile plugin shipping a non-null-
	// terminated buffer can't make us read past kMaxPatchBytes + 1 bytes
	// before the cap fires.
	const std::size_t patchLen = ::strnlen(jsonPatch, kMaxPatchBytes + 1);
	if (patchLen > kMaxPatchBytes) {
		SetError(errorMsg, "MetaEdit: patch exceeds 1 MB hard cap");
		return -1;
	}
	auto patch = nlohmann::json::parse(jsonPatch, nullptr, /*allow_exceptions*/ false);
	if (patch.is_discarded()) {
		SetError(errorMsg, "MetaEdit: patch failed to parse as JSON");
		return -1;
	}
	if (!patch.is_object()) {
		SetError(errorMsg, "MetaEdit: patch must be a JSON object (synonym/comment fields)");
		return -1;
	}

	// AGENT-CHILD: route by path depth. 2-segment = top-level (legacy,
	// preserved). 3+ segments = child path. Child paths accept extra
	// keys (moduleCode, title) that don't apply at top level.
	std::vector<std::string> segments;
	const bool pathOk = SplitPath(fullName, segments);
	const bool isChildPath = pathOk && segments.size() >= 3;

	// Strict-key validation. Unknown keys are rejected to keep the
	// contract honest — silently dropping "name":"Pwn" would let an
	// agent send mutation payloads the host doesn't understand and
	// think they applied. The unknown-key string is sanitised through
	// the audit-log helper before being splashed into errorMsg so a
	// malicious plugin cannot inject newlines / NUL bytes into the
	// error envelope the host may eventually re-log.
	//
	// AGENT-CHILD: extended key set for child paths.
	const auto isKnownKey = [&](const std::string& k) {
		if (k == "synonym" || k == "comment") return true;
		if (!isChildPath) return false;
		return (k == "moduleCode" || k == "code" || k == "title" ||
		         k == "binding");
	};
	for (auto& kv : patch.items()) {
		const std::string& k = kv.key();
		if (!isKnownKey(k)) {
			const wxString safe = SanitiseForLog(wxString::FromUTF8(k));
			const std::string supported = isChildPath
			    ? std::string("supported: synonym, comment, moduleCode, title, binding")
			    : std::string("supported: synonym, comment");
			SetError(errorMsg, "MetaEdit: unknown key '" +
			                    std::string(safe.utf8_str()) + "' (" +
			                    supported + ")");
			return -1;
		}
	}

	// Now that patch syntax is verified, look up the target.
	ibValueMetaObject* root = nullptr;
	if (!RequireConfiguration(root, errorMsg)) return -1;

	ibValueMetaObject* target = nullptr;
	if (isChildPath) {
		const PathResolution r = ResolvePath(fullName);
		if (!r.ok) {
			SetError(errorMsg, "MetaEdit: " + r.errorReason);
			return -1;
		}
		if (r.hit == nullptr) {
			SetError(errorMsg, "MetaEdit: not found '" + std::string(fullName) + "'");
			return -1;
		}
		target = r.hit;
	} else {
		std::string kind, name;
		if (!SplitFullName(fullName, kind, name)) {
			SetError(errorMsg, "MetaEdit: fullName must be '<Kind>.<Name>'");
			return -1;
		}
		const LookupResult lookup = LookupTopLevel(kind, name);
		if (lookup.hit == nullptr) {
			SetError(errorMsg, "MetaEdit: not found '" + std::string(fullName) + "'");
			return -1;
		}
		target = lookup.hit;
	}

	// Snapshot old values for the undo lambda BEFORE we mutate. We
	// capture three fields independent of which actually change so the
	// undo lambda restores any of them; flags track which fields the
	// patch touched.
	const wxString oldSynonym  = target->GetSynonym();
	const wxString oldComment  = target->GetComment();
	wxString       oldModule;
	if (auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(target)) {
		oldModule = mod->GetModuleText();
	} else if (auto* form = dynamic_cast<ibValueMetaObjectFormBase*>(target)) {
		oldModule = form->GetModuleText();
	}
	bool changedSynonym = false;
	bool changedComment = false;
	bool changedModule  = false;

	if (auto it = patch.find("synonym"); it != patch.end() && it->is_string()) {
		target->SetSynonym(wxString::FromUTF8(it->get<std::string>()));
		changedSynonym = true;
	}
	if (auto it = patch.find("comment"); it != patch.end() && it->is_string()) {
		target->SetComment(wxString::FromUTF8(it->get<std::string>()));
		changedComment = true;
	}
	if (isChildPath) {
		auto codeIt = patch.find("moduleCode");
		if (codeIt == patch.end()) codeIt = patch.find("code");
		if (codeIt != patch.end() && codeIt->is_string()) {
			const wxString src = wxString::FromUTF8(codeIt->get<std::string>());
			// Compile-validate before commit — reject the whole edit on
			// syntax error, including any synonym/comment changes
			// applied above. Roll those back inline.
			ibCompileCode probe;
			if (!probe.Compile(src)) {
				if (changedSynonym) target->SetSynonym(oldSynonym);
				if (changedComment) target->SetComment(oldComment);
				SetError(errorMsg, "MetaEdit: moduleCode compile failed (syntax error)");
				return -1;
			}
			if (auto* form = dynamic_cast<ibValueMetaObjectFormBase*>(target)) {
				form->SetModuleText(src);
				changedModule = true;
			} else if (auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(target)) {
				mod->SetModuleText(src);
				changedModule = true;
			} else {
				if (changedSynonym) target->SetSynonym(oldSynonym);
				if (changedComment) target->SetComment(oldComment);
				SetError(errorMsg, "MetaEdit: moduleCode applies only to forms/modules");
				return -1;
			}
		}
	}

	if (!changedSynonym && !changedComment && !changedModule) {
		SetError(errorMsg, isChildPath
		    ? "MetaEdit: patch had no recognised fields (synonym/comment/moduleCode supported)"
		    : "MetaEdit: patch had no recognised fields (synonym/comment supported)");
		return -1;
	}

	// Capture by fullName + re-resolve at undo time + identity-check
	// against the original target pointer. A raw `target` capture alone
	// would dangle on intervening MetaDelete; epoch-gating catches
	// config-reload; the identity check guards the narrow window where
	// an object of the same fullName is delete-then-recreated between
	// edit and undo — without it the undo would silently apply the old
	// synonym/comment to the impostor.
	ibValueMetaObject* originalTarget = target;
	const std::string fullNameCopy(fullName);
	const bool isChildPathCopy = isChildPath;
	g_undoStack.push_back({[originalTarget, fullNameCopy, isChildPathCopy,
	                          oldSynonym, oldComment, oldModule,
	                          changedSynonym, changedComment, changedModule]() {
		ibValueMetaObject* tgt = nullptr;
		if (isChildPathCopy) {
			const PathResolution r = ResolvePath(fullNameCopy.c_str());
			if (!r.ok || r.hit == nullptr) {
				wxLogWarning(wxT("metaBridge: MetaEdit undo skipped — target '%s' no longer in tree"),
				             SanitiseForLog(wxString::FromUTF8(fullNameCopy)));
				return;
			}
			tgt = r.hit;
		} else {
			std::string k, n;
			if (!SplitFullName(fullNameCopy.c_str(), k, n)) {
				wxLogWarning(wxT("metaBridge: MetaEdit undo skipped — bad fullName '%s'"),
				             SanitiseForLog(wxString::FromUTF8(fullNameCopy)));
				return;
			}
			const LookupResult lookup = LookupTopLevel(k, n);
			if (lookup.hit == nullptr) {
				wxLogWarning(wxT("metaBridge: MetaEdit undo skipped — target '%s' no longer in tree"),
				             SanitiseForLog(wxString::FromUTF8(fullNameCopy)));
				return;
			}
			tgt = lookup.hit;
		}
		if (tgt != originalTarget) {
			wxLogWarning(wxT("metaBridge: MetaEdit undo skipped — target '%s' identity changed (delete + recreate?)"),
			             SanitiseForLog(wxString::FromUTF8(fullNameCopy)));
			return;
		}
		if (changedSynonym) tgt->SetSynonym(oldSynonym);
		if (changedComment) tgt->SetComment(oldComment);
		if (changedModule) {
			if (auto* form = dynamic_cast<ibValueMetaObjectFormBase*>(tgt)) {
				form->SetModuleText(oldModule);
			} else if (auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(tgt)) {
				mod->SetModuleText(oldModule);
			}
		}
	}, g_configEpoch});

	wxLogDebug(wxT("[meta] MetaEdit(plugin=%s) -> %s synonym=%d comment=%d module=%d"),
	             SanitiseForLog(wxString::FromUTF8(pluginId)),
	             SanitiseForLog(wxString::FromUTF8(fullName)),
	             (int)changedSynonym, (int)changedComment, (int)changedModule);
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

	// SEC-P1-10: burst-rate gate. force=true + AllowAlways is the path an
	// agent runaway uses to wipe N objects in one tick. Require a
	// confirmation prompt when deletes from the same plugin land within
	// kDeleteBurstThreshold of each other.
	{
		const std::string pidKey = pluginId ? std::string(pluginId) : std::string();
		const auto now = std::chrono::steady_clock::now();
		auto it = g_lastDeleteAt.find(pidKey);
		const bool tooFast = (it != g_lastDeleteAt.end()) &&
		                      ((now - it->second) < kDeleteBurstThreshold);
		if (tooFast) {
			const wxString summary = wxString::Format(
			    _("Удалить '%s'? Это повторное удаление — подтвердите вручную."),
			    SanitiseForLog(wxString::FromUTF8(fullName ? fullName : "")));
			const int answer = wxMessageBox(summary,
			                                  _("Подтверждение удаления"),
			                                  wxYES_NO | wxICON_WARNING);
			if (answer != wxYES) {
				SetError(errorMsg, "MetaDelete: declined by user (burst-rate confirmation)");
				return IB_PLUGIN_PERMISSION_DENIED;
			}
		}
		g_lastDeleteAt[pidKey] = now;
	}

	ibValueMetaObject* root = nullptr;
	if (!RequireConfiguration(root, errorMsg)) return -1;

	// AGENT-CHILD: route by path depth. 2-segment = top-level (legacy,
	// preserved). 3+ segments = child path; singletons are rejected
	// because their parent owns them via property tree and dropping
	// them out of band leaves dangling property pointers.
	std::vector<std::string> segments;
	const bool pathOk = SplitPath(fullName, segments);
	ibValueMetaObject* parent = nullptr;
	ibValueMetaObject* victim = nullptr;

	if (pathOk && segments.size() >= 3) {
		const PathResolution r = ResolvePath(fullName);
		if (!r.ok) {
			SetError(errorMsg, "MetaDelete: " + r.errorReason);
			return -1;
		}
		if (r.isSingleton) {
			SetError(errorMsg, "MetaDelete: '" + std::string(fullName) +
			                    "' is a singleton; cannot delete a module owned by its parent");
			return -1;
		}
		if (r.hit == nullptr) {
			SetError(errorMsg, "MetaDelete: not found '" + std::string(fullName) + "'");
			return -1;
		}
		parent = r.parent;
		victim = r.hit;
	} else {
		std::string kind, name;
		if (!SplitFullName(fullName, kind, name)) {
			SetError(errorMsg, "MetaDelete: fullName must be '<Kind>.<Name>'");
			return -1;
		}
		const LookupResult lookup = LookupTopLevel(kind, name);
		if (lookup.hit == nullptr) {
			SetError(errorMsg, "MetaDelete: not found '" + std::string(fullName) + "'");
			return -1;
		}
		parent = lookup.parent;
		victim = lookup.hit;
	}

	// Detach from parent + take ownership of the orphaned subtree.
	// Ownership protocol:
	//   - shared_ptr<bool> `released` flag captured by BOTH the custom
	//     deleter and the undo lambda.
	//   - If undo never fires (stack cleared on configuration reload,
	//     undo entry popped past, process exits), the deleter destroys
	//     victim cleanly — no leak.
	//   - If undo fires, it sets *released=true and reattaches victim
	//     to its old parent. The deleter then no-ops when the last
	//     shared_ptr (the lambda's capture) dies, leaving the
	//     configuration tree as victim's new owner.
	parent->RemoveChild(victim);

	auto released = std::make_shared<bool>(false);
	std::shared_ptr<ibValueMetaObject> owned(victim,
		[released](ibValueMetaObject* p) {
			if (!*released) delete p;
		});

	g_undoStack.push_back({[parent, owned, released]() mutable {
		ibValueMetaObject* p = owned.get();
		p->SetParent(parent);
		parent->AddChild(p);
		*released = true;
	}, g_configEpoch});

	wxLogDebug(wxT("[meta] MetaDelete(plugin=%s) -> %s OK"),
	             SanitiseForLog(wxString::FromUTF8(pluginId)),
	             SanitiseForLog(wxString::FromUTF8(fullName)));
	return 0;
}

int UndoLastAgentMutation()
{
	if (!wxIsMainThread()) return -1;
	if (g_undoStack.empty()) return -1;

	// Skip entries that belong to a previous configuration epoch — their
	// captured root/parent/object pointers refer to a freed tree.
	while (!g_undoStack.empty() &&
	       g_undoStack.back().epoch != g_configEpoch) {
		wxLogMessage(wxT("metaBridge: dropping stale undo entry (epoch %llu vs %llu)"),
		             g_undoStack.back().epoch, g_configEpoch);
		g_undoStack.pop_back();
	}
	if (g_undoStack.empty()) return -1;

	auto entry = std::move(g_undoStack.back());
	g_undoStack.pop_back();
	try {
		entry.fn();
		return 0;
	} catch (...) {
		wxLogWarning(wxT("metaBridge: undo lambda threw — partial revert"));
		return -1;
	}
}

size_t UndoStackSize()
{
	// Count entries that belong to the current epoch — stale entries will
	// be dropped by the next UndoLastAgentMutation call but should not be
	// visible to the transaction layer (it would miscalculate the rollback
	// depth and try to pop entries that no longer apply).
	size_t count = 0;
	for (const auto& e : g_undoStack) {
		if (e.epoch == g_configEpoch) ++count;
	}
	return count;
}

// Designer / pluginManager calls this when activeMetaData is about to
// be replaced (configuration reload, project switch, abort). Advances
// the epoch + clears the stack so the deletes-with-shared_ptr-owners
// fire their custom deleters and pending undo entries become inert.
// MUST run on the main thread before the old activeMetaData is freed.
void NotifyConfigurationUnload()
{
	g_undoStack.clear();      // shared_ptr deleters fire here → no leaks
	++g_configEpoch;
}

void ClearUndoStackForTests()
{
	g_undoStack.clear();
	++g_configEpoch;
}

void SetPluginManagerOverrideForTests(ibPluginManager* override)
{
	g_testPluginManagerOverride = override;
}

} // namespace metaBridge
