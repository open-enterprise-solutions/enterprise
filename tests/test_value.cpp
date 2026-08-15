// =============================================================================
// OES Enterprise — ibValue type conversion tests
//
// Tests the ibValue class from src/engine/backend/compiler/value.h,
// covering construction and conversion for the four primitive types:
// TYPE_NUMBER, TYPE_STRING, TYPE_BOOLEAN, TYPE_DATE.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/compiler/value.h"
#include "backend/system/value/valueArray.h"   // ValueHashContract — composite keys

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
// GetValueHash — deterministic per value (it keys every hash index in the
// engine); distinct values must not collide on the obvious cases.
//
// These used to test GetHashKey, the rendered wxString identity. That one was
// removed on 2026-08-15 — a value's identity is the value, and the hash is
// bound to CompareValueLS instead (see ValueHashContract below for the rule
// that binding has to satisfy).
// ===========================================================================

TEST(ValueHash, DeterministicForSameValue) {
    EXPECT_EQ(ibValue(ibNumber(7)).GetValueHash(), ibValue(ibNumber(7)).GetValueHash());
    EXPECT_EQ(ibValue(wxString(wxT("k"))).GetValueHash(),
              ibValue(wxString(wxT("k"))).GetValueHash());
}

TEST(ValueHash, DiffersForDifferentNumbers) {
    EXPECT_NE(ibValue(ibNumber(1)).GetValueHash(), ibValue(ibNumber(2)).GetValueHash());
}

// ===========================================================================
// Ordering ACROSS kinds — a scalar and a string are separated, not compared.
//
// Neither coercion states a fact about such a pair: "abc" read as a number is
// 0, and 1 read as text is "1". So the order places the two kinds in different
// stretches (numeric-ish first, text after) and only compares payloads within a
// kind. Totality is the point — std::map keys and std::sort need every pair
// placed — and these tests are what says the placement was chosen, not stumbled
// into by whichever coercion the left-hand tag happened to trigger.
//
// Coercion survives WITHIN the numeric-ish kinds, where it is a fact: True is 1.
// ===========================================================================

TEST(ValueOrderAcrossKinds, NumberOrdersBeforeString) {
    EXPECT_TRUE (ibValue(1) < ibValue(wxString(wxT("abc"))));
    EXPECT_FALSE(ibValue(wxString(wxT("abc"))) < ibValue(1));
}

// The case that pins the rule down: with coercion, "0" would become 0 and this
// would read false. The kinds are apart, so the digits in the string are not
// consulted at all.
TEST(ValueOrderAcrossKinds, NumberOrdersBeforeStringThatLooksSmaller) {
    EXPECT_TRUE (ibValue(1) < ibValue(wxString(wxT("0"))));
    EXPECT_FALSE(ibValue(wxString(wxT("0"))) < ibValue(1));
}

TEST(ValueOrderAcrossKinds, BooleanOrdersBeforeString) {
    EXPECT_TRUE (ibValue(true) < ibValue(wxString(wxT("True"))));
    EXPECT_FALSE(ibValue(wxString(wxT("True"))) < ibValue(true));
}

// A BOOLEAN AND A NUMBER NO LONGER MEET ON VALUE, and that is a fix rather than
// a loss. The old rule read both sides as whatever the LEFT tag said, so any
// non-zero number was equal to True — which makes the order NON-TRANSITIVE
// (True == 2, True == 3, 2 != 3) and a non-transitive comparator is undefined
// behaviour under std::sort, not merely surprising. Booleans occupy their own
// stretch of the order now; every boolean sorts before every number.
//
// Arithmetic is untouched: `True + 1` still coerces. This is about ORDER only.
TEST(ValueOrderAcrossKinds, BooleanAndNumberAreSeparatedNotCoerced) {
    EXPECT_TRUE (ibValue(true)  < ibValue(2));    // by rank: boolean before number
    EXPECT_TRUE (ibValue(false) < ibValue(0));    // even against zero
    EXPECT_FALSE(ibValue(2) < ibValue(true));
    // ...and inside the boolean rank the value still decides.
    EXPECT_TRUE (ibValue(false) < ibValue(true));
    EXPECT_FALSE(ibValue(true)  < ibValue(false));
}

// The transitivity the separation buys, stated as the property it is: nothing
// may compare equal to two values that differ from each other.
TEST(ValueOrderAcrossKinds, EqualityUnderOrderIsTransitive) {
    const ibValue samples[] = {
        ibValue(), ibValue(true), ibValue(false), ibValue(0), ibValue(1), ibValue(2),
        ibValue(ibNumber(1.0)), ibValue(wxString(wxT("1"))), ibValue(wxString(wxT("abc"))),
    };
    for (const ibValue& a : samples)
        for (const ibValue& b : samples)
            for (const ibValue& c : samples)
                if (a.CompareValueLS(b) == 0 && b.CompareValueLS(c) == 0)
                    EXPECT_EQ(a.CompareValueLS(c), 0)
                        << "'" << a.GetString().ToStdString() << "' == '"
                        << b.GetString().ToStdString() << "' == '"
                        << c.GetString().ToStdString() << "', but not the first and last";
}

TEST(ValueOrderAcrossKinds, EmptySortsBelowEverything) {
    EXPECT_TRUE(ibValue() < ibValue(1));
    EXPECT_TRUE(ibValue() < ibValue(wxString(wxT("a"))));
}

// Equality is type-strict and stays so: it never had the coercion the order is
// giving up here, and `1` was never equal to "1".
TEST(ValueOrderAcrossKinds, EqualityRemainsTypeStrict) {
    EXPECT_FALSE(ibValue(1) == ibValue(wxString(wxT("1"))));
    EXPECT_TRUE (ibValue(1) != ibValue(wxString(wxT("1"))));
}

// '<>' is '=' negated now, so the two can no longer answer differently about
// one pair — which is what they could do while each carried its own switch.
TEST(ValueOrderAcrossKinds, NotEqualIsExactlyTheNegationOfEqual) {
    const ibValue samples[] = {
        ibValue(), ibValue(1), ibValue(true), ibValue(wxString(wxT("1"))), ibValue(wxString(wxT("x"))),
    };
    for (const ibValue& a : samples)
        for (const ibValue& b : samples)
            EXPECT_NE(a.CompareValueEQ(b), a.CompareValueNE(b));
}

// ===========================================================================
// Comparison THROUGH A REFERENCE.
//
// A value that arrives by reffer must answer exactly as the value itself: `=`
// cannot depend on how the operand was passed. Two accessors keep that true and
// they are NOT interchangeable — GetType() follows the reffer chain to what the
// value IS, while the raw m_typeClass tag says only where the bytes are, and is
// consulted purely to decide whether a payload can be read off the field.
//
// These are the tests that catch a "read the field directly" optimisation that
// forgot the distinction: swap one GetType() for the raw tag and the equalities
// below turn false while everything else in the suite stays green.
//
// Ownership: the reffer holds the only count on its target and drops it on
// scope exit (ibValue::DecrRef), so `ibValue(new ibValue(...))` leaks nothing.
// ===========================================================================

TEST(ValueThroughReference, NumberEqualsAReferenceToTheSameNumber) {
    const ibValue direct(7);
    const ibValue viaRef(new ibValue(7));
    EXPECT_TRUE (direct.CompareValueEQ(viaRef));
    EXPECT_TRUE (viaRef.CompareValueEQ(direct));
    EXPECT_FALSE(direct.CompareValueNE(viaRef));
}

TEST(ValueThroughReference, StringEqualsAReferenceToTheSameString) {
    const ibValue direct(wxString(wxT("abc")));
    const ibValue viaRef(new ibValue(wxString(wxT("abc"))));
    EXPECT_TRUE(direct.CompareValueEQ(viaRef));
    EXPECT_TRUE(viaRef.CompareValueEQ(direct));
}

TEST(ValueThroughReference, OrderingIsTheSameThroughAReference) {
    const ibValue five(5);
    const ibValue nineViaRef(new ibValue(9));
    EXPECT_EQ(five.CompareValueLS(nineViaRef), -1);
    EXPECT_EQ(nineViaRef.CompareValueLS(five),  1);
}

// The rank rule has to read through the reffer too, or a number would sort
// before a bare string and against a referenced one by a different rule.
TEST(ValueThroughReference, RankRuleReadsThroughAReference) {
    const ibValue number(1);
    const ibValue textViaRef(new ibValue(wxString(wxT("0"))));
    EXPECT_EQ(number.CompareValueLS(textViaRef), -1);
    EXPECT_EQ(textViaRef.CompareValueLS(number),  1);
}

TEST(ValueThroughReference, ReferenceToNumberIsNotEqualToItsSpelling) {
    const ibValue numberViaRef(new ibValue(1));
    EXPECT_FALSE(numberViaRef.CompareValueEQ(ibValue(wxString(wxT("1")))));
}

// ===========================================================================
// THE HASH CONTRACT — order-equal implies hash-equal.
//
// GetValueHash exists so a hashed index can replace an ordered one (the LINQ
// join, procUnitLinq.cpp). That substitution is only sound while every pair the
// ORDER calls equal lands in the same bucket; the converse is free, since a
// collision costs one comparison and nothing else.
//
// So this is not a test of a hash function, it is a test of AGREEMENT between
// two methods that are edited in different files by different people. It walks
// every pair of a deliberately awkward sample set — the kinds that coerce into
// each other (True is 1, a date is its instant), the ones that must NOT (a
// number and its spelling), a reference standing in for its target, and the
// composites that hash by their contents.
//
// A failure here does not mean "the hash is weak". It means an index built on
// it will silently lose rows.
// ===========================================================================

TEST(ValueHashContract, OrderEqualImpliesHashEqual) {
    ibValueArray* arrayA = new ibValueArray();
    ibValueArray* arrayB = new ibValueArray();
    for (int i = 1; i <= 3; ++i) { arrayA->Add(ibValue(i)); arrayB->Add(ibValue(i)); }

    const ibValue samples[] = {
        ibValue(),                                   // Undefined
        ibValue(0), ibValue(1), ibValue(2),
        ibValue(ibNumber(1.0)),                      // the same number as 1
        ibValue(ibNumber(1.5)),                      // shares 1's bucket, differs in order
        ibValue(true), ibValue(false),               // True coerces to 1
        ibValue(wxString(wxT("1"))),                 // NOT the number 1
        ibValue(wxString(wxT("abc"))),
        ibValue(new ibValue(1)),                     // a reference to 1
        ibValue(new ibValue(wxString(wxT("abc")))),  // a reference to "abc"
        ibValue(static_cast<ibValue*>(arrayA)),
        ibValue(static_cast<ibValue*>(arrayB)),      // equal contents, separate object
    };

    for (const ibValue& a : samples) {
        for (const ibValue& b : samples) {
            if (a.CompareValueLS(b) != 0)
                continue;
            EXPECT_EQ(a.GetValueHash(), b.GetValueHash())
                << "order-equal values hash apart: '" << a.GetString().ToStdString()
                << "' vs '" << b.GetString().ToStdString() << "'";
        }
    }
}

// The two the join actually leans on, stated on their own so a failure names
// itself instead of arriving as one line of the sweep above.
TEST(ValueHashContract, TrailingZeroHashesWithTheInteger) {
    EXPECT_EQ(ibValue(1).GetValueHash(), ibValue(ibNumber(1.0)).GetValueHash());
}

TEST(ValueHashContract, ReferenceHashesAsItsTarget) {
    EXPECT_EQ(ibValue(7).GetValueHash(), ibValue(new ibValue(7)).GetValueHash());
    EXPECT_EQ(ibValue(wxString(wxT("k"))).GetValueHash(),
              ibValue(new ibValue(wxString(wxT("k")))).GetValueHash());
}

TEST(ValueHashContract, ArraysWithEqualContentsHashAlike) {
    ibValueArray* a = new ibValueArray();
    ibValueArray* b = new ibValueArray();
    for (int i = 0; i < 4; ++i) { a->Add(ibValue(i)); b->Add(ibValue(i)); }
    const ibValue va(static_cast<ibValue*>(a));
    const ibValue vb(static_cast<ibValue*>(b));
    ASSERT_EQ(va.CompareValueLS(vb), 0);
    EXPECT_EQ(va.GetValueHash(), vb.GetValueHash());

    b->Add(ibValue(99));                       // now longer -> not equal any more
    EXPECT_NE(va.CompareValueLS(vb), 0);
}
