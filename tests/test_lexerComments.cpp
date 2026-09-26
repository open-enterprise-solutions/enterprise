// =============================================================================
// A long run of `//` comment lines must not take the process down.
//
// ibTranslateCode::SkipSpaces skipped a comment line and then CALLED ITSELF for the next line - so a
// block of n consecutive comment lines was n frames deep. A module that carries a big block of
// commented-out code (a migrated configuration has them) overflowed the stack of the thread that
// compiles it: enterprise died with a crash report whose faulting thread was 511+ frames of
// ibTranslateCode::SkipSpaces, one per line (2026-09-21).
//
// The number of lines the process survives depends on the size of the thread's stack and of the
// frame, which vary by platform and build - so the test does not guess a "safe" length. It uses far
// more lines than any stack could hold as frames of this function (two million; even a 32-byte frame
// would need 64 MB) and asks the only question that matters: does lexing finish, and does the code
// AFTER the comments still lex.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"

namespace {

wxString CommentLines(size_t count, const wxString& line)
{
	wxString out;
	out.reserve(count * (line.length() + 1));
	for (size_t i = 0; i < count; ++i) {
		out += line;
		out += wxT('\n');
	}
	return out;
}

} // namespace

// Two million comment lines, then a statement: it lexes, and the statement is still there.
TEST(LexerComments, ALongRunOfCommentLines_DoesNotOverflowTheStack)
{
	const wxString source = CommentLines(2000000, wxT("// commented out")) + wxT("a = 1;");

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(source);
	ASSERT_TRUE(cc.PrepareLexem());
	EXPECT_GE(cc.GetLexemCount(), 4u) << "the statement after the comments must still lex";
}

// The same with Windows line ends and indented comments - the paths that reach the recursion differently.
TEST(LexerComments, CommentLinesWithCarriageReturnsAndIndent_AlsoSurvive)
{
	const wxString source = CommentLines(1000000, wxT("\t  // a line\r")) + wxT("a = 1;");

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(source);
	ASSERT_TRUE(cc.PrepareLexem());
	EXPECT_GE(cc.GetLexemCount(), 4u);
}

// A comment on the last line, with no line end after it, is a comment and nothing else.
TEST(LexerComments, ACommentOnTheLastLine_WithNoLineEnd_IsFine)
{
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT("a = 1;\n// the end"));
	ASSERT_TRUE(cc.PrepareLexem());
	EXPECT_GE(cc.GetLexemCount(), 4u);
}

// Comments do not change what surrounds them.
TEST(LexerComments, CommentsBetweenStatements_LeaveTheStatementsAlone)
{
	ibCompileCode with(wxT("test"), wxT("memory"), false);
	with.Load(wxT("a = 1;\n// one\n// two\n\n   // three\nb = 2;"));
	ASSERT_TRUE(with.PrepareLexem());

	ibCompileCode without(wxT("test"), wxT("memory"), false);
	without.Load(wxT("a = 1;\nb = 2;"));
	ASSERT_TRUE(without.PrepareLexem());

	EXPECT_EQ(with.GetLexemCount(), without.GetLexemCount());
}

// ---- what the walk reports AFTER comments: the line, and the character/UTF-8 offset ----------------
//
// SkipSpaces also keeps the line counter and the UTF-8 offset, and the recursion it lost carried both
// through the comment's line end. These pin them down: the same numbers the recursive version gave.

namespace {

const ibLexem* FindIdentifier(const ibCompileCode& cc, const wxString& name)
{
	for (const ibLexem& lexem : cc.GetLexems())
		if (lexem.m_lexType == IDENTIFIER && lexem.m_strData.CmpNoCase(name) == 0)   // identifiers are stored upper-cased
			return &lexem;
	return nullptr;
}

} // namespace

TEST(LexerComments, TheLineAfterSeveralComments_IsCountedRight)
{
	// four line ends of every kind before `abc`: \n, \r\n (two characters, one line each way it is counted
	// today), and a tab-indented comment
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT("// one\n// two\n\t// three\n\nabc = 1;"));
	ASSERT_TRUE(cc.PrepareLexem());
	const ibLexem* abc = FindIdentifier(cc, wxT("abc"));
	ASSERT_NE(abc, nullptr);
	EXPECT_EQ(abc->GetLine(), 5u) << "abc is on the fifth line";
}

TEST(LexerComments, TheOffsetsAfterAComment_PointAtTheStatement_InCharactersAndInUtf8)
{
	const wxString source = wxString::FromUTF8("// \xD1\x82\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82\n// \xD0\xB5\xD1\x89\xD1\x91\nabc = 1;");   // two Cyrillic comment lines
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(source);
	ASSERT_TRUE(cc.PrepareLexem());
	const ibLexem* abc = FindIdentifier(cc, wxT("abc"));
	ASSERT_NE(abc, nullptr);

	const size_t character = source.Find(wxT("abc"));
	EXPECT_EQ(abc->m_numString, character) << "character offset";
	const wxString before = source.Left(character);
	EXPECT_EQ(abc->m_numUtf8String, static_cast<unsigned int>(before.ToUTF8().length())) << "UTF-8 byte offset";
}
