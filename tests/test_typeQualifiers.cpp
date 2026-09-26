// =============================================================================
// A type named at run time, and the qualifiers a script gives it.
//
// Two defects, both measured 2026-09-21 on a running copy:
//
//   * A description a script built with no qualifier took ibTypeData's own defaults - ten digits and no
//     fraction, ten characters, a date without its time - which are the designer's defaults for a NEW
//     ATTRIBUTE. `New TypeDescription("String")` cut a text to ten characters, "Number" rounded to a
//     whole number, and a value table's column added without a type kept ten characters of anything.
//   * The qualifiers had no Init of their own, so `New QualifierNumber(15, 2)` ignored its arguments and
//     no fraction could be declared from a script at all.
//
// What these pin: no qualifier limits nothing (ibValueTypeDescription::Unqualified); a qualifier takes
// what it is given and refuses what it cannot hold; and the designer's defaults still mean what they did
// for the attributes that carry them.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/value.h"
#include "backend/system/value/valueType.h"
#include "backend/backend_exception.h"

namespace {

ibValue AdjustedTo(const ibTypeDescription& type, const ibValue& value)
{
	return ibValueTypeDescription::AdjustValue(type, value);
}

} // namespace

TEST(TypeQualifiers, NoQualifierLimitsNothing)
{
	const ibTypeDescription::ibTypeData none = ibValueTypeDescription::Unqualified();
	EXPECT_EQ(none.GetPrecision(), 0) << "precision 0 is no limit, as a string's length 0 is";
	EXPECT_EQ(none.GetLength(), 0);
	EXPECT_EQ(none.GetDateFraction(), ibDateFractions::ibDateFractions_DateTime);

	EXPECT_EQ(AdjustedTo(ibTypeDescription(g_valueNumberCLSID, none), ibValue(0.25)).GetString(), wxT("0.25"))
		<< "an unqualified number is not rounded";
	const wxString text = wxT("abcdefghijklmnopqrstuvwxyz");
	EXPECT_EQ(AdjustedTo(ibTypeDescription(g_valueStringCLSID, none), ibValue(text)).GetString(), text)
		<< "an unqualified string is not cut";
}

TEST(TypeQualifiers, TheDesignersDefaultsStillMeanWhatTheyDid)
{
	// A new attribute's defaults are a column width in the database; they are not what a script meant by
	// "a number", but they are what an attribute declared with them has always been adjusted to.
	EXPECT_EQ(AdjustedTo(ibTypeDescription(g_valueNumberCLSID), ibValue(0.25)).GetInteger(), 0);
	EXPECT_EQ(AdjustedTo(ibTypeDescription(g_valueStringCLSID), ibValue(wxString(wxT("abcdefghijklmnop")))).GetString(),
		wxT("abcdefghij"));
}

TEST(TypeQualifiers, AQualifierTakesItsArguments)
{
	ibValuePtr<ibValueQualifierNumber> number(new ibValueQualifierNumber());
	ibValue precision(15), scale(2);
	ibValue* numberArgs[] = { &precision, &scale };
	ASSERT_TRUE(number->Init(numberArgs, 2));
	EXPECT_EQ(number->m_qNumber.m_precision, 15);
	EXPECT_EQ(number->m_qNumber.m_scale, 2);
	EXPECT_EQ(AdjustedTo(ibTypeDescription(g_valueNumberCLSID, number->m_qNumber, ibQualifierDate(), ibQualifierString()),
		ibValue(0.256)).GetString(), wxT("0.26")) << "a declared fraction is kept to its scale";

	ibValuePtr<ibValueQualifierString> string(new ibValueQualifierString());
	ibValue length(3);
	ibValue* stringArgs[] = { &length };
	ASSERT_TRUE(string->Init(stringArgs, 1));
	EXPECT_EQ(string->m_qString.m_length, 3);
}

TEST(TypeQualifiers, AQualifierGivenNothingLimitsNothing)
{
	ibValuePtr<ibValueQualifierNumber> number(new ibValueQualifierNumber());
	ASSERT_TRUE(number->Init());
	EXPECT_EQ(number->m_qNumber.m_precision, 0);

	ibValuePtr<ibValueQualifierString> string(new ibValueQualifierString());
	ASSERT_TRUE(string->Init());
	EXPECT_EQ(string->m_qString.m_length, 0);
}

TEST(TypeQualifiers, AQualifierRefusesWhatItCannotHold)
{
	ibValuePtr<ibValueQualifierNumber> number(new ibValueQualifierNumber());
	ibValue precision(10), tooManyAfterThePoint(12);
	ibValue* args[] = { &precision, &tooManyAfterThePoint };
	EXPECT_THROW(number->Init(args, 2), ibBackendException) << "more digits after the point than in all";

	ibValue negative(-1);
	ibValue* negativeArgs[] = { &negative };
	EXPECT_THROW(number->Init(negativeArgs, 1), ibBackendException);
}
