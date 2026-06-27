// =============================================================================
// OES Enterprise — ibUniqueKey / ibUniqueKeyPair tests
//
// ibUniqueKey (backend/uniqueKey.h) is GUID-based row identity AND the
// form-instance identity every open form carries. ibUniqueKeyPair extends it
// with a composite (metaID -> value) key for register records, while keeping a
// fresh per-instance GUID as the stable form-instance handle. Identity,
// validity and equality here are load-bearing for record reads and for
// FindFormBySourceUniqueKey. Pure — no DB.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/uniqueKey.h"
#include "backend/guid.h"

// ---------------------------------------------------------------------------
// ibUniqueKey — GUID identity
// ---------------------------------------------------------------------------

TEST(UniqueKey, DefaultIsNotOk) {
    ibUniqueKey k;                        // nil guid
    EXPECT_FALSE(k.IsOk());
    EXPECT_FALSE(k.isValid());
}

TEST(UniqueKey, FromGuidIsOk) {
    ibUniqueKey k(ibGuid::newGuid());
    EXPECT_TRUE(k.IsOk());
    EXPECT_TRUE(k.isValid());
}

TEST(UniqueKey, CarriesItsGuid) {
    const ibGuid g = ibGuid::newGuid();
    ibUniqueKey k(g);
    EXPECT_EQ(k.GetGuid(), g);
    EXPECT_TRUE(k == g);                  // operator==(ibGuid)
}

TEST(UniqueKey, EqualForSameGuid) {
    const ibGuid g = ibGuid::newGuid();
    EXPECT_TRUE(ibUniqueKey(g) == ibUniqueKey(g));
}

TEST(UniqueKey, UnequalForDifferentGuid) {
    EXPECT_TRUE(ibUniqueKey(ibGuid::newGuid()) != ibUniqueKey(ibGuid::newGuid()));
}

TEST(UniqueKey, ConvertsToGuidStringForm) {
    const ibGuid g = ibGuid::newGuid();
    ibUniqueKey k(g);
    EXPECT_EQ(static_cast<wxString>(k), g.str());
}

TEST(UniqueKey, ResetMakesInvalid) {
    ibUniqueKey k(ibGuid::newGuid());
    ASSERT_TRUE(k.IsOk());
    k.reset();
    EXPECT_FALSE(k.IsOk());
}

TEST(UniqueKey, OrderingIsAntisymmetric) {
    ibUniqueKey a(ibGuid::newGuid());
    ibUniqueKey b(ibGuid::newGuid());
    if (a != b) {
        EXPECT_NE(a < b, b < a);          // exactly one direction
        EXPECT_EQ(a < b, b > a);
    }
    EXPECT_FALSE(a < a);                  // irreflexive
}

// ---------------------------------------------------------------------------
// ibUniqueKeyPair — composite key, fresh per-instance form GUID
// ---------------------------------------------------------------------------

TEST(UniqueKeyPair, EmptyIsNotOkButHasGuid) {
    ibUniqueKeyPair p;                    // no composite keys yet
    EXPECT_FALSE(p.IsOk());               // IsOk requires populated composite key
    EXPECT_TRUE(p.isValid());             // ...but m_objGuid is a fresh wxNewUniqueGuid
}

TEST(UniqueKeyPair, EachInstanceHasDistinctGuid) {
    ibUniqueKeyPair p1;
    ibUniqueKeyPair p2;
    EXPECT_NE(p1.GetGuid(), p2.GetGuid());   // stable per-instance form identity
}

TEST(UniqueKeyPair, MissingKeyLookup) {
    ibUniqueKeyPair p;
    EXPECT_FALSE(p.FindKey(12345));          // no such dimension
}
