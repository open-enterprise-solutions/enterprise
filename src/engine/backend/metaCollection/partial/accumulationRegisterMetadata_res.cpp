#include "accumulationRegister.h"

static const ibString s_accumRegister_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='2.5' fill='#2f9e44'/><rect x='3.5' y='9' width='2.4' height='3.5' fill='#ffffff'/><rect x='6.8' y='6.5' width='2.4' height='6' fill='#ffffff'/><rect x='10.1' y='3.5' width='2.4' height='9' fill='#ffffff'/></svg>");

ibPicture ibValueMetaObjectAccumulationRegister::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectAccumulationRegister::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_accumRegister_svg);

	return picture;
}
