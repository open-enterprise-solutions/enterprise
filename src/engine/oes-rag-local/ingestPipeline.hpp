/////////////////////////////////////////////////////////////////////////////
// ingestPipeline — walk a directory tree of OES/BAS XML files, extract
// per-object semantic chunks, optionally embed via Ollama, and write a
// JSON-on-disk index.
//
// v1 scope (this scaffold):
//   - Recursive std::filesystem walk for *.xml under <corpusDir>.
//   - Per-file chunking: 1 chunk per file initially (file path +
//     stripped text snippet). The chunk text is the raw file content
//     truncated to 8KB — good enough for lexical retrieval to surface
//     the right object on a query like "контрагенты" or "регистр
//     накопления товары".
//   - Embeddings: SKIPPED in v1 (writes empty embedding arrays). The
//     ingest pipeline pings Ollama once; if reachable AND a future
//     --embed flag is set we'd round-trip each chunk; otherwise lexical
//     only.
//
// v2 deferred:
//   - Per-object chunking (parse XML, emit one chunk per Catalog /
//     Document / Register definition).
//   - Real Ollama embedding round-trip with rate limiting.
//   - Companion .bsl module file pickup.
//   - Synonym / description extraction from <synonym>/<comment> elements.
/////////////////////////////////////////////////////////////////////////////

#ifndef OES_RAG_LOCAL_INGEST_PIPELINE_HPP
#define OES_RAG_LOCAL_INGEST_PIPELINE_HPP

#include <cstddef>
#include <string>

namespace oesRagLocal {

struct IngestOptions {
	std::string corpusDir;   // directory to walk recursively
	std::string outPath;     // where to write *.meta.json
	std::string ollamaUrl;   // for v2 embeddings; v1 ignores after Ping
};

struct IngestResult {
	std::size_t files    = 0;
	std::size_t chunks   = 0;
	bool        embedded = false;     // true if real embeddings were written
	std::string indexPath;            // where the index landed on disk
	std::string error;                // populated when RunIngest returns false
};

// Run the v1 ingest. Returns true on success with *result populated.
// On failure (corpus dir missing, no xml files, write error) returns
// false with result->error set.
bool RunIngest(const IngestOptions& opts, IngestResult* result);

} // namespace oesRagLocal

#endif // OES_RAG_LOCAL_INGEST_PIPELINE_HPP
