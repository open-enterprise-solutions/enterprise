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
// Footprint probe — reports the real sizeof on this build/platform.
// Not an assertion (the number is informational); run the suite and read
// the printed line. Used to measure the ibValue memory-reduction arc
// (Phase 0 baseline → after each phase). See docs/value-audit.md.
// ===========================================================================

TEST(ValueTest, SizeofReport) {
    RecordProperty("sizeof_ibValue", (int)sizeof(ibValue));
    RecordProperty("sizeof_ibNumber", (int)sizeof(ibNumber));
    RecordProperty("sizeof_wxString", (int)sizeof(wxString));
    std::cout << "[ footprint ] sizeof(ibValue)="  << sizeof(ibValue)
              << "  sizeof(ibNumber)=" << sizeof(ibNumber)
              << "  sizeof(wxString)=" << sizeof(wxString) << std::endl;
    SUCCEED();
}

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

// ---------------------------------------------------------------------------
// Character POINTERS must land as strings, not as Boolean.
//
// Every test above hands ibValue a wxString, which is exactly why none of them
// caught this: a raw `const char*` / `const wchar_t*` has a STANDARD conversion
// to bool, which outranks the user-defined one to wxString. With the string
// ctors declared `char*` (non-const), `ibValue v = wxEmptyString` — and
// wxEmptyString IS a `const wxChar*` — silently produced Boolean TRUE, and
// GetString() then answered "True" instead of "". That reached a user as a
// document numbered "True0000001" (2026-08-03).
//
// The same trap had already been found once for `const ibValue*` and closed
// with an overload there; these pin the character-pointer half of the family.
// ---------------------------------------------------------------------------

TEST(ValueTest, EmptyStringPointerIsStringNotBoolean) {
    ibValue v = wxEmptyString;
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_TRUE(v.GetString().IsEmpty());
    EXPECT_NE(v.GetString(), wxT("True"));
}

TEST(ValueTest, WideLiteralIsStringNotBoolean) {
    ibValue v = wxT("text");
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_EQ(v.GetString(), wxT("text"));
}

TEST(ValueTest, NarrowLiteralIsStringNotBoolean) {
    ibValue v = "narrow";
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_EQ(v.GetString(), wxT("narrow"));
}

TEST(ValueTest, AssignEmptyStringPointerIsStringNotBoolean) {
    ibValue v(ibNumber(1));
    v = wxEmptyString;
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_TRUE(v.GetString().IsEmpty());
}

TEST(ValueTest, AssignWideLiteralIsStringNotBoolean) {
    ibValue v(ibNumber(1));
    v = wxT("assigned");
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_STRING);
    EXPECT_EQ(v.GetString(), wxT("assigned"));
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

// ===========================================================================
// TYPE_CONST_REFFER — non-owning, read-only reference (const-meta refactor).
//
// Guards the trap where `value = someConstPtr` (e.g. a const ibValueMetaObject*
// from GetMetaObject()) silently bound to operator=(bool) — const ptr -> bool —
// and turned the object into a Boolean. The new operator=(const ibValue*) stores
// it as TYPE_CONST_REFFER: weak (no ref-count, Reset never deletes), read-only,
// but read paths delegate to the object. See docs/value-const-reffer.md.
// ===========================================================================

namespace {
// Minimal aggregate used as a const-ref target. Tracks its own destruction so a
// test can assert the const-ref never deletes a non-owned object, and overrides
// GetString so we can verify read delegation reaches the object.
class ConstRefProbe : public ibValue {
public:
    explicit ConstRefProbe(bool* deletedFlag)
        : ibValue(ibValueTypes::TYPE_VALUE, false), m_deleted(deletedFlag) {}
    ~ConstRefProbe() override { if (m_deleted) *m_deleted = true; }
    wxString GetString() const override { return wxT("PROBE"); }
private:
    bool* m_deleted;
};
} // namespace

TEST(ValueConstRef, AssignConstPtrBindsReferenceNotBoolean) {
    bool deleted = false;
    ConstRefProbe probe(&deleted);
    const ibValue* cp = &probe;
    ibValue v;
    v = cp;  // must pick operator=(const ibValue*), NOT operator=(bool)
    // GetType() delegates through the reference, so check the slot's own kind.
    EXPECT_TRUE(v.IsConstReference());
    EXPECT_EQ(v.GetRef(), &probe);   // resolves to the object, not a Boolean
}

