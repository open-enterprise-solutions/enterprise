/////////////////////////////////////////////////////////////////////////////
// queryServer — HTTP front-end for the local RAG index.
//
// Endpoints:
//   POST /query   {"text": "...", "topK": 5}
//                 → {"ok":true, "results":[{chunk, source, score}, ...]}
//   POST /llm     {"prompt": "...", "model": "qwen2.5:14b-instruct"}
//                 → forwarded to local Ollama /api/chat, response repacked
//                   in Pugi-llm_query-shaped envelope:
//                   {"ok":true, "content":[{"type":"text","text":"..."}],
//                    "structuredContent":{"ok":true, "model":...,
//                    "_source":"local-rag-fallback"}}
//   GET  /health  → 200 if index loaded AND Ollama Ping succeeds,
//                   else 503 with stderr-style envelope.
//
// Binds 127.0.0.1 only — air-gapped tier still wants strict locality.
// No CORS — clients are oes-mcp + dev tooling, both on localhost.
/////////////////////////////////////////////////////////////////////////////

#ifndef OES_RAG_LOCAL_QUERY_SERVER_HPP
#define OES_RAG_LOCAL_QUERY_SERVER_HPP

#include <string>

namespace oesRagLocal {

class RagIndex;

struct ServerConfig {
	int         port = 11700;
	std::string ollamaUrl;   // for /llm proxy + /health probe
	std::string indexPath;   // reported in /health body for diagnostics
};

// Block on httplib::Server::listen. Returns the OS exit code (0 on clean
// shutdown via SIGINT, non-zero on bind / listen failure).
int RunQueryServer(const ServerConfig& cfg, RagIndex& index);

} // namespace oesRagLocal

#endif // OES_RAG_LOCAL_QUERY_SERVER_HPP
