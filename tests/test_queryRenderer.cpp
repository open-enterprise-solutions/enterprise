// Golden tests for the L2 query renderer (docs/query-language-arc.md §6, §16).
//
// Pure: no database, no connection. One IR rendered through three dictionaries
// must yield three dialect-correct SQL strings + an identical bind plan. This
// is the proof that the Dialect Dictionary closes the dialect difference and
// that there are no per-DBMS forks in the renderer.

#include <gtest/gtest.h>

#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2: IR + DDL + DML + renderer (merged)
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#ifdef OES_USE_MYSQL
#include "backend/databaseLayer/mysql/mysqlDatabaseLayer.h"   // MySQL dialect — only when the driver is built
#endif
#include "backend/databaseLayer/databaseLayerException.h"   // ibBackendQueryException (AlterColumn-on-SQLite throw)

namespace {

// SELECT * FROM Reference17 WHERE (Code_S = ?) ORDER BY Code_S ASC  [limit 50]
ibQueryIR SampleListQuery()
{
	ibQueryRelPtr rel =
		ibLimit(
			ibSort(
				ibFilter(
					ibScan(wxT("Reference17")),
					ibBinOp(ibQueryBinOp::Eq,
					        ibCol(wxT("Code_S")),
					        ibConst(ibValue(wxString(wxT("APPLE-01")))))),
				{ { ibCol(wxT("Code_S")), ibQuerySortDir::Asc } }),
			50);
	return ibQueryIR(rel);
}

std::string Sql(const ibRenderedQuery& r) { return r.m_sql.ToStdString(); }

// Each driver owns its dialect — static accessor, no instance, no DB lib.
ibDialectDictionary PgDialect()     { return ibDatabaseLayerPostgres::Dialect(); }
ibDialectDictionary FbDialect()     { return ibDatabaseLayerFirebird::Dialect(); }
ibDialectDictionary SqliteDialect() { return ibDatabaseLayerSQLite::Dialect(); }
#ifdef OES_USE_MYSQL
ibDialectDictionary MysqlDialect()  { return ibDatabaseLayerMySQL::Dialect(); }
#endif

} // namespace

TEST(QueryRenderer, Firebird_UsesFirstForPagination)
{
	ibQueryRenderer renderer(FbDialect());
	const ibRenderedQuery out = renderer.Render(SampleListQuery());

	EXPECT_EQ(Sql(out),
		"SELECT FIRST 50 * FROM Reference17 WHERE (Code_S = ?) ORDER BY Code_S ASC");

	ASSERT_EQ(out.m_params.size(), 1u);
	EXPECT_FALSE(out.m_params[0].m_external);
	EXPECT_EQ(out.m_params[0].m_value.GetString().ToStdString(), "APPLE-01");
}

TEST(QueryRenderer, Sqlite_UsesLimitForPagination)
{
	ibQueryRenderer renderer(SqliteDialect());
	const ibRenderedQuery out = renderer.Render(SampleListQuery());

	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference17 WHERE (Code_S = ?) ORDER BY Code_S ASC LIMIT 50");
}

TEST(QueryRenderer, Postgres_UsesDollarPlaceholders)
{
	ibQueryRenderer renderer(PgDialect());
	const ibRenderedQuery out = renderer.Render(SampleListQuery());

	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference17 WHERE (Code_S = $1) ORDER BY Code_S ASC LIMIT 50");
}

// Same IR, same bind plan regardless of dialect — only the SQL spelling differs.
TEST(QueryRenderer, BindPlanIsDialectIndependent)
{
	const ibRenderedQuery fb = ibQueryRenderer(FbDialect()).Render(SampleListQuery());
	const ibRenderedQuery pg = ibQueryRenderer(PgDialect()).Render(SampleListQuery());

	ASSERT_EQ(fb.m_params.size(), pg.m_params.size());
	ASSERT_EQ(fb.m_params.size(), 1u);
	EXPECT_EQ(fb.m_params[0].m_value.GetString().ToStdString(),
	          pg.m_params[0].m_value.GetString().ToStdString());
}

