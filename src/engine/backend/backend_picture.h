#ifndef __BACKEND_PICTURE_H__
#define __BACKEND_PICTURE_H__

#include "pictureDescription.h"
#include "picturePredefined.h"

struct ibBackendPictureEntry {
	wxString m_name;
	wxBitmap m_data;
	ibPictureID m_id;
};

// ⭐ A PICTURE AS THE SERVER KEEPS IT — PNG, base64, as the wire carries it. Made once and copied freely: ibString
// counts its owners atomically, so sessions on worker threads share one with no wx object between them. A wx image
// counts its references in a plain int (wxRefCounter::m_count, and wx stays as it is upstream): four sessions copying
// the same class icon freed it under each other (2026-10-06). A window makes its wx image from this one anew.
class BACKEND_API ibServerPicture {
public:

	ibServerPicture() = default;
	// From a picture's source as it is kept — a master in base64 — at the size it is shown.
	ibServerPicture(const wxString& base64, const wxSize& size);
	// From an image made just now, shared with nobody.
	explicit ibServerPicture(const wxImage& image);

	bool IsOk() const { return !m_data.IsEmpty(); }

	// PNG, base64 — what a frame carries.
	const ibString& GetData() const { return m_data; }

	// For a window: made anew every time, its own.
	wxBitmap ToBitmap() const;
	wxIcon   ToIcon() const;

private:
	ibString m_data;
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
	// picture, a class's icon) is made once and kept here; a configuration's and a file's are made from what they keep.
	static ibServerPicture GetServerPicture(const ibPictureDescription& pictureDesc, const class ibMetaData* metaData = nullptr);

#pragma region __picture_factory_h__
	static bool IsRegisterPicture(const ibPictureID& id);
	static wxBitmap GetPicture(const ibPictureID& id);
	static wxIcon GetPictureAsIcon(const ibPictureID& id);
	static std::vector<ibBackendPictureEntry> GetArrayPicture();
#pragma endregion 

#pragma region __picture_conv_h__
	static wxString CreateBase64Image(const wxImage& image);
	static wxImage GetImageFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
	static wxBitmap GetBitmapFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
	static wxIcon GetIconFromBase64(const wxString& src, const wxSize& size = wxDefaultSize);
#pragma endregion 
};

#endif