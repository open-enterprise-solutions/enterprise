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
		wxT("</Types>");
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
	// EMPTY AND STILL REQUIRED — a document part with no relationships part is
	// reported as corrupt, not as a document with nothing linked.
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"/>");
}

// One cell's text, with the font and colours it had here.
wxString RunOf(const ibSpreadsheetCellDescription* cell, const wxString& text)
{
	wxString properties;

	if (cell != nullptr) {
		const wxFont& font = cell->m_font;
		if (font.IsOk()) {
			if (font.GetWeight() >= wxFONTWEIGHT_BOLD)
				properties += wxT("<w:b/>");
			if (font.GetStyle() == wxFONTSTYLE_ITALIC)
				properties += wxT("<w:i/>");
			if (!font.GetFaceName().IsEmpty())
				properties += wxString::Format(
					wxT("<w:rFonts w:ascii=\"%s\" w:hAnsi=\"%s\"/>"),
					XmlText(font.GetFaceName()), XmlText(font.GetFaceName()));
			// WORD COUNTS IN HALF-POINTS.
			if (font.GetPointSize() > 0)
				properties += wxString::Format(wxT("<w:sz w:val=\"%d\"/>"), font.GetPointSize() * 2);
		}

		if (cell->m_textColour.IsOk())
			properties += wxString::Format(wxT("<w:color w:val=\"%s\"/>"), HexOf(cell->m_textColour));
	}

	wxString run = wxT("<w:r>");
	if (!properties.IsEmpty())
		run += wxT("<w:rPr>") + properties + wxT("</w:rPr>");
	run += wxString::Format(wxT("<w:t xml:space=\"preserve\">%s</w:t>"), XmlText(text));
	run += wxT("</w:r>");
	return run;
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

	wxString table = wxT("<w:tbl>");

	// ⚠ THE GRID IS DECLARED FIRST and Word believes it over the cells: column
	// widths live here, in twentieths of a point, and our pixels are 96 to the inch
	// (a pixel is 15 twips).
	table += wxT("<w:tblPr><w:tblBorders>")
		wxT("<w:top w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
		wxT("<w:left w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
		wxT("<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
		wxT("<w:right w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
		wxT("<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
		wxT("<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
		wxT("</w:tblBorders></w:tblPr>");

	table += wxT("<w:tblGrid>");
	for (int col = 0; col <= numCols; col++)
		table += wxString::Format(wxT("<w:gridCol w:w=\"%d\"/>"), sheet.GetColSize(col) * 15);
	table += wxT("</w:tblGrid>");

	for (int row = 0; row <= numRows; row++) {

		wxString cells;
		for (int col = 0; col <= numCols; col++) {

			const ibSpreadsheetCellDescription* cell = sheet.GetCell(row, col);

			int spanRows = 1, spanCols = 1;
			const int span = cell != nullptr ? cell->GetSize(&spanRows, &spanCols) : 0;

			wxString properties = wxString::Format(wxT("<w:tcW w:w=\"%d\" w:type=\"dxa\"/>"),
				sheet.GetColSize(col) * 15);

			// ⭐ A MERGE IS SAID TWICE IN WORD, once per direction: sideways as a span
			// on the owning cell, downwards as "this cell continues the one above" on
			// every cell it covers. A covered cell therefore still has to be WRITTEN —
			// unlike in a workbook, where it is simply absent.
			if (span == 1 && spanCols > 1)
				properties += wxString::Format(wxT("<w:gridSpan w:val=\"%d\"/>"), spanCols);

			if (span == 1 && spanRows > 1)
				properties += wxT("<w:vMerge w:val=\"restart\"/>");
			else if (span == -1)
				properties += wxT("<w:vMerge/>");

			if (cell != nullptr && cell->m_backgroundColour.IsOk())
				properties += wxString::Format(
					wxT("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%s\"/>"),
					HexOf(cell->m_backgroundColour));

			if (cell != nullptr) {
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
			// table cell without one as a damaged file.
			cells += wxT("<w:tc><w:tcPr>") + properties + wxT("</w:tcPr>") + paragraph + wxT("</w:tc>");

			// A cell covered SIDEWAYS was already spoken for by the gridSpan above.
			if (span == 1 && spanCols > 1)
				col += spanCols - 1;
		}

		if (cells.IsEmpty())
			continue;

		wxString rowProperties;
		const int height = sheet.GetRowSize(row);
		if (height > 0 && height != s_defaultRowHeight)
			rowProperties = wxString::Format(wxT("<w:trPr><w:trHeight w:val=\"%d\"/></w:trPr>"), height * 15);

		table += wxT("<w:tr>") + rowProperties + cells + wxT("</w:tr>");

		// ⭐ WHERE A PAGE ENDED HERE, A PAGE ENDS THERE. A break inside a table has to
		// close it and open the next one — Word has no "break between these rows".
		if (pageBreaks.find(row) != pageBreaks.end() && row < numRows) {
			table += wxT("</w:tbl>");
			body += table;
			body += wxT("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
			table = wxT("<w:tbl><w:tblPr><w:tblBorders>")
				wxT("<w:top w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
				wxT("<w:left w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
				wxT("<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
				wxT("<w:right w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
				wxT("<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
				wxT("<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>")
				wxT("</w:tblBorders></w:tblPr>");
		}
	}

	table += wxT("</w:tbl>");
	body += table;

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
	AddPart(zip, wxT("word/document.xml"),            body);

	// The central directory is written on Close — a zip that is merely destructed
	// is a file that exists and is not a zip.
	return zip.Close() && file.Close();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatDocx);
