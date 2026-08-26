////////////////////////////////////////////////////////////////////////////
//	Description : our document, written out as an Excel workbook
////////////////////////////////////////////////////////////////////////////

// ⭐ AN .xlsx IS A ZIP OF XML PARTS, and this file writes the six a workbook
// cannot do without:
//
//   [Content_Types].xml          what each part is
//   _rels/.rels                  where the workbook is
//   xl/workbook.xml              the one sheet, named
//   xl/_rels/workbook.xml.rels   where the sheet and the styles are
//   xl/styles.xml                the fonts, fills, borders and alignments used
//   xl/worksheets/sheet1.xml     the cells themselves
//
// Text goes inline (`t="inlineStr"`), so there is no shared-string table to keep
// in step — a table that buys nothing until a document repeats the same string
// thousands of times, and costs a second index that can disagree with the cells.

#include "backend/sheetFormat/sheetFormatXlsx.h"

#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/sstream.h>

#include <map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// XML text — the four characters that would otherwise end the element early.
// ⚠ AND THE CONTROL CHARACTERS. A cell can hold anything a person typed; XML 1.0
// simply has no way to express most codes below 0x20, and Excel rejects the file
// rather than the cell. They are dropped, not escaped.
// ---------------------------------------------------------------------------
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

// A1 / B12 / AA3 — the column letters Excel addresses cells by, zero-based in, one-based out.
wxString CellRef(int row, int col)
{
	wxString letters;
	for (int rest = col; ; rest = rest / 26 - 1) {
		letters.Prepend(static_cast<wxChar>(wxT('A') + rest % 26));
		if (rest < 26)
			break;
	}
	return letters + wxString::Format(wxT("%d"), row + 1);
}

// #RRGGBB -> the ARGB hex Excel wants. Alpha is always FF: our colours have no
// transparency, and a zero alpha there reads as an invisible fill.
wxString ArgbOf(const wxColour& colour)
{
	return wxString::Format(wxT("FF%02X%02X%02X"), colour.Red(), colour.Green(), colour.Blue());
}

// ⚠ IS THIS TEXT A NUMBER — asked so a number reaches Excel AS a number, which is
// what makes it summable there. Asked in the C locale (ToCDouble) because that is
// how the sheet stores it, and answered "no" for anything with a stray character,
// so "1 234" or "12%" stay text rather than becoming a silently different value.
bool NumberOf(const wxString& text, double& value)
{
	return !text.IsEmpty() && text.ToCDouble(&value);
}

// The style of one cell, as the small set of things Excel keeps per cell. Cells
// that look alike share one entry — a sheet has thousands of cells and a handful
// of looks.
struct CellStyle {
	wxString m_fontFace;
	int      m_fontSize = 8;
	bool     m_bold = false;
	bool     m_italic = false;
	wxString m_textColour;    // ARGB, empty = automatic
	wxString m_fillColour;    // ARGB, empty = none
	wxString m_alignHorz;     // "", "center", "right"
	wxString m_alignVert;     // "", "center", "bottom"
	bool     m_borders[4] = { false, false, false, false };   // left, right, top, bottom
	wxString m_borderColour;

	bool operator<(const CellStyle& o) const
	{
		if (m_fontFace != o.m_fontFace)         return m_fontFace < o.m_fontFace;
		if (m_fontSize != o.m_fontSize)         return m_fontSize < o.m_fontSize;
		if (m_bold != o.m_bold)                 return m_bold < o.m_bold;
		if (m_italic != o.m_italic)             return m_italic < o.m_italic;
		if (m_textColour != o.m_textColour)     return m_textColour < o.m_textColour;
		if (m_fillColour != o.m_fillColour)     return m_fillColour < o.m_fillColour;
		if (m_alignHorz != o.m_alignHorz)       return m_alignHorz < o.m_alignHorz;
		if (m_alignVert != o.m_alignVert)       return m_alignVert < o.m_alignVert;
		if (m_borderColour != o.m_borderColour) return m_borderColour < o.m_borderColour;
		for (int i = 0; i < 4; i++)
			if (m_borders[i] != o.m_borders[i]) return m_borders[i] < o.m_borders[i];
		return false;
	}
};

