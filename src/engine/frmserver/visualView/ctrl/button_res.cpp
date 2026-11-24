#include "widgets.h"

#include "backend/backend_picture.h"

static const ibString s_button_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#1c6fd1'/><ellipse cx='5.8' cy='8' rx='1.7' ry='2.1' fill='none' stroke='#ffffff' stroke-width='1.3'/><path d='M9.2 5.9 V10.1 M11.8 5.9 L9.5 8 L11.8 10.1' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>";

ibPicture ibValueButton::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueButton::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_button_svg);
	return picture;
}