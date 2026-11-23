// =============================================================================
// The input mask — core/inputMask.h.
//
// What a field lets you type: a position takes a character of its class or
// nothing, a literal stays where it stands. Its failures are a key that went in
// where it should not (a fourth digit after the point of a Number(10,3), a day
// 32), a key that did not go in where it should, and a value read back wrong.
// These pin the reading, the typing (Replace — the one verb both editors call;
// the browser's controls/inputMask.js transcribes these cases), the reading back
// (Strip), the run a double click selects (GetRun) and the masks a type gives.
// =============================================================================

#include <gtest/gtest.h>
#include "core/inputMask.h"

namespace {

// A type description as FromTypeDesc asks it — the engine's and the client's answer the same five questions.
struct FakeType {
	ibValueTypes    m_kind = ibValueTypes::TYPE_STRING;
	unsigned int    m_count = 1;
	unsigned char   m_precision = 0;
	unsigned char   m_scale = 0;
	bool            m_nonNegative = false;
	ibDateFractions m_fraction = ibDateFractions::ibDateFractions_DateTime;

	unsigned int GetClsidCount() const { return m_count; }
	bool ContainType(ibValueTypes kind) const { return kind == m_kind; }
	unsigned char GetPrecision() const { return m_precision; }
	unsigned char GetScale() const { return m_scale; }
	bool IsNonNegative() const { return m_nonNegative; }
	ibDateFractions GetDateFraction() const { return m_fraction; }
};

FakeType Number(unsigned char precision, unsigned char scale, bool nonNegative = false)
{
	FakeType type;
	type.m_kind = ibValueTypes::TYPE_NUMBER;
	type.m_precision = precision;
	type.m_scale = scale;
	type.m_nonNegative = nonNegative;
	return type;
}

FakeType Date(ibDateFractions fraction)
{
	FakeType type;
	type.m_kind = ibValueTypes::TYPE_DATE;
	type.m_fraction = fraction;
	return type;
}

ibInputMask FromType(const FakeType& type)
{
	ibInputMask mask;
	EXPECT_TRUE(ibInputMask::FromTypeDesc(type, mask));
	return mask;
}

// Typed one key at a time at the caret, as an editor does: the caret moves on with each.
void Type(const ibInputMask& mask, ibString& masked, size_t& caret, const ibString& keys)
{
	for (size_t i = 0; i < keys.length(); ++i)
		mask.Replace(masked, caret, caret, ibString(keys[i]), caret);
}

} // namespace

// ------------------------------- reading ------------------------------------

TEST(InputMask, RendersAsWritten)
{
	EXPECT_EQ(ibString(ibT("+1 (000) 000-00-00;0;_")), ibInputMask::Parse(ibT("+1 (000) 000-00-00;0;_")).Render());
	EXPECT_TRUE(ibInputMask::Parse(ibString()).IsEmpty());
	EXPECT_TRUE(ibInputMask::Parse(ibT(";1;_")).IsEmpty());
	EXPECT_FALSE(ibInputMask::Parse(ibT("!#99.9")).IsEmpty());   // a number's mask has no positions, and is a mask
}

TEST(InputMask, TheSkeletonIsLiteralsAndBlanks)
{
	EXPECT_EQ(ibString(ibT("  .  .    ")), ibInputMask::Parse(ibT("00.00.0000")).Apply(ibString()));
	EXPECT_EQ(ibString(ibT("___")), ibInputMask::Parse(ibT("000;1;_")).Apply(ibString()));   // the third section
	EXPECT_EQ(ibString(ibT("0-  ")), ibInputMask::Parse(ibT("\\0-00")).Apply(ibString()));   // `\` makes a literal
}

TEST(InputMask, AnExclamationBeforeNoShapeIsPassedOver)
{
	EXPECT_EQ(ibString(ibT("  -  ")), ibInputMask::Parse(ibT("!00-00")).Apply(ibString()));
	EXPECT_EQ(ibString(ibT("  -  ")), ibInputMask::Parse(ibT("00!-00")).Apply(ibString()));
}

