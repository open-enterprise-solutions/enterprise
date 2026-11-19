// =============================================================================
// The base in memory (databaseLayer/databaseMemoryBase.h) against SQLite: one IR, one door, the same rows.
//
// Each base sits behind a pool and a holder of its own, and every statement goes through ibDatabaseQueryBuilder — the
// door the query engine uses — so SQLite is handed the rendered text and the memory base the IR itself
// (ibSqlFeatures::m_inMemory). A cell is read the way its kind says (a number by GetResultNumber, a date by
// GetResultDate, a key by its bytes), so SQLite's typing — a date kept as text, a sum as a double — compares by what it
// means. docs/private/query-one-road.md.
//
// What SQLite has no form for (ROLLUP) or answers otherwise on purpose (a division by zero) is checked against the
// memory base alone, with the rows written out.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/appHost.h"   // ibApplicationHost::GetQueryMemoryManager — whose pool the spaces come from
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseMemoryBase.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataSerialization.h"      // ibSerializeValue / ibDeserializeValue — a value, as its node
#include "backend/system/value/valueArray.h"   // an object, kept as its node
#include "core/serialize/dataBuilder.h"        // ibDataNode

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using Rows = std::vector<std::vector<std::string>>;

// --- the parts of an IR, short --------------------------------------------------------------------------------

ibQueryExprPtr Col(const char* name) { return ibCol(ibString::FromUTF8(name)); }
ibQueryExprPtr Col(const char* qualifier, const char* name) { return ibCol(ibString::FromUTF8(qualifier), ibString::FromUTF8(name)); }
ibQueryExprPtr Str(const char* text) { return ibConst(ibValue(ibString::FromUTF8(text))); }
ibQueryExprPtr Num(int n) { return ibConst(ibValue(ibNumber(n))); }
ibQueryExprPtr Dec(const char* text)
{
	ibNumber n;
	n.FromString(ibString::FromUTF8(text));
	return ibConst(ibValue(n));
}
ibQueryExprPtr Day(int y, int m, int d, int h = 0, int mi = 0, int s = 0)
{
	return ibConst(ibValue(y, m, d, static_cast<unsigned short>(h), static_cast<unsigned short>(mi), static_cast<unsigned short>(s)));
}
ibQueryExprPtr Bool(bool b) { return ibConst(ibValue(b)); }
ibQueryExprPtr Null() { return ibConst(ibValue()); }   // an empty value binds as NULL
// A reference's key: sixteen bytes, the last one `n`.
ibQueryExprPtr Key(int n)
{
	unsigned char bytes[16] = {};
	bytes[15] = static_cast<unsigned char>(n);
	return ibConstBlob(bytes, sizeof(bytes));
}
std::string KeyText(int n)
{
	char last[3];
	std::snprintf(last, sizeof(last), "%02x", n);
	return std::string(30, '0') + last;
}
ibQueryExprPtr Op(ibQueryBinOp op, ibQueryExprPtr a, ibQueryExprPtr b) { return ibBinOp(op, std::move(a), std::move(b)); }
ibQueryProjItem As(ibQueryExprPtr e, const char* alias = "") { return ibQueryProjItem{ std::move(e), ibString::FromUTF8(alias) }; }
ibQuerySortKey Asc(ibQueryExprPtr e) { return ibQuerySortKey{ std::move(e), ibQuerySortDir::Asc }; }
ibQuerySortKey Desc(ibQueryExprPtr e) { return ibQuerySortKey{ std::move(e), ibQuerySortDir::Desc }; }
ibDdlColumn Field(const char* name, const ibColumnType& type, bool primaryKey = false)
{
	ibDdlColumn c;
	c.m_name       = ibString::FromUTF8(name);
	c.m_type       = type;
	c.m_primaryKey = primaryKey;
	return c;
}
ibDmlAssign Set(const char* column, ibQueryExprPtr value) { return ibDmlAssign{ ibString::FromUTF8(column), std::move(value) }; }

// --- reading -----------------------------------------------------------------------------------------------------

// One cell as text, read as `kind` says: N number, S string, D date, B boolean, X bytes.
std::string Text(ibQueryResult& rs, int field, char kind)
{
	if (rs.IsResultNull(field))
		return "NULL";
	switch (kind) {
	case 'N': return rs.GetResultNumber(field).ToString().ToStdString();
	case 'D': return rs.GetResultDate(field).Format(ibT("%Y-%m-%d %H:%M:%S")).ToStdString();
	case 'B': return rs.GetResultBool(field) ? "true" : "false";
	case 'X': {
		ibMemoryBuffer bytes;
		rs.GetResultBlob(field, bytes);
		std::string hex;
		for (size_t i = 0; i < bytes.GetDataLen(); ++i) {
			char two[3];
			std::snprintf(two, sizeof(two), "%02x", static_cast<const unsigned char*>(bytes.GetData())[i]);
			hex += two;
		}
		return hex;
	}
	default:  return rs.GetResultString(field).ToStdString();
	}
}

