#include "chartOfCalculationTypes.h"

/* The chart's book, orange, with a dial: the kinds of calculation */
static const ibString s_chartOfCalculationTypes_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='1' width='12' height='14' rx='1.5' fill='#f76707'/><path d='M3.5 1 H5 V15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#d9480f'/><circle cx='9.5' cy='8' r='3' fill='none' stroke='#ffffff' stroke-width='1.3'/><path d='M9.5 6.4 V9.6' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectChartOfCalculationTypes::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectChartOfCalculationTypes::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_chartOfCalculationTypes_svg);

	return picture;
}
