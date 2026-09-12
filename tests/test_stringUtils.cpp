// =============================================================================
// OES Enterprise — stringUtils tests
//
// Covers the stringUtils namespace (backend/stringUtils.h): name validation,
// synonym generation, case folding, comparison, trimming, conversions and
// char predicates.
//
// CheckCorrectName / GenerateSynonym carry the Cyrillic handling fixed during
// the macOS port (commit f0d041c2: cp1251 byte ranges -> Unicode A..ya). The
// Cyrillic cases below are a direct regression guard for that change. They are
// built from Unicode CODE POINTS via the Cp() helper, so the source file stays
// pure ASCII and the assertions never depend on its encoding.
// =============================================================================

#include <gtest/gtest.h>
#include <initializer_list>
#include <map>
#include "backend/stringUtils.h"

using namespace stringUtils;

namespace {
// Build a wxString from Unicode code points — keeps this file ASCII-only.
wxString Cp(std::initializer_list<int> cps) {
    wxString s;
    for (int cp : cps) s += wxUniChar(cp);
    return s;
}
// Cyrillic fixtures (code points, no literals):
//   IMYA       = "Имя"        (I=0418, m=043C, ya=044F)
//   IMYATOVARA = "ИмяТовара"
//   IMYA_TOVARA= "Имя товара"
} // namespace

// ---------------------------------------------------------------------------
// CheckCorrectName — wxNOT_FOUND if every char is a valid identifier char,
// otherwise the index of the first offending char.
// ---------------------------------------------------------------------------

TEST(StringUtils, NameValidLatin) {
    EXPECT_EQ(CheckCorrectName(wxT("Valid_Name123")), wxNOT_FOUND);
}

TEST(StringUtils, NameValidCyrillic) {
    EXPECT_EQ(CheckCorrectName(Cp({0x0418, 0x043C, 0x044F})), wxNOT_FOUND);
}

TEST(StringUtils, NameEmptyIsValid) {
    EXPECT_EQ(CheckCorrectName(wxEmptyString), wxNOT_FOUND);
}

TEST(StringUtils, NameRejectsSpaceAtIndex) {
    EXPECT_EQ(CheckCorrectName(wxT("Bad Name")), 3);   // the space
}

TEST(StringUtils, NameRejectsPunctuation) {
    EXPECT_EQ(CheckCorrectName(wxT("a-b")), 1);        // the hyphen
}

// ---------------------------------------------------------------------------
// GenerateSynonym — split CamelCase on an uppercase letter, lower the tail.
// ---------------------------------------------------------------------------

TEST(StringUtils, SynonymSplitsLatinCamelCase) {
    EXPECT_EQ(GenerateSynonym(wxT("MyName")), wxT("My name"));
}

TEST(StringUtils, SynonymLeavesSingleWord) {
    EXPECT_EQ(GenerateSynonym(wxT("Product")), wxT("Product"));
}

TEST(StringUtils, SynonymSplitsCyrillicCamelCase) {
    // CamelCase split on a Cyrillic uppercase letter -> exercises the
    // upper->lower (+0x20) branch in GenerateSynonym.
    const wxString in   = Cp({0x0418,0x043C,0x044F,0x0422,0x043E,0x0432,0x0430,0x0440,0x0430});
    const wxString want = Cp({0x0418,0x043C,0x044F,0x0020,0x0442,0x043E,0x0432,0x0430,0x0440,0x0430});
    EXPECT_EQ(GenerateSynonym(in), want);
}

// ---------------------------------------------------------------------------
// Case folding + comparison
// ---------------------------------------------------------------------------

TEST(StringUtils, MakeUpperFoldsAndTrims) {
    EXPECT_EQ(MakeUpper(wxT("abc")), wxT("ABC"));
    EXPECT_EQ(MakeUpper(wxT("  abc  ")), wxT("ABC"));   // MakeUpper trims
}

TEST(StringUtils, CompareStringCaseInsensitiveByDefault) {
    EXPECT_TRUE(CompareString(wxT("ABC"), wxT("abc")));
    EXPECT_FALSE(CompareString(wxT("ABC"), wxT("abc"), /*case_sensitive*/true));
}

TEST(StringUtils, CompareStringLengthAndContent) {
    EXPECT_FALSE(CompareString(wxT("abc"), wxT("abcd")));   // length differs
    EXPECT_FALSE(CompareString(wxT("abc"), wxT("abd")));    // content differs
}

// ---------------------------------------------------------------------------
// ibCaseFoldLess — the ORDER that CompareString's equality belongs to, for a map
// whose names are matched without case (a document's parameters, the lexer's
// keywords). Names CompareString calls equal must be ONE key of such a map.
// ---------------------------------------------------------------------------

TEST(StringUtils, CaseFoldLessNamesEqualWithoutCaseAreOneKey) {
    const ibCaseFoldLess less;
    EXPECT_TRUE(CompareString(wxT("Partner"), wxT("PARTNER")));
    EXPECT_FALSE(less(wxT("Partner"), wxT("PARTNER")));
    EXPECT_FALSE(less(wxT("PARTNER"), wxT("Partner")));
}

TEST(StringUtils, CaseFoldLessOrdersByTheFirstCharacterThatDiffers) {
    const ibCaseFoldLess less;
    EXPECT_TRUE(less(wxT("abc"), wxT("ABD")));
    EXPECT_FALSE(less(wxT("ABD"), wxT("abc")));
    // names sharing a long prefix part at one character — how a composed sheet names its links
    EXPECT_TRUE(less(wxT("Link_00000001_0002"), wxT("link_00000001_0003")));
    EXPECT_FALSE(less(wxT("link_00000001_0003"), wxT("Link_00000001_0002")));
}

// A sign is compared as it stands, but against a LETTER it is compared with the letter folded — '[' lies
// between 'Z' and 'a', so the raw order and the folded one disagree about it, and the folded one is the rule.
TEST(StringUtils, CaseFoldLessComparesASignWithTheLetterFolded) {
    const ibCaseFoldLess less;
    EXPECT_FALSE(less(wxT("["), wxT("a")));   // '[' after 'A'
    EXPECT_TRUE(less(wxT("a"), wxT("[")));
    EXPECT_TRUE(less(wxT("9"), wxT("a")));    // a digit before any letter, either way
    EXPECT_TRUE(less(wxT("1"), wxT("2")));    // signs and digits by themselves
}

TEST(StringUtils, CaseFoldLessShorterPrefixIsTheLesser) {
    const ibCaseFoldLess less;
    EXPECT_TRUE(less(wxT("ab"), wxT("ABC")));
    EXPECT_FALSE(less(wxT("ABC"), wxT("ab")));
    EXPECT_FALSE(less(wxEmptyString, wxEmptyString));
}

TEST(StringUtils, CaseFoldLessMapFindsANameInAnotherCase) {
    std::map<wxString, int, ibCaseFoldLess> names;
    names[wxT("Partner")] = 1;
    names[wxT("PARTNER")] = 2;   // the same key, written again
    EXPECT_EQ(names.size(), 1u);
    const auto found = names.find(wxT("partner"));
    ASSERT_NE(found, names.end());
    EXPECT_EQ(found->second, 2);
}

// ---------------------------------------------------------------------------
// Trimming
// ---------------------------------------------------------------------------

TEST(StringUtils, TrimVariants) {
    EXPECT_EQ(TrimAll(wxT("  hi  ")), wxT("hi"));
    wxString l = wxT("xxhi");
    wxString r = wxT("hixx");
    EXPECT_EQ(TrimLeft(l, wxUniChar('x')), wxT("hi"));    // in-place (wxString&) overload
    EXPECT_EQ(TrimRight(r, wxUniChar('x')), wxT("hi"));
}

// Regression guard: the const-ref TrimRight overload used to return the original
// string unchanged (returned strSource, not result) — a silent no-op. A const
// lvalue binds to that overload, so this fails if the bug ever returns.
TEST(StringUtils, TrimRightConstOverloadTrims) {
    const wxString src = wxT("hixx");
    EXPECT_EQ(TrimRight(src, wxUniChar('x')), wxT("hi"));
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

TEST(StringUtils, NumericConversions) {
    EXPECT_EQ(StrToInt(wxT("42")), 42);
    EXPECT_EQ(StrToInt(wxT("-7")), -7);
    EXPECT_EQ(StrToUInt(wxT("100")), 100u);
    EXPECT_EQ(IntToStr(-5), wxT("-5"));
    EXPECT_EQ(UIntToStr(100u), wxT("100"));
}

// ---------------------------------------------------------------------------
// Char predicates
// ---------------------------------------------------------------------------

TEST(StringUtils, CharPredicates) {
    EXPECT_TRUE(IsDigit(wxUniChar('5')));
    EXPECT_FALSE(IsDigit(wxUniChar('a')));
    EXPECT_TRUE(IsSpace(wxUniChar(' ')));
    EXPECT_FALSE(IsSpace(wxUniChar('a')));
    EXPECT_TRUE(IsSymbol(wxUniChar('!')));
    EXPECT_FALSE(IsSymbol(wxUniChar('_')));   // '_' is explicitly not a symbol
    EXPECT_FALSE(IsSymbol(wxUniChar('a')));
    EXPECT_TRUE(IsWord(wxUniChar('a')));
}
