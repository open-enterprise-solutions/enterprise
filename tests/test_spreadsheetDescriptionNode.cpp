////////////////////////////////////////////////////////////////////////////
//	Description : ibSpreadsheetCellDescriptionMemory / ibSpreadsheetDescriptionMemory
//	              — the NODE form of a sheet (backend/spreadsheetDescription.cpp).
//
//	              A print layout IS a sheet: the areas a report fills, the
//	              parameters it substitutes, the fonts and borders a person looks
//	              at. While it travelled as an opaque Binary blob a template could
//	              be stored and shown and nothing else; as a structure it can be
//	              read, written and generated. What is pinned here is that the
//	              structure is LOSSLESS — write it, read it back, and the sheet
//	              is the same sheet.
//
//	              Pure backend: no window, no database, no wxApp. The node is
//	              built and consumed in memory; no provider (binary / JSON) is
//	              involved, because the shape is what these two classes own.
//
//	⚠ THE CRT LEAK DUMP ON EXIT IS EXPECTED AND IS NOT A LEAK — every
//	  ibSpreadsheetCellDescription holds a wxFont and two wxColour members, and
//	  those are released by wxEntryCleanup, which a console test never runs.
//	  Diagnosed 2026-08-18 down to a bare ibSpreadsheetDescription on the stack.
//	  See the same note in test_spreadsheetDocument.cpp / test_spreadsheetCompose.cpp.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/spreadsheetDescription.h"
#include "backend/serialize/dataBuilder.h"

namespace {

// A cell with everything a cell can carry set to something that is NOT its
// default — the node form writes only what differs, so a default field would
// prove nothing about the round trip.
void FillCell(ibSpreadsheetCellDescription& cell)
{
	cell.m_value = wxT("[Partner] owes [Sum]");
	cell.m_detailsParameter = wxT("Cell_3_4");

	cell.m_alignHorz = wxALIGN_RIGHT;
	cell.m_alignVert = wxALIGN_BOTTOM;
	cell.m_textOrient = wxVERTICAL;

	// 🛑 NO FONT IS SET HERE, and the reason is not the one this comment used to give.
	// CONSTRUCTING a wxFont is harmless without a display — every one of the 1752 tests
	// builds s_defaultSpreadsheetFont at start-up. COMPARING two of them is not:
	// wxFontBase::operator== asks GetPixelSize(), and that opens a wxScreenDC to measure
	// the glyphs. The node writer used to ask exactly that ("is this still the default
	// font?"), so six round-trip tests died inside GTK while the ones that wrote no cell
	// passed (SEGFAULT, "GLib-GObject-WARNING: invalid (NULL) pointer instance" — CI,
	// 2026-09-02). The cure was on the writing side: a cell now carries NO font until
	// somebody sets one, and an unset font is simply not written.
	//
	// ⚠ WHAT A SET FONT LOOKS LIKE ON THE WIRE is still not covered here — that wants a
	// suite with a toolkit under it, the GUI job CI runs on its own. What is pinned below
	// is the shape the two classes OWN, and the rule itself is pinned in
	// WriteNode_ACellWithNoFont_WritesNone.
	cell.m_backgroundColour = wxColour(10, 20, 30);
	cell.m_textColour = wxColour(200, 100, 50);

	cell.m_borderAt[0].m_style = wxPENSTYLE_SOLID;      // left
	cell.m_borderAt[0].m_width = 2;
	cell.m_borderAt[0].m_colour = wxColour(1, 2, 3);

	cell.m_borderAt[3].m_style = wxPENSTYLE_DOT;        // bottom
	cell.m_borderAt[3].m_width = 3;
	cell.m_borderAt[3].m_colour = wxColour(4, 5, 6);

	// right (1) and top (2) stay transparent — a transparent border is the
	// ABSENCE of a border and says nothing in the node.

	cell.SetSize(2, 3);
	cell.m_fitMode = ibSpreadsheetCellDescription::Mode_Clip;
	cell.m_isReadOnly = true;
	cell.m_fillSetType = ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate;
}

// A template sheet with one of everything a SHEET can carry.
void FillSheet(ibSpreadsheetDescription& sheet)
{
	sheet.SetCellValue(0, 0, wxT("Invoice"));
	sheet.SetCellSize(0, 0, 1, 3);                      // a merged title
	sheet.SetCellAlignment(0, 0, wxALIGN_CENTER, wxALIGN_CENTER);
	// (no SetCellFont — see FillCell for why a toolkit object has no place here)

	sheet.SetCellValue(1, 0, wxT("[Partner]"));
	sheet.SetCellFillType(1, 0, ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate);
	sheet.SetCellDetailsParameter(1, 0, wxT("Cell_1_0"));
	sheet.SetCellBackgroundColour(1, 0, wxColour(10, 20, 30));
	sheet.SetCellTextColour(1, 0, wxColour(200, 100, 50));

	ibSpreadsheetBorderDescription border;
	border.m_style = wxPENSTYLE_SOLID;
	border.m_width = 2;
	border.m_colour = wxColour(1, 2, 3);
	sheet.SetCellBorderLeft(1, 0, border);
	sheet.SetCellBorderBottom(1, 0, border);

	sheet.SetCellValue(2, 1, wxT("Total"));
	sheet.SetCellReadOnly(2, 1, true);
	sheet.SetCellFitMode(2, 1, ibSpreadsheetCellDescription::Mode_Clip);
	sheet.SetCellTextOrient(2, 1, wxVERTICAL);

	sheet.AddRowArea(wxT("Header"), 0, 0);
	sheet.AddRowArea(wxT("Detail"), 1, 2);
	sheet.AddColArea(wxT("Left"), 0, 1);

	sheet.AddRowBrake(2);
	sheet.AddColBrake(1);

	sheet.SetRowSize(0, 30);
	sheet.SetColSize(1, 90);

	sheet.SetRowFreeze(1);
	sheet.SetColFreeze(2);

	sheet.AddRowGroup(1, 2, 1, false);
	sheet.AddRowGroup(2, 2, 2, true);
	sheet.AddColGroup(0, 1, 1, false);
}

} // namespace

