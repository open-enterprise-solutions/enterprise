#include "metaScheduledJobObject.h"
#include "core/graphics/picture.h"   // ibPicture::FromSvg — the drawing, at 16 once

static const ibString s_scheduledJob_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2.6 4.2 L4.6 2.4' fill='none' stroke='#e03131' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><path d='M13.4 4.2 L11.4 2.4' fill='none' stroke='#e03131' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><circle cx='8' cy='9' r='5.5' fill='#e03131'/><path d='M8 6 V9 H10.3' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectScheduledJob::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectScheduledJob::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_scheduledJob_svg);

	return picture;
}
