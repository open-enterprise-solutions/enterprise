/////////////////////////////////////////////////////////////////////////////
// ragIndex — in-memory chunk store + retrieval over JSON-on-disk.
//
// v1 (this scaffold): flat JSON file `<index>.meta.json` of the form:
//   {
//     "version": "oes-rag-local/1",
//     "model":   "lexical-only" | "nomic-embed-text",
//     "dim":     0 | 768,
//     "chunks": [
//       { "id": "...", "source": "path/to/file.xml#object",
//         "text": "object name + attrs + description",
//         "embedding": [] | [floats...]
//       }, ...
//     ]
//   }
//
// Retrieval — Search():
//   - If embeddings present AND queryEmbedding supplied → cosine top-k.
//   - Else → brute-force substring/token-overlap scoring over `text`.
//
// v2 deferred:
//   - FAISS index file companion (`*.faiss`) for O(log n) ANN.
//   - mmap-backed chunk store for >>RAM corpora.
//   - Incremental update (current API rewrites the whole file).
/////////////////////////////////////////////////////////////////////////////

#ifndef OES_RAG_LOCAL_RAG_INDEX_HPP
#define OES_RAG_LOCAL_RAG_INDEX_HPP

#include "3rdparty/nlohmann/json.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace oesRagLocal {

struct Chunk {
	std::string        id;          // unique within an index, e.g. "BAS_BUH:Catalog:Контрагенты"
	std::string        source;      // source file path (+ optional #fragment)
	std::string        text;        // searchable content
	std::vector<float> embedding;   // empty when index is lexical-only
};

struct SearchHit {
	std::size_t chunkIndex;  // index into RagIndex::Chunks()
	float       score;       // higher = better match
};

class RagIndex {
public:
	// Load a previously-written `*.meta.json` file. Returns false on
	// missing / malformed file with `*errOut` populated. A freshly-
	// constructed RagIndex with no Load is also valid — Size() == 0,
	// Search() returns empty.
	bool Load(const std::string& path, std::string* errOut);

	// Persist the current chunk list to disk as JSON. Used by the ingest
	// pipeline. Returns false on write failure.
	bool Save(const std::string& path, std::string* errOut) const;

	// Replace the chunk list (used by ingest before Save).
	void SetChunks(std::vector<Chunk> chunks);

	// Lexical (token-overlap) retrieval. v1 default. Case-insensitive,
	// scores by count of query-token substring hits in chunk text.
	std::vector<SearchHit> SearchLexical(const std::string& query, int topK) const;

	const std::vector<Chunk>& Chunks() const { return m_chunks; }
	std::size_t Size() const { return m_chunks.size(); }

	bool Loaded() const { return m_loaded; }
	const std::string& Model() const { return m_model; }
	const std::string& Version() const { return m_version; }

private:
	std::vector<Chunk> m_chunks;
	std::string        m_version = "oes-rag-local/1";
	std::string        m_model   = "lexical-only";
	bool               m_loaded  = false;
};

} // namespace oesRagLocal

#endif // OES_RAG_LOCAL_RAG_INDEX_HPP