// Regression guard: the bind plan MUST follow SQL-text (placeholder) order.
// RenderExpr appends params as a side effect, and operator+ does NOT sequence
// its operands (MSVC evaluates function arguments right-to-left). Relying on
// `"(" + RenderExpr(lhs) + ... + RenderExpr(rhs) + ")"` once reversed the plan
// per BinOp — so a keyset anchor crossed its bindings (e.g. a guid bound into a
// DATE column → Firebird type-mismatch crash). A nested OR-of-AND keyset
// predicate is the exact shape that exposes it; the earlier tests only checked
// SQL text (always correct) and missed the plan order.
TEST(QueryRenderer, BindPlanFollowsPlaceholderOrder)
{
	// (col_a > :0) OR ((col_a = :0) AND (col_b >= :1))
	ibQueryExprPtr pred =
		ibBinOp(ibQueryBinOp::Or,
			ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("col_a")), ibParam(0)),
			ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("col_a")), ibParam(0)),
				ibBinOp(ibQueryBinOp::Ge, ibCol(wxT("col_b")), ibParam(1))));

	ibQueryIR ir(ibFilter(ibScan(wxT("T")), pred));
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).Render(ir);

	EXPECT_EQ(Sql(out),
		"SELECT * FROM T WHERE ((col_a > ?) OR ((col_a = ?) AND (col_b >= ?)))");

	// Three placeholders, left-to-right; external indices MUST match text order.
	ASSERT_EQ(out.m_params.size(), 3u);
	ASSERT_TRUE(out.m_params[0].m_external);
	ASSERT_TRUE(out.m_params[1].m_external);
	ASSERT_TRUE(out.m_params[2].m_external);
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);   // col_a >  -> Param(0)
	EXPECT_EQ(out.m_params[1].m_externalIndex, 0);   // col_a =  -> Param(0)
	EXPECT_EQ(out.m_params[2].m_externalIndex, 1);   // col_b >= -> Param(1)
}

// Offset on a LIMIT/OFFSET dialect.
TEST(QueryRenderer, Sqlite_LimitWithOffset)
{
	ibQueryIR ir(ibLimit(ibScan(wxT("Document42")), 20, 100));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ir);
	EXPECT_EQ(Sql(out), "SELECT * FROM Document42 LIMIT 20 OFFSET 100");
}

// Firebird FIRST + SKIP.
TEST(QueryRenderer, Firebird_FirstWithSkip)
{
	ibQueryIR ir(ibLimit(ibScan(wxT("Document42")), 20, 100));
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).Render(ir);
	EXPECT_EQ(Sql(out), "SELECT FIRST 20 SKIP 100 * FROM Document42");
}

// Aggregate — a register-balance shape: dimension + SUM(resource) AS alias,
// filtered, grouped. SUM(x) is an ibFunc projection item; the group key is a column.
TEST(QueryRenderer, Aggregate_BalanceGroupBy)
{
	std::vector<ibQueryProjItem> proj = {
		{ ibCol(wxT("recorder")), wxEmptyString },
		{ ibFunc(wxT("SUM"), { ibCol(wxT("qty_N")) }), wxT("balance") },
	};
	ibQueryRelPtr agg = ibAggregate(
		ibFilter(ibScan(wxT("Register7")),
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("active_B")), ibConst(ibValue(true)))),
		proj, { ibCol(wxT("recorder")) });

	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(agg));
	EXPECT_EQ(Sql(out),
		"SELECT recorder, SUM(qty_N) AS balance FROM Register7 WHERE (active_B = ?) GROUP BY recorder");
	ASSERT_EQ(out.m_params.size(), 1u);   // the active flag, bound
}

// HAVING is post-aggregation: its param follows the WHERE params in the bind plan.
TEST(QueryRenderer, Aggregate_HavingParamOrder)
{
	std::vector<ibQueryProjItem> proj = {
		{ ibCol(wxT("recorder")), wxEmptyString },
		{ ibFunc(wxT("SUM"), { ibCol(wxT("qty_N")) }), wxT("balance") },
	};
	ibQueryRelPtr agg = ibAggregate(
		ibFilter(ibScan(wxT("Register7")),
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("active_B")), ibParam(0))),
		proj, { ibCol(wxT("recorder")) },
		ibBinOp(ibQueryBinOp::Gt, ibFunc(wxT("SUM"), { ibCol(wxT("qty_N")) }), ibParam(1)));

	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(ibQueryIR(agg));
	EXPECT_EQ(Sql(out),
		"SELECT recorder, SUM(qty_N) AS balance FROM Register7 WHERE (active_B = $1) "
		"GROUP BY recorder HAVING (SUM(qty_N) > $2)");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);   // WHERE active -> Param(0), placeholder $1
	EXPECT_EQ(out.m_params[1].m_externalIndex, 1);   // HAVING SUM > -> Param(1), placeholder $2
}

