// =============================================================================
// What the query's parser hands back is DATA the engine reads at its own door: a constant in one slot
// (ibAnyValuePtr<ibAnyValue>, core/query/queryAST.h) and a refusal shaped like the translator's
// (ibQueryRefusal, core/query/queryRefusal.h).
//
// Pinned here: a constant the text wrote is the core's ibLexValue, Boolean and NULL among them, and the engine
// reads it back as its own value; a value a run put there (a reference) comes back as the same object; the
// primitives' names are one table, the one the engine registers by; a text that does not read is a refusal with its
// place, and the engine's door raises it as ibBackendQuerySyntaxException with the same place.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/query/queryParser.h"
#include "backend/query/queryException.h"
#include "backend/system/value/valueArray.h"
#include "core/compiler/lexValue.h"
#include "core/query/queryRefusal.h"
#include "core/description/typeDescription.h"

namespace {

ibQueryAstExprPtr Literal(const wxString& text)
{
	const ibQueryAstExprPtr expr = ibQueryParser().ParseExpression(text);
	EXPECT_TRUE(expr && expr->m_kind == ibQueryAstExprKind::Literal) << text.ToStdString();
	return expr;
}

} // namespace

TEST(QueryCoreData, TheTextsConstantsAreLexValuesTheEngineReadsBack)
{
	struct { const wxChar* text; ibValueTypes type; } const samples[] = {
		{ wxT("15.25"), ibValueTypes::TYPE_NUMBER }, { wxT("\"A-01\""), ibValueTypes::TYPE_STRING },
		{ wxT("TRUE"), ibValueTypes::TYPE_BOOLEAN }, { wxT("FALSE"), ibValueTypes::TYPE_BOOLEAN },
		{ wxT("NULL"), ibValueTypes::TYPE_NULL }, { wxT("UNDEFINED"), ibValueTypes::TYPE_EMPTY },
	};
	for (const auto& sample : samples) {
		const ibQueryAstExprPtr expr = Literal(sample.text);
		ASSERT_TRUE(expr && expr->m_literal) << wxString(sample.text).ToStdString();
		EXPECT_NE(nullptr, dynamic_cast<const ibLexValue*>(&*expr->m_literal)) << "the parser's own constant";
		EXPECT_EQ(sample.type, expr->m_literal->GetType());
		EXPECT_EQ(sample.type, ibValue::FromAnyValue(expr->m_literal).GetType());
	}
	EXPECT_TRUE(ibValue::FromAnyValue(Literal(wxT("TRUE"))->m_literal).GetBoolean());
	EXPECT_FALSE(ibValue::FromAnyValue(Literal(wxT("FALSE"))->m_literal).GetBoolean());
	EXPECT_EQ(15, ibValue::FromAnyValue(Literal(wxT("15"))->m_literal).GetNumber().ToInt());
	EXPECT_EQ(wxString(wxT("A-01")), wxString(ibValue::FromAnyValue(Literal(wxT("\"A-01\""))->m_literal).GetString()));
}

TEST(QueryCoreData, AValueARunPutThereComesBackAsItself)
{
	ibValueArray* array = new ibValueArray();
	array->Add(ibValue(1));
	const ibValue value(static_cast<ibValue*>(array));

	ibQueryAstExprPtr expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
	expr->m_literal = value.ToAnyValue();
	const ibValue back = ibValue::FromAnyValue(expr->m_literal);
	EXPECT_EQ(value.GetRef(), back.GetRef()) << "the same object, not a copy of it";
	EXPECT_EQ(ibValueTypes::TYPE_EMPTY, ibValue::FromAnyValue(ibAnyValuePtr<ibAnyValue>()).GetType());
}

TEST(QueryCoreData, ThePrimitivesHaveOneTableOfNames)
{
	for (const ibValueTypes vt : { ibValueTypes::TYPE_EMPTY, ibValueTypes::TYPE_BOOLEAN, ibValueTypes::TYPE_NUMBER,
		ibValueTypes::TYPE_DATE, ibValueTypes::TYPE_STRING, ibValueTypes::TYPE_NULL }) {
		ASSERT_NE(nullptr, GetNameObjectFromVT(vt)) << static_cast<int>(vt);
		EXPECT_EQ(wxString(GetNameObjectFromVT(vt)), ibValue::GetNameObjectFromVT(vt));
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
	EXPECT_EQ(wxString(wxT("Number")), made->m_path.front());
	ASSERT_EQ(2u, made->m_args.size());
	ASSERT_TRUE(made->m_args[0]->m_literal);
	EXPECT_EQ(15, made->m_args[0]->m_literal->GetNumber().ToInt());
}

TEST(QueryCoreData, ARefusalIsDataAndTheEnginesDoorRaisesItsOwn)
{
	try {
		ibQueryParser().Parse(wxT("SELECT a FROM\n  WHERE"));
		FAIL() << "the text does not read";
	}
	catch (const ibQueryRefusal& refusal) {
		EXPECT_EQ(2u, refusal.m_currLine);
		EXPECT_FALSE(refusal.m_errorDesc.IsEmpty());
		try {
			ibBackendQuerySyntaxException::Raise(refusal);
			FAIL() << "the door does not return";
		}
		catch (const ibBackendQuerySyntaxException& e) {
			EXPECT_EQ(refusal.m_currLine, e.GetLine());
			EXPECT_EQ(refusal.m_currPos, e.GetColumn());
			EXPECT_EQ(refusal.m_errorDesc, e.GetErrorDescription());
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
