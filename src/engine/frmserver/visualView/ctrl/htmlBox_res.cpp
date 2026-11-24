#include "htmlBox.h"

#include "backend/backend_picture.h"

static const ibString s_htmlBox_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2 1 H7.5 L11 4.5 V14 Q11 15 10 15 H2 Q1 15 1 14 V2 Q1 1 2 1 Z' fill='#5a7b95'/><path d='M7.5 1 V3.7 Q7.5 4.5 8.3 4.5 H11 Z' fill='#a8bac8'/><circle cx='10.6' cy='10.6' r='4.6' fill='#1098ad' stroke='#ffffff' stroke-width='0.8'/><ellipse cx='10.6' cy='10.6' rx='1.8' ry='4' fill='none' stroke='#ffffff' stroke-width='0.9'/><path d='M6.4 10.6 H14.8' fill='none' stroke='#ffffff' stroke-width='0.9' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

ibPicture ibValueHTMLBox::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueHTMLBox::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_htmlBox_svg);
	return picture;
}