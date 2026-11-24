#include "metaHttpServiceObject.h"
#include "core/graphics/picture.h"   // ibPicture::FromSvg — the drawing, at 16 once

static const ibString s_httpService_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#1098ad'/><ellipse cx='8' cy='8' rx='2.8' ry='6.3' fill='none' stroke='#ffffff' stroke-width='1.1'/><path d='M1.5 8 H14.5 M2.6 4.6 H13.4 M2.6 11.4 H13.4' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/></svg>");
static const ibString s_urlTemplate_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M5 2 L1.5 14' fill='none' stroke='#5a7b95' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/><path d='M9.2 3 C7.6 3 7.6 4.2 7.6 5.8 C7.6 7.1 7.1 8 6 8 C7.1 8 7.6 8.9 7.6 10.2 C7.6 11.8 7.6 13 9.2 13 M11.8 3 C13.4 3 13.4 4.2 13.4 5.8 C13.4 7.1 13.9 8 15 8 C13.9 8 13.4 8.9 13.4 10.2 C13.4 11.8 13.4 13 11.8 13' fill='none' stroke='#1c6fd1' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'/></svg>");
static const ibString s_httpServiceMethod_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='8.5' y='1.5' width='6.5' height='13' rx='1.5' fill='#5a7b95'/><path d='M1.5 8 H9.5' fill='none' stroke='#2f9e44' stroke-width='1.9' stroke-linecap='round' stroke-linejoin='round'/><path d='M6.5 4.8 L9.7 8 L6.5 11.2' fill='none' stroke='#2f9e44' stroke-width='1.9' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12.8' cy='8' r='0.9' fill='#ffffff'/></svg>");

ibPicture ibValueMetaObjectHttpService::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectHttpService::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_httpService_svg);

	return picture;
}

ibPicture ibValueMetaObjectUrlTemplate::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectUrlTemplate::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_urlTemplate_svg);

	return picture;
}

ibPicture ibValueMetaObjectHttpServiceMethod::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectHttpServiceMethod::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_httpServiceMethod_svg);

	return picture;
}
