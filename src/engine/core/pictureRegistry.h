#ifndef _CORE_PICTURE_REGISTRY_H__
#define _CORE_PICTURE_REGISTRY_H__

////////////////////////////////////////////////////////////////////////////
// ibPictureRegistry — THE PICTURES A PROCESS REGISTERED, by their numbers: a name and the picture as data.
////////////////////////////////////////////////////////////////////////////
//
// The engine's own pictures are registered here as the core loads (picturePredefined.cpp), a side's own
// as its DLL loads, and every reader asks here. KEPT AS THEIR DRAWING (ibPicture::FromSvg — the SVG's text), drawn by
// whoever shows one at the size it shows it: writing a PNG is 85 % of drawing one (~0.6 ms a picture in Release,
// 2026-11-24), and the browser draws the SVG itself. Kept as data, as the server keeps a picture (ibPicture): a window
// makes its wx image from one anew, so no two sessions copy one shared wx object.
//
////////////////////////////////////////////////////////////////////////////

#include <vector>

#include "core/core.h"
#include "core/fstring.h"   // ibString — the SVG text a picture is drawn from
#include "core/graphics/picture.h"   // ibPicture — a picture as data

// A PICTURE BY ITS NUMBER — its name, its number, the picture.
struct ibPictureEntry {
	ibString m_name;
	ibPictureID m_id;
	ibPicture m_picture;
};

class CORE_API ibPictureRegistry {
	ibPictureRegistry() = delete;
public:

	// A picture under its number, as its SVG. One already there stays, and false says so: the sides of one process
	// register the same ones (the thin designer opening a file base loads the backend beside the client).
	static bool RegisterPicture(const ibString& name, ibPictureID id, const ibString& svg);
	static bool IsRegisterPicture(ibPictureID id);

	// The picture of that number, its drawing — none: none registered.
	static ibPicture GetPicture(ibPictureID id);
	static std::vector<ibPictureEntry> GetArrayPicture();
};

// Register an SVG written in the source under its number — a second time under one number asserts.
extern CORE_API bool RegisterPicture(const ibString name, const ibPictureID& id, const ibString& svg);

#endif
