#include "metaDimensionObject.h"

static const ibString s_dimension_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#f76707'/><circle cx='4.4' cy='8' r='1.6' fill='none' stroke='#ffffff' stroke-width='1.3'/><path d='M6 8 H8.4' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='butt' stroke-linejoin='round'/><path d='M7.7 8 V9.8' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectDimension::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDimension::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_dimension_svg);

	return picture;
}