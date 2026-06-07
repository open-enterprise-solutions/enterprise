// Showcase: ibListSqlBuilder  →  ibDatabaseQueryBuilder (L2)
// (docs/query-language-arc.md §18).
//
// ibListSqlBuilder (metaCollection/partial/list/listSqlBuilder.cpp) is the
// hand-rolled SQL-fragment builder behind the paged Fetch impls. It is a
// proto-builder that CONFLATES two tiers:
//   * L3 work — metadata → physical names (composite _N/_S/_R fields, guidName
//     for reference attrs, the keyset cursor predicate);
//   * L2 work — physical → dialect SQL, and there it carries the disease this
//     arc cures: an explicit per-DBMS fork. BuildSelectHead / AppendLimit
//     hand-code `if GetDatabaseLayerType() == DATABASELAYER_FIREBIRD` to emit
//     `SELECT FIRST n` on Firebird vs `... LIMIT n` everywhere else.
//
// The clean split routes the L2 half through ibDatabaseQueryBuilder: the SAME
// fluent build renders to dialect-correct SQL for every driver, and the FB fork
// vanishes — it lives once, as data, inside ibDialectDictionary. The bind plan
// is produced by the renderer from Const/Param nodes, so the manual positional
// BindFilterParams / BindAnchorSortParams also disappear.
//
// Pure test: Build() needs no connection (ibConnectionScope is passive without
// an appData pool), so we render the built IR through three dictionaries and
// compare. No database, no appData.

#include <gtest/gtest.h>

#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

std::string Sql(const ibRenderedQuery& r) { return r.m_sql.ToStdString(); }

// Each driver owns its dialect now (no free factory) — pull it from the driver's
// static accessor (no instance, no DB client lib).
ibDialectDictionary PgDialect()     { return ibDatabaseLayerPostgres::Dialect(); }
ibDialectDictionary FbDialect()     { return ibDatabaseLayerFirebird::Dialect(); }
ibDialectDictionary SqliteDialect() { return ibDatabaseLayerSQLite::Dialect(); }

// The paged list query ibListSqlBuilder assembles for a Catalog list view:
//   BuildSelectHead + BuildFilterWhere(Code_S = ?) + BuildOrderBy(Code_S ASC)
//   + AppendLimit(50)
// — but as one fluent build that yields a dialect-free ibQueryIR.
ibQueryIR PagedListIR()
{
	return ibDatabaseQueryBuilder()
		.From(wxT("Reference17"))
		.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("Code_S")), ibParam(0)))
		.OrderBy(wxT("Code_S"), ibQuerySortDir::Asc)
		.Limit(50)
		.Build();
}

} // namespace

// The fork ibListSqlBuilder hand-codes (FIRST vs LIMIT) — gone. One IR, the
// dictionary spells each dialect.
TEST(QueryBuilderShowcase, PagedList_Firebird_FirstClause)
{
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).Render(PagedListIR());
	EXPECT_EQ(Sql(out),
		"SELECT FIRST 50 * FROM Reference17 WHERE (Code_S = ?) ORDER BY Code_S ASC");
}

TEST(QueryBuilderShowcase, PagedList_Sqlite_LimitClause)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(PagedListIR());
	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference17 WHERE (Code_S = ?) ORDER BY Code_S ASC LIMIT 50");
}

TEST(QueryBuilderShowcase, PagedList_Postgres_LimitAndDollarParam)
{
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(PagedListIR());
	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference17 WHERE (Code_S = $1) ORDER BY Code_S ASC LIMIT 50");
}

// The filter value rides as ONE external bind param, identical across dialects —
// replacing ibListSqlBuilder::BindFilterParams' manual positional binding.
TEST(QueryBuilderShowcase, BindPlanIsDialectIndependent)
{
	const ibRenderedQuery fb = ibQueryRenderer(FbDialect()).Render(PagedListIR());
	const ibRenderedQuery pg = ibQueryRenderer(PgDialect()).Render(PagedListIR());

	ASSERT_EQ(fb.m_params.size(), 1u);
	ASSERT_EQ(pg.m_params.size(), 1u);
	EXPECT_TRUE(fb.m_params[0].m_external);
	EXPECT_EQ(fb.m_params[0].m_externalIndex, 0);
	EXPECT_EQ(pg.m_params[0].m_externalIndex, 0);
}

