// String '+' lowers to concatenation. Firebird dialect 3 refuses VARCHAR + VARCHAR
// ("Strings cannot be added or subtracted"). A string literal, a _S field, or a
// carried column beside one is spelled ||. A number, and a date shifted by a number, stay +.

#include <gtest/gtest.h>

#include "backend/compiler/value.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/dbTableProvider.h"
#include "backend/query/queryColumn.h"
#include "backend/query/queryable.h"
#include "core/types.h"

namespace {

class NamedCol : public ibBackendQueryColumn {
public:
	NamedCol(const wxString& name, Kind kind, ibTypeDescription type)
		: m_name(name), m_kind(kind), m_type(std::move(type)) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return 1; }
	Kind               GetColumnKind()   const override { return m_kind; }
private:
	wxString                  m_name;
	Kind                      m_kind;
	mutable ibTypeDescription m_type;
};

wxString Render(const ibDialectDictionary& dialect, const ibQueryExprPtr& expr, const wxString& alias)
{
	const ibQueryIR ir(ibProject(ibScan(wxT("T")), { { expr, alias } }));
	return ibQueryRenderer(dialect).Render(ir).m_sql;
}

ibQueryColumnExprPtr Add(ibQueryColumnExprPtr lhs, ibQueryColumnExprPtr rhs)
{
	return ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Add, std::move(lhs), std::move(rhs));
}

// The statement Firebird refused: (carried description + " ") + identifier_S.
ibQueryColumnExprPtr PhoneOf(ibBackendQueryColumn& carried, ibBackendQueryColumn& ident)
{
	return Add(Add(ibQueryColumnExpr::Col(&carried),
	               ibQueryColumnExpr::Const(ibValue(wxString(wxT(" "))))),
	           ibQueryColumnExpr::Col(&ident));
}

} // namespace

TEST(QueryStringConcat, CarriedStringBesideAStringFieldRendersConcatenation)
{
	NamedCol carried(wxT("out_q_carried0"), ibBackendQueryColumn::Kind::Computed, ibTypeDescription());
	NamedCol ident(wxT("fld2430"), ibBackendQueryColumn::Kind::Composite, ibTypeDescription(g_valueStringCLSID));
	const ibQueryExprPtr expr = ibDbTableProvider::BuildColumnExprIR(nullptr, PhoneOf(carried, ident));
	ASSERT_NE(nullptr, expr);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), expr, wxT("phone")).ToStdString(),
		"SELECT ((out_q_carried0 || CAST(? AS VARCHAR(1))) || fld2430_S) AS phone FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerPostgres::Dialect(), expr, wxT("phone")).ToStdString(),
		"SELECT ((out_q_carried0 || CAST($1 AS VARCHAR(1))) || fld2430_S) AS phone FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerSQLite::Dialect(), expr, wxT("phone")).ToStdString(),
		"SELECT ((out_q_carried0 || CAST(? AS VARCHAR(1))) || fld2430_S) AS phone FROM T");
}

TEST(QueryStringConcat, TypedCarriedStringAndARawStringConcatenate)
{
	NamedCol carried(wxT("out_q_carried0"), ibBackendQueryColumn::Kind::Computed,
	                 ibTypeDescription(g_valueStringCLSID));
	const ibBackendColumnRawDB description = ibBackendColumnRawDB::String(wxT("Description"));
	const ibQueryColumnExprPtr expr = Add(ibQueryColumnExpr::Col(&carried),
	                                      ibQueryColumnExpr::Col(&description));
	const ibQueryExprPtr ir = ibDbTableProvider::BuildColumnExprIR(nullptr, expr);
	ASSERT_NE(nullptr, ir);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), ir, wxT("phone")).ToStdString(),
		"SELECT (out_q_carried0 || Description) AS phone FROM T");
}

TEST(QueryStringConcat, NumberAdditionStaysPlus)
{
	NamedCol qty(wxT("qty"), ibBackendQueryColumn::Kind::Composite, ibTypeDescription(g_valueNumberCLSID));
	const ibQueryExprPtr expr = ibDbTableProvider::BuildColumnExprIR(nullptr,
		Add(ibQueryColumnExpr::Col(&qty), ibQueryColumnExpr::Const(ibValue(ibNumber(1)))));
	ASSERT_NE(nullptr, expr);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), expr, wxT("n")).ToStdString(),
		"SELECT (qty_N + CAST(? AS NUMERIC(18,6))) AS n FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerPostgres::Dialect(), expr, wxT("n")).ToStdString(),
		"SELECT (qty_N + CAST($1 AS NUMERIC(18,6))) AS n FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerSQLite::Dialect(), expr, wxT("n")).ToStdString(),
		"SELECT (qty_N + CAST(? AS DECIMAL(18,6))) AS n FROM T");
}

TEST(QueryStringConcat, DateShiftedByANumberStaysPlus)
{
	NamedCol when(wxT("fld9"), ibBackendQueryColumn::Kind::Composite, ibTypeDescription(g_valueDateCLSID));
	const ibQueryExprPtr expr = ibDbTableProvider::BuildColumnExprIR(nullptr,
		Add(ibQueryColumnExpr::Col(&when), ibQueryColumnExpr::Const(ibValue(ibNumber(1)))));
	ASSERT_NE(nullptr, expr);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), expr, wxT("d")).ToStdString(),
		"SELECT (fld9_D + CAST(? AS NUMERIC(18,6))) AS d FROM T");
}

TEST(QueryStringConcat, TwoUntypedColumnsStayAddition)
{
	NamedCol a(wxT("out_a"), ibBackendQueryColumn::Kind::Computed, ibTypeDescription());
	NamedCol b(wxT("out_b"), ibBackendQueryColumn::Kind::Computed, ibTypeDescription());
	const ibQueryExprPtr expr = ibDbTableProvider::BuildColumnExprIR(nullptr,
		Add(ibQueryColumnExpr::Col(&a), ibQueryColumnExpr::Col(&b)));
	ASSERT_NE(nullptr, expr);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), expr, wxT("n")).ToStdString(),
		"SELECT (out_a + out_b) AS n FROM T");
}

TEST(QueryStringConcat, ConcatOperatorRendersPipes)
{
	const ibQueryExprPtr expr = ibBinOp(ibQueryBinOp::Concat,
		ibCol(wxT("a_S")),
		ibCast(ibConst(ibValue(wxString(wxT("x")))), ibTypeString(1)));

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), expr, wxT("v")).ToStdString(),
		"SELECT (a_S || CAST(? AS VARCHAR(1))) AS v FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerPostgres::Dialect(), expr, wxT("v")).ToStdString(),
		"SELECT (a_S || CAST($1 AS VARCHAR(1))) AS v FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerSQLite::Dialect(), expr, wxT("v")).ToStdString(),
		"SELECT (a_S || CAST(? AS VARCHAR(1))) AS v FROM T");
}
