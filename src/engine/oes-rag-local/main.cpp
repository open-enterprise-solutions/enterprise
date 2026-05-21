/////////////////////////////////////////////////////////////////////////////
// oes-rag-local — main entry point + subcommand dispatcher.
//
// Subcommands:
//   ingest <corpus-dir> [--out <index-path>]
//                  Walk a directory tree of OES/BAS XML files, extract per-
//                  object semantic chunks, optionally embed via Ollama, and
//                  write a flat JSON index (v1) or FAISS index (v2).
//   serve <index-path> [--port <n>] [--ollama <url>]
//                  Load the index and listen for /query, /llm, /health on
//                  localhost (default 11700).
//   health         Probe local Ollama (default http://localhost:11434) and
//                  exit 0 on success, non-zero with structured stderr error
//                  envelope otherwise.
//   --help, -h     Print usage + exit 0.
//
// Designed to be small, self-contained, and air-gap-friendly: no wxWidgets,
// no backend.dll link, no GUI subsystems. Communicates with oes-mcp purely
// over HTTP (oes-mcp picks up OES_MCP_RAG_FALLBACK_URL at runtime).
/////////////////////////////////////////////////////////////////////////////

#include "ingestPipeline.hpp"
#include "ollamaClient.hpp"
#include "queryServer.hpp"
#include "ragIndex.hpp"

#include "3rdparty/nlohmann/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintHelp()
{
	std::fprintf(stderr,
		"oes-rag-local — local RAG fallback for oes-mcp (air-gapped tier)\n"
		"\n"
		"Usage:\n"
		"  oes-rag-local ingest <corpus-dir> [--out <index-path>]\n"
		"      Walk a directory of OES/BAS XML files, extract semantic\n"
		"      chunks, write index file. v1 ships lexical-only (no\n"
		"      embeddings); v2 will round-trip chunks through Ollama.\n"
		"\n"
		"  oes-rag-local serve <index-path> [--port <n>] [--ollama <url>]\n"
		"      Load index, listen on localhost (default port 11700) for:\n"
		"        POST /query  {text, topK}   -> brute-force retrieval\n"
		"        POST /llm    {prompt, model} -> proxy to local Ollama\n"
		"        GET  /health                -> 200 if loaded, else 503\n"
		"\n"
		"  oes-rag-local health [--ollama <url>]\n"
		"      Probe local Ollama tag list. Exit 0 if reachable, non-zero\n"
		"      with structured stderr envelope otherwise.\n"
		"\n"
		"  oes-rag-local --help\n"
		"      This message.\n"
		"\n"
		"Environment:\n"
		"  OES_RAG_LOCAL_PORT       default port for `serve` (default 11700)\n"
		"  OES_RAG_OLLAMA_URL       default Ollama base URL (default\n"
		"                           http://localhost:11434)\n"
		"\n"
		"Wire-up with oes-mcp:\n"
		"  export OES_MCP_RAG_FALLBACK_URL=http://localhost:11700\n"
		"  export OES_MCP_RAG_FALLBACK_TIMEOUT_MS=2000\n"
		"\n"
		"v1 scope:\n"
		"  - Brute-force substring retrieval, no FAISS, no embeddings.\n"
		"  - Single-corpus, in-process index.\n"
		"  - Ollama proxy stub (forwards prompt, no streaming).\n"
		"v2 deferred:\n"
		"  - Real FAISS index over Ollama nomic-embed-text embeddings.\n"
		"  - Multi-corpus, hot-reload, streaming chat.\n");
}

// Parse `--flag <value>` style options from argv starting at index `from`.
// Unknown flags fall through to the caller. Used by each subcommand to pull
// its optional knobs without a full opt-parser dependency.
struct ParsedOpts {
	std::string outPath;     // --out
	std::string indexPath;   // positional after subcommand for serve/(positional first)
	std::string ollamaUrl;   // --ollama
	int         port = 0;    // --port
	std::vector<std::string> positional;
};

ParsedOpts ParseArgs(int argc, char** argv, int from)
{
	ParsedOpts out;
	for (int i = from; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--out" && i + 1 < argc) { out.outPath = argv[++i]; }
		else if (a == "--ollama" && i + 1 < argc) { out.ollamaUrl = argv[++i]; }
		else if (a == "--port" && i + 1 < argc) { out.port = std::atoi(argv[++i]); }
		else if (!a.empty() && a[0] == '-') {
			std::fprintf(stderr, "oes-rag-local: unknown flag '%s'\n", a.c_str());
		}
		else { out.positional.push_back(a); }
	}
	return out;
}

std::string EnvOr(const char* key, const std::string& fallback)
{
	const char* v = std::getenv(key);
	return (v && *v) ? std::string(v) : fallback;
}

