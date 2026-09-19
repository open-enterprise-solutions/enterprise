// =============================================================================
// A value written into an attribute whose type is a FAMILY — `DocumentRef`, `AnyRef`, `CatalogRef` —
// (#157).
//
// A family names no single type: it creates nothing and its gate (AllowValue) admits every member.
// ibValueTypeDescription::AdjustValue compared the value's class with the declaration's, missed (the
// value's class is the CONCRETE reference, `DocumentRef.Other`, not the family's id) and asked the
// factory for a `DocumentRef`, which "cannot be created without arguments". So every assignment of a
// reference to an attribute of a generic type raised.
//
// No database and no configuration: the families are registered by the platform itself, and a member's
// class id is all the gate looks at, so a probe value that answers a class id stands in for a reference.
// =============================================================================

#include <gtest/gtest.h>

#include <vector>

#include "backend/compiler/value.h"
#include "backend/metaCtor.h"                  // ib_find_any_reference / ibCtorMetaAnyReference
#include "backend/system/value/valueType.h"    // ibValueTypeDescription::AdjustValue
#include "backend/typeDescription.h"           // ibTypeDescription

namespace {

// Stands in for a reference to some object: answers the class id it is given.
class ibReferenceProbe : public ibValue {
public:
	explicit ibReferenceProbe(const ibClassID& clsid)
		: ibValue(ibValueTypes::TYPE_VALUE, false), m_clsid(clsid) {}
	ibClassID GetClassType() const override { return m_clsid; }
private:
	ibClassID m_clsid;
};

// A value that holds a reference to a heap probe — the shape an object's `Ref` has: TYPE_REFFER, and
// GetClassType() answers the referenced object's class.
ibValue ReferenceTo(const ibClassID& clsid) {
	return ibValue(static_cast<ibValue*>(new ibReferenceProbe(clsid)));
}

ibTypeDescription Declares(const ibClassID& clsid) {
	return ibTypeDescription(std::vector<ibClassID>{ clsid });
}

const ibClassID kMember   = make_clsid("FamilyTestMember",   ibClassKind_Reference);
const ibClassID kStranger = make_clsid("FamilyTestStranger", ibClassKind_Reference);

} // namespace

// ---- AnyRef: every reference passes ---------------------------------------------------------------

TEST(TypeFamilyAdjust, AnyRef_AReference_PassesAsItIs)
{
	const ibCtorAbstractType* anyRef = ibValue::GetAvailableCtor(wxT("AnyRef"));
	ASSERT_NE(anyRef, nullptr);
	ASSERT_TRUE(anyRef->IsFamily());

	const ibValue reference = ReferenceTo(kMember);
	const ibValue result = ibValueTypeDescription::AdjustValue(Declares(anyRef->GetClassType()), reference, nullptr);

	EXPECT_EQ(result.GetClassType(), kMember) << "a reference written into an AnyRef attribute is kept";
}

TEST(TypeFamilyAdjust, AnyRef_NotAReference_BecomesEmpty)
{
	const ibCtorAbstractType* anyRef = ibValue::GetAvailableCtor(wxT("AnyRef"));
	ASSERT_NE(anyRef, nullptr);

	const ibValue result = ibValueTypeDescription::AdjustValue(Declares(anyRef->GetClassType()), ibValue(5), nullptr);

	EXPECT_TRUE(result.IsEmpty()) << "a number is not a reference; the family has no empty one to hand back";
}

// ---- DocumentRef: only the members of THIS metatype pass -------------------------------------------

TEST(TypeFamilyAdjust, DocumentRef_AMember_PassesAsItIs_AStrangerDoesNot)
{
	ibCtorMetaAnyReference* family = ib_find_any_reference(wxT("Document"));
	ASSERT_NE(family, nullptr) << "the DocumentRef family is registered with the Document metatype";
	ASSERT_TRUE(family->IsFamily());

	family->AddMember(kMember);   // what registering a document's reference does

	const ibTypeDescription declared = Declares(family->GetClassType());

	const ibValue member = ibValueTypeDescription::AdjustValue(declared, ReferenceTo(kMember), nullptr);
	EXPECT_EQ(member.GetClassType(), kMember) << "a document's reference is kept in a DocumentRef attribute";

	const ibValue stranger = ibValueTypeDescription::AdjustValue(declared, ReferenceTo(kStranger), nullptr);
	EXPECT_TRUE(stranger.IsEmpty()) << "a reference to something that is not a document is not kept";

	family->RemoveMember(kMember);
}

TEST(TypeFamilyAdjust, DocumentRef_EmptyValue_StaysEmpty)
{
	ibCtorMetaAnyReference* family = ib_find_any_reference(wxT("Document"));
	ASSERT_NE(family, nullptr);

	const ibValue result = ibValueTypeDescription::AdjustValue(Declares(family->GetClassType()), ibValue(), nullptr);

	EXPECT_TRUE(result.IsEmpty());
}

// ---- the empty value of a family --------------------------------------------------------------------

// What a new object's attribute starts as. It used to raise "cannot be created without arguments" and
// have that swallowed by the caller; it is now simply empty.
TEST(TypeFamilyAdjust, DefaultValueOfAFamily_IsEmpty_AndDoesNotThrow)
{
	ibCtorMetaAnyReference* family = ib_find_any_reference(wxT("Document"));
	ASSERT_NE(family, nullptr);

	ibValue result;
	EXPECT_NO_THROW(result = ibValueTypeDescription::AdjustValue(Declares(family->GetClassType()), nullptr));
	EXPECT_TRUE(result.IsEmpty());
}

// ---- a type that is NOT a family is untouched --------------------------------------------------------

TEST(TypeFamilyAdjust, NotAFamily_IsNotClaimed)
{
	const ibCtorAbstractType* number = ibValue::GetAvailableCtor(wxT("Number"));
	ASSERT_NE(number, nullptr);
	EXPECT_FALSE(number->IsFamily()) << "only the barriers that create nothing answer yes";
}
