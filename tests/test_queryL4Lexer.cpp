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

// COMMENTS. The query language takes its comment form from the shared lexer primitives
// (ibTranslateCode::SkipSpaces): `//` to end of line, and nothing else. That is inherited rather
// than written here, which is exactly why it is worth pinning — nothing in the query layer says
// so, and the pane that displays a query has to agree with it.
TEST(QueryL4Lexer, LineComment_IsSkipped)
{
	const auto t = Lex(wxT("SELECT Code // the article number\nFROM Catalog.Products"));

	ASSERT_GE(t.size(), 6u);
	size_t i = 0;
	EXPECT_TRUE(t[i++].IsKeyword(ibQueryKeyword::Select));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Code"));
	EXPECT_TRUE(t[i++].IsKeyword(ibQueryKeyword::From));   // the comment produced no token at all
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Catalog"));
	EXPECT_TRUE(t[i++].IsPunct(wxT('.')));
	EXPECT_EQ(t[i].m_kind, ibQueryTokenKind::Ident);  EXPECT_EQ(t[i++].m_text, wxT("Products"));
	EXPECT_TRUE(t.back().IsEnd());
}

TEST(QueryL4Lexer, CommentOnItsOwnLine_AndAtTheVeryEnd)
{
	// A whole line of comment, and one with no newline after it — the second is the case that walks
	// off the end of the buffer rather than off the end of a line.
	const auto t = Lex(wxT("// what this query is for\nSELECT Code FROM Catalog.Products // trailing"));

	ASSERT_GE(t.size(), 6u);
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Select));
	EXPECT_EQ(t[1].m_text, wxT("Code"));
	EXPECT_TRUE(t[2].IsKeyword(ibQueryKeyword::From));
	EXPECT_TRUE(t.back().IsEnd());
}

TEST(QueryL4Lexer, SlashesInsideAStringAreNotAComment)
{
	// The case a comment-skipper usually gets wrong: whitespace skipping stops AT the quote, and the
	// string primitive then reads to its closing quote — so the slashes never reach the skipper.
	// If they ever did, the rest of the query would silently vanish and the failure would look like
	// a parse error somewhere else entirely.
	const auto t = Lex(wxT("SELECT Code FROM Catalog.Products WHERE Site = \"http://example.org\" AND Code = \"A-01\""));

	ASSERT_GE(t.size(), 12u);
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Select));

	bool sawUrl = false, sawTrailingCode = false;
	for (const ibQueryToken& tok : t) {
		if (tok.m_kind == ibQueryTokenKind::String && tok.m_literal.GetString() == wxT("http://example.org"))
			sawUrl = true;
		if (tok.m_kind == ibQueryTokenKind::String && tok.m_literal.GetString() == wxT("A-01"))
			sawTrailingCode = true;   // everything AFTER the slashes still tokenized
	}
	EXPECT_TRUE(sawUrl);
	EXPECT_TRUE(sawTrailingCode);
	EXPECT_TRUE(t.back().IsEnd());
}

TEST(QueryL4Lexer, DoubleDashIsNotAComment)
{
	// Scintilla's SQL lexer colours `--` as a comment; this language does not have that form, and the
	// pane is styled with the C lexer for exactly this reason. Two minus signs are two operators.
	const auto t = Lex(wxT("SELECT a -- b"));
	EXPECT_TRUE(t[0].IsKeyword(ibQueryKeyword::Select));
	EXPECT_EQ(t[1].m_kind, ibQueryTokenKind::Ident);
	EXPECT_TRUE(t[2].IsOp(wxT("-")));
	EXPECT_TRUE(t[3].IsOp(wxT("-")));
	EXPECT_EQ(t[4].m_kind, ibQueryTokenKind::Ident);
}

// NOTE: non-ASCII metaobject / attribute identifiers are
// validated end-to-end by the runtime codeRunner smoke (step 5), where the real
// config drives them — kept out of this gated golden test to avoid source-encoding
// fragility (MSVC reads non-BOM sources in the ANSI codepage unless /utf-8 is set).
