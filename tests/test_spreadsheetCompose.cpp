////////////////////////////////////////////////////////////////////////////
//	Description : ibSpreadsheetComposeDriver — the composition's LAYOUT into a
//	              spreadsheet document. No window, no database: the driver is fed
//	              the same calls the composer makes and the DOCUMENT is read back.
//
//	              This is the point of putting the output in the backend — the
//	              numbers and the shape of a report can be asserted with nothing
//	              on screen. A frontend that draws it is a second reader.
//
//	⚠ THE CRT LEAK DUMP ON EXIT IS EXPECTED HERE, AND IT IS NOT A LEAK.
//	  Every ibSpreadsheetCellDescription carries `wxFont m_font` and two wxColour
//	  members seeded from wxSystemSettings. Those are GDI objects with shared data
//	  that wxEntryCleanup releases — and a console test never runs it, so anything
//	  that writes ONE cell reports a few live blocks at exit. Tests that touch no
//	  cells (DataNode) are clean, which is what makes the dump look like ours.
//	  Diagnosed 2026-08-18 by narrowing to a bare ibSpreadsheetDescription on the
//	  stack; do not spend the afternoon on it twice.
//
//	  (Worth knowing for a different reason: a font plus two colours PER CELL is
//	  what a large report pays for. A 10k x 10 sheet is 100k cells carrying 300k
//	  GDI handles' worth of members — a real cost, and a real optimisation target
//	  the day a report gets big.)
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/composition/spreadsheetComposeDriver.h"

namespace {

// One output column, named. The driver reads the NAME only, so a schema entry
// with no query column behind it is enough here.
ibQueryLowering::OutputColumn Col(const wxString& name) {
	ibQueryLowering::OutputColumn c;
	c.m_name = name;
	return c;
}

std::vector<ibQueryLowering::OutputColumn> Schema() {
	return { Col(wxT("Partner")), Col(wxT("Amount")) };
}

// A document the driver writes into. wxObjectDataPtr because the spreadsheet
// object is ref-counted.
wxObjectDataPtr<ibBackendSpreadsheetObject> MakeDocument() {
	return wxObjectDataPtr<ibBackendSpreadsheetObject>(new ibBackendSpreadsheetObject());
}

// ⭐ A DIMENSION SAYS WHICH LEVEL IT BELONGS TO, and the driver reads that rather than counting
// dimension columns — a level may be made of SEVERAL fields, so "the n-th dimension column" and
// "the n-th level" stopped being the same number. A schema entry with no level (-1) is not a
// dimension the layout can place, so every Dim() here names its own.
ibQueryLowering::OutputColumn Dim(const wxString& name, int level) {
	ibQueryLowering::OutputColumn c;
	c.m_name = name;
	c.m_role = ibQueryLowering::ibColumnRole::Dimension;
	c.m_level = level;
	return c;
}

ibQueryLowering::OutputColumn Measure(const wxString& name) {
	ibQueryLowering::OutputColumn c;
	c.m_name = name;
	c.m_role = ibQueryLowering::ibColumnRole::Measure;
	return c;
}

} // namespace

// A document nobody wrote into is empty — and it is also the CONTROL for the leak
// check: if this one reports allocations at exit, they belong to the document, not
// to the driver's layout.
TEST(SpreadsheetCompose, EmptyDocument_IsEmpty)
{
	auto doc = MakeDocument();
	EXPECT_TRUE(doc->IsEmptyDocument());
	EXPECT_EQ(0, doc->GetNumberRows());
}

// A bare composition starts at the top: row 0 is the column header, rows follow.
TEST(SpreadsheetCompose, NoHeading_ColumnTitlesOnFirstRow)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnComplete(false);

	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(0, 0));
	EXPECT_EQ(wxT("Amount"), doc->GetCellValue(0, 1));
	EXPECT_EQ(wxT("Alpha"), doc->GetCellValue(1, 0));
	EXPECT_EQ(1, driver.GetRowsWritten());
}

// A heading pushes the table down and leaves ONE blank row between the two, so
// the parameters never read as a row of the table.
TEST(SpreadsheetCompose, Heading_PushesTableDownWithOneBlankRow)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());
	driver.SetTitle(wxT("Gross profit by partner"));
	driver.AddHeaderLine(wxT("Period >= 01.01.2011"));

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnComplete(false);

	EXPECT_EQ(wxT("Gross profit by partner"), doc->GetCellValue(0, 0));
	EXPECT_EQ(wxT("Period >= 01.01.2011"), doc->GetCellValue(1, 0));
	EXPECT_TRUE(doc->GetCellValue(2, 0).IsEmpty());     // the blank separator
	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(3, 0)); // column titles below it
	EXPECT_EQ(wxT("Alpha"), doc->GetCellValue(4, 0));
	EXPECT_EQ(1, driver.GetRowsWritten());
}

