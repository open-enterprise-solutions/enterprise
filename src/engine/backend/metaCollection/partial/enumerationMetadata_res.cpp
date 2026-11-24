#include "enumeration.h"

static const ibString s_enum_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='3' cy='4' r='1.7' fill='#ae3ec9'/><circle cx='3' cy='8' r='1.7' fill='#ae3ec9'/><circle cx='3' cy='12' r='1.7' fill='#ae3ec9'/><path d='M6.5 4 H14 M6.5 8 H14 M6.5 12 H11.5' fill='none' stroke='#5a7b95' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectEnumeration::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectEnumeration::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_enum_svg);

	return picture;
}
