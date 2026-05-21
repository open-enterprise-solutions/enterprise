/////////////////////////////////////////////////////////////////////////////
// formLayoutBlob — neutral XML DSL implementation plus legacy-binary
// deferral. See header for the architectural blocker on the old binary
// form blob. The serializer accepts/writes the sidecar XML format and
// still reports kGuiDependency for non-XML legacy blobs.
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

#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

#include <set>

namespace ibFormLayoutError {

const wxChar* const kGuiDependency = wxT("OES_E_FORM_BLOB_GUI_DEPENDENCY");
const wxChar* const kNotAForm      = wxT("OES_E_NOT_A_FORM");
const wxChar* const kNotFound      = wxT("OES_E_NOT_FOUND");
const wxChar* const kEmptyBlob     = wxT("OES_E_FORM_BLOB_EMPTY");
const wxChar* const kInvalidDsl    = wxT("OES_E_FORM_DSL_INVALID");

} // namespace ibFormLayoutError

namespace {

wxString Attr(wxXmlNode* node, const wxString& name)
{
    if (node == nullptr) return wxEmptyString;
    return node->GetAttribute(name, wxEmptyString);
}

int AttrInt(wxXmlNode* node, const wxString& name, int fallback = 0)
{
    long v = fallback;
    if (node) node->GetAttribute(name, wxString::Format(wxT("%d"), fallback)).ToLong(&v);
    return (int)v;
}

bool LooksLikeXml(const wxMemoryBuffer& blob)
{
    const auto* p = static_cast<const unsigned char*>(blob.GetData());
    const size_t n = blob.GetDataLen();
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ch = p[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        return ch == '<';
    }
    return false;
}

bool ParseControl(wxXmlNode* node, ibFormLayoutControl& out)
{
    if (node == nullptr || node->GetName() != wxT("control"))
        return false;
    out.id      = Attr(node, wxT("id"));
    out.kind    = Attr(node, wxT("kind"));
    out.name    = Attr(node, wxT("name"));
    out.binding = Attr(node, wxT("binding"));
    out.geometry.x      = AttrInt(node, wxT("x"));
    out.geometry.y      = AttrInt(node, wxT("y"));
    out.geometry.width  = AttrInt(node, wxT("width"));
    out.geometry.height = AttrInt(node, wxT("height"));

    for (wxXmlNode* c = node->GetChildren(); c != nullptr; c = c->GetNext()) {
        if (c->GetName() == wxT("synonym")) {
            const wxString locale = Attr(c, wxT("locale"));
            if (!locale.IsEmpty())
                out.synonym[locale] = c->GetNodeContent();
        } else if (c->GetName() == wxT("control")) {
            ibFormLayoutControl child;
            if (!ParseControl(c, child))
                return false;
            out.children.push_back(std::move(child));
        }
    }
    return true;
}

wxXmlNode* BuildControlNode(const ibFormLayoutControl& ctrl)
{
    wxXmlNode* node = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("control"));
    node->AddAttribute(wxT("id"), ctrl.id);
    node->AddAttribute(wxT("kind"), ctrl.kind);
    node->AddAttribute(wxT("name"), ctrl.name);
    if (!ctrl.binding.IsEmpty())
        node->AddAttribute(wxT("binding"), ctrl.binding);
    node->AddAttribute(wxT("x"), wxString::Format(wxT("%d"), ctrl.geometry.x));
    node->AddAttribute(wxT("y"), wxString::Format(wxT("%d"), ctrl.geometry.y));
    node->AddAttribute(wxT("width"), wxString::Format(wxT("%d"), ctrl.geometry.width));
    node->AddAttribute(wxT("height"), wxString::Format(wxT("%d"), ctrl.geometry.height));

    for (const auto& kv : ctrl.synonym) {
        wxXmlNode* syn = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("synonym"));
        syn->AddAttribute(wxT("locale"), kv.first);
        syn->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, kv.second));
        node->AddChild(syn);
    }
    for (const auto& child : ctrl.children)
        node->AddChild(BuildControlNode(child));
    return node;
}

} // namespace

bool ibFormLayoutSerializer::ParseFromBlob(const wxMemoryBuffer& blob,
                                            ibFormLayoutBlob& out,
                                            wxString& error)
{
    out = ibFormLayoutBlob();
    if (blob.GetDataLen() == 0) {
        error = ibFormLayoutError::kEmptyBlob;
        return false;
    }
    if (!LooksLikeXml(blob)) {
        error = ibFormLayoutError::kGuiDependency;
        return false;
    }

    const auto* raw = static_cast<const char*>(blob.GetData());
    wxString xmlText = wxString::FromUTF8(raw, blob.GetDataLen());
    wxStringInputStream stream(xmlText);
    wxXmlDocument doc;
    {
        wxLogNull suppress;
        if (!doc.Load(stream)) {
            error = ibFormLayoutError::kInvalidDsl;
            return false;
        }
    }
    wxXmlNode* root = doc.GetRoot();
    if (root == nullptr || root->GetName() != wxT("FormLayout")) {
        error = ibFormLayoutError::kInvalidDsl;
        return false;
    }
    out.formKind = Attr(root, wxT("formKind"));
    for (wxXmlNode* c = root->GetChildren(); c != nullptr; c = c->GetNext()) {
        if (c->GetName() != wxT("control"))
            continue;
        ibFormLayoutControl ctrl;
        if (!ParseControl(c, ctrl)) {
            error = ibFormLayoutError::kInvalidDsl;
            out = ibFormLayoutBlob();
            return false;
        }
        out.controls.push_back(std::move(ctrl));
    }
    error.clear();
    return true;
}

bool ibFormLayoutSerializer::SerializeToBlob(const ibFormLayoutBlob& in,
                                              wxMemoryBuffer& out,
                                              wxString& error)
{
    out = wxMemoryBuffer();
    wxXmlDocument doc;
    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("FormLayout"));
    root->AddAttribute(wxT("version"), wxT("1"));
    root->AddAttribute(wxT("formKind"), in.formKind);
    doc.SetRoot(root);
    for (const auto& ctrl : in.controls)
        root->AddChild(BuildControlNode(ctrl));

    wxMemoryOutputStream stream;
    if (!doc.Save(stream, 2)) {
        error = ibFormLayoutError::kInvalidDsl;
        return false;
    }
    const size_t len = stream.GetLength();
    std::vector<char> bytes(len);
    if (len > 0) {
        stream.CopyTo(bytes.data(), len);
        out.AppendData(bytes.data(), len);
    }
    error.clear();
    return true;
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
