// =============================================================================
// Where a module's text folds — core/compiler/scriptFoldCode.h.
//
// The fold manager was a private class nested in both code editors (the desktop's and the thin client's). It
// moved to the core as is; these pin what the editors read of it: the level each line gets (UpdateFoldLevel)
// and the mask the auto-indent decodes (GetFoldMask), in both syntaxes.
// =============================================================================

#include <gtest/gtest.h>

#include "core/compiler/scriptFoldCode.h"
#include "core/compiler/translateCode.h"
#include "core/compiler/codeDef.h"

namespace {

// The text read the way an editor reads it: a refusal of the translator leaves the lexems read so far.
void Read(ibTranslateCode& tc, const wxString& text)
{
	tc.Load(text);
	try {
		tc.Translate();
	}
	catch (...) {
	}
}

// The line the first keyword `key` stands on, as the lexems number it.
int LineOf(const ibTranslateCode& tc, int key, int nth = 0)
{
	for (size_t i = 0; i < tc.GetLexCount(); ++i) {
		const ibLexem& lex = tc.GetLexAt(i);
		if (lex.m_lexType == KEYWORD && lex.m_numData == key && nth-- == 0)
			return lex.m_numLine;
	}
	return -1;
}

int LineOfDelimiter(const ibTranslateCode& tc, wxUniChar c)
{
	for (size_t i = 0; i < tc.GetLexCount(); ++i) {
		const ibLexem& lex = tc.GetLexAt(i);
		if (lex.m_lexType == DELIMITER && lex.m_numData == static_cast<int>(c))
			return lex.m_numLine;
	}
	return -1;
}

constexpr int Base   = ibFoldCode::FoldLevelBase;
constexpr int Header = ibFoldCode::FoldLevelHeader;
constexpr int White  = ibFoldCode::FoldLevelWhite;
constexpr int Else   = ibFoldCode::FoldLevelElse;

} // namespace

TEST(FoldLevel, NothingIsComputedUntilTheTextChanged)
{
	ibTranslateCode tc(CODE_VES);
	Read(tc, wxT("Procedure P()\n\ta = 1;\nEndProcedure\n"));

	ibFoldCode fp;
	std::vector<int> levels{ 7 };
	EXPECT_FALSE(fp.UpdateFoldLevel(tc, 3, levels));
	EXPECT_EQ(std::vector<int>{ 7 }, levels);

	fp.RecalcFoldLevel();
	EXPECT_TRUE(fp.UpdateFoldLevel(tc, 3, levels));
	EXPECT_FALSE(fp.UpdateFoldLevel(tc, 3, levels)) << "one recalculation per change";
}

TEST(FoldLevel, AProcedureFoldsFromItsHeaderToItsEnd)
{
	ibTranslateCode tc(CODE_VES);
	Read(tc, wxT("Procedure P()\n\ta = 1;\nEndProcedure\nb = 2;\n"));

	const int open  = LineOf(tc, KEY_PROCEDURE);
	const int close = LineOf(tc, KEY_ENDPROCEDURE);
	ASSERT_GE(open, 0);
	ASSERT_EQ(open + 2, close);

	ibFoldCode fp;
	fp.RecalcFoldLevel();
	std::vector<int> levels;
	ASSERT_TRUE(fp.UpdateFoldLevel(tc, close + 2, levels));
	ASSERT_GT(static_cast<int>(levels.size()), close + 1);

	EXPECT_EQ(Base | Header, levels[open]);
	EXPECT_EQ(Base + 1, levels[open + 1]);
	EXPECT_EQ((Base + 1) | White, levels[close]);
	EXPECT_EQ(Base, levels[close + 1]);

	// The indentation's view: the header at the outer level, the body one in, the end back out.
	EXPECT_EQ(Base | Header, fp.GetFoldMask(open));
	EXPECT_EQ(Base + 1, fp.GetFoldMask(open + 1));
	EXPECT_EQ((Base + 1) | White, fp.GetFoldMask(close));
	EXPECT_EQ(Base, fp.GetFoldMask(close + 1));
}

TEST(FoldLevel, ElseIsAMarkInsideTheFold)
{
	ibTranslateCode tc(CODE_VES);
	Read(tc, wxT("If a Then\n\tb = 1;\nElse\n\tb = 2;\nEndIf;\n"));

	const int elseLine = LineOf(tc, KEY_ELSE);
	ASSERT_GE(elseLine, 0);

	ibFoldCode fp;
	fp.RecalcFoldLevel();
	std::vector<int> levels;
	ASSERT_TRUE(fp.UpdateFoldLevel(tc, elseLine + 3, levels));
	EXPECT_EQ((Base + 1) | Else, fp.GetFoldMask(elseLine));
	EXPECT_EQ(Base + 1, levels[elseLine]);
}

TEST(FoldLevel, CesFoldsByBracesNotByKeywords)
{
	ibTranslateCode tc(CODE_CES);
	Read(tc, wxT("Procedure P()\n{\n\ta = 1;\n}\n"));

	const int open  = LineOfDelimiter(tc, '{');
	const int close = LineOfDelimiter(tc, '}');
	ASSERT_GE(open, 0);
	ASSERT_EQ(open + 2, close);

	ibFoldCode fp;
	fp.RecalcFoldLevel();
	std::vector<int> levels;
	ASSERT_TRUE(fp.UpdateFoldLevel(tc, close + 1, levels));

	EXPECT_EQ(Base, levels[open - 1]) << "the keyword opens no fold in CES";
	EXPECT_EQ(Base | Header, levels[open]);
	EXPECT_EQ(Base + 1, levels[open + 1]);
	EXPECT_EQ((Base + 1) | White, levels[close]);
}

TEST(FoldLevel, AnOpenThatNeverClosesFoldsNothing)
{
	ibTranslateCode tc(CODE_VES);
	Read(tc, wxT("a = 1;\nIf a Then\n\tb = 1;\n\tb = 2;\n"));

	ibFoldCode fp;
	fp.RecalcFoldLevel();
	std::vector<int> levels;
	ASSERT_TRUE(fp.UpdateFoldLevel(tc, 4, levels));
	for (int line = 1; line <= 4; ++line)
		EXPECT_EQ(Base, levels[line]) << line;
}

TEST(FoldLevel, AOneLineQueryIsBalanced)
{
	ibTranslateCode tc(CODE_VES);
	Read(tc, wxT("a = from x in arr select x;\nb = 1;\n"));

	const int line = LineOf(tc, KEY_FROM);
	ASSERT_GE(line, 0);

	ibFoldCode fp;
	fp.RecalcFoldLevel();
	std::vector<int> levels;
	ASSERT_TRUE(fp.UpdateFoldLevel(tc, line + 2, levels));
	EXPECT_EQ(Base, fp.GetFoldMask(line));
	EXPECT_EQ(Base, fp.GetFoldMask(line + 1));
}
