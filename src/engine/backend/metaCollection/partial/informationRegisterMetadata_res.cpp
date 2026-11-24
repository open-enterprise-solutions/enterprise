#include "informationRegister.h"

static const ibString s_informationRegister_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='2.5' fill='#1c6fd1'/><circle cx='8' cy='4.6' r='1.4' fill='#ffffff'/><path d='M8 7.2 V12.4' fill='none' stroke='#ffffff' stroke-width='2.2' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectInformationRegister::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectInformationRegister::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_informationRegister_svg);

	return picture;
}
