#ifndef _CORE_PICTURE_H__
#define _CORE_PICTURE_H__

////////////////////////////////////////////////////////////////////////////
// ibPicture — A PICTURE AS DATA: a PNG's bytes, or an SVG's text (UTF-8) — the drawing itself — as the wire carries
// them (a Binary). The bytes say which: a PNG's signature, else a `<`; nothing beside them does.
////////////////////////////////////////////////////////////////////////////
//
// An engine's picture is its drawing, kept and sent as written, and drawn by whoever shows it at the size it shows it;
// an image a person gave is its PNG. Made once and copied freely: ibMemoryBuffer counts its owners atomically, so
// sessions on worker threads share one with no wx object between them. A wx image counts its references in a plain
// int (wxRefCounter::m_count, and wx stays as it is upstream): four sessions copying the same class icon freed it
// under each other (2026-10-06). A window makes its wx image from this one anew — the side's converter does (the
// desktop: deskgui/artProvider.h; the thin client: frmclient/win/picture.h). Read, scaled and written here by LodePNG,
// drawn from SVG by lunasvg and said as SVG here, so the server has no wx image at all.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/graphics/geometry.h"   // ibSize — how big
#include "core/memory/memoryBuffer.h"

class CORE_API ibPicture {
public:

	ibPicture() = default;
	// From the bytes themselves — a PNG's, or an SVG's text as UTF-8.
	explicit ibPicture(const ibMemoryBuffer& data) : m_data(data) {}
	// The drawing itself, kept as its text — every picture written in the source is one, an `s_<name>_svg` beside the
	// code that shows it. None: the text is no SVG (it does not begin with `<`).
	static ibPicture FromSvg(const ibString& svg);
	// …drawn at the size asked: a PNG, the drawing fitted into it from the top left corner (lunasvg, as it is released:
	// nearly all of static SVG; not filters, not animation). None: the text is no SVG, or it draws nothing — every pixel
	// transparent.
	static ibPicture FromSvg(const ibString& svg, const ibSize& size);

	bool IsOk() const { return !m_data.IsEmpty(); }

	// How big it is: the PNG's header, or the size the drawing gives itself — none (0, 0): the bytes are neither.
	ibSize GetSize() const;
	// The same picture at the size asked, made anew: a drawing drawn at it, a PNG resampled — each pixel the average of
	// what it covers; a PNG that size already is itself. None: the bytes are neither, or the size is none, or the drawing
	// draws nothing.
	ibPicture Scale(const ibSize& size) const;

	// The same picture said as SVG: a drawing is its text. A PNG keeps no drawing, so what is said is its pixels: each
	// run of one colour a rectangle, runs standing on one another joined, every colour ONE path; drawn back at the same
	// size it gives the same picture. None: the bytes are neither, or more than `mostRects` rectangles would say a PNG
	// (a photograph).
	ibString ToSvg(size_t mostRects) const;

	// The bytes — what a frame carries.
	const ibMemoryBuffer& GetData() const { return m_data; }

private:
	ibMemoryBuffer m_data;
};

#endif