Rows ReadAll(ibQueryResult rs, const std::string& kinds)
{
	Rows rows;
	while (rs.Next()) {
		std::vector<std::string> row;
		for (size_t i = 0; i < kinds.size(); ++i)
			row.push_back(Text(rs, static_cast<int>(i + 1), kinds[i]));
		rows.push_back(std::move(row));
	}
	return rows;
}

// One base behind a pool and a holder of its own. A cursor is read to its end before the next statement: the pool
// has one connection.
struct Base
{
	ibConnectionPool         m_pool{ ib::AppDataCtorToken{} };
	ibSingleConnectionHolder m_holder;

	explicit Base(std::shared_ptr<ibDatabaseLayer> db)
	{
		m_pool.Init(std::move(db), /*maxSize=*/1, /*minIdle=*/0);
		m_holder.SetPool(&m_pool);
	}

	int Run(const ibDdlStatement& ddl)
	{
		ibDatabaseQueryBuilder q(&m_holder);
		return q.Execute(ddl);
	}
	int Run(const ibDmlStatement& dml)
	{
		ibDatabaseQueryBuilder q(&m_holder);
		return q.Execute(dml);
	}
	Rows Read(const ibQueryIR& ir, const std::string& kinds, const std::vector<ibValue>& params = {})
	{
		ibDatabaseQueryBuilder q(&m_holder);
		return ReadAll(q.ExecuteIR(ir, params), kinds);
	}
	Rows Returning(const ibDmlStatement& dml, const std::string& kinds)
	{
		ibDatabaseQueryBuilder q(&m_holder);
		return ReadAll(q.ExecuteReturning(dml), kinds);
	}
};

std::shared_ptr<ibDatabaseLayer> OpenSqlite()
{
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	db->Open(ibT(":memory:"));
	return db;
}

// Goods (a key, a code, a quantity, a price, a date, a flag, a group's key, a note) and Groups (a key, a name, a
// parent's key) — the same rows in both bases.
struct MemoryBaseParity : ::testing::Test
{
	Base m_sqlite{ OpenSqlite() };
	Base m_memory{ std::make_shared<ibDatabaseLayerMemory>() };

	void Both(const ibDdlStatement& ddl)
	{
		m_sqlite.Run(ddl);
		m_memory.Run(ddl);
	}
	void Both(const ibDmlStatement& dml)
	{
		const int sqlite = m_sqlite.Run(dml);
		EXPECT_EQ(sqlite, m_memory.Run(dml)) << "rows written";
	}
	// The rows of both, in the order the IR gives them — or as a set where it names none.
	Rows Same(const ibQueryIR& ir, const std::string& kinds, bool ordered = true, const std::vector<ibValue>& params = {})
	{
		Rows sqlite = m_sqlite.Read(ir, kinds, params);
		Rows memory = m_memory.Read(ir, kinds, params);
		if (!ordered) {
			std::sort(sqlite.begin(), sqlite.end());
			std::sort(memory.begin(), memory.end());
		}
		EXPECT_EQ(sqlite, memory);
		return memory;
	}

	void SetUp() override
	{
		Both(ibCreateTable(ibT("Goods"), {
			Field("_IDRRef", ibTypeBinary(16), true), Field("_Code", ibTypeString(20)), Field("_Qty", ibTypeNumber(15, 0)),
			Field("_Price", ibTypeNumber(15, 2)), Field("_Born", ibTypeDate()), Field("_Flag", ibTypeBoolean()),
			Field("_GroupRRef", ibTypeBinary(16)), Field("_Note", ibTypeString(50)) }));
		ibDmlStatement goods = ibInsert(ibT("Goods"), {
			Set("_IDRRef", Key(1)), Set("_Code", Str("A-001")), Set("_Qty", Num(5)), Set("_Price", Dec("2.5")),
			Set("_Born", Day(2026, 1, 15)), Set("_Flag", Bool(true)), Set("_GroupRRef", Key(101)), Set("_Note", Str("milk")) });
		goods.m_extraRows = {
			{ Key(2), Str("A-002"), Num(3),  Num(4),       Day(2026, 2, 3, 10, 30),     Bool(false), Key(101), Null() },
			{ Key(3), Str("B-001"), Null(),  Dec("1.25"),  Day(2026, 2, 28),            Bool(true),  Key(102), Str("Bread") },
			{ Key(4), Str("B-002"), Num(7),  Num(10),      Day(2026, 4, 1),             Bool(false), Null(),   Str("milk powder") },
			{ Key(5), Str("C-001"), Num(3),  Dec("0.5"),   Day(2025, 12, 31, 23, 59, 59), Bool(true), Key(103), Null() },
		};
		Both(goods);

		Both(ibCreateTable(ibT("Groups"), {
			Field("_IDRRef", ibTypeBinary(16), true), Field("_Name", ibTypeString(20)), Field("_ParentRRef", ibTypeBinary(16)) }));
		ibDmlStatement groups = ibInsert(ibT("Groups"), { Set("_IDRRef", Key(100)), Set("_Name", Str("Food")), Set("_ParentRRef", Null()) });
		groups.m_extraRows = {
			{ Key(101), Str("Dairy"),  Key(100) },
			{ Key(102), Str("Bakery"), Key(100) },
			{ Key(103), Str("Other"),  Null() },
		};
		Both(groups);
	}
};

} // namespace

