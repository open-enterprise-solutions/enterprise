#include "metaAttributeObject.h"

static const ibString s_attribute_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#1c6fd1'/><path d='M3.5 8 H7.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

// THE DEFAULT COLUMN PICTURE — declared on both column faces (srcColumn.h, queryColumn.h) and defined
// here, beside the picture it names. Any column that is not a metaobject — a view's column, a temp
// table's, a synthetic projection — reads as a plain attribute, which is what it is from the
// reader's point of view. Metaobject columns override and wear their own.
ibPictureDescription ibBackendSourceColumn::GetColumnPicture() const
{
	return g_metaAttributeCLSID;
}

ibPictureDescription ibBackendQueryColumn::GetColumnPicture() const
{
	return g_metaAttributeCLSID;
}

ibPicture ibValueMetaObjectAttribute::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectAttribute::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_attribute_svg);

	return picture;
}
