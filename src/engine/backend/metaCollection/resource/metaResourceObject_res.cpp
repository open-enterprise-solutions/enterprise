#include "metaResourceObject.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picResourceCLSID — the core's drawing of a resource, a settings field's too

ibPicture ibValueMetaObjectResource::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectResource::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picResourceCLSID);
}
