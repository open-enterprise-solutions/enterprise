// =============================================================================
// What the query's parser hands back is DATA the engine reads at its own door: a constant as the text writes one
// (ibVariant, core/query/queryAST.h) and a refusal shaped like the translator's (ibQueryRefusal,
// core/query/queryRefusal.h).
//
// Pinned here: a constant the text wrote is the core's ibVariant, Boolean and NULL among them, and the engine
// reads it back as its own value; the primitives' names are one table, the one the engine registers by; a text that
// does not read is a refusal with its place, and the engine's door raises it as ibBackendQuerySyntaxException with
// the same place.
// =============================================================================

#include <gtest/gtest.h>

#include "core/query/queryParser.h"
#include "backend/query/queryException.h"
#include "backend/compiler/value.h"
#include "core/variant.h"
#include "core/query/queryRefusal.h"
#include "core/description/typeDescription.h"

namespace {

ibQueryAstExprPtr Literal(const ibString& text)
{
	const ibQueryAstExprPtr expr = ibQueryParser().ParseExpression(text);
	EXPECT_TRUE(expr && expr->m_kind == ibQueryAstExprKind::Literal) << text.ToStdString();
	return expr;
}

} // namespace

TEST(QueryCoreData, TheTextsConstantsAreLiteralsTheEngineReadsBack)
{
	struct { const wxChar* text; ibValueTypes type; } const samples[] = {
		{ wxT("15.25"), ibValueTypes::TYPE_NUMBER }, { wxT("\"A-01\""), ibValueTypes::TYPE_STRING },
		{ wxT("TRUE"), ibValueTypes::TYPE_BOOLEAN }, { wxT("FALSE"), ibValueTypes::TYPE_BOOLEAN },
		{ wxT("NULL"), ibValueTypes::TYPE_NULL }, { wxT("UNDEFINED"), ibValueTypes::TYPE_EMPTY },
	};
	for (const auto& sample : samples) {
		const ibQueryAstExprPtr expr = Literal(sample.text);
		ASSERT_TRUE(expr) << ibString(sample.text).ToStdString();
		EXPECT_EQ(sample.type, expr->m_literal.GetType());
		EXPECT_EQ(sample.type, ibValue(expr->m_literal).GetType());
	}
	EXPECT_TRUE(ibValue(Literal(wxT("TRUE"))->m_literal).GetBoolean());
	EXPECT_FALSE(ibValue(Literal(wxT("FALSE"))->m_literal).GetBoolean());
	EXPECT_EQ(15, ibValue(Literal(wxT("15"))->m_literal).GetNumber().ToInt());
	EXPECT_EQ(ibString(wxT("A-01")), ibString(ibValue(Literal(wxT("\"A-01\""))->m_literal).GetString()));
}

TEST(QueryCoreData, ThePrimitivesHaveOneTableOfNames)
{
	for (const ibValueTypes vt : { ibValueTypes::TYPE_EMPTY, ibValueTypes::TYPE_BOOLEAN, ibValueTypes::TYPE_NUMBER,
		ibValueTypes::TYPE_DATE, ibValueTypes::TYPE_STRING, ibValueTypes::TYPE_NULL }) {
		ASSERT_NE(nullptr, GetNameObjectFromVT(vt)) << static_cast<int>(vt);
		EXPECT_EQ(ibString(GetNameObjectFromVT(vt)), ibValue::GetNameObjectFromVT(vt));
		// …and it is the name the class is REGISTERED by: the registry finds the primitive's clsid under it.
		EXPECT_EQ(GetIDByVT(vt), ibValue::GetIDObjectFromString(GetNameObjectFromVT(vt))) << GetNameObjectFromVT(vt);
	}
	EXPECT_EQ(nullptr, GetNameObjectFromVT(ibValueTypes::TYPE_VALUE));
}

TEST(QueryCoreData, ACastIsWrittenAndReadInThoseNames)
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
	EXPECT_EQ(ibString(wxT("Number")), made->m_path.front());
	ASSERT_EQ(2u, made->m_args.size());
	EXPECT_EQ(15, made->m_args[0]->m_literal.GetNumber().ToInt());
}

TEST(QueryCoreData, ARefusalIsDataAndTheEnginesDoorRaisesItsOwn)
{
	try {
		ibQueryParser().Parse(wxT("SELECT a FROM\n  WHERE"));
		FAIL() << "the text does not read";
	}
	catch (const ibQueryRefusal& refusal) {
		EXPECT_EQ(2u, refusal.m_currLine);
		EXPECT_FALSE(refusal.GetErrorDescription().IsEmpty());
		try {
			ibBackendQuerySyntaxException::Raise(refusal);
			FAIL() << "the door does not return";
		}
		catch (const ibBackendQuerySyntaxException& e) {
			EXPECT_EQ(refusal.m_currLine, e.GetLine());
			EXPECT_EQ(refusal.m_currPos, e.GetColumn());
			EXPECT_EQ(refusal.GetErrorDescription(), e.GetErrorDescription());
		}
	}

	// A lexer refusal is the same data.
	try {
		ibQueryParser().Parse(wxT("SELECT DATETIME(2026, 13, 40) AS d"));
	}
	catch (const ibQueryRefusal& refusal) {
		EXPECT_GE(refusal.m_currLine, 1u);
	}
	catch (...) {
		ADD_FAILURE() << "a text that does not read is an ibQueryRefusal";
	}
}
