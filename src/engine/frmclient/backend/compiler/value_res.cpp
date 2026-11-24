#include "value.h"

#include "core/picturePredefined.h"             // g_picValueCLSID — the engine's drawing of a value
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture::GetPictureAsIcon — a picture by its id

// THE ENGINE'S OWN PICTURE OF A VALUE — the core's, by its id: a field of a settings window wears it.
wxIcon ibValue::GetIconGroup()
{
	return ibBackendPicture::GetPictureAsIcon(g_picValueCLSID);
}
