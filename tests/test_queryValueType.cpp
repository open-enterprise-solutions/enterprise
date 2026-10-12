// VALUETYPE / TYPE, including the 1C spellings ТИПЗНАЧЕНИЯ / ТИП and the primitive
// names STRING / СТРОКА (and number, date, boolean). A type literal must not ask the
// ctor registry under the query's own word: that throws "Object 'СТРОКА' is not exist".

#include <gtest/gtest.h>

#include <clocale>

#include "backend/query/dbTableProvider.h"
#include "backend/query/queryParser.h"
#include "core/types.h"

namespace {

// The query lexer classifies letters with iswalpha, which in the C locale knows
// only ASCII. A running base has a UTF-8 locale (that is how СТРОКА reaches the
// type lookup at all). The test process does not, so this asks for one.
void LettersBeyondAscii()
{
	if (std::setlocale(LC_CTYPE, "en_US.UTF-8") == nullptr
	    && std::setlocale(LC_CTYPE, "en_US.utf8") == nullptr
	    && std::setlocale(LC_CTYPE, "C.UTF-8") == nullptr)
		std::setlocale(LC_CTYPE, "C.utf8");
}

ibQuerySelectPtr Parse(const wxString& text)
{
	return ibQueryParser().Parse(text);
}

const ibQueryAstExpr* TypeCall(const ibQuerySelect& sel)
{
	if (sel.m_where == nullptr || sel.m_where->m_kind != ibQueryAstExprKind::Compare)
		return nullptr;
	const ibQueryAstExpr* rhs = sel.m_where->m_rhs.get();
	if (rhs == nullptr || rhs->m_kind != ibQueryAstExprKind::ScalarCall || rhs->m_scalar != ibQueryScalarFn::Type)
		return nullptr;
	return rhs;
}

void ExpectPrimitive(const wxString& name, ibClassID clsid)
{
	ibClassID got = 0;
	ASSERT_TRUE(ibTypeValueClsid(ibTypeValueByName(name), got)) << name;
	EXPECT_EQ(got, clsid) << name;
}

} // namespace

TEST(QueryValueType, EnglishCallParsesAndNamesAString)
{
	const ibQuerySelectPtr sel = Parse(wxT(
		"SELECT PRESENTATION(X.V) AS V FROM Catalog.T AS X "
		"WHERE VALUETYPE(X.V) = TYPE(STRING)"));
	ASSERT_NE(nullptr, sel);
	const ibQueryAstExpr* type = TypeCall(*sel);
	ASSERT_NE(nullptr, type);
	ASSERT_FALSE(type->m_args.empty());
	ASSERT_FALSE(type->m_args.front()->m_path.empty());
	EXPECT_EQ(type->m_args.front()->m_path.back(), wxT("STRING"));

	ExpectPrimitive(wxT("STRING"), g_valueStringCLSID);
	ExpectPrimitive(wxT("String"), g_valueStringCLSID);
	ExpectPrimitive(wxT("NUMBER"), g_valueNumberCLSID);
	ExpectPrimitive(wxT("DATE"), g_valueDateCLSID);
	ExpectPrimitive(wxT("BOOLEAN"), g_valueBooleanCLSID);
}

TEST(QueryValueType, RussianCallParsesAndNamesAString)
{
	LettersBeyondAscii();
	const ibQuerySelectPtr sel = Parse(wxT(
		"SELECT PRESENTATION(X.Значение) AS Значение FROM Catalog.T AS X "
		"WHERE ТИПЗНАЧЕНИЯ(X.Значение) = ТИП(СТРОКА)"));
	ASSERT_NE(nullptr, sel);
	ASSERT_NE(nullptr, sel->m_where);
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Compare);
	ASSERT_NE(sel->m_where->m_lhs, nullptr);
	EXPECT_EQ(sel->m_where->m_lhs->m_scalar, ibQueryScalarFn::ValueType);
	const ibQueryAstExpr* type = TypeCall(*sel);
	ASSERT_NE(nullptr, type);
	ASSERT_FALSE(type->m_args.empty());
	ASSERT_FALSE(type->m_args.front()->m_path.empty());
	EXPECT_EQ(type->m_args.front()->m_path.back(), wxT("СТРОКА"));

	ExpectPrimitive(wxT("СТРОКА"), g_valueStringCLSID);
	ExpectPrimitive(wxT("Строка"), g_valueStringCLSID);
	ExpectPrimitive(wxT("ЧИСЛО"), g_valueNumberCLSID);
	ExpectPrimitive(wxT("ДАТА"), g_valueDateCLSID);
	ExpectPrimitive(wxT("БУЛЕВО"), g_valueBooleanCLSID);
}

TEST(QueryValueType, AnUnknownNameIsNotAType)
{
	EXPECT_TRUE(ibTypeValueByName(wxT("NotARealType")).IsEmpty());
}
