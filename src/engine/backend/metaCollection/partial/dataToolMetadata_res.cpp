#include "dataTool.h"

static const ibString s_dataTool_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M4 4 H11.5 M12 4 V11.5' fill='none' stroke='#5a7b95' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='4' cy='4' r='3' fill='#2f9e44'/><path d='M12 0.8 L15.2 4 L12 7.2 L8.8 4 Z' fill='#f08c00'/><rect x='9' y='9' width='6' height='6' rx='1' fill='#1c6fd1'/></svg>");

ibPicture ibValueMetaObjectDataTool::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDataTool::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_dataTool_svg);

	return picture;
}
