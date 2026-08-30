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

#include <algorithm>
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
	if (text.IsEmpty())
		return false;

	// ⭐⭐ A LEADING ZERO MEANS IT IS NOT A NUMBER. `00000000001` parses perfectly and is not one: the
	// zeros are part of the VALUE, and written as a number it reaches Excel as `1`, right-aligned, with
	// the document it names no longer findable by its number (Max, 2026-08-30, from the opened sheet).
	//
	// Every identifier a business system prints looks like this — document numbers, codes, article
	// numbers, account numbers — so the rule is about a whole CLASS of columns, not about one column.
	// A single `0` is still zero; a leading `+`/`-` is stepped over first.
	size_t at = 0;
	if (text[at] == wxT('+') || text[at] == wxT('-'))
		at++;
	if (at + 1 < text.length() && text[at] == wxT('0') && text[at + 1] != wxT('.') && text[at + 1] != wxT(','))
		return false;

	return text.ToCDouble(&value);
}

// ⭐ EXCEL NAMES A LINE, WE DESCRIBE ONE — a pen style and a width against a closed
// vocabulary of nine names. Both halves are read, because "dotted" and "thick" are
// different answers to different questions and a cell can say both.
wxString BorderStyleOf(const ibSpreadsheetBorderDescription& border)
{
	const bool heavy = border.m_width >= 3;
	const bool bold  = border.m_width == 2;

	switch (border.m_style) {
	case wxPENSTYLE_TRANSPARENT: return wxEmptyString;
	case wxPENSTYLE_DOT:         return bold || heavy ? wxT("mediumDashDotDot") : wxT("dotted");
	case wxPENSTYLE_SHORT_DASH:
	case wxPENSTYLE_LONG_DASH:   return bold || heavy ? wxT("mediumDashed") : wxT("dashed");
	case wxPENSTYLE_DOT_DASH:    return bold || heavy ? wxT("mediumDashDot") : wxT("dashDot");
	default: break;
	}
	return heavy ? wxT("thick") : (bold ? wxT("medium") : wxT("thin"));
}

// The style of one cell, as the small set of things Excel keeps per cell. Cells
// that look alike share one entry — a sheet has thousands of cells and a handful
// of looks.
//
// ⭐⭐ EVERY PROPERTY THE CELL INSPECTOR SHOWS TRAVELS (Max, 2026-08-30: *"these
// properties must move over"*, over the panel: alignment, orientation, font,
// background, text colour, four borders and their colour). The two it does not
// carry are the two that are not a LOOK at all: `Fill type` and `Details parameter`
// are what a TEMPLATE says about where a cell's text comes from — there is no
// template on the far side, only the text the fill produced.
struct CellStyle {
	wxString m_fontFace;
	int      m_fontSize = 8;
	bool     m_bold = false;
	bool     m_italic = false;
	bool     m_underlined = false;
	bool     m_struck = false;
	wxString m_textColour;    // ARGB, empty = automatic
	wxString m_fillColour;    // ARGB, empty = none
	wxString m_alignHorz;     // "", "center", "right"
	wxString m_alignVert;     // "", "center", "bottom"
	int      m_rotation = 0;  // degrees, 90 = the vertical orientation
	bool     m_wrapText = false;
	bool     m_locked = false;
	wxString m_borderStyle[4];   // left, right, top, bottom — empty = no edge
	wxString m_borderColour[4];

	bool operator<(const CellStyle& o) const
	{
		if (m_fontFace != o.m_fontFace)         return m_fontFace < o.m_fontFace;
		if (m_fontSize != o.m_fontSize)         return m_fontSize < o.m_fontSize;
		if (m_bold != o.m_bold)                 return m_bold < o.m_bold;
		if (m_italic != o.m_italic)             return m_italic < o.m_italic;
		if (m_underlined != o.m_underlined)     return m_underlined < o.m_underlined;
		if (m_struck != o.m_struck)             return m_struck < o.m_struck;
		if (m_textColour != o.m_textColour)     return m_textColour < o.m_textColour;
		if (m_fillColour != o.m_fillColour)     return m_fillColour < o.m_fillColour;
		if (m_alignHorz != o.m_alignHorz)       return m_alignHorz < o.m_alignHorz;
		if (m_alignVert != o.m_alignVert)       return m_alignVert < o.m_alignVert;
		if (m_rotation != o.m_rotation)         return m_rotation < o.m_rotation;
		if (m_wrapText != o.m_wrapText)         return m_wrapText < o.m_wrapText;
		if (m_locked != o.m_locked)             return m_locked < o.m_locked;
		for (int i = 0; i < 4; i++) {
			if (m_borderStyle[i] != o.m_borderStyle[i])   return m_borderStyle[i] < o.m_borderStyle[i];
			if (m_borderColour[i] != o.m_borderColour[i]) return m_borderColour[i] < o.m_borderColour[i];
		}
		return false;
	}
};

CellStyle StyleOf(const ibSpreadsheetCellDescription& cell)
{
	CellStyle style;

	const wxFont& font = cell.m_font;
	if (font.IsOk()) {
		style.m_fontFace   = font.GetFaceName();
		style.m_fontSize   = font.GetPointSize();
		style.m_bold       = font.GetWeight() >= wxFONTWEIGHT_BOLD;
		style.m_italic     = font.GetStyle() == wxFONTSTYLE_ITALIC;
		style.m_underlined = font.GetUnderlined();
		style.m_struck     = font.GetStrikethrough();
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

	// TEXT ORIENTATION — ours is a direction, Excel's is an ANGLE, and the direction we
	// have is the angle it calls 90.
	if (cell.m_textOrient == wxVERTICAL)
		style.m_rotation = 90;

	// ⚠ FIT MODE IS CARRIED AS THE NEAREST TRUE THING, and it is not the same thing.
	// `Overflow` is exactly Excel's default — text spills right while the neighbour is
	// empty. `Clip` and the three ellipsize modes all mean "the text stays inside its
	// cell", and the only way Excel says that is `wrapText`, which keeps it inside by
	// WRAPPING rather than by cutting. So the boundary survives the trip and the manner
	// of cutting does not; the alternative was to drop the property entirely.
	style.m_wrapText = cell.m_fitMode != ibSpreadsheetCellDescription::Mode_Overflow;

	// READ-ONLY is Excel's `locked`, which only bites once a sheet is protected — ours is
	// not, so this carries the fact rather than enforcing it, and a round trip keeps it.
	style.m_locked = cell.m_isReadOnly;

	// FOUR EDGES, each with its own line and its own colour — the inspector offers one
	// colour for all four, the model holds one per edge, and the wider of the two is what
	// is written.
	for (int i = 0; i < 4; i++) {
		const ibSpreadsheetBorderDescription& border = cell.m_borderAt[i];
		style.m_borderStyle[i] = BorderStyleOf(border);
		if (!style.m_borderStyle[i].IsEmpty())
			style.m_borderColour[i] = ArgbOf(border.m_colour);
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
		if (style.m_bold)       xml += wxT("<b/>");
		if (style.m_italic)     xml += wxT("<i/>");
		if (style.m_underlined) xml += wxT("<u/>");
		if (style.m_struck)     xml += wxT("<strike/>");
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
		const wxChar* const names[4] = { wxT("left"), wxT("right"), wxT("top"), wxT("bottom") };
		xml += wxT("<border>");
		for (int i = 0; i < 4; i++) {
			if (!style.m_borderStyle[i].IsEmpty())
				xml += wxString::Format(wxT("<%s style=\"%s\"><color rgb=\"%s\"/></%s>"),
					names[i], style.m_borderStyle[i],
					style.m_borderColour[i].IsEmpty() ? wxT("FF000000") : style.m_borderColour[i],
					names[i]);
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

		// ⚠ THE `apply*` FLAGS ARE NOT DECORATION — an `<alignment>` written without
		// `applyAlignment="1"` is present in the file and ignored by Excel, which is the
		// worst of the three possible outcomes: it looks written and behaves unwritten.
		const bool hasAlign = !style.m_alignHorz.IsEmpty() || !style.m_alignVert.IsEmpty()
			|| style.m_rotation != 0 || style.m_wrapText;

		if (hasAlign)      xml += wxT(" applyAlignment=\"1\"");
		if (style.m_locked) xml += wxT(" applyProtection=\"1\"");

		if (hasAlign || style.m_locked) {
			xml += wxT(">");
			if (hasAlign) {
				xml += wxT("<alignment");
				if (!style.m_alignHorz.IsEmpty())
					xml += wxString::Format(wxT(" horizontal=\"%s\""), style.m_alignHorz);
				if (!style.m_alignVert.IsEmpty())
					xml += wxString::Format(wxT(" vertical=\"%s\""), style.m_alignVert);
				if (style.m_rotation != 0)
					xml += wxString::Format(wxT(" textRotation=\"%d\""), style.m_rotation);
				if (style.m_wrapText)
					xml += wxT(" wrapText=\"1\"");
				xml += wxT("/>");
			}
			if (style.m_locked)
				xml += wxT("<protection locked=\"1\"/>");
			xml += wxT("</xf>");
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

	const int numCols = sheet.GetNumberCols();
	const int numRows = sheet.GetNumberRows();

	// ⭐⭐ THE OUTLINE BUTTON BELONGS TO THE LINE ABOVE ITS GROUP, and Excel assumes the opposite.
	// Its default is `summaryBelow` — the group's heading is the line AFTER the range, so the +/-
	// lands at the FOOT of every group (Max, 2026-08-30: *"the plus is at the end"*). Ours is a
	// report: the heading comes FIRST and the detail hangs under it, which is what `flatten` below
	// already says when it marks `group.m_start - 1` as the collapsed line.
	//
	// So the sheet is told which way it reads, once, instead of every group being shifted by one to
	// fit the other convention — the same fact, said where it belongs. `sheetPr` is the FIRST child
	// the schema allows, before `sheetViews`.
	body += wxT("<sheetPr><outlinePr summaryBelow=\"0\" summaryRight=\"0\"/></sheetPr>");

	// ⭐⭐ FROZEN PANES FIRST — the worksheet's children are ORDERED BY THE SCHEMA, and `sheetViews`
	// stands before `cols`: `sheetPr → dimension → sheetViews → sheetFormatPr → cols → sheetData`.
	//
	// 🛑 IT WAS APPENDED AFTER THE COLUMNS, and Excel does not report a misplaced element — it
	// declares the whole PART unreadable, throws `/xl/worksheets/sheet1.xml` away and rebuilds an
	// empty one: "Excel was able to open the file by repairing or removing the unreadable content."
	// Everything the sheet carried went with it — fills, fonts, borders, merges — which reads as "the
	// styles are not being written" while styles.xml is sitting in the archive, complete and correct
	// (Max, 2026-08-30: *"make the fill, the font, the borders be written into the Excel file"* —
	// they were; the part holding them was being discarded).
	//
	// ⚠ AND THE POSITION IS REPORTED USELESSLY: the document is written as ONE line, so every error
	// Excel finds is "line 1", whatever it is. The order is checked by reading the schema, not the log.
	if (sheet.GetRowFreeze() > 0 || sheet.GetColFreeze() > 0) {
		const wxString topLeft = CellRef(sheet.GetRowFreeze(), sheet.GetColFreeze());
		wxString pane = wxT("<pane");
		if (sheet.GetColFreeze() > 0) pane += wxString::Format(wxT(" xSplit=\"%d\""), sheet.GetColFreeze());
		if (sheet.GetRowFreeze() > 0) pane += wxString::Format(wxT(" ySplit=\"%d\""), sheet.GetRowFreeze());
		pane += wxString::Format(wxT(" topLeftCell=\"%s\" activePane=\"bottomRight\" state=\"frozen\"/>"), topLeft);

		body += wxT("<sheetViews><sheetView workbookViewId=\"0\">") + pane + wxT("</sheetView></sheetViews>");
	}

	// ⭐⭐ THE GROUPINGS TRAVEL — Excel calls them an OUTLINE and says the same thing a different way:
	// we hold a RANGE with a depth (`AddRowGroup(start, end, level)`), it holds the depth ON EACH LINE
	// (`outlineLevel`) and works the ranges out from the runs. So the ranges are flattened here, once,
	// into a level per row and per column (Max, 2026-08-30: *"we have groupings now — Excel supports
	// them too; teach it to carry them"*).
	//
	// A COLLAPSED group is two more facts and both are needed or the sheet opens wrong: the lines
	// inside it are `hidden`, and the SUMMARY line — the one before the range, where the button sits —
	// is `collapsed`. Written without the second, Excel shows a folded group with no way to open it.
	std::vector<int>  rowLevel(static_cast<size_t>(numRows) + 2, 0), colLevel(static_cast<size_t>(numCols) + 2, 0);
	std::vector<bool> rowHidden(rowLevel.size(), false), colHidden(colLevel.size(), false);
	std::vector<bool> rowCollapsed(rowLevel.size(), false), colCollapsed(colLevel.size(), false);
	int maxRowLevel = 0, maxColLevel = 0;

	const auto flatten = [](const ibSpreadsheetGroupDescription& group, std::vector<int>& level,
	                        std::vector<bool>& hidden, std::vector<bool>& collapsed, int& deepest) {
		for (unsigned int at = group.m_start; at <= group.m_end && at + 1 < level.size(); at++) {
			level[at] = std::max<int>(level[at], static_cast<int>(group.m_level));
			deepest   = std::max<int>(deepest, level[at]);
			if (group.m_collapsed)
				hidden[at] = true;
		}
		// …and the summary line, which stands BEFORE the range — the same line the grid hangs the
		// button on (ibGrid::NormalizeRowGroups).
		if (group.m_collapsed && group.m_start > 0 && group.m_start - 1 < collapsed.size())
			collapsed[group.m_start - 1] = true;
	};

	for (int i = 0; i < sheet.GetGroupNumberRows(); i++)
		if (const ibSpreadsheetGroupDescription* group = sheet.GetRowGroupByIdx(i))
			flatten(*group, rowLevel, rowHidden, rowCollapsed, maxRowLevel);
	for (int i = 0; i < sheet.GetGroupNumberCols(); i++)
		if (const ibSpreadsheetGroupDescription* group = sheet.GetColGroupByIdx(i))
			flatten(*group, colLevel, colHidden, colCollapsed, maxColLevel);

	// ⭐⭐ THE SHEET DECLARES ITS OWN DEFAULTS, and that is what makes the two programs agree about a
	// size. Excel's default row is 15 POINTS and ours is 15 — the same number in a different unit
	// (Max, 2026-08-30: *"the cell height is 15, yes — but the grid and Excel see them differently"*).
	//
	// ⭐ SO THE UNITS ARE TREATED AS THE SAME ONE, because at the default they already are: what both
	// numbers mean is ONE LINE OF THE DEFAULT FONT — 15px of an 8pt face here, 15pt of an 11pt face
	// there. Converted by 72/96 instead, a row of ours came out at 11.25 while every row that had no
	// height of its own stayed at Excel's 15, so a sheet ended up with two different "defaults" in it.
	// Written 1:1 against a declared default, a row twice the default height is twice it in both.
	//
	// ⚠ AND IT IS WRITTEN UNCONDITIONALLY: the anchor is needed whether or not the sheet has an
	// outline. Without it the numbers are read against Excel's defaults rather than against ours.
	body += wxString::Format(
		wxT("<sheetFormatPr defaultRowHeight=\"%d\" defaultColWidth=\"%.2f\" outlineLevelRow=\"%d\" outlineLevelCol=\"%d\"/>"),
		s_defaultRowHeight, s_defaultColWidth / 7.0, maxRowLevel, maxColLevel);

	// COLUMN WIDTHS — Excel counts them in characters, we in pixels; the ratio it
	// uses for the default font is 7 pixels to the character.

	// ⚠ AND A COLUMN IS WRITTEN FOR EITHER REASON — a width of its own OR a place in the outline. Kept
	// on the width alone, a grouped column of ordinary width had no `<col>` to carry its level and the
	// grouping stopped at the sheet's edge.
	//
	// ⚠ AN EMPTY `<cols/>` IS INVALID — the element requires at least one child — so the entries are
	// built first and the container is written only if there are any.
	{
		wxString cols;
		for (int col = 0; col <= numCols; col++) {
			const int width = sheet.GetColSize(col);
			const int level = (static_cast<size_t>(col) < colLevel.size()) ? colLevel[col] : 0;
			const bool sized = (width != s_defaultColWidth);
			if (!sized && level == 0)
				continue;

			// ⭐ A HIDDEN COLUMN IS A ZERO WIDTH HERE — that is what «Hide» does in the editor
			// (`ibGridEditor::OnHideCell` → `SetColSize(col, 0)`) — and it is `hidden` there. Said as a
			// width of nothing it depends on the reader's mood; said as the flag it is what it means.
			const bool hidden = (sized && width == 0)
				|| (static_cast<size_t>(col) < colHidden.size() && colHidden[col]);

			cols += wxString::Format(wxT("<col min=\"%d\" max=\"%d\""), col + 1, col + 1);
			if (sized && width > 0)
				cols += wxString::Format(wxT(" width=\"%.2f\" customWidth=\"1\""), width / 7.0);
			if (level > 0)
				cols += wxString::Format(wxT(" outlineLevel=\"%d\""), level);
			if (hidden)
				cols += wxT(" hidden=\"1\"");
			if (static_cast<size_t>(col) < colCollapsed.size() && colCollapsed[col])
				cols += wxT(" collapsed=\"1\"");
			cols += wxT("/>");
		}
		if (!cols.IsEmpty())
			body += wxT("<cols>") + cols + wxT("</cols>");
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
		// The height goes across as it stands, against the default declared in `sheetFormatPr` above.
		// A height of ZERO is how «Hide» is stored here, so it is written as the flag rather than as
		// a height of nothing.
		if (height != s_defaultRowHeight && height > 0)
			rowTag += wxString::Format(wxT(" ht=\"%d\" customHeight=\"1\""), height);
		// …AND ITS PLACE IN THE OUTLINE — the depth, whether it is folded away inside a closed group,
		// and whether it is the summary line that opens one.
		const int level = (static_cast<size_t>(row) < rowLevel.size()) ? rowLevel[row] : 0;
		if (level > 0)
			rowTag += wxString::Format(wxT(" outlineLevel=\"%d\""), level);
		if (height == 0 || (static_cast<size_t>(row) < rowHidden.size() && rowHidden[row]))
			rowTag += wxT(" hidden=\"1\"");
		if (static_cast<size_t>(row) < rowCollapsed.size() && rowCollapsed[row])
			rowTag += wxT(" collapsed=\"1\"");
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
