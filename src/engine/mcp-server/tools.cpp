/////////////////////////////////////////////////////////////////////////////
// tools — see header. Each tool wraps an existing host API so the LLM
// gets the same semantics Designer's UI does, without us re-implementing
// the validation / undo / policy plumbing.
/////////////////////////////////////////////////////////////////////////////

#include "tools.h"
#include "headless_app.h"
#include "resources.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/plugin/metaBridge.h"
#include "backend/plugin/pluginApi.h"
#include "backend/plugin/byokEnv.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileContext.h"
#include "backend/testing/testRunner.hpp"
#include "backend/metaCollection/formLayoutBlob.hpp"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/migration/basXmlReader.hpp"
#include "backend/migration/basCfReader.hpp"
#include "backend/metaCollection/metaRoleObject.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/informationRegister.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/accumulationRegisterEnum.h"
#include "backend/metaCollection/partial/enumeration.h"
#include "backend/metaCollection/enumeration/metaEnumObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/session/session.h"

#include <wx/datetime.h>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/cpp-httplib/httplib.h"

#include <wx/string.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace mcpServer {
namespace {

// MCP: pluginId stamped on every meta.* mutation. The headless layer
// pre-grants this id the AllowAlways wildcard so policy gates pass.
constexpr const char* kPluginId = "mcp-server";

// MCP: a single text-content payload — what 90% of tools return. Pass
// `isError=true` for tool-level failures (vs JSON-RPC protocol errors).
nlohmann::json TextResult(const std::string& text, bool isError = false)
{
	nlohmann::json content = nlohmann::json::array();
	nlohmann::json item;
	item["type"] = "text";
	item["text"] = text;
	content.push_back(item);

	nlohmann::json env;
	env["content"] = std::move(content);
	if (isError) env["isError"] = true;
	return env;
}

nlohmann::json JsonResult(const nlohmann::json& payload)
{
	// MCP: the spec only standardises text/image/resource content types.
	// Pretty-printed JSON inside a text block is the established pattern
	// every MCP client (Claude Code, Cursor, JetBrains) renders correctly.
	return TextResult(payload.dump(2), false);
}

// MCP spec 2025-06-18: tools that declare an outputSchema emit
// structuredContent inline (see ToolMetaQuery / ToolListObjects /
// ToolReadModule below). Each tool builds the structured payload itself
// because the text representation is a pretty-JSON dump of the same
// fields — wrapping that in a helper would force two passes.

// Error envelope with a minimal structuredContent.{error} shape — spec
// allows the structured payload to deviate from outputSchema on isError
// (the schema describes success), but emitting *something* keeps the
// agent's branch on `result.structuredContent` exhaustive instead of
// needing a "maybe missing" fallback.
nlohmann::json StructuredError(const std::string& reason)
{
	nlohmann::json env = TextResult(reason, true);
	nlohmann::json err = nlohmann::json::object();
	err["error"] = reason;
	env["structuredContent"] = std::move(err);
	return env;
}

// Free a malloc'd buffer that metaBridge returned via its out-params.
void FreeIfSet(char* p)
{
	if (p != nullptr) std::free(p);
}

// MCP: hard pre-check for every config-bound tool. Returns an error
// envelope when activeMetaData is not live; otherwise nullopt and the
// caller proceeds.
std::optional<nlohmann::json> RequireConfig()
{
	if (!IsReady()) {
		return TextResult("oes-mcp: no configuration loaded — start the server "
		                    "with an OES configuration directory in argv[1] or "
		                    "OES_CONFIG_PATH", true);
	}
	return std::nullopt;
}

// MCP concurrency Layer 2: re-probe the configuration lock at the start of
// every MUTATING tool. Read-only tools don't call this (the session-start
// lock from Layer 1 is enough — read paths see a consistent in-memory
// tree). If an exclusive holder appeared since our session started, or
// our own entry was reaped (unusual, but possible if someone hand-edited
// the manifest), we refuse the mutation with a structured error so the
// agent can retry / surface the issue to the user.
//
// The error envelope shape matches the Designer-concurrency spec:
//   isError: true
//   structuredContent.errorCode = "OES_E_LOCK_LOST"
//   structuredContent.retry = true
std::optional<nlohmann::json> RequireLockStillHeld(const char* toolName)
{
	if (IsLockStillHeld()) return std::nullopt;
	nlohmann::json env = TextResult(
		std::string(toolName) +
		": another process holds exclusive lock — mutation aborted",
		true);
	nlohmann::json sc = nlohmann::json::object();
	sc["errorCode"] = "OES_E_LOCK_LOST";
	sc["retry"]     = true;
	env["structuredContent"] = std::move(sc);
	return env;
}

// MCP: pull a string field from args with a sensible default. Returns
// empty string on missing/wrong-type rather than throwing so individual
// tools can validate with a clearer message.
std::string ArgString(const nlohmann::json& args, const char* key)
{
	if (!args.is_object()) return std::string();
	auto it = args.find(key);
	if (it == args.end() || !it->is_string()) return std::string();
	return it->get<std::string>();
}

bool ArgBool(const nlohmann::json& args, const char* key, bool def = false)
{
	if (!args.is_object()) return def;
	auto it = args.find(key);
	if (it == args.end() || !it->is_boolean()) return def;
	return it->get<bool>();
}

// Forward decls — bodies live next to the tools that introduced them
// (read_module owns ResolveByPath; meta_query borrows it for the comment
// + module enrichment pass).
ibValueMetaObject* ResolveByPath(const std::string& fullName);

// MCP: brute-force CLSID→kind label. We don't have a public exported
// CLSIDToKindString, so we probe metaBridge's reverse map by string. The
// candidate set covers every top-level + child kind the tools surface;
// returns empty string when nothing matches.
const char* CLSIDToKindLabel(unsigned long long cls)
{
	static const char* kCandidates[] = {
		// Top-level business objects
		"Catalog", "Document", "Enumeration", "Constant",
		"InformationRegister", "AccumulationRegister",
		"DataProcessor", "ExternalDataProcessor",
		"Report", "ExternalReport",
		"ChartOfCharacteristicTypes", "ChartOfAccounts",
		"AccountingRegister",
		// Child kinds the agent walks into
		"Attribute", "TabularSection", "Form", "Command",
		"Module", "CommonModule", "ManagerModule",
		nullptr
	};
	for (const char* const* p = kCandidates; *p; ++p) {
		if (metaBridge::KindStringToCLSID(*p) == cls) return *p;
	}
	return "";
}

// MCP: derive any resource URIs that should be invalidated by a mutation
// to `fullName`. Top-level Catalog.X → oes://catalog/X. Any path ending
// in a known module suffix → oes://module/<owner>/<kind>. Mutations on
// nested children (Catalog.X.Attributes.Y) bubble up to the owning
// Catalog so subscribers on the parent get refreshed without per-child
// subscriptions. Always also fires oes://config/current so manifest
// watchers stay live.
void EmitResourceMutation(const std::string& fullName)
{
	EmitResourceUpdated("oes://config/current");
	if (fullName.empty()) return;

	// Split on '.' so we can inspect the head.
	std::vector<std::string> segs;
	std::string cur;
	for (char c : fullName) {
		if (c == '.') { segs.push_back(cur); cur.clear(); }
		else            cur.push_back(c);
	}
	if (!cur.empty()) segs.push_back(cur);
	if (segs.empty()) return;

	// Catalog.X[.anything] → invalidate oes://catalog/X.
	if (segs[0] == "Catalog" && segs.size() >= 2) {
		EmitResourceUpdated("oes://catalog/" + segs[1]);
	}

	// Module-flavoured tail? The known module kinds are the only suffixes
	// that map to a module resource. Anything else doesn't have a per-
	// path resource so we skip.
	if (segs.size() >= 2) {
		const std::string& tail = segs.back();
		const bool moduleTail =
			tail == "ObjectModule" || tail == "ManagerModule" ||
			tail == "Module"       || tail == "CommonModule";
		if (moduleTail) {
			std::string owner;
			for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
				if (!owner.empty()) owner += ".";
				owner += segs[i];
			}
			EmitResourceUpdated("oes://module/" + owner + "/" + tail);
		}
	}
}

// =========================================================================
// Tool: meta_query
// =========================================================================

// MCP: locale-keyed synonym map. metaBridge only surfaces a single string
// today; we emit it under the empty-string key as a "default locale"
// placeholder so the schema's locale→label shape stays honest. When a
// multi-locale property model lands the caller can swap to BCP-47 keys
// without breaking consumers.
nlohmann::json SynonymMap(const std::string& syn)
{
	nlohmann::json m = nlohmann::json::object();
	if (!syn.empty()) m[""] = syn;
	return m;
}

// MCP: build the attribute-shape JSON object matching outputSchema's
// `attributes[]` items. type/length/precision aren't exposed via the
// shallow metaBridge serialiser yet — emit them only when we can pull
// them off the live metaObject. For Phase 3.1 that means "name + synonym",
// type qualifiers wait on a future metaBridge surface.
nlohmann::json BuildAttributeEntry(const ibValueMetaObject* attr)
{
	nlohmann::json a;
	a["name"]    = std::string(attr->GetName().utf8_str());
	a["type"]    = "";  // placeholder — type qualifier surface not exported yet
	a["synonym"] = SynonymMap(std::string(attr->GetSynonym().utf8_str()));
	return a;
}

nlohmann::json ToolMetaQuery(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredError("meta_query: 'fullName' is required "
		                         "(e.g. 'Catalog.Contractors')");
	}

	// First pass — let metaBridge resolve the object and produce the
	// existing JSON payload (we keep emitting it inside `content` for
	// back-compat with clients that don't read structuredContent yet).
	char* jsonOut = nullptr;
	char* errMsg  = nullptr;
	const int rc = metaBridge::HostMetaQuery(fullName.c_str(), nullptr, &jsonOut, &errMsg);
	if (rc != 0) {
		std::string msg = "meta_query failed";
		if (errMsg != nullptr) { msg += ": "; msg += errMsg; }
		FreeIfSet(errMsg); FreeIfSet(jsonOut);
		return StructuredError(msg);
	}
	std::string payload = (jsonOut != nullptr) ? std::string(jsonOut) : std::string("{}");
	FreeIfSet(jsonOut); FreeIfSet(errMsg);

	// Second pass — walk the live node to enrich. metaBridge's flat
	// `children` array bundles every kind together; we split it into the
	// typed buckets outputSchema declares (attributes, tabularSections,
	// forms, commands, modules) without re-querying.
	nlohmann::json structured;
	structured["fullName"] = fullName;

	ibValueMetaObject* node = ResolveByPath(fullName);
	std::string kindLabel;
	std::string synonymStr;
	std::string commentStr;
	if (node != nullptr) {
		kindLabel  = CLSIDToKindLabel(static_cast<unsigned long long>(node->GetClassType()));
		synonymStr = std::string(node->GetSynonym().utf8_str());
		commentStr = std::string(node->GetComment().utf8_str());
	}

	// Fall back to metaBridge's payload fields when ResolveByPath fails
	// (e.g. fullName mapped through a kind alias the path-resolver doesn't
	// recognise) — keeps the structured envelope filled in.
	auto parsed = nlohmann::json::parse(payload, nullptr, false);
	if (parsed.is_object()) {
		if (kindLabel.empty() && parsed.contains("kind") && parsed["kind"].is_string()) {
			kindLabel = parsed["kind"].get<std::string>();
		}
		if (synonymStr.empty() && parsed.contains("synonym") && parsed["synonym"].is_string()) {
			synonymStr = parsed["synonym"].get<std::string>();
		}
	}

	structured["kind"]    = kindLabel;
	structured["synonym"] = SynonymMap(synonymStr);
	structured["comment"] = commentStr;

	nlohmann::json attributes      = nlohmann::json::array();
	nlohmann::json tabularSections = nlohmann::json::array();
	nlohmann::json forms           = nlohmann::json::array();
	nlohmann::json commands        = nlohmann::json::array();
	nlohmann::json modules         = nlohmann::json::array();

	if (node != nullptr) {
		const std::vector<ibValueMetaObject*> all = node->GetAnyArrayObject<>();
		for (const ibValueMetaObject* child : all) {
			if (child == nullptr) continue;
			const unsigned long long cls = static_cast<unsigned long long>(child->GetClassType());
			const std::string childLabel = CLSIDToKindLabel(cls);

			// Bucket assignment — string-compare on the resolved kind label
			// keeps the routing readable and survives any CLSID renumbering.
			if (childLabel == "Attribute") {
				attributes.push_back(BuildAttributeEntry(child));
				continue;
			}
			if (childLabel == "TabularSection") {
				nlohmann::json ts;
				ts["name"]    = std::string(child->GetName().utf8_str());
				ts["synonym"] = SynonymMap(std::string(child->GetSynonym().utf8_str()));
				nlohmann::json tsAttrs = nlohmann::json::array();
				const std::vector<ibValueMetaObject*> tsKids = child->GetAnyArrayObject<>();
				for (const ibValueMetaObject* grand : tsKids) {
					if (grand == nullptr) continue;
					if (CLSIDToKindLabel(static_cast<unsigned long long>(grand->GetClassType()))
					    != std::string("Attribute")) continue;
					tsAttrs.push_back(BuildAttributeEntry(grand));
				}
				ts["attributes"] = std::move(tsAttrs);
				tabularSections.push_back(std::move(ts));
				continue;
			}
			if (childLabel == "Form") {
				nlohmann::json f;
				f["name"] = std::string(child->GetName().utf8_str());
				f["kind"] = childLabel;
				forms.push_back(std::move(f));
				continue;
			}
			if (childLabel == "Command") {
				nlohmann::json c;
				c["name"] = std::string(child->GetName().utf8_str());
				commands.push_back(std::move(c));
				continue;
			}
			// Module / CommonModule / ManagerModule — anything that derives
			// from ibValueMetaObjectModuleBase is a module the agent might
			// want to read via read_module.
			if (dynamic_cast<const ibValueMetaObjectModuleBase*>(child) != nullptr) {
				nlohmann::json m;
				// Translate the OES kind label into the schema's enum:
				//   Module        → ObjectModule (per-instance object code)
				//   ManagerModule → ManagerModule
				//   CommonModule  → ObjectModule (closest fit — common modules
				//                   carry per-object code without a manager)
				// Anything else falls back to CommonModule which is the
				// safest "shared code" bucket.
				if (childLabel == "Module")              m["kind"] = "ObjectModule";
				else if (childLabel == "ManagerModule")  m["kind"] = "ManagerModule";
				else if (childLabel == "CommonModule")   m["kind"] = "ObjectModule";
				else                                      m["kind"] = "ObjectModule";
				modules.push_back(std::move(m));
				continue;
			}
		}
	}

	structured["attributes"]      = std::move(attributes);
	structured["tabularSections"] = std::move(tabularSections);
	structured["forms"]           = std::move(forms);
	structured["commands"]        = std::move(commands);
	structured["modules"]         = std::move(modules);

	// Text content keeps the existing pretty-printed JSON payload so any
	// client that still reads `content[].text` doesn't regress.
	const std::string textBody = parsed.is_discarded() ? payload : parsed.dump(2);
	nlohmann::json env = TextResult(textBody, false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: meta_create
// =========================================================================
nlohmann::json ToolMetaCreate(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	// MCP concurrency Layer 2: lock re-check before any mutation.
	if (auto lockFail = RequireLockStillHeld("meta_create"); lockFail) return *lockFail;
	const std::string kind     = ArgString(args, "kind");
	const std::string fullName = ArgString(args, "fullName");
	if (kind.empty() || fullName.empty()) {
		return TextResult("meta_create: 'kind' and 'fullName' are required", true);
	}
	// MCP: HostMetaCreate accepts a properties payload as JSON text. When
	// the caller passed a JSON object, serialise it; an empty/missing
	// object becomes the literal "{}".
	std::string propsJson = "{}";
	if (args.is_object() && args.contains("properties")) {
		propsJson = args["properties"].dump();
	}
	char* errMsg = nullptr;
	const int rc = metaBridge::HostMetaCreate(kPluginId, kind.c_str(),
		fullName.c_str(), propsJson.c_str(), &errMsg);
	if (rc != 0) {
		std::string msg = "meta_create failed";
		if (errMsg != nullptr) { msg += ": "; msg += errMsg; }
		else if (rc == IB_PLUGIN_PERMISSION_DENIED) {
			msg += ": permission denied (policy gate refused)";
		}
		FreeIfSet(errMsg);
		return TextResult(msg, true);
	}
	FreeIfSet(errMsg);
	// MCP concurrency Layer 3: broadcast successful mutation so Designer's
	// notifier can refresh its tree / surface a toast.
	NotifyMutation("meta_create", fullName);
	EmitResourceMutation(fullName);
	return TextResult("meta_create OK: " + fullName, false);
}

// =========================================================================
// Tool: meta_edit
// =========================================================================
nlohmann::json ToolMetaEdit(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("meta_edit"); lockFail) return *lockFail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return TextResult("meta_edit: 'fullName' is required", true);
	}
	std::string patchJson = "{}";
	if (args.is_object() && args.contains("patch")) {
		patchJson = args["patch"].dump();
	} else if (args.is_object() && args.contains("properties")) {
		// MCP: accept either `patch` (canonical) or `properties` (mirror of
		// meta_create) so the agent's call sites are forgiving.
		patchJson = args["properties"].dump();
	}
	char* errMsg = nullptr;
	const int rc = metaBridge::HostMetaEdit(kPluginId, fullName.c_str(),
		patchJson.c_str(), &errMsg);
	if (rc != 0) {
		std::string msg = "meta_edit failed";
		if (errMsg != nullptr) { msg += ": "; msg += errMsg; }
		else if (rc == IB_PLUGIN_PERMISSION_DENIED) {
			msg += ": permission denied (policy gate refused)";
		}
		FreeIfSet(errMsg);
		return TextResult(msg, true);
	}
	FreeIfSet(errMsg);
	NotifyMutation("meta_edit", fullName);
	EmitResourceMutation(fullName);
	return TextResult("meta_edit OK: " + fullName, false);
}

// =========================================================================
// Tool: meta_delete
// =========================================================================
nlohmann::json ToolMetaDelete(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("meta_delete"); lockFail) return *lockFail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return TextResult("meta_delete: 'fullName' is required", true);
	}
	// MCP: metaBridge requires force=true to actually drop objects —
	// mirrors `rm --no-preserve-root` so a misspoken intent doesn't nuke
	// a Catalog. We pass the flag through verbatim so the caller's intent
	// is auditable.
	nlohmann::json props = nlohmann::json::object();
	if (args.is_object() && args.contains("properties") && args["properties"].is_object()) {
		props = args["properties"];
	}
	if (ArgBool(args, "force")) props["force"] = true;
	const std::string propsJson = props.dump();

	char* errMsg = nullptr;
	const int rc = metaBridge::HostMetaDelete(kPluginId, fullName.c_str(),
		propsJson.c_str(), &errMsg);
	if (rc != 0) {
		std::string msg = "meta_delete failed";
		if (errMsg != nullptr) { msg += ": "; msg += errMsg; }
		else if (rc == IB_PLUGIN_PERMISSION_DENIED) {
			msg += ": permission denied (policy gate refused)";
		}
		FreeIfSet(errMsg);
		return TextResult(msg, true);
	}
	FreeIfSet(errMsg);
	NotifyMutation("meta_delete", fullName);
	EmitResourceMutation(fullName);
	return TextResult("meta_delete OK: " + fullName, false);
}