// ---------------------------------------------------------------------------
//  The CELL describes itself
// ---------------------------------------------------------------------------

// ⭐ THE CELL SAYS WHAT IT IS; the sheet only says WHERE. Every field it carries
// has to survive the trip, or a template loses a border / a merge / a fill type
// the first time it is saved.
TEST(SpreadsheetCellDescriptionMemory, WriteNodeReadNode_EverySetField_RoundTrips)
{
	ibSpreadsheetCellDescription source(3, 4);
	FillCell(source);

	ibDataValue value;
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::WriteNode(value, source));

	// The target is placed at the same address — the node carries no row/col of
	// its own, because placing a cell is the SHEET's job.
	ibSpreadsheetCellDescription target(3, 4);
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::ReadNode(value, target));

	// what is IN it
	EXPECT_EQ(source.m_value, target.m_value);
	EXPECT_EQ(source.m_detailsParameter, target.m_detailsParameter);
	EXPECT_EQ(source.m_fillSetType, target.m_fillSetType);

	// alignment and orientation
	EXPECT_EQ(source.m_alignHorz, target.m_alignHorz);
	EXPECT_EQ(source.m_alignVert, target.m_alignVert);
	EXPECT_EQ(source.m_textOrient, target.m_textOrient);

	// (a SET font is deliberately not exercised here — see FillCell)
	EXPECT_FALSE(target.m_font.IsOk());

	// the colours, exactly — an unset colour stays unset, so a set one has to
	// come back as itself rather than as today's desktop theme
	ASSERT_TRUE(target.m_backgroundColour.IsOk());
	ASSERT_TRUE(target.m_textColour.IsOk());
	EXPECT_EQ(source.m_backgroundColour, target.m_backgroundColour);
	EXPECT_EQ(source.m_textColour, target.m_textColour);

	// borders — the two drawn ones survive, the two undrawn ones stay undrawn
	EXPECT_TRUE(target.m_borderAt[0] == source.m_borderAt[0]);
	EXPECT_TRUE(target.m_borderAt[3] == source.m_borderAt[3]);
	EXPECT_EQ(wxPENSTYLE_TRANSPARENT, target.m_borderAt[1].m_style);
	EXPECT_EQ(wxPENSTYLE_TRANSPARENT, target.m_borderAt[2].m_style);

	// the merge
	int rows = 0, cols = 0;
	target.GetSize(&rows, &cols);
	EXPECT_EQ(2, rows);
	EXPECT_EQ(3, cols);

	// fit mode and read-only
	EXPECT_EQ(ibSpreadsheetCellDescription::Mode_Clip, target.m_fitMode);
	EXPECT_TRUE(target.m_isReadOnly);
}