// --- expression completeness: IN / IS NULL / NOT / BETWEEN / CASE -----------

TEST(QueryRenderer, In_ListBindsEachValue)
{
	ibQueryRelPtr rel = ibFilter(ibScan(wxT("Reference9")),
		ibIn(ibCol(wxT("code_S")),
			{ ibConst(ibValue(wxString(wxT("A")))), ibConst(ibValue(wxString(wxT("B")))), ibConst(ibValue(wxString(wxT("C")))) }));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out), "SELECT * FROM Reference9 WHERE (code_S IN (?, ?, ?))");
	ASSERT_EQ(out.m_params.size(), 3u);
}

TEST(QueryRenderer, In_NegatedWithDollarPlaceholders)
{
	ibQueryRelPtr rel = ibFilter(ibScan(wxT("Reference9")),
		ibIn(ibCol(wxT("code_S")), { ibParam(0), ibParam(1) }, /*negated*/ true));
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out), "SELECT * FROM Reference9 WHERE (code_S NOT IN ($1, $2))");
}

TEST(QueryRenderer, In_EmptyListIsConstantFalse)
{
	ibQueryRelPtr rel = ibFilter(ibScan(wxT("Reference9")), ibIn(ibCol(wxT("code_S")), {}));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out), "SELECT * FROM Reference9 WHERE (1 = 0)");
	ASSERT_EQ(out.m_params.size(), 0u);
}

TEST(QueryRenderer, IsNull_AndIsNotNull)
{
	ibQueryRelPtr a = ibFilter(ibScan(wxT("Reference9")), ibIsNull(ibCol(wxT("parent_RRRef"))));
	EXPECT_EQ(Sql(ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(a))),
		"SELECT * FROM Reference9 WHERE (parent_RRRef IS NULL)");
	ibQueryRelPtr b = ibFilter(ibScan(wxT("Reference9")), ibIsNull(ibCol(wxT("parent_RRRef")), /*negated*/ true));
	EXPECT_EQ(Sql(ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(b))),
		"SELECT * FROM Reference9 WHERE (parent_RRRef IS NOT NULL)");
}

TEST(QueryRenderer, Not_WrapsPredicate)
{
	ibQueryRelPtr rel = ibFilter(ibScan(wxT("Reference9")),
		ibNot(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("mark_B")), ibConst(ibValue(true)))));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out), "SELECT * FROM Reference9 WHERE (NOT (mark_B = ?))");
	ASSERT_EQ(out.m_params.size(), 1u);
}

TEST(QueryRenderer, Between_DesugarsToRangeKeepingParamOrder)
{
	ibQueryRelPtr rel = ibFilter(ibScan(wxT("Document42")),
		ibBetween(ibCol(wxT("date_D")), ibParam(0), ibParam(1)));
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out), "SELECT * FROM Document42 WHERE ((date_D >= $1) AND (date_D <= $2))");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);
	EXPECT_EQ(out.m_params[1].m_externalIndex, 1);
}

// Searched CASE in a projection — the in-DB enum ordering (replaces the RAM
// parent-position sort). Bind plan follows WHEN/THEN/ELSE text order.
TEST(QueryRenderer, Case_EnumOrderingProjection)
{
	std::vector<ibQueryProjItem> proj = {
		{ ibCol(wxT("guidName")), wxEmptyString },
		{ ibCase({
			{ ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("guidName")), ibParam(0)), ibConst(ibValue(0.0)) },
			{ ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("guidName")), ibParam(1)), ibConst(ibValue(1.0)) },
		}, ibConst(ibValue(99.0))), wxT("pos") },
	};
	ibQueryRelPtr rel = ibProject(ibScan(wxT("Enum9")), proj);
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out),
		"SELECT guidName, (CASE WHEN (guidName = ?) THEN ? WHEN (guidName = ?) THEN ? ELSE ? END) AS pos FROM Enum9");
	ASSERT_EQ(out.m_params.size(), 5u);
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);   // WHEN #1 condition
	EXPECT_EQ(out.m_params[2].m_externalIndex, 1);   // WHEN #2 condition
}

