// =============================================================================
// A workbook read into our document - the parts written here by hand, zipped in memory, read by the reader. What
// each cell must say is written as a literal beside the part that says it: the reference is not the reader.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/mstream.h>
#include <wx/zipstrm.h>

#include <string>
#include <utility>
#include <vector>

#include "backend/sheetFormat/sheetFormatXlsx.h"
#include "core/description/spreadsheetDescription.h"
#include "core/exception.h"

namespace {

void Zip(const std::vector<std::pair<const char*, std::string>>& parts, wxMemoryOutputStream& out)
{
	{
		wxZipOutputStream zip(out);
		for (const auto& part : parts) {
			zip.PutNextEntry(wxString::FromUTF8(part.first));
			zip.Write(part.second.data(), part.second.size());
		}
		zip.Close();
	}
}

bool Read(const std::vector<std::pair<const char*, std::string>>& parts, ibSpreadsheetDescription& sheet)
{
	wxMemoryOutputStream zipped;
	Zip(parts, zipped);
	wxMemoryInputStream in(zipped);
	return ibSheetFormatXlsx().Read(in, sheet);
}

ibString CellText(const ibSpreadsheetDescription& sheet, int row, int col)
{
	const ibSpreadsheetCellDescription* cell = sheet.GetCell(row, col);
	return cell != nullptr ? cell->GetValue() : ibString();
}

const char* const kRels =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	"<Relationship Id=\"rId1\" Type=\"worksheet\" Target=\"worksheets/sheet1.xml\"/>"
	"<Relationship Id=\"rId2\" Type=\"worksheet\" Target=\"worksheets/sheet2.xml\"/>"
	"<Relationship Id=\"rId3\" Type=\"sharedStrings\" Target=\"sharedStrings.xml\"/>"
	"<Relationship Id=\"rId4\" Type=\"styles\" Target=\"styles.xml\"/>"
	"</Relationships>";

// The workbook lists the SECOND file first: the tabs' order is the workbook's, not the files' names.
const char* const kWorkbook =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
	" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
	"<sheets><sheet name=\"Summary\" sheetId=\"2\" r:id=\"rId2\"/><sheet name=\"Data\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
	"</workbook>";

// String 0 is split across two runs; string 1 is Cyrillic; string 2 is a single space kept by xml:space.
const char* const kShared =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"3\" uniqueCount=\"3\">"
	"<si><r><rPr><b/></rPr><t>Total</t></r><r><t xml:space=\"preserve\"> 2026 &amp; on</t></r></si>"
	"<si><t>\xD0\x9F\xD1\x96\xD0\xB4\xD1\x81\xD1\x83\xD0\xBC\xD0\xBE\xD0\xBA</t></si>"
	"<si><t xml:space=\"preserve\"> </t></si>"
	"</sst>";

// Style 1 fills its cell with 336699.
const char* const kStyles =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
	"<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
	"<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
	"<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF336699\"/></patternFill></fill></fills>"
	"<borders count=\"1\"><border/></borders>"
	"<cellXfs count=\"2\"><xf fontId=\"0\" fillId=\"0\" borderId=\"0\"/><xf fontId=\"0\" fillId=\"1\" borderId=\"0\"/></cellXfs>"
	"</styleSheet>";

const char* const kSummary =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
	"<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\" s=\"1\"><v>1</v></c></row>"
	"<row r=\"2\"><c r=\"A2\"><f>1+1</f><v>2</v></c><c r=\"C2\" t=\"inlineStr\"><is><t>in &lt;line&gt;</t></is></c>"
	"<c r=\"D2\" t=\"s\"><v>2</v></c></row>"
	"</sheetData><mergeCells count=\"1\"><mergeCell ref=\"A1:B1\"/></mergeCells></worksheet>";

const char* const kData =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
	"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
	"<row r=\"1\"><c r=\"A1\"><v>42</v></c></row>"
	"</sheetData></worksheet>";

} // namespace

TEST(SheetFormatXlsxRead, AWorkbookIsReadInTheWorkbooksOrderWithItsStringsStylesAndMerges)
{
	ibSpreadsheetDescription sheet;
	ASSERT_TRUE(Read({
		{ "xl/_rels/workbook.xml.rels", kRels },
		{ "xl/workbook.xml", kWorkbook },
		{ "xl/sharedStrings.xml", kShared },
		{ "xl/styles.xml", kStyles },
		{ "xl/worksheets/sheet1.xml", kData },
		{ "xl/worksheets/sheet2.xml", kSummary },
	}, sheet));

	// "Summary" first, two rows.
	EXPECT_EQ(CellText(sheet, 0, 0), ibString(ibT("Total 2026 & on")));
	EXPECT_EQ(CellText(sheet, 0, 1), ibString(ibT("\x041F\x0456\x0434\x0441\x0443\x043C\x043E\x043A")));
	EXPECT_EQ(CellText(sheet, 1, 0), ibString(ibT("2")));            // the value, not the formula
	EXPECT_EQ(CellText(sheet, 1, 2), ibString(ibT("in <line>")));
	EXPECT_EQ(CellText(sheet, 1, 3), ibString(ibT(" ")));
	EXPECT_EQ(sheet.GetCellBackgroundColour(0, 1), ibColour(0x33, 0x66, 0x99));
	EXPECT_FALSE(sheet.GetCellBackgroundColour(0, 0).IsOk());
	int rows = 0, cols = 0;
	sheet.GetCellSize(0, 0, &rows, &cols);
	EXPECT_EQ(rows, 1);
	EXPECT_EQ(cols, 2);

	// "Data" under it.
	EXPECT_EQ(CellText(sheet, 2, 0), ibString(ibT("42")));
}

TEST(SheetFormatXlsxRead, APartThatIsNotXmlRefusesTheFileAndAWorkbookThatNamesNoSheetIsRefused)
{
	ibSpreadsheetDescription sheet;
	sheet.SetCellValue(5, 5, ibT("kept"));
	// The shared strings do not parse: the file is refused in the parser's words, naming the part — not read with its
	// string cells empty — and the document it was handed stays as it was.
	ibString refusal;
	try {
		Read({
			{ "xl/_rels/workbook.xml.rels", kRels },
			{ "xl/workbook.xml", kWorkbook },
			{ "xl/sharedStrings.xml", "<sst><si><t>broken</si>" },
			{ "xl/worksheets/sheet1.xml", kData },
		}, sheet);
	}
	catch (const ibCoreException& err) {
		refusal = err.GetErrorDescription();
	}
	EXPECT_TRUE(refusal.Contains(ibT("xl/sharedstrings.xml"))) << refusal.ToStdString();
	EXPECT_EQ(CellText(sheet, 5, 5), ibString(ibT("kept")));

	// A well-formed workbook that lists no sheet, and no sheet1 to fall back on: nothing to read, and the document
	// it was handed stays as it was.
	ibSpreadsheetDescription untouched;
	untouched.SetCellValue(0, 0, ibT("kept"));
	EXPECT_FALSE(Read({ { "xl/_rels/workbook.xml.rels", kRels }, { "xl/workbook.xml", "<workbook><sheets/></workbook>" } }, untouched));
	EXPECT_EQ(CellText(untouched, 0, 0), ibString(ibT("kept")));
}

TEST(SheetFormatXlsxRead, AnIndentedStylesPartGivesEachCellItsOwnStyle)
{
	// The same styles as kStyles, laid out by a program that indents and comments: the tables are indexed by
	// position, and neither the whitespace nor the comment between their elements is an element.
	const char* const indented =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
		"  <fonts count=\"2\">\n    <font><sz val=\"11\"/></font>\n    <!-- bold -->\n    <font><b/><sz val=\"11\"/></font>\n  </fonts>\n"
		"  <fills count=\"2\">\n    <fill><patternFill patternType=\"none\"/></fill>\n"
		"    <fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF336699\"/></patternFill></fill>\n  </fills>\n"
		"  <borders count=\"2\">\n    <border/>\n    <border><left style=\"thin\"/></border>\n  </borders>\n"
		"  <cellXfs count=\"2\">\n    <xf fontId=\"0\" fillId=\"0\" borderId=\"0\"/>\n"
		"    <xf fontId=\"1\" fillId=\"1\" borderId=\"1\"/>\n  </cellXfs>\n"
		"</styleSheet>\n";

	ibSpreadsheetDescription sheet;
	ASSERT_TRUE(Read({
		{ "xl/_rels/workbook.xml.rels", kRels },
		{ "xl/workbook.xml", kWorkbook },
		{ "xl/sharedStrings.xml", kShared },
		{ "xl/styles.xml", indented },
		{ "xl/worksheets/sheet1.xml", kData },
		{ "xl/worksheets/sheet2.xml", kSummary },
	}, sheet));
	EXPECT_EQ(sheet.GetCellBackgroundColour(0, 1), ibColour(0x33, 0x66, 0x99));
	const ibSpreadsheetCellDescription* styled = sheet.GetCell(0, 1);
	ASSERT_NE(styled, nullptr);
	EXPECT_EQ(styled->m_font.m_weight, ibFONTWEIGHT_BOLD);
	EXPECT_NE(styled->m_borderAt[0].m_style, ibPenStyle_Transparent);   // the left side of border 1
}
