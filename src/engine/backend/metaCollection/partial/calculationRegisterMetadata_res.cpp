#include "calculationRegister.h"

static const ibString s_calculationRegister_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='2.5' fill='#7048e8'/><rect x='2.8' y='4.8' width='10.4' height='6.4' rx='0.8' fill='none' stroke='#ffffff' stroke-width='1.1'/><circle cx='8' cy='8' r='1.6' fill='#ffffff'/></svg>");

ibPicture ibValueMetaObjectCalculationRegister::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectCalculationRegister::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_calculationRegister_svg);

	return picture;
}
