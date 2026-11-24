#include "metaSectionObject.h"

static const ibString s_interface_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#1c6fd1'/><rect x='2' y='5' width='12' height='8' rx='0.5' fill='#a5d8ff'/><rect x='2' y='5' width='4' height='8' fill='#ffffff'/><path d='M3 7 H5 M3 9 H5 M3 11 H5' fill='none' stroke='#1c6fd1' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/><path d='M7.5 7.5 H12 M7.5 10 H10.5' fill='none' stroke='#1c6fd1' stroke-width='1.2' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectSection::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSection::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_interface_svg);

	return picture;
}
