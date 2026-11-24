#include "metaCommandObject.h"
#include "core/graphics/picture.h"   // ibPicture::FromSvg — the drawing, at 16 once

static const ibString s_command_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='12' height='8' rx='2' fill='#1c6fd1'/><path d='M4 6 H10' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><path d='M8.5 7.5 V15.2 L10.3 13.5 L11.6 16 L13.2 15.2 L12 12.8 H14.5 Z' fill='#5a7b95' stroke='#ffffff' stroke-width='1' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectCommand::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectCommand::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_command_svg);

	return picture;
}
