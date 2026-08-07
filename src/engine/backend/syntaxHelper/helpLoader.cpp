/////////////////////////////////////////////////////////////////////////////
// LoadHelpCorpus — non-throwing JSON → ibHelpCorpus factory.
//
// See helpLoader.h for the contract and design v5 §3.3 for the binding
// spec. The loader does NOT throw — bad JSON is a data condition, not a
// runtime exception path.
/////////////////////////////////////////////////////////////////////////////

#include "backend/syntaxHelper/helpLoader.h"

#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpEntry.h"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include "3rdparty/nlohmann/json.hpp"

#include <exception>
#include <map>
#include <memory>
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

}   // namespace (close anonymous so CanonicaliseLocale gets external linkage)

// Canonicalise platform-locale strings to the corpus's directory naming
// convention. wxLocale::GetCanonicalName() returns ISO 639 + ISO 3166
// in xx or xx_YY form ("en", "en_US", "en-GB", "ru_RU", …); the first
// two chars are always the language code, which is what we use as
// directory name.
//
// Open-set: we do NOT whitelist languages here. If the corpus ships
// content for "fr", drop the files in syntaxHelper/fr/ and it
// loads — no C++ change needed. Missing language directory surfaces
// as a kFatal load error inside the corresponding LoadSource call;
// the corpus stays non-null (empty fallback) so the read path keeps
// its branch-free invariant.
//
// Empty / 1-char input → "en" as a defensive minimum so the loader
// has *something* to look for (early-boot edge cases where wxLocale
// hasn't initialised yet).
wxString CanonicaliseLocale(const wxString& raw) {
	if (raw.length() < 2) return wxT("en");
	return raw.Left(2).Lower();
}

