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
#include "backend/migration/snapshotManager.hpp"
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
#include "backend/typeDescription.h"
#include "backend/guid.h"

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
#include <set>
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

// MCP auto-snapshot: capture prior state before a mutation. Returns the
// snapshot id ("" when the manager is absent or in Disabled mode). For
// `meta_create` the priorState is `null`; for the other three we ask
// metaBridge for the current shape so the rollback path has something
// to restore. Failures here NEVER block the mutation — capture is an
// audit aide, not a hard prerequisite.
std::string CaptureSnapshotForMutation(const std::string& toolName,
                                        const std::string& fullName)
{
	auto* mgr = GetSnapshotManager();
	if (mgr == nullptr) return std::string();
	if (mgr->Mode() == migration::snapshots::CaptureMode::Disabled) {
		return std::string();
	}

	std::string priorJson = "null";
	if (toolName != "meta_create") {
		// For edit / delete / write_module we want the live shape. HostMetaQuery
		// returns malloc'd JSON; treat IO failure here as "no prior state
		// known" rather than aborting the mutation.
		char* jsonOut = nullptr;
		char* errMsg  = nullptr;
		const int rc = metaBridge::HostMetaQuery(
			fullName.c_str(), nullptr, &jsonOut, &errMsg);
		if (rc == 0 && jsonOut != nullptr) priorJson = std::string(jsonOut);
		FreeIfSet(jsonOut); FreeIfSet(errMsg);
	}

	const wxString id = mgr->CaptureBeforeMutation(
		wxString::FromUTF8(toolName.c_str()),
		wxString::FromUTF8(fullName.c_str()),
		wxString::FromUTF8(priorJson.c_str()));
	return std::string(id.utf8_str());
}

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

	// MCP auto-snapshot: capture priorState BEFORE the mutation runs.
	// `meta_create` has no prior state — the captured body records null
	// + the operation type, which is enough for the rollback plan to
	// emit a delete on undo.
	const std::string snapshotId = CaptureSnapshotForMutation("meta_create", fullName);

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
		// MCP: the orphan snapshot stays on disk — operators can use it for
		// post-mortem ("the mutation failed but here's what we tried to
		// replace"). Could prune later in a v2 retention sweep.
		return TextResult(msg, true);
	}
	FreeIfSet(errMsg);
	// MCP concurrency Layer 3: broadcast successful mutation so Designer's
	// notifier can refresh its tree / surface a toast.
	NotifyMutation("meta_create", fullName);
	EmitResourceMutation(fullName);

	nlohmann::json env = TextResult("meta_create OK: " + fullName, false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"]       = true;
	sc["fullName"] = fullName;
	if (!snapshotId.empty()) sc["snapshotId"] = snapshotId;
	env["structuredContent"] = std::move(sc);
	return env;
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

	const std::string snapshotId = CaptureSnapshotForMutation("meta_edit", fullName);

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

	nlohmann::json env = TextResult("meta_edit OK: " + fullName, false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"] = true;
	if (!snapshotId.empty()) sc["snapshotId"] = snapshotId;
	env["structuredContent"] = std::move(sc);
	return env;
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

	// MCP auto-snapshot: capture BEFORE delete so the rollback path can
	// recreate the object verbatim. This is the highest-stakes mutation
	// — also the one where the snapshot pays for itself loudest.
	const std::string snapshotId = CaptureSnapshotForMutation("meta_delete", fullName);

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

	nlohmann::json env = TextResult("meta_delete OK: " + fullName, false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"] = true;
	if (!snapshotId.empty()) sc["snapshotId"] = snapshotId;
	env["structuredContent"] = std::move(sc);
	return env;
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

	// MCP auto-snapshot: snapshot the prior source text before we
	// overwrite it. meta_query on a module path returns shallow metadata
	// only (no source), so we synthesise a priorState object here with
	// the full text. The rollback path can then re-call write_module
	// with the same payload to revert.
	std::string snapshotId;
	if (auto* mgr = GetSnapshotManager(); mgr != nullptr &&
	    mgr->Mode() != migration::snapshots::CaptureMode::Disabled) {
		nlohmann::json prior;
		prior["path"]   = fullName;
		prior["kind"]   = "module";
		prior["source"] = std::string(mod->GetModuleText().utf8_str());
		const wxString id = mgr->CaptureBeforeMutation(
			wxT("write_module"),
			wxString::FromUTF8(fullName.c_str()),
			wxString::FromUTF8(prior.dump().c_str()));
		snapshotId = std::string(id.utf8_str());
	}

	// MCP: no compile-check yet — Designer's editor invokes ibCompileCode
	// from a wider context (debug server, dependency wiring). We accept
	// the write unconditionally; callers should pair this with
	// `compile_check` when validation matters.
	mod->SetModuleText(wxString::FromUTF8(source.c_str()));
	NotifyMutation("write_module", fullName);
	EmitResourceMutation(fullName);

	nlohmann::json env = TextResult("write_module OK: " + fullName +
		" (" + std::to_string(source.size()) + " bytes)", false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"]       = true;
	sc["fullName"] = fullName;
	sc["bytes"]    = source.size();
	if (!snapshotId.empty()) sc["snapshotId"] = snapshotId;
	env["structuredContent"] = std::move(sc);
	return env;
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
	std::string locale;
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

std::string NormalizePugiLocale(std::string raw)
{
	for (char& c : raw) {
		if (c == '_') c = '-';
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	if (raw.empty() || raw == "c" || raw == "posix" ||
	    raw.rfind("en", 0) == 0) {
		return "en-US";
	}
	if (raw == "uk" || raw.rfind("uk-", 0) == 0) {
		return "uk-UA";
	}
	if (raw == "ru" || raw.rfind("ru-", 0) == 0 ||
	    raw == "be" || raw.rfind("be-", 0) == 0) {
		return "ru-RU";
	}
	return "en-US";
}

std::string NormalizePugiEndpoint(std::string raw)
{
	while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' ||
	                         raw.back() == '\r' || raw.back() == '\n')) {
		raw.pop_back();
	}
	while (!raw.empty() && raw.back() == '/') raw.pop_back();
	if (raw.empty()) return "https://mcp.pugi.io/api/oes-mcp/invoke";
	const std::string suffix = "/api/oes-mcp/invoke";
	if (raw.size() >= suffix.size() &&
	    raw.compare(raw.size() - suffix.size(), suffix.size(), suffix) == 0) {
		return raw;
	}
	return raw + suffix;
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
	out.locale   = envOrEmpty("OES_PUGI_LOCALE");
	if (out.endpoint.empty()) out.endpoint = envOrEmpty("PUGI_BASE_URL");
	if (out.token.empty())    out.token    = envOrEmpty("PUGI_OES_API_KEY");
	if (out.tenant.empty())   out.tenant   = envOrEmpty("PUGI_TENANT_ID");
	if (out.tenant.empty())   out.tenant   = envOrEmpty("PUGI_TENANT");
	if (out.locale.empty())   out.locale   = envOrEmpty("PUGI_OES_LOCALE");
	if (out.locale.empty())   out.locale   = envOrEmpty("LOCALE");

	// Fill missing pieces from the aiBridge.env file (byokEnv namespace
	// already handles dotenv parsing, quoted values, perms warnings).
	if (out.endpoint.empty() || out.token.empty() ||
	    out.tenant.empty() || out.locale.empty()) {
		const byokEnv::PluginEnv all = byokEnv::LoadAll();
		const std::string pluginId   = "aiBridge";
		if (out.endpoint.empty()) out.endpoint = byokEnv::Get(all, pluginId, "ENDPOINT");
		if (out.token.empty())    out.token    = byokEnv::Get(all, pluginId, "TOKEN");
		if (out.tenant.empty())   out.tenant   = byokEnv::Get(all, pluginId, "TENANT");
		if (out.endpoint.empty()) out.endpoint = byokEnv::Get(all, pluginId, "PUGI_BASE_URL");
		if (out.token.empty())    out.token    = byokEnv::Get(all, pluginId, "PUGI_OES_API_KEY");
		if (out.tenant.empty())   out.tenant   = byokEnv::Get(all, pluginId, "PUGI_TENANT_ID");
		if (out.tenant.empty())   out.tenant   = byokEnv::Get(all, pluginId, "PUGI_TENANT");
		if (out.locale.empty())   out.locale   = byokEnv::Get(all, pluginId, "PUGI_OES_LOCALE");
		if (out.locale.empty())   out.locale   = byokEnv::Get(all, pluginId, "LOCALE");
	}
	if (out.endpoint.empty()) out.endpoint = "https://mcp.pugi.io";
	out.endpoint = NormalizePugiEndpoint(out.endpoint);
	out.locale = NormalizePugiLocale(out.locale);

	out.loaded = true;
	{
		std::lock_guard<std::mutex> lk(PugiConfigMutex());
		PugiConfigCache() = out;
	}
	return out;
}

bool EnvelopeIsError(const nlohmann::json& env)
{
	return env.is_object() && env.contains("isError") &&
	       env["isError"].is_boolean() && env["isError"].get<bool>();
}

bool EnvelopeHasOkFalse(const nlohmann::json& env)
{
	if (!env.is_object() || !env.contains("structuredContent") ||
	    !env["structuredContent"].is_object()) {
		return false;
	}
	const auto& sc = env["structuredContent"];
	return sc.contains("ok") && sc["ok"].is_boolean() &&
	       !sc["ok"].get<bool>();
}

std::string TextFromEnvelope(const nlohmann::json& env, size_t maxLen = 4000)
{
	std::string out;
	if (env.is_object() && env.contains("content") && env["content"].is_array()) {
		for (const auto& item : env["content"]) {
			if (!item.is_object()) continue;
			auto it = item.find("text");
			if (it == item.end() || !it->is_string()) continue;
			if (!out.empty()) out += "\n";
			out += it->get<std::string>();
			if (out.size() >= maxLen) break;
		}
	}
	if (out.size() > maxLen) {
		out.resize(maxLen);
		out += "...";
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
	if (cfg.endpoint.empty() || cfg.token.empty()) {
		return offlineMaker(
		    "no Pugi credentials configured (need OES_PUGI_ENDPOINT/TOKEN "
		    "or aiBridge.env with ENDPOINT/TOKEN)");
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
		{ "Content-Type",  "application/json"    },
		{ "Accept",        "application/json"    },
		{ "User-Agent",    "oes-mcp/1.0"         },
	};
	if (!cfg.tenant.empty()) {
		headers.emplace("X-Tenant-Id", cfg.tenant);
	}

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

	nlohmann::json env = PugiHttpInvoke("sigma_check", input, SigmaOfflineEnvelope);
	bool needsDiagnostic = EnvelopeIsError(env) || EnvelopeHasOkFalse(env);
	if (!needsDiagnostic && env.contains("structuredContent") &&
	    env["structuredContent"].is_object()) {
		const auto& sc = env["structuredContent"];
		const bool offline = sc.contains("offline") && sc["offline"].is_boolean() &&
		                     sc["offline"].get<bool>();
		const std::string reason = sc.contains("reason") && sc["reason"].is_string()
			? sc["reason"].get<std::string>() : std::string();
		needsDiagnostic = offline && reason.rfind("Pugi returned", 0) == 0;
	}
	if (!needsDiagnostic) {
		return env;
	}

	const PugiConfig cfg = LoadPugiConfig();
	if (cfg.token.empty()) {
		return env;
	}

	std::string verdict = TextFromEnvelope(env, 2500);
	if (verdict.empty() && env.contains("structuredContent")) {
		verdict = env["structuredContent"].dump(2);
		if (verdict.size() > 2500) {
			verdict.resize(2500);
			verdict += "...";
		}
	}

	std::string prompt;
	prompt += "Ты Sigma-эксперт OES. Объясни результат sigma_check ";
	prompt += "кратко и прикладно: причина, риск, как исправить в конфигурации. ";
	prompt += "Не переписывай весь JSON, дай список действий.\n\n";
	prompt += "Metadata:\n";
	prompt += input["metadata"].dump(2);
	if (prompt.size() > 5000) {
		prompt.resize(5000);
		prompt += "...";
	}
	prompt += "\n\nsigma_check result:\n";
	prompt += verdict.empty() ? env.dump(2) : verdict;

	nlohmann::json diagInput;
	diagInput["prompt"] = prompt;
	diagInput["locale"] = cfg.locale.empty() ? "uk-UA" : cfg.locale;
	diagInput["maxTokens"] = 700;

	nlohmann::json diag = PugiHttpInvoke("llm_query", diagInput,
		[](const std::string& reason) {
			return PugiOfflineEnvelope("llm_query", reason);
		});

	if (!env.contains("structuredContent") || !env["structuredContent"].is_object()) {
		env["structuredContent"] = nlohmann::json::object();
	}
	if (!EnvelopeIsError(diag)) {
		const std::string text = TextFromEnvelope(diag, 3000);
		if (!text.empty()) {
			env["structuredContent"]["diagnostic"] = text;
			if (env.contains("content") && env["content"].is_array()) {
				nlohmann::json item;
				item["type"] = "text";
				item["text"] = std::string("sigma_check diagnostic:\n") + text;
				env["content"].push_back(std::move(item));
			}
		}
	} else {
		env["structuredContent"]["diagnosticUnavailable"] = TextFromEnvelope(diag, 1000);
	}
	return env;
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
// Tool: headless_smoke_run
//
// Post-apply validation shortcut. It reports whether the configuration is
// loaded, whether startup compile reached a usable root, and optionally runs
// the functional test suite. It is intentionally read-only: tests execute
// through the same fixture rollback path as run_tests.
// =========================================================================
nlohmann::json ToolHeadlessSmokeRun(const nlohmann::json& args)
{
	const bool runTests = ArgBool(args, "runTests", true);
	const bool stopOnFirstFailure = ArgBool(args, "stopOnFirstFailure", true);

	nlohmann::json structured = nlohmann::json::object();
	structured["load"] = nlohmann::json::object();
	structured["load"]["ok"] = IsReady() && activeMetaData != nullptr;
	structured["load"]["configPath"] = LoadedConfigPath();

	if (!IsReady() || activeMetaData == nullptr) {
		structured["ok"] = false;
		structured["compile"] = {
			{ "ok", false },
			{ "phase", "startup" },
			{ "message", "configuration is not loaded" },
		};
		structured["tests"] = {
			{ "run", false },
			{ "summary", nullptr },
		};
		nlohmann::json env = TextResult(
			"headless_smoke_run: no configuration loaded", true);
		env["structuredContent"] = std::move(structured);
		return env;
	}

	if (auto* root = activeMetaData->GetCommonMetaObject()) {
		structured["load"]["name"] = std::string(root->GetName().utf8_str());
	}

	structured["compile"] = {
		{ "ok", true },
		{ "phase", "startup" },
		{ "message", "configuration loaded and startup compile completed" },
	};

	bool ok = true;
	if (runTests) {
		ibTesting::TestRunOptions opts;
		opts.stopOnFirstFailure = stopOnFirstFailure;
		const std::string filter = ArgString(args, "filter");
		if (!filter.empty()) {
			opts.filter = wxString::FromUTF8(filter.c_str());
		}
		if (args.is_object()) {
			auto mit = args.find("modules");
			if (mit != args.end() && mit->is_array()) {
				for (const auto& m : *mit) {
					if (m.is_string()) {
						opts.moduleFilter.push_back(
							wxString::FromUTF8(m.get<std::string>().c_str()));
					}
				}
			}
		}

		ibTesting::TestRun run = ibTesting::RunTests(opts);
		nlohmann::json summary;
		summary["total"]            = run.summary.total;
		summary["passed"]           = run.summary.passed;
		summary["failed"]           = run.summary.failed;
		summary["errored"]          = run.summary.errored;
		summary["skipped"]          = run.summary.skipped;
		summary["durationMs"]       = run.summary.durationMs;
		summary["fixtureDegraded"]  = run.summary.fixtureDegraded;
		if (!run.error.IsEmpty()) {
			summary["error"] = std::string(run.error.utf8_str());
			ok = false;
		}
		if (run.summary.failed > 0 || run.summary.errored > 0) {
			ok = false;
		}
		structured["tests"] = {
			{ "run", true },
			{ "summary", std::move(summary) },
		};
	} else {
		structured["tests"] = {
			{ "run", false },
			{ "summary", nullptr },
		};
	}
	structured["ok"] = ok;

	std::string text = ok
		? std::string("headless_smoke_run: OK")
		: std::string("headless_smoke_run: failed");
	if (structured.contains("tests") &&
	    structured["tests"].contains("summary") &&
	    structured["tests"]["summary"].is_object()) {
		const auto& s = structured["tests"]["summary"];
		text += " (tests total=" + std::to_string(s.value("total", 0)) +
		        " failed=" + std::to_string(s.value("failed", 0)) +
		        " errored=" + std::to_string(s.value("errored", 0)) + ")";
	}

	nlohmann::json env = TextResult(text, !ok);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tools: validate_query / execute_query
//
// Read-only database query helper for MCP clients. The safety policy is:
//   - only SELECT / WITH statements are accepted;
//   - obvious DDL/DML/control tokens are rejected before prepare;
//   - parameters are bound through ibPreparedStatement.
// =========================================================================
std::string SqlMaskLiteralsAndComments(const std::string& sql)
{
	std::string out;
	out.reserve(sql.size());
	bool inSingle = false;
	bool inDouble = false;
	bool inLineComment = false;
	bool inBlockComment = false;
	for (size_t i = 0; i < sql.size(); ++i) {
		const char c = sql[i];
		const char n = (i + 1 < sql.size()) ? sql[i + 1] : '\0';
		if (inLineComment) {
			if (c == '\n') {
				inLineComment = false;
				out.push_back(c);
			} else {
				out.push_back(' ');
			}
			continue;
		}
		if (inBlockComment) {
			if (c == '*' && n == '/') {
				inBlockComment = false;
				out.push_back(' ');
				out.push_back(' ');
				++i;
			} else {
				out.push_back(' ');
			}
			continue;
		}
		if (!inSingle && !inDouble && c == '-' && n == '-') {
			inLineComment = true;
			out.push_back(' ');
			out.push_back(' ');
			++i;
			continue;
		}
		if (!inSingle && !inDouble && c == '/' && n == '*') {
			inBlockComment = true;
			out.push_back(' ');
			out.push_back(' ');
			++i;
			continue;
		}
		if (!inDouble && c == '\'') {
			inSingle = !inSingle;
			out.push_back(' ');
			continue;
		}
		if (!inSingle && c == '"') {
			inDouble = !inDouble;
			out.push_back(' ');
			continue;
		}
		out.push_back((inSingle || inDouble)
			? ' '
			: static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	}
	return out;
}

std::string TrimSqlCopy(std::string s)
{
	auto notSpace = [](unsigned char c) {
		return c != ' ' && c != '\t' && c != '\r' && c != '\n';
	};
	while (!s.empty() && !notSpace(static_cast<unsigned char>(s.front()))) {
		s.erase(s.begin());
	}
	while (!s.empty() && !notSpace(static_cast<unsigned char>(s.back()))) {
		s.pop_back();
	}
	return s;
}

nlohmann::json ValidateReadOnlySql(const std::string& sql)
{
	nlohmann::json out;
	out["ok"] = false;
	out["readOnly"] = false;
	out["errors"] = nlohmann::json::array();

	const std::string trimmed = TrimSqlCopy(sql);
	if (trimmed.empty()) {
		out["errors"].push_back("query is empty");
		return out;
	}
	const std::string masked = TrimSqlCopy(SqlMaskLiteralsAndComments(trimmed));
	if (masked.empty()) {
		out["errors"].push_back("query has no executable statement");
		return out;
	}
	if (masked.find(';') != std::string::npos &&
	    masked.find(';') != masked.size() - 1) {
		out["errors"].push_back("multiple statements are not allowed");
		return out;
	}

	std::smatch m;
	if (!std::regex_search(masked, m, std::regex("^([a-z]+)"))) {
		out["errors"].push_back("cannot determine query verb");
		return out;
	}
	const std::string verb = m[1].str();
	if (verb != "select" && verb != "with") {
		out["errors"].push_back("only SELECT/WITH read-only queries are allowed");
		return out;
	}

	static const std::set<std::string> banned = {
		"insert", "update", "delete", "merge", "create", "alter", "drop",
		"truncate", "grant", "revoke", "vacuum", "attach", "detach",
		"call", "execute", "exec", "replace",
	};
	std::regex wordRe("[a-z_]+");
	for (auto it = std::sregex_iterator(masked.begin(), masked.end(), wordRe);
	     it != std::sregex_iterator(); ++it) {
		const std::string word = it->str();
		if (banned.find(word) != banned.end()) {
			out["errors"].push_back("write/control token is not allowed: " + word);
			return out;
		}
	}

	out["ok"] = true;
	out["readOnly"] = true;
	return out;
}

void BindQueryParams(ibPreparedStatement* stmt, const nlohmann::json& params)
{
	if (stmt == nullptr || !params.is_array()) return;
	int index = 1;
	for (const auto& p : params) {
		if (p.is_null()) {
			stmt->SetParamNull(index);
		} else if (p.is_boolean()) {
			stmt->SetParamBool(index, p.get<bool>());
		} else if (p.is_number_integer()) {
			stmt->SetParamInt(index, p.get<int>());
		} else if (p.is_number_float()) {
			stmt->SetParamDouble(index, p.get<double>());
		} else if (p.is_string()) {
			const std::string s = p.get<std::string>();
			stmt->SetParamString(index, wxString::FromUTF8(s.c_str()));
		} else {
			stmt->SetParamString(index, wxString::FromUTF8(p.dump().c_str()));
		}
		++index;
	}
}

nlohmann::json QueryValueToJson(ibDatabaseResultSet* rs, int col, int type)
{
	if (rs->IsFieldNull(col)) return nullptr;
	switch (type) {
	case ibResultSetMetaData::COLUMN_INTEGER:
		return rs->GetResultInt(col);
	case ibResultSetMetaData::COLUMN_DOUBLE:
		return rs->GetResultDouble(col);
	case ibResultSetMetaData::COLUMN_BOOL:
		return rs->GetResultBool(col);
	case ibResultSetMetaData::COLUMN_DATE:
		return std::string(rs->GetResultDate(col).FormatISOCombined(' ').utf8_str());
	case ibResultSetMetaData::COLUMN_BLOB: {
		wxMemoryBuffer buffer;
		rs->GetResultBlob(col, buffer);
		return std::string("<blob ") + std::to_string(buffer.GetDataLen()) + " bytes>";
	}
	default:
		return std::string(rs->GetResultString(col).utf8_str());
	}
}

nlohmann::json ToolValidateQuery(const nlohmann::json& args)
{
	const std::string query = ArgString(args, "query");
	nlohmann::json structured = ValidateReadOnlySql(query);
	nlohmann::json env = TextResult(structured["ok"].get<bool>()
		? "validate_query: OK"
		: "validate_query: rejected", !structured["ok"].get<bool>());
	env["structuredContent"] = std::move(structured);
	return env;
}

nlohmann::json ToolExecuteQuery(const nlohmann::json& args)
{
	if (auto err = RequireConfig()) return *err;
	const std::string query = ArgString(args, "query");
	nlohmann::json validation = ValidateReadOnlySql(query);
	if (!validation["ok"].get<bool>()) {
		nlohmann::json env = TextResult("execute_query: rejected", true);
		env["structuredContent"] = std::move(validation);
		return env;
	}

	int maxRows = 50;
	if (args.is_object() && args.contains("maxRows") && args["maxRows"].is_number_integer()) {
		maxRows = args["maxRows"].get<int>();
	}
	if (maxRows < 1) maxRows = 1;
	if (maxRows > 500) maxRows = 500;

	nlohmann::json structured;
	structured["ok"] = false;
	structured["readOnly"] = true;
	structured["columns"] = nlohmann::json::array();
	structured["rows"] = nlohmann::json::array();
	structured["truncated"] = false;

	try {
		auto db = db_query;
		ibStatementGuard stmt(db, db->PrepareStatement(wxString::FromUTF8(query.c_str())));
		if (!stmt) {
			return StructuredError("execute_query: prepare failed");
		}
		if (args.is_object() && args.contains("params")) {
			BindQueryParams(stmt.get(), args["params"]);
		}
		ibResultSetGuard rs(db, stmt->RunQueryWithResults());
		if (!rs) {
			return StructuredError("execute_query: query returned no result set");
		}

		ibResultSetMetaData* md = rs->GetMetaData();
		const int colCount = md ? md->GetColumnCount() : 0;
		std::vector<int> types;
		types.reserve(colCount);
		for (int i = 1; i <= colCount; ++i) {
			const int type = md->GetColumnType(i);
			types.push_back(type);
			nlohmann::json col;
			col["name"] = std::string(md->GetColumnName(i).utf8_str());
			col["type"] = type;
			col["size"] = md->GetColumnSize(i);
			structured["columns"].push_back(std::move(col));
		}
		if (md) rs->CloseMetaData(md);

		int rowCount = 0;
		while (rs->Next()) {
			if (rowCount >= maxRows) {
				structured["truncated"] = true;
				break;
			}
			nlohmann::json row = nlohmann::json::object();
			for (int i = 1; i <= colCount; ++i) {
				const std::string name =
					structured["columns"][static_cast<size_t>(i - 1)]["name"].get<std::string>();
				row[name] = QueryValueToJson(rs.get(), i, types[static_cast<size_t>(i - 1)]);
			}
			structured["rows"].push_back(std::move(row));
			++rowCount;
		}
		structured["ok"] = true;
		structured["rowCount"] = structured["rows"].size();
	} catch (const ibBackendException& e) {
		return StructuredError(std::string("execute_query: ") +
		                       std::string(e.GetErrorDescription().utf8_str()));
	} catch (const std::exception& e) {
		return StructuredError(std::string("execute_query: ") + e.what());
	}

	nlohmann::json env = TextResult("execute_query: OK");
	env["structuredContent"] = std::move(structured);
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
// Refactoring primitives (5) — added 2026-05-21. find_references,
// rename_with_refs, metadata_diff, dependency_graph, extract_module_to_common.
//
// Coverage matrix (pragmatic v1):
//   find_references         : REAL    — attribute type CLSID match + module
//                                        text regex (word-boundary), with
//                                        register dimension / record bindings.
//   rename_with_refs        : REAL    — dryRun default true; apply via
//                                        metaBridge::HostMetaEdit on the
//                                        target (rename) + write_module on
//                                        every module reference (regex-replace
//                                        with word boundaries).
//   metadata_diff           : PARTIAL — inline-snapshot mode REAL (deep-diff
//                                        two JSON objects from meta_query);
//                                        file mode returns OES_E_NOT_IMPLEMENTED
//                                        (loading two configs in parallel is
//                                        heavy, deferred to v2).
//   dependency_graph        : REAL    — BFS up to depth, reuses find_references
//                                        logic in both directions.
//   extract_module_to_common: PARTIAL — dryRun shows extracted text +
//                                        planned mutations; actual apply
//                                        deferred (text-based extraction is
//                                        brittle when locals/closures involved).
// =========================================================================
namespace {

// MCP: collect every metadata node under root with a kind + fullName.
// Used by find_references / dependency_graph to walk the tree once and
// inspect each node's outgoing refs.
struct TreeNodeRef {
	ibValueMetaObject* node;
	std::string        fullName;   // e.g. "Catalog.X" or "Document.Y.Attributes.Z"
	std::string        kindLabel;  // resolved via CLSIDToKindLabel
};

void CollectTreeNodes(ibValueMetaObject* node,
                      const std::string& parentPath,
                      std::vector<TreeNodeRef>& out)
{
	if (node == nullptr) return;
	const std::string name = std::string(node->GetName().utf8_str());
	if (name.empty()) {
		// The configuration root has no name; just recurse into children
		// without contributing a self-entry.
		for (ibValueMetaObject* child : node->GetAnyArrayObject<>()) {
			CollectTreeNodes(child, std::string(), out);
		}
		return;
	}
	const std::string path = parentPath.empty() ? name : (parentPath + "." + name);
	const char* kindLabel = CLSIDToKindLabel(
		static_cast<unsigned long long>(node->GetClassType()));
	TreeNodeRef ref;
	ref.node      = node;
	ref.fullName  = path;
	ref.kindLabel = (kindLabel != nullptr) ? std::string(kindLabel) : std::string();
	out.push_back(std::move(ref));
	for (ibValueMetaObject* child : node->GetAnyArrayObject<>()) {
		CollectTreeNodes(child, path, out);
	}
}

// MCP: top-level fullName of `path`, e.g. "Catalog.X.Attributes.Y" -> "Catalog.X".
// Refs to children of the target are not surfaced as separate hits — the
// agent already knows the children belong to the parent.
std::string TopLevelOf(const std::string& path)
{
	std::size_t firstDot = path.find('.');
	if (firstDot == std::string::npos) return path;
	std::size_t secondDot = path.find('.', firstDot + 1);
	if (secondDot == std::string::npos) return path;
	return path.substr(0, secondDot);
}

// MCP: build a word-boundary regex for the bare object name. We escape any
// regex metacharacters in the needle so identifiers with dots / parens
// don't blow up regex_search. Word boundaries use \b which matches
// Latin word chars only — for Cyrillic / CJK identifiers we fall back to
// matching the bare substring without boundary anchors. Lexer-aware
// matching is a v2 follow-up.
bool NeedleHasWordChars(const std::string& s)
{
	for (unsigned char c : s) {
		if ((c & 0x80) != 0) return false;  // non-ASCII -> no word boundary
	}
	return true;
}

std::string EscapeRegex(const std::string& s)
{
	std::string out;
	out.reserve(s.size() * 2);
	for (char c : s) {
		switch (c) {
			case '.': case '^': case '$': case '|': case '?': case '*':
			case '+': case '(': case ')': case '[': case ']': case '{':
			case '}': case '\\':
				out.push_back('\\');
				// fall through
			default:
				out.push_back(c);
		}
	}
	return out;
}

// MCP: split a fullName "<Kind>.<Name>" into (kindLabel, bareName). Returns
// {fullName, ""} when there's no dot — caller treats that as "kind-only".
std::pair<std::string, std::string> SplitTopName(const std::string& fullName)
{
	auto dot = fullName.find('.');
	if (dot == std::string::npos) return { fullName, std::string() };
	return { fullName.substr(0, dot), fullName.substr(dot + 1) };
}

// MCP: build the structured reference record. kind ∈ {attribute_type,
// module_text, register_dimension, register_record_binding,
// form_attribute_binding}. context is a short human-readable hint.
nlohmann::json MakeReferenceEntry(const std::string& kind,
                                  const std::string& location,
                                  const std::string& context)
{
	nlohmann::json r;
	r["kind"]     = kind;
	r["location"] = location;
	r["context"]  = context;
	return r;
}

// MCP: scan a single attribute (or attribute-like child) for type refs to
// the target object. We compare CLSIDs directly — the attribute's
// ibTypeDescription holds a list of ibClassID values, each of which is
// either a primitive type id or a metadata-object CLSID. The target's
// CLSID identifies its KIND (Catalog/Document/etc) — to detect a *typed*
// reference to a specific instance we need a per-instance ref. OES models
// reference types via a Catalog/Document metaobject's CLSID (CtorClass
// registration), so equality on the metaId is the right hook. We surface
// any attribute whose type list contains the target's metaId.
//
// Note: ibMetaData::GetMetaIDByCLSID isn't a thing — each metaObject has
// its own m_metaId persisted to disk. We check both:
//   1) child->GetClassType() == target->GetClassType() (kind match) — too
//      broad on its own
//   2) AND attribute carries a type pointing into the metaId namespace
//      (we'd need IsRegisterCtor mapping). For v1 we use the simpler
//      structural check: the metaObject's own CLSID appears in the
//      attribute's type list. That covers all in-tree CatalogRef /
//      DocumentRef typings because OES registers the per-target CLSID
//      when the attribute editor binds a Reference type.
bool AttributeTypeRefsTarget(const ibValueMetaObject* attr,
                             ibClassID targetClsid)
{
	auto* concrete = dynamic_cast<const ibValueMetaObjectAttribute*>(attr);
	if (concrete == nullptr) return false;
	const ibTypeDescription& td = concrete->GetTypeDesc();
	const unsigned int n = td.GetClsidCount();
	for (unsigned int i = 0; i < n; ++i) {
		if (td.GetByIdx(i) == targetClsid) return true;
	}
	return false;
}

// MCP: scan a module's source text for word-boundary matches of the bare
// target name. Returns the first matching line + a short snippet, plus
// the total count of hits.
struct ModuleScanResult {
	bool        any         = false;
	unsigned    line        = 0;
	unsigned    matchCount  = 0;
	std::string snippet;
};

ModuleScanResult ScanModuleForName(const std::string& source,
                                   const std::string& bareName)
{
	ModuleScanResult res;
	if (source.empty() || bareName.empty()) return res;

	std::string pattern;
	if (NeedleHasWordChars(bareName)) {
		pattern = "\\b" + EscapeRegex(bareName) + "\\b";
	} else {
		// Cyrillic / non-ASCII — match the literal substring. The agent
		// gets back a slightly noisier hit list (false positives in
		// comments or string literals), but the alternative is a full
		// lexer pass which is deferred.
		pattern = EscapeRegex(bareName);
	}

	try {
		std::regex rx(pattern, std::regex::ECMAScript);
		auto begin = std::sregex_iterator(source.begin(), source.end(), rx);
		auto end   = std::sregex_iterator();
		for (auto it = begin; it != end; ++it) {
			res.matchCount++;
			if (!res.any) {
				res.any = true;
				const std::size_t pos = static_cast<std::size_t>(it->position(0));
				// 1-based line number
				unsigned line = 1;
				for (std::size_t i = 0; i < pos && i < source.size(); ++i) {
					if (source[i] == '\n') ++line;
				}
				res.line = line;
				// Snippet: the line containing the match
				std::size_t lineStart = pos;
				while (lineStart > 0 && source[lineStart - 1] != '\n') --lineStart;
				std::size_t lineEnd = pos;
				while (lineEnd < source.size() && source[lineEnd] != '\n') ++lineEnd;
				res.snippet = source.substr(lineStart, lineEnd - lineStart);
				// Trim leading whitespace
				std::size_t firstNonWs = res.snippet.find_first_not_of(" \t\r");
				if (firstNonWs != std::string::npos && firstNonWs > 0) {
					res.snippet = res.snippet.substr(firstNonWs);
				}
				// Cap snippet length to keep output compact
				if (res.snippet.size() > 200) {
					res.snippet = res.snippet.substr(0, 200) + "...";
				}
			}
		}
	} catch (const std::regex_error&) {
		// Pattern construction shouldn't fail given our escaping, but
		// leave res in default (no-match) state if it does.
	}
	return res;
}

// MCP: walk the configuration and collect every reference to `target`.
// Used by find_references AND dependency_graph (the latter reuses the
// same routine in both directions).
//
// targetFullName is the fully-qualified name (e.g. "Catalog.X"). We split
// into (kind, bareName); the kind isn't strictly needed for matching but
// helps the agent disambiguate when multiple objects share a name across
// kinds.
//
// Returns a list of {kind, location, context} entries. Self-references
// (the target's own children, e.g. an attribute on Catalog.X with type
// Catalog.X) are emitted so circular dependencies are visible.
std::vector<nlohmann::json> CollectReferencesTo(const std::string& targetFullName)
{
	std::vector<nlohmann::json> out;
	if (activeMetaData == nullptr) return out;

	ibValueMetaObject* target = ResolveByPath(targetFullName);
	if (target == nullptr) return out;
	const ibClassID targetClsid = target->GetClassType();
	const auto [/*kind*/ _ignoredKind, bareName] = SplitTopName(targetFullName);
	(void)_ignoredKind;

	// First pass — collect every node so we can iterate without
	// re-walking the tree per check.
	std::vector<TreeNodeRef> all;
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) return out;
	for (ibValueMetaObject* child : root->GetAnyArrayObject<>()) {
		CollectTreeNodes(child, std::string(), all);
	}

	// For each node, run the per-kind checks. Attribute-type checks
	// only run on Attribute children; module text scans run on
	// ibValueMetaObjectModuleBase nodes. The top-level filter (drop
	// children of the target itself) is enforced after the match so
	// circular self-refs are still surfaced once at the top level.
	for (const TreeNodeRef& ref : all) {
		if (ref.node == target) continue;  // skip self

		// (1) Attribute type ref
		if (ref.kindLabel == "Attribute") {
			if (AttributeTypeRefsTarget(ref.node, targetClsid)) {
				out.push_back(MakeReferenceEntry(
					"attribute_type", ref.fullName,
					std::string("type points to ") + targetFullName));
				continue;
			}
		}

		// (2) Module text ref (substring / word-boundary against bareName)
		if (!bareName.empty()) {
			if (auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(ref.node)) {
				const std::string source = std::string(mod->GetModuleText().utf8_str());
				const ModuleScanResult sr = ScanModuleForName(source, bareName);
				if (sr.any) {
					std::string ctx = "line " + std::to_string(sr.line);
					if (!sr.snippet.empty()) ctx += ": " + sr.snippet;
					if (sr.matchCount > 1) {
						ctx += " (+" + std::to_string(sr.matchCount - 1) + " more)";
					}
					out.push_back(MakeReferenceEntry(
						"module_text", ref.fullName, ctx));
					continue;
				}
			}
		}
	}

	return out;
}

} // namespace

// =========================================================================
// Tool: find_references
// =========================================================================
nlohmann::json ToolFindReferences(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"find_references: 'fullName' is required (e.g. 'Catalog.Контрагенты')");
	}
	ibValueMetaObject* target = ResolveByPath(fullName);
	if (target == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"find_references: target not found: " + fullName);
	}

	std::vector<nlohmann::json> refs = CollectReferencesTo(fullName);

	nlohmann::json structured;
	structured["target"]           = fullName;
	structured["references"]       = nlohmann::json::array();
	for (const auto& r : refs) structured["references"].push_back(r);
	structured["totalReferences"]  = refs.size();

	nlohmann::json env = TextResult(structured.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: rename_with_refs
// =========================================================================
nlohmann::json ToolRenameWithRefs(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string oldName = ArgString(args, "fullName");
	const std::string newName = ArgString(args, "newName");
	// MCP: dryRun defaults to TRUE — text-based ref patching is best-effort
	// (word-boundary regex on module sources can drop hits inside string
	// literals or pick up false positives in comments). Force the agent
	// to opt-in to the actual mutation.
	const bool dryRun = ArgBool(args, "dryRun", true);
	if (oldName.empty() || newName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"rename_with_refs: 'fullName' and 'newName' are required");
	}

	ibValueMetaObject* target = ResolveByPath(oldName);
	if (target == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"rename_with_refs: target not found: " + oldName);
	}

	// Validate that both fullNames share the same kind — renaming across
	// kinds (Catalog.X -> Document.X) isn't a rename, it's a recreate.
	const auto [oldKind, oldBare] = SplitTopName(oldName);
	const auto [newKind, newBare] = SplitTopName(newName);
	if (oldKind != newKind) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"rename_with_refs: cross-kind rename not supported (" +
			oldKind + " -> " + newKind + ")");
	}
	if (newBare.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"rename_with_refs: newName must include the bare name "
			"(e.g. 'Catalog.NewName')");
	}

	// Gather references (uses the same routine as find_references).
	std::vector<nlohmann::json> refs = CollectReferencesTo(oldName);

	nlohmann::json structured;
	structured["oldName"]            = oldName;
	structured["newName"]            = newName;
	structured["dryRun"]             = dryRun;
	structured["updatedReferences"]  = nlohmann::json::array();
	for (const auto& r : refs) structured["updatedReferences"].push_back(r);
	structured["warnings"]           = nlohmann::json::array();

	if (dryRun) {
		structured["ok"]                = true;
		structured["appliedMutations"]  = 0;
		structured["warnings"].push_back(
			"dry-run: no mutations applied. Pass dryRun=false to execute.");
		structured["warnings"].push_back(
			"text-based reference patching is best-effort — string literals "
			"and comments containing the bare name will be rewritten too.");
		nlohmann::json env = TextResult(structured.dump(2), false);
		env["structuredContent"] = std::move(structured);
		return env;
	}

	// Apply path — re-check lock before any mutation.
	if (auto lockFail = RequireLockStillHeld("rename_with_refs"); lockFail) {
		return *lockFail;
	}

	unsigned applied = 0;

	// 1) Rewrite module text refs (regex word-boundary replace) via
	//    write_module against each module location.
	for (const auto& r : refs) {
		if (!r.is_object() || !r.contains("kind") || !r["kind"].is_string()) continue;
		if (r["kind"].get<std::string>() != "module_text") continue;
		const std::string modPath = r.value("location", std::string());
		if (modPath.empty()) continue;
		ibValueMetaObject* node = ResolveByPath(modPath);
		if (node == nullptr) {
			structured["warnings"].push_back(
				"module path could not be re-resolved: " + modPath);
			continue;
		}
		auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(node);
		if (mod == nullptr) continue;
		std::string source = std::string(mod->GetModuleText().utf8_str());
		std::string pattern = NeedleHasWordChars(oldBare)
			? ("\\b" + EscapeRegex(oldBare) + "\\b")
			: EscapeRegex(oldBare);
		try {
			std::regex rx(pattern, std::regex::ECMAScript);
			std::string replaced = std::regex_replace(source, rx, newBare);
			if (replaced != source) {
				mod->SetModuleText(wxString::FromUTF8(replaced.c_str()));
				NotifyMutation("rename_with_refs", modPath);
				EmitResourceMutation(modPath);
				++applied;
			}
		} catch (const std::regex_error& e) {
			structured["warnings"].push_back(
				std::string("regex replace failed on ") + modPath + ": " + e.what());
		}
	}

	// 2) Rename the target itself via metaBridge::HostMetaEdit with a
	//    {"name": "<newBare>"} patch. metaBridge edits land on the live
	//    object and push an undo lambda for Ctrl+Z.
	{
		nlohmann::json patch;
		patch["name"] = newBare;
		const std::string patchJson = patch.dump();
		char* errMsg = nullptr;
		const int rc = metaBridge::HostMetaEdit(kPluginId, oldName.c_str(),
			patchJson.c_str(), &errMsg);
		if (rc != 0) {
			std::string msg = "rename_with_refs: failed to rename target";
			if (errMsg != nullptr) { msg += ": "; msg += errMsg; }
			FreeIfSet(errMsg);
			structured["warnings"].push_back(msg);
		} else {
			NotifyMutation("rename_with_refs", oldName);
			EmitResourceMutation(oldName);
			EmitResourceMutation(newName);
			++applied;
		}
		FreeIfSet(errMsg);
	}

	structured["ok"]               = true;
	structured["appliedMutations"] = applied;

	nlohmann::json env = TextResult(structured.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: metadata_diff
// =========================================================================
namespace {

// MCP: deep-diff two JSON values, emitting modified entries keyed by
// dotted JSON-Path. Lists are compared positionally — index changes show
// up as modifications, not adds/removes. v2 may switch to set-diff with
// LCS for arrays-of-objects keyed by `name`.
void DiffJsonRecursive(const std::string& path,
                       const nlohmann::json& left,
                       const nlohmann::json& right,
                       std::vector<nlohmann::json>& changes)
{
	if (left.type() != right.type()) {
		nlohmann::json c;
		c["path"] = path;
		c["old"]  = left;
		c["new"]  = right;
		changes.push_back(std::move(c));
		return;
	}
	if (left.is_object()) {
		for (auto it = left.begin(); it != left.end(); ++it) {
			const std::string sub = path.empty() ? it.key() : (path + "." + it.key());
			if (!right.contains(it.key())) {
				nlohmann::json c;
				c["path"] = sub;
				c["old"]  = it.value();
				c["new"]  = nullptr;
				changes.push_back(std::move(c));
				continue;
			}
			DiffJsonRecursive(sub, it.value(), right[it.key()], changes);
		}
		for (auto it = right.begin(); it != right.end(); ++it) {
			if (!left.contains(it.key())) {
				const std::string sub = path.empty() ? it.key() : (path + "." + it.key());
				nlohmann::json c;
				c["path"] = sub;
				c["old"]  = nullptr;
				c["new"]  = it.value();
				changes.push_back(std::move(c));
			}
		}
		return;
	}
	if (left.is_array()) {
		const std::size_t n = std::max(left.size(), right.size());
		for (std::size_t i = 0; i < n; ++i) {
			const std::string sub = path + "[" + std::to_string(i) + "]";
			if (i >= left.size()) {
				nlohmann::json c;
				c["path"] = sub;
				c["old"]  = nullptr;
				c["new"]  = right[i];
				changes.push_back(std::move(c));
				continue;
			}
			if (i >= right.size()) {
				nlohmann::json c;
				c["path"] = sub;
				c["old"]  = left[i];
				c["new"]  = nullptr;
				changes.push_back(std::move(c));
				continue;
			}
			DiffJsonRecursive(sub, left[i], right[i], changes);
		}
		return;
	}
	// Primitive — straight inequality
	if (left != right) {
		nlohmann::json c;
		c["path"] = path;
		c["old"]  = left;
		c["new"]  = right;
		changes.push_back(std::move(c));
	}
}

// MCP: enumerate top-level entries on each snapshot side. Snapshots are
// shaped {fullName -> object}. We accept either that flat form, or an
// array of {fullName, ...} objects. The caller's contract is documented
// in the tool description.
std::map<std::string, nlohmann::json> NormaliseSnapshot(const nlohmann::json& snap)
{
	std::map<std::string, nlohmann::json> out;
	if (snap.is_object()) {
		// Flat form: top-level keys are fullNames.
		for (auto it = snap.begin(); it != snap.end(); ++it) {
			out[it.key()] = it.value();
		}
		return out;
	}
	if (snap.is_array()) {
		for (const auto& entry : snap) {
			if (!entry.is_object()) continue;
			if (!entry.contains("fullName") || !entry["fullName"].is_string()) continue;
			out[entry["fullName"].get<std::string>()] = entry;
		}
		return out;
	}
	return out;
}

} // namespace

nlohmann::json ToolMetadataDiff(const nlohmann::json& args)
{
	// File mode — DEFERRED. Loading two full configurations side-by-side
	// requires duplicating the headless boot path with a second
	// activeMetaData root; the v1 surface is inline-snapshot only.
	if (args.is_object() && (args.contains("leftPath") || args.contains("rightPath"))) {
		return StructuredErrorCode("OES_E_NOT_IMPLEMENTED",
			"metadata_diff: file-path mode is deferred — load both snapshots "
			"via meta_query (or a separate configurator session) and pass them "
			"as leftSnapshot / rightSnapshot inline JSON instead");
	}

	if (!args.is_object() ||
	    !args.contains("leftSnapshot") || !args.contains("rightSnapshot"))
	{
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"metadata_diff: 'leftSnapshot' and 'rightSnapshot' inline JSON "
			"objects are required");
	}

	const auto leftMap  = NormaliseSnapshot(args["leftSnapshot"]);
	const auto rightMap = NormaliseSnapshot(args["rightSnapshot"]);

	nlohmann::json added    = nlohmann::json::array();
	nlohmann::json removed  = nlohmann::json::array();
	nlohmann::json modified = nlohmann::json::array();

	for (const auto& kv : leftMap) {
		auto rit = rightMap.find(kv.first);
		if (rit == rightMap.end()) {
			nlohmann::json e;
			e["fullName"] = kv.first;
			e["kind"]     = kv.second.is_object() && kv.second.contains("kind")
				? kv.second["kind"] : nlohmann::json();
			removed.push_back(std::move(e));
			continue;
		}
		std::vector<nlohmann::json> changes;
		DiffJsonRecursive(std::string(), kv.second, rit->second, changes);
		if (!changes.empty()) {
			nlohmann::json e;
			e["fullName"] = kv.first;
			e["kind"]     = kv.second.is_object() && kv.second.contains("kind")
				? kv.second["kind"] : nlohmann::json();
			e["changes"]  = nlohmann::json::array();
			for (const auto& c : changes) e["changes"].push_back(c);
			modified.push_back(std::move(e));
		}
	}
	for (const auto& kv : rightMap) {
		if (leftMap.find(kv.first) == leftMap.end()) {
			nlohmann::json e;
			e["fullName"] = kv.first;
			e["kind"]     = kv.second.is_object() && kv.second.contains("kind")
				? kv.second["kind"] : nlohmann::json();
			added.push_back(std::move(e));
		}
	}

	nlohmann::json structured;
	structured["added"]    = std::move(added);
	structured["removed"]  = std::move(removed);
	structured["modified"] = std::move(modified);
	nlohmann::json summary;
	summary["added"]    = structured["added"].size();
	summary["removed"]  = structured["removed"].size();
	summary["modified"] = structured["modified"].size();
	structured["summary"] = std::move(summary);

	nlohmann::json env = TextResult(structured.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: dependency_graph
// =========================================================================
namespace {

// MCP: scan a node's outgoing references — used by dependency_graph in
// direction="out" mode. Symmetric to CollectReferencesTo (which walks
// the tree for *incoming* refs).
std::vector<std::pair<std::string, std::string>> OutgoingRefsFrom(
	ibValueMetaObject* origin, const std::string& originFullName)
{
	std::vector<std::pair<std::string, std::string>> out;  // (toFullName, label)
	if (origin == nullptr || activeMetaData == nullptr) return out;

	// (1) Attribute type CLSIDs — walk every descendant Attribute and
	//     resolve its types back to a top-level metaObject. We compare
	//     against every top-level object's CLSID and match.
	std::vector<TreeNodeRef> topNodes;
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) return out;
	for (ibValueMetaObject* child : root->GetAnyArrayObject<>()) {
		if (child == nullptr) continue;
		const char* kindLabel = CLSIDToKindLabel(
			static_cast<unsigned long long>(child->GetClassType()));
		if (kindLabel == nullptr || *kindLabel == '\0') continue;
		TreeNodeRef ref;
		ref.node      = child;
		ref.fullName  = std::string(kindLabel) + "." +
			std::string(child->GetName().utf8_str());
		ref.kindLabel = kindLabel;
		topNodes.push_back(std::move(ref));
	}

	// Walk origin's own attribute descendants
	std::vector<TreeNodeRef> originSubtree;
	CollectTreeNodes(origin, std::string(), originSubtree);
	for (const TreeNodeRef& sub : originSubtree) {
		if (sub.kindLabel != "Attribute") continue;
		for (const TreeNodeRef& candidate : topNodes) {
			if (candidate.node == origin) continue;  // skip self
			if (AttributeTypeRefsTarget(sub.node, candidate.node->GetClassType())) {
				out.push_back({ candidate.fullName, "attribute_type" });
			}
		}
	}

	// (2) Module text — scan every module under origin for top-level
	//     object names. Word-boundary, same logic as CollectReferencesTo.
	for (const TreeNodeRef& sub : originSubtree) {
		auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(sub.node);
		if (mod == nullptr) continue;
		const std::string source = std::string(mod->GetModuleText().utf8_str());
		if (source.empty()) continue;
		for (const TreeNodeRef& candidate : topNodes) {
			if (candidate.node == origin) continue;
			const std::string bareName = std::string(candidate.node->GetName().utf8_str());
			if (bareName.empty()) continue;
			const ModuleScanResult sr = ScanModuleForName(source, bareName);
			if (sr.any) {
				out.push_back({ candidate.fullName, "module_call" });
			}
		}
	}

	(void)originFullName;
	return out;
}

} // namespace

nlohmann::json ToolDependencyGraph(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"dependency_graph: 'fullName' is required");
	}
	std::string direction = ArgString(args, "direction");
	if (direction.empty()) direction = "both";
	if (direction != "in" && direction != "out" && direction != "both") {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"dependency_graph: 'direction' must be one of: in, out, both");
	}
	int depth = 2;
	if (args.is_object() && args.contains("depth") && args["depth"].is_number_integer()) {
		depth = args["depth"].get<int>();
		if (depth < 1) depth = 1;
		if (depth > 5) depth = 5;  // safety cap — exponential blowup beyond this
	}

	ibValueMetaObject* root = ResolveByPath(fullName);
	if (root == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"dependency_graph: root not found: " + fullName);
	}

	// BFS: maintain a visited set keyed by top-level fullName, with the
	// distance from root. We only enumerate top-level nodes here — child
	// references roll up to their owning top-level object.
	std::set<std::string> visited;
	std::vector<nlohmann::json> nodes;
	std::vector<nlohmann::json> edges;

	struct QueueEntry {
		std::string fullName;
		int         distance;
	};
	std::vector<QueueEntry> queue;
	queue.push_back({ TopLevelOf(fullName), 0 });
	visited.insert(TopLevelOf(fullName));

	while (!queue.empty()) {
		const QueueEntry e = queue.front();
		queue.erase(queue.begin());

		ibValueMetaObject* node = ResolveByPath(e.fullName);
		if (node != nullptr) {
			const char* kindLabel = CLSIDToKindLabel(
				static_cast<unsigned long long>(node->GetClassType()));
			nlohmann::json n;
			n["fullName"] = e.fullName;
			n["kind"]     = (kindLabel != nullptr) ? std::string(kindLabel) : std::string();
			nodes.push_back(std::move(n));
		}

		if (e.distance >= depth) continue;

		// Outgoing edges
		if (direction == "out" || direction == "both") {
			if (node != nullptr) {
				auto outs = OutgoingRefsFrom(node, e.fullName);
				for (const auto& kv : outs) {
					nlohmann::json edge;
					edge["from"]  = e.fullName;
					edge["to"]    = kv.first;
					edge["label"] = kv.second;
					edges.push_back(std::move(edge));
					if (visited.insert(kv.first).second) {
						queue.push_back({ kv.first, e.distance + 1 });
					}
				}
			}
		}

		// Incoming edges — reuse CollectReferencesTo, then bucket to top-level
		if (direction == "in" || direction == "both") {
			auto refs = CollectReferencesTo(e.fullName);
			for (const auto& r : refs) {
				if (!r.is_object() || !r.contains("location") ||
				    !r["location"].is_string()) continue;
				const std::string loc = r["location"].get<std::string>();
				const std::string topLoc = TopLevelOf(loc);
				const std::string label  = r.value("kind", std::string("ref"));
				nlohmann::json edge;
				edge["from"]  = topLoc;
				edge["to"]    = e.fullName;
				edge["label"] = label;
				edges.push_back(std::move(edge));
				if (visited.insert(topLoc).second) {
					queue.push_back({ topLoc, e.distance + 1 });
				}
			}
		}
	}

	nlohmann::json structured;
	structured["root"]      = fullName;
	structured["direction"] = direction;
	structured["depth"]     = depth;
	structured["nodes"]     = nlohmann::json::array();
	for (const auto& n : nodes) structured["nodes"].push_back(n);
	structured["edges"]     = nlohmann::json::array();
	for (const auto& e : edges) structured["edges"].push_back(e);

	nlohmann::json env = TextResult(structured.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: extract_module_to_common
// =========================================================================
namespace {

// MCP: find a procedure / function definition by name in source. Returns
// the start/end byte offsets of the full definition (Procedure/Function
// keyword through matching EndProcedure/EndFunction) plus the keyword
// kind. Returns {0,0,""} when not found. CES braces are NOT handled in
// v1 — extraction is VES-only for the initial drop. CES support is a
// straightforward follow-up but needs a brace counter.
struct ProcedureSlice {
	std::size_t start    = 0;
	std::size_t end      = 0;
	std::string kind;     // "Procedure" or "Function"
	std::string fullDef;  // verbatim source between [start, end)
};

ProcedureSlice FindProcedure(const std::string& source, const std::string& name)
{
	ProcedureSlice res;
	if (source.empty() || name.empty()) return res;

	// VES pattern: (Procedure|Function)\s+<name>\s*\(
	// Match opening, then find matching End{Procedure|Function}
	const std::string esc = EscapeRegex(name);
	const std::string pattern = "(Procedure|Function)\\s+" + esc + "\\s*\\(";
	try {
		std::regex rx(pattern, std::regex::ECMAScript | std::regex::icase);
		std::smatch m;
		if (!std::regex_search(source, m, rx)) return res;
		res.start = static_cast<std::size_t>(m.position(0));
		res.kind  = m[1].str();

		// Find matching EndProcedure / EndFunction. Naive lookup — nested
		// definitions break this. v1 assumes single-level. (v2: lexer.)
		const std::string endTok = (res.kind == "Function" || res.kind == "function")
			? "EndFunction" : "EndProcedure";
		std::regex endRx("\\b" + endTok + "\\b",
			std::regex::ECMAScript | std::regex::icase);
		auto begin = std::sregex_iterator(source.begin() + res.start, source.end(), endRx);
		auto end   = std::sregex_iterator();
		if (begin == end) return ProcedureSlice{};
		res.end = res.start +
			static_cast<std::size_t>(begin->position(0) + begin->length(0));
		res.fullDef = source.substr(res.start, res.end - res.start);
		return res;
	} catch (const std::regex_error&) {
		return ProcedureSlice{};
	}
}

} // namespace

nlohmann::json ToolExtractModuleToCommon(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string sourcePath   = ArgString(args, "sourceFullName");
	const std::string procName     = ArgString(args, "procedureName");
	const std::string targetCommon = ArgString(args, "targetCommonModule");
	// dryRun default TRUE — actual apply is deferred (see warnings below).
	const bool dryRun = ArgBool(args, "dryRun", true);
	if (sourcePath.empty() || procName.empty() || targetCommon.empty()) {
		return StructuredErrorCode("OES_E_INVALID_INPUT",
			"extract_module_to_common: 'sourceFullName', 'procedureName', and "
			"'targetCommonModule' are required");
	}

	ibValueMetaObject* sourceNode = ResolveByPath(sourcePath);
	if (sourceNode == nullptr) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"extract_module_to_common: source module not found: " + sourcePath);
	}
	auto* sourceMod = dynamic_cast<ibValueMetaObjectModuleBase*>(sourceNode);
	if (sourceMod == nullptr) {
		return StructuredErrorCode("OES_E_NOT_A_MODULE",
			"extract_module_to_common: '" + sourcePath + "' is not a module");
	}

	const std::string source = std::string(sourceMod->GetModuleText().utf8_str());
	const ProcedureSlice slice = FindProcedure(source, procName);
	if (slice.fullDef.empty()) {
		return StructuredErrorCode("OES_E_NOT_FOUND",
			"extract_module_to_common: procedure '" + procName +
			"' not found in " + sourcePath +
			" (v1 supports VES Procedure/Function syntax only; CES brace "
			"matching is deferred)");
	}

	// Count call-sites inside the source module (excluding the definition
	// itself). word-boundary regex on procName.
	unsigned callsites = 0;
	{
		const std::string pattern = NeedleHasWordChars(procName)
			? ("\\b" + EscapeRegex(procName) + "\\b")
			: EscapeRegex(procName);
		try {
			std::regex rx(pattern, std::regex::ECMAScript);
			auto begin = std::sregex_iterator(source.begin(), source.end(), rx);
			auto end   = std::sregex_iterator();
			for (auto it = begin; it != end; ++it) {
				const std::size_t pos = static_cast<std::size_t>(it->position(0));
				// Skip the definition line itself
				if (pos >= slice.start && pos < slice.end) continue;
				++callsites;
			}
		} catch (const std::regex_error&) {
			// callsites stays 0 — best effort
		}
	}

	nlohmann::json structured;
	structured["sourceFullName"]      = sourcePath;
	structured["procedureName"]       = procName;
	structured["targetCommonModule"]  = targetCommon;
	structured["extractedTo"]         = targetCommon + "." + procName;
	structured["sourceCallsiteCount"] = callsites;
	structured["extractedText"]       = slice.fullDef;
	structured["dryRun"]              = dryRun;
	structured["plannedMutations"]    = nlohmann::json::array();

	// Planned mutations the apply path would execute. Surfaced even in
	// dry-run so the agent can reason about scope before opting in.
	{
		nlohmann::json m1;
		m1["op"]       = "append_text";
		m1["target"]   = targetCommon;
		m1["bytes"]    = slice.fullDef.size();
		m1["note"]     = "Append extracted procedure to the common module";
		structured["plannedMutations"].push_back(std::move(m1));

		nlohmann::json m2;
		m2["op"]       = "replace_definition";
		m2["target"]   = sourcePath;
		m2["bytes"]    = slice.fullDef.size();
		m2["note"]     = "Replace original definition with a thin wrapper that "
			"forwards to " + targetCommon + "." + procName;
		structured["plannedMutations"].push_back(std::move(m2));
	}

	structured["warnings"] = nlohmann::json::array();
	structured["warnings"].push_back(
		"v1 PARTIAL: dry-run only. Text-based extraction is brittle when the "
		"procedure body references local module variables, owns inner "
		"closures, or relies on Export-scoped state. Pass dryRun=false to "
		"opt-in once the platform's lexer-aware extractor lands.");

	if (!dryRun) {
		// Actual mutation path is deferred — the wrapper-rewrite step needs
		// a lexer-aware locator for the original definition's parameter
		// list (so the thin wrapper forwards the right arguments), plus
		// CommonModule existence + auto-create logic. v1 stops here and
		// returns the deferral.
		structured["ok"]        = false;
		structured["errorCode"] = "OES_E_NOT_IMPLEMENTED";
		nlohmann::json env = TextResult(structured.dump(2), true);
		env["structuredContent"] = std::move(structured);
		env["isError"]           = true;
		return env;
	}

	structured["ok"] = true;
	nlohmann::json env = TextResult(structured.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: snapshots_list
// =========================================================================
nlohmann::json ToolSnapshotsList(const nlohmann::json& args)
{
	// MCP: snapshots_list is config-bound — the store lives at
	// <configDir>/sys/oes-snapshots. In --no-config mode the manager
	// is absent, so we return a structured error that matches the
	// "no config loaded" convention used by every other config-bound
	// tool.
	auto* mgr = GetSnapshotManager();
	if (mgr == nullptr) {
		return StructuredError("snapshots_list: no configuration loaded — "
		                        "start the server with a config directory");
	}

	std::size_t limit = 50;
	if (args.is_object() && args.contains("limit") && args["limit"].is_number_integer()) {
		const long long v = args["limit"].get<long long>();
		if (v > 0) limit = static_cast<std::size_t>(v);
	}

	wxDateTime since = wxInvalidDateTime;
	if (args.is_object() && args.contains("since") && args["since"].is_string()) {
		const std::string s = args["since"].get<std::string>();
		const wxString ws = wxString::FromUTF8(s.c_str());
		wxDateTime dt;
		if (dt.ParseISOCombined(ws) || dt.ParseDateTime(ws)) {
			// MCP: treat the parsed instant as UTC so it lines up with the
			// store's UTC-normalised timestamps. Without this `since` would
			// drift by the system TZ offset.
			dt.MakeFromTimezone(wxDateTime::UTC);
			since = dt;
		} else {
			return StructuredError("snapshots_list: 'since' must be ISO-8601");
		}
	}

	const auto rows = mgr->List(limit, since);
	nlohmann::json snapshots = nlohmann::json::array();
	for (const auto& r : rows) {
		nlohmann::json item;
		item["id"]          = std::string(r.id.utf8_str());
		if (r.timestamp.IsValid()) {
			item["timestamp"] = std::string(
				r.timestamp.ToUTC().Format(wxT("%Y-%m-%dT%H:%M:%SZ")).utf8_str());
		}
		item["tool"]        = std::string(r.triggeredBy.utf8_str());
		item["fullName"]    = std::string(r.fullName.utf8_str());
		item["operation"]   = std::string(r.operation.utf8_str());
		item["description"] = std::string(r.description.utf8_str());
		item["sizeBytes"]   = r.sizeBytes;
		item["consumed"]    = r.consumed;
		snapshots.push_back(std::move(item));
	}

	nlohmann::json structured;
	structured["snapshots"] = std::move(snapshots);
	structured["total"]     = rows.size();

	nlohmann::json env = TextResult(structured.dump(2), false);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Tool: snapshot_rollback
//
// dryRun=true (default): returns a plan describing the reverse mutations
// without applying them.
// dryRun=false: returns OES_E_NOT_IMPLEMENTED — the apply path needs
// metaBridge undo-of-undo testing that's deferred to v2.
// =========================================================================
nlohmann::json ToolSnapshotRollback(const nlohmann::json& args)
{
	auto* mgr = GetSnapshotManager();
	if (mgr == nullptr) {
		return StructuredError("snapshot_rollback: no configuration loaded");
	}
	const std::string id = ArgString(args, "snapshotId");
	if (id.empty()) {
		return StructuredError("snapshot_rollback: 'snapshotId' is required");
	}
	const bool dryRun = args.is_object() && args.contains("dryRun")
		? ArgBool(args, "dryRun", true)
		: true;  // default to safe-mode

	const wxString body = mgr->Load(wxString::FromUTF8(id.c_str()));
	if (body.IsEmpty()) {
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"] = "OES_E_SNAPSHOT_NOT_FOUND";
		sc["id"]        = id;
		nlohmann::json env = TextResult(
			"snapshot_rollback: unknown snapshot id '" + id + "'", true);
		env["structuredContent"] = std::move(sc);
		return env;
	}

	auto parsed = nlohmann::json::parse(std::string(body.utf8_str()), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		return StructuredError("snapshot_rollback: snapshot body is not JSON");
	}

	// MCP: re-construct the reverse-mutation plan from the snapshot body.
	// Plan shape mirrors the wizard Applier format:
	//   { op: create|edit|delete|write_module, fullName, properties?, source? }
	// so a future apply path can reuse oes_template_customize's executor.
	const std::string op = parsed.value("operation", std::string());
	const std::string fullName = parsed.value("fullName", std::string());

	nlohmann::json plan = nlohmann::json::array();

	if (op == "create") {
		// Undo a creation by deleting the same object.
		nlohmann::json m;
		m["op"]       = "delete";
		m["fullName"] = fullName;
		m["force"]    = true;
		m["rationale"] = "snapshot recorded a create — undo by delete";
		plan.push_back(std::move(m));
	} else if (op == "delete") {
		// Undo a deletion by recreating from the captured priorState. The
		// agent / executor must look at priorState["kind"] (or fall back to
		// the meta_query-flavoured payload's "kind") to drive HostMetaCreate.
		nlohmann::json m;
		m["op"]         = "create";
		m["fullName"]   = fullName;
		// Best-effort kind extraction from the meta_query-shaped priorState.
		std::string kind;
		if (parsed.contains("priorState") && parsed["priorState"].is_object()) {
			if (parsed["priorState"].contains("kind") &&
			    parsed["priorState"]["kind"].is_string()) {
				kind = parsed["priorState"]["kind"].get<std::string>();
			}
		}
		if (kind.empty()) {
			// Derive from the dotted path's head segment (Catalog.X → Catalog).
			const auto dot = fullName.find('.');
			if (dot != std::string::npos) kind = fullName.substr(0, dot);
		}
		m["kind"]       = kind;
		m["properties"] = parsed.value("priorState", nlohmann::json::object());
		m["rationale"]  = "snapshot recorded a delete — undo by recreate";
		plan.push_back(std::move(m));
	} else if (op == "edit") {
		// Undo an edit by re-applying priorState as a full property merge.
		nlohmann::json m;
		m["op"]         = "edit";
		m["fullName"]   = fullName;
		m["properties"] = parsed.value("priorState", nlohmann::json::object());
		m["rationale"]  = "snapshot recorded an edit — undo by restoring prior shape";
		plan.push_back(std::move(m));
	} else if (op == "write_module") {
		nlohmann::json m;
		m["op"]       = "write_module";
		m["fullName"] = fullName;
		// priorState body is `{path, kind, source}` — pull source verbatim.
		std::string priorSource;
		if (parsed.contains("priorState") && parsed["priorState"].is_object() &&
		    parsed["priorState"].contains("source") &&
		    parsed["priorState"]["source"].is_string()) {
			priorSource = parsed["priorState"]["source"].get<std::string>();
		}
		m["source"]    = priorSource;
		m["rationale"] = "snapshot recorded a write_module — undo by restoring prior source";
		plan.push_back(std::move(m));
	} else {
		nlohmann::json sc;
		sc["errorCode"] = "OES_E_SNAPSHOT_UNSUPPORTED_OP";
		sc["operation"] = op;
		nlohmann::json env = TextResult(
			"snapshot_rollback: unsupported operation '" + op + "'", true);
		env["structuredContent"] = std::move(sc);
		return env;
	}

	nlohmann::json structured;
	structured["id"]       = id;
	structured["dryRun"]   = dryRun;
	structured["plan"]     = std::move(plan);
	structured["operation"] = op;
	structured["fullName"] = fullName;

	if (dryRun) {
		structured["ok"]              = true;
		structured["restoredObjects"] = 0;
		nlohmann::json env = TextResult(structured.dump(2), false);
		env["structuredContent"] = std::move(structured);
		return env;
	}

	// MCP: apply path is intentionally deferred — the undo plan we emit
	// is the same shape oes_template_customize executes, but driving it
	// from inside oes-mcp requires careful undo-of-undo testing
	// (re-pushing the metaBridge stack, re-emitting NotifyMutation,
	// re-firing resource subscribers without re-triggering downstream
	// captures into an infinite loop). v2.
	structured["ok"]        = false;
	structured["errorCode"] = "OES_E_NOT_IMPLEMENTED";
	structured["message"]   = "apply path deferred to v2 — dry-run plan is correct";
	nlohmann::json env = TextResult(structured.dump(2), true);
	env["structuredContent"] = std::move(structured);
	return env;
}

// =========================================================================
// Transactional staging — process-singleton state for transaction_begin /
// transaction_commit / transaction_rollback. One active transaction at a
// time; refusal on double-begin.
//
// Design: we don't touch wxCommandProcessor directly — oes-mcp runs
// headless and the Designer's wxDoc command processor is not wired in
// that mode. Instead we snapshot the metaBridge undo stack depth on
// begin, then on rollback we pop UndoLastAgentMutation() repeatedly until
// the stack is back to that depth. Commit just forgets the record (the
// undo entries remain individually revertable via Ctrl+Z in Designer; v2
// can collapse them into one compound entry once Designer plumbs
// wxCommandProcessor into Phase 3.4).
// =========================================================================

struct TransactionRecord {
	std::string label;           // audit-log description
	size_t      undoDepthBefore; // metaBridge undo stack depth at begin
	int         mutationCount;   // incremented on every successful mutation
};

std::map<std::string, TransactionRecord> g_activeTransactions;
std::mutex                               g_txMutex;

// Generate a simple time-based UUID-like id: "tx-<timestamp_us>-<random>".
// We don't depend on libUUID; ibGuid::newGuid() lives in backend.dll but
// requires wx base init. A hex timestamp + random suffix gives adequate
// uniqueness for a process-local map with a single active tx at a time.
std::string GenerateTransactionId()
{
	// Use the existing ibGuid machinery — it's available in the headless
	// app since backend.dll is always loaded before mcp-server runs.
	const ibGuid g = ibGuid::newGuid();
	return std::string(g.str().utf8_str());
}

// =========================================================================
// Tool: transaction_begin
// =========================================================================
nlohmann::json ToolTransactionBegin(const nlohmann::json& args)
{
	const std::string label = ArgString(args, "label");

	std::lock_guard<std::mutex> lock(g_txMutex);
	if (!g_activeTransactions.empty()) {
		const auto& existing = g_activeTransactions.begin();
		nlohmann::json env = TextResult(
			"transaction_begin: another transaction is already active "
			"(id=" + existing->first + "). Call transaction_commit or "
			"transaction_rollback first.", true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"]           = "OES_E_TX_ALREADY_ACTIVE";
		sc["existingTransaction"] = existing->first;
		env["structuredContent"]  = std::move(sc);
		return env;
	}

	const size_t depth = metaBridge::UndoStackSize();
	const std::string txId = GenerateTransactionId();

	g_activeTransactions[txId] = { label, depth, 0 };

	wxLogMessage(wxT("oes-mcp: transaction_begin id=%s label=%s undoDepth=%zu"),
	             wxString::FromUTF8(txId.c_str()),
	             wxString::FromUTF8(label.c_str()),
	             depth);

	nlohmann::json env = TextResult(
		"transaction_begin OK: id=" + txId +
		" undoStackBefore=" + std::to_string(static_cast<int>(depth)),
		false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"]             = true;
	sc["transactionId"]  = txId;
	sc["undoStackBefore"] = static_cast<int>(depth);
	env["structuredContent"] = std::move(sc);
	return env;
}

// =========================================================================
// Tool: transaction_commit
// =========================================================================
nlohmann::json ToolTransactionCommit(const nlohmann::json& args)
{
	const std::string txId = ArgString(args, "transactionId");
	if (txId.empty()) {
		return TextResult("transaction_commit: 'transactionId' is required", true);
	}

	std::lock_guard<std::mutex> lock(g_txMutex);
	auto it = g_activeTransactions.find(txId);
	if (it == g_activeTransactions.end()) {
		nlohmann::json env = TextResult(
			"transaction_commit: unknown transactionId '" + txId + "'", true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"]      = "OES_E_TX_NOT_FOUND";
		sc["transactionId"]  = txId;
		env["structuredContent"] = std::move(sc);
		return env;
	}

	const int mutationsApplied = it->second.mutationCount;
	const size_t depthAfter    = metaBridge::UndoStackSize();
	g_activeTransactions.erase(it);

	wxLogMessage(wxT("oes-mcp: transaction_commit id=%s mutations=%d undoDepth=%zu"),
	             wxString::FromUTF8(txId.c_str()),
	             mutationsApplied,
	             depthAfter);

	// v1: undo entries stay as individual steps — Ctrl+Z reverts them
	// one by one. Compound-undo collapse (wxCommandProcessor::Submit with
	// a grouped command) is deferred to Phase 3.4 Designer integration.
	nlohmann::json env = TextResult(
		"transaction_commit OK: id=" + txId +
		" mutationsApplied=" + std::to_string(mutationsApplied) +
		" undoStackAfter=" + std::to_string(static_cast<int>(depthAfter)),
		false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"]              = true;
	sc["mutationsApplied"] = mutationsApplied;
	sc["undoStackAfter"]   = static_cast<int>(depthAfter);
	env["structuredContent"] = std::move(sc);
	return env;
}

// =========================================================================
// Tool: transaction_rollback
// =========================================================================
nlohmann::json ToolTransactionRollback(const nlohmann::json& args)
{
	const std::string txId = ArgString(args, "transactionId");
	if (txId.empty()) {
		return TextResult("transaction_rollback: 'transactionId' is required", true);
	}

	std::lock_guard<std::mutex> lock(g_txMutex);
	auto it = g_activeTransactions.find(txId);
	if (it == g_activeTransactions.end()) {
		nlohmann::json env = TextResult(
			"transaction_rollback: unknown transactionId '" + txId + "'", true);
		nlohmann::json sc = nlohmann::json::object();
		sc["errorCode"]      = "OES_E_TX_NOT_FOUND";
		sc["transactionId"]  = txId;
		env["structuredContent"] = std::move(sc);
		return env;
	}

	const size_t targetDepth = it->second.undoDepthBefore;
	int reverted = 0;

	// Pop undo entries until we reach the depth captured at begin.
	// metaBridge::UndoLastAgentMutation() handles epoch checks internally —
	// stale entries (dead config) are silently dropped, which is the same
	// safe behaviour we want here.
	while (metaBridge::UndoStackSize() > targetDepth) {
		const int rc = metaBridge::UndoLastAgentMutation();
		if (rc != 0) {
			// Stack may have been cleared by a config reload (epoch change).
			// Stop reverting — the tree is already gone.
			break;
		}
		++reverted;
	}

	g_activeTransactions.erase(it);

	wxLogMessage(wxT("oes-mcp: transaction_rollback id=%s reverted=%d"),
	             wxString::FromUTF8(txId.c_str()),
	             reverted);

	nlohmann::json env = TextResult(
		"transaction_rollback OK: id=" + txId +
		" mutationsReverted=" + std::to_string(reverted),
		false);
	nlohmann::json sc = nlohmann::json::object();
	sc["ok"]               = true;
	sc["mutationsReverted"] = reverted;
	env["structuredContent"] = std::move(sc);
	return env;
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
	{
		nlohmann::json smokeLoad;
		smokeLoad["type"] = "object";
		smokeLoad["properties"] = {
			{ "ok", nlohmann::json{ {"type","boolean"} } },
			{ "configPath", nlohmann::json{ {"type","string"} } },
			{ "name", nlohmann::json{ {"type","string"} } },
		};

		nlohmann::json smokeCompile;
		smokeCompile["type"] = "object";
		smokeCompile["properties"] = {
			{ "ok", nlohmann::json{ {"type","boolean"} } },
			{ "phase", nlohmann::json{ {"type","string"} } },
			{ "message", nlohmann::json{ {"type","string"} } },
		};

		nlohmann::json smokeTests;
		smokeTests["type"] = "object";
		smokeTests["properties"] = {
			{ "run", nlohmann::json{ {"type","boolean"} } },
			{ "summary", nlohmann::json{ {"type", nlohmann::json::array({"object", "null"})} } },
		};

		nlohmann::json outSmoke;
		outSmoke["type"] = "object";
		outSmoke["properties"] = {
			{ "ok", nlohmann::json{ {"type","boolean"} } },
			{ "load", smokeLoad },
			{ "compile", smokeCompile },
			{ "tests", smokeTests },
		};
		outSmoke["required"] = nlohmann::json::array({ "ok", "load", "compile", "tests" });

		nlohmann::json runTestsIn;
		runTestsIn["type"] = "boolean";
		runTestsIn["default"] = true;
		runTestsIn["description"] = "Run functional tests after load/compile checks";

		nlohmann::json stopIn;
		stopIn["type"] = "boolean";
		stopIn["default"] = true;
		stopIn["description"] = "Stop functional tests after first failure/error";

		nlohmann::json modulesIn;
		modulesIn["type"] = "array";
		modulesIn["items"] = nlohmann::json{ {"type","string"} };
		modulesIn["description"] = "Restrict tests to these module full names";

		table.push_back({
			{ "headless_smoke_run",
			  "Post-apply validation shortcut for agents. Verifies the "
			  "headless configuration is loaded, reports startup compile "
			  "status, and optionally runs the functional test suite through "
			  "the same rollback fixture as run_tests. Returns "
			  "structuredContent.ok plus load/compile/tests details.",
			  schemaObj({
			    { "runTests",           runTestsIn },
			    { "stopOnFirstFailure", stopIn     },
			    { "filter",             str("Optional test-name glob filter") },
			    { "modules",            modulesIn  },
			  }, {}),
			  ann(/*readOnly*/true, /*destructive*/false, /*idempotent*/true, /*openWorld*/false),
			  std::move(outSmoke)
			},
			&ToolHeadlessSmokeRun
		});
	}
	{
		nlohmann::json paramsIn;
		paramsIn["type"] = "array";
		paramsIn["items"] = nlohmann::json::object();
		paramsIn["description"] = "Positional parameters bound to ? placeholders";

		nlohmann::json maxRowsIn;
		maxRowsIn["type"] = "integer";
		maxRowsIn["default"] = 50;
		maxRowsIn["minimum"] = 1;
		maxRowsIn["maximum"] = 500;
		maxRowsIn["description"] = "Maximum rows to return";

		nlohmann::json validateOut;
		validateOut["type"] = "object";
		validateOut["properties"] = {
			{ "ok",       nlohmann::json{ {"type","boolean"} } },
			{ "readOnly", nlohmann::json{ {"type","boolean"} } },
			{ "errors",   nlohmann::json{ {"type","array"}, {"items", nlohmann::json{ {"type","string"} }} } },
		};
		validateOut["required"] = nlohmann::json::array({ "ok", "readOnly", "errors" });

		nlohmann::json executeOut;
		executeOut["type"] = "object";
		executeOut["properties"] = {
			{ "ok",        nlohmann::json{ {"type","boolean"} } },
			{ "readOnly",  nlohmann::json{ {"type","boolean"} } },
			{ "columns",   nlohmann::json{ {"type","array"} } },
			{ "rows",      nlohmann::json{ {"type","array"} } },
			{ "rowCount",  nlohmann::json{ {"type","integer"} } },
			{ "truncated", nlohmann::json{ {"type","boolean"} } },
		};
		executeOut["required"] = nlohmann::json::array(
			{ "ok", "readOnly", "columns", "rows", "truncated" });

		table.push_back({
			{ "validate_query",
			  "Validate that a SQL query is read-only and safe for execute_query. "
			  "Only SELECT/WITH statements are accepted; DDL/DML/control tokens "
			  "are rejected before database prepare.",
			  schemaObj({
			    { "query", str("SQL SELECT/WITH query to validate") },
			  }, { "query" }),
			  ann(true, false, true, false),
			  std::move(validateOut)
			},
			&ToolValidateQuery
		});
		table.push_back({
			{ "execute_query",
			  "Run a read-only SQL SELECT/WITH query against the live config "
			  "database. Parameters are bound via ibPreparedStatement; mutating "
			  "SQL is rejected before prepare. Returns columns and up to maxRows rows.",
			  schemaObj({
			    { "query",   str("SQL SELECT/WITH query. Use ? placeholders for params") },
			    { "params",  paramsIn },
			    { "maxRows", maxRowsIn },
			  }, { "query" }),
			  ann(true, false, false, false),
			  std::move(executeOut)
			},
			&ToolExecuteQuery
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

	// =====================================================================
	// Refactoring primitives (5) — added 2026-05-21. Cross-object awareness
	// for agents that need to do more than point-edit. find_references and
	// dependency_graph declare outputSchema; the mutating tools declare
	// destructive=true since they overwrite source text / metadata.
	// =====================================================================
	{
		nlohmann::json refItem;
		refItem["type"]       = "object";
		refItem["properties"] = nlohmann::json::object();
		refItem["properties"]["kind"]     = nlohmann::json{
			{"type","string"},
			{"description","attribute_type / module_text / register_dimension / register_record_binding / form_attribute_binding"}
		};
		refItem["properties"]["location"] = nlohmann::json{ {"type","string"} };
		refItem["properties"]["context"]  = nlohmann::json{ {"type","string"} };
		refItem["required"]   = nlohmann::json::array({ "kind", "location" });

		nlohmann::json outFR;
		outFR["type"]       = "object";
		outFR["properties"] = nlohmann::json::object();
		outFR["properties"]["target"]          = nlohmann::json{ {"type","string"} };
		outFR["properties"]["references"]      = nlohmann::json{ {"type","array"}, {"items", refItem} };
		outFR["properties"]["totalReferences"] = nlohmann::json{ {"type","integer"} };
		outFR["required"]   = nlohmann::json::array({ "target", "references" });

		table.push_back({
			{ "find_references",
			  "Find every reference to a metadata object across the loaded "
			  "configuration. Walks the tree and checks attribute type CLSIDs, "
			  "register dimension types, and module source text (word-boundary "
			  "match on the bare name). Returns structuredContent.references[] "
			  "with kind/location/context per hit. Use before rename_with_refs "
			  "or delete to see the blast radius.",
			  schemaObj({
			    { "fullName", str("Target full name, e.g. 'Catalog.Контрагенты'") },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outFR)
			},
			&ToolFindReferences
		});
	}
	table.push_back({
		{ "rename_with_refs",
		  "Rename a metadata object and rewrite every reference to it (module "
		  "text + attribute types). dryRun=true (default) returns the planned "
		  "mutations without applying. dryRun=false applies the rename via "
		  "metaBridge::HostMetaEdit (pushes an undo lambda for Ctrl+Z) and "
		  "rewrites every module reference via word-boundary regex. WARNING: "
		  "text-based ref patching is best-effort — string literals and "
		  "comments containing the bare name will be rewritten too.",
		  schemaObj({
		    { "fullName", str("Old full name, e.g. 'Catalog.Старое'") },
		    { "newName",  str("New full name, same kind, e.g. 'Catalog.Новое'") },
		    { "dryRun",   boolP("If true (default), preview only — no mutations applied") },
		  }, { "fullName", "newName" }),
		  // destructive=true: overwrites module sources + the target's name.
		  // Idempotent at the final-state level: applying the same rename
		  // twice resolves to the same metadata graph.
		  ann(/*readOnly*/false, /*destructive*/true,
		      /*idempotent*/true, /*openWorld*/false)
		},
		&ToolRenameWithRefs
	});
	{
		nlohmann::json snapIn;
		snapIn["type"]        = nlohmann::json::array({ "object", "array" });
		snapIn["description"] = "Inline metadata snapshot — either a flat map "
			"{fullName -> object} or an array of objects with fullName fields. "
			"Pass results of meta_query/list_objects directly.";

		nlohmann::json changeItem;
		changeItem["type"]       = "object";
		changeItem["properties"] = nlohmann::json::object();
		changeItem["properties"]["path"] = nlohmann::json{ {"type","string"} };
		changeItem["properties"]["old"]  = nlohmann::json::object();
		changeItem["properties"]["new"]  = nlohmann::json::object();

		table.push_back({
			{ "metadata_diff",
			  "Diff two metadata snapshots. v1 inline-snapshot mode only — "
			  "load both via meta_query / list_objects and pass them as "
			  "leftSnapshot / rightSnapshot. File-path mode "
			  "(leftPath/rightPath) returns OES_E_NOT_IMPLEMENTED. Returns "
			  "added[]/removed[]/modified[] with per-object change paths.",
			  schemaObj({
			    { "leftSnapshot",  snapIn },
			    { "rightSnapshot", snapIn },
			    { "leftPath",      str("(deferred) path to first config snapshot file") },
			    { "rightPath",     str("(deferred) path to second config snapshot file") },
			  }, {}),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false)
			},
			&ToolMetadataDiff
		});
	}
	{
		nlohmann::json nodeItem;
		nodeItem["type"]       = "object";
		nodeItem["properties"] = nlohmann::json::object();
		nodeItem["properties"]["fullName"] = nlohmann::json{ {"type","string"} };
		nodeItem["properties"]["kind"]     = nlohmann::json{ {"type","string"} };

		nlohmann::json edgeItem;
		edgeItem["type"]       = "object";
		edgeItem["properties"] = nlohmann::json::object();
		edgeItem["properties"]["from"]  = nlohmann::json{ {"type","string"} };
		edgeItem["properties"]["to"]    = nlohmann::json{ {"type","string"} };
		edgeItem["properties"]["label"] = nlohmann::json{
			{"type","string"},
			{"description","attribute_type / module_call / register_record / ..."}
		};

		nlohmann::json outDG;
		outDG["type"]       = "object";
		outDG["properties"] = nlohmann::json::object();
		outDG["properties"]["root"]      = nlohmann::json{ {"type","string"} };
		outDG["properties"]["direction"] = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({ "in", "out", "both" })}
		};
		outDG["properties"]["depth"]     = nlohmann::json{ {"type","integer"} };
		outDG["properties"]["nodes"]     = nlohmann::json{ {"type","array"}, {"items", nodeItem} };
		outDG["properties"]["edges"]     = nlohmann::json{ {"type","array"}, {"items", edgeItem} };
		outDG["required"]   = nlohmann::json::array({ "root", "nodes", "edges" });

		nlohmann::json dirIn;
		dirIn["type"]        = "string";
		dirIn["enum"]        = nlohmann::json::array({ "in", "out", "both" });
		dirIn["default"]     = "both";
		dirIn["description"] = "out: what this object references. in: what references this object. both: union.";

		nlohmann::json depthIn;
		depthIn["type"]        = "integer";
		depthIn["default"]     = 2;
		depthIn["description"] = "BFS depth (clamped 1..5)";

		table.push_back({
			{ "dependency_graph",
			  "BFS dependency graph rooted at a metadata object. direction='out' "
			  "follows outgoing references (attribute types + module calls); "
			  "direction='in' reuses find_references logic to walk incoming "
			  "edges; 'both' merges. depth is clamped to 1..5 to bound the "
			  "expansion. Returns structuredContent.nodes[]/edges[] for graph "
			  "visualisation or impact analysis.",
			  schemaObj({
			    { "fullName",  str("Root object full name") },
			    { "direction", dirIn  },
			    { "depth",     depthIn },
			  }, { "fullName" }),
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outDG)
			},
			&ToolDependencyGraph
		});
	}
	table.push_back({
		{ "extract_module_to_common",
		  "Extract a procedure from a source module into a CommonModule. "
		  "STATUS: PARTIAL v1 — dryRun=true (default) returns the extracted "
		  "text + planned mutations + call-site count. dryRun=false returns "
		  "OES_E_NOT_IMPLEMENTED until the lexer-aware extractor lands "
		  "(text-based extraction is brittle when locals or closures are "
		  "involved). VES Procedure/Function syntax only; CES brace matching "
		  "is a follow-up.",
		  schemaObj({
		    { "sourceFullName",     str("Source module path, e.g. 'Document.Заказ.ObjectModule'") },
		    { "procedureName",      str("Procedure/function name to extract") },
		    { "targetCommonModule", str("Target CommonModule full path, e.g. 'CommonModules.Расчёты'") },
		    { "dryRun",             boolP("If true (default), preview only") },
		  }, { "sourceFullName", "procedureName", "targetCommonModule" }),
		  // destructive=true: would mutate two modules when apply path lands.
		  // Idempotent=false: re-applying produces duplicate definitions.
		  ann(/*readOnly*/false, /*destructive*/true,
		      /*idempotent*/false, /*openWorld*/false)
		},
		&ToolExtractModuleToCommon
	});

	table.push_back({
		{ "config_info",
		  "Return readiness state, loaded configuration path, top-level "
		  "object count, and root name.",
		  schemaObj({}, {}),
		  ann(true, false, true, false)
		},
		&ToolConfigInfo
	});

	// =====================================================================
	// snapshots_list — read-only audit trail
	// =====================================================================
	{
		nlohmann::json snapItem;
		snapItem["type"]       = "object";
		snapItem["properties"] = nlohmann::json::object();
		snapItem["properties"]["id"]          = nlohmann::json{ {"type","string"} };
		snapItem["properties"]["timestamp"]   = nlohmann::json{ {"type","string"}, {"description","ISO-8601 UTC"} };
		snapItem["properties"]["tool"]        = nlohmann::json{ {"type","string"} };
		snapItem["properties"]["fullName"]    = nlohmann::json{ {"type","string"} };
		snapItem["properties"]["operation"]   = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({ "create", "edit", "delete", "write_module", "unknown" })}
		};
		snapItem["properties"]["description"] = nlohmann::json{ {"type","string"} };
		snapItem["properties"]["sizeBytes"]   = nlohmann::json{ {"type","integer"} };
		snapItem["properties"]["consumed"]    = nlohmann::json{ {"type","boolean"} };
		snapItem["required"]   = nlohmann::json::array({ "id", "operation" });

		nlohmann::json outSL;
		outSL["type"]       = "object";
		outSL["properties"] = nlohmann::json::object();
		outSL["properties"]["snapshots"] = nlohmann::json{ {"type","array"}, {"items", snapItem} };
		outSL["properties"]["total"]     = nlohmann::json{ {"type","integer"} };
		outSL["required"]   = nlohmann::json::array({ "snapshots", "total" });

		nlohmann::json limitProp;
		limitProp["type"]        = "integer";
		limitProp["default"]     = 50;
		limitProp["description"] = "Max rows to return (newest-first).";

		nlohmann::json sinceProp;
		sinceProp["type"]        = "string";
		sinceProp["description"] = "Optional ISO-8601 cut-off; only snapshots at-or-after this instant are returned.";

		table.push_back({
			{ "snapshots_list",
			  "List per-mutation auto-snapshots written to "
			  "<config>/sys/oes-snapshots/. Snapshots are captured BEFORE "
			  "every meta_create / meta_edit / meta_delete / write_module "
			  "call so a one-keystroke rollback is available. Newest-first.",
			  schemaObj({
			    { "limit", limitProp },
			    { "since", sinceProp },
			  }, {}),
			  // Read-only: just reads the audit trail. Idempotent: same args
			  // produce the same row set (between captures).
			  ann(/*readOnly*/true, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outSL)
			},
			&ToolSnapshotsList
		});
	}

	// =====================================================================
	// snapshot_rollback — dry-run plan today; apply path deferred
	// =====================================================================
	{
		nlohmann::json planItem;
		planItem["type"]       = "object";
		planItem["properties"] = nlohmann::json::object();
		planItem["properties"]["op"]         = nlohmann::json{
			{"type","string"},
			{"enum", nlohmann::json::array({ "create", "edit", "delete", "write_module" })}
		};
		planItem["properties"]["fullName"]   = nlohmann::json{ {"type","string"} };
		planItem["properties"]["kind"]       = nlohmann::json{ {"type","string"} };
		planItem["properties"]["properties"] = nlohmann::json{ {"type","object"} };
		planItem["properties"]["source"]     = nlohmann::json{ {"type","string"} };
		planItem["properties"]["rationale"]  = nlohmann::json{ {"type","string"} };
		planItem["properties"]["force"]      = nlohmann::json{ {"type","boolean"} };
		planItem["required"]   = nlohmann::json::array({ "op", "fullName" });

		nlohmann::json outSR;
		outSR["type"]       = "object";
		outSR["properties"] = nlohmann::json::object();
		outSR["properties"]["id"]              = nlohmann::json{ {"type","string"} };
		outSR["properties"]["dryRun"]          = nlohmann::json{ {"type","boolean"} };
		outSR["properties"]["operation"]       = nlohmann::json{ {"type","string"} };
		outSR["properties"]["fullName"]        = nlohmann::json{ {"type","string"} };
		outSR["properties"]["plan"]            = nlohmann::json{ {"type","array"}, {"items", planItem} };
		outSR["properties"]["ok"]              = nlohmann::json{ {"type","boolean"} };
		outSR["properties"]["restoredObjects"] = nlohmann::json{ {"type","integer"} };
		outSR["properties"]["errorCode"]       = nlohmann::json{ {"type","string"} };
		outSR["properties"]["message"]         = nlohmann::json{ {"type","string"} };
		outSR["required"]   = nlohmann::json::array({ "id", "dryRun", "plan" });

		nlohmann::json snapIdProp;
		snapIdProp["type"]        = "string";
		snapIdProp["description"] = "Snapshot id from snapshots_list (e.g. '20260521T103045-0001')";

		nlohmann::json dryRunProp;
		dryRunProp["type"]        = "boolean";
		dryRunProp["default"]     = true;
		dryRunProp["description"] = "If true (default), return the reverse-mutation plan without applying. "
		                              "If false, returns OES_E_NOT_IMPLEMENTED in v1.";

		table.push_back({
			{ "snapshot_rollback",
			  "Rollback a previously-captured snapshot. dryRun=true (default) "
			  "returns the reverse-mutation plan: a delete for a recorded "
			  "create, a recreate from priorState for a recorded delete, "
			  "an edit restoring priorState for a recorded edit, and a "
			  "write_module restoring the prior source. dryRun=false is "
			  "deferred to v2 (apply path needs metaBridge undo-of-undo "
			  "testing). On success the snapshot is marked consumed so "
			  "subsequent calls return OES_E_SNAPSHOT_ALREADY_CONSUMED.",
			  schemaObj({
			    { "snapshotId", snapIdProp },
			    { "dryRun",     dryRunProp },
			  }, { "snapshotId" }),
			  // destructiveHint mirrors what apply mode WILL do; agents
			  // gate on this hint to surface a confirmation prompt even
			  // for dry-run, which is consistent with the spec's "treat
			  // the hint as a property of the tool, not the call".
			  ann(/*readOnly*/false, /*destructive*/true,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outSR)
			},
			&ToolSnapshotRollback
		});
	}

	// =====================================================================
	// transaction_begin
	// =====================================================================
	{
		nlohmann::json labelProp;
		labelProp["type"]        = "string";
		labelProp["description"] = "Short human-readable description of the "
		                           "transaction for the audit log (optional).";

		nlohmann::json outTB;
		outTB["type"]       = "object";
		outTB["properties"] = nlohmann::json::object();
		outTB["properties"]["ok"]             = nlohmann::json{ {"type","boolean"} };
		outTB["properties"]["transactionId"]  = nlohmann::json{ {"type","string"},
		    {"description","Opaque UUID to pass to transaction_commit or transaction_rollback"} };
		outTB["properties"]["undoStackBefore"] = nlohmann::json{ {"type","integer"},
		    {"description","metaBridge undo-stack depth at the time of begin"} };
		outTB["required"] = nlohmann::json::array({ "ok", "transactionId", "undoStackBefore" });

		table.push_back({
			{ "transaction_begin",
			  "Open a new transaction staging context. All meta_create / "
			  "meta_edit / meta_delete / write_module calls made after this "
			  "will be grouped as a single logical unit. Call "
			  "transaction_commit to lock them in, or transaction_rollback "
			  "to revert every mutation made since begin. Only one active "
			  "transaction is allowed at a time — returns OES_E_TX_ALREADY_ACTIVE "
			  "if another is open.",
			  schemaObj({
			    { "label", labelProp },
			  }, {}),
			  ann(/*readOnly*/false, /*destructive*/false,
			      /*idempotent*/false, /*openWorld*/false),
			  std::move(outTB)
			},
			&ToolTransactionBegin
		});
	}

	// =====================================================================
	// transaction_commit
	// =====================================================================
	{
		nlohmann::json txIdProp;
		txIdProp["type"]        = "string";
		txIdProp["description"] = "Transaction id returned by transaction_begin.";

		nlohmann::json outTC;
		outTC["type"]       = "object";
		outTC["properties"] = nlohmann::json::object();
		outTC["properties"]["ok"]             = nlohmann::json{ {"type","boolean"} };
		outTC["properties"]["mutationsApplied"] = nlohmann::json{ {"type","integer"},
		    {"description","Number of mutations made inside this transaction"} };
		outTC["properties"]["undoStackAfter"]   = nlohmann::json{ {"type","integer"},
		    {"description","metaBridge undo-stack depth after commit"} };
		outTC["required"] = nlohmann::json::array({ "ok", "mutationsApplied", "undoStackAfter" });

		table.push_back({
			{ "transaction_commit",
			  "Commit the active transaction, locking in all mutations made "
			  "since transaction_begin. The transaction is removed from the "
			  "active map. Mutations are individually revertable via Ctrl+Z "
			  "in Designer (compound-undo grouping deferred to v2). Returns "
			  "OES_E_TX_NOT_FOUND when the transactionId is unknown or was "
			  "already committed/rolled back (idempotent-safe).",
			  schemaObj({
			    { "transactionId", txIdProp },
			  }, { "transactionId" }),
			  ann(/*readOnly*/false, /*destructive*/false,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outTC)
			},
			&ToolTransactionCommit
		});
	}

	// =====================================================================
	// transaction_rollback
	// =====================================================================
	{
		nlohmann::json txIdProp;
		txIdProp["type"]        = "string";
		txIdProp["description"] = "Transaction id returned by transaction_begin.";

		nlohmann::json outTR;
		outTR["type"]       = "object";
		outTR["properties"] = nlohmann::json::object();
		outTR["properties"]["ok"]               = nlohmann::json{ {"type","boolean"} };
		outTR["properties"]["mutationsReverted"] = nlohmann::json{ {"type","integer"},
		    {"description","Number of undo entries popped from the metaBridge stack"} };
		outTR["required"] = nlohmann::json::array({ "ok", "mutationsReverted" });

		table.push_back({
			{ "transaction_rollback",
			  "Rollback the active transaction by reverting all mutations "
			  "pushed to the metaBridge undo stack since transaction_begin. "
			  "Each mutation is undone in LIFO order until the stack depth "
			  "returns to the value captured at begin. Returns "
			  "OES_E_TX_NOT_FOUND when the transactionId is unknown or was "
			  "already committed/rolled back (idempotent-safe). A config "
			  "reload during the transaction will cause the undo stack to be "
			  "cleared by metaBridge; rollback will report 0 mutationsReverted "
			  "and succeed (the config reload already discarded the mutations).",
			  schemaObj({
			    { "transactionId", txIdProp },
			  }, { "transactionId" }),
			  ann(/*readOnly*/false, /*destructive*/true,
			      /*idempotent*/true, /*openWorld*/false),
			  std::move(outTR)
			},
			&ToolTransactionRollback
		});
	}

	// mcp-1c compatibility aliases (inspired by feenlace/mcp-1c MIT,
	// 2026-05-21 t2-020). Agents trained on mcp-1c tool names work
	// against oes-mcp without rewriting prompts. Aliases share the
	// same handler, schemas, and annotations as their canonical tool;
	// only `name` differs and the description carries an "[alias]" tag.
	{
		const std::pair<const char*, const char*> aliasMap[] = {
			{ "meta_query",   "get_object_structure"   },
			{ "config_info",  "get_configuration_info" },
			{ "search_text",  "search_code"            },
			{ "list_objects", "get_metadata_tree"      },
		};
		const size_t baseCount = table.size();
		for (const auto& pair : aliasMap) {
			for (size_t i = 0; i < baseCount; ++i) {
				if (table[i].desc.name != pair.first) continue;
				ToolEntry alias = table[i];
				alias.desc.name = pair.second;
				alias.desc.description =
				    std::string("[alias of ") + pair.first + "] " +
				    table[i].desc.description;
				table.push_back(std::move(alias));
				break;
			}
		}
	}

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