// =========================================================================
// Tool: list_objects
// =========================================================================
nlohmann::json ToolListObjects(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	const std::string kindFilter = ArgString(args, "kind");
	const std::string namePattern = ArgString(args, "namePattern");
	const unsigned long long clsidFilter = kindFilter.empty()
		? 0ull
		: metaBridge::KindStringToCLSID(kindFilter.c_str());
	if (!kindFilter.empty() && clsidFilter == 0ull) {
		return StructuredError("list_objects: unknown kind '" + kindFilter + "'");
	}

	ibMetaDataConfigurationBase* mc = activeMetaData;
	if (mc == nullptr) return StructuredError("list_objects: no configuration");
	ibValueMetaObject* root = mc->GetCommonMetaObject();
	if (root == nullptr) return StructuredError("list_objects: configuration has no root");

	// MCP: optional namePattern — ECMAScript regex against the object name.
	// Bad pattern → error envelope; empty pattern means "match all".
	std::optional<std::regex> namRx;
	if (!namePattern.empty()) {
		try {
			namRx.emplace(namePattern, std::regex::ECMAScript | std::regex::icase);
		} catch (const std::regex_error& e) {
			return StructuredError(std::string("list_objects: invalid namePattern: ") + e.what());
		}
	}

	nlohmann::json objects = nlohmann::json::array();
	const std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
	for (const ibValueMetaObject* obj : all) {
		if (obj == nullptr) continue;
		const ibClassID cls = obj->GetClassType();
		if (clsidFilter != 0ull && static_cast<unsigned long long>(cls) != clsidFilter) continue;
		const std::string name = std::string(obj->GetName().utf8_str());
		if (namRx && !std::regex_search(name, *namRx)) continue;

		// MCP: outputSchema requires fullName+kind on each entry; skip rows
		// where we can't resolve the kind label (would produce a partial
		// row that fails schema validation downstream).
		const char* kindLabel = CLSIDToKindLabel(static_cast<unsigned long long>(cls));
		if (kindLabel == nullptr || *kindLabel == '\0') continue;

		nlohmann::json item;
		item["fullName"] = std::string(kindLabel) + "." + name;
		item["kind"]     = kindLabel;
		// outputSchema declares synonym as a single string (primary locale
		// label) — keep it flat here. meta_query, which surfaces the full
		// per-locale map, is the place for the richer shape.
		item["synonym"]  = std::string(obj->GetSynonym().utf8_str());
		objects.push_back(std::move(item));
	}

	nlohmann::json filter = nlohmann::json::object();
	if (!kindFilter.empty())   filter["kind"]        = kindFilter;
	if (!namePattern.empty())  filter["namePattern"] = namePattern;

	nlohmann::json structured;
	structured["count"]   = objects.size();
	structured["filter"]  = std::move(filter);
	structured["objects"] = std::move(objects);

	// Preserve the existing text rendering — pretty JSON of the same shape
	// is what existing tests/clients expect inside content[0].text.
	const std::string textBody = structured.dump(2);
	nlohmann::json env = TextResult(textBody, false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: read_module
// Walks the metadata tree to find an object/manager/form module and
// returns its CES/VES source text. fullName shapes:
//   Catalog.X.ObjectModule
//   Catalog.X.ManagerModule
//   Catalog.X.Forms.<FormName>.Module
//   CommonModules.<Name>
// =========================================================================

// MCP: walk dotted path through root's children using ibValueMetaObject
// GetName matching. Returns nullptr when any segment doesn't resolve.
ibValueMetaObject* ResolveByPath(const std::string& fullName)
{
	if (activeMetaData == nullptr) return nullptr;
	ibValueMetaObject* node = activeMetaData->GetCommonMetaObject();
	if (node == nullptr) return nullptr;

	// Split on '.' — same convention as metaBridge.
	std::vector<std::string> segs;
	std::string cur;
	for (char c : fullName) {
		if (c == '.') {
			if (cur.empty()) return nullptr;
			segs.push_back(cur);
			cur.clear();
		} else {
			cur.push_back(c);
		}
	}
	if (!cur.empty()) segs.push_back(cur);
	if (segs.empty()) return nullptr;

	for (const auto& seg : segs) {
		const wxString needle = wxString::FromUTF8(seg.c_str());
		const std::vector<ibValueMetaObject*> children = node->GetAnyArrayObject<>();
		ibValueMetaObject* hit = nullptr;
		for (ibValueMetaObject* child : children) {
			if (child == nullptr) continue;
			if (child->GetName() == needle) { hit = child; break; }
		}
		if (hit == nullptr) return nullptr;
		node = hit;
	}
	return node;
}

nlohmann::json ToolReadModule(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredError("read_module: 'fullName' is required "
		                         "(e.g. 'Catalog.X.ObjectModule')");
	}
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredError("read_module: path not found '" + fullName + "'");
	}
	auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(node);
	if (mod == nullptr) {
		return StructuredError("read_module: '" + fullName +
		                         "' is not a module object");
	}

	const std::string source = std::string(mod->GetModuleText().utf8_str());

	// Map the OES module CLSID to the schema's enum. CommonModule lives
	// at top level (not nested under a Catalog/Document), so we treat it
	// as an ObjectModule for the schema — the path itself disambiguates
	// the location, the enum just classifies the *kind* of source.
	const std::string kindLabel = CLSIDToKindLabel(
		static_cast<unsigned long long>(node->GetClassType()));
	std::string schemaKind = "ObjectModule";
	if      (kindLabel == "ManagerModule") schemaKind = "ManagerModule";
	else if (kindLabel == "Form")          schemaKind = "FormModule";
	else if (kindLabel == "Command")       schemaKind = "CommandModule";
	// Default ("ObjectModule") covers Module + CommonModule.

	// Line count — count '\n' + 1 for the last (potentially unterminated)
	// line. Empty source yields 0.
	unsigned long long lineCount = 0;
	if (!source.empty()) {
		lineCount = 1;
		for (char c : source) if (c == '\n') ++lineCount;
	}

	nlohmann::json structured;
	structured["path"]       = fullName;
	structured["kind"]       = schemaKind;
	// MCP: syntaxMode is a process-global compiler setting (see
	// docs/lambda.md). Modules don't carry their own mode flag yet, so we
	// surface the current global as the best-available signal — callers
	// that need per-module syntax can override via compile_check's own
	// syntaxMode arg.
	structured["syntaxMode"] = (ibCompileCode::GetCodeStyle() == CODE_VES)
		? std::string("ves") : std::string("ces");
	structured["source"]     = source;
	structured["lineCount"]  = lineCount;
	structured["byteSize"]   = static_cast<unsigned long long>(source.size());

	// Text content: keep the existing pretty-JSON rendering of the same
	// payload so the old contract (json-in-text) keeps working.
	nlohmann::json textPayload;
	textPayload["fullName"] = fullName;
	textPayload["source"]   = source;
	nlohmann::json env = TextResult(textPayload.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: write_module
// =========================================================================
nlohmann::json ToolWriteModule(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("write_module"); lockFail) return *lockFail;
	const std::string fullName = ArgString(args, "fullName");
	const std::string source   = ArgString(args, "source");
	if (fullName.empty()) {
		return TextResult("write_module: 'fullName' is required", true);
	}
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return TextResult("write_module: path not found '" + fullName + "'", true);
	}
	auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(node);
	if (mod == nullptr) {
		return TextResult("write_module: '" + fullName +
		                    "' is not a module object", true);
	}
	// MCP: no compile-check yet — Designer's editor invokes ibCompileCode
	// from a wider context (debug server, dependency wiring). We accept
	// the write unconditionally; callers should pair this with
	// `compile_check` when validation matters.
	mod->SetModuleText(wxString::FromUTF8(source.c_str()));
	NotifyMutation("write_module", fullName);
	EmitResourceMutation(fullName);
	return TextResult("write_module OK: " + fullName +
	                    " (" + std::to_string(source.size()) + " bytes)", false);
}

// =========================================================================
// Tool: compile_check
// =========================================================================

// MCP: capturing subclass — overrides DoSetError so we keep the structured
// fields (line / column / message / code) before the base re-throws via
// ProcessError. The compiler is fail-fast: it throws on the FIRST error,
// so at most one entry lands in the errors[] array per call. We honour
// that — multi-error reporting would require a different parser stance.
class CompileCheckCapture : public ibCompileCode {
public:
	CompileCheckCapture(const wxString& moduleName, const wxString& docPath)
		: ibCompileCode(moduleName, docPath, false)
		, m_captured(false)
		, m_capturedCode(0)
		, m_capturedLine(0)
		, m_capturedColumn(0)
	{}

	bool        m_captured;
	int         m_capturedCode;
	unsigned int m_capturedLine;
	unsigned int m_capturedColumn;
	std::string m_capturedDesc;     // formatted error description
	std::string m_capturedRawDesc;  // raw strErrorDesc (without the codeError template)

protected:

	// MCP: column is computed from currPos by walking back to the previous
	// newline in the source buffer (m_strBuffer lives on ibTranslateCode).
	// currLine is already 1-based when SetError forwards from the base
	// translator path.
	void DoSetError(int codeError,
		const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& strErrorDesc) const override
	{
		// Cast away const because the capture members are mutable-by-intent
		// for this debug-only subclass. The base method is `const` so we
		// can override but writing through `this` needs the const-cast.
		auto* self = const_cast<CompileCheckCapture*>(this);
		self->m_captured     = true;
		self->m_capturedCode = codeError;
		self->m_capturedLine = currLine;

		// Column: walk back from currPos to the previous '\n' or buffer
		// start. m_strBuffer is the (uppercased) source buffer; positions
		// are byte offsets into it.
		unsigned int colStart = 0;
		if (!m_strBuffer.empty() && currPos > 0) {
			unsigned int i = (currPos >= m_strBuffer.length())
				? static_cast<unsigned int>(m_strBuffer.length() - 1)
				: currPos;
			for (; i > 0; --i) {
				if (m_strBuffer[i] == wxT('\n')) { colStart = i + 1; break; }
			}
		}
		self->m_capturedColumn = (currPos >= colStart) ? (currPos - colStart + 1) : 1;
		self->m_capturedRawDesc = std::string(strErrorDesc.utf8_str());

		// MCP: re-use base's formatting so the human-readable message
		// matches Designer's compile output. ProcessError throws, which
		// unwinds the parser back to ibCompileCode::Compile's `return
		// false` path. We catch the throw at the call site.
		ibCompileCode::DoSetError(codeError, strFileName, strModuleName,
			strDocPath, currPos, currLine, strErrorDesc);
	}
};

