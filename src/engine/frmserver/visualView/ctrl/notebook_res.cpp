#include "notebook.h"

#include "backend/backend_picture.h"

static const ibString s_notebook_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='6.5' y='1.5' width='5' height='4.5' rx='1' fill='#a8bac8'/><rect x='1' y='1' width='6' height='5' rx='1' fill='#5a7b95'/><rect x='1' y='5' width='14' height='10' rx='1.2' fill='#5a7b95'/><rect x='2' y='6' width='12' height='8' rx='0.3' fill='#a8bac8'/></svg>";

ibPicture ibValueNotebook::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueNotebook::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_notebook_svg);
	return picture;
}