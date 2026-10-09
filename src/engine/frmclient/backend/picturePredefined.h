#ifndef _FRMCLIENT_BACKEND_PICTURE_PREDEFINED_H__
#define _FRMCLIENT_BACKEND_PICTURE_PREDEFINED_H__

#include "core/clsid.h"
#include "frmclient/win/picture.h"   // ibPictureID

// THE ENGINE'S OWN PICTURES, by number — its backend/picturePredefined.h, as far as the client's windows name one. The
// picture itself is the server's: it is sent with what shows it, and kept by its number (ibBackendPicture).
constexpr ibPictureID g_picChangeFormCLSID = picture_to_clsid("PC_CHAGF");

#endif
