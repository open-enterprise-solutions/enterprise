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
// DB-free: a fresh in-memory application, never run — ContributeTables only DECLARES.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <regex>

#include <wx/string.h>   // wxT — the engine's headers include no wx

#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/query/provider/database/registerQueryLowering.h"  // ibRegFinerThanStored — which units the totals answer
#include "backend/query/provider/database/structure/schemaSnapshot.h"
#include "backend/query/columnLayout.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"   // a dialect to read the rows' SQL in

namespace {

struct RegisterSurfaceFix {
	ibMetaDataApplicationFile cfg;
	ibValueMetaObjectAccumulationRegister* reg = nullptr;
	ibSchemaSnapshot snapshot;
	const ibSchemaTable* totals = nullptr;

	RegisterSurfaceFix() {
		ibValueMetaObjectApplication* root = cfg.GetRootMetaObject();
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

	const ibMaterializeView* View(const ibString& name) const {
		if (totals == nullptr) return nullptr;
		for (const ibMaterializeView& v : totals->m_materialize.m_views)
			if (v.m_name == name) return &v;
		return nullptr;
	}
};

} // namespace

// ⭐ THE INDEX A READING CAN RIDE: every dimension first, in declared order, then the unit, the period last — and
// not unique, because it is a read path and not the key.
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
	ASSERT_EQ(read->m_columns.size(), 4u);
	EXPECT_EQ(read->m_columns[0], dimensions[0]->GetQueryColumn());
	EXPECT_EQ(read->m_columns[1], dimensions[1]->GetQueryColumn());
	EXPECT_EQ(read->m_columns[2]->GetPhysicalName(), f.totals->m_materialize.m_periodUnitColumn)
		<< "the unit is an equality, as a dimension is";
	EXPECT_EQ(read->m_columns[3]->GetPhysicalName(), f.totals->m_materialize.m_periodColumn)
		<< "the period closes the index — the bound a balance is read up to";

	// …and the key is still there, still unique — opening with the UNIT, the period after it: a reading of every
	// key takes a unit's rows as one range of it (`unit = Year AND period < …`), and the upsert names all of it.
	const ibSchemaIndex* key = nullptr;
	for (const ibSchemaIndex& index : f.totals->m_indexes)
		if (index.m_name.EndsWith(wxT("_PK")) && index.m_unique)
			key = &index;
	ASSERT_NE(key, nullptr);
	ASSERT_GE(key->m_columns.size(), 2u);
	EXPECT_EQ(key->m_columns[0]->GetPhysicalName(), f.totals->m_materialize.m_periodUnitColumn);
	EXPECT_EQ(key->m_columns[1]->GetPhysicalName(), f.totals->m_materialize.m_periodColumn);
}

// ⭐ EVERY DIMENSION IS A CUT, and each has its own index: the dimension, the unit, the period. `_DL` above serves a
// reading of the leading dimensions only; a reading by the second one rides this.
TEST(AccumulationRegisterSurface, TotalsCarryAnIndexPerDimension) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr);

	for (const auto dimension : f.reg->GetDimensionArrayObject()) {
		const ibString name = f.totals->m_name + ibString::Format(ibT("_CL%d"), dimension->GetQueryColumn()->GetColumnId());
		const ibSchemaIndex* cut = nullptr;
		for (const ibSchemaIndex& index : f.totals->m_indexes)
			if (index.m_name == name)
				cut = &index;
		ASSERT_NE(cut, nullptr) << "no index of its own for the cut " << name.ToStdString();
		EXPECT_FALSE(cut->m_unique);
		ASSERT_EQ(cut->m_columns.size(), 3u);
		EXPECT_EQ(cut->m_columns[0], dimension->GetQueryColumn());
		EXPECT_EQ(cut->m_columns[1]->GetPhysicalName(), f.totals->m_materialize.m_periodUnitColumn);
		EXPECT_EQ(cut->m_columns[2]->GetPhysicalName(), f.totals->m_materialize.m_periodColumn);
	}
}

