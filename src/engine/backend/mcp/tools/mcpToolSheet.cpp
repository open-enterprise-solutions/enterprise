////////////////////////////////////////////////////////////////////////////
//	Description : templates — the sheet a printed form is made of
////////////////////////////////////////////////////////////////////////////
//
// ⭐ A PRINTED FORM IS NOT DRAWN, IT IS ASSEMBLED. The template is a sheet cut
// into named AREAS — a header, a row of the table, a footer — and the printing
// module puts those areas out one after another, filling their PARAMETERS as it
// goes. So the two things that matter in a template are the areas and the
// parameters; the rest is appearance.
//
// This became possible the moment the sheet stopped being an opaque blob and
// started describing itself into the node (spreadsheetDescription.cpp). Before
// that a template could be shown and stored and nothing else.
//
// ⚠ A CELL IS ADDRESSED BY ITS PLACE, not by an id. That is what a sheet IS —
// row and column are the identity — so there is nothing to hand back and nothing
// to remember between calls.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"
#include "backend/mcp/mcpClipboard.h"   // the caller's own board — copy and paste share it

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaSpreadsheetObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/sheetFormat/sheetFormat.h"   // a format registers itself and says what it reads
#include "backend/spreadsheetDescription.h"
#include "backend/typeconv.h"   // a colour from its name

namespace {

// The template a caller named, refused in words when it is something else.
ibValueMetaObjectSpreadsheetBase* FindTemplate(const ibDataNode& params, wxString& refusal)
{
	// The configuration check, the missing-argument refusal and the not-found refusal, in one
	// call — see ibMcpObjectNamed for why they belong together.
	ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
	if (object == nullptr)
		return nullptr;

	ibValueMetaObjectSpreadsheetBase* sheet =
		dynamic_cast<ibValueMetaObjectSpreadsheetBase*>(object);
	if (sheet == nullptr) {
		refusal = wxString::Format(
			ibMcpText("'%s' is not a template. Create one with metadata_create kind=Template under an "
			  "object, or kind=CommonTemplate under Common."), object->GetName());
		return nullptr;
	}

	return sheet;
}


using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The template's NodeId."), /*required*/ true);
	return s_a;
}

const ibArg& ArgArea()
{
	static const ibArg s_a(wxT("area"), ibArg::Kind::Text,
		ibMcpText("Only the cells inside this area. Omit for the whole sheet."));
	return s_a;
}

const ibArg& ArgRow()
{
	static const ibArg s_a(wxT("row"), ibArg::Kind::Whole,
		ibMcpText("Row, 1-based - the number the editor shows in the margin."), /*required*/ true);
	return s_a;
}

const ibArg& ArgCol()
{
	static const ibArg s_a(wxT("col"), ibArg::Kind::Whole,
		ibMcpText("Column, 1-based - the number the editor shows across the top."), /*required*/ true);
	return s_a;
}

// 🛑⭐ TWO NUMBERINGS MET HERE AND NOBODY TRANSLATED — the whole sheet was off by one, invisibly.
//
// These tools say "1-based" because that is what the person at the editor sees in the margin, and
// what anybody describing a blank means by "the first row". The description underneath stores rows
// and columns from ZERO, as the grid walks them. The number was handed straight across.
//
// So `sheet_cell {row: 1}` wrote the SECOND line of the sheet, every layout came out shifted a row
// down and a column right, and row 1 could not be addressed at all: a size set on it went to row 2
// and the first line kept its default forever (Max, 2026-09-02, from the screen: *"the size of the
// first row never changes… as if the numbering starts from the second cell"*).
//
// ⚠ AND IT HID BEHIND ITS OWN SYMMETRY. `sheet_get` printed the stored index under the same name,
// so writing row 1 and reading row 1 agreed perfectly - the tools were consistent with each other
// and wrong about the sheet. Only a person LOOKING at the result could see it, which is why it
// survived every check made through this door.
//
// ⭐ ONE TRANSLATION, AT THE BOUNDARY. Everything a caller sends comes through Line(); everything
// answered goes back out through LineOut(). Not twelve subtractions at twelve call sites - the
// thirteenth is what this defect is made of.
s32 Line(const ibArg& argument, const ibDataNode& params)
{
	return (s32)argument.Whole(params) - 1;
}

s64 LineOut(int stored)
{
	return (s64)stored + 1;
}

const ibArg& ArgValue()
{
	static const ibArg s_a(wxT("value"), ibArg::Kind::Text,
		ibMcpText("The text shown. For a caption this is all there is."));
	return s_a;
}

const ibArg& ArgParameter()
{
	static const ibArg s_a(wxT("parameter"), ibArg::Kind::Text,
		ibMcpText("The name the module fills in. A cell with one is a hole, not a caption."));
	return s_a;
}

const ibArg& ArgAlign()
{
	static const ibArg s_a(wxT("align"), ibArg::Kind::Text,
		ibMcpText("left, center or right."));
	return s_a;
}

const ibArg& ArgUnderline()
{
	static const ibArg s_a(wxT("underline"), ibArg::Kind::Flag,
		ibMcpText("Rule a line under the cell - this is what a blank to be filled in by hand "
			  "looks like, and what makes an empty printed form usable."));
	return s_a;
}

const ibArg& ArgColSpan()
{
	static const ibArg s_a(wxT("colSpan"), ibArg::Kind::Whole,
		ibMcpText("How many columns the cell occupies - a heading spans the page. This MERGES: the "
			  "cells it covers become part of this one."));
	return s_a;
}

const ibArg& ArgRowSpan()
{
	static const ibArg s_a(wxT("rowSpan"), ibArg::Kind::Whole,
		ibMcpText("How many rows it occupies - a tall cell for a hand-written block."));
	return s_a;
}

const ibArg& ArgItalic()
{
	static const ibArg s_a(wxT("italic"), ibArg::Kind::Flag,
		ibMcpText("For the small explanations under a line."));
	return s_a;
}

