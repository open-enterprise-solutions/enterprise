////////////////////////////////////////////////////////////////////////////
//	Description : an Excel workbook, read into our own document
////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE WORKBOOK BECOMES ONE DOCUMENT, ITS SHEETS SEPARATED BY PAGE BREAKS
// (Max, 2026-08-26). We have no tabs; what a second sheet means here is "this
// part prints separately", and the document already says that. So the sheets are
// laid one under another and a row break is put between them.
//
// ⚠ A ZIP IS READ FORWARD ONLY. wxZipInputStream hands out entries in the order
// they were stored and cannot seek back, and the parts refer to each other
// (workbook -> rels -> sheets, cells -> sharedStrings). So the pass below COLLECTS
// what it needs into memory first and interprets afterwards — a spreadsheet's
// parts are text and small beside its cells.

#include "backend/sheetFormat/sheetFormatXlsx.h"

#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

#include <map>
#include <vector>

namespace {

// A1 -> (row, col), zero-based. Returns false on anything that is not a reference.
bool CellAt(const wxString& ref, int& row, int& col)
{
	col = 0;
	size_t at = 0;
	for (; at < ref.length() && wxIsalpha(ref[at]); at++)
		col = col * 26 + (wxToupper(ref[at]).GetValue() - wxT('A') + 1);

	if (at == 0 || at >= ref.length())
		return false;

	long number = 0;
	if (!ref.Mid(at).ToLong(&number) || number <= 0)
		return false;

	row = static_cast<int>(number) - 1;
	col -= 1;
	return true;
}

// Every part of the package, by name. Read in one forward pass.
using ibPackage = std::map<wxString, wxString>;

// ⭐⭐ A PART NAME IS CASE-INSENSITIVE, so there is ONE spelling of it in this file and the entry is
// folded onto it the moment it is read — never compared as it happened to be stored.
//
// 🛑 IT WAS COMPARED AS STORED, and a real workbook stored `xl/SharedStrings.xml` while naming it
// `xl/sharedStrings.xml` in both `[Content_Types].xml` and the relationship that points at it. The
// part was therefore skipped as one we do not read, every `t="s"` cell resolved to nothing, and the
// sheet arrived with its geometry intact and not one word in it (Max, 2026-08-30: *"it does not load
// the header at all"* — it loaded no text anywhere).
//
// ⚠ AND THE LOOKUPS BELOW USE THE SAME KEY. Normalising on the way in and then searching for a
// capital letter would be the same defect one step further along.
wxString PartKey(const wxString& name)
{
	wxString key = name;
	key.Replace(wxT("\\"), wxT("/"));
	if (key.StartsWith(wxT("/")))
		key = key.Mid(1);
	return key.Lower();
}

// ⚠ AND `true` IS `1`. An XSD boolean has FOUR spellings — `1`/`0` and `true`/`false` — Excel writes
// the first pair and other producers write the second, so an attribute compared against one of them
// reads every file that chose the other as "absent". The workbook above says `customHeight="true"`.
bool BoolOf(const wxXmlNode* node, const wxString& attribute, bool absent = false)
{
	const wxString value = node->GetAttribute(attribute, wxEmptyString);
	if (value.IsEmpty())
		return absent;
	return value == wxT("1") || value.IsSameAs(wxT("true"), false);
}

// What a sheet is made of, as far as this reader is concerned: which sheets there are and in what
// order (workbook + its rels), the strings they share, the STYLES they point at, and the sheets.
//
// ⭐⭐ THE STYLES ARE READ NOW, and until they were the reader gave back a document that looked
// nothing like the one written: fills, fonts, borders and alignment all sat in `xl/styles.xml`, the
// cells pointed at them with `s="…"`, and this side read the text alone (Max, 2026-08-30: *"reading
// does not take the styles into account"*). Writing them and not reading them is one format
// implemented twice, in opposite directions, with only one of them finished.
bool IsPartWeRead(const wxString& key)
{
	return key == wxT("xl/workbook.xml")
		|| key == wxT("xl/_rels/workbook.xml.rels")
		|| key == wxT("xl/sharedstrings.xml")
		|| key == wxT("xl/styles.xml")
		|| key.StartsWith(wxT("xl/worksheets/"));
}

// ---------------------------------------------------------------------------
// The look of one cell, as `xl/styles.xml` states it. A cell names an index into
// `cellXfs`; the entry there names a font, a fill and a border by index of their
// own, so the part is read once into flat tables and the cells then cost a lookup.
// ---------------------------------------------------------------------------
struct XlsxStyle {
	wxFont   m_font;
	bool     m_hasFont = false;
	wxColour m_textColour;
	wxColour m_fillColour;
	ibSpreadsheetBorderDescription m_border[4];   // left, right, top, bottom
	bool     m_hasBorder[4] = { false, false, false, false };
	int      m_alignHorz = ibAlignmentHorz_Left;
	int      m_alignVert = ibAlignmentVert_Top;
	bool     m_hasAlign = false;
	int      m_textOrient = wxHORIZONTAL;
	ibSpreadsheetCellDescription::ibFitMode m_fitMode = ibSpreadsheetCellDescription::Mode_Overflow;
	bool     m_readOnly = false;
};

// Excel's nine line names back to a pen and a width. The names that differ only in
// weight collapse onto the same pen with a wider stroke, which is what the grid draws.
void PenOf(const wxString& name, ibSpreadsheetBorderDescription& border)
{
	border.m_width = 1;

	if (name == wxT("dotted") || name == wxT("hair"))
		border.m_style = wxPENSTYLE_DOT;
	else if (name == wxT("dashed"))
		border.m_style = wxPENSTYLE_SHORT_DASH;
	else if (name == wxT("mediumDashed")) {
		border.m_style = wxPENSTYLE_SHORT_DASH;
		border.m_width = 2;
	}
	else if (name == wxT("dashDot") || name == wxT("dashDotDot"))
		border.m_style = wxPENSTYLE_DOT_DASH;
	else if (name == wxT("mediumDashDot") || name == wxT("mediumDashDotDot") || name == wxT("slantDashDot")) {
		border.m_style = wxPENSTYLE_DOT_DASH;
		border.m_width = 2;
	}
	else if (name == wxT("medium") || name == wxT("double")) {
		border.m_style = wxPENSTYLE_SOLID;
		border.m_width = 2;
	}
	else if (name == wxT("thick")) {
		border.m_style = wxPENSTYLE_SOLID;
		border.m_width = 3;
	}
	else
		border.m_style = wxPENSTYLE_SOLID;   // "thin", and anything a later Excel invents
}

// "FFD4E4D4" / "D4E4D4" -> a colour. An INDEXED or THEME colour is not resolved — those need the
// workbook's palette and its theme part, and a wrong colour is worse than the default one.
wxColour ColourOf(const wxXmlNode* node)
{
	if (node == nullptr)
		return wxColour();

	const wxString rgb = node->GetAttribute(wxT("rgb"), wxEmptyString);
	if (rgb.length() < 6)
		return wxColour();

	unsigned long value = 0;
	if (!rgb.Right(6).ToULong(&value, 16))
		return wxColour();

	return wxColour(static_cast<unsigned char>((value >> 16) & 0xFF),
	                static_cast<unsigned char>((value >> 8) & 0xFF),
	                static_cast<unsigned char>(value & 0xFF));
}

const wxXmlNode* ChildNamed(const wxXmlNode* parent, const wxString& name)
{
	for (const wxXmlNode* child = parent != nullptr ? parent->GetChildren() : nullptr;
	     child != nullptr; child = child->GetNext())
		if (child->GetName() == name)
			return child;
	return nullptr;
}

// 🛑⭐ A FONT FLAG IS THE ELEMENT **AND** ITS VALUE, and reading only the first turned every
// document from one producer into struck-through prose.
//
// In OOXML `<b/>` means bold and `<b val="0"/>` means NOT bold — the element carries an optional
// value that DENIES it. Excel simply omits the element when a flag is off, so testing for presence
// works on Excel's own files and on nothing else. LibreOffice writes every flag out explicitly,
// the off ones included: `<b val="false"/><i val="false"/><strike val="false"/>`.
//
// Read by presence, such a font came back bold, italic, underlined AND struck through — all four
// at once, on every cell of the sheet. Reported off the screen (Max, 2026-09-02: *"it keeps putting
// struck-through text"*), with a workbook that says exactly that; one defect standing on four lines
// that all looked right.
bool FontFlag(const wxXmlNode* font, const wxString& name, bool absent = false)
{
	const wxXmlNode* flag = ChildNamed(font, name);
	if (flag == nullptr)
		return absent;

	// Present and unqualified is the ordinary "on"; a value is the denial.
	const wxString value = flag->GetAttribute(wxT("val"), wxEmptyString);
	if (value.IsEmpty())
		return true;

	return !(value == wxT("0") || value.IsSameAs(wxT("false"), false));
}

// ⚠ UNDERLINE IS NOT A FLAG AT ALL — it is a STYLE with a name, and one of the names means none.
// `<u/>` is single, `<u val="double"/>` is double, and `<u val="none"/>` — what a producer writing
// everything out says for an unstyled font — is no underline whatever. The reader above cannot
// answer this one: "none" is neither 0 nor false, so it would read as switched on.
bool Underlined(const wxXmlNode* font)
{
	const wxXmlNode* flag = ChildNamed(font, wxT("u"));
	if (flag == nullptr)
		return false;

	const wxString value = flag->GetAttribute(wxT("val"), wxEmptyString);
	return value.IsEmpty() || !value.IsSameAs(wxT("none"), false);
}

std::vector<XlsxStyle> ReadStyles(const wxString& xmlText)
{
	std::vector<XlsxStyle> byIndex;
	if (xmlText.IsEmpty())
		return byIndex;

	wxStringInputStream input(xmlText);
	wxXmlDocument xml;
	if (!xml.Load(input) || xml.GetRoot() == nullptr)
		return byIndex;

	struct Fill  { wxColour m_colour; };
	struct Border { ibSpreadsheetBorderDescription m_side[4]; bool m_has[4] = { false, false, false, false }; };

	std::vector<wxFont>  fonts;
	std::vector<wxColour> fontColours;
	std::vector<Fill>    fills;
	std::vector<Border>  borders;

	for (const wxXmlNode* table = xml.GetRoot()->GetChildren(); table != nullptr; table = table->GetNext()) {
		if (table->GetName() == wxT("fonts")) {
			for (const wxXmlNode* font = table->GetChildren(); font != nullptr; font = font->GetNext()) {
				double size = 8.0;
				if (const wxXmlNode* sz = ChildNamed(font, wxT("sz")))
					sz->GetAttribute(wxT("val"), wxT("8")).ToCDouble(&size);
				wxString face;
				if (const wxXmlNode* name = ChildNamed(font, wxT("name")))
					face = name->GetAttribute(wxT("val"), wxEmptyString);

				wxFont made(wxFontInfo(static_cast<int>(size + 0.5)).FaceName(face)
					.Bold(FontFlag(font, wxT("b")))
					.Italic(FontFlag(font, wxT("i")))
					.Underlined(Underlined(font))
					.Strikethrough(FontFlag(font, wxT("strike"))));
				fonts.push_back(made);
				fontColours.push_back(ColourOf(ChildNamed(font, wxT("color"))));
			}
		}
		else if (table->GetName() == wxT("fills")) {
			for (const wxXmlNode* fill = table->GetChildren(); fill != nullptr; fill = fill->GetNext()) {
				Fill made;
				if (const wxXmlNode* pattern = ChildNamed(fill, wxT("patternFill")))
					if (pattern->GetAttribute(wxT("patternType"), wxEmptyString) == wxT("solid"))
						made.m_colour = ColourOf(ChildNamed(pattern, wxT("fgColor")));
				fills.push_back(made);
			}
		}
		else if (table->GetName() == wxT("borders")) {
			static const wxString sideName[4] = { wxT("left"), wxT("right"), wxT("top"), wxT("bottom") };
			for (const wxXmlNode* border = table->GetChildren(); border != nullptr; border = border->GetNext()) {
				Border made;
				for (int i = 0; i < 4; i++) {
					const wxXmlNode* side = ChildNamed(border, sideName[i]);
					if (side == nullptr)
						continue;
					const wxString line = side->GetAttribute(wxT("style"), wxEmptyString);
					if (line.IsEmpty() || line == wxT("none"))
						continue;
					made.m_has[i] = true;
					PenOf(line, made.m_side[i]);
					const wxColour colour = ColourOf(ChildNamed(side, wxT("color")));
					if (colour.IsOk())
						made.m_side[i].m_colour = colour;
				}
				borders.push_back(made);
			}
		}
	}

	// …and `cellXfs`, which is what a cell's `s` indexes. Read last, because it points at the three
	// tables above and they must be in hand.
	for (const wxXmlNode* table = xml.GetRoot()->GetChildren(); table != nullptr; table = table->GetNext()) {
		if (table->GetName() != wxT("cellXfs"))
			continue;

		for (const wxXmlNode* xf = table->GetChildren(); xf != nullptr; xf = xf->GetNext()) {
			XlsxStyle style;
			long index = 0;

			if (xf->GetAttribute(wxT("fontId"), wxT("0")).ToLong(&index)
				&& index >= 0 && static_cast<size_t>(index) < fonts.size()) {
				style.m_font    = fonts[static_cast<size_t>(index)];
				style.m_hasFont = style.m_font.IsOk();
				style.m_textColour = fontColours[static_cast<size_t>(index)];
			}
			if (xf->GetAttribute(wxT("fillId"), wxT("0")).ToLong(&index)
				&& index >= 0 && static_cast<size_t>(index) < fills.size())
				style.m_fillColour = fills[static_cast<size_t>(index)].m_colour;
			if (xf->GetAttribute(wxT("borderId"), wxT("0")).ToLong(&index)
				&& index >= 0 && static_cast<size_t>(index) < borders.size())
				for (int i = 0; i < 4; i++) {
					style.m_hasBorder[i] = borders[static_cast<size_t>(index)].m_has[i];
					style.m_border[i]    = borders[static_cast<size_t>(index)].m_side[i];
				}

			if (const wxXmlNode* align = ChildNamed(xf, wxT("alignment"))) {
				const wxString horz = align->GetAttribute(wxT("horizontal"), wxEmptyString);
				const wxString vert = align->GetAttribute(wxT("vertical"), wxEmptyString);
				if (horz == wxT("center") || horz == wxT("centerContinuous"))
					style.m_alignHorz = ibAlignmentHorz_Center;
				else if (horz == wxT("right"))  style.m_alignHorz = ibAlignmentHorz_Right;
				if (vert == wxT("center"))      style.m_alignVert = ibAlignmentVert_Center;
				else if (vert == wxT("bottom")) style.m_alignVert = ibAlignmentVert_Bottom;
				style.m_hasAlign = !horz.IsEmpty() || !vert.IsEmpty();

				// ⚠ `justify` AND `distributed` HAVE NO PLACE OF THEIR OWN HERE, and both mean the
				// text fills the cell's width — which is `wrapText` by another route, so the cell is
				// confined rather than left to spill.
				const bool spread = horz == wxT("justify") || horz == wxT("distributed")
					|| BoolOf(align, wxT("wrapText"));
				if (spread)
					style.m_fitMode = ibSpreadsheetCellDescription::Mode_Clip;

				// An angle back to a direction: anything that is not level is drawn vertically here.
				long rotation = 0;
				if (align->GetAttribute(wxT("textRotation"), wxT("0")).ToLong(&rotation) && rotation != 0)
					style.m_textOrient = wxVERTICAL;
			}

			if (const wxXmlNode* protect = ChildNamed(xf, wxT("protection")))
				style.m_readOnly = BoolOf(protect, wxT("locked"));

			byIndex.push_back(style);
		}
	}

	return byIndex;
}

bool ReadPackage(const wxString& fileName, ibPackage& parts)
{
	wxFileInputStream file(fileName);
	if (!file.IsOk())
		return false;

	wxZipInputStream zip(file);
	if (!zip.IsOk())
		return false;

	for (;;) {
		std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry());
		if (!entry)
			break;

		const wxString name = PartKey(entry->GetInternalName());

		// ⭐ ONLY THE PARTS THIS READER SPEAKS, NAMED — not everything except a list of what to
		// skip. The two read the same on the files we thought of and differently on the rest:
		// a workbook also carries themes, printer settings, drawings, certificates, and
		// xl/calcChain.xml — which is the FORMULA DEPENDENCY GRAPH and on a large sheet rivals
		// the sheet itself in size. Skipping four names left all of those being read into memory
		// to be ignored, which is most of what made a big workbook slow to open.
		if (!IsPartWeRead(name))
			continue;

		wxStringOutputStream out;
		zip.Read(out);
		parts[name] = out.GetString();
	}

	return !parts.empty();
}

bool ParseXml(const wxString& text, wxXmlDocument& xml)
{
	if (text.IsEmpty())
		return false;

	wxStringInputStream in(text);
	// ⚠ THE ENGINE'S OWN VERDICT, kept quiet. A part that does not parse is not our
	// error to report per part — the caller is told the file could not be read, once.
	wxLogNull noLog;
	return xml.Load(in);
}

// <sst><si><t>text</t></si>… — the table Excel puts repeated strings in. A cell
// then says t="s" and carries the INDEX.
//
// ⚠ A STRING MAY BE SPLIT ACROSS RUNS (<si><r><t>Total</t></r><r><t> 2026</t></r></si>)
// when part of it is formatted differently — reading only the first <t> silently
// truncates every such cell, which is exactly what a heading looks like.
void ReadSharedStrings(const ibPackage& parts, std::vector<wxString>& strings)
{
	const auto found = parts.find(wxT("xl/sharedstrings.xml"));
	if (found == parts.end())
		return;

	wxXmlDocument xml;
	if (!ParseXml(found->second, xml) || xml.GetRoot() == nullptr)
		return;

	for (wxXmlNode* si = xml.GetRoot()->GetChildren(); si != nullptr; si = si->GetNext()) {
		if (si->GetName() != wxT("si"))
			continue;

		wxString text;
		for (wxXmlNode* part = si->GetChildren(); part != nullptr; part = part->GetNext()) {
			if (part->GetName() == wxT("t"))
				text += part->GetNodeContent();
			else if (part->GetName() == wxT("r")) {
				for (wxXmlNode* run = part->GetChildren(); run != nullptr; run = run->GetNext())
					if (run->GetName() == wxT("t"))
						text += run->GetNodeContent();
			}
		}
		strings.push_back(text);
	}
}

