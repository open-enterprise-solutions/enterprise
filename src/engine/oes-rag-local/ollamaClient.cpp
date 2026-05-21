/////////////////////////////////////////////////////////////////////////////
// ollamaClient.cpp — implementation. Uses cpp-httplib (already vendored).
//
// Timeouts: ping=2s, embed=15s, chat=60s. The chat timeout matches what
// aiBridge uses for llm_query — a quick query gets a few-token answer in
// under 5s, but reasoning-heavy prompts on a CPU-only Ollama setup can
// genuinely take 30-60s.
//
// URL parsing is intentionally minimal: we split scheme+host:port from
// path the same way tools.cpp's PugiSplitUrl does. Callers pass
// `http://localhost:11434` (the Ollama default), `https://...` works
// only when we were built with CPPHTTPLIB_OPENSSL_SUPPORT.
/////////////////////////////////////////////////////////////////////////////

#include "ollamaClient.hpp"

#include "3rdparty/cpp-httplib/httplib.h"

#include <utility>

namespace oesRagLocal {

namespace {

// SplitUrl — minimal scheme+host[:port] / path split. Returns {base, path}.
// On unparseable input returns {"", ""}. Mirrors PugiSplitUrl in
// src/engine/mcp-server/tools.cpp to keep behaviour predictable across
// our HTTP touch-points.
std::pair<std::string, std::string> SplitUrl(const std::string& url)
{
	const auto schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return {"", ""};
	const auto pathStart = url.find('/', schemeEnd + 3);
	if (pathStart == std::string::npos) return {url, "/"};
	return {url.substr(0, pathStart), url.substr(pathStart)};
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
using HttpClient = httplib::Client;
#else
using HttpClient = httplib::Client;
#endif

} // namespace

OllamaClient::OllamaClient(std::string baseUrl)
	: m_baseUrl(std::move(baseUrl))
{
	// Strip trailing slash so concatenation with paths stays clean.
	if (!m_baseUrl.empty() && m_baseUrl.back() == '/') {
		m_baseUrl.pop_back();
	}
}

bool OllamaClient::Ping(std::string* errOut) const
{
	const auto [base, ignored] = SplitUrl(m_baseUrl + "/api/tags");
	if (base.empty()) {
		if (errOut) *errOut = "malformed Ollama URL: " + m_baseUrl;
		return false;
	}
	httplib::Client cli(base);
	cli.set_connection_timeout(2);
	cli.set_read_timeout(2);

	auto res = cli.Get("/api/tags");
	if (!res) {
		if (errOut) *errOut = std::string("transport: ")
		                       + httplib::to_string(res.error());
		return false;
	}
	if (res->status < 200 || res->status >= 300) {
		if (errOut) *errOut = "Ollama returned HTTP "
		                       + std::to_string(res->status);
		return false;
	}
	return true;
}

bool OllamaClient::Embed(const std::string& model,
                         const std::string& text,
                         std::vector<float>* vecOut,
                         std::string* errOut) const
{
	const auto [base, ignored] = SplitUrl(m_baseUrl + "/api/embeddings");
	if (base.empty()) {
		if (errOut) *errOut = "malformed Ollama URL: " + m_baseUrl;
		return false;
	}
	httplib::Client cli(base);
	cli.set_connection_timeout(5);
	cli.set_read_timeout(15);

	nlohmann::json body;
	body["model"]  = model;
	body["prompt"] = text;
	const std::string bodyStr = body.dump();

	auto res = cli.Post("/api/embeddings", bodyStr, "application/json");
	if (!res) {
		if (errOut) *errOut = std::string("transport: ")
		                       + httplib::to_string(res.error());
		return false;
	}
	if (res->status < 200 || res->status >= 300) {
		if (errOut) *errOut = "Ollama embed returned HTTP "
		                       + std::to_string(res->status);
		return false;
	}
	auto parsed = nlohmann::json::parse(res->body, nullptr,
	                                    /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object() ||
	    !parsed.contains("embedding") || !parsed["embedding"].is_array()) {
		if (errOut) *errOut = "Ollama embed: response missing 'embedding' array";
		return false;
	}
	vecOut->clear();
	vecOut->reserve(parsed["embedding"].size());
	for (const auto& v : parsed["embedding"]) {
		if (v.is_number()) vecOut->push_back(v.get<float>());
	}
	return !vecOut->empty();
}

bool OllamaClient::Chat(const std::string& model,
                        const std::string& prompt,
                        nlohmann::json* responseOut,
                        std::string* errOut) const
{
	const auto [base, ignored] = SplitUrl(m_baseUrl + "/api/chat");
	if (base.empty()) {
		if (errOut) *errOut = "malformed Ollama URL: " + m_baseUrl;
		return false;
	}
	httplib::Client cli(base);
	cli.set_connection_timeout(5);
	cli.set_read_timeout(60);

	nlohmann::json body;
	body["model"]  = model;
	body["stream"] = false;
	nlohmann::json msg;
	msg["role"]    = "user";
	msg["content"] = prompt;
	body["messages"] = nlohmann::json::array({ msg });
	const std::string bodyStr = body.dump();

	auto res = cli.Post("/api/chat", bodyStr, "application/json");
	if (!res) {
		if (errOut) *errOut = std::string("transport: ")
		                       + httplib::to_string(res.error());
		return false;
	}
	if (res->status < 200 || res->status >= 300) {
		if (errOut) *errOut = "Ollama chat returned HTTP "
		                       + std::to_string(res->status);
		return false;
	}
	auto parsed = nlohmann::json::parse(res->body, nullptr,
	                                    /*allow_exceptions=*/false);
	if (parsed.is_discarded()) {
		if (errOut) *errOut = "Ollama chat: non-JSON response";
		return false;
	}
	if (responseOut) *responseOut = std::move(parsed);
	return true;
}

} // namespace oesRagLocal
