// =============================================================================
// OES Enterprise — ibValuePoint / ibValueSize / ibValueColour tests
//
// The thin geometric script value types (backend/system/value/value{Point,
// Size,Colour}.h): each wraps a wx type, round-trips through its conversion
// operator, formats via GetString and reports IsEmpty for the wx "default/invalid"
// value. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/system/value/valuePoint.h"
#include "backend/system/value/valueSize.h"
#include "backend/system/value/valueColour.h"

// ---------------------------------------------------------------------------
// ibValuePoint
// ---------------------------------------------------------------------------

TEST(ValuePoint, RoundTripsThroughOperator) {
    ibValuePoint p(wxPoint(3, 4));
    EXPECT_EQ(static_cast<wxPoint>(p), wxPoint(3, 4));
    EXPECT_EQ(p.m_point, wxPoint(3, 4));
    EXPECT_FALSE(p.IsEmpty());
    EXPECT_FALSE(p.GetString().IsEmpty());
}

TEST(ValuePoint, DefaultPositionIsEmpty) {
    ibValuePoint p(wxDefaultPosition);
    EXPECT_TRUE(p.IsEmpty());
}

// ---------------------------------------------------------------------------
// ibValueSize
// ---------------------------------------------------------------------------

TEST(ValueSize, RoundTripsThroughOperator) {
    ibValueSize s(wxSize(10, 20));
    EXPECT_EQ(static_cast<wxSize>(s), wxSize(10, 20));
    EXPECT_EQ(s.m_size, wxSize(10, 20));
    EXPECT_FALSE(s.IsEmpty());
    EXPECT_FALSE(s.GetString().IsEmpty());
}

TEST(ValueSize, DefaultSizeIsEmpty) {
    ibValueSize s(wxDefaultSize);
    EXPECT_TRUE(s.IsEmpty());
}

// ---------------------------------------------------------------------------
// ibValueColour
// ---------------------------------------------------------------------------

TEST(ValueColour, RoundTripsThroughOperator) {
    ibValueColour c(wxColour(255, 0, 0));
    EXPECT_EQ(static_cast<wxColour>(c), wxColour(255, 0, 0));
    EXPECT_FALSE(c.IsEmpty());
    EXPECT_FALSE(c.GetString().IsEmpty());
}

TEST(ValueColour, InvalidColourIsEmpty) {
    ibValueColour c(wxColour{});   // default-constructed wxColour is not Ok
    EXPECT_TRUE(c.IsEmpty());
}