// --- subquery-as-source (derived table) ------------------------------------

// The FROM subquery's binds land BEFORE the outer WHERE's — proving the param plan
// follows SQL text position across nesting (the slice / register-compute requirement).
TEST(QueryRenderer, Subquery_AsSoleFromSourceKeepsBindOrder)
{
	ibQueryRelPtr inner = ibFilter(ibScan(wxT("Reference9")),
		ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("code_S")), ibParam(0)));
	ibQueryRelPtr rel = ibFilter(ibSubquery(inner, wxT("sub")),
		ibBinOp(ibQueryBinOp::Like, ibCol(wxT("name_S")), ibParam(1)));
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out),
		"SELECT * FROM (SELECT * FROM Reference9 WHERE (code_S > $1)) AS sub WHERE (name_S LIKE $2)");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);   // inner subquery param first ($1)
	EXPECT_EQ(out.m_params[1].m_externalIndex, 1);   // outer WHERE param second ($2)
}

// The slice shape: an aggregate subquery joined back to the base table. This is what
// lets the register slice's "MAX(period) GROUP BY dims" inner select move off raw SQL.
TEST(QueryRenderer, Subquery_AggregateJoinedToTable)
{
	ibQueryRelPtr inner = ibAggregate(
		ibScan(wxT("Reg7")),
		{ { ibCol(wxT("recorder")), wxEmptyString },
		  { ibFunc(wxT("MAX"), { ibCol(wxT("period_D")) }), wxEmptyString } },
		{ ibCol(wxT("recorder")) });
	ibQueryRelPtr rel = ibFilter(
		ibJoin(ibSubquery(inner, wxT("T1")), ibScan(wxT("Reg7")),
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("T1"), wxT("recorder")), ibCol(wxT("Reg7"), wxT("recorder")))),
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("active_B")), ibParam(0)));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel));
	EXPECT_EQ(Sql(out),
		"SELECT * FROM (SELECT recorder, MAX(period_D) FROM Reg7 GROUP BY recorder) AS T1 "
		"INNER JOIN Reg7 ON (T1.recorder = Reg7.recorder) WHERE (active_B = ?)");
	ASSERT_EQ(out.m_params.size(), 1u);
}

// --- Distinct / Union ------------------------------------------------------

TEST(QueryRenderer, Distinct_RendersAfterSelect)
{
	ibQueryRelPtr rel = ibDistinct(ibProject(ibScan(wxT("Reference9")), { { ibCol(wxT("code_S")), wxEmptyString } }));
	EXPECT_EQ(Sql(ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel))),
		"SELECT DISTINCT code_S FROM Reference9");
}

TEST(QueryRenderer, Distinct_AfterFirstInFirebird)
{
	ibQueryRelPtr rel = ibLimit(ibDistinct(ibProject(ibScan(wxT("Reference9")), { { ibCol(wxT("code_S")), wxEmptyString } })), 10);
	EXPECT_EQ(Sql(ibQueryRenderer(FbDialect()).Render(ibQueryIR(rel))),
		"SELECT FIRST 10 DISTINCT code_S FROM Reference9");
}

TEST(QueryRenderer, Union_SplicesMembersKeepingBindOrder)
{
	ibQueryRelPtr left  = ibFilter(ibScan(wxT("Reference9")), ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("code_S")), ibParam(0)));
	ibQueryRelPtr right = ibFilter(ibScan(wxT("Reference9")), ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("code_S")), ibParam(1)));
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(ibQueryIR(ibUnion(left, right)));
	EXPECT_EQ(Sql(out),
		"SELECT * FROM Reference9 WHERE (code_S = $1) UNION SELECT * FROM Reference9 WHERE (code_S = $2)");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_EQ(out.m_params[0].m_externalIndex, 0);
	EXPECT_EQ(out.m_params[1].m_externalIndex, 1);
}

TEST(QueryRenderer, UnionAll_OrderedViaSubqueryWrap)
{
	ibQueryRelPtr u   = ibUnionAll(ibScan(wxT("A1")), ibScan(wxT("B2")));
	ibQueryRelPtr rel = ibSort(ibSubquery(u, wxT("u")), { { ibCol(wxT("code_S")), ibQuerySortDir::Asc } });
	EXPECT_EQ(Sql(ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(rel))),
		"SELECT * FROM (SELECT * FROM A1 UNION ALL SELECT * FROM B2) AS u ORDER BY code_S ASC");
}

// ===========================================================================
// DDL — one CreateTable IR, three dialects. The type-map closes the column-type
// forks (Guid: VARCHAR(36)/TEXT/UUID, boolean: SMALLINT/INTEGER/BOOLEAN,
// number: DECIMAL/NUMERIC). Firebird uses VARCHAR(36) (not CHAR(36)) so a guid
// reads back without a charset-padded CHAR tail — see firebirdDatabaseLayer.cpp.
// ===========================================================================

namespace {

ibDdlStatement SampleCreateTable()
{
	return ibCreateTable(wxT("Reference17"), {
		ibDdlColumn{ wxT("Ref"),      ibTypeGuid(),         false, true  },
		ibDdlColumn{ wxT("Code_S"),   ibTypeString(50),     true,  false },
		ibDdlColumn{ wxT("Price_N"),  ibTypeNumber(18, 2),  false, false },
		ibDdlColumn{ wxT("Active_B"), ibTypeBoolean(),      false, false },
	});
}

} // namespace

TEST(QueryDdlRenderer, Firebird_CreateTable)
{
	ibQueryRenderer r(FbDialect());
	EXPECT_EQ(r.RenderDDL(SampleCreateTable()).ToStdString(),
		"CREATE TABLE Reference17 (Ref VARCHAR(36) PRIMARY KEY, Code_S VARCHAR(50) NOT NULL, "
		"Price_N NUMERIC(18,2), Active_B SMALLINT)");
}

TEST(QueryDdlRenderer, Sqlite_CreateTable)
{
	ibQueryRenderer r(SqliteDialect());
	EXPECT_EQ(r.RenderDDL(SampleCreateTable()).ToStdString(),
		"CREATE TABLE Reference17 (Ref TEXT PRIMARY KEY, Code_S VARCHAR(50) NOT NULL, "
		"Price_N DECIMAL(18,2), Active_B INTEGER)");
}

TEST(QueryDdlRenderer, Postgres_CreateTable)
{
	ibQueryRenderer r(PgDialect());
	EXPECT_EQ(r.RenderDDL(SampleCreateTable()).ToStdString(),
		"CREATE TABLE Reference17 (Ref UUID PRIMARY KEY, Code_S VARCHAR(50) NOT NULL, "
		"Price_N NUMERIC(18,2), Active_B BOOLEAN)");
}

TEST(QueryDdlRenderer, DropTableIfExists)
{
	ibQueryRenderer r(SqliteDialect());
	EXPECT_EQ(r.RenderDDL(ibDropTable(wxT("Document42"), /*ifExists=*/true)).ToStdString(),
		"DROP TABLE IF EXISTS Document42");
}

TEST(QueryDdlRenderer, Postgres_BlobIsBytea)
{
	ibQueryRenderer r(PgDialect());
	ibDdlStatement ddl = ibCreateTable(wxT("T"), { ibDdlColumn{ wxT("Data"), ibTypeBlob(), false, false } });
	EXPECT_EQ(r.RenderDDL(ddl).ToStdString(), "CREATE TABLE T (Data BYTEA)");
}

TEST(QueryDdlRenderer, CreateIndex)
{
	ibDdlStatement s = ibCreateIndex(wxT("Reference9"), wxT("Reference9_INDEX"), { wxT("code_S"), wxT("name_S") });
	EXPECT_EQ(ibQueryRenderer(SqliteDialect()).RenderDDL(s).ToStdString(),
		"CREATE INDEX Reference9_INDEX ON Reference9 (code_S, name_S)");
}

TEST(QueryDdlRenderer, CreateUniqueIndex)
{
	ibDdlStatement s = ibCreateIndex(wxT("Reg7"), wxT("Reg7_UX"), { wxT("recorder"), wxT("line_N") }, /*unique=*/true);
	EXPECT_EQ(ibQueryRenderer(FbDialect()).RenderDDL(s).ToStdString(),
		"CREATE UNIQUE INDEX Reg7_UX ON Reg7 (recorder, line_N)");
}

