// =============================================================================
// OES Enterprise — ibValueStructure tests
//
// ibValueStructure (backend/system/value/valueMap.h) is the script Structure:
// a STRING-keyed ibValueContainer (`New Structure("F1, F2", v1, v2)`).
// Keys must be strings; lookup via the non-throwing Property. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
#include <cwctype>   // towupper — the non-ASCII folding probe below
#include <map>
#include "backend/system/value/valueMap.h"
#include "backend/backend_exception.h"

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

// ===========================================================================
// A FIELD IS A NAME, SO IT FOLDS CASE
//
// A script reaches a structure's field through a dot and does not care how it
// was typed: `s.Name` and `s.name` are one field. That is the difference from a
// Container, whose string keys are values and are compared as written
// (test_valueContainer.cpp) — and it is why only a Structure answers FindProp.
// ===========================================================================

TEST(ValueStructure, FieldNamesFoldCase) {
    ibValueStructure s;
    s.Insert(Field(wxT("Key")), ibValue(ibNumber(1)));
    ibValue out;
    EXPECT_TRUE(s.Property(Field(wxT("kEY")), out));
    EXPECT_TRUE(s.Property(Field(wxT("KEY")), out));
    s.SetAt(Field(wxT("KEY")), ibValue(ibNumber(2)));    // the same field, written in another case
    EXPECT_EQ(s.Count(), 1u);
    ASSERT_TRUE(s.Property(Field(wxT("key")), out));
    EXPECT_EQ(out.GetInteger(), 2);
}

// ...and past ASCII the fold is the C library's, which is where it stops being
// the structure's business: std::towupper folds a non-ASCII letter only when the
// process locale says how, and a gtest binary runs in "C", where it does not.
//
// ⚠ WORTH KNOWING, because it is not a property of this code: the same script
// sees case-SENSITIVE Cyrillic field names in a headless run (daemon, codeRunner,
// this suite) and case-INSENSITIVE ones under a UI locale. Deciding that is a
// language question, not a folding one, so this test states the rule and skips
// where the platform will not honour it rather than asserting either answer.
TEST(ValueStructure, NonAsciiFieldNamesFoldCase) {
    // Universal-character escapes, never literal Cyrillic: this file has no BOM,
    // so MSVC decodes a literal in the system code page and it arrives as
    // something else (warning C4066). U+041A/U+043A KA, U+041B/U+043B EL,
    // U+042E/U+044E YU, U+0427/U+0447 CHE -- "Kluch" in three cases.
    if (std::towupper((wint_t)L'\u043A') != (wint_t)L'\u041A')
        GTEST_SKIP() << "process locale does not fold non-ASCII (C locale) - see the note above";

    ibValueStructure s;
    s.Insert(Field(wxT("\u041A\u043B\u044E\u0447")), ibValue(ibNumber(1)));   // mixed case
    ibValue out;
    EXPECT_TRUE(s.Property(Field(wxT("\u041A\u041B\u042E\u0427")), out));     // all upper
    EXPECT_TRUE(s.Property(Field(wxT("\u043A\u043B\u044E\u0447")), out));     // all lower
}

// The dot is a Structure's: FindProp finds a field by its name, in any case.
TEST(ValueStructure, AFieldIsAProperty) {
    ibValueStructure s;
    s.Insert(Field(wxT("Name")), ibValue(ibNumber(7)));
    const long at = s.FindProp(wxT("name"));
    ASSERT_NE(at, wxNOT_FOUND);
    ibValue out;
    ASSERT_TRUE(s.GetPropVal(at, out));
    EXPECT_EQ(out.GetInteger(), 7);
}

// ...and a field is NAMED by its name and REACHED by it too - the two questions that part on a
// Container (test_valueContainer.cpp) are one here, because a field name is written after a dot.
TEST(ValueStructure, AFieldIsNamedAndReachedByItsName) {
    ibValueStructure s;
    s.Insert(Field(wxT("Name")), ibValue(ibNumber(7)));
    EXPECT_EQ(s.GetPropName(0), wxString(wxT("Name")));
    EXPECT_EQ(s.AccessorOf(0),  wxString(wxT("Name")));
    EXPECT_EQ(s.FindProp(s.GetPropName(0)), 0);
}

