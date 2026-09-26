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
#include "backend/backend_exception.h"   // a door that refuses does it by raising

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
// requires a string, and it raises on anything else). Every key, a string
// included, is compared AS A VALUE: a number by its magnitude, a reference by
// its guid, a string as the string it is — "ab" and "AB" are two keys, as
// `"ab" = "AB"` is False. (A Structure folds case, because its keys are field
// NAMES reached through a dot; test_valueStructure.cpp states that half.)
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

// ⚠ CHANGED 2026-09-23. A Container used to fold the case of a string key, as a
// Structure does, so "ab" and "AB" were one key and the second Insert raised
// "already using". A Container binds a runtime VALUE to a value, and two strings
// that differ in case are two values — the rule is the language's own `=`.
//
// SetAt for the second key and Property for the reads, because both stay off
// appData, which this binary does not bring up: a duplicate Insert or a missed
// GetAt reaches for it to decide whether to raise. The Insert road is the same
// lookup, and tests/scripts/test_container_keys_suite.txt drives it for real.
TEST(ValueContainer, StringKeysThatDifferOnlyInCaseAreTwoKeys) {
    ibValueContainer c;
    c.Insert(Key(wxT("ab")), ibValue(ibNumber(1)));
    c.SetAt (Key(wxT("AB")), ibValue(ibNumber(2)));
    EXPECT_EQ(c.Count(), 2u);
    ibValue out;
    ASSERT_TRUE(c.Property(Key(wxT("ab")), out));
    EXPECT_EQ(out.GetInteger(), 1);
    ASSERT_TRUE(c.Property(Key(wxT("AB")), out));
    EXPECT_EQ(out.GetInteger(), 2);
    EXPECT_FALSE(c.Property(Key(wxT("Ab")), out)) << "a third spelling is a third key, and it is not there";
    EXPECT_FALSE(c.Property(Key(wxT("aB")), out));
}

// The same past ASCII, and with NO dependence on the process locale any more: a
// Container does not fold at all, so what towupper would say is not asked.
// Escapes, not literal Cyrillic (this file has no BOM, and MSVC would decode a
// literal in the system code page): U+0431 U+043E U+0440 U+0449 is "borshch" in
// small letters, U+0411 U+041E U+0420 U+0429 the same in capitals.
TEST(ValueContainer, NonAsciiKeysThatDifferOnlyInCaseAreTwoKeys) {
    ibValueContainer c;
    c.Insert(Key(wxT("\u0431\u043E\u0440\u0449")), ibValue(ibNumber(1)));
    c.SetAt (Key(wxT("\u0411\u041E\u0420\u0429")), ibValue(ibNumber(2)));
    EXPECT_EQ(c.Count(), 2u);
    ibValue out;
    ASSERT_TRUE(c.Property(Key(wxT("\u0431\u043E\u0440\u0449")), out));
    EXPECT_EQ(out.GetInteger(), 1);
    ASSERT_TRUE(c.Property(Key(wxT("\u0411\u041E\u0420\u0429")), out));
    EXPECT_EQ(out.GetInteger(), 2);
}

// THE DOT READS A KEY AS WRITTEN. `c.Name` asks FindProp, which is the same lookup as `[key]`: the
// key "Name" is there, and "name" is another key that is not.
TEST(ValueContainer, TheDotFindsAKeyAsWritten) {
    ibValueContainer c;
    c.Insert(Key(wxT("Name")), ibValue(ibNumber(1)));
    EXPECT_EQ(c.FindProp(wxT("Name")), 0);
    EXPECT_EQ(c.FindProp(wxT("name")), wxNOT_FOUND);
    ibValue out;
    EXPECT_TRUE(c.Property(Key(wxT("Name")), out));
}

// WHAT AN ENTRY IS NAMED is its key's text, and a caller that wants a name wants exactly that: a
// LINQ projection over containers names the columns of its answer with GetPropName. The walk hands
// back every key, of any kind, as itself and in the order it was inserted.
TEST(ValueContainer, AnEntryIsNamedByItsKeyAndWalkedInOrder) {
    ibValueContainer c;
    ibValue date; date.SetType(ibValueTypes::TYPE_DATE);
    ibValue yes;  yes.SetBoolean(wxT("True"));
    c.Insert(Key(wxT("Name")), Num(1));
    c.Insert(Num(42), Num(2));
    c.Insert(date, Num(3));
    c.Insert(yes, Num(4));

    EXPECT_EQ(c.GetPropName(0), wxString(wxT("Name")));
    EXPECT_EQ(c.GetPropName(1), wxString(wxT("42")));
    EXPECT_EQ(c.FindProp(c.GetPropName(0)), 0) << "a string key is found again by its name";

    const std::shared_ptr<ibValueIteratorState> iterator = c.CreateIterator();
    ASSERT_NE(iterator, nullptr);
    ibValue pair, key, value;
    for (int at = 0; at < 4; at++) {
        ASSERT_TRUE(iterator->MoveNext(pair)) << "element " << at;
        ASSERT_TRUE(pair.GetPropVal(pair.FindProp(wxT("Key")), key));
        ASSERT_TRUE(pair.GetPropVal(pair.FindProp(wxT("Value")), value));
        EXPECT_EQ(value.GetInteger(), at + 1) << "element " << at << " is the one inserted " << at;
    }
    EXPECT_EQ(key.GetType(), ibValueTypes::TYPE_BOOLEAN) << "a key of any kind comes back as itself";
    EXPECT_FALSE(iterator->MoveNext(pair));
}

// GET IS THE THIRD QUESTION. Property answers whether a key is there, and hands the value back
// through its second argument; [key] answers the value and raises when the key is not there. Get
// answers the value, or Undefined -- what "nothing is bound to this key" looks like everywhere
// else in the language, and what the reference system's Map answers.
//
// Called the way the runtime calls it, FindMethod then CallAsFunc with that number, because a
// method number IS a position in the member table: an entry added in the wrong place would still
// compile and would run some other method.
TEST(ValueContainer, GetAnswersTheValueOrUndefined) {
    ibValueContainer c;
    c.Insert(Key(wxT("k")), Num(7));

    const long at = c.FindMethod(wxT("Get"));
    ASSERT_NE(at, wxNOT_FOUND) << "Get is not on the member table";

    ibValue key = Key(wxT("k"));
    ibValue* args[1] = { &key };
    ibValue out = Num(1);                       // not empty to begin with: Get has to write its answer
    ASSERT_TRUE(c.CallAsFunc(at, out, args, 1));
    EXPECT_EQ(out.GetInteger(), 7);

    ibValue absent = Key(wxT("nope"));
    ibValue* argsAbsent[1] = { &absent };
    out = Num(1);
    ASSERT_TRUE(c.CallAsFunc(at, out, argsAbsent, 1));
    EXPECT_EQ(out.GetType(), ibValueTypes::TYPE_EMPTY) << "a key that is not there answers Undefined";
}

// ...and a call that forgot the key is refused by name rather than answered. The arity check
// catches only a call with too many arguments; the slot this one would have read is made empty and
// handed over, so without this Get would say "the key is not there" about a key nobody wrote.
TEST(ValueContainer, GetWithoutAKeyIsRefused) {
    ibValueContainer c;
    c.Insert(Key(wxT("k")), Num(7));
    const long at = c.FindMethod(wxT("Get"));
    ASSERT_NE(at, wxNOT_FOUND);

    ibValue out;
    ibValue empty;
    ibValue* args[1] = { &empty };
    EXPECT_THROW((void)c.CallAsFunc(at, out, args, 0), ibBackendException);
}
