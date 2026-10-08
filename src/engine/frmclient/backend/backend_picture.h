#ifndef _FRMCLIENT_BACKEND_PICTURE_H__
#define _FRMCLIENT_BACKEND_PICTURE_H__

#include <wx/bitmap.h>
#include <wx/icon.h>

#include "core/clsid.h"
#include "frmclient/win/picture.h"   // a picture by its id, as the server gave it

// A BACKEND PICTURE — the engine's ibBackendPicture (backend/backend_picture.h) as a window of the client's asks it: the
// picture of that id the server gave this client (the frame's Pictures, a question's Picture), none until it has.
class ibBackendPicture {
public:
	static wxBitmap GetPicture(ibPictureID id) {
		return ibProtocolPictureById(id);
	}

	static wxIcon GetPictureAsIcon(ibClassID id) {
		wxIcon icon;
		const wxBitmap picture = ibProtocolPicture(wxString::Format(wxT("%llu"), static_cast<unsigned long long>(id)));
		if (picture.IsOk())
			icon.CopyFromBitmap(picture);
		return icon;
	}
};

#endif
