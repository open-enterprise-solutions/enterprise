#include "form.h"

#include "backend/backend_picture.h"

static const ibString s_form_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#1c6fd1'/><rect x='2' y='5' width='12' height='8' rx='0.5' fill='#a5d8ff'/><rect x='11' y='3.1' width='2.4' height='1' rx='0.4' fill='#ffffff'/></svg>";

ibPicture ibValueForm::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueForm::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_form_svg);
	return picture;
}