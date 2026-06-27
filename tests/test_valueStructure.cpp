// =============================================================================
// OES Enterprise — ibValueStructure tests
//
// ibValueStructure (backend/system/value/valueMap.h) is the script Structure:
// a STRING-keyed ibValueContainer (1C-style `New Structure("F1, F2", v1, v2)`).
// Keys must be strings; lookup via the non-throwing Property. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include <map>
#include "backend/system/value/valueMap.h"

namespace {
ibValue Field(const wxChar* k) { return ibValue(wxString(k)); }
}

TEST(ValueStructure, EmptyByDefault) {
    ibValueStructure s;
    EXPECT_TRUE(s.IsEmpty());
    EXPECT_EQ(s.Count(), 0u);
}

TEST(ValueStructure, InsertStringKeyAndProperty) {
    ibValueStructure s;
    s.Insert(Field(wxT("Field1")), ibValue(ibNumber(10)));
    EXPECT_EQ(s.Count(), 1u);
    ibValue out;
    ASSERT_TRUE(s.Property(Field(wxT("Field1")), out));
    EXPECT_EQ(out.GetInteger(), 10);
}

TEST(ValueStructure, PropertyMissReturnsFalse) {
    ibValueStructure s;
    ibValue out;
    EXPECT_FALSE(s.Property(Field(wxT("Nope")), out));
}

TEST(ValueStructure, ConstructFromStringMap) {
    std::map<wxString, ibValue> m;
    m[wxT("A")] = ibValue(ibNumber(1));
    m[wxT("B")] = ibValue(ibNumber(2));
    ibValueStructure s(m);
    EXPECT_EQ(s.Count(), 2u);
    ibValue out;
    ASSERT_TRUE(s.Property(Field(wxT("B")), out));
    EXPECT_EQ(out.GetInteger(), 2);
}

TEST(ValueStructure, DeleteRemovesField) {
    ibValueStructure s;
    s.Insert(Field(wxT("A")), ibValue(ibNumber(1)));
    s.Insert(Field(wxT("B")), ibValue(ibNumber(2)));
    s.Delete(Field(wxT("A")));
    EXPECT_EQ(s.Count(), 1u);
    ibValue out;
    EXPECT_FALSE(s.Property(Field(wxT("A")), out));
    EXPECT_TRUE (s.Property(Field(wxT("B")), out));
}

TEST(ValueStructure, ClearEmpties) {
    ibValueStructure s;
    s.Insert(Field(wxT("A")), ibValue(ibNumber(1)));
    s.Clear();
    EXPECT_TRUE(s.IsEmpty());
}
