#include "chartOfCharacteristicTypes.h"

/* The chart's book, orange, with a tag: the kinds of characteristic */
static const ibString s_chartOfCharacteristicTypes_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='1' width='12' height='14' rx='1.5' fill='#f76707'/><path d='M3.5 1 H5 V15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#d9480f'/><path d='M6.7 5.4 H10.2 L12.8 8 L10.2 10.6 H6.7 Z' fill='#ffffff'/><circle cx='8.2' cy='8' r='0.9' fill='#f76707'/></svg>");

ibPicture ibValueMetaObjectChartOfCharacteristicTypes::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectChartOfCharacteristicTypes::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_chartOfCharacteristicTypes_svg);

	return picture;
}
