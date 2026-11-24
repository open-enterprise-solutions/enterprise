#include "constant.h"

static const ibString s_constant_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#5a7b95'/><path d='M2.8 6.2 H7.8' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='butt' stroke-linejoin='round'/><path d='M4.3 6.4 V10' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><path d='M6.5 6.4 C6.5 8.4 6.6 9.8 7.6 9.8' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectConstant::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectConstant::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_constant_svg);
	
	return picture;
}