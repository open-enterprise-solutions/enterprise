// =============================================================================
// OES Enterprise — ibValueStructure tests
//
// ibValueStructure (backend/system/value/valueMap.h) is the script Structure:
// a STRING-keyed ibValueContainer (`New Structure("F1, F2", v1, v2)`).
// Keys must be strings; lookup via the non-throwing Property. Pure (no DB).
// =============================================================================

#include <gtest/gtest.h>
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
// COPYING — the verb behind `Val`
//
// `Clone` is virtual, so a type may state how it duplicates itself. What it
// gets when it states nothing is the packed-form road: pack, then create from
// what was packed, through the registered constructor. A type that has neither
// and is not a primitive RAISES — never a quiet empty, and never a share.
// ===========================================================================

TEST(ValueClone, APrimitiveIsItsOwnCopy) {
	const ibValue num(42);
	const ibValue copy = num.Clone();
	EXPECT_EQ(copy.GetInteger(), 42);

	const ibValue str(wxString(wxT("text")));
	EXPECT_STREQ(str.Clone().GetString().c_str(), wxT("text"));

	const ibValue empty;
	EXPECT_TRUE(empty.Clone().IsEmpty()) << "an empty value copies to an empty one";
}

TEST(ValueClone, AStructureCopiesAndTheCopyIsIndependent) {
	// The whole point of `Val`: the callee gets the same structure and cannot
	// reach back through it into the caller's.
	ibValueStructure src;
	src.Insert(Field(wxT("Count")), ibValue(ibNumber(1)));

	const ibValue copy = src.Clone();
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
	EXPECT_THROW((void)value.Clone(), ibBackendException);
}

TEST(ValueClone, ATypeMayStateItsOwnCopy) {
	// Clone is virtual precisely so this is possible.
	class ibValueOwnCopy : public ibValue {
	public:
		virtual ibValue Clone() const override { return ibValue(7); }
	};

	ibValueOwnCopy value;
	EXPECT_EQ(value.Clone().GetInteger(), 7);
}
