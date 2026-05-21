/////////////////////////////////////////////////////////////////////////////
// aiBridge — first-party ABI v4 reference plugin.
//
// Wires the host's WebView pane + AI provider registry to a real
// OpenAI-compatible HTTP endpoint. Reads two environment values from
// the per-plugin BYOK store (~/.config/OES/plugins/aiBridge.env):
//
//   TOKEN     — Bearer auth header value (required to talk to the API)
//   ENDPOINT  — full URL of the chat-completions endpoint (default:
//               https://api.openai.com/v1/chat/completions)
//   MODEL     — model name (default: gpt-4o-mini)
//
// The plugin:
//   1. Registers itself as an AI provider with supportedModes={chat, agent}
//      so the host's HasAIProviderFor("chat") gate flips ON.
//   2. Registers a WebView pane backed by sample.html so the user has
//      the demo chat UI without needing a separate bundle.
//   3. Intercepts chat.send envelopes from the WebView's postMessage
//      channel, spawns a worker thread that POSTs to the endpoint, and
//      streams response chunks back via WebPaneSend chat.delta/end/error
//      envelopes the sample bundle already renders.
//
// HTTP via cpp-httplib (already vendored under src/3rdparty/cpp-httplib).
// JSON via nlohmann (already vendored under src/3rdparty/nlohmann).
/////////////////////////////////////////////////////////////////////////////

#include "backend/plugin/pluginApi.h"

#include "3rdparty/cpp-httplib/httplib.h"
#include "3rdparty/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>   // PATH_MAX
#endif
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// OES_PLUGIN_EXPORT comes from backend/plugin/pluginApi.h.

