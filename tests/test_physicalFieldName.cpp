// =============================================================================
// OES Enterprise — ibPhysicalFieldName: "fld<id>", spelled without a format string
//
// Every metadata column is stored under the name its metaID spells, and every cell
// of every read is found by it. The name used to be printed through "fld%i"; it is
// now counted and laid down by hand (metaAttributeObject.h), which is exactly the
// kind of code that is right for the ids anyone tries and wrong at the edges — so
// the edges are pinned: zero, one digit, a carry, the top of the int range, and the
// ids an int reads as negative.
//
// A metaID is an UNSIGNED 32-bit number: an int that reads negative is the upper
// half of that range and is spelled as the u32 it is — there is no minus in a
// field name (Max, 2026-09-12: "-23456 -> fld + u32"). Pure.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/metaCollection/attribute/metaAttributeObject.h"

TEST(PhysicalFieldName, SpellsTheIdAfterThePrefix) {
    EXPECT_EQ(ibPhysicalFieldName(1461), wxT("fld1461"));
    EXPECT_EQ(ibPhysicalFieldName(7),    wxT("fld7"));
    EXPECT_EQ(ibPhysicalFieldName(10),   wxT("fld10"));   // a carry into a second digit
}

TEST(PhysicalFieldName, ZeroIsOneDigit) {
    EXPECT_EQ(ibPhysicalFieldName(0), wxT("fld0"));
}

TEST(PhysicalFieldName, TheTopOfTheIntRangeFits) {
    EXPECT_EQ(ibPhysicalFieldName(2147483647), wxT("fld2147483647"));
}

// Read as negative, spelled as the u32 it is: ten digits at most, and never a sign.
TEST(PhysicalFieldName, ANegativeIntIsSpelledAsItsUnsignedValue) {
    EXPECT_EQ(ibPhysicalFieldName(-23456),          wxT("fld4294943840"));
    EXPECT_EQ(ibPhysicalFieldName(-1),              wxT("fld4294967295"));
    EXPECT_EQ(ibPhysicalFieldName(-2147483647 - 1), wxT("fld2147483648"));
}
