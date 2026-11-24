#include "textBox.h"

#include "backend/backend_picture.h"

static const ibString s_textBox_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1.5' width='14' height='13' rx='1.5' fill='#1c6fd1'/><rect x='2' y='2.5' width='12' height='11' rx='0.6' fill='#ffffff'/><path d='M4 5.5 H12 M4 8 H12 M4 10.5 H9.5' fill='none' stroke='#1c6fd1' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

ibPicture ibValueTextBox::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueTextBox::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_textBox_svg);
	return picture;
}