#ifndef _IB_SCRIPT_CHECK_H_
#define _IB_SCRIPT_CHECK_H_

////////////////////////////////////////////////////////////////////////////
//	Description : ibCheckScript — compile a module text, report, never run
////////////////////////////////////////////////////////////////////////////
//
// COMPILE ONLY. The text goes through the same compiler the designer uses and
// is thrown away: nothing is registered, no module is replaced, the open
// configuration never learns it happened. An empty answer means it compiles.
//
// WHY IT LIVES HERE. The body used to sit in an anonymous namespace inside
// plugin/pluginHost.cpp, which made the plugin boundary the OWNER of a
// mechanism rather than a window onto one. It has two consumers now — the
// plugin capability (ibPluginScript::Check) and the MCP server in the core —
// and a mechanism with two consumers belongs to its own subsystem, next to the
// compiler whose door it is. The plugin host delegates here and holds nothing.
//
// The answer is ibDiagnostic (backend_diagnostic.h): the failure as DATA —
// module, line, position, code, message — so a caller can navigate to it
// instead of parsing a sentence.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/backend_diagnostic.h"

#include <vector>
#include <wx/string.h>

// TEXT ON ITS OWN. `moduleName` is what the messages are written against — pass
// the name of the module being edited so a report reads the way the designer's
// would.
//
// ⭐⭐ GIVE IT A CONFIGURATION AND IT IS JUDGED IN ONE. Without `metaData` the text
// is compiled with no parent context, so nothing outside the snippet resolves —
// including the platform's own 98 global functions, which made every call to one
// an error: `Message("x")` came back as *"Procedure or function not detected
// (Message)"* while the same text inside a module compiled clean (2026-09-01).
// A checker that flags the built-ins is worse than none.
//
// With it, the compile is parented to the module manager that configuration
// compiles against — Manager, the metatype collections and the globals — which
// is the same context a real module sees (Max: *"you have to take the session's
// root module; from the session you get the root descriptor"*).
//
// ⚠ IT IS PASSED, NOT REACHED FOR. Several configurations can be open at once
// and the active one is not necessarily the caller's.
BACKEND_API std::vector<ibDiagnostic> ibCheckScript(const wxString& text,
	const wxString& moduleName = wxT("module"),
	const class ibMetaData* metaData = nullptr);

// WHY A SECOND DOOR. The designer's *Syntax control* button does NOT compile a
// bare text: it resolves the module through the configuration's compile cache
// and compiles it against the metaobject that owns it, so the object's own
// attributes, its parent's context and every cross-module export resolve. A
// check that skips that reports failures the designer would not — and misses
// the ones it would. For a caller writing code into a configuration (an
// assistant, a build step) the weaker answer is the harmful one: plausible and
// wrong. This door is the button's own road.
enum class ibScriptCheckOutcome {
	Checked,      // the module was compiled; m_diagnostics is the verdict
	Unreachable,  // no compile module for this metaobject — nothing was checked
};

struct ibScriptCheckAnswer {
	ibScriptCheckOutcome      m_outcome = ibScriptCheckOutcome::Unreachable;
	std::vector<ibDiagnostic> m_diagnostics;

	// Clean means CHECKED and clean. "Nothing was checked" is not a pass, and
	// saying so is the whole reason the outcome is carried separately: the
	// designer's own button returns true when it finds no compile module, which
	// is fine for a person watching and wrong for anything acting on the answer.
	bool IsClean() const {
		return m_outcome == ibScriptCheckOutcome::Checked && m_diagnostics.empty();
	}
};

BACKEND_API ibScriptCheckAnswer ibCheckModule(const class ibValueMetaObject* metaObject);

#endif // _IB_SCRIPT_CHECK_H_