TEST(ValueConstRef, Predicates) {
    bool deleted = false;
    ConstRefProbe probe(&deleted);
    ibValue v; v = static_cast<const ibValue*>(&probe);
    EXPECT_TRUE(v.IsReference());
    EXPECT_TRUE(v.IsConstReference());
}

TEST(ValueConstRef, ReadDelegatesToObject) {
    bool deleted = false;
    ConstRefProbe probe(&deleted);
    ibValue v; v = static_cast<const ibValue*>(&probe);
    EXPECT_EQ(v.GetRef(), &probe);                  // resolve reaches the object
    EXPECT_TRUE(v.GetString() == wxT("PROBE"));     // read delegates through union
}

TEST(ValueConstRef, ResetDoesNotDeleteNonOwned) {
    bool deleted = false;
    ConstRefProbe* probe = new ConstRefProbe(&deleted);
    {
        ibValue v;
        v = static_cast<const ibValue*>(probe);
        v.Reset();                 // must NOT DecrRef/delete the non-owned object
        EXPECT_FALSE(deleted);
    }                              // ibValue dtor — also must not delete
    EXPECT_FALSE(deleted);
    delete probe;                  // we own it
    EXPECT_TRUE(deleted);
}

TEST(ValueConstRef, SlotStaysReassignable) {
    // Reset excludes TYPE_CONST_REFFER from the write-denied throw, so a slot
    // holding a const-ref can be reassigned (unlike a true const literal).
    bool deleted = false;
    ConstRefProbe probe(&deleted);
    ibValue v; v = static_cast<const ibValue*>(&probe);
    ibValue n(42);
    EXPECT_NO_THROW(v = n);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_FALSE(deleted);          // reassign didn't delete the non-owned object
}

TEST(ValueConstRef, CopyIsWeakAndReadOnly) {
    bool deleted = false;
    ConstRefProbe probe(&deleted);
    ibValue v; v = static_cast<const ibValue*>(&probe);
    ibValue copy(v);                // copy ctor → weak, no IncrRef
    EXPECT_TRUE(copy.IsConstReference());
    EXPECT_EQ(copy.GetRef(), &probe);
    EXPECT_FALSE(deleted);
}

// ===========================================================================
// NULL vs EMPTY — TYPE_NULL is a SQL null (the driver yields it); TYPE_EMPTY is
// Undefined (a composite with no type chosen yet). They are DISTINCT, and the
// query NULL semantics (test_queryParity) rely on the distinction.
// ===========================================================================

TEST(ValueNull, EmptyIsNotNull) {
    ibValue v;
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_EMPTY);
    EXPECT_TRUE(v.IsEmpty());
    EXPECT_FALSE(v.IsNull());
}

TEST(ValueNull, SqlNullIsNull) {
    ibValue v(ibValueTypes::TYPE_NULL);
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NULL);
    EXPECT_TRUE(v.IsNull());
}

TEST(ValueNull, EmptyAndNullAreDistinctTypes) {
    EXPECT_NE(ibValue().GetType(), ibValue(ibValueTypes::TYPE_NULL).GetType());
}

// ===========================================================================
// Primitive value accessors (typed reads)
// ===========================================================================

TEST(ValueAccess, NumberReadsBack) {
    ibValue v(ibNumber(42));
    EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER);
    EXPECT_EQ(v.GetInteger(), 42);
    EXPECT_EQ(v.GetNumber(), ibNumber(42));
}

TEST(ValueAccess, BooleanReadsBack) {
    EXPECT_TRUE(ibValue(true).GetBoolean());
    EXPECT_FALSE(ibValue(false).GetBoolean());
}

TEST(ValueAccess, StringReadsBack) {
    EXPECT_EQ(ibValue(wxString(wxT("hi"))).GetString(), wxT("hi"));
}

// ===========================================================================
// GetHashKey — deterministic per value (used as a map key); distinct values
// must not collide on the obvious cases.
// ===========================================================================

TEST(ValueHash, DeterministicForSameValue) {
    EXPECT_EQ(ibValue(ibNumber(7)).GetHashKey(), ibValue(ibNumber(7)).GetHashKey());
    EXPECT_EQ(ibValue(wxString(wxT("k"))).GetHashKey(),
              ibValue(wxString(wxT("k"))).GetHashKey());
}

TEST(ValueHash, DiffersForDifferentNumbers) {
    EXPECT_NE(ibValue(ibNumber(1)).GetHashKey(), ibValue(ibNumber(2)).GetHashKey());
}