// A filter, an expression in the projection, a sort and a page.
TEST_F(MemoryBaseParity, FilterProjectSortPage)
{
	const ibQueryIR ir(ibLimit(ibSort(ibProject(
		ibFilter(ibScan(ibT("Goods")), Op(ibQueryBinOp::And,
			Op(ibQueryBinOp::Ge, Col("_Qty"), Num(3)),
			Op(ibQueryBinOp::Eq, Col("_Flag"), Bool(true)))),
		{ As(Col("_Code")), As(Op(ibQueryBinOp::Mul, Col("_Qty"), Col("_Price")), "Total") }),
		{ Desc(Col("_Code")) }), 1, 1));
	const Rows rows = Same(ir, "SN");
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows[0][0], "A-001");
	EXPECT_EQ(rows[0][1], "12.5");
}

// LIKE with the case aside, as the renderer asks it.
TEST_F(MemoryBaseParity, LikeIgnoresCase)
{
	const ibQueryIR ir(ibSort(ibProject(
		ibFilter(ibScan(ibT("Goods")), Op(ibQueryBinOp::Like, Col("_Note"), Str("%MILK%"))),
		{ As(Col("_Code")) }), { Asc(Col("_Code")) }));
	EXPECT_EQ(Same(ir, "S").size(), 2u);
}

// NOT over a NULL drops the row, NOT IN a list with a NULL keeps none, IS NULL finds what is missing.
TEST_F(MemoryBaseParity, ThreeValuedLogic)
{
	EXPECT_EQ(Same(ibQueryIR(ibSort(ibProject(
		ibFilter(ibScan(ibT("Goods")), ibNot(Op(ibQueryBinOp::Eq, Col("_Qty"), Num(3)))),
		{ As(Col("_Code")) }), { Asc(Col("_Code")) })), "S").size(), 2u);
	EXPECT_TRUE(Same(ibQueryIR(ibProject(
		ibFilter(ibScan(ibT("Goods")), ibIn(Col("_Qty"), { Num(5), Null() }, true)),
		{ As(Col("_Code")) })), "S").empty());
	EXPECT_EQ(Same(ibQueryIR(ibSort(ibProject(
		ibFilter(ibScan(ibT("Goods")), ibIsNull(Col("_Note"))),
		{ As(Col("_Code")) }), { Asc(Col("_Code")) })), "S").size(), 2u);
	EXPECT_EQ(Same(ibQueryIR(ibProject(
		ibFilter(ibScan(ibT("Goods")), ibIn(Col("_Qty"), {})),
		{ As(Col("_Code")) })), "S").size(), 0u);
}

// GROUP BY a key with NULLs, every fold, HAVING; and a fold with no GROUP BY over no rows is one row.
TEST_F(MemoryBaseParity, GroupByHavingAndEmptyFold)
{
	const ibQueryExprPtr group = Col("_GroupRRef");
	const Rows rows = Same(ibQueryIR(ibSort(ibAggregate(ibScan(ibT("Goods")),
		{ As(group), As(ibFunc(ibT("COUNT")), "N"), As(ibFunc(ibT("COUNT"), { Col("_Qty") }), "Q"),
		  As(ibFunc(ibT("SUM"), { Col("_Qty") }), "S"), As(ibFunc(ibT("MIN"), { Col("_Code") }), "Lo"),
		  As(ibFunc(ibT("MAX"), { Col("_Born") }), "Hi") },
		{ group }, Op(ibQueryBinOp::Ge, ibFunc(ibT("COUNT")), Num(1))),
		{ Asc(group) })), "XNNNSD");
	ASSERT_EQ(rows.size(), 4u);
	EXPECT_EQ(rows[0][0], "NULL");
	EXPECT_EQ(rows[1][1], "2");

	const Rows none = Same(ibQueryIR(ibAggregate(
		ibFilter(ibScan(ibT("Goods")), Op(ibQueryBinOp::Gt, Col("_Qty"), Num(100))),
		{ As(ibFunc(ibT("COUNT")), "N"), As(ibFunc(ibT("SUM"), { Col("_Qty") }), "S") }, {})), "NN");
	ASSERT_EQ(none.size(), 1u);
	EXPECT_EQ(none[0][0], "0");
	EXPECT_EQ(none[0][1], "NULL");
}

