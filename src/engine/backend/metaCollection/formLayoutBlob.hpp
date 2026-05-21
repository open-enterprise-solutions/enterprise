/////////////////////////////////////////////////////////////////////////////
// formLayoutBlob — neutral DTO + parser interface for the on-disk form
// layout blob held by `ibValueMetaObjectFormBase::GetFormData()`.
//
// STATUS (2026-05-21): DEFERRED — backend-only parsing of the binary
// chunk format is blocked because the per-control payloads in
// `eDataBlock` are read positionally by frontend control classes
// (`ibValueFrame::LoadData` overrides under
// `src/engine/frontend/visualView/ctrl/`). Backend has the outer
// chunk envelope (`ibReaderMemory::open_chunk*`) but no neutral schema
// describing which property fields each control class emits in which
// order. Pulling that schema into backend requires either:
//   (a) mirroring ~25 control-class property layouts as backend-side
//       declarative schemas (large, two-place updates on every new
//       control), or
//   (b) splitting `ibValueFrame` into a data half (backend) + a visual
//       half (frontend) — cross-cutting refactor across all controls,
//       or
//   (c) introducing a parallel XML form-DSL that Designer writes
//       alongside the binary blob, and MCP reads/writes the XML.
//
// This header keeps the DTO types in backend (no wxWidgets GUI deps —
// only `wxString`, `std::vector`, `std::map`, `int`) so that whichever
// approach lands later, the agent-facing surface is already shaped.
//
// Two-DLL boundary: backend.dll only includes `wx/string.h`,
// `wx/buffer.h`, and standard library — NEVER any GUI header. The
// `ibFormLayoutSerializer` methods below are declared but their
// implementations return the "GUI dependency" error code; do NOT add
// a real parser here that walks per-control payloads — that
// implementation belongs in frontend or behind a backend-side schema
// registry.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_FORM_LAYOUT_BLOB_HPP_
#define _IB_FORM_LAYOUT_BLOB_HPP_

#include <wx/string.h>
#include <wx/buffer.h>

#include <map>
#include <vector>

#include "backend/backend.h"

// -----------------------------------------------------------------------
// Stable error codes shared with the MCP tools surface. Agents key off
// these strings — keep them stable across refactors.
// -----------------------------------------------------------------------
namespace ibFormLayoutError {

// The form blob requires GUI symbols (frontend control classes) to
// parse. Backend cannot read it without architectural changes — see
// the header comment above.
BACKEND_API extern const wxChar* const kGuiDependency;        // "OES_E_FORM_BLOB_GUI_DEPENDENCY"

// The path resolved to an object that is not a form (the agent passed
// a wrong fullName).
BACKEND_API extern const wxChar* const kNotAForm;             // "OES_E_NOT_A_FORM"

// The path didn't resolve to any object at all.
BACKEND_API extern const wxChar* const kNotFound;             // "OES_E_NOT_FOUND"

// The form has no stored blob yet (empty buffer — newly created
// form that the user hasn't opened in Designer yet).
BACKEND_API extern const wxChar* const kEmptyBlob;            // "OES_E_FORM_BLOB_EMPTY"

} // namespace ibFormLayoutError

// -----------------------------------------------------------------------
// DTO — what the future parser will produce and the future serializer
// will consume. Kept in backend so MCP can hold instances without
// touching GUI types.
// -----------------------------------------------------------------------
struct BACKEND_API ibFormLayoutGeometry {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

struct BACKEND_API ibFormLayoutControl {
    wxString                                id;        // stable identifier (control GUID or generated)
    wxString                                kind;      // "Button", "TextCtrl", "CheckBox", "Group", ...
    wxString                                name;      // programmatic identifier (script-visible)
    std::map<wxString, wxString>            synonym;   // locale -> label (e.g. {"ru": "Кнопка"})
    wxString                                binding;   // bound attribute path (or empty)
    ibFormLayoutGeometry                    geometry;
    std::vector<ibFormLayoutControl>        children;
};

struct BACKEND_API ibFormLayoutBlob {
    wxString                                formKind;  // "ItemForm" / "ListForm" / "ChoiceForm" / ...
    std::vector<ibFormLayoutControl>        controls;
};

// -----------------------------------------------------------------------
// Serializer — parsing/serializing methods. CURRENTLY DEFERRED: both
// methods return false and set `error` to the kGuiDependency code.
// Tests in `tests/test_formLayoutBlob.cpp` lock that contract in so
// the day someone reaches for a partial implementation they have to
// confront the architectural decision in the header.
// -----------------------------------------------------------------------
class BACKEND_API ibFormLayoutSerializer {
public:
    // Parse the on-disk form data blob into a DTO. Returns true on
    // success. On failure sets `error` to a kFormLayoutError code and
    // returns false; `out` is left in a valid-but-empty state.
    static bool ParseFromBlob(const wxMemoryBuffer& blob,
                              ibFormLayoutBlob& out,
                              wxString& error);

    // Serialise a DTO back into the on-disk form data blob. Same
    // failure contract.
    static bool SerializeToBlob(const ibFormLayoutBlob& in,
                                wxMemoryBuffer& out,
                                wxString& error);
};

// -----------------------------------------------------------------------
// Validator — DTO-level checks (no GUI deps). Each issue carries a
// stable `code` so agents can branch programmatically; `path` is the
// dotted control name path for surfacing in editor UIs.
// -----------------------------------------------------------------------
class BACKEND_API ibFormLayoutValidator {
public:
    struct Issue {
        wxString code;       // e.g. "DUPLICATE_ID" / "NEGATIVE_GEOMETRY"
        wxString message;    // human-readable
        wxString path;       // dotted path "Form.Group1.Button2"
    };

    // Pure DTO-shape validation. Does NOT consult the owner object's
    // attribute list (that is the future responsibility once the
    // parser produces real control kinds). Empty controls[] is valid.
    static std::vector<Issue> Validate(const ibFormLayoutBlob& blob);
};

#endif // _IB_FORM_LAYOUT_BLOB_HPP_