// The same trip with the DEFAULT font, so the type's own comparison can be used
// as one assertion. (It is a partial comparison — see the test below — which is
// why the field-by-field one above exists as well.)
TEST(SpreadsheetCellDescriptionMemory, WriteNodeReadNode_ComparesEqualByTheTypesOwnEquality)
{
	ibSpreadsheetCellDescription source(3, 4);
	FillCell(source);

	ibDataValue value;
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::WriteNode(value, source));

	ibSpreadsheetCellDescription target(3, 4);
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::ReadNode(value, target));

	EXPECT_TRUE(target == source);

	// …plus the three fields that comparison does not look at.
	EXPECT_EQ(source.m_value, target.m_value);
	EXPECT_EQ(source.m_fitMode, target.m_fitMode);
	EXPECT_EQ(source.m_isReadOnly, target.m_isReadOnly);
}

// A cell nobody customised comes back the same, and — the point of the format —
// ONLY WHAT DIFFERS FROM THE DEFAULT IS WRITTEN. A sheet is mostly empty and a
// cell is mostly ordinary; writing every field of every one would bury the two
// that were actually set, and is also what lets an older sheet with fewer fields
// read correctly.
TEST(SpreadsheetCellDescriptionMemory, WriteNode_ADefaultCell_WritesNothingButStillRoundTrips)
{
	ibSpreadsheetCellDescription source(1, 1);

	ibDataValue value;
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::WriteNode(value, source));

	const std::shared_ptr<ibDataNode>& node = value.AsChild();
	ASSERT_TRUE(node != nullptr);

	EXPECT_EQ(nullptr, node->FindField(wxT("value")));
	EXPECT_EQ(nullptr, node->FindField(wxT("parameter")));
	EXPECT_EQ(nullptr, node->FindField(wxT("alignHorz")));
	EXPECT_EQ(nullptr, node->FindField(wxT("alignVert")));
	EXPECT_EQ(nullptr, node->FindField(wxT("orient")));
	EXPECT_EQ(nullptr, node->FindField(wxT("rowSpan")));
	EXPECT_EQ(nullptr, node->FindField(wxT("colSpan")));
	EXPECT_EQ(nullptr, node->FindField(wxT("fit")));
	EXPECT_EQ(nullptr, node->FindField(wxT("readOnly")));
	EXPECT_EQ(nullptr, node->FindField(wxT("fill")));

	ibSpreadsheetCellDescription target(1, 1);
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::ReadNode(value, target));

	EXPECT_TRUE(target.IsEmptyValue());
	EXPECT_TRUE(target.IsEmptyParameter());
	EXPECT_EQ((int)wxALIGN_LEFT, target.m_alignHorz);
	EXPECT_EQ((int)wxALIGN_TOP, target.m_alignVert);
	EXPECT_EQ(ibSpreadsheetCellDescription::Mode_Overflow, target.m_fitMode);
	EXPECT_FALSE(target.m_isReadOnly);
	EXPECT_EQ(ibSpreadsheetFillType::ibSpreadsheetFillType_StrText, target.m_fillSetType);
	EXPECT_FALSE(target.m_backgroundColour.IsOk());   // unset stays UNSET
	EXPECT_FALSE(target.m_textColour.IsOk());
}

// ⭐ A FONT NOBODY SET IS NOT WRITTEN, and this is the rule rather than a detail of
// the format: the writer decides by asking the cell whether it HAS a font, never by
// comparing one font with another. wxFont's own equality measures both through a
// wxScreenDC, so the old comparison made saving a sheet a question to the display —
// answerable on a desktop, fatal on a headless runner, and a different answer on a
// second monitor with another DPI. The colours two lines below it always worked this
// way; the font is now on the same road.
TEST(SpreadsheetCellDescriptionMemory, WriteNode_ACellWithNoFont_WritesNone)
{
	ibSpreadsheetCellDescription source(1, 1);
	source.m_value = wxT("Total");

	ASSERT_FALSE(source.m_font.IsOk());   // a new cell carries none

	ibDataValue value;
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::WriteNode(value, source));

	const std::shared_ptr<ibDataNode>& node = value.AsChild();
	ASSERT_TRUE(node != nullptr);
	EXPECT_EQ(nullptr, node->FindField(wxT("font")));

	ibSpreadsheetCellDescription target(1, 1);
	ASSERT_TRUE(ibSpreadsheetCellDescriptionMemory::ReadNode(value, target));
	EXPECT_FALSE(target.m_font.IsOk());   // and it does not acquire one on the way back
}

// The cell compares by EVERYTHING it carries - the text, the fit mode and the
// read-only flag included - and the SHEET's comparison is built on it, so two
// sheets differing in any one cell field are different sheets.
//
// 🛑 THIS TEST SAID THE OPPOSITE UNTIL 2026-09-02, and said it in a comment
// beginning "stated, not endorsed": it claimed the operator ignored those three
// fields and asserted two cells differing only in them were EQUAL. Nothing
// disagreed, because the .sln does not compile the tests and this file had never
// been run anywhere - the first machine to execute it was CI, on the push. A
// test written from a belief about code, rather than from the code, pins the
// belief.
TEST(SpreadsheetCellDescription, Equality_LooksAtEveryFieldTheCellCarries)
{
	ibSpreadsheetCellDescription first(2, 2);
	ibSpreadsheetCellDescription second(2, 2);

	EXPECT_TRUE(first == second);   // same position, nothing set - the same cell

	first.m_value = wxT("Alpha");
	second.m_value = wxT("Beta");
	EXPECT_FALSE(first == second);

	second.m_value = wxT("Alpha");
	EXPECT_TRUE(first == second);

	first.m_fitMode = ibSpreadsheetCellDescription::Mode_Clip;
	EXPECT_FALSE(first == second);

	second.m_fitMode = ibSpreadsheetCellDescription::Mode_Clip;
	first.m_isReadOnly = true;
	EXPECT_FALSE(first == second);

	second.m_isReadOnly = true;
	second.m_detailsParameter = wxT("Ref");
	EXPECT_FALSE(first == second);
}

// 🛑 A CELL POINTER MUST SURVIVE THE NEXT CELL BEING MADE, and for a long time it did not.
//
// GetOrCreateCell hands out a pointer INTO the container the cells live in. While that was a
// `std::vector`, creating one more cell moved all of them, and every pointer taken before that
// moment pointed at freed memory — writing through one corrupted the heap. It is not a mistake a
// caller can avoid: SetCellSize is built on exactly this order, taking the owner cell, creating
// the cells the merge covers, then writing the span back through the first pointer. `sheet_cell`
// with a colSpan on an empty template killed the designer that way (2026-09-02).
//
// So the invariant is pinned here rather than the two callers being made careful: the address of a
// cell does not change, and what was written through it is still there afterwards.
TEST(SpreadsheetDescription, GetOrCreateCell_APointerStaysGoodWhenMoreCellsAreMade)
{
	ibSpreadsheetDescription sheet;

	ibSpreadsheetCellDescription* first = sheet.GetOrCreateCell(1, 1);
	ASSERT_NE(first, nullptr);
	first->m_value = wxT("Header");

	// Enough to force any growth strategy to relocate several times over.
	for (int row = 1; row <= 8; row++)
		for (int col = 1; col <= 8; col++)
			sheet.GetOrCreateCell(row, col);

	EXPECT_EQ(sheet.GetOrCreateCell(1, 1), first)
		<< "the cell moved - a pointer taken earlier is now dangling";
	EXPECT_EQ(first->m_value, wxT("Header"))
		<< "the cell survived as an address but not as a value";
}

// AND THE MERGE ITSELF, which is the caller that met it first. A span over cells that do not exist
// yet creates them, and the owner's own span is written after that.
TEST(SpreadsheetDescription, SetCellSize_SpanningIntoUnmadeCells_KeepsTheOwnersSpan)
{
	ibSpreadsheetDescription sheet;

	ibSpreadsheetCellDescription* owner = sheet.GetOrCreateCell(1, 1);
	ASSERT_NE(owner, nullptr);

	sheet.SetCellSize(1, 1, 1, 6);   // one cell becomes six - five of them made right here

	int rows = 1, cols = 1;
	sheet.GetOrCreateCell(1, 1)->GetSize(&rows, &cols);

	EXPECT_EQ(rows, 1);
	EXPECT_EQ(cols, 6) << "the span was written through a pointer taken before the cells were made";

	// And what the span covers points back at its owner, which is how the grid finds it.
	int coveredRows = 1, coveredCols = 1;
	sheet.GetOrCreateCell(1, 3)->GetSize(&coveredRows, &coveredCols);
	EXPECT_EQ(coveredCols, -2) << "a covered cell says where its owner is, as a negative offset";
}

// ---------------------------------------------------------------------------
//  The SHEET — positions, bands, freeze; the cells answer for themselves
// ---------------------------------------------------------------------------

TEST(SpreadsheetDescriptionMemory, WriteNodeReadNode_WholeSheet_RoundTrips)
{
	ibSpreadsheetDescription source;
	FillSheet(source);

	ibDataValue value;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::WriteNode(value, source));

	ibSpreadsheetDescription target;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::ReadNode(value, target));

	// ---- cells: how many, where, and what they carry
	ASSERT_EQ(source.GetCellCount(), target.GetCellCount());
	EXPECT_EQ(source.GetNumberRows(), target.GetNumberRows());
	EXPECT_EQ(source.GetNumberCols(), target.GetNumberCols());

	EXPECT_EQ(wxT("Invoice"), target.GetCellValue(0, 0));
	EXPECT_EQ(wxT("[Partner]"), target.GetCellValue(1, 0));
	EXPECT_EQ(wxT("Total"), target.GetCellValue(2, 1));
	EXPECT_EQ(wxT("Cell_1_0"), target.GetCellDetailsParameter(1, 0));
	EXPECT_EQ(ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate,
		target.GetFillType(1, 0));

	// the merge, and — the half that used to be missing — the cells it COVERS,
	// each holding the negative offset back to the main one
	int rows = 0, cols = 0;
	target.GetCellSize(0, 0, &rows, &cols);
	EXPECT_EQ(1, rows);
	EXPECT_EQ(3, cols);
	target.GetCellSize(0, 1, &rows, &cols);
	EXPECT_EQ(0, rows);
	EXPECT_EQ(-1, cols);
	target.GetCellSize(0, 2, &rows, &cols);
	EXPECT_EQ(0, rows);
	EXPECT_EQ(-2, cols);

	int horiz = 0, vert = 0;
	target.GetCellAlignment(0, 0, &horiz, &vert);
	EXPECT_EQ((int)wxALIGN_CENTER, horiz);
	EXPECT_EQ((int)wxALIGN_CENTER, vert);

	EXPECT_EQ((int)wxVERTICAL, target.GetCellTextOrient(2, 1));
	EXPECT_TRUE(target.IsCellReadOnly(2, 1));
	EXPECT_EQ(ibSpreadsheetCellDescription::Mode_Clip, target.GetCellFitMode(2, 1));

	// The colours are read off the cell rather than through the document getter:
	// that getter resolves an unset colour by asking the DESKTOP, which is a
	// question a headless runner cannot answer.
	const ibSpreadsheetCellDescription* painted = target.GetCell(1, 0);
	ASSERT_NE(nullptr, painted);
	EXPECT_EQ(wxColour(10, 20, 30), painted->m_backgroundColour);
	EXPECT_EQ(wxColour(200, 100, 50), painted->m_textColour);

	EXPECT_TRUE(target.GetCellBorderLeft(1, 0) == source.GetCellBorderLeft(1, 0));
	EXPECT_TRUE(target.GetCellBorderBottom(1, 0) == source.GetCellBorderBottom(1, 0));
	EXPECT_EQ(wxPENSTYLE_TRANSPARENT, target.GetCellBorderRight(1, 0).m_style);

	const ibSpreadsheetCellDescription* titled = target.GetCell(0, 0);
	const ibSpreadsheetCellDescription* titledSource = source.GetCell(0, 0);
	ASSERT_NE(nullptr, titled);
	ASSERT_NE(nullptr, titledSource);
	EXPECT_EQ(titledSource->m_alignHorz, titled->m_alignHorz);
	EXPECT_EQ(titledSource->m_alignVert, titled->m_alignVert);

	// ---- areas
	ASSERT_EQ(2, target.GetAreaNumberRows());
	ASSERT_EQ(1, target.GetAreaNumberCols());
	ASSERT_NE(nullptr, target.GetRowAreaByIdx(0));
	ASSERT_NE(nullptr, target.GetRowAreaByIdx(1));
	ASSERT_NE(nullptr, target.GetColAreaByIdx(0));
	EXPECT_TRUE(*target.GetRowAreaByIdx(0) == *source.GetRowAreaByIdx(0));
	EXPECT_TRUE(*target.GetRowAreaByIdx(1) == *source.GetRowAreaByIdx(1));
	EXPECT_TRUE(*target.GetColAreaByIdx(0) == *source.GetColAreaByIdx(0));
	ASSERT_NE(nullptr, target.GetRowAreaByName(wxT("Detail")));
	EXPECT_EQ(1u, target.GetRowAreaByName(wxT("Detail"))->m_start);
	EXPECT_EQ(2u, target.GetRowAreaByName(wxT("Detail"))->m_end);

	// ---- page breaks
	EXPECT_EQ(1, target.GetBrakeNumberRows());
	EXPECT_EQ(1, target.GetBrakeNumberCols());
	EXPECT_TRUE(target.IsRowBrake(2));
	EXPECT_TRUE(target.IsColBrake(1));

	// ---- line sizes
	EXPECT_EQ(source.GetSizeNumberRows(), target.GetSizeNumberRows());
	EXPECT_EQ(source.GetSizeNumberCols(), target.GetSizeNumberCols());
	EXPECT_EQ(30, target.GetRowSize(0));
	EXPECT_EQ(90, target.GetColSize(1));

	// ---- freeze
	EXPECT_EQ(1, target.GetRowFreeze());
	EXPECT_EQ(2, target.GetColFreeze());

	// ---- and the sheet as a whole, by the type's own comparison. ⚠ That
	// comparison covers cells, areas, breaks and freeze only — line sizes and
	// OUTLINE GROUPS are outside it, which is exactly why both are asserted by
	// hand (sizes above, groups in the test below) rather than left to this line.
	EXPECT_TRUE(target == source);
}

