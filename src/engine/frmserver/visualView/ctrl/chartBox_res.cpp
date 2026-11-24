#include "chartBox.h"

#include "backend/backend_picture.h"

static const ibString s_chartBox_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='7.5' height='7.5' rx='1' fill='#e03131'/><circle cx='10.5' cy='10' r='4.6' fill='#fcc419'/><path d='M1 15 L4.5 9.5 L8 15 Z' fill='#1c6fd1'/></svg>";

ibPicture ibValueChartBox::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueChartBox::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_chartBox_svg);
	return picture;
}