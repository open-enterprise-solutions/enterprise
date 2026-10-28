////////////////////////////////////////////////////////////////////////////
//	Description : our document, written out as a Word file
////////////////////////////////////////////////////////////////////////////

// ⭐ A .docx IS A ZIP OF XML PARTS — the same shape as the workbook next door,
// with a different vocabulary inside. Four parts are enough for a document that
// Word opens without complaint:
//
//   [Content_Types].xml            what each part is
//   _rels/.rels                    where the document is
//   word/document.xml              the table itself
//   word/_rels/document.xml.rels   (empty, and still required)
//
// ⭐⭐ THE SHEET BECOMES ONE TABLE. A row is a row, a cell is a cell, and the
// page breaks a workbook's sheets left behind become Word's own page breaks —
// so a report that printed on three pages here prints on three there.

#include "backend/sheetFormat/sheetFormatDocx.h"
#include "backend/backend_localization.h"   // a caption goes out in one language

#include <wx/zipstrm.h>

#include <set>

namespace {

// The same four characters, and the same refusal of control codes: Word rejects
// the whole part rather than the character.
ibString XmlText(const ibString& text)
{
	ibString out;
	out.reserve(text.length() + 16);

	for (const ibChar ch : text) {
		switch (ch) {
		case '&':  out += ibT("&amp;");  break;
		case '<':  out += ibT("&lt;");   break;
		case '>':  out += ibT("&gt;");   break;
		case '"':  out += ibT("&quot;"); break;
		case '\'': out += ibT("&apos;"); break;
		default:
			if (ch >= 0x20 || ch == '\t' || ch == '\n' || ch == '\r')
				out += ch;
			break;
		}
	}
	return out;
}

ibString HexOf(const ibColour& colour)
{
	return ibString::Format(ibT("%02X%02X%02X"), colour.m_red, colour.m_green, colour.m_blue);
}

void AddPart(wxZipOutputStream& zip, const ibString& name, const ibString& body)
{
	zip.PutNextEntry(name);
	const std::string utf8 = body.ToUTF8();
	zip.Write(utf8.data(), utf8.length());
}

ibString ContentTypes()
{
	return ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		ibT("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">")
		ibT("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>")
		ibT("<Default Extension=\"xml\" ContentType=\"application/xml\"/>")
		ibT("<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>")
		ibT("<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>")
		ibT("<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>")
		ibT("</Types>");
}

// ⭐⭐ THE DOCUMENT DECLARES WHICH WORD IT IS FOR, and until it did Word opened it in COMPATIBILITY
// MODE — which is not a label on the title bar but a different engine underneath: the pre-2013
// table layout, and on a table of tens of thousands of rows that is the difference between a
// document and a hang (Max, 2026-08-30: the title bar read "compatibility mode" and then "not
// responding", with one core pegged).
//
// A part of four lines, and it is the whole statement: we write today's WordprocessingML.
ibString Settings()
{
	return ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		ibT("<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
		ibT("<w:compat>")
		ibT("<w:compatSetting w:name=\"compatibilityMode\"")
		ibT(" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/>")
		ibT("</w:compat>")
		ibT("</w:settings>");
}

// ⭐⭐ AND WHAT A PARAGRAPH LOOKS LIKE WHEN NOBODY SAYS. With no styles part, Word applies its own
// defaults to every cell: 8pt of space AFTER each paragraph and 1.08 line spacing. A sheet's row is
// a line of text and nothing else, so every row came out half again as tall as it is here — which
// is both wrong to look at and hundreds of extra pages for Word to lay out.
//
// The sheet's own default size is declared here too (half-points), so the runs below need only
// speak when a cell DIFFERS from it — the common case then costs nothing to say.
ibString Styles()
{
	return ibString::Format(
		ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		ibT("<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
		ibT("<w:docDefaults>")
		ibT("<w:rPrDefault><w:rPr><w:sz w:val=\"%d\"/></w:rPr></w:rPrDefault>")
		ibT("<w:pPrDefault><w:pPr>")
		ibT("<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>")
		ibT("</w:pPr></w:pPrDefault>")
		ibT("</w:docDefaults>")
		ibT("</w:styles>"),
		ibDefaultSpreadsheetFont().GetPointSize() * 2);
}

ibString RootRels()
{
	return ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		ibT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
		ibT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>")
		ibT("</Relationships>");
}

ibString DocumentRels()
{
	// The two parts the document leans on. It was empty here — required even then, because a
	// document part with no relationships part is reported as corrupt rather than as a document
	// with nothing linked — and the parts it now names are what keep Word out of compatibility mode
	// and off its own paragraph defaults.
	return ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		ibT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
		ibT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>")
		ibT("<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>")
		ibT("</Relationships>");
}

// ⭐ THE SAME FOUR EDGES AS THE WORKBOOK, in Word's vocabulary: a line name, a weight in EIGHTHS of
// a point, and a colour.
//
// ⚠ THE SCHEMA ORDERS THEM top → left → bottom → right, and ours are held left, right, top, bottom.
//
// 🛑⭐ AN EDGE THE CELL DOES NOT HAVE IS NOT WRITTEN AT ALL — not written as `nil`. It was, and the
// `nil` was there to overrule the blanket grid the TABLE used to declare; the same change that put
// the borders on the cells took that grid away, so the four fillers were overruling nothing. On a
// sheet whose cells have no borders — which is every cell of a 26 000-row export — that is ~100
// bytes of nothing per cell, some 15 MB of XML, and Word still resolving four edges against four
// neighbours for every one of 156 000 cells. The document opened and then held the processor
// (Max, 2026-08-30).
//
// ⭐ The shape of the mistake: a defence built on top of a fact that had already been removed. The
// table says nothing about borders now, so silence about an edge already means "no edge".
ibString BordersOf(const ibSpreadsheetCellDescription* cell)
{
	if (cell == nullptr)
		return ibEmptyString;

	static const int           s_ours[4]  = { 2, 0, 3, 1 };   // top, left, bottom, right
	static const ibChar* const s_names[4] = { ibT("top"), ibT("left"), ibT("bottom"), ibT("right") };

	ibString edges;
	for (int i = 0; i < 4; i++) {
		const ibSpreadsheetBorderDescription& border = cell->m_borderAt[s_ours[i]];
		if (border.m_style == ibPenStyle_Transparent)
			continue;

		const ibChar* line = ibT("single");
		switch (border.m_style) {
		case ibPenStyle_Dot:        line = ibT("dotted");  break;
		case ibPenStyle_ShortDash:
		case ibPenStyle_LongDash:   line = ibT("dashed");  break;
		case ibPenStyle_DotDash:    line = ibT("dotDash"); break;
		default: break;
		}

		edges += ibString::Format(ibT("<w:%s w:val=\"%s\" w:sz=\"%d\" w:space=\"0\" w:color=\"%s\"/>"),
			s_names[i], line, std::max(border.m_width, 1) * 4, HexOf(border.m_colour));
	}

	return edges.IsEmpty() ? ibString() : ibT("<w:tcBorders>") + edges + ibT("</w:tcBorders>");
}

// One cell's text, with the font and colours it had here.
ibString RunOf(const ibSpreadsheetCellDescription* cell, const ibString& text)
{
	// ⚠ IN THE SCHEMA'S ORDER, which for a run is `rFonts → b → i → strike → color → sz → u`.
	// `w:rPr` is a SEQUENCE and not a bag: a validator reads a member out of place as the element
	// simply not being there, and the ones after it as unexpected.
	ibString properties;

	if (cell != nullptr) {
		const ibFont& font = cell->m_font;
		if (font.IsOk()) {
			if (!font.m_faceName.IsEmpty())
				properties += ibString::Format(
					ibT("<w:rFonts w:ascii=\"%s\" w:hAnsi=\"%s\"/>"),
					XmlText(font.m_faceName), XmlText(font.m_faceName));
			if (font.m_weight >= ibFONTWEIGHT_BOLD)
				properties += ibT("<w:b/>");
			if (font.m_style == ibFONTSTYLE_ITALIC)
				properties += ibT("<w:i/>");
			if (font.m_strikethrough)
				properties += ibT("<w:strike/>");
		}

		if (cell->m_textColour.IsOk())
			properties += ibString::Format(ibT("<w:color w:val=\"%s\"/>"), HexOf(cell->m_textColour));

		if (font.IsOk()) {
			// WORD COUNTS IN HALF-POINTS — and says nothing where the cell agrees with the size the
			// styles part already declared for the whole document. On a sheet where every cell is
			// the default size that is one element saved per cell, out of a hundred thousand.
			if (font.GetPointSize() > 0 && font.GetPointSize() != ibDefaultSpreadsheetFont().GetPointSize())
				properties += ibString::Format(ibT("<w:sz w:val=\"%d\"/>"), font.GetPointSize() * 2);
			if (font.m_underlined)
				properties += ibT("<w:u w:val=\"single\"/>");
		}
	}

	ibString run = ibT("<w:r>");
	if (!properties.IsEmpty())
		run += ibT("<w:rPr>") + properties + ibT("</w:rPr>");
	run += ibString::Format(ibT("<w:t xml:space=\"preserve\">%s</w:t>"), XmlText(text));
	run += ibT("</w:r>");
	return run;
}

// ⭐⭐ THE TABLE NO LONGER DRAWS ITS OWN GRID. It used to declare `single` on all six edges
// including `insideH`/`insideV`, so every export came out fully ruled whatever the report looked
// like — a borderless report gained lines it never had, and a report with its own borders had them
// overruled. The cells say it now, each for itself (`BordersOf`), which is what the workbook next
// door already does (Max, 2026-08-30: *"these properties must move over"*).
//
// ⚠ AND THE LAYOUT IS FIXED, or the widths below are advice. Left to `autofit`, Word re-measures
// every column from its contents and the `w:tcW` we compute is ignored — the same table, two
// different shapes, depending on what happened to be typed in it.
ibString TableStart()
{
	return ibT("<w:tbl><w:tblPr>")
		ibT("<w:tblW w:w=\"0\" w:type=\"auto\"/>")
		ibT("<w:tblLayout w:type=\"fixed\"/>")
		ibT("<w:tblCellMar>")
		ibT("<w:left w:w=\"28\" w:type=\"dxa\"/><w:right w:w=\"28\" w:type=\"dxa\"/>")
		ibT("</w:tblCellMar>")
		ibT("</w:tblPr>");
}

// ⭐⭐ A TABLE IS LAID OUT AS ONE PIECE, so a long sheet is written as MANY TABLES. Word does not
// paginate a table incrementally — it measures the whole thing, and the cost grows faster than the
// number of rows, which is why 26 000 rows in one table opened and then stopped answering while a
// core ran flat out (Max, 2026-08-30). Two hundred rows each, laid one after another with the same
// `<w:tblGrid>`, look like the one table they replace and lay out in a straight line.
const int s_rowsPerTable = 200;

// ⚠ AND THEY NEED SOMETHING BETWEEN THEM. Two `<w:tbl>` elements with nothing in between are JOINED
// back into one table when the document is opened — the split would be undone by the reader and
// nothing gained. So a paragraph stands between them, and it is a HIDDEN one: a paragraph mark
// marked `w:vanish` is not displayed, not printed and takes no height, so the tables still abut.
ibString TableSeparator()
{
	return ibT("<w:p><w:pPr>")
		ibT("<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"1\" w:lineRule=\"exact\"/>")
		ibT("<w:rPr><w:vanish/><w:sz w:val=\"2\"/></w:rPr>")
		ibT("</w:pPr></w:p>");
}

// ⚠ AND THE SPLIT MUST NOT FALL THROUGH A MERGE. A `vMerge` continuation only means anything inside
// the table that started it — carried into a new table it becomes a cell of its own, and the merge
// comes apart. So a row that continues one is never the first row of a chunk.
bool ContinuesMerge(const ibSpreadsheetDescription& sheet, int row, int numCols)
{
	for (int col = 0; col <= numCols; col++) {
		const ibSpreadsheetCellDescription* cell = sheet.GetCell(row, col);
		if (cell != nullptr && cell->GetSize(nullptr, nullptr) == -1)
			return true;
	}
	return false;
}

} // namespace

bool ibSheetFormatDocx::Read(wxInputStream& /*input*/, ibSpreadsheetDescription& /*sheet*/) const
{
	// See the header: a Word file is a flow, not a sheet. Refused plainly rather
	// than half-guessed.
	return false;
}

bool ibSheetFormatDocx::Write(wxOutputStream& output, const ibSpreadsheetDescription& sheet) const
{
	const int numRows = sheet.GetNumberRows();
	const int numCols = sheet.GetNumberCols();

	// The rows a page break falls on — looked up per row while writing.
	std::set<int> pageBreaks;
	for (int i = 0; i < sheet.GetBrakeNumberRows(); i++)
		pageBreaks.insert(sheet.GetRowBrakeByIdx(i));

	ibString body = ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		ibT("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
		ibT("<w:body>");

	// ⚠ THE GRID IS DECLARED FIRST and Word believes it over the cells: column
	// widths live here, in twentieths of a point, and our pixels are 96 to the inch
	// (a pixel is 15 twips).
	// ⚠ AND EVERY TABLE CARRIES IT, not only the first. A page break closes the table and opens
	// another, and the one after the break was opened without a grid at all — which is why its
	// columns came out re-measured while the first page's held.
	ibString grid = ibT("<w:tblGrid>");
	for (int col = 0; col <= numCols; col++)
		grid += ibString::Format(ibT("<w:gridCol w:w=\"%d\"/>"), sheet.GetColSize(col) * 15);
	grid += ibT("</w:tblGrid>");

	ibString table = TableStart() + grid;
	int rowsInTable = 0;

	for (int row = 0; row <= numRows; row++) {

		ibString cells;
		for (int col = 0; col <= numCols; col++) {

			const ibSpreadsheetCellDescription* cell = sheet.GetCell(row, col);

			int spanRows = 1, spanCols = 1;
			const int span = cell != nullptr ? cell->GetSize(&spanRows, &spanCols) : 0;

			// ⭐ A MERGE IS SAID TWICE IN WORD, once per direction: sideways as a span
			// on the owning cell, downwards as "this cell continues the one above" on
			// every cell it covers. A covered cell therefore still has to be WRITTEN —
			// unlike in a workbook, where it is simply absent.
			//
			// 🛑 AND A COVERED CELL KNOWS ITS OWNER, which is what a merge spanning BOTH
			// directions needs. A covered cell holds `(ownerRow - row, ownerCol - col)`, so the
			// continuation row can ask the owner how wide it is: the leftmost covered column
			// carries the `vMerge` WITH the same `gridSpan`, and the columns inside that span are
			// skipped. Written one cell per covered column instead, the rows below a 2×2 merge
			// held more grid columns than the row above them and Word drew the merge crooked.
			int continuedCols = 1;
			if (span == -1) {
				const int ownerRow = row + spanRows;   // both offsets are zero or negative
				const int ownerCol = col + spanCols;

				if (ownerCol != col)
					continue;   // inside the owner's gridSpan — the cell to the left speaks for it

				int ownerRows = 1, ownerCols = 1;
				if (const ibSpreadsheetCellDescription* owner = sheet.GetCell(ownerRow, ownerCol))
					owner->GetSize(&ownerRows, &ownerCols);
				continuedCols = std::max(ownerCols, 1);
			}

			// ⚠ THE WIDTH IS THE GRID'S TO STATE, and it states it once at the top of the table.
			// Repeated on every cell it is 156 000 copies of what `<w:tblGrid>` already said — and
			// under `tblLayout fixed` the grid is what Word reads anyway. A cell says a width only
			// where the grid cannot: when it spans several of its columns.
			//
			// …and on the FIRST row, which some readers of a fixed-layout table measure from
			// instead of the grid. Six elements once, rather than six on every row.
			ibString properties;

			const int spanned = (span == 1) ? spanCols : (span == -1 ? continuedCols : 1);

			if (row == 0 || spanned > 1) {
				int width = 0;
				for (int i = 0; i < spanned; i++)
					width += sheet.GetColSize(col + i);
				properties += ibString::Format(ibT("<w:tcW w:w=\"%d\" w:type=\"dxa\"/>"), width * 15);
			}

			if (spanned > 1)
				properties += ibString::Format(ibT("<w:gridSpan w:val=\"%d\"/>"), spanned);

			if (span == 1 && spanRows > 1)
				properties += ibT("<w:vMerge w:val=\"restart\"/>");
			else if (span == -1)
				properties += ibT("<w:vMerge/>");

			properties += BordersOf(cell);

			if (cell != nullptr && cell->m_backgroundColour.IsOk())
				properties += ibString::Format(
					ibT("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%s\"/>"),
					HexOf(cell->m_backgroundColour));

			// ⚠ AND `w:tcPr` IS A SEQUENCE TOO: `tcW → gridSpan → vMerge → tcBorders → shd →
			// textDirection → vAlign`. The direction stands BEFORE the vertical alignment.
			if (cell != nullptr) {
				// TEXT ORIENTATION — a direction here, a text FLOW there. Bottom-to-top is the one
				// a sheet means by «vertical», the same rotation the workbook writes as 90°.
				if (cell->m_textOrient == wxVERTICAL)
					properties += ibT("<w:textDirection w:val=\"btLr\"/>");

				switch (cell->m_alignVert) {
				case ibAlignmentVert_Center: properties += ibT("<w:vAlign w:val=\"center\"/>"); break;
				case ibAlignmentVert_Bottom: properties += ibT("<w:vAlign w:val=\"bottom\"/>"); break;
				default: break;
				}
			}

			ibString paragraphProperties;
			if (cell != nullptr) {
				switch (cell->m_alignHorz) {
				case ibAlignmentHorz_Center: paragraphProperties = ibT("<w:jc w:val=\"center\"/>"); break;
				case ibAlignmentHorz_Right:  paragraphProperties = ibT("<w:jc w:val=\"right\"/>");  break;
				default: break;
				}
			}

			// the text, not the stored form — see the xlsx writer
			const ibString text = cell != nullptr ? ibLocalization::GetTranslateGetRawLocText(ibBackendLocalization::GetUserLanguage(), cell->GetValue()) : ibString();

			ibString paragraph = ibT("<w:p>");
			if (!paragraphProperties.IsEmpty())
				paragraph += ibT("<w:pPr>") + paragraphProperties + ibT("</w:pPr>");
			if (!text.IsEmpty())
				paragraph += RunOf(cell, text);
			paragraph += ibT("</w:p>");

			// ⚠ A CELL ALWAYS CARRIES A PARAGRAPH, even an empty one: Word reports a
			// table cell without one as a damaged file. Its PROPERTIES are another matter — an
			// empty `<w:tcPr></w:tcPr>` on every cell of a large sheet is a megabyte of nothing.
			cells += ibT("<w:tc>");
			if (!properties.IsEmpty())
				cells += ibT("<w:tcPr>") + properties + ibT("</w:tcPr>");
			cells += paragraph + ibT("</w:tc>");

			// A cell covered SIDEWAYS was already spoken for by the gridSpan above.
			if (span == 1 && spanCols > 1)
				col += spanCols - 1;
			else if (span == -1 && continuedCols > 1)
				col += continuedCols - 1;
		}

		if (cells.IsEmpty())
			continue;

		// ⭐ A HIDDEN ROW IS A ROW THAT IS NOT THERE. Word has no «hidden» for a table row, and a
		// height of nothing draws a hairline instead of nothing — so the row is left out, which is
		// what «Hide» asked for and what the workbook says with `hidden="1"`.
		//
		// ⚠ LEFT OUT OF THE TABLE, NOT OUT OF THE WALK: a page break that fell on it still has to
		// happen, so this skips the row and not the rest of the turn.
		const int height = sheet.GetRowSize(row);

		// 🛑 AND A HEIGHT NEEDS ITS RULE. `w:hRule` defaults to `auto`, and under `auto` Word
		// IGNORES the `w:val` beside it — the height was written, was valid, and did nothing.
		// `atLeast` is what a sheet's row height means: this tall, taller if the text needs it.
		ibString rowProperties;
		if (height != s_defaultRowHeight && height > 0)
			rowProperties = ibString::Format(
				ibT("<w:trPr><w:trHeight w:hRule=\"atLeast\" w:val=\"%d\"/></w:trPr>"), height * 15);

		if (height > 0) {
			table += ibT("<w:tr>") + rowProperties + cells + ibT("</w:tr>");
			rowsInTable++;
		}

		// ⭐ WHERE A PAGE ENDED HERE, A PAGE ENDS THERE. A break inside a table has to
		// close it and open the next one — Word has no "break between these rows".
		if (pageBreaks.find(row) != pageBreaks.end() && row < numRows) {
			table += ibT("</w:tbl>");
			body += table;
			body += ibT("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
			table = TableStart() + grid;
			rowsInTable = 0;
		}
		// …and where nothing ended, the table is closed anyway once it has grown long enough. The
		// reader sees the same rows in the same places; Word gets a hundred small problems instead
		// of one large one.
		else if (rowsInTable >= s_rowsPerTable && row < numRows
			&& !ContinuesMerge(sheet, row + 1, numCols)) {
			table += ibT("</w:tbl>");
			body += table + TableSeparator();
			table = TableStart() + grid;
			rowsInTable = 0;
		}
	}

	// ⚠ AND ONLY IF IT HOLDS ANYTHING. A split that fell on the very last row leaves a table opened
	// and never filled, and a `<w:tbl>` with no rows in it is a table Word draws as a blank stub.
	if (rowsInTable > 0) {
		table += ibT("</w:tbl>");
		body += table;
	}

	// A trailing paragraph — a body that ends with a table leaves Word with nowhere
	// to put the cursor after it.
	body += ibT("<w:p/>");
	body += ibT("</w:body></w:document>");

	wxZipOutputStream zip(output);
	if (!zip.IsOk())
		return false;

	AddPart(zip, ibT("[Content_Types].xml"),          ContentTypes());
	AddPart(zip, ibT("_rels/.rels"),                  RootRels());
	AddPart(zip, ibT("word/_rels/document.xml.rels"), DocumentRels());
	AddPart(zip, ibT("word/styles.xml"),              Styles());
	AddPart(zip, ibT("word/settings.xml"),            Settings());
	AddPart(zip, ibT("word/document.xml"),            body);

	// The central directory is written on Close — a zip that is merely destructed
	// is a file that exists and is not a zip.
	return zip.Close();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatDocx);