nlohmann::json ToolCompileCheck(const nlohmann::json& args)
{
	const std::string source = ArgString(args, "source");
	if (source.empty()) {
		return TextResult("compile_check: 'source' is required", true);
	}

	// MCP: syntaxMode is the canonical key per the v1 schema; `mode` is
	// accepted as a back-compat alias. Empty / unknown → CES (the platform
	// default for new modules since 2026-05-10).
	std::string syntaxMode = ArgString(args, "syntaxMode");
	if (syntaxMode.empty()) syntaxMode = ArgString(args, "mode");
	std::transform(syntaxMode.begin(), syntaxMode.end(), syntaxMode.begin(),
		[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
	short codeStyle = CODE_CES;
	if (syntaxMode == "ves") codeStyle = CODE_VES;
	else if (!syntaxMode.empty() && syntaxMode != "ces") {
		return TextResult("compile_check: 'syntaxMode' must be 'ces' or 'ves'", true);
	}

	// MCP: real compile needs appData live — the success path of
	// ibCompileCode::Compile fires AfterCompile through appData's plugin
	// manager. Without a loaded configuration appData is nullptr and the
	// success path would segfault. Failure paths don't touch appData, but
	// we refuse the whole call to keep behaviour predictable across both
	// outcomes.
	if (!IsReady() || appData == nullptr) {
		return TextResult("compile_check requires a loaded configuration "
			"(start oes-mcp with a config path; --no-config mode is not "
			"supported for this tool)", true);
	}

	// MCP: optional context — when supplied, we resolve the metadata module
	// to inherit its module-name / doc-path for error attribution. Identifier
	// resolution against parent attributes is not wired here (would require
	// AddContextVariable plumbing matching what ibCompileModule does at load
	// time); we accept the arg for future expansion and surface a note in
	// the structured payload.
	wxString moduleName(wxT("mcp.compile_check"));
	wxString docPath(wxT("memory"));
	std::string contextNote;
	const std::string contextPath = ArgString(args, "context");
	if (!contextPath.empty()) {
		ibValueMetaObject* node = ResolveByPath(contextPath);
		if (node == nullptr) {
			return TextResult("compile_check: context path not found '" +
				contextPath + "'", true);
		}
		if (dynamic_cast<ibValueMetaObjectModuleBase*>(node) == nullptr) {
			return TextResult("compile_check: context '" + contextPath +
				"' is not a module object", true);
		}
		moduleName = node->GetName();
		// MCP: docPath stays "memory" — using the real module guid would
		// trip up ProcessError's activeMetaData lookup, since our source
		// isn't the module's persisted text.
		contextNote = "context attached to '" + contextPath +
			"' for module-name attribution only; identifier resolution "
			"against context attributes is not yet wired";
	}

	// Снимок прежнего синтаксиса — глобал у компилятора, восстановим в finally.
	const short savedStyle = ibCompileCode::GetCodeStyle();
	ibCompileCode::SetCodeStyle(codeStyle);

	CompileCheckCapture cc(moduleName, docPath);
	bool compileOk = false;
	std::string fallbackMsg;
	try {
		compileOk = cc.Compile(wxString::FromUTF8(source.c_str()));
	} catch (const ibBackendException& e) {
		// Capture path — DoSetError already stamped m_captured*. The
		// thrown message goes into fallbackMsg in case the capture
		// didn't fire (e.g. early lexer throw).
		fallbackMsg = std::string(e.GetErrorDescription().utf8_str());
	} catch (const std::exception& e) {
		fallbackMsg = std::string("std::exception during compile: ") + e.what();
	} catch (...) {
		fallbackMsg = "unknown exception during compile";
	}

	// Восстанавливаем глобальный стиль независимо от исхода.
	ibCompileCode::SetCodeStyle(savedStyle);

	if (compileOk && !cc.m_captured) {
		nlohmann::json env;
		nlohmann::json content = nlohmann::json::array();
		nlohmann::json item;
		item["type"] = "text";
		item["text"] = "OK";
		content.push_back(std::move(item));
		env["content"] = std::move(content);

		nlohmann::json structured;
		structured["ok"]       = true;
		structured["warnings"] = nlohmann::json::array();
		if (!contextNote.empty()) structured["note"] = contextNote;
		env["structuredContent"] = std::move(structured);
		return env;
	}

	// Build the structured errors array. With the fail-fast compiler this
	// is always at most one entry.
	nlohmann::json errors = nlohmann::json::array();
	std::string humanMsg;
	if (cc.m_captured) {
		nlohmann::json err;
		err["line"]     = static_cast<unsigned int>(cc.m_capturedLine);
		err["column"]   = static_cast<unsigned int>(cc.m_capturedColumn);
		err["severity"] = "error";
		// Prefer the formatted error description (with the codeError template
		// substituted) — falls back to raw text when the captured raw desc
		// alone already carries the full message.
		std::string msg = std::string(
			ibBackendException::Format(cc.m_capturedCode,
				wxString::FromUTF8(cc.m_capturedRawDesc.c_str())).utf8_str());
		if (msg.empty()) msg = cc.m_capturedRawDesc;
		err["message"]  = msg;
		err["code"]     = cc.m_capturedCode;
		errors.push_back(err);

		humanMsg = "compile error at line " + std::to_string(cc.m_capturedLine) +
		            ", column " + std::to_string(cc.m_capturedColumn) + ": " + msg;
	} else {
		// Fallback path — compile failed without our DoSetError firing.
		// Surface the thrown message verbatim so the agent has something
		// to act on.
		humanMsg = fallbackMsg.empty()
			? std::string("compile_check: unknown failure (no error captured)")
			: ("compile_check: " + fallbackMsg);
		nlohmann::json err;
		err["line"]     = 0;
		err["column"]   = 0;
		err["severity"] = "error";
		err["message"]  = humanMsg;
		err["code"]     = -1;
		errors.push_back(err);
	}

	nlohmann::json env;
	nlohmann::json content = nlohmann::json::array();
	nlohmann::json item;
	item["type"] = "text";
	item["text"] = humanMsg;
	content.push_back(std::move(item));
	env["content"] = std::move(content);
	env["isError"] = true;

	nlohmann::json structured;
	structured["ok"]     = false;
	structured["errors"] = std::move(errors);
	if (!contextNote.empty()) structured["note"] = contextNote;
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: sigma_check — HTTP proxy to Pugi MCP service
//
// Σ-invariant validation lives on the Pugi side (cloud RAG over BAS
// Бухгалтерія 2.1 UA + OES_DEMO). oes-mcp only proxies the request and
// passes the verdict back to the caller.
//
// Configuration (priority order):
//   1. $OES_PUGI_ENDPOINT / $OES_PUGI_TOKEN / $OES_PUGI_TENANT env vars
//   2. <plugins-config-dir>/aiBridge.env (ENDPOINT, TOKEN, TENANT keys)
//
// Fail-open contract: on any transport / connectivity failure the tool
// returns a non-error envelope with structuredContent.ok=true,
// offline=true, warning=... — the caller decides whether to proceed.
// This keeps the Designer save flow usable offline.
// =========================================================================
struct PugiConfig {
	std::string endpoint;
	std::string token;
	std::string tenant;
	bool        loaded = false;
};

std::mutex& PugiConfigMutex()
{
	static std::mutex m;
	return m;
}

PugiConfig& PugiConfigCache()
{
	static PugiConfig cfg;
	return cfg;
}

// MCP: read endpoint/token/tenant from env vars first, then fall back to
// the byokEnv aiBridge.env file. Cached after first successful load so
// subsequent tool calls don't re-stat the env file. A reload simply means
// restarting oes-mcp — Designer's plugin manager owns the live edit path.
PugiConfig LoadPugiConfig()
{
	{
		std::lock_guard<std::mutex> lk(PugiConfigMutex());
		if (PugiConfigCache().loaded) return PugiConfigCache();
	}

	PugiConfig out;

	auto envOrEmpty = [](const char* key) -> std::string {
		const char* v = std::getenv(key);
		return v ? std::string(v) : std::string();
	};
	out.endpoint = envOrEmpty("OES_PUGI_ENDPOINT");
	out.token    = envOrEmpty("OES_PUGI_TOKEN");
	out.tenant   = envOrEmpty("OES_PUGI_TENANT");

	// Fill missing pieces from the aiBridge.env file (byokEnv namespace
	// already handles dotenv parsing, quoted values, perms warnings).
	if (out.endpoint.empty() || out.token.empty() || out.tenant.empty()) {
		const byokEnv::PluginEnv all = byokEnv::LoadAll();
		const std::string pluginId   = "aiBridge";
		if (out.endpoint.empty()) out.endpoint = byokEnv::Get(all, pluginId, "ENDPOINT");
		if (out.token.empty())    out.token    = byokEnv::Get(all, pluginId, "TOKEN");
		if (out.tenant.empty())   out.tenant   = byokEnv::Get(all, pluginId, "TENANT");
	}

	out.loaded = true;
	{
		std::lock_guard<std::mutex> lk(PugiConfigMutex());
		PugiConfigCache() = out;
	}
	return out;
}

// MCP: produce the fail-open envelope. Tool is annotated readOnly=true
// so the agent can keep going — the caller (Designer save flow, agent
// chain) inspects structuredContent.offline to decide whether to gate.
nlohmann::json SigmaOfflineEnvelope(const std::string& reason)
{
	nlohmann::json env;
	nlohmann::json content = nlohmann::json::array();
	nlohmann::json item;
	item["type"] = "text";
	item["text"] = "sigma_check: Pugi unreachable, validation skipped (offline mode): " + reason;
	content.push_back(item);
	env["content"] = std::move(content);

	nlohmann::json structured;
	structured["ok"]      = true;
	structured["offline"] = true;
	structured["warning"] = "validation deferred";
	structured["reason"]  = reason;
	env["structuredContent"] = std::move(structured);
	return env;
}

// MCP: split scheme+host vs path — same trivial parser aiBridge uses.
std::pair<std::string, std::string> PugiSplitUrl(const std::string& url)
{
	const auto schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return {std::string(), std::string()};
	const auto pathStart = url.find('/', schemeEnd + 3);
	if (pathStart == std::string::npos) return {url, "/"};
	return {url.substr(0, pathStart), url.substr(pathStart)};
}

// =========================================================================
// Local RAG fallback chain — when Pugi cloud is unreachable (transport
// error or 5xx), and OES_MCP_RAG_FALLBACK_URL is set, oes-mcp tries the
// local oes-rag-local sidecar before returning the offline envelope.
//
// Wire contract: the sidecar exposes POST /llm and POST /query on a
// loopback HTTP port. We route llm-shaped tool calls to /llm and pass
// through everything else as the raw input on the assumption the sidecar
// understands the same `{name, input}` shape oes-mcp uses.
//
// Stamping: any successful local response gets `_source =
// "local-rag-fallback"` injected into its structuredContent (if present)
// or as a top-level marker, so the caller can tell cloud vs local apart.
//
// Strict allow-list: only tools that benefit from RAG-style retrieval
// (llm_query, sigma_check, oes_template_*) attempt the local fallback.
// Pure metadata tools (meta_query etc.) don't make this round trip —
// they're already local and never hit Pugi in the first place.
// =========================================================================
namespace {

bool ToolIsRagFriendly(const std::string& toolName)
{
	return toolName == "llm_query"
	    || toolName == "sigma_check"
	    || toolName == "oes_templates_list"
	    || toolName == "oes_template_get"
	    || toolName == "oes_template_customize"
	    || toolName == "oes_demo_data_get";
}

// TryLocalRagFallback — POST to the sidecar. Returns true on a 2xx local
// response with *payloadOut populated as a usable MCP envelope. Returns
// false (and leaves *payloadOut untouched) on any error — caller then
// falls through to the offline envelope.
bool TryLocalRagFallback(const std::string& toolName,
                         const nlohmann::json& input,
                         nlohmann::json* payloadOut)
{
	const char* urlEnv = std::getenv("OES_MCP_RAG_FALLBACK_URL");
	if (!urlEnv || !*urlEnv) return false;
	if (!ToolIsRagFriendly(toolName)) return false;

	int timeoutMs = 2000;
	if (const char* tEnv = std::getenv("OES_MCP_RAG_FALLBACK_TIMEOUT_MS")) {
		const int parsed = std::atoi(tEnv);
		if (parsed > 0 && parsed < 60000) timeoutMs = parsed;
	}

	// Split base URL (scheme://host:port) from path. Sidecar exposes
	// fixed endpoint names regardless of toolName; we pick the right
	// one based on whether the tool is LLM-shaped or retrieval-shaped.
	const std::string url = urlEnv;
	const auto schemeEnd  = url.find("://");
	if (schemeEnd == std::string::npos) return false;
	const auto pathStart  = url.find('/', schemeEnd + 3);
	const std::string base = (pathStart == std::string::npos)
	    ? url : url.substr(0, pathStart);

	// Route: llm_query -> /llm (chat-style), everything else -> /query
	// (retrieval-style). v2 may add per-tool routing.
	const char* endpoint = (toolName == "llm_query") ? "/llm" : "/query";

	httplib::Client cli(base);
	cli.set_connection_timeout(0, timeoutMs * 1000);  // microseconds
	cli.set_read_timeout(0, timeoutMs * 1000);
	cli.set_follow_location(false);

	nlohmann::json body;
	if (toolName == "llm_query") {
		// Sidecar /llm expects {prompt, model}; map from Pugi shape if present.
		if (input.is_object()) {
			if (input.contains("prompt")) body["prompt"] = input["prompt"];
			else if (input.contains("text")) body["prompt"] = input["text"];
			if (input.contains("model")) body["model"] = input["model"];
		}
	} else {
		// Retrieval-friendly tools: feed a text query the sidecar can match.
		// We extract any string fields likely to carry user intent; sidecar
		// concatenates them at the substring layer.
		std::string text;
		if (input.is_object()) {
			for (const char* k : { "userPrompt", "query", "text", "templateId" }) {
				if (input.contains(k) && input[k].is_string()) {
					if (!text.empty()) text.push_back(' ');
					text += input[k].get<std::string>();
				}
			}
		}
		body["text"] = text;
		body["topK"] = 5;
	}

	auto res = cli.Post(endpoint, body.dump(), "application/json");
	if (!res || res->status < 200 || res->status >= 300) return false;

	auto parsed = nlohmann::json::parse(res->body, nullptr,
	                                    /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) return false;

	// Sidecar already returns a usable envelope (content[] + optional
	// structuredContent). Stamp the source marker on whichever container
	// is present so callers can tell cloud vs local apart.
	if (parsed.contains("structuredContent") &&
	    parsed["structuredContent"].is_object()) {
		parsed["structuredContent"]["_source"] = "local-rag-fallback";
	} else {
		parsed["_source"] = "local-rag-fallback";
	}
	*payloadOut = std::move(parsed);
	return true;
}

} // namespace

// =========================================================================
// PugiHttpInvoke — shared proxy plumbing for sigma_check + oes_templates_*
// tools.  Validates config, POSTs `{name, input}` to Pugi's invoke endpoint,
// passes the response back through Pugi's `result` unwrap, and folds the
// fail-open contract (transport / missing-creds → caller-shaped envelope).
//
// Degradation chain (when OES_MCP_RAG_FALLBACK_URL is set and the tool is
// RAG-friendly):  Pugi cloud → local oes-rag-local sidecar → offline envelope.
// Triggered on transport errors and 5xx responses; 4xx surfaces directly
// (those are client-side validation errors not retryable locally).
//
// Callers supply:
//   toolName     — used in error strings and the request body's "name" field
//   input        — the JSON object passed through as `input` to Pugi
//   offlineMaker — caller picks the structuredContent shape for fail-open
//                   (sigma_check returns ok=true; new tools return ok=false)
//
// Return: a full MCP `tools/call` result envelope (content[] + optional
// structuredContent + optional isError).  Never throws — every failure
// surfaces as a well-formed envelope so the agent loop keeps running.
// =========================================================================
using PugiOfflineMaker = std::function<nlohmann::json(const std::string& reason)>;

nlohmann::json PugiHttpInvoke(const std::string& toolName,
                              const nlohmann::json& input,
                              const PugiOfflineMaker& offlineMaker)
{
	const PugiConfig cfg = LoadPugiConfig();
	if (cfg.endpoint.empty() || cfg.token.empty() || cfg.tenant.empty()) {
		return offlineMaker(
		    "no Pugi credentials configured (need OES_PUGI_ENDPOINT/TOKEN/TENANT "
		    "or aiBridge.env with ENDPOINT/TOKEN/TENANT)");
	}

	// Build the request body — matches the proven Pugi MCP invocation
	// shape aiBridge uses for its llm_query / triple_review calls:
	//   { "name": "<tool>", "input": { ...args... } }
	// The Pugi gateway rejects {tool, arguments} with a 400, so we mirror
	// what its OpenAPI schema actually accepts.
	nlohmann::json body;
	body["name"]  = toolName;
	body["input"] = input;

	// Scheme + HTTPS precondition. If we were built without OpenSSL the
	// https://mcp.pugi.io endpoint can't even connect — fail open rather
	// than spelunking through cpp-httplib's TLS errors.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
	if (cfg.endpoint.rfind("https://", 0) == 0) {
		return offlineMaker(
		    "oes-mcp built without OpenSSL; cannot reach https:// Pugi endpoint");
	}
#endif

	const auto [base, path] = PugiSplitUrl(cfg.endpoint);
	if (base.empty()) {
		return TextResult(toolName + ": malformed ENDPOINT URL: " + cfg.endpoint, true);
	}

	httplib::Client cli(base);
	cli.set_connection_timeout(5);
	cli.set_read_timeout(10);   // 10s ceiling per spec — stdio loop can't block longer.
	cli.set_follow_location(false);  // SEC: never replay bearer to redirect target.

	httplib::Headers headers = {
		{ "Authorization", "Bearer " + cfg.token },
		{ "X-Tenant-Id",   cfg.tenant            },
		{ "Content-Type",  "application/json"    },
		{ "Accept",        "application/json"    },
	};

	const std::string bodyStr = body.dump();
	auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
	if (!res) {
		// Network / connect / timeout — try local sidecar first (only when
		// OES_MCP_RAG_FALLBACK_URL is set AND tool is RAG-friendly), then
		// fall open to offline envelope.
		nlohmann::json localPayload;
		if (TryLocalRagFallback(toolName, input, &localPayload)) {
			return localPayload;
		}
		return offlineMaker(
		    std::string("transport error: ") + httplib::to_string(res.error()));
	}
	if (res->status >= 300 && res->status < 400) {
		// Redirect with Authorization attached would leak the bearer.
		return TextResult(
		    toolName + ": Pugi returned redirect; bearer not forwarded for security",
		    true);
	}
	if (res->status >= 500) {
		// 5xx — server-side failure, treat like a transport error and
		// try local fallback before failing open. 4xx skips this path
		// (those are client-side validation errors not retryable locally).
		nlohmann::json localPayload;
		if (TryLocalRagFallback(toolName, input, &localPayload)) {
			return localPayload;
		}
		std::string snippet = res->body.size() > 500
		    ? res->body.substr(0, 500) + "..."
		    : res->body;
		return offlineMaker(
		    "Pugi returned " + std::to_string(res->status) + ": " + snippet);
	}
	if (res->status >= 400) {
		// Pugi returned an error envelope. Surface a clean message and
		// truncate the raw body so we don't echo back potentially
		// sensitive request slices.
		std::string snippet = res->body.size() > 500
		    ? res->body.substr(0, 500) + "..."
		    : res->body;
		return TextResult(
		    toolName + ": Pugi returned " + std::to_string(res->status) +
		    ": " + snippet, true);
	}

	// 2xx — parse JSON. On parse failure surface the (truncated) body.
	auto parsed = nlohmann::json::parse(res->body, nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		std::string snippet = res->body.size() > 500
		    ? res->body.substr(0, 500) + "..."
		    : res->body;
		return TextResult(
		    toolName + ": Pugi returned non-JSON response: " + snippet, true);
	}

	// Pugi wraps tool output in {"result": {...}}. The inner result may
	// itself be a full MCP envelope (content[] + structuredContent) OR a
	// flat shape — we accept both. Older test backends sometimes return
	// the envelope at the top level too, so we check both.
	const nlohmann::json* payload = &parsed;
	if (parsed.contains("result") && parsed["result"].is_object()) {
		payload = &parsed["result"];
	}

	nlohmann::json env;
	if (payload->contains("content") && (*payload)["content"].is_array()) {
		env["content"] = (*payload)["content"];
	} else {
		nlohmann::json content = nlohmann::json::array();
		nlohmann::json item;
		item["type"] = "text";
		item["text"] = payload->dump(2);
		content.push_back(item);
		env["content"] = std::move(content);
	}

	if (payload->contains("structuredContent")) {
		env["structuredContent"] = (*payload)["structuredContent"];
		// Mirror ok=false → isError=true so MCP clients that branch on
		// the envelope flag (rather than digging into structuredContent)
		// still see the failure.
		const auto& sc = (*payload)["structuredContent"];
		if (sc.is_object() && sc.contains("ok") && sc["ok"].is_boolean() &&
		    !sc["ok"].get<bool>()) {
			env["isError"] = true;
		}
	}
	return env;
}

// Generic offline envelope for the template-proxy tools: ok=false, offline=true.
// (sigma_check uses a different shape — ok=true, validation deferred — so it
// keeps its own SigmaOfflineEnvelope.)
nlohmann::json PugiOfflineEnvelope(const std::string& toolName, const std::string& reason)
{
	nlohmann::json env;
	nlohmann::json content = nlohmann::json::array();
	nlohmann::json item;
	item["type"] = "text";
	item["text"] = toolName + ": Pugi unreachable, offline mode: " + reason;
	content.push_back(item);
	env["content"] = std::move(content);

	nlohmann::json structured;
	structured["ok"]      = false;
	structured["offline"] = true;
	structured["reason"]  = reason;
	env["structuredContent"] = std::move(structured);
	return env;
}

nlohmann::json ToolSigmaCheck(const nlohmann::json& args)
{
	// Input shape: { metadata: object, moduleCode?: string, rules?: string[] }
	if (!args.is_object() || !args.contains("metadata")) {
		return TextResult("sigma_check: 'metadata' is required", true);
	}

	// sigma_check's offline envelope is "validation deferred, ok=true" — the
	// Designer save flow inspects offline=true to decide whether to gate.
	// New template-proxy tools use the generic ok=false offline envelope,
	// so we keep both shapes side by side.
	nlohmann::json input = nlohmann::json::object();
	input["metadata"] = args["metadata"];
	if (args.contains("moduleCode") && args["moduleCode"].is_string()) {
		input["moduleCode"] = args["moduleCode"];
	}
	if (args.contains("rules") && args["rules"].is_array()) {
		input["rules"] = args["rules"];
	}

	return PugiHttpInvoke("sigma_check", input, SigmaOfflineEnvelope);
}

// =========================================================================
// Pugi template-proxy tools — oes_templates_list / oes_template_get /
// oes_template_customize / oes_demo_data_get.  All four reuse the shared
// PugiHttpInvoke helper; each only differs in how it shapes its `input`
// envelope and which arg validation it performs client-side.
// =========================================================================

// oes_templates_list: enumerates the 4 production OES configuration templates
// (accounting-demo / manufacturing-demo / services-demo / trade-demo).
nlohmann::json ToolOesTemplatesList(const nlohmann::json& args)
{
	nlohmann::json input = nlohmann::json::object();
	if (args.is_object()) {
		if (args.contains("locale") && args["locale"].is_string()) {
			input["locale"] = args["locale"];
		}
		if (args.contains("tags") && args["tags"].is_array()) {
			input["tags"] = args["tags"];
		}
	}
	return PugiHttpInvoke("oes_templates_list", input,
		[](const std::string& reason) {
			return PugiOfflineEnvelope("oes_templates_list", reason);
		});
}

// oes_template_get: fetch full template structure (mutations[]) and optional
// demo data rows.  Trust Pugi's shape — passthrough.
nlohmann::json ToolOesTemplateGet(const nlohmann::json& args)
{
	if (!args.is_object() || !args.contains("templateId") ||
	    !args["templateId"].is_string() || args["templateId"].get<std::string>().empty()) {
		return TextResult("oes_template_get: 'templateId' is required", true);
	}

	nlohmann::json input = nlohmann::json::object();
	input["templateId"] = args["templateId"];
	if (args.contains("includeData") && args["includeData"].is_boolean()) {
		input["includeData"] = args["includeData"];
	} else {
		input["includeData"] = false;  // explicit default per spec
	}
	if (args.contains("locale") && args["locale"].is_string()) {
		input["locale"] = args["locale"];
	}

	return PugiHttpInvoke("oes_template_get", input,
		[](const std::string& reason) {
			return PugiOfflineEnvelope("oes_template_get", reason);
		});
}

// oes_template_customize: clone a template + apply user modifications.
// Hybrid input: explicit `modifications` object OR natural-language
// `userPrompt` (the Sigma agent reasons over the prompt on the Pugi side).
nlohmann::json ToolOesTemplateCustomize(const nlohmann::json& args)
{
	if (!args.is_object() || !args.contains("templateId") ||
	    !args["templateId"].is_string() || args["templateId"].get<std::string>().empty()) {
		return TextResult("oes_template_customize: 'templateId' is required", true);
	}

	nlohmann::json input = nlohmann::json::object();
	input["templateId"] = args["templateId"];
	if (args.contains("modifications") && args["modifications"].is_object()) {
		input["modifications"] = args["modifications"];
	}
	if (args.contains("userPrompt") && args["userPrompt"].is_string()) {
		input["userPrompt"] = args["userPrompt"];
	}

	return PugiHttpInvoke("oes_template_customize", input,
		[](const std::string& reason) {
			return PugiOfflineEnvelope("oes_template_customize", reason);
		});
}

// oes_demo_data_get: fetch demo data rows.  Two mutually-exclusive modes:
//   - templateId  → cached, O(1) Pugi lookup
//   - configHints → LLM-generated, ~3s Anvil call
// Spec says "exactly one of {templateId, configHints} required — validate
// client-side before round-tripping to Pugi" (Pugi may not enforce).
nlohmann::json ToolOesDemoDataGet(const nlohmann::json& args)
{
	const bool hasTemplateId =
	    args.is_object() && args.contains("templateId") &&
	    args["templateId"].is_string() && !args["templateId"].get<std::string>().empty();
	const bool hasConfigHints =
	    args.is_object() && args.contains("configHints") &&
	    args["configHints"].is_object() && !args["configHints"].empty();

	if (hasTemplateId && hasConfigHints) {
		return TextResult(
		    "oes_demo_data_get: provide exactly one of 'templateId' or "
		    "'configHints', not both", true);
	}
	if (!hasTemplateId && !hasConfigHints) {
		return TextResult(
		    "oes_demo_data_get: one of 'templateId' or 'configHints' is "
		    "required", true);
	}

	nlohmann::json input = nlohmann::json::object();
	if (hasTemplateId)  input["templateId"]  = args["templateId"];
	if (hasConfigHints) input["configHints"] = args["configHints"];

	return PugiHttpInvoke("oes_demo_data_get", input,
		[](const std::string& reason) {
			return PugiOfflineEnvelope("oes_demo_data_get", reason);
		});
}

// =========================================================================
// Tool: search_text — substring search across module sources + synonyms.
// =========================================================================
nlohmann::json ToolSearchText(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string query = ArgString(args, "query");
	if (query.empty()) {
		return TextResult("search_text: 'query' is required", true);
	}
	const bool useRegex = ArgBool(args, "regex");

	// MCP: case-insensitive compare for substring mode. We lowercase both
	// sides once. Regex mode honours std::regex flags as-is.
	auto lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		return s;
	};
	const std::string needle = useRegex ? query : lower(query);

	std::optional<std::regex> rx;
	if (useRegex) {
		try {
			rx.emplace(query, std::regex::ECMAScript | std::regex::icase);
		} catch (const std::regex_error& e) {
			return TextResult(std::string("search_text: invalid regex: ") + e.what(), true);
		}
	}

	auto matches = [&](const std::string& haystack) -> bool {
		if (useRegex) return std::regex_search(haystack, *rx);
		return lower(haystack).find(needle) != std::string::npos;
	};

	nlohmann::json hits = nlohmann::json::array();

	// MCP: depth-first walk over the configuration tree. We collect hits
	// against name, synonym, and module text for module-bearing nodes.
	std::function<void(ibValueMetaObject*, const std::string&)> walk;
	walk = [&](ibValueMetaObject* node, const std::string& parentPath) {
		if (node == nullptr) return;
		const std::string name = std::string(node->GetName().utf8_str());
		const std::string path = parentPath.empty() ? name : (parentPath + "." + name);

		const std::string syn = std::string(node->GetSynonym().utf8_str());
		if (matches(name) || matches(syn)) {
			nlohmann::json h;
			h["path"]    = path;
			h["field"]   = matches(name) ? "name" : "synonym";
			h["snippet"] = matches(name) ? name : syn;
			hits.push_back(h);
		}

		if (auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(node)) {
			const std::string src = std::string(mod->GetModuleText().utf8_str());
			if (!src.empty() && matches(src)) {
				// Return a short context snippet around the first hit so
				// the agent doesn't have to fetch the whole module to see
				// what matched.
				const std::string hay = useRegex ? src : lower(src);
				const std::size_t pos = useRegex
					? 0  // regex match offset reported by regex_search is fiddly;
					     // we just point to file start.
					: hay.find(needle);
				const std::size_t begin = (pos > 40) ? pos - 40 : 0;
				const std::size_t len   = std::min<std::size_t>(120, src.size() - begin);
				nlohmann::json h;
				h["path"]    = path + ".module";
				h["field"]   = "source";
				h["snippet"] = src.substr(begin, len);
				hits.push_back(h);
			}
		}

		const std::vector<ibValueMetaObject*> children = node->GetAnyArrayObject<>();
		for (ibValueMetaObject* child : children) {
			walk(child, path);
		}
	};

	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root != nullptr) {
		const std::vector<ibValueMetaObject*> top = root->GetAnyArrayObject<>();
		for (ibValueMetaObject* child : top) walk(child, std::string());
	}

	nlohmann::json out;
	out["query"] = query;
	out["count"] = hits.size();
	out["hits"]  = std::move(hits);
	return JsonResult(out);
}

// =========================================================================
// Tool: save_config
// =========================================================================
nlohmann::json ToolSaveConfig(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("save_config"); lockFail) return *lockFail;
	const std::string path = ArgString(args, "path");
	std::string err;
	if (!SaveConfiguration(path, err)) {
		return TextResult("save_config failed: " + err, true);
	}
	NotifyMutation("save_config", path.empty() ? LoadedConfigPath() : path);
	// MCP resources: a successful save flips the manifest. Subscribed
	// clients on oes://config/current get a fresh ping; the matcher
	// drops it on the floor when no one's listening.
	EmitResourceUpdated("oes://config/current");
	nlohmann::json out;
	out["saved"] = true;
	out["path"]  = path.empty() ? (LoadedConfigPath() + "/config.OES-DB") : path;
	return JsonResult(out);
}

// =========================================================================
// Tool: run_tests
//
// Executes every `@test` procedure across the loaded configuration's
// modules. Each test runs inside a database-transaction fixture so writes
// auto-rollback between tests. Returns a structured report (summary +
// per-test results); failures include actual/expected/assertion shape so
// the agent can surface them without parsing text.
//
// Annotations: readOnly=true (mutates DB but rolled back so net = read);
// destructive=false; idempotent=true; openWorld=false.
// =========================================================================
nlohmann::json ToolRunTests(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	ibTesting::TestRunOptions opts;
	if (args.is_object()) {
		auto fit = args.find("filter");
		if (fit != args.end() && fit->is_string()) {
			opts.filter = wxString::FromUTF8(fit->get<std::string>().c_str());
		}
		auto mit = args.find("modules");
		if (mit != args.end() && mit->is_array()) {
			for (const auto& m : *mit) {
				if (m.is_string()) {
					opts.moduleFilter.push_back(
						wxString::FromUTF8(m.get<std::string>().c_str()));
				}
			}
		}
		auto sit = args.find("stopOnFirstFailure");
		if (sit != args.end() && sit->is_boolean()) {
			opts.stopOnFirstFailure = sit->get<bool>();
		}
	}
	const std::string fmt = ArgString(args, "format");

	ibTesting::TestRun run = ibTesting::RunTests(opts);

	// Top-level fatal — surface as isError with structured envelope so
	// the agent can branch deterministically (matches the existing
	// StructuredError pattern).
	if (!run.error.IsEmpty()) {
		nlohmann::json env = TextResult(
			std::string("run_tests: ") + run.error.utf8_str().data(), true);
		nlohmann::json sc;
		sc["errorCode"] = "OES_E_NO_CONFIG";
		sc["error"]     = std::string(run.error.utf8_str().data());
		env["structuredContent"] = std::move(sc);
		return env;
	}

	auto statusToString = [](ibTesting::TestStatus s) -> const char* {
		switch (s) {
		case ibTesting::TestStatus::Passed:  return "passed";
		case ibTesting::TestStatus::Failed:  return "failed";
		case ibTesting::TestStatus::Error:   return "error";
		case ibTesting::TestStatus::Skipped: return "skipped";
		}
		return "unknown";
	};

	nlohmann::json summary;
	summary["total"]            = run.summary.total;
	summary["passed"]           = run.summary.passed;
	summary["failed"]           = run.summary.failed;
	summary["errored"]          = run.summary.errored;
	summary["skipped"]          = run.summary.skipped;
	summary["durationMs"]       = run.summary.durationMs;
	summary["fixtureDegraded"]  = run.summary.fixtureDegraded;

	nlohmann::json testsArr = nlohmann::json::array();
	for (const auto& r : run.tests) {
		nlohmann::json t;
		t["name"]       = std::string(r.name.utf8_str().data());
		t["procedure"]  = std::string(r.procedure.utf8_str().data());
		t["module"]     = std::string(r.module.utf8_str().data());
		t["status"]     = statusToString(r.status);
		t["durationMs"] = r.durationMs;
		if (r.status == ibTesting::TestStatus::Failed ||
		    r.status == ibTesting::TestStatus::Error) {
			nlohmann::json f;
			f["assertion"] = std::string(r.failure.assertion.utf8_str().data());
			f["actual"]    = std::string(r.failure.actualText.utf8_str().data());
			f["expected"]  = std::string(r.failure.expectedText.utf8_str().data());
			f["message"]   = std::string(r.failure.message.utf8_str().data());
			f["line"]      = r.failure.line;
			t["failure"]   = std::move(f);
		}
		testsArr.push_back(std::move(t));
	}

	nlohmann::json structured;
	structured["summary"] = std::move(summary);
	structured["tests"]   = std::move(testsArr);

	// Text rendition — JSON for `json`/default, "junit" stub for the
	// CI-import path, plain-text human-readable for `text`. JUnit XML
	// dump is intentionally minimal (testsuites/testcase) so a CI
	// runner like GitHub Actions can consume it via xUnit reporter.
	std::string textRender;
	if (fmt == "junit") {
		std::string xml;
		xml.reserve(1024);
		xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
		xml += "<testsuites name=\"oes\" tests=\"" + std::to_string(run.summary.total) +
		        "\" failures=\"" + std::to_string(run.summary.failed) +
		        "\" errors=\"" + std::to_string(run.summary.errored) + "\">\n";
		// Group by module
		std::map<std::string, std::vector<const ibTesting::TestResult*>> byModule;
		for (const auto& r : run.tests) {
			byModule[std::string(r.module.utf8_str().data())].push_back(&r);
		}
		for (const auto& kv : byModule) {
			xml += "  <testsuite name=\"" + kv.first + "\" tests=\"" +
			        std::to_string(kv.second.size()) + "\">\n";
			for (const auto* r : kv.second) {
				xml += "    <testcase classname=\"" + kv.first +
				        "\" name=\"" + std::string(r->name.utf8_str().data()) +
				        "\" time=\"" + std::to_string(r->durationMs / 1000.0) + "\"";
				if (r->status == ibTesting::TestStatus::Passed ||
				    r->status == ibTesting::TestStatus::Skipped) {
					xml += "/>\n";
				} else {
					xml += ">\n      <failure message=\"" +
					        std::string(r->failure.message.utf8_str().data()) + "\"/>\n";
					xml += "    </testcase>\n";
				}
			}
			xml += "  </testsuite>\n";
		}
		xml += "</testsuites>\n";
		textRender = std::move(xml);
	} else if (fmt == "text") {
		std::string txt;
		txt += "Tests: " + std::to_string(run.summary.total) +
		        "  passed: " + std::to_string(run.summary.passed) +
		        "  failed: " + std::to_string(run.summary.failed) +
		        "  errored: " + std::to_string(run.summary.errored) +
		        "  skipped: " + std::to_string(run.summary.skipped) +
		        "  (" + std::to_string(run.summary.durationMs) + "ms)\n";
		for (const auto& r : run.tests) {
			txt += "  ";
			txt += statusToString(r.status);
			txt += "  ";
			txt += std::string(r.module.utf8_str().data());
			txt += " :: ";
			txt += std::string(r.name.utf8_str().data());
			txt += "\n";
			if (r.status == ibTesting::TestStatus::Failed ||
			    r.status == ibTesting::TestStatus::Error) {
				txt += "      ";
				txt += std::string(r.failure.message.utf8_str().data());
				txt += "\n";
			}
		}
		textRender = std::move(txt);
	} else {
		// Default: pretty JSON dump of the structured payload.
		textRender = structured.dump(2);
	}

	nlohmann::json content = nlohmann::json::array();
	nlohmann::json item;
	item["type"] = "text";
	item["text"] = std::move(textRender);
	content.push_back(std::move(item));

	nlohmann::json env;
	env["content"]           = std::move(content);
	env["structuredContent"] = std::move(structured);
	// Carry isError when any test failed/errored so the agent's standard
	// "did this tool succeed?" branch reflects test-suite-as-a-whole.
	if (run.summary.failed > 0 || run.summary.errored > 0) {
		env["isError"] = true;
	}
	return env;
}

// =========================================================================
// Tool: config_info
// =========================================================================
nlohmann::json ToolConfigInfo(const nlohmann::json& /*args*/)
{
	nlohmann::json out;
	out["ready"]       = IsReady();
	out["configPath"]  = LoadedConfigPath();
	if (!IsReady() || activeMetaData == nullptr) {
		return JsonResult(out);
	}
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root != nullptr) {
		const std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
		out["topLevelCount"] = all.size();
		out["name"] = std::string(root->GetName().utf8_str());
	}
	return JsonResult(out);
}

// =========================================================================
// Tool: form_layout_read / form_layout_set
//
// DEFERRED — see `backend/metaCollection/formLayoutBlob.hpp` for the
// architectural blocker. The form data blob held by
// `ibValueMetaObjectFormBase` is a binary chunk format whose per-
// control payloads are deserialised by frontend `ibValueFrame`
// subclasses. Backend has no neutral schema for the property layouts,
// so a backend-side parser would be either a fragile re-implementation
// of all ~25 control classes (option a) or a cross-cutting split of
// `ibValueFrame` into data + visual halves (option b).
//
// These tools still register in `tools/list` so agents see they exist
// and the deferral is visible in `description`. Calls against any
// path return `isError:true` with structuredContent.errorCode set to
// a stable code (`OES_E_FORM_BLOB_GUI_DEPENDENCY` when the form
// exists, `OES_E_NOT_A_FORM` for non-form targets, `OES_E_NOT_FOUND`
// for missing paths, `OES_E_NO_CONFIG` in `--no-config` mode).
//
// When the backing implementation lands, only `ToolFormLayoutRead`
// and `ToolFormLayoutSet` need to change — DTO + validator + tool
// registration stay as-is.
// =========================================================================

nlohmann::json BuildFormLayoutError(const std::string& tool,
                                      const std::string& errorCode,
                                      const std::string& message)
{
	nlohmann::json env = TextResult(tool + ": " + message, true);
	nlohmann::json sc = nlohmann::json::object();
	sc["errorCode"] = errorCode;
	sc["message"]   = message;
	sc["deferred"]  = (errorCode == "OES_E_FORM_BLOB_GUI_DEPENDENCY");
	env["structuredContent"] = std::move(sc);
	return env;
}

// Resolve a form path -> ibValueMetaObjectFormBase*. Returns nullptr
// and fills `failure` with the error envelope on any miss; caller
// just returns the envelope. Every error envelope this helper emits
// carries structuredContent.errorCode so agents can branch
// programmatically — including the --no-config path, where the
// shared `RequireConfig()` envelope is upgraded to a structured form.
ibValueMetaObjectFormBase* ResolveFormByPath(const std::string& tool,
                                               const std::string& fullName,
                                               nlohmann::json& failure)
{
	if (!IsReady()) {
		failure = BuildFormLayoutError(tool, "OES_E_NO_CONFIG",
		                                "no configuration loaded — start the "
		                                "server with an OES configuration "
		                                "directory in argv[1] or "
		                                "OES_CONFIG_PATH");
		return nullptr;
	}
	if (fullName.empty()) {
		failure = BuildFormLayoutError(tool, "OES_E_INVALID_ARG",
		                                "'fullName' is required (e.g. "
		                                "'Catalog.X.Forms.ItemForm')");
		return nullptr;
	}
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		failure = BuildFormLayoutError(tool,
		                                std::string(wxString(ibFormLayoutError::kNotFound).utf8_str()),
		                                "path not found '" + fullName + "'");
		return nullptr;
	}
	auto* formNode = dynamic_cast<ibValueMetaObjectFormBase*>(node);
	if (formNode == nullptr) {
		failure = BuildFormLayoutError(tool,
		                                std::string(wxString(ibFormLayoutError::kNotAForm).utf8_str()),
		                                "'" + fullName + "' is not a form");
		return nullptr;
	}
	return formNode;
}

nlohmann::json ToolFormLayoutRead(const nlohmann::json& args)
{
	const std::string fullName = ArgString(args, "fullName");
	nlohmann::json failure;
	ibValueMetaObjectFormBase* formNode = ResolveFormByPath(
		"form_layout_read", fullName, failure);
	if (formNode == nullptr) return failure;

	// We have a real form. The serializer is deferred; surface that
	// fact deterministically so agents can branch on errorCode.
	const wxMemoryBuffer blob = formNode->GetFormData();
	ibFormLayoutBlob dto;
	wxString errMsg;
	(void)ibFormLayoutSerializer::ParseFromBlob(blob, dto, errMsg);

	const std::string errorCode = std::string(errMsg.utf8_str());
	std::string humanMsg;
	if (errorCode == "OES_E_FORM_BLOB_EMPTY") {
		humanMsg = "form has no stored layout yet (open it once in "
		            "Designer to materialise the blob)";
	} else {
		humanMsg = "form layout requires architectural work to expose "
		            "without GUI deps — see backend/metaCollection/"
		            "formLayoutBlob.hpp";
	}
	return BuildFormLayoutError("form_layout_read", errorCode, humanMsg);
}

