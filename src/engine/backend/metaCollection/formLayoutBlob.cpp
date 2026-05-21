/////////////////////////////////////////////////////////////////////////////
// formLayoutBlob — stub implementation. See header for the architectural
// blocker. The serializer always reports kGuiDependency; the validator
// performs DTO-level checks that are useful TODAY (round-tripping
// agent-supplied trees, catching duplicate ids, etc.) so the surface is
// real even though the on-disk parse path is deferred.
//
// IMPORTANT: backend.dll boundary — no GUI headers allowed here. The
// chunk-level binary envelope IS readable from backend (the chunk
// reader/writer pair lives in `backend/io/`), but each control's
// payload requires the matching frontend class to interpret. Don't
// "peek" inside the envelope from this file — the resulting blob
// snippets are useless without the schema, and the half-implementation
// invites bugs.
/////////////////////////////////////////////////////////////////////////////

#include "metaCollection/formLayoutBlob.hpp"

#include <set>

namespace ibFormLayoutError {

const wxChar* const kGuiDependency = wxT("OES_E_FORM_BLOB_GUI_DEPENDENCY");
const wxChar* const kNotAForm      = wxT("OES_E_NOT_A_FORM");
const wxChar* const kNotFound      = wxT("OES_E_NOT_FOUND");
const wxChar* const kEmptyBlob     = wxT("OES_E_FORM_BLOB_EMPTY");

} // namespace ibFormLayoutError

// -----------------------------------------------------------------------
// Serializer — DEFERRED. Both directions return the GUI-dependency
// code. Implementations stay short so the next agent immediately
// hits the architectural note in the header instead of stumbling
// through a half-finished parser.
// -----------------------------------------------------------------------
bool ibFormLayoutSerializer::ParseFromBlob(const wxMemoryBuffer& blob,
                                            ibFormLayoutBlob& out,
                                            wxString& error)
{
    out = ibFormLayoutBlob();
    if (blob.GetDataLen() == 0) {
        error = ibFormLayoutError::kEmptyBlob;
        return false;
    }
    error = ibFormLayoutError::kGuiDependency;
    return false;
}

bool ibFormLayoutSerializer::SerializeToBlob(const ibFormLayoutBlob& /*in*/,
                                              wxMemoryBuffer& out,
                                              wxString& error)
{
    out = wxMemoryBuffer();
    error = ibFormLayoutError::kGuiDependency;
    return false;
}

// -----------------------------------------------------------------------
// Validator — DTO-level checks. Useful even with the parser deferred
// because the future MCP write path will produce DTOs that this
// validator can lint before any serializer is reached.
// -----------------------------------------------------------------------
namespace {

void WalkControls(const std::vector<ibFormLayoutControl>& controls,
                  const wxString& parentPath,
                  std::set<wxString>& seenIds,
                  std::vector<ibFormLayoutValidator::Issue>& issues)
{
    for (const auto& ctrl : controls) {
        const wxString here = parentPath.IsEmpty()
            ? ctrl.name
            : parentPath + wxT(".") + ctrl.name;

        if (ctrl.kind.IsEmpty()) {
            ibFormLayoutValidator::Issue iss;
            iss.code    = wxT("MISSING_KIND");
            iss.message = wxT("control has no kind");
            iss.path    = here;
            issues.push_back(iss);
        }
        if (ctrl.name.IsEmpty()) {
            ibFormLayoutValidator::Issue iss;
            iss.code    = wxT("MISSING_NAME");
            iss.message = wxT("control has no name");
            iss.path    = here;
            issues.push_back(iss);
        }
        if (!ctrl.id.IsEmpty()) {
            if (seenIds.find(ctrl.id) != seenIds.end()) {
                ibFormLayoutValidator::Issue iss;
                iss.code    = wxT("DUPLICATE_ID");
                iss.message = wxT("control id is not unique: ") + ctrl.id;
                iss.path    = here;
                issues.push_back(iss);
            } else {
                seenIds.insert(ctrl.id);
            }
        }
        if (ctrl.geometry.width < 0 || ctrl.geometry.height < 0
            || ctrl.geometry.x < 0 || ctrl.geometry.y < 0) {
            ibFormLayoutValidator::Issue iss;
            iss.code    = wxT("NEGATIVE_GEOMETRY");
            iss.message = wxT("geometry components must be non-negative");
            iss.path    = here;
            issues.push_back(iss);
        }
        WalkControls(ctrl.children, here, seenIds, issues);
    }
}

} // namespace

std::vector<ibFormLayoutValidator::Issue>
ibFormLayoutValidator::Validate(const ibFormLayoutBlob& blob)
{
    std::vector<Issue> issues;
    std::set<wxString> seenIds;
    WalkControls(blob.controls, wxString(), seenIds, issues);
    return issues;
}
