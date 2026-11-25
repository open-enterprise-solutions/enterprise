////////////////////////////////////////////////////////////////////////////
//	Description : a PDF file written page by page, by libharu
////////////////////////////////////////////////////////////////////////////

// ⭐ THE FORMAT IS LIBHARU'S (src/3rdparty/libharu, taken as it is released): the file, its objects, the fonts it
// embeds — only the glyphs drawn — with the map back to Unicode, the compression. What is ours is the door: the DC's
// words, the page's space with y down, and a font handed in as bytes.
//
// ⭐ A FONT IS TRUETYPE OVER UTF-8 (HPDF_UseUTFEncodings): any character the font has a glyph for is drawn, and the
// file says which letters the glyphs are, so a reader finds and copies the text.

#include "core/pdf/pdfDocument.h"

#include <hpdf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace {

// libharu's text is 16-bit: a character past the Basic Multilingual Plane (an emoji) has no glyph number there, and
// a control character none at all. A tab is a space; the rest of what cannot be drawn is left out.
std::string DrawableText(const ibString& text)
{
	ibString drawable;
	for (size_t i = 0; i < text.Len(); ++i) {
		const char16_t ch = static_cast<char16_t>(text[i]);
		if (ch >= 0xD800 && ch <= 0xDFFF)
			continue;
		if (ch == '\t')
			drawable += ibChar(' ');
		else if (ch >= 0x20)
			drawable += static_cast<ibChar>(ch);
	}
	return drawable.ToUtf8();
}

HPDF_REAL Part(unsigned char component) { return static_cast<HPDF_REAL>(component / 255.0); }

// A font file's numbers are big-endian.
uint16_t U16(const std::string& bytes, size_t at)
{
	return static_cast<uint16_t>((static_cast<unsigned char>(bytes[at]) << 8) | static_cast<unsigned char>(bytes[at + 1]));
}

uint32_t U32(const std::string& bytes, size_t at)
{
	return (uint32_t(U16(bytes, at)) << 16) | U16(bytes, at + 2);
}

void SetU32(std::string& bytes, size_t at, uint32_t value)
{
	for (size_t k = 0; k < 4; ++k)
		bytes[at + k] = static_cast<char>(value >> (24 - 8 * k));
}

// ⭐ A COLLECTION'S FACE AS A FONT FILE OF ITS OWN. libharu reads a collection (.ttc) only from a file by its name, and
// the door takes bytes, so the face is cut out here. A collection is a table directory per face over tables the faces
// share, each table where it lies in the whole file: the face's directory is copied, its tables laid after it end to
// end on four-byte boundaries, and the directory told where they are now. The checksums stay as they were — libharu
// writes its own for what it embeds. Empty when the collection has no such face, or is cut short.
std::string FaceOf(const std::string& collection, int index)
{
	if (index < 0 || collection.size() < 12 || static_cast<uint32_t>(index) >= U32(collection, 8)
		|| collection.size() < 12 + 4 * (size_t(index) + 1))
		return std::string();
	const size_t at = U32(collection, 12 + 4 * size_t(index));
	if (at > collection.size() || collection.size() - at < 12)
		return std::string();
	const size_t directory = 12 + 16 * size_t(U16(collection, at + 4));
	if (collection.size() - at < directory)
		return std::string();

	std::string face = collection.substr(at, directory);
	for (size_t record = 12; record < directory; record += 16) {
		const size_t offset = U32(face, record + 8), length = U32(face, record + 12);
		if (offset > collection.size() || collection.size() - offset < length)
			return std::string();
		face.resize((face.size() + 3) & ~size_t(3), '\0');
		SetU32(face, record + 8, static_cast<uint32_t>(face.size()));
		face.append(collection, offset, length);
	}
	return face;
}

} // namespace

// ⭐ WHAT THE DOCUMENT HOLDS BETWEEN CALLS — libharu's document and the page being drawn, the fonts it loaded, and
// what is set. An error libharu reports is kept: the file it would save is not the one asked for.
class ibPdfDocumentState {
public:

	explicit ibPdfDocumentState(std::ostream& output) : m_output(output) {}

	~ibPdfDocumentState() {
		if (m_pdf != nullptr)
			HPDF_Free(m_pdf);
	}

	static void HPDF_STDCALL OnError(HPDF_STATUS error, HPDF_STATUS /*detail*/, void* state) {
		static_cast<ibPdfDocumentState*>(state)->m_error = error;
	}

	HPDF_Font CurrentFont() const {
		return m_font >= 0 && m_font < static_cast<int>(m_fonts.size()) ? m_fonts[m_font] : nullptr;
	}

	HPDF_REAL Y(double y) const { return static_cast<HPDF_REAL>(m_pageHeight - y); }

	std::ostream& m_output;
	HPDF_Doc m_pdf = nullptr;
	HPDF_STATUS m_error = HPDF_OK;
	bool m_closed = false;

	std::vector<HPDF_Font> m_fonts;

	int m_font = ibNOT_FOUND;
	double m_size = 0;
	bool m_underlined = false;
	bool m_strikethrough = false;
	ibColour m_textColour = ibColour(0, 0, 0);
	ibColour m_penColour = ibColour(0, 0, 0);
	double m_penWidth = 1;
	std::vector<HPDF_REAL> m_dashes;
	ibColour m_brushColour = ibColour(255, 255, 255);

	HPDF_Page m_page = nullptr;
	double m_pageHeight = 0;
	bool m_clipped = false;
};

ibPdfDocument::ibPdfDocument(std::ostream& output)
	: m_state(std::make_unique<ibPdfDocumentState>(output))
{
	ibPdfDocumentState& state = *m_state;
	state.m_pdf = HPDF_New(&ibPdfDocumentState::OnError, &state);
	if (state.m_pdf == nullptr) {
		state.m_error = HPDF_FAILED_TO_ALLOC_MEM;
		return;
	}
	HPDF_SetCompressionMode(state.m_pdf, HPDF_COMP_ALL);
	HPDF_UseUTFEncodings(state.m_pdf);
}

ibPdfDocument::~ibPdfDocument() = default;

int ibPdfDocument::AddFont(const std::string& bytes, int index)
{
	ibPdfDocumentState& state = *m_state;
	if (state.m_pdf == nullptr || bytes.empty())
		return ibNOT_FOUND;

	// A collection's face is cut out first; a font file holds one face, 0.
	const bool collection = bytes.compare(0, 4, "ttcf") == 0;
	const std::string cut = collection ? FaceOf(bytes, index) : std::string();
	const std::string& file = collection ? cut : bytes;
	if (file.empty() || (!collection && index != 0))
		return ibNOT_FOUND;

	// libharu copies the bytes into a stream of its own.
	const char* const name = HPDF_LoadTTFontFromMemory(state.m_pdf, reinterpret_cast<const HPDF_BYTE*>(file.data()),
		static_cast<HPDF_UINT>(file.size()), HPDF_TRUE);
	const HPDF_Font font = name != nullptr ? HPDF_GetFont(state.m_pdf, name, "UTF-8") : nullptr;
	if (font == nullptr) {
		// Not a font it can embed — not TrueType, CFF outlines, a licence that forbids embedding: a refusal of this
		// font, not of the document.
		HPDF_ResetError(state.m_pdf);
		state.m_error = HPDF_OK;
		return ibNOT_FOUND;
	}
	state.m_fonts.push_back(font);
	return static_cast<int>(state.m_fonts.size() - 1);
}

void ibPdfDocument::SetFont(int font, double size, bool underlined, bool strikethrough)
{
	m_state->m_font = font;
	m_state->m_size = size;
	m_state->m_underlined = underlined;
	m_state->m_strikethrough = strikethrough;
}

void ibPdfDocument::SetTextForeground(const ibColour& colour)
{
	m_state->m_textColour = colour;
}

void ibPdfDocument::SetPen(const ibColour& colour, double width, const std::vector<double>& dashes)
{
	m_state->m_penColour = colour;
	m_state->m_penWidth = width;
	m_state->m_dashes.clear();
	for (const double dash : dashes)
		m_state->m_dashes.push_back(static_cast<HPDF_REAL>(dash));
}

void ibPdfDocument::SetBrush(const ibColour& colour)
{
	m_state->m_brushColour = colour;
}

void ibPdfDocument::GetTextExtent(const ibString& text, double* width, double* height) const
{
	const HPDF_Font font = m_state->CurrentFont();
	if (width != nullptr) {
		*width = 0;
		if (font != nullptr) {
			const std::string utf8 = DrawableText(text);
			const HPDF_TextWidth measured = HPDF_Font_TextWidth(font, reinterpret_cast<const HPDF_BYTE*>(utf8.data()),
				static_cast<HPDF_UINT>(utf8.size()));
			*width = measured.width * m_state->m_size / 1000.0;   // the font's widths are in thousandths of its size
		}
	}
	if (height != nullptr)
		*height = GetCharHeight();
}

double ibPdfDocument::GetCharHeight() const
{
	const HPDF_Font font = m_state->CurrentFont();
	if (font == nullptr)
		return 0;
	return (HPDF_Font_GetAscent(font) - HPDF_Font_GetDescent(font)) * m_state->m_size / 1000.0;
}

void ibPdfDocument::StartPage(double width, double height)
{
	ibPdfDocumentState& state = *m_state;
	EndPage();
	if (state.m_pdf == nullptr)
		return;
	state.m_page = HPDF_AddPage(state.m_pdf);
	if (state.m_page == nullptr)
		return;
	HPDF_Page_SetWidth(state.m_page, static_cast<HPDF_REAL>(width));
	HPDF_Page_SetHeight(state.m_page, static_cast<HPDF_REAL>(height));
	state.m_pageHeight = height;
}

void ibPdfDocument::EndPage()
{
	DestroyClippingRegion();
	m_state->m_page = nullptr;
}

void ibPdfDocument::SetClippingRegion(double x, double y, double width, double height)
{
	ibPdfDocumentState& state = *m_state;
	if (state.m_page == nullptr)
		return;
	DestroyClippingRegion();
	HPDF_Page_GSave(state.m_page);
	HPDF_Page_Rectangle(state.m_page, static_cast<HPDF_REAL>(x), state.Y(y + height),
		static_cast<HPDF_REAL>(width), static_cast<HPDF_REAL>(height));
	HPDF_Page_Clip(state.m_page);
	HPDF_Page_EndPath(state.m_page);
	state.m_clipped = true;
}

void ibPdfDocument::DestroyClippingRegion()
{
	ibPdfDocumentState& state = *m_state;
	if (state.m_page == nullptr || !state.m_clipped)
		return;
	HPDF_Page_GRestore(state.m_page);
	state.m_clipped = false;
}

void ibPdfDocument::DrawRectangle(double x, double y, double width, double height)
{
	ibPdfDocumentState& state = *m_state;
	if (state.m_page == nullptr || width <= 0 || height <= 0)
		return;
	const ibColour& colour = state.m_brushColour;
	HPDF_Page_SetRGBFill(state.m_page, Part(colour.m_red), Part(colour.m_green), Part(colour.m_blue));
	HPDF_Page_Rectangle(state.m_page, static_cast<HPDF_REAL>(x), state.Y(y + height),
		static_cast<HPDF_REAL>(width), static_cast<HPDF_REAL>(height));
	HPDF_Page_Fill(state.m_page);
}

void ibPdfDocument::DrawLine(double x1, double y1, double x2, double y2)
{
	ibPdfDocumentState& state = *m_state;
	if (state.m_page == nullptr || state.m_penWidth <= 0)
		return;
	const ibColour& colour = state.m_penColour;
	HPDF_Page_SetRGBStroke(state.m_page, Part(colour.m_red), Part(colour.m_green), Part(colour.m_blue));
	HPDF_Page_SetLineWidth(state.m_page, static_cast<HPDF_REAL>(state.m_penWidth));
	HPDF_Page_SetDash(state.m_page, state.m_dashes.empty() ? nullptr : state.m_dashes.data(),
		static_cast<HPDF_UINT>(state.m_dashes.size()), 0);
	HPDF_Page_MoveTo(state.m_page, static_cast<HPDF_REAL>(x1), state.Y(y1));
	HPDF_Page_LineTo(state.m_page, static_cast<HPDF_REAL>(x2), state.Y(y2));
	HPDF_Page_Stroke(state.m_page);
}

void ibPdfDocument::DrawText(const ibString& text, double x, double y)
{
	DrawRotatedText(text, x, y, 0);
}

void ibPdfDocument::DrawRotatedText(const ibString& text, double x, double y, double angle)
{
	ibPdfDocumentState& state = *m_state;
	const HPDF_Font font = state.CurrentFont();
	if (state.m_page == nullptr || font == nullptr || state.m_size <= 0)
		return;
	const std::string utf8 = DrawableText(text);
	if (utf8.empty())
		return;

	// ⭐ THE BASELINE'S START: the top-left corner moved down the glyphs' own "down" by the ascent — down the page
	// for upright text, to the right for text turned a quarter anticlockwise. In PDF's space, y up.
	const double radians = angle * 3.14159265358979323846 / 180.0;
	double c = std::cos(radians), s = std::sin(radians);
	if (std::fabs(c) < 1e-9) c = 0;
	if (std::fabs(s) < 1e-9) s = 0;
	const double ascent = HPDF_Font_GetAscent(font) * state.m_size / 1000.0;
	const HPDF_REAL originX = static_cast<HPDF_REAL>(x + ascent * s);
	const HPDF_REAL originY = static_cast<HPDF_REAL>(state.m_pageHeight - y - ascent * c);
	const HPDF_REAL a = static_cast<HPDF_REAL>(c), b = static_cast<HPDF_REAL>(s);

	const ibColour& colour = state.m_textColour;
	HPDF_Page_SetRGBFill(state.m_page, Part(colour.m_red), Part(colour.m_green), Part(colour.m_blue));
	HPDF_Page_BeginText(state.m_page);
	HPDF_Page_SetFontAndSize(state.m_page, font, static_cast<HPDF_REAL>(state.m_size));
	HPDF_Page_SetTextMatrix(state.m_page, a, b, -b, a, originX, originY);
	HPDF_Page_ShowText(state.m_page, utf8.c_str());
	HPDF_Page_EndText(state.m_page);

	// The two lines a font may carry, where a type of the usual proportions has them — a tenth of the size under
	// the baseline, a quarter above it — laid in the text's own space so they turn with it.
	if (state.m_underlined || state.m_strikethrough) {
		double width = 0;
		GetTextExtent(text, &width);
		const HPDF_REAL thickness = static_cast<HPDF_REAL>(std::max(state.m_size * 0.06, 0.25));
		HPDF_Page_GSave(state.m_page);
		HPDF_Page_Concat(state.m_page, a, b, -b, a, originX, originY);
		if (state.m_underlined)
			HPDF_Page_Rectangle(state.m_page, 0, static_cast<HPDF_REAL>(-state.m_size * 0.1) - thickness / 2,
				static_cast<HPDF_REAL>(width), thickness);
		if (state.m_strikethrough)
			HPDF_Page_Rectangle(state.m_page, 0, static_cast<HPDF_REAL>(state.m_size * 0.25) - thickness / 2,
				static_cast<HPDF_REAL>(width), thickness);
		HPDF_Page_Fill(state.m_page);
		HPDF_Page_GRestore(state.m_page);
	}
}

bool ibPdfDocument::Close()
{
	ibPdfDocumentState& state = *m_state;
	if (state.m_closed)
		return state.m_error == HPDF_OK;
	EndPage();
	state.m_closed = true;
	if (state.m_pdf == nullptr || state.m_error != HPDF_OK)
		return false;

	// libharu lays the file out in a stream of its own, read back here into the caller's.
	if (HPDF_SaveToStream(state.m_pdf) != HPDF_OK)
		return false;
	HPDF_ResetStream(state.m_pdf);
	std::vector<HPDF_BYTE> buffer(64 * 1024);
	for (;;) {
		HPDF_UINT32 length = static_cast<HPDF_UINT32>(buffer.size());
		const HPDF_STATUS read = HPDF_ReadFromStream(state.m_pdf, buffer.data(), &length);
		if (length > 0)
			state.m_output.write(reinterpret_cast<const char*>(buffer.data()), length);
		if (read == HPDF_STREAM_EOF)
			break;
		if (read != HPDF_OK)
			return false;
	}
	// The end of its stream, which libharu reports through the error handler as well.
	HPDF_ResetError(state.m_pdf);
	state.m_error = HPDF_OK;
	state.m_output.flush();
	return static_cast<bool>(state.m_output);
}
