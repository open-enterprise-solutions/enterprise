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
#include "core/guid.h"
#include "backend/backend_exception.h"
#include "backend/system/value/valueGuid.h"

// ---------------------------------------------------------------------------
// Generation + validity
// ---------------------------------------------------------------------------

TEST(Guid, NewGuidIsValid) {
    EXPECT_TRUE(ibGuid(ibGuid::newGuid()).isValid());
}

TEST(Guid, DefaultIsInvalidNil) {
    EXPECT_FALSE(ibGuid().isValid());      // all-zero nil UUID
}

TEST(Guid, TwoNewGuidsDiffer) {
    EXPECT_NE(ibGuid(ibGuid::newGuid()), ibGuid(ibGuid::newGuid()));
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
    const wxString s = ibGuid(ibGuid::newGuid()).str();
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
    EXPECT_EQ(ibGuid(ibGuid::newGuid()).bytes().size(), 16u);
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

// New Guid("0000...") is the empty identity. isValid() stays false for the nil
// GUID, which means "no object", and the script value still holds it.
TEST(ValueGuid, NilStringIsAnEmptyGuid)
{
	const wxString nil = wxT("00000000-0000-0000-0000-000000000000");
	ibValue text(nil);
	ibValue* params[] = { &text };

	ibValueGuid guid;
	ASSERT_TRUE(guid.Init(params, 1));
	EXPECT_TRUE(guid.IsEmpty());
	EXPECT_EQ(guid.GetString(), nil);

	ibValueGuid other;
	ASSERT_TRUE(other.Init(params, 1));
	EXPECT_TRUE(guid.CompareValueEQ(other));

	ibValue realText(wxT("6fab82bc-d41d-4664-b0f6-bb61b0ed78d8"));
	ibValue* realParams[] = { &realText };
	ibValueGuid real;
	ASSERT_TRUE(real.Init(realParams, 1));
	EXPECT_FALSE(real.IsEmpty());
	EXPECT_FALSE(guid.CompareValueEQ(real));

	ibValue junk(wxT("not-a-guid"));
	ibValue* bad[] = { &junk };
	ibValueGuid refused;
	EXPECT_THROW(refused.Init(bad, 1), ibBackendException);
}

// The text tells a guid from not one; the nil guid's text is one. A refusal leaves the guid as it was.
TEST(Guid, ParseTellsTheNilGuidFromText)
{
	ibGuid parsed(ibGuid::newGuid());
	const ibGuid before = parsed;
	EXPECT_FALSE(ibGuid::parse(wxT("not-a-guid"), parsed));
	EXPECT_FALSE(ibGuid::parse(wxT("6fab82bc-d41d-4664-b0f6"), parsed));
	EXPECT_EQ(parsed, before);

	EXPECT_TRUE(ibGuid::parse(wxT("00000000-0000-0000-0000-000000000000"), parsed));
	EXPECT_FALSE(parsed.isValid());

	EXPECT_TRUE(ibGuid::parse(wxT("6fab82bc-d41d-4664-b0f6-bb61b0ed78d8"), parsed));
	EXPECT_EQ(parsed.str(), wxT("6fab82bc-d41d-4664-b0f6-bb61b0ed78d8"));
}