namespace {

const ibHostAPI* g_host    = nullptr;
const char*      g_paneId  = "aiBridge.chat";

void DiagWrite(const std::string& line)
{
	FILE* f = std::fopen("/tmp/oes-diag.log", "a");
	if (f == nullptr) return;
	std::fprintf(f, "%s\n", line.c_str());
	std::fclose(f);
}

std::mutex g_auditMu;

std::string LocalTimestamp(char dateOut[11])
{
	std::time_t now = std::time(nullptr);
	std::tm tmNow{};
#if defined(_WIN32)
	localtime_s(&tmNow, &now);
#else
	localtime_r(&now, &tmNow);
#endif
	std::ostringstream ts;
	ts << std::put_time(&tmNow, "%Y-%m-%dT%H:%M:%S%z");
	if (dateOut != nullptr) {
		std::strftime(dateOut, 11, "%Y-%m-%d", &tmNow);
	}
	return ts.str();
}

std::filesystem::path AuditDir()
{
#if defined(_WIN32)
	const char* home = std::getenv("USERPROFILE");
#else
	const char* home = std::getenv("HOME");
#endif
	if (home == nullptr || *home == '\0') {
		return std::filesystem::temp_directory_path() / "oes" / "ai-audit";
	}
	return std::filesystem::path(home) / ".oes" / "ai-audit";
}

void AuditWrite(std::string event, nlohmann::json fields = nlohmann::json::object())
{
	try {
		char date[11] = {};
		nlohmann::json row = nlohmann::json::object();
		row["ts"]     = LocalTimestamp(date);
		row["plugin"] = "aiBridge";
		row["event"]  = std::move(event);
		if (fields.is_object()) {
			for (auto it = fields.begin(); it != fields.end(); ++it) {
				row[it.key()] = it.value();
			}
		}

		const std::filesystem::path dir = AuditDir();
		std::filesystem::create_directories(dir);
		const std::filesystem::path file =
		    dir / (std::string("aiBridge-") + date + ".jsonl");

		std::lock_guard<std::mutex> lk(g_auditMu);
		std::ofstream out(file, std::ios::binary | std::ios::app);
		if (!out.is_open()) return;
		out << row.dump() << '\n';
	} catch (const std::exception& e) {
		DiagWrite("aiBridge audit write exception: " + std::string(e.what()));
	} catch (...) {
		DiagWrite("aiBridge audit write exception");
	}
}

// SEC-P0-1: write the unredacted HTTP error body to a diagnostic file
// only when OES_AI_DEBUG_UNSAFE=1. User-visible EmitError envelopes
// never receive the body slice — even truncated to 256 bytes, OpenAI
// error payloads can echo back the bearer token / request body.
void DiagWriteUnsafeBody(const char* phase, int status, const std::string& body)
{
	const char* gate = std::getenv("OES_AI_DEBUG_UNSAFE");
	if (gate == nullptr || std::strcmp(gate, "1") != 0) return;
	FILE* f = std::fopen("/tmp/oes-diag.log", "a");
	if (f == nullptr) return;
	std::fprintf(f, "[aiBridge.unsafe] %s HTTP %d body[0..512]=%.512s\n",
	             phase ? phase : "?", status, body.c_str());
	std::fclose(f);
}

std::mutex                   g_workersMu;
std::vector<std::thread>     g_workers;
std::atomic<bool>            g_shuttingDown{false};

// Track in-flight httplib::Client instances so shutdown can wait for the
// owning workers without leaving stale active-client entries behind.
std::mutex                                  g_clientsMu;
std::vector<httplib::Client*>               g_activeClients;

void RegisterClient(httplib::Client* c)
{
	std::lock_guard<std::mutex> lk(g_clientsMu);
	g_activeClients.push_back(c);
}

void UnregisterClient(httplib::Client* c)
{
	std::lock_guard<std::mutex> lk(g_clientsMu);
	g_activeClients.erase(
	    std::remove(g_activeClients.begin(), g_activeClients.end(), c),
	    g_activeClients.end());
}

// RAII binder — register on construction, unregister on destruction so
// every early return path is covered without explicit cleanup.
struct ClientGuard {
	httplib::Client* cli;
	explicit ClientGuard(httplib::Client* c) : cli(c) { RegisterClient(cli); }
	~ClientGuard() { UnregisterClient(cli); }
	ClientGuard(const ClientGuard&) = delete;
	ClientGuard& operator=(const ClientGuard&) = delete;
};

httplib::Client& NewHttpClient(const std::string& base)
{
	// cpp-httplib's SSL client destructor can abort on macOS when SSL state
	// is still attached to the internal socket after a completed request.
	// aiBridge is a process-lifetime plugin; keep clients alive until
	// process exit instead of destroying them on the worker stack.
	return *new httplib::Client(base);
}

constexpr const char* kHttpUserAgent = "OES-Designer/1.0";

httplib::Headers MakeJsonHeaders(const std::string& token,
                                  const std::string& tenant,
                                  const char* accept = "application/json")
{
	httplib::Headers headers = {
		{ "Authorization", "Bearer " + token },
		{ "Content-Type",  "application/json" },
		{ "Accept",        accept ? accept : "application/json" },
		{ "User-Agent",    kHttpUserAgent },
	};
	if (!tenant.empty()) {
		headers.emplace("X-Tenant-Id", tenant);
	}
	return headers;
}

// Per-request cancel tokens for the Stop Generation feature. Pane fires
// agent.cancel with the original requestId; the worker polls between
// SSE chunks. shared_ptr lets the worker capture by value so erase from
// the map can't dangle.
std::mutex                                                          g_cancelMu;
std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> g_cancelTokens;

// AGENT-MODE: oes_agent plan cache — Approve/Reject handlers look up the
// server-issued planId here to recover the conversationId + mutations[]
// without re-asking the server. Cleared on resolve so a long Designer
// session doesn't accumulate plans forever.
struct CachedPlan {
	std::string    conversationId;
	nlohmann::json mutations;   // array of {op, kind, fullName, properties}
};
std::mutex                                  g_plansMu;
std::unordered_map<std::string, CachedPlan> g_planCache;

std::shared_ptr<std::atomic<bool>> AllocCancelToken(const std::string& rid)
{
	auto tok = std::make_shared<std::atomic<bool>>(false);
	std::lock_guard<std::mutex> lk(g_cancelMu);
	g_cancelTokens[rid] = tok;
	return tok;
}

void EraseCancelToken(const std::string& rid)
{
	std::lock_guard<std::mutex> lk(g_cancelMu);
	g_cancelTokens.erase(rid);
}

bool TripCancelToken(const std::string& rid)
{
	std::lock_guard<std::mutex> lk(g_cancelMu);
	auto it = g_cancelTokens.find(rid);
	if (it == g_cancelTokens.end()) return false;
	it->second->store(true);
	return true;
}

// Cached env snapshot — populated once at initialize() because the host's
// ReadPluginEnv trampoline depends on a thread-local `currentPluginId`
// that is only set during oes_plugin_initialize(). Worker threads
// spawned later (RunChatRequest) cannot reach the env via the host;
// reading it once here, while we're on the init thread, sidesteps that
// entirely.
std::mutex                                  g_envMu;
std::unordered_map<std::string, std::string> g_envCache;

void PrimeEnvCache(const char* const* keys)
{
	if (g_host == nullptr || g_host->ReadPluginEnv == nullptr) return;
	std::lock_guard<std::mutex> lk(g_envMu);
	for (const char* const* p = keys; *p != nullptr; ++p) {
		char* buf = g_host->ReadPluginEnv(*p);
		if (buf == nullptr) {
			g_envCache.erase(*p);
			continue;
		}
		g_envCache[*p] = buf;
		if (g_host->FreeBuffer) g_host->FreeBuffer(buf);
	}
}

// Read a cached env value (worker-thread safe). Falls back to the
// supplied default when the key is missing. If the cache is empty (no
// PrimeEnvCache called yet), tries the live host trampoline as a
// best-effort fallback — useful for the init phase itself.
std::string ReadEnv(const char* key, const char* fallback)
{
	{
		std::lock_guard<std::mutex> lk(g_envMu);
		auto it = g_envCache.find(key);
		if (it != g_envCache.end()) return it->second;
	}
	// Fallback: try the live trampoline. Works only on the init thread
	// where tl_currentPluginId is set, which is exactly the path that
	// primes the cache below — so this branch covers any code path that
	// hits ReadEnv before PrimeEnvCache runs.
	if (g_host != nullptr && g_host->ReadPluginEnv != nullptr) {
		char* buf = g_host->ReadPluginEnv(key);
		if (buf != nullptr) {
			std::string out(buf);
			if (g_host->FreeBuffer) g_host->FreeBuffer(buf);
			return out;
		}
	}
	return fallback ? std::string(fallback) : std::string();
}

std::string ReadEnvAny(const char* key1, const char* key2,
                       const char* key3, const char* fallback)
{
	std::string value = ReadEnv(key1, "");
	if (!value.empty()) return value;
	if (key2 != nullptr) {
		value = ReadEnv(key2, "");
		if (!value.empty()) return value;
	}
	if (key3 != nullptr) {
		value = ReadEnv(key3, "");
		if (!value.empty()) return value;
	}
	return fallback ? std::string(fallback) : std::string();
}

std::string TrimCopy(std::string value)
{
	const auto notSpace = [](unsigned char c) {
		return c != ' ' && c != '\t' && c != '\r' && c != '\n';
	};
	while (!value.empty() && !notSpace(static_cast<unsigned char>(value.front()))) {
		value.erase(value.begin());
	}
	while (!value.empty() && !notSpace(static_cast<unsigned char>(value.back()))) {
		value.pop_back();
	}
	if (value.size() >= 2 &&
	    ((value.front() == '"' && value.back() == '"') ||
	     (value.front() == '\'' && value.back() == '\''))) {
		value = value.substr(1, value.size() - 2);
	}
	return value;
}

std::string NormalizePugiLocale(std::string raw)
{
	raw = TrimCopy(raw);
	if (raw.empty()) return "uk-UA";
	std::string lower;
	lower.reserve(raw.size());
	for (unsigned char c : raw) {
		lower.push_back(static_cast<char>(std::tolower(c)));
	}
	if (lower == "uk" || lower == "uk_ua" || lower == "uk-ua") return "uk-UA";
	if (lower == "ru" || lower == "ru_ru" || lower == "ru-ru" ||
	    lower == "be" || lower == "be_by" || lower == "be-by") return "ru-RU";
	if (lower == "en" || lower == "en_us" || lower == "en-us" ||
	    lower == "c" || lower == "posix") return "en-US";
	return "en-US";
}

std::string PugiEndpoint()
{
	const std::string explicitEndpoint = TrimCopy(ReadEnv("ENDPOINT", ""));
	if (!explicitEndpoint.empty()) return explicitEndpoint;

	std::string base = TrimCopy(ReadEnv("PUGI_BASE_URL", "https://mcp.pugi.io"));
	while (!base.empty() && base.back() == '/') base.pop_back();
	if (base.empty()) return "https://mcp.pugi.io/api/oes-mcp/invoke";
	if (base.size() >= 19 &&
	    base.compare(base.size() - 19, 19, "/api/oes-mcp/invoke") == 0) {
		return base;
	}
	return base + "/api/oes-mcp/invoke";
}

std::string PugiToken()
{
	return TrimCopy(ReadEnvAny("TOKEN", "PUGI_OES_API_KEY",
	                          "PUGI_TENANT_TOKEN", ""));
}

std::string PugiTenant()
{
	return TrimCopy(ReadEnvAny("TENANT", "PUGI_TENANT_ID",
	                          "PUGI_TENANT", ""));
}

std::string PugiLocale()
{
	return NormalizePugiLocale(ReadEnvAny("LOCALE", "PUGI_OES_LOCALE",
	                                      nullptr, "uk-UA"));
}

struct ChatProfile {
	std::string id;
	std::string protocol;
	std::string endpoint;
	std::string model;
};

ChatProfile ResolveChatProfile(std::string requested)
{
	requested = TrimCopy(std::move(requested));
	if (requested == "pugi") {
		return {
			"pugi",
			"pugi-mcp",
			PugiEndpoint(),
			"pugi-mcp",
		};
	}
	if (requested == "openai-fast") {
		return {
			"openai-fast",
			"openai",
			ReadEnv("ENDPOINT", "https://api.openai.com/v1/chat/completions"),
			ReadEnv("MODEL_FAST", "gpt-4o-mini"),
		};
	}
	if (requested == "openai-quality") {
		return {
			"openai-quality",
			"openai",
			ReadEnv("ENDPOINT", "https://api.openai.com/v1/chat/completions"),
			ReadEnv("MODEL_QUALITY", "gpt-4o"),
		};
	}
	const std::string protocol = ReadEnv("PROTOCOL", "openai");
	return {
		requested.empty() ? std::string("env") : requested,
		protocol,
		protocol == "pugi-mcp"
		    ? PugiEndpoint()
		    : ReadEnv("ENDPOINT", "https://api.openai.com/v1/chat/completions"),
		ReadEnv("MODEL", "gpt-4o-mini"),
	};
}

// Split a URL into (scheme+host, path). Trivial parser — enough for
// the simple "https://api.openai.com/v1/chat/completions" shape we
// expect. Returns {"", ""} on malformed input.
std::pair<std::string, std::string> SplitUrl(const std::string& url)
{
	const auto schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return {std::string(), std::string()};
	const auto pathStart = url.find('/', schemeEnd + 3);
	if (pathStart == std::string::npos) {
		return {url, "/"};
	}
	return {url.substr(0, pathStart), url.substr(pathStart)};
}

// SEC-P1-6: scheme allow-list + HTTPS support precondition. Returns empty
// string on success, an error reason on rejection. Only http:// and
// https:// are permitted; an https:// endpoint without OpenSSL support
// must refuse — silently downgrading to plaintext leaks the bearer.
std::string ValidateEndpointScheme(const std::string& url)
{
	if (url.rfind("http://", 0) == 0) {
		return std::string();
	}
	if (url.rfind("https://", 0) == 0) {
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
		return "aiBridge: HTTPS endpoint requires OpenSSL — rebuild with OpenSSL or use http://";
#else
		return std::string();
#endif
	}
	return "aiBridge: only http:// and https:// schemes are allowed";
}

std::string ShellQuote(const std::string& value)
{
	std::string out = "'";
	for (char c : value) {
		if (c == '\'') out += "'\\''";
		else out.push_back(c);
	}
	out += "'";
	return out;
}

std::string CurlConfigEscape(const std::string& value)
{
	std::string out;
	out.reserve(value.size());
	for (char c : value) {
		if (c == '\\' || c == '"') out.push_back('\\');
		out.push_back(c);
	}
	return out;
}

std::filesystem::path TempPath(const char* suffix)
{
	const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
	std::ostringstream name;
	name << "aibridge-" << ticks << "-" << tid << suffix;
	return std::filesystem::temp_directory_path() / name.str();
}

struct CurlResponse {
	int status = 0;
	std::string body;
	std::string error;
};

CurlResponse CurlPostJson(const std::string& endpoint,
                          const std::string& token,
                          const std::string& tenant,
                          const nlohmann::json& body,
                          int timeoutSec)
{
	CurlResponse out;
	const std::filesystem::path bodyPath = TempPath(".json");
	const std::filesystem::path cfgPath  = TempPath(".curl");

	try {
		{
			std::ofstream f(bodyPath, std::ios::binary);
			if (!f.is_open()) {
				out.error = "cannot create request body file";
				return out;
			}
			f << body.dump();
			std::error_code ec;
			std::filesystem::permissions(
				bodyPath,
				std::filesystem::perms::owner_read |
				std::filesystem::perms::owner_write,
				std::filesystem::perm_options::replace,
				ec);
		}
		{
			std::ofstream f(cfgPath, std::ios::binary);
			if (!f.is_open()) {
				out.error = "cannot create curl config file";
				std::error_code ec;
				std::filesystem::remove(bodyPath, ec);
				return out;
			}
			f << "silent\n";
			f << "show-error\n";
			f << "request = \"POST\"\n";
			f << "url = \"" << CurlConfigEscape(endpoint) << "\"\n";
			f << "max-time = " << timeoutSec << "\n";
			f << "header = \"Authorization: Bearer "
			  << CurlConfigEscape(token) << "\"\n";
			f << "header = \"Content-Type: application/json\"\n";
			f << "header = \"Accept: application/json\"\n";
			f << "header = \"User-Agent: " << kHttpUserAgent << "\"\n";
			if (!tenant.empty()) {
				f << "header = \"X-Tenant-Id: "
				  << CurlConfigEscape(tenant) << "\"\n";
			}
			f << "data-binary = \"@" << CurlConfigEscape(bodyPath.string())
			  << "\"\n";
			f << "write-out = \"\\n%{http_code}\"\n";
			std::error_code ec;
			std::filesystem::permissions(
				cfgPath,
				std::filesystem::perms::owner_read |
				std::filesystem::perms::owner_write,
				std::filesystem::perm_options::replace,
				ec);
		}

		const std::string curl =
			std::filesystem::exists("/usr/bin/curl") ? "/usr/bin/curl" : "curl";
		const std::string cmd = ShellQuote(curl) + " --config " +
		                        ShellQuote(cfgPath.string()) + " 2>&1";
		FILE* pipe = popen(cmd.c_str(), "r");
		if (pipe == nullptr) {
			out.error = "cannot start curl";
		} else {
			char buf[4096];
			while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
				out.body += buf;
			}
			(void)pclose(pipe);
		}
	} catch (const std::exception& e) {
		out.error = e.what();
	} catch (...) {
		out.error = "curl transport exception";
	}

	std::error_code ec;
	std::filesystem::remove(bodyPath, ec);
	std::filesystem::remove(cfgPath, ec);

	std::string text = out.body;
	while (!text.empty() &&
	       (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
		text.pop_back();
	}
	if (text.size() >= 3 &&
	    std::isdigit(static_cast<unsigned char>(text[text.size() - 1])) &&
	    std::isdigit(static_cast<unsigned char>(text[text.size() - 2])) &&
	    std::isdigit(static_cast<unsigned char>(text[text.size() - 3]))) {
		out.status = std::atoi(text.substr(text.size() - 3).c_str());
		text.erase(text.size() - 3);
		if (!text.empty() && text.back() == '\n') text.pop_back();
		if (!text.empty() && text.back() == '\r') text.pop_back();
		out.body = text;
	}
	if (out.status == 0 && out.error.empty()) {
		out.error = out.body.empty() ? "curl returned no HTTP status" : out.body;
	}
	return out;
}

// Sends a chat.error envelope to the pane.
void EmitError(const std::string& message)
{
	if (g_host == nullptr || g_host->WebPaneSend == nullptr) return;
	nlohmann::json env;
	env["kind"]    = "error";
	env["error"]   = nlohmann::json::object();
	env["error"]["code"]   = "transport";
	env["error"]["detail"] = message;
	const std::string s = env.dump();
	g_host->WebPaneSend(g_paneId, s.c_str());
}

// Sends a single chat.delta envelope with the accumulated text fragment.
void EmitDelta(const std::string& requestId, const std::string& delta)
{
	if (g_host == nullptr || g_host->WebPaneSend == nullptr) return;
	nlohmann::json env;
	env["kind"]      = "chat.delta";
	env["requestId"] = requestId;
	env["delta"]     = delta;
	const std::string s = env.dump();
	g_host->WebPaneSend(g_paneId, s.c_str());
}

int NormalizeConfidencePercent(double value)
{
	if (value >= 0.0 && value <= 1.0) value *= 100.0;
	if (value < 0.0) value = 0.0;
	if (value > 100.0) value = 100.0;
	return static_cast<int>(value + 0.5);
}

int ExtractConfidencePercent(const nlohmann::json& obj)
{
	if (!obj.is_object()) return -1;
	static const char* kKeys[] = {
		"confidence",
		"selfConfidence",
		"suitabilityScore",
		"score",
		nullptr
	};
	for (const char** key = kKeys; *key != nullptr; ++key) {
		if (!obj.contains(*key) || !obj[*key].is_number()) continue;
		return NormalizeConfidencePercent(obj[*key].get<double>());
	}
	if (obj.contains("reliability") && obj["reliability"].is_object()) {
		return ExtractConfidencePercent(obj["reliability"]);
	}
	return -1;
}

// Sends the chat.end envelope so the pane closes its streaming row +
// renders the footer cost meter.
void EmitEnd(const std::string& requestId, const std::string& model,
              int tokensIn, int tokensOut, int confidencePercent = -1,
              const char* confidenceSource = nullptr)
{
	if (g_host == nullptr || g_host->WebPaneSend == nullptr) return;
	nlohmann::json env;
	env["kind"]      = "chat.end";
	env["requestId"] = requestId;
	env["meta"]      = nlohmann::json::object();
	env["meta"]["model"] = model;
	if (confidencePercent >= 0) {
		env["meta"]["confidence"] = confidencePercent;
		env["meta"]["suitabilityConcern"] = confidencePercent < 70;
		if (confidenceSource != nullptr && confidenceSource[0] != '\0') {
			env["meta"]["confidenceSource"] = confidenceSource;
		}
	}
	if (tokensIn > 0 || tokensOut > 0) {
		env["meta"]["tokens"]            = nlohmann::json::object();
		env["meta"]["tokens"]["in"]      = tokensIn;
		env["meta"]["tokens"]["out"]     = tokensOut;
	}
	const std::string s = env.dump();
	g_host->WebPaneSend(g_paneId, s.c_str());
}

// Emit the chat.end envelope with cancelled:true. The Pugi-MCP path
// stops mid-typewriter when cancellation trips; the SSE path stops mid-
// receiver. Both reach this so the pane sees the same wire signal.
void EmitCancelledEnd(const std::string& requestId, const std::string& model,
                      int tokensIn, int tokensOut)
{
	if (g_host == nullptr || g_host->WebPaneSend == nullptr) return;
	nlohmann::json env;
	env["kind"]      = "chat.end";
	env["requestId"] = requestId;
	env["meta"]      = nlohmann::json::object();
	env["meta"]["model"]      = model;
	env["meta"]["cancelled"]  = true;
	if (tokensIn > 0 || tokensOut > 0) {
		env["meta"]["tokens"]            = nlohmann::json::object();
		env["meta"]["tokens"]["in"]      = tokensIn;
		env["meta"]["tokens"]["out"]     = tokensOut;
	}
	const std::string s = env.dump();
	g_host->WebPaneSend(g_paneId, s.c_str());
}

// Emit the response text as a typewriter-streamed sequence of chat.delta
// envelopes followed by chat.end. Used by the Pugi-MCP path (single-
// response endpoint) so the UI gets the same incremental render the
// SSE OpenAI path produces. Skipped on empty input. Honours a cancel
// token between chunks — when tripped, emits chat.end{cancelled:true}
// and returns early without finishing the body.
void EmitTypewriter(const std::string& requestId, const std::string& text,
                     const std::string& model, int approxTokens,
                     const std::shared_ptr<std::atomic<bool>>& cancelTok,
                     int confidencePercent = -1)
{
	const size_t kChunkChars = 96;
	const std::chrono::milliseconds kChunkDelay(40);
	size_t i = 0;
	while (i < text.size()) {
		if (cancelTok && cancelTok->load()) {
			EmitCancelledEnd(requestId, model, approxTokens, approxTokens);
			return;
		}
		size_t end = std::min(i + kChunkChars, text.size());
		// UTF-8 codepoint-safe boundary — never split a multibyte glyph.
		while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
			++end;
		}
		EmitDelta(requestId, text.substr(i, end - i));
		i = end;
		if (i < text.size()) std::this_thread::sleep_for(kChunkDelay);
	}
	EmitEnd(requestId, model, approxTokens, approxTokens,
	        confidencePercent, confidencePercent >= 0 ? "pugi" : nullptr);
}

// Pugi-MCP request path. Talks to mcp.pugi.io / Anvil with the request
// shape that backend actually expects:
//   POST  <endpoint>
//   H:    Authorization: Bearer <token>
//         X-Tenant-Id: <tenant-uuid>
//         Content-Type: application/json
//   Body: {"name":"ai_chat_query","input":{"prompt":"...","locale":"uk-UA"}}
// Response: 200/201 {"result":{"content":"...","model":"...",...}}
//           4xx     {"message":"...","statusCode":N}
// Single-shot — no SSE. Streaming effect is faked via EmitTypewriter.
//
// Optional `toolName` overrides the default "llm_query"; used by the
// triple-review op which calls Anvil's `triple_review` tool instead.
// Optional `extraInput` is merged into the input object — triple_review
// expects extra fields like `code`, `language`, `context`.
// renderedResponse, when non-empty, replaces the response-text extracted
// from result.content; lets the caller insert the structured-findings
// envelope (see RunTripleReview) before typewriting.
void RunPugiMcpRequest(std::string requestId, std::string prompt,
                        const std::string& endpoint, const std::string& token,
                        const std::string& tenant,   const std::string& locale,
                        const std::string& mode,
                        const std::shared_ptr<std::atomic<bool>>& cancelTok)
{
	// SEC-P1-6: scheme + HTTPS precondition.
	if (auto why = ValidateEndpointScheme(endpoint); !why.empty()) {
		EmitError(why);
		return;
	}
	if (g_shuttingDown.load()) {
		EmitError("aiBridge[pugi-mcp]: shutdown in progress");
		return;
	}

	nlohmann::json body;
	body["name"]                     = "ai_chat_query";
	body["input"]                    = nlohmann::json::object();
	body["input"]["prompt"]          = prompt;
	body["input"]["locale"]          = NormalizePugiLocale(locale);
	body["input"]["conversation"]    = nlohmann::json::array();
	body["input"]["mode"]            = mode.empty() ? std::string("chat") : mode;

	AuditWrite("chat.request", {
		{ "requestId", requestId },
		{ "protocol",  "pugi-mcp" },
		{ "tool",      "ai_chat_query" },
		{ "mode",      body["input"]["mode"] },
		{ "locale",    body["input"]["locale"] },
		{ "chars",     static_cast<int>(prompt.size()) },
	});
	const CurlResponse res = CurlPostJson(endpoint, token, tenant, body, 70);
	if (res.status == 0) {
		EmitError("aiBridge[pugi-mcp]: HTTP failed — " + res.error);
		return;
	}
	if (res.status >= 300 && res.status < 400) {
		EmitError("aiBridge[pugi-mcp]: server returned redirect; bearer not forwarded for security");
		return;
	}
	if (res.status >= 400) {
		// SEC-P0-1: redact body — OpenAI/Anvil error payloads echo back
		// the bearer token and/or original request body. Unredacted slice
		// goes to /tmp/oes-diag.log only when OES_AI_DEBUG_UNSAFE=1.
		DiagWriteUnsafeBody("pugi-mcp", res.status, res.body);
		AuditWrite("chat.error", {
			{ "requestId", requestId },
			{ "protocol",  "pugi-mcp" },
			{ "tool",      "ai_chat_query" },
			{ "status",    res.status },
		});
		EmitError("aiBridge[pugi-mcp]: HTTP " + std::to_string(res.status));
		return;
	}

	// Extract result.content. Tolerate result-as-string fallback for
	// any backend version that returns a flat shape.
	std::string content;
	std::string respModel = "pugi-mcp";
	int confidencePercent = -1;
	auto parsed = nlohmann::json::parse(res.body, nullptr, /*allow_exceptions=*/false);
	if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("result")) {
		const auto& r = parsed["result"];
		if (r.is_object()) {
			if (r.contains("content") && r["content"].is_string()) {
				content = r["content"].get<std::string>();
			}
			if (r.contains("model") && r["model"].is_string()) {
				respModel = r["model"].get<std::string>();
			}
			confidencePercent = ExtractConfidencePercent(r);
		} else if (r.is_string()) {
			content = r.get<std::string>();
		}
	}
	if (content.empty()) {
		// Server returned something we can't unwrap — emit raw so the
		// user sees the actual diagnostic, not a silent failure.
		content = res.body;
	}

	const int approxTokens = static_cast<int>(content.size() / 4);
	AuditWrite("chat.response", {
		{ "requestId", requestId },
		{ "protocol",  "pugi-mcp" },
		{ "tool",      "ai_chat_query" },
		{ "status",    res.status },
		{ "model",     respModel },
		{ "chars",     static_cast<int>(content.size()) },
		{ "confidence", confidencePercent },
	});
	EmitTypewriter(requestId, content, respModel, approxTokens, cancelTok,
	               confidencePercent);
}

// Triple-review path — calls the `triple_review` Anvil tool with the raw
// CES/VES module text, then emits a structured `agent.tripleReview`
// envelope so the pane can render the verdict + per-reviewer findings as
// a table instead of raw JSON.
//
// Server contract (confirmed via the Pugi smoke run, 2026-05-20):
//   input  = { code, language: "CES"|"VES"|"BSL", locale, context? }
//   output = {
//     verdict:  "PASS"|"WARN"|"BLOCK",
//     reason:   "<one-line summary>",
//     reviewerCount: N,
//     reviewers: [ {model, content, p0, p1, p2, p3, latencyMs} ],
//     findings:  [ {severity:"P0..P3", line:N, message, fix, reviewer} ],
//     counts:    { P0, P1, P2, P3 }
//   }
//
// On 4xx / network error we emit a `kind: "error"` envelope so the pane's
// existing error path renders it consistently with chat failures.
void RunTripleReview(std::string requestId, std::string code,
                      const std::string& endpoint, const std::string& token,
                      const std::string& tenant,   const std::string& locale,
                      const std::string& language,
                      const std::shared_ptr<std::atomic<bool>>& cancelTok)
{
	// SEC-P1-6: scheme + HTTPS precondition.
	if (auto why = ValidateEndpointScheme(endpoint); !why.empty()) {
		EmitError(why);
		return;
	}
	if (g_shuttingDown.load()) {
		EmitError("aiBridge[triple-review]: shutdown in progress");
		return;
	}

	nlohmann::json body;
	body["name"]              = "triple_review";
	body["input"]             = nlohmann::json::object();
	body["input"]["code"]     = code;
	body["input"]["language"] = language.empty() ? std::string("CES") : language;
	body["input"]["locale"]   = NormalizePugiLocale(locale);

	AuditWrite("review.request", {
		{ "requestId", requestId },
		{ "tool",      "triple_review" },
		{ "language",  body["input"]["language"] },
		{ "locale",    body["input"]["locale"] },
		{ "chars",     static_cast<int>(code.size()) },
	});
	const CurlResponse res = CurlPostJson(endpoint, token, tenant, body, 70);
	if (res.status == 0) {
		EmitError("aiBridge[triple-review]: HTTP failed — " + res.error);
		return;
	}
	// SEC-P1-5: refuse 3xx — see RunPugiMcpRequest for the rationale.
	if (res.status >= 300 && res.status < 400) {
		EmitError("aiBridge[triple-review]: server returned redirect; bearer not forwarded for security");
		return;
	}
	if (res.status >= 400) {
		// SEC-P0-1: redact body — see RunPugiMcpRequest.
		DiagWriteUnsafeBody("triple-review", res.status, res.body);
		AuditWrite("review.error", {
			{ "requestId", requestId },
			{ "tool",      "triple_review" },
			{ "status",    res.status },
		});
		EmitError("aiBridge[triple-review]: HTTP " + std::to_string(res.status));
		return;
	}

	// Honour an inline cancel — the work is done server-side, but the
	// user may have hit Stop before the response landed. Skip the
	// envelope emit and let chat.end clean up.
	if (cancelTok && cancelTok->load()) {
		EmitCancelledEnd(requestId, "triple_review", 0, 0);
		return;
	}

	auto parsed = nlohmann::json::parse(res.body, nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object() ||
	    !parsed.contains("result") || !parsed["result"].is_object()) {
		EmitError("aiBridge[triple-review]: malformed response envelope");
		return;
	}
	const auto& result = parsed["result"];
	AuditWrite("review.response", {
		{ "requestId", requestId },
		{ "tool",      "triple_review" },
		{ "status",    res.status },
		{ "verdict",   result.value("verdict", std::string()) },
		{ "findings",  result.contains("findings") && result["findings"].is_array()
		                  ? static_cast<int>(result["findings"].size()) : 0 },
	});

	// Emit a single agent.tripleReview envelope containing the whole
	// result object. The pane has a dedicated renderer that walks
	// reviewers[] / findings[] / counts to lay out a structured verdict
	// view — no string parsing on the OES side.
	nlohmann::json env;
	env["kind"]      = "agent.tripleReview";
	env["requestId"] = requestId;
	env["result"]    = result;
	const std::string payload = env.dump();
	if (g_host != nullptr && g_host->WebPaneSend != nullptr) {
		g_host->WebPaneSend(g_paneId, payload.c_str());
	}

	// Also emit chat.end so the pane's pending-streaming state resets
	// cleanly. Tokens here are approximate — sum the reviewer counts.
	int tokensIn = static_cast<int>(code.size() / 4);
	int tokensOut = 0;
	if (result.contains("reviewers") && result["reviewers"].is_array()) {
		for (const auto& rv : result["reviewers"]) {
			tokensOut += rv.value("tokensUsed", 0);
		}
	}
	const int confidencePercent = ExtractConfidencePercent(result);
	EmitEnd(requestId, "triple_review", tokensIn, tokensOut,
	        confidencePercent, confidencePercent >= 0 ? "pugi" : nullptr);
}

// AGENT-MODE: fire-and-forget POST to oes_agent_resolve. Closes the round-
// trip after Approve / Reject / Partial. Failures are swallowed — the user
// already saw the plan apply, server-side bookkeeping is best-effort.
void PostAgentResolve(const std::string& endpoint, const std::string& token,
                       const std::string& tenant,
                       const std::string& planId, const std::string& conversationId,
                       const std::string& action,
                       const std::vector<int>& appliedOps,
                       const std::vector<int>& failedOps)
{
	if (auto why = ValidateEndpointScheme(endpoint); !why.empty()) return;
	if (g_shuttingDown.load()) return;

	nlohmann::json body;
	body["name"]                       = "oes_agent_resolve";
	body["input"]                       = nlohmann::json::object();
	body["input"]["planId"]             = planId;
	body["input"]["conversationId"]     = conversationId;
	body["input"]["action"]             = action;
	body["input"]["appliedOps"]         = appliedOps;
	body["input"]["failedOps"]          = failedOps;

	AuditWrite("agent.resolve", {
		{ "planId",     planId },
		{ "action",     action },
		{ "appliedOps", static_cast<int>(appliedOps.size()) },
		{ "failedOps",  static_cast<int>(failedOps.size()) },
	});
	const CurlResponse res = CurlPostJson(endpoint, token, tenant, body, 15);
	if (res.status == 0) {
		DiagWrite("aiBridge[oes-agent-resolve]: HTTP failed: " + res.error);
	} else if (res.status >= 400) {
		DiagWriteUnsafeBody("oes-agent-resolve", res.status, res.body);
	}
}

// AGENT-MODE: oes_agent path — calls the `oes_agent` Anvil tool with the
// user prompt + Designer context, receives a structured plan, and emits
// an `agent.plan` envelope so the pane renders rationale + mutations +
// Approve/Reject links. The plan is also cached so the approve handler
// can walk the mutations[] without round-tripping the server again.
void RunOesAgent(std::string requestId, std::string prompt,
                  const std::string& endpoint, const std::string& token,
                  const std::string& tenant,   const std::string& locale,
                  const std::string& contextJson,
                  const std::shared_ptr<std::atomic<bool>>& cancelTok)
{
	if (auto why = ValidateEndpointScheme(endpoint); !why.empty()) {
		EmitError(why);
		return;
	}
	if (g_shuttingDown.load()) {
		EmitError("aiBridge[oes-agent]: shutdown in progress");
		return;
	}

	nlohmann::json body;
	body["name"]              = "oes_agent";
	body["input"]             = nlohmann::json::object();
	body["input"]["prompt"]   = prompt;
	body["input"]["locale"]   = NormalizePugiLocale(locale);
	// AGENT-MODE: forward Designer context verbatim when provided; the
	// server uses it to disambiguate fullName references like "Catalog2".
	if (!contextJson.empty()) {
		auto ctx = nlohmann::json::parse(contextJson, nullptr, false);
		if (!ctx.is_discarded() && ctx.is_object()) {
			body["input"]["context"] = ctx;
		}
	}

	AuditWrite("agent.request", {
		{ "requestId",  requestId },
		{ "tool",       "oes_agent" },
		{ "locale",     body["input"]["locale"] },
		{ "chars",      static_cast<int>(prompt.size()) },
		{ "hasContext", body["input"].contains("context") },
	});
	const CurlResponse res = CurlPostJson(endpoint, token, tenant, body, 190);
	if (res.status == 0) {
		EmitError("aiBridge[oes-agent]: HTTP failed — " + res.error);
		return;
	}
	if (res.status >= 300 && res.status < 400) {
		EmitError("aiBridge[oes-agent]: server returned redirect; bearer not forwarded for security");
		return;
	}
	if (res.status >= 400) {
		DiagWriteUnsafeBody("oes-agent", res.status, res.body);
		AuditWrite("agent.error", {
			{ "requestId", requestId },
			{ "tool",      "oes_agent" },
			{ "status",    res.status },
		});
		EmitError("aiBridge[oes-agent]: HTTP " + std::to_string(res.status));
		return;
	}
	if (cancelTok && cancelTok->load()) {
		EmitCancelledEnd(requestId, "oes_agent", 0, 0);
		return;
	}

	auto parsed = nlohmann::json::parse(res.body, nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object() ||
	    !parsed.contains("result") || !parsed["result"].is_object()) {
		EmitError("aiBridge[oes-agent]: malformed response envelope");
		return;
	}
	const auto& result = parsed["result"];
	const std::string planId         = result.value("planId",         std::string());
	const std::string conversationId = result.value("conversationId", std::string());
	const std::string rationale      = result.value("rationale",      std::string());
	const std::string model          = result.value("model",          std::string("oes_agent"));
	const int         tokensUsed     = result.value("tokensUsed",     0);
	const int         confidencePercent = ExtractConfidencePercent(result);

	nlohmann::json mutations = nlohmann::json::array();
	if (result.contains("mutations") && result["mutations"].is_array()) {
		mutations = result["mutations"];
	}
	AuditWrite("agent.plan", {
		{ "requestId", requestId },
		{ "planId",    planId },
		{ "model",     model },
		{ "mutations", static_cast<int>(mutations.size()) },
		{ "confidence", confidencePercent },
	});

	// AGENT-MODE: stash {planId → {conversationId, mutations}} so the
	// approve / reject handlers can resolve without a server round-trip.
	if (!planId.empty()) {
		std::lock_guard<std::mutex> lk(g_plansMu);
		g_planCache[planId] = CachedPlan{ conversationId, mutations };
	}

	// Emit the agent.plan envelope — pane's DispatchEnvelope renders it
	// with rationale + per-mutation lines + Approve/Reject links.
	nlohmann::json env;
	env["kind"]            = "agent.plan";
	env["requestId"]       = requestId;
	env["planId"]          = planId;
	env["conversationId"]  = conversationId;
	env["rationale"]       = rationale;
	env["mutations"]       = mutations;
	if (result.contains("rollbackHints")) env["rollbackHints"] = result["rollbackHints"];
	if (result.contains("language"))      env["language"]      = result["language"];
	const std::string payload = env.dump();
	if (g_host != nullptr && g_host->WebPaneSend != nullptr) {
		g_host->WebPaneSend(g_paneId, payload.c_str());
	}

	// Close streaming-state for the pane's pending row.
	EmitEnd(requestId, model, static_cast<int>(prompt.size() / 4), tokensUsed,
	        confidencePercent, confidencePercent >= 0 ? "pugi" : nullptr);
}

// AGENT-MODE: walk the cached plan's mutations[] and dispatch each one
// to the host's MetaCreate / MetaEdit / MetaDelete trampoline. Tracks
// appliedOps[] + failedOps[] by mutation index. On a per-op failure we
// emit a kind:"error" envelope describing which op failed (so the user
// sees the diagnostic) and continue with the remaining ops — partial
// apply is preferred over all-or-nothing because each MetaCreate is
// already individually undoable via Ctrl+Z.
void RunAgentApply(std::string planId, std::string conversationId,
                    nlohmann::json mutations,
                    const std::string& endpoint, const std::string& token,
                    const std::string& tenant)
{
	if (g_host == nullptr) return;

	std::vector<int> appliedOps;
	std::vector<int> failedOps;

	for (size_t i = 0; i < mutations.size(); ++i) {
		const auto& m = mutations[i];
		if (!m.is_object()) { failedOps.push_back(static_cast<int>(i)); continue; }
		const std::string op       = m.value("op",       std::string());
		const std::string kind     = m.value("kind",     std::string());
		const std::string fullName = m.value("fullName", std::string());
		const std::string props    = m.contains("properties")
		                                ? m["properties"].dump()
		                                : std::string("{}");

		int rc = -1;
		char* err = nullptr;
		if (op == "create" && g_host->MetaCreate != nullptr) {
			rc = g_host->MetaCreate(kind.c_str(), fullName.c_str(),
			                          props.c_str(), &err);
		} else if (op == "edit" && g_host->MetaEdit != nullptr) {
			// AGENT-MODE: MetaEdit takes jsonPatch — pass the properties
			// blob verbatim; server-side prompt is responsible for emitting
			// RFC 6902 shape when op=edit.
			rc = g_host->MetaEdit(fullName.c_str(), props.c_str(), &err);
		} else if (op == "delete" && g_host->MetaDelete != nullptr) {
			rc = g_host->MetaDelete(fullName.c_str(), props.c_str(), &err);
		} else {
			EmitError("aiBridge[oes-agent]: unknown op '" + op + "' at index " +
			           std::to_string(i));
			failedOps.push_back(static_cast<int>(i));
			continue;
		}

		if (rc == 0) {
			appliedOps.push_back(static_cast<int>(i));
		} else {
			std::string detail = "op[" + std::to_string(i) + "] " + op + " " +
			                       kind + " " + fullName + " failed (rc=" +
			                       std::to_string(rc) + ")";
			if (err != nullptr) detail += ": " + std::string(err);
			EmitError("aiBridge[oes-agent]: " + detail);
			failedOps.push_back(static_cast<int>(i));
		}
		if (err != nullptr && g_host->FreeBuffer != nullptr) {
			g_host->FreeBuffer(err);
		}
	}

	// Emit agent.applied so the pane marks the plan row as resolved.
	nlohmann::json env;
	env["kind"]        = "agent.applied";
	env["planId"]      = planId;
	env["appliedOps"]  = appliedOps;
	env["failedOps"]   = failedOps;
	const std::string payload = env.dump();
	if (g_host->WebPaneSend != nullptr) {
		g_host->WebPaneSend(g_paneId, payload.c_str());
	}
	AuditWrite("agent.apply", {
		{ "planId",     planId },
		{ "mutations",  static_cast<int>(mutations.size()) },
		{ "appliedOps", static_cast<int>(appliedOps.size()) },
		{ "failedOps",  static_cast<int>(failedOps.size()) },
	});

	// Close the server round-trip. action depends on whether anything failed.
	std::string action = "approved";
	if (!failedOps.empty()) {
		action = appliedOps.empty() ? "rejected" : "partial";
	}
	// AGENT-MODE: fire-and-forget on a separate worker so the apply path
	// returns immediately and the test / UI doesn't wait on the HTTP POST.
	// Mirrors the reject path's shape.
	{
		std::lock_guard<std::mutex> lk(g_workersMu);
		g_workers.emplace_back([endpoint, token, tenant, planId, conversationId,
		                          action, appliedOps, failedOps]() {
			PostAgentResolve(endpoint, token, tenant, planId, conversationId,
			                  action, appliedOps, failedOps);
		});
	}

	// Drop the cached plan — single-shot approve/reject by design.
	{
		std::lock_guard<std::mutex> lk(g_plansMu);
		g_planCache.erase(planId);
	}
}

// Worker thread body — performs the HTTP POST + streams chunks back to
// the pane. Owns its own httplib::Client; no shared state. cancelTok
// is the per-request stop signal; receiver polls it between SSE frames
// so a pane-fired agent.cancel halts further delta emits.
void RunChatRequest(std::string requestId, std::string prompt, std::string mode,
                    std::string profile,
                    std::shared_ptr<std::atomic<bool>> cancelTok)
{
	const std::string token    = ReadEnv("TOKEN",    "");
	const ChatProfile resolved = ResolveChatProfile(std::move(profile));
	const std::string protocol = resolved.protocol;  // "openai" or "pugi-mcp"
	const std::string endpoint = resolved.endpoint;
	const std::string model    = resolved.model;

	if (protocol != "pugi-mcp" && token.empty()) {
		EmitError("aiBridge: no TOKEN in plugin env. Set it via Tools → Plugins → Edit API token.");
		return;
	}

	// Pugi-MCP branch — bypass the OpenAI SSE path entirely.
	if (protocol == "pugi-mcp") {
		const std::string pugiToken = PugiToken();
		const std::string tenant = PugiTenant();
		if (pugiToken.empty()) {
			EmitError("aiBridge[pugi-mcp]: TOKEN or PUGI_OES_API_KEY is required.");
			return;
		}
		const std::string locale = PugiLocale();
		const std::string ridCopy = requestId;
		RunPugiMcpRequest(std::move(requestId), std::move(prompt),
		                    endpoint, pugiToken, tenant, locale, mode, cancelTok);
		EraseCancelToken(ridCopy);
		return;
	}

	// SEC-P1-6: scheme + HTTPS precondition.
	if (auto why = ValidateEndpointScheme(endpoint); !why.empty()) {
		EmitError(why);
		return;
	}
	const auto [base, path] = SplitUrl(endpoint);
	if (base.empty()) {
		EmitError("aiBridge: malformed ENDPOINT URL: " + endpoint);
		return;
	}

	httplib::Client& cli = NewHttpClient(base);
	cli.set_connection_timeout(15);
	cli.set_read_timeout(120);
	// SEC-P1-5: refuse 3xx — see RunPugiMcpRequest.
	cli.set_follow_location(false);
	// SEC-P2-1: register before Post() so shutdown can stop() us mid-flight.
	ClientGuard chatGuard(&cli);
	if (g_shuttingDown.load()) {
		EmitError("aiBridge: shutdown in progress");
		return;
	}

	// Build OpenAI-compatible chat-completions request body. Streaming
	// is requested so the host renders incremental chat.delta chunks
	// matching the spec's WebView protocol.
	nlohmann::json body;
	body["model"]    = model;
	body["stream"]   = true;
	body["messages"] = nlohmann::json::array();
	nlohmann::json sys;
	sys["role"]    = "system";
	sys["content"] = (mode == "agent")
	    ? "You are an OES Designer AI agent. You may propose metadata mutations."
	    : "You are an OES Designer AI assistant. Respond concisely.";
	body["messages"].push_back(sys);
	nlohmann::json usr;
	usr["role"]    = "user";
	usr["content"] = prompt;
	body["messages"].push_back(usr);

	httplib::Headers headers = MakeJsonHeaders(token, std::string(),
	                                           "text/event-stream");
	AuditWrite("chat.request", {
		{ "requestId", requestId },
		{ "protocol",  "openai" },
		{ "mode",      mode },
		{ "profile",   resolved.id },
		{ "model",     model },
		{ "chars",     static_cast<int>(prompt.size()) },
	});

	// SSE parser state. Each "data: <json>\n\n" frame is a chat
	// completion chunk. The body comes in as raw bytes from
	// ContentReceiver; we accumulate into m_sseBuf and process every
	// "\n\n" boundary.
	std::string sseBuf;
	int tokensOut = 0;

	auto parseChunk = [&](const std::string& chunkJson) {
		try {
			auto j = nlohmann::json::parse(chunkJson, nullptr, false);
			if (j.is_discarded()) return;
			if (!j.contains("choices") || !j["choices"].is_array()) return;
			for (auto& choice : j["choices"]) {
				if (!choice.contains("delta") || !choice["delta"].is_object()) continue;
				auto& delta = choice["delta"];
				if (delta.contains("content") && delta["content"].is_string()) {
					const std::string piece = delta["content"].get<std::string>();
					if (!piece.empty()) {
						EmitDelta(requestId, piece);
						tokensOut += static_cast<int>(piece.size() / 4); // crude estimate
					}
				}
			}
		} catch (...) {
			// Swallow malformed chunks — provider will resync.
		}
	};

	bool cancelled = false;
	auto receiver = [&](const char* data, size_t len) {
		if (g_shuttingDown.load()) return false;
		// Cancellation check per receive callback — httplib invokes this
		// once per socket read, so the worst-case latency between Stop
		// click and chunks halting is one TCP frame.
		if (cancelTok && cancelTok->load()) {
			cancelled = true;
			return false;
		}
		sseBuf.append(data, len);
		while (true) {
			const auto end = sseBuf.find("\n\n");
			if (end == std::string::npos) break;
			const std::string frame = sseBuf.substr(0, end);
			sseBuf.erase(0, end + 2);
			// Strip the "data: " prefix from every line, concat.
			std::string payload;
			std::size_t pos = 0;
			while (pos < frame.size()) {
				const auto eol = frame.find('\n', pos);
				const std::string line = (eol == std::string::npos)
				    ? frame.substr(pos)
				    : frame.substr(pos, eol - pos);
				pos = (eol == std::string::npos) ? frame.size() : eol + 1;
				if (line.rfind("data: ", 0) == 0) {
					payload += line.substr(6);
				}
			}
			if (payload == "[DONE]" || payload.empty()) continue;
			parseChunk(payload);
		}
		return true;
	};

	const std::string bodyStr = body.dump();
	httplib::Result res;
	try {
		res = cli.Post(path.c_str(), headers, bodyStr, "application/json", receiver);
	} catch (const std::exception& e) {
		EraseCancelToken(requestId);
		EmitError(std::string("aiBridge: transport exception — ") + e.what());
		return;
	} catch (...) {
		EraseCancelToken(requestId);
		EmitError("aiBridge: transport exception");
		return;
	}

	// Cancel path: receiver returned false because cancelTok flipped. The
	// httplib Post return value is "no res / connection closed" in that
	// case — we don't want to surface that as a transport error. Emit
	// chat.end{cancelled:true} so the pane wraps up cleanly.
	if (cancelled) {
		EraseCancelToken(requestId);
		const int tokensIn = static_cast<int>(prompt.size() / 4);
		EmitCancelledEnd(requestId, model, tokensIn, tokensOut);
		return;
	}

	if (!res) {
		EraseCancelToken(requestId);
		EmitError("aiBridge: HTTP failed — " +
		           std::string(httplib::to_string(res.error())));
		return;
	}
	// SEC-P1-5: refuse 3xx — auto-follow is disabled, so a 3xx now bubbles
	// up here rather than chaining the bearer to the redirect target.
	if (res->status >= 300 && res->status < 400) {
		EraseCancelToken(requestId);
		EmitError("aiBridge: server returned redirect; bearer not forwarded for security");
		return;
	}
	if (res->status >= 400) {
		EraseCancelToken(requestId);
		// SEC-P0-1: redact body — see RunPugiMcpRequest.
		DiagWriteUnsafeBody("chat", res->status, res->body);
		AuditWrite("chat.error", {
			{ "requestId", requestId },
			{ "protocol",  "openai" },
			{ "status",    res->status },
		});
		EmitError("aiBridge: HTTP " + std::to_string(res->status));
		return;
	}
	const int tokensIn = static_cast<int>(prompt.size() / 4);
	AuditWrite("chat.response", {
		{ "requestId", requestId },
		{ "protocol",  "openai" },
		{ "model",     model },
		{ "tokensOut", tokensOut },
	});
	EmitEnd(requestId, model, tokensIn, tokensOut);
	EraseCancelToken(requestId);
}

// Called by the host whenever the WebView pane posts a message. We
// only handle chat.send (Mode 1 / Mode 2 prompts). All other envelopes
// (action.open, agent.approve etc.) bubble through unhandled — Phase 7
// scope is the chat round-trip.
void OnPaneMessage(const char* paneId, const char* jsonInline, void* /*ud*/)
{
	(void)paneId;
	if (jsonInline == nullptr) return;
	try {
		auto j = nlohmann::json::parse(jsonInline, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return;
		if (!j.contains("kind") || !j["kind"].is_string()) return;
		const std::string kind = j["kind"].get<std::string>();

		// Stop Generation: pane fires agent.cancel with the original
		// requestId. Trip the token so the in-flight worker exits early.
		if (kind == "agent.cancel") {
			const std::string rid = j.value("requestId", std::string());
			TripCancelToken(rid);
			return;
		}

		// AGENT-MODE: agent.approve / agent.reject — pane fires these when
		// the user clicks the Approve/Reject link on a rendered plan row.
		// Approve walks mutations[] through MetaCreate/Edit/Delete. Reject
		// just closes the server-side round-trip.
		if (kind == "agent.approve" || kind == "agent.reject") {
			const std::string planId = j.value("planId", std::string());
			if (planId.empty()) {
				EmitError("aiBridge[oes-agent]: agent." +
				           std::string(kind == "agent.approve" ? "approve" : "reject") +
				           " missing planId");
				return;
			}

			CachedPlan plan;
			{
				std::lock_guard<std::mutex> lk(g_plansMu);
				auto it = g_planCache.find(planId);
				if (it == g_planCache.end()) {
					EmitError("aiBridge[oes-agent]: no cached plan for planId=" + planId);
					return;
				}
				plan = it->second;
			}

			const std::string endpoint = PugiEndpoint();
			const std::string token  = PugiToken();
			const std::string tenant = PugiTenant();

			if (kind == "agent.reject") {
				// AGENT-MODE: nothing to walk — just close the round-trip.
				std::lock_guard<std::mutex> lk(g_workersMu);
				g_workers.emplace_back([planId, conv = plan.conversationId,
				                          endpoint, token, tenant]() {
					PostAgentResolve(endpoint, token, tenant, planId, conv,
					                  "rejected", {}, {});
				});
				// Drop cached plan so a stale Approve click later no-ops.
				std::lock_guard<std::mutex> pk(g_plansMu);
				g_planCache.erase(planId);
				// Emit applied so the pane greys the plan row.
				nlohmann::json env;
				env["kind"]       = "agent.applied";
				env["planId"]     = planId;
				env["appliedOps"] = nlohmann::json::array();
				env["failedOps"]  = nlohmann::json::array();
				const std::string payload = env.dump();
				if (g_host != nullptr && g_host->WebPaneSend != nullptr) {
					g_host->WebPaneSend(g_paneId, payload.c_str());
				}
				return;
			}

			// Approve path — walk mutations synchronously while this
			// OnPaneMessage dispatch is still on the UI thread and still
			// carries the pane's pluginId policy scope. MetaCreate/Edit/Delete
			// require both; moving this work to a background worker trips the
			// main-thread guard and loses tl_currentPluginId.
			auto muts = plan.mutations;
			auto conv = plan.conversationId;
			RunAgentApply(planId, conv, muts, endpoint, token, tenant);
			return;
		}

		if (kind != "chat.send" && kind != "editor.skill") return;

		std::string prompt = j.value("prompt", std::string());
		const std::string op = j.value("op", std::string());

		// editor.skill op="triple-review" — call the Anvil triple_review
		// MCP tool directly (different request shape than llm_query) so
		// the pane gets structured per-reviewer findings + verdict
		// instead of free-text. Worker spawned inline because the path
		// uses pugi-mcp credentials regardless of PROTOCOL.
		if (kind == "editor.skill" && op == "triple-review") {
			const std::string token = PugiToken();
			const std::string endpoint = PugiEndpoint();
			const std::string tenant   = PugiTenant();
			const std::string locale   = PugiLocale();
			if (token.empty()) {
				EmitError("aiBridge[triple-review]: requires TOKEN or PUGI_OES_API_KEY in env.");
				return;
			}
			std::string requestId = j.value("requestId", std::string());
			if (requestId.empty()) {
				requestId = "req-" + std::to_string(
				    std::chrono::steady_clock::now().time_since_epoch().count());
			}
			const std::string code     = j.value("code",     std::string());
			const std::string language = j.value("language", std::string("CES"));
			auto cancelTok = AllocCancelToken(requestId);
			{
				std::lock_guard<std::mutex> lk(g_workersMu);
				g_workers.emplace_back(
				    [requestId, code, endpoint, token, tenant, locale, language, cancelTok]() {
					RunTripleReview(requestId, code, endpoint, token, tenant,
					                  locale, language, cancelTok);
					EraseCancelToken(requestId);
				});
			}
			return;
		}

		// AGENT-MODE: editor.skill op="agent" — call the `oes_agent` Anvil
		// tool to produce a structured plan. The user's natural-language
		// request lives in `prompt`; the editor's current code (when any)
		// rides along as additional context. Designer context (configName,
		// openObjects, currentSelection) is packed verbatim into the
		// server payload.
		if (kind == "editor.skill" && op == "agent") {
			const std::string token = PugiToken();
			const std::string endpoint = PugiEndpoint();
			const std::string tenant   = PugiTenant();
			const std::string locale   = PugiLocale();
			if (token.empty()) {
				EmitError("aiBridge[oes-agent]: requires TOKEN or PUGI_OES_API_KEY in env.");
				return;
			}
			std::string requestId = j.value("requestId", std::string());
			if (requestId.empty()) {
				requestId = "req-" + std::to_string(
				    std::chrono::steady_clock::now().time_since_epoch().count());
			}
			// AGENT-MODE: the user's intent — prefer explicit `prompt`,
			// fall back to `code` so the right-click "Create object via
			// agent" submenu (which has no separate prompt field) works.
			std::string agentPrompt = j.value("prompt", std::string());
			if (agentPrompt.empty()) {
				agentPrompt = j.value("code", std::string());
			}
			if (agentPrompt.empty()) {
				EmitError("aiBridge[oes-agent]: empty prompt");
				return;
			}

			// AGENT-MODE: assemble context — pass through known fields.
			nlohmann::json ctx = nlohmann::json::object();
			if (j.contains("configName"))      ctx["configName"]      = j["configName"];
			if (j.contains("openObjects"))     ctx["openObjects"]     = j["openObjects"];
			if (j.contains("currentSelection"))ctx["currentSelection"]= j["currentSelection"];
			if (j.contains("context") && j["context"].is_object()) {
				for (auto it = j["context"].begin(); it != j["context"].end(); ++it) {
					ctx[it.key()] = it.value();
				}
			}
			const std::string contextJson = ctx.dump();

			auto cancelTok = AllocCancelToken(requestId);
			{
				std::lock_guard<std::mutex> lk(g_workersMu);
				g_workers.emplace_back(
				    [requestId, agentPrompt, endpoint, token, tenant, locale, contextJson, cancelTok]() {
					RunOesAgent(requestId, agentPrompt, endpoint, token, tenant,
					              locale, contextJson, cancelTok);
					EraseCancelToken(requestId);
				});
			}
			return;
		}

		if (kind == "editor.skill") {
			const std::string code = j.value("code", std::string());
			AuditWrite(op == "commit" ? "commit_message.request" : "skill.request", {
				{ "op",      op.empty() ? std::string("send") : op },
				{ "chars",   static_cast<int>(code.size()) },
				{ "hasPrompt", !j.value("prompt", std::string()).empty() },
			});
			// Wrap the skill into a chat prompt with a stable preamble.
			// op="commit" carries a git diff (not source code), so the
			// preamble spells out the Conventional-Commits format we
			// want back — the LLM otherwise tends to drift into long
			// narrative summaries. Mirrors 1С:Workmate "Generate Commit
			// Message". The fence language is "diff" so the LLM sees
			// the payload as a diff, not as runnable code.
			static const char* opLabels[][2] = {
			    {"explain", "Объясни этот код."},
			    {"review",  "Проверь код на ошибки и улучши его."},
			    {"fix",     "Найди ошибки и предложи исправление."},
			    {"doc",     "Сгенерируй документирующий комментарий."},
			    {"send",    "Прокомментируй этот код."},
			    {"commit",
			     "Проанализируй следующий git diff и сгенерируй сообщение "
			     "коммита в стиле Conventional Commits на русском языке. "
			     "Сначала верни сообщение одним блоком кода без посторонних "
			     "пояснений вокруг блока — оно должно быть готово к "
			     "копированию и вставке в `git commit -m`.\n\n"
			     "Формат:\n"
			     "<тип>(<область>): <краткое описание до 72 символов>\n\n"
			     "<детальное описание изменений по пунктам, если их "
			     "больше одного>\n\n"
			     "Типы: feat, fix, refactor, docs, test, chore, build, ci, "
			     "perf, style. Область — опциональная, в скобках."},
			    {nullptr,   nullptr},
			};
			const char* preamble = "Прокомментируй этот код.";
			for (auto* row = opLabels[0]; row[0] != nullptr; row += 2) {
				if (op == row[0]) { preamble = row[1]; break; }
			}
			// op=commit ships its payload as a diff; everything else
			// is source code in CES/VES. The fence language hint helps
			// the LLM treat the body correctly.
			const std::string fenceLang = (op == "commit") ? "diff" : "";
			prompt = std::string(preamble) + "\n\n```" + fenceLang + "\n" +
			         code + "\n```";
			if (!j.value("prompt", std::string()).empty()) {
				prompt += "\n\n" + j["prompt"].get<std::string>();
			}
		}
		if (prompt.empty()) return;

		// Prefer the envelope's requestId so the pane's agent.cancel
		// keying lines up with the worker's token. Fall back to a fresh
		// id only when the caller didn't provide one (older bundles).
		std::string requestId = j.value("requestId", std::string());
		if (requestId.empty()) {
			requestId = "req-" + std::to_string(
			    std::chrono::steady_clock::now().time_since_epoch().count());
		}
		const std::string mode = j.value("mode", std::string("chat"));
		const std::string profile = j.value("profile", std::string("env"));

		// Allocate the cancel token before spawning so a near-instant
		// agent.cancel still finds a registered entry.
		auto cancelTok = AllocCancelToken(requestId);

		// Spawn a detached worker so the host's UI thread returns
		// immediately. Track the thread so shutdown can join + drain.
		{
			std::lock_guard<std::mutex> lk(g_workersMu);
			g_workers.emplace_back(RunChatRequest, requestId, prompt, mode,
			                       profile, cancelTok);
		}
	} catch (...) {
		// Plugin-side bug — surface a single error envelope so the user
		// sees something instead of silent dead air.
		EmitError("aiBridge: onMessage threw");
	}
}

// Walk up from the binary path to find assets/pluginWebPane/sample.html.
// The plugin DLL lives at .../build/bin/plugins/aiBridge.dylib in dev
// builds; sample.html lives 4 dirs up. Production install would ship
// the bundle next to the dylib via CMake install rules; for dev we
// search upwards (matches the host's fallback behaviour).
// Resolves the absolute filesystem path of the loaded plugin shared
// library itself. CWD-relative lookups are unreliable on macOS when the
// app is launched via Finder (CWD == "/"), so we anchor to our own
// binary's location and walk from there.
std::string PluginSelfDir()
{
#if defined(__APPLE__) || defined(__linux__)
	Dl_info info{};
	if (dladdr(reinterpret_cast<void*>(&PluginSelfDir), &info) != 0 &&
	    info.dli_fname != nullptr) {
		char abs[PATH_MAX];
		if (realpath(info.dli_fname, abs) != nullptr) {
			std::string p(abs);
			const auto slash = p.find_last_of('/');
			if (slash != std::string::npos) return p.substr(0, slash);
		}
	}
#elif defined(_WIN32)
	HMODULE hm = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(&PluginSelfDir), &hm) != 0) {
		char buf[MAX_PATH];
		const DWORD n = GetModuleFileNameA(hm, buf, MAX_PATH);
		if (n > 0 && n < MAX_PATH) {
			std::string p(buf, n);
			const auto slash = p.find_last_of("\\/");
			if (slash != std::string::npos) return p.substr(0, slash);
		}
	}
