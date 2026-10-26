#ifndef _FRMCLIENT_PICTURE_H__
#define _FRMCLIENT_PICTURE_H__

#include <cstdint>
#include <vector>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/string.h>

#include "core/core.h"    // ibPictureID — a backend picture's number, the id of a row's picture
#include "frmclient/frmclient.h"

class ibDataNode;
class ibDataValue;
class ibMemoryBuffer;
class ibProtocolNode;

// ⭐ A PICTURE AS THE WIRE NAMES IT — a field of a node: its id, a String — a backend picture's number in decimal, a
// configuration's guid and its version, `<guid>#<version>` (ibBackendPicture::SendPicture) — or, a picture with no id of
// its own (a file's), its PNG itself, a Binary. Drawn from what this client keeps: the engine's own are the core's
// (registered as it loads, core/picturePredefined.cpp), any other is asked of the server by its id the first time it is
// named (ibProtocol::ibMethod::Picture) and kept with its version — one the server has none of as well, so it is not
// asked again; one named by another version is asked anew and kept in its place. An empty bitmap when there is none.
FRMCLIENT_API wxBitmap ibProtocolPicture(const ibProtocolNode& node, const char* name);
// …a field of an engine's node — a property's row (ibBackendProperty::GetProtocolValue).
FRMCLIENT_API wxBitmap ibProtocolPicture(const ibDataNode& node, const char* name);
// …a field's value as it was read — kept apart from its node (a settings field's).
FRMCLIENT_API wxBitmap ibProtocolPicture(const ibDataValue& value);
// …a picture by its id alone — one the client holds (a palette's, a list collected before it is drawn).
FRMCLIENT_API wxBitmap ibProtocolPicture(const wxString& id);
// …a backend picture by its number — a row's state picture (ibDataViewModel::GetRowPicture).
FRMCLIENT_API wxBitmap ibProtocolPictureById(ibPictureID id);
// …and the pictures a list names, before it is drawn — a tree's, a palette's, a choice list's: those not kept yet asked
// of the server in ONE call, and kept.
FRMCLIENT_API void     ibProtocolPictureReceive(const std::vector<wxString>& ids);
// …and every picture an answer names (a frame, a patch: its Picture, Icon, …Picture fields, however deep), before any of
// it is drawn — in ONE call, so the parts drawn after it (the templates, the tree, a command bar) find theirs kept and
// ask nothing each.
FRMCLIENT_API void     ibProtocolPictureReceive(const ibProtocolNode& answer);

// A PICTURE'S PNG as an image, to be scaled first — none: the bytes are no PNG.
FRMCLIENT_API wxImage ibProtocolImage(const ibMemoryBuffer& png);

#endif
