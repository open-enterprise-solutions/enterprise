#ifndef _FRMCLIENT_PICTURE_H__
#define _FRMCLIENT_PICTURE_H__

#include <cstdint>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/string.h>

#include "core/types.h"   // ibPictureID — a backend picture's number, the id of a row's picture
#include "frmclient/frmclient.h"

// A PICTURE AS THE WIRE CARRIES IT — by its id, as a frame names a picture the client was given once (a command's,
// a button's, a column's: the answer's Pictures, kept here), or a PNG in base64 itself (a tab's Icon, a choice's
// Picture, a schema item's Icon: the server's ibServerPicture), made a bitmap; an empty one when there is none.
FRMCLIENT_API wxBitmap ibProtocolPicture(const wxString& value);
// …and as an image, to be scaled first — what the art provider makes its pictures of (artProvider/).
FRMCLIENT_API wxImage ibProtocolImage(const wxString& base64);

// A PICTURE BY ITS ID — kept as it was given, once: by the answer that gave it (Pictures, beside the frame) and by a
// list's read (a row's state picture, ibDataViewModel::GetRowPicture). Drawn from here; an id not given draws nothing.

FRMCLIENT_API void     ibProtocolPictureKeep(const wxString& id, const wxBitmap& picture);
FRMCLIENT_API void     ibProtocolPictureKeep(ibPictureID id, const wxBitmap& picture);
FRMCLIENT_API wxBitmap ibProtocolPictureById(ibPictureID id);

#endif
