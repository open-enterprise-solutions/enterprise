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

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"

#include "3rdparty/nlohmann/json.hpp"

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
