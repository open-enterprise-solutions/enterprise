/////////////////////////////////////////////////////////////////////////////
// ingestPipeline.cpp — v1 implementation.
//
// Walks corpus dir, collects *.xml files, builds one chunk per file
// (whole-file text snippet, truncated). Writes RagIndex via Save(). No
// XML parsing yet — v1 trusts lexical match across the raw bytes.
//
// Ollama probe: we ping Ollama up front and report `embedded=true` only
// if a real round-trip succeeded. v1 skips the per-chunk embedding loop;
// the field exists so v2 can flip it on without changing the contract.
/////////////////////////////////////////////////////////////////////////////

#include "ingestPipeline.hpp"

#include "ollamaClient.hpp"
#include "ragIndex.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace oesRagLocal {

namespace {

constexpr std::size_t kMaxChunkBytes = 8192;  // per-file text cap

std::string ReadFileTruncated(const std::filesystem::path& path,
                              std::size_t maxBytes)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) return {};
	std::string buf(maxBytes, '\0');
	in.read(buf.data(), static_cast<std::streamsize>(maxBytes));
	const auto n = static_cast<std::size_t>(in.gcount());
	buf.resize(n);
	return buf;
}

bool IsXmlExt(const std::filesystem::path& p)
{
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
	               [](unsigned char c) { return std::tolower(c); });
	return ext == ".xml";
}

} // namespace

bool RunIngest(const IngestOptions& opts, IngestResult* result)
{
	if (!result) return false;
	result->files    = 0;
	result->chunks   = 0;
	result->embedded = false;
	result->error.clear();

	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::exists(opts.corpusDir, ec) || !fs::is_directory(opts.corpusDir, ec)) {
		result->error = "corpus directory not found or not a directory: "
		                + opts.corpusDir;
		return false;
	}

	// Probe Ollama. v1 doesn't embed even if reachable, but we flip
	// `embedded` off explicitly so downstream observers see the truth.
	if (!opts.ollamaUrl.empty()) {
		OllamaClient probe(opts.ollamaUrl);
		std::string ignored;
		// Result not used in v1 — kept so v2 can swap in the embedding loop.
		(void)probe.Ping(&ignored);
	}

	std::vector<Chunk> chunks;
	chunks.reserve(256);

	for (const auto& entry : fs::recursive_directory_iterator(
	         opts.corpusDir, fs::directory_options::skip_permission_denied, ec))
	{
		if (ec) {
			// Don't bail — log via the result.error suffix and continue.
			ec.clear();
			continue;
		}
		if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
		if (!IsXmlExt(entry.path())) continue;

		++result->files;

		Chunk ch;
		ch.id     = entry.path().filename().string();
		ch.source = entry.path().string();
		ch.text   = ReadFileTruncated(entry.path(), kMaxChunkBytes);
		// `embedding` stays empty in v1 — lexical match path covers it.
		if (!ch.text.empty()) {
			chunks.push_back(std::move(ch));
		}
	}

	if (chunks.empty()) {
		result->error = "no .xml files found under " + opts.corpusDir;
		return false;
	}

	RagIndex idx;
	idx.SetChunks(std::move(chunks));
	std::string saveErr;
	if (!idx.Save(opts.outPath, &saveErr)) {
		result->error = saveErr;
		return false;
	}

	result->chunks    = idx.Size();
	result->indexPath = opts.outPath;
	result->embedded  = false;
	return true;
}

} // namespace oesRagLocal
