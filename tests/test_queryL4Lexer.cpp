// L4-1 text query language — lexer tests (queryLexer.{h,cpp}).
//
// Pure tokenization: no database, no appData. Confirms keyword classification
// (against the QUERY keyword table, separate from the script lexer), identifier
// case preservation, &parameters, glued multi-char operators, literals, and
// Cyrillic identifiers (metaobject / attribute names).

#include <gtest/gtest.h>

#include "backend/query/queryLexer.h"

namespace {

std::vector<ibQueryToken> Lex(const wxString& text)
{
	ibQueryLexer lexer;
	return lexer.Tokenize(text);
}

} // namespace

TEST(QueryL4Lexer, BasicSelect_KindsAndKeywords)
{
	const auto t = Lex(wxT("SELECT Code, Name FROM Catalog.Products WHERE Code = \"A-01\""));

	ASSERT_GE(t.size(), 13u);
	size_t i = 0;
	EXPECT_TRUE(t[i++].IsKeyword(ibQueryKeyword::Select));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Code"));
	EXPECT_TRUE(t[i++].IsPunct(wxT(',')));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Name"));
	EXPECT_TRUE(t[i++].IsKeyword(ibQueryKeyword::From));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Catalog"));
	EXPECT_TRUE(t[i++].IsPunct(wxT('.')));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Products"));
	EXPECT_TRUE(t[i++].IsKeyword(ibQueryKeyword::Where));
	EXPECT_EQ(t[i++].m_kind, ibQueryTokenKind::Ident);  // Code
	EXPECT_TRUE(t[i++].IsOp(wxT("=")));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::String); EXPECT_EQ(t[i++].m_literal.GetString(), wxT("A-01"));
	EXPECT_TRUE(t.back().IsEnd());
}

TEST(QueryL4Lexer, KeywordsAreCaseInsensitive_IdentifiersPreserveCase)
{
	const auto t = Lex(wxT("select Foo from Catalog.Bar"));
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Select));   // lowercase keyword still recognized
	EXPECT_EQ(t[1].m_kind, ibQueryTokenKind::Ident);
	EXPECT_EQ(t[1].m_text, wxT("Foo"));                    // original case kept
	EXPECT_TRUE(t[2].IsKeyword(ibQueryKeyword::From));
}

TEST(QueryL4Lexer, Parameter_AmpersandName)
{
	const auto t = Lex(wxT("WHERE Store = &Warehouse"));
	ASSERT_GE(t.size(), 4u);
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Where));
	EXPECT_EQ(t[1].m_kind, ibQueryTokenKind::Ident);
	EXPECT_TRUE(t[2].IsOp(wxT("=")));
	EXPECT_EQ(t[3].m_kind, ibQueryTokenKind::Param);
	EXPECT_EQ(t[3].m_text, wxT("Warehouse"));
}

TEST(QueryL4Lexer, GluedMultiCharOperators)
{
	const auto t = Lex(wxT("a <= b >= c <> d != e < f"));
	// idents at even positions, operators between
	EXPECT_TRUE(t[1].IsOp(wxT("<=")));
	EXPECT_TRUE(t[3].IsOp(wxT(">=")));
	EXPECT_TRUE(t[5].IsOp(wxT("<>")));
	EXPECT_TRUE(t[7].IsOp(wxT("<>")));   // != normalizes to <>
	EXPECT_TRUE(t[9].IsOp(wxT("<")));
}

TEST(QueryL4Lexer, NumberAndStarLiterals)
{
	const auto t = Lex(wxT("SELECT * WHERE Qty > 100.5"));
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Select));
	EXPECT_TRUE(t[1].IsOp(wxT("*")));
	EXPECT_TRUE(t[2].IsKeyword(ibQueryKeyword::Where));
	EXPECT_EQ(t[3].m_kind, ibQueryTokenKind::Ident);
	EXPECT_TRUE(t[4].IsOp(wxT(">")));
	EXPECT_EQ(t[5].m_kind, ibQueryTokenKind::Number);
}

TEST(QueryL4Lexer, ArithmeticOperatorsIncludingModulo)
{
	// + - * / % are all Op tokens (% must NOT fall through to Punct).
	const auto t = Lex(wxT("a + b - c * d / e % f"));
	EXPECT_TRUE(t[1].IsOp(wxT("+")));
	EXPECT_TRUE(t[3].IsOp(wxT("-")));
	EXPECT_TRUE(t[5].IsOp(wxT("*")));
	EXPECT_TRUE(t[7].IsOp(wxT("/")));
	EXPECT_TRUE(t[9].IsOp(wxT("%")));
}

TEST(QueryL4Lexer, UnionAndCaseKeywords)
{
	const auto t = Lex(wxT("UNION ALL CASE WHEN THEN ELSE END"));
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Union));
	EXPECT_TRUE(t[1].IsKeyword(ibQueryKeyword::All));
	EXPECT_TRUE(t[2].IsKeyword(ibQueryKeyword::Case));
	EXPECT_TRUE(t[3].IsKeyword(ibQueryKeyword::When));
	EXPECT_TRUE(t[4].IsKeyword(ibQueryKeyword::Then));
	EXPECT_TRUE(t[5].IsKeyword(ibQueryKeyword::Else));
	EXPECT_TRUE(t[6].IsKeyword(ibQueryKeyword::End));
}

// NOTE: non-ASCII metaobject / attribute identifiers are
// validated end-to-end by the runtime codeRunner smoke (step 5), where the real
// config drives them — kept out of this gated golden test to avoid source-encoding
// fragility (MSVC reads non-BOM sources in the ANSI codepage unless /utf-8 is set).