TEST(QueryDdlRenderer, DropColumn)
{
	EXPECT_EQ(ibQueryRenderer(PgDialect()).RenderDDL(ibDropColumn(wxT("Reference9"), wxT("oldAttr_S"))).ToStdString(),
		"ALTER TABLE Reference9 DROP COLUMN oldAttr_S");
}

TEST(QueryDdlRenderer, DropIndex_StandaloneVsMysqlOnTable)
{
	EXPECT_EQ(ibQueryRenderer(SqliteDialect()).RenderDDL(ibDropIndex(wxT("Reference9_INDEX"), wxT("Reference9"))).ToStdString(),
		"DROP INDEX Reference9_INDEX");
#ifdef OES_USE_MYSQL
	EXPECT_EQ(ibQueryRenderer(MysqlDialect()).RenderDDL(ibDropIndex(wxT("Reference9_INDEX"), wxT("Reference9"))).ToStdString(),
		"DROP INDEX Reference9_INDEX ON Reference9");
#endif
}

// ALTER COLUMN (type change): PG/FB use ALTER COLUMN ... TYPE, MySQL MODIFY COLUMN;
// SQLite cannot do it in place — its empty template makes the renderer THROW.
TEST(QueryDdlRenderer, AlterColumn_TemplateShape)
{
	ibDdlColumn c{ wxT("flag_B"), ibTypeBoolean(), false, false };
	EXPECT_EQ(ibQueryRenderer(PgDialect()).RenderDDL(ibAlterColumn(wxT("Reg7"), c)).ToStdString(),
		"ALTER TABLE Reg7 ALTER COLUMN flag_B TYPE BOOLEAN");
#ifdef OES_USE_MYSQL
	EXPECT_EQ(ibQueryRenderer(MysqlDialect()).RenderDDL(ibAlterColumn(wxT("Reg7"), c)).ToStdString(),
		"ALTER TABLE Reg7 MODIFY COLUMN flag_B TINYINT");
#endif
}

TEST(QueryDdlRenderer, AlterColumn_SqliteThrows)
{
	ibDdlColumn c{ wxT("flag_B"), ibTypeBoolean(), false, false };
	EXPECT_THROW(ibQueryRenderer(SqliteDialect()).RenderDDL(ibAlterColumn(wxT("Reg7"), c)),
		ibBackendQueryException);
}

// ===========================================================================
// DML — Insert / Update / Delete. Const + Param both become bound placeholders;
// the bind plan mirrors the placeholder order.
// ===========================================================================

TEST(QueryDmlRenderer, Insert_Postgres_DollarPlaceholders)
{
	ibDmlStatement ins = ibInsert(wxT("Reference17"), {
		{ wxT("Code_S"),  ibConst(ibValue(wxString(wxT("APPLE-01")))) },
		{ wxT("Price_N"), ibParam(0) },
	});
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).RenderDML(ins);

	EXPECT_EQ(Sql(out), "INSERT INTO Reference17 (Code_S, Price_N) VALUES ($1, $2)");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_FALSE(out.m_params[0].m_external);
	EXPECT_TRUE(out.m_params[1].m_external);
	EXPECT_EQ(out.m_params[1].m_externalIndex, 0);
}

TEST(QueryDmlRenderer, Update_Sqlite_SetThenWhereOrder)
{
	ibDmlStatement upd = ibUpdate(wxT("Document42"),
		{ { wxT("Posted_B"), ibConst(ibValue(true)) } },
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("Ref")), ibParam(0)));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).RenderDML(upd);

	EXPECT_EQ(Sql(out), "UPDATE Document42 SET Posted_B = ? WHERE (Ref = ?)");
	ASSERT_EQ(out.m_params.size(), 2u);   // SET value first, then WHERE value
}

TEST(QueryDmlRenderer, Delete_Firebird)
{
	ibDmlStatement del = ibDelete(wxT("Document42"),
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("Ref")), ibParam(0)));
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).RenderDML(del);

	EXPECT_EQ(Sql(out), "DELETE FROM Document42 WHERE (Ref = ?)");
}

TEST(QueryDmlRenderer, DeleteAll_NoWhere)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).RenderDML(ibDelete(wxT("Tmp")));
	EXPECT_EQ(Sql(out), "DELETE FROM Tmp");
	EXPECT_TRUE(out.m_params.empty());
}