// COUNT(DISTINCT x) and DISTINCT rows.
TEST_F(MemoryBaseParity, Distinct)
{
	Same(ibQueryIR(ibAggregate(ibScan(ibT("Goods")), { As(ibFunc(ibT("COUNT"), { Col("_Qty") }, true), "N") }, {})), "N");
	EXPECT_EQ(Same(ibQueryIR(ibDistinct(ibProject(ibScan(ibT("Goods")), { As(Col("_Qty")) }))), "N", false).size(), 4u);
}

// Joins on a reference's key, under aliases: inner, left (a good with no group), full.
TEST_F(MemoryBaseParity, Joins)
{
	const auto join = [](ibQueryJoinType type) {
		return ibJoin(ibScan(ibT("Goods"), ibT("G")), ibScan(ibT("Groups"), ibT("R")),
			Op(ibQueryBinOp::Eq, Col("G", "_GroupRRef"), Col("R", "_IDRRef")), type);
	};
	EXPECT_EQ(Same(ibQueryIR(ibSort(ibProject(join(ibQueryJoinType::Inner), { As(Col("G", "_Code")), As(Col("R", "_Name")) }),
		{ Asc(Col("G", "_Code")) })), "SS").size(), 4u);
	EXPECT_EQ(Same(ibQueryIR(ibSort(ibProject(join(ibQueryJoinType::Left), { As(Col("G", "_Code")), As(Col("R", "_Name")) }),
		{ Asc(Col("G", "_Code")) })), "SS").size(), 5u);
	EXPECT_EQ(Same(ibQueryIR(ibProject(join(ibQueryJoinType::Full), { As(Col("G", "_Code")), As(Col("R", "_Name")) })),
		"SS", false).size(), 6u);
}

// A derived table, UNION and UNION ALL under a sort.
TEST_F(MemoryBaseParity, SubqueryAndUnion)
{
	const auto codes = [](int qty) {
		return ibProject(ibFilter(ibScan(ibT("Goods")), Op(ibQueryBinOp::Ge, Col("_Qty"), Num(qty))), { As(Col("_Code")) });
	};
	EXPECT_EQ(Same(ibQueryIR(ibSort(ibProject(ibSubquery(ibUnionAll(codes(3), codes(5)), ibT("U")), { As(Col("U", "_Code")) }),
		{ Asc(Col("U", "_Code")) })), "S").size(), 6u);
	EXPECT_EQ(Same(ibQueryIR(ibSort(ibProject(ibSubquery(ibUnion(codes(3), codes(5)), ibT("U")), { As(Col("U", "_Code")) }),
		{ Asc(Col("U", "_Code")) })), "S").size(), 4u);
}

// Ranking and running folds: ties, NULL first, the peers included or not, the rows before, the whole partition.
TEST_F(MemoryBaseParity, Windows)
{
	const auto over = [](ibQueryExprPtr fn, std::vector<ibQueryExprPtr> partition, std::vector<ibQuerySortKey> order, ibQueryFrame frame) {
		return ibWindowed(std::move(fn), ibQueryWindow{ std::move(partition), std::move(order), frame });
	};
	const ibQueryIR ir(ibSort(ibProject(ibScan(ibT("Goods")), {
		As(Col("_Code")),
		As(over(ibFunc(ibT("ROW_NUMBER")), {}, { Asc(Col("_Born")) }, ibQueryFrame::NoFrame), "RowNo"),
		As(over(ibFunc(ibT("RANK")), {}, { Asc(Col("_Qty")) }, ibQueryFrame::NoFrame), "Rnk"),
		As(over(ibFunc(ibT("DENSE_RANK")), {}, { Asc(Col("_Qty")) }, ibQueryFrame::NoFrame), "Dense"),
		As(over(ibFunc(ibT("SUM"), { Col("_Price") }), { Col("_Flag") }, { Asc(Col("_Qty")) }, ibQueryFrame::RangeThroughPeers), "Peers"),
		As(over(ibFunc(ibT("SUM"), { Col("_Price") }), {}, { Asc(Col("_Born")) }, ibQueryFrame::RowsThroughCurrent), "Running"),
		As(over(ibFunc(ibT("SUM"), { Col("_Price") }), {}, { Asc(Col("_Born")) }, ibQueryFrame::RowsBeforeCurrent), "Before"),
		As(over(ibFunc(ibT("COUNT")), {}, {}, ibQueryFrame::NoFrame), "Counted") }),   // not "All": a word of SQL's
		{ Asc(Col("_Code")) }));
	const Rows rows = Same(ir, "SNNNNNNN");
	ASSERT_EQ(rows.size(), 5u);
	EXPECT_EQ(rows[4][7], "5");
}

