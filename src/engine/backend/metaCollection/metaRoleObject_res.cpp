#include "metaRoleObject.h"

static const ibString s_role_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='5.2' cy='5.2' r='4' fill='#f08c00'/><circle cx='4.6' cy='4.6' r='1.3' fill='#ffffff'/><path d='M7.5 7.5 L14 14' fill='none' stroke='#f08c00' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'/><path d='M10.8 10.8 L12.8 8.8 M12.6 12.6 L14.4 10.8' fill='none' stroke='#f08c00' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectRole::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectRole::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_role_svg);

	return picture;
}
