// A totals rebuild is decided from the declared type, not the SQL layout. CatalogRef.A|B|C and
// A|B occupy the same reference pair, and the differ still clears every C. A wider string, or a
// type that was added, clears nothing.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/compiler/value.h"
#include "backend/query/columnLayout.h"
#include "backend/query/derivedStateBuilder.h"
#include "backend/query/queryable.h"
#include "backend/query/schemaSnapshot.h"
#include "core/clsid.h"

namespace {

struct TypedColumn : ibBackendQueryColumn {
	wxString physical;
	ibMetaID id = 1;
	mutable ibTypeDescription type;

	wxString GetName() const override { return physical; }
	wxString GetPhysicalName() const override { return physical; }
	ibTypeDescription& GetTypeDesc() const override { return type; }
	ibMetaID GetColumnId() const override { return id; }
};

struct TotalsTypeLoss : ::testing::Test {
	wxInitializer wx;
};

void ExpectSameLayout(const ibBackendQueryColumn& was, const ibBackendQueryColumn& now)
{
	const std::vector<ibColumnSlot> oldSlots = DescribeColumnLayout(&was);
	const std::vector<ibColumnSlot> newSlots = DescribeColumnLayout(&now);
	ASSERT_EQ(oldSlots.size(), newSlots.size());
	for (size_t i = 0; i < oldSlots.size(); ++i) {
		EXPECT_EQ(oldSlots[i].m_name, newSlots[i].m_name);
		EXPECT_TRUE(ibSameFieldType(oldSlots[i].m_type, newSlots[i].m_type));
	}
}

ibSchemaTable Derived(const ibBackendQueryColumn* key)
{
	ibSchemaTable table;
	table.m_derived = true;
	table.m_materialize.Key(key);
	return table;
}

} // namespace

TEST_F(TotalsTypeLoss, ARemovedReferenceTargetOnTheKeyRebuilds)
{
	TypedColumn was;
	was.physical = wxT("goods");
	was.id = 7;
	was.type.AppendMetaType(reference_to_clsid(1));
	was.type.AppendMetaType(reference_to_clsid(2));
	was.type.AppendMetaType(reference_to_clsid(3));
	TypedColumn now = was;
	now.type.ClearMetaType(reference_to_clsid(3));
	ExpectSameLayout(was, now);

	const ibSchemaTable oldTable = Derived(&was);
	const ibSchemaTable newTable = Derived(&now);
	EXPECT_TRUE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, ARemovedReferenceTargetInTheContributionRebuilds)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("qty");
	was.id = 9;
	was.type.AppendMetaType(reference_to_clsid(1));
	was.type.AppendMetaType(reference_to_clsid(2));
	was.type.AppendMetaType(reference_to_clsid(3));
	TypedColumn now = was;
	now.type.ClearMetaType(reference_to_clsid(3));
	ExpectSameLayout(was, now);

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&was));
	newTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&now));
	EXPECT_TRUE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, ARemovedReferenceTargetInTheGuardRebuilds)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("active");
	was.id = 4;
	was.type.AppendMetaType(reference_to_clsid(1));
	was.type.AppendMetaType(reference_to_clsid(2));
	TypedColumn now = was;
	now.type.ClearMetaType(reference_to_clsid(2));

	ibQueryCondition oldCond;
	oldCond.m_col = &was;
	ibQueryCondition newCond;
	newCond.m_col = &now;

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.m_guardExpr = ibQueryPredicate::Leaf(oldCond);
	newTable.m_materialize.m_guardExpr = ibQueryPredicate::Leaf(newCond);
	EXPECT_TRUE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, AWiderStringClearsNothing)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("code");
	was.id = 3;
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	was.type.m_typeData.SetString(10);
	TypedColumn now = was;
	now.type.m_typeData.SetString(20);

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&was));
	newTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&now));
	EXPECT_FALSE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}

TEST_F(TotalsTypeLoss, AnAddedTypeClearsNothing)
{
	TypedColumn key;
	key.physical = wxT("period");
	key.id = 1;
	key.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	TypedColumn was;
	was.physical = wxT("qty");
	was.id = 3;
	was.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);
	TypedColumn now = was;
	now.type.AppendMetaType(ibValueTypes::TYPE_STRING);

	ibSchemaTable oldTable = Derived(&key);
	ibSchemaTable newTable = Derived(&key);
	oldTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&was));
	newTable.m_materialize.Accumulate(nullptr, wxString(), ibQueryColumnExpr::Col(&now));
	EXPECT_FALSE(ibDerivedState::NeedsRegeneration(&oldTable, newTable));
}
