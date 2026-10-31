// =============================================================================
// The format string as a value — core/formatString.h.
//
// What Format(value, format) reads, and what the format string constructor
// edits. Its failures are quiet: a code dropped on the way through a window,
// an empty value taken for an absent one, a space trimmed to nothing. These
// pin the reading, the writing and the applying — the last against the help's
// own examples, since Format now runs exactly this.
// =============================================================================

#include <gtest/gtest.h>
#include "core/formatString.h"
#include "backend/compiler/value.h"

namespace {

ibValue Number(const ibString& text)
{
	ibValue value;
	value.SetNumber(text);
	return value;
}

} // namespace

// ------------------------------- reading ------------------------------------

TEST(FormatString, EachCodeLandsInItsField)
{
	const ibFormatString format = ibFormatString::Parse(
		wxT("ND=10; NFD=2; NDS=,; NGS= ; NG=4; NZ=-; DF=dd.MM.yyyy; DE=none; BT=yes; BF=no"));

	EXPECT_EQ(10, format.m_number.m_digits.value_or(-1));
	EXPECT_EQ(2, format.m_number.m_fractionDigits.value_or(-1));
	EXPECT_EQ(ibT(','), format.m_number.m_decimalSeparator.value_or(ibT('?')));
	EXPECT_EQ(ibT(' '), format.m_number.m_groupSeparator.value_or(ibT('?')));   // all space is a space
	EXPECT_EQ(4, format.m_number.m_groupSize.value_or(-1));
	EXPECT_EQ(wxT("-"), format.m_number.m_zero.value_or(wxT("?")));
	EXPECT_EQ(wxT("dd.MM.yyyy"), format.m_date.m_pattern.value_or(wxT("?")));
	EXPECT_EQ(wxT("none"), format.m_date.m_empty.value_or(wxT("?")));
	EXPECT_EQ(wxT("yes"), format.m_boolean.m_true.value_or(wxT("?")));
	EXPECT_EQ(wxT("no"), format.m_boolean.m_false.value_or(wxT("?")));
	EXPECT_TRUE(format.m_other.empty());
}

// No `NZ` prints a zero as a number; `NZ=` prints it as nothing. Two different requests.
TEST(FormatString, AnEmptyCodeIsNotAnAbsentOne)
{
	const ibFormatString absent = ibFormatString::Parse(wxT("NFD=2"));
	const ibFormatString empty  = ibFormatString::Parse(wxT("NFD=2; NZ="));

	EXPECT_FALSE(absent.m_number.m_zero.has_value());
	ASSERT_TRUE(empty.m_number.m_zero.has_value());
	EXPECT_EQ(wxT(""), *empty.m_number.m_zero);

	EXPECT_EQ(wxT("0.00"), absent.Apply(ibValue(0)));
	EXPECT_EQ(wxT(""), empty.Apply(ibValue(0)));
}

// A code this engine does not read was written by somebody; a window rewriting the string keeps it.
TEST(FormatString, ACodeNotReadIsKeptAndWrittenBack)
{
	const ibFormatString format = ibFormatString::Parse(wxT("NN=1; NFD=2"));

	ASSERT_EQ(1u, format.m_other.size());
	EXPECT_EQ(wxT("NN"), format.m_other[0].first);
	EXPECT_EQ(wxT("1"), format.m_other[0].second);
	EXPECT_EQ(wxT("NFD=2; NN=1"), format.Render());
}

// As when the pairs went into a map: a later pair of the same code wins.
TEST(FormatString, ALaterPairOfTheSameCodeWins)
{
	EXPECT_EQ(3, ibFormatString::Parse(wxT("NFD=1; NFD=3")).m_number.m_fractionDigits.value_or(-1));
}

// ------------------------------- writing ------------------------------------

// Written and read back, a format is the same format — the space value included, which is the one a
// careless writer would trim away.
TEST(FormatString, WhatIsWrittenReadsBackTheSame)
{
	const ibString texts[] = {
		wxT("NFD=2; NGS= "),
		wxT("ND=10; NDS=,; NG=4; NZ="),
		wxT("DF=dd.MM.yyyy HH:mm:ss; DE=-"),
		wxT("BT=Yes; BF=No"),
	};
	for (const ibString& text : texts) {
		const ibFormatString format = ibFormatString::Parse(text);
		EXPECT_EQ(text, format.Render());
		EXPECT_TRUE(ibFormatString::Parse(format.Render()) == format) << text.ToStdString();
	}
}

TEST(FormatString, SemicolonAndEqualsCannotBeWritten)
{
	EXPECT_TRUE(ibFormatString::IsWritable(wxT("n/a")));
	EXPECT_FALSE(ibFormatString::IsWritable(wxT("a;b")));
	EXPECT_FALSE(ibFormatString::IsWritable(wxT("a=b")));
}

// ------------------------------- applying -----------------------------------

// The help's own examples — Format runs exactly this now.
TEST(FormatString, TheHelpExamplesPrintWhatTheHelpSays)
{
	EXPECT_EQ(wxT("1250.50"), ibFormatString::Parse(wxT("NFD=2")).Apply(Number(wxT("1250.5"))));
	EXPECT_EQ(wxT("-"), ibFormatString::Parse(wxT("NZ=-")).Apply(ibValue(0)));
	EXPECT_EQ(wxT("yes"), ibFormatString::Parse(wxT("BT=yes; BF=no")).Apply(ibValue(true)));
	EXPECT_EQ(wxT("no"), ibFormatString::Parse(wxT("BT=yes; BF=no")).Apply(ibValue(false)));
}

TEST(FormatString, AGroupSeparatorGroupsByThreeUnlessToldOtherwise)
{
	EXPECT_EQ(wxT("1 234 567.89"), ibFormatString::Parse(wxT("NFD=2; NGS= ")).Apply(Number(wxT("1234567.891"))));
	EXPECT_EQ(wxT("123,4567"), ibFormatString::Parse(wxT("NGS=,; NG=4")).Apply(Number(wxT("1234567"))));
	EXPECT_EQ(wxT("12,5"), ibFormatString::Parse(wxT("NDS=,")).Apply(Number(wxT("12.5"))));
}

// `MM` is the month and `mm` the minutes.
TEST(FormatString, ADatePatternAndAnEmptyDate)
{
	const ibValue date(2026, 9, 15, 14, 5, 7);
	EXPECT_EQ(wxT("15.09.2026"), ibFormatString::Parse(wxT("DF=dd.MM.yyyy")).Apply(date));
	EXPECT_EQ(wxT("14:05"), ibFormatString::Parse(wxT("DF=HH:mm")).Apply(date));
	EXPECT_EQ(wxT("-"), ibFormatString::Parse(wxT("DF=dd.MM.yyyy; DE=-")).Apply(ibValue(ibValueTypes::TYPE_DATE)));
}

// One letter is the number as it is, two pad it, three and four spell it out; a run of y longer than two is the
// full year; text in quotes stands as written.
TEST(FormatString, EveryDateSpelling)
{
	const ibValue date(2026, 9, 5, 16, 3, 2);   // a Saturday
	EXPECT_EQ(wxT("5.9.26"), ibFormatString::Parse(wxT("DF=d.M.yy")).Apply(date));
	EXPECT_EQ(wxT("2026-09-05 16:03:02"), ibFormatString::Parse(wxT("DF=yyyy-MM-dd HH:mm:ss")).Apply(date));
	EXPECT_EQ(wxT("09.2026"), ibFormatString::Parse(wxT("DF=MM.yyyy")).Apply(date));
	EXPECT_EQ(wxT("4:03 PM"), ibFormatString::Parse(wxT("DF=h:mm tt")).Apply(date));
	EXPECT_EQ(wxT("Saturday, 5 September 2026"), ibFormatString::Parse(wxT("DF=dddd, d MMMM yyyy")).Apply(date));
	EXPECT_EQ(wxT("Sat 5 Sep"), ibFormatString::Parse(wxT("DF=ddd d MMM")).Apply(date));
	EXPECT_EQ(wxT("September 2026"), ibFormatString::Parse(wxT("DF=MMMM yyyy")).Apply(date));
	EXPECT_EQ(wxT("day 05"), ibFormatString::Parse(wxT("DF='day' dd")).Apply(date));
}

// A value in quotes is the value: the quotes fence it and are not a literal of the pattern.
TEST(FormatString, AQuotedValueIsTheValue)
{
	const ibValue date(2026, 9, 5, 16, 3, 2);
	EXPECT_EQ(wxT("09.2026"), ibFormatString::Parse(wxT("DF='MM.yyyy'")).Apply(date));
	EXPECT_EQ(wxT("09.2026"), ibFormatString::Parse(wxT("DF=\"MM.yyyy\"")).Apply(date));
	EXPECT_EQ(wxT("day 05 of September"), ibFormatString::Parse(wxT("DF='day' dd 'of' MMMM")).Apply(date)) << "two literals, no fence";
	EXPECT_EQ(wxT("yes"), ibFormatString::Parse(wxT("BT='yes'")).Apply(ibValue(true)));
}

// A code is its Latin name and nothing else: spelled in Cyrillic it is a code this engine does not read, kept as
// written, and a pattern's letters are Latin too.
TEST(FormatString, ACodeIsItsLatinName)
{
	ibFormatCode none = ibFormatCode::NumberDigits;
	EXPECT_FALSE(ibFormatString::FindCode(wxT("\u0414\u0424"), none));   // DF in Cyrillic letters
	const ibFormatString format = ibFormatString::Parse(wxT("\u0414\u0424=dd.MM.yyyy"));
	EXPECT_FALSE(format.m_date.m_pattern.has_value());
	ASSERT_EQ(1u, format.m_other.size());

	const ibValue date(2026, 10, 13, 15, 30, 45);
	EXPECT_EQ(wxT("\u0434\u0434.10.2026"), ibFormatString::Parse(wxT("DF=\u0434\u0434.MM.yyyy")).Apply(date));
}

// DLF presets; DF wins over DLF; L is kept and written back. The names said in the language L names are
// test_dictionary's (FormatSaysTheNamesInTheLanguageLNames): they need a catalog, which the test writes there.
TEST(FormatString, LocalFormatAndLanguage)
{
	const ibValue date(2026, 10, 13, 15, 30, 45);
	EXPECT_EQ(wxT("13.10.2026"), ibFormatString::Parse(wxT("DLF=D")).Apply(date));
	EXPECT_EQ(wxT("15:30:45"), ibFormatString::Parse(wxT("DLF=T")).Apply(date));
	EXPECT_EQ(wxT("13.10.2026 15:30:45"), ibFormatString::Parse(wxT("DLF=DT")).Apply(date));
	EXPECT_EQ(wxT("13 October 2026"), ibFormatString::Parse(wxT("DLF=DD")).Apply(date));
	EXPECT_EQ(wxT("2026"), ibFormatString::Parse(wxT("DLF=DD; DF=yyyy")).Apply(date));
	EXPECT_EQ(wxT("DLF=DD; L=uk"), ibFormatString::Parse(wxT("L=uk; DLF=DD")).Render());
}

// NLZ pads the whole part to ND - NFD digits.
TEST(FormatString, LeadingZeros)
{
	EXPECT_EQ(wxT("00042"), ibFormatString::Parse(wxT("ND=5; NLZ=")).Apply(Number(wxT("42"))));
	EXPECT_EQ(wxT("-0042.50"), ibFormatString::Parse(wxT("ND=6; NFD=2; NLZ=")).Apply(Number(wxT("-42.5"))));
}

// The out-argument form writes the same text over whatever the string held, and says whether there is any.
TEST(FormatString, TheOutArgumentWritesTheSameText)
{
	ibString text(wxT("whatever was there"));
	EXPECT_TRUE(ibFormatString::Parse(wxT("NFD=2; NGS= ")).Apply(Number(wxT("1234567.891")), text));
	EXPECT_EQ(wxT("1 234 567.89"), text);
	EXPECT_FALSE(ibFormatString::Parse(wxT("NZ=")).Apply(ibValue(0), text));
	EXPECT_EQ(wxT(""), text);
}

// A value of any other type prints as its plain string, whatever the codes say.
TEST(FormatString, AStringIsNotFormatted)
{
	EXPECT_EQ(wxT("text"), ibFormatString::Parse(wxT("NFD=2; BT=yes")).Apply(ibValue(ibString(wxT("text")))));
}

// ------------------------------ one code at a time --------------------------

// The door Parse and Render go through, and the one format_string edits by: Set reads a value as Parse
// does, Get answers it as Render writes it, Clear takes it out — absent, not empty.
TEST(FormatString, OneCodeAtATime)
{
	ibFormatString format;
	format.Set(ibFormatCode::NumberFractionDigits, wxT("2"));
	format.Set(ibFormatCode::NumberGroupSeparator, wxT(" "));
	format.Set(ibFormatCode::NumberZero, wxT(""));
	EXPECT_EQ(wxT("NFD=2; NGS= ; NZ="), format.Render());
	EXPECT_EQ(wxT("2"), format.Get(ibFormatCode::NumberFractionDigits).value_or(wxT("?")));
	EXPECT_FALSE(format.Get(ibFormatCode::DatePattern).has_value());

	format.Clear(ibFormatCode::NumberZero);
	EXPECT_FALSE(format.Get(ibFormatCode::NumberZero).has_value());
	EXPECT_EQ(wxT("NFD=2; NGS= "), format.Render());

	// Every code is found by its spelling and spelled back the same.
	for (const ibFormatCode code : ibFormatString::Codes()) {
		ibFormatCode found;
		ASSERT_TRUE(ibFormatString::FindCode(ibFormatString::CodeName(code), found));
		EXPECT_EQ(code, found);
	}
	ibFormatCode none;
	EXPECT_FALSE(ibFormatString::FindCode(wxT("NN"), none));
	EXPECT_FALSE(ibFormatString::FindCode(wxT("nfd"), none));   // capitals, as Format reads them
}

// -------------------------------- presets -----------------------------------

TEST(FormatString, APresetIsItsPattern)
{
	EXPECT_EQ(ibDatePreset::Date, ibFormatString::PresetOf(wxT("dd.MM.yyyy")));
	EXPECT_EQ(ibDatePreset::Custom, ibFormatString::PresetOf(wxT("d/M/yy")));
	EXPECT_EQ(wxT("dd.MM.yyyy"), ibFormatString::PresetPattern(ibDatePreset::Date));
	EXPECT_TRUE(ibFormatString::PresetPattern(ibDatePreset::Custom).IsEmpty());
	EXPECT_EQ(ibString(ibT("NFD")), ibFormatString::CodeName(ibFormatCode::NumberFractionDigits));
	EXPECT_EQ(ibFormatKind::Date, ibFormatString::KindOf(ibFormatCode::DateEmpty));
}
