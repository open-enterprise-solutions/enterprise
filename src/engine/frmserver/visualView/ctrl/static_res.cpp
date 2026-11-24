#include "static.h"

#include "backend/backend_picture.h"

static const ibString s_static_svg ="<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='3.9' cy='10.2' r='1.9' fill='none' stroke='#5a7b95' stroke-width='1.4'/><path d='M5.8 8.3 V12.1 M7.9 4.6 V12.1' fill='none' stroke='#5a7b95' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><circle cx='9.8' cy='10.2' r='1.9' fill='none' stroke='#5a7b95' stroke-width='1.4'/><path d='M15 8.9 Q14.3 8 13.4 8.2 Q12.4 8.4 12.4 10.2 Q12.4 12 13.4 12.2 Q14.3 12.4 15 11.5' fill='none' stroke='#5a7b95' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";

ibPicture ibValueStatic::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueStatic::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_static_svg);
	return picture;
}