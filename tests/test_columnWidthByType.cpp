// =============================================================================
// OES Enterprise — a column's width by its type (docs/private/column-widths-by-type.md).
//
// A column whose width is not set asks for what its type needs, in characters: the server answers
// (ibBackendTypeApplicationFactory::GetWidthFromColumn) and each client turns it into pixels with its own
// font. A simple type is measured — a string, a number, a date, a boolean; a reference, and any other type
// that is not a simple one, takes one width for all; a composite type its widest member; no type, nothing.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include "backend/backend_type.h"
#include "backend/metaCollection/metaObject.h"   // g_metaCatalogCLSID — a reference's metaclass
#include "core/formatString.h"

namespace {

// The width a type is shown in through the format it gives itself — a column with no format of its own.
unsigned int WidthOf(const ibTypeDescription& type)
{
	ibFormatString format;
	ibFormatString::FromTypeDesc(type, format);
	return ibBackendTypeApplicationFactory::GetWidthFromColumn(format, type);
}

ibTypeDescription StringOf(unsigned short length)
{
	return ibTypeDescription(g_valueStringCLSID, ibTypeDescription::ibTypeData(length));
}

// A catalog's reference, by its id alone: no application is needed to say how wide one is.
constexpr ibClassID g_goodsRefCLSID = reference_to_clsid(42, clsid_metaclass(g_metaCatalogCLSID));

} // namespace

// A string is as wide as its length, kept between 3 and 40; one of no length is 30.
TEST(ColumnWidthByType, AStringIsItsLength)
{
	EXPECT_EQ(WidthOf(StringOf(5)), 5u);
	EXPECT_EQ(WidthOf(StringOf(1)), 3u);
	EXPECT_EQ(WidthOf(StringOf(100)), 40u);
	EXPECT_EQ(WidthOf(StringOf(0)), 30u);
}

// A tabular section's line number, Number(6,0) that holds no sign: six digits.
TEST(ColumnWidthByType, ALineNumberIsSixDigits)
{
	EXPECT_EQ(WidthOf(ibTypeDescription(g_valueNumberCLSID, ibTypeDescription::ibTypeData(6, 0, true))), 6u);
}

// A number is its widest figure as the format prints it: the sign, the point, the fraction, the group separators.
TEST(ColumnWidthByType, ANumberCountsItsFractionAndSeparators)
{
	const ibTypeDescription type(g_valueNumberCLSID, ibTypeDescription::ibTypeData(15, 2));
	ibFormatString format = ibFormatString::Parse(wxT("NGS= "));
	ibFormatString::FromTypeDesc(type, format);
	const unsigned int width = ibBackendTypeApplicationFactory::GetWidthFromColumn(format, type);
	EXPECT_EQ(width, format.Apply(ibNumber(ibString(wxT("-9999999999999.99")))).length());
	EXPECT_GT(width, 15u + 2u) << "the sign, the point and the group separators count";
}

// A number nobody bounded — an average, a product — asks for twelve.
TEST(ColumnWidthByType, AnUnboundedNumberIsTwelve)
{
	EXPECT_EQ(WidthOf(ibTypeDescription(g_valueNumberCLSID, ibTypeDescription::ibTypeData(0, 0))), 12u);
}

// A date is its pattern printed: a date alone, a time alone, both.
TEST(ColumnWidthByType, ADateIsItsPattern)
{
	EXPECT_EQ(WidthOf(ibTypeDescription(g_valueDateCLSID, ibTypeDescription::ibTypeData(ibDateFractions_Date))), 10u);
	EXPECT_EQ(WidthOf(ibTypeDescription(g_valueDateCLSID, ibTypeDescription::ibTypeData(ibDateFractions_Time))), 8u);
	EXPECT_EQ(WidthOf(ibTypeDescription(g_valueDateCLSID, ibTypeDescription::ibTypeData(ibDateFractions_DateTime))), 19u);
}

// A boolean is the longer of its texts, or a tick box.
TEST(ColumnWidthByType, ABooleanIsItsTexts)
{
	const ibTypeDescription type(g_valueBooleanCLSID);
	EXPECT_EQ(WidthOf(type), 3u);
	EXPECT_EQ(ibBackendTypeApplicationFactory::GetWidthFromColumn(ibFormatString::Parse(wxT("BF=No; BT=Yes")), type), 3u);
	EXPECT_EQ(ibBackendTypeApplicationFactory::GetWidthFromColumn(ibFormatString::Parse(wxT("BF=Disabled; BT=Enabled")), type), 8u);
}

// A reference is not measured: every one takes the width of a text whose length its type does not say.
TEST(ColumnWidthByType, AReferenceIsOneWidthForAll)
{
	EXPECT_EQ(WidthOf(ibTypeDescription(g_goodsRefCLSID)), 30u);
}

// A composite type asks for its widest member.
TEST(ColumnWidthByType, CompositeTakesItsWidest)
{
	EXPECT_EQ(WidthOf(ibTypeDescription(std::vector<ibClassID>{ g_valueStringCLSID, g_goodsRefCLSID }, ibTypeDescription::ibTypeData(5))), 30u);
	EXPECT_EQ(WidthOf(ibTypeDescription(std::vector<ibClassID>{ g_valueStringCLSID, g_valueDateCLSID }, ibTypeDescription::ibTypeData(5))), 10u)
		<< "a date alone, wider than String(5)";
}

// No type: nothing to show by type — the column's caption gives its width.
TEST(ColumnWidthByType, NoTypeGivesZero)
{
	EXPECT_EQ(WidthOf(ibTypeDescription()), 0u);
}