#endif
	return std::string();
}

bool FileExists(const std::string& path)
{
	if (path.empty()) return false;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) return false;
	std::fclose(f);
	return true;
}

std::string LocateSampleBundle()
{
	// Anchor on our own .bundle/.dylib/.dll path, then walk up the
	// tree looking for assets/pluginWebPane/sample.html. The dev layout
	// puts the binary at build/bin/plugins/libaiBridge.bundle — six
	// `..` levels reach the repo root. Installed layout puts assets
	// next to the binary directory or one level up. Try both.
	std::string base = PluginSelfDir();
	if (!base.empty()) {
		static const char* suffixes[] = {
			"/assets/pluginWebPane/sample.html",
			"/../assets/pluginWebPane/sample.html",
			"/../../assets/pluginWebPane/sample.html",
			"/../../../assets/pluginWebPane/sample.html",
			"/../../../../assets/pluginWebPane/sample.html",
			"/../../../../../assets/pluginWebPane/sample.html",
			"/../../../../../../assets/pluginWebPane/sample.html",
			"/sample.html",
		};
		for (const char* s : suffixes) {
			std::string candidate = base + s;
			if (FileExists(candidate)) {
#if defined(__APPLE__) || defined(__linux__)
				char abs[PATH_MAX];
				if (realpath(candidate.c_str(), abs) != nullptr) {
					return std::string(abs);
				}
#endif
				return candidate;
			}
		}
	}

	// Last-resort CWD fallback (kept for `codeRunner` / unit-test
	// scenarios where CWD === repo root).
	static const char* cwdCandidates[] = {
		"./assets/pluginWebPane/sample.html",
		"../assets/pluginWebPane/sample.html",
		"../../assets/pluginWebPane/sample.html",
	};
	for (const char* c : cwdCandidates) {
		if (FileExists(c)) {
#if defined(__APPLE__) || defined(__linux__)
			char abs[PATH_MAX];
			if (realpath(c, abs) != nullptr) return std::string(abs);
#endif
			return std::string(c);
		}
	}
	return std::string();
}

// Provider Query — not used by Phase 2 routing (plugin handles
// chat.send via OnPaneMessage end-to-end), but the registry requires a
// non-null pointer. Return success so future kernel-side routing can
// dispatch through here.
int ProviderQuery(const char* /*requestJson*/, const char* /*requestId*/, void* /*ud*/)
{
	return 0;
}

const char* kProviderModes[] = { "chat", "agent", nullptr };

} // namespace

static const ibPluginInfo s_info = {
	IB_PLUGIN_ABI_VERSION,
	"aiBridge",
	"1.0.0",
	"First-party LLM bridge — OpenAI-compatible HTTP, BYOK token, sample WebView.",
	"Open Enterprise Solutions"
};

extern "C" {

OES_PLUGIN_EXPORT const ibPluginInfo* oes_plugin_info(void)
{
	return &s_info;
}

OES_PLUGIN_EXPORT int oes_plugin_initialize(const ibHostAPI* host)
{
	g_host = host;
	if (g_host == nullptr) return 0;
	// SEC-P2-1: clear the shutdown latch from any prior load cycle. Static
	// state persists across dlclose+dlopen on macOS/Linux when the dynamic
	// linker caches the image; without this the next session's workers
	// would short-circuit on the inherited "true".
	g_shuttingDown.store(false);
	g_host->Log("aiBridge: initializing (ABI v4)", 0);

	// Cache all env keys we'll need at runtime. The host's ReadPluginEnv
	// is gated by a thread-local plugin id that is ONLY valid inside
	// this init call; worker threads spawned for chat requests cannot
	// reach the env any other way. Read once + remember.
	static const char* kCachedEnvKeys[] = {
		"TOKEN", "PROTOCOL", "ENDPOINT", "MODEL",
		"MODEL_FAST", "MODEL_QUALITY",
		"TENANT", "LOCALE",
		"PUGI_BASE_URL", "PUGI_OES_API_KEY", "PUGI_TENANT_TOKEN",
		"PUGI_TENANT_ID", "PUGI_TENANT", "PUGI_OES_LOCALE",
		nullptr
	};
	PrimeEnvCache(kCachedEnvKeys);

	// Register as AI provider so the editor's HasAIProviderFor("chat")
	// gate flips ON and the right-click submenu enables.
	ibPluginAIProvider prov{};
	prov.providerId     = "aiBridge";
	prov.displayName    = "AI Assistant";
	prov.iconPath       = nullptr;
	prov.supportedModes = kProviderModes;
	prov.Query          = &ProviderQuery;
	prov.Cancel         = nullptr;
	prov.ListModels     = nullptr;
	prov.userData       = nullptr;
	g_host->RegisterAIProvider(&prov);

	// Register WebView pane. The host will Show() it on first AI Chat
	// click via the Tools menu fallback path.
	const std::string selfDir = PluginSelfDir();
	if (!selfDir.empty()) {
		const std::string msg = "aiBridge: plugin dir = " + selfDir;
		g_host->Log(msg.c_str(), 0);
	}
	const std::string bundle = LocateSampleBundle();
	if (bundle.empty()) {
		g_host->Log("aiBridge: sample.html not found — pane not registered. "
		              "Expected at <pluginDir>/sample.html or relative to repo root.", 1);
		return 0;
	}
	{
		const std::string msg = "aiBridge: sample.html = " + bundle;
		g_host->Log(msg.c_str(), 0);
	}
	g_host->RegisterWebPane(g_paneId, "AI Assistant", bundle.c_str(),
	                          &OnPaneMessage, nullptr);
	g_host->Log("aiBridge: pane registered as aiBridge.chat", 0);
	return 0;
}

OES_PLUGIN_EXPORT void oes_plugin_shutdown(void)
{
	// SEC-P0-4: set the shutdown flag BEFORE the join loop so any worker
	// blocked inside cli.Post()'s receiver callback returns false on the
	// next read tick and unblocks. detach() was racy — once this function
	// returned, g_host became null while a worker could still be mid-emit.
	g_shuttingDown.store(true);
	// Trip every in-flight cancel token too so the SSE receiver's
	// cancelTok->load() short-circuits on the next chunk. Belt-and-braces
	// with g_shuttingDown; either path stops the worker.
	{
		std::lock_guard<std::mutex> lk(g_cancelMu);
		for (auto& kv : g_cancelTokens) {
			if (kv.second) kv.second->store(true);
		}
	}
	// Avoid httplib::Client::stop() here: on macOS it can abort inside
	// close_socket() when the request is already completing. Workers use
	// short request timeouts, so shutdown waits for natural completion.
	std::vector<std::thread> drain;
	{
		std::lock_guard<std::mutex> lk(g_workersMu);
		drain.swap(g_workers);
	}
	for (auto& t : drain) {
		if (t.joinable()) t.join();
	}
	// g_host nulled AFTER the join so workers can still safely call
	// WebPaneSend in any wind-down path before they observe g_shuttingDown.
	if (g_host) g_host->Log("aiBridge: shutting down", 0);
	g_host = nullptr;
}

} // extern "C"
