/////////////////////////////////////////////////////////////////////////////
// LoadHelpCorpus — non-throwing JSON → ibHelpCorpus factory.
//
// See helpLoader.h for the contract and design v5 §3.3 for the binding
// spec. The loader does NOT throw — bad JSON is a data condition, not a
// runtime exception path.
/////////////////////////////////////////////////////////////////////////////

#include "backend/help/helpLoader.h"

#include "backend/help/helpCorpus.h"
#include "backend/help/helpEntry.h"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>

#include "3rdparty/nlohmann/json.hpp"

#include <exception>
#include <utility>
#include <vector>

using nlohmann::json;

namespace {

// Map a serialised kind string to ibHelpKind. Unknown values default to
// kKeyword and the caller records a kWarning — keeps the loader
// forward-compatible with corpora that introduce new kinds before the
// runtime supports them.
ibHelpKind ParseKind(const std::string& s) {
	if (s == "keyword")           return ibHelpKind::kKeyword;
	if (s == "system_function")   return ibHelpKind::kSystemFunction;
	if (s == "system_constant")   return ibHelpKind::kSystemConstant;
	if (s == "system_enum")       return ibHelpKind::kSystemEnum;
	if (s == "metaobject_type")   return ibHelpKind::kMetaObjectType;
	if (s == "metaobject_attribute") return ibHelpKind::kMetaObjectAttribute;
	if (s == "metaobject_method") return ibHelpKind::kMetaObjectMethod;
	if (s == "primitive_type")    return ibHelpKind::kPrimitiveType;
	if (s == "collection")        return ibHelpKind::kCollection;
	if (s == "event")             return ibHelpKind::kEvent;
	if (s == "operator")          return ibHelpKind::kOperator;
	return ibHelpKind::kKeyword;
}

wxString Utf8(const std::string& s) {
	return wxString::FromUTF8(s.c_str(), s.size());
}

// Safe field reader — returns default when the key is missing OR present
// with a non-string type. nlohmann::json's `value(k, def)` throws on
// type mismatch; we want to skip-and-continue instead.
std::string SafeStr(const json& obj, const char* key,
                    const std::string& def = std::string()) {
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_string()) return def;
	return it->get<std::string>();
}

bool SafeBool(const json& obj, const char* key, bool def) {
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_boolean()) return def;
	return it->get<bool>();
}

std::vector<wxString> SafeStrArray(const json& obj, const char* key) {
	std::vector<wxString> out;
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_array()) return out;
	for (const auto& v : *it) {
		if (v.is_string()) out.push_back(Utf8(v.get<std::string>()));
	}
	return out;
}

// Read an entire file into a string. Returns empty + sets *ok = false on
// any I/O error. wxFile is used so the loader plays nicely with platforms
// where std::ifstream's path encoding differs (Windows non-ASCII).
// wxInvalidOffset from Length() = I/O failure, not "empty file" — handled
// explicitly. wxFile::Read can return short on signal interrupt / certain
// network filesystems, so we loop until done or a zero-progress read
// terminates the loop as a hard failure.
std::string ReadFile(const wxString& path, bool* ok) {
	*ok = false;
	wxFile f(path, wxFile::read);
	if (!f.IsOpened()) return {};

	const wxFileOffset len = f.Length();
	if (len == wxInvalidOffset) return {};   // I/O error
	if (len == 0)               { *ok = true; return {}; }
	if (len < 0)                return {};   // defensive — wxInvalidOffset is -1 historically

	std::string buf(static_cast<size_t>(len), '\0');
	size_t totalRead = 0;
	const size_t want = static_cast<size_t>(len);
	while (totalRead < want) {
		ssize_t got = f.Read(&buf[totalRead], want - totalRead);
		if (got < 0)                return {};                       // hard error
		if (got == 0)               return {};                       // unexpected EOF / no progress
		totalRead += static_cast<size_t>(got);
	}
	// Strip UTF-8 BOM if present (§2.3 — loader tolerates it).
	if (buf.size() >= 3 &&
	    static_cast<unsigned char>(buf[0]) == 0xEF &&
	    static_cast<unsigned char>(buf[1]) == 0xBB &&
	    static_cast<unsigned char>(buf[2]) == 0xBF) {
		buf.erase(0, 3);
	}
	*ok = true;
	return buf;
}

// Canonicalise platform-locale strings to the corpus's directory naming
// convention. wxLocale returns "en", "ru", "uk", "en_US", … ; corpus
// directories are "en-US", "ru-RU", "uk-UA". Unknown locales fall back
// to "en-US" rather than failing the load — a missing locale dir is a
// degraded-but-functional state, fatal-failing startup is worse.
wxString CanonicaliseLocale(const wxString& raw) {
	wxString s = raw;
	s.Replace(wxT("_"), wxT("-"));
	if (s.IsSameAs(wxT("ru"), false) || s.IsSameAs(wxT("ru-RU"), false))
		return wxT("ru-RU");
	if (s.IsSameAs(wxT("uk"), false) || s.IsSameAs(wxT("uk-UA"), false))
		return wxT("uk-UA");
	if (s.IsSameAs(wxT("en"), false) ||
	    s.IsSameAs(wxT("en-US"), false) ||
	    s.IsSameAs(wxT("en-GB"), false))
		return wxT("en-US");
	return wxT("en-US");
}

// Parse one bucket file. Per-entry try/catch is the load-bearing
// non-throwing contract (§3.3): nlohmann::json::value() handles only
// missing keys, type-mismatched present keys still throw. The outer
// try/catch around the json::parse() call covers top-level malformed
// JSON. std::exception in the outer catch covers std::bad_alloc and
// similar — never let the loader unwind across cpp-httplib handlers.
void ParseBucket(const wxString&               bucketPath,
                 std::vector<ibHelpEntry>&     out,
                 std::vector<ibHelpLoadError>& errors) {
	bool readOk = false;
	std::string raw = ReadFile(bucketPath, &readOk);
	if (!readOk) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxT("Cannot read bucket file.");
		errors.push_back(std::move(e));
		return;
	}

	json doc;
	try {
		doc = json::parse(raw);
	} catch (const json::exception& ex) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxString::Format(
		    wxT("Malformed JSON: %s"), Utf8(ex.what()));
		errors.push_back(std::move(e));
		return;
	} catch (const std::exception& ex) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxString::Format(
		    wxT("I/O / allocation failure: %s"), Utf8(ex.what()));
		errors.push_back(std::move(e));
		return;
	}

	// Format identifier gate — aligns with OES-JSON-1.0 / OES-XML-2.0
	// metadata-export convention. Missing or wrong format is fatal for
	// the bucket.
	const std::string fmt = SafeStr(doc, "format");
	const wxString    expectedFmt = IB_HELP_FORMAT_NAME;
	if (fmt.empty() || Utf8(fmt) != expectedFmt) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxString::Format(
		    wxT("Unexpected or missing format identifier (expected '%s', got '%s')."),
		    expectedFmt, Utf8(fmt));
		errors.push_back(std::move(e));
		return;
	}

	// Schema version gate.
	int schemaVersion = doc.value("schema_version", 0);
	if (schemaVersion <= 0) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxT("Missing or invalid schema_version.");
		errors.push_back(std::move(e));
		return;
	}
	if (schemaVersion > kIbHelpSchemaVersion) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxString::Format(
		    wxT("Schema version %d is newer than loader (%d)."),
		    schemaVersion, kIbHelpSchemaVersion);
		errors.push_back(std::move(e));
		return;
	}

	auto entriesIt = doc.find("entries");
	if (entriesIt == doc.end() || !entriesIt->is_array()) {
		ibHelpLoadError e;
		e.bucketPath = bucketPath;
		e.severity   = ibHelpLoadSeverity::kFatal;
		e.message    = wxT("Missing or invalid 'entries' array.");
		errors.push_back(std::move(e));
		return;
	}

	for (const auto& obj : *entriesIt) {
		try {
			if (!obj.is_object()) continue;

			ibHelpEntry e;
			e.id          = Utf8(SafeStr(obj, "id"));
			if (e.id.empty()) {
				ibHelpLoadError err;
				err.bucketPath = bucketPath;
				err.severity   = ibHelpLoadSeverity::kEntrySkipped;
				err.message    = wxT("Entry without id.");
				errors.push_back(std::move(err));
				continue;
			}

			e.nameLocal    = Utf8(SafeStr(obj, "name_local"));
			e.nameEn       = Utf8(SafeStr(obj, "name_en"));
			e.signature    = Utf8(SafeStr(obj, "signature"));
			e.description  = Utf8(SafeStr(obj, "description"));
			e.syntaxBlock  = Utf8(SafeStr(obj, "syntax_block"));
			e.parameters   = Utf8(SafeStr(obj, "parameters"));
			e.returnDescr  = Utf8(SafeStr(obj, "return_descr"));
			e.example      = Utf8(SafeStr(obj, "example"));
			e.availability = Utf8(SafeStr(obj, "availability"));
			e.kind         = ParseKind(SafeStr(obj, "kind", "keyword"));
			e.categoryKeys = SafeStrArray(obj, "category_keys");
			e.seeAlso      = SafeStrArray(obj, "see_also");
			e.reviewed     = SafeBool(obj, "reviewed", false);

			out.push_back(std::move(e));
		} catch (const json::exception& ex) {
			ibHelpLoadError err;
			err.bucketPath = bucketPath;
			err.severity   = ibHelpLoadSeverity::kEntrySkipped;
			err.message    = wxString::Format(
			    wxT("Entry parse error: %s"), Utf8(ex.what()));
			errors.push_back(std::move(err));
		} catch (const std::exception& ex) {
			ibHelpLoadError err;
			err.bucketPath = bucketPath;
			err.severity   = ibHelpLoadSeverity::kEntrySkipped;
			err.message    = wxString::Format(
			    wxT("Entry construction error: %s"), Utf8(ex.what()));
			errors.push_back(std::move(err));
		}
	}
}

// Walk a locale directory's bucket files. `_categories.json` is reserved
// for the category dictionary (Phase 1 doesn't consume it yet — Phase 3
// will read it for tree display names). Anything starting with `_` is
// skipped here.
std::vector<wxString> ListBuckets(const wxString& localeDir) {
	std::vector<wxString> out;
	if (!wxDir::Exists(localeDir)) return out;

	wxDir dir(localeDir);
	if (!dir.IsOpened()) return out;

	wxString name;
	bool cont = dir.GetFirst(&name, wxT("*.json"), wxDIR_FILES);
	while (cont) {
		if (!name.StartsWith(wxT("_"))) {
			wxFileName fn(localeDir, name);
			out.push_back(fn.GetFullPath());
		}
		cont = dir.GetNext(&name);
	}
	return out;
}

// Build a single-source corpus from a directory. The loader collects
// entries + errors locally, then constructs the corpus once with
// everything in hand — no partial corpus exists at any point.
//
// May throw std::bad_alloc out of make_shared / index construction;
// LoadHelpCorpus catches that and returns an empty fallback so the
// non-throwing contract holds end-to-end.
std::shared_ptr<const ibHelpCorpus>
LoadSource(const wxString&               localeCode,
           const wxString&               sourceRoot,
           ibHelpCorpus::Source          sourceTag,
           std::vector<ibHelpLoadError>& errorsOut) {
	const wxString localeDir =
	    wxFileName(sourceRoot, localeCode).GetFullPath();

	// Surface missing-locale-directory as a kFatal so operators see
	// "we shipped without the corpus" rather than the silent
	// empty-corpus state. Bucket-less directories also count (after the
	// ParseBucket loop below detects zero files).
	if (!wxDir::Exists(localeDir)) {
		ibHelpLoadError err;
		err.bucketPath = localeDir;
		err.severity   = ibHelpLoadSeverity::kFatal;
		err.message    = wxString::Format(
		    wxT("Help corpus directory does not exist: %s"), localeDir);
		errorsOut.push_back(std::move(err));
		return std::make_shared<ibHelpCorpus>(
		    localeCode, sourceTag, std::vector<ibHelpEntry>{},
		    std::vector<ibHelpLoadError>{});
	}

	std::vector<ibHelpEntry>     entries;
	std::vector<ibHelpLoadError> localErrors;
	const auto buckets = ListBuckets(localeDir);
	for (const wxString& bucket : buckets) {
		ParseBucket(bucket, entries, localErrors);
	}
	if (buckets.empty()) {
		ibHelpLoadError err;
		err.bucketPath = localeDir;
		err.severity   = ibHelpLoadSeverity::kFatal;
		err.message    = wxString::Format(
		    wxT("Help corpus directory has no bucket files: %s"), localeDir);
		localErrors.push_back(std::move(err));
	}

	// Duplicate-id check within the source. Per §3.6 step 1, duplicates
	// within a single source are fatal for that entry pair; we drop the
	// second occurrence and record the error (with the SECOND occurrence's
	// bucket path so operators can locate the offending file).
	std::vector<ibHelpEntry> deduped;
	deduped.reserve(entries.size());
	std::unordered_map<wxString, size_t> seenAt;
	for (auto& e : entries) {
		auto it = seenAt.find(e.id);
		if (it != seenAt.end()) {
			ibHelpLoadError err;
			err.bucketPath = localeDir;
			err.severity   = ibHelpLoadSeverity::kFatal;
			err.message    = wxString::Format(
			    wxT("Duplicate id '%s' within source corpus — second occurrence dropped."),
			    e.id);
			localErrors.push_back(std::move(err));
			continue;
		}
		seenAt[e.id] = deduped.size();
		deduped.push_back(std::move(e));
	}

	// Errors live INSIDE the corpus snapshot — there is no parallel
	// errors-on-result vector. Callers see one canonical source via
	// `corpus->LoadErrors()`. The errorsOut parameter is still in the
	// signature for backwards compatibility with the outer loader's
	// catch path but is not used here.
	(void)errorsOut;
	return std::make_shared<ibHelpCorpus>(
	    localeCode, sourceTag, std::move(deduped), std::move(localErrors));
}

} // namespace

