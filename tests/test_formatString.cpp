// =============================================================================
// The format string as a value — backend/formatString.h.
//
// What Format(value, format) reads, and what the format string constructor
// edits. Its failures are quiet: a code dropped on the way through a window,
// an empty value taken for an absent one, a space trimmed to nothing. These
// pin the reading, the writing and the applying — the last against the help's
// own examples, since Format now runs exactly this.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/formatString.h"
#include "backend/compiler/value.h"

namespace {

ibValue Number(const wxString& text)
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
		wxT("ND=10; NFD=2; NDS=,; NGS= ; NG=4; NZ=-; DF=dd.mm.yyyy; DE=none; BT=yes; BF=no"));

	EXPECT_EQ(10, format.m_number.m_digits.value_or(-1));
	EXPECT_EQ(2, format.m_number.m_fractionDigits.value_or(-1));
	EXPECT_EQ(wxUniChar(','), format.m_number.m_decimalSeparator.value_or(wxUniChar('?')));
	EXPECT_EQ(wxUniChar(' '), format.m_number.m_groupSeparator.value_or(wxUniChar('?')));   // all space is a space
	EXPECT_EQ(4, format.m_number.m_groupSize.value_or(-1));
	EXPECT_EQ(wxT("-"), format.m_number.m_zero.value_or(wxT("?")));
	EXPECT_EQ(wxT("dd.mm.yyyy"), format.m_date.m_pattern.value_or(wxT("?")));
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
	const ibFormatString format = ibFormatString::Parse(wxT("NLZ=1; NFD=2"));

	ASSERT_EQ(1u, format.m_other.size());
	EXPECT_EQ(wxT("NLZ"), format.m_other[0].first);
	EXPECT_EQ(wxT("1"), format.m_other[0].second);
	EXPECT_EQ(wxT("NFD=2; NLZ=1"), format.Render());
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
	const wxString texts[] = {
		wxT("NFD=2; NGS= "),
		wxT("ND=10; NDS=,; NG=4; NZ="),
		wxT("DF=dd.mm.yyyy HH:MM:SS; DE=-"),
		wxT("BT=Yes; BF=No"),
	};
	for (const wxString& text : texts) {
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

// ⚠ `mm` is the month and `MM` the minutes.
TEST(FormatString, ADatePatternAndAnEmptyDate)
{
	const ibValue date(2026, 9, 15, 14, 5, 7);
	EXPECT_EQ(wxT("15.09.2026"), ibFormatString::Parse(wxT("DF=dd.mm.yyyy")).Apply(date));
	EXPECT_EQ(wxT("14:05"), ibFormatString::Parse(wxT("DF=HH:MM")).Apply(date));
	EXPECT_EQ(wxT("-"), ibFormatString::Parse(wxT("DF=dd.mm.yyyy; DE=-")).Apply(ibValue(ibValueTypes::TYPE_DATE)));
}

// A value of any other type prints as its plain string, whatever the codes say.
TEST(FormatString, AStringIsNotFormatted)
{
	EXPECT_EQ(wxT("text"), ibFormatString::Parse(wxT("NFD=2; BT=yes")).Apply(ibValue(wxString(wxT("text")))));
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
	EXPECT_FALSE(ibFormatString::FindCode(wxT("NLZ"), none));
	EXPECT_FALSE(ibFormatString::FindCode(wxT("nfd"), none));   // capitals, as Format reads them
}

// -------------------------------- presets -----------------------------------

TEST(FormatString, APresetIsItsPattern)
{
	EXPECT_EQ(ibDatePreset::Date, ibFormatString::PresetOf(wxT("dd.mm.yyyy")));
	EXPECT_EQ(ibDatePreset::Custom, ibFormatString::PresetOf(wxT("d/m/yy")));
	EXPECT_EQ(wxT("dd.mm.yyyy"), ibFormatString::PresetPattern(ibDatePreset::Date));
	EXPECT_TRUE(ibFormatString::PresetPattern(ibDatePreset::Custom).IsEmpty());
	EXPECT_STREQ(wxT("NFD"), ibFormatString::CodeName(ibFormatCode::NumberFractionDigits));
	EXPECT_EQ(ibFormatKind::Date, ibFormatString::KindOf(ibFormatCode::DateEmpty));
}
