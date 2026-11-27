#include "documentNumerator.h"

/* SVG */
static const ibString s_documentNumerator_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H10 L13.5 4.5 V14 Q13.5 15 12.5 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#1c6fd1'/><path d='M4.5 6 H11 M4.5 8.5 H11 M4.5 11 H8.5' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/><path d='M10 1 V4.5 H13.5' fill='#74c0fc'/></svg>");

ibPicture ibValueMetaObjectDocumentNumerator::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDocumentNumerator::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_documentNumerator_svg);

	return picture;
}