// ⭐ GROUPS — THE OUTLINE. Nothing stored them before 2026-08-31, here or in the
// byte form that came before: a fold a person made lived in memory and was gone
// on the next open, which reads as the platform forgetting rather than as a gap
// (the bands were still there, only their nesting was not).
//
// ⚠ AND THE SHEET'S operator== DOES NOT COMPARE THEM. So the whole-sheet
// assertion above would stay green if groups were dropped again — this test is
// the only thing that would not.
TEST(SpreadsheetDescriptionMemory, WriteNodeReadNode_OutlineGroups_Survive)
{
	ibSpreadsheetDescription source;
	source.SetCellValue(0, 0, wxT("x"));
	source.AddRowGroup(1, 4, 1, false);
	source.AddRowGroup(2, 3, 2, true);     // nested, and FOLDED
	source.AddColGroup(0, 2, 1, false);

	ibDataValue value;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::WriteNode(value, source));

	ibSpreadsheetDescription target;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::ReadNode(value, target));

	ASSERT_EQ(2, target.GetGroupNumberRows());
	ASSERT_EQ(1, target.GetGroupNumberCols());

	const ibSpreadsheetGroupDescription* outer = target.GetRowGroupByIdx(0);
	ASSERT_NE(nullptr, outer);
	EXPECT_EQ(1u, outer->m_start);
	EXPECT_EQ(4u, outer->m_end);
	EXPECT_EQ(1u, outer->m_level);
	EXPECT_FALSE(outer->m_collapsed);

	const ibSpreadsheetGroupDescription* inner = target.GetRowGroupByIdx(1);
	ASSERT_NE(nullptr, inner);
	EXPECT_EQ(2u, inner->m_start);
	EXPECT_EQ(3u, inner->m_end);
	EXPECT_EQ(2u, inner->m_level);
	// FOLDED IS A STATE OF THE DOCUMENT, not of the person looking at it — a
	// template that opens folded is how a long form stays readable.
	EXPECT_TRUE(inner->m_collapsed);

	const ibSpreadsheetGroupDescription* column = target.GetColGroupByIdx(0);
	ASSERT_NE(nullptr, column);
	EXPECT_EQ(0u, column->m_start);
	EXPECT_EQ(2u, column->m_end);
	EXPECT_EQ(1u, column->m_level);

	// NOTE (not asserted): ibSpreadsheetGroupDescription::m_head — where the fold
	// marker sits when the producer already knows, used by the cross-table — is
	// NOT part of the node form. A group read back always has the default (-1),
	// i.e. "let the grid work it out". No template declares one today; if one
	// ever does, this is the line to add.
}

// A sheet that says nothing is an ordinary state, not a failure.
TEST(SpreadsheetDescriptionMemory, ReadNode_ANullSheetNode_LeavesTheDescriptionEmpty)
{
	ibSpreadsheetDescription target;

	EXPECT_TRUE(ibSpreadsheetDescriptionMemory::ReadNode(
		ibDataValue::Child(std::shared_ptr<ibDataNode>()), target));

	EXPECT_TRUE(target.IsEmptySpreadsheet());
	EXPECT_EQ(0, target.GetCellCount());
}

// Twice through the mill: reading what was written and writing it again must
// produce the same sheet, not merely one that happened to match on the first
// pass (a read that consumes more or less than the write emitted only diverges
// on re-entry).
TEST(SpreadsheetDescriptionMemory, WriteNodeReadNode_IsAFixedPoint)
{
	ibSpreadsheetDescription first;
	FillSheet(first);

	ibDataValue firstValue;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::WriteNode(firstValue, first));

	ibSpreadsheetDescription second;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::ReadNode(firstValue, second));

	ibDataValue secondValue;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::WriteNode(secondValue, second));

	ibSpreadsheetDescription third;
	ASSERT_TRUE(ibSpreadsheetDescriptionMemory::ReadNode(secondValue, third));

	EXPECT_TRUE(third == second);
	EXPECT_EQ(second.GetGroupNumberRows(), third.GetGroupNumberRows());
	EXPECT_EQ(second.GetGroupNumberCols(), third.GetGroupNumberCols());
	EXPECT_EQ(second.GetSizeNumberRows(), third.GetSizeNumberRows());
	EXPECT_EQ(second.GetSizeNumberCols(), third.GetSizeNumberCols());
}