CellStyle StyleOf(const ibSpreadsheetCellDescription& cell)
{
	CellStyle style;

	const wxFont& font = cell.m_font;
	if (font.IsOk()) {
		style.m_fontFace = font.GetFaceName();
		style.m_fontSize = font.GetPointSize();
		style.m_bold     = font.GetWeight() >= wxFONTWEIGHT_BOLD;
		style.m_italic   = font.GetStyle() == wxFONTSTYLE_ITALIC;
	}

	if (cell.m_textColour.IsOk())
		style.m_textColour = ArgbOf(cell.m_textColour);
	if (cell.m_backgroundColour.IsOk())
		style.m_fillColour = ArgbOf(cell.m_backgroundColour);

	switch (cell.m_alignHorz) {
	case ibAlignmentHorz_Center: style.m_alignHorz = wxT("center"); break;
	case ibAlignmentHorz_Right:  style.m_alignHorz = wxT("right");  break;
	default: break;
	}

	switch (cell.m_alignVert) {
	case ibAlignmentVert_Center: style.m_alignVert = wxT("center"); break;
	case ibAlignmentVert_Bottom: style.m_alignVert = wxT("bottom"); break;
	default: break;
	}

	// FOUR EDGES, one flag each: Excel's border styles are its own vocabulary and a
	// thin line is what every one of ours renders as there. The COLOUR is carried,
	// because that is what a person notices.
	for (int i = 0; i < 4; i++) {
		const ibSpreadsheetBorderDescription& border = cell.m_borderAt[i];
		style.m_borders[i] = border.m_style != wxPENSTYLE_TRANSPARENT;
		if (style.m_borders[i] && style.m_borderColour.IsEmpty())
			style.m_borderColour = ArgbOf(border.m_colour);
	}

	return style;
}

// --- the parts ------------------------------------------------------------

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
		wxT("<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>")
		wxT("<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
		wxT("<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>")
		wxT("</Types>");
}

wxString RootRels()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
		wxT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>")
		wxT("</Relationships>");
}

wxString Workbook()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"")
		wxT(" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">")
		wxT("<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>")
		wxT("</workbook>");
}

wxString WorkbookRels()
{
	return wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
		wxT("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>")
		wxT("<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>")
		wxT("</Relationships>");
}

// ⚠ INDEX ZERO OF EVERY LIST IS RESERVED. Excel requires a default font, two
// default fills (none and gray125 — the second is never used and must still be
// there) and an empty border; a file whose indices start at the real entries
// opens as damaged.
wxString Styles(const std::vector<CellStyle>& styles)
{
	wxString xml = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");

	// fonts
	xml += wxString::Format(wxT("<fonts count=\"%u\">"), static_cast<unsigned>(styles.size() + 1));
	xml += wxT("<font><sz val=\"11\"/><name val=\"Calibri\"/></font>");
	for (const CellStyle& style : styles) {
		xml += wxT("<font>");
		xml += wxString::Format(wxT("<sz val=\"%d\"/>"), style.m_fontSize > 0 ? style.m_fontSize : 11);
		if (style.m_bold)   xml += wxT("<b/>");
		if (style.m_italic) xml += wxT("<i/>");
		if (!style.m_textColour.IsEmpty())
			xml += wxString::Format(wxT("<color rgb=\"%s\"/>"), style.m_textColour);
		xml += wxString::Format(wxT("<name val=\"%s\"/>"),
			XmlText(style.m_fontFace.IsEmpty() ? wxT("Calibri") : style.m_fontFace));
		xml += wxT("</font>");
	}
	xml += wxT("</fonts>");

	// fills
	xml += wxString::Format(wxT("<fills count=\"%u\">"), static_cast<unsigned>(styles.size() + 2));
	xml += wxT("<fill><patternFill patternType=\"none\"/></fill>");
	xml += wxT("<fill><patternFill patternType=\"gray125\"/></fill>");
	for (const CellStyle& style : styles) {
		if (style.m_fillColour.IsEmpty())
			xml += wxT("<fill><patternFill patternType=\"none\"/></fill>");
		else
			xml += wxString::Format(
				wxT("<fill><patternFill patternType=\"solid\"><fgColor rgb=\"%s\"/><bgColor indexed=\"64\"/></patternFill></fill>"),
				style.m_fillColour);
	}
	xml += wxT("</fills>");

	// borders
	xml += wxString::Format(wxT("<borders count=\"%u\">"), static_cast<unsigned>(styles.size() + 1));
	xml += wxT("<border><left/><right/><top/><bottom/><diagonal/></border>");
	for (const CellStyle& style : styles) {
		const wxString colour = style.m_borderColour.IsEmpty() ? wxT("FF000000") : style.m_borderColour;
		const wxChar* const names[4] = { wxT("left"), wxT("right"), wxT("top"), wxT("bottom") };
		xml += wxT("<border>");
		for (int i = 0; i < 4; i++) {
			if (style.m_borders[i])
				xml += wxString::Format(wxT("<%s style=\"thin\"><color rgb=\"%s\"/></%s>"),
					names[i], colour, names[i]);
			else
				xml += wxString::Format(wxT("<%s/>"), names[i]);
		}
		xml += wxT("<diagonal/></border>");
	}
	xml += wxT("</borders>");

	xml += wxT("<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>");

	xml += wxString::Format(wxT("<cellXfs count=\"%u\">"), static_cast<unsigned>(styles.size() + 1));
	xml += wxT("<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>");
	for (size_t i = 0; i < styles.size(); i++) {
		const CellStyle& style = styles[i];
		const unsigned at = static_cast<unsigned>(i + 1);
		xml += wxString::Format(
			wxT("<xf numFmtId=\"0\" fontId=\"%u\" fillId=\"%u\" borderId=\"%u\" xfId=\"0\"")
			wxT(" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\""),
			at, at + 1, at);

		if (!style.m_alignHorz.IsEmpty() || !style.m_alignVert.IsEmpty()) {
			xml += wxT(" applyAlignment=\"1\"><alignment");
			if (!style.m_alignHorz.IsEmpty())
				xml += wxString::Format(wxT(" horizontal=\"%s\""), style.m_alignHorz);
			if (!style.m_alignVert.IsEmpty())
				xml += wxString::Format(wxT(" vertical=\"%s\""), style.m_alignVert);
			xml += wxT("/></xf>");
		}
		else {
			xml += wxT("/>");
		}
	}
	xml += wxT("</cellXfs>");

	xml += wxT("</styleSheet>");
	return xml;
}

} // namespace

bool ibSheetFormatXlsx::Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const
{
	// --- the styles the cells actually use, gathered before anything is written --
	std::map<CellStyle, size_t> styleAt;
	std::vector<CellStyle>      styles;

	const int cellCount = sheet.GetCellCount();
	for (int i = 0; i < cellCount; i++) {
		const ibSpreadsheetCellDescription* cell = sheet.GetCellByIdx(i);
		if (cell == nullptr)
			continue;

		const CellStyle style = StyleOf(*cell);
		if (styleAt.find(style) == styleAt.end()) {
			styleAt[style] = styles.size();
			styles.push_back(style);
		}
	}

	// --- the sheet itself --------------------------------------------------------
	wxString body = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
		wxT("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");

	// COLUMN WIDTHS — Excel counts them in characters, we in pixels; the ratio it
	// uses for the default font is 7 pixels to the character.
	const int numCols = sheet.GetNumberCols();
	const int numRows = sheet.GetNumberRows();

	if (sheet.GetSizeNumberCols() > 0) {
		body += wxT("<cols>");
		for (int col = 0; col <= numCols; col++) {
			const int width = sheet.GetColSize(col);
			if (width == s_defaultColWidth)
				continue;
			body += wxString::Format(
				wxT("<col min=\"%d\" max=\"%d\" width=\"%.2f\" customWidth=\"1\"%s/>"),
				col + 1, col + 1, width / 7.0,
				width == 0 ? wxT(" hidden=\"1\"") : wxT(""));
		}
		body += wxT("</cols>");
	}

	// ⚠ FROZEN PANES GO BEFORE THE DATA — sheetView is ordered by the schema, and a
	// file that puts it after <sheetData> does not open.
	if (sheet.GetRowFreeze() > 0 || sheet.GetColFreeze() > 0) {
		const wxString topLeft = CellRef(sheet.GetRowFreeze(), sheet.GetColFreeze());
		wxString pane = wxT("<pane");
		if (sheet.GetColFreeze() > 0) pane += wxString::Format(wxT(" xSplit=\"%d\""), sheet.GetColFreeze());
		if (sheet.GetRowFreeze() > 0) pane += wxString::Format(wxT(" ySplit=\"%d\""), sheet.GetRowFreeze());
		pane += wxString::Format(wxT(" topLeftCell=\"%s\" activePane=\"bottomRight\" state=\"frozen\"/>"), topLeft);

		body = body + wxT("<sheetViews><sheetView workbookViewId=\"0\">") + pane + wxT("</sheetView></sheetViews>");
	}

	body += wxT("<sheetData>");

	for (int row = 0; row <= numRows; row++) {
		wxString cells;

		for (int col = 0; col <= numCols; col++) {
			const ibSpreadsheetCellDescription* cell = sheet.GetCell(row, col);
			if (cell == nullptr)
				continue;

			// A CELL COVERED BY A MERGE carries no value of its own — the span is
			// written once, by its owner, in <mergeCells> below.
			int spanRows = 1, spanCols = 1;
			if (cell->GetSize(&spanRows, &spanCols) == -1)
				continue;

			const auto found = styleAt.find(StyleOf(*cell));
			const unsigned styleIndex = found != styleAt.end()
				? static_cast<unsigned>(found->second + 1) : 0u;

			const wxString value = cell->GetValue();
			if (value.IsEmpty() && styleIndex == 0)
				continue;   // nothing to say about this cell at all

			double number = 0.0;
			if (NumberOf(value, number)) {
				cells += wxString::Format(wxT("<c r=\"%s\" s=\"%u\"><v>%s</v></c>"),
					CellRef(row, col), styleIndex, value);
			}
			else if (!value.IsEmpty()) {
				cells += wxString::Format(wxT("<c r=\"%s\" s=\"%u\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%s</t></is></c>"),
					CellRef(row, col), styleIndex, XmlText(value));
			}
			else {
				cells += wxString::Format(wxT("<c r=\"%s\" s=\"%u\"/>"), CellRef(row, col), styleIndex);
			}
		}

		if (cells.IsEmpty())
			continue;

		const int height = sheet.GetRowSize(row);
		wxString rowTag = wxString::Format(wxT("<row r=\"%d\""), row + 1);
		if (height != s_defaultRowHeight) {
			// Excel keeps row height in POINTS, the screen in pixels: 72 to 96.
			rowTag += wxString::Format(wxT(" ht=\"%.2f\" customHeight=\"1\""), height * 0.75);
			if (height == 0)
				rowTag += wxT(" hidden=\"1\"");
		}
		rowTag += wxT(">");

		body += rowTag + cells + wxT("</row>");
	}

	body += wxT("</sheetData>");

	// --- merged cells ------------------------------------------------------------
	wxString merges;
	unsigned mergeCount = 0;
	for (int i = 0; i < cellCount; i++) {
		const ibSpreadsheetCellDescription* cell = sheet.GetCellByIdx(i);
		if (cell == nullptr)
			continue;

		int spanRows = 1, spanCols = 1;
		if (cell->GetSize(&spanRows, &spanCols) != 1)
			continue;   // not the owner of a span

		merges += wxString::Format(wxT("<mergeCell ref=\"%s:%s\"/>"),
			CellRef(cell->m_row, cell->m_col),
			CellRef(cell->m_row + spanRows - 1, cell->m_col + spanCols - 1));
		mergeCount++;
	}
	if (mergeCount > 0)
		body += wxString::Format(wxT("<mergeCells count=\"%u\">"), mergeCount) + merges + wxT("</mergeCells>");

	// --- page breaks — the sheets a workbook would have had -----------------------
	const int rowBreaks = sheet.GetBrakeNumberRows();
	if (rowBreaks > 0) {
		body += wxString::Format(wxT("<rowBreaks count=\"%d\" manualBreakCount=\"%d\">"), rowBreaks, rowBreaks);
		for (int i = 0; i < rowBreaks; i++)
			body += wxString::Format(wxT("<brk id=\"%d\" max=\"16383\" man=\"1\"/>"), sheet.GetRowBrakeByIdx(i) + 1);
		body += wxT("</rowBreaks>");
	}

	const int colBreaks = sheet.GetBrakeNumberCols();
	if (colBreaks > 0) {
		body += wxString::Format(wxT("<colBreaks count=\"%d\" manualBreakCount=\"%d\">"), colBreaks, colBreaks);
		for (int i = 0; i < colBreaks; i++)
			body += wxString::Format(wxT("<brk id=\"%d\" max=\"1048575\" man=\"1\"/>"), sheet.GetColBrakeByIdx(i) + 1);
		body += wxT("</colBreaks>");
	}

	body += wxT("</worksheet>");

	// --- write the package -------------------------------------------------------
	wxFileOutputStream file(fileName);
	if (!file.IsOk())
		return false;

	wxZipOutputStream zip(file);
	if (!zip.IsOk())
		return false;

	AddPart(zip, wxT("[Content_Types].xml"),        ContentTypes());
	AddPart(zip, wxT("_rels/.rels"),                RootRels());
	AddPart(zip, wxT("xl/workbook.xml"),            Workbook());
	AddPart(zip, wxT("xl/_rels/workbook.xml.rels"), WorkbookRels());
	AddPart(zip, wxT("xl/styles.xml"),              Styles(styles));
	AddPart(zip, wxT("xl/worksheets/sheet1.xml"),   body);

	// ⚠ CLOSED EXPLICITLY, and its answer read: a zip finishes with a central
	// directory written on Close, so a stream that is merely destructed can leave a
	// file that exists, has size, and is not a zip.
	return zip.Close() && file.Close();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatXlsx);
