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

#include "backend/backend_mainFrame.h"   // ibBackendDocFrame — the window a finished sheet is shown in
#include "backend/backend_spreadsheet.h" // ibBackendSpreadsheetObject — a description IS a sheet
#include "backend/session/session.h"     // ibSession::CurrentFrame — and whether there is one at all

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

// ⚠ NOT MARKED REQUIRED, THOUGH EVERY TOOL HERE NEEDS THEM ONE WAY OR ANOTHER — and the three that
// share them need them in three DIFFERENT ways, which a per-argument flag cannot say:
//
//   sheet_cell   both, or neither when the cells arrive as a list in `cells` — each entry then
//                carries its own, one level in from where a tool-level gate can look.
//   sheet_size   EXACTLY ONE of the two: a row has a height, a column has a width. The flag said
//                "both required" while the body refuses when both come — the schema contradicted
//                the tool it described.
//   sheet_paste  both, always.
//
// So the pair is optional in the schema and each body refuses in its own words, saying what IT
// wanted. Same treatment, same reason, as `start` / `end` on sheet_band.
//
// ⚠ AND THE SENTENCES STAY NEUTRAL because they are read in all three places: `cells` was named
// here for a moment and turned up in the published schema of sheet_size and sheet_paste, which have
// no such thing. A shared argument may only say what is true of every tool that takes it.
const ibArg& ArgRow()
{
	static const ibArg s_a(wxT("row"), ibArg::Kind::Whole,
		ibMcpText("Row, 1-based - the number the editor shows in the margin."));
	return s_a;
}

const ibArg& ArgCol()
{
	static const ibArg s_a(wxT("col"), ibArg::Kind::Whole,
		ibMcpText("Column, 1-based - the number the editor shows across the top."));
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
		ibMcpText("The text shown. For a caption this is all there is - and it is a LOCALISED string: "
			  "it is translated when the sheet is put out, so it may carry the every-language form "
			  "(`en = 'Goods'; ru = 'Tovary';`) exactly as a synonym does. A `template` text is "
			  "translated the same way, before its [holes] are filled."));
	return s_a;
}

const ibArg& ArgParameter()
{
	static const ibArg s_a(wxT("parameter"), ibArg::Kind::Text,
		ibMcpText("The name the module fills in, when the WHOLE cell is that value. A cell with one "
			  "is a hole, not a caption - and it is stamped as a parameter cell, which is what "
			  "makes the module's Parameters.Set reach it.\n"
			  "⚠ A PLAIN NAME, NOT A TRANSLATED STRING. It is looked up as written, so it is an "
			  "identifier the module knows - never the every-language form a caption may take. "
			  "The two are different kinds of text and the sheet treats them differently."));
	return s_a;
}

// ⭐⭐ A CELL IS FILLED IN ONE OF THREE WAYS, AND THE SHEET HAS ALWAYS KNOWN THAT.
// ibSpreadsheetFillType is Text, Parameter or Template (spreadsheetDescription.h), and it is what
// the designer's property panel calls "Fill type".
//
// 🛑 THIS TOOL WROTE THE PARAMETER NAME AND NEVER THE TYPE. `SetParameter` fills
// m_detailsParameter; the cell stayed StrText, so the module's Parameters.Set had nothing to
// substitute into and the printed form came out with every hole EMPTY. Found by looking at the
// designer's own property panel after building a print form through these tools (2026-09-05):
// every parameter cell read `Fill type: Text`, and sheet_get - which reports the parameter name -
// could not show it, because it did not report the type either. Written and never read, on both
// sides at once.
//
// The ordinary cases decide themselves: a `parameter` makes a parameter cell, a `value` makes a
// text one. This argument is for the third, which nothing else can say - a caption with holes IN
// it, `"No. [Number] of [Date]"` - and for a caller that would rather be explicit than rely on
// what it happened to pass.
// ⭐ THE OTHER PARAMETER, AND IT IS NOT THE SAME ONE. A cell can carry a DECIPHER value beside
// what it prints: click the total on a report and be shown what it is made of. It is the field the
// designer's panel calls `Details parameter`, it is set per printed cell as an area is put out, and
// it has nothing to do with what the cell SAYS.
const ibArg& ArgDetails()
{
	static const ibArg s_a(wxT("details"), ibArg::Kind::Text,
		ibMcpText("The DECIPHER parameter - what this cell drills down to when somebody clicks it in a "
			  "printed report. Not what the cell shows: that is `parameter` or `value`."));
	return s_a;
}

const ibArg& ArgFill()
{
	static const ibArg s_a(wxT("fill"), ibArg::Kind::Text,
		ibMcpText("How the cell is filled: `text` is a caption as typed, `parameter` means the whole "
			  "cell is one substituted value, `template` means the TEXT carries holes in square "
			  "brackets - \"No. [Number] of [Date]\" - which the module fills by those names. "
			  "Omitted, passing `parameter` makes a parameter cell and passing `value` makes a text "
			  "one, which is right nearly always; name it when you want a template."),
		/*required*/ false, { wxT("text"), wxT("parameter"), wxT("template") });
	return s_a;
}

// The fill type as the word the caller was given, so what is read back and what may be written are
// the same three words rather than a number and a vocabulary.
wxString FillWord(ibSpreadsheetFillType type)
{
	switch (type) {
		case ibSpreadsheetFillType_StrParameter: return wxT("parameter");
		case ibSpreadsheetFillType_StrTemplate:  return wxT("template");
		default: break;
	}
	return wxT("text");
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

// ⭐⭐ A RULE HAS A THICKNESS, AND A FORM IS READ BY IT. The outer frame of a table and the lines
// between its rows are not the same weight on any printed document: the heavy line says where the
// table begins and the hairline says where one row ends, and a sheet ruled entirely at 1 reads as
// a grey mesh with no structure in it.
//
// 🛑 THIS TOOL RULED EVERYTHING AT 1 and offered no way to say otherwise, so every form built
// through this door came out flat. Counted in a working configuration's own templates (2026-09-05,
// a cash order): of twenty left borders, ELEVEN are width 3, two are 2, two are 1 and five are 0 —
// the heavy line is the COMMON case, not the exception, and the one this door could not draw.
const ibArg& ArgBorderWidth()
{
	static const ibArg s_a(wxT("borderWidth"), ibArg::Kind::Whole,
		ibMcpText("How heavy the rule is: 0 rubs it out, 1 is a hairline for the lines inside a "
			  "table, 2 and 3 are the weights a printed form frames itself with. Default 1. "
			  "Applies to the sides named by `border`."));
	return s_a;
}

// ⭐⭐ THE PLURAL, ON THE VERB THAT IS ACTUALLY REPEATED. A blank is not laid out one cell at a
// time by anybody's choice — a header band alone is a dozen, and one real form took 25 separate
// calls (2026-09-05), each a round trip, each able to fail on its own with the previous two dozen
// already written.
//
// ⚠ IT IS NOT A GENERIC BATCH, AND THAT WAS THE FORK. A `mcp_batch` taking {tool, arguments} pairs
// would cover every verb at once — and would have to reproduce, inside itself, the argument gate,
// the exception classification and the journal line that the SERVER does around each call. That is
// the envelope's own rule (`mcp_call` is unwrapped in the server precisely so none of it is
// duplicated), and a batch tool would have been the second copy of all three. The neighbour that
// already got this right is `section_include`, whose `ids` says the same thing: a section is
// normally filled in one go, so the verb takes a set and one is the degenerate case.
const ibArg& ArgCells()
{
	static const ibArg s_a(wxT("cells"), ibArg::Kind::Many,
		ibMcpText("Several cells at once, instead of the single-cell arguments. Each entry is the "
			  "same shape as one call - {row, col, value|parameter, fill, bold, border, ...} - and "
			  "`id` is named once, on the outside. They are written in order and reported on "
			  "separately; a refusal stops the run and says which entry it was, with the ones "
			  "before it already written, because a template has no transaction. Lay a band out "
			  "in one call: it is the same work with one round trip instead of a dozen."));
	return s_a;
}

const ibArg& ArgFit()
{
	static const ibArg s_a(wxT("fit"), ibArg::Kind::Text,
		ibMcpText("What long text does: wrap, overflow, clip, or ellipsis. On paper this decides "
			  "whether a name runs into the next cell, is cut, or continues on a further line "
			  "inside the same cell. `wrap` is what a column heading over a narrow money column "
			  "almost always wants, and it is the commonest of the four in real blanks; the cell "
			  "keeps the height the band gives it, so leave room for the lines you expect."));
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

// 🛑⭐ NOT REQUIRED, BECAUSE WHETHER THEY ARE DEPENDS ON WHAT THE CALL IS DOING - and the gate
// cannot know that. Declared required, this pair made `sheet_band {what: 'break', at: 12}`
// impossible: a page break is addressed by `at`, says so in its own schema, and was refused for a
// missing `start` it has no use for. Setting a page break through this door could not be done at
// all (measured 2026-09-05). Removing an area has the same shape: the NAME says which one.
//
// ⭐ So the gate answers "it did not come" and each tool answers "it did not come FOR THIS", in
// its own words and with the shape it wanted - which is the only place that knows.
const ibArg& ArgStart()
{
	static const ibArg s_a(wxT("start"), ibArg::Kind::Whole,
		ibMcpText("First row, 1-based. Needed where a band is a RANGE - an area, a group - and not "
			  "where one point addresses it."));
	return s_a;
}

const ibArg& ArgEnd()
{
	static const ibArg s_a(wxT("end"), ibArg::Kind::Whole,
		ibMcpText("Last row, inclusive. Needed alongside `start`."));
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
		ibMcpText("Apply it to COLUMNS instead of rows. For a band: a vertical page break, a frozen "
			  "left edge, a column group. For an AREA: a vertical block a module joins sideways, "
			  "which is how one template serves a document with an article code and one without."));
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
			  "freeze this is the same as at=0; for a group, `start` and `end` say which; for an "
			  "AREA, the name does."));
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

		// ⭐⭐ AND THE VERTICAL ONES, which this reading did not know existed. A template is cut both
		// ways — row bands put out down the page, COLUMN blocks joined sideways so one blank serves
		// a document with an article code and one without — and listing only half of that made the
		// other half invisible the moment it could be created (2026-09-05, on the call that first
		// made one). Named as a band so the two cannot be confused: they are addressed separately
		// and a name may honestly be used once in each.
		for (int idx = 0; idx < desc.GetAreaNumberCols(); ++idx) {

			const ibSpreadsheetAreaDescription* area = desc.GetColAreaByIdx(idx);
			if (area == nullptr)
				continue;

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("name"), area->m_label);
			node->SetValue(wxT("band"), wxString(wxT("columns")));
			node->AddField(wxT("start"), ibDataValue::Int(LineOut(area->m_start)));
			node->AddField(wxT("end"), ibDataValue::Int(LineOut(area->m_end)));
			node->AddField(wxT("width"),
				ibDataValue::Int((s64)(area->m_end - area->m_start + 1)));

			areas.push_back(ibDataValue::Child(node));
		}

		result.AddField(wxT("areas"), ibDataValue::Array(areas));

		// ⭐⭐ AND A SHEET WITH NO BANDS IS A SHEET NOBODY HAS CUT YET — said HERE, because this is
		// the one call a caller makes before laying anything out, and the mistake it prevents is
		// made in the first minute and paid for in the last.
		//
		// A template is not drawn cell by cell; it is read for what REPEATS and cut into bands — a
		// header said once, a detail line drawn once per row, a footer, and the column blocks that
		// make one blank serve several variants. That knowledge is written down and is nothing this
		// tool could teach in a sentence, so the answer names where it lives rather than paraphrasing
		// it. A caller that already knows loses one line; a caller that does not would otherwise
		// build a form cell by cell and discover the bands only when the second variant is asked
		// for (measured on this server 2026-09-05: a print form laid out in twenty-five calls
		// without one of these patterns being read).
		if (areas.empty()) {
			result.SetValue(wxT("nextStep"),
				ibMcpText("This sheet has no AREAS, so a module can only print it whole - and areas are "
				  "what a printed form is made of. Before laying cells out, read how a blank is taken "
				  "apart: pattern_read 'form-to-areas' for the bands and the column blocks, "
				  "'printing' for the order of the blocks a person expects to see. Then name the "
				  "bands with sheet_area."));
		}

		// ⭐⭐ THE BANDS THAT ARE NOT THE DEFAULT — which is the half that made sizing BLIND.
		//
		// sheet_size can set a row's height and a column's width, and nothing could read one back:
		// a caller could not tell a column it had narrowed from one it had never touched, could not
		// compare two, and could not learn what the default even is. So it guessed, and a form came
		// out with `Counterparty:` clipped to `Counter` (2026-09-05, seen on the screen and not in
		// any answer this door gave).
		//
		// Only the ones STORED are listed, because that is what "not the default" means here and a
		// line per untouched column would bury the four that were set. `hidden` is a width of
		// nothing, which is what hidden IS on a sheet — said as a word rather than left as a zero
		// the reader has to know how to read.
		{
			std::vector<ibDataValue> bands;

			const auto band = [&bands](const wxChar* what, s64 at, int size) {
				std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
				node->SetValue(wxT("band"), wxString(what));
				node->AddField(wxT("at"), ibDataValue::Int(at));
				node->AddField(wxT("size"), ibDataValue::Int((s64)size));
				if (size == 0)
					node->AddField(wxT("hidden"), ibDataValue::Bool(true));
				bands.push_back(ibDataValue::Child(node));
			};

			for (int idx = 0; idx < desc.GetSizeNumberRows(); ++idx)
				if (const ibSpreadsheetRowSizeDescription* r = desc.GetRowSizeByIdx((size_t)idx))
					band(wxT("row"), LineOut((int)r->m_row), (int)r->m_height);

			for (int idx = 0; idx < desc.GetSizeNumberCols(); ++idx)
				if (const ibSpreadsheetColSizeDescription* c = desc.GetColSizeByIdx((size_t)idx))
					band(wxT("column"), LineOut((int)c->m_col), (int)c->m_width);

			if (!bands.empty())
				result.AddField(wxT("sizes"), ibDataValue::Array(bands));
		}

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
		return ibMcpText("Put text or a PARAMETER into a cell of a template - or into a whole band of "
			"them at once through `cells`, which is how a header row or a detail line is "
			"meant to be laid out. A parameter is what the printing module substitutes when "
			"it puts the area out - the whole reason a template is not a picture.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgCells(), ArgRow(), ArgCol(), ArgValue(), ArgParameter(), ArgDetails(), ArgFill(), ArgAlign(), ArgUnderline(), ArgColSpan(), ArgRowSpan(), ArgItalic(), ArgBold(), ArgFontSize(), ArgValign(), ArgVertical(), ArgBackground(), ArgColour(), ArgBorder(), ArgBorderWidth(), ArgFit(), ArgReadOnly() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// ⭐ THE SET FIRST, AND IT RUNS THE SINGLE-CELL BODY BELOW ONCE PER ENTRY. Written this way
		// rather than as a second implementation: every rule about parameters, fill types, borders
		// and spans stays in one place, so a cell written in a set is written by exactly the code
		// that writes one on its own. The template is named once, on the outside, and carried in.
		if (const ibDataValue* many = params.FindField(ArgCells().Name())) {

			if (many->Kind() != ibDataKind::Array) {
				refusal = ibMcpText("`cells` is a LIST of cells - [{row, col, value}, ...].");
				return false;
			}

			const ibDataValue* named = params.FindField(ArgId().Name());

			std::vector<ibDataValue> written;
			int at = 0;

			for (const ibDataValue& each : many->AsArray()) {

				++at;

				if (each.Kind() != ibDataKind::Child || each.AsChild() == nullptr) {
					refusal = wxString::Format(
						ibMcpText("Entry %i of `cells` is not a cell - each one is an object, "
							  "{row, col, ...}."), at);
					return false;
				}

				ibDataNode one = *each.AsChild();

				if (named != nullptr && one.FindField(ArgId().Name()) == nullptr)
					one.AddField(ArgId().Name(), *named);

				ibDataNode said;
				if (!Call(one, said, refusal)) {
					// ⚠ SAID PLAINLY: THE EARLIER ONES ARE ALREADY IN. Nothing here is a
					// transaction, so calling the set atomic would be the lie. The caller is told
					// which entry stopped it and how many stand, which is what it needs to carry
					// on from rather than start over.
					refusal = wxString::Format(
						ibMcpText("Cell %i of %u: %s. The %i before it are written."),
						at, (unsigned)many->AsArray().size(), refusal, at - 1);
					return false;
				}

				written.push_back(ibDataValue::Child(std::make_shared<ibDataNode>(said)));
			}

			result.AddField(wxT("written"), ibDataValue::Int((s64)written.size()));
			result.AddField(wxT("cells"), ibDataValue::Array(written));
			return true;
		}

		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		const s32 row = Line(ArgRow(), params);
		const s32 col = Line(ArgCol(), params);

		if (row < 0 || col < 0) {
			refusal = ibMcpText("A cell is addressed by row and column, both 1 or more - or a whole "
				  "band of them arrives in `cells`, each entry carrying its own.");
			return false;
		}

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		ibSpreadsheetCellDescription* cell = desc.GetOrCreateCell(row, col);
		if (cell == nullptr) {
			refusal = ibMcpText("That cell could not be made.");
			return false;
		}
		// (A set of cells arrives through `cells` and comes back through here one entry at a time —
		//  see the branch at the top of this method.)

		const bool gaveValue     = params.FindField(ArgValue().Name())     != nullptr;
		const bool gaveParameter = params.FindField(ArgParameter().Name()) != nullptr;

		if (gaveValue)
			cell->SetValue(ArgValue().Text(params));

		// 🛑⭐⭐ A PARAMETER CELL CARRIES THE NAME AS ITS VALUE — AND THIS WROTE IT SOMEWHERE ELSE.
		//
		// ibBackendSpreadsheetObject::Put substitutes with
		//     cell->m_value = ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType)
		// so for a Parameter cell the WHOLE TEXT is the name looked up, and for a Template cell the
		// names are the `[...]` inside that text. The name has to be in m_value or there is nothing
		// to substitute.
		//
		// SetParameter fills m_detailsParameter, which is a DIFFERENT FIELD FOR A DIFFERENT JOB: the
		// decipher parameter, used ten lines below Put's substitution to give each printed cell its
		// own drill-down value. The designer's property panel names them apart — `Fill type` against
		// `Details parameter` — and this tool had been writing the second while its argument said,
		// and its description promised, the first.
		//
		// What that cost: every parameter cell built through this door was EMPTY. Empty in the
		// editor, because there is no text; empty on paper, because the substitution reads the text
		// there is none of. Seen on the screen (Max, 2026-09-05) as a print form whose detail band
		// was a row of blank boxes, after `sheet_get` had reported the parameter names back quite
		// happily — the read side agreed with the write side and both were looking at the wrong
		// field.
		if (gaveParameter)
			cell->SetValue(ArgParameter().Text(params));

		// The decipher parameter, when one is asked for — the field SetParameter actually fills.
		if (params.FindField(ArgDetails().Name()) != nullptr)
			cell->SetParameter(ArgDetails().Text(params));

		// ⭐⭐ AND THE FILL TYPE WITH IT — see the note on ArgFill. Writing the parameter name
		// without stamping the type left a cell that LOOKS like a hole to this tool and is a
		// caption to everything that reads the sheet.
		const wxString fill = ArgFill().Text(params);

		if (!fill.IsEmpty()) {
			desc.SetCellFillType(row, col,
				fill.IsSameAs(wxT("parameter"), false) ? ibSpreadsheetFillType_StrParameter
				: fill.IsSameAs(wxT("template"), false) ? ibSpreadsheetFillType_StrTemplate
				: ibSpreadsheetFillType_StrText);
		}
		// Not said, so it follows what arrived: a name to substitute is a parameter cell, a
		// caption is a text one. Only touched when one of them came, so a call that changes
		// nothing but the colour leaves a template cell a template.
		else if (gaveParameter && !ArgParameter().Text(params).IsEmpty())
			desc.SetCellFillType(row, col, ibSpreadsheetFillType_StrParameter);
		else if (gaveValue)
			desc.SetCellFillType(row, col, ibSpreadsheetFillType_StrText);

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

			// A cell that carries no font of its own is shown in the sheet's default
			// one, so that is what "make it bold" starts from.
			wxFont font = cell->m_font.IsOk() ? cell->m_font : s_defaultSpreadsheetFont;

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

			// The weight, if one was named — see ArgBorderWidth for why the heavy line is the case
			// this door could not draw. Zero rubs the rule out, which is how a form takes a line
			// AWAY from one side of a framed block.
			const int weight = params.FindField(ArgBorderWidth().Name()) != nullptr
				? (int)ArgBorderWidth().Whole(params) : 1;

			if (weight < 0) {
				refusal = ibMcpText("A border width is 0 or more - 0 rubs the rule out.");
				return false;
			}

			for (int side = 0; side < 4; ++side) {
				if (!sides[side])
					continue;
				cell->m_borderAt[side].m_style = weight > 0 ? wxPENSTYLE_SOLID : wxPENSTYLE_TRANSPARENT;
				cell->m_borderAt[side].m_width = weight;
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
			else if (fit.IsSameAs(wxT("wrap"), false))
				cell->m_fitMode = ibSpreadsheetCellDescription::Mode_Wrap;
			else if (fit.IsSameAs(wxT("clip"), false))
				cell->m_fitMode = ibSpreadsheetCellDescription::Mode_Clip;
			else if (fit.IsSameAs(wxT("ellipsis"), false))
				cell->m_fitMode = ibSpreadsheetCellDescription::Mode_EllipsizeEnd;
			else {
				refusal = ibMcpText("Fit is wrap, overflow, clip or ellipsis.");
				return false;
			}
		}

		if (params.FindField(ArgReadOnly().Name()) != nullptr)
			cell->m_isReadOnly = ArgReadOnly().Flag(params);

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.AddField(wxT("row"), ibDataValue::Int(LineOut(row)));
		result.AddField(wxT("col"), ibDataValue::Int(LineOut(col)));
		// ⚠ THE TEXT IS ANSWERED UNDER THE NAME IT WAS SENT AS. A parameter cell holds the name in
		// its value — that is what a parameter cell IS — so answering it as `value` would tell a
		// caller that its `parameter` had not been taken. The fill type decides which word to use,
		// which is the same fact deciding it in both directions.
		const wxString said = cell->GetValue();
		if (!said.IsEmpty()) {
			result.SetValue(desc.GetFillType(row, col) == ibSpreadsheetFillType_StrParameter
				? wxT("parameter") : wxT("value"), said);
		}

		// And the decipher parameter, which is a different field and a different question.
		if (!cell->GetParameter().IsEmpty())
			result.SetValue(wxT("details"), cell->GetParameter());

		// ⭐ AND WHAT IT NOW IS, because that is the half a caller could not see. A cell carrying a
		// parameter name and a fill type of Text is the shape that prints an empty hole, and until
		// this answered there was no way to tell one from a working cell without opening the
		// designer and looking at the property panel.
		result.SetValue(wxT("fill"), FillWord(desc.GetFillType(row, col)));

		// ⭐ AND THE PLACEMENT, for the same reason. `fit` could be written and never read, so a
		// caller checking its own work saw a cell that agreed with everything it had asked for
		// while the one property that decides whether a heading is legible went unmentioned. Said
		// only when it is not the default, so an ordinary cell's answer stays short.
		switch (cell->m_fitMode) {
		case ibSpreadsheetCellDescription::Mode_Wrap:
			result.SetValue(wxT("fit"), wxString(wxT("wrap"))); break;
		case ibSpreadsheetCellDescription::Mode_Clip:
			result.SetValue(wxT("fit"), wxString(wxT("clip"))); break;
		case ibSpreadsheetCellDescription::Mode_EllipsizeStart:
		case ibSpreadsheetCellDescription::Mode_EllipsizeMiddle:
		case ibSpreadsheetCellDescription::Mode_EllipsizeEnd:
			result.SetValue(wxT("fit"), wxString(wxT("ellipsis"))); break;
		default:
			break;
		}

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
		return ibMcpText("Name a band as an AREA - the unit a printing module puts out. A header, a "
			"table row, a footer: the module names these, so a template without them can only "
			"be printed whole.\n"
			"AND A TEMPLATE IS CUT BOTH WAYS. `columns: true` names a band of COLUMNS instead, "
			"which is how one blank serves several variants: a module JOINS the column blocks a "
			"document actually needs - with the article code or without it, with the discount "
			"columns or without - and puts the row bands out down the page. A vertical cut goes "
			"through every horizontal band it crosses, the heading and the totals included.\n"
			"Naming an area that already exists MOVES it, so a layout can be adjusted while it "
			"is being built; `remove: true` takes one away.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgName(), ArgStart(), ArgEnd(), ArgColumns(), ArgRemove() };
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

		// ⚠ AND THE RANGE IS ONLY ASKED FOR WHEN THERE IS ONE. A removal is addressed by the NAME —
		// demanding a first and last row to take an area away is asking which rows the thing being
		// removed occupied, which the caller may not know and which cannot matter.
		//
		// A BAND THAT ENDS BEFORE IT BEGINS is not a narrow area, it is a mistake,
		// and it would print nothing while looking declared.
		if (!ArgRemove().Flag(params) && (start < 0 || end < start)) {
			refusal = ibMcpText("An area runs from a first row to a last one, both 1 or more.");
			return false;
		}

		ibSpreadsheetDescription desc = sheet->GetSpreadsheetDesc();

		// ⭐⭐ ROWS OR COLUMNS, BECAUSE A TEMPLATE IS CUT BOTH WAYS AND THIS DOOR COULD ONLY CUT ONE.
		// The description has carried column areas all along - AddColArea beside AddRowArea, with
		// their own list - and they are what makes a blank serve several variants: the module JOINS
		// the blocks a document needs and PUTS the row bands down the page. Everything the corpus
		// says about optional column blocks (`form-to-areas`) was unbuildable from here.
		const bool columns = ArgColumns().Flag(params);

		// Whether one of this name is already there, which decides between adding and moving.
		bool exists = false;
		const int count = columns ? desc.GetAreaNumberCols() : desc.GetAreaNumberRows();

		for (int idx = 0; idx < count && !exists; ++idx) {
			const ibSpreadsheetAreaDescription* area =
				columns ? desc.GetColAreaByIdx(idx) : desc.GetRowAreaByIdx(idx);
			exists = area != nullptr && area->m_label.IsSameAs(name, false);
		}

		// 🛑 TAKING ONE AWAY, AND MOVING ONE, WERE BOTH IMPOSSIBLE. A second call with a name that
		// existed was REFUSED - and a layout is built by adjustment, so the bands move as the sheet
		// takes shape. Three refusals in one sitting here (2026-09-05), after which the areas of a
		// half-built form could not be corrected at all: the only way out was to delete the
		// template and start again.
		if (ArgRemove().Flag(params)) {

			if (!exists) {
				refusal = wxString::Format(
					ibMcpText("'%s' has no %s area called '%s' to remove."),
					sheet->GetName(), columns ? ibMcpText("column") : ibMcpText("row"), name);
				return false;
			}

			if (columns) desc.DeleteColArea(name);
			else         desc.DeleteRowArea(name);

			sheet->SetSpreadsheetDesc(desc);
			activeMetaData->Modify(true);

			result.AddField(wxT("removed"), ibDataValue::Bool(true));
			result.SetValue(wxT("name"), name);
			result.SetValue(wxT("band"), wxString(columns ? wxT("columns") : wxT("rows")));
			return true;
		}

		// ⭐ NAMING ONE THAT EXISTS MOVES IT. An area is identified by its NAME - that is what the
		// module puts out - so the same name twice is one area in two places, which is a
		// correction and not a conflict.
		if (exists) {
			if (columns) desc.SetColSizeArea(name, start, end);
			else         desc.SetRowSizeArea(name, start, end);
		}
		else {
			if (columns) desc.AddColArea(name, start, end);
			else         desc.AddRowArea(name, start, end);
		}

		sheet->SetSpreadsheetDesc(desc);
		activeMetaData->Modify(true);

		result.AddField(exists ? wxT("moved") : wxT("added"), ibDataValue::Bool(true));
		result.SetValue(wxT("name"), name);
		result.SetValue(wxT("band"), wxString(columns ? wxT("columns") : wxT("rows")));
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

//---------------------------------------------------------------------------
// sheet_show
//---------------------------------------------------------------------------
//
// ⭐⭐ THE ONE THING THIS DOOR COULD NOT DO IS LET SOMEBODY LOOK. Everything else about a template
// can be written and read back over the wire, and the last question — does it actually look right
// on paper — had no answer here at all: a caller could build a whole blank, verify every cell it
// had written, and still not know that a heading was cut in half. It ended with "open it in the
// designer and see", which needs a person who did not build it to go and find it.
//
// ⚠ FOR THE PERSON, NOT FOR THE CALLER (Max, 2026-09-05: *"purely so I can see"*). Nothing comes
// back but the fact that a window opened; a picture is not something this wire carries, and
// pretending otherwise would be the same lie as a read that agrees with its own write.
//
// ⭐ AND IT IS ASSEMBLED FROM WHAT IS ALREADY THERE, not built: a template's description IS a
// sheet (ibBackendSpreadsheetObject takes one whole), and a sheet already knows how to be shown —
// ShowSpreadsheetDocument makes a document view over it, the same road `Show()` takes at runtime
// and the list output takes from a form. Three lines of connecting, no new mechanism.
class ibMcpToolSheetShow : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_show"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("opening the template '%s' for you to look at"),
			ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Open a template in a window, as a spreadsheet, so the PERSON at the designer can "
			"look at it. Everything else here answers in text; this is the one question text "
			"cannot answer - whether a heading wraps or is cut, whether a column is wide enough, "
			"whether the frame lines fall where they should.\n"
			"Nothing comes back but the fact that it opened: use it to ASK somebody to look, at "
			"the point where a blank is finished, and say what you would like checked. The window "
			"is a copy and cannot be typed into - editing a template is what the tree is for.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectSpreadsheetBase* sheet = FindTemplate(params, refusal);
		if (sheet == nullptr)
			return false;

		// ⚠ THERE HAS TO BE SOMEBODY THERE. A server-side host has no frame at all, and answering
		// "shown" into a process with no screen would be a report of something that did not happen.
		ibBackendDocFrame* const frame = ibSession::CurrentFrame();

		if (frame == nullptr) {
			refusal = ibMcpText("There is no window to show it in - this platform is running without "
				"a screen. `sheet_get` answers everything about a template that text can.");
			return false;
		}

		wxObjectDataPtr<ibBackendSpreadsheetObject> shown(
			new ibBackendSpreadsheetObject(sheet->GetSpreadsheetDesc()));

		// ⚠ READ-ONLY, BECAUSE IT IS A COPY. Typing into it would change nothing and look as though
		// it had - the window holds its own description, not the template's.
		shown->EnableEditing(false);

		const wxString title = wxString::Format(ibMcpText("Template: %s"), sheet->GetName());

		if (!frame->ShowSpreadsheetDocument(title, shown)) {
			refusal = ibMcpText("The window could not be opened.");
			return false;
		}

		result.SetValue(wxT("shown"), title);
		result.SetValue(wxT("note"),
			ibMcpText("It is open in front of the person at the designer. Say what you would like "
			  "them to check - nothing about how it LOOKS comes back through here."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetShow);

//---------------------------------------------------------------------------
// sheet_export
//---------------------------------------------------------------------------
//
// ⭐⭐ THE OTHER HALF OF `sheet_import`, AND ITS ABSENCE WAS FELT AS SOON AS A FILE CAME BACK WRONG.
// A format that can only be read is not a format — a blank that arrives from an accountant has to
// be able to go back to them, into the program they live in, with the changes in it.
//
// ⚠ AND IT IS WHAT MAKES A ROUND TRIP TESTABLE AT ALL. An export that lost every row height was
// found by a person doing it by hand (Max, 2026-09-05), and the fault could not be narrowed from
// here because only he could produce the file: three candidate roads, no way to run one. A door
// that reads but cannot write leaves the person as the only instrument.
class ibMcpToolSheetExport : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("sheet_export"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("writing the template '%s' out to a file"),
			ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Write a template OUT to a spreadsheet file - the reverse of sheet_import. The "
			"format is decided by the name's extension, exactly as it is on the way in.\n"
			"Two uses: handing a blank back to the person who supplied it, in the program they "
			"work in; and checking a round trip, since what a file keeps and what it drops is "
			"only visible by writing one and reading it again.");
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

		if (fileName.IsEmpty()) {
			refusal = ibMcpText("Say where to write it - the extension decides the format.");
			return false;
		}

		const ibSheetFormat* format = ibSheetFormatFor(fileName);

		if (format == nullptr || !format->CanWrite()) {

			// The list is the registry's, so it grows by itself — same as on the reading side.
			wxString writable;
			for (const ibSheetFormat* known : ibSheetFormats()) {
				if (known == nullptr || !known->CanWrite())
					continue;
				if (!writable.IsEmpty())
					writable << wxT(", ");
				writable << known->GetExtension();
			}

			refusal = wxString::Format(
				ibMcpText("Nothing here writes '%s'. These can be written: %s."), fileName, writable);
			return false;
		}

		// ⭐ THE TEMPLATE'S OWN DESCRIPTION, NOT A COPY THROUGH A WINDOW. That is the point of
		// having this here: the file then answers for what the CONFIGURATION holds, and a
		// difference between this file and one saved from an editor says the editor's road is
		// where something is lost.
		if (!format->Write(fileName, sheet->GetSpreadsheetDesc())) {
			refusal = wxString::Format(ibMcpText("'%s' could not be written."), fileName);
			return false;
		}

		result.SetValue(wxT("written"), fileName);
		result.SetValue(wxT("format"), format->GetName());

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSheetExport);
