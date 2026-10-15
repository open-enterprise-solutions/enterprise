#ifndef _FRMCLIENT_BACKEND_PICTURE_H__
#define _FRMCLIENT_BACKEND_PICTURE_H__

#include <wx/bitmap.h>
#include <wx/icon.h>

#include "core/clsid.h"
#include "frmclient/win/picture.h"   // a picture by its id, as the client has it

// A BACKEND PICTURE — the engine's ibBackendPicture (backend/backend_picture.h) as a window of the client's asks it: the
// picture of that id this client has (ibProtocolPicture) — the engine's own, registered in the core as it loads
// (core/picturePredefined.cpp), and the ones the server gave it, asked for by their ids.
class ibBackendPicture {
public:
	static wxBitmap GetPicture(ibPictureID id) {
		return ibProtocolPictureById(id);
	}

	static wxIcon GetPictureAsIcon(ibClassID id) {
		wxIcon icon;
		const wxBitmap picture = GetPicture(id);
		if (picture.IsOk())
			icon.CopyFromBitmap(picture);
		return icon;
	}

	static wxImage GetImageFromBase64(const wxString& src, const wxSize& size = wxDefaultSize) {
		const wxImage image = ibProtocolImage(src);
		return size != wxDefaultSize && image.IsOk() ? image.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH) : image;
	}
};

#endif