// 🛑 THE PERIOD CLOSES THE INDEX WHATEVER IS CUT. "Equalities on the dimensions, a RANGE on the period" is served
// only while the period stands right after the dimensions that were kept. The columns were cut greedily from
// the end, so with a key too wide for the engine it was the PERIOD that went first - the index then narrowed by
// the dimensions and walked the key's whole history. Asked with a ceiling of the test's own: a suite has no
// engine to ask, and with none nothing is ever cut.
TEST(AccumulationRegisterSurface, WhenTheKeyIsTooWideTheDimensionsAreCutAndThePeriodStays) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr);
	const ibSchemaIndex* read = nullptr;
	for (const ibSchemaIndex& index : f.totals->m_indexes)
		if (index.m_name.EndsWith(wxT("_DL")))
			read = &index;
	ASSERT_NE(read, nullptr);
	ASSERT_EQ(read->m_columns.size(), 4u);   // two dimensions, the unit, the period
	const std::shared_ptr<const ibBackendQueryColumn> first = read->m_columns[0];
	const std::shared_ptr<const ibBackendQueryColumn> second = read->m_columns[1];
	const std::shared_ptr<const ibBackendQueryColumn> period = read->m_columns[3];

	const size_t room = DescribeColumnLayout(*first).size() + DescribeColumnLayout(*period).size();
	const auto ceiling = [room](size_t fields, size_t) { return fields <= room; };   // the first dimension + the period, no more

	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> fitted = ibFitLookupColumns({ first, second }, period, ceiling);
	ASSERT_EQ(fitted.size(), 2u);
	EXPECT_EQ(fitted[0], first);
	EXPECT_EQ(fitted[1], period) << "the second dimension is what gives way, not the period";

	// Without a closing column the cut is what it always was - the leading columns that fit…
	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> leading = ibFitLookupColumns({ first, second, period }, nullptr, ceiling);
	ASSERT_FALSE(leading.empty());
	EXPECT_EQ(leading.front(), first);
	EXPECT_NE(leading.back(), period) << "this is the defect the closing column is there for";

	// …and with no ceiling at all nothing is cut.
	EXPECT_EQ(ibFitLookupColumns({ first, second }, period, nullptr).size(), 3u);
}

// A register with NO dimensions has nothing to put first: the index would be the period alone, which is how
// the key already opens - one more index for the trigger to keep on every movement, serving no reading.
TEST(AccumulationRegisterSurface, ARegisterWithoutDimensionsGetsNoDimensionFirstIndex) {
	ibMetaDataApplicationFile cfg;
	ibValueMetaObjectApplication* root = cfg.GetRootMetaObject();
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
// tables (ibRegFillTotalsRows), so no second view exists for a base to lack or a downgrade to leave standing.
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

// ⭐ A UNIT FINER THAN THE DAY IS NO PROJECTION OF THE TOTALS: a stored row holds its whole day under its first
// instant, so an hour, a minute, a second are answered by the movements — on every road (ibRegFinerThanStored).
TEST(AccumulationRegisterSurface, AUnitFinerThanTheDayReadsTheMovements) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ibRegFold fold;
	fold.m_kind = ibRegGranularity::Calendar;
	for (const ibTotalsPeriod finer : { ibTotalsPeriod::Second, ibTotalsPeriod::Minute, ibTotalsPeriod::Hour }) {
		fold.m_unit = finer;
		EXPECT_TRUE(ibRegFinerThanStored(f.reg, fold)) << ibRegisterUnitWord(finer).ToStdString();
	}
	for (const ibTotalsPeriod kept : { ibTotalsPeriod::Day, ibTotalsPeriod::Week, ibTotalsPeriod::Month, ibTotalsPeriod::Year }) {
		fold.m_unit = kept;
		EXPECT_FALSE(ibRegFinerThanStored(f.reg, fold)) << ibRegisterUnitWord(kept).ToStdString();
	}
	fold.m_kind = ibRegGranularity::Recorder;   // the movement grain has a road of its own, not this one
	EXPECT_FALSE(ibRegFinerThanStored(f.reg, fold));
}

