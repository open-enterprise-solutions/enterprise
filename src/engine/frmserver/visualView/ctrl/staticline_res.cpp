#include "widgets.h"

#include "backend/backend_picture.h"

static const ibString s_staticLine_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAhklEQVR4nOzToRGAQBDF0NwNJaFQlI5C0RM0AA2QPLnrMvMnchO5AiBXAOQKgFwBkCsAcgVArgDIFQC5AiBXAOQKgFwBkCsAcsvXY932mx+5zmO83ZsAcgVArgDI6QMM5JoAcgVArgDIFQC5AiBXAOQKgFwBkCsAcgVArgDIFQC5AiCnD/AAAAD//82BpW0AAAAGSURBVAMACI0Egv3h7FoAAAAASUVORK5CYII=";

ibPicture ibValueStaticLine::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueStaticLine::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_staticLine_png, ibSize(16, 16));
	return picture;
}