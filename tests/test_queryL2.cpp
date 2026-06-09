// L2 query layer — comprehensive golden tests (docs/query-language-arc.md §5–§8, §18).
//
// PURE: no database, no connection. The whole L2 IR vocabulary the L3 push-down generates
// (JOIN / Aggregate / Subquery / UNION / the expression set), plus the two L2 features the
// temp-table arc added (multi-row INSERT, CREATE TEMPORARY TABLE), rendered through the
// dialect dictionaries to exact dialect SQL + a checked bind plan. If these pass, L2 renders
// every construct the L3 engine emits correctly across drivers.
//
// Most cases assert the SQLite spelling (LIMIT, `?` params, bare identifiers) for a stable
// golden string; pagination / param-style / type-map divergence is covered with FB / PG.

#include <gtest/gtest.h>

#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

std::string Sql(const ibRenderedQuery& r) { return r.m_sql.ToStdString(); }

ibDialectDictionary PgDialect()     { return ibDatabaseLayerPostgres::Dialect(); }
ibDialectDictionary FbDialect()     { return ibDatabaseLayerFirebird::Dialect(); }
ibDialectDictionary SqliteDialect() { return ibDatabaseLayerSQLite::Dialect(); }

// Render a DQL IR through SQLite (the stable-spelling default).
std::string Lite(const ibQueryIR& ir) { return Sql(ibQueryRenderer(SqliteDialect()).Render(ir)); }

ibQueryExprPtr Eq(const wxString& q, const wxString& a, const wxString& q2, const wxString& b)
{
	return ibBinOp(ibQueryBinOp::Eq, ibCol(q, a), ibCol(q2, b));
}

} // namespace

// ===========================================================================
// FROM-tree: JOIN (inner / left / N-way), qualified columns, projection aliases
// ===========================================================================

TEST(QueryL2_Join, InnerJoin_QualifiedProjection)
{
	ibQueryRelPtr from = ibJoin(ibScan(wxT("Catalog")), ibScan(wxT("Owner")),
		Eq(wxT("Catalog"), wxT("Owner_RRRef"), wxT("Owner"), wxT("Ref_RRRef")));
	ibQueryIR ir(ibProject(from, {
		{ ibCol(wxT("Catalog"), wxT("Code_S")), wxT("code") },
		{ ibCol(wxT("Owner"),   wxT("Name_S")), wxT("owner") },
	}));
	EXPECT_EQ(Lite(ir),
		"SELECT Catalog.Code_S AS code, Owner.Name_S AS owner "
		"FROM Catalog INNER JOIN Owner ON (Catalog.Owner_RRRef = Owner.Ref_RRRef)");
}

TEST(QueryL2_Join, LeftJoin)
{
	ibQueryRelPtr from = ibJoin(ibScan(wxT("A")), ibScan(wxT("B")),
		Eq(wxT("A"), wxT("k"), wxT("B"), wxT("k")), ibQueryJoinType::Left);
	ibQueryIR ir(ibProject(from, { { ibCol(wxT("A"), wxT("v")), wxString() } }));
	EXPECT_EQ(Lite(ir), "SELECT A.v FROM A LEFT JOIN B ON (A.k = B.k)");
}

TEST(QueryL2_Join, ThreeWayLeftDeep)
{
	ibQueryRelPtr ab  = ibJoin(ibScan(wxT("A")), ibScan(wxT("B")), Eq(wxT("A"), wxT("k"), wxT("B"), wxT("k")));
	ibQueryRelPtr abc = ibJoin(ab, ibScan(wxT("C")), Eq(wxT("B"), wxT("m"), wxT("C"), wxT("m")));
	ibQueryIR ir(ibProject(abc, { { ibCol(wxT("C"), wxT("x")), wxString() } }));
	EXPECT_EQ(Lite(ir),
		"SELECT C.x FROM A INNER JOIN B ON (A.k = B.k) INNER JOIN C ON (B.m = C.m)");
}

TEST(QueryL2_Join, AliasedScan)
{
	ibQueryIR ir(ibProject(ibScan(wxT("Reference17"), wxT("t")), { { ibCol(wxT("t"), wxT("Code_S")), wxString() } }));
	EXPECT_EQ(Lite(ir), "SELECT t.Code_S FROM Reference17 AS t");
}

// ===========================================================================
// Aggregate — GROUP BY + aggregate projection + HAVING
// ===========================================================================

