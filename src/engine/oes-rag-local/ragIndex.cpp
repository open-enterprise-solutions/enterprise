/////////////////////////////////////////////////////////////////////////////
// ragIndex.cpp — flat-JSON chunk store implementation.
//
// Performance note (v1): SearchLexical walks every chunk and runs case-
// insensitive substring matches per query token. For the BAS_BUH corpus
// (~3200 objects, ~6400 chunks) this is well under 10ms on commodity
// hardware — fine for the v1 air-gapped use case. v2 introduces FAISS
// when corpora cross the ~100k-chunks threshold or when latency budgets
// demand it.
/////////////////////////////////////////////////////////////////////////////

#include "ragIndex.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace oesRagLocal {

namespace {

std::string ToLower(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		out.push_back(static_cast<char>(
			std::tolower(static_cast<unsigned char>(c))));
	}
	return out;
}

// Tokenize on whitespace and common punctuation. Returns lower-cased
// tokens >= 2 chars. Filters short / noise tokens.
std::vector<std::string> Tokenize(const std::string& s)
{
	std::vector<std::string> out;
	std::string cur;
	auto flush = [&]() {
		if (cur.size() >= 2) out.push_back(ToLower(cur));
		cur.clear();
	};
	for (char c : s) {
		const unsigned char u = static_cast<unsigned char>(c);
		// Treat non-alnum (ASCII) as separators; UTF-8 multibyte sequences
		// for Cyrillic stay together because their bytes have the high
		// bit set, and we only break on ASCII separators.
		if ((u & 0x80) == 0 && !std::isalnum(u)) {
			flush();
		} else {
			cur.push_back(c);
		}
	}
	flush();
	return out;
}

} // namespace

bool RagIndex::Load(const std::string& path, std::string* errOut)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		if (errOut) *errOut = "index file not found: " + path;
		return false;
	}
	std::stringstream buf;
	buf << in.rdbuf();
	auto parsed = nlohmann::json::parse(buf.str(), nullptr,
	                                    /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		if (errOut) *errOut = "index file is not valid JSON: " + path;
		return false;
	}
	if (parsed.contains("version") && parsed["version"].is_string()) {
		m_version = parsed["version"].get<std::string>();
	}
	if (parsed.contains("model") && parsed["model"].is_string()) {
		m_model = parsed["model"].get<std::string>();
	}
	m_chunks.clear();
	if (parsed.contains("chunks") && parsed["chunks"].is_array()) {
		m_chunks.reserve(parsed["chunks"].size());
		for (const auto& c : parsed["chunks"]) {
			if (!c.is_object()) continue;
			Chunk ch;
			if (c.contains("id") && c["id"].is_string())
				ch.id = c["id"].get<std::string>();
			if (c.contains("source") && c["source"].is_string())
				ch.source = c["source"].get<std::string>();
			if (c.contains("text") && c["text"].is_string())
				ch.text = c["text"].get<std::string>();
			if (c.contains("embedding") && c["embedding"].is_array()) {
				ch.embedding.reserve(c["embedding"].size());
				for (const auto& v : c["embedding"]) {
					if (v.is_number())
						ch.embedding.push_back(v.get<float>());
				}
			}
			m_chunks.push_back(std::move(ch));
		}
	}
	m_loaded = true;
	return true;
}

bool RagIndex::Save(const std::string& path, std::string* errOut) const
{
	nlohmann::json out;
	out["version"] = m_version;
	out["model"]   = m_model;
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& ch : m_chunks) {
		nlohmann::json c;
		c["id"]     = ch.id;
		c["source"] = ch.source;
		c["text"]   = ch.text;
		c["embedding"] = ch.embedding;   // empty array when lexical-only
		arr.push_back(std::move(c));
	}
	out["chunks"] = std::move(arr);

	std::ofstream f(path, std::ios::binary);
	if (!f.is_open()) {
		if (errOut) *errOut = "cannot write index file: " + path;
		return false;
	}
	f << out.dump(2);
	if (!f.good()) {
		if (errOut) *errOut = "index file write failed: " + path;
		return false;
	}
	return true;
}

void RagIndex::SetChunks(std::vector<Chunk> chunks)
{
	m_chunks = std::move(chunks);
	m_loaded = true;
}

std::vector<SearchHit> RagIndex::SearchLexical(const std::string& query, int topK) const
{
	std::vector<SearchHit> hits;
	if (m_chunks.empty() || query.empty() || topK <= 0) return hits;

	const auto tokens = Tokenize(query);
	if (tokens.empty()) return hits;

	hits.reserve(m_chunks.size());
	for (std::size_t i = 0; i < m_chunks.size(); ++i) {
		const std::string lower = ToLower(m_chunks[i].text);
		float score = 0.0f;
		for (const auto& t : tokens) {
			std::size_t pos = 0;
			while ((pos = lower.find(t, pos)) != std::string::npos) {
				score += 1.0f;
				pos += t.size();
				// Cap per-token contribution so a chunk that mentions a
				// token 100 times doesn't dominate the top-k.
				if (score > 100.0f) break;
			}
		}
		if (score > 0.0f) {
			hits.push_back({ i, score });
		}
	}
	std::sort(hits.begin(), hits.end(),
	          [](const SearchHit& a, const SearchHit& b) {
		          return a.score > b.score;
	          });
	if (static_cast<int>(hits.size()) > topK) {
		hits.resize(static_cast<std::size_t>(topK));
	}
	return hits;
}

} // namespace oesRagLocal
