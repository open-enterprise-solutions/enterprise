// =============================================================================
// OES Enterprise — ibMetaDescription tests
//
// ibMetaDescription (backend/typeDescription.h) is a list of metaIDs — the
// metadata-side "allowed types" for the binding descriptions (Owner, Generation,
// RegisterRecord, ChartOfAccounts, ChartOfCharacteristicTypes). metaID, not guid,
// because it lives INSIDE the metadata tree (a guid->id resolve at load would hit
// not-yet-loaded refs). Pure — no DB.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include "backend/typeDescription.h"   // ibMetaDescription

TEST(MetaDescription, DefaultIsNotOk) {
    ibMetaDescription md;
    EXPECT_FALSE(md.IsOk());
    EXPECT_EQ(md.GetTypeCount(), 0u);
}

TEST(MetaDescription, SingleId) {
    ibMetaDescription md(42);
    EXPECT_TRUE(md.IsOk());
    EXPECT_EQ(md.GetTypeCount(), 1u);
    EXPECT_TRUE(md.ContainMetaType(42));
    EXPECT_FALSE(md.ContainMetaType(43));
    EXPECT_EQ(md.GetByIdx(0), 42);
}

TEST(MetaDescription, MultipleIds) {
    ibMetaDescription md(std::vector<ibMetaID>{ 1, 2, 3 });
    EXPECT_EQ(md.GetTypeCount(), 3u);
    EXPECT_TRUE(md.ContainMetaType(2));
    EXPECT_FALSE(md.ContainMetaType(9));
}

TEST(MetaDescription, AppendAndClear) {
    ibMetaDescription md;
    md.AppendMetaType(10);
    md.AppendMetaType(20);
    EXPECT_EQ(md.GetTypeCount(), 2u);
    md.ClearMetaType();
    EXPECT_FALSE(md.IsOk());
}

TEST(MetaDescription, SetDefaultReplaces) {
    ibMetaDescription md(1);
    md.SetDefaultMetaType(99);     // clears, then appends
    EXPECT_EQ(md.GetTypeCount(), 1u);
    EXPECT_TRUE(md.ContainMetaType(99));
    EXPECT_FALSE(md.ContainMetaType(1));
}