// NESTING IS THE INDENT, and it rides on the column the GROUPINGS are read down —
// they share one, so the depth has to show inside it. (Before the layout knew about
// roles the indent went on column 0 whatever stood there; a detail field is not a
// level and gets none.)
TEST(SpreadsheetCompose, DeeperLevel_IsIndentedInTheDimensionColumn)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns({ Dim(wxT("Partner"), 0), Dim(wxT("Product"), 1), Measure(wxT("Amount")) });
	driver.OnRow(1, true,  { ibValue(wxT("Group")), ibValue(),             ibValue(100) });
	driver.OnRow(2, false, { ibValue(),             ibValue(wxT("Leaf")), ibValue(40)  });
	driver.OnComplete(true);

	// Two heading lines (one per level), then the rows.
	EXPECT_TRUE(doc->GetCellValue(2, 0).EndsWith(wxT("Group")));
	EXPECT_TRUE(doc->GetCellValue(3, 0).EndsWith(wxT("Leaf")));
	EXPECT_TRUE(doc->GetCellValue(3, 0).StartsWith(wxT(" ")));
	EXPECT_LT(doc->GetCellValue(2, 0).length(), doc->GetCellValue(3, 0).length());
}

// EVERY NON-EMPTY CELL CARRIES ITS VALUE, and WHAT that means is the value's own
// business: a click ends in ibValue::ShowValue, and a value with nothing to show
// shows nothing. Deciding here which types are "openable" would be a second, poorer
// answer to a question the value already answers (2026-08-20).
TEST(SpreadsheetCompose, EveryNonEmptyCell_CarriesItsValue)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnRow(0, false, { ibValue(), ibValue(20) });
	driver.OnComplete(false);

	wxString details;
	doc->GetCellDetailsParameter(1, 0, details);
	EXPECT_FALSE(details.IsEmpty());
	doc->GetCellDetailsParameter(1, 1, details);
	EXPECT_FALSE(details.IsEmpty());

	// …and an EMPTY value binds nothing: there is no value behind that cell at all.
	doc->GetCellDetailsParameter(2, 0, details);
	EXPECT_TRUE(details.IsEmpty());
}

// COMPOSING TWICE REPLACES. Changing a filter and pressing Generate again is the
// ordinary case; appending would grow a report that looks like its data doubled.
//
// ⭐ AND A COMPOSE IS A DRIVER. The composition builds one per run (valueDataComposition::Compose),
// so "compose again" is a NEW driver over the same document — which is what clears it. Reusing one
// driver means something else entirely; see the test below.
TEST(SpreadsheetCompose, SecondCompose_ReplacesTheFirst)
{
	auto doc = MakeDocument();
	{
		ibSpreadsheetComposeDriver driver(doc.get());
		driver.OnColumns(Schema());
		driver.OnRow(0, false, { ibValue(wxT("Alpha")), ibValue(10) });
		driver.OnRow(0, false, { ibValue(wxT("Beta")), ibValue(20) });
		driver.OnComplete(false);
		EXPECT_EQ(2, driver.GetRowsWritten());
	}

	ibSpreadsheetComposeDriver again(doc.get());
	again.OnColumns(Schema());
	again.OnRow(0, false, { ibValue(wxT("Gamma")), ibValue(30) });
	again.OnComplete(false);

	EXPECT_EQ(1, again.GetRowsWritten());
	EXPECT_EQ(wxT("Gamma"), doc->GetCellValue(1, 0));
	EXPECT_TRUE(doc->GetCellValue(2, 0).IsEmpty());   // the second row of the first run is gone
}

// ⭐ AN OUTPUT IS A SECTION OF ONE SHEET (Max). A composition hands its outputs to the SAME driver,
// one after another, and each prints BELOW the previous one — clearing per output is what made a
// second output erase the first. Between them go two blank lines: one reads as a row that failed to
// print, two say "this report ended, another begins".
TEST(SpreadsheetCompose, SecondOutput_PrintsBelowTheFirstAfterAGap)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnComplete(false);

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("Gamma")), ibValue(30) });
	driver.OnComplete(false);

	// The first section stayed where it was.
	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(0, 0));
	EXPECT_EQ(wxT("Alpha"),   doc->GetCellValue(1, 0));
	// …then the gap, then the second section's own header and row.
	EXPECT_TRUE(doc->GetCellValue(2, 0).IsEmpty());
	EXPECT_TRUE(doc->GetCellValue(3, 0).IsEmpty());
	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(4, 0));
	EXPECT_EQ(wxT("Gamma"),   doc->GetCellValue(5, 0));
	// The count is the REPORT's, not the section's — both rows are on the sheet.
	EXPECT_EQ(2, driver.GetRowsWritten());
}

// ===========================================================================
//  The layout is the REPORT's, not the query's (2026-08-19/20)
// ===========================================================================

// GROUPINGS STACK INTO ONE COLUMN, read DOWN the page; a resource takes a column of
// its own. Giving every level a column of its own spreads a two-level report across
// the screen and leaves both columns mostly empty.
TEST(SpreadsheetCompose, DimensionsShareOneColumn_MeasuresGetTheirOwn)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns({ Dim(wxT("Partner"), 0), Dim(wxT("Product"), 1), Measure(wxT("Amount")) });
	driver.OnRow(1, true,  { ibValue(wxT("Alpha")), ibValue(),               ibValue(100) });
	driver.OnRow(2, false, { ibValue(),             ibValue(wxT("Widget")), ibValue(40)  });
	driver.OnComplete(true);

	// Two heading lines — one per level, in the column they are read in.
	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(0, 0));
	EXPECT_EQ(wxT("Product"), doc->GetCellValue(1, 0));
	// …and the measure names itself once, in a column of its own.
	EXPECT_EQ(wxT("Amount"), doc->GetCellValue(0, 1));

	// Both levels land in column 0, the deeper one indented.
	EXPECT_TRUE(doc->GetCellValue(2, 0).EndsWith(wxT("Alpha")));
	EXPECT_TRUE(doc->GetCellValue(3, 0).EndsWith(wxT("Widget")));
	EXPECT_LT(doc->GetCellValue(2, 0).length(), doc->GetCellValue(3, 0).length());
}

// A ROW SAYS ONLY HOW DEEP IT IS; the outline is what the SEQUENCE means. A row
// followed by deeper rows heads a group — the rows under it are what folds.
TEST(SpreadsheetCompose, RowLevels_BecomeOutlineGroups)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) });
	driver.OnRow(1, true,  { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnRow(2, false, { ibValue(wxT("Widget")), ibValue(40) });
	driver.OnComplete(true);

	// The document carries the levels as groups — one per heading that has something under it.
	EXPECT_GT(doc->GetSpreadsheetDesc().GetGroupNumberRows(), 0);
}

// A COMPOSED REPORT COMES UP READ-ONLY. Not decoration: the drill-down only answers
// a click while the sheet is not editable, so a report left editable is a report
// whose cells stop opening.
TEST(SpreadsheetCompose, ComposedDocument_IsReadOnly)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	EXPECT_TRUE(doc->IsEditable());   // a fresh document is a sheet somebody may fill in

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnComplete(false);

	EXPECT_FALSE(doc->IsEditable());
}

// EACH COLUMN AS WIDE AS WHAT IT HOLDS — a composed report has nobody to drag a
// border, and a clipped value reads as a different value.
TEST(SpreadsheetCompose, Columns_AreSizedFromTheirContent)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns(Schema());
	driver.OnRow(0, false, { ibValue(wxT("a name long enough to need more than the default width")), ibValue(10) });
	driver.OnComplete(false);

	EXPECT_GT(doc->GetColSize(0), doc->GetColSize(1));
}

// ⭐ THE GRAND TOTAL IS WRITTEN LAST, whatever order it ARRIVES in. The fold's walk is pre-order,
// so the root — the row standing for everything — is handed over BEFORE the first heading; printed
// where it arrives it sits above the column titles' first group, which is not where a reader looks
// for the sum of a report (Max, 2026-08-21: "the totals must always be at the end").
TEST(SpreadsheetCompose, GrandTotal_ArrivesFirstAndIsPrintedLast)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) });
	driver.OnRow(0, true,  { ibValue(),             ibValue(140) });   // the root — everything
	driver.OnRow(1, true,  { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnRow(1, true,  { ibValue(wxT("Beta")),  ibValue(40)  });
	driver.OnComplete(true);

	// Row 0 is the header; the two groups follow; the total closes the section.
	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(0, 0));
	EXPECT_TRUE(doc->GetCellValue(1, 0).EndsWith(wxT("Alpha")));
	EXPECT_TRUE(doc->GetCellValue(2, 0).EndsWith(wxT("Beta")));
	EXPECT_EQ(wxT("140"), doc->GetCellValue(3, 1));
	EXPECT_FALSE(doc->GetCellValue(3, 0).IsEmpty());   // and it says what it is
}

// …AND ITS CAPTION STAYS INSIDE THE DIMENSION AREA. With no dimensions there IS no such area —
// resources and no grouping is one row over everything — so column 0 holds a FIGURE, and writing
// the word "Total" into it would replace the first number with a caption.
TEST(SpreadsheetCompose, GrandTotalWithNoDimensions_WritesFiguresOnly)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns({ Measure(wxT("Amount")), Measure(wxT("Count")) });
	driver.OnRow(0, true, { ibValue(140), ibValue(2) });
	driver.OnComplete(true);

	EXPECT_EQ(wxT("140"), doc->GetCellValue(1, 0));
	EXPECT_EQ(wxT("2"),   doc->GetCellValue(1, 1));
}

// A GROUP HEADING CARRIES ITS OWN FIGURES and there is no "Total …" line under it: the heading IS
// the group's total, and repeating it a row below says the same thing twice (Max, 2026-08-22: "you
// already have the resource there — it is not readable"). Only the grand total stands alone.
TEST(SpreadsheetCompose, NoPerGroupTotalLine_TheHeadingCarriesTheFigures)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnColumns({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) });
	driver.OnRow(1, true,  { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnRow(2, false, { ibValue(),             ibValue(60)  });
	driver.OnRow(2, false, { ibValue(),             ibValue(40)  });
	driver.OnComplete(true);

	EXPECT_EQ(wxT("100"), doc->GetCellValue(1, 1));   // the heading's own figure
	EXPECT_EQ(3, driver.GetRowsWritten());            // heading + two rows, and nothing else
}
