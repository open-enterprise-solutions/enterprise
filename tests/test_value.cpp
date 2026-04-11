// =============================================================================
// OES Enterprise — ibValue type conversion tests
//
// Tests the ibValue class from src/engine/backend/compiler/value.h,
// covering construction and conversion for the four primitive types:
// TYPE_NUMBER, TYPE_STRING, TYPE_BOOLEAN, TYPE_DATE.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/compiler/value.h"

// ===========================================================================
// TYPE_BOOLEAN
// ===========================================================================

TEST(ValueTest, BooleanTrue) {
    ibValue v(true);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_BOOLEAN);
    EXPECT_TRUE(v.GetBoolean());
}

TEST(ValueTest, BooleanFalse) {
    ibValue v(false);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_BOOLEAN);
    EXPECT_FALSE(v.GetBoolean());
}

TEST(ValueTest, BooleanToString) {
    ibValue vTrue(true);
    ibValue vFalse(false);
    // GetString() on a boolean should return a non-empty representation
    EXPECT_FALSE(vTrue.GetString().IsEmpty());
    EXPECT_FALSE(vFalse.GetString().IsEmpty());
}

// ===========================================================================
// TYPE_NUMBER
// ===========================================================================

TEST(ValueTest, NumberFromInt) {
    ibValue v(42);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_EQ(v.GetInteger(), 42);
}

TEST(ValueTest, NumberFromDouble) {
    ibValue v(3.14);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_NEAR(v.GetDouble(), 3.14, 1e-9);
}

TEST(ValueTest, NumberFromUnsigned) {
    ibValue v(100u);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_EQ(v.GetUInteger(), 100u);
}

TEST(ValueTest, NumberGetBoolean) {
    ibValue zero(0);
    ibValue nonzero(7);
    EXPECT_FALSE(zero.GetBoolean());
    EXPECT_TRUE(nonzero.GetBoolean());
}

TEST(ValueTest, NumberToString) {
    ibValue v(123);
    wxString s = v.GetString();
    EXPECT_FALSE(s.IsEmpty());
}

// ===========================================================================
// TYPE_STRING
// ===========================================================================

TEST(ValueTest, StringConstruction) {
    ibValue v(wxString(wxT("hello")));
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_EQ(v.GetString(), wxT("hello"));
}

TEST(ValueTest, EmptyString) {
    ibValue v(wxString(wxT("")));
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_TRUE(v.GetString().IsEmpty());
}

// ===========================================================================
// TYPE_DATE
// ===========================================================================

TEST(ValueTest, DateFromComponents) {
    ibValue v(2025, 1, 15, 10, 30, 0);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_DATE);
    int y, m, d;
    v.FromDate(y, m, d);
    EXPECT_EQ(y, 2025);
    EXPECT_EQ(m, 1);
    EXPECT_EQ(d, 15);
}

TEST(ValueTest, DateFromDateTime) {
    wxDateTime dt(15, wxDateTime::Jan, 2025, 10, 30, 0);
    ibValue v(dt);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_DATE);
    wxDateTime recovered = v.GetDateTime();
    EXPECT_EQ(recovered.GetYear(), 2025);
    EXPECT_EQ(recovered.GetMonth(), wxDateTime::Jan);
    EXPECT_EQ(recovered.GetDay(), 15);
}

// ===========================================================================
// TYPE_EMPTY (default construction)
// ===========================================================================

TEST(ValueTest, DefaultIsEmpty) {
    ibValue v;
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_EMPTY);
    EXPECT_TRUE(v.IsEmpty());
}

// ===========================================================================
// Copy and assignment
// ===========================================================================

TEST(ValueTest, CopyConstructor) {
    ibValue original(42);
    ibValue copy(original);
    EXPECT_EQ(copy.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_EQ(copy.GetInteger(), 42);
}

TEST(ValueTest, AssignmentOperator) {
    ibValue v;
    v = 99;
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_EQ(v.GetInteger(), 99);
}

TEST(ValueTest, AssignString) {
    ibValue v;
    v = wxString(wxT("test"));
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_EQ(v.GetString(), wxT("test"));
}

TEST(ValueTest, AssignBoolean) {
    ibValue v;
    v = true;
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_BOOLEAN);
    EXPECT_TRUE(v.GetBoolean());
}

// ===========================================================================
// Comparison operators
// ===========================================================================

TEST(ValueTest, NumberEquality) {
    ibValue a(10);
    ibValue b(10);
    ibValue c(20);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(ValueTest, NumberOrdering) {
    ibValue a(5);
    ibValue b(10);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(b >= a);
}

TEST(ValueTest, StringEquality) {
    ibValue a(wxString(wxT("abc")));
    ibValue b(wxString(wxT("abc")));
    ibValue c(wxString(wxT("xyz")));
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

// ===========================================================================
// SetType — change value type
// ===========================================================================

TEST(ValueTest, SetTypeChangesType) {
    ibValue v(42);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    v.SetType(ibValueTypes::TYPE_STRING);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
}