// Multi-column ORDER BY (BuildOrderBy's multi-field pass) + a `<>` filter
// (BuildFilterWhere's non-equal op) + paging with an offset. FB uses FIRST/SKIP,
// SQLite uses LIMIT/OFFSET — same fluent build.
namespace {

ibQueryIR DocListIR()
{
	return ibDatabaseQueryBuilder()
		.From(wxT("Document42"))
		.Where(ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("Status_N")), ibParam(0)))
		.OrderBy(wxT("Date_D"),   ibQuerySortDir::Desc)
		.OrderBy(wxT("Number_S"), ibQuerySortDir::Asc)
		.Limit(20, 100)
		.Build();
}

} // namespace

TEST(QueryBuilderShowcase, DocList_Firebird_FirstSkipMultiSort)
{
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).Render(DocListIR());
	EXPECT_EQ(Sql(out),
		"SELECT FIRST 20 SKIP 100 * FROM Document42 WHERE (Status_N <> ?) "
		"ORDER BY Date_D DESC, Number_S ASC");
}

TEST(QueryBuilderShowcase, DocList_Sqlite_LimitOffsetMultiSort)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(DocListIR());
	EXPECT_EQ(Sql(out),
		"SELECT * FROM Document42 WHERE (Status_N <> ?) "
		"ORDER BY Date_D DESC, Number_S ASC LIMIT 20 OFFSET 100");
}

// ===========================================================================
// The hardest fragment — ibListSqlBuilder::BuildAnchorPredicate (the keyset
// cursor). Proves L2 expresses even the OR-of-AND lexicographic predicate that
// the hand-rolled builder string-assembles. For one sort column (Code_S ASC)
// plus the row-identity tail, forward + exclusive, the cursor is:
//
//     (Code_S > ?)
//     OR (Code_S = ? AND Ref > <lastGuid>)
//
// Built as nested ibBinOp(Or/And/Gt/Eq). Two improvements over ibListSqlBuilder
// fall out for free: the manual BindAnchorSortParams positional binding is gone,
// and the last-row guid — which ibListSqlBuilder inlines as a string literal
// `'%s'` — rides as a BOUND parameter (ibConst), not concatenated SQL.
// ===========================================================================
namespace {

ibQueryIR KeysetCursorIR()
{
	// "Ref" stands in for the row-identity column (guidName in the live code).
	ibQueryExprPtr anchor =
		ibBinOp(ibQueryBinOp::Or,
			ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("Code_S")), ibParam(0)),
			ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("Code_S")), ibParam(1)),
				ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("Ref")),
				        ibConst(ibValue(wxString(wxT("APPLE-LAST")))))));

	return ibDatabaseQueryBuilder()
		.From(wxT("Reference17"))
		.Where(anchor)
		.OrderBy(wxT("Code_S"), ibQuerySortDir::Asc)
		.OrderBy(wxT("Ref"),    ibQuerySortDir::Asc)
		.Limit(50)
		.Build();
}

} // namespace

TEST(QueryBuilderShowcase, KeysetCursor_Firebird_OrOfAnd)
{
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).Render(KeysetCursorIR());
	EXPECT_EQ(Sql(out),
		"SELECT FIRST 50 * FROM Reference17 "
		"WHERE ((Code_S > ?) OR ((Code_S = ?) AND (Ref > ?))) "
		"ORDER BY Code_S ASC, Ref ASC");
}

TEST(QueryBuilderShowcase, KeysetCursor_GuidIsBoundNotInlined)
{
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(KeysetCursorIR());

	// $1, $2 are the external sort-col binds; $3 is the guid as a bound const —
	// the literal string never reaches the SQL text.
	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference17 "
		"WHERE ((Code_S > $1) OR ((Code_S = $2) AND (Ref > $3))) "
		"ORDER BY Code_S ASC, Ref ASC LIMIT 50");

	ASSERT_EQ(out.m_params.size(), 3u);
	EXPECT_TRUE(out.m_params[0].m_external);          // sort col, external idx 0
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);
	EXPECT_TRUE(out.m_params[1].m_external);          // sort col, external idx 1
	EXPECT_EQ(out.m_params[1].m_externalIndex, 1);
	EXPECT_FALSE(out.m_params[2].m_external);          // guid — bound const, not inlined
	EXPECT_EQ(out.m_params[2].m_value.GetString().ToStdString(), "APPLE-LAST");
}

// ===========================================================================
// Aggregation (ИТОГИ): the fluent door's GroupBy() + Project() emit an Aggregate
// IR node from Build() — the register-balance / turnover compute shape, dialect-free.
// ===========================================================================
namespace {

ibQueryIR BalanceIR()
{
	return ibDatabaseQueryBuilder()
		.From(wxT("Register7"))
		.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("active_B")), ibParam(0)))
		.Project({ { ibCol(wxT("recorder")), wxEmptyString },
		           { ibFunc(wxT("SUM"), { ibCol(wxT("qty_N")) }), wxT("balance") } })
		.GroupBy(ibCol(wxT("recorder")))
		.OrderBy(wxT("recorder"), ibQuerySortDir::Asc)
		.Build();
}

} // namespace

