#include "document.h"

static const ibString s_document_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3.5 1 H10 L14 5 V13.5 Q14 15 12.5 15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#1c6fd1'/><path d='M10 1 V3.8 Q10 5 11.2 5 H14 Z' fill='#a5d8ff'/><path d='M4.5 7.5 H11' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><path d='M4.5 10 H8' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><circle cx='10.6' cy='11.6' r='1.7' fill='none' stroke='#ffffff' stroke-width='1.2'/></svg>");

ibPicture ibValueMetaObjectDocument::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDocument::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_document_svg);

	return picture;
}