namespace {

// Parse one bucket file. Per-entry try/catch is the load-bearing
// non-throwing contract (§3.3): nlohmann::json::value() handles only
// missing keys, type-mismatched present keys still throw. The outer
// try/catch around the json::parse() call covers top-level malformed
// JSON. std::exception in the outer catch covers std::bad_alloc and
// similar — never let the loader unwind across cpp-httplib handlers.
//
// Content-based signature (raw bytes already loaded) so the same
// parser drives FileSystemSource and ZipSource — only the storage
// layer knows how to fetch the bytes; the parser is medium-agnostic.
// `bucketPath` is the display string used in error records (file
// path for FS sources, "<zip>!<entry>" for zip sources).
void ParseBucket(const std::string&            raw,
                 const wxString&               bucketPath,
                 std::vector<ibHelpEntry>&     out,
                 std::vector<ibHelpLoadError>& errors) {
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

			e.nameLocal      = Utf8(SafeStr(obj, "name_local"));
			e.nameEn         = Utf8(SafeStr(obj, "name_en"));
			e.signature      = Utf8(SafeStr(obj, "signature"));
			e.description    = Utf8(SafeStr(obj, "description"));
			e.syntaxBlock    = Utf8(SafeStr(obj, "syntax_block"));
			e.syntaxBlockVes = Utf8(SafeStr(obj, "syntax_block_ves"));
			e.parameters     = Utf8(SafeStr(obj, "parameters"));
			e.returnDescr    = Utf8(SafeStr(obj, "return_descr"));
			e.example        = Utf8(SafeStr(obj, "example"));
			e.exampleVes     = Utf8(SafeStr(obj, "example_ves"));
			e.availability   = Utf8(SafeStr(obj, "availability"));
			e.kind           = ParseKind(SafeStr(obj, "kind", "keyword"));
			e.categoryKeys   = SafeStrArray(obj, "category_keys");
			e.seeAlso        = SafeStrArray(obj, "see_also");
			e.reviewed       = SafeBool(obj, "reviewed", false);

			// modes: array of "ves" / "ces"; missing or empty = both.
			const auto modeArr = SafeStrArray(obj, "modes");
			if (!modeArr.empty()) {
				unsigned mask = 0u;
				for (const wxString& m : modeArr) {
					const wxString low = m.Lower();
					if      (low == wxT("ves") || low == wxT("vbs"))
						mask |= ibHelpEntry::kModeVes;
					else if (low == wxT("ces"))
						mask |= ibHelpEntry::kModeCes;
				}
				if (mask != 0u) e.modes = mask;
			}

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

// ---------------------------------------------------------------------------
// HelpBucketSource — storage abstraction.
//
// Two real implementations: FileSystemSource (loose directory) and
// ZipSource (.hlk = zip archive, used in production). LoadSource is
// medium-agnostic — it iterates bucket names from the source, asks
// for content per name, and feeds bytes to ParseBucket. Adding a
// third source (HTTPS / DB) is one more subclass, no LoadSource
// changes.
//
// `BucketName` is the leaf file name ("global_functions.json"),
// NOT the full path. `DisplayPath` is a human-readable label used
// in error records ("<dir>/<name>" for FS, "<zip>!<name>" for zip).
// ---------------------------------------------------------------------------

class HelpBucketSource {
public:
	virtual ~HelpBucketSource() = default;

	// True iff the source exists (directory present, zip openable).
	virtual bool Exists() const = 0;

	// Human-readable label for error records (path to dir / zip).
	virtual wxString DisplayRoot() const = 0;

	// All *.json bucket names except those starting with '_'
	// (the leading-underscore prefix is reserved for category
	// dictionary and other meta files).
	virtual std::vector<wxString> ListBucketNames() const = 0;

	// Read a bucket's full content as a UTF-8 byte string. Returns
	// empty + *ok = false on I/O failure.
	virtual std::string ReadBucket(const wxString& bucketName,
	                                bool* ok) const = 0;

	// Display string for one bucket — passed into error records.
	virtual wxString BucketDisplayPath(const wxString& bucketName) const = 0;
};

class FileSystemSource final : public HelpBucketSource {
public:
	explicit FileSystemSource(const wxString& localeDir)
	    : m_dir(localeDir) {}

	bool Exists() const override { return wxDir::Exists(m_dir); }
	wxString DisplayRoot() const override { return m_dir; }

	std::vector<wxString> ListBucketNames() const override {
		std::vector<wxString> out;
		if (!wxDir::Exists(m_dir)) return out;
		wxDir dir(m_dir);
		if (!dir.IsOpened()) return out;
		wxString name;
		bool cont = dir.GetFirst(&name, wxT("*.json"), wxDIR_FILES);
		while (cont) {
			if (!name.StartsWith(wxT("_"))) out.push_back(name);
			cont = dir.GetNext(&name);
		}
		return out;
	}

	std::string ReadBucket(const wxString& bucketName, bool* ok) const override {
		const wxString full = wxFileName(m_dir, bucketName).GetFullPath();
		return ReadFile(full, ok);
	}

	wxString BucketDisplayPath(const wxString& bucketName) const override {
		return wxFileName(m_dir, bucketName).GetFullPath();
	}

private:
	wxString m_dir;
};

// Zip source — reads `<root>/<locale>.hlk` (DEFLATE zip). Loads all
// entries into memory at ctor; corpus is small (<1 MB compressed)
// and an open zip stream is not random-access, so a one-shot load
// is simpler than re-scanning per ReadBucket call.
class ZipSource final : public HelpBucketSource {
public:
	explicit ZipSource(const wxString& zipPath) : m_path(zipPath) {
		wxFileInputStream fis(zipPath);
		if (!fis.IsOk()) return;
		// Pin entry-name decoding to UTF-8 explicitly — Windows tar.exe
		// (bsdtar) writes zip filenames as UTF-8 with the General Purpose
		// flag bit 11 unset for ASCII-only paths, so wxConvLocal (cp1251)
		// would mis-decode non-ASCII names and trip a wx DEBUG assert in
		// the string iterator. UTF-8 is the safe superset.
		wxZipInputStream zis(fis, wxConvUTF8);
		std::unique_ptr<wxZipEntry> e;
		while (true) {
			e.reset(zis.GetNextEntry());
			if (!e) break;
			if (e->IsDir()) continue;
			wxString name = e->GetName();
			// Strip "./" prefix that `tar -C dir .` adds — otherwise
			// "./_categories.json" would not match the leading-_ filter
			// in ListBucketNames and would be loaded as a normal bucket.
			if (name.StartsWith(wxT("./")) || name.StartsWith(wxT(".\\")))
				name = name.Mid(2);
			// Defensive: skip zero-length names (e.g. the bare "./" dir
			// marker after strip) — wx would assert on empty iterator.
			if (name.IsEmpty()) continue;
			if (!zis.OpenEntry(*e)) continue;
			const size_t sz = static_cast<size_t>(e->GetSize());
			std::string buf;
			buf.resize(sz);
			if (sz > 0) zis.Read(&buf[0], sz);
			// wxZipInputStream's Read may short-read on the last
			// chunk; LastRead() gives actual byte count.
			buf.resize(static_cast<size_t>(zis.LastRead()));
			m_entries.emplace(name, std::move(buf));
		}
		m_ok = true;
	}

	bool Exists() const override { return m_ok && !m_entries.empty(); }
	wxString DisplayRoot() const override { return m_path; }

	std::vector<wxString> ListBucketNames() const override {
		std::vector<wxString> out;
		for (const auto& kv : m_entries) {
			if (kv.first.StartsWith(wxT("_"))) continue;
			if (!kv.first.EndsWith(wxT(".json"))) continue;
			out.push_back(kv.first);
		}
		return out;
	}

	std::string ReadBucket(const wxString& bucketName, bool* ok) const override {
		auto it = m_entries.find(bucketName);
		if (it == m_entries.end()) { *ok = false; return {}; }
		*ok = true;
		return it->second;
	}

	wxString BucketDisplayPath(const wxString& bucketName) const override {
		return m_path + wxT("!") + bucketName;
	}

private:
	wxString m_path;
	std::map<wxString, std::string> m_entries;
	bool m_ok = false;
};

// Resolve which source to use for `<root>/<locale>`. Priority:
//   1. <root>/<locale>.hlk → ZipSource (prod layout)
//   2. <root>/<locale>/    → FileSystemSource (dev / unpacked)
// Returns nullptr when neither exists; caller surfaces kFatal.
std::unique_ptr<HelpBucketSource>
ResolveSource(const wxString& sourceRoot, const wxString& localeCode) {
	const wxString hbkPath =
	    wxFileName(sourceRoot, localeCode + wxT(".hlk")).GetFullPath();
	if (wxFileName::FileExists(hbkPath)) {
		auto zip = std::make_unique<ZipSource>(hbkPath);
		if (zip->Exists()) return zip;
	}
	const wxString dirPath = wxFileName(sourceRoot, localeCode).GetFullPath();
	if (wxDir::Exists(dirPath)) {
		return std::make_unique<FileSystemSource>(dirPath);
	}
	return nullptr;
}

// Build a single-source corpus from any HelpBucketSource. The loader
// collects entries + errors locally, then constructs the corpus once
// with everything in hand — no partial corpus exists at any point.
//
// May throw std::bad_alloc out of make_shared / index construction;
// LoadHelpCorpus catches that and returns an empty fallback so the
// non-throwing contract holds end-to-end.
std::shared_ptr<const ibHelpCorpus>
LoadSource(const wxString&               localeCode,
           const wxString&               sourceRoot,
           ibHelpCorpus::Source          sourceTag,
           std::vector<ibHelpLoadError>& errorsOut) {
	std::unique_ptr<HelpBucketSource> source =
	    ResolveSource(sourceRoot, localeCode);

	// Surface missing-locale-source as a kFatal so operators see
	// "we shipped without the corpus" rather than the silent
	// empty-corpus state. The error MUST land inside the constructed
	// corpus (corpus->LoadErrors()) — appData reads errors only from
	// the corpus snapshot; the `errorsOut` parameter is legacy
	// plumbing that LoadHelpCorpus no longer consults.
	if (!source || !source->Exists()) {
		ibHelpLoadError err;
		err.bucketPath = wxFileName(sourceRoot, localeCode).GetFullPath();
		err.severity   = ibHelpLoadSeverity::kFatal;
		err.message    = wxString::Format(
		    wxT("Help corpus not found for locale '%s' under: %s"),
		    localeCode, sourceRoot);
		std::vector<ibHelpLoadError> errs;
		errs.push_back(std::move(err));
		return std::make_shared<ibHelpCorpus>(
		    localeCode, sourceTag, std::vector<ibHelpEntry>{},
		    std::move(errs));
	}

	std::vector<ibHelpEntry>     entries;
	std::vector<ibHelpLoadError> localErrors;
	const auto bucketNames = source->ListBucketNames();
	for (const wxString& name : bucketNames) {
		bool readOk = false;
		std::string raw = source->ReadBucket(name, &readOk);
		const wxString displayPath = source->BucketDisplayPath(name);
		if (!readOk) {
			ibHelpLoadError e;
			e.bucketPath = displayPath;
			e.severity   = ibHelpLoadSeverity::kFatal;
			e.message    = wxT("Cannot read bucket.");
			localErrors.push_back(std::move(e));
			continue;
		}
		ParseBucket(raw, displayPath, entries, localErrors);
	}
	if (bucketNames.empty()) {
		ibHelpLoadError err;
		err.bucketPath = source->DisplayRoot();
		err.severity   = ibHelpLoadSeverity::kFatal;
		err.message    = wxString::Format(
		    wxT("Help corpus source has no bucket files: %s"),
		    source->DisplayRoot());
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
			err.bucketPath = source->DisplayRoot();
			err.severity   = ibHelpLoadSeverity::kFatal;
			err.message    = wxString::Format(
			    wxT("Duplicate id '%s' within source corpus - second occurrence dropped."),
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
	wxString locale = CanonicaliseLocale(localeCode);

	// Graceful language fallback: if the requested language has neither
	// a <locale>.hlk zip nor a <locale>/ directory shipped, fall back
	// to English instead of emitting a fatal "not found" error. The
	// user gets English help — better than no help — and a kWarning
	// record so operators see the substitution.
	if (locale != wxT("en") && !ResolveSource(platformDir, locale)) {
		ibHelpLoadError warn;
		warn.bucketPath = wxFileName(platformDir, locale).GetFullPath();
		warn.severity   = ibHelpLoadSeverity::kWarning;
		warn.message    = wxString::Format(
		    wxT("Help corpus for locale '%s' not shipped; falling back to 'en'."),
		    locale);
		result.errors.push_back(std::move(warn));
		locale = wxT("en");
	}

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
		    wxT("Help corpus construction failed: %s - falling back to empty."),
		    Utf8(ex.what()));
		result.errors.push_back(std::move(e));
		result.corpus.reset();
	}

	// Always return a non-null corpus so callers can keep the
	// GetHelpCorpus() invariant branch-free. Both the LoadSource missing-
	// dir path and the outer catch above can leave `corpus` null. When
	// we fall back to empty here, fold any outer-catch errors into the
	// corpus's internal LoadErrors so callers see them through the same
	// surface (appData reads errors only from corpus->LoadErrors()).
	if (!result.corpus) {
		try {
			result.corpus = std::make_shared<ibHelpCorpus>(
			    locale, ibHelpCorpus::Source::kPlatform,
			    std::vector<ibHelpEntry>{},
			    std::move(result.errors));
			result.errors.clear();
		} catch (...) {
			// If even the empty-corpus alloc fails fall to the secondary
			// no-error fallback below; appData's GetHelpCorpus invariant
			// is best-effort under OOM and a null shared_ptr here means
			// "process is past saving anyway".
		}
	}
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