const ibArg& ArgBold()
{
	static const ibArg s_a(wxT("bold"), ibArg::Kind::Flag,
		ibMcpText("For a title or a total."));
	return s_a;
}

const ibArg& ArgFontSize()
{
	static const ibArg s_a(wxT("fontSize"), ibArg::Kind::Whole,
		ibMcpText("Point size."));
	return s_a;
}

const ibArg& ArgValign()
{
	static const ibArg s_a(wxT("valign"), ibArg::Kind::Text,
		ibMcpText("top, middle or bottom - which matters the moment a row is tall."));
	return s_a;
}

const ibArg& ArgVertical()
{
	static const ibArg s_a(wxT("vertical"), ibArg::Kind::Flag,
		ibMcpText("Turn the text on its side - a narrow column heading in a wide table."));
	return s_a;
}

const ibArg& ArgBackground()
{
	static const ibArg s_a(wxT("background"), ibArg::Kind::Text,
		ibMcpText("Fill colour, as a name or #rrggbb. Unset means unset, and the platform "
			  "resolves it where there is a screen to ask."));
	return s_a;
}

const ibArg& ArgColour()
{
	static const ibArg s_a(wxT("colour"), ibArg::Kind::Text,
		ibMcpText("Text colour, same form."));
	return s_a;
}

const ibArg& ArgBorder()
{
	static const ibArg s_a(wxT("border"), ibArg::Kind::Text,
		ibMcpText("Rule lines on named sides: any of left, right, top, bottom, or `all`. "
			  "`underline` is the common case of this and stays as its own argument."));
	return s_a;
}

const ibArg& ArgFit()
{
	static const ibArg s_a(wxT("fit"), ibArg::Kind::Text,
		ibMcpText("What long text does: overflow, clip, or ellipsis. On paper this decides "
			  "whether a name runs into the next cell or is cut."));
	return s_a;
}

