// =============================================================================
// OES Enterprise — ibValuePoint / ibValueSize / ibValueColour tests
//
// The thin geometric script value types (backend/system/value/value{Point,
// Size,Colour}.h): each holds the core's data (core/graphics), formats via
// GetString and reports IsEmpty for the "default/unset" value. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/system/value/valuePoint.h"
#include "backend/system/value/valueSize.h"
#include "backend/system/value/valueColour.h"

// ---------------------------------------------------------------------------
// ibValuePoint
// ---------------------------------------------------------------------------

TEST(ValuePoint, HoldsThePoint) {
    ibValuePoint p(ibPoint(3, 4));
    EXPECT_TRUE(p.m_point == ibPoint(3, 4));
    EXPECT_FALSE(p.IsEmpty());
    EXPECT_FALSE(p.GetString().IsEmpty());
}

TEST(ValuePoint, DefaultPositionIsEmpty) {
    ibValuePoint p(ibDefaultPosition);
    EXPECT_TRUE(p.IsEmpty());
}

// ---------------------------------------------------------------------------
// ibValueSize
// ---------------------------------------------------------------------------

TEST(ValueSize, HoldsTheSize) {
    ibValueSize s(ibSize(10, 20));
    EXPECT_TRUE(s.m_size == ibSize(10, 20));
    EXPECT_FALSE(s.IsEmpty());
    EXPECT_FALSE(s.GetString().IsEmpty());
}

TEST(ValueSize, DefaultSizeIsEmpty) {
    ibValueSize s(ibDefaultSize);
    EXPECT_TRUE(s.IsEmpty());
}

// ---------------------------------------------------------------------------
// ibValueColour
// ---------------------------------------------------------------------------

TEST(ValueColour, HoldsTheColour) {
    ibValueColour c(ibColour(255, 0, 0));
    EXPECT_TRUE(c.m_colour == ibColour(255, 0, 0));
    EXPECT_FALSE(c.IsEmpty());
    EXPECT_FALSE(c.GetString().IsEmpty());
}

TEST(ValueColour, UnsetColourIsEmpty) {
    ibValueColour c(ibColour{});   // a default ibColour is no colour at all
    EXPECT_TRUE(c.IsEmpty());
}
