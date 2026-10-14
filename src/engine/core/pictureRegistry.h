#ifndef _CORE_PICTURE_REGISTRY_H__
#define _CORE_PICTURE_REGISTRY_H__

////////////////////////////////////////////////////////////////////////////
// ibPictureRegistry — THE PICTURES A PROCESS REGISTERED, by their numbers: a name and the picture as data.
////////////////////////////////////////////////////////////////////////////
//
// The engine's own pictures are registered here as the core loads (picturePredefined.cpp), a side's own
// as its DLL loads, and every reader asks here. Kept as data, as the server keeps a picture (ibPicture): a window
// makes its wx image from one anew, so no two sessions copy one shared wx object.
//
////////////////////////////////////////////////////////////////////////////

#include <vector>

#include "core/core.h"
#include "core/graphics/picture.h"   // ibPicture — a picture as data

// A PICTURE BY ITS NUMBER — its name, its number, the picture.
struct ibPictureEntry {
	wxString m_name;
	ibPictureID m_id;
	ibPicture m_picture;
};

class CORE_API ibPictureRegistry {
	ibPictureRegistry() = delete;
public:

	// A picture under its number. One already there stays, and false says so: the sides of one process register the
	// same ones (the thin designer opening a file base loads the backend beside the client).
	static bool RegisterPicture(const wxString& name, ibPictureID id, const ibPicture& picture);
	static bool IsRegisterPicture(ibPictureID id);

	// The picture of that number — none: none registered.
	static ibPicture GetPicture(ibPictureID id);
	static std::vector<ibPictureEntry> GetArrayPicture();
};

// Initialize with Base-64 + PNG data — a master, kept as it is: a reader scales it to the size it shows.
extern CORE_API bool RegisterPicture(const wxString name, const ibPictureID& id, const wxString& base64);

#endif