TEST(QueryBuilderShowcase, Aggregate_GroupBySumOrdered)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(BalanceIR());
	EXPECT_EQ(Sql(out),
		"SELECT recorder, SUM(qty_N) AS balance FROM Register7 WHERE (active_B = ?) "
		"GROUP BY recorder ORDER BY recorder ASC");
	ASSERT_EQ(out.m_params.size(), 1u);
}

// ===========================================================================
// JOIN composition: the fluent door's Join() chains a FROM-tree source. This is
// the physical shape the L3 dot-walk lowers to — "Номенклатура.Производитель.
// Наименование" becomes a catalog ⋈ producer-catalog join on the reference GUID
// column (fldN_RRRef) = the producer's row key (uuid). Columns are QUALIFIED by
// source table (ibCol(table, name)) and the projection is ALIASED so same-named
// physical columns across the two tables stay distinct in the cursor.
// ===========================================================================
namespace {

ibQueryIR RefJoinIR()
{
	return ibDatabaseQueryBuilder()
		.From(wxT("Reference17"))
		.Join(wxT("Reference42"),
			ibBinOp(ibQueryBinOp::Eq,
				ibCol(wxT("Reference17"), wxT("fld5_RRRef")),
				ibCol(wxT("Reference42"), wxT("uuid"))))
		.Project({ { ibCol(wxT("Reference17"), wxT("Code_S")), wxT("code") },
		           { ibCol(wxT("Reference42"), wxT("Name_S")), wxT("producer") } })
		.AddSortKey(ibQuerySortKey{ ibCol(wxT("Reference17"), wxT("Code_S")), ibQuerySortDir::Asc })
		.Build();
}

} // namespace

TEST(QueryBuilderShowcase, RefJoin_QualifiedColumnsAliasedProjection)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(RefJoinIR());
	EXPECT_EQ(Sql(out),
		"SELECT Reference17.Code_S AS code, Reference42.Name_S AS producer "
		"FROM Reference17 INNER JOIN Reference42 "
		"ON (Reference17.fld5_RRRef = Reference42.uuid) "
		"ORDER BY Reference17.Code_S ASC");
	ASSERT_EQ(out.m_params.size(), 0u);   // the join key is a column-to-column equality, no binds
}

// The whole join is metadata→physical structure with no dialect divergence (no
// pagination, no params) — so it renders byte-identical across every driver.
TEST(QueryBuilderShowcase, RefJoin_DialectIndependent)
{
	const std::string fb = Sql(ibQueryRenderer(FbDialect()).Render(RefJoinIR()));
	const std::string pg = Sql(ibQueryRenderer(PgDialect()).Render(RefJoinIR()));
	const std::string sl = Sql(ibQueryRenderer(SqliteDialect()).Render(RefJoinIR()));
	EXPECT_EQ(fb, sl);
	EXPECT_EQ(pg, sl);
}

// Virtual-table join — "Справочник ⋈ СрезПоследних". The slice's
// "MAX(period) GROUP BY dim" inner aggregate becomes a derived-table FROM source
// (ibSubquery), joined LEFT so a catalog row with no slice row still appears. The
// ibQueryRelPtr Join() overload composes a non-table right source.
namespace {

ibQueryIR SliceJoinIR()
{
	ibQueryRelPtr slice = ibSubquery(
		ibDatabaseQueryBuilder()
			.From(wxT("Register7"))
			.Project({ { ibCol(wxT("dim_ref")), wxEmptyString },
			           { ibFunc(wxT("MAX"), { ibCol(wxT("period_D")) }), wxT("maxp") } })
			.GroupBy(ibCol(wxT("dim_ref")))
			.Build().m_root,
		wxT("S"));

	return ibDatabaseQueryBuilder()
		.From(wxT("Reference17"))
		.Join(slice,
			ibBinOp(ibQueryBinOp::Eq,
				ibCol(wxT("Reference17"), wxT("uuid")),
				ibCol(wxT("S"), wxT("dim_ref"))),
			ibQueryJoinType::Left)
		.Build();
}

} // namespace

TEST(QueryBuilderShowcase, SliceJoin_DerivedTableLeftJoin)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(SliceJoinIR());
	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference17 LEFT JOIN "
		"(SELECT dim_ref, MAX(period_D) AS maxp FROM Register7 GROUP BY dim_ref) AS S "
		"ON (Reference17.uuid = S.dim_ref)");
}
