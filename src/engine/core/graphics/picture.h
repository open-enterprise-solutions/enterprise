#ifndef _CORE_PICTURE_H__
#define _CORE_PICTURE_H__

////////////////////////////////////////////////////////////////////////////
// ibPicture — A PICTURE AS DATA: PNG, base64, as the wire carries it.
////////////////////////////////////////////////////////////////////////////
//
// Made once and copied freely: ibString counts its owners atomically, so sessions on worker threads share
// one with no wx object between them. A wx image counts its references in a plain int
// (wxRefCounter::m_count, and wx stays as it is upstream): four sessions copying the same class icon freed
// it under each other (2026-10-06). A window makes its wx image from this one anew — the side's converter
// does (backend: ibBackendPicture::FromImage / ToBitmap / ToIcon).
//
////////////////////////////////////////////////////////////////////////////

#include "core/fstring.h"

class ibPicture {
public:

	ibPicture() = default;
	// From the PNG in base64 itself.
	explicit ibPicture(const ibString& data) : m_data(data) {}

	bool IsOk() const { return !m_data.IsEmpty(); }

	// PNG, base64 — what a frame carries.
	const ibString& GetData() const { return m_data; }

private:
	ibString m_data;
};

#endif
