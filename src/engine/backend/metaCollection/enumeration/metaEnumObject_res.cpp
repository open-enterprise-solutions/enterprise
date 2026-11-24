#include "metaEnumObject.h"

static const ibString s_enumeration_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#ae3ec9'/><circle cx='5' cy='8' r='1.8' fill='#ffffff'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectEnum::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectEnum::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_enumeration_svg);

	return picture;
}