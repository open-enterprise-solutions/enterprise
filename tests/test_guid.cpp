// =============================================================================
// OES Enterprise — ibGuid tests
//
// Covers ibGuid (backend/guid.h): generation, validity, value semantics,
// the canonical 8-4-4-4-12 string form and its round-trip, equality and a
// total ordering. ibGuid is the identity of every reference value in the
// system (see reference-as-key), so its round-trip and ordering are
// load-bearing for the DB key path.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/guid.h"

// ---------------------------------------------------------------------------
// Generation + validity
// ---------------------------------------------------------------------------

TEST(Guid, NewGuidIsValid) {
    EXPECT_TRUE(ibGuid::newGuid().isValid());
}

TEST(Guid, DefaultIsInvalidNil) {
    EXPECT_FALSE(ibGuid().isValid());      // all-zero nil UUID
}

TEST(Guid, TwoNewGuidsDiffer) {
    EXPECT_NE(ibGuid::newGuid(), ibGuid::newGuid());
}

TEST(Guid, ResetMakesInvalid) {
    ibGuid g = ibGuid::newGuid();
    ASSERT_TRUE(g.isValid());
    g.reset();
    EXPECT_FALSE(g.isValid());
}

// ---------------------------------------------------------------------------
// String form + round-trip
// ---------------------------------------------------------------------------

TEST(Guid, StrIsCanonical36) {
    const wxString s = ibGuid::newGuid().str();
    EXPECT_EQ(s.length(), 36u);            // 8-4-4-4-12 + 4 hyphens
    EXPECT_EQ(s.Freq(wxT('-')), 4);
}

TEST(Guid, RoundTripViaString) {
    const ibGuid g = ibGuid::newGuid();
    const wxString s = g.str();
    const ibGuid parsed(s);                // wxString ctor
    EXPECT_EQ(parsed, g);
    EXPECT_EQ(parsed.str(), s);
}

// ---------------------------------------------------------------------------
// Value semantics
// ---------------------------------------------------------------------------

TEST(Guid, CopyEquals) {
    const ibGuid g = ibGuid::newGuid();
    const ibGuid copy(g);
    EXPECT_EQ(copy, g);
}

TEST(Guid, AssignEquals) {
    const ibGuid g = ibGuid::newGuid();
    ibGuid a;
    a = g;
    EXPECT_EQ(a, g);
}

TEST(Guid, SelfEquality) {
    const ibGuid g = ibGuid::newGuid();
    EXPECT_TRUE(g == g);
    EXPECT_FALSE(g != g);
}

TEST(Guid, BytesAre16) {
    EXPECT_EQ(ibGuid::newGuid().bytes().size(), 16u);
}

// ---------------------------------------------------------------------------
// Ordering — strict, antisymmetric, consistent with operator>
// ---------------------------------------------------------------------------

TEST(Guid, OrderingIsAntisymmetric) {
    const ibGuid a = ibGuid::newGuid();
    const ibGuid b = ibGuid::newGuid();
    if (a != b) {
        EXPECT_NE(a < b, b < a);           // exactly one direction holds
        EXPECT_EQ(a < b, b > a);           // '<' and '>' agree
    }
    EXPECT_FALSE(a < a);                   // irreflexive
}