ibHelpLoadResult LoadHelpCorpus(const wxString& localeCode,
                                const wxString& platformDir,
                                const wxString& configCacheDir) {
	ibHelpLoadResult result;
	const wxString locale = CanonicaliseLocale(localeCode);

	// Outer net for the non-throwing contract. make_shared, BuildIndexes
	// (unordered_map allocations), and BuildCategoryTree (unique_ptr
	// allocations) can all throw std::bad_alloc. Catch as wide as we can
	// without swallowing programming errors — std::exception covers the
	// allocation-failure class; anything weirder falls through and the
	// process probably wanted to die anyway.
	try {
		std::shared_ptr<const ibHelpCorpus> platform =
		    LoadSource(locale, platformDir,
		               ibHelpCorpus::Source::kPlatform, result.errors);

		std::shared_ptr<const ibHelpCorpus> perConfig;
		if (!configCacheDir.empty()) {
			perConfig = LoadSource(locale, configCacheDir,
			                        ibHelpCorpus::Source::kPerConfiguration,
			                        result.errors);
		}

		if (perConfig) {
			result.corpus = std::make_shared<ibHelpCorpus>(
			    std::move(platform), std::move(perConfig), locale);
		} else {
			result.corpus = std::move(platform);
		}
	} catch (const std::exception& ex) {
		ibHelpLoadError e;
		e.severity = ibHelpLoadSeverity::kFatal;
		e.message  = wxString::Format(
		    wxT("Help corpus construction failed: %s — falling back to empty."),
		    Utf8(ex.what()));
		result.errors.push_back(std::move(e));
		result.corpus.reset();
	}

	// Always return a non-null corpus so callers can keep the
	// GetHelpCorpus() invariant branch-free. Both the LoadSource missing-
	// dir path and the outer catch above can leave `corpus` null.
	if (!result.corpus) {
		try {
			result.corpus = std::make_shared<ibHelpCorpus>(locale);
		} catch (...) {
			// If even the empty-corpus alloc fails, the caller's invariant
			// is best-effort — appData's lazy-init does its own fallback.
		}
	}

	return result;
}
