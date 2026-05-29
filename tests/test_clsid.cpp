// =============================================================================
// OES Enterprise — CLSID roundtrip tests
//
// Tests string_to_clsid() and clsid_to_string() defined in
// src/engine/backend/clsid.h.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/backend_core.h"

// ---------------------------------------------------------------------------
// FNV-1a hash contract. The legacy reversible 8-byte ASCII pack was replaced
// by a one-way FNV-1a hash (clsid.h), so string -> clsid no longer round-trips:
// clsid_to_string returns a hex form of the hash, NOT the original string.
// These tests pin the current contract — determinism + the hex representation.
// ---------------------------------------------------------------------------

TEST(ClsidTest, HashIsDeterministic) {
    const wxString input = wxT("VL_NUMB");
    ibClassID id = string_to_clsid(input);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(id, string_to_clsid(input)); // same input -> same hash, every time
}

TEST(ClsidTest, ClsidToStringIsHexOfHash) {
    const wxString input = wxT("ABCDEFGH");
    ibClassID id = string_to_clsid(input);
    EXPECT_NE(id, 0u);
    // clsid_to_string is now a debug hex of the 64-bit hash, not an inverse.
    EXPECT_EQ(clsid_to_string(id),
              wxString::Format(wxT("0x%016llX"), static_cast<uint64_t>(id)));
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

TEST(ClsidTest, SingleCharHashesNonZeroAndLengthSensitive) {
    ibClassID id = string_to_clsid(wxT("A"));
    EXPECT_NE(id, 0u);
    // FNV-1a folds length in, so "A" and "AA" hash to different ids.
    EXPECT_NE(id, string_to_clsid(wxT("AA")));
}

TEST(ClsidTest, DifferentStringsDifferentIds) {
    ibClassID a = string_to_clsid(wxT("MD_CAT "));
    ibClassID b = string_to_clsid(wxT("MD_DOC "));
    EXPECT_NE(a, b);
}
