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
#include <regex>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/query/columnLayout.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"   // a dialect to read the rows' SQL in

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

// A register with NO dimensions has nothing to put first: the index would be the period alone, which is how
// the key already opens - one more index for the trigger to keep on every movement, serving no reading.
TEST(AccumulationRegisterSurface, ARegisterWithoutDimensionsGetsNoDimensionFirstIndex) {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	auto* reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
		cfg.CreateMetaObject(g_metaAccumulationRegisterCLSID, root, /*runObject*/ false));
	ASSERT_NE(reg, nullptr);
	cfg.CreateMetaObject(g_metaResourceCLSID, reg, false);   // a resource, so the totals are declared at all

	ibSchemaSnapshot snapshot;
	reg->ContributeTables(snapshot);
	bool sawTotals = false;
	for (const ibSchemaTable& t : snapshot.Tables()) {
		if (!t.m_derived || t.m_materialize.m_views.empty())
			continue;
		sawTotals = true;
		for (const ibSchemaIndex& index : t.m_indexes)
			EXPECT_FALSE(index.m_name.EndsWith(wxT("_DL"))) << index.m_name.ToStdString();
	}
	EXPECT_TRUE(sawTotals);
}

// The dressed views stay what they were — what a query reads directly, the turnovers with both halves in
// them — and they are ALL that is declared: the readings that fold take the arms as relations over the two
// tables (GetTotalsRows), so no second view exists for a base to lack or a downgrade to leave standing.
TEST(AccumulationRegisterSurface, OnlyTheDressedViewsAreDeclared) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr);
	const ibMaterializeView* dressed = f.View(f.reg->GetTurnoverViewName());
	ASSERT_NE(dressed, nullptr);
	EXPECT_TRUE(dressed->m_withMovements);
	EXPECT_NE(f.View(f.reg->GetBalanceViewName()), nullptr);   // and the balance view beside it
	EXPECT_EQ(f.totals->m_materialize.m_views.size(), 2u);
}

namespace {

wxString SqlOf(const ibQueryRelPtr& rel)
{
	return ibQueryRenderer(ibDatabaseLayerSQLite::Dialect()).Render(ibQueryIR(ibProject(rel))).m_sql;
}

// 🛑 WHETHER THE SQL NAMES A RELATION IS ASKED OF THE WHOLE IDENTIFIER. A balance register's totals table is
// `<register>_BalanceTotals`, so a plain Contains took it for the balance view `<register>_Balance`: the stored
// rows read exactly the table they should, and the test called that the view (CI, 2026-09-21).
bool NamesRelation(const wxString& sql, const wxString& name)
{
	return std::regex_search(sql.ToStdString(), std::regex("\\b" + name.ToStdString() + "\\b"));
}

} // namespace

// ⭐ THE ROWS AS THEY STAND COME FROM THE DECLARATION: the stored half is the totals table itself, the movement
// half is the movements — neither names a view, and both publish the figures under the dressed view's names, so
// a reading swaps the relation and not the columns.
TEST(AccumulationRegisterSurface, TheRowsAsTheyStandReadTheTwoTables) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr);

	ibQueryRelPtr stored, moved;
	ASSERT_TRUE(f.reg->GetTotalsRows(stored, moved));
	ASSERT_TRUE(stored);
	ASSERT_TRUE(moved) << "a register with a recorder has a movement arm";

	const wxString storedSql = SqlOf(stored);
	const wxString movedSql  = SqlOf(moved);
	EXPECT_TRUE(NamesRelation(storedSql, f.totals->m_name)) << storedSql;
	EXPECT_FALSE(storedSql.Contains(wxT("GROUP BY"))) << storedSql;
	EXPECT_TRUE(NamesRelation(movedSql, f.totals->m_materialize.SourceTable())) << movedSql;
	EXPECT_FALSE(NamesRelation(movedSql, f.totals->m_name)) << movedSql;
	for (const wxString& sql : { storedSql, movedSql }) {
		EXPECT_FALSE(NamesRelation(sql, f.reg->GetTurnoverViewName())) << sql;
		EXPECT_FALSE(NamesRelation(sql, f.reg->GetBalanceViewName())) << sql;
	}

	const ibMaterializeView* dressed = f.View(f.reg->GetTurnoverViewName());
	ASSERT_NE(dressed, nullptr);
	for (const ibMaterializeViewColumn& c : dressed->m_columns) {
		EXPECT_TRUE(storedSql.Contains(c.m_alias)) << c.m_alias.ToStdString();
		EXPECT_TRUE(movedSql.Contains(c.m_alias)) << c.m_alias.ToStdString();
	}
}

// The read forms are the declaration's own: every contribution, the guard (an inactive movement counts for
// nothing, read or accumulated), the movement's instant — and the period's TYPE TAG, which the movements'
// period index opens with and without which a reader's `period >= <floor>` walked every movement there is.
TEST(AccumulationRegisterSurface, TheMovementRowsCarryTheReadForms) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr);

	const ibMaterializeSpec spec = f.totals->m_materialize.ToReadSpec(f.totals->m_name);
	ASSERT_FALSE(spec.m_deltas.empty());
	for (const ibMaterializeDelta& d : spec.m_deltas)
		EXPECT_TRUE(d.m_valueIR) << d.m_column.ToStdString();
	EXPECT_TRUE(spec.m_guardIR);
	EXPECT_TRUE(spec.m_periodSourceIR);
	EXPECT_TRUE(spec.m_periodIsDateIR);

	// …and the apply's spec carries none of it: the maintenance renders text and never pays for a lowering.
	const ibMaterializeSpec render = f.totals->m_materialize.ToRenderSpec(f.totals->m_name);
	EXPECT_FALSE(render.m_guardIR);
	EXPECT_FALSE(render.m_periodIsDateIR);

	wxString tagField;
	for (const ibColumnSlot& slot : DescribeColumnLayout(f.reg->GetRegisterPeriod()->GetQueryColumn()))
		if (slot.m_role == ibColumnRole::Discriminator)
			tagField = slot.m_name;
	ASSERT_FALSE(tagField.IsEmpty());

	ibQueryRelPtr stored, moved;
	ASSERT_TRUE(f.reg->GetTotalsRows(stored, moved));
	const wxString movedSql = SqlOf(moved);
	EXPECT_TRUE(movedSql.Contains(tagField)) << movedSql;
}