const ibArg& ArgReadOnly()
{
	static const ibArg s_a(wxT("readOnly"), ibArg::Kind::Flag,
		ibMcpText("The cell cannot be typed into when the sheet is shown."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("What the module will call it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgStart()
{
	static const ibArg s_a(wxT("start"), ibArg::Kind::Whole,
		ibMcpText("First row, 1-based."), /*required*/ true);
	return s_a;
}

const ibArg& ArgEnd()
{
	static const ibArg s_a(wxT("end"), ibArg::Kind::Whole,
		ibMcpText("Last row, inclusive."), /*required*/ true);
	return s_a;
}

const ibArg& ArgFile()
{
	static const ibArg s_a(wxT("file"), ibArg::Kind::Text,
		ibMcpText("Full path to the file. The format is decided by its extension."), /*required*/ true);
	return s_a;
}

const ibArg& ArgSize()
{
	static const ibArg s_a(wxT("size"), ibArg::Kind::Whole,
		ibMcpText("The measurement. Omit or 0 puts the band back to the platform's default."));
	return s_a;
}

const ibArg& ArgHide()
{
	static const ibArg s_a(wxT("hide"), ibArg::Kind::Flag,
		ibMcpText("Hide the band - a width of nothing, which is what hidden IS on a sheet."));
	return s_a;
}

const ibArg& ArgWhat()
{
	static const ibArg s_a(wxT("what"), ibArg::Kind::Text,
		ibMcpText("A page `break`, a `freeze` line that holds rows in place while the rest scrolls, "
			  "or a `group` that can be collapsed."),
			/*required*/ true, { wxT("break"), wxT("freeze"), wxT("group") });
	return s_a;
}

const ibArg& ArgColumns()
{
	static const ibArg s_a(wxT("columns"), ibArg::Kind::Flag,
		ibMcpText("Apply it to columns instead of rows - a vertical page break, a frozen left "
			  "edge, a column group."));
	return s_a;
}

const ibArg& ArgAt()
{
	static const ibArg s_a(wxT("at"), ibArg::Kind::Whole,
		ibMcpText("For a break: the row or column the page ends before. For a freeze: everything "
			  "up to here stays put; 0 unfreezes."));
	return s_a;
}

const ibArg& ArgLevel()
{
	static const ibArg s_a(wxT("level"), ibArg::Kind::Whole,
		ibMcpText("For a group: how deep it nests. Default 1."));
	return s_a;
}

const ibArg& ArgCollapsed()
{
	static const ibArg s_a(wxT("collapsed"), ibArg::Kind::Flag,
		ibMcpText("For a group: start folded. This is a state of the DOCUMENT, so a long form can "
			  "open readable."));
	return s_a;
}

const ibArg& ArgRemove()
{
	static const ibArg s_a(wxT("remove"), ibArg::Kind::Flag,
		ibMcpText("Take it off instead of putting it on. For a break, `at` says which one; for a "
			  "freeze this is the same as at=0; for a group, `start` and `end` say which."));
	return s_a;
}

const ibArg& ArgTop()
{
	static const ibArg s_a(wxT("top"), ibArg::Kind::Whole,
		ibMcpText("First row of the rectangle."), /*required*/ true);
	return s_a;
}

const ibArg& ArgLeft()
{
	static const ibArg s_a(wxT("left"), ibArg::Kind::Whole,
		ibMcpText("First column."), /*required*/ true);
	return s_a;
}

const ibArg& ArgBottom()
{
	static const ibArg s_a(wxT("bottom"), ibArg::Kind::Whole,
		ibMcpText("Last row, inclusive."), /*required*/ true);
	return s_a;
}

const ibArg& ArgRight()
{
	static const ibArg s_a(wxT("right"), ibArg::Kind::Whole,
		ibMcpText("Last column, inclusive."), /*required*/ true);
	return s_a;
}

const ibArg& ArgSlot()
{
	static const ibArg s_a(wxT("slot"), ibArg::Kind::Text,
		ibMcpText("Which buffer to put it in. Omit for the usual one."));
	return s_a;
}

const ibArg& ArgTimes()
{
	static const ibArg s_a(wxT("times"), ibArg::Kind::Whole,
		ibMcpText("Lay it down more than once, each block below the last - so a band that repeats "
			  "is one call. Default 1."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// sheet_get
//---------------------------------------------------------------------------
class ibMcpToolSheetGet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_get"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("reading the template '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("A template as it stands: its named AREAS - which is what a printing module "
			"puts out - and every cell that has something in it, with the PARAMETER each "
			"substitutes and the FORMATTING it carries (merge, alignment, font, colours, a "
			"ruled line). Ask before filling one in: an area is the unit a report prints, and "
			"reading a cell back is how you check that what you wrote is what it got.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgArea() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const ibSpreadsheetDescription& desc = sheet->GetSpreadsheetDesc();

		// ⭐ HOW BIG IT IS, AND HOW MUCH OF IT FITS — the questions anything
		// generating a sheet has to ask and cannot answer for itself.
		//
		// A module putting an area out per row needs to know where it has got to;
		// a form being laid out from paper needs to know whether it still fits on
		// the page. Both are readings the sheet already keeps and nothing was
		// handing over: the extent it occupies, and the page breaks that cut it.
		result.AddField(wxT("rows"), ibDataValue::Int((s64)desc.GetNumberRows()));
		result.AddField(wxT("cols"), ibDataValue::Int((s64)desc.GetNumberCols()));

		// PAGES, counted the only way a sheet can count them: by the breaks it
		// declares. A break says where the paper ends deliberately; without one
		// the answer is one page as far as the DOCUMENT is concerned, and where it
		// really ends is the printer's business.
		const int rowBreaks = desc.GetBrakeNumberRows();
		const int colBreaks = desc.GetBrakeNumberCols();
		result.AddField(wxT("pagesDown"), ibDataValue::Int((s64)(rowBreaks + 1)));
		result.AddField(wxT("pagesAcross"), ibDataValue::Int((s64)(colBreaks + 1)));

		// ⭐ AND WHERE THEY ARE, not only how many. A count says a break exists and leaves the one
		// question a caller actually has — which row, so it can be moved or taken off — answerable
		// only by guessing. Same rule as the cell formatting below: whatever can be written has to
		// be readable back, or the writer has no way to check itself.
		if (rowBreaks > 0 || colBreaks > 0) {

			ibDataNode& breaks = result.Child(wxT("breaks"));

			std::vector<ibDataValue> down, across;
			for (int idx = 0; idx < rowBreaks; ++idx)
				down.push_back(ibDataValue::Int((s64)desc.GetRowBrakeByIdx(idx)));
			for (int idx = 0; idx < colBreaks; ++idx)
				across.push_back(ibDataValue::Int((s64)desc.GetColBrakeByIdx(idx)));

			breaks.AddField(wxT("rows"), ibDataValue::Array(down));
			breaks.AddField(wxT("cols"), ibDataValue::Array(across));
		}

		if (desc.GetRowFreeze() > 0 || desc.GetColFreeze() > 0) {
			ibDataNode& freeze = result.Child(wxT("freeze"));
			freeze.AddField(wxT("rows"), ibDataValue::Int((s64)desc.GetRowFreeze()));
			freeze.AddField(wxT("cols"), ibDataValue::Int((s64)desc.GetColFreeze()));
		}

		// THE AREAS FIRST, because they are what a module names. A template with no
		// area can be printed only whole, which is rarely what anybody wants.
		std::vector<ibDataValue> areas;
		int wantedStart = -1, wantedEnd = -1;
		const wxString wanted = ArgArea().Text(params);

		for (int idx = 0; idx < desc.GetAreaNumberRows(); ++idx) {

			const ibSpreadsheetAreaDescription* area = desc.GetRowAreaByIdx(idx);
			if (area == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("name"), area->m_label);
			node->AddField(wxT("start"), ibDataValue::Int(LineOut(area->m_start)));
			node->AddField(wxT("end"), ibDataValue::Int(LineOut(area->m_end)));

			// HOW TALL IT IS, since that is what decides how many of it fit. A
			// module repeating a one-row band and a module repeating a five-row one
			// have very different answers to "how many go on a page".
			node->AddField(wxT("height"),
				ibDataValue::Int((s64)(area->m_end - area->m_start + 1)));

			areas.push_back(ibDataValue::Child(node));

			if (!wanted.IsEmpty() && area->m_label.IsSameAs(wanted, false)) {
				wantedStart = (int)area->m_start;
				wantedEnd = (int)area->m_end;
			}
		}

		result.AddField(wxT("areas"), ibDataValue::Array(areas));

		if (!wanted.IsEmpty() && wantedStart < 0) {
			refusal = wxString::Format(
				ibMcpText("'%s' has no area called '%s'."), sheet->GetName(), wanted);
			return false;
		}

		std::vector<ibDataValue> cells;
		for (int idx = 0; idx < desc.GetCellCount(); ++idx) {

			const ibSpreadsheetCellDescription* cell = desc.GetCellByIdx(idx);
			if (cell == nullptr)
				continue;

			if (wantedStart >= 0
				&& ((int)cell->m_row < wantedStart || (int)cell->m_row > wantedEnd))
				continue;

			// ⭐⭐ THE CELL SAYS ITSELF, and this asks the one that already does it.
			//
			// ibSpreadsheetCellDescriptionMemory::WriteNode is how a cell is written to a FILE —
			// value, parameter, alignment, orientation, font, colours, borders, span, everything,
			// and only what differs from the default. Its own header states the rule this file was
			// breaking: *a part that cannot say what it is forces its container to know, and the
			// container then has to be edited every time the part gains a field.*
			//
			// 🛑 SO THERE WERE TWO DESCRIBERS. `DescribeFormatting` here was seventy-eight lines
			// re-deriving the same facts from the cell's fields — and it had already fallen behind
			// in the small way that shape always falls behind: three of its defaults are not the
			// zero, each one found by being wrong first, and its own comments say so. A field added
			// to a cell reached the file and never reached a caller.
			//
			// The one difference the copy bought was words for three enumerators (`center`,
			// `right`, `clip`) where the format writes numbers, and it could not even do that
			// completely — it carried `alignRaw` / `valignRaw` escapes for the values its table did
			// not know. Being told what is stored is better than being told a word that is
			// sometimes missing.
			// ⚠ AND IT SAYS WHETHER IT COULD. A cell that failed to describe itself would otherwise
			// arrive as an address with nothing in it — indistinguishable from an empty cell, which
			// is the one thing this reading exists to tell apart.
			ibDataValue described;
			if (!ibSpreadsheetCellDescriptionMemory::WriteNode(described, *cell)
				|| described.Kind() != ibDataKind::Child) {
				refusal = wxString::Format(
					ibMcpText("The cell at row %i, column %i could not describe itself."),
					(int)LineOut(cell->m_row), (int)LineOut(cell->m_col));
				return false;
			}

			std::shared_ptr<ibDataNode> node = described.AsChild();

			// The ADDRESS is the container's to say — the cell knows what it holds, not where it
			// sits, which is exactly the division WriteNode draws.
			node->AddField(wxT("row"), ibDataValue::Int(LineOut(cell->m_row)));
			node->AddField(wxT("col"), ibDataValue::Int(LineOut(cell->m_col)));

			// ⭐ THE FILTER FOLLOWS THE REPORTER. A cell with nothing to say is one
			// the node has nothing in beyond its address — so the test is asked of
			// the node that was just built, not of a hand-kept list of what counts
			// as content.
			//
			// It used to be that list ("no value and no parameter"), and it hid
			// exactly the cells this reading was extended for: a ruled blank line
			// carries no text at all, and every one of them was dropped from the
			// answer while sitting plainly on the screen.
			if (node->Fields().size() <= 2)
				continue;

			cells.push_back(ibDataValue::Child(node));
		}

		result.AddField(wxT("cells"), ibDataValue::Array(cells));

		if (areas.empty())
			result.SetValue(wxT("note"),
				ibMcpText("No areas declared. A printing module names areas to put out, so a template "
				  "without them can only be printed whole."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetGet);

//---------------------------------------------------------------------------
// sheet_cell
//---------------------------------------------------------------------------
class ibMcpToolSheetCell : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_cell"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("writing a cell of the template '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Put text or a PARAMETER into one cell of a template. A parameter is what the "
			"printing module substitutes when it puts the area out - the whole reason a "
			"template is not a picture.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgRow(), ArgCol(), ArgValue(), ArgParameter(), ArgAlign(), ArgUnderline(), ArgColSpan(), ArgRowSpan(), ArgItalic(), ArgBold(), ArgFontSize(), ArgValign(), ArgVertical(), ArgBackground(), ArgColour(), ArgBorder(), ArgFit(), ArgReadOnly() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const s32 row = Line(ArgRow(), params);
		const s32 col = Line(ArgCol(), params);

		if (row < 0 || col < 0) {
			refusal = ibMcpText("A cell is addressed by row and column, both 1 or more.");
			return false;
		}

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		ibSpreadsheetCellDescription* cell = desc.GetOrCreateCell(row, col);
		if (cell == nullptr) {
			refusal = ibMcpText("That cell could not be made.");
			return false;
		}

		if (params.FindField(ArgValue().Name()) != nullptr)
			cell->SetValue(ArgValue().Text(params));
		if (params.FindField(ArgParameter().Name()) != nullptr)
			cell->SetParameter(ArgParameter().Text(params));

		const wxString align = ArgAlign().Text(params);
		if (!align.IsEmpty()) {

			// ⚠ THE SHEET'S OWN ENUM, not the wx constant that looks like it.
			// `ibAlignmentHorz_Center` is wxALIGN_CENTER (0x0900); writing
			// wxALIGN_CENTER_HORIZONTAL (0x0100) stored a number the property has
			// no name for — so the inspector showed the alignment as EMPTY and the
			// text stayed left. Right-looking, silently wrong, and found only by
			// looking at the screen.
			int horizontal = ibAlignmentHorz_Left;
			if (align.IsSameAs(wxT("center"), false))     horizontal = ibAlignmentHorz_Center;
			else if (align.IsSameAs(wxT("right"), false)) horizontal = ibAlignmentHorz_Right;
			else if (!align.IsSameAs(wxT("left"), false)) {
				refusal = ibMcpText("Alignment is left, center or right.");
				return false;
			}

			cell->m_alignHorz = horizontal;
		}

		// ⭐ THE RULED LINE IS THE BLANK. Drawn as the cell's bottom border, which
		// is what it is on paper — not a row of underscores, which would be text
		// the module would have to print around.
		if (ArgUnderline().Flag(params)) {
			cell->m_borderAt[3].m_style = wxPENSTYLE_SOLID;
			cell->m_borderAt[3].m_width = 1;
			cell->m_borderAt[3].m_colour = *wxBLACK;
		}

		// ⚠ MERGING IS NOT SETTING A NUMBER. SetCellSize is the grid's own mechanism:
		// it releases whatever the old span held and marks every covered cell with
		// the offset back to its owner. Writing m_col_size straight left those
		// cells believing they were their own — which draws right and behaves
		// wrongly the moment anything walks the sheet.
		const s32 colSpan = (s32)ArgColSpan().Whole(params);
		const s32 rowSpan = (s32)ArgRowSpan().Whole(params);

		if (colSpan > 1 || rowSpan > 1)
			desc.SetCellSize(row, col, rowSpan > 1 ? rowSpan : 1, colSpan > 1 ? colSpan : 1);

		// THE FONT IS ONE OBJECT, so every question about it is answered on one
		// copy and written back once — three separate reads would each start from
		// the stored font and the last would win.
		if (params.FindField(ArgItalic().Name()) != nullptr
			|| params.FindField(ArgBold().Name()) != nullptr
			|| params.FindField(ArgFontSize().Name()) != nullptr) {

			wxFont font = cell->m_font;

			if (params.FindField(ArgItalic().Name()) != nullptr)
				font.SetStyle(ArgItalic().Flag(params)
					? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL);

			if (params.FindField(ArgBold().Name()) != nullptr)
				font.SetWeight(ArgBold().Flag(params)
					? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);

			const s32 points = (s32)ArgFontSize().Whole(params);
			if (points > 0)
				font.SetPointSize(points);

			cell->m_font = font;
		}

		const wxString valign = ArgValign().Text(params);
		if (!valign.IsEmpty()) {

			// The same rule as above: the sheet's own names, not the wx ones that
			// resemble them.
			if (valign.IsSameAs(wxT("top"), false))         cell->m_alignVert = ibAlignmentVert_Top;
			else if (valign.IsSameAs(wxT("middle"), false)) cell->m_alignVert = ibAlignmentVert_Center;
			else if (valign.IsSameAs(wxT("bottom"), false)) cell->m_alignVert = ibAlignmentVert_Bottom;
			else {
				refusal = ibMcpText("Vertical alignment is top, middle or bottom.");
				return false;
			}
		}

		if (params.FindField(ArgVertical().Name()) != nullptr)
			cell->m_textOrient = ArgVertical().Flag(params)
				? wxVERTICAL : wxHORIZONTAL;

		// ⚠ AN UNSET COLOUR IS UNSET — the cell says so deliberately, and the
		// getters resolve it where there IS a screen to ask. So a colour is only
		// written when one was given, never defaulted here.
		const wxString background = ArgBackground().Text(params);
		if (!background.IsEmpty())
			cell->m_backgroundColour = typeConv::StringToColour(background);

		const wxString colour = ArgColour().Text(params);
		if (!colour.IsEmpty())
			cell->m_textColour = typeConv::StringToColour(colour);

		const wxString border = ArgBorder().Text(params);
		if (!border.IsEmpty()) {

			const bool all = border.IsSameAs(wxT("all"), false);
			const bool sides[4] = {
				all || border.Lower().Contains(wxT("left")),
				all || border.Lower().Contains(wxT("right")),
				all || border.Lower().Contains(wxT("top")),
				all || border.Lower().Contains(wxT("bottom")),
			};

			for (int side = 0; side < 4; ++side) {
				if (!sides[side])
					continue;
				cell->m_borderAt[side].m_style = wxPENSTYLE_SOLID;
				cell->m_borderAt[side].m_width = 1;
				cell->m_borderAt[side].m_colour = *wxBLACK;
			}
		}

		const wxString fit = ArgFit().Text(params);
		if (!fit.IsEmpty()) {

			// ⚠ AND HERE THE CELL'S NESTED ENUM IS THE RIGHT ONE — the sheet-level
			// ibSpreadsheetFitMode numbers differently (Overflow = 4). Two enums
			// that mean the same thing and count apart is exactly the trap the
			// alignment fell into, so the class that OWNS the member decides.
			if (fit.IsSameAs(wxT("overflow"), false))
				cell->m_fitMode = ibSpreadsheetCellDescription::Mode_Overflow;
			else if (fit.IsSameAs(wxT("clip"), false))
				cell->m_fitMode = ibSpreadsheetCellDescription::Mode_Clip;
			else if (fit.IsSameAs(wxT("ellipsis"), false))
				cell->m_fitMode = ibSpreadsheetCellDescription::Mode_EllipsizeEnd;
			else {
				refusal = ibMcpText("Fit is overflow, clip or ellipsis.");
				return false;
			}
		}

		if (params.FindField(ArgReadOnly().Name()) != nullptr)
			cell->m_isReadOnly = ArgReadOnly().Flag(params);

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.AddField(wxT("row"), ibDataValue::Int(LineOut(row)));
		result.AddField(wxT("col"), ibDataValue::Int(LineOut(col)));
		if (!cell->GetValue().IsEmpty())
			result.SetValue(wxT("value"), cell->GetValue());
		if (!cell->GetParameter().IsEmpty())
			result.SetValue(wxT("parameter"), cell->GetParameter());

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetCell);

//---------------------------------------------------------------------------
// sheet_area
//---------------------------------------------------------------------------
class ibMcpToolSheetArea : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_area"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("declaring the area '%s' in '%s'"),
			ArgName().Text(params), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Name a band of rows as an AREA - the unit a printing module puts out. A "
			"header, a table row, a footer: the module names these, so a template without "
			"them can only be printed whole.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgName(), ArgStart(), ArgEnd() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const wxString name = ArgName().Text(params);
		const s32 start = Line(ArgStart(), params);
		const s32 end = Line(ArgEnd(), params);

		if (name.IsEmpty()) {
			refusal = ibMcpText("An area needs a name - that is what the module puts out.");
			return false;
		}

		// A BAND THAT ENDS BEFORE IT BEGINS is not a narrow area, it is a mistake,
		// and it would print nothing while looking declared.
		if (start < 0 || end < start) {
			refusal = ibMcpText("An area runs from a first row to a last one, both 1 or more.");
			return false;
		}

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		for (int idx = 0; idx < desc.GetAreaNumberRows(); ++idx) {
			const ibSpreadsheetAreaDescription* area = desc.GetRowAreaByIdx(idx);
			if (area != nullptr && area->m_label.IsSameAs(name, false)) {
				refusal = wxString::Format(
					ibMcpText("'%s' already has an area called '%s'."), sheet->GetName(), name);
				return false;
			}
		}

		desc.AddRowArea(name, start, end);

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.AddField(wxT("added"), ibDataValue::Bool(true));
		result.SetValue(wxT("name"), name);
		result.AddField(wxT("start"), ibDataValue::Int(LineOut(start)));
		result.AddField(wxT("end"), ibDataValue::Int(LineOut(end)));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetArea);

//---------------------------------------------------------------------------
// sheet_import
//---------------------------------------------------------------------------
//
// ⭐ A PRINTED FORM USUALLY EXISTS BEFORE THE CONFIGURATION DOES. Somebody has an
// invoice in Excel, a contract in Word, an act the accountant has been filling in
// by hand for years — and the job is not to draw it again, it is to turn THAT
// into a template: the same layout, with the changing parts made into parameters
// and the repeating band made into an area.
//
// So this is the first half of that: read the file as it stands. What comes in is
// a sheet like any other — cells, fonts, borders, merges — and everything after
// it (sheet_area, sheet_cell) is the ordinary way to cut it into parts.
//
// THE FORMAT IS FOUND, NOT NAMED. A format registers itself and says which
// extension it answers to; asking here means a format added tomorrow is readable
// the day it registers, and an unreadable extension is refused WITH the list of
// the ones that are.
//
class ibMcpToolSheetImport : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_import"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("importing a file into the template '%s'"),
			ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Read a spreadsheet or document file INTO a template - an existing invoice, "
			"act or contract becomes the layout, and what changes in it becomes parameters "
			"afterwards with sheet_cell. This is how a printed form that already exists on "
			"paper gets into the configuration without being drawn again.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgFile() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const wxString fileName = ArgFile().Text(params);

		if (!wxFileExists(fileName)) {
			refusal = wxString::Format(ibMcpText("There is no file at '%s'."), fileName);
			return false;
		}

		const ibSheetFormat* format = ibSheetFormatFor(fileName);
		if (format == nullptr || !format->CanRead()) {

			// The list is the registry's, so it grows by itself.
			wxString readable;
			for (const ibSheetFormat* known : ibSheetFormats()) {
				if (known == nullptr || !known->CanRead())
					continue;
				readable << (readable.IsEmpty() ? wxT("") : wxT(", ")) << known->GetExtension();
			}

			refusal = wxString::Format(
				ibMcpText("Nothing here reads that kind of file. Readable: %s."),
				readable.IsEmpty() ? wxT("none") : readable);
			return false;
		}

		ibSpreadsheetDescription desc;
		if (!format->Read(fileName, desc)) {
			refusal = wxString::Format(
				ibMcpText("'%s' could not read that file - it may be damaged, or written by something "
				  "this format does not follow."), format->GetName());
			return false;
		}

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.AddField(wxT("imported"), ibDataValue::Bool(true));
		result.SetValue(wxT("format"), format->GetName());
		result.AddField(wxT("cells"), ibDataValue::Int((s64)desc.GetCellCount()));
		result.AddField(wxT("areas"), ibDataValue::Int((s64)desc.GetAreaNumberRows()));

		// ⚠ WHAT CAME IN IS A PICTURE UNTIL IT IS CUT UP. A foreign file has no
		// areas and no parameters — it is one flat sheet — so the next two steps
		// are what turn it into a printed FORM rather than a copy of one.
		result.SetValue(wxT("nextStep"),
			ibMcpText("Read it with sheet_get, then declare the bands with sheet_area and turn the "
			  "changing cells into parameters with sheet_cell."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetImport);

//---------------------------------------------------------------------------
// sheet_size
//---------------------------------------------------------------------------
//
// ⭐ A MEASUREMENT BELONGS TO THE BAND, NOT TO THE CELL. A row has one height and
// a column one width however many cells stand in them — so this is a verb of its
// own rather than two more arguments on sheet_cell, where they would be repeated
// per cell and could disagree.
//
// WHY IT MATTERS FOR A PRINTED FORM. A layout copied from paper has the right
// content and the wrong proportions until the bands are set: a heading block that
// should occupy the right half of the page, a signature line of a definite
// length, a row left tall enough to be written in by hand. Nothing else expresses
// those.
//
// ⚠ HIDING IS A WIDTH OF ZERO — not a flag of its own. A band nobody can see and
// a band with no width are the same thing to a sheet, and inventing a second way
// to say it would make "hidden but wide" expressible and meaningless.
//
class ibMcpToolSheetSize : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_size"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("sizing a band of the template '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Set the HEIGHT of a row or the WIDTH of a column, or hide one by giving it "
			"nothing. A layout taken from paper is right in its content and wrong in its "
			"proportions until these are set.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgRow(), ArgCol(), ArgSize(), ArgHide() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		// ⚠ ASKED IN THE CALLER'S OWN NUMBERS FIRST, because here a zero means "not said" rather
		// than a band: the choice between a row and a column is made on what ARRIVED, and only
		// then is the one that was named translated into the sheet's own numbering.
		const s32 rowSaid = (s32)ArgRow().Whole(params);
		const s32 colSaid = (s32)ArgCol().Whole(params);

		// ONE BAND AT A TIME. Accepting both would leave a caller unsure which
		// measurement the number was, and one of the two silently ignored.
		if ((rowSaid > 0) == (colSaid > 0)) {
			refusal = ibMcpText("Name a row or a column - one of the two.");
			return false;
		}

		const s32 row = rowSaid - 1;
		const s32 col = colSaid - 1;

		s32 size = (s32)ArgSize().Whole(params);
		if (ArgHide().Flag(params))
			size = 1;   // the smallest a band can be and still be a band

		if (size < 0) {
			refusal = ibMcpText("A size is 0 (the default) or more.");
			return false;
		}

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		if (rowSaid > 0) desc.SetRowSize(row, size);
		else             desc.SetColSize(col, size);

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.SetValue(wxT("band"), wxString(rowSaid > 0 ? wxT("row") : wxT("column")));
		result.AddField(wxT("at"), ibDataValue::Int((s64)(rowSaid > 0 ? rowSaid : colSaid)));
		result.AddField(wxT("size"), ibDataValue::Int((s64)size));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetSize);

//---------------------------------------------------------------------------
// sheet_band
//---------------------------------------------------------------------------
//
// ⭐ THE THINGS A SHEET SAYS ABOUT WHOLE BANDS, in one verb because they are one
// idea: a row or a column, and something declared about it that is not its
// contents — where the page breaks, what stays put while the rest scrolls, and
// which stretch folds into which.
//
// PAGE BREAKS ARE HORIZONTAL AND VERTICAL, and both are the same statement made
// about a different band: "the paper ends here". A printed form of any length
// needs them, and nothing else expresses where a page is meant to end rather
// than where it happens to.
//
// FREEZING IS NOT A BREAK even though both name a line. A break says where the
// paper ends; a freeze says what stays on screen — one is about printing and the
// other about reading, and merging them would make "frozen at the page break"
// look like a single fact.
//
// ⚠ GROUPS WERE NEVER STORED until today (see spreadsheetDescription.cpp): an
// outline a person folded lived in memory and was gone on the next open. Both
// halves — the store and this door — landed together, because either alone is
// useless.
//
class ibMcpToolSheetBand : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_band"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString what = ArgWhat().Text(params);
		return wxString::Format(ibMcpText("setting %s in the template '%s'"),
			what.IsEmpty() ? ibMcpText("a band") : what, ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("What a template says about whole rows or columns: a page BREAK (where the "
			"paper ends, horizontally or vertically), a FREEZE (what stays put while the rest "
			"scrolls), or a GROUP (a stretch that folds, so a long form stays readable).");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgWhat(), ArgColumns(), ArgAt(), ArgStart(), ArgEnd(), ArgLevel(), ArgCollapsed(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const wxString what = ArgWhat().Text(params);
		const bool columns = ArgColumns().Flag(params);
		const bool remove = ArgRemove().Flag(params);

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		if (what.IsSameAs(wxT("break"), false)) {

			const s32 at = Line(ArgAt(), params);
			if (at < 0) {
				refusal = ibMcpText("A page break is placed at a row or column, 1 or more.");
				return false;
			}

			if (remove) {
				if (columns) desc.DeleteColBrake(at);
				else         desc.DeleteRowBrake(at);
			}
			else {
				if (columns) desc.AddColBrake(at);
				else         desc.AddRowBrake(at);
			}

			result.SetValue(wxT("what"), wxString(wxT("break")));
			result.SetValue(wxT("did"), wxString(remove ? wxT("removed") : wxT("placed")));
			result.AddField(wxT("at"), ibDataValue::Int(LineOut(at)));
		}
		else if (what.IsSameAs(wxT("freeze"), false)) {

			// 0 IS A LEGITIMATE ANSWER HERE and means "nothing is frozen" — which
			// is why this one does not refuse it the way a break does. `remove` is
			// therefore the same statement said the other way, and is accepted for
			// the sake of one spelling across all three rather than because the
			// freeze needed it.
			//
			// ⚠ AND IT IS A COUNT, NOT AN ADDRESS — "how many bands stay put", which is why it is
			// NOT translated the way a break or a group is: freezing 2 rows freezes rows 1 and 2,
			// and subtracting one here would freeze the wrong number of them.
			const s32 at = remove ? 0 : (s32)ArgAt().Whole(params);
			if (at < 0) {
				refusal = ibMcpText("A freeze runs up to a row or column, or 0 for none.");
				return false;
			}

			if (columns) desc.SetColFreeze(at);
			else         desc.SetRowFreeze(at);

			result.SetValue(wxT("what"), wxString(wxT("freeze")));
			result.AddField(wxT("at"), ibDataValue::Int((s64)at));
		}
		else if (what.IsSameAs(wxT("group"), false)) {

			const s32 start = Line(ArgStart(), params);
			const s32 end = Line(ArgEnd(), params);

			// A GROUP THAT ENDS BEFORE IT BEGINS folds nothing while looking
			// declared — the same mistake an area can make, refused the same way.
			if (start < 0 || end < start) {
				refusal = ibMcpText("A group runs from a first band to a last one, both 1 or more.");
				return false;
			}

			s32 level = (s32)ArgLevel().Whole(params);
			if (level <= 0)
				level = 1;

			const bool collapsed = ArgCollapsed().Flag(params);

			// Matched by the pair that IS the group — level and collapsed are what
			// it carries, not what names it.
			if (remove) {
				if (columns) desc.DeleteColGroup(start, end);
				else         desc.DeleteRowGroup(start, end);
			}
			else {
				if (columns) desc.AddColGroup(start, end, level, collapsed);
				else         desc.AddRowGroup(start, end, level, collapsed);
			}

			result.SetValue(wxT("what"), wxString(wxT("group")));
			result.SetValue(wxT("did"), wxString(remove ? wxT("removed") : wxT("placed")));
			result.AddField(wxT("start"), ibDataValue::Int(LineOut(start)));
			result.AddField(wxT("end"), ibDataValue::Int(LineOut(end)));
			if (!remove)
				result.AddField(wxT("level"), ibDataValue::Int((s64)level));
		}
		else {
			// An unknown word must not fall through to a default: a misspelling
			// would then quietly do something else.
			refusal = ibMcpText("Unknown. Use break, freeze or group.");
			return false;
		}

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.SetValue(wxT("band"), wxString(columns ? wxT("columns") : wxT("rows")));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetBand);

//---------------------------------------------------------------------------
// sheet_copy / sheet_paste
//---------------------------------------------------------------------------
//
// ⭐ A RECTANGLE, NOT A SELECTION. The grid editor has a copy of its own
// (gridEditorClipbrd.cpp), but it reads whatever the mouse has highlighted and
// writes undo commands as it goes — none of which is reachable without an open
// editor window, and none of which is wanted here. A caller building a template
// says WHICH rectangle in words.
//
// So this pair does not go near the grid. It reads cells out of the description
// and writes them back at an offset, which is all a copy of cells is once the
// selection and the undo stack are somebody else's concern.
//
// ⭐ THE CELL COPIES ITSELF. ibSpreadsheetCellDescription::SetCell takes every
// field EXCEPT row and column — the two are commented out at the point of the
// copy, deliberately — so "the content moves, the address stays" is already the
// meaning of the assignment. The offsets travel beside the cell instead, the way
// the editor's own clipboard carries them.
//

class ibMcpToolSheetCopy : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_copy"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("copying cells of the template '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Copy a rectangle of cells into the caller's own buffer, to be laid down "
			"again elsewhere with sheet_paste. Everything a cell carries comes with it - the "
			"text or parameter, the font and colours, the borders, the alignment, the merge "
			"size. Use it to repeat a band rather than describing the same cells twice.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgTop(), ArgLeft(), ArgBottom(), ArgRight(), ArgSlot() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const s32 top = Line(ArgTop(), params);
		const s32 left = Line(ArgLeft(), params);
		const s32 bottom = Line(ArgBottom(), params);
		const s32 right = Line(ArgRight(), params);

		// A RECTANGLE THAT ENDS BEFORE IT BEGINS copies nothing while looking
		// asked-for — refused the same way an area and a group are.
		if (top < 0 || left < 0 || bottom < top || right < left) {
			refusal = ibMcpText("A rectangle runs from a top-left cell to a bottom-right one, "
				"all 1 or more.");
			return false;
		}

		const ibSpreadsheetDescription& desc = sheet->GetSpreadsheetDesc();

		std::vector<ibDataValue> cells;

		for (s32 row = top; row <= bottom; row++) {
			for (s32 col = left; col <= right; col++) {

				const ibSpreadsheetCellDescription* cell = desc.GetCell(row, col);

				// AN EMPTY CELL IS NOT COPIED, which is not the same as copying
				// a blank one: pasting the rectangle then leaves whatever stands
				// under those places alone. That is what "copy these three
				// headings" means; a caller who wants the blanks laid down too
				// gives them a cell of their own first.
				if (cell == nullptr)
					continue;

				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

				// ⚠ s32, NOT s64. SetValue dispatches through ibDataCodec<T> and
				// only s32 has a specialization — an s64 fails to compile inside
				// dataBuilder.h, nowhere near the line that asked for it.
				entry->SetValue(wxT("row"), (s32)(row - top));
				entry->SetValue(wxT("col"), (s32)(col - left));

				ibDataValue body;
				if (!ibSpreadsheetCellDescriptionMemory::WriteNode(body, *cell))
					continue;

				entry->AddField(wxT("cell"), body);

				cells.push_back(ibDataValue::Child(entry));
			}
		}

		const wxString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		slot.m_kind = ibMcpClipboardKind::Cells;
		slot.m_name = sheet->GetName();
		slot.m_what = wxString::Format(ibMcpText("%d x %d"), bottom - top + 1, right - left + 1);

		slot.m_payload = ibDataNode();
		slot.m_payload.SetValue(wxT("rows"), (s32)(bottom - top + 1));
		slot.m_payload.SetValue(wxT("cols"), (s32)(right - left + 1));
		slot.m_payload.AddField(wxT("cells"), ibDataValue::Array(cells));

		result.SetValue(wxT("slot"), slotName.IsEmpty() ? wxString(wxT("default")) : slotName);
		result.SetValue(wxT("shape"), slot.m_what);
		result.AddField(wxT("cells"), ibDataValue::Int((s64)cells.size()));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetCopy);

class ibMcpToolSheetPaste : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_paste"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("pasting cells into the template '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Lay the copied rectangle of cells down again, its top-left corner at the "
			"row and column given. Cells the copy did not include are left as they are.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgRow(), ArgCol(), ArgTimes(), ArgSlot() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const s32 row = Line(ArgRow(), params);
		const s32 col = Line(ArgCol(), params);

		if (row < 0 || col < 0) {
			refusal = ibMcpText("Cells land at a row and column, both 1 or more.");
			return false;
		}

		const wxString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		if (slot.IsEmpty()) {
			refusal = ibMcpText("Nothing has been copied. Call sheet_copy first.");
			return false;
		}

		if (slot.m_kind != ibMcpClipboardKind::Cells) {
			refusal = wxString::Format(ibMcpText("The buffer holds %s, not cells."),
				ibMcpClipboardKindName(slot.m_kind));
			return false;
		}

		s32 times = (s32)ArgTimes().Whole(params);
		if (times <= 0)
			times = 1;

		const s32 height = slot.m_payload.GetValue<s32>(wxT("rows"));

		const ibDataValue* cells = slot.m_payload.FindField(wxT("cells"));
		if (cells == nullptr || cells->Kind() != ibDataKind::Array) {
			refusal = ibMcpText("The buffer holds no cells.");
			return false;
		}

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		int written = 0;

		for (s32 pass = 0; pass < times; pass++) {

			const s32 offset = pass * height;

			for (const ibDataValue& item : cells->AsArray()) {

				if (item.Kind() != ibDataKind::Child)
					continue;

				const std::shared_ptr<ibDataNode>& entry = item.AsChild();
				if (entry == nullptr)
					continue;

				const s32 atRow = row + offset + entry->GetValue<s32>(wxT("row"));
				const s32 atCol = col + entry->GetValue<s32>(wxT("col"));

				const ibDataValue* body = entry->FindField(wxT("cell"));
				if (body == nullptr)
					continue;

				ibSpreadsheetCellDescription* target = desc.GetOrCreateCell(atRow, atCol);
				if (target == nullptr)
					continue;

				// The cell reads its own fields back, and row and column are not
				// among them — so the cell standing here keeps its address.
				if (!ibSpreadsheetCellDescriptionMemory::ReadNode(*body, *target))
					continue;

				// ⚠ A MERGE IS NOT A NUMBER ON THE OWNER. ReadNode restores
				// m_col_size / m_row_size straight, which leaves the cells the
				// span covers still believing they are their own — it draws right
				// and behaves wrongly the moment anything walks the sheet.
				// SetCellSize is the mechanism that marks them, and it has to be
				// asked HERE for the same reason sheet_cell asks it rather than
				// writing the field.
				const int rowSize = (int)target->m_row_size;
				const int colSize = (int)target->m_col_size;

				if (rowSize > 1 || colSize > 1)
					desc.SetCellSize(atRow, atCol, rowSize > 1 ? rowSize : 1,
						colSize > 1 ? colSize : 1);

				written++;
			}
		}

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.SetValue(wxT("row"), row);
		result.SetValue(wxT("col"), col);
		result.AddField(wxT("cells"), ibDataValue::Int((s64)written));
		result.AddField(wxT("times"), ibDataValue::Int((s64)times));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetPaste);
