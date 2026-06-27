// =============================================================================
// OES Enterprise — ibValueContainer (script Map / Dictionary) tests
//
// ibValueContainer (backend/system/value/valueMap.h) is the script keyed
// container: Insert / GetAt / SetAt / Property / Delete over a
// std::map<ibValue, ibValue>. Keys and values are ibValues; lookup uses the
// container's ibValue comparator. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include <map>
#include "backend/system/value/valueMap.h"

namespace {
ibValue Key(const wxChar* k) { return ibValue(wxString(k)); }
}

TEST(ValueContainer, EmptyByDefault) {
    ibValueContainer c;
    EXPECT_TRUE(c.IsEmpty());
    EXPECT_EQ(c.Count(), 0u);
}

TEST(ValueContainer, InsertAndGetAt) {
    ibValueContainer c;
    c.Insert(Key(wxT("k")), ibValue(ibNumber(42)));
    EXPECT_EQ(c.Count(), 1u);
    ibValue out;
    EXPECT_TRUE(c.GetAt(Key(wxT("k")), out));
    EXPECT_EQ(out.GetInteger(), 42);
}

// Property is the NON-throwing lookup: a miss returns false. (GetAt, by
// contrast, raises "Key not found" outside designer mode — script-error
// semantics — so it is not used for miss-testing here.)
TEST(ValueContainer, PropertyMissReturnsFalse) {
    ibValueContainer c;
    ibValue out;
    EXPECT_FALSE(c.Property(Key(wxT("nope")), out));
}

// SetAt delegates to Insert, which is insert-once (raises "Key already using"
// on an existing key). So SetAt ADDS a new key; it does not update in place.
TEST(ValueContainer, SetAtAddsNewKey) {
    ibValueContainer c;
    c.SetAt(Key(wxT("k")), ibValue(ibNumber(5)));
    EXPECT_EQ(c.Count(), 1u);
    ibValue out;
    ASSERT_TRUE(c.Property(Key(wxT("k")), out));
    EXPECT_EQ(out.GetInteger(), 5);
}

TEST(ValueContainer, PropertyLooksUpValue) {
    ibValueContainer c;
    c.Insert(Key(wxT("a")), ibValue(ibNumber(7)));
    ibValue found;
    EXPECT_TRUE(c.Property(Key(wxT("a")), found));
    EXPECT_EQ(found.GetInteger(), 7);
}

TEST(ValueContainer, DeleteRemovesKey) {
    ibValueContainer c;
    c.Insert(Key(wxT("a")), ibValue(ibNumber(1)));
    c.Insert(Key(wxT("b")), ibValue(ibNumber(2)));
    c.Delete(Key(wxT("a")));
    EXPECT_EQ(c.Count(), 1u);
    ibValue out;
    EXPECT_FALSE(c.Property(Key(wxT("a")), out));   // gone (no-throw lookup)
    EXPECT_TRUE (c.Property(Key(wxT("b")), out));
}

TEST(ValueContainer, ClearEmpties) {
    ibValueContainer c;
    c.Insert(Key(wxT("a")), ibValue(ibNumber(1)));
    c.Clear();
    EXPECT_TRUE(c.IsEmpty());
}

TEST(ValueContainer, ConstructFromMap) {
    std::map<ibValue, ibValue> m;
    m[Key(wxT("x"))] = ibValue(ibNumber(9));
    ibValueContainer c(m);
    EXPECT_EQ(c.Count(), 1u);
    ibValue out;
    ASSERT_TRUE(c.GetAt(Key(wxT("x")), out));
    EXPECT_EQ(out.GetInteger(), 9);
}
