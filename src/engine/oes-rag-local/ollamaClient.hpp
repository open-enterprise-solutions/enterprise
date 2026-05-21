/////////////////////////////////////////////////////////////////////////////
// ollamaClient — HTTP client for Ollama (https://ollama.com) endpoints.
// Wraps the minimal surface oes-rag-local needs:
//
//   - POST /api/embeddings  {"model": ..., "prompt": ...}
//     → returns 768-dim float vector for nomic-embed-text. Used by the
//       ingest pipeline (v2 — v1 ships lexical-only and never calls this).
//
//   - POST /api/chat  {"model": ..., "messages": [{"role":"user","content":...}]}
//     → returns assistant response. Used by the /llm proxy on the query
//       server. v1 = non-streaming (single JSON response); streaming chunks
//       are deferred to v2.
//
//   - GET /api/tags
//     → used by `oes-rag-local health` to verify Ollama is reachable.
//
// Throw-by-value contract: no exceptions are thrown. All failures surface
// as `false` returns with an out-parameter `*errOut` populated.
/////////////////////////////////////////////////////////////////////////////

#ifndef OES_RAG_LOCAL_OLLAMA_CLIENT_HPP
#define OES_RAG_LOCAL_OLLAMA_CLIENT_HPP

#include "3rdparty/nlohmann/json.hpp"

#include <string>
#include <vector>

namespace oesRagLocal {

class OllamaClient {
public:
	explicit OllamaClient(std::string baseUrl);

	// GET /api/tags — lightweight liveness probe. Returns true on 2xx,
	// false on any transport / non-2xx error with *errOut populated.
	bool Ping(std::string* errOut) const;

	// POST /api/embeddings. On success fills *vecOut with the embedding.
	// v1 callers do NOT invoke this — the ingest pipeline checks
	// `Ping` first and only calls Embed when Ollama is reachable;
	// otherwise it writes zero-vectors and the query path falls back to
	// lexical matching. This kept compile-time/runtime deps trivial.
	bool Embed(const std::string& model,
	           const std::string& text,
	           std::vector<float>* vecOut,
	           std::string* errOut) const;

	// POST /api/chat. Non-streaming. Returns the raw response JSON on
	// success. v1 caller (query server /llm endpoint) passes this back
	// untouched to mimic the Pugi llm_query response shape.
	bool Chat(const std::string& model,
	          const std::string& prompt,
	          nlohmann::json* responseOut,
	          std::string* errOut) const;

	const std::string& BaseUrl() const { return m_baseUrl; }

private:
	std::string m_baseUrl;   // e.g. "http://localhost:11434"
};

} // namespace oesRagLocal

#endif // OES_RAG_LOCAL_OLLAMA_CLIENT_HPP
