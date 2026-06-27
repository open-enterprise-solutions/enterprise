// =============================================================================
// OES Enterprise — ibTypeDescription tests
//
// ibTypeDescription (backend/typeDescription.h) is the attribute type model:
// a list of allowed type CLSIDs + number/date/string qualifiers (precision,
// scale, length). Every attribute, query column and DDL column carries one, so
// its predicates (ContainType), qualifiers and value-equality are load-bearing.
// Pure — no DB / appData.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include "backend/typeDescription.h"

// ---------------------------------------------------------------------------
// Construction + validity
// ---------------------------------------------------------------------------

TEST(TypeDescription, DefaultIsNotOk) {
    ibTypeDescription td;
    EXPECT_FALSE(td.IsOk());
    EXPECT_EQ(td.GetClsidCount(), 0u);
}

TEST(TypeDescription, SingleClsidIsOk) {
    ibTypeDescription td(g_valueNumberCLSID);
    EXPECT_TRUE(td.IsOk());
    EXPECT_EQ(td.GetClsidCount(), 1u);
    EXPECT_EQ(td.GetFirstClsid(), g_valueNumberCLSID);
}

TEST(TypeDescription, MultiTypeList) {
    ibTypeDescription td(std::vector<ibClassID>{ g_valueNumberCLSID, g_valueStringCLSID });
    EXPECT_EQ(td.GetClsidCount(), 2u);
    EXPECT_EQ(td.GetByIdx(0), g_valueNumberCLSID);
    EXPECT_TRUE(td.ContainType(g_valueNumberCLSID));
    EXPECT_TRUE(td.ContainType(g_valueStringCLSID));
}

// ---------------------------------------------------------------------------
// ContainType
// ---------------------------------------------------------------------------

TEST(TypeDescription, ContainTypeByValType) {
    ibTypeDescription td(g_valueNumberCLSID);
    EXPECT_TRUE(td.ContainType(ibValueTypes::TYPE_NUMBER));
    EXPECT_FALSE(td.ContainType(ibValueTypes::TYPE_STRING));
}

TEST(TypeDescription, ContainTypeByClsid) {
    ibTypeDescription td(g_valueStringCLSID);
    EXPECT_TRUE(td.ContainType(g_valueStringCLSID));
    EXPECT_FALSE(td.ContainType(g_valueNumberCLSID));
}

// ---------------------------------------------------------------------------
// Qualifiers (precision / scale / length)
// ---------------------------------------------------------------------------

TEST(TypeDescription, NumberQualifier) {
    ibTypeDescription td(g_valueNumberCLSID);
    td.SetNumber(18, 2);
    EXPECT_EQ((int)td.GetPrecision(), 18);
    EXPECT_EQ((int)td.GetScale(), 2);
}

TEST(TypeDescription, StringQualifier) {
    ibTypeDescription td(g_valueStringCLSID);
    td.SetString(50);
    EXPECT_EQ((int)td.GetLength(), 50);
}

TEST(TypeDescription, SetNumberAddsTheType) {
    ibTypeDescription td;            // empty
    td.SetNumber(10, 0);
    EXPECT_TRUE(td.ContainType(ibValueTypes::TYPE_NUMBER));
    EXPECT_TRUE(td.IsOk());
}

// ---------------------------------------------------------------------------
// Equality — type list AND qualifiers must match
// ---------------------------------------------------------------------------

TEST(TypeDescription, EqualSameTypeAndQualifier) {
    ibTypeDescription a(g_valueNumberCLSID); a.SetNumber(18, 2);
    ibTypeDescription b(g_valueNumberCLSID); b.SetNumber(18, 2);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TypeDescription, UnequalOnDifferentScale) {
    ibTypeDescription a(g_valueNumberCLSID); a.SetNumber(18, 2);
    ibTypeDescription b(g_valueNumberCLSID); b.SetNumber(18, 4);
    EXPECT_TRUE(a != b);
}

TEST(TypeDescription, UnequalOnDifferentType) {
    ibTypeDescription a(g_valueNumberCLSID);
    ibTypeDescription b(g_valueStringCLSID);
    EXPECT_TRUE(a != b);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST(TypeDescription, ClearMakesEmpty) {
    ibTypeDescription td(g_valueNumberCLSID);
    ASSERT_TRUE(td.IsOk());
    td.ClearMetaType();
    EXPECT_FALSE(td.IsOk());
    EXPECT_EQ(td.GetClsidCount(), 0u);
}