nlohmann::json ToolFormLayoutSet(const nlohmann::json& args)
{
	const std::string fullName = ArgString(args, "fullName");
	nlohmann::json failure;
	ibValueMetaObjectFormBase* formNode = ResolveFormByPath(
		"form_layout_set", fullName, failure);
	if (formNode == nullptr) return failure;

	// Mirror the read-side deferral message. We still walk the
	// validator over the incoming `controls` so the surface area is
	// exercised even today — that catches malformed agent input
	// before it would ever reach a real serializer.
	ibFormLayoutBlob dto;
	auto itControls = args.find("controls");
	if (itControls != args.end() && itControls->is_array()) {
		// Minimal walk: each control entry is shape-checked but we
		// don't deeply populate the DTO (no real serializer to feed).
		// Future implementation replaces this stub with a full mapper
		// from JSON -> DTO.
		for (const auto& c : *itControls) {
			if (!c.is_object()) continue;
			ibFormLayoutControl ctrl;
			if (c.contains("id")      && c["id"].is_string())      ctrl.id      = wxString::FromUTF8(c["id"].get<std::string>().c_str());
			if (c.contains("kind")    && c["kind"].is_string())    ctrl.kind    = wxString::FromUTF8(c["kind"].get<std::string>().c_str());
			if (c.contains("name")    && c["name"].is_string())    ctrl.name    = wxString::FromUTF8(c["name"].get<std::string>().c_str());
			if (c.contains("binding") && c["binding"].is_string()) ctrl.binding = wxString::FromUTF8(c["binding"].get<std::string>().c_str());
			dto.controls.push_back(ctrl);
		}
	}
	const auto issues = ibFormLayoutValidator::Validate(dto);
	if (!issues.empty()) {
		nlohmann::json env = TextResult(
			"form_layout_set: input failed DTO validation", true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"] = "OES_E_INVALID_LAYOUT";
		nlohmann::json arr = nlohmann::json::array();
		for (const auto& iss : issues) {
			nlohmann::json e;
			e["code"]    = std::string(iss.code.utf8_str());
			e["message"] = std::string(iss.message.utf8_str());
			e["path"]    = std::string(iss.path.utf8_str());
			arr.push_back(std::move(e));
		}
		sc["issues"] = std::move(arr);
		env["structuredContent"] = std::move(sc);
		return env;
	}

	// DTO accepted. Write path remains deferred.
	return BuildFormLayoutError(
		"form_layout_set",
		std::string(wxString(ibFormLayoutError::kGuiDependency).utf8_str()),
		"form layout writes require architectural work to expose "
		"without GUI deps — DTO validated successfully but the "
		"serializer is deferred; see backend/metaCollection/"
		"formLayoutBlob.hpp");
}

// =========================================================================
// Tool: import_bas_xml — BAS / 1С Configuration.xml -> OES mutations[]
// =========================================================================

// Build the structuredContent envelope shared by both BAS import tools.
nlohmann::json BuildBasImportEnvelope(const migration::bas::ImportResult& r,
                                       bool preview)
{
	nlohmann::json sc = nlohmann::json::object();

	nlohmann::json summary = nlohmann::json::object();
	summary["totalScanned"]    = r.totalScanned;
	summary["imported"]        = r.imported;
	summary["skippedDeleted"]  = r.skippedDeleted;
	summary["skippedDeferred"] = r.skippedDeferred;
	summary["skippedUnknown"]  = r.skippedUnknown;
	summary["skippedFiltered"] = r.skippedFiltered;
	summary["parseFailures"]   = r.parseFailures;

	nlohmann::json counts = nlohmann::json::object();
	for (const auto& kv : r.countsByKind) {
		counts[std::string(kv.first.utf8_str().data())] = kv.second;
	}
	summary["counts"] = std::move(counts);
	sc["summary"]     = std::move(summary);

	nlohmann::json warns = nlohmann::json::array();
	for (const auto& w : r.warnings) {
		warns.push_back(std::string(w.utf8_str().data()));
	}
	sc["warnings"] = std::move(warns);

	// MCP: mutations[] is the payload the wizard Applier consumes.
	// Pass through verbatim — same shape as oes_template_get.
	sc["mutations"] = r.mutations;
	sc["preview"]   = preview;
	return sc;
}

nlohmann::json ToolImportBasXml(const nlohmann::json& args)
{
	// MCP: read-only against the OES configuration (we don't apply
	// mutations here — the wizard Applier does). But we DO require a
	// loaded config so the response can be threaded into oes_template_*
	// pipelines downstream.
	if (auto fail = RequireConfig(); fail) return *fail;

	const std::string configurationPath = ArgString(args, "configurationPath");
	if (configurationPath.empty()) {
		nlohmann::json env = TextResult(
			"import_bas_xml: 'configurationPath' is required", true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"] = "OES_E_BAS_INVALID_INPUT";
		env["structuredContent"] = std::move(sc);
		return env;
	}

	migration::bas::ImportOptions opts;
	opts.configurationPath = wxString::FromUTF8(configurationPath.c_str());
	const std::string objectsRoot = ArgString(args, "objectsRoot");
	if (!objectsRoot.empty()) {
		opts.objectsRoot = wxString::FromUTF8(objectsRoot.c_str());
	}
	opts.skipDeleted = args.is_object() && args.contains("skipDeleted")
		? ArgBool(args, "skipDeleted", true)
		: true;
	opts.preview = ArgBool(args, "preview", false);
	if (args.is_object() && args.contains("objectFilter")
	    && args["objectFilter"].is_array())
	{
		for (const auto& v : args["objectFilter"]) {
			if (v.is_string()) {
				opts.filter.push_back(
					wxString::FromUTF8(v.get<std::string>().c_str()));
			}
		}
	}

	migration::bas::ImportResult r =
		migration::bas::ImportXmlConfiguration(opts);

	if (r.fatal) {
		nlohmann::json env = TextResult(
			std::string("import_bas_xml: ") +
			std::string(r.fatalMsg.utf8_str().data()), true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"] = std::string(r.fatalCode.utf8_str().data());
		env["structuredContent"] = std::move(sc);
		return env;
	}

	nlohmann::json sc = BuildBasImportEnvelope(r, opts.preview);

	nlohmann::json env = TextResult(
		std::string("import_bas_xml: scanned ")
		+ std::to_string(r.totalScanned)
		+ " / imported "
		+ std::to_string(r.imported)
		+ " / deferred "
		+ std::to_string(r.skippedDeferred)
		+ " / deleted "
		+ std::to_string(r.skippedDeleted)
		+ " / warnings "
		+ std::to_string(r.warnings.size()),
		false);
	env["structuredContent"] = std::move(sc);
	return env;
}

// =========================================================================
// Tool: import_bas_cf — binary .cf archive (deferred unpacker)
// =========================================================================

nlohmann::json ToolImportBasCf(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	const std::string cfPath = ArgString(args, "cfPath");
	if (cfPath.empty()) {
		nlohmann::json env = TextResult(
			"import_bas_cf: 'cfPath' is required", true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"] = "OES_E_BAS_INVALID_INPUT";
		env["structuredContent"] = std::move(sc);
		return env;
	}

	migration::bas::CfReadResult r =
		migration::bas::ReadCfArchive(wxString::FromUTF8(cfPath.c_str()));

	// All paths through v1 surface an error envelope — Ok branch is
	// reserved for the future unpacker.
	const bool isError = r.status != migration::bas::CfStatus::Ok;
	nlohmann::json env = TextResult(
		std::string("import_bas_cf: ")
		+ std::string(r.message.utf8_str().data()),
		isError);

	nlohmann::json sc = nlohmann::json::object();
	if (!r.errorCode.empty()) {
		sc["errorCode"] = std::string(r.errorCode.utf8_str().data());
	}
	if (r.status == migration::bas::CfStatus::Ok) {
		sc = BuildBasImportEnvelope(r.import, /*preview=*/false);
	}
	env["structuredContent"] = std::move(sc);
	return env;
}

// =========================================================================
// Role / ACL / Journal / Register / Predefined tools (8) — added 2026-05-21.
//
// Coverage matrix (discovery findings):
//   role_list              : REAL    — ibValueMetaObjectRole exists (MD_ROLE)
//   role_acl_read          : STUB    — Role has no permissions data model yet
//   role_acl_set           : STUB    — same
//   journal_query          : PARTIAL — Document mode REAL via SQL; Journal mode
//                                       returns OES_E_KIND_NOT_SUPPORTED
//                                       (no ibValueMetaObjectJournal class)
//   register_query         : REAL    — records mode via ses_query prepared stmt
//                                       (balance/turnover deferred — manager
//                                       paths are session-scoped and bind to
//                                       value-model tables, not raw JSON)
//   register_write         : STUB    — write path goes through CreateRecordSet
//                                       which is not invokable from the MCP
//                                       boundary; agent should write a posting
//                                       module via write_module instead
//   predefined_values_list : REAL    — Catalog/ChartOfChar via
//                                       GetPredefinedValueArray; Enumeration
//                                       via ibValueMetaObjectEnum children
//   predefined_values_set  : STUB    — append/set/delete API exists on the
//                                       parent class but routing it through
//                                       the metaBridge undo lambda needs an
//                                       Edit-policy extension that hasn't
//                                       landed yet; STUB unlocks the surface.
// =========================================================================

namespace {

// MCP: structured error helper with errorCode set. Callers pass a stable
// OES_E_* code plus a human-readable reason; output schema for these tools
// accepts the same shape across success/error so the agent never branches
// on "maybe missing" structuredContent.
nlohmann::json StructuredErrorCode(const std::string& code, const std::string& reason)
{
	nlohmann::json env = TextResult(reason, true);
	nlohmann::json sc = nlohmann::json::object();
	sc["errorCode"] = code;
	sc["message"]   = reason;
	env["structuredContent"] = std::move(sc);
	return env;
}

// MCP: split "<Kind>.<Name>" head — separate kind label from the bare
// object name so we can look up the live ibValueMetaObject by both the
// CLSID filter (kind) and name match.
std::pair<std::string, std::string> SplitKindAndName(const std::string& fullName)
{
	auto dot = fullName.find('.');
	if (dot == std::string::npos) return { fullName, std::string() };
	return { fullName.substr(0, dot), fullName.substr(dot + 1) };
}

} // namespace

// =========================================================================
// Tool: role_list
// =========================================================================
nlohmann::json ToolRoleList(const nlohmann::json& /*args*/)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	ibMetaDataConfigurationBase* mc = activeMetaData;
	if (mc == nullptr) {
		return StructuredErrorCode("OES_E_NO_CONFIG", "role_list: no configuration");
	}
	ibValueMetaObject* root = mc->GetCommonMetaObject();
	if (root == nullptr) {
		return StructuredErrorCode("OES_E_NO_CONFIG", "role_list: configuration has no root");
	}

	nlohmann::json roles = nlohmann::json::array();
	const std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
	for (const ibValueMetaObject* obj : all) {
		if (obj == nullptr) continue;
		// Filter by CLSID — Role is the only kind we surface here.
		if (obj->GetClassType() != g_metaRoleCLSID) continue;
		nlohmann::json item;
		item["fullName"] = std::string("Role.") + std::string(obj->GetName().utf8_str());
		item["name"]     = std::string(obj->GetName().utf8_str());
		item["synonym"]  = std::string(obj->GetSynonym().utf8_str());
		item["comment"]  = std::string(obj->GetComment().utf8_str());
		roles.push_back(std::move(item));
	}

	nlohmann::json structured;
	structured["count"] = roles.size();
	structured["roles"] = std::move(roles);

	const std::string textBody = structured.dump(2);
	nlohmann::json env = TextResult(textBody, false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: role_acl_read — DEFERRED
// =========================================================================
nlohmann::json ToolRoleAclRead(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"role_acl_read: 'fullName' is required (e.g. 'Role.Administrator')");
	}
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"role_acl_read: role not found: " + fullName);
	}
	if (node->GetClassType() != g_metaRoleCLSID) {
		return StructuredErrorCode("OES_E_NOT_A_ROLE",
			"role_acl_read: object is not a Role: " + fullName);
	}
	// Real impl is blocked on the Role permissions data model — today the
	// platform's access-rights are stored per data-class (Catalog/Document
	// /Register own m_roleRead/m_roleWrite/m_roleDelete pointers to a Role
	// instance) rather than on the Role itself. Surfacing the inverse view
	// requires a global walk + role-pointer-equality probe that bypasses
	// the metadata Save/Load contract; we defer until t2-001-roles ships.
	return StructuredErrorCode("OES_E_NOT_IMPLEMENTED",
		"role_acl_read: Role permissions data model is not yet implemented in "
		"OES backend. Role objects exist as tree nodes but carry no per-"
		"subject permission matrix. Deferred until t2-001-roles ships.");
}

// =========================================================================
// Tool: role_acl_set — DEFERRED
// =========================================================================
nlohmann::json ToolRoleAclSet(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("role_acl_set"); lockFail) return *lockFail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"role_acl_set: 'fullName' is required");
	}
	// Same architectural block as role_acl_read; the write path needs a
	// data model to write into.
	return StructuredErrorCode("OES_E_NOT_IMPLEMENTED",
		"role_acl_set: Role permissions data model is not yet implemented. "
		"Deferred until t2-001-roles ships.");
}

// =========================================================================
// Tool: journal_query — Document-mode REAL, Journal-mode DEFERRED
// =========================================================================
nlohmann::json ToolJournalQuery(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"journal_query: 'fullName' is required (e.g. 'Document.SalesOrder')");
	}

	auto [kindHead, /*objName*/objNameUnused] = SplitKindAndName(fullName);
	(void)objNameUnused;
	if (kindHead == "Journal" || kindHead == "DocumentJournal") {
		// No ibValueMetaObjectJournal class exists in backend yet (the BAS
		// migration table flags DocumentJournal as Deferred). Surface this
		// up-front so the agent doesn't probe further.
		return StructuredErrorCode("OES_E_KIND_NOT_SUPPORTED",
			"journal_query: Journal/DocumentJournal metadata kind is not "
			"implemented in OES backend yet. Use Document.<Name> instead "
			"to query a single document's rows. Deferred via BAS mapping "
			"table.");
	}
	if (kindHead != "Document") {
		return StructuredErrorCode("OES_E_KIND_NOT_SUPPORTED",
			"journal_query: only 'Document.<Name>' is supported today "
			"(got '" + kindHead + "')");
	}

	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"journal_query: document not found: " + fullName);
	}
	auto* doc = dynamic_cast<ibValueMetaObjectDocument*>(node);
	if (doc == nullptr) {
		return StructuredErrorCode("OES_E_NOT_A_DOCUMENT",
			"journal_query: resolved object is not a Document: " + fullName);
	}

	// Filters (all optional)
	wxDateTime dateFromVal;
	wxDateTime dateToVal;
	bool haveFrom = false;
	bool haveTo   = false;
	bool havePosted = false;
	bool postedFilter = false;
	int  limit  = 100;
	int  offset = 0;

	if (args.is_object()) {
		if (args.contains("filters") && args["filters"].is_object()) {
			const auto& f = args["filters"];
			if (f.contains("dateFrom") && f["dateFrom"].is_string()) {
				const wxString s = wxString::FromUTF8(f["dateFrom"].get<std::string>().c_str());
				if (dateFromVal.ParseISOCombined(s) || dateFromVal.ParseISODate(s)) {
					haveFrom = true;
				}
			}
			if (f.contains("dateTo") && f["dateTo"].is_string()) {
				const wxString s = wxString::FromUTF8(f["dateTo"].get<std::string>().c_str());
				if (dateToVal.ParseISOCombined(s) || dateToVal.ParseISODate(s)) {
					haveTo = true;
				}
			}
			if (f.contains("posted") && f["posted"].is_boolean()) {
				havePosted   = true;
				postedFilter = f["posted"].get<bool>();
			}
		}
		if (args.contains("limit") && args["limit"].is_number_integer()) {
			limit = args["limit"].get<int>();
			if (limit < 1)    limit = 1;
			if (limit > 1000) limit = 1000;
		}
		if (args.contains("offset") && args["offset"].is_number_integer()) {
			offset = args["offset"].get<int>();
			if (offset < 0) offset = 0;
		}
	}

	// Build prepared statement. We use the predefined Date/Posted attributes
	// the Document base class exposes; their composite-column names are
	// resolved by GetCompositeSQLFieldName so the same query works across
	// every database driver.
	const ibValueMetaObjectAttributeBase* attrDate    = doc->GetDocumentDate();
	const ibValueMetaObjectAttributeBase* attrPosted  = doc->GetDocumentPosted();

	wxString query = wxT("SELECT * FROM %s");
	bool firstWhere = true;
	if (haveFrom && attrDate) {
		query += firstWhere ? wxT(" WHERE ") : wxT(" AND ");
		query += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attrDate, wxT(">="));
		firstWhere = false;
	}
	if (haveTo && attrDate) {
		query += firstWhere ? wxT(" WHERE ") : wxT(" AND ");
		query += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attrDate, wxT("<="));
		firstWhere = false;
	}
	if (havePosted && attrPosted) {
		query += firstWhere ? wxT(" WHERE ") : wxT(" AND ");
		query += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attrPosted);
		firstWhere = false;
	}

	if (ses_query == nullptr || !ses_query->IsOpen()) {
		return StructuredErrorCode("OES_E_NO_SESSION",
			"journal_query: session database is not open — start oes-mcp "
			"with a real configuration directory");
	}

	ibPreparedStatement* stmt = nullptr;
	try {
		stmt = ses_query->PrepareStatement(query, doc->GetTableNameDB());
	} catch (const ibBackendException& err) {
		return StructuredErrorCode("OES_E_QUERY_FAILED",
			std::string("journal_query: PrepareStatement failed: ")
			+ std::string(err.GetErrorDescription().utf8_str()));
	}
	if (stmt == nullptr) {
		return StructuredErrorCode("OES_E_QUERY_FAILED",
			"journal_query: PrepareStatement returned null");
	}

	int pos = 1;
	if (haveFrom && attrDate) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(attrDate, dateFromVal, stmt, pos);
	}
	if (haveTo && attrDate) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(attrDate, dateToVal, stmt, pos);
	}
	if (havePosted && attrPosted) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(attrPosted, postedFilter, stmt, pos);
	}

	nlohmann::json rows = nlohmann::json::array();
	int totalSeen = 0;
	try {
		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		if (rs != nullptr) {
			while (rs->Next()) {
				++totalSeen;
				if (totalSeen <= offset) continue;
				if (static_cast<int>(rows.size()) >= limit) continue;
				nlohmann::json row = nlohmann::json::object();
				for (const auto* attr : doc->GetGenericAttributeArrayObject()) {
					if (attr == nullptr) continue;
					ibValue cell;
					if (ibValueMetaObjectAttributeBase::GetValueAttribute(attr, cell, rs)) {
						// Stringify — keeps the JSON shape stable across
						// every attribute type without re-implementing
						// ibValue → JSON.
						row[std::string(attr->GetName().utf8_str())] =
							std::string(cell.GetString().utf8_str());
					}
				}
				rows.push_back(std::move(row));
			}
			ses_query->CloseResultSet(rs);
		}
		ses_query->CloseStatement(stmt);
	} catch (const ibBackendException& err) {
		return StructuredErrorCode("OES_E_QUERY_FAILED",
			std::string("journal_query: RunQueryWithResults failed: ")
			+ std::string(err.GetErrorDescription().utf8_str()));
	}

	nlohmann::json structured;
	structured["fullName"] = fullName;
	structured["count"]    = rows.size();
	structured["total"]    = totalSeen;
	structured["limit"]    = limit;
	structured["offset"]   = offset;
	structured["rows"]     = std::move(rows);

	const std::string textBody = structured.dump(2);
	nlohmann::json env = TextResult(textBody, false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: register_query — records-mode REAL; balance/turnover DEFERRED
// =========================================================================
nlohmann::json ToolRegisterQuery(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"register_query: 'fullName' is required "
			"(e.g. 'InformationRegister.PriceList')");
	}
	std::string mode = ArgString(args, "mode");
	if (mode.empty()) mode = "records";

	if (mode != "records") {
		// balance/turnover requires the manager's aggregation pipeline which
		// builds tables seeded with per-register CLSIDs and a live
		// ibValueModelTable target; not invokable from the MCP boundary
		// today. Returning a stable code lets agents choose between waiting
		// for impl or building the aggregate in script via write_module.
		return StructuredErrorCode("OES_E_NOT_SUPPORTED_FOR_KIND",
			"register_query: mode='" + mode + "' is deferred. Only "
			"mode='records' is supported today; use a CES/VES query module "
			"for balance / turnover until the aggregation API lands.");
	}

	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"register_query: register not found: " + fullName);
	}
	auto* reg = dynamic_cast<ibValueMetaObjectRegisterData*>(node);
	if (reg == nullptr) {
		return StructuredErrorCode("OES_E_NOT_A_REGISTER",
			"register_query: resolved object is not a Register: " + fullName);
	}

	int limit = 100;
	if (args.is_object() && args.contains("limit") && args["limit"].is_number_integer()) {
		limit = args["limit"].get<int>();
		if (limit < 1)    limit = 1;
		if (limit > 1000) limit = 1000;
	}

	// Map filter keys (string names from the caller) to dimension attrs.
	std::map<const ibValueMetaObjectAttributeBase*, ibValue> dimFilter;
	if (args.is_object() && args.contains("filters") && args["filters"].is_object()) {
		const auto& f = args["filters"];
		for (const auto* dim : reg->GetDimentionArrayObject()) {
			if (dim == nullptr) continue;
			const std::string dname = std::string(dim->GetName().utf8_str());
			if (!f.contains(dname)) continue;
			const auto& jv = f[dname];
			ibValue iv;
			if (jv.is_string()) {
				iv = ibValue(wxString::FromUTF8(jv.get<std::string>().c_str()));
			} else if (jv.is_number_integer()) {
				iv = ibValue(static_cast<int>(jv.get<long long>()));
			} else if (jv.is_boolean()) {
				iv = ibValue(jv.get<bool>());
			} else {
				continue;
			}
			dimFilter.emplace(dim, std::move(iv));
		}
	}

	if (ses_query == nullptr || !ses_query->IsOpen()) {
		return StructuredErrorCode("OES_E_NO_SESSION",
			"register_query: session database is not open");
	}

	wxString query = wxT("SELECT * FROM %s");
	bool firstWhere = true;
	for (const auto& kv : dimFilter) {
		query += firstWhere ? wxT(" WHERE ") : wxT(" AND ");
		query += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(kv.first);
		firstWhere = false;
	}

	ibPreparedStatement* stmt = nullptr;
	try {
		stmt = ses_query->PrepareStatement(query, reg->GetTableNameDB());
	} catch (const ibBackendException& err) {
		return StructuredErrorCode("OES_E_QUERY_FAILED",
			std::string("register_query: PrepareStatement failed: ")
			+ std::string(err.GetErrorDescription().utf8_str()));
	}
	if (stmt == nullptr) {
		return StructuredErrorCode("OES_E_QUERY_FAILED",
			"register_query: PrepareStatement returned null");
	}

	int pos = 1;
	for (const auto& kv : dimFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(kv.first, kv.second, stmt, pos);
	}

	nlohmann::json rows = nlohmann::json::array();
	int totalSeen = 0;
	try {
		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		if (rs != nullptr) {
			while (rs->Next()) {
				++totalSeen;
				if (static_cast<int>(rows.size()) >= limit) continue;
				nlohmann::json row = nlohmann::json::object();
				for (const auto* attr : reg->GetGenericAttributeArrayObject()) {
					if (attr == nullptr) continue;
					ibValue cell;
					if (ibValueMetaObjectAttributeBase::GetValueAttribute(attr, cell, rs)) {
						row[std::string(attr->GetName().utf8_str())] =
							std::string(cell.GetString().utf8_str());
					}
				}
				rows.push_back(std::move(row));
			}
			ses_query->CloseResultSet(rs);
		}
		ses_query->CloseStatement(stmt);
	} catch (const ibBackendException& err) {
		return StructuredErrorCode("OES_E_QUERY_FAILED",
			std::string("register_query: RunQueryWithResults failed: ")
			+ std::string(err.GetErrorDescription().utf8_str()));
	}

	nlohmann::json structured;
	structured["fullName"] = fullName;
	structured["mode"]     = mode;
	structured["count"]    = rows.size();
	structured["total"]    = totalSeen;
	structured["rows"]     = std::move(rows);

	const std::string textBody = structured.dump(2);
	nlohmann::json env = TextResult(textBody, false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: register_write — DEFERRED
// =========================================================================
nlohmann::json ToolRegisterWrite(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("register_write"); lockFail) return *lockFail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"register_write: 'fullName' is required");
	}
	// Validate target shape so the deferral message is contextual.
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"register_write: register not found: " + fullName);
	}
	if (dynamic_cast<ibValueMetaObjectAccumulationRegister*>(node) == nullptr) {
		return StructuredErrorCode("OES_E_KIND_NOT_SUPPORTED",
			"register_write: only AccumulationRegister is in scope today "
			"(got '" + fullName + "')");
	}
	// Real impl would go through the per-register RecordSet manager
	// (ibValueManagerDataObjectAccumulationRegister) — that path expects a
	// live posting transaction associated with a Document recorder, which
	// the MCP boundary doesn't have. Agents should write a posting
	// procedure via `write_module` against the Document's module instead;
	// that procedure can then call the register's API in CES/VES.
	return StructuredErrorCode("OES_E_NOT_IMPLEMENTED",
		"register_write: direct register writes from MCP are deferred. "
		"AccumulationRegister writes are bound to a Document.Posting() "
		"transaction context which the MCP server does not have. Use "
		"`write_module` to author a CES/VES posting procedure on the "
		"recorder Document instead.");
}

