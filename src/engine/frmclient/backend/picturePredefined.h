#ifndef _FRMCLIENT_BACKEND_PICTURE_PREDEFINED_H__
#define _FRMCLIENT_BACKEND_PICTURE_PREDEFINED_H__

#include "core/clsid.h"
#include "frmclient/win/picture.h"   // ibPictureID

// THE ENGINE'S OWN PICTURES, by number — its backend/picturePredefined.h, as far as the client's windows name one. The
// picture itself is the server's: it is sent with what shows it, and kept by its number (ibBackendPicture).
constexpr ibPictureID g_picErrorCLSID = picture_to_clsid("PC_ERROR");
constexpr ibPictureID g_picUpdateFormCLSID = picture_to_clsid("PC_REFRE");
constexpr ibPictureID g_picChangeFormCLSID = picture_to_clsid("PC_CHAGF");
constexpr ibPictureID g_picSelectCLSID = picture_to_clsid("PC_SELVL");
constexpr ibPictureID g_picFilterSetCLSID = picture_to_clsid("PC_FLTES");
constexpr ibPictureID g_picFilterClearCLSID = picture_to_clsid("PC_FLTEC");
constexpr ibPictureID g_picUserCLSID = picture_to_clsid("PC_USER");
constexpr ibPictureID g_picAuthenticationCLSID = picture_to_clsid("PC_ATTON");

#endif
