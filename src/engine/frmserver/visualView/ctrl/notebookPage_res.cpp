#include "notebook.h"

#include "backend/backend_picture.h"

static const ibString s_notebookPage_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='6.5' height='4' rx='1' fill='#1c6fd1'/><rect x='1' y='3.5' width='14' height='11.5' rx='1.2' fill='#5a7b95'/><rect x='2' y='4.5' width='12' height='9.5' rx='0.3' fill='#a8bac8'/><path d='M3.5 7.5 H12.5 M3.5 9.8 H10.5 M3.5 12 H11.5' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

ibPicture ibValueNotebookPage::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueNotebookPage::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_notebookPage_svg);
	return picture;
}