// ------------------------------- typing -------------------------------------

TEST(InputMask, ADateIsTypedOverItsSeparators)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = mask.Apply(ibString());
	size_t caret = 0;

	Type(mask, masked, caret, ibT("23"));
	EXPECT_EQ(ibString(ibT("23.  .    ")), masked);
	EXPECT_EQ(2u, caret);

	Type(mask, masked, caret, ibT("1"));   // steps over the dot
	EXPECT_EQ(ibString(ibT("23.1 .    ")), masked);
	EXPECT_EQ(4u, caret);
}

TEST(InputMask, ALiteralTypedStepsPastItsPlace)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = mask.Apply(ibString());
	size_t caret = 0;

	Type(mask, masked, caret, ibT("1."));
	EXPECT_EQ(ibString(ibT("1 .  .    ")), masked);
	EXPECT_EQ(3u, caret);
}

TEST(InputMask, AKeyThePositionDoesNotTakeDoesNotGoIn)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = mask.Apply(ibString());
	size_t caret = 0;

	EXPECT_FALSE(mask.Replace(masked, 0, 0, ibT("x"), caret));
	EXPECT_EQ(ibString(ibT("  .  .    ")), masked);
	EXPECT_EQ(0u, caret);
}

TEST(InputMask, TypingOverwrites)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = ibT("23.11.2026");
	size_t caret = 0;

	EXPECT_TRUE(mask.Replace(masked, 0, 0, ibT("1"), caret));
	EXPECT_EQ(ibString(ibT("13.11.2026")), masked);
	EXPECT_EQ(1u, caret);
}

TEST(InputMask, APasteIsLaidFromTheStart)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = ibT("23.11.2026");
	size_t caret = 10;

	EXPECT_TRUE(mask.Replace(masked, 0, 10, ibT("01022024"), caret));
	EXPECT_EQ(ibString(ibT("01.02.2024")), masked);
	EXPECT_EQ(10u, caret);
}

TEST(InputMask, LettersTakeTheCaseTheMaskSays)
{
	const ibInputMask upper = ibInputMask::Parse(ibT(">LL"));
	ibString masked = upper.Apply(ibString());
	size_t caret = 0;
	Type(upper, masked, caret, ibT("ab"));
	EXPECT_EQ(ibString(ibT("AB")), masked);

	const ibInputMask mixed = ibInputMask::Parse(ibT("<L<>L"));
	masked = mixed.Apply(ibString());
	caret = 0;
	Type(mixed, masked, caret, ibT("AB"));
	EXPECT_EQ(ibString(ibT("aB")), masked);
}

// ------------------------------- deleting -----------------------------------

TEST(InputMask, BackspaceOverASeparatorEmptiesThePositionBeforeIt)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = ibT("23.1 .    ");
	size_t caret = 3;   // just after the first dot

	EXPECT_TRUE(mask.Replace(masked, 2, 3, ibString(), caret));
	EXPECT_EQ(ibString(ibT("2 .1 .    ")), masked);
	EXPECT_EQ(1u, caret);
}

TEST(InputMask, DeleteOverASeparatorEmptiesThePositionAfterIt)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	ibString masked = ibT("23.1 .    ");
	size_t caret = 2;   // just before the first dot

	EXPECT_TRUE(mask.Replace(masked, 2, 3, ibString(), caret));
	EXPECT_EQ(ibString(ibT("23.  .    ")), masked);
	EXPECT_EQ(3u, caret);
}

// ------------------------------- a number -----------------------------------

// Max, 2026-11-23: "make the input of a number as in 1C" — typed as a number is, its text the number's own: no
// positions in front of it to show as blanks.
TEST(InputMask, ANumberIsTypedAsANumber)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = mask.Apply(ibString());
	EXPECT_EQ(ibString(), masked);

	size_t caret = 0;
	Type(mask, masked, caret, ibT("123"));
	EXPECT_EQ(ibString(ibT("123")), masked);
	EXPECT_EQ(3u, caret);
}

// Max, 2026-11-23: "now you can type 20, 30 digits after the point; the mask simply does not let you past the limit".
TEST(InputMask, ANumberTakesNoMoreDigitsThanItsTypeKeeps)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked;
	size_t caret = 0;

	Type(mask, masked, caret, ibT("1234567"));
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("8"), caret));   // an eighth whole digit
	EXPECT_EQ(ibString(ibT("1234567")), masked);

	Type(mask, masked, caret, ibT(".234"));
	EXPECT_EQ(ibString(ibT("1234567.234")), masked);
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("5"), caret));   // a fourth digit after the point
	EXPECT_EQ(ibString(ibT("1234567.234")), masked);
}

TEST(InputMask, ACommaIsThePoint)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked;
	size_t caret = 0;

	Type(mask, masked, caret, ibT("12,5"));
	EXPECT_EQ(ibString(ibT("12.5")), masked);
}

TEST(InputMask, ThePointTypedWhereItIsStepsPastIt)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = ibT("12.5");
	size_t caret = 1;

	EXPECT_TRUE(mask.Replace(masked, 1, 1, ibT(","), caret));
	EXPECT_EQ(ibString(ibT("12.5")), masked);
	EXPECT_EQ(3u, caret);
}

TEST(InputMask, ThePointGoesInWhereTheFractionHoldsWhatFollows)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = ibT("12345");
	size_t caret = 2;
	EXPECT_TRUE(mask.Replace(masked, 2, 2, ibT("."), caret));
	EXPECT_EQ(ibString(ibT("12.345")), masked);
	EXPECT_EQ(3u, caret);

	masked = ibT("12345");
	caret = 1;
	EXPECT_FALSE(mask.Replace(masked, 1, 1, ibT("."), caret));   // four digits after it
	EXPECT_EQ(ibString(ibT("12345")), masked);
}

TEST(InputMask, MinusTogglesTheSign)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked;
	size_t caret = 0;

	Type(mask, masked, caret, ibT("12-"));
	EXPECT_EQ(ibString(ibT("-12")), masked);
	EXPECT_EQ(3u, caret);
	Type(mask, masked, caret, ibT("-"));
	EXPECT_EQ(ibString(ibT("12")), masked);
	EXPECT_EQ(2u, caret);

	const ibInputMask nonNegative = FromType(Number(10, 3, true));
	EXPECT_FALSE(nonNegative.Replace(masked, 2, 2, ibT("-"), caret));
	EXPECT_EQ(ibString(ibT("12")), masked);
}

// Max, 2026-11-23: a zero at rest stays `0.000` — and typing into it starts the number, as on a calculator.
TEST(InputMask, ALoneZeroGivesWayToTheFirstDigit)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = mask.Apply(ibT("0.000"));
	EXPECT_EQ(ibString(ibT("0.000")), masked);

	size_t caret = 1;
	Type(mask, masked, caret, ibT("50"));
	EXPECT_EQ(ibString(ibT("50.000")), masked);
	EXPECT_EQ(2u, caret);
}

TEST(InputMask, AFullFractionIsTypedOver)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = ibT("12.500");
	size_t caret = 3;

	Type(mask, masked, caret, ibT("75"));
	EXPECT_EQ(ibString(ibT("12.750")), masked);
	EXPECT_EQ(5u, caret);
}

TEST(InputMask, BackspaceInANumberRemovesTheDigit)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = ibT("123");
	size_t caret = 3;

	EXPECT_TRUE(mask.Replace(masked, 2, 3, ibString(), caret));
	EXPECT_EQ(ibString(ibT("12")), masked);
	EXPECT_EQ(2u, caret);
}

TEST(InputMask, ThePointIsNotStruckOutBetweenPartsTooLongTogether)
{
	const ibInputMask mask = FromType(Number(10, 3));
	ibString masked = ibT("1234567.234");
	size_t caret = 8;

	EXPECT_FALSE(mask.Replace(masked, 7, 8, ibString(), caret));   // 1234567234 is ten whole digits
	EXPECT_EQ(ibString(ibT("1234567.234")), masked);
}

// ------------------------------- a date -------------------------------------

// Max, 2026-11-23: "it does not check the date: I type 99 and it erases the whole line".
TEST(InputMask, ADigitNoDayBeginsWithIsNotTaken)
{
	const ibInputMask mask = FromType(Date(ibDateFractions::ibDateFractions_Date));
	ibString masked = mask.Apply(ibString());
	EXPECT_EQ(ibString(ibT("  .  .    ")), masked);
	size_t caret = 0;

	EXPECT_FALSE(mask.Replace(masked, 0, 0, ibT("9"), caret));   // no day is 9_
	Type(mask, masked, caret, ibT("3"));
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("2"), caret));   // nor 32
	Type(mask, masked, caret, ibT("1"));
	EXPECT_EQ(ibString(ibT("31.  .    ")), masked);

	masked = mask.Apply(ibString());
	caret = 0;
	Type(mask, masked, caret, ibT("0"));
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("0"), caret));   // nor 00
}

TEST(InputMask, AMonthPastTwelveIsNotTaken)
{
	const ibInputMask mask = FromType(Date(ibDateFractions::ibDateFractions_Date));
	ibString masked = mask.Apply(ibString());
	size_t caret = 0;

	Type(mask, masked, caret, ibT("011"));
	EXPECT_EQ(ibString(ibT("01.1 .    ")), masked);
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("3"), caret));
	Type(mask, masked, caret, ibT("2"));
	EXPECT_EQ(ibString(ibT("01.12.    ")), masked);
}

TEST(InputMask, ADigitTypedOverMakesNoImpossibleDay)
{
	const ibInputMask mask = FromType(Date(ibDateFractions::ibDateFractions_Date));
	ibString masked = ibT("15.11.2026");
	size_t caret = 0;

	EXPECT_TRUE(mask.Replace(masked, 0, 0, ibT("3"), caret));   // 35 is no day: the 5 goes
	EXPECT_EQ(ibString(ibT("3 .11.2026")), masked);
	Type(mask, masked, caret, ibT("0"));
	EXPECT_EQ(ibString(ibT("30.11.2026")), masked);
}

TEST(InputMask, TheTimeTakesNoImpossibleHourOrMinute)
{
	const ibInputMask mask = FromType(Date(ibDateFractions::ibDateFractions_DateTime));
	ibString masked = mask.Apply(ibT("23.11.2026"));
	EXPECT_EQ(ibString(ibT("23.11.2026   :  :  ")), masked);
	size_t caret = 11;

	Type(mask, masked, caret, ibT("2"));
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("5"), caret));   // no hour 25
	Type(mask, masked, caret, ibT("3"));
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("6"), caret));   // no minute 6_
	Type(mask, masked, caret, ibT("59"));
	EXPECT_EQ(ibString(ibT("23.11.2026 23:59:  ")), masked);
}

TEST(InputMask, NoYearIsZero)
{
	const ibInputMask mask = FromType(Date(ibDateFractions::ibDateFractions_Date));
	ibString masked = ibT("23.11.    ");
	size_t caret = 6;

	Type(mask, masked, caret, ibT("000"));
	EXPECT_EQ(ibString(ibT("23.11.000 ")), masked);
	EXPECT_FALSE(mask.Replace(masked, caret, caret, ibT("0"), caret));
}

// --------------------------- laying a text in --------------------------------

TEST(InputMask, AShownNumberIsLaidIntoItsMask)
{
	const ibInputMask mask = FromType(Number(10, 3));
	EXPECT_EQ(ibString(ibT("1234.500")), mask.Apply(ibT("1234.500")));
	EXPECT_EQ(ibString(ibT("1234.500")), mask.Apply(ibT("1 234,500")));   // a group's space is no part of a number
	EXPECT_EQ(ibString(ibT("-1234.500")), mask.Apply(ibT("-1234.500")));
	EXPECT_EQ(ibString(ibT("1234567")), mask.Apply(ibT("123456789")));   // what does not fit is dropped
}

TEST(InputMask, ATextInTheMasksShapeStaysAsItIs)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("00.00.0000"));
	EXPECT_EQ(ibString(ibT("23.11.2026")), mask.Apply(ibT("23.11.2026")));
}

TEST(InputMask, ALiteralInTheTextFindsItsPlace)
{
	const ibInputMask mask = ibInputMask::Parse(ibT("+1 (000) 000-00-00;0;_"));
	EXPECT_EQ(ibString(ibT("+1 (555) 123-45-67")), mask.Apply(ibT("5551234567")));
	EXPECT_EQ(ibString(ibT("+1 (555) 123-45-67")), mask.Apply(ibT("15551234567")));   // the 1 typed is the 1 written
}

// ------------------------------- reading back -------------------------------

TEST(InputMask, TheSecondSectionDecidesWhetherTheLiteralsStay)
{
	ibString text;
	EXPECT_TRUE(ibInputMask::Parse(ibT("+1 (000) 000-00-00;0;_")).Strip(ibT("+1 (555) 123-45-67"), text));
	EXPECT_EQ(ibString(ibT("5551234567")), text);
	EXPECT_TRUE(ibInputMask::Parse(ibT("+1 (000) 000-00-00;1;_")).Strip(ibT("+1 (555) 123-45-67"), text));
	EXPECT_EQ(ibString(ibT("+1 (555) 123-45-67")), text);
}

TEST(InputMask, ARequiredPositionLeftBlankIsRefused)
{
	ibString text, refusal;
	EXPECT_FALSE(ibInputMask::Parse(ibT("00.00.0000")).Strip(ibT("23.11.    "), text, &refusal));
	EXPECT_FALSE(refusal.IsEmpty());

	refusal.clear();
	EXPECT_FALSE(FromType(Date(ibDateFractions::ibDateFractions_Time)).Strip(ibT("12:  :  "), text, &refusal));
	EXPECT_FALSE(refusal.IsEmpty());   // a time alone is typed whole
}

TEST(InputMask, AWhollyBlankTextIsTheEmptyValue)
{
	ibString text = ibT("?");
	EXPECT_TRUE(ibInputMask::Parse(ibT("00.00.0000")).Strip(ibT("  .  .    "), text));
	EXPECT_TRUE(text.IsEmpty());
}

TEST(InputMask, NothingIsKeptAfterTheLastCharacterTyped)
{
	ibString text;
	EXPECT_TRUE(FromType(Date(ibDateFractions::ibDateFractions_DateTime)).Strip(ibT("23.11.2026   :  :  "), text));
	EXPECT_EQ(ibString(ibT("23.11.2026")), text);   // a document's date typed alone: its midnight, as before
}

TEST(InputMask, ANumberReadsBackAsANumber)
{
	const ibInputMask number = FromType(Number(10, 3));
	ibString text;
	EXPECT_TRUE(number.Strip(ibT("123"), text));
	EXPECT_EQ(ibString(ibT("123")), text);
	EXPECT_TRUE(number.Strip(ibT("-1234.500"), text));
	EXPECT_EQ(ibString(ibT("-1234.500")), text);
	EXPECT_TRUE(number.Strip(ibT(".5"), text));
	EXPECT_EQ(ibString(ibT("0.5")), text);
	EXPECT_TRUE(number.Strip(ibT("-.5"), text));
	EXPECT_EQ(ibString(ibT("-0.5")), text);
	EXPECT_TRUE(number.Strip(ibT("12."), text));
	EXPECT_EQ(ibString(ibT("12")), text);
	EXPECT_TRUE(number.Strip(ibT("-"), text));   // no digit: the empty value
	EXPECT_TRUE(text.IsEmpty());
}

// ------------------------------- a double click -----------------------------

// Max, 2026-11-23: "when I double-click, it does not select the digits".
TEST(InputMask, ADoubleClickSelectsTheRunTheCaretStandsIn)
{
	const ibInputMask date = FromType(Date(ibDateFractions::ibDateFractions_Date));
	size_t first = 0, last = 0;

	EXPECT_TRUE(date.GetRun(ibT("23.11.2026"), 4, first, last));   // in the month
	EXPECT_EQ(3u, first);
	EXPECT_EQ(4u, last);
	EXPECT_TRUE(date.GetRun(ibT("23.11.2026"), 2, first, last));   // just after the day
	EXPECT_EQ(0u, first);
	EXPECT_EQ(1u, last);
	EXPECT_TRUE(date.GetRun(ibT("23.11.2026"), 10, first, last));   // at the end: the year
	EXPECT_EQ(6u, first);
	EXPECT_EQ(9u, last);

	EXPECT_FALSE(ibInputMask().GetRun(ibT("abc"), 1, first, last));   // no mask: the editor's own word
}

TEST(InputMask, ADoubleClickInANumberSelectsItsWholePartOrItsFraction)
{
	const ibInputMask number = FromType(Number(10, 3));
	size_t first = 0, last = 0;

	EXPECT_TRUE(number.GetRun(ibT("-1234.500"), 3, first, last));   // the whole part, not its sign
	EXPECT_EQ(1u, first);
	EXPECT_EQ(4u, last);
	EXPECT_TRUE(number.GetRun(ibT("-1234.500"), 7, first, last));
	EXPECT_EQ(6u, first);
	EXPECT_EQ(8u, last);
	EXPECT_TRUE(number.GetRun(ibT("123"), 3, first, last));
	EXPECT_EQ(0u, first);
	EXPECT_EQ(2u, last);
	EXPECT_FALSE(number.GetRun(ibString(), 0, first, last));
}

// ------------------------------- from the type ------------------------------

TEST(InputMask, ANumberGivesItsDigitsAndItsSign)
{
	EXPECT_EQ(ibString(ibT("!#9999999.999")), FromType(Number(10, 3)).Render());
	EXPECT_EQ(ibString(ibT("!9999999.999")), FromType(Number(10, 3, true)).Render());
	EXPECT_EQ(ibString(ibT("!#99999")), FromType(Number(5, 0)).Render());

	ibInputMask mask;
	EXPECT_FALSE(ibInputMask::FromTypeDesc(Number(0, 0), mask));   // nobody bounded it
}

TEST(InputMask, ADateGivesItsPattern)
{
	EXPECT_EQ(ibString(ibT("!dd.MM.yyyy")), FromType(Date(ibDateFractions::ibDateFractions_Date)).Render());
	EXPECT_EQ(ibString(ibT("!HH:mm:ss")), FromType(Date(ibDateFractions::ibDateFractions_Time)).Render());
	EXPECT_EQ(ibString(ibT("!dd.MM.yyyy HH:mm:ss")), FromType(Date(ibDateFractions::ibDateFractions_DateTime)).Render());
}

TEST(InputMask, AnyOtherTypeGivesNone)
{
	ibInputMask mask;
	EXPECT_FALSE(ibInputMask::FromTypeDesc(FakeType(), mask));   // a string

	FakeType composite = Number(10, 2);
	composite.m_count = 2;
	EXPECT_FALSE(ibInputMask::FromTypeDesc(composite, mask));
}
