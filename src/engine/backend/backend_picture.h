#ifndef __BACKEND_PICTURE_H__
#define __BACKEND_PICTURE_H__

#include "core/graphics/picture.h"   // ibPicture — a picture as data, what the server keeps and sends

#include "core/description/pictureDescription.h"
#include "core/picturePredefined.h"

#include "backend/backend_core.h"

struct ibBackendPictureEntry {
	wxString m_name;
	wxBitmap m_data;
	ibPictureID m_id;
};

class BACKEND_API ibBackendPicture {
	ibBackendPicture() = delete;
public:

	static void RegisterPicture(const wxString name, const ibPictureID& id, const wxBitmap& bitmap);

	static bool LoadFromFile(const wxString& strFileName, ibExternalPictureDescription& pictureDesc);
	static bool LoadFromFile(const wxString& strFileName, ibPictureDescription& pictureDesc);

	static wxBitmap CreatePicture(const ibExternalPictureDescription& pictureDesc, const wxSize& size = wxSize(16, 16));
	static wxBitmap CreatePicture(const ibPictureDescription& pictureDesc,
		const class ibMetaData* metaData = nullptr, const wxSize& size = wxSize(16, 16));

	// THE PICTURE A SERVER SENDS, by its description — never a wx image on a worker thread. A backend one (a registered
	// picture, a class's icon) is made once and kept here; a type of the configuration's own (a catalog's reference),
	// which no global class knows, is its constructor's icon in the configuration given; a configuration's picture and
	// a file's are made from what they keep.
	static ibPicture GetServerPicture(const ibPictureDescription& pictureDesc, const class ibMetaData* metaData = nullptr);

	// ⭐ A PICTURE BY ITS ID — all an answer names a picture by: a backend picture's number, a configuration picture's
	// guid and its version, `<guid>#<version>`, of the configuration given (none given: no configuration picture). The
	// picture itself the client loads apart, by the id (ibProtocol::ibMethod::Picture), and keeps with its version; one
	// held as a file's bytes has no id of its own and goes as itself. Empty: no picture.
	static ibString SendPicture(const ibPictureDescription& pictureDesc, const class ibMetaData* metaData);
	// …a backend picture by its number alone — no configuration names it: a decimal (a std::uint64_t is past what a JSON number
	// keeps). 0: no picture.
	static ibString SendPicture(const ibPictureID& id);

#pragma region __picture_factory_h__
	static bool IsRegisterPicture(const ibPictureID& id);
	static wxBitmap GetPicture(const ibPictureID& id);
	static wxIcon GetPictureAsIcon(const ibPictureID& id);
	// …and by its description: a backend one by its number, as above; a configuration's and a file's made from what they keep.
	static wxIcon GetPictureAsIcon(const ibPictureDescription& pictureDesc, const class ibMetaData* metaData = nullptr);
	static std::vector<ibBackendPictureEntry> GetArrayPicture();
#pragma endregion 

#pragma region __picture_conv_h__
	static ibString CreateBase64Image(const wxImage& image);
	static wxImage GetImageFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
	static wxBitmap GetBitmapFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
	static wxIcon GetIconFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);

	// A PICTURE AS DATA (core/graphics/picture.h) and a wx one: from an image made just now, shared with nobody —
	// a master kept in base64 is read at the size it is shown first (GetImageFromBase64); and for a window, made
	// anew every time, its own.
	static ibPicture FromImage(const wxImage& image);
	static wxBitmap ToBitmap(const ibPicture& picture);
	static wxIcon ToIcon(const ibPicture& picture);
#pragma endregion
};

// ⭐ A PICTURE'S DOOR ON THIS SIDE — the core one reads the id as it was written; this one also asks the
// engine's registry whether it has that picture.
class BACKEND_API ibBackendPictureDescriptionMemory : public ibPictureDescriptionMemory {
public:
	static bool ReadNode(const class ibDataValue& value, ibPictureDescription& pictureDesc);
};

// Initialize with XPM data
extern BACKEND_API bool RegisterBackendPicture(const wxString name,
	const ibPictureID& id, const char* const* data);

// Initialize with Base-64 + PNG data
extern BACKEND_API bool RegisterBackendPicture(const wxString name,
	const ibPictureID& id, const wxString& base64);

// Initialize with BMP
extern BACKEND_API bool RegisterBackendPicture(const wxString name,
	const ibPictureID& id, const wxBitmap& bitmap);

#endif