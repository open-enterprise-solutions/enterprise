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

// ⭐ AN OUTPUT IS ANNOUNCED WITH ITS SCHEMA — there is no separate "here are the columns" event any
// more, because which columns an output has is part of that output starting. The titles default to
// the column names, which is what the composer fills in when nothing overrode them.
ibCompositionOutputInfo SchemaInfo(std::vector<ibQueryLowering::OutputColumn> schema) {
	ibCompositionOutputInfo info;
	info.m_schema = std::move(schema);
	for (const ibQueryLowering::OutputColumn& column : info.m_schema)
		info.m_titles.push_back(column.m_name);
	return info;
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

// A PROJECTED FIELD — what a DETAIL row says about itself. Not a dimension and not a figure, which
// is exactly the role the lowering stamps on everything a query merely selects.
ibQueryLowering::OutputColumn Detail(const wxString& name) {
	ibQueryLowering::OutputColumn c;
	c.m_name = name;
	c.m_role = ibQueryLowering::ibColumnRole::Detail;
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

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnOutputEnd(false);

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

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnOutputEnd(false);

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

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Dim(wxT("Product"), 1), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, true,  { ibValue(wxT("Group")), ibValue(),             ibValue(100) });
	driver.OnRow(2, { ibValue(),             ibValue(wxT("Leaf")), ibValue(40)  });
	driver.OnOutputEnd(true);

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

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnRow(0, { ibValue(), ibValue(20) });
	driver.OnOutputEnd(false);

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
		driver.OnOutputBegin(SchemaInfo(Schema()));
		driver.OnRow(0, { ibValue(wxT("Alpha")), ibValue(10) });
		driver.OnRow(0, { ibValue(wxT("Beta")), ibValue(20) });
		driver.OnOutputEnd(false);
		EXPECT_EQ(2, driver.GetRowsWritten());
	}

	ibSpreadsheetComposeDriver again(doc.get());
	again.OnOutputBegin(SchemaInfo(Schema()));
	again.OnRow(0, { ibValue(wxT("Gamma")), ibValue(30) });
	again.OnOutputEnd(false);

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

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnOutputEnd(false);

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("Gamma")), ibValue(30) });
	driver.OnOutputEnd(false);

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

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Dim(wxT("Product"), 1), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, true,  { ibValue(wxT("Alpha")), ibValue(),               ibValue(100) });
	driver.OnRow(2, { ibValue(),             ibValue(wxT("Widget")), ibValue(40)  });
	driver.OnOutputEnd(true);

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

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, true,  { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnRow(2, { ibValue(wxT("Widget")), ibValue(40) });
	driver.OnOutputEnd(true);

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

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("Alpha")), ibValue(10) });
	driver.OnOutputEnd(false);

	EXPECT_FALSE(doc->IsEditable());
}

// EACH COLUMN AS WIDE AS WHAT IT HOLDS — a composed report has nobody to drag a
// border, and a clipped value reads as a different value.
TEST(SpreadsheetCompose, Columns_AreSizedFromTheirContent)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnOutputBegin(SchemaInfo(Schema()));
	driver.OnRow(0, { ibValue(wxT("a name long enough to need more than the default width")), ibValue(10) });
	driver.OnOutputEnd(false);

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

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(0, ibSelectorNodeKind::Group, true, true,  { ibValue(),             ibValue(140) });   // the root — everything
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, true,  { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, true,  { ibValue(wxT("Beta")),  ibValue(40)  });
	driver.OnOutputEnd(true);

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

	driver.OnOutputBegin(SchemaInfo({ Measure(wxT("Amount")), Measure(wxT("Count")) }));
	driver.OnGroupBegin(0, ibSelectorNodeKind::Group, true, true, { ibValue(140), ibValue(2) });
	driver.OnOutputEnd(true);

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

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, true,  { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnRow(2, { ibValue(),             ibValue(60)  });
	driver.OnRow(2, { ibValue(),             ibValue(40)  });
	driver.OnOutputEnd(true);

	EXPECT_EQ(wxT("100"), doc->GetCellValue(1, 1));   // the heading's own figure
	EXPECT_EQ(3, driver.GetRowsWritten());            // heading + two rows, and nothing else
}

// ===========================================================================
//  The cross-table — the same driver, laid out the other way
// ===========================================================================

namespace {

// A table's schema: row headings, column headings, and what stands where they meet. The AXIS is not
// in the schema — it is the OUTPUT INFO's answer, read off `m_rowLevels` — so a cross schema is an
// ordinary schema whose deeper dimensions happen to read across the page.
ibCompositionOutputInfo CrossInfo(const std::vector<ibQueryLowering::OutputColumn>& schema, size_t rowLevels)
{
	ibCompositionOutputInfo info;
	info.m_kind      = ibCompositionOutputKind::Table;
	info.m_schema    = schema;
	info.m_rowLevels = rowLevels;
	return info;
}

} // namespace

// ⭐⭐ A TABLE IS PRINTED WHEN ITS WIDTH IS KNOWN, and not before. The walk arrives row heading first,
// then the column headings under it; the second row introduces a column key the first never had, and
// the header still has to carry it — which is the whole reason a table cannot stream.
TEST(SpreadsheetCross, AColumnKeySeenLateStillGetsItsColumn)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	// Partner reads down the page (level 0), Warehouse across it (level 1).
	driver.OnOutputBegin(CrossInfo({ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Measure(wxT("Amount")) }, 1));

	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(30) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(30) });

	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Beta")), ibValue(), ibValue(70) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(70) });   // a key nobody saw before

	driver.OnOutputEnd(true);

	// The header names both keys, in the order they were first seen, and closes with the row total.
	EXPECT_EQ(wxT("North"), doc->GetCellValue(0, 1));
	EXPECT_EQ(wxT("South"), doc->GetCellValue(0, 2));
	EXPECT_EQ(wxT("Total"), doc->GetCellValue(0, 3));

	// Alpha bought in the North only; the cell where it meets the South stays EMPTY. A zero there
	// would state a measurement nobody made.
	EXPECT_EQ(wxT("Alpha"), doc->GetCellValue(1, 0).Trim(false));
	EXPECT_EQ(wxT("30"), doc->GetCellValue(1, 1));
	EXPECT_EQ(wxT(""),   doc->GetCellValue(1, 2));
	EXPECT_EQ(wxT("30"), doc->GetCellValue(1, 3));   // …and its row total is its own figure

	EXPECT_EQ(wxT(""),   doc->GetCellValue(2, 1));
	EXPECT_EQ(wxT("70"), doc->GetCellValue(2, 2));
	EXPECT_EQ(2, driver.GetRowsWritten());
}

// ⭐ THE ROW'S TOTAL COSTS NOTHING. The fold already computed the figures at the row heading, so a
// table gets its right-hand column out of what the walk hands over and needs no second pass for it.
TEST(SpreadsheetCross, TheRowHeadingsOwnFiguresAreTheRowTotal)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnOutputBegin(CrossInfo({ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Measure(wxT("Amount")) }, 1));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(100) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(60) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(40) });
	driver.OnOutputEnd(true);

	EXPECT_EQ(wxT("60"),  doc->GetCellValue(1, 1));
	EXPECT_EQ(wxT("40"),  doc->GetCellValue(1, 2));
	EXPECT_EQ(wxT("100"), doc->GetCellValue(1, 3));
}

// ⭐⭐ A DETAIL RECORD IS A LINE OF THE TABLE, WITH CELLS ACROSS IT (Max, 2026-08-26: "its own line,
// cells by the columns").
//
// 🛑 IT USED TO BE DROPPED, on the argument that "a cell holds what was computed, not what it was
// computed from". That answered a question nobody asked: a detail record was never going INTO a
// cell — it is a ROW, and what stands across a row are its columns. What made the argument look
// right was the fold's shape, which put the detail level after the column keys.
TEST(SpreadsheetCross, ADetailRecordIsALineOfTheTableWithItsOwnCells)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	const std::vector<ibQueryLowering::OutputColumn> schema =
		{ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Measure(wxT("Amount")), Detail(wxT("Doc")) };

	driver.OnOutputBegin(CrossInfo(schema, 1));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(30), ibValue() });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(wxT("North")), ibValue(30), ibValue() });
	// The record hangs under the ROW heading and carries a cell of its own — its level is past the
	// last dimension, which is how it is told from a column key.
	driver.OnRow(3, { ibValue(), ibValue(), ibValue(30), ibValue(wxT("Inv-7")) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(30), ibValue() });
	driver.OnOutputEnd(true);

	EXPECT_EQ(2, driver.GetRowsWritten());                       // the heading AND the record
	EXPECT_EQ(wxT("Inv-7"), doc->GetCellValue(2, 0).Trim(false));  // what the record says, on the left
	EXPECT_EQ(wxT("30"),    doc->GetCellValue(2, 1));              // …and its figure under its column
}

// ⭐ AN OUTPUT WITH NO COLUMN AXIS IS THE ORDINARY REPORT, printed as it arrives. The table layout is
// not a fallback and not a mode a report can drift into — it is taken only when there is something
// to lay out across the page.
TEST(SpreadsheetCross, WithNoColumnAxisTheStreamingLayoutIsUsed)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	ibCompositionOutputInfo info;
	info.m_kind      = ibCompositionOutputKind::Grouping;
	info.m_schema    = { Dim(wxT("Partner"), 0), Measure(wxT("Amount")) };
	info.m_rowLevels = 1;
	driver.OnOutputBegin(info);

	// The header is written straight away, which is exactly what a table cannot do.
	EXPECT_EQ(wxT("Partner"), doc->GetCellValue(0, 0));

	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnOutputEnd(true);
	EXPECT_EQ(1, driver.GetRowsWritten());
}

// ⭐⭐ THE COLUMN TOTALS ARE THE ROOT'S CELLS, and they land in the bottom row under the columns they
// belong to. They used to cost a SECOND FOLD of the whole output, on the argument that one tree
// could not hold both sets of subtotals — true of one chain, not of a tree where every heading
// carries its own column branch.
//
// The corner is the grand total — one sentence read across and closed at the right, not two rows
// saying the same thing.
TEST(SpreadsheetCross, ColumnTotalsComeFromTheRootAndCloseTheTable)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	const std::vector<ibQueryLowering::OutputColumn> schema =
		{ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Measure(wxT("Amount")) };

	driver.OnOutputBegin(CrossInfo(schema, 1));
	driver.OnGroupBegin(0, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(), ibValue(100) });          // the grand total
	// ⭐ ITS CELLS COME NEXT — the column totals. The fold hangs the column branch under EVERY
	// heading, and the root is the heading over everything, so what each column adds up to arrives
	// with the rest of the tree instead of costing a second read of the whole output.
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(60) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(40) });

	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(60) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(60) });
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Beta")), ibValue(), ibValue(40) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(40) });

	driver.OnOutputEnd(true);

	// Header, Alpha, Beta, then the bottom line.
	EXPECT_EQ(wxT("Total"), doc->GetCellValue(3, 0));
	EXPECT_EQ(wxT("60"),    doc->GetCellValue(3, 1));
	EXPECT_EQ(wxT("40"),    doc->GetCellValue(3, 2));
	EXPECT_EQ(wxT("100"),   doc->GetCellValue(3, 3));   // the corner
}

// ⭐⭐ THE TOTALS SETTLE THE COLUMN ORDER. They are the ROOT's cells, so they arrive before any row
// — and every column key is therefore numbered before a row can ask for it. A row that meets the
// keys in a different order (its own second key first) still lands in the columns the header
// announced, which is the whole reason the table is one width for every line.
TEST(SpreadsheetCross, TheColumnTotalsSettleTheOrderOfTheColumns)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	const std::vector<ibQueryLowering::OutputColumn> schema =
		{ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Measure(wxT("Amount")) };

	driver.OnOutputBegin(CrossInfo(schema, 1));
	driver.OnGroupBegin(0, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(), ibValue(100) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(60) });   // …the root's cells
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(40) });

	// The only row meets South FIRST — and South is still the second column.
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(100) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(40) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(60) });

	driver.OnOutputEnd(true);

	EXPECT_EQ(wxT("North"), doc->GetCellValue(0, 1));
	EXPECT_EQ(wxT("South"), doc->GetCellValue(0, 2));
	EXPECT_EQ(wxT("Alpha"), doc->GetCellValue(1, 0).Trim(false));
	EXPECT_EQ(wxT("60"),    doc->GetCellValue(1, 1));   // North's figure under North
	EXPECT_EQ(wxT("40"),    doc->GetCellValue(1, 2));   // …and South's under South
}

// ⭐ AN OUTPUT NAMES ITSELF over its own block. The name travelled from the composer to the driver
// and was read by nobody (audit § C8) — so a report of two outputs printed two blocks of figures
// with nothing to say which was which.
TEST(SpreadsheetCompose, AnOutputPrintsItsOwnName)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	ibCompositionOutputInfo info;
	info.m_schema    = { Dim(wxT("Partner"), 0), Measure(wxT("Amount")) };
	info.m_rowLevels = 1;
	info.m_name      = wxT("By partner");
	driver.OnOutputBegin(info);
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(100) });
	driver.OnOutputEnd(true);

	EXPECT_EQ(wxT("By partner"), doc->GetCellValue(0, 0));   // the caption, above its header
	EXPECT_EQ(wxT("Partner"),    doc->GetCellValue(1, 0));
}

// ⭐⭐ A COLUMN AXIS DEEPER THAN ONE LEVEL GETS SUBTOTAL COLUMNS. Warehouse then Month: a figure per
// month, and after the last month of a warehouse, that warehouse's own — which the fold already
// computed at the upper node and which would otherwise be thrown away.
TEST(SpreadsheetCross, AnUpperColumnHeadingGetsItsOwnTotalColumn)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnOutputBegin(CrossInfo(
		{ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Dim(wxT("Month"), 2), Measure(wxT("Amount")) }, 1));

	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(), ibValue(100) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(wxT("North")), ibValue(), ibValue(70) });
	driver.OnGroupBegin(3, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(), ibValue(wxT("Jan")), ibValue(30) });
	driver.OnGroupBegin(3, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(), ibValue(wxT("Feb")), ibValue(40) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(wxT("South")), ibValue(), ibValue(30) });
	driver.OnGroupBegin(3, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(), ibValue(wxT("Jan")), ibValue(30) });

	driver.OnOutputEnd(true);

	// ⭐ A HEADING'S TOTAL CLOSES IT (Max, 2026-08-26) — the children first, the figure that sums them
	// after, the way a printed report reads down the page:
	// Columns: Jan, Feb, [North total], Jan, [South total], then the row total.
	//   dim=0    1    2        3         4         5              6
	EXPECT_EQ(wxT("70"),  doc->GetCellValue(2, 1)) << "North's own figure, BEFORE its months";
	EXPECT_EQ(wxT("30"),  doc->GetCellValue(2, 2));
	EXPECT_EQ(wxT("40"),  doc->GetCellValue(2, 3));
	EXPECT_EQ(wxT("30"),  doc->GetCellValue(2, 4)) << "South's own figure";
	EXPECT_EQ(wxT("30"),  doc->GetCellValue(2, 5));
	EXPECT_EQ(wxT("100"), doc->GetCellValue(2, 6)) << "and the row total closes the table";

	// ⭐ THE HEADING COVERS ITS WHOLE GROUP — its total and its months — so the page says whose the
	// months are. It is written on the run's first column and merged across the rest (Max: "the
	// grouping has to run to the end").
	EXPECT_EQ(wxT("North"), doc->GetCellValue(0, 1));
	EXPECT_EQ(wxT("Total"), doc->GetCellValue(1, 1)) << "its total is the FIRST column inside it";
	EXPECT_EQ(wxT("Jan"),   doc->GetCellValue(1, 2));
	EXPECT_EQ(wxT("Feb"),   doc->GetCellValue(1, 3));
	EXPECT_EQ(wxT("South"), doc->GetCellValue(0, 4));
}

// ⚠ AND A SINGLE-LEVEL COLUMN AXIS IS LAID OUT EXACTLY AS BEFORE — nothing totals a prefix, so no
// prefix gets a column. The common table must not pay for the deep one.
TEST(SpreadsheetCross, OneColumnLevelGetsNoSubtotalColumns)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnOutputBegin(CrossInfo({ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Measure(wxT("Amount")) }, 1));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(70) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("North")), ibValue(30) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(wxT("South")), ibValue(40) });
	driver.OnOutputEnd(true);

	EXPECT_EQ(wxT("North"), doc->GetCellValue(0, 1));
	EXPECT_EQ(wxT("South"), doc->GetCellValue(0, 2));
	EXPECT_EQ(wxT("Total"), doc->GetCellValue(0, 3));   // the ROW total, immediately after the keys
	EXPECT_EQ(wxT("30"), doc->GetCellValue(1, 1));
	EXPECT_EQ(wxT("40"), doc->GetCellValue(1, 2));
	EXPECT_EQ(wxT("70"), doc->GetCellValue(1, 3));
}

