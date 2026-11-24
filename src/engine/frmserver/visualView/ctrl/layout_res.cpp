#include "layout.h"

#include "backend/backend_picture.h"

static const ibString s_layout_svg ="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1.5' y='1.5' width='13' height='13' rx='1.5' fill='none' stroke='#1c6fd1' stroke-width='1.3'/><rect x='3.5' y='3.5' width='9' height='4' rx='0.8' fill='#74c0fc'/><rect x='3.5' y='8.5' width='9' height='4' rx='0.8' fill='#74c0fc'/></svg>";

ibPicture ibValueLayout::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueLayout::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_layout_svg);
	return picture;
}