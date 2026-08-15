// =============================================================================
// OES Enterprise — a property absent from a node keeps its declared default
//
// `prop->SetNodeValue(node.GetProperty(prop->GetName()))` is how every property
// in the tree is read, and GetProperty answers an EMPTY value when the file has
// no such property. That empty value used to reach the property TYPE, and each
// type turned it into its own zero: false for a boolean, 0 for a number. So any
// property DECLARED with a non-zero default arrived unset the first time an
// older configuration was opened — silently, because an assigned zero is
// indistinguishable from a read one.
//
// It cost a full diagnosis to find, because the symptom surfaced four layers
// away: the accounting register's `Correspondence` and `SplitTotals` are both
// default-true, both went false on load, the credit-side analytics slots were
// deactivated in consequence, the schema snapshot then knew of no slots while
// the DDL still built them — and every apply re-issued ADD for columns that
// already existed, reported as `RDB$INDEX_15 violation` on a column the same
// apply had just created.
//
// The gate now lives in the non-virtual door (ibBackendProperty::SetNodeValue),
// so no property type ever sees an empty value. These tests pin the door's two
// halves: absent leaves the default alone, present is still read normally.
//
// Pure — no DB, no session, no metadata.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/propertyManager/propertyManager.h"          // ibPropertyObject
#include "backend/propertyManager/property/propertyBoolean.h"
#include "backend/propertyManager/property/propertyNumber.h"   // ibPropertyInteger
#include "backend/serialize/dataBuilder.h"                    // ibDataValue / ibDataNode

namespace {

// A property owner whose declared defaults are deliberately NOT the zero of
// their types — that is the whole point of the check.
class DefaultsNode : public ibPropertyObject {
public:
	wxString GetClassName() const override { return wxT("DefaultsNode"); }
	bool     IsEditable() const override { return true; }
	wxString GetObjectTypeName() const override { return wxT("DefaultsNode"); }

	// true / 8 / "on" — every one of them survives only if "absent" is not read
	// as "the type's zero".
	ibPropertyBoolean* m_propertyFlagOn  = CreateProperty<ibPropertyBoolean>(m_category, wxT("FlagOn"),  true);
	ibPropertyBoolean* m_propertyFlagOff = CreateProperty<ibPropertyBoolean>(m_category, wxT("FlagOff"), false);
	ibPropertyInteger* m_propertyShards  = CreateProperty<ibPropertyInteger>(m_category, wxT("Shards"), 8);
};

}  // namespace

// ---------------------------------------------------------------------------
// The case that was broken: the node simply has no such property.
// ---------------------------------------------------------------------------
TEST(PropertyDefaults, AbsentValueKeepsDeclaredDefault) {
	DefaultsNode node;
	ibDataNode empty;   // a configuration written before these properties existed

	// Exactly the call every metatype makes.
	EXPECT_FALSE(node.m_propertyFlagOn->SetNodeValue(empty.GetProperty(wxT("FlagOn"))))
		<< "nothing was read, and the door must say so";
	EXPECT_FALSE(node.m_propertyShards->SetNodeValue(empty.GetProperty(wxT("Shards"))));

	EXPECT_TRUE(node.m_propertyFlagOn->GetValueAsBoolean())
		<< "a default-true flag went false on load - this is the Correspondence / SplitTotals bug";
	EXPECT_EQ(node.m_propertyShards->GetValueAsInteger(), 8)
		<< "a default-8 number was reset to zero by a property that was never in the file";
}

// ---------------------------------------------------------------------------
// The gate must not swallow real values — including the ones that LOOK like
// the type's zero. "Absent" and "explicitly false" are different answers, and
// telling them apart is the entire fix.
// ---------------------------------------------------------------------------
TEST(PropertyDefaults, PresentValueIsReadEvenWhenItIsTheTypeZero) {
	DefaultsNode node;
	ibDataNode stored;
	stored.SetProperty(wxT("FlagOn"), ibDataValue::Bool(false));   // switched OFF on purpose
	stored.SetProperty(wxT("Shards"), ibDataValue::Int(0));

	EXPECT_TRUE(node.m_propertyFlagOn->SetNodeValue(stored.GetProperty(wxT("FlagOn"))));
	EXPECT_TRUE(node.m_propertyShards->SetNodeValue(stored.GetProperty(wxT("Shards"))));

	EXPECT_FALSE(node.m_propertyFlagOn->GetValueAsBoolean())
		<< "an explicit false must survive - otherwise the gate has merely moved the bug";
	EXPECT_EQ(node.m_propertyShards->GetValueAsInteger(), 0);
}

// ---------------------------------------------------------------------------
// A default-false property is the case that USED to pass by accident: its
// default and the type's zero coincide, so the old code looked correct on it.
// Kept so the pair reads as a contrast rather than a single example.
// ---------------------------------------------------------------------------
TEST(PropertyDefaults, DefaultFalseIsUnaffectedEitherWay) {
	DefaultsNode node;
	ibDataNode empty;

	node.m_propertyFlagOff->SetNodeValue(empty.GetProperty(wxT("FlagOff")));
	EXPECT_FALSE(node.m_propertyFlagOff->GetValueAsBoolean());

	ibDataNode stored;
	stored.SetProperty(wxT("FlagOff"), ibDataValue::Bool(true));
	node.m_propertyFlagOff->SetNodeValue(stored.GetProperty(wxT("FlagOff")));
	EXPECT_TRUE(node.m_propertyFlagOff->GetValueAsBoolean());
}

// ---------------------------------------------------------------------------
// Write then read back through the two doors — the round trip a save/load does.
// ---------------------------------------------------------------------------
TEST(PropertyDefaults, RoundTripThroughBothDoors) {
	DefaultsNode source;
	source.m_propertyFlagOn->SetValue(false);
	source.m_propertyShards->SetValue(3);

	ibDataNode saved;
	saved.SetProperty(wxT("FlagOn"), source.m_propertyFlagOn->GetNodeValue());
	saved.SetProperty(wxT("Shards"), source.m_propertyShards->GetNodeValue());

	DefaultsNode loaded;
	loaded.m_propertyFlagOn->SetNodeValue(saved.GetProperty(wxT("FlagOn")));
	loaded.m_propertyShards->SetNodeValue(saved.GetProperty(wxT("Shards")));

	EXPECT_FALSE(loaded.m_propertyFlagOn->GetValueAsBoolean());
	EXPECT_EQ(loaded.m_propertyShards->GetValueAsInteger(), 3);
}
