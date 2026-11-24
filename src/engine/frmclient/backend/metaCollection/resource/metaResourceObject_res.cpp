#include "metaResourceObject.h"

#include "core/picturePredefined.h"             // g_picResourceCLSID — the engine's drawing of a resource
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture::GetPictureAsIcon — a picture by its id

// THE ENGINE'S OWN PICTURE OF A RESOURCE — the core's, by its id: a field of a settings window declared a resource
// wears it.
wxIcon ibValueMetaObjectResource::GetIconGroup()
{
	return ibBackendPicture::GetPictureAsIcon(g_picResourceCLSID);
}
