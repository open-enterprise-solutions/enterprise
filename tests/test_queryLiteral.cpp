// =============================================================================
// A query's constant — core/query/queryLiteral.h — and how the query's text refuses (core/query/queryRefusal.h).
//
// The query's lexer, parser and AST name no runtime, so the thin client can read a query as the engine does. Pinned
// here: a primitive goes through the core's constant and comes back the same engine value; a value a run put there
// (a reference) is held and comes back as the same object; the primitives' names the parser reads are the ones the
// engine registers; a text that does not read is still the engine's syntax refusal, with its place.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/query/queryLiteral.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryException.h"
#include "backend/system/value/valueArray.h"
#include "core/query/queryRefusal.h"
#include "core/description/typeDescription.h"

TEST(QueryLiteral, APrimitiveIsWrittenAsTextAndReadBackTheSame)
{
	const ibValue samples[] = {
		ibValue(), ibValue(ibNumber(15.25)), ibValue(wxString(wxT("A-01"))), ibValue(true), ibValue(false),
	};
	for (const ibValue& value : samples) {
		const ibQueryLiteral literal = ibQueryLiteralOf(value);
		EXPECT_FALSE(literal.GetHeld()) << value.GetString().ToStdString();
		EXPECT_EQ(value.GetType(), literal.GetType());
		EXPECT_EQ(0, value.CompareValueLS(ibQueryLiteralValue(literal))) << value.GetString().ToStdString();
	}

	ibQueryLiteral null;
	null.SetType(ibValueTypes::TYPE_NULL);
	EXPECT_EQ(ibValueTypes::TYPE_NULL, ibQueryLiteralValue(null).GetType());
}

TEST(QueryLiteral, AValueARunPutThereIsHeldAndComesBackAsItself)
{
	ibValueArray* array = new ibValueArray();
	array->Add(ibValue(1));
	const ibValue value(static_cast<ibValue*>(array));

	const ibQueryLiteral literal = ibQueryLiteralOf(value);
	ASSERT_TRUE(literal.GetHeld());
	const ibValue back = ibQueryLiteralValue(literal);
	EXPECT_EQ(value.GetRef(), back.GetRef()) << "the same object, not a copy of it";
}

TEST(QueryLiteral, ThePrimitivesAreNamedAsTheEngineRegistersThem)
{
	for (const ibValueTypes vt : { ibValueTypes::TYPE_EMPTY, ibValueTypes::TYPE_BOOLEAN, ibValueTypes::TYPE_NUMBER,
		ibValueTypes::TYPE_DATE, ibValueTypes::TYPE_STRING, ibValueTypes::TYPE_NULL })
		EXPECT_EQ(ibValue::GetNameObjectFromVT(vt), wxString(GetNameByVT(vt))) << static_cast<int>(vt);
	EXPECT_EQ(nullptr, GetNameByVT(ibValueTypes::TYPE_VALUE));
}

TEST(QueryLiteral, ACastIsWrittenAndReadInThoseNames)
{
	const ibQueryAstExprPtr expr = ibQueryParser().ParseExpression(wxT("CAST(a AS Number(15, 2))"));
	ASSERT_TRUE(expr);
	ibTypeDescription type;
	ASSERT_TRUE(ibQueryCastType(*expr, type));
	EXPECT_EQ(GetIDByVT(ibValueTypes::TYPE_NUMBER), type.GetFirstClsid());
	EXPECT_EQ(15u, type.GetPrecision());
	EXPECT_EQ(2u, type.GetScale());

	const ibQueryAstExprPtr made = ibQueryMakeCast(ibQueryAstExpr::Make(ibQueryAstExprKind::Column), type);
	ASSERT_TRUE(made);
	ASSERT_EQ(1u, made->m_path.size());
	EXPECT_EQ(wxString(wxT("Number")), made->m_path.front());
	ASSERT_EQ(2u, made->m_args.size());
	EXPECT_EQ(15, made->m_args[0]->m_literal.GetNumber().ToInt());
}

TEST(QueryRefusal, ATextThatDoesNotReadIsTheEnginesSyntaxRefusalWithItsPlace)
{
	try {
		ibQueryParser().Parse(wxT("SELECT a FROM\n  WHERE"));
		FAIL() << "the text does not read";
	}
	catch (const ibBackendQuerySyntaxException& e) {
		EXPECT_EQ(2u, e.GetLine());
	}

	try {
		ibQueryRefusal::Syntax(3, 4, wxT("refused"));
		FAIL() << "a refusal does not return";
	}
	catch (const ibBackendQuerySyntaxException& e) {
		EXPECT_EQ(3u, e.GetLine());
		EXPECT_EQ(4u, e.GetColumn());
	}
}
