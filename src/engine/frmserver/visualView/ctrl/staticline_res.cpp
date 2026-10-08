#include "widgets.h"

#include "backend/backend_picture.h"

static const wxString s_staticLine_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAhklEQVR4nOzToRGAQBDF0NwNJaFQlI5C0RM0AA2QPLnrMvMnchO5AiBXAOQKgFwBkCsAcgVArgDIFQC5AiBXAOQKgFwBkCsAcsvXY932mx+5zmO83ZsAcgVArgDI6QMM5JoAcgVArgDIFQC5AiBXAOQKgFwBkCsAcgVArgDIFQC5AiCnD/AAAAD//82BpW0AAAAGSURBVAMACI0Egv3h7FoAAAAASUVORK5CYII=";

wxIcon ibValueStaticLine::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_staticLine_png, wxSize(16, 16));
}

wxIcon ibValueStaticLine::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_staticLine_png, wxSize(16, 16));
}