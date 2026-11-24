#include "metaFormObject.h"

static const ibString s_form_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#1c6fd1'/><rect x='2' y='5' width='12' height='8' rx='0.5' fill='#a5d8ff'/><rect x='3.5' y='6.5' width='4' height='2' rx='0.5' fill='#ffffff'/><rect x='3.5' y='10' width='4' height='2' rx='0.5' fill='#ffffff'/><rect x='8.5' y='6.5' width='4' height='2' rx='0.5' fill='#1c6fd1'/><rect x='8.5' y='10' width='4' height='2' rx='0.5' fill='#1c6fd1'/></svg>");

ibPicture ibValueMetaObjectForm::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectForm::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_form_svg);

	return picture;
}

ibPicture ibValueMetaObjectSharedForm::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSharedForm::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_form_svg);

	return picture;
}