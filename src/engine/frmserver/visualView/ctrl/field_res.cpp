#include "field.h"

#include "backend/backend_picture.h"

// A field: its title's lines on the left, the slot its view stands in on the right — one picture whatever the view.
static const ibString s_field_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='0.8' y='5.6' width='4.4' height='1.6' rx='0.8' fill='#5a7b95'/><rect x='0.8' y='8.8' width='3' height='1.6' rx='0.8' fill='#5a7b95'/><rect x='6.2' y='3' width='9' height='10' rx='1.5' fill='#1c6fd1'/><rect x='7.2' y='4' width='7' height='8' rx='0.7' fill='#ffffff'/><rect x='8.5' y='7' width='3.5' height='2' rx='0.7' fill='#1c6fd1'/></svg>";

ibPicture ibValueField::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueField::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_field_svg);
	return picture;
}
