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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// OES_PLUGIN_EXPORT comes from backend/plugin/pluginApi.h.

namespace {

const ibHostAPI* g_host    = nullptr;
const char*      g_paneId  = "aiBridge.chat";

std::mutex                   g_workersMu;
std::vector<std::thread>     g_workers;
std::atomic<bool>            g_shuttingDown{false};

// Read an env value from the per-plugin BYOK store, falling back to a
// caller-supplied default. Returns std::string for ergonomics; the
// raw buffer the host hands us is freed via FreeBuffer right after.
std::string ReadEnv(const char* key, const char* fallback)
{
	if (g_host == nullptr || g_host->ReadPluginEnv == nullptr) {
		return fallback ? std::string(fallback) : std::string();
	}
	char* buf = g_host->ReadPluginEnv(key);
	if (buf == nullptr) return fallback ? std::string(fallback) : std::string();
	std::string out(buf);
	if (g_host->FreeBuffer) g_host->FreeBuffer(buf);
	return out;
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

// Worker thread body — performs the HTTP POST + streams chunks back to
// the pane. Owns its own httplib::Client; no shared state.
void RunChatRequest(std::string requestId, std::string prompt, std::string mode)
{
	const std::string token    = ReadEnv("TOKEN",    "");
	const std::string endpoint = ReadEnv("ENDPOINT",
	                                       "https://api.openai.com/v1/chat/completions");
	const std::string model    = ReadEnv("MODEL",    "gpt-4o-mini");

	if (token.empty()) {
		EmitError("aiBridge: no TOKEN in plugin env. Set it via Tools → Plugins → Edit API token.");
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
	cli.set_follow_location(true);

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

	auto receiver = [&](const char* data, size_t len) {
		if (g_shuttingDown.load()) return false;
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

	if (!res) {
		EmitError("aiBridge: HTTP failed — " +
		           std::string(httplib::to_string(res.error())));
		return;
	}
	if (res->status >= 400) {
		EmitError("aiBridge: HTTP " + std::to_string(res->status) +
		           " — " + res->body.substr(0, 256));
		return;
	}
	const int tokensIn = static_cast<int>(prompt.size() / 4);
	EmitEnd(requestId, model, tokensIn, tokensOut);
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
		if (kind != "chat.send" && kind != "editor.skill") return;

		std::string prompt = j.value("prompt", std::string());
		const std::string op = j.value("op", std::string());
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

		const std::string requestId = "req-" + std::to_string(
		    std::chrono::steady_clock::now().time_since_epoch().count());
		const std::string mode = j.value("mode", std::string("chat"));

		// Spawn a detached worker so the host's UI thread returns
		// immediately. Track the thread so shutdown can join + drain.
		{
			std::lock_guard<std::mutex> lk(g_workersMu);
			g_workers.emplace_back(RunChatRequest, requestId, prompt, mode);
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
std::string LocateSampleBundle()
{
	// Lazy: rely on the same env the user typed in plugins.json5 dialog?
	// No — host's CallWebPaneRegister wants an absolute path. We hard-
	// code the dev layout and let install rules override later.
	static const char* candidates[] = {
		"./assets/pluginWebPane/sample.html",
		"../assets/pluginWebPane/sample.html",
		"../../assets/pluginWebPane/sample.html",
		"../../../assets/pluginWebPane/sample.html",
		"../../../../assets/pluginWebPane/sample.html",
		"../../../../../assets/pluginWebPane/sample.html",
		"../../../../../../assets/pluginWebPane/sample.html",
	};
	for (const char* c : candidates) {
		FILE* f = std::fopen(c, "rb");
		if (f != nullptr) {
			std::fclose(f);
			char abs[PATH_MAX];
			if (realpath(c, abs) != nullptr) return std::string(abs);
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
	const std::string bundle = LocateSampleBundle();
	if (bundle.empty()) {
		g_host->Log("aiBridge: sample.html not found — pane not registered", 1);
		return 0;
	}
	g_host->RegisterWebPane(g_paneId, "AI Assistant", bundle.c_str(),
	                          &OnPaneMessage, nullptr);
	return 0;
}

OES_PLUGIN_EXPORT void oes_plugin_shutdown(void)
{
	g_shuttingDown.store(true);
	std::vector<std::thread> drain;
	{
		std::lock_guard<std::mutex> lk(g_workersMu);
		drain.swap(g_workers);
	}
	for (auto& t : drain) {
		if (t.joinable()) t.detach(); // best-effort; httplib client owns the socket
	}
	if (g_host) g_host->Log("aiBridge: shutting down", 0);
	g_host = nullptr;
}

} // extern "C"
