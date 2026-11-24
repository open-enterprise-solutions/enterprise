#include "metaObjectMetadata.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picApplicationCLSID — the core's drawing of an application, the launcher's too

ibPicture ibValueMetaObjectApplication::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectApplication::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picApplicationCLSID);
}
