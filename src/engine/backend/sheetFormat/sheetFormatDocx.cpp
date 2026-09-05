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

#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <set>

namespace {

// The same four characters, and the same refusal of control codes: Word rejects
// the whole part rather than the character.
wxString XmlText(const wxString& text)
{
	wxString out;
	out.reserve(text.length() + 16);

	for (const wxUniChar ch : text) {
		switch (ch.GetValue()) {
		case '&':  out += wxT("&amp;");  break;
		case '<':  out += wxT("&lt;");   break;
		case '>':  out += wxT("&gt;");   break;
		case '"':  out += wxT("&quot;"); break;
		case '\'': out += wxT("&apos;"); break;
		default:
			if (ch.GetValue() >= 0x20 || ch == '\t' || ch == '\n' || ch == '\r')
				out += ch;
			break;
		}
	}
	return out;
}

wxString HexOf(const wxColour& colour)
{
	return wxString::Format(wxT("%02X%02X%02X"), colour.Red(), colour.Green(), colour.Blue());
}

void AddPart(wxZipOutputStream& zip, const wxString& name, const wxString& body)
{
	zip.PutNextEntry(name);
	const wxScopedCharBuffer utf8 = body.ToUTF8();
	zip.Write(utf8.data(), utf8.length());
}

wxString ContentTypes()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">")
		wxT("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>")
		wxT("<Default Extension=\"xml\" ContentType=\"application/xml\"/>")
		wxT("<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>")
		wxT("<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>")
		wxT("<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>")
		wxT("</Types>");
}

// ⭐⭐ THE DOCUMENT DECLARES WHICH WORD IT IS FOR, and until it did Word opened it in COMPATIBILITY
// MODE — which is not a label on the title bar but a different engine underneath: the pre-2013
// table layout, and on a table of tens of thousands of rows that is the difference between a
// document and a hang (Max, 2026-08-30: the title bar read "compatibility mode" and then "not
// responding", with one core pegged).
//
// A part of four lines, and it is the whole statement: we write today's WordprocessingML.
wxString Settings()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
		wxT("<w:compat>")
		wxT("<w:compatSetting w:name=\"compatibilityMode\"")
		wxT(" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/>")
		wxT("</w:compat>")
		wxT("</w:settings>");
}

// ⭐⭐ AND WHAT A PARAGRAPH LOOKS LIKE WHEN NOBODY SAYS. With no styles part, Word applies its own
// defaults to every cell: 8pt of space AFTER each paragraph and 1.08 line spacing. A sheet's row is
// a line of text and nothing else, so every row came out half again as tall as it is here — which
// is both wrong to look at and hundreds of extra pages for Word to lay out.
//
// The sheet's own default size is declared here too (half-points), so the runs below need only
// speak when a cell DIFFERS from it — the common case then costs nothing to say.
wxString Styles()
{
	return wxString::Format(
		wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
		wxT("<w:docDefaults>")
		wxT("<w:rPrDefault><w:rPr><w:sz w:val=\"%d\"/></w:rPr></w:rPrDefault>")
		wxT("<w:pPrDefault><w:pPr>")
		wxT("<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>")
		wxT("</w:pPr></w:pPrDefault>")
		wxT("</w:docDefaults>")
		wxT("</w:styles>"),
		s_defaultSpreadsheetFont.GetPointSize() * 2);
}

wxString RootRels()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
		wxT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>")
		wxT("</Relationships>");
}

wxString DocumentRels()
{
	// The two parts the document leans on. It was empty here — required even then, because a
	// document part with no relationships part is reported as corrupt rather than as a document
	// with nothing linked — and the parts it now names are what keep Word out of compatibility mode
	// and off its own paragraph defaults.
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
		wxT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>")
		wxT("<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>")
		wxT("</Relationships>");
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
wxString BordersOf(const ibSpreadsheetCellDescription* cell)
{
	if (cell == nullptr)
		return wxEmptyString;

	static const int           s_ours[4]  = { 2, 0, 3, 1 };   // top, left, bottom, right
	static const wxChar* const s_names[4] = { wxT("top"), wxT("left"), wxT("bottom"), wxT("right") };

	wxString edges;
	for (int i = 0; i < 4; i++) {
		const ibSpreadsheetBorderDescription& border = cell->m_borderAt[s_ours[i]];
		if (border.m_style == wxPENSTYLE_TRANSPARENT)
			continue;

		const wxChar* line = wxT("single");
		switch (border.m_style) {
		case wxPENSTYLE_DOT:        line = wxT("dotted");  break;
		case wxPENSTYLE_SHORT_DASH:
		case wxPENSTYLE_LONG_DASH:  line = wxT("dashed");  break;
		case wxPENSTYLE_DOT_DASH:   line = wxT("dotDash"); break;
		default: break;
		}

		edges += wxString::Format(wxT("<w:%s w:val=\"%s\" w:sz=\"%d\" w:space=\"0\" w:color=\"%s\"/>"),
			s_names[i], line, wxMax(border.m_width, 1) * 4, HexOf(border.m_colour));
	}

	return edges.IsEmpty() ? wxString() : wxT("<w:tcBorders>") + edges + wxT("</w:tcBorders>");
}

// One cell's text, with the font and colours it had here.
wxString RunOf(const ibSpreadsheetCellDescription* cell, const wxString& text)
{
	// ⚠ IN THE SCHEMA'S ORDER, which for a run is `rFonts → b → i → strike → color → sz → u`.
	// `w:rPr` is a SEQUENCE and not a bag: a validator reads a member out of place as the element
	// simply not being there, and the ones after it as unexpected.
	wxString properties;

	if (cell != nullptr) {
		const wxFont& font = cell->m_font;
		if (font.IsOk()) {
			if (!font.GetFaceName().IsEmpty())
				properties += wxString::Format(
					wxT("<w:rFonts w:ascii=\"%s\" w:hAnsi=\"%s\"/>"),
					XmlText(font.GetFaceName()), XmlText(font.GetFaceName()));
			if (font.GetWeight() >= wxFONTWEIGHT_BOLD)
				properties += wxT("<w:b/>");
			if (font.GetStyle() == wxFONTSTYLE_ITALIC)
				properties += wxT("<w:i/>");
			if (font.GetStrikethrough())
				properties += wxT("<w:strike/>");
		}

		if (cell->m_textColour.IsOk())
			properties += wxString::Format(wxT("<w:color w:val=\"%s\"/>"), HexOf(cell->m_textColour));

		if (font.IsOk()) {
			// WORD COUNTS IN HALF-POINTS — and says nothing where the cell agrees with the size the
			// styles part already declared for the whole document. On a sheet where every cell is
			// the default size that is one element saved per cell, out of a hundred thousand.
			if (font.GetPointSize() > 0 && font.GetPointSize() != s_defaultSpreadsheetFont.GetPointSize())
				properties += wxString::Format(wxT("<w:sz w:val=\"%d\"/>"), font.GetPointSize() * 2);
			if (font.GetUnderlined())
				properties += wxT("<w:u w:val=\"single\"/>");
		}
	}

	wxString run = wxT("<w:r>");
	if (!properties.IsEmpty())
		run += wxT("<w:rPr>") + properties + wxT("</w:rPr>");
	run += wxString::Format(wxT("<w:t xml:space=\"preserve\">%s</w:t>"), XmlText(text));
	run += wxT("</w:r>");
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
wxString TableStart()
{
	return wxT("<w:tbl><w:tblPr>")
		wxT("<w:tblW w:w=\"0\" w:type=\"auto\"/>")
		wxT("<w:tblLayout w:type=\"fixed\"/>")
		wxT("<w:tblCellMar>")
		wxT("<w:left w:w=\"28\" w:type=\"dxa\"/><w:right w:w=\"28\" w:type=\"dxa\"/>")
		wxT("</w:tblCellMar>")
		wxT("</w:tblPr>");
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
wxString TableSeparator()
{
	return wxT("<w:p><w:pPr>")
		wxT("<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"1\" w:lineRule=\"exact\"/>")
		wxT("<w:rPr><w:vanish/><w:sz w:val=\"2\"/></w:rPr>")
		wxT("</w:pPr></w:p>");
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

bool ibSheetFormatDocx::Read(const wxString& WXUNUSED(fileName), ibSpreadsheetDescription& WXUNUSED(sheet)) const
{
	// See the header: a Word file is a flow, not a sheet. Refused plainly rather
	// than half-guessed.
	return false;
}

bool ibSheetFormatDocx::Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const
{
	const int numRows = sheet.GetNumberRows();
	const int numCols = sheet.GetNumberCols();

	// The rows a page break falls on — looked up per row while writing.
	std::set<int> pageBreaks;
	for (int i = 0; i < sheet.GetBrakeNumberRows(); i++)
		pageBreaks.insert(sheet.GetRowBrakeByIdx(i));

	wxString body = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
		wxT("<w:body>");

	// ⚠ THE GRID IS DECLARED FIRST and Word believes it over the cells: column
	// widths live here, in twentieths of a point, and our pixels are 96 to the inch
	// (a pixel is 15 twips).
	// ⚠ AND EVERY TABLE CARRIES IT, not only the first. A page break closes the table and opens
	// another, and the one after the break was opened without a grid at all — which is why its
	// columns came out re-measured while the first page's held.
	wxString grid = wxT("<w:tblGrid>");
	for (int col = 0; col <= numCols; col++)
		grid += wxString::Format(wxT("<w:gridCol w:w=\"%d\"/>"), sheet.GetColSize(col) * 15);
	grid += wxT("</w:tblGrid>");

	wxString table = TableStart() + grid;
	int rowsInTable = 0;

	for (int row = 0; row <= numRows; row++) {

		wxString cells;
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
				continuedCols = wxMax(ownerCols, 1);
			}

			// ⚠ THE WIDTH IS THE GRID'S TO STATE, and it states it once at the top of the table.
			// Repeated on every cell it is 156 000 copies of what `<w:tblGrid>` already said — and
			// under `tblLayout fixed` the grid is what Word reads anyway. A cell says a width only
			// where the grid cannot: when it spans several of its columns.
			//
			// …and on the FIRST row, which some readers of a fixed-layout table measure from
			// instead of the grid. Six elements once, rather than six on every row.
			wxString properties;

			const int spanned = (span == 1) ? spanCols : (span == -1 ? continuedCols : 1);

			if (row == 0 || spanned > 1) {
				int width = 0;
				for (int i = 0; i < spanned; i++)
					width += sheet.GetColSize(col + i);
				properties += wxString::Format(wxT("<w:tcW w:w=\"%d\" w:type=\"dxa\"/>"), width * 15);
			}

			if (spanned > 1)
				properties += wxString::Format(wxT("<w:gridSpan w:val=\"%d\"/>"), spanned);

			if (span == 1 && spanRows > 1)
				properties += wxT("<w:vMerge w:val=\"restart\"/>");
			else if (span == -1)
				properties += wxT("<w:vMerge/>");

			properties += BordersOf(cell);

			if (cell != nullptr && cell->m_backgroundColour.IsOk())
				properties += wxString::Format(
					wxT("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%s\"/>"),
					HexOf(cell->m_backgroundColour));

			// ⚠ AND `w:tcPr` IS A SEQUENCE TOO: `tcW → gridSpan → vMerge → tcBorders → shd →
			// textDirection → vAlign`. The direction stands BEFORE the vertical alignment.
			if (cell != nullptr) {
				// TEXT ORIENTATION — a direction here, a text FLOW there. Bottom-to-top is the one
				// a sheet means by «vertical», the same rotation the workbook writes as 90°.
				if (cell->m_textOrient == wxVERTICAL)
					properties += wxT("<w:textDirection w:val=\"btLr\"/>");

				switch (cell->m_alignVert) {
				case ibAlignmentVert_Center: properties += wxT("<w:vAlign w:val=\"center\"/>"); break;
				case ibAlignmentVert_Bottom: properties += wxT("<w:vAlign w:val=\"bottom\"/>"); break;
				default: break;
				}
			}

			wxString paragraphProperties;
			if (cell != nullptr) {
				switch (cell->m_alignHorz) {
				case ibAlignmentHorz_Center: paragraphProperties = wxT("<w:jc w:val=\"center\"/>"); break;
				case ibAlignmentHorz_Right:  paragraphProperties = wxT("<w:jc w:val=\"right\"/>");  break;
				default: break;
				}
			}

			const wxString text = cell != nullptr ? cell->GetValue() : wxString();

			wxString paragraph = wxT("<w:p>");
			if (!paragraphProperties.IsEmpty())
				paragraph += wxT("<w:pPr>") + paragraphProperties + wxT("</w:pPr>");
			if (!text.IsEmpty())
				paragraph += RunOf(cell, text);
			paragraph += wxT("</w:p>");

			// ⚠ A CELL ALWAYS CARRIES A PARAGRAPH, even an empty one: Word reports a
			// table cell without one as a damaged file. Its PROPERTIES are another matter — an
			// empty `<w:tcPr></w:tcPr>` on every cell of a large sheet is a megabyte of nothing.
			cells += wxT("<w:tc>");
			if (!properties.IsEmpty())
				cells += wxT("<w:tcPr>") + properties + wxT("</w:tcPr>");
			cells += paragraph + wxT("</w:tc>");

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
		wxString rowProperties;
		if (height != s_defaultRowHeight && height > 0)
			rowProperties = wxString::Format(
				wxT("<w:trPr><w:trHeight w:hRule=\"atLeast\" w:val=\"%d\"/></w:trPr>"), height * 15);

		if (height > 0) {
			table += wxT("<w:tr>") + rowProperties + cells + wxT("</w:tr>");
			rowsInTable++;
		}

		// ⭐ WHERE A PAGE ENDED HERE, A PAGE ENDS THERE. A break inside a table has to
		// close it and open the next one — Word has no "break between these rows".
		if (pageBreaks.find(row) != pageBreaks.end() && row < numRows) {
			table += wxT("</w:tbl>");
			body += table;
			body += wxT("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
			table = TableStart() + grid;
			rowsInTable = 0;
		}
		// …and where nothing ended, the table is closed anyway once it has grown long enough. The
		// reader sees the same rows in the same places; Word gets a hundred small problems instead
		// of one large one.
		else if (rowsInTable >= s_rowsPerTable && row < numRows
			&& !ContinuesMerge(sheet, row + 1, numCols)) {
			table += wxT("</w:tbl>");
			body += table + TableSeparator();
			table = TableStart() + grid;
			rowsInTable = 0;
		}
	}

	// ⚠ AND ONLY IF IT HOLDS ANYTHING. A split that fell on the very last row leaves a table opened
	// and never filled, and a `<w:tbl>` with no rows in it is a table Word draws as a blank stub.
	if (rowsInTable > 0) {
		table += wxT("</w:tbl>");
		body += table;
	}

	// A trailing paragraph — a body that ends with a table leaves Word with nowhere
	// to put the cursor after it.
	body += wxT("<w:p/>");
	body += wxT("</w:body></w:document>");

	wxFileOutputStream file(fileName);
	if (!file.IsOk())
		return false;

	wxZipOutputStream zip(file);
	if (!zip.IsOk())
		return false;

	AddPart(zip, wxT("[Content_Types].xml"),          ContentTypes());
	AddPart(zip, wxT("_rels/.rels"),                  RootRels());
	AddPart(zip, wxT("word/_rels/document.xml.rels"), DocumentRels());
	AddPart(zip, wxT("word/styles.xml"),              Styles());
	AddPart(zip, wxT("word/settings.xml"),            Settings());
	AddPart(zip, wxT("word/document.xml"),            body);

	// The central directory is written on Close — a zip that is merely destructed
	// is a file that exists and is not a zip.
	return zip.Close() && file.Close();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatDocx);