// ===========================================================================
// JOIN — the FROM source is a tree, rendered recursively. Qualified columns
// disambiguate. (Phase 2 node; needed by L3 reference auto-joins / registers.)
// ===========================================================================

namespace {

// SELECT D.Number_S, R.Code_S FROM Document42 INNER JOIN Reference17
//   ON (Document42.Customer_R = Reference17.Ref) WHERE (Reference17.Code_S = ?)
ibQueryIR JoinQuery()
{
	ibQueryRelPtr rel =
		ibProject(
			ibFilter(
				ibJoin(ibScan(wxT("Document42")),
				       ibScan(wxT("Reference17")),
				       ibBinOp(ibQueryBinOp::Eq,
				               ibCol(wxT("Document42"), wxT("Customer_R")),
				               ibCol(wxT("Reference17"), wxT("Ref")))),
				ibBinOp(ibQueryBinOp::Eq,
				        ibCol(wxT("Reference17"), wxT("Code_S")),
				        ibParam(0))),
			{ ibQueryProjItem{ ibCol(wxT("Document42"),  wxT("Number_S")), wxString() },
			  ibQueryProjItem{ ibCol(wxT("Reference17"), wxT("Code_S")),   wxString() } });
	return ibQueryIR(rel);
}

} // namespace

TEST(QueryJoinRenderer, InnerJoin_Sqlite)
{
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(JoinQuery());
	EXPECT_EQ(Sql(out),
		"SELECT Document42.Number_S, Reference17.Code_S "
		"FROM Document42 INNER JOIN Reference17 "
		"ON (Document42.Customer_R = Reference17.Ref) "
		"WHERE (Reference17.Code_S = ?)");
}

TEST(QueryJoinRenderer, LeftJoin_Firebird)
{
	ibQueryIR ir(
		ibJoin(ibScan(wxT("A")), ibScan(wxT("B")),
		       ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("A"), wxT("k")), ibCol(wxT("B"), wxT("k"))),
		       ibQueryJoinType::Left));
	const ibRenderedQuery out = ibQueryRenderer(FbDialect()).Render(ir);
	EXPECT_EQ(Sql(out), "SELECT * FROM A LEFT JOIN B ON (A.k = B.k)");
}

// ON-predicate bind params come BEFORE the WHERE's — matching their position in
// the finished SQL (FROM ... ON, then WHERE).
TEST(QueryJoinRenderer, OnParamsPrecedeWhereParams_Postgres)
{
	ibQueryIR ir(
		ibFilter(
			ibJoin(ibScan(wxT("A")), ibScan(wxT("B")),
			       ibBinOp(ibQueryBinOp::And,
			               ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("A"), wxT("k")), ibCol(wxT("B"), wxT("k"))),
			               ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("B"), wxT("active")), ibConst(ibValue(true))))),
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("A"), wxT("y")), ibParam(0))));
	const ibRenderedQuery out = ibQueryRenderer(PgDialect()).Render(ir);

	EXPECT_EQ(Sql(out),
		"SELECT * FROM A INNER JOIN B "
		"ON ((A.k = B.k) AND (B.active = $1)) "
		"WHERE (A.y = $2)");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_FALSE(out.m_params[0].m_external);   // ON's Const(true) is bound first
	EXPECT_TRUE(out.m_params[1].m_external);     // WHERE's Param second
	EXPECT_EQ(out.m_params[1].m_externalIndex, 0);
}

// Opaque blob constant — the metadata layer encodes a reference / binary key,
// L2 binds the bytes via SetParamBlob without interpreting them.
TEST(QueryRenderer, BlobConst_BindsAsOpaqueBytes)
{
	const unsigned char ref[4] = { 0x01, 0x02, 0x03, 0x04 };
	ibQueryIR ir(ibFilter(ibScan(wxT("T")),
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("ref_col")), ibConstBlob(ref, sizeof(ref)))));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ir);

	EXPECT_EQ(Sql(out), "SELECT * FROM T WHERE (ref_col = ?)");
	ASSERT_EQ(out.m_params.size(), 1u);
	EXPECT_FALSE(out.m_params[0].m_external);
	EXPECT_TRUE(out.m_params[0].m_isBlob);
	EXPECT_EQ(out.m_params[0].m_blob.GetDataLen(), 4u);
}
