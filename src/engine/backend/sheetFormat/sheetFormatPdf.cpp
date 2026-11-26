////////////////////////////////////////////////////////////////////////////
//	Description : our document, written out as a PDF file
////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE PRINTOUT, ONE STOREY DOWN. What the window prints (frmclient's gridPrintout.cpp, ibGrid::DrawTextRectangle)
// is laid out here by the same rules, so a sheet saved as PDF and the same sheet printed are the same pages:
//
//   the rows       a row with a height of its own keeps it; one without is as tall as its text, measured as the
//                  window measures it (ibSpreadsheetRowHeight) — in the font the page is drawn in
//   the pages      A4 upright, 50 of the sheet's pixels of margin each side, bands of columns and of rows; a break
//                  where the sheet asks for one, never inside a merged cell unless the cell is bigger than the page
//   each page      the fills, then the text, then the rules — so no fill covers a neighbour's text or rule
//   a cell's text  its alignment and orientation, wrapped, cut with an ellipsis or let out over empty neighbours as
//                  its placement says, clipped to what it may cover; after the first line, whole lines only
//
// The sheet counts in pixels at 96 to the inch, which is three quarters of a point; the page in points.

#include "backend/sheetFormat/sheetFormatPdf.h"
#include "backend/backend_localization.h"   // a caption goes out in one language

#include "core/exception.h"         // ibCoreException — no font to write in
#include "core/fileSystem.h"        // ibProgramSubfolder, ibPathOf — the fonts the program ships
#include "core/system.h"            // ibSystemFontFolders — …and the ones the machine keeps
#include "core/pdf/pdfDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

namespace {

constexpr double kPageWidth = 595.28;    // A4, in points
constexpr double kPageHeight = 841.89;
constexpr double kPoint = 0.75;          // one of the sheet's pixels

// The printout's numbers, in the sheet's pixels: the margin round the page, what it takes off a cell's left before
// the text, the text's own margin at the sides, and what an automatic row leaves for the window's chrome when it
// breaks a line — narrower than the page breaks it, so paper never breaks a line the screen did not.
constexpr int kMargin = 50;
constexpr int kCellInset = 2;
constexpr int kTextMargin = 1;
constexpr int kChromeWrapping = 5;

/////////////////////////////////////////////////////////////////////////////
// THE FONTS THIS MACHINE HAS
/////////////////////////////////////////////////////////////////////////////

// One face a folder offers — where it is, what it is called and how it looks, read off its tables; the rest of the
// file is read only when a page is drawn in it.
struct FontFace {
	ibString path;
	int      index = 0;       // which of a collection's (.ttc) faces; a font file holds one, 0
	ibString family;          // the name table's family (1), in lower case — what a sheet names a font by
	ibString typographic;     // …and the typographic family (16), where a face's weight is not in its family's name
	int      weight = 400;
	bool     italic = false;
};

uint16_t U16(const unsigned char* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t U32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
const unsigned char* Bytes(const std::string& text) { return reinterpret_cast<const unsigned char*>(text.data()); }

// `length` bytes at `at`, or nothing when the file is shorter.
std::string ReadAt(std::ifstream& file, uint64_t at, size_t length)
{
	std::string bytes(length, '\0');
	file.clear();
	file.seekg(static_cast<std::streamoff>(at));
	file.read(bytes.data(), static_cast<std::streamsize>(length));
	return static_cast<size_t>(file.gcount()) == length ? bytes : std::string();
}

// A name out of a name table in lower case — the English one Windows writes first, else any Unicode one, else the
// Macintosh one; empty when the table has none of that number.
ibString NameOf(const std::string& table, unsigned id)
{
	if (table.size() < 6)
		return ibString();
	const unsigned char* data = Bytes(table);
	const unsigned count = U16(data + 2), strings = U16(data + 4);
	const unsigned char* chosen = nullptr;
	int rank = 0;
	for (unsigned i = 0; i < count && 6 + 12 * size_t(i + 1) <= table.size(); ++i) {
		const unsigned char* record = data + 6 + 12 * i;
		if (U16(record + 6) != id)
			continue;
		const unsigned platform = U16(record), encoding = U16(record + 2), language = U16(record + 4);
		int candidate = 0;
		if (platform == 3 && (encoding == 0 || encoding == 1 || encoding == 10))
			candidate = language == 0x409 ? 4 : 3;
		else if (platform == 0)
			candidate = 2;
		else if (platform == 1 && encoding == 0)
			candidate = 1;
		if (candidate > rank) {
			rank = candidate;
			chosen = record;
		}
	}
	if (chosen == nullptr)
		return ibString();
	const size_t length = U16(chosen + 8), at = size_t(strings) + U16(chosen + 10);
	if (at + length > table.size())
		return ibString();
	ibString name;
	if (rank >= 2) {   // UTF-16, big-endian
		for (size_t k = 0; k + 1 < length; k += 2)
			name += static_cast<ibChar>(U16(data + at + k));
	}
	else {             // Macintosh Roman: what a family is named in is ASCII
		for (size_t k = 0; k < length; ++k)
			name += static_cast<ibChar>(data[at + k]);
	}
	return name.Lower();
}

// The face whose table directory is at `at` — a font file's start, or a collection's place for its face `index` —
// by its names and its look; a table is where its record says in the whole file either way. Left out, as libharu
// would refuse them later: outlines that are not TrueType's own (CFF) and a licence that forbids embedding (OS/2
// fsType: restricted, or bitmaps only).
void ReadFace(std::ifstream& file, const std::filesystem::path& path, uint32_t at, int index,
	std::vector<FontFace>& faces)
{
	const std::string directory = ReadAt(file, at, 12);
	if (directory.empty())
		return;
	const unsigned count = U16(Bytes(directory) + 4);
	const std::string records = ReadAt(file, uint64_t(at) + 12, 16 * size_t(count));
	if (records.empty())
		return;
	std::map<std::string, std::pair<uint32_t, uint32_t>> tables;
	for (unsigned i = 0; i < count; ++i) {
		const unsigned char* record = Bytes(records) + 16 * i;
		tables[std::string(reinterpret_cast<const char*>(record), 4)] = { U32(record + 8), U32(record + 12) };
	}
	const auto names = tables.find("name");
	if (!tables.contains("glyf") || names == tables.end())
		return;

	FontFace face;
	face.path = ibTextOf(path);
	face.index = index;
	const std::string table = ReadAt(file, names->second.first, std::min<uint32_t>(names->second.second, 1u << 20));
	face.family = NameOf(table, 1);
	face.typographic = NameOf(table, 16);
	if (face.family.IsEmpty())
		return;

	if (const auto os2 = tables.find("OS/2"); os2 != tables.end() && os2->second.second >= 64) {
		const std::string bytes = ReadAt(file, os2->second.first, 64);
		if (bytes.empty())
			return;
		const unsigned fsType = U16(Bytes(bytes) + 8);
		if ((fsType & 0x000F) == 0x0002 || (fsType & 0x0200) != 0)
			return;
		face.weight = U16(Bytes(bytes) + 4);
		face.italic = (U16(Bytes(bytes) + 62) & 0x0001) != 0;   // fsSelection
	}
	else if (const auto head = tables.find("head"); head != tables.end() && head->second.second >= 46) {
		const std::string bytes = ReadAt(file, head->second.first, 46);
		if (!bytes.empty()) {
			const unsigned style = U16(Bytes(bytes) + 44);         // macStyle
			face.weight = (style & 0x0001) != 0 ? 700 : 400;
			face.italic = (style & 0x0002) != 0;
		}
	}
	faces.push_back(std::move(face));
}

// The faces one file holds: a font file's one, or each of a collection's (.ttc), by its number there — the CJK fonts
// a system keeps come as collections.
void ReadFaces(const std::filesystem::path& path, std::vector<FontFace>& faces)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return;
	const std::string header = ReadAt(file, 0, 12);
	if (header.empty())
		return;
	if (header.compare(0, 4, "ttcf") != 0) {
		ReadFace(file, path, 0, 0, faces);
		return;
	}
	const uint32_t count = U32(Bytes(header) + 8);
	if (count > 1024)
		return;   // no collection holds that many faces: not a font
	const std::string places = ReadAt(file, 12, 4 * size_t(count));
	for (size_t i = 0; i + 4 <= places.size(); i += 4)
		ReadFace(file, path, U32(Bytes(places) + i), static_cast<int>(i / 4), faces);
}

// ⭐ EVERY FACE THIS PROCESS CAN WRITE IN — the program's own folder first, then the system's, read once: a font
// installed after the first PDF is seen by the next start.
const std::vector<FontFace>& Faces()
{
	static const std::vector<FontFace> faces = [] {
		std::vector<ibString> folders;
		const ibString shipped = ibProgramSubfolder(ibT("fonts"));
		if (!shipped.IsEmpty())
			folders.push_back(shipped);
		for (const ibString& folder : ibSystemFontFolders())
			folders.push_back(folder);

		std::vector<FontFace> found;
		for (const ibString& folder : folders) {
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(ibPathOf(folder),
					std::filesystem::directory_options::skip_permission_denied, error), end;
				!error && it != end; it.increment(error)) {
				std::error_code kind;
				if (!it->is_regular_file(kind))
					continue;
				const ibString extension = ibTextOf(it->path().extension()).Lower();
				if (extension == ibT(".ttf") || extension == ibT(".otf") || extension == ibT(".ttc"))
					ReadFaces(it->path(), found);
			}
		}
		return found;
	}();
	return faces;
}

// ⭐⭐ THE FACES A SHEET'S FONT MAY BE DRAWN IN, the likeliest first: the one it names, by its family and then by its
// typographic family; for a name this machine does not have, the face its metrics were copied for, then the rest of
// its kind; then anything at all. Of a family's faces, the one that slants as asked and is nearest in weight.
std::vector<const FontFace*> FacesFor(const ibFont& font)
{
	const std::vector<FontFace>& faces = Faces();
	const bool italic = font.m_style != ibFONTSTYLE_NORMAL;

	std::vector<const FontFace*> chosen;
	auto offer = [&](auto&& matches) {
		const FontFace* best = nullptr;
		int distance = 0;
		for (const FontFace& face : faces) {
			if (!matches(face))
				continue;
			const int apart = (face.italic != italic ? 10000 : 0) + std::abs(face.weight - font.m_weight);
			if (best == nullptr || apart < distance) {
				best = &face;
				distance = apart;
			}
		}
		if (best != nullptr && std::find(chosen.begin(), chosen.end(), best) == chosen.end())
			chosen.push_back(best);
	};

	const ibString wanted = font.m_faceName.Lower();
	if (!wanted.IsEmpty()) {
		offer([&](const FontFace& face) { return face.family == wanted; });
		offer([&](const FontFace& face) { return face.typographic == wanted; });
	}

	// The kind a face is of: what it is called, else the family the sheet gave it.
	static const ibChar* const s_serif[] = { ibT("times new roman"), ibT("liberation serif") };
	static const ibChar* const s_mono[]  = { ibT("courier new"), ibT("liberation mono") };
	static const ibChar* const s_sans[]  = { ibT("arial"), ibT("liberation sans") };
	const bool serif = wanted == ibT("times new roman") || wanted == ibT("times") || wanted == ibT("liberation serif")
		|| (wanted.IsEmpty() && font.m_family == ibFONTFAMILY_ROMAN);
	const bool mono = wanted == ibT("courier new") || wanted == ibT("courier") || wanted == ibT("consolas")
		|| wanted == ibT("lucida console") || wanted == ibT("liberation mono")
		|| (wanted.IsEmpty() && (font.m_family == ibFONTFAMILY_MODERN || font.m_family == ibFONTFAMILY_TELETYPE));
	for (const ibChar* name : serif ? s_serif : mono ? s_mono : s_sans) {
		const ibString substitute(name);
		offer([&](const FontFace& face) { return face.family == substitute || face.typographic == substitute; });
	}

	offer([](const FontFace&) { return true; });
	return chosen;
}

std::string FileBytes(const ibString& path)
{
	std::ifstream file(ibPathOf(path), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/////////////////////////////////////////////////////////////////////////////
// THE PAGES
/////////////////////////////////////////////////////////////////////////////

// A position inside the margins, in the sheet's pixels, as the PDF's points on the page.
double PageX(double x) { return (kMargin + x) * kPoint; }
double PageY(double y) { return (kMargin + y) * kPoint; }

ibString TextOf(const ibSpreadsheetCellDescription* cell)
{
	// the text, not the stored form — see the xlsx writer
	return cell != nullptr ? ibBackendLocalization::GetTranslateGetRawLocText(cell->GetValue()) : ibString();
}

// The lines typed into a text; a line break at the very end opens none.
std::vector<ibString> LinesOf(const ibString& text)
{
	std::vector<ibString> lines;
	ibString line;
	for (const ibChar ch : text) {
		if (ch == ibT('\n')) {
			lines.push_back(line);
			line.Clear();
		}
		else if (ch != ibT('\r')) {
			line += ch;
		}
	}
	if (!line.IsEmpty())
		lines.push_back(line);
	return lines;
}

// The dashes of a rule, on and off, in points — for a line that many pixels wide.
std::vector<double> DashesOf(ibSpreadsheetPenStyle style, int width)
{
	const double unit = std::max(width, 1) * kPoint;
	switch (style) {
	case ibPenStyle_Dot:       return { unit, unit };
	case ibPenStyle_ShortDash: return { 3 * unit, 2 * unit };
	case ibPenStyle_LongDash:  return { 6 * unit, 3 * unit };
	case ibPenStyle_DotDash:   return { 6 * unit, 2 * unit, unit, 2 * unit };
	default:                   return {};
	}
}

class Printout {
public:

	Printout(const ibSpreadsheetDescription& sheet, ibPdfDocument& pdf)
		: m_sheet(sheet), m_pdf(pdf), m_rows(sheet.GetNumberRows()), m_cols(sheet.GetNumberCols()) {}

	void Print() {
		MeasureRows();
		Paginate();
		// The printout's order: a band of rows, its bands of columns left to right, then the next band down.
		for (size_t rowBand = 0; rowBand < m_rowsPerPage.size(); ++rowBand)
			for (size_t colBand = 0; colBand < m_colsPerPage.size(); ++colBand)
				DrawPage(rowBand, colBand);
	}

private:

	// What a sheet's font is drawn in: the first of its faces that reads. ⚠ NONE AT ALL is a refusal, not a page
	// of empty cells — a PDF that silently drops every letter loses the document instead of showing it.
	void UseFont(const ibFont& font) {
		const std::string key = font.m_faceName.ToUtf8() + '|' + std::to_string(font.m_weight) + '|'
			+ std::to_string(font.m_style) + '|' + std::to_string(font.m_family);
		auto known = m_fontOf.find(key);
		if (known == m_fontOf.end()) {
			int number = ibNOT_FOUND;
			for (const FontFace* face : FacesFor(font)) {
				const auto added = m_faceOf.find(face);
				number = added != m_faceOf.end() ? added->second : m_pdf.AddFont(FileBytes(face->path), face->index);
				m_faceOf[face] = number;
				if (number != ibNOT_FOUND)
					break;
			}
			if (number == ibNOT_FOUND)
				ibCoreException::Error(_("A PDF file cannot be written: no TrueType font was found, neither in the program's \"fonts\" folder nor among the system's fonts."));
			known = m_fontOf.emplace(key, number).first;
		}
		m_pdf.SetFont(known->second, font.m_pointSize, font.m_underlined, font.m_strikethrough);
	}

	// What the window's DC answers, in the sheet's pixels: a text's width, and a line's height as a whole number of
	// them — the height the window's rows were made from.
	double Width(const ibString& text) const {
		double width = 0;
		m_pdf.GetTextExtent(text, &width);
		return width / kPoint;
	}

	double CharHeight() const {
		return static_cast<double>(std::lround(m_pdf.GetCharHeight() / kPoint));
	}

	// The width a cell's text takes drawn as it stands — its widest line, and the inset and the margin a line starts
	// from (DrawCellText) — in the sheet's pixels: what the core weighs against the room to the sheet's end
	// (IsCellWrapped), as the desktop's ibSpreadsheetTextWidth answers it. It sets the cell's font.
	int TextWidth(const ibSpreadsheetCellDescription* cell) {
		UseFont(cell->m_font.IsOk() ? cell->m_font : ibDefaultSpreadsheetFont());
		double widest = 0;
		for (const ibString& one : LinesOf(TextOf(cell)))
			widest = std::max(widest, Width(one));
		return static_cast<int>(std::ceil(widest)) + kCellInset + kTextMargin;
	}

	// One line broken at its spaces so no piece is wider than `maxWidth` — ibGrid::WrapTextLine's rule: the space
	// goes with the word before it, and a word wider than the column is cut between its letters, each piece as wide
	// as fits; a space never starts a piece, nor does the second half of a surrogate pair.
	void Wrap(const ibString& line, double maxWidth, std::vector<ibString>& lines) const {
		if (maxWidth <= 0 || line.IsEmpty() || Width(line) <= maxWidth) {
			lines.push_back(line);
			return;
		}
		ibString current;
		size_t at = 0;
		while (at < line.Len()) {
			size_t end = at;
			while (end < line.Len() && line[end] != ibT(' '))
				++end;
			if (end < line.Len())
				++end;
			const ibString word = line.Mid(at, end - at);
			at = end;
			if (!current.IsEmpty()) {
				if (Width(current + word) <= maxWidth) {
					current += word;
					continue;
				}
				lines.push_back(current);
			}
			size_t from = 0;
			if (Width(word) > maxWidth) {
				for (size_t i = 1; i < word.Len(); ++i) {
					const ibChar letter = word[i];
					if (Width(word.Mid(from, i + 1 - from)) > maxWidth && letter != ibT(' ')
						&& !(letter >= 0xDC00 && letter <= 0xDFFF)) {
						lines.push_back(word.Mid(from, i - from));
						from = i;
					}
				}
			}
			current = word.Mid(from);
		}
		if (!current.IsEmpty())
			lines.push_back(current);
	}

	// A line cut to `maxWidth` with an ellipsis at its end, its start or its middle — as the window cuts it.
	ibString Ellipsized(const ibString& line, double maxWidth, ibSpreadsheetCellDescription::ibFitMode mode) const {
		if (Width(line) <= maxWidth)
			return line;
		const ibString ellipsis(static_cast<ibChar>(0x2026));
		double room = maxWidth - Width(ellipsis);
		if (room <= 0)
			return ellipsis;

		std::vector<double> widths(line.Len());
		for (size_t i = 0; i < line.Len(); ++i)
			widths[i] = Width(ibString(line[i]));

		size_t head = 0, tail = 0;
		const bool fromHead = mode != ibSpreadsheetCellDescription::ibFitMode::Mode_EllipsizeStart;
		const bool fromTail = mode != ibSpreadsheetCellDescription::ibFitMode::Mode_EllipsizeEnd;
		for (bool turn = fromHead; head + tail < line.Len(); turn = fromHead && fromTail ? !turn : turn) {
			const double next = turn ? widths[head] : widths[line.Len() - 1 - tail];
			if (next > room)
				break;
			room -= next;
			(turn ? head : tail) += 1;
		}
		return line.Left(head) + ellipsis + line.Right(tail);
	}

	// A line wider than `maxWidth` shown as the `#` that fit in its place, one at least — ibGrid::MaskTextLine's rule.
	ibString Masked(const ibString& line, double maxWidth) const {
		if (line.IsEmpty() || Width(line) <= maxWidth)
			return line;
		const double hash = Width(ibT("#"));
		const size_t count = hash > 0 ? static_cast<size_t>(std::max(1.0, std::floor(maxWidth / hash))) : 1;
		return ibString(static_cast<ibChar>('#'), count);
	}

	// ⭐ THE ROWS AT THE HEIGHTS THE WINDOW SHOWS THEM AT (ibSpreadsheetRowHeight, frmclient's gridEditorDoc.cpp):
	// a row of its own height keeps it; one without is the default row, taller where a cell's text needs more — a
	// larger font, a line break, wrapping. The default row is one line of the default font and the padding is what
	// that leaves, so a line in the default font never makes a row grow.
	void MeasureRows() {
		m_rowHeights.assign(m_rows, s_defaultRowHeight);
		const ibFont ordinary = ibDefaultSpreadsheetFont();
		double padding = 0;
		bool measured = false;

		for (int row = 0; row < m_rows; ++row) {
			if (m_sheet.HasRowSize(row)) {
				m_rowHeights[row] = m_sheet.GetRowSize(row);
				continue;
			}
			int height = s_defaultRowHeight;
			for (int col = 0; col < m_cols; ++col) {
				const ibSpreadsheetCellDescription* cell = m_sheet.GetCell(row, col);
				if (cell == nullptr || cell->IsEmptyValue())
					continue;
				int spanRows = 1, spanCols = 1;
				if (cell->GetSize(&spanRows, &spanCols) < 0 || spanRows > 1 || cell->m_textOrient == ibOrient_Vertical)
					continue;

				const bool wrap = m_sheet.IsCellWrapped(row, col, [&] { return TextWidth(cell); });
				const double points = cell->m_font.IsOk() ? cell->m_font.m_pointSize : ordinary.m_pointSize;
				const ibString text = TextOf(cell);
				if (text.IsEmpty())
					continue;
				if (!wrap && text.find(ibT('\n')) == ibString::npos && points <= ordinary.m_pointSize)
					continue;

				if (!measured) {
					UseFont(ordinary);
					padding = s_defaultRowHeight - CharHeight();
					measured = true;
				}
				UseFont(cell->m_font.IsOk() ? cell->m_font : ordinary);

				std::vector<ibString> lines = LinesOf(text);
				if (wrap) {
					int width = 0;   // the whole span
					for (int c = col; c < col + std::max(spanCols, 1); ++c)
						width += m_sheet.GetColSize(c);
					std::vector<ibString> wrapped;
					for (const ibString& one : lines)
						Wrap(one, width - kChromeWrapping, wrapped);
					lines = std::move(wrapped);
				}
				const double need = lines.size() * CharHeight() + padding;
				height = std::max(height, static_cast<int>(std::ceil(need)));
			}
			m_rowHeights[row] = height;
		}
	}

	// ⭐ A PAGE BREAK MAY NOT FALL INSIDE A MERGED CELL: a covered cell holds the negative offset to the cell that
	// owns it, so the break belongs at that owner and the whole block goes over (gridPrintout's RowBreakAt).
	int RowBreakAt(int row) const {
		int highest = row;
		for (int col = 0; col < m_cols; ++col) {
			int spanRows = 0, spanCols = 0;
			if (m_sheet.GetCellSize(row, col, &spanRows, &spanCols) < 0 && row + spanRows < highest)
				highest = row + spanRows;
		}
		return highest;
	}

	int ColBreakAt(int col) const {
		int leftmost = col;
		for (int row = 0; row < m_rows; ++row) {
			int spanRows = 0, spanCols = 0;
			if (m_sheet.GetCellSize(row, col, &spanRows, &spanCols) < 0 && col + spanCols < leftmost)
				leftmost = col + spanCols;
		}
		return leftmost;
	}

	// ⭐⭐ THE BANDS, as the printout cuts them (gridPrintout's OnPreparePrinting, where each rule has its story): the
	// whole sheet is walked; the largest break is the sheet's extent and not a break; a block wider or taller than
	// the page is torn where the page ends; and a break is taken only if it moves.
	void Paginate() {
		const int roomWidth = static_cast<int>(kPageWidth / kPoint) - 2 * kMargin;
		const int roomHeight = static_cast<int>(kPageHeight / kPoint) - 2 * kMargin;

		m_colsPerPage.assign(1, 0);
		const int colExtent = m_sheet.GetMaxColBrake();
		for (int i = 0, width = 0; i < m_cols; ++i) {
			const int size = m_sheet.GetColSize(i);
			const bool asked = m_colsPerPage.back() != i && i != colExtent && m_sheet.IsColBrake(i);
			if (width + size > roomWidth || asked) {
				int at = ColBreakAt(i);
				if (at <= m_colsPerPage.back())
					at = i;
				if (at > m_colsPerPage.back()) {
					m_colsPerPage.push_back(at);
					width = 0;
					i = at - 1;
					continue;
				}
			}
			width += size;
		}

		m_rowsPerPage.assign(1, 0);
		const int rowExtent = m_sheet.GetMaxRowBrake();
		for (int i = 0, height = 0; i < m_rows; ++i) {
			const int size = m_rowHeights[i];
			const bool asked = m_rowsPerPage.back() != i && i != rowExtent && m_sheet.IsRowBrake(i);
			if (height + size > roomHeight || asked) {
				int at = RowBreakAt(i);
				if (at <= m_rowsPerPage.back())
					at = i;
				if (at > m_rowsPerPage.back()) {
					m_rowsPerPage.push_back(at);
					height = 0;
					i = at - 1;
					continue;
				}
			}
			height += size;
		}
	}

	enum class Pass { Fill, Text, Rule };

	void DrawPage(size_t rowBand, size_t colBand) {
		const int fromRow = m_rowsPerPage[rowBand];
		const int toRow = rowBand + 1 < m_rowsPerPage.size() ? m_rowsPerPage[rowBand + 1] : m_rows;
		const int fromCol = m_colsPerPage[colBand];
		const int toCol = colBand + 1 < m_colsPerPage.size() ? m_colsPerPage[colBand + 1] : m_cols;

		// Where each column and row of the band begins on the page, and where the last one ends.
		std::vector<double> colX(1, 0), rowY(1, 0);
		for (int col = fromCol; col < toCol; ++col)
			colX.push_back(colX.back() + m_sheet.GetColSize(col));
		for (int row = fromRow; row < toRow; ++row)
			rowY.push_back(rowY.back() + m_rowHeights[row]);

		m_pdf.StartPage(kPageWidth, kPageHeight);
		for (const Pass pass : { Pass::Fill, Pass::Text, Pass::Rule }) {
			for (int row = fromRow; row < toRow; ++row) {
				for (int col = fromCol; col < toCol; ++col) {
					int spanRows = 1, spanCols = 1;
					const int span = m_sheet.GetCellSize(row, col, &spanRows, &spanCols);
					const ibSpreadsheetCellDescription* cell = m_sheet.GetCell(row, col);
					if (span < 0 || cell == nullptr)
						continue;   // covered by a merge, or nothing there

					// ⚠ ONLY AS FAR AS THIS PAGE GOES: a block torn by the pagination is drawn inside its band.
					const int lastRow = span > 0 ? std::min(row + spanRows, toRow) : row + 1;
					const int lastCol = span > 0 ? std::min(col + spanCols, toCol) : col + 1;
					const double x = colX[col - fromCol], w = colX[lastCol - fromCol] - x;
					const double y = rowY[row - fromRow], h = rowY[lastRow - fromRow] - y;
					if (w <= 0 || h <= 0)
						continue;   // a hidden row or column

					switch (pass) {
					case Pass::Fill: DrawFill(cell, x, y, w, h); break;
					case Pass::Text: DrawCellText(cell, row, col, x, y, w, h, fromCol, toCol, colX); break;
					case Pass::Rule: DrawRules(cell, x, y, w, h); break;
					}
				}
			}
		}
		m_pdf.EndPage();
	}

	// The page is white already: a fill is drawn only where a cell has a colour of its own.
	void DrawFill(const ibSpreadsheetCellDescription* cell, double x, double y, double w, double h) {
		const ibColour& colour = cell->m_backgroundColour;
		if (!colour.IsOk() || colour == ibDefaultSpreadsheetBackgroundColour())
			return;
		m_pdf.SetBrush(colour);
		m_pdf.DrawRectangle(PageX(x), PageY(y), w * kPoint, h * kPoint);
	}

	// The four edges, held left, right, top, bottom — on the cell's own lines, the merged block's for a merge.
	void DrawRules(const ibSpreadsheetCellDescription* cell, double x, double y, double w, double h) {
		const double edges[4][4] = {
			{ x, y, x, y + h }, { x + w, y, x + w, y + h }, { x, y, x + w, y }, { x, y + h, x + w, y + h } };
		for (int i = 0; i < 4; ++i) {
			const ibSpreadsheetBorderDescription& border = cell->m_borderAt[i];
			if (border.m_style == ibPenStyle_Transparent)
				continue;
			m_pdf.SetPen(border.m_colour.IsOk() ? border.m_colour : ibDefaultSpreadsheetTextColour(),
				std::max(border.m_width, 1) * kPoint, DashesOf(border.m_style, border.m_width));
			m_pdf.DrawLine(PageX(edges[i][0]), PageY(edges[i][1]), PageX(edges[i][2]), PageY(edges[i][3]));
		}
	}

	// ⭐ A CELL'S TEXT AS ibGrid::DrawTextRectangle PLACES IT, in the rectangle the printout gives it (its left
	// two pixels off): the lines stacked from where the vertical alignment puts the block, each line where the
	// horizontal one puts it, a line after the first drawn only if it fits whole, the whole cut to what the cell
	// may cover. Turned on its side, the same with the two directions exchanged.
	void DrawCellText(const ibSpreadsheetCellDescription* cell, int row, int col,
		double x, double y, double w, double h, int fromCol, int toCol, const std::vector<double>& colX) {
		const ibString text = TextOf(cell);
		if (text.IsEmpty())
			return;

		UseFont(cell->m_font.IsOk() ? cell->m_font : ibDefaultSpreadsheetFont());
		m_pdf.SetTextForeground(cell->m_textColour.IsOk() ? cell->m_textColour : ibDefaultSpreadsheetTextColour());

		const double rx = x + kCellInset, rw = w - kCellInset, ry = y, rh = h;
		const bool vertical = cell->m_textOrient == ibOrient_Vertical;
		const auto mode = cell->m_fitMode;
		const auto textWidth = [&] { return TextWidth(cell); };
		const bool wrapped = m_sheet.IsCellWrapped(row, col, textWidth);
		const double room = (vertical ? rh : rw) - 2 * kTextMargin;

		std::vector<ibString> lines;
		for (const ibString& one : LinesOf(text)) {
			if (wrapped)
				Wrap(one, room, lines);
			else if (mode == ibSpreadsheetCellDescription::ibFitMode::Mode_EllipsizeStart
				|| mode == ibSpreadsheetCellDescription::ibFitMode::Mode_EllipsizeMiddle
				|| mode == ibSpreadsheetCellDescription::ibFitMode::Mode_EllipsizeEnd)
				lines.push_back(Ellipsized(one, room, mode));
			else if (mode == ibSpreadsheetCellDescription::ibFitMode::Mode_Mask)
				lines.push_back(Masked(one, room));
			else
				lines.push_back(one);
		}
		if (lines.empty())
			return;

		// ⭐ LET OUT OVER THE NEIGHBOURS as the screen runs it on (GetCellOverflowCols): Auto that does not break,
		// aligned left, over the empty cells after it, as far as this page's band. Centred and right-aligned text
		// ran out to the left here as well, where the screen and the printout cut it (2026-11-26).
		const double clipLeft = rx;
		double clipRight = rx + rw;
		if (const int overflowCols = m_sheet.GetCellOverflowCols(row, col, toCol, textWidth); overflowCols > 0) {
			int spanCols = 1;
			m_sheet.GetCellSize(row, col, nullptr, &spanCols);
			clipRight = colX[col + spanCols + overflowCols - fromCol];
		}
		m_pdf.SetClippingRegion(PageX(clipLeft), PageY(ry), (clipRight - clipLeft) * kPoint, rh * kPoint);

		const double lineHeight = CharHeight();
		const double block = lines.size() * lineHeight;
		const int horz = cell->m_alignHorz, vert = cell->m_alignVert;

		if (!vertical) {
			double ty = vert == ibAlignmentVert_Bottom ? ry + rh - block
				: vert == ibAlignmentVert_Center ? ry + (rh - block) / 2 : ry;
			for (size_t l = 0; l < lines.size(); ++l) {
				if (l > 0 && ty + lineHeight > ry + rh)
					break;
				if (!lines[l].IsEmpty()) {
					const double lineWidth = Width(lines[l]);
					const double tx = horz == ibAlignmentHorz_Right ? rx + rw - lineWidth - kTextMargin
						: horz == ibAlignmentHorz_Center ? rx + (rw - lineWidth) / 2 : rx + kTextMargin;
					m_pdf.DrawText(lines[l], PageX(tx), PageY(ty));
				}
				ty += lineHeight;
			}
		}
		else {
			double tx = vert == ibAlignmentVert_Bottom ? rx + rw - block - kTextMargin
				: vert == ibAlignmentVert_Center ? rx + (rw - block) / 2 : rx + kTextMargin;
			for (size_t l = 0; l < lines.size(); ++l) {
				if (l > 0 && tx + lineHeight > rx + rw)
					break;
				if (!lines[l].IsEmpty()) {
					const double lineWidth = Width(lines[l]);
					const double ty = horz == ibAlignmentHorz_Right ? ry + lineWidth + kTextMargin
						: horz == ibAlignmentHorz_Center ? ry + rh - (rh - lineWidth) / 2 : ry + rh - kTextMargin;
					m_pdf.DrawRotatedText(lines[l], PageX(tx), PageY(ty), 90);
				}
				tx += lineHeight;
			}
		}
		m_pdf.DestroyClippingRegion();
	}

	const ibSpreadsheetDescription& m_sheet;
	ibPdfDocument& m_pdf;
	const int m_rows;
	const int m_cols;

	std::vector<int> m_rowHeights;
	std::vector<int> m_colsPerPage;   // the first column of each band
	std::vector<int> m_rowsPerPage;   // the first row of each band

	std::map<std::string, int> m_fontOf;          // a sheet's font → the document's
	std::map<const FontFace*, int> m_faceOf;      // a face → the document's font made of it, ibNOT_FOUND if it would not read
};

} // namespace

bool ibSheetFormatPdf::Read(std::istream& /*input*/, ibSpreadsheetDescription& /*sheet*/) const
{
	// See the header: a PDF is a picture of a page, not a sheet.
	return false;
}

bool ibSheetFormatPdf::Write(std::ostream& output, const ibSpreadsheetDescription& sheet) const
{
	ibPdfDocument pdf(output);
	Printout(sheet, pdf).Print();
	return pdf.Close();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatPdf);
