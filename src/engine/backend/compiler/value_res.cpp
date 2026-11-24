#include "value.h"

#include "backend/backend_picture.h"
#include "core/picturePredefined.h"   // g_picValueCLSID — the core's drawing of a value, the desktop's as well

ibPicture ibValue::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValue::GetIconGroup()
{
	return ibBackendPicture::GetPicture(g_picValueCLSID);
}
