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
#include "core/diagnostics/journal.h"   // ibJournalStopwatch / ibJournalInfo — what a read of a book cost, step by step

#include <wx/zipstrm.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

#include <map>
#include <vector>

namespace {

// A1 -> (row, col), zero-based. Returns false on anything that is not a reference.
bool CellAt(const ibString& ref, int& row, int& col)
{
	col = 0;
	size_t at = 0;
	for (; at < ref.length() && ibIsAlpha(ref[at]); at++)
		col = col * 26 + (ibToUpper(ref[at]) - ibT('A') + 1);

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
using ibPackage = std::map<ibString, ibString>;

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
ibString PartKey(const ibString& name)
{
	ibString key = name;
	key.Replace(ibT("\\"), ibT("/"));
	if (key.StartsWith(ibT("/")))
		key = key.Mid(1);
	return key.Lower();
}

// ⚠ AND `true` IS `1`. An XSD boolean has FOUR spellings — `1`/`0` and `true`/`false` — Excel writes
// the first pair and other producers write the second, so an attribute compared against one of them
// reads every file that chose the other as "absent". The workbook above says `customHeight="true"`.
bool BoolOf(const wxXmlNode* node, const ibString& attribute, bool absent = false)
{
	const ibString value = node->GetAttribute(attribute, ibEmptyString);
	if (value.IsEmpty())
		return absent;
	return value == ibT("1") || value.IsSameAs(ibT("true"), false);
}

// What a sheet is made of, as far as this reader is concerned: which sheets there are and in what
// order (workbook + its rels), the strings they share, the STYLES they point at, and the sheets.
//
// ⭐⭐ THE STYLES ARE READ NOW, and until they were the reader gave back a document that looked
// nothing like the one written: fills, fonts, borders and alignment all sat in `xl/styles.xml`, the
// cells pointed at them with `s="…"`, and this side read the text alone (Max, 2026-08-30: *"reading
// does not take the styles into account"*). Writing them and not reading them is one format
// implemented twice, in opposite directions, with only one of them finished.
bool IsPartWeRead(const ibString& key)
{
	return key == ibT("xl/workbook.xml")
		|| key == ibT("xl/_rels/workbook.xml.rels")
		|| key == ibT("xl/sharedstrings.xml")
		|| key == ibT("xl/styles.xml")
		|| key.StartsWith(ibT("xl/worksheets/"));
}

// ---------------------------------------------------------------------------
// The look of one cell, as `xl/styles.xml` states it. A cell names an index into
// `cellXfs`; the entry there names a font, a fill and a border by index of their
// own, so the part is read once into flat tables and the cells then cost a lookup.
// ---------------------------------------------------------------------------
struct XlsxStyle {
	ibFont   m_font;
	bool     m_hasFont = false;
	ibColour m_textColour;
	ibColour m_fillColour;
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
void PenOf(const ibString& name, ibSpreadsheetBorderDescription& border)
{
	border.m_width = 1;

	if (name == ibT("dotted") || name == ibT("hair"))
		border.m_style = ibPenStyle_Dot;
	else if (name == ibT("dashed"))
		border.m_style = ibPenStyle_ShortDash;
	else if (name == ibT("mediumDashed")) {
		border.m_style = ibPenStyle_ShortDash;
		border.m_width = 2;
	}
	else if (name == ibT("dashDot") || name == ibT("dashDotDot"))
		border.m_style = ibPenStyle_DotDash;
	else if (name == ibT("mediumDashDot") || name == ibT("mediumDashDotDot") || name == ibT("slantDashDot")) {
		border.m_style = ibPenStyle_DotDash;
		border.m_width = 2;
	}
	else if (name == ibT("medium") || name == ibT("double")) {
		border.m_style = ibPenStyle_Solid;
		border.m_width = 2;
	}
	else if (name == ibT("thick")) {
		border.m_style = ibPenStyle_Solid;
		border.m_width = 3;
	}
	else
		border.m_style = ibPenStyle_Solid;   // "thin", and anything a later Excel invents
}

// "FFD4E4D4" / "D4E4D4" -> a colour. An INDEXED or THEME colour is not resolved — those need the
// workbook's palette and its theme part, and a wrong colour is worse than the default one.
ibColour ColourOf(const wxXmlNode* node)
{
	if (node == nullptr)
		return ibColour();

	const ibString rgb = node->GetAttribute(wxT("rgb"), ibEmptyString);
	if (rgb.length() < 6)
		return ibColour();

	unsigned long value = 0;
	if (!rgb.Right(6).ToULong(&value, 16))
		return ibColour();

	return ibColour(static_cast<unsigned char>((value >> 16) & 0xFF),
	                static_cast<unsigned char>((value >> 8) & 0xFF),
	                static_cast<unsigned char>(value & 0xFF));
}

const wxXmlNode* ChildNamed(const wxXmlNode* parent, const ibString& name)
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
bool FontFlag(const wxXmlNode* font, const ibString& name, bool absent = false)
{
	const wxXmlNode* flag = ChildNamed(font, name);
	if (flag == nullptr)
		return absent;

	// Present and unqualified is the ordinary "on"; a value is the denial.
	const ibString value = flag->GetAttribute(wxT("val"), ibEmptyString);
	if (value.IsEmpty())
		return true;

	return !(value == ibT("0") || value.IsSameAs(ibT("false"), false));
}

// ⚠ UNDERLINE IS NOT A FLAG AT ALL — it is a STYLE with a name, and one of the names means none.
// `<u/>` is single, `<u val="double"/>` is double, and `<u val="none"/>` — what a producer writing
// everything out says for an unstyled font — is no underline whatever. The reader above cannot
// answer this one: "none" is neither 0 nor false, so it would read as switched on.
bool Underlined(const wxXmlNode* font)
{
	const wxXmlNode* flag = ChildNamed(font, ibT("u"));
	if (flag == nullptr)
		return false;

	const ibString value = flag->GetAttribute(wxT("val"), ibEmptyString);
	return value.IsEmpty() || !value.IsSameAs(ibT("none"), false);
}

std::vector<XlsxStyle> ReadStyles(const ibString& xmlText)
{
	std::vector<XlsxStyle> byIndex;
	if (xmlText.IsEmpty())
		return byIndex;

	wxStringInputStream input(xmlText);
	wxXmlDocument xml;
	if (!xml.Load(input) || xml.GetRoot() == nullptr)
		return byIndex;

	struct Fill  { ibColour m_colour; };
	struct Border { ibSpreadsheetBorderDescription m_side[4]; bool m_has[4] = { false, false, false, false }; };

	std::vector<ibFont>  fonts;
	std::vector<ibColour> fontColours;
	std::vector<Fill>    fills;
	std::vector<Border>  borders;

	for (const wxXmlNode* table = xml.GetRoot()->GetChildren(); table != nullptr; table = table->GetNext()) {
		if (table->GetName() == ibT("fonts")) {
			for (const wxXmlNode* font = table->GetChildren(); font != nullptr; font = font->GetNext()) {
				double size = 8.0;
				if (const wxXmlNode* sz = ChildNamed(font, ibT("sz")))
					sz->GetAttribute(wxT("val"), wxT("8")).ToCDouble(&size);
				ibString face;
				if (const wxXmlNode* name = ChildNamed(font, ibT("name")))
					face = name->GetAttribute(wxT("val"), ibEmptyString);

				ibFont made;
				made.m_pointSize = static_cast<int>(size + 0.5);
				made.m_faceName = face;
				made.m_weight = FontFlag(font, ibT("b")) ? ibFONTWEIGHT_BOLD : ibFONTWEIGHT_NORMAL;
				made.m_style = FontFlag(font, ibT("i")) ? ibFONTSTYLE_ITALIC : ibFONTSTYLE_NORMAL;
				made.m_underlined = Underlined(font);
				made.m_strikethrough = FontFlag(font, ibT("strike"));
				fonts.push_back(made);
				fontColours.push_back(ColourOf(ChildNamed(font, ibT("color"))));
			}
		}
		else if (table->GetName() == ibT("fills")) {
			for (const wxXmlNode* fill = table->GetChildren(); fill != nullptr; fill = fill->GetNext()) {
				Fill made;
				if (const wxXmlNode* pattern = ChildNamed(fill, ibT("patternFill")))
					if (pattern->GetAttribute(wxT("patternType"), ibEmptyString) == wxT("solid"))
						made.m_colour = ColourOf(ChildNamed(pattern, ibT("fgColor")));
				fills.push_back(made);
			}
		}
		else if (table->GetName() == ibT("borders")) {
			static const ibString sideName[4] = { ibT("left"), ibT("right"), ibT("top"), ibT("bottom") };
			for (const wxXmlNode* border = table->GetChildren(); border != nullptr; border = border->GetNext()) {
				Border made;
				for (int i = 0; i < 4; i++) {
					const wxXmlNode* side = ChildNamed(border, sideName[i]);
					if (side == nullptr)
						continue;
					const ibString line = side->GetAttribute(wxT("style"), ibEmptyString);
					if (line.IsEmpty() || line == ibT("none"))
						continue;
					made.m_has[i] = true;
					PenOf(line, made.m_side[i]);
					const ibColour colour = ColourOf(ChildNamed(side, ibT("color")));
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
		if (table->GetName() != ibT("cellXfs"))
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

			if (const wxXmlNode* align = ChildNamed(xf, ibT("alignment"))) {
				const ibString horz = align->GetAttribute(wxT("horizontal"), ibEmptyString);
				const ibString vert = align->GetAttribute(wxT("vertical"), ibEmptyString);
				if (horz == ibT("center") || horz == ibT("centerContinuous"))
					style.m_alignHorz = ibAlignmentHorz_Center;
				else if (horz == ibT("right"))  style.m_alignHorz = ibAlignmentHorz_Right;
				if (vert == ibT("center"))      style.m_alignVert = ibAlignmentVert_Center;
				else if (vert == ibT("bottom")) style.m_alignVert = ibAlignmentVert_Bottom;
				style.m_hasAlign = !horz.IsEmpty() || !vert.IsEmpty();

				// ⚠ `justify` AND `distributed` HAVE NO PLACE OF THEIR OWN HERE, and both mean the
				// text fills the cell's width — which is `wrapText` by another route, so the cell is
				// confined rather than left to spill.
				const bool spread = horz == ibT("justify") || horz == ibT("distributed")
					|| BoolOf(align, ibT("wrapText"));
				if (spread)
					// ⭐ AND IT IS `Wrap` NOW, WHICH IS WHAT THE FILE ACTUALLY SAID. This read
					// `wrapText` as `Clip` because there was no wrapping mode to read it into, so an
					// imported blank came back with its column headings cut off mid-word — the one
					// placement most of them use. The approximation is gone; the mapping is exact
					// in both directions.
					style.m_fitMode = ibSpreadsheetCellDescription::Mode_Wrap;

				// An angle back to a direction: anything that is not level is drawn vertically here.
				long rotation = 0;
				if (align->GetAttribute(wxT("textRotation"), wxT("0")).ToLong(&rotation) && rotation != 0)
					style.m_textOrient = wxVERTICAL;
			}

			if (const wxXmlNode* protect = ChildNamed(xf, ibT("protection")))
				style.m_readOnly = BoolOf(protect, ibT("locked"));

			byIndex.push_back(style);
		}
	}

	return byIndex;
}

bool ReadPackage(wxInputStream& input, ibPackage& parts)
{
	wxZipInputStream zip(input);
	if (!zip.IsOk())
		return false;

	for (;;) {
		std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry());
		if (!entry)
			break;

		const ibString name = PartKey(entry->GetInternalName());

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

bool ParseXml(const ibString& text, wxXmlDocument& xml)
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
void ReadSharedStrings(const ibPackage& parts, std::vector<ibString>& strings)
{
	const auto found = parts.find(ibT("xl/sharedstrings.xml"));
	if (found == parts.end())
		return;

	wxXmlDocument xml;
	if (!ParseXml(found->second, xml) || xml.GetRoot() == nullptr)
		return;

	for (wxXmlNode* si = xml.GetRoot()->GetChildren(); si != nullptr; si = si->GetNext()) {
		if (si->GetName() != ibT("si"))
			continue;

		ibString text;
		for (wxXmlNode* part = si->GetChildren(); part != nullptr; part = part->GetNext()) {
			if (part->GetName() == ibT("t"))
				text += part->GetNodeContent();
			else if (part->GetName() == ibT("r")) {
				for (wxXmlNode* run = part->GetChildren(); run != nullptr; run = run->GetNext())
					if (run->GetName() == ibT("t"))
						text += run->GetNodeContent();
			}
		}
		strings.push_back(text);
	}
}

// The sheets, in the order the workbook lists them — which is the order a person
// sees the tabs in, and therefore the order they must be laid out in.
void ReadSheetOrder(const ibPackage& parts, std::vector<ibString>& sheetParts)
{
	// workbook.xml gives each sheet an r:id; the rels part says which file that is.
	std::map<ibString, ibString> targetById;

	wxXmlDocument rels;
	const auto relsPart = parts.find(ibT("xl/_rels/workbook.xml.rels"));
	if (relsPart != parts.end() && ParseXml(relsPart->second, rels) && rels.GetRoot() != nullptr) {
		for (wxXmlNode* rel = rels.GetRoot()->GetChildren(); rel != nullptr; rel = rel->GetNext()) {
			const ibString id = rel->GetAttribute(wxT("Id"));
			ibString target = rel->GetAttribute(wxT("Target"));
			if (id.IsEmpty() || target.IsEmpty())
				continue;
			if (!target.StartsWith(ibT("/")))
				target = ibT("xl/") + target;
			targetById[id] = PartKey(target);
		}
	}

	wxXmlDocument workbook;
	const auto workbookPart = parts.find(ibT("xl/workbook.xml"));
	if (workbookPart != parts.end() && ParseXml(workbookPart->second, workbook) && workbook.GetRoot() != nullptr) {
		for (wxXmlNode* node = workbook.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {
			if (node->GetName() != ibT("sheets"))
				continue;
			for (wxXmlNode* sheet = node->GetChildren(); sheet != nullptr; sheet = sheet->GetNext()) {
				const ibString id = sheet->GetAttribute(wxT("r:id"), sheet->GetAttribute(wxT("id")));
				const auto target = targetById.find(id);
				if (target != targetById.end())
					sheetParts.push_back(target->second);
			}
		}
	}

	// A WORKBOOK THAT NAMES NOTHING WE CAN FOLLOW still usually has the first sheet
	// where everyone puts it. Better one sheet than an empty document.
	if (sheetParts.empty() && parts.find(ibT("xl/worksheets/sheet1.xml")) != parts.end())
		sheetParts.push_back(ibT("xl/worksheets/sheet1.xml"));
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
		deepest = std::max(deepest, at.second);

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

int ReadSheet(const ibString& partText, const std::vector<ibString>& strings,
              const std::vector<XlsxStyle>& styles, ibSpreadsheetDescription& document, int topRow, ibJournalStopwatch& parsed)
{
	wxXmlDocument xml;
	parsed.Resume();
	const bool read = ParseXml(partText, xml);
	parsed.Pause();
	if (!read || xml.GetRoot() == nullptr)
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

	if (const wxXmlNode* format = ChildNamed(xml.GetRoot(), ibT("sheetFormatPr"))) {
		double value = 0.0;
		if (format->GetAttribute(wxT("defaultRowHeight"), ibEmptyString).ToCDouble(&value) && value > 0.0)
			theirRow = value;
		if (format->GetAttribute(wxT("defaultColWidth"), ibEmptyString).ToCDouble(&value) && value > 0.0)
			theirCol = value;
	}

	const double rowScale = s_defaultRowHeight / theirRow;
	const double colScale = s_defaultColWidth / theirCol;

	for (wxXmlNode* node = xml.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {

		// --- frozen panes ----------------------------------------------------------
		// Only from the FIRST sheet: the document has one pair of frozen edges and the
		// sheets are laid one under another, so a later sheet's split would land on rows
		// that belong to the one before it.
		if (node->GetName() == ibT("sheetViews") && topRow == 0) {
			for (wxXmlNode* view = node->GetChildren(); view != nullptr; view = view->GetNext()) {
				const wxXmlNode* pane = ChildNamed(view, ibT("pane"));
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
		if (node->GetName() == ibT("rowBreaks") || node->GetName() == ibT("colBreaks")) {
			const bool rows = node->GetName() == ibT("rowBreaks");

			for (wxXmlNode* brk = node->GetChildren(); brk != nullptr; brk = brk->GetNext()) {
				long at = 0;
				if (brk->GetName() != ibT("brk")
					|| !brk->GetAttribute(wxT("id"), wxT("0")).ToLong(&at) || at <= 0)
					continue;

				if (rows) document.AddRowBrake(topRow + static_cast<int>(at) - 1);
				else if (topRow == 0) document.AddColBrake(static_cast<int>(at) - 1);
			}
			continue;
		}

		// --- column widths, and where a column sits in the outline -------------------
		if (node->GetName() == ibT("cols")) {
			for (wxXmlNode* col = node->GetChildren(); col != nullptr; col = col->GetNext()) {
				long from = 0, to = 0;
				double width = 0.0;
				if (!col->GetAttribute(wxT("min"), wxT("0")).ToLong(&from) ||
					!col->GetAttribute(wxT("max"), wxT("0")).ToLong(&to))
					continue;

				const bool sized = col->GetAttribute(wxT("width"), ibEmptyString).ToCDouble(&width) && width > 0.0;
				const bool folded = BoolOf(col, ibT("hidden"));

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

		if (node->GetName() != ibT("sheetData"))
			continue;

		// --- the cells --------------------------------------------------------------
		for (wxXmlNode* row = node->GetChildren(); row != nullptr; row = row->GetNext()) {
			if (row->GetName() != ibT("row"))
				continue;

			long rowNumber = 0;
			if (!row->GetAttribute(wxT("r"), wxT("0")).ToLong(&rowNumber) || rowNumber <= 0)
				continue;

			const int documentRow = topRow + static_cast<int>(rowNumber) - 1;
			usedRows = std::max(usedRows, static_cast<int>(rowNumber));

			// The height comes across against the sheet's own default (see `rowScale` above), and a
			// hidden row is a height of nothing, as «Hide» leaves it.
			//
			// ⚠ ONLY A HEIGHT SOMEBODY SET — `customHeight`. Excel writes `ht` on every row it has laid
			// out, the ones it fitted to their text included; read as a height of its own, each of those
			// would lose its automatic height here (spreadsheetDescription.h, HasRowSize) and stop
			// following its text the moment the workbook was opened.
			const bool folded = BoolOf(row, ibT("hidden"));

			double height = 0.0;
			if (folded)
				document.SetRowSize(documentRow, 0);
			else if (BoolOf(row, ibT("customHeight"))
				&& row->GetAttribute(wxT("ht"), ibEmptyString).ToCDouble(&height) && height > 0.0)
				document.SetRowSize(documentRow, static_cast<int>(height * rowScale + 0.5));

			long depth = 0;
			if (row->GetAttribute(wxT("outlineLevel"), wxT("0")).ToLong(&depth) && depth > 0)
				rowLevel[static_cast<int>(rowNumber) - 1] = static_cast<int>(depth);
			if (folded)
				rowHidden[static_cast<int>(rowNumber) - 1] = true;

			for (wxXmlNode* cell = row->GetChildren(); cell != nullptr; cell = cell->GetNext()) {
				if (cell->GetName() != ibT("c"))
					continue;

				int cellRow = 0, cellCol = 0;
				if (!CellAt(cell->GetAttribute(wxT("r")), cellRow, cellCol))
					continue;

				const ibString type = cell->GetAttribute(wxT("t"), wxT("n"));

				ibString value;
				for (wxXmlNode* part = cell->GetChildren(); part != nullptr; part = part->GetNext()) {
					// ⚠ <v> IS TAKEN AND <f> IS NOT. A cell may carry a formula and the
					// value the other program last computed for it; we do not evaluate
					// formulas, so the value is what a person saw there — and a formula
					// dropped into a cell as text would be a lie about what it is.
					if (part->GetName() == ibT("v"))
						value = part->GetNodeContent();
					else if (part->GetName() == ibT("is")) {
						for (wxXmlNode* inline_ = part->GetChildren(); inline_ != nullptr; inline_ = inline_->GetNext())
							if (inline_->GetName() == ibT("t"))
								value += inline_->GetNodeContent();
					}
				}

				if (type == ibT("s")) {
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
				const bool hasStyle = cell->GetAttribute(wxT("s"), ibEmptyString).ToLong(&styleAt)
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
		if (node->GetName() != ibT("mergeCells"))
			continue;

		for (wxXmlNode* merge = node->GetChildren(); merge != nullptr; merge = merge->GetNext()) {
			const ibString ref = merge->GetAttribute(wxT("ref"));
			const int colon = ref.Find(ibT(':'));
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

bool ibSheetFormatXlsx::Read(wxInputStream& input, ibSpreadsheetDescription& sheet) const
{
	// Its stages timed — what of a workbook takes the time (a debug build's journal).
	ibJournalStopwatch unpacked, said, styled, filled, parsed, handed;

	unpacked.Resume();
	ibPackage parts;
	const bool opened = ReadPackage(input, parts);
	unpacked.Pause();
	if (!opened)
		return false;

	std::vector<ibString> sheetParts;
	ReadSheetOrder(parts, sheetParts);
	if (sheetParts.empty())
		return false;   // nothing in this file is a worksheet

	said.Resume();
	std::vector<ibString> strings;
	ReadSharedStrings(parts, strings);
	said.Pause();

	styled.Resume();
	const auto stylePart = parts.find(ibT("xl/styles.xml"));
	const std::vector<XlsxStyle> styles =
		ReadStyles(stylePart != parts.end() ? stylePart->second : ibString());
	styled.Pause();

	// ⚠ FILLED INTO A DOCUMENT OF ITS OWN and handed over only once it is whole: a
	// caller that gets false must be free to keep the document it already had, and
	// a half-read workbook is worse than none.
	ibSpreadsheetDescription read;

	filled.Resume();
	int topRow = 0;
	for (size_t at = 0; at < sheetParts.size(); at++) {
		const auto part = parts.find(sheetParts[at]);
		if (part == parts.end())
			continue;

		// ⭐ THE BREAK GOES BEFORE EVERY SHEET BUT THE FIRST — that is what makes the
		// workbook's tabs into this document's pages.
		if (at > 0 && topRow > 0)
			read.AddRowBrake(topRow - 1);

		const int used = ReadSheet(part->second, strings, styles, read, topRow, parsed);
		topRow += std::max(used, 1);
	}

	// ⚠ MOVED, NOT COPIED: a copy's cell makes a font of its own (ibSpreadsheetCellDescription::SetCell) — a GDI font,
	// where this reading shares one of a style's among all its cells. A sheet of 46 thousand cells copied ran the
	// process out of them (CreateFont failed, the client hung, 2026-10-08).
	filled.Pause();
	handed.Resume();
	sheet = std::move(read);
	handed.Pause();

	ibJournalInfo(ibT("sheet"), ibT("xlsx read: %u parts unpacked %lld ms, %u strings %lld ms, %u styles %lld ms, %d rows filled %lld ms (the XML parsed %lld ms), handed %lld ms"),
		static_cast<unsigned>(parts.size()), unpacked.Ms(), static_cast<unsigned>(strings.size()), said.Ms(),
		static_cast<unsigned>(styles.size()), styled.Ms(), topRow, filled.Ms(), parsed.Ms(), handed.Ms());
	return true;
}