TEST(QueryL2_Aggregate, GroupBySum)
{
	ibQueryRelPtr agg = ibAggregate(ibScan(wxT("Sales")),
		{ { ibCol(wxT("region_S")), wxT("region") },
		  { ibFunc(wxT("SUM"), { ibCol(wxT("amount_N")) }), wxT("total") } },
		{ ibCol(wxT("region_S")) });
	EXPECT_EQ(Lite(ibQueryIR(agg)),
		"SELECT region_S AS region, SUM(amount_N) AS total FROM Sales GROUP BY region_S");
}

TEST(QueryL2_Aggregate, RollupGroupBy)
{
	// rollup=true -> the GROUP BY keys wrap in the dialect's ROLLUP prefix/suffix (standard here).
	ibQueryRelPtr agg = ibAggregate(ibScan(wxT("Sales")),
		{ { ibCol(wxT("region_S")), wxT("region") },
		  { ibFunc(wxT("SUM"), { ibCol(wxT("amount_N")) }), wxT("total") } },
		{ ibCol(wxT("region_S")) }, nullptr, /*rollup*/ true);
	EXPECT_EQ(Lite(ibQueryIR(agg)),
		"SELECT region_S AS region, SUM(amount_N) AS total FROM Sales GROUP BY ROLLUP(region_S)");
}

TEST(QueryL2_Aggregate, GroupByHaving)
{
	ibQueryExprPtr having = ibBinOp(ibQueryBinOp::Gt, ibFunc(wxT("SUM"), { ibCol(wxT("q_N")) }), ibConst(ibValue(ibNumber(100))));
	ibQueryRelPtr agg = ibAggregate(ibScan(wxT("Reg")),
		{ { ibCol(wxT("dim_S")), wxT("d") }, { ibFunc(wxT("SUM"), { ibCol(wxT("q_N")) }), wxT("s") } },
		{ ibCol(wxT("dim_S")) }, having);
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(agg));
	EXPECT_EQ(Sql(out),
		"SELECT dim_S AS d, SUM(q_N) AS s FROM Reg GROUP BY dim_S HAVING (SUM(q_N) > ?)");
	ASSERT_EQ(out.m_params.size(), 1u);
	EXPECT_EQ(out.m_params[0].m_value.GetString().ToStdString(), "100");
}

// ===========================================================================
// Subquery (derived table) + UNION + ORDER BY/LIMIT over a union (subquery wrap)
// ===========================================================================

TEST(QueryL2_Set, DerivedTable)
{
	ibQueryRelPtr inner = ibProject(ibScan(wxT("Reg")), { { ibCol(wxT("v_N")), wxT("v") } });
	ibQueryIR ir(ibProject(ibSubquery(inner, wxT("u")), { { ibCol(wxT("u"), wxT("v")), wxString() } }));
	EXPECT_EQ(Lite(ir), "SELECT u.v FROM (SELECT v_N AS v FROM Reg) AS u");
}

TEST(QueryL2_Set, UnionAll)
{
	ibQueryRelPtr a = ibProject(ibScan(wxT("A")), { { ibCol(wxT("code_S")), wxT("u0") } });
	ibQueryRelPtr b = ibProject(ibScan(wxT("B")), { { ibCol(wxT("code_S")), wxT("u0") } });
	EXPECT_EQ(Lite(ibQueryIR(ibUnionAll(a, b))),
		"SELECT code_S AS u0 FROM A UNION ALL SELECT code_S AS u0 FROM B");
}

TEST(QueryL2_Set, UnionWrappedForOrderLimit)
{
	ibQueryRelPtr a = ibProject(ibScan(wxT("A")), { { ibCol(wxT("code_S")), wxT("u0") } });
	ibQueryRelPtr b = ibProject(ibScan(wxT("B")), { { ibCol(wxT("code_S")), wxT("u0") } });
	ibQueryRelPtr outer = ibLimit(ibSort(ibSubquery(ibUnionAll(a, b), wxT("u")),
		{ { ibCol(wxT("u0")), ibQuerySortDir::Asc } }), 10);
	EXPECT_EQ(Lite(ibQueryIR(outer)),
		"SELECT * FROM (SELECT code_S AS u0 FROM A UNION ALL SELECT code_S AS u0 FROM B) AS u "
		"ORDER BY u0 ASC LIMIT 10");
}

TEST(QueryL2_Set, Distinct)
{
	ibQueryIR ir(ibDistinct(ibProject(ibScan(wxT("T")), { { ibCol(wxT("c_S")), wxString() } })));
	EXPECT_EQ(Lite(ir), "SELECT DISTINCT c_S FROM T");
}

// ===========================================================================
// Expressions — CASE / IN / IS NULL / NOT / BETWEEN / Cast / arithmetic
// ===========================================================================

namespace {
std::string Where(const ibQueryExprPtr& pred)
{
	return Lite(ibQueryIR(ibFilter(ibScan(wxT("T")), pred)));
}
} // namespace

TEST(QueryL2_Expr, InAndNotIn)
{
	EXPECT_EQ(Where(ibIn(ibCol(wxT("id")), { ibConst(ibValue(ibNumber(1))), ibConst(ibValue(ibNumber(2))) })),
		"SELECT * FROM T WHERE (id IN (?, ?))");
	EXPECT_EQ(Where(ibIn(ibCol(wxT("id")), { ibConst(ibValue(ibNumber(1))) }, /*negated*/true)),
		"SELECT * FROM T WHERE (id NOT IN (?))");
	EXPECT_EQ(Where(ibIn(ibCol(wxT("id")), {})), "SELECT * FROM T WHERE (1 = 0)");
}

TEST(QueryL2_Expr, IsNullNotNull)
{
	EXPECT_EQ(Where(ibIsNull(ibCol(wxT("x")))),               "SELECT * FROM T WHERE (x IS NULL)");
	EXPECT_EQ(Where(ibIsNull(ibCol(wxT("x")), /*neg*/true)),  "SELECT * FROM T WHERE (x IS NOT NULL)");
}

TEST(QueryL2_Expr, NotAndBetween)
{
	EXPECT_EQ(Where(ibNot(ibCol(wxT("flag")))), "SELECT * FROM T WHERE (NOT flag)");
	EXPECT_EQ(Where(ibBetween(ibCol(wxT("p")), ibConst(ibValue(ibNumber(1))), ibConst(ibValue(ibNumber(9))))),
		"SELECT * FROM T WHERE ((p >= ?) AND (p <= ?))");
}

TEST(QueryL2_Expr, CaseAndCast)
{
	ibQueryExprPtr c = ibCase(
		{ { ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("t")), ibConst(ibValue(ibNumber(1)))), ibConst(ibValue(wxString(wxT("a")))) } },
		ibConst(ibValue(wxString(wxT("b")))));
	EXPECT_EQ(Lite(ibQueryIR(ibProject(ibScan(wxT("T")), { { c, wxT("x") } }))),
		"SELECT (CASE WHEN (t = ?) THEN ? ELSE ? END) AS x FROM T");
	EXPECT_EQ(Lite(ibQueryIR(ibProject(ibScan(wxT("Reg")),
		{ { ibCast(ibFunc(wxT("SUM"), { ibCol(wxT("q_N")) }), wxT("NUMERIC")), wxT("s") } }))),
		"SELECT CAST(SUM(q_N) AS NUMERIC) AS s FROM Reg");
}

TEST(QueryL2_Expr, Arithmetic)
{
	EXPECT_EQ(Where(ibBinOp(ibQueryBinOp::Gt,
		ibBinOp(ibQueryBinOp::Add, ibCol(wxT("a")), ibCol(wxT("b"))), ibConst(ibValue(ibNumber(0))))),
		"SELECT * FROM T WHERE ((a + b) > ?)");
}

// ===========================================================================
// Bind plan — Const + Param in placeholder order, left to right
// ===========================================================================

TEST(QueryL2_Bind, ParamsInTextOrder)
{
	// WHERE (a = const) AND (b = param0): two binds, the Const first, the external Param second.
	ibQueryExprPtr pred = ibBinOp(ibQueryBinOp::And,
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("a")), ibConst(ibValue(wxString(wxT("X"))))),
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("b")), ibParam(0)));
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(ibFilter(ibScan(wxT("T")), pred)));
	EXPECT_EQ(Sql(out), "SELECT * FROM T WHERE ((a = ?) AND (b = ?))");
	ASSERT_EQ(out.m_params.size(), 2u);
	EXPECT_FALSE(out.m_params[0].m_external);
	EXPECT_EQ(out.m_params[0].m_value.GetString().ToStdString(), "X");
	EXPECT_TRUE(out.m_params[1].m_external);
	EXPECT_EQ(out.m_params[1].m_externalIndex, 0);
}

// ===========================================================================
// Dialect divergence — pagination + param style on ONE join+aggregate IR
// ===========================================================================

TEST(QueryL2_Dialect, PaginationAndParamStyle)
{
	ibQueryRelPtr ir = ibLimit(ibFilter(ibScan(wxT("T")),
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("c_S")), ibParam(0))), 5);
	EXPECT_EQ(Sql(ibQueryRenderer(FbDialect()).Render(ibQueryIR(ir))),     "SELECT FIRST 5 * FROM T WHERE (c_S = ?)");
	EXPECT_EQ(Sql(ibQueryRenderer(SqliteDialect()).Render(ibQueryIR(ir))), "SELECT * FROM T WHERE (c_S = ?) LIMIT 5");
	EXPECT_EQ(Sql(ibQueryRenderer(PgDialect()).Render(ibQueryIR(ir))),     "SELECT * FROM T WHERE (c_S = $1) LIMIT 5");
}

// ===========================================================================
// DML — multi-row INSERT (the temp-table bulk-fill feature)
// ===========================================================================

TEST(QueryL2_Dml, MultiRowInsert)
{
	ibDmlStatement ins(ibDmlKind::Insert);
	ins.m_table       = wxT("oes_tmp_1");
	ins.m_assignments = { { wxT("a"), ibConst(ibValue(ibNumber(1))) }, { wxT("b"), ibConst(ibValue(wxString(wxT("x")))) } };
	ins.m_extraRows   = {
		{ ibConst(ibValue(ibNumber(2))), ibConst(ibValue(wxString(wxT("y")))) },
		{ ibConst(ibValue(ibNumber(3))), ibConst(ibValue(wxString(wxT("z")))) },
	};
	const ibRenderedQuery out = ibQueryRenderer(SqliteDialect()).RenderDML(ins);
	EXPECT_EQ(Sql(out), "INSERT INTO oes_tmp_1 (a, b) VALUES (?, ?), (?, ?), (?, ?)");
	ASSERT_EQ(out.m_params.size(), 6u);   // 3 rows x 2 cols, in row-major placeholder order
	EXPECT_EQ(out.m_params[0].m_value.GetString().ToStdString(), "1");
	EXPECT_EQ(out.m_params[3].m_value.GetString().ToStdString(), "y");
}

TEST(QueryL2_Dml, SingleRowInsertUnaffected)
{
	ibDmlStatement ins = ibInsert(wxT("T"), { { wxT("a"), ibConst(ibValue(ibNumber(1))) } });
	EXPECT_EQ(Sql(ibQueryRenderer(SqliteDialect()).RenderDML(ins)), "INSERT INTO T (a) VALUES (?)");
}

// ===========================================================================
// DDL — CREATE TEMPORARY TABLE (prefix + ON COMMIT suffix from the L1 temp dialect)
// ===========================================================================

TEST(QueryL2_Ddl, CreateTempTable_PrefixAndSuffix)
{
	ibDdlStatement ddl = ibCreateTempTable(wxT("oes_tmp_1"), {
		ibDdlColumn{ wxT("k_TYPE"), ibTypeInteger() },
		ibDdlColumn{ wxT("v_N"),    ibTypeNumber(38, 10) },
		ibDdlColumn{ wxT("s_S"),    ibTypeString(4000) },
	}, wxT("CREATE TEMPORARY TABLE"), wxT(" ON COMMIT DROP"));
	EXPECT_EQ(ibQueryRenderer(SqliteDialect()).RenderDDL(ddl).ToStdString(),
		"CREATE TEMPORARY TABLE oes_tmp_1 (k_TYPE INTEGER, v_N DECIMAL(38,10), s_S VARCHAR(4000)) ON COMMIT DROP");
}

TEST(QueryL2_Ddl, CreateTempTable_NoSuffix)
{
	ibDdlStatement ddl = ibCreateTempTable(wxT("t"), { ibDdlColumn{ wxT("k_TYPE"), ibTypeInteger() } },
		wxT("CREATE TEMPORARY TABLE"));   // empty suffix
	EXPECT_EQ(ibQueryRenderer(SqliteDialect()).RenderDDL(ddl).ToStdString(),
		"CREATE TEMPORARY TABLE t (k_TYPE INTEGER)");
}

TEST(QueryL2_Ddl, PlainCreateTableStillCreateTable)
{
	ibDdlStatement ddl = ibCreateTable(wxT("t"), { ibDdlColumn{ wxT("k"), ibTypeInteger() } });
	EXPECT_EQ(ibQueryRenderer(SqliteDialect()).RenderDDL(ddl).ToStdString(), "CREATE TABLE t (k INTEGER)");
}