// A named query, and a recursive one walking the groups down from Food: the goods under it.
TEST_F(MemoryBaseParity, NamedAndRecursiveQueries)
{
	ibQueryIR walk(ibSort(ibProject(
		ibFilter(ibScan(ibT("Goods")), ibInQuery(Col("_GroupRRef"), ibProject(ibScan(ibT("walk")), { As(Col("_IDRRef")) }))),
		{ As(Col("_Code")) }), { Asc(Col("_Code")) }));
	walk.m_with.push_back(ibQueryCte{ ibT("walk"), ibUnion(
		ibProject(ibFilter(ibScan(ibT("Groups")), Op(ibQueryBinOp::Eq, Col("_IDRRef"), Key(100))), { As(Col("_IDRRef")) }),
		ibProject(ibJoin(ibScan(ibT("Groups"), ibT("c")), ibScan(ibT("walk"), ibT("w")),
			Op(ibQueryBinOp::Eq, Col("c", "_ParentRRef"), Col("w", "_IDRRef"))), { As(Col("c", "_IDRRef"), "_IDRRef") })), true });
	EXPECT_EQ(Same(walk, "S"), (Rows{ { "A-001" }, { "A-002" }, { "B-001" } }));

	ibQueryIR named(ibSort(ibProject(ibScan(ibT("cheap")), { As(Col("_Code")) }), { Asc(Col("_Code")) }));
	named.m_with.push_back(ibQueryCte{ ibT("cheap"),
		ibProject(ibFilter(ibScan(ibT("Goods")), Op(ibQueryBinOp::Lt, Col("_Price"), Num(3))), { As(Col("_Code")) }), false });
	EXPECT_EQ(Same(named, "S").size(), 3u);
}

// A correlated EXISTS and its negation.
TEST_F(MemoryBaseParity, CorrelatedExists)
{
	const auto exists = [](bool negated) {
		return ibQueryIR(ibSort(ibProject(ibFilter(ibScan(ibT("Groups"), ibT("R")), ibExists(
			ibProject(ibFilter(ibScan(ibT("Goods"), ibT("G")), Op(ibQueryBinOp::And,
				Op(ibQueryBinOp::Eq, Col("G", "_GroupRRef"), Col("R", "_IDRRef")),
				Op(ibQueryBinOp::Eq, Col("G", "_Flag"), Bool(true)))), { As(Num(1), "One") }), negated)),
			{ As(Col("R", "_Name")) }), { Asc(Col("R", "_Name")) }));
	};
	EXPECT_EQ(Same(exists(false), "S"), (Rows{ { "Bakery" }, { "Dairy" }, { "Other" } }));
	EXPECT_EQ(Same(exists(true), "S"), (Rows{ { "Food" } }));
}

// CASE, COALESCE, a concatenation with a NULL, a substring, a parameter.
TEST_F(MemoryBaseParity, Expressions)
{
	const ibQueryIR ir(ibSort(ibProject(ibScan(ibT("Goods")), {
		As(Col("_Code")),
		As(ibCase({ { Op(ibQueryBinOp::Gt, Col("_Qty"), ibParam(0)), Str("many") }, { ibIsNull(Col("_Qty")), Str("none") } }, Str("few")), "Size"),
		As(ibFunc(ibT("COALESCE"), { Col("_Note"), Str("n/a") }), "Note"),
		As(Op(ibQueryBinOp::Concat, Op(ibQueryBinOp::Concat, Col("_Code"), Str("/")), Col("_Note")), "Label"),
		As(ibSubstring(Col("_Code"), Num(3), Num(3)), "Tail") }),
		{ Asc(Col("_Code")) }));
	const Rows rows = Same(ir, "SSSSS", true, { ibValue(ibNumber(4)) });
	ASSERT_EQ(rows.size(), 5u);
	EXPECT_EQ(rows[0], (std::vector<std::string>{ "A-001", "many", "milk", "A-001/milk", "001" }));
	EXPECT_EQ(rows[1][3], "NULL");
}

// The calendar: the start and the end of a month, a month later, the days until a date, the quarter.
TEST_F(MemoryBaseParity, Calendar)
{
	const ibQueryIR ir(ibSort(ibProject(ibScan(ibT("Goods")), {
		As(Col("_Code")),
		As(ibPeriodTrunc(Col("_Born"), ibTotalsPeriod::Month), "Start"),
		As(ibPeriodEnd(Col("_Born"), ibTotalsPeriod::Month), "End"),
		As(ibDateAdd(Col("_Born"), ibTotalsPeriod::Month, Num(1)), "Later"),
		As(ibDateDiff(Col("_Born"), Day(2026, 3, 1), ibTotalsPeriod::Day), "Days"),
		As(ibDatePartOf(Col("_Born"), ibDatePart::Quarter), "Quarter") }),
		{ Asc(Col("_Code")) }));
	const Rows rows = Same(ir, "SDDDNN");
	ASSERT_EQ(rows.size(), 5u);
	EXPECT_EQ(rows[4][3], "2026-01-31 23:59:59");
}

// Writes: UPDATE reading the row it writes, DELETE, UPSERT on the key, INSERT … SELECT, RETURNING — then the tables.
TEST_F(MemoryBaseParity, Writes)
{
	Both(ibUpdate(ibT("Goods"), { Set("_Qty", Op(ibQueryBinOp::Add, Col("_Qty"), Num(1))) }, Op(ibQueryBinOp::Eq, Col("_Flag"), Bool(true))));
	Both(ibDelete(ibT("Goods"), ibIsNull(Col("_Qty"))));
	Both(ibUpsert(ibT("Goods"), { Set("_IDRRef", Key(2)), Set("_Code", Str("A-002x")), Set("_Qty", Num(9)) }, { ibT("_IDRRef") }));
	Both(ibUpsert(ibT("Goods"), { Set("_IDRRef", Key(9)), Set("_Code", Str("Z-009")), Set("_Qty", Num(1)) }, { ibT("_IDRRef") }));

	Both(ibCreateTable(ibT("Copy"), { Field("_Code", ibTypeString(20)), Field("_Qty", ibTypeNumber(15, 0)) }));
	Both(ibInsertSelect(ibT("Copy"), { ibT("_Code"), ibT("_Qty") },
		ibProject(ibFilter(ibScan(ibT("Goods")), Op(ibQueryBinOp::Gt, Col("_Qty"), Num(3))), { As(Col("_Code")), As(Col("_Qty")) })));

	const ibDmlStatement returning = ibReturning(
		ibUpdate(ibT("Copy"), { Set("_Qty", Num(0)) }, Op(ibQueryBinOp::Like, Col("_Code"), Str("a%"))), { ibT("_Code") });
	Rows sqlite = m_sqlite.Returning(returning, "S");
	Rows memory = m_memory.Returning(returning, "S");
	std::sort(sqlite.begin(), sqlite.end());
	std::sort(memory.begin(), memory.end());
	EXPECT_EQ(sqlite, memory);

	Same(ibQueryIR(ibSort(ibProject(ibScan(ibT("Goods")), { As(Col("_IDRRef")), As(Col("_Code")), As(Col("_Qty")) }),
		{ Asc(Col("_IDRRef")) })), "XSN");
	Same(ibQueryIR(ibSort(ibProject(ibScan(ibT("Copy")), { As(Col("_Code")), As(Col("_Qty")) }), { Asc(Col("_Code")) })), "SN");

	Both(ibDropTable(ibT("Copy")));
	Both(ibDropTable(ibT("Copy"), /*ifExists=*/true));
}

// ROLLUP, which SQLite has no form for: every level, GROUPING telling them apart, the grand total.
TEST_F(MemoryBaseParity, MemoryOnly_RollupFoldsEveryLevel)
{
	const ibQueryExprPtr flag = Col("_Flag"), group = Col("_GroupRRef");
	const ibQueryIR ir(ibSort(ibAggregate(ibScan(ibT("Goods")),
		{ As(flag), As(group), As(ibFunc(ibT("GROUPING"), { flag }), "GF"), As(ibFunc(ibT("GROUPING"), { group }), "GG"),
		  As(ibFunc(ibT("SUM"), { Col("_Qty") }), "S") },
		{ flag, group }, nullptr, /*rollup=*/true),
		{ Asc(ibFunc(ibT("GROUPING"), { flag })), Asc(ibFunc(ibT("GROUPING"), { group })), Asc(flag), Asc(group) }));
	const Rows rows = m_memory.Read(ir, "BXNNN");
	const Rows expected = {
		{ "false", "NULL",       "0", "0", "7" },
		{ "false", KeyText(101), "0", "0", "3" },
		{ "true",  KeyText(101), "0", "0", "5" },
		{ "true",  KeyText(102), "0", "0", "NULL" },
		{ "true",  KeyText(103), "0", "0", "3" },
		{ "false", "NULL",       "0", "1", "10" },
		{ "true",  "NULL",       "0", "1", "8" },
		{ "NULL",  "NULL",       "1", "1", "18" },
	};
	EXPECT_EQ(rows, expected);
}

// A division by zero is refused, as PostgreSQL, Firebird and MS SQL refuse it (SQLite answers NULL: tests only).
TEST_F(MemoryBaseParity, MemoryOnly_DivisionByZeroIsRefused)
{
	const ibQueryIR ir(ibProject(ibScan(ibT("Goods")), { As(Op(ibQueryBinOp::Div, Col("_Qty"), Num(0)), "X") }));
	EXPECT_THROW(m_memory.Read(ir, "N"), ibCoreException);
}

// A rollback puts the tables back as the transaction found them.
TEST_F(MemoryBaseParity, MemoryOnly_RollbackPutsTheRowsBack)
{
	{
		ibDatabaseQueryBuilder q(&m_memory.m_holder);
		q.BeginTransaction();
		EXPECT_EQ(q.Execute(ibDelete(ibT("Goods"))), 5);
		q.RollBack();
	}
	EXPECT_EQ(m_memory.Read(ibQueryIR(ibProject(ibScan(ibT("Goods")), { As(Col("_Code")) })), "S").size(), 5u);
}

// SQL text is refused in words: the base reads the IR alone.
TEST(MemoryBase, SqlTextIsRefused)
{
	auto db = std::make_shared<ibDatabaseLayerMemory>();
	EXPECT_THROW(db->RunQuery(ibT("SELECT 1")), ibBackendDatabaseException);
	EXPECT_EQ(db->GetDatabaseLayerName(), ibString(ibT("Memory")));
	EXPECT_TRUE(db->GetDialect().m_features.m_inMemory);
}

// A clone is a space of its own, empty — a pool grows by cloning, and what one read copies in another does not see.
TEST(MemoryBase, ACloneIsASpaceOfItsOwn)
{
	auto db = std::make_shared<ibDatabaseLayerMemory>();
	Base base(db);
	base.Run(ibCreateTable(ibT("T"), { Field("_A", ibTypeString(10)) }));
	EXPECT_TRUE(db->TableExists(ibT("t")));
	EXPECT_EQ(db->GetColumns(ibT("T")), (std::vector<ibString>{ ibT("_A") }));
	std::unique_ptr<ibDatabaseLayer> clone(db->Clone());
	EXPECT_FALSE(clone->TableExists(ibT("T")));
	EXPECT_TRUE(clone->GetTables().empty());
}

// ⭐ A SPACE IS ONE EXECUTION'S — the queries' memory manager hands two executions at once two spaces, neither of them the
// connection every space is cloned from, and a space goes with its execution: what was left in it goes too.
TEST(MemoryBase, ASpaceIsOneExecutionsAndGoesWithIt)
{
	const auto tables = [](ibMemorySpace& space) {
		ibConnectionScope scope(space.Holder());
		return scope ? scope->GetTables() : std::vector<ibString>{ ibT("<no connection>") };
	};
	const ibConnectionPool& pool = *ibApplicationHost::GetQueryMemoryManager().GetConnectionPool();
	const std::size_t live = pool.LiveSize();
	{
		ibMemorySpace first;
		ibMemorySpace second;
		EXPECT_EQ(pool.LiveSize(), live + 2) << "a space each, cloned: the seed is nobody's";
		{
			ibDatabaseQueryBuilder q(first.Holder());
			q.Execute(ibCreateTable(ibT("Left"), { Field("_A", ibTypeString(10)) }));
		}
		EXPECT_EQ(tables(first), (std::vector<ibString>{ ibT("Left") }));
		EXPECT_TRUE(tables(second).empty());
	}
	EXPECT_EQ(pool.LiveSize(), live) << "both forgotten, with their tables";
	ibMemorySpace next;
	EXPECT_TRUE(tables(next).empty());
}

// ⭐ A FIELD KEEPS A VALUE AS ITS NODE — the _V field of a column that declares no type (ibColumnRole::Value): the value
// written as the bytes of the node it serialises to (SetParamBlob), kept as any base keeps bytes, and
// its bytes read back (GetResultBlob) as the node by whoever has the configuration. A number, a text, a date and an object in one
// field come back each as it was written, from either base — and neither knows what it holds.
TEST_F(MemoryBaseParity, AFieldKeepsAValueAsItsNode)
{
	Both(ibCreateTable(ibT("T"), { Field("_N", ibTypeNumber(10, 0), true), Field("_V", ibTypeBlob()) }));
	const std::vector<ibValue> values = {
		ibValue(ibNumber(7)),
		ibValue(ibString(ibT("seven"))),
		ibValue(ibDateTime(2026, 11, 18, 0, 0, 0)),
		ibValue(new ibValueArray({ ibValue(1), ibValue(3) })),
	};
	for (Base* base : { &m_sqlite, &m_memory }) {
		{
			ibBatchInsert batch(ibT("T"), { ibT("_N"), ibT("_V") }, &base->m_holder);
			for (size_t i = 0; i < values.size(); ++i) {
				ibDataNode node;
				ibSerializeValue(values[i], node);
				ibMemoryBuffer bytes;   // the node's bytes, as the column codec binds them
				ibBinaryProvider().Write(node, bytes);
				batch.SetParamNumber(1, ibNumber(static_cast<int>(i + 1)));
				batch.SetParamBlob(2, bytes.GetData(), static_cast<long>(bytes.GetDataLen()));
				batch.RunQuery();
			}
			batch.SetParamNumber(1, ibNumber(static_cast<int>(values.size() + 1)));
			batch.SetParamNull(2);
			batch.RunQuery();
			batch.Flush();
		}   // the batch holds the one connection the pool has

		ibDatabaseQueryBuilder q(&base->m_holder);
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibSort(ibProject(ibScan(ibT("T")), { As(Col("_N")), As(Col("_V")) }),
			{ Asc(Col("_N")) })));
		std::vector<ibValue> read;
		while (rs.Next()) {
			// The bytes of the node the value was written as — read as the column codec reads them.
			ibMemoryBuffer bytes;
			ibDataNode node;
			if (rs.GetResultBlob(2, bytes) != nullptr && bytes.GetDataLen() > 0)
				ibBinaryProvider().Read(bytes, node);
			read.push_back(rs.IsResultNull(2) ? ibValue(TYPE_NULL) : ibDeserializeValue(node));
		}
		ASSERT_EQ(read.size(), values.size() + 1);
		EXPECT_EQ(read[0].GetNumber().ToInt(), 7);
		EXPECT_EQ(read[1].GetString(), ibString(ibT("seven")));
		EXPECT_EQ(read[2].GetDate().Format(ibT("%Y-%m-%d")), ibString(ibT("2026-11-18")));
		EXPECT_EQ(read[3].GetClassType(), values[3].GetClassType()) << "an object, read back as one";
		EXPECT_EQ(read[4].GetType(), TYPE_NULL);
	}
}

// ⭐ A FIELD A ROW LEAVES OUT HOLDS ITS DEFAULT — a row inserted without it, and every row already there when the field
// is added, from either base: SQLite told it as text (ibSqlOfValue), the base in memory keeping it a field
// (ibMemoryTable::m_defaults). A structure creates every field of a column holding its write of a fresh value
// (structureBatch.cpp), so a seed that fills a row in part leaves what the application would have written.
TEST_F(MemoryBaseParity, AFieldARowLeavesOutHoldsItsDefault)
{
	const auto defaulted = [](const char* name, const ibColumnType& type, ibQueryExprPtr value) {
		ibDdlColumn c = Field(name, type);
		c.m_notNull = true;
		c.m_default = std::move(value);
		return c;
	};
	const unsigned char zeroKey[16] = {};
	Both(ibCreateTable(ibT("T"), {
		Field("_K", ibTypeNumber(10, 0), true),
		defaulted("_N", ibTypeNumber(15, 2), Num(0)),
		defaulted("_B", ibTypeBoolean(), Bool(false)),
		defaulted("_S", ibTypeString(20), Str("")),
		defaulted("_D", ibTypeDate(), ibConst(ibValue(ibDateTime()))),
		defaulted("_R", ibTypeBinary(16), ibConstBlob(zeroKey, sizeof(zeroKey))) }));
	Both(ibInsert(ibT("T"), { Set("_K", Num(1)) }));
	Both(ibAddColumn(ibT("T"), defaulted("_E", ibTypeInteger(), Num(-1))));   // over the row already there
	Both(ibInsert(ibT("T"), { Set("_K", Num(2)), Set("_N", Num(7)) }));

	const Rows rows = Same(ibQueryIR(ibSort(ibProject(ibScan(ibT("T")),
		{ As(Col("_K")), As(Col("_N")), As(Col("_B")), As(Col("_S")), As(Col("_D")), As(Col("_R")), As(Col("_E")) }),
		{ Asc(Col("_K")) })), "NNBSDXN");
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(rows[0], (std::vector<std::string>{ "1", "0", "false", "", rows[0][4], std::string(32, '0'), "-1" }));
	EXPECT_EQ(rows[1][1], "7");
	EXPECT_EQ(rows[1][6], "-1");
}

// ⭐ ROWS HANDED ON ARE HELD, NOT COPIED, AND A WRITE COPIES THEM FIRST (ibMemoryRows) — a result read straight from a
// table keeps what it read when the table is written after it, and when the table is dropped.
TEST(MemoryBase, AResultKeepsItsRowsPastAWriteToItsTable)
{
	auto db = std::make_shared<ibDatabaseLayerMemory>();
	ibQueryInterpreter& base = *db->GetQueryInterpreter();
	const std::vector<ibValue> none;
	base.Execute(ibCreateTable(ibT("T"), { Field("_A", ibTypeNumber(10, 0)) }));
	ibDmlStatement insert = ibInsert(ibT("T"), { Set("_A", Num(1)) });
	insert.m_extraRows = { { Num(2) } };
	base.Execute(insert, none);

	const ibQueryIR all(ibScan(ibT("T")));
	const ibMemoryTable read = base.Execute(all, none);
	base.Execute(ibUpdate(ibT("T"), { Set("_A", Num(9)) }), none);
	const ibMemoryTable after = base.Execute(all, none);
	base.Execute(ibDropTable(ibT("T")));

	const auto values = [](const ibMemoryTable& table) {
		std::vector<int> out;
		for (const ibMemoryRow& row : table.m_rows)
			out.push_back(row[0].AsNumber().ToInt());
		return out;
	};
	EXPECT_EQ(values(read), (std::vector<int>{ 1, 2 })) << "the rows read before the write";
	EXPECT_EQ(values(after), (std::vector<int>{ 9, 9 }));
}
