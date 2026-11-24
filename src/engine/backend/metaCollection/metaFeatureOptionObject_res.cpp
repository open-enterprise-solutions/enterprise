#include "metaFeatureOptionObject.h"

// THE CONSTANT'S PLATE, WITH A SWITCH FOR ITS SIGN. An option IS a constant, so it keeps the plate the
// constant and the session parameter share and the stroke on the right that stands for the value; where
// the constant shows π, the option shows what its value is — on or off.

static const ibString s_featureOption_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#5a7b95'/><rect x='2.5' y='5.7' width='6.2' height='4.6' rx='2.3' fill='#ffffff'/><circle cx='6.4' cy='8' r='1.5' fill='#2f9e44'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectFeatureOption::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectFeatureOption::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_featureOption_svg);

	return picture;
}
