#include "gridBox.h"

#include "backend/backend_picture.h"

static const ibString s_gridBox_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1.5' width='14' height='13' rx='1.2' fill='#5a7b95'/><rect x='2' y='5' width='12' height='8.5' rx='0.3' fill='#a8bac8'/><rect x='2' y='5' width='3' height='8.5' fill='#5a7b95'/><path d='M5 9.2 H14 M9.5 5 V13.5' fill='none' stroke='#ffffff' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

ibPicture ibValueGridBox::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueGridBox::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_gridBox_svg);
	return picture;
}