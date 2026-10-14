#ifndef _FRMCLIENT_BACKEND_PICTURE_H__
#define _FRMCLIENT_BACKEND_PICTURE_H__

#include <wx/bitmap.h>
#include <wx/icon.h>

#include "core/clsid.h"
#include "core/pictureRegistry.h"   // the engine's own pictures, as the core registered them
#include "frmclient/win/picture.h"   // a picture by its id, as the server gave it

// A BACKEND PICTURE — the engine's ibBackendPicture (backend/backend_picture.h) as a window of the client's asks it: the
// picture of that id this client has — the engine's own, registered in the core as it loads
// (core/picturePredefined.cpp), and the ones the server gave it (the frame's Pictures, a question's Picture).
class ibBackendPicture {
public:
	static wxBitmap GetPicture(ibPictureID id) {
		// A registered one is a master (tools/pictures/render.js), shown at 16.
		const ibPicture registered = ibPictureRegistry::GetPicture(id);
		if (registered.IsOk())
			return wxBitmap(GetImageFromBase64(registered.GetData(), wxSize(16, 16)));
		return ibProtocolPictureById(id);
	}

	static wxIcon GetPictureAsIcon(ibClassID id) {
		wxIcon icon;
		const wxBitmap picture = ibPictureRegistry::IsRegisterPicture(id) ? GetPicture(id)
			: ibProtocolPicture(wxString::Format(wxT("%llu"), static_cast<unsigned long long>(id)));
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