int CmdHealth(const ParsedOpts& opts)
{
	const std::string ollamaUrl = !opts.ollamaUrl.empty()
		? opts.ollamaUrl
		: EnvOr("OES_RAG_OLLAMA_URL", "http://localhost:11434");

	oesRagLocal::OllamaClient cli(ollamaUrl);
	std::string err;
	if (cli.Ping(&err)) {
		std::fprintf(stdout,
			"{\"ok\":true,\"ollama\":\"%s\",\"status\":\"reachable\"}\n",
			ollamaUrl.c_str());
		return 0;
	}

	// Structured stderr envelope — matches the oes-mcp offline-envelope
	// pattern. Exit non-zero so shell scripts can branch on it.
	nlohmann::json env;
	env["ok"]      = false;
	env["ollama"]  = ollamaUrl;
	env["status"]  = "unreachable";
	env["reason"]  = err.empty() ? std::string("no response") : err;
	std::fprintf(stderr, "%s\n", env.dump().c_str());
	return 2;
}

int CmdIngest(const ParsedOpts& opts)
{
	if (opts.positional.empty()) {
		std::fprintf(stderr,
			"oes-rag-local ingest: corpus directory required\n"
			"Usage: oes-rag-local ingest <corpus-dir> [--out <index-path>]\n");
		return 64; // EX_USAGE
	}

	const std::string corpusDir = opts.positional.front();
	const std::string outPath = !opts.outPath.empty()
		? opts.outPath
		: (corpusDir + "/oes-rag.meta.json");

	oesRagLocal::IngestOptions iopts;
	iopts.corpusDir = corpusDir;
	iopts.outPath   = outPath;
	iopts.ollamaUrl = !opts.ollamaUrl.empty()
		? opts.ollamaUrl
		: EnvOr("OES_RAG_OLLAMA_URL", "http://localhost:11434");

	oesRagLocal::IngestResult result;
	if (!oesRagLocal::RunIngest(iopts, &result)) {
		std::fprintf(stderr,
			"oes-rag-local ingest: failed — %s\n",
			result.error.c_str());
		return 1;
	}

	std::fprintf(stdout,
		"{\"ok\":true,\"chunks\":%zu,\"files\":%zu,\"index\":\"%s\","
		"\"embedded\":%s}\n",
		result.chunks,
		result.files,
		result.indexPath.c_str(),
		result.embedded ? "true" : "false");
	return 0;
}

int CmdServe(const ParsedOpts& opts)
{
	if (opts.positional.empty()) {
		std::fprintf(stderr,
			"oes-rag-local serve: index path required\n"
			"Usage: oes-rag-local serve <index-path> [--port <n>] [--ollama <url>]\n");
		return 64;
	}

	const std::string indexPath = opts.positional.front();
	const int port = opts.port > 0
		? opts.port
		: std::atoi(EnvOr("OES_RAG_LOCAL_PORT", "11700").c_str());
	const std::string ollamaUrl = !opts.ollamaUrl.empty()
		? opts.ollamaUrl
		: EnvOr("OES_RAG_OLLAMA_URL", "http://localhost:11434");

	oesRagLocal::RagIndex index;
	std::string loadErr;
	const bool loaded = index.Load(indexPath, &loadErr);
	if (!loaded) {
		// Don't bail — /health returns 503 instead. Air-gapped operators
		// often start the server first and ingest second; we want the
		// process up so the orchestrator can see it.
		std::fprintf(stderr,
			"oes-rag-local serve: index not loaded (%s) — /health will "
			"return 503 until you run `ingest`.\n",
			loadErr.c_str());
	} else {
		std::fprintf(stderr,
			"oes-rag-local serve: loaded %zu chunks from %s\n",
			index.Size(), indexPath.c_str());
	}

	oesRagLocal::ServerConfig scfg;
	scfg.port      = port;
	scfg.ollamaUrl = ollamaUrl;
	scfg.indexPath = indexPath;
	return oesRagLocal::RunQueryServer(scfg, index);
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		PrintHelp();
		return 64; // EX_USAGE — no subcommand
	}

	const std::string sub = argv[1];
	if (sub == "--help" || sub == "-h" || sub == "help") {
		PrintHelp();
		return 0;
	}

	const ParsedOpts opts = ParseArgs(argc, argv, /*from=*/2);

	if (sub == "ingest") return CmdIngest(opts);
	if (sub == "serve")  return CmdServe(opts);
	if (sub == "health") return CmdHealth(opts);

	std::fprintf(stderr,
		"oes-rag-local: unknown subcommand '%s'\n\n", sub.c_str());
	PrintHelp();
	return 64;
}
