#ifndef _FRMCLIENT_BACKEND_PICTURE_H__
#define _FRMCLIENT_BACKEND_PICTURE_H__

#include <wx/bitmap.h>
#include <wx/icon.h>

#include <vector>

#include "core/clsid.h"
#include "core/description/pictureDescription.h"   // ibExternalPictureDescription — a file's picture, carried whole
#include "frmclient/frmclient.h"
#include "frmclient/win/picture.h"   // a picture by its id, as the client has it

struct ibBackendPictureEntry {
	wxString m_name;
	wxBitmap m_data;
	ibPictureID m_id;
};

// A BACKEND PICTURE — the engine's ibBackendPicture (backend/backend_picture.h) as a window of the client's asks it: the
// picture of that id this client has (ibProtocolPicture) — the engine's own, registered in the core as it loads
// (core/picturePredefined.cpp), and the ones the server gave it, asked for by their ids. A file's picture is the
// client's: the file is on its machine, read here and sent whole in the value.
class FRMCLIENT_API ibBackendPicture {
public:
	static bool LoadFromFile(const wxString& strFileName, ibExternalPictureDescription& pictureDesc);
	static wxBitmap CreatePicture(const ibExternalPictureDescription& pictureDesc, const wxSize& size = wxSize(16, 16));

	// The pictures a picture property offers — the engine's, registered in the core.
	static std::vector<ibBackendPictureEntry> GetArrayPicture();

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

	// A PICTURE AS A DESCRIPTION SAYS IT — the desktop's. One of the engine's pictures by its number; a configuration's
	// by its guid, as the server names it (ibBackendPicture::SendPicture) and the client receives it (ibProtocolPicture).
	static wxIcon GetPictureAsIcon(const ibPictureDescription& pictureDesc, const class ibMetaData* WXUNUSED(metaData) = nullptr) {
		if (pictureDesc.m_type == ibPictureType::eFromBackend)
			return GetPictureAsIcon(pictureDesc.m_class_identifier);
		wxIcon icon;
		if (pictureDesc.m_type == ibPictureType::eFromConfiguration && pictureDesc.m_meta_guid.isValid()) {
			const wxBitmap picture = ibProtocolPicture(pictureDesc.m_meta_guid.str());
			if (picture.IsOk())
				icon.CopyFromBitmap(picture);
		}
		return icon;
	}

	static wxImage GetImageFromBase64(const wxString& src, const wxSize& size = wxDefaultSize) {
		const wxImage image = ibProtocolImage(src);
		return size != wxDefaultSize && image.IsOk() ? image.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH) : image;
	}
};

#endif
