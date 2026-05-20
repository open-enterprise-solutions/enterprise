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

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
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

// Per-request cancel tokens for the Stop Generation feature. Pane fires
// agent.cancel with the original requestId; the worker polls between
// SSE chunks. shared_ptr lets the worker capture by value so erase from
// the map can't dangle.
std::mutex                                                          g_cancelMu;
std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> g_cancelTokens;

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

// Sends the chat.end envelope so the pane closes its streaming row +
// renders the footer cost meter.
void EmitEnd(const std::string& requestId, const std::string& model,
              int tokensIn, int tokensOut)
{
	if (g_host == nullptr || g_host->WebPaneSend == nullptr) return;
	nlohmann::json env;
	env["kind"]      = "chat.end";
	env["requestId"] = requestId;
	env["meta"]      = nlohmann::json::object();
	env["meta"]["model"] = model;
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
                     const std::shared_ptr<std::atomic<bool>>& cancelTok)
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
	EmitEnd(requestId, model, approxTokens, approxTokens);
}

// Pugi-MCP request path. Talks to mcp.pugi.io / Anvil with the request
// shape that backend actually expects:
//   POST  <endpoint>
//   H:    Authorization: Bearer <token>
//         X-Pugi-Tenant: <tenant-uuid>
//         Content-Type: application/json
//   Body: {"name":"llm_query","input":{"prompt":"...","locale":"uk-UA"}}
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
                        const std::shared_ptr<std::atomic<bool>>& cancelTok)
{
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

	httplib::Client cli(base);
	cli.set_connection_timeout(15);
	cli.set_read_timeout(120);
	// SEC-P1-5: never follow cross-origin redirects with Authorization +
	// X-Pugi-Tenant attached. cpp-httplib's follower replays full headers
	// to the redirect target — a redirect to an attacker host would leak
	// the bearer. Surface 3xx as an explicit error instead.
	cli.set_follow_location(false);

	nlohmann::json body;
	body["name"]            = "llm_query";
	body["input"]           = nlohmann::json::object();
	body["input"]["prompt"] = prompt;
	body["input"]["locale"] = locale.empty() ? std::string("uk-UA") : locale;

	httplib::Headers headers = {
		{ "Authorization", "Bearer " + token },
		{ "X-Pugi-Tenant", tenant            },
		{ "Content-Type",  "application/json" },
		{ "Accept",        "application/json" },
	};

	const std::string bodyStr = body.dump();
	auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
	if (!res) {
		EmitError("aiBridge[pugi-mcp]: HTTP failed — " +
		           std::string(httplib::to_string(res.error())));
		return;
	}
	// SEC-P1-5: 3xx is no longer auto-followed. Refuse rather than re-
	// emit Authorization to a different origin.
	if (res->status >= 300 && res->status < 400) {
		EmitError("aiBridge[pugi-mcp]: server returned redirect; bearer not forwarded for security");
		return;
	}
	if (res->status >= 400) {
		// SEC-P0-1: redact body — OpenAI/Anvil error payloads echo back
		// the bearer token and/or original request body. Unredacted slice
		// goes to /tmp/oes-diag.log only when OES_AI_DEBUG_UNSAFE=1.
		DiagWriteUnsafeBody("pugi-mcp", res->status, res->body);
		EmitError("aiBridge[pugi-mcp]: HTTP " + std::to_string(res->status));
		return;
	}

	// Extract result.content. Tolerate result-as-string fallback for
	// any backend version that returns a flat shape.
	std::string content;
	std::string respModel = "pugi-mcp";
	auto parsed = nlohmann::json::parse(res->body, nullptr, /*allow_exceptions=*/false);
	if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("result")) {
		const auto& r = parsed["result"];
		if (r.is_object()) {
			if (r.contains("content") && r["content"].is_string()) {
				content = r["content"].get<std::string>();
			}
			if (r.contains("model") && r["model"].is_string()) {
				respModel = r["model"].get<std::string>();
			}
		} else if (r.is_string()) {
			content = r.get<std::string>();
		}
	}
	if (content.empty()) {
		// Server returned something we can't unwrap — emit raw so the
		// user sees the actual diagnostic, not a silent failure.
		content = res->body;
	}

	const int approxTokens = static_cast<int>(content.size() / 4);
	EmitTypewriter(requestId, content, respModel, approxTokens, cancelTok);
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
	const auto [base, path] = SplitUrl(endpoint);
	if (base.empty()) {
		EmitError("aiBridge[triple-review]: malformed ENDPOINT URL: " + endpoint);
		return;
	}

	httplib::Client cli(base);
	cli.set_connection_timeout(15);
	// Triple-review fans out across multiple models server-side and can
	// reasonably take longer than a single llm_query — bump the read
	// timeout. cancelTok still lets the user stop sooner.
	cli.set_read_timeout(180);
	// SEC-P1-5: refuse redirects so bearer + X-Pugi-Tenant aren't replayed.
	cli.set_follow_location(false);

	nlohmann::json body;
	body["name"]              = "triple_review";
	body["input"]             = nlohmann::json::object();
	body["input"]["code"]     = code;
	body["input"]["language"] = language.empty() ? std::string("CES") : language;
	body["input"]["locale"]   = locale.empty()   ? std::string("uk-UA") : locale;

	httplib::Headers headers = {
		{ "Authorization", "Bearer " + token },
		{ "X-Pugi-Tenant", tenant            },
		{ "Content-Type",  "application/json" },
		{ "Accept",        "application/json" },
	};

	const std::string bodyStr = body.dump();
	auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
	if (!res) {
		EmitError("aiBridge[triple-review]: HTTP failed — " +
		           std::string(httplib::to_string(res.error())));
		return;
	}
	// SEC-P1-5: refuse 3xx — see RunPugiMcpRequest for the rationale.
	if (res->status >= 300 && res->status < 400) {
		EmitError("aiBridge[triple-review]: server returned redirect; bearer not forwarded for security");
		return;
	}
	if (res->status >= 400) {
		// SEC-P0-1: redact body — see RunPugiMcpRequest.
		DiagWriteUnsafeBody("triple-review", res->status, res->body);
		EmitError("aiBridge[triple-review]: HTTP " + std::to_string(res->status));
		return;
	}

	// Honour an inline cancel — the work is done server-side, but the
	// user may have hit Stop before the response landed. Skip the
	// envelope emit and let chat.end clean up.
	if (cancelTok && cancelTok->load()) {
		EmitCancelledEnd(requestId, "triple_review", 0, 0);
		return;
	}

	auto parsed = nlohmann::json::parse(res->body, nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object() ||
	    !parsed.contains("result") || !parsed["result"].is_object()) {
		EmitError("aiBridge[triple-review]: malformed response envelope");
		return;
	}
	const auto& result = parsed["result"];

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
	EmitEnd(requestId, "triple_review", tokensIn, tokensOut);
}

// Worker thread body — performs the HTTP POST + streams chunks back to
// the pane. Owns its own httplib::Client; no shared state. cancelTok
// is the per-request stop signal; receiver polls it between SSE frames
// so a pane-fired agent.cancel halts further delta emits.
void RunChatRequest(std::string requestId, std::string prompt, std::string mode,
                    std::shared_ptr<std::atomic<bool>> cancelTok)
{
	const std::string token    = ReadEnv("TOKEN",    "");
	const std::string protocol = ReadEnv("PROTOCOL", "openai");  // or "pugi-mcp"
	const std::string endpoint = ReadEnv("ENDPOINT",
	                                       protocol == "pugi-mcp"
	                                          ? "https://mcp.pugi.io/api/oes-mcp/invoke"
	                                          : "https://api.openai.com/v1/chat/completions");
	const std::string model    = ReadEnv("MODEL",    "gpt-4o-mini");

	if (token.empty()) {
		EmitError("aiBridge: no TOKEN in plugin env. Set it via Tools → Plugins → Edit API token.");
		return;
	}

	// Pugi-MCP branch — bypass the OpenAI SSE path entirely.
	if (protocol == "pugi-mcp") {
		const std::string tenant = ReadEnv("TENANT", "");
		if (tenant.empty()) {
			EmitError("aiBridge[pugi-mcp]: TENANT env var is required (tenant UUID).");
			return;
		}
		const std::string locale = ReadEnv("LOCALE", "uk-UA");
		const std::string ridCopy = requestId;
		RunPugiMcpRequest(std::move(requestId), std::move(prompt),
		                    endpoint, token, tenant, locale, cancelTok);
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

	httplib::Client cli(base);
	cli.set_connection_timeout(15);
	cli.set_read_timeout(120);
	// SEC-P1-5: refuse 3xx — see RunPugiMcpRequest.
	cli.set_follow_location(false);

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

	httplib::Headers headers = {
		{ "Authorization", "Bearer " + token },
		{ "Content-Type",  "application/json" },
		{ "Accept",        "text/event-stream" },
	};

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
	auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json", receiver);

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
		EmitError("aiBridge: HTTP " + std::to_string(res->status));
		return;
	}
	const int tokensIn = static_cast<int>(prompt.size() / 4);
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

		if (kind != "chat.send" && kind != "editor.skill") return;

		std::string prompt = j.value("prompt", std::string());
		const std::string op = j.value("op", std::string());

		// editor.skill op="triple-review" — call the Anvil triple_review
		// MCP tool directly (different request shape than llm_query) so
		// the pane gets structured per-reviewer findings + verdict
		// instead of free-text. Worker spawned inline because the path
		// uses pugi-mcp credentials regardless of PROTOCOL.
		if (kind == "editor.skill" && op == "triple-review") {
			const std::string token = ReadEnv("TOKEN", "");
			const std::string endpoint = ReadEnv("ENDPOINT",
			    "https://mcp.pugi.io/api/oes-mcp/invoke");
			const std::string tenant   = ReadEnv("TENANT", "");
			const std::string locale   = ReadEnv("LOCALE", "uk-UA");
			if (token.empty() || tenant.empty()) {
				EmitError("aiBridge[triple-review]: requires TOKEN + TENANT in env.");
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

		if (kind == "editor.skill") {
			const std::string code = j.value("code", std::string());
			// Wrap the skill into a chat prompt with a stable preamble.
			static const char* opLabels[][2] = {
			    {"explain", "Объясни этот код."},
			    {"review",  "Проверь код на ошибки и улучши его."},
			    {"fix",     "Найди ошибки и предложи исправление."},
			    {"doc",     "Сгенерируй документирующий комментарий."},
			    {"send",    "Прокомментируй этот код."},
			    {nullptr,   nullptr},
			};
			const char* preamble = "Прокомментируй этот код.";
			for (auto* row = opLabels[0]; row[0] != nullptr; row += 2) {
				if (op == row[0]) { preamble = row[1]; break; }
			}
			prompt = std::string(preamble) + "\n\n```\n" + code + "\n```";
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

		// Allocate the cancel token before spawning so a near-instant
		// agent.cancel still finds a registered entry.
		auto cancelTok = AllocCancelToken(requestId);

		// Spawn a detached worker so the host's UI thread returns
		// immediately. Track the thread so shutdown can join + drain.
		{
			std::lock_guard<std::mutex> lk(g_workersMu);
			g_workers.emplace_back(RunChatRequest, requestId, prompt, mode, cancelTok);
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
	g_host->Log("aiBridge: initializing (ABI v4)", 0);

	// Cache all env keys we'll need at runtime. The host's ReadPluginEnv
	// is gated by a thread-local plugin id that is ONLY valid inside
	// this init call; worker threads spawned for chat requests cannot
	// reach the env any other way. Read once + remember.
	static const char* kCachedEnvKeys[] = {
		"TOKEN", "PROTOCOL", "ENDPOINT", "MODEL",
		"TENANT", "LOCALE",
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
