/////////////////////////////////////////////////////////////////////////////
// resources — see header. Five resources land in v1:
//
//   oes://config/current          concrete  application/json
//   oes://sigma-rules             concrete  application/json
//   oes://catalog/{name}          template  application/json
//   oes://module/{owner}/{kind}   template  text/x-oes-script
//   oes://docs/syntax/{ves|ces}   template  text/markdown
//
// Concrete resources advertise themselves verbatim in resources/list.
// Template resources advertise their RFC-6570 string in
// resources/templates/list. `resources/read` accepts either form — for
// a templated URI the caller substitutes the placeholders themselves
// (e.g. `oes://catalog/Контрагенты`).
//
// Subscribe/unsubscribe are in-memory only — v2 will persist across
// restarts. v1 fires-and-forgets on the notify side (no ack tracking).
//
// MCP: tools.cpp calls EmitResourceUpdated after every save_config and
// meta.* mutation. This file owns the dispatch through the NotifySink
// installed by main.cpp; tools.cpp never touches stdio directly.
/////////////////////////////////////////////////////////////////////////////

#include "resources.h"
#include "headless_app.h"
#include "tools.h"

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/plugin/metaBridge.h"

#include <wx/string.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace mcpServer {
namespace {

// =========================================================================
// Subscription set + notify sink (process-wide)
// =========================================================================

std::mutex            g_subMu;
std::set<std::string> g_subscriptions;
NotifySink            g_notifySink;

// MCP: split "oes://catalog/Foo" into ("oes", "catalog", "Foo") on '/'
// (after the "://" header). Returns true when the URI parses; the head
// vector holds the schemeless segments. Empty trailing slash yields an
// empty segment which is OK — the matcher rejects mismatched arity.
bool SplitUri(const std::string& uri,
              std::string& scheme,
              std::vector<std::string>& segments)
{
	const auto sep = uri.find("://");
	if (sep == std::string::npos) return false;
	scheme = uri.substr(0, sep);
	const std::string rest = uri.substr(sep + 3);
	segments.clear();
	std::string cur;
	for (char c : rest) {
		if (c == '/') {
			segments.push_back(cur);
			cur.clear();
		} else {
			cur.push_back(c);
		}
	}
	segments.push_back(cur);
	return true;
}

// MCP: match a concrete URI against a template URI. Placeholders are
// `{name}` segments — they capture the entire segment of the concrete
// URI and land in `outParams` keyed by the placeholder name. Literal
// segments must match byte-for-byte.
//
// Returns true on full match. Returns false on scheme mismatch, arity
// mismatch, or literal mismatch.
bool MatchTemplate(const std::string& templateUri,
                   const std::string& concreteUri,
                   std::map<std::string, std::string>& outParams)
{
	std::string tplScheme, conScheme;
	std::vector<std::string> tplSegs, conSegs;
	if (!SplitUri(templateUri, tplScheme, tplSegs)) return false;
	if (!SplitUri(concreteUri, conScheme, conSegs)) return false;
	if (tplScheme != conScheme) return false;
	if (tplSegs.size() != conSegs.size()) return false;
	for (std::size_t i = 0; i < tplSegs.size(); ++i) {
		const std::string& t = tplSegs[i];
		const std::string& c = conSegs[i];
		if (t.size() >= 2 && t.front() == '{' && t.back() == '}') {
			outParams[t.substr(1, t.size() - 2)] = c;
		} else if (t != c) {
			return false;
		}
	}
	return true;
}

// =========================================================================
// Reader implementations (one per resource)
// =========================================================================

// MCP: oes://config/current — manifest of the loaded configuration. Mirrors
// config_info's shape but adds a per-kind histogram so the agent can see
// "how many Catalogs / Documents / ..." without enumerating the tree.
std::string ReadConfigCurrent(const std::map<std::string, std::string>&)
{
	nlohmann::json out;
	out["ready"]   = IsReady();
	out["loading"] = IsLoading();
	out["path"]    = LoadedConfigPath();

	if (!IsReady() || activeMetaData == nullptr) {
		out["name"]        = "";
		out["objectCount"] = 0;
		out["kinds"]       = nlohmann::json::object();
		return out.dump(2);
	}

	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	std::size_t total = 0;
	nlohmann::json kinds = nlohmann::json::object();
	std::string rootName;
	if (root != nullptr) {
		rootName = std::string(root->GetName().utf8_str());
		const std::vector<ibValueMetaObject*> all = root->GetAnyArrayObject<>();
		total = all.size();

		// Histogram by kind label. We probe metaBridge for each known
		// top-level kind so the bucket names match what the rest of the
		// MCP surface advertises (Catalog / Document / ...).
		static const char* kTopLevel[] = {
			"Catalog", "Document", "Enumeration", "Constant",
			"InformationRegister", "AccumulationRegister",
			"DataProcessor", "Report",
			"ChartOfCharacteristicTypes", "ChartOfAccounts",
			"AccountingRegister",
			nullptr
		};
		for (const char* const* p = kTopLevel; *p; ++p) {
			const unsigned long long cls = metaBridge::KindStringToCLSID(*p);
			if (cls == 0ull) continue;
			std::size_t n = 0;
			for (const ibValueMetaObject* obj : all) {
				if (obj == nullptr) continue;
				if (static_cast<unsigned long long>(obj->GetClassType()) == cls) ++n;
			}
			if (n > 0) kinds[*p] = n;
		}
	}

	out["name"]         = rootName;
	out["objectCount"]  = total;
	out["kinds"]        = std::move(kinds);
	// lastModified — best-effort. We don't track an mtime explicitly;
	// surface the load-time path so consumers can stat it themselves.
	out["lastModified"] = "";
	return out.dump(2);
}

// MCP: oes://sigma-rules — hardcoded for v1. The real source is the Pugi
// MCP server (`sigma_check` already proxies there) but Pugi doesn't expose
// a rule-catalogue endpoint yet (mcp-003 will). This stub keeps the
// resources/list surface stable so agents can discover the rule set
// without making a probe call.
std::string ReadSigmaRulesImpl(const std::map<std::string, std::string>&)
{
	nlohmann::json rules = nlohmann::json::array();

	auto addRule = [&](const char* name, const char* description,
	                   const char* scope, const char* severity,
	                   const char* example) {
		nlohmann::json r;
		r["name"]        = name;
		r["description"] = description;
		r["scope"]       = scope;
		r["severity"]    = severity;
		r["example"]     = example;
		rules.push_back(std::move(r));
	};

	// Σ-unique — unique-key invariant on Catalog code/description columns.
	addRule(
		"Σ-unique",
		"Every Catalog row must have a unique Code (and Description when "
		"marked as the primary uniqueness field). Violations surface when "
		"two records share the same key.",
		"Catalog, ChartOfCharacteristicTypes",
		"high",
		"Two Контрагенты rows share Code='000001'"
	);

	// Σ-balance — accounting register total credit/debit must zero out.
	addRule(
		"Σ-balance",
		"For an AccountingRegister, every posted Document must produce "
		"records where Σ(debit) == Σ(credit). Violations indicate an "
		"unbalanced posting routine.",
		"AccountingRegister postings",
		"critical",
		"Sale Document posts 1000 to Cash debit but only 800 to Revenue credit"
	);

	// Σ-nonneg — accumulation register remainders cannot dip below zero.
	addRule(
		"Σ-nonneg",
		"AccumulationRegister of Balance type with NonNegativeBalance "
		"flag set must never let a resource go negative at any moment in "
		"history. Violations indicate over-issue or missing receipts.",
		"AccumulationRegister (Balance, NonNegativeBalance=true)",
		"high",
		"Goods issued from a warehouse before they were received"
	);

	// Σ-files — referenced binary blobs / templates must exist on disk.
	addRule(
		"Σ-files",
		"Forms, templates, and ChartOfAccounts code resources that "
		"reference external files (icons, report templates, attached "
		"docs) must have those files reachable from the configuration "
		"root. Violations indicate broken deployment packaging.",
		"Form templates, Report layouts, attached resources",
		"medium",
		"Form references icon.png that does not exist in the bundle"
	);

	nlohmann::json out;
	out["version"] = "v1";
	out["source"]  = "oes-mcp built-in";
	out["note"]    = "v1 is hardcoded. Pugi MCP will own the canonical "
	                 "rule catalogue once mcp-003 lands.";
	out["rules"]   = std::move(rules);
	return out.dump(2);
}

// MCP: oes://catalog/{name} — internally dispatches to meta_query so the
// payload matches the structuredContent shape the agent already knows.
// We strip the MCP `content[]` wrapping and emit just the inner JSON.
std::string ReadCatalog(const std::map<std::string, std::string>& params)
{
	auto it = params.find("name");
	if (it == params.end() || it->second.empty()) {
		nlohmann::json err;
		err["code"]    = -32602;
		err["message"] = "oes://catalog/{name}: name placeholder is empty";
		throw err;
	}
	const std::string fullName = std::string("Catalog.") + it->second;

	nlohmann::json args;
	args["fullName"] = fullName;
	nlohmann::json env = DispatchTool("meta_query", args);

	// Surface either structuredContent (the canonical shape) or fall back
	// to the inner text payload when the tool errored.
	if (env.is_object() && env.contains("structuredContent")) {
		return env["structuredContent"].dump(2);
	}
	if (env.is_object() && env.contains("isError") && env["isError"].get<bool>()) {
		nlohmann::json err;
		err["code"]    = -32602;
		err["message"] = "oes://catalog/" + it->second + ": not found or "
		                  "configuration not loaded";
		throw err;
	}
	if (env.is_object() && env.contains("content") &&
	    env["content"].is_array() && !env["content"].empty()) {
		const auto& first = env["content"][0];
		if (first.is_object() && first.contains("text") && first["text"].is_string()) {
			return first["text"].get<std::string>();
		}
	}
	return env.dump(2);
}

// MCP: oes://module/{owner}/{kind} — dispatches to read_module with the
// reconstructed dotted path. {owner} is the dotted location string up to
// the module slot (URL-decoded by the host before it reaches us). {kind}
// is one of ObjectModule / ManagerModule / Module / CommonModule / ...
// but we accept whatever read_module's ResolveByPath understands.
//
// Two URI shapes flow through us:
//   oes://module/Catalog.X/ObjectModule
//   oes://module/Document.Y.Forms.ItemForm/Module
// In both cases the dotted path is `<owner>.<kind>`.
std::string ReadModule(const std::map<std::string, std::string>& params)
{
	auto owner = params.find("owner");
	auto kind  = params.find("kind");
	if (owner == params.end() || owner->second.empty() ||
	    kind == params.end()  || kind->second.empty()) {
		nlohmann::json err;
		err["code"]    = -32602;
		err["message"] = "oes://module/{owner}/{kind}: both placeholders required";
		throw err;
	}
	const std::string fullName = owner->second + "." + kind->second;

	nlohmann::json args;
	args["fullName"] = fullName;
	nlohmann::json env = DispatchTool("read_module", args);

	if (env.is_object() && env.contains("structuredContent") &&
	    env["structuredContent"].is_object() &&
	    env["structuredContent"].contains("source") &&
	    env["structuredContent"]["source"].is_string()) {
		return env["structuredContent"]["source"].get<std::string>();
	}
	if (env.is_object() && env.contains("isError") && env["isError"].get<bool>()) {
		nlohmann::json err;
		err["code"]    = -32602;
		err["message"] = "oes://module/" + owner->second + "/" + kind->second +
		                  ": module not found or configuration not loaded";
		throw err;
	}
	return env.dump(2);
}

// MCP: oes://docs/syntax/{lang} — slice section 5 ("Built-in Language
// Reference") out of `docs/oes-product-reference.md`. Both syntax modes
// share that section today; the {lang} placeholder selects an emphasis
// header so the rendered markdown leads with the requested flavour.
//
// Path resolution: OES_DOCS_PATH env var if set, otherwise walk up from
// the binary location (build/bin/Debug/oes-mcp → repo root → docs/...).
// Final fallback is `docs/oes-product-reference.md` in CWD which works
// when the smoke test runs from the repo root.
std::string LoadDocsFile()
{
	const char* env = std::getenv("OES_DOCS_PATH");
	std::vector<std::string> candidates;
	if (env != nullptr && *env != '\0') {
		candidates.emplace_back(env);
	}
	// Try common build-relative paths. These cover Debug/Release as well
	// as cmake's flat-bin layout. None of them are mandatory — the env
	// var is the canonical override.
	candidates.emplace_back("docs/oes-product-reference.md");
	candidates.emplace_back("../docs/oes-product-reference.md");
	candidates.emplace_back("../../docs/oes-product-reference.md");
	candidates.emplace_back("../../../docs/oes-product-reference.md");
	for (const auto& c : candidates) {
		std::ifstream f(c, std::ios::binary);
		if (!f) continue;
		std::ostringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
	return std::string();
}

// MCP: slice between two heading markers. Returns the substring starting
// at `startHeading` (inclusive) and stopping just before `endHeading`
// (exclusive). When the start marker is missing we return empty; when the
// end marker is missing we return the tail.
std::string SliceSection(const std::string& doc,
                         const std::string& startHeading,
                         const std::string& endHeading)
{
	const auto a = doc.find(startHeading);
	if (a == std::string::npos) return std::string();
	const auto b = doc.find(endHeading, a + startHeading.size());
	if (b == std::string::npos) return doc.substr(a);
	return doc.substr(a, b - a);
}

std::string ReadDocsSyntax(const std::map<std::string, std::string>& params)
{
	auto it = params.find("lang");
	if (it == params.end() || it->second.empty()) {
		nlohmann::json err;
		err["code"]    = -32602;
		err["message"] = "oes://docs/syntax/{lang}: lang must be 'ves' or 'ces'";
		throw err;
	}
	std::string lang = it->second;
	std::transform(lang.begin(), lang.end(), lang.begin(),
		[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
	if (lang != "ves" && lang != "ces") {
		nlohmann::json err;
		err["code"]    = -32602;
		err["message"] = "oes://docs/syntax/{lang}: lang must be 'ves' or 'ces'";
		throw err;
	}

	const std::string doc = LoadDocsFile();
	if (doc.empty()) {
		nlohmann::json err;
		err["code"]    = -32603;
		err["message"] = "oes://docs/syntax: docs/oes-product-reference.md not "
		                  "found. Set OES_DOCS_PATH or run from the repo root.";
		throw err;
	}

	// Section 5 covers both languages; slice it once and prepend a
	// language-specific lead-in so the agent's first paragraph names the
	// flavour the caller asked for.
	const std::string section = SliceSection(doc,
		"## 5. Built-in Language Reference",
		"## 6. Database Driver Capabilities Matrix");
	if (section.empty()) {
		nlohmann::json err;
		err["code"]    = -32603;
		err["message"] = "oes://docs/syntax: section 5 not found in reference doc";
		throw err;
	}

	std::string lead;
	if (lang == "ces") {
		lead = "# OES Syntax Reference (CES — default for new configs)\n\n"
		       "CES is the C-flavoured syntax mode. `if (cond) { ... }`, "
		       "braces and semicolons. Default for new OES configurations.\n\n";
	} else {
		lead = "# OES Syntax Reference (VES — 1С/BSL-style)\n\n"
		       "VES is the Visual-Basic / 1С-flavoured syntax mode. "
		       "`If cond Then ... EndIf`, English keywords, no braces. "
		       "Existing migrated configs preserve this mode.\n\n";
	}
	return lead + section;
}

} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================

void ResourceRegistry::Register(ResourceDescriptor desc)
{
	for (auto& e : m_entries) {
		if (e.uri == desc.uri && e.isTemplate == desc.isTemplate) {
			e = std::move(desc);
			return;
		}
	}
	m_entries.push_back(std::move(desc));
}

std::vector<ResourceDescriptor> ResourceRegistry::ListConcrete() const
{
	std::vector<ResourceDescriptor> out;
	for (const auto& e : m_entries) {
		if (!e.isTemplate) out.push_back(e);
	}
	return out;
}

std::vector<ResourceDescriptor> ResourceRegistry::ListTemplates() const
{
	std::vector<ResourceDescriptor> out;
	for (const auto& e : m_entries) {
		if (e.isTemplate) out.push_back(e);
	}
	return out;
}

const ResourceDescriptor* ResourceRegistry::FindMatching(const std::string& uri) const
{
	// Exact match wins.
	for (const auto& e : m_entries) {
		if (!e.isTemplate && e.uri == uri) return &e;
	}
	// Template walk.
	std::map<std::string, std::string> params;
	for (const auto& e : m_entries) {
		if (!e.isTemplate) continue;
		params.clear();
		if (MatchTemplate(e.uri, uri, params)) return &e;
	}
	return nullptr;
}

nlohmann::json ResourceRegistry::Read(const std::string& uri) const
{
	// Exact match first — concrete resources skip the template walker so
	// `oes://config/current` doesn't accidentally land on `oes://...{...}`.
	for (const auto& e : m_entries) {
		if (e.isTemplate || e.uri != uri) continue;
		std::string body = e.read({});
		nlohmann::json item;
		item["uri"]      = uri;
		item["mimeType"] = e.mimeType;
		item["text"]     = std::move(body);
		nlohmann::json out;
		out["contents"]  = nlohmann::json::array({ std::move(item) });
		return out;
	}

	// Template walk.
	std::map<std::string, std::string> params;
	for (const auto& e : m_entries) {
		if (!e.isTemplate) continue;
		params.clear();
		if (!MatchTemplate(e.uri, uri, params)) continue;
		std::string body = e.read(params);
		nlohmann::json item;
		item["uri"]      = uri;
		item["mimeType"] = e.mimeType;
		item["text"]     = std::move(body);
		nlohmann::json out;
		out["contents"]  = nlohmann::json::array({ std::move(item) });
		return out;
	}

	nlohmann::json err;
	err["code"]    = -32602;  // InvalidParams
	err["message"] = "resources/read: unknown URI '" + uri + "'";
	throw err;
}

ResourceRegistry& AllResources()
{
	static ResourceRegistry reg;
	static std::once_flag init;
	std::call_once(init, [] {
		// 1) oes://config/current — manifest of the loaded configuration.
		{
			ResourceDescriptor d;
			d.uri         = "oes://config/current";
			d.name        = "Current configuration manifest";
			d.description = "Loaded configuration: name, path, object count, "
			                 "and per-kind histogram (Catalog/Document/etc).";
			d.mimeType    = "application/json";
			d.isTemplate  = false;
			d.read        = &ReadConfigCurrent;
			reg.Register(std::move(d));
		}
		// 2) oes://sigma-rules — canonical invariant catalogue (stubbed v1).
		{
			ResourceDescriptor d;
			d.uri         = "oes://sigma-rules";
			d.name        = "Σ-rules invariant catalogue";
			d.description = "Schema-level invariants the configuration should "
			                 "uphold: Σ-unique, Σ-balance, Σ-nonneg, Σ-files. "
			                 "v1 is hardcoded; Pugi MCP will own the canonical "
			                 "source once mcp-003 lands.";
			d.mimeType    = "application/json";
			d.isTemplate  = false;
			d.read        = &ReadSigmaRulesImpl;
			reg.Register(std::move(d));
		}
		// 3) oes://catalog/{name} — Catalog metadata as JSON (proxies meta_query).
		{
			ResourceDescriptor d;
			d.uri         = "oes://catalog/{name}";
			d.name        = "Catalog metadata by name";
			d.description = "Resolves to `meta_query` on Catalog.{name}: "
			                 "attributes, tabular sections, forms, commands, "
			                 "modules. Subscribable per-name.";
			d.mimeType    = "application/json";
			d.isTemplate  = true;
			d.read        = &ReadCatalog;
			reg.Register(std::move(d));
		}
		// 4) oes://module/{owner}/{kind} — module source text.
		{
			ResourceDescriptor d;
			d.uri         = "oes://module/{owner}/{kind}";
			d.name        = "Module source by path";
			d.description = "Returns the CES/VES source of a module. "
			                 "owner is the dotted location (e.g. Catalog.X or "
			                 "Document.Y.Forms.ItemForm); kind is one of "
			                 "ObjectModule / ManagerModule / Module.";
			d.mimeType    = "text/x-oes-script";
			d.isTemplate  = true;
			d.read        = &ReadModule;
			reg.Register(std::move(d));
		}
		// 5) oes://docs/syntax/{ves|ces} — language reference quick-card.
		{
			ResourceDescriptor d;
			d.uri         = "oes://docs/syntax/{lang}";
			d.name        = "Built-in language reference";
			d.description = "Quick-card for OES script: 43 keywords, 69 "
			                 "opcodes, 91 built-in functions, operator "
			                 "precedence, literal syntax. lang is 'ves' or "
			                 "'ces'.";
			d.mimeType    = "text/markdown";
			d.isTemplate  = true;
			d.read        = &ReadDocsSyntax;
			reg.Register(std::move(d));
		}
	});
	return reg;
}

void SetNotifySink(NotifySink sink)
{
	std::lock_guard<std::mutex> lk(g_subMu);
	g_notifySink = std::move(sink);
}

void EmitResourceUpdated(const std::string& uri)
{
	NotifySink sink;
	bool subscribed = false;
	{
		std::lock_guard<std::mutex> lk(g_subMu);
		sink = g_notifySink;
		subscribed = (g_subscriptions.count(uri) > 0);
	}
	if (!sink || !subscribed) return;
	// v1: fire-and-forget. Callers don't await delivery.
	sink(uri);
}

void Subscribe(const std::string& uri)
{
	std::lock_guard<std::mutex> lk(g_subMu);
	g_subscriptions.insert(uri);
}

void Unsubscribe(const std::string& uri)
{
	std::lock_guard<std::mutex> lk(g_subMu);
	g_subscriptions.erase(uri);
}

bool IsSubscribed(const std::string& uri)
{
	std::lock_guard<std::mutex> lk(g_subMu);
	return g_subscriptions.count(uri) > 0;
}

void ClearSubscriptions()
{
	std::lock_guard<std::mutex> lk(g_subMu);
	g_subscriptions.clear();
}

} // namespace mcpServer