// ⭐⭐ WIDTHS BELONG TO THE SHEET, NOT TO THE OUTPUT. Two outputs print onto one sheet, so column 0
// is the SAME column for both and has to fit whichever of them puts more there.
//
// 🛑 IT WAS RESET PER OUTPUT (`m_widest.assign(...)` in OnColumns), so a second, narrower report
// re-sized the shared columns to its own text and the first report's values were clipped in place —
// silently, since nothing about a too-narrow column says it is too narrow.
TEST(SpreadsheetCompose, ASecondOutputDoesNotShrinkTheFirstsColumns)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	// First output: a long value in column 0.
	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("A very long partner name indeed")), ibValue(10) });
	driver.OnOutputEnd(true);
	const int afterFirst = doc->GetColSize(0);

	// Second output onto the same sheet: a short value in the same column.
	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("X"), 0), Measure(wxT("N")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("ab")), ibValue(1) });
	driver.OnOutputEnd(true);

	EXPECT_EQ(afterFirst, doc->GetColSize(0))
		<< "the shared column must still fit the widest text any output put in it";
}

// …and a LATER output that needs MORE room gets it: the sheet grows, it does not merely hold.
TEST(SpreadsheetCompose, ASecondOutputWidensAColumnWhenItNeedsMore)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("P"), 0), Measure(wxT("A")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("ab")), ibValue(1) });
	driver.OnOutputEnd(true);
	const int afterFirst = doc->GetColSize(0);

	driver.OnOutputBegin(SchemaInfo({ Dim(wxT("Partner"), 0), Measure(wxT("Amount")) }));
	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("A very long partner name indeed")), ibValue(10) });
	driver.OnOutputEnd(true);

	EXPECT_GT(doc->GetColSize(0), afterFirst);
}

// ⭐⭐ A GROUP'S TOTAL COLUMN IS PRINTED EVEN OVER A SINGLE CHILD, and the reason is the fold: it is
// the group's OWN column — the one thing left on screen when its children are hidden — because a
// column group, unlike a row group, has no heading line of its own to stay behind on.
//
// 🛑 Both other readings were tried on 2026-08-26 and both broke the fold: the total after its
// children ("the order a report reads down the page"), and no total over a single child ("the group
// shows it anyway"). The reference report settles it — a collapsed period shows exactly one column,
// carrying its own figure, single child or not.
TEST(SpreadsheetCross, EveryHeadingGetsItsOwnTotalColumnEvenOverOneChild)
{
	auto doc = MakeDocument();
	ibSpreadsheetComposeDriver driver(doc.get());

	driver.OnOutputBegin(CrossInfo(
		{ Dim(wxT("Partner"), 0), Dim(wxT("Warehouse"), 1), Dim(wxT("Month"), 2), Measure(wxT("Amount")) }, 1));

	driver.OnGroupBegin(1, ibSelectorNodeKind::Group, true, false, { ibValue(wxT("Alpha")), ibValue(), ibValue(), ibValue(100) });
	// North has TWO months, South has ONE — both get a total column all the same.
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(wxT("North")), ibValue(), ibValue(70) });
	driver.OnGroupBegin(3, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(), ibValue(wxT("Jan")), ibValue(30) });
	driver.OnGroupBegin(3, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(), ibValue(wxT("Feb")), ibValue(40) });
	driver.OnGroupBegin(2, ibSelectorNodeKind::Group, true, false, { ibValue(), ibValue(wxT("South")), ibValue(), ibValue(30) });
	driver.OnGroupBegin(3, ibSelectorNodeKind::Group, false, false, { ibValue(), ibValue(), ibValue(wxT("Jan")), ibValue(30) });

	driver.OnOutputEnd(true);

	// Columns: [North total], Jan, Feb, [South total], Jan, then the row total.
	//   dim=0        1         2    3        4          5         6
	EXPECT_EQ(wxT("70"),  doc->GetCellValue(2, 1)) << "North opens with what it adds up to";
	EXPECT_EQ(wxT("30"),  doc->GetCellValue(2, 2));
	EXPECT_EQ(wxT("40"),  doc->GetCellValue(2, 3));
	EXPECT_EQ(wxT("30"),  doc->GetCellValue(2, 4)) << "South's total — the same figure as its one child";
	EXPECT_EQ(wxT("30"),  doc->GetCellValue(2, 5));
	EXPECT_EQ(wxT("100"), doc->GetCellValue(2, 6)) << "and the row total closes the table";
}
