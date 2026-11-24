#include "tableBox.h"

#include "backend/backend_picture.h"

static const ibString s_tableBoxColumn_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1.5' width='14' height='13' rx='1.2' fill='#5a7b95'/><rect x='2' y='5' width='12' height='8.5' rx='0.3' fill='#a8bac8'/><path d='M2 9.2 H14 M6.3 5 V13.5 M10.3 5 V13.5' fill='none' stroke='#ffffff' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/><rect x='6.3' y='1.5' width='4' height='13' rx='0.6' fill='#e03131'/><path d='M6.3 9.2 H10.3' fill='none' stroke='#ffffff' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

ibPicture ibValueModelTableBoxColumn::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueModelTableBoxColumn::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_tableBoxColumn_svg);
	return picture;
}