#ifndef _CORE_PDF_DOCUMENT_H__
#define _CORE_PDF_DOCUMENT_H__

/////////////////////////////////////////////////////////////////////////////
// ibPdfDocument — A PDF FILE DRAWN PAGE BY PAGE, in the words of the DC a printout draws on (wxDC): StartPage and
// EndPage, a font, a pen and a brush set, then DrawRectangle, DrawLine, DrawText, and a clipping region.
//
// ⭐ THE FORMAT IS LIBHARU'S (src/3rdparty/libharu, a submodule taken as it is released, as lunasvg is for SVG): this
// is the one door to it, and nothing past the core includes it.
//
// ⭐ OVER A STREAM, AS ibZipWriter IS: Close writes the file into it. A document that is merely destroyed has written
// nothing.
//
// ⭐ POINTS, Y DOWN, THE ORIGIN AT THE PAGE'S TOP LEFT — the way a sheet is laid out. PDF's own space runs up from the
// bottom; that is this door's business, not its caller's.
//
// ⭐ A FONT IS A TRUETYPE FILE'S BYTES — a font file, or a collection with the number of its face — embedded with only
// the glyphs the document drew, and a map back to Unicode, so a reader finds, selects and copies the text as the
// letters it is. A character the font has no glyph for is drawn as the font's empty box.
/////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/graphics/colour.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

class ibPdfDocumentState;

class CORE_API ibPdfDocument {
public:

	explicit ibPdfDocument(std::ostream& output);
	~ibPdfDocument();

	// A font from a TrueType file's bytes — a .ttf, whose one face is 0, or a collection's (.ttc) face `index`; the
	// number SetFont takes it by. ibNOT_FOUND for bytes libharu does not embed: no TrueType font, no such face,
	// outlines that are not TrueType's own (CFF), a licence that forbids it.
	int AddFont(const std::string& bytes, int index = 0);

	// What text is drawn and measured in: a font AddFont gave, its size in points, and the two lines a font may carry.
	void SetFont(int font, double size, bool underlined = false, bool strikethrough = false);
	void SetTextForeground(const ibColour& colour);
	// A line's colour, its width in points and its dashes — on, off, on… in points; none is a solid line.
	void SetPen(const ibColour& colour, double width, const std::vector<double>& dashes = {});
	// A rectangle's fill.
	void SetBrush(const ibColour& colour);

	// A line of text in the font set, in points: how wide, and how tall — the font's ascent and descent, as the
	// DC's character height is. A font is set before anything is measured.
	void GetTextExtent(const ibString& text, double* width, double* height = nullptr) const;
	double GetCharHeight() const;

	void StartPage(double width, double height);
	void EndPage();

	// What is drawn after is cut to the rectangle, until the region is destroyed or the page ends.
	void SetClippingRegion(double x, double y, double width, double height);
	void DestroyClippingRegion();

	// Filled with the brush; no outline.
	void DrawRectangle(double x, double y, double width, double height);
	void DrawLine(double x1, double y1, double x2, double y2);
	// The text's top left at (x, y), as the DC's; …and turned `angle` degrees anticlockwise about that corner.
	void DrawText(const ibString& text, double x, double y);
	void DrawRotatedText(const ibString& text, double x, double y, double angle);

	// The file, written into the stream: false when libharu refused something drawn, or the stream did not take it.
	bool Close();

private:
	std::unique_ptr<ibPdfDocumentState> m_state;
};

#endif // !_CORE_PDF_DOCUMENT_H__
