#include "metaSessionParameterObject.h"

// THE ATTRIBUTE'S SHAPE, IN ANOTHER COLOUR. A session parameter IS an attribute —
// same editor, same type page, same everything a declaration needs — so the icon
// keeps the attribute's plate, in amber, with a person on it: the session it
// belongs to. A different picture would claim a different kind of thing; the same
// picture would hide that this one belongs to the session rather than to a table.

static const ibString s_sessionParameter_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#f08c00'/><circle cx='5' cy='6.6' r='1.3' fill='#ffffff'/><path d='M2.6 10.6 Q2.6 8.6 5 8.6 Q7.4 8.6 7.4 10.6 Z' fill='#ffffff'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectSessionParameter::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSessionParameter::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_sessionParameter_svg);

	return picture;
}