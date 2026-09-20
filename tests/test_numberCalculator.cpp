// =============================================================================
// ibNumberCalculator — the arithmetic behind the calculator a number field offers.
// Exact decimals, pocket-calculator order of operations (left to right, one
// operator at a time), and a display that says what was typed.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/utils/numberCalculator.hpp"

namespace {

using Op = ibNumberCalculator::Op;

void Type(ibNumberCalculator& calc, const char* keys)
{
	for (const char* k = keys; *k != '\0'; ++k) {
		switch (*k) {
		case '.': calc.Point(); break;
		case '+': calc.Operator(Op::Add); break;
		case '-': calc.Operator(Op::Subtract); break;
		case '*': calc.Operator(Op::Multiply); break;
		case '/': calc.Operator(Op::Divide); break;
		case '=': calc.Equals(); break;
		case '<': calc.Backspace(); break;
		case '~': calc.Negate(); break;
		case 'C': calc.Clear(); break;
		default:  calc.Digit(*k - '0'); break;
		}
	}
}

wxString Shown(const char* keys, const ibNumber& initial = ibNumber(0))
{
	ibNumberCalculator calc(initial);
	Type(calc, keys);
	return calc.Display();
}

} // namespace

TEST(NumberCalculator, Digits_TypedInOrder_AreShownAsTyped)
{
	EXPECT_EQ(Shown("1205"), wxT("1205"));
}

TEST(NumberCalculator, LeadingZero_IsNotKept)
{
	EXPECT_EQ(Shown("007"), wxT("7"));
	EXPECT_EQ(Shown("000"), wxT("0"));
}

TEST(NumberCalculator, Point_AfterNothing_StartsWithZero)
{
	EXPECT_EQ(Shown("."), wxT("0."));
	EXPECT_EQ(Shown(".5"), wxT("0.5"));
}

TEST(NumberCalculator, Point_TypedTwice_IsIgnoredTheSecondTime)
{
	EXPECT_EQ(Shown("1.2.3"), wxT("1.23"));
}

TEST(NumberCalculator, TrailingZeros_WhileTyping_AreKept)
{
	// "12.50" is what the person typed; it is only a result that is shown in its shortest form.
	EXPECT_EQ(Shown("12.50"), wxT("12.50"));
}

TEST(NumberCalculator, Addition_OfDecimals_IsExact)
{
	// The reason this works in ibNumber and not in double.
	EXPECT_EQ(Shown("0.1+0.2="), wxT("0.3"));
}

TEST(NumberCalculator, Subtraction_BelowZero_ShowsTheSign)
{
	EXPECT_EQ(Shown("3-10="), wxT("-7"));
}

TEST(NumberCalculator, Multiplication_OfMoney_KeepsBothPlaces)
{
	EXPECT_EQ(Shown("19.99*3="), wxT("59.97"));
}

TEST(NumberCalculator, Division_ThatEnds_HasNoTrailingZeros)
{
	EXPECT_EQ(Shown("10/4="), wxT("2.5"));
}

TEST(NumberCalculator, Division_ThatDoesNotEnd_IsCutAtTheResultScale)
{
	EXPECT_EQ(Shown("1/3="), wxT("0.3333333333"));
	EXPECT_EQ(Shown("2/3="), wxT("0.6666666667"));   // half away from zero
}

TEST(NumberCalculator, Division_ByZero_IsAnError_NotACrash)
{
	ibNumberCalculator calc;
	Type(calc, "5/0=");
	EXPECT_TRUE(calc.HasError());
	EXPECT_EQ(calc.Value(), ibNumber(0));
}

TEST(NumberCalculator, Error_IsLeftByTheNextDigit)
{
	ibNumberCalculator calc;
	Type(calc, "5/0=");
	ASSERT_TRUE(calc.HasError());
	Type(calc, "7");
	EXPECT_FALSE(calc.HasError());
	EXPECT_EQ(calc.Display(), wxT("7"));
}

TEST(NumberCalculator, Chain_IsWorkedLeftToRight_LikeAPocketCalculator)
{
	// 2 + 3 * 4 is (2 + 3) * 4 = 20 on a pocket calculator, not 14.
	EXPECT_EQ(Shown("2+3*4="), wxT("20"));
}

TEST(NumberCalculator, OperatorPressedTwice_ReplacesTheFirst)
{
	EXPECT_EQ(Shown("8+*2="), wxT("16"));
}

TEST(NumberCalculator, Equals_WithNoOperator_KeepsTheEntry)
{
	EXPECT_EQ(Shown("42="), wxT("42"));
}

TEST(NumberCalculator, Value_WhileTyping_IsTheTypedNumber)
{
	ibNumberCalculator calc;
	Type(calc, "12.5");
	EXPECT_EQ(calc.Value(), ibNumber(wxString(wxT("12.5"))));
}

TEST(NumberCalculator, Value_AfterAnOperatorWithNoOperand_IsTheAccumulated)
{
	// "12 +" — ending on an operator does not invent an operand: OK takes 12.
	ibNumberCalculator calc;
	Type(calc, "12+");
	EXPECT_EQ(calc.Value(), ibNumber(12));
	EXPECT_EQ(calc.Pending(), Op::Add);
}

TEST(NumberCalculator, Value_OfATrailingPoint_IsTheWholeNumber)
{
	ibNumberCalculator calc;
	Type(calc, "12.");
	EXPECT_EQ(calc.Value(), ibNumber(12));
}

TEST(NumberCalculator, Initial_IsShownAsIs_AndTheFirstDigitReplacesIt)
{
	ibNumberCalculator calc(ibNumber(wxString(wxT("1500.25"))));
	EXPECT_EQ(calc.Display(), wxT("1500.25"));
	Type(calc, "9");
	EXPECT_EQ(calc.Display(), wxT("9"));
}

TEST(NumberCalculator, Initial_IsCarriedIntoAnOperation)
{
	EXPECT_EQ(Shown("*2=", ibNumber(wxString(wxT("1500.25")))), wxT("3000.5"));
}

TEST(NumberCalculator, Backspace_TakesOffTheLastCharacter)
{
	EXPECT_EQ(Shown("125<"), wxT("12"));
	EXPECT_EQ(Shown("1.5<<"), wxT("1"));
	EXPECT_EQ(Shown("5<"), wxT("0"));
}

TEST(NumberCalculator, Backspace_OnAResult_DoesNothing)
{
	EXPECT_EQ(Shown("2+2=<"), wxT("4"));
}

TEST(NumberCalculator, Negate_WhileTyping_TogglesTheSign)
{
	EXPECT_EQ(Shown("5~"), wxT("-5"));
	EXPECT_EQ(Shown("5~~"), wxT("5"));
}

TEST(NumberCalculator, Negate_OfAResult_FlipsIt)
{
	EXPECT_EQ(Shown("2+3=~"), wxT("-5"));
}

TEST(NumberCalculator, NegativeEntry_IsUsedInTheOperation)
{
	EXPECT_EQ(Shown("10+5~="), wxT("5"));
}

TEST(NumberCalculator, Clear_ResetsEverything)
{
	ibNumberCalculator calc;
	Type(calc, "12+34C");
	EXPECT_EQ(calc.Display(), wxT("0"));
	EXPECT_EQ(calc.Pending(), Op::None);
	Type(calc, "5=");
	EXPECT_EQ(calc.Display(), wxT("5"));
}

TEST(NumberCalculator, Typing_IsBoundedInLength)
{
	ibNumberCalculator calc;
	Type(calc, "123456789012345678901234567890");
	EXPECT_EQ(calc.Display().Length(), 20u);
}

TEST(NumberCalculator, NegativeZero_IsShownAsZero)
{
	EXPECT_EQ(Shown("0~="), wxT("0"));
}
