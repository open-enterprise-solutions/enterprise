/////////////////////////////////////////////////////////////////////////////
// queryServer.cpp — cpp-httplib server. Bound to 127.0.0.1 only.
//
// Response shape rationale: /llm mirrors the Pugi llm_query envelope so
// the oes-mcp degradation chain can pass the response straight through
// to its caller after stamping `_source = "local-rag-fallback"`.
//
// Signal handling: we install SIGINT/SIGTERM handlers that stop the
// server. The httplib server itself isn't async-signal-safe, but
// stop() is a simple bool toggle in modern cpp-httplib builds.
/////////////////////////////////////////////////////////////////////////////

#include "queryServer.hpp"

#include "ollamaClient.hpp"
#include "ragIndex.hpp"

#include "3rdparty/cpp-httplib/httplib.h"
#include "3rdparty/nlohmann/json.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <string>

namespace oesRagLocal {

namespace {

httplib::Server* g_serverRef = nullptr;

#if defined(_WIN32)
BOOL WINAPI ConsoleCtrlHandler(DWORD /*type*/) {
	if (g_serverRef) g_serverRef->stop();
	return TRUE;
}
#else
void PosixSignal(int /*sig*/) {
	if (g_serverRef) g_serverRef->stop();
}
#endif

nlohmann::json TextEnvelope(const std::string& text, bool ok)
{
	nlohmann::json env;
	env["ok"] = ok;
	nlohmann::json content = nlohmann::json::array();
	nlohmann::json item;
	item["type"] = "text";
	item["text"] = text;
	content.push_back(std::move(item));
	env["content"] = std::move(content);
	return env;
}

} // namespace

int RunQueryServer(const ServerConfig& cfg, RagIndex& index)
{
	httplib::Server svr;
	g_serverRef = &svr;
#if defined(_WIN32)
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
	std::signal(SIGINT,  PosixSignal);
	std::signal(SIGTERM, PosixSignal);
#endif

	svr.set_tcp_nodelay(true);
	svr.set_keep_alive_max_count(1);
	svr.set_read_timeout(5);
	svr.set_write_timeout(10);

	// GET /health — 200 if index loaded AND Ollama reachable, else 503.
	svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
		nlohmann::json body;
		body["index"]      = cfg.indexPath;
		body["chunks"]     = index.Size();
		body["model"]      = index.Model();
		body["loaded"]     = index.Loaded();

		bool ollamaUp = false;
		std::string ollamaErr;
		if (!cfg.ollamaUrl.empty()) {
			OllamaClient probe(cfg.ollamaUrl);
			ollamaUp = probe.Ping(&ollamaErr);
		} else {
			ollamaErr = "no ollama URL configured";
		}
		body["ollama"]     = cfg.ollamaUrl;
		body["ollamaUp"]   = ollamaUp;
		body["ollamaErr"]  = ollamaErr;

		const bool healthy = index.Loaded() && index.Size() > 0 && ollamaUp;
		body["ok"]         = healthy;
		res.status         = healthy ? 200 : 503;
		res.set_content(body.dump(), "application/json");
	});

	// POST /query — brute-force lexical retrieval. v1.
	svr.Post("/query", [&](const httplib::Request& req, httplib::Response& res) {
		auto parsed = nlohmann::json::parse(req.body, nullptr,
		                                    /*allow_exceptions=*/false);
		if (parsed.is_discarded() || !parsed.is_object() ||
		    !parsed.contains("text") || !parsed["text"].is_string()) {
			res.status = 400;
			res.set_content(
				TextEnvelope("query: 'text' is required (string)", false).dump(),
				"application/json");
			return;
		}
		const std::string text = parsed["text"].get<std::string>();
		int topK = 5;
		if (parsed.contains("topK") && parsed["topK"].is_number_integer()) {
			topK = std::max(1, parsed["topK"].get<int>());
		}

		const auto hits = index.SearchLexical(text, topK);
		nlohmann::json results = nlohmann::json::array();
		for (const auto& h : hits) {
			const auto& c = index.Chunks()[h.chunkIndex];
			nlohmann::json r;
			r["id"]     = c.id;
			r["source"] = c.source;
			r["score"]  = h.score;
			// Truncate `text` so the wire response stays small; full
			// chunk text is still on disk in the index for offline review.
			r["chunk"]  = c.text.size() > 400
			                ? c.text.substr(0, 400) + "..."
			                : c.text;
			results.push_back(std::move(r));
		}

		nlohmann::json env;
		env["ok"]       = true;
		env["query"]    = text;
		env["topK"]     = topK;
		env["count"]    = results.size();
		env["results"]  = std::move(results);
		env["_source"]  = "local-rag-fallback";
		env["mode"]     = index.Model();   // "lexical-only" in v1
		res.status      = 200;
		res.set_content(env.dump(), "application/json");
	});

	// POST /llm — proxy to local Ollama /api/chat, repack envelope.
	svr.Post("/llm", [&](const httplib::Request& req, httplib::Response& res) {
		auto parsed = nlohmann::json::parse(req.body, nullptr,
		                                    /*allow_exceptions=*/false);
		if (parsed.is_discarded() || !parsed.is_object() ||
		    !parsed.contains("prompt") || !parsed["prompt"].is_string()) {
			res.status = 400;
			res.set_content(
				TextEnvelope("llm: 'prompt' is required (string)", false).dump(),
				"application/json");
			return;
		}
		const std::string prompt = parsed["prompt"].get<std::string>();
		const std::string model = (parsed.contains("model") &&
		                           parsed["model"].is_string())
			? parsed["model"].get<std::string>()
			: std::string("qwen2.5:14b-instruct");

		if (cfg.ollamaUrl.empty()) {
			res.status = 503;
			res.set_content(
				TextEnvelope("llm: no ollama URL configured", false).dump(),
				"application/json");
			return;
		}

		OllamaClient cli(cfg.ollamaUrl);
		nlohmann::json chatResp;
		std::string err;
		if (!cli.Chat(model, prompt, &chatResp, &err)) {
			res.status = 502;
			res.set_content(
				TextEnvelope("llm: ollama unreachable — " + err, false).dump(),
				"application/json");
			return;
		}

		// Extract the assistant message — Ollama /api/chat shape:
		//   { "message": { "role": "assistant", "content": "..." }, ... }
		std::string answer;
		if (chatResp.is_object() && chatResp.contains("message") &&
		    chatResp["message"].is_object() &&
		    chatResp["message"].contains("content") &&
		    chatResp["message"]["content"].is_string()) {
			answer = chatResp["message"]["content"].get<std::string>();
		} else {
			answer = chatResp.dump();
		}

		nlohmann::json env = TextEnvelope(answer, true);
		nlohmann::json structured;
		structured["ok"]      = true;
		structured["model"]   = model;
		structured["_source"] = "local-rag-fallback";
		env["structuredContent"] = std::move(structured);
		res.status = 200;
		res.set_content(env.dump(), "application/json");
	});

	std::fprintf(stderr,
		"oes-rag-local serve: listening on 127.0.0.1:%d (ollama=%s, index=%s)\n",
		cfg.port,
		cfg.ollamaUrl.empty() ? "(none)" : cfg.ollamaUrl.c_str(),
		cfg.indexPath.c_str());

	if (!svr.listen("127.0.0.1", cfg.port)) {
		std::fprintf(stderr,
			"oes-rag-local serve: failed to bind 127.0.0.1:%d\n", cfg.port);
		g_serverRef = nullptr;
		return 1;
	}
	g_serverRef = nullptr;
	return 0;
}

} // namespace oesRagLocal
