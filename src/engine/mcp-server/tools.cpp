/////////////////////////////////////////////////////////////////////////////
// tools — see header. Each tool wraps an existing host API so the LLM
// gets the same semantics Designer's UI does, without us re-implementing
// the validation / undo / policy plumbing.
/////////////////////////////////////////////////////////////////////////////

#include "tools.h"
#include "headless_app.h"

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

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/cpp-httplib/httplib.h"

#include <wx/string.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
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
	return TextResult("meta_create OK: " + fullName, false);
}

// =========================================================================
// Tool: meta_edit
// =========================================================================
nlohmann::json ToolMetaEdit(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
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
	return TextResult("meta_edit OK: " + fullName, false);
}

// =========================================================================
// Tool: meta_delete
// =========================================================================
nlohmann::json ToolMetaDelete(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
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
std::pair<std::string, std::string> SigmaSplitUrl(const std::string& url)
{
	const auto schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return {std::string(), std::string()};
	const auto pathStart = url.find('/', schemeEnd + 3);
	if (pathStart == std::string::npos) return {url, "/"};
	return {url.substr(0, pathStart), url.substr(pathStart)};
}

nlohmann::json ToolSigmaCheck(const nlohmann::json& args)
{
	// Input shape: { metadata: object, moduleCode?: string, rules?: string[] }
	if (!args.is_object() || !args.contains("metadata")) {
		return TextResult("sigma_check: 'metadata' is required", true);
	}

	const PugiConfig cfg = LoadPugiConfig();
	if (cfg.endpoint.empty() || cfg.token.empty() || cfg.tenant.empty()) {
		// Treat missing creds as offline so the agent loop survives
		// when oes-mcp is launched outside the Designer environment.
		return SigmaOfflineEnvelope(
		    "no Pugi credentials configured (need OES_PUGI_ENDPOINT/TOKEN/TENANT "
		    "or aiBridge.env with ENDPOINT/TOKEN/TENANT)");
	}

	// Build the request body — matches the proven Pugi MCP invocation
	// shape aiBridge uses for its llm_query / triple_review calls:
	//   { "name": "<tool>", "input": { ...args... } }
	// The Pugi gateway rejects {tool, arguments} with a 400, so we mirror
	// what its OpenAPI schema actually accepts.
	nlohmann::json input = nlohmann::json::object();
	input["metadata"] = args["metadata"];
	if (args.contains("moduleCode") && args["moduleCode"].is_string()) {
		input["moduleCode"] = args["moduleCode"];
	}
	if (args.contains("rules") && args["rules"].is_array()) {
		input["rules"] = args["rules"];
	}

	nlohmann::json body;
	body["name"]  = "sigma_check";
	body["input"] = std::move(input);

	// Scheme + HTTPS precondition. If we were built without OpenSSL the
	// https://mcp.pugi.io endpoint can't even connect — fail open rather
	// than spelunking through cpp-httplib's TLS errors.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
	if (cfg.endpoint.rfind("https://", 0) == 0) {
		return SigmaOfflineEnvelope(
		    "oes-mcp built without OpenSSL; cannot reach https:// Pugi endpoint");
	}
#endif

	const auto [base, path] = SigmaSplitUrl(cfg.endpoint);
	if (base.empty()) {
		return TextResult("sigma_check: malformed ENDPOINT URL: " + cfg.endpoint, true);
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
		// Network / connect / timeout — fail open.
		return SigmaOfflineEnvelope(
		    std::string("transport error: ") + httplib::to_string(res.error()));
	}
	if (res->status >= 300 && res->status < 400) {
		// Redirect with Authorization attached would leak the bearer.
		return TextResult(
		    "sigma_check: Pugi returned redirect; bearer not forwarded for security",
		    true);
	}
	if (res->status >= 400) {
		// Pugi returned an error envelope. Surface a clean message and
		// truncate the raw body so we don't echo back potentially
		// sensitive request slices.
		std::string snippet = res->body.size() > 500
		    ? res->body.substr(0, 500) + "..."
		    : res->body;
		return TextResult(
		    "sigma_check: Pugi returned " + std::to_string(res->status) +
		    ": " + snippet, true);
	}

	// 2xx — parse JSON. On parse failure surface the (truncated) body.
	auto parsed = nlohmann::json::parse(res->body, nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		std::string snippet = res->body.size() > 500
		    ? res->body.substr(0, 500) + "..."
		    : res->body;
		return TextResult(
		    "sigma_check: Pugi returned non-JSON response: " + snippet, true);
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
	const std::string path = ArgString(args, "path");
	std::string err;
	if (!SaveConfiguration(path, err)) {
		return TextResult("save_config failed: " + err, true);
	}
	nlohmann::json out;
	out["saved"] = true;
	out["path"]  = path.empty() ? (LoadedConfigPath() + "/config.OES-DB") : path;
	return JsonResult(out);
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
