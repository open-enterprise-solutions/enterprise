/////////////////////////////////////////////////////////////////////////////
// dumpHelp — CLI tool that emits OES's built-in language registries as
// JSON to stdout. Consumed by tools/help-skeleton.py (Phase 2) to
// generate the help-corpus bucket skeletons.
//
// No GUI, no database, no metadata configuration. Just reads:
//   - s_listKeyWord                              from translateCode.cpp
//   - ibValueSystemFunction::PrepareNames()      from systemManager.cpp
//
// Per-configuration metadata enumeration (Catalogs/Documents/Registers
// attribute and method dumps) is Phase 5, not Phase 2, and lives elsewhere.
//
// Output schema (single JSON document on stdout):
//
//   {
//     "format": "OES-HELP-DUMP-1.0",
//     "schema_version": 1,
//     "keywords":   [{"name_en": "If", "category": "core"}, ...],
//     "functions":  [{"name_en": "Message", "param_count": 2,
//                     "signature": "...", "is_procedure": false}, ...]
//   }
//
// Intentionally minimal — the skeleton generator owns the OES-HELP-1.0
// bucket layout. dumpHelp is just the "what built-ins exist" feed.
/////////////////////////////////////////////////////////////////////////////

#include <wx/init.h>
#include <wx/string.h>

#include "backend/compiler/value.h"
#include "backend/system/systemManager.h"
#include "3rdparty/nlohmann/json.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

// translateCode.cpp keeps s_listKeyWord file-static. The cleanest way
// to walk it without a circular header is the existing ibTranslateCode
// helpers: PrepareLexem populates the global lookup, and the keyword
// roster is available via IsKeyWord(). For the dump we just want the
// raw list, so reach through the helper that surface-exposes it.
//
// To avoid pulling translateCode internals into the public header we
// declare an extern hook that the loader calls below. The hook is
// defined inline next to s_listKeyWord — a one-line patch in
// translateCode.cpp.
extern "C" {
// Defined in translateCode.cpp. Walks s_listKeyWord in declaration
// order and invokes the callback with (name, shortDescription) once
// per entry. Returns the count.
typedef void (*ibKeyWordDumpCb)(const wxString& name,
                                 const wxString& shortDescription,
                                 void* userdata);
BACKEND_API long ibDumpKeyWords(ibKeyWordDumpCb cb, void* userdata);
}

namespace {

// Plain UTF-8 string from a wxString. nlohmann::json handles utf8 std::string
// natively; we keep wxString only at the OES-API boundary.
std::string ToUtf8(const wxString& s) {
	const wxScopedCharBuffer u = s.utf8_str();
	return std::string(u.data(), u.length());
}

void EmitKeywords(nlohmann::json& root) {
	nlohmann::json arr = nlohmann::json::array();
	auto cb = [](const wxString& name, const wxString& shortDescription,
	             void* userdata) {
		auto* arrPtr = static_cast<nlohmann::json*>(userdata);
		nlohmann::json entry;
		entry["name_en"]          = ToUtf8(name);
		entry["short_description"] = ToUtf8(shortDescription);
		// Bucket assignment is the skeleton-generator's job — dumpHelp
		// stays neutral and emits the raw registry contents.
		arrPtr->push_back(std::move(entry));
	};
	ibDumpKeyWords(cb, &arr);
	root["keywords"] = std::move(arr);
}

void EmitSystemFunctions(nlohmann::json& root) {
	// Instantiate the system-function container so PrepareNames populates
	// its method-helper. The container is value-typed (lives on the
	// stack) — fine for one-shot enumeration. PrepareNames is idempotent
	// across instances; multiple dumpHelp invocations don't conflict.
	ibValueSystemFunction sys;
	sys.PrepareNames();

	const auto* methods = sys.GetPMethods();
	const auto& list    = methods->GetMethodList();

	nlohmann::json arr = nlohmann::json::array();
	for (const auto& m : list) {
		nlohmann::json entry;
		entry["name_en"]      = ToUtf8(m.m_fieldName);
		entry["signature"]    = ToUtf8(m.m_strHelper);
		entry["param_count"]  = static_cast<int>(m.m_paramCount);
		entry["is_procedure"] = !m.HasReturn();
		arr.push_back(std::move(entry));
	}
	root["functions"] = std::move(arr);
}

} // namespace

int main(int argc, char* argv[]) {
	// Minimal wx init — needed for wxString construction in dependent code.
	// wxApp is overkill (it expects GUI subsystem on macOS); wxEntryStart
	// is the headless pair for app-less programs.
	if (!wxInitialize(argc, argv)) {
		std::fprintf(stderr, "dumpHelp: wxInitialize failed\n");
		return 2;
	}

	nlohmann::json root;
	root["format"]         = "OES-HELP-DUMP-1.0";
	root["schema_version"] = 1;

	try {
		EmitKeywords(root);
		EmitSystemFunctions(root);
	} catch (const std::exception& ex) {
		std::fprintf(stderr, "dumpHelp: %s\n", ex.what());
		wxUninitialize();
		return 1;
	}

	std::cout << root.dump(2) << '\n';
	wxUninitialize();
	return 0;
}
