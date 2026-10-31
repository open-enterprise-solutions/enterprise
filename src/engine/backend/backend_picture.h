#ifndef __BACKEND_PICTURE_H__
#define __BACKEND_PICTURE_H__

#include "core/graphics/picture.h"   // ibPicture — a picture as data, what the server keeps and sends

#include "core/description/pictureDescription.h"
#include "core/picturePredefined.h"
#include "core/pictureRegistry.h"    // ibPictureEntry — a picture by its number

#include "backend/backend_core.h"

class ibDataValue;   // core/serialize/dataBuilder.h — a picture as an answer names it

// ⭐ THE SERVER'S PICTURES ARE DATA — ibPicture, a PNG's bytes, read and scaled by the core; no wx image here. A window
// makes its own from one (the thin client: frmclient/win/picture.h).
class BACKEND_API ibBackendPicture {
	ibBackendPicture() = delete;
public:

	// A FILE'S PICTURE — its PNG, as it is: the server reads no other kind. Refused: no file, or no PNG in it.
	static bool LoadFromFile(const ibString& strFileName, ibExternalPictureDescription& pictureDesc);
	static bool LoadFromFile(const ibString& strFileName, ibPictureDescription& pictureDesc);

	static ibPicture CreatePicture(const ibExternalPictureDescription& pictureDesc, const ibSize& size = ibSize(16, 16));
	static ibPicture CreatePicture(const ibPictureDescription& pictureDesc,
		const class ibMetaData* metaData = nullptr, const ibSize& size = ibSize(16, 16));

	// THE PICTURE A SERVER SENDS, by its description. A backend one (a registered picture, a class's icon) is made once
	// and kept here; a type of the configuration's own (a catalog's reference), which no global class knows, is its
	// constructor's icon in the configuration given; a configuration's picture and a file's are made from what they keep.
	static ibPicture GetServerPicture(const ibPictureDescription& pictureDesc, const class ibMetaData* metaData = nullptr);

	// ⭐ A PICTURE BY ITS ID — all an answer names a picture by: a backend picture's number, a configuration picture's
	// guid and its version, `<guid>#<version>`, of the configuration given (none given: no configuration picture) — a
	// String. The picture itself the client loads apart, by the id (ibProtocol::ibMethod::Picture), and keeps with its
	// version; one held as a file's bytes has no id of its own and goes as itself, its PNG — a Binary. Empty: no picture.
	static ibDataValue SendPicture(const ibPictureDescription& pictureDesc, const class ibMetaData* metaData);
	// …a backend picture by its number alone — no configuration names it: a decimal (a std::uint64_t is past what a JSON number
	// keeps). 0: no picture.
	static ibString SendPicture(const ibPictureID& id);

#pragma region __picture_factory_h__
	static bool IsRegisterPicture(const ibPictureID& id);
	// A backend picture by its number, at 16: a registered one scaled from its master, else a class's icon.
	static ibPicture GetPicture(const ibPictureID& id);
	// …and every one of them: the registered ones, the metadata's classes' and the controls'.
	static std::vector<ibPictureEntry> GetArrayPicture();
#pragma endregion
};

// ⭐ A PICTURE'S DOOR ON THIS SIDE — the core one reads the id as it was written; this one also asks the
// engine's registry whether it has that picture.
class BACKEND_API ibBackendPictureDescriptionMemory : public ibPictureDescriptionMemory {
public:
	static bool ReadNode(const ibDataValue& value, ibPictureDescription& pictureDesc);
};

#endif
