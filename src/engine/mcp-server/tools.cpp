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
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileContext.h"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/string.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
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

// =========================================================================
// Tool: meta_query
// =========================================================================
nlohmann::json ToolMetaQuery(const nlohmann::json& args)
{
	if (auto fail = RequireConfig(); fail) return *fail;
	const std::string fullName = ArgString(args, "fullName");
	if (fullName.empty()) {
		return TextResult("meta_query: 'fullName' is required (e.g. 'Catalog.Contractors')", true);
	}
	char* jsonOut = nullptr;
	char* errMsg  = nullptr;
	const int rc = metaBridge::HostMetaQuery(fullName.c_str(), nullptr, &jsonOut, &errMsg);
	if (rc != 0) {
		std::string msg = "meta_query failed";
		if (errMsg != nullptr) { msg += ": "; msg += errMsg; }
		FreeIfSet(errMsg); FreeIfSet(jsonOut);
		return TextResult(msg, true);
	}
	std::string payload = (jsonOut != nullptr) ? std::string(jsonOut) : std::string("{}");
	FreeIfSet(jsonOut); FreeIfSet(errMsg);
	// MCP: pretty-print the JSON so Claude Code's text-content renderer
	// surfaces it readably. The raw payload is single-line dump.
	auto parsed = nlohmann::json::parse(payload, nullptr, false);
	if (parsed.is_discarded()) return TextResult(payload, false);
	return JsonResult(parsed);
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
	const unsigned long long clsidFilter = kindFilter.empty()
		? 0ull
		: metaBridge::KindStringToCLSID(kindFilter.c_str());
	if (!kindFilter.empty() && clsidFilter == 0ull) {
		return TextResult("list_objects: unknown kind '" + kindFilter + "'", true);
	}

	ibMetaDataConfigurationBase* mc = activeMetaData;
	if (mc == nullptr) return TextResult("list_objects: no configuration", true);
	ibValueMetaObject* root = mc->GetCommonMetaObject();
	if (root == nullptr) return TextResult("list_objects: configuration has no root", true);

	nlohmann::json objects = nlohmann::json::array();
	const std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
	for (const ibValueMetaObject* obj : all) {
		if (obj == nullptr) continue;
		const ibClassID cls = obj->GetClassType();
		if (clsidFilter != 0ull && static_cast<unsigned long long>(cls) != clsidFilter) continue;
		nlohmann::json item;
		item["name"]    = std::string(obj->GetName().utf8_str());
		item["synonym"] = std::string(obj->GetSynonym().utf8_str());
		// Reuse metaBridge's display-case kind label so the JSON output
		// is consistent across tools (Catalog, not catalog).
		const wxString needle = obj->GetName();
		// MCP: brute-force CLSID→string by trying every known kind — we
		// don't have a public CLSIDToKindString export. There are ~11
		// top-level kinds so this stays O(1) per row.
		const char* candidates[] = {
			"Catalog", "Document", "Enumeration", "Constant",
			"InformationRegister", "AccumulationRegister",
			"DataProcessor", "Report",
			"ChartOfCharacteristicTypes", "ChartOfAccounts",
			"AccountingRegister",
			nullptr
		};
		for (const char* const* p = candidates; *p; ++p) {
			if (metaBridge::KindStringToCLSID(*p) == static_cast<unsigned long long>(cls)) {
				item["kind"] = std::string(*p);
				break;
			}
		}
		// Best-effort fullName — only emitted when we recognise the kind.
		if (item.contains("kind")) {
			item["fullName"] = item["kind"].get<std::string>() + "." +
			                    item["name"].get<std::string>();
		}
		objects.push_back(item);
	}
	nlohmann::json out;
	out["count"]   = objects.size();
	out["objects"] = std::move(objects);
	return JsonResult(out);
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
		return TextResult("read_module: 'fullName' is required (e.g. 'Catalog.X.ObjectModule')", true);
	}
	ibValueMetaObject* node = ResolveByPath(fullName);
	if (node == nullptr) {
		return TextResult("read_module: path not found '" + fullName + "'", true);
	}
	auto* mod = dynamic_cast<ibValueMetaObjectModuleBase*>(node);
	if (mod == nullptr) {
		return TextResult("read_module: '" + fullName +
		                    "' is not a module object", true);
	}
	nlohmann::json out;
	out["fullName"] = fullName;
	out["source"]   = std::string(mod->GetModuleText().utf8_str());
	return JsonResult(out);
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
// Tool: sigma_check
// =========================================================================
nlohmann::json ToolSigmaCheck(const nlohmann::json& args)
{
	// MCP: deferred — the Σ-invariant builtin is not registered on the
	// system manager in this branch. The hook lives here so a future
	// commit can wire ibValueSigmaCheck without breaking clients that
	// already enumerate the tool table.
	(void)args;
	return TextResult("sigma_check: deferred — Σ-invariant checks are not "
	                    "yet exposed through the MCP surface in v1", true);
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

	table.push_back({
		{ "meta_query",
		  "Read an OES metadata object's structure as JSON. fullName is "
		  "'<Kind>.<Name>' (e.g. 'Catalog.Контрагенты').",
		  schemaObj({
		    { "fullName", str("Object full name, e.g. 'Catalog.Контрагенты'") },
		  }, { "fullName" }),
		  ann(true, false, true, false)
		},
		&ToolMetaQuery
	});
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
	table.push_back({
		{ "list_objects",
		  "Enumerate top-level metadata objects. Filter by kind to scope the "
		  "result (Catalog / Document / ...). Empty kind returns all.",
		  schemaObj({
		    { "kind", str("Optional kind filter (Catalog, Document, ...). Empty = all.") },
		  }, {}),
		  ann(true, false, true, false)
		},
		&ToolListObjects
	});
	table.push_back({
		{ "read_module",
		  "Read the CES/VES source of an object/manager/common/form module. "
		  "fullName is the dotted path, e.g. 'Catalog.X.ObjectModule' or "
		  "'CommonModules.Y'.",
		  schemaObj({
		    { "fullName", str("Dotted module path") },
		  }, { "fullName" }),
		  ann(true, false, true, false)
		},
		&ToolReadModule
	});
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
	table.push_back({
		{ "sigma_check",
		  "Run the Σ-invariant checks. Deferred in v1 — returns isError:true.",
		  schemaObj({}, {}),
		  ann(true, false, true, false)
		},
		&ToolSigmaCheck
	});
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
