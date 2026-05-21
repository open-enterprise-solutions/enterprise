/////////////////////////////////////////////////////////////////////////////
// test_formLayoutBlob — locks in the DTO + validator contract for the
// form-layout authoring surface (MCP form_layout_read / form_layout_set).
//
// STATUS (2026-05-21): the serializer is DEFERRED — see
// `src/engine/backend/metaCollection/formLayoutBlob.hpp` for the
// architectural blocker. These tests assert the deferral contract:
// the parser MUST report `OES_E_FORM_BLOB_GUI_DEPENDENCY` on any
// non-empty input and `OES_E_FORM_BLOB_EMPTY` on empty input. When
// the real parser lands these assertions flip naturally — the test
// file is the contract.
//
// The validator runs against agent-supplied DTOs even today (the MCP
// write path lints input before bouncing it on the deferral) so the
// validator side has real assertions on duplicate id, missing kind,
// negative geometry, and deeply-nested handling.
/////////////////////////////////////////////////////////////////////////////

#include "gtest/gtest.h"

#include "backend/metaCollection/formLayoutBlob.hpp"

// -----------------------------------------------------------------------
// Serializer contract — deferred. Empty blob -> kEmptyBlob, otherwise
// kGuiDependency. The day the parser lands these flip to checking the
// actual round-trip; the test file documents which assertions move.
// -----------------------------------------------------------------------

TEST(FormLayoutSerializer, ParseRejectsEmptyBlobWithEmptyCode)
{
    wxMemoryBuffer empty;
    ibFormLayoutBlob out;
    wxString err;
    EXPECT_FALSE(ibFormLayoutSerializer::ParseFromBlob(empty, out, err));
    EXPECT_EQ(err, wxString(ibFormLayoutError::kEmptyBlob));
    EXPECT_TRUE(out.controls.empty());
    EXPECT_TRUE(out.formKind.IsEmpty());
}

TEST(FormLayoutSerializer, ParseDeferredOnNonEmptyBlob)
{
    // Any non-empty buffer must hit the GUI dependency code today.
    // Real shape doesn't matter — the parser bails before reading.
    wxMemoryBuffer buf;
    const unsigned char payload[] = { 0x01, 0x02, 0x03, 0x04 };
    buf.AppendData(payload, sizeof(payload));

    ibFormLayoutBlob out;
    wxString err;
    EXPECT_FALSE(ibFormLayoutSerializer::ParseFromBlob(buf, out, err));
    EXPECT_EQ(err, wxString(ibFormLayoutError::kGuiDependency));
}

TEST(FormLayoutSerializer, SerializeAlwaysDeferred)
{
    ibFormLayoutBlob in;
    in.formKind = wxT("ItemForm");
    ibFormLayoutControl btn;
    btn.id      = wxT("ctl_001");
    btn.kind    = wxT("Button");
    btn.name    = wxT("OkButton");
    btn.synonym[wxT("ru")] = wxT("ОК");
    in.controls.push_back(btn);

    wxMemoryBuffer out;
    wxString err;
    EXPECT_FALSE(ibFormLayoutSerializer::SerializeToBlob(in, out, err));
    EXPECT_EQ(err, wxString(ibFormLayoutError::kGuiDependency));
}

// -----------------------------------------------------------------------
// Validator — real assertions today. Used by form_layout_set before
// the deferral bounce so agent input is shaken out for free.
// -----------------------------------------------------------------------

TEST(FormLayoutValidator, AcceptsEmptyControls)
{
    ibFormLayoutBlob blob;
    blob.formKind = wxT("ItemForm");
    const auto issues = ibFormLayoutValidator::Validate(blob);
    EXPECT_TRUE(issues.empty());
}

TEST(FormLayoutValidator, AcceptsSingleValidControl)
{
    ibFormLayoutBlob blob;
    ibFormLayoutControl btn;
    btn.id   = wxT("ctl_001");
    btn.kind = wxT("Button");
    btn.name = wxT("OkButton");
    btn.geometry.width  = 80;
    btn.geometry.height = 24;
    blob.controls.push_back(btn);
    const auto issues = ibFormLayoutValidator::Validate(blob);
    EXPECT_TRUE(issues.empty());
}

TEST(FormLayoutValidator, FlagsMissingKind)
{
    ibFormLayoutBlob blob;
    ibFormLayoutControl ctrl;
    ctrl.name = wxT("X");
    blob.controls.push_back(ctrl);
    const auto issues = ibFormLayoutValidator::Validate(blob);
    bool found = false;
    for (const auto& iss : issues)
        if (iss.code == wxT("MISSING_KIND")) found = true;
    EXPECT_TRUE(found);
}

