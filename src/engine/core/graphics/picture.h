#ifndef _CORE_PICTURE_H__
#define _CORE_PICTURE_H__

////////////////////////////////////////////////////////////////////////////
// ibPicture — A PICTURE AS DATA: its PNG's bytes, as the wire carries them (a Binary).
////////////////////////////////////////////////////////////////////////////
//
// Made once and copied freely: ibMemoryBuffer counts its owners atomically, so sessions on worker threads share
// one with no wx object between them. A wx image counts its references in a plain int
// (wxRefCounter::m_count, and wx stays as it is upstream): four sessions copying the same class icon freed
// it under each other (2026-10-06). A window makes its wx image from this one anew — the side's converter
// does (backend: ibBackendPicture::FromImage / ToBitmap / ToIcon).
//
////////////////////////////////////////////////////////////////////////////

#include "core/memory/memoryBuffer.h"

class ibPicture {
public:

	ibPicture() = default;
	// From the PNG's bytes themselves.
	explicit ibPicture(const ibMemoryBuffer& png) : m_data(png) {}

	bool IsOk() const { return !m_data.IsEmpty(); }

	// The PNG's bytes — what a frame carries.
	const ibMemoryBuffer& GetData() const { return m_data; }

private:
	ibMemoryBuffer m_data;
};

#endif
