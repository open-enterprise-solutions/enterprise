// =============================================================================
// What an accumulation register DECLARES for its readings — read off the declaration itself.
//
// A balance is asked of one warehouse, of these items, up to a moment: equalities on the dimensions and a
// bound on the period. Until 2026-09-19 nothing the register declared could serve that question —
//   * the totals' only index opened with the PERIOD, so every balance walked every warehouse's rows;
//   * the only surface was a view that computed six calendar units on every stored row and, with split
//     totals, folded the shards in a GROUP BY the engine will not push a condition beneath.
// Measured on Firebird over three months of a ten-warehouse base: 0.6 s a balance, every posting; 0.04 s
// once the register declared what these tests pin.
//
// DB-free: a fresh in-memory configuration, never run — ContributeTables only DECLARES.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/query/columnLayout.h"

namespace {

struct RegisterSurfaceFix {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectAccumulationRegister* reg = nullptr;
	ibSchemaSnapshot snapshot;
	const ibSchemaTable* totals = nullptr;

	RegisterSurfaceFix() {
		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		if (root == nullptr) return;
		reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
			cfg.CreateMetaObject(g_metaAccumulationRegisterCLSID, root, /*runObject*/ false));
		if (reg == nullptr) return;
		cfg.CreateMetaObject(g_metaDimensionCLSID, reg, false);   // "warehouse"
		cfg.CreateMetaObject(g_metaDimensionCLSID, reg, false);   // "item"
		cfg.CreateMetaObject(g_metaResourceCLSID,  reg, false);   // "quantity"

		reg->ContributeTables(snapshot);
		for (const ibSchemaTable& t : snapshot.Tables())
			if (t.m_derived && !t.m_materialize.m_views.empty())
				totals = &t;
	}

	const ibMaterializeView* View(const wxString& name) const {
		if (totals == nullptr) return nullptr;
		for (const ibMaterializeView& v : totals->m_materialize.m_views)
			if (v.m_name == name) return &v;
		return nullptr;
	}
};

} // namespace

// ⭐ THE INDEX A READING CAN RIDE: every dimension first, in declared order, the period last — and not
// unique, because it is a read path and not the key.
TEST(AccumulationRegisterSurface, TotalsCarryADimensionFirstIndex) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr) << "a register with a resource declares a maintained totals table";

	const auto dimensions = f.reg->GetDimensionArrayObject();
	ASSERT_EQ(dimensions.size(), 2u);

	const ibSchemaIndex* read = nullptr;
	for (const ibSchemaIndex& index : f.totals->m_indexes)
		if (index.m_name.EndsWith(wxT("_DL")))
			read = &index;
	ASSERT_NE(read, nullptr) << "no dimension-first index on " << f.totals->m_name.ToStdString();

	EXPECT_FALSE(read->m_unique);
	ASSERT_EQ(read->m_columns.size(), 3u);
	EXPECT_EQ(read->m_columns[0], dimensions[0]->GetQueryColumn());
	EXPECT_EQ(read->m_columns[1], dimensions[1]->GetQueryColumn());
	EXPECT_EQ(read->m_columns[2]->GetPhysicalName(), f.totals->m_materialize.m_periodColumn)
		<< "the period closes the index — the bound a balance is read up to";

	// …and the key is still there, still unique, still opening with the period: the upsert names all of it.
	bool keyKept = false;
	for (const ibSchemaIndex& index : f.totals->m_indexes)
		if (index.m_name.EndsWith(wxT("_PK")) && index.m_unique)
			keyKept = true;
	EXPECT_TRUE(keyKept);
}

// The dressed view stays what it was — what a query reads directly — with both halves in it.
TEST(AccumulationRegisterSurface, TheDressedViewIsUntouched) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	const ibMaterializeView* dressed = f.View(f.reg->GetTurnoverViewName());
	ASSERT_NE(dressed, nullptr);
	EXPECT_FALSE(dressed->m_rawRows);
	EXPECT_FALSE(dressed->m_movementsOnly);
	EXPECT_TRUE(dressed->m_withMovements);
	EXPECT_TRUE(dressed->m_movementWhere.IsEmpty());

	EXPECT_NE(f.View(f.reg->GetBalanceViewName()), nullptr);   // and the balance view beside it
}

// ⭐ THE ROWS AS THEY STAND, IN TWO RELATIONS: the stored half with nothing computed and nothing grouped,
// the movement half on its own — so a reading that cuts between them asks each of its own.
TEST(AccumulationRegisterSurface, TheFlowIsDeclaredAsTwoHalves) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);

	const ibMaterializeView* stored = f.View(f.reg->GetFlowViewName());
	ASSERT_NE(stored, nullptr);
	EXPECT_TRUE(stored->m_rawRows);
	EXPECT_FALSE(stored->m_withMovements);
	EXPECT_FALSE(stored->m_movementsOnly);
	EXPECT_FALSE(stored->m_movementColumns.empty()) << "published as typed nulls — one column list for both halves";

	const ibMaterializeView* moved = f.View(f.reg->GetFlowMovedViewName());
	ASSERT_NE(moved, nullptr);
	EXPECT_TRUE(moved->m_rawRows);
	EXPECT_TRUE(moved->m_movementsOnly);

	// The same figures under the same names on all three surfaces — a reader swaps the relation, not the columns.
	const ibMaterializeView* dressed = f.View(f.reg->GetTurnoverViewName());
	ASSERT_NE(dressed, nullptr);
	ASSERT_EQ(stored->m_columns.size(), dressed->m_columns.size());
	ASSERT_EQ(moved->m_columns.size(), dressed->m_columns.size());
	for (size_t i = 0; i < dressed->m_columns.size(); ++i) {
		EXPECT_EQ(stored->m_columns[i].m_alias, dressed->m_columns[i].m_alias);
		EXPECT_EQ(moved->m_columns[i].m_alias, dressed->m_columns[i].m_alias);
	}
}

// The movement half names the period's TYPE TAG: the movements' period index opens with it, the view does
// not publish it, and without it a reader's `period >= <floor>` walked every movement there is.
TEST(AccumulationRegisterSurface, TheMovementHalfNamesThePeriodsTypeTag) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	const ibMaterializeView* moved = f.View(f.reg->GetFlowMovedViewName());
	ASSERT_NE(moved, nullptr);

	wxString tagField;
	for (const ibColumnSlot& slot : DescribeColumnLayout(f.reg->GetRegisterPeriod()->GetQueryColumn()))
		if (slot.m_role == ibColumnRole::Discriminator)
			tagField = slot.m_name;
	ASSERT_FALSE(tagField.IsEmpty());

	EXPECT_EQ(moved->m_movementWhere,
		wxString::Format(wxT("{row}.%s = %i"), tagField, ibPersistedTypeTag(ibColumnRole::Date)));
}
