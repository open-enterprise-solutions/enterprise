/////////////////////////////////////////////////////////////////////////////
// basXmlReader — pull a BAS / 1С Configuration.xml-style export tree into
// an OES mutations[] array.
//
// The output shape matches `oes_template_get` / `oes_template_customize`
// passthrough: { summary: {...}, mutations: [{op,kind,fullName,
// properties|source|syntaxMode}, ...] }. The wizard Applier on the
// other end (Pugi / Designer) consumes this verbatim.
//
// Design choices:
//   - Single facade — three internal layers (root manifest scan, per-
//     object dispatch, type qualifier mapping in basMapping). Caller
//     gets one entry point per format.
//   - Reads via wxXmlDocument so no extra deps (already linked in
//     backend.dll for ibXmlReader).
//   - Object filter applied on the manifest-level enumeration so we
//     don't open files we won't emit.
//   - Errors are STRUCTURED, not thrown. metaBridge expects throw-by-
//     value for runtime, but migration is offline work — a malformed
//     input file should surface as a warning, not nuke the whole batch.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_BAS_XML_READER_HPP_
#define _IB_BAS_XML_READER_HPP_

#include "3rdparty/nlohmann/json.hpp"

#include <wx/string.h>

#include <string>
#include <vector>

namespace migration {
namespace bas {

// Per-file outcome bucket. Counters drive the summary; warnings are a
// flat list ready to feed into the MCP response. Mutations[] is the
// payload the Applier consumes.
struct ImportResult {
	int totalScanned     = 0;
	int imported         = 0;
	int skippedDeleted   = 0;
	int skippedDeferred  = 0;
	int skippedUnknown   = 0;
	int skippedFiltered  = 0;
	int parseFailures    = 0;
	std::vector<wxString>      warnings;
	std::vector<nlohmann::json> mutations;

	// Per-kind import tally — used to populate summary.counts.
	std::vector<std::pair<wxString, int>> countsByKind;

	// Diagnostics — populated only on fatal failure (root manifest
	// missing, XML unreadable). When set, the MCP layer maps to
	// OES_E_BAS_PARSE_FAIL.
	bool      fatal     = false;
	wxString  fatalCode;
	wxString  fatalMsg;
};

// Glob-style name filter. Pattern syntax:
//   "Catalog.Контрагенты"   — exact match
//   "Catalog.*"             — every Catalog
//   "*"                     — everything
// Multiple patterns OR-combine; empty vector = no filter.
using ObjectFilter = std::vector<wxString>;

// Parameters for a single import run. Mirrors the import_bas_xml MCP
// input shape; the MCP tool layer converts JSON -> this struct.
struct ImportOptions {
	wxString     configurationPath;   // path to root Configuration.xml
	wxString     objectsRoot;         // dir holding per-object subtrees;
	                                  // empty -> dirname(configurationPath)
	ObjectFilter filter;
	bool         skipDeleted = true;
	bool         preview     = false; // currently no different from non-preview
	                                  // at this layer — Applier decides apply vs
	                                  // dry-run upstream. Stored for parity with
	                                  // the MCP request.
};

// Run a full BAS XML import. Populates the ImportResult and returns it
// by value. Callers should check `fatal` before trusting the counters.
ImportResult ImportXmlConfiguration(const ImportOptions& opts);

// Helpers exposed for the unit tests — let test_basMigration drive a
// single per-object file without spinning up a Configuration.xml manifest.
//
// ImportSingleObjectFile reads `path`, dispatches on the root element
// kind, and appends mutations[] / warnings to `out`. Returns true on
// successful parse; false records a parse failure in `out` and skips.
bool ImportSingleObjectFile(const wxString& path, ImportResult& out);

// Synthetic helper used by tests + the in-memory variant: parse the
// contents of `xmlText` as a single per-object file. Same accumulation
// semantics as ImportSingleObjectFile.
bool ImportSingleObjectFromText(const wxString& xmlText, ImportResult& out);

} // namespace bas
} // namespace migration

#endif // _IB_BAS_XML_READER_HPP_
