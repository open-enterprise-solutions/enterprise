#include "catalog.h"

static const ibString s_catalog_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='1' width='12' height='14' rx='1.5' fill='#f76707'/><path d='M3.5 1 H5 V15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#d9480f'/><path d='M7.5 5 H11.5 M7.5 7.5 H11.5' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectCatalog::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectCatalog::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_catalog_svg);

	return picture;
}