#ifndef _FRMCLIENT_PICTURE_H__
#define _FRMCLIENT_PICTURE_H__

#include <cstdint>
#include <vector>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/string.h>

#include "core/core.h"    // ibPictureID — a backend picture's number, the id of a row's picture
#include "frmclient/frmclient.h"

// ⭐ A PICTURE AS THE WIRE NAMES IT — by its id: a backend picture's number in decimal, a configuration's guid and its
// version, `<guid>#<version>` (ibBackendPicture::SendPicture); a picture with no id of its own (a file's) is its PNG in
// base64 itself. Drawn from what this client keeps: the engine's own are the core's (registered as it loads,
// core/picturePredefined.cpp), any other is asked of the server by its id the first time it is named
// (ibProtocol::ibMethod::Picture) and kept with its version — one the server has none of as well, so it is not asked
// again; one named by another version is asked anew and kept in its place. An empty bitmap when there is none.
FRMCLIENT_API wxBitmap ibProtocolPicture(const wxString& value);
// …a backend picture by its number — a row's state picture (ibDataViewModel::GetRowPicture).
FRMCLIENT_API wxBitmap ibProtocolPictureById(ibPictureID id);
// …and the pictures a list names, before it is drawn — a tree's, a palette's, a choice list's: those not kept yet asked
// of the server in ONE call, and kept.
FRMCLIENT_API void     ibProtocolPictureReceive(const std::vector<wxString>& ids);
// …and every picture an answer names (a frame, a patch: its Picture, Icon, …Picture fields, however deep), before any of
// it is drawn — in ONE call, so the parts drawn after it (the templates, the tree, a command bar) find theirs kept and
// ask nothing each.
FRMCLIENT_API void     ibProtocolPictureReceive(const class ibProtocolNode& answer);

// A picture's data as an image, to be scaled first — what the art provider makes its pictures of (artProvider/).
FRMCLIENT_API wxImage ibProtocolImage(const wxString& base64);

#endif