// ⭐ THE TOTALS KEEP EVERY UNIT OF ibRegTotalsUnits, each its own rows under the unit column, the day first — so one
// posting is one delta per unit: the insert trigger writes the movement into the row of each, keyed by that unit's
// truncation of the movement's instant.
TEST(AccumulationRegisterSurface, ThePostingIsWrittenIntoTheRowOfEveryUnit) {
	RegisterSurfaceFix f;
	ASSERT_NE(f.reg, nullptr);
	ASSERT_NE(f.totals, nullptr);

	const ibSchemaMaterialize& m = f.totals->m_materialize;
	const std::vector<ibTotalsPeriod> units(std::begin(ibRegTotalsUnits), std::end(ibRegTotalsUnits));
	EXPECT_EQ(m.m_periodUnitColumn, ibString(PeriodUnitColumnName()));
	EXPECT_EQ(m.m_periodUnits, units);
	EXPECT_EQ(m.m_periodUnits.front(), f.reg->GetTotalsPeriodUnit()) << "the finest is the grain every reading means";

	const ibMaterializeSpec render = m.ToRenderSpec(f.totals->m_name);
	const ibMaterializeSql sql = RenderMaterialization(render,
		&ibDatabaseLayerSQLite::MaterializationDialect(), ibDatabaseLayerSQLite::Dialect());
	ibString onInsert;
	for (size_t i = 0; !sql.CreateAt(i).IsEmpty(); ++i)
		if (sql.CreateAt(i).Contains(f.totals->m_name + wxT("_AI")))
			onInsert = sql.CreateAt(i);
	ASSERT_FALSE(onInsert.IsEmpty());

	const ibString upsert = wxT("INSERT INTO ") + f.totals->m_name + wxT(" (");
	size_t deltas = 0;
	for (size_t at = onInsert.find(upsert, 0); at != ibString::npos; at = onInsert.find(upsert, at + upsert.length()))
		++deltas;
	EXPECT_EQ(deltas, units.size()) << onInsert.ToStdString();

	ibString instant = render.m_periodSourceExpr;
	instant.Replace(wxT("{row}"), ibDatabaseLayerSQLite::MaterializationDialect().m_newRowAlias);
	for (const ibTotalsPeriod unit : units) {
		ibString truncated = ibDatabaseLayerSQLite::Dialect().m_periodTrunc.at(unit);
		truncated.Replace(wxT("{expr}"), instant);
		EXPECT_TRUE(onInsert.Contains(truncated)) << ibRegisterUnitWord(unit).ToStdString();
	}
}

namespace {

ibString SqlOf(const ibQueryRelPtr& rel)
{
	return ibQueryRenderer(ibDatabaseLayerSQLite::Dialect()).Render(ibQueryIR(ibProject(rel))).m_sql;
}

// 🛑 WHETHER THE SQL NAMES A RELATION IS ASKED OF THE WHOLE IDENTIFIER. A balance register's totals table is
// `<register>_BalanceTotals`, so a plain Contains took it for the balance view `<register>_Balance`: the stored
// rows read exactly the table they should, and the test called that the view (CI, 2026-09-21).
bool NamesRelation(const ibString& sql, const ibString& name)
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

	ibMaterializeReadSpec read;
	ASSERT_TRUE(ibRegFillTotalsRows(read, f.reg));
	ASSERT_TRUE(read.m_storedRows);
	ASSERT_TRUE(read.m_movedRows) << "a register with a recorder has a movement arm";

	// The stored rows are of every unit the totals keep, and are handed over with the units and their column.
	EXPECT_EQ(read.m_storedUnits, std::vector<ibTotalsPeriod>(std::begin(ibRegTotalsUnits), std::end(ibRegTotalsUnits)));
	EXPECT_EQ(read.m_periodUnitColumn, ibString(PeriodUnitColumnName()));

	const ibString storedSql = SqlOf(read.m_storedRows);
	const ibString movedSql  = SqlOf(read.m_movedRows);
	EXPECT_TRUE(NamesRelation(storedSql, f.totals->m_name)) << storedSql;
	EXPECT_TRUE(NamesRelation(storedSql, PeriodUnitColumnName())) << storedSql;
	EXPECT_FALSE(storedSql.Contains(wxT("WHERE"))) << storedSql;   // no unit chosen here: the reading says which
	EXPECT_FALSE(storedSql.Contains(wxT("GROUP BY"))) << storedSql;
	EXPECT_TRUE(NamesRelation(movedSql, f.totals->m_materialize.SourceTable())) << movedSql;
	EXPECT_FALSE(NamesRelation(movedSql, f.totals->m_name)) << movedSql;
	for (const ibString& sql : { storedSql, movedSql }) {
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

	ibString tagField;
	for (const ibColumnSlot& slot : DescribeColumnLayout(*f.reg->GetRegisterPeriod()->GetQueryColumn()))
		if (slot.m_role == ibColumnRole::Discriminator)
			tagField = slot.m_name;
	ASSERT_FALSE(tagField.IsEmpty());

	ibMaterializeReadSpec read;
	ASSERT_TRUE(ibRegFillTotalsRows(read, f.reg));
	const ibString movedSql = SqlOf(read.m_movedRows);
	EXPECT_TRUE(movedSql.Contains(tagField)) << movedSql;
}