// =========================================================================
// Tool: predefined_values_list — REAL for Catalog/ChartOfChar/Enumeration
// =========================================================================
nlohmann::json ToolPredefinedValuesList(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;

	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"predefined_values_list: 'fullName' is required "
			"(e.g. 'Catalog.X' or 'Enumeration.Y')");
	}
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"predefined_values_list: object not found: " + fullName);
	}

	nlohmann::json predefined = nlohmann::json::array();
	std::string kind;

	// Catalog / ChartOfCharacteristicTypes — values held in
	// m_predefinedObjectVector on the hierarchy-mutable-ref base.
	if (auto* hier = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(node)) {
		kind = "Hierarchy";
		for (const auto& pv : hier->GetPredefinedValueArray()) {
			if (pv == nullptr) continue;
			nlohmann::json item;
			item["name"]        = std::string(pv->GetPredefinedName().utf8_str());
			item["code"]        = std::string(pv->GetPredefinedCode().utf8_str());
			item["description"] = std::string(pv->GetPredefinedDescription().utf8_str());
			item["isFolder"]    = pv->IsPredefinedFolder();
			item["parent"]      = std::string(pv->GetPredefinedParentName().utf8_str());
			predefined.push_back(std::move(item));
		}
	}
	// Enumeration — values are ibValueMetaObjectEnum children in the tree.
	else if (dynamic_cast<ibValueMetaObjectEnumeration*>(node) != nullptr) {
		kind = "Enumeration";
		const std::vector<ibValueMetaObject*> kids = node->GetAnyArrayObject<>();
		for (const ibValueMetaObject* child : kids) {
			if (child == nullptr) continue;
			if (child->GetClassType() != g_metaEnumCLSID) continue;
			nlohmann::json item;
			item["name"]    = std::string(child->GetName().utf8_str());
			item["synonym"] = std::string(child->GetSynonym().utf8_str());
			item["comment"] = std::string(child->GetComment().utf8_str());
			predefined.push_back(std::move(item));
		}
	}
	else {
		return StructuredErrorCode("OES_E_KIND_NOT_SUPPORTED",
			"predefined_values_list: object does not carry predefined "
			"values (supported: Catalog, ChartOfCharacteristicTypes, "
			"Enumeration). Got: " + fullName);
	}

	nlohmann::json structured;
	structured["fullName"]   = fullName;
	structured["kind"]       = kind;
	structured["count"]      = predefined.size();
	structured["predefined"] = std::move(predefined);

	const std::string textBody = structured.dump(2);
	nlohmann::json env = TextResult(textBody, false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: predefined_values_set — DEFERRED
// =========================================================================
nlohmann::json ToolPredefinedValuesSet(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	if (auto lockFail = RequireLockStillHeld("predefined_values_set"); lockFail) return *lockFail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"predefined_values_set: 'fullName' is required");
	}
	// Validate target shape so the deferral message is contextual.
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"predefined_values_set: object not found: " + fullName);
	}
	const bool isHier = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(node) != nullptr;
	const bool isEnum = dynamic_cast<ibValueMetaObjectEnumeration*>(node) != nullptr;
	if (!isHier && !isEnum) {
		return StructuredErrorCode("OES_E_KIND_NOT_SUPPORTED",
			"predefined_values_set: object does not carry predefined values "
			"(supported: Catalog, ChartOfCharacteristicTypes, Enumeration)");
	}
	// Real impl needs an Edit-policy extension that wires the
	// AppendPredefinedValue / SetPredefinedValue / DeletePredefinedValue
	// trio (or the Enumeration child-add path) into a metaBridge undo
	// lambda. The data model exists; the metaBridge routing doesn't.
	return StructuredErrorCode("OES_E_NOT_IMPLEMENTED",
		"predefined_values_set: predefined-value mutation routing through "
		"metaBridge is deferred. Backend has AppendPredefinedValue / "
		"SetPredefinedValue / DeletePredefinedValue, but the policy-gated "
		"undo lambda wiring hasn't landed. Use Designer's Predefined "
		"Values dialog for now.");
}

