#include "metaEventHandlerObject.h"
#include "core/graphics/picture.h"   // ibPicture::FromSvg — the drawing, at 16 once

/* A bell, violet: what answers an event */
static const ibString s_eventHandler_svg =ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M8 1.5 Q12.5 1.5 12.5 7 V10 L14.5 12.5 H1.5 L3.5 10 V7 Q3.5 1.5 8 1.5 Z' fill='#7048e8'/><circle cx='8' cy='14' r='1.8' fill='#7048e8'/></svg>");

ibPicture ibValueMetaObjectEventHandler::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectEventHandler::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_eventHandler_svg);

	return picture;
}