// The sheets, in the order the workbook lists them — which is the order a person
// sees the tabs in, and therefore the order they must be laid out in.
void ReadSheetOrder(const ibPackage& parts, std::vector<wxString>& sheetParts)
{
	// workbook.xml gives each sheet an r:id; the rels part says which file that is.
	std::map<wxString, wxString> targetById;

	wxXmlDocument rels;
	const auto relsPart = parts.find(wxT("xl/_rels/workbook.xml.rels"));
	if (relsPart != parts.end() && ParseXml(relsPart->second, rels) && rels.GetRoot() != nullptr) {
		for (wxXmlNode* rel = rels.GetRoot()->GetChildren(); rel != nullptr; rel = rel->GetNext()) {
			const wxString id = rel->GetAttribute(wxT("Id"));
			wxString target = rel->GetAttribute(wxT("Target"));
			if (id.IsEmpty() || target.IsEmpty())
				continue;
			if (!target.StartsWith(wxT("/")))
				target = wxT("xl/") + target;
			targetById[id] = PartKey(target);
		}
	}

	wxXmlDocument workbook;
	const auto workbookPart = parts.find(wxT("xl/workbook.xml"));
	if (workbookPart != parts.end() && ParseXml(workbookPart->second, workbook) && workbook.GetRoot() != nullptr) {
		for (wxXmlNode* node = workbook.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {
			if (node->GetName() != wxT("sheets"))
				continue;
			for (wxXmlNode* sheet = node->GetChildren(); sheet != nullptr; sheet = sheet->GetNext()) {
				const wxString id = sheet->GetAttribute(wxT("r:id"), sheet->GetAttribute(wxT("id")));
				const auto target = targetById.find(id);
				if (target != targetById.end())
					sheetParts.push_back(target->second);
			}
		}
	}

	// A WORKBOOK THAT NAMES NOTHING WE CAN FOLLOW still usually has the first sheet
	// where everyone puts it. Better one sheet than an empty document.
	if (sheetParts.empty() && parts.find(wxT("xl/worksheets/sheet1.xml")) != parts.end())
		sheetParts.push_back(wxT("xl/worksheets/sheet1.xml"));
}

// One worksheet, laid into the document starting at `topRow`. Returns how many
// rows it occupied.
// ⭐⭐ EXCEL HOLDS THE DEPTH ON EVERY LINE, WE HOLD RANGES — the writer flattens the ranges into
// levels, so reading is the same journey backwards: for each depth, every unbroken run of lines at
// that depth or deeper is one group. A run whose lines are all hidden is a group that was closed.
//
// ⚠ THE RUN IS `level >= depth`, NOT `level == depth`. A group with a group inside it holds lines of
// BOTH depths; matched exactly, the outer group would come back in pieces with the inner one cut out
// of its middle.
void GroupsFromLevels(const std::map<int, int>& level, const std::map<int, bool>& hidden,
                      ibSpreadsheetDescription& document, bool rows, int offset)
{
	int deepest = 0;
	for (const auto& at : level)
		deepest = wxMax(deepest, at.second);

	for (int depth = 1; depth <= deepest; depth++) {
		int start = -1, end = -1;
		bool allHidden = true;

		const auto close = [&]() {
			if (start < 0)
				return;
			if (rows) document.AddRowGroup(start + offset, end + offset, depth, allHidden);
			else      document.AddColGroup(start, end, depth, allHidden);
			start = -1;
			allHidden = true;
		};

		for (const auto& at : level) {
			if (at.second < depth) {
				close();
				continue;
			}
			if (start < 0 || at.first != end + 1) {
				close();
				start = at.first;
			}
			end = at.first;

			const auto folded = hidden.find(at.first);
			if (folded == hidden.end() || !folded->second)
				allHidden = false;
		}
		close();
	}
}

int ReadSheet(const wxString& partText, const std::vector<wxString>& strings,
              const std::vector<XlsxStyle>& styles, ibSpreadsheetDescription& document, int topRow)
{
	wxXmlDocument xml;
	if (!ParseXml(partText, xml) || xml.GetRoot() == nullptr)
		return 0;

	int usedRows = 0;
	std::map<int, int> rowLevel, colLevel;
	std::map<int, bool> rowHidden, colHidden;

	// ⭐⭐ A SIZE IS READ AGAINST THE SHEET'S OWN DEFAULT, because that is the only thing the two
	// programs measure alike: a row of the default height is ONE LINE of the default font in both,
	// whatever number each writes for it. This workbook says 11.429 and Excel says 15 and ours says
	// 15 — taken as a number, its rows arrive a third too short; taken as a RATIO to what the sheet
	// itself calls normal, a row of ordinary height stays ordinary and a double one stays double.
	//
	// (This is the same rule the writer states from the other side, where it declares OUR defaults in
	// `sheetFormatPr` so the far side has something to measure against. Our own files scale by one.)
	double theirRow = s_defaultRowHeight;
	double theirCol = s_defaultColWidth / 7.0;

	if (const wxXmlNode* format = ChildNamed(xml.GetRoot(), wxT("sheetFormatPr"))) {
		double value = 0.0;
		if (format->GetAttribute(wxT("defaultRowHeight"), wxEmptyString).ToCDouble(&value) && value > 0.0)
			theirRow = value;
		if (format->GetAttribute(wxT("defaultColWidth"), wxEmptyString).ToCDouble(&value) && value > 0.0)
			theirCol = value;
	}

	const double rowScale = s_defaultRowHeight / theirRow;
	const double colScale = s_defaultColWidth / theirCol;

	for (wxXmlNode* node = xml.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {

		// --- frozen panes ----------------------------------------------------------
		// Only from the FIRST sheet: the document has one pair of frozen edges and the
		// sheets are laid one under another, so a later sheet's split would land on rows
		// that belong to the one before it.
		if (node->GetName() == wxT("sheetViews") && topRow == 0) {
			for (wxXmlNode* view = node->GetChildren(); view != nullptr; view = view->GetNext()) {
				const wxXmlNode* pane = ChildNamed(view, wxT("pane"));
				if (pane == nullptr)
					continue;
				long split = 0;
				if (pane->GetAttribute(wxT("xSplit"), wxT("0")).ToLong(&split) && split > 0)
					document.SetColFreeze(static_cast<int>(split));
				if (pane->GetAttribute(wxT("ySplit"), wxT("0")).ToLong(&split) && split > 0)
					document.SetRowFreeze(static_cast<int>(split));
			}
			continue;
		}

		// --- where the pages end -----------------------------------------------------
		// ⭐ A BREAK IS A FACT ABOUT THE DOCUMENT, and it was travelling one way only: written into
		// every workbook we save and read back out of none of them, so a file saved with its pages
		// laid out came home flat. `id` is the index of the first line of the NEW page, and ours
		// says which line the OLD page ended on — hence the step back.
		if (node->GetName() == wxT("rowBreaks") || node->GetName() == wxT("colBreaks")) {
			const bool rows = node->GetName() == wxT("rowBreaks");

			for (wxXmlNode* brk = node->GetChildren(); brk != nullptr; brk = brk->GetNext()) {
				long at = 0;
				if (brk->GetName() != wxT("brk")
					|| !brk->GetAttribute(wxT("id"), wxT("0")).ToLong(&at) || at <= 0)
					continue;

				if (rows) document.AddRowBrake(topRow + static_cast<int>(at) - 1);
				else if (topRow == 0) document.AddColBrake(static_cast<int>(at) - 1);
			}
			continue;
		}

		// --- column widths, and where a column sits in the outline -------------------
		if (node->GetName() == wxT("cols")) {
			for (wxXmlNode* col = node->GetChildren(); col != nullptr; col = col->GetNext()) {
				long from = 0, to = 0;
				double width = 0.0;
				if (!col->GetAttribute(wxT("min"), wxT("0")).ToLong(&from) ||
					!col->GetAttribute(wxT("max"), wxT("0")).ToLong(&to))
					continue;

				const bool sized = col->GetAttribute(wxT("width"), wxEmptyString).ToCDouble(&width) && width > 0.0;
				const bool folded = BoolOf(col, wxT("hidden"));

				long depth = 0;
				col->GetAttribute(wxT("outlineLevel"), wxT("0")).ToLong(&depth);

				for (long at = from; at <= to && at > 0; at++) {
					const int index = static_cast<int>(at) - 1;
					// A hidden column is a width of nothing here, which is what «Hide» leaves behind.
					if (folded)
						document.SetColSize(index, 0);
					else if (sized)
						document.SetColSize(index, static_cast<int>(width * colScale + 0.5));

					if (depth > 0)
						colLevel[index] = static_cast<int>(depth);
					if (folded)
						colHidden[index] = true;
				}
			}
			continue;
		}

		if (node->GetName() != wxT("sheetData"))
			continue;

		// --- the cells --------------------------------------------------------------
		for (wxXmlNode* row = node->GetChildren(); row != nullptr; row = row->GetNext()) {
			if (row->GetName() != wxT("row"))
				continue;

			long rowNumber = 0;
			if (!row->GetAttribute(wxT("r"), wxT("0")).ToLong(&rowNumber) || rowNumber <= 0)
				continue;

			const int documentRow = topRow + static_cast<int>(rowNumber) - 1;
			usedRows = wxMax(usedRows, static_cast<int>(rowNumber));

			// The height comes across against the sheet's own default (see `rowScale` above), and a
			// hidden row is a height of nothing, as «Hide» leaves it.
			const bool folded = BoolOf(row, wxT("hidden"));

			double height = 0.0;
			if (folded)
				document.SetRowSize(documentRow, 0);
			else if (row->GetAttribute(wxT("ht"), wxEmptyString).ToCDouble(&height) && height > 0.0)
				document.SetRowSize(documentRow, static_cast<int>(height * rowScale + 0.5));

			long depth = 0;
			if (row->GetAttribute(wxT("outlineLevel"), wxT("0")).ToLong(&depth) && depth > 0)
				rowLevel[static_cast<int>(rowNumber) - 1] = static_cast<int>(depth);
			if (folded)
				rowHidden[static_cast<int>(rowNumber) - 1] = true;

			for (wxXmlNode* cell = row->GetChildren(); cell != nullptr; cell = cell->GetNext()) {
				if (cell->GetName() != wxT("c"))
					continue;

				int cellRow = 0, cellCol = 0;
				if (!CellAt(cell->GetAttribute(wxT("r")), cellRow, cellCol))
					continue;

				const wxString type = cell->GetAttribute(wxT("t"), wxT("n"));

				wxString value;
				for (wxXmlNode* part = cell->GetChildren(); part != nullptr; part = part->GetNext()) {
					// ⚠ <v> IS TAKEN AND <f> IS NOT. A cell may carry a formula and the
					// value the other program last computed for it; we do not evaluate
					// formulas, so the value is what a person saw there — and a formula
					// dropped into a cell as text would be a lie about what it is.
					if (part->GetName() == wxT("v"))
						value = part->GetNodeContent();
					else if (part->GetName() == wxT("is")) {
						for (wxXmlNode* inline_ = part->GetChildren(); inline_ != nullptr; inline_ = inline_->GetNext())
							if (inline_->GetName() == wxT("t"))
								value += inline_->GetNodeContent();
					}
				}

				if (type == wxT("s")) {
					long index = 0;
					if (value.ToLong(&index) && index >= 0 && static_cast<size_t>(index) < strings.size())
						value = strings[static_cast<size_t>(index)];
					else
						value.clear();
				}

				// ⭐ A CELL WITH NO TEXT IS STILL A CELL once styles are read: a filled band, a ruled
				// box, a heading's underline — none of them carry a word. Skipped on the value
				// alone, the sheet came back with its shape but none of its markings.
				long styleAt = 0;
				const bool hasStyle = cell->GetAttribute(wxT("s"), wxEmptyString).ToLong(&styleAt)
					&& styleAt >= 0 && static_cast<size_t>(styleAt) < styles.size();

				if (value.IsEmpty() && !hasStyle)
					continue;

				const int at = topRow + cellRow;
				if (!value.IsEmpty())
					document.SetCellValue(at, cellCol, value);

				if (!hasStyle)
					continue;

				const XlsxStyle& style = styles[static_cast<size_t>(styleAt)];

				if (style.m_hasFont)
					document.SetCellFont(at, cellCol, style.m_font);
				if (style.m_textColour.IsOk())
					document.SetCellTextColour(at, cellCol, style.m_textColour);
				if (style.m_fillColour.IsOk())
					document.SetCellBackgroundColour(at, cellCol, style.m_fillColour);
				if (style.m_hasAlign)
					document.SetCellAlignment(at, cellCol, style.m_alignHorz, style.m_alignVert);

				document.SetCellTextOrient(at, cellCol, style.m_textOrient);
				document.SetCellFitMode(at, cellCol, style.m_fitMode);
				document.SetCellReadOnly(at, cellCol, style.m_readOnly);

				if (style.m_hasBorder[0]) document.SetCellBorderLeft(at, cellCol, style.m_border[0]);
				if (style.m_hasBorder[1]) document.SetCellBorderRight(at, cellCol, style.m_border[1]);
				if (style.m_hasBorder[2]) document.SetCellBorderTop(at, cellCol, style.m_border[2]);
				if (style.m_hasBorder[3]) document.SetCellBorderBottom(at, cellCol, style.m_border[3]);
			}
		}
	}

	GroupsFromLevels(rowLevel, rowHidden, document, true, topRow);
	if (topRow == 0)
		GroupsFromLevels(colLevel, colHidden, document, false, 0);

	// --- merged cells ---------------------------------------------------------------
	for (wxXmlNode* node = xml.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() != wxT("mergeCells"))
			continue;

		for (wxXmlNode* merge = node->GetChildren(); merge != nullptr; merge = merge->GetNext()) {
			const wxString ref = merge->GetAttribute(wxT("ref"));
			const int colon = ref.Find(wxT(':'));
			if (colon == wxNOT_FOUND)
				continue;

			int fromRow = 0, fromCol = 0, toRow = 0, toCol = 0;
			if (!CellAt(ref.Left(colon), fromRow, fromCol) || !CellAt(ref.Mid(colon + 1), toRow, toCol))
				continue;

			document.SetCellSize(topRow + fromRow, fromCol, toRow - fromRow + 1, toCol - fromCol + 1);
		}
	}

	return usedRows;
}

} // namespace

bool ibSheetFormatXlsx::Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const
{
	ibPackage parts;
	if (!ReadPackage(fileName, parts))
		return false;

	std::vector<wxString> sheetParts;
	ReadSheetOrder(parts, sheetParts);
	if (sheetParts.empty())
		return false;   // nothing in this file is a worksheet

	std::vector<wxString> strings;
	ReadSharedStrings(parts, strings);

	const auto stylePart = parts.find(wxT("xl/styles.xml"));
	const std::vector<XlsxStyle> styles =
		ReadStyles(stylePart != parts.end() ? stylePart->second : wxString());

	// ⚠ FILLED INTO A DOCUMENT OF ITS OWN and handed over only once it is whole: a
	// caller that gets false must be free to keep the document it already had, and
	// a half-read workbook is worse than none.
	ibSpreadsheetDescription read;

	int topRow = 0;
	for (size_t at = 0; at < sheetParts.size(); at++) {
		const auto part = parts.find(sheetParts[at]);
		if (part == parts.end())
			continue;

		// ⭐ THE BREAK GOES BEFORE EVERY SHEET BUT THE FIRST — that is what makes the
		// workbook's tabs into this document's pages.
		if (at > 0 && topRow > 0)
			read.AddRowBrake(topRow - 1);

		const int used = ReadSheet(part->second, strings, styles, read, topRow);
		topRow += wxMax(used, 1);
	}

	sheet = read;
	return true;
}
