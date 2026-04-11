// =============================================================================
// OES Enterprise — CLSID roundtrip tests
//
// Tests string_to_clsid() and clsid_to_string() defined in
// src/engine/backend/clsid.h.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/backend_core.h"

// ---------------------------------------------------------------------------
// Roundtrip: string -> clsid -> string must recover the original (padded to 8)
// ---------------------------------------------------------------------------

TEST(ClsidTest, RoundtripSevenCharString) {
    const wxString input = wxT("VL_NUMB");
    ibClassID id = string_to_clsid(input);
    EXPECT_NE(id, 0u);
    wxString recovered = clsid_to_string(id);
    // string_to_clsid pads shorter strings with spaces to 8 chars
    EXPECT_TRUE(recovered.StartsWith(input));
}

TEST(ClsidTest, RoundtripExactEightChars) {
    const wxString input = wxT("ABCDEFGH");
    ibClassID id = string_to_clsid(input);
    EXPECT_NE(id, 0u);
    wxString recovered = clsid_to_string(id);
    EXPECT_EQ(recovered, input);
}

// ---------------------------------------------------------------------------
// Known CLSIDs used throughout the codebase
// ---------------------------------------------------------------------------

TEST(ClsidTest, KnownCLSIDs) {
    // Each known CLSID string must produce a distinct, non-zero value
    const wxString known[] = {
        wxT("VL_UNDF"), wxT("VL_BOOL"), wxT("VL_NUMB"),
        wxT("VL_DATE"), wxT("VL_STRI"), wxT("VL_NULL"),
    };
    std::set<ibClassID> ids;
    for (const auto& name : known) {
        ibClassID id = string_to_clsid(name);
        EXPECT_NE(id, 0u) << "CLSID for " << name.ToStdString() << " should not be zero";
        ids.insert(id);
    }
    // All IDs must be unique
    EXPECT_EQ(ids.size(), std::size(known));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(ClsidTest, EmptyStringProducesZero) {
    EXPECT_EQ(string_to_clsid(wxT("")), 0u);
}

TEST(ClsidTest, ZeroClsidProducesEmptyString) {
    EXPECT_EQ(clsid_to_string(0), wxEmptyString);
}

TEST(ClsidTest, SingleCharString) {
    const wxString input = wxT("A");
    ibClassID id = string_to_clsid(input);
    EXPECT_NE(id, 0u);
    wxString recovered = clsid_to_string(id);
    // First char must be 'A', rest padded with spaces
    EXPECT_EQ(recovered[0], 'A');
}

TEST(ClsidTest, DifferentStringsDifferentIds) {
    ibClassID a = string_to_clsid(wxT("MD_CAT "));
    ibClassID b = string_to_clsid(wxT("MD_DOC "));
    EXPECT_NE(a, b);
}