// =========================================================================
// Registry — built once on first AllTools() call.
// =========================================================================
const std::vector<ToolEntry>& BuildRegistry()
{
	static std::vector<ToolEntry> table;
	if (!table.empty()) return table;

	auto schemaObj = [](std::initializer_list<std::pair<std::string, nlohmann::json>> props,
	                     std::initializer_list<std::string> required) {
		nlohmann::json schema;
		schema["type"]       = "object";
		schema["properties"] = nlohmann::json::object();
		for (const auto& kv : props) schema["properties"][kv.first] = kv.second;
		if (required.size() > 0) {
			nlohmann::json req = nlohmann::json::array();
			for (const auto& r : required) req.push_back(r);
			schema["required"] = std::move(req);
		}
		return schema;
	};
	auto str = [](const std::string& desc) {
		nlohmann::json p; p["type"] = "string"; p["description"] = desc; return p;
	};
	auto obj = [](const std::string& desc) {
		nlohmann::json p; p["type"] = "object"; p["description"] = desc; return p;
	};
	auto boolP = [](const std::string& desc) {
		nlohmann::json p; p["type"] = "boolean"; p["description"] = desc; return p;
	};
	// MCP: hint quad per spec 2025-06-18. Order: readOnly, destructive,
	// idempotent, openWorld. openWorld is false for every tool — oes-mcp is
	// a closed metadata domain bound to the active configuration.
	auto ann = [](bool readOnly, bool destructive, bool idempotent, bool openWorld) {
		ToolAnnotations a;
		a.readOnlyHint    = readOnly;
		a.destructiveHint = destructive;
		a.idempotentHint  = idempotent;
		a.openWorldHint   = openWorld;
		return a;
	};

	{
		// MCP spec 2025-06-18: outputSchema for meta_query. Declares the
		// shape of structuredContent on success — attributes/tabularSections/
		// forms/commands/modules are typed buckets the agent can index into
		// without parsing the text payload.
		nlohmann::json attrItem;
		attrItem["type"]       = "object";
		attrItem["properties"] = nlohmann::json::object();
		attrItem["properties"]["name"]      = nlohmann::json{ {"type","string"} };
		attrItem["properties"]["type"]      = nlohmann::json{ {"type","string"} };
		attrItem["properties"]["length"]    = nlohmann::json{ {"type","integer"} };
		attrItem["properties"]["precision"] = nlohmann::json{ {"type","integer"} };
		attrItem["properties"]["synonym"]   = nlohmann::json{ {"type","object"} };
		attrItem["required"]   = nlohmann::json::array({ "name", "type" });

		nlohmann::json tsItem;
		tsItem["type"]       = "object";
		tsItem["properties"] = nlohmann::json::object();
		tsItem["properties"]["name"]       = nlohmann::json{ {"type","string"} };
		tsItem["properties"]["synonym"]    = nlohmann::json{ {"type","object"} };
		tsItem["properties"]["attributes"] = nlohmann::json{ {"type","array"}, {"items", attrItem} };

		nlohmann::json formItem;
		formItem["type"]       = "object";
		formItem["properties"] = nlohmann::json::object();
		formItem["properties"]["name"] = nlohmann::json{ {"type","string"} };
		formItem["properties"]["kind"] = nlohmann::json{ {"type","string"} };

		nlohmann::json cmdItem;
		cmdItem["type"]       = "object";
		cmdItem["properties"] = nlohmann::json::object();
		cmdItem["properties"]["name"] = nlohmann::json{ {"type","string"} };

		nlohmann::json modItem;
		modItem["type"]       = "object";
		modItem["properties"] = nlohmann::json::object();
		modItem["properties"]["kind"] = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({"ObjectModule","ManagerModule","FormModule","CommandModule"})}
		};

		nlohmann::json synonymProp;
		synonymProp["type"]                 = "object";
		synonymProp["additionalProperties"] = nlohmann::json{ {"type","string"} };
		synonymProp["description"]          = "Locale -> label map";

		nlohmann::json outMQ;
		outMQ["type"]       = "object";
		outMQ["properties"] = nlohmann::json::object();
		outMQ["properties"]["fullName"]        = nlohmann::json{ {"type","string"} };
		outMQ["properties"]["kind"]            = nlohmann::json{ {"type","string"}, {"description","Catalog/Document/InformationRegister/etc"} };
		outMQ["properties"]["synonym"]         = synonymProp;
		outMQ["properties"]["comment"]         = nlohmann::json{ {"type","string"} };
		outMQ["properties"]["attributes"]      = nlohmann::json{ {"type","array"}, {"items", attrItem} };
		outMQ["properties"]["tabularSections"] = nlohmann::json{ {"type","array"}, {"items", tsItem} };
		outMQ["properties"]["forms"]           = nlohmann::json{ {"type","array"}, {"items", formItem} };
		outMQ["properties"]["commands"]        = nlohmann::json{ {"type","array"}, {"items", cmdItem} };
		outMQ["properties"]["modules"]         = nlohmann::json{ {"type","array"}, {"items", modItem} };
		outMQ["required"]   = nlohmann::json::array({ "fullName", "kind" });

		table.push_back({
			{ "meta_query",
			  "Read an OES metadata object's structure as JSON. fullName is "
			  "'<Kind>.<Name>' (e.g. 'Catalog.Контрагенты'). Returns "
			  "structuredContent with typed buckets for attributes, "
			  "tabularSections, forms, commands, modules — no text parsing "
			  "needed.",
			  schemaObj({
			    { "fullName", str("Object full name, e.g. 'Catalog.Контрагенты'") },
			  }, { "fullName" }),
			  ann(true, false, true, false),
			  std::move(outMQ)
			},
			&ToolMetaQuery
		});
	}
	table.push_back({
		{ "meta_create",
		  "Create a new OES metadata object or child (Catalog, Document, "
		  "Form, ItemForm, Attribute, TabularSection, ...).",
		  schemaObj({
		    { "kind",       str("Object kind: Catalog, Document, Form, ItemForm, ...") },
		    { "fullName",   str("Full path, e.g. 'Catalog.X' or 'Catalog.X.Forms.Y'") },
		    { "properties", obj("Object properties (synonym, attributes, controls, etc.)") },
		  }, { "kind", "fullName" }),
		  // Non-destructive (creates new — no prior state lost) but not
		  // idempotent: second call with same fullName errors on duplicate.
		  ann(false, false, false, false)
		},
		&ToolMetaCreate
	});
	table.push_back({
		{ "meta_edit",
		  "Patch an existing metadata object. Accepts either RFC-6902-style "
		  "patches or a plain properties merge.",
		  schemaObj({
		    { "fullName",   str("Full path of the object to edit") },
		    { "patch",      obj("Patch object (or properties merge)") },
		    { "properties", obj("Alias for 'patch'") },
		  }, { "fullName" }),
		  // Non-destructive in the spec sense (no data loss — overwrites
		  // fields); idempotent because re-applying the same patch yields
		  // the same final state.
		  ann(false, false, true, false)
		},
		&ToolMetaEdit
	});
	table.push_back({
		{ "meta_delete",
		  "Delete a metadata object. Requires force=true to actually drop the "
		  "object — mirrors `rm --no-preserve-root`.",
		  schemaObj({
		    { "fullName",   str("Full path of the object to delete") },
		    { "force",      boolP("Must be true to confirm an irreversible delete") },
		    { "properties", obj("Optional extra properties forwarded to HostMetaDelete") },
		  }, { "fullName" }),
		  // Destructive — drops prior state. Idempotent: second delete on a
		  // missing object is a no-op tail (terminal state matches).
		  ann(false, true, true, false)
		},
		&ToolMetaDelete
	});
	{
		// outputSchema for list_objects — count + filter + objects[].
		nlohmann::json objItem;
		objItem["type"]       = "object";
		objItem["properties"] = nlohmann::json::object();
		objItem["properties"]["fullName"] = nlohmann::json{ {"type","string"} };
		objItem["properties"]["kind"]     = nlohmann::json{ {"type","string"} };
		objItem["properties"]["synonym"]  = nlohmann::json{ {"type","string"}, {"description","Primary locale label"} };
		objItem["required"]   = nlohmann::json::array({ "fullName", "kind" });

		nlohmann::json filterProp;
		filterProp["type"]       = "object";
		filterProp["properties"] = nlohmann::json::object();
		filterProp["properties"]["kind"]        = nlohmann::json{ {"type","string"} };
		filterProp["properties"]["namePattern"] = nlohmann::json{ {"type","string"} };

		nlohmann::json outLO;
		outLO["type"]       = "object";
		outLO["properties"] = nlohmann::json::object();
		outLO["properties"]["count"]   = nlohmann::json{ {"type","integer"} };
		outLO["properties"]["filter"]  = std::move(filterProp);
		outLO["properties"]["objects"] = nlohmann::json{ {"type","array"}, {"items", objItem} };
		outLO["required"]   = nlohmann::json::array({ "count", "objects" });

		table.push_back({
			{ "list_objects",
			  "Enumerate top-level metadata objects. Filter by kind to scope the "
			  "result (Catalog / Document / ...). Optional namePattern is an "
			  "ECMAScript regex against the object name. Empty filters return all.",
			  schemaObj({
			    { "kind",        str("Optional kind filter (Catalog, Document, ...). Empty = all.") },
			    { "namePattern", str("Optional ECMAScript regex against object name (case-insensitive)") },
			  }, {}),
			  ann(true, false, true, false),
			  std::move(outLO)
			},
			&ToolListObjects
		});
	}
	{
		// outputSchema for read_module — path/kind/syntaxMode/source +
		// derived line/byte counts so the agent can decide whether to chunk
		// the read without re-fetching.
		nlohmann::json outRM;
		outRM["type"]       = "object";
		outRM["properties"] = nlohmann::json::object();
		outRM["properties"]["path"]       = nlohmann::json{
			{"type","string"},
			{"description","Resolved metadata path, e.g. Catalog.X.ObjectModule"}
		};
		outRM["properties"]["kind"]       = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({"ObjectModule","ManagerModule","FormModule","CommandModule"})}
		};
		outRM["properties"]["syntaxMode"] = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({"ces","ves"})}
		};
		outRM["properties"]["source"]     = nlohmann::json{
			{"type","string"},
			{"description","Full module source text"}
		};
		outRM["properties"]["lineCount"]  = nlohmann::json{ {"type","integer"} };
		outRM["properties"]["byteSize"]   = nlohmann::json{ {"type","integer"} };
		outRM["required"]   = nlohmann::json::array({ "path", "kind", "source" });

		table.push_back({
			{ "read_module",
			  "Read the CES/VES source of an object/manager/common/form module. "
			  "fullName is the dotted path, e.g. 'Catalog.X.ObjectModule' or "
			  "'CommonModules.Y'. Returns structuredContent with path, kind, "
			  "syntaxMode, source, lineCount, byteSize.",
			  schemaObj({
			    { "fullName", str("Dotted module path") },
			  }, { "fullName" }),
			  ann(true, false, true, false),
			  std::move(outRM)
			},
			&ToolReadModule
		});
	}
	table.push_back({
		{ "write_module",
		  "Replace the source of a module object. Pair with compile_check / "
		  "Designer compile pass for validation.",
		  schemaObj({
		    { "fullName", str("Dotted module path") },
		    { "source",   str("New CES/VES source text") },
		  }, { "fullName", "source" }),
		  // Destructive — replaces prior source text. Idempotent: writing the
		  // same source twice produces the same module state.
		  ann(false, true, true, false)
		},
		&ToolWriteModule
	});
	table.push_back({
		{ "compile_check",
		  "Validate CES/VES source via ibCompileCode. On success returns "
		  "structuredContent.ok=true; on failure returns isError:true and "
		  "structuredContent.errors[] with line/column/message/severity. "
		  "Requires a loaded configuration.",
		  schemaObj({
		    { "source",     str("CES or VES source code to validate") },
		    { "syntaxMode", str("'ces' or 'ves' (case-insensitive). Default: ces") },
		    { "mode",       str("Alias for 'syntaxMode' (back-compat)") },
		    { "context",    str("Optional metadata object full name to compile in "
		                          "context (e.g. 'Catalog.X.ObjectModule')") },
		  }, { "source" }),
		  ann(true, false, true, false)
		},
		&ToolCompileCheck
	});
	{
		// sigma_check pulls in array-typed and object-typed parameters that
		// the local lambdas don't cover, so we build the schema inline here.
		nlohmann::json rulesProp;
		rulesProp["type"]        = "array";
		rulesProp["items"]       = nlohmann::json::object();
		rulesProp["items"]["type"] = "string";
		rulesProp["description"] = "Optional rule filter (e.g. ['Σ-unique','Σ-balance'])";
		table.push_back({
			{ "sigma_check",
			  "Validate a metadata snapshot against Σ-invariants via the Pugi "
			  "MCP service (cloud RAG over BAS Бухгалтерія 2.1 UA + OES_DEMO). "
			  "Returns structuredContent.ok plus a violations[] list when rules "
			  "fail. Fails open with offline:true when Pugi is unreachable so "
			  "callers can choose whether to abort or proceed.",
			  schemaObj({
			    { "metadata",   obj("Metadata snapshot to validate (e.g. result of meta_query on Catalog/Document/Register)") },
			    { "moduleCode", str("Optional module source to include in validation context") },
			    { "rules",      rulesProp },
			  }, { "metadata" }),
			  // openWorldHint=true: this tool now reaches an external HTTP
			  // service (mcp.pugi.io), so the agent should treat its result
			  // as dependent on outside state rather than the in-memory config.
			  ann(true, false, true, /*openWorld=*/true)
			},
			&ToolSigmaCheck
		});
	}
	// =====================================================================
	// Pugi template-proxy tools — added 2026-05-21.  All four are HTTP
	// proxies into Pugi's template catalogue / demo-data generator; they
	// reuse the same PugiHttpInvoke helper sigma_check uses (10s timeout,
	// fail-open envelope, bearer-token redirect guard, OpenSSL precondition).
	// openWorldHint=true on every tool because the result depends on external
	// state (Pugi's catalogue + Anvil's LLM generator).
	// =====================================================================
	{
		// oes_templates_list — declares a structured output schema so the
		// agent can index template metadata without parsing the text payload.
		// The other three template tools pass through Pugi's response shape
		// verbatim (Pugi owns the contract there).
		nlohmann::json synonymProp;
		synonymProp["type"]                 = "object";
		synonymProp["additionalProperties"] = nlohmann::json{ {"type","string"} };
		synonymProp["description"]          = "Locale -> label map";

		nlohmann::json tagsArr;
		tagsArr["type"]  = "array";
		tagsArr["items"] = nlohmann::json{ {"type","string"} };

		nlohmann::json previewModulesArr;
		previewModulesArr["type"]  = "array";
		previewModulesArr["items"] = nlohmann::json{ {"type","string"} };

		nlohmann::json thumbnailProp;
		thumbnailProp["type"] = nlohmann::json::array({ "string", "null" });

		nlohmann::json tplItem;
		tplItem["type"]       = "object";
		tplItem["properties"] = nlohmann::json::object();
		tplItem["properties"]["id"]              = nlohmann::json{ {"type","string"} };
		tplItem["properties"]["version"]         = nlohmann::json{ {"type","string"} };
		tplItem["properties"]["name"]            = synonymProp;
		tplItem["properties"]["description"]     = synonymProp;
		tplItem["properties"]["tags"]            = tagsArr;
		tplItem["properties"]["objectCount"]     = nlohmann::json{ {"type","integer"} };
		tplItem["properties"]["demoRowCount"]    = nlohmann::json{ {"type","integer"} };
		tplItem["properties"]["minHostAbi"]      = nlohmann::json{ {"type","integer"} };
		tplItem["properties"]["thumbnailUrl"]    = thumbnailProp;
		tplItem["properties"]["previewModules"]  = previewModulesArr;

		nlohmann::json outList;
		outList["type"]       = "object";
		outList["properties"] = nlohmann::json::object();
		outList["properties"]["templates"] = nlohmann::json{
			{"type", "array"}, {"items", tplItem}
		};
		outList["required"] = nlohmann::json::array({ "templates" });

		// Input schema: optional locale (string|null) + optional tags (string[]).
		nlohmann::json localeIn;
		localeIn["type"]        = nlohmann::json::array({ "string", "null" });
		localeIn["description"] = "Preferred locale tag for name/description (e.g. 'uk', 'ru'); null = server default";
		nlohmann::json tagsIn;
		tagsIn["type"]        = "array";
		tagsIn["items"]       = nlohmann::json{ {"type","string"} };
		tagsIn["description"] = "Optional tag filter (intersection)";

		table.push_back({
			{ "oes_templates_list",
			  "List 4 production OES configuration templates from Pugi: "
			  "accounting-demo, manufacturing-demo, services-demo, "
			  "trade-demo. Returns structuredContent.templates[] with id, "
			  "version, locale-keyed name/description, tags, object/row "
			  "counts, minHostAbi, optional thumbnail and previewModules.",
			  schemaObj({
			    { "locale", localeIn },
			    { "tags",   tagsIn   },
			  }, {}),
			  ann(true, false, true, /*openWorld=*/true),
			  std::move(outList)
			},
			&ToolOesTemplatesList
		});
	}
	{
		// oes_template_get — passthrough (Pugi owns the mutations[]/demoData[]
		// shape).  Input enum guards client-side against unknown template ids
		// so we don't waste a Pugi round-trip on typos.
		nlohmann::json templateIdEnum;
		templateIdEnum["type"] = "string";
		templateIdEnum["enum"] = nlohmann::json::array({
			"accounting-demo", "manufacturing-demo", "services-demo", "trade-demo"
		});
		templateIdEnum["description"] = "Template id (enum-checked client-side)";

		nlohmann::json includeDataProp;
		includeDataProp["type"]        = "boolean";
		includeDataProp["default"]     = false;
		includeDataProp["description"] = "Include demoData rows alongside structure";

		nlohmann::json localeIn;
		localeIn["type"]        = nlohmann::json::array({ "string", "null" });
		localeIn["description"] = "Preferred locale tag for synonyms";

		table.push_back({
			{ "oes_template_get",
			  "Fetch full template structure (mutations[]) and optional demo "
			  "data rows for application via the Pugi proxy. "
			  "Pugi returns {structure:[mutations[]], demoData:[inserts[]]} — "
			  "passthrough shape.",
			  schemaObj({
			    { "templateId",  templateIdEnum  },
			    { "includeData", includeDataProp },
			    { "locale",      localeIn        },
			  }, { "templateId" }),
			  ann(true, false, true, /*openWorld=*/true)
			},
			&ToolOesTemplateGet
		});
	}
	{
		// oes_template_customize — passthrough.  Hybrid input: explicit
		// modifications object OR natural-language userPrompt (or both).
		// `templateId` is required; we don't lock the enum here because Pugi
		// may accept user-cloned template ids in the future.
		nlohmann::json modsProp;
		modsProp["type"]        = "object";
		modsProp["description"] = "Rename map, swap operations, exclude lists — schema defined by Pugi";

		nlohmann::json promptProp;
		promptProp["type"]        = nlohmann::json::array({ "string", "null" });
		promptProp["description"] = "Natural-language tweak request (Sigma agent reasons over it)";

		table.push_back({
			{ "oes_template_customize",
			  "Clone an OES template + apply user modifications via the Pugi "
			  "proxy. Hybrid: explicit `modifications` field OR natural-language "
			  "`userPrompt` (Sigma agent reasons). Returns customized "
			  "mutations[] in Pugi's passthrough shape.",
			  schemaObj({
			    { "templateId",    str("Template id to clone") },
			    { "modifications", modsProp                    },
			    { "userPrompt",    promptProp                  },
			  }, { "templateId" }),
			  // readOnly=false: it generates a new artefact, but destructive=false
			  // (no prior state lost — clone, not overwrite). Idempotent at the
			  // server level: same inputs → same mutations[].
			  ann(false, false, true, /*openWorld=*/true)
			},
			&ToolOesTemplateCustomize
		});
	}
	{
		// oes_demo_data_get — passthrough.  Mutually-exclusive input:
		// templateId (O(1) cached) XOR configHints (LLM-generated, ~3s).
		// Validation is enforced in ToolOesDemoDataGet client-side; Pugi may
		// not enforce so we don't relay a malformed request to it.
		nlohmann::json templateIdProp;
		templateIdProp["type"]        = nlohmann::json::array({ "string", "null" });
		templateIdProp["description"] = "Template id for O(1) cached demo data";

		nlohmann::json hintsProp;
		hintsProp["type"]        = nlohmann::json::array({ "object", "null" });
		hintsProp["description"] = "Schema-aware LLM generation context — describe target config structure";

		table.push_back({
			{ "oes_demo_data_get",
			  "Fetch demo data rows from Pugi. Two modes — provide EXACTLY "
			  "ONE: templateId (cached) OR configHints (LLM-generated, ~3s "
			  "Anvil call). Returns Pugi passthrough shape "
			  "{rows:[{op:'insert', kind, fullName, rows:[...], "
			  "postAfterInsert:bool}]}.",
			  schemaObj({
			    { "templateId",  templateIdProp },
			    { "configHints", hintsProp      },
			  }, {}),
			  ann(true, false, true, /*openWorld=*/true)
			},
			&ToolOesDemoDataGet
		});
	}
	table.push_back({
		{ "search_text",
		  "Full-text search across module sources, names, and synonyms. "
		  "Defaults to case-insensitive substring; pass regex=true for "
		  "ECMAScript regex.",
		  schemaObj({
		    { "query", str("Search query (substring or regex)") },
		    { "regex", boolP("Treat query as ECMAScript regex") },
		  }, { "query" }),
		  ann(true, false, true, false)
		},
		&ToolSearchText
	});
	table.push_back({
		{ "save_config",
		  "Persist the live configuration to a .OES-DB snapshot. Empty path "
		  "writes 'config.OES-DB' next to the loaded configuration directory.",
		  schemaObj({
		    { "path", str("Optional target path for the .OES-DB snapshot") },
		  }, {}),
		  // Writes to disk but the result is a deterministic snapshot of
		  // current in-memory state — not destructive, idempotent.
		  ann(false, false, true, false)
		},
		&ToolSaveConfig
	});
	{
		// outputSchema for run_tests — summary + per-test details, used
		// by the agent to branch without re-parsing the text payload.
		nlohmann::json failureItem;
		failureItem["type"]       = "object";
		failureItem["properties"] = nlohmann::json::object();
		failureItem["properties"]["assertion"] = nlohmann::json{ {"type","string"} };
		failureItem["properties"]["actual"]    = nlohmann::json{ {"type","string"} };
		failureItem["properties"]["expected"]  = nlohmann::json{ {"type","string"} };
		failureItem["properties"]["message"]   = nlohmann::json{ {"type","string"} };
		failureItem["properties"]["line"]      = nlohmann::json{ {"type","integer"} };

		nlohmann::json testItem;
		testItem["type"]       = "object";
		testItem["properties"] = nlohmann::json::object();
		testItem["properties"]["name"]       = nlohmann::json{ {"type","string"} };
		testItem["properties"]["procedure"]  = nlohmann::json{ {"type","string"} };
		testItem["properties"]["module"]     = nlohmann::json{ {"type","string"} };
		testItem["properties"]["status"]     = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({"passed","failed","error","skipped"})}
		};
		testItem["properties"]["durationMs"] = nlohmann::json{ {"type","integer"} };
		testItem["properties"]["failure"]    = failureItem;
		testItem["required"]   = nlohmann::json::array({ "name", "module", "status" });

		nlohmann::json summaryProp;
		summaryProp["type"]       = "object";
		summaryProp["properties"] = nlohmann::json::object();
		summaryProp["properties"]["total"]            = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["passed"]           = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["failed"]           = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["errored"]          = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["skipped"]          = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["durationMs"]       = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["fixtureDegraded"]  = nlohmann::json{ {"type","boolean"} };

		nlohmann::json outRT;
		outRT["type"]       = "object";
		outRT["properties"] = nlohmann::json::object();
		outRT["properties"]["summary"] = std::move(summaryProp);
		outRT["properties"]["tests"]   = nlohmann::json{ {"type","array"}, {"items", testItem} };
		outRT["required"]   = nlohmann::json::array({ "summary", "tests" });

		nlohmann::json filterIn;
		filterIn["type"]        = nlohmann::json::array({ "string", "null" });
		filterIn["description"] = "Glob pattern on test names ('TestОрдер*'). Empty/null = all.";

		nlohmann::json modulesIn;
		modulesIn["type"]        = "array";
		modulesIn["items"]       = nlohmann::json{ {"type","string"} };
		modulesIn["description"] = "Restrict to these module full names";

		nlohmann::json fmtIn;
		fmtIn["type"]        = "string";
		fmtIn["enum"]        = nlohmann::json::array({ "json", "junit", "text" });
		fmtIn["default"]     = "json";
		fmtIn["description"] = "Text-payload format (structuredContent is JSON in all modes)";

		nlohmann::json stopIn;
		stopIn["type"]        = "boolean";
		stopIn["default"]     = false;
		stopIn["description"] = "Stop the run after the first failure/error";

		table.push_back({
			{ "run_tests",
			  "Execute every `// @test`-annotated procedure across the "
			  "loaded configuration's modules. Each test runs inside a "
			  "database-transaction fixture (writes auto-roll-back). "
			  "Returns structuredContent.summary + tests[] with status, "
			  "duration, and failure shape (assertion/actual/expected/"
			  "message/line) on Failed/Error. isError surfaces when any "
			  "test failed or errored so agents can branch deterministically.",
			  schemaObj({
			    { "filter",             filterIn  },
			    { "modules",            modulesIn },
			    { "format",             fmtIn     },
			    { "stopOnFirstFailure", stopIn    },
			  }, {}),
			  // readOnly=true: net effect is read (fixture rollback). But
			  // we DO touch the DB transiently; clients with strict
			  // read-only policy will see this annotation and accept it,
			  // which matches the semantic intent. destructive=false,
			  // idempotent=true (same tests + same inputs = same report),
			  // openWorld=false (no network).
			  ann(/*readOnly*/true, /*destructive*/false, /*idempotent*/true, /*openWorld*/false),
			  std::move(outRT)
			},
			&ToolRunTests
		});
	}

	// MCP: form_layout_read — DEFERRED. The form blob format is
	// readable at the chunk envelope level (backend has the chunk
	// reader) but per-control payloads are deserialised by frontend
	// `ibValueFrame` subclasses; backend has no neutral schema. See
	// `backend/metaCollection/formLayoutBlob.hpp` for the
	// architectural options. Tool registers so agents see it exists.
	{
		nlohmann::json controlItem;
		controlItem["type"]       = "object";
		controlItem["properties"] = nlohmann::json::object();
		controlItem["properties"]["id"]       = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["kind"]     = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["name"]     = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["binding"]  = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["synonym"]  = nlohmann::json{ {"type","object"} };
		controlItem["properties"]["geometry"] = nlohmann::json{ {"type","object"} };
		controlItem["properties"]["children"] = nlohmann::json{ {"type","array"} };

		nlohmann::json outFR;
		outFR["type"]       = "object";
		outFR["properties"] = nlohmann::json::object();
		outFR["properties"]["fullName"] = nlohmann::json{ {"type","string"} };
		outFR["properties"]["formKind"] = nlohmann::json{ {"type","string"} };
		outFR["properties"]["controls"] = nlohmann::json{ {"type","array"}, {"items", controlItem} };
		outFR["properties"]["errorCode"] = nlohmann::json{ {"type","string"} };
		outFR["properties"]["deferred"]  = nlohmann::json{ {"type","boolean"} };

		table.push_back({
			{ "form_layout_read",
			  "Read an OES form's control tree as JSON. fullName is "
			  "'<Kind>.<Name>.Forms.<FormName>' (e.g. "
			  "'Catalog.Контрагенты.Forms.ItemForm'). STATUS: "
			  "DEFERRED — the form data blob is a binary chunk format "
			  "whose per-control payloads are read by frontend control "
			  "classes; backend has no neutral schema. Calls against a "
			  "real form return isError with "
			  "structuredContent.errorCode = "
			  "'OES_E_FORM_BLOB_GUI_DEPENDENCY'. Path-resolution and "
			  "kind errors return their own stable codes "
			  "(OES_E_NOT_FOUND / OES_E_NOT_A_FORM).",
			  schemaObj({
			    { "fullName", str("Form full path, e.g. 'Catalog.X.Forms.ItemForm'") },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false, /*idempotent*/true, /*openWorld*/false),
			  std::move(outFR)
			},
			&ToolFormLayoutRead
		});
	}

	// MCP: form_layout_set — DEFERRED for the same reason. The DTO
	// validator runs against any incoming `controls` so agents can
	// shake out structural input bugs today; the actual write path
	// to the form blob requires the same architectural work as the
	// read side.
	{
		nlohmann::json controlItem;
		controlItem["type"]       = "object";
		controlItem["properties"] = nlohmann::json::object();
		controlItem["properties"]["id"]       = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["kind"]     = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["name"]     = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["binding"]  = nlohmann::json{ {"type","string"} };
		controlItem["properties"]["geometry"] = nlohmann::json{ {"type","object"} };
		controlItem["properties"]["children"] = nlohmann::json{ {"type","array"} };

		nlohmann::json outFS;
		outFS["type"]       = "object";
		outFS["properties"] = nlohmann::json::object();
		outFS["properties"]["ok"]              = nlohmann::json{ {"type","boolean"} };
		outFS["properties"]["controlsApplied"] = nlohmann::json{ {"type","integer"} };
		outFS["properties"]["warnings"]        = nlohmann::json{ {"type","array"}, {"items", nlohmann::json{ {"type","string"} }} };
		outFS["properties"]["errorCode"]       = nlohmann::json{ {"type","string"} };

		nlohmann::json controlsIn;
		controlsIn["type"]        = "array";
		controlsIn["items"]       = controlItem;
		controlsIn["description"] = "Full replacement control tree (depth-first list)";

		table.push_back({
			{ "form_layout_set",
			  "Overwrite an OES form's control tree from JSON. STATUS: "
			  "DEFERRED — the on-disk write path is blocked on the "
			  "same architectural work as form_layout_read. Agent-"
			  "supplied DTOs are still validated (duplicate id, "
			  "missing kind/name, negative geometry) and surfaced as "
			  "OES_E_INVALID_LAYOUT before the deferral message — "
			  "useful for shaking out client-side bugs today. On a "
			  "well-formed DTO the call returns isError with "
			  "structuredContent.errorCode = "
			  "'OES_E_FORM_BLOB_GUI_DEPENDENCY'.",
			  schemaObj({
			    { "fullName", str("Form full path") },
			    { "controls", controlsIn },
			    { "validate", boolP("Run DTO validation (default true)") },
			  }, { "fullName", "controls" }),
			  // destructive=true: when implemented, overwrites the
			  // entire form layout. Idempotent on identical input.
			  ann(/*readOnly*/false, /*destructive*/true, /*idempotent*/true, /*openWorld*/false),
			  std::move(outFS)
			},
			&ToolFormLayoutSet
		});
	}

	// =====================================================================
	// BAS / 1С migration tools — added 2026-05-21. Read external corpus
	// files on disk (Configuration.xml + per-object XML, or binary .cf),
	// emit mutations[] in the same passthrough shape oes_template_get
	// returns so the existing wizard Applier (preview + apply) handles
	// the rest. openWorld=false (local file read), destructive=false
	// (additive only — wizard owns the apply decision).
	// =====================================================================
	{
		nlohmann::json filterArr;
		filterArr["type"]        = "array";
		filterArr["items"]       = nlohmann::json{ {"type","string"} };
		filterArr["description"] =
			"Glob patterns ('Catalog.Контрагенты' or 'Document.*'). "
			"Multiple patterns OR-combine. Empty = no filter.";

		nlohmann::json objectsRootProp;
		objectsRootProp["type"]        = nlohmann::json::array({ "string", "null" });
		objectsRootProp["description"] =
			"Directory holding per-object subtrees. Defaults to dirname(configurationPath).";

		nlohmann::json skipDeletedProp;
		skipDeletedProp["type"]        = "boolean";
		skipDeletedProp["default"]     = true;
		skipDeletedProp["description"] =
			"Skip legacy migration-debt objects whose names start with "
			"'Удалить...' / 'УДАЛИТЬ...' / 'Видалити...' (BAS has ~80).";

		nlohmann::json previewProp;
		previewProp["type"]        = "boolean";
		previewProp["default"]     = false;
		previewProp["description"] =
			"Dry-run flag passed through to the response envelope. The wizard "
			"Applier decides preview vs apply downstream — this tool always "
			"returns mutations[] without applying.";

		// Output schema — the wizard Applier indexes summary.counts +
		// mutations[] without parsing the text payload.
		nlohmann::json mutItem;
		mutItem["type"]       = "object";
		mutItem["properties"] = nlohmann::json::object();
		mutItem["properties"]["op"]         = nlohmann::json{ {"type","string"} };
		mutItem["properties"]["kind"]       = nlohmann::json{ {"type","string"} };
		mutItem["properties"]["fullName"]   = nlohmann::json{ {"type","string"} };
		mutItem["properties"]["properties"] = nlohmann::json{ {"type","object"} };
		mutItem["required"]   = nlohmann::json::array({ "op", "kind", "fullName" });

		nlohmann::json summaryProp;
		summaryProp["type"]       = "object";
		summaryProp["properties"] = nlohmann::json::object();
		summaryProp["properties"]["totalScanned"]    = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["imported"]        = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["skippedDeleted"]  = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["skippedDeferred"] = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["skippedUnknown"]  = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["skippedFiltered"] = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["parseFailures"]   = nlohmann::json{ {"type","integer"} };
		summaryProp["properties"]["counts"]          = nlohmann::json{ {"type","object"} };

		nlohmann::json outXml;
		outXml["type"]       = "object";
		outXml["properties"] = nlohmann::json::object();
		outXml["properties"]["summary"]   = summaryProp;
		outXml["properties"]["mutations"] = nlohmann::json{ {"type","array"}, {"items", mutItem} };
		outXml["properties"]["warnings"]  = nlohmann::json{
			{"type","array"}, {"items", nlohmann::json{ {"type","string"} }} };
		outXml["properties"]["preview"]   = nlohmann::json{ {"type","boolean"} };
		outXml["required"]   = nlohmann::json::array({ "summary", "mutations" });

		table.push_back({
			{ "import_bas_xml",
			  "Import a BAS / 1С Configuration.xml + per-object XML tree, "
			  "emitting mutations[] in the same passthrough shape as "
			  "oes_template_get so the existing wizard Applier handles "
			  "preview + apply. Top-8 OES-supported kinds (Catalog, "
			  "Document, Enumeration, Constant, InformationRegister, "
			  "AccumulationRegister, ChartOfAccounts, "
			  "ChartOfCharacteristicTypes, Report, DataProcessor, "
			  "CommonModule) are emitted directly; Subsystem / Role / "
			  "Form-blob / Template kinds are skipped as DEFERRED "
			  "(structuredContent.warnings explains why).",
			  schemaObj({
			    { "configurationPath", str("Path to root Configuration.xml file") },
			    { "objectsRoot",       objectsRootProp },
			    { "objectFilter",      filterArr },
			    { "skipDeleted",       skipDeletedProp },
			    { "preview",           previewProp },
			  }, { "configurationPath" }),
			  // readOnly=false (we read external files and produce a fresh
			  // artefact, not just a snapshot of in-memory state); but
			  // destructive=false (no prior state lost) and openWorld=false
			  // (local filesystem only). Idempotent at the file level —
			  // same inputs = same mutations[].
			  ann(/*readOnly*/false, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outXml)
			},
			&ToolImportBasXml
		});
	}
	{
		// import_bas_cf — binary archive. Output schema mirrors XML
		// import so a future unpacker can swap in without breaking the
		// agent contract; v1 always returns isError with
		// structuredContent.errorCode = OES_E_BAS_CF_UNSUPPORTED.
		nlohmann::json mutItem;
		mutItem["type"]       = "object";
		mutItem["properties"] = nlohmann::json::object();
		mutItem["properties"]["op"]         = nlohmann::json{ {"type","string"} };
		mutItem["properties"]["kind"]       = nlohmann::json{ {"type","string"} };
		mutItem["properties"]["fullName"]   = nlohmann::json{ {"type","string"} };
		mutItem["properties"]["properties"] = nlohmann::json{ {"type","object"} };

		nlohmann::json outCf;
		outCf["type"]       = "object";
		outCf["properties"] = nlohmann::json::object();
		outCf["properties"]["errorCode"] = nlohmann::json{ {"type","string"} };
		outCf["properties"]["summary"]   = nlohmann::json{ {"type","object"} };
		outCf["properties"]["mutations"] = nlohmann::json{ {"type","array"}, {"items", mutItem} };
		outCf["properties"]["warnings"]  = nlohmann::json{
			{"type","array"}, {"items", nlohmann::json{ {"type","string"} }} };

		table.push_back({
			{ "import_bas_cf",
			  "Import a BAS / 1С binary .cf archive. STATUS: DEFERRED — "
			  ".cf is a proprietary LZF-compressed container; v1 detects "
			  "the magic header and returns isError with structuredContent."
			  "errorCode='OES_E_BAS_CF_UNSUPPORTED' plus actionable guidance "
			  "(open in 1С/BAS Configurator, run 'Dump configuration files' "
			  "to export XML, then call import_bas_xml on the result).",
			  schemaObj({
			    { "cfPath", str("Filesystem path to the .cf archive") },
			  }, { "cfPath" }),
			  ann(/*readOnly*/false, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outCf)
			},
			&ToolImportBasCf
		});
	}

	// =====================================================================
	// Role / ACL / Journal / Register / Predefined tools — added 2026-05-21.
	// Five tools register an outputSchema (the read-only ones). Three
	// mutators (role_acl_set, register_write, predefined_values_set) are
	// deferred stubs that still register so the surface is complete.
	// =====================================================================
	{
		// role_list — read-only enumeration. Output schema is small: one
		// fullName/name/synonym/comment record per Role.
		nlohmann::json roleItem;
		roleItem["type"]       = "object";
		roleItem["properties"] = nlohmann::json::object();
		roleItem["properties"]["fullName"] = nlohmann::json{ {"type","string"} };
		roleItem["properties"]["name"]     = nlohmann::json{ {"type","string"} };
		roleItem["properties"]["synonym"]  = nlohmann::json{ {"type","string"} };
		roleItem["properties"]["comment"]  = nlohmann::json{ {"type","string"} };
		roleItem["required"]   = nlohmann::json::array({ "fullName", "name" });

		nlohmann::json outRL;
		outRL["type"]       = "object";
		outRL["properties"] = nlohmann::json::object();
		outRL["properties"]["count"] = nlohmann::json{ {"type","integer"} };
		outRL["properties"]["roles"] = nlohmann::json{ {"type","array"}, {"items", roleItem} };
		outRL["required"]   = nlohmann::json::array({ "count", "roles" });

		table.push_back({
			{ "role_list",
			  "Enumerate every Role metadata object in the loaded "
			  "configuration. Returns structuredContent.roles[] with "
			  "fullName/name/synonym/comment. Role permissions matrix "
			  "(subjects -> rights) is exposed by role_acl_read (currently "
			  "deferred — Role objects exist as tree nodes but carry no "
			  "permissions data model yet).",
			  schemaObj({}, {}),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outRL)
			},
			&ToolRoleList
		});
	}
	{
		// role_acl_read — DEFERRED but advertises outputSchema so the agent
		// can branch on errorCode without re-parsing the text payload.
		nlohmann::json permItem;
		permItem["type"]       = "object";
		permItem["properties"] = nlohmann::json::object();
		permItem["properties"]["object"]  = nlohmann::json{ {"type","string"} };
		permItem["properties"]["right"]   = nlohmann::json{ {"type","string"} };
		permItem["properties"]["granted"] = nlohmann::json{ {"type","boolean"} };
		permItem["properties"]["rls"]     = nlohmann::json{ {"type","string"} };

		nlohmann::json outRR;
		outRR["type"]       = "object";
		outRR["properties"] = nlohmann::json::object();
		outRR["properties"]["role"]        = nlohmann::json{ {"type","string"} };
		outRR["properties"]["permissions"] = nlohmann::json{ {"type","array"}, {"items", permItem} };
		outRR["properties"]["errorCode"]   = nlohmann::json{ {"type","string"} };
		outRR["properties"]["message"]     = nlohmann::json{ {"type","string"} };

		table.push_back({
			{ "role_acl_read",
			  "Read the access-rights matrix for a Role (subject objects -> "
			  "rights -> granted/RLS). STATUS: DEFERRED — Role objects "
			  "currently carry no permission matrix in the OES metadata "
			  "model; access rights are stored per data-class "
			  "(Catalog/Document/Register own m_roleRead/m_roleWrite/"
			  "m_roleDelete pointers). Calls return isError with "
			  "structuredContent.errorCode='OES_E_NOT_IMPLEMENTED' until "
			  "t2-001-roles ships.",
			  schemaObj({
			    { "fullName", str("Role full path, e.g. 'Role.Administrator'") },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outRR)
			},
			&ToolRoleAclRead
		});
	}
	{
		// role_acl_set — DEFERRED, mutation half of role_acl_read.
		nlohmann::json permItem;
		permItem["type"]       = "object";
		permItem["properties"] = nlohmann::json::object();
		permItem["properties"]["object"]  = nlohmann::json{ {"type","string"} };
		permItem["properties"]["right"]   = nlohmann::json{ {"type","string"} };
		permItem["properties"]["granted"] = nlohmann::json{ {"type","boolean"} };
		permItem["properties"]["rls"]     = nlohmann::json{ {"type","string"} };

		nlohmann::json permsIn;
		permsIn["type"]        = "array";
		permsIn["items"]       = permItem;
		permsIn["description"] = "Full replacement permission matrix";

		table.push_back({
			{ "role_acl_set",
			  "Replace the access-rights matrix for a Role. STATUS: "
			  "DEFERRED — same architectural block as role_acl_read; the "
			  "platform has no per-Role permission data model to write "
			  "into yet. Returns isError with "
			  "structuredContent.errorCode='OES_E_NOT_IMPLEMENTED'.",
			  schemaObj({
			    { "fullName",    str("Role full path") },
			    { "permissions", permsIn },
			  }, { "fullName", "permissions" }),
			  ann(/*readOnly*/false, /*destructive*/true,
			      /*idempotent*/true, /*openWorld*/false)
			},
			&ToolRoleAclSet
		});
	}
	{
		// journal_query — Document-mode REAL, Journal-mode DEFERRED.
		nlohmann::json rowItem;
		rowItem["type"]                 = "object";
		rowItem["additionalProperties"] = true;
		rowItem["description"]          = "Document row — keys mirror the document's attribute names";

		nlohmann::json filtersProp;
		filtersProp["type"]       = "object";
		filtersProp["properties"] = nlohmann::json::object();
		filtersProp["properties"]["dateFrom"] = nlohmann::json{
			{"type", nlohmann::json::array({"string","null"})},
			{"description","ISO-8601 date or datetime (inclusive lower bound)"}
		};
		filtersProp["properties"]["dateTo"]   = nlohmann::json{
			{"type", nlohmann::json::array({"string","null"})},
			{"description","ISO-8601 date or datetime (inclusive upper bound)"}
		};
		filtersProp["properties"]["posted"]   = nlohmann::json{
			{"type", nlohmann::json::array({"boolean","null"})},
			{"description","Posted flag filter"}
		};
		filtersProp["properties"]["type"]     = nlohmann::json{
			{"type", nlohmann::json::array({"string","null"})},
			{"description","Document type filter — reserved for future Journal mode"}
		};

		nlohmann::json outJQ;
		outJQ["type"]       = "object";
		outJQ["properties"] = nlohmann::json::object();
		outJQ["properties"]["fullName"] = nlohmann::json{ {"type","string"} };
		outJQ["properties"]["count"]    = nlohmann::json{ {"type","integer"} };
		outJQ["properties"]["total"]    = nlohmann::json{ {"type","integer"} };
		outJQ["properties"]["limit"]    = nlohmann::json{ {"type","integer"} };
		outJQ["properties"]["offset"]   = nlohmann::json{ {"type","integer"} };
		outJQ["properties"]["rows"]     = nlohmann::json{ {"type","array"}, {"items", rowItem} };
		outJQ["properties"]["errorCode"]= nlohmann::json{ {"type","string"} };

		table.push_back({
			{ "journal_query",
			  "Read rows from a Document table with date/posted filters. "
			  "fullName='Document.<Name>' is REAL today; "
			  "fullName='Journal.<Name>' / 'DocumentJournal.<Name>' returns "
			  "structuredContent.errorCode='OES_E_KIND_NOT_SUPPORTED' "
			  "because OES has no ibValueMetaObjectJournal class yet (BAS "
			  "migration table flags DocumentJournal as Deferred). All "
			  "filters use ibPreparedStatement — no SQL injection.",
			  schemaObj({
			    { "fullName", str("Document or Journal full name") },
			    { "filters",  filtersProp },
			    { "limit",    nlohmann::json{ {"type","integer"}, {"default", 100}, {"description","Max rows to return (1..1000)"} } },
			    { "offset",   nlohmann::json{ {"type","integer"}, {"default", 0},   {"description","Row offset (>=0)"} } },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outJQ)
			},
			&ToolJournalQuery
		});
	}
	{
		// register_query — records-mode REAL.
		nlohmann::json rowItem;
		rowItem["type"]                 = "object";
		rowItem["additionalProperties"] = true;
		rowItem["description"]          = "Register row — keys mirror dimension + resource names";

		nlohmann::json filtersProp;
		filtersProp["type"]                 = "object";
		filtersProp["additionalProperties"] = true;
		filtersProp["description"]          = "Dimension filter values keyed by dimension name";

		nlohmann::json modeIn;
		modeIn["type"]        = "string";
		modeIn["enum"]        = nlohmann::json::array({ "records", "balance", "turnover" });
		modeIn["default"]     = "records";
		modeIn["description"] = "Query mode — 'records' is REAL today; "
			"'balance'/'turnover' return OES_E_NOT_SUPPORTED_FOR_KIND";

		nlohmann::json outRQ;
		outRQ["type"]       = "object";
		outRQ["properties"] = nlohmann::json::object();
		outRQ["properties"]["fullName"] = nlohmann::json{ {"type","string"} };
		outRQ["properties"]["mode"]     = nlohmann::json{ {"type","string"} };
		outRQ["properties"]["count"]    = nlohmann::json{ {"type","integer"} };
		outRQ["properties"]["total"]    = nlohmann::json{ {"type","integer"} };
		outRQ["properties"]["rows"]     = nlohmann::json{ {"type","array"}, {"items", rowItem} };
		outRQ["properties"]["errorCode"]= nlohmann::json{ {"type","string"} };

		table.push_back({
			{ "register_query",
			  "Query an InformationRegister or AccumulationRegister. "
			  "mode='records' returns raw rows filtered by dimensions "
			  "(REAL via ibPreparedStatement). mode='balance'/'turnover' "
			  "are DEFERRED — the manager aggregation pipeline binds to "
			  "ibValueModelTable and isn't reachable from the MCP "
			  "boundary; agents should author CES/VES query modules "
			  "instead until the standalone aggregation API lands.",
			  schemaObj({
			    { "fullName", str("'InformationRegister.<Name>' or 'AccumulationRegister.<Name>'") },
			    { "mode",     modeIn },
			    { "filters",  filtersProp },
			    { "period",   nlohmann::json{ {"type", nlohmann::json::array({"string","null"})}, {"description","ISO-8601 period for periodic InformationRegister (reserved)"} } },
			    { "limit",    nlohmann::json{ {"type","integer"}, {"default", 100}, {"description","Max rows (1..1000)"} } },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outRQ)
			},
			&ToolRegisterQuery
		});
	}
	table.push_back({
		{ "register_write",
		  "Write a single record into an AccumulationRegister "
		  "(programmatic fixture / migration path). STATUS: DEFERRED — "
		  "AccumulationRegister writes are bound to a Document.Posting() "
		  "transaction which the MCP boundary doesn't carry. Returns "
		  "isError with structuredContent.errorCode='OES_E_NOT_IMPLEMENTED' "
		  "and guides the agent to write a posting procedure via "
		  "write_module on the recorder Document.",
		  schemaObj({
		    { "fullName",   str("'AccumulationRegister.<Name>'") },
		    { "recordType", str("'Receive' or 'Expense'") },
		    { "period",     str("ISO-8601 period") },
		    { "document",   str("Optional recorder document reference") },
		    { "dimensions", obj("Dimension values keyed by name") },
		    { "resources",  obj("Resource values keyed by name") },
		  }, { "fullName", "recordType", "period" }),
		  // destructive=false: the deferred behaviour is additive (insert
		  // a record); idempotent=false: same payload twice yields two
		  // rows when implemented. openWorld=false: closed metadata domain.
		  ann(/*readOnly*/false, /*destructive*/false,
		      /*idempotent*/false, /*openWorld*/false)
		},
		&ToolRegisterWrite
	});
	{
		// predefined_values_list — REAL for Catalog/ChartOfChar/Enumeration.
		nlohmann::json item;
		item["type"]       = "object";
		item["properties"] = nlohmann::json::object();
		item["properties"]["name"]        = nlohmann::json{ {"type","string"} };
		item["properties"]["code"]        = nlohmann::json{ {"type","string"} };
		item["properties"]["description"] = nlohmann::json{ {"type","string"} };
		item["properties"]["synonym"]     = nlohmann::json{ {"type","string"} };
		item["properties"]["comment"]     = nlohmann::json{ {"type","string"} };
		item["properties"]["isFolder"]    = nlohmann::json{ {"type","boolean"} };
		item["properties"]["parent"]      = nlohmann::json{ {"type","string"} };

		nlohmann::json outPL;
		outPL["type"]       = "object";
		outPL["properties"] = nlohmann::json::object();
		outPL["properties"]["fullName"]   = nlohmann::json{ {"type","string"} };
		outPL["properties"]["kind"]       = nlohmann::json{ {"type","string"} };
		outPL["properties"]["count"]      = nlohmann::json{ {"type","integer"} };
		outPL["properties"]["predefined"] = nlohmann::json{ {"type","array"}, {"items", item} };
		outPL["required"]   = nlohmann::json::array({ "fullName", "predefined" });

		table.push_back({
			{ "predefined_values_list",
			  "List predefined items on a Catalog / "
			  "ChartOfCharacteristicTypes (returns "
			  "{name, code, description, isFolder, parent}) or the value "
			  "elements of an Enumeration (returns {name, synonym, "
			  "comment}). Other kinds return "
			  "OES_E_KIND_NOT_SUPPORTED.",
			  schemaObj({
			    { "fullName", str("Object full name, e.g. 'Catalog.Roles' or 'Enumeration.OrderState'") },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outPL)
			},
			&ToolPredefinedValuesList
		});
	}
	{
		// predefined_values_set — DEFERRED.
		nlohmann::json item;
		item["type"]       = "object";
		item["properties"] = nlohmann::json::object();
		item["properties"]["name"]        = nlohmann::json{ {"type","string"} };
		item["properties"]["code"]        = nlohmann::json{ {"type","string"} };
		item["properties"]["description"] = nlohmann::json{ {"type","string"} };
		item["properties"]["synonym"]     = nlohmann::json{ {"type","string"} };
		item["properties"]["isFolder"]    = nlohmann::json{ {"type","boolean"} };
		item["properties"]["parent"]      = nlohmann::json{ {"type","string"} };
		item["required"]   = nlohmann::json::array({ "name" });

		nlohmann::json predefinedIn;
		predefinedIn["type"]        = "array";
		predefinedIn["items"]       = item;
		predefinedIn["description"] = "Full replacement predefined list";

		table.push_back({
			{ "predefined_values_set",
			  "Replace the predefined-values list on a Catalog / "
			  "ChartOfCharacteristicTypes / Enumeration. STATUS: DEFERRED "
			  "— backend has AppendPredefinedValue / SetPredefinedValue "
			  "/ DeletePredefinedValue but the policy-gated metaBridge "
			  "undo lambda for predefined-value mutations hasn't landed. "
			  "Returns isError with "
			  "structuredContent.errorCode='OES_E_NOT_IMPLEMENTED'.",
			  schemaObj({
			    { "fullName",   str("Object full name") },
			    { "predefined", predefinedIn },
			  }, { "fullName", "predefined" }),
			  // destructive=true: when implemented, replaces the entire
			  // predefined list. idempotent=true: same payload twice =
			  // same final state.
			  ann(/*readOnly*/false, /*destructive*/true,
			      /*idempotent*/true, /*openWorld*/false)
			},
			&ToolPredefinedValuesSet
		});
	}

	table.push_back({
		{ "config_info",
		  "Return readiness state, loaded configuration path, top-level "
		  "object count, and root name.",
		  schemaObj({}, {}),
		  ann(true, false, true, false)
		},
		&ToolConfigInfo
	});

	return table;
}

} // namespace

const std::vector<ToolEntry>& AllTools()
{
	return BuildRegistry();
}

nlohmann::json DispatchTool(const std::string& name, const nlohmann::json& args)
{
	for (const auto& t : AllTools()) {
		if (t.desc.name == name) {
			return t.fn(args);
		}
	}
	return TextResult("oes-mcp: unknown tool '" + name + "'", true);
}

} // namespace mcpServer