TEST(FormLayoutValidator, FlagsMissingName)
{
    ibFormLayoutBlob blob;
    ibFormLayoutControl ctrl;
    ctrl.kind = wxT("Button");
    blob.controls.push_back(ctrl);
    const auto issues = ibFormLayoutValidator::Validate(blob);
    bool found = false;
    for (const auto& iss : issues)
        if (iss.code == wxT("MISSING_NAME")) found = true;
    EXPECT_TRUE(found);
}

TEST(FormLayoutValidator, FlagsDuplicateId)
{
    ibFormLayoutBlob blob;
    ibFormLayoutControl a;
    a.id = wxT("dup");
    a.kind = wxT("Button"); a.name = wxT("A");
    ibFormLayoutControl b;
    b.id = wxT("dup");
    b.kind = wxT("Button"); b.name = wxT("B");
    blob.controls.push_back(a);
    blob.controls.push_back(b);
    const auto issues = ibFormLayoutValidator::Validate(blob);
    bool found = false;
    for (const auto& iss : issues)
        if (iss.code == wxT("DUPLICATE_ID")) found = true;
    EXPECT_TRUE(found);
}

TEST(FormLayoutValidator, FlagsNegativeGeometry)
{
    ibFormLayoutBlob blob;
    ibFormLayoutControl ctrl;
    ctrl.kind = wxT("Button");
    ctrl.name = wxT("Bad");
    ctrl.geometry.width = -5;
    blob.controls.push_back(ctrl);
    const auto issues = ibFormLayoutValidator::Validate(blob);
    bool found = false;
    for (const auto& iss : issues)
        if (iss.code == wxT("NEGATIVE_GEOMETRY")) found = true;
    EXPECT_TRUE(found);
}

TEST(FormLayoutValidator, WalksDeeplyNestedTree)
{
    // Group > Notebook > Panel > Button — duplicate id at innermost
    // level must be caught despite the depth.
    ibFormLayoutBlob blob;
    ibFormLayoutControl group;
    group.kind = wxT("Group"); group.name = wxT("Outer"); group.id = wxT("g1");

    ibFormLayoutControl notebook;
    notebook.kind = wxT("Notebook"); notebook.name = wxT("Nb"); notebook.id = wxT("nb1");

    ibFormLayoutControl panel;
    panel.kind = wxT("Panel"); panel.name = wxT("P"); panel.id = wxT("p1");

    ibFormLayoutControl btn1;
    btn1.kind = wxT("Button"); btn1.name = wxT("B1"); btn1.id = wxT("shared");
    ibFormLayoutControl btn2;
    btn2.kind = wxT("Button"); btn2.name = wxT("B2"); btn2.id = wxT("shared");

    panel.children.push_back(btn1);
    panel.children.push_back(btn2);
    notebook.children.push_back(panel);
    group.children.push_back(notebook);
    blob.controls.push_back(group);

    const auto issues = ibFormLayoutValidator::Validate(blob);
    bool foundDup = false;
    wxString dupPath;
    for (const auto& iss : issues) {
        if (iss.code == wxT("DUPLICATE_ID")) {
            foundDup = true;
            dupPath  = iss.path;
        }
    }
    EXPECT_TRUE(foundDup);
    EXPECT_TRUE(dupPath.Contains(wxT("Outer.Nb.P.B2")));
}

TEST(FormLayoutValidator, IdUniquenessIsTreeWide)
{
    // Same id used at sibling-of-sibling positions across the tree.
    ibFormLayoutBlob blob;
    ibFormLayoutControl a;
    a.id = wxT("x"); a.kind = wxT("Group"); a.name = wxT("A");
    ibFormLayoutControl b;
    b.id = wxT("x"); b.kind = wxT("Button"); b.name = wxT("B");
    blob.controls.push_back(a);
    blob.controls.push_back(b);
    const auto issues = ibFormLayoutValidator::Validate(blob);
    int dupCount = 0;
    for (const auto& iss : issues)
        if (iss.code == wxT("DUPLICATE_ID")) ++dupCount;
    EXPECT_EQ(dupCount, 1);
}

// -----------------------------------------------------------------------
// Error code stability — agents key off these strings. If they ever
// move, every downstream code path must be audited; the test pins them.
// -----------------------------------------------------------------------

TEST(FormLayoutErrorCodes, AreStableStrings)
{
    EXPECT_STREQ(ibFormLayoutError::kGuiDependency,
                 wxT("OES_E_FORM_BLOB_GUI_DEPENDENCY"));
    EXPECT_STREQ(ibFormLayoutError::kNotAForm,
                 wxT("OES_E_NOT_A_FORM"));
    EXPECT_STREQ(ibFormLayoutError::kNotFound,
                 wxT("OES_E_NOT_FOUND"));
    EXPECT_STREQ(ibFormLayoutError::kEmptyBlob,
                 wxT("OES_E_FORM_BLOB_EMPTY"));
}
