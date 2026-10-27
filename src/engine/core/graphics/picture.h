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
// does (the thin client: frmclient/win/picture.h; codeRunner: codeRunner/win/picture.h). Read, scaled and written
// here by LodePNG, drawn from SVG and said as SVG here (NanoSVG), so the server has no wx image at all.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/graphics/geometry.h"   // ibSize — how big
#include "core/memory/memoryBuffer.h"

class CORE_API ibPicture {
public:

	ibPicture() = default;
	// From the PNG's bytes themselves.
	explicit ibPicture(const ibMemoryBuffer& png) : m_data(png) {}
	// …and from a master in the source — a PNG written as Base-64 text in a `_res.cpp`, at the size asked (Scale) or
	// as it is written. None: the text is no Base-64.
	static ibPicture FromBase64(const ibString& text, const ibSize& size = ibDefaultSize);
	// …and drawn from SVG text at the size asked, the drawing fitted into it from the top left corner (NanoSVG: shapes,
	// paths, fills and strokes are read; text and filters are not). None: the text is no SVG, or it draws nothing —
	// every pixel transparent.
	static ibPicture FromSvg(const ibString& svg, const ibSize& size);

	bool IsOk() const { return !m_data.IsEmpty(); }

	// How big it is, from the PNG's header — none (0, 0): the bytes are no PNG.
	ibSize GetSize() const;
	// The same picture at the size asked, made anew — each pixel the average of what it covers; itself, when it is that
	// size already. None: the bytes are no PNG, or the size is none.
	ibPicture Scale(const ibSize& size) const;

	// The same picture said as SVG — a PNG keeps no drawing, so what is said is its pixels: each run of one colour a
	// rectangle, runs standing on one another joined, every colour ONE path; drawn back at the same size it gives the
	// same picture. None: the bytes are no PNG, or more than `mostRects` rectangles would say it (a photograph).
	ibString ToSvg(size_t mostRects) const;

	// The PNG's bytes — what a frame carries.
	const ibMemoryBuffer& GetData() const { return m_data; }

private:
	ibMemoryBuffer m_data;
};

#endif