// ...unless the name is not one. A field is whatever string was inserted, and nothing makes it an
// identifier: a spreadsheet document's Areas are keyed by an area's free-text label. `s.some label`
// is a field no dot can reach, so the subscript reaches it - which is what the watch must write.
TEST(ValueStructure, AFieldNameThatIsNotANameIsReachedBySubscript) {
    ibValueStructure s;
    s.Insert(Field(wxT("some label")), ibValue(ibNumber(7)));
    s.Insert(Field(wxT("2nd")), ibValue(ibNumber(8)));
    EXPECT_EQ(s.AccessorOf(0), wxString(wxT("[\"some label\"]")));
    EXPECT_EQ(s.AccessorOf(1), wxString(wxT("[\"2nd\"]")));
    ibValue out;
    ASSERT_TRUE(s.Property(Field(wxT("some label")), out)) << "and the subscript form does reach it";
    EXPECT_EQ(out.GetInteger(), 7);
}

// ===========================================================================
// COPYING — the verb behind `Val`
//
// `Clone` is virtual, so a type may state how it duplicates itself. What it
// gets when it states nothing is the packed-form road: pack, then create from
// what was packed, through the registered constructor. A type that has neither
// and is not a primitive RAISES — never a quiet empty, and never a share.
// ===========================================================================

TEST(ValueClone, APrimitiveIsItsOwnCopy) {
	const ibValue num(42);
	const ibValue copy = num.CloneValue();
	EXPECT_EQ(copy.GetInteger(), 42);

	const ibValue str(wxString(wxT("text")));
	EXPECT_STREQ(str.CloneValue().GetString().c_str(), wxT("text"));

	const ibValue empty;
	EXPECT_TRUE(empty.CloneValue().IsEmpty()) << "an empty value copies to an empty one";
}

TEST(ValueClone, AStructureCopiesAndTheCopyIsIndependent) {
	// The whole point of `Val`: the callee gets the same structure and cannot
	// reach back through it into the caller's.
	ibValueStructure src;
	src.Insert(Field(wxT("Count")), ibValue(ibNumber(1)));

	const ibValue copy = src.CloneValue();
	ASSERT_FALSE(copy.IsEmpty()) << "a structure has a packed form and must copy";
	EXPECT_NE(copy.GetRef(), &src) << "a copy that is the same object is not a copy";

	ibValue out;
	ibValueStructure* pCopy = dynamic_cast<ibValueStructure*>(copy.GetRef());
	ASSERT_NE(pCopy, nullptr);
	ASSERT_TRUE(pCopy->Property(Field(wxT("Count")), out));
	EXPECT_EQ(out.GetInteger(), 1);

	// Independent: changing the copy leaves the original as it was.
	pCopy->Insert(Field(wxT("Extra")), ibValue(ibNumber(2)));
	EXPECT_EQ(src.Count(), 1u);
}

TEST(ValueClone, AValueWithNoPackedFormRaises) {
	// The failure a form hits. Raising is the requirement: falling back to
	// sharing would compile, run, and alias until the day it mattered.
	class ibValueNoCopy : public ibValue {
	public:
		virtual bool IsTransferable() const override { return false; }
	};

	ibValueNoCopy value;
	// PAST THE PRIMITIVE SWITCH. A default-constructed value is TYPE_EMPTY, and
	// an empty value legitimately copies to an empty one — the refusal only
	// applies to something that HAS contents and cannot pack them.
	value.SetType(ibValueTypes::TYPE_VALUE);
	EXPECT_THROW((void)value.CloneValue(), ibBackendException);
}

TEST(ValueClone, ATypeMayStateItsOwnCopy) {
	// CloneValue is virtual precisely so this is possible.
	class ibValueOwnCopy : public ibValue {
	public:
		virtual ibValue CloneValue() const override { return ibValue(7); }
	};

	ibValueOwnCopy value;
	EXPECT_EQ(value.CloneValue().GetInteger(), 7);
}
