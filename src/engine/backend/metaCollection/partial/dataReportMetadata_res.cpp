#include "dataReport.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picReportCLSID — the core's drawing of a report, the Reports group's as well

ibPicture ibValueMetaObjectReport::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectReport::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picReportCLSID);
}
