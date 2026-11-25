// =============================================================================
// A PDF drawn here and read back by a reader of its own: the cross-reference table walked to every object, the page
// tree walked to every page, the streams inflated, the text read off each page's drawing. The reader knows the format,
// not the code that wrote it; what each page must say is written as a literal beside the drawing that says it.
// =============================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "backend/sheetFormat/sheetFormatPdf.h"
#include "core/description/spreadsheetDescription.h"
#include "core/fileSystem.h"
#include "core/memory/compression.h"
#include "core/memory/memoryBuffer.h"
#include "core/pdf/pdfDocument.h"
#include "core/spreadsheet/sheetFormat.h"

namespace {

// A font the program ships (fonts/ in the tree, found as the program finds it).
std::string ShippedFont(const char* name)
{
	const ibString folder = ibProgramSubfolder(ibT("fonts"));
	std::ifstream file(ibPathOf(folder + ibFILE_SEP_PATH + ibString::FromUTF8(name)), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Font files packed into one collection (.ttc) as the format lays one out: its header with each face's place, then
// each face's table directory with its tables after it, every record pointing into the whole file.
std::string Collection(const std::vector<std::string>& fonts)
{
	const auto get = [](const std::string& bytes, size_t at, size_t width) {
		uint32_t value = 0;
		for (size_t k = 0; k < width; ++k)
			value = (value << 8) | static_cast<unsigned char>(bytes[at + k]);
		return value;
	};
	const auto put = [](std::string& bytes, size_t at, uint32_t value) {
		for (size_t k = 0; k < 4; ++k)
			bytes[at + k] = static_cast<char>(value >> (24 - 8 * k));
	};

	std::string collection("ttcf\0\1\0\0", 8);
	collection.append(4 + 4 * fonts.size(), '\0');
	put(collection, 8, static_cast<uint32_t>(fonts.size()));
	for (size_t face = 0; face < fonts.size(); ++face) {
		const std::string& font = fonts[face];
		const size_t at = collection.size();
		put(collection, 12 + 4 * face, static_cast<uint32_t>(at));
		const size_t tables = get(font, 4, 2);
		collection.append(font, 0, 12 + 16 * tables);
		for (size_t record = 12; record < 12 + 16 * tables; record += 16) {
			collection.resize((collection.size() + 3) / 4 * 4, '\0');
			put(collection, at + record + 8, static_cast<uint32_t>(collection.size()));
			collection.append(font, get(font, record + 8, 4), get(font, record + 12, 4));
		}
	}
	return collection;
}

// How wide a text is at 10 pt in a font's face, measured in a document of its own; -1 when the face is not taken.
double WidthIn(const std::string& font, int index, const ibString& text)
{
	std::ostringstream out(std::ios::binary);
	ibPdfDocument pdf(out);
	const int number = pdf.AddFont(font, index);
	if (number == ibNOT_FOUND)
		return -1;
	pdf.SetFont(number, 10);
	double width = 0;
	pdf.GetTextExtent(text, &width);
	return width;
}

class PdfReader {
public:

	explicit PdfReader(std::string bytes) : m_bytes(std::move(bytes)) {}

	// Every object the table names, found where it says each one starts; false at the first that is not there.
	bool Read() {
		if (m_bytes.rfind("%PDF-", 0) != 0)
			return false;
		const size_t start = m_bytes.rfind("startxref");
		if (start == std::string::npos)
			return false;
		const size_t table = std::stoul(m_bytes.substr(start + 9));
		if (m_bytes.compare(table, 4, "xref") != 0)
			return false;
		std::istringstream in(m_bytes.substr(table + 4));
		int first = 0, count = 0;
		in >> first >> count;
		for (int number = first; number < first + count; ++number) {
			std::string offset, generation, kind;
			in >> offset >> generation >> kind;
			if (kind != "n")
				continue;
			const size_t at = std::stoul(offset);
			const std::string head = std::to_string(number) + " 0 obj";
			if (m_bytes.compare(at, head.size(), head) != 0)
				return false;
			m_offsets[number] = at + head.size();
		}
		std::string word;
		while (in >> word && word != "trailer") {}
		std::getline(in, m_trailer, '\0');
		return !m_offsets.empty();
	}

	// An object's own text — a stream's dictionary without its bytes.
	std::string Object(int number) const {
		const auto found = m_offsets.find(number);
		if (found == m_offsets.end())
			return std::string();
		const size_t end = m_bytes.find("endobj", found->second);
		const size_t stream = m_bytes.find("stream", found->second);
		return m_bytes.substr(found->second, std::min(end, stream) - found->second);
	}

	// A stream's bytes, inflated when they say they are deflated; its length, when it is another object, read there.
	std::string Stream(int number) const {
		const std::string dictionary = Object(number);
		const size_t at = m_offsets.at(number) + dictionary.size() + 6;   // "stream"
		const size_t data = at + (m_bytes.compare(at, 2, "\r\n") == 0 ? 2 : 1);
		std::smatch length;
		size_t size = 0;
		if (std::regex_search(dictionary, length, std::regex(R"(/Length\s+(\d+)\s+0\s+R)")))
			size = std::stoul(Object(std::stoi(length[1])));
		else if (std::regex_search(dictionary, length, std::regex(R"(/Length\s+(\d+))")))
			size = std::stoul(length[1]);
		const std::string bytes = m_bytes.substr(data, size);
		if (dictionary.find("/FlateDecode") == std::string::npos)
			return bytes;
		ibMemoryBuffer inflated;
		if (!ibZlibInflate(bytes.data(), bytes.size(), inflated))
			return std::string();
		return std::string(static_cast<const char*>(inflated.GetData()), inflated.GetDataLen());
	}

	static int Ref(const std::string& text, const char* key) {
		std::smatch found;
		return std::regex_search(text, found, std::regex(std::string(key) + R"(\s+(\d+)\s+0\s+R)")) ? std::stoi(found[1]) : 0;
	}

	// The pages in their order, the page tree walked from the catalog.
	std::vector<int> Pages() const {
		std::vector<int> pages;
		Walk(Ref(Object(Ref(m_trailer, "/Root")), "/Pages"), pages);
		return pages;
	}

	// What a page says: each string it shows — two bytes a character, as Identity-H over Unicode writes them —
	// one line each.
	ibString Text(int page) const {
		const std::string drawing = Stream(Ref(Object(page), "/Contents"));
		static const std::regex shown(R"(<([0-9A-Fa-f]*)>\s*Tj)");
		ibString text;
		for (std::sregex_iterator it(drawing.begin(), drawing.end(), shown), end; it != end; ++it) {
			const std::string hex = (*it)[1];
			if (!text.IsEmpty())
				text += ibChar('\n');
			for (size_t k = 0; k + 4 <= hex.size(); k += 4)
				text += static_cast<ibChar>(std::stoul(hex.substr(k, 4), nullptr, 16));
		}
		return text;
	}

	const std::string& Bytes() const { return m_bytes; }

private:

	void Walk(int node, std::vector<int>& pages) const {
		const std::string text = Object(node);
		if (text.find("/Kids") == std::string::npos) {
			pages.push_back(node);
			return;
		}
		const std::string kids = text.substr(text.find("/Kids"));
		static const std::regex reference(R"((\d+)\s+0\s+R)");
		const std::string list = kids.substr(0, kids.find(']'));
		for (std::sregex_iterator it(list.begin(), list.end(), reference), end; it != end; ++it)
			Walk(std::stoi((*it)[1]), pages);
	}

	std::string m_bytes;
	std::map<int, size_t> m_offsets;
	std::string m_trailer;
};

std::string Written(const ibSpreadsheetDescription& sheet)
{
	std::ostringstream out(std::ios::binary);
	EXPECT_TRUE(ibSheetFormatPdf().Write(out, sheet));
	return out.str();
}

} // namespace

TEST(PdfDocument, TwoPagesAreReadBackWithTheirText)
{
	const std::string font = ShippedFont("LiberationSans-Regular.ttf");
	ASSERT_FALSE(font.empty()) << "fonts/LiberationSans-Regular.ttf is part of the tree";

	std::ostringstream out(std::ios::binary);
	{
		ibPdfDocument pdf(out);
		const int sans = pdf.AddFont(font);
		ASSERT_NE(sans, ibNOT_FOUND);
		pdf.SetFont(sans, 10);
		pdf.StartPage(595.28, 841.89);
		pdf.SetBrush(ibColour(255, 240, 200));
		pdf.DrawRectangle(30, 30, 200, 20);
		pdf.SetPen(ibColour(0, 0, 0), 0.75, { 3, 2 });
		pdf.DrawLine(30, 50, 230, 50);
		pdf.DrawText(ibT("Invoice"), 32, 32);
		pdf.StartPage(595.28, 841.89);
		pdf.DrawText(ibString(ibT("\u0421\u0443\u043C\u043C\u0430: 1 200,00")), 32, 32);
		pdf.DrawRotatedText(ibT("Up"), 100, 200, 90);
		ASSERT_TRUE(pdf.Close());
	}

	PdfReader reader(out.str());
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 2u);
	EXPECT_EQ(reader.Text(pages[0]), ibString(ibT("Invoice")));
	EXPECT_EQ(reader.Text(pages[1]), ibString(ibT("\u0421\u0443\u043C\u043C\u0430: 1 200,00\nUp")));
}

TEST(PdfDocument, TheFontGoesInWithOnlyTheGlyphsDrawn)
{
	const std::string font = ShippedFont("LiberationSans-Regular.ttf");
	ASSERT_FALSE(font.empty());

	std::ostringstream out(std::ios::binary);
	{
		ibPdfDocument pdf(out);
		pdf.SetFont(pdf.AddFont(font), 12);
		pdf.StartPage(200, 100);
		pdf.DrawText(ibT("abc"), 10, 10);
		ASSERT_TRUE(pdf.Close());
	}

	PdfReader reader(out.str());
	ASSERT_TRUE(reader.Read());
	EXPECT_NE(reader.Bytes().find("LiberationSans"), std::string::npos);
	EXPECT_NE(reader.Bytes().find("/ToUnicode"), std::string::npos);

	std::smatch file;
	ASSERT_TRUE(std::regex_search(reader.Bytes(), file, std::regex(R"(/FontFile2\s+(\d+)\s+0\s+R)")));
	const std::string program = reader.Stream(std::stoi(file[1]));
	ASSERT_GE(program.size(), 12u);
	EXPECT_EQ(program.substr(0, 4), std::string("\0\1\0\0", 4));   // a TrueType font
	EXPECT_LT(program.size(), font.size() / 2) << "three letters do not carry the whole font";
}

TEST(PdfDocument, TextIsMeasuredInTheFont)
{
	std::ostringstream out(std::ios::binary);
	ibPdfDocument pdf(out);
	EXPECT_EQ(pdf.AddFont("not a font at all"), ibNOT_FOUND);

	pdf.SetFont(pdf.AddFont(ShippedFont("LiberationSans-Regular.ttf")), 10);
	double one = 0, two = 0, height = 0;
	pdf.GetTextExtent(ibT("M"), &one, &height);
	pdf.GetTextExtent(ibT("MM"), &two);
	// Liberation Sans keeps Arial's metrics: an M is 1706 of 2048 to the em, a line 1854 above and 434 below.
	EXPECT_NEAR(one, 1706.0 / 2048 * 10, 0.02);
	EXPECT_NEAR(two, 2 * one, 0.001);
	EXPECT_NEAR(height, (1854.0 + 434.0) / 2048 * 10, 0.02);
	EXPECT_DOUBLE_EQ(pdf.GetCharHeight(), height);
}

// The faces of a collection (.ttc) are taken by their number: each measures and draws as the font file it came from.
TEST(PdfDocument, AFaceIsTakenOutOfACollectionByItsNumber)
{
	const std::string regular = ShippedFont("LiberationSans-Regular.ttf");
	const std::string bold = ShippedFont("LiberationSans-Bold.ttf");
	ASSERT_FALSE(regular.empty());
	ASSERT_FALSE(bold.empty());
	const std::string collection = Collection({ regular, bold });

	const ibString text = ibT("Total 1200");
	ASSERT_NE(WidthIn(regular, 0, text), WidthIn(bold, 0, text)) << "the two faces are told apart by their widths";
	EXPECT_DOUBLE_EQ(WidthIn(collection, 0, text), WidthIn(regular, 0, text));
	EXPECT_DOUBLE_EQ(WidthIn(collection, 1, text), WidthIn(bold, 0, text));
	EXPECT_EQ(WidthIn(collection, 2, text), -1) << "the collection holds two faces";
	EXPECT_EQ(WidthIn(regular, 1, text), -1) << "a font file holds one";

	std::ostringstream out(std::ios::binary);
	{
		ibPdfDocument pdf(out);
		pdf.SetFont(pdf.AddFont(collection, 1), 12);
		pdf.StartPage(200, 100);
		pdf.DrawText(text, 10, 10);
		ASSERT_TRUE(pdf.Close());
	}
	PdfReader reader(out.str());
	ASSERT_TRUE(reader.Read());
	EXPECT_EQ(reader.Text(reader.Pages().front()), text);
	EXPECT_NE(reader.Bytes().find("LiberationSans-Bold"), std::string::npos);
}

TEST(SheetFormatPdf, TheFormatIsChosenByTheNameAndOnlyWrites)
{
	const ibSheetFormat* format = ibSheetFormatFor(ibT("report.PDF"));
	ASSERT_NE(format, nullptr);
	EXPECT_EQ(format->GetExtension(), ibString(ibT("pdf")));
	EXPECT_FALSE(format->CanRead());
	EXPECT_TRUE(format->CanWrite());
	EXPECT_NE(ibSheetFormatSaveFilter().find(ibT("*.pdf")), ibString::npos);
	EXPECT_EQ(ibSheetFormatMask().find(ibT("*.pdf")), ibString::npos) << "nothing opens a PDF as a sheet";
}

TEST(SheetFormatPdf, TheCellsAreWrittenRowByRow)
{
	ibSpreadsheetDescription sheet;
	sheet.SetCellValue(0, 0, ibT("Name"));
	sheet.SetCellValue(0, 1, ibString(ibT("\u0421\u0443\u043C\u043C\u0430")));
	sheet.SetCellValue(1, 0, ibT("Tea"));
	sheet.SetCellValue(1, 1, ibT("12.50"));

	PdfReader reader(Written(sheet));
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 1u);
	EXPECT_EQ(reader.Text(pages[0]), ibString(ibT("Name\n\u0421\u0443\u043C\u043C\u0430\nTea\n12.50")));
}

TEST(SheetFormatPdf, AnEmptySheetIsOneBlankPage)
{
	PdfReader reader(Written(ibSpreadsheetDescription()));
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 1u);
	EXPECT_TRUE(reader.Text(pages[0]).IsEmpty());
}

// A4 between margins of 50 is 1022 of the sheet's pixels high and 693 wide: 68 rows of 15, 9 columns of 70.
TEST(SheetFormatPdf, ALongSheetIsCutIntoBandsOfRows)
{
	ibSpreadsheetDescription sheet;
	for (int row = 0; row < 200; ++row)
		sheet.SetCellValue(row, 0, ibString::Format(ibT("Row %d"), row));

	PdfReader reader(Written(sheet));
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 3u);
	EXPECT_TRUE(reader.Text(pages[1]).StartsWith(ibT("Row 68\n")));
	EXPECT_TRUE(reader.Text(pages[2]).StartsWith(ibT("Row 136\n")));
	EXPECT_TRUE(reader.Text(pages[2]).EndsWith(ibT("Row 199")));
}

TEST(SheetFormatPdf, AWideSheetIsCutIntoBandsOfColumnsLeftToRight)
{
	ibSpreadsheetDescription sheet;
	for (int col = 0; col < 15; ++col)
		sheet.SetCellValue(0, col, ibString::Format(ibT("C%d"), col));

	PdfReader reader(Written(sheet));
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 2u);
	EXPECT_EQ(reader.Text(pages[0]), ibString(ibT("C0\nC1\nC2\nC3\nC4\nC5\nC6\nC7\nC8")));
	EXPECT_EQ(reader.Text(pages[1]), ibString(ibT("C9\nC10\nC11\nC12\nC13\nC14")));
}

// The largest break is where the sheet ends, not a break; one above it is taken.
TEST(SheetFormatPdf, ABreakTheSheetAsksForStartsAPage)
{
	ibSpreadsheetDescription sheet;
	for (int row = 0; row < 10; ++row)
		sheet.SetCellValue(row, 0, ibString::Format(ibT("Row %d"), row));
	sheet.AddRowBrake(5);
	sheet.AddRowBrake(9);

	PdfReader reader(Written(sheet));
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 2u);
	EXPECT_TRUE(reader.Text(pages[0]).EndsWith(ibT("Row 4")));
	EXPECT_TRUE(reader.Text(pages[1]).StartsWith(ibT("Row 5\n")));
}

// The page would end after row 67, inside a block merged over rows 66 to 70: the block goes over whole.
TEST(SheetFormatPdf, APageDoesNotEndInsideAMergedCell)
{
	ibSpreadsheetDescription sheet;
	for (int row = 0; row < 100; ++row) {
		if (row < 67 || row > 70)
			sheet.SetCellValue(row, 0, ibString::Format(ibT("Row %d"), row));
	}
	sheet.SetCellSize(66, 0, 5, 1);

	PdfReader reader(Written(sheet));
	ASSERT_TRUE(reader.Read());
	const std::vector<int> pages = reader.Pages();
	ASSERT_EQ(pages.size(), 2u);
	EXPECT_TRUE(reader.Text(pages[0]).EndsWith(ibT("Row 65")));
	EXPECT_TRUE(reader.Text(pages[1]).StartsWith(ibT("Row 66\nRow 71")));
}
