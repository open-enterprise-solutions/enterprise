// =============================================================================
// OES Enterprise — ibValueContainer (script Map / Dictionary) tests
//
// ibValueContainer (backend/system/value/valueMap.h) is the script keyed
// container: Insert / GetAt / SetAt / Property / Delete over a
// std::map<ibValue, ibValue>. Keys and values are ibValues; lookup uses the
// container's ibValue comparator. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include <cwctype>   // towupper — the non-ASCII folding probe below
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

// =============================================================================
// Key IDENTITY — what counts as the same key.
//
// A CONTAINER takes any value as a key (its Structure subclass is the one that
// requires a string, and it raises on anything else — so none of this applies
// there). TWO RULES, by the kind of key:
//
//   a STRING key folds case — `Name` and `name` are one field, which is how a
//   script reaches a structure's members.
//   anything else is compared AS A VALUE: a number by its magnitude, a
//   reference by its guid.
//
// ⚠ CHANGED 2026-08-15. The container used to render every non-string key to
// text (ibValue::GetHashKey, now removed), so `1` and "1" were ONE key. They are
// two keys now — the same answer the language's comparison gives everywhere
// else. The tests below state both halves so the rule is written down, not
// inferred from whichever helper the lookup happens to call.
//
// Property is used throughout: it is the no-throw lookup (see above), and Insert
// on a duplicate raises outside designer mode, which is exactly what a collision
// between two of these keys would look like.
// =============================================================================
namespace {
ibValue Num(long long v) { return ibValue(ibNumber(v)); }
}

TEST(ValueContainer, NumericKeyIsFoundByItsOwnValue) {
    ibValueContainer c;
    c.Insert(Num(42), Key(wxT("answer")));
    ibValue out;
    ASSERT_TRUE(c.Property(Num(42), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("answer")));
}

TEST(ValueContainer, NegativeAndZeroNumericKeysRoundTrip) {
    ibValueContainer c;
    c.Insert(Num(-42), Key(wxT("neg")));
    c.Insert(Num(0),   Key(wxT("zero")));
    ibValue out;
    ASSERT_TRUE(c.Property(Num(-42), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("neg")));
    ASSERT_TRUE(c.Property(Num(0), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("zero")));
    EXPECT_EQ(c.Count(), 2u);   // -42 and 0 did not fold together
}

// Past the 47-bit immediate mantissa, so the number lives on the heap tier and
// the inline print has to agree with the text path there too.
TEST(ValueContainer, WideNumericKeyRoundTrips) {
    ibValueContainer c;
    c.Insert(Num(9007199254740993LL), Key(wxT("wide")));
    ibValue out;
    ASSERT_TRUE(c.Property(Num(9007199254740993LL), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("wide")));
}

// A NUMBER AND ITS SPELLING ARE TWO KEYS — fixed 2026-08-15 with the removal of
// the rendered identity.
//
// Why this is a FIX and not a preference: a composite-typed attribute holds a
// number in one row and a string in another, and a report grouped by it used to
// put both in ONE group — they rendered to the same text — silently adding two
// different values' sums together. A text key is matched as text now, a number
// as a number, which is also what the value ordering says (scalars and text sit
// in different ranks), so the container and the language answer alike.
TEST(ValueContainer, NumberAndItsSpellingAreDifferentKeys) {
    ibValueContainer c;
    c.Insert(Num(1), Key(wxT("one")));
    ibValue out;
    EXPECT_FALSE(c.Property(Key(wxT("1")), out));   // the text "1" is not the number 1
    ASSERT_TRUE (c.Property(Num(1), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("one")));

    // ...and both may live in one container at once, which the old rule made impossible.
    c.Insert(Key(wxT("1")), Key(wxT("text")));
    EXPECT_EQ(c.Count(), 2u);
    ASSERT_TRUE(c.Property(Key(wxT("1")), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("text")));
}

// 1.0 and 1 are the same number, so they are the same key — canonicality is
// settled by ibNumber before the key is ever spelled.
TEST(ValueContainer, TrailingZeroIsNotASecondKey) {
    ibValueContainer c;
    c.Insert(Num(1), Key(wxT("one")));
    ibValue out;
    ASSERT_TRUE(c.Property(ibValue(ibNumber(1.0)), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("one")));
}

// The guard that earns the fast path: a truncating conversion would make 1.5
// identify as "1" and silently overwrite a genuine 1.
TEST(ValueContainer, FractionDoesNotCollideWithItsTruncation) {
    ibValueContainer c;
    c.Insert(Num(1), Key(wxT("whole")));
    c.Insert(ibValue(ibNumber(1.5)), Key(wxT("fraction")));
    EXPECT_EQ(c.Count(), 2u);
    ibValue out;
    ASSERT_TRUE(c.Property(Num(1), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("whole")));
    ASSERT_TRUE(c.Property(ibValue(ibNumber(1.5)), out));
    EXPECT_EQ(out.GetString(), wxString(wxT("fraction")));
}

// String keys fold case — the ASCII fast path must not change that.
TEST(ValueContainer, StringKeysFoldCase) {
    ibValueContainer c;
    c.Insert(Key(wxT("Key")), ibValue(ibNumber(1)));
    ibValue out;
    EXPECT_TRUE(c.Property(Key(wxT("kEY")), out));
    EXPECT_TRUE(c.Property(Key(wxT("KEY")), out));
}

// ...and past ASCII the fold is the C library's, which is where it stops being
// the container's business: std::towupper folds a non-ASCII letter only when the
// process locale says how, and a gtest binary runs in "C", where it does not.
//
// ⚠ WORTH KNOWING, because it is not a property of this code: the same script
// sees case-SENSITIVE Cyrillic keys in a headless run (daemon, codeRunner, this
// suite) and case-INSENSITIVE ones under a UI locale. For a Russian-language
// platform where `Structure.Kluch` is ordinary, that is a real difference in
// meaning between two ways of running the same configuration — deciding it is a
// language question, not a folding one, so this test states the rule and skips
// where the platform will not honour it rather than asserting either answer.
// Escapes, not literal Cyrillic: every other file under tests/ is pure ASCII,
// and none carries a BOM — so a literal here is decoded by MSVC in the system
// code page and comes out as something else entirely (warning C4066 caught it,
// after the guard below had silently been comparing rubbish). U+041A/U+043A are
// CAPITAL/SMALL KA, U+041B/U+043B EL, U+042E/U+044E YU, U+0427/U+0447 CHE.
TEST(ValueContainer, NonAsciiKeysFoldCase) {
    // Universal-character escapes, never literal Cyrillic: this file has no BOM,
    // so MSVC decodes a literal in the system code page and it arrives as
    // something else (warning C4066 caught exactly that, after the guard below
    // had spent a build comparing rubbish). U+041A/U+043A KA, U+041B/U+043B EL,
    // U+042E/U+044E YU, U+0427/U+0447 CHE -- "Kluch" in three cases.
    if (std::towupper((wint_t)L'\u043A') != (wint_t)L'\u041A')
        GTEST_SKIP() << "process locale does not fold non-ASCII (C locale) - see the note above";

    ibValueContainer c;
    c.Insert(Key(wxT("\u041A\u043B\u044E\u0447")), ibValue(ibNumber(1)));   // mixed case
    ibValue out;
    EXPECT_TRUE(c.Property(Key(wxT("\u041A\u041B\u042E\u0427")), out));     // all upper
    EXPECT_TRUE(c.Property(Key(wxT("\u043A\u043B\u044E\u0447")), out));     // all lower
}
