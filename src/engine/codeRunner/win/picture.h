#ifndef _FRONTEND_PICTURE_H__
#define _FRONTEND_PICTURE_H__

////////////////////////////////////////////////////////////////////////////
// A PICTURE AS DATA, DRAWN ON THE DESKTOP — the core and the server keep a picture as its PNG (core/graphics/picture.h)
// and have no wx image; a window makes its own from one, anew every time, as big as the picture is — a size is asked
// of the core (ibPicture::FromBase64, ibPicture::Scale). The thin client's twin is frmclient/win/picture.h (ibProtocolImage).
////////////////////////////////////////////////////////////////////////////

#include <wx/bitmap.h>
#include <wx/icon.h>

#include "core/graphics/picture.h"   // ibPicture — a picture as data

#include <wx/wx.h>

wxBitmap ToBitmap(const ibPicture& picture);
wxIcon ToIcon(const ibPicture& picture);
// …and a wx image's PNG as Base-64 text — for a page that takes a picture in a data URI (the desktop's web view), never the wire.
ibString CreateBase64Image(const wxImage& image);

#endif
