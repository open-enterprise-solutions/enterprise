#include "metaTemplateObject.h"

static const ibString s_grid_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='1.5' fill='#2f9e44'/><path d='M1 5.5 H15 M1 10 H15 M5.5 1 V15 M10 1 V15' fill='none' stroke='#ffffff' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/><rect x='5.5' y='5.5' width='4.5' height='4.5' fill='#8ce99a'/></svg>");

ibPicture ibValueMetaObjectTemplateBase::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectTemplateBase::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_grid_svg);

	return picture;
}