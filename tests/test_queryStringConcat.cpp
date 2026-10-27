// String '+' lowers to concatenation. Firebird dialect 3 refuses VARCHAR + VARCHAR
// ("Strings cannot be added or subtracted"). The lowering types the sum the way it types an output
// (TypeOfExpr): a string sum is spelled ||, and a number, a date shifted by a number and two untyped
// columns stay +.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "backend/compiler/value.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/dbTableProvider.h"
#include "backend/query/queryColumn.h"
#include "backend/query/queryLowering.h"
#include "backend/query/queryable.h"
#include "core/description/typeDescription.h"
#include "core/query/queryParser.h"

namespace {

class NamedCol : public ibBackendQueryColumn {
public:
	NamedCol(const ibString& name, Kind kind, ibTypeDescription type)
		: m_name(name), m_kind(kind), m_type(std::move(type)) {}
	ibString           GetName()         const override { return m_name; }
	ibString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return 1; }
	Kind               GetColumnKind()   const override { return m_kind; }
private:
	ibString                  m_name;
	Kind                      m_kind;
	mutable ibTypeDescription m_type;
};

// One source vending its columns by name — what the lowering resolves the expression's names through.
class Source : public ibBackendQueryable {
public:
	Source() {
		ibGuidImpl impl{}; impl.m_data1 = 1; m_key = ibGuid(impl);
	}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }

	ibString GetQueryTableName() const override { return wxT("T"); }
	ibMetaID GetQueryTableId()   const override { return 1; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	bool     IsComputedInRam()   const override { return false; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const ibString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
private:
	ibUniqueKey m_key;
	std::vector<const ibBackendQueryColumn*> m_cols;
};

// `SELECT <expr> AS v`, the expression lowered against `source` and written for the dialect.
std::string Render(const ibDialectDictionary& dialect, const Source& source, const ibString& expr)
{
	ibQueryParser parser;
	const ibQuerySelectPtr select = parser.Parse(wxT("SELECT ") + expr + wxT(" AS v FROM Catalog.T"));
	if (select == nullptr || select->m_projections.empty())
		return "(no parse)";
	const ibQueryColumnExprPtr lowered =
		ibQueryLowering::LowerLambdaColumnExpr(&source, *select->m_projections.front().m_expr, {});
	if (lowered == nullptr)
		return "(not lowered)";
	const ibQueryIR ir(ibProject(ibScan(wxT("T")),
		{ { ibDbTableProvider::BuildColumnExprIR(nullptr, lowered), wxT("v") } }));
	return ibQueryRenderer(dialect).Render(ir).m_sql.ToStdString();
}

} // namespace

// The statement Firebird refused: (carried description + " ") + identifier_S. The carried column is
// untyped (the select cannot see into it); beside a string it is concatenated.
TEST(QueryStringConcat, CarriedStringBesideAStringFieldRendersConcatenation)
{
	NamedCol carried(wxT("out_q_carried0"), ibBackendQueryColumn::Kind::Computed, ibTypeDescription());
	NamedCol ident(wxT("fld2430"), ibBackendQueryColumn::Kind::Composite, ibTypeDescription(g_valueStringCLSID));
	Source source;
	source.AddCol(&carried);
	source.AddCol(&ident);
	const ibString phone = wxT("out_q_carried0 + \" \" + fld2430");

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), source, phone),
		"SELECT ((out_q_carried0 || CAST(? AS VARCHAR(1))) || fld2430_S) AS v FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerPostgres::Dialect(), source, phone),
		"SELECT ((out_q_carried0 || CAST($1 AS VARCHAR(1))) || fld2430_S) AS v FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerSQLite::Dialect(), source, phone),
		"SELECT ((out_q_carried0 || CAST(? AS VARCHAR(1))) || fld2430_S) AS v FROM T");
}

TEST(QueryStringConcat, NumberAdditionStaysPlus)
{
	NamedCol qty(wxT("qty"), ibBackendQueryColumn::Kind::Composite, ibTypeDescription(g_valueNumberCLSID));
	Source source;
	source.AddCol(&qty);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), source, wxT("qty + 1")),
		"SELECT (qty_N + CAST(? AS NUMERIC(18,6))) AS v FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerPostgres::Dialect(), source, wxT("qty + 1")),
		"SELECT (qty_N + CAST($1 AS NUMERIC(18,6))) AS v FROM T");
	EXPECT_EQ(Render(ibDatabaseLayerSQLite::Dialect(), source, wxT("qty + 1")),
		"SELECT (qty_N + CAST(? AS DECIMAL(18,6))) AS v FROM T");
}

TEST(QueryStringConcat, DateShiftedByANumberStaysPlus)
{
	NamedCol moment(wxT("fld9"), ibBackendQueryColumn::Kind::Composite, ibTypeDescription(g_valueDateCLSID));
	Source source;
	source.AddCol(&moment);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), source, wxT("fld9 + 1")),
		"SELECT (fld9_D + CAST(? AS NUMERIC(18,6))) AS v FROM T");
}

TEST(QueryStringConcat, TwoUntypedColumnsStayAddition)
{
	NamedCol a(wxT("out_a"), ibBackendQueryColumn::Kind::Computed, ibTypeDescription());
	NamedCol b(wxT("out_b"), ibBackendQueryColumn::Kind::Computed, ibTypeDescription());
	Source source;
	source.AddCol(&a);
	source.AddCol(&b);

	EXPECT_EQ(Render(ibDatabaseLayerFirebird::Dialect(), source, wxT("out_a + out_b")),
		"SELECT (out_a + out_b) AS v FROM T");
}

TEST(QueryStringConcat, ConcatOperatorRendersPipes)
{
	const ibQueryExprPtr expr = ibBinOp(ibQueryBinOp::Concat,
		ibCol(wxT("a_S")),
		ibCast(ibConst(ibValue(ibString(wxT("x")))), ibTypeString(1)));
	const ibQueryIR ir(ibProject(ibScan(wxT("T")), { { expr, wxT("v") } }));

	EXPECT_EQ(ibQueryRenderer(ibDatabaseLayerFirebird::Dialect()).Render(ir).m_sql.ToStdString(),
		"SELECT (a_S || CAST(? AS VARCHAR(1))) AS v FROM T");
	EXPECT_EQ(ibQueryRenderer(ibDatabaseLayerPostgres::Dialect()).Render(ir).m_sql.ToStdString(),
		"SELECT (a_S || CAST($1 AS VARCHAR(1))) AS v FROM T");
	EXPECT_EQ(ibQueryRenderer(ibDatabaseLayerSQLite::Dialect()).Render(ir).m_sql.ToStdString(),
		"SELECT (a_S || CAST(? AS VARCHAR(1))) AS v FROM T");
}
