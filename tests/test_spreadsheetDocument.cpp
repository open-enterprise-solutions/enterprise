////////////////////////////////////////////////////////////////////////////
//	Description : ibBackendSpreadsheetObject — the SERVER-SIDE spreadsheet
//	              document. No window, no database, no wxApp: the document IS
//	              the result, and a view is a subscriber to it.
//
//	              What is pinned here: cells and their growth, document
//	              parameters and the two fill types that read them, drill-down,
//	              merge, freeze, print breaks, outline groups, the area verbs
//	              (Get / Put / Join) that templates are built from, and the
//	              notifier contract a renderer relies on.
//
//	⚠ THE CRT LEAK DUMP ON EXIT IS EXPECTED AND IS NOT A LEAK — every cell holds
//	  a wxFont and two wxColour members seeded from wxSystemSettings, and those
//	  are released by wxEntryCleanup, which a console test never runs. Diagnosed
//	  2026-08-18 down to a bare ibSpreadsheetDescription on the stack. See the
//	  same note in test_spreadsheetCompose.cpp.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/backend_spreadsheet.h"
#include "backend/backend_localization.h"

namespace {

// ⚠ A COMPUTED CELL IS LOCALISABLE TEXT, not a bare string. The parameter and
// template fill types run their result through the localisation layer, so what
// comes back is a raw loc-text envelope (`en = '42';`) and has to be translated
// before it is compared. Plain TEXT cells skip that entirely — the asymmetry is
// real and is why these helpers exist rather than a direct EXPECT_EQ.
wxString Translated(const wxString& raw) {
	return ibBackendLocalization::GetTranslateGetRawLocText(raw);
}
wxString Localised(const wxString& plain) {
	return ibBackendLocalization::CreateLocalizationRawLocText(plain);
}

wxObjectDataPtr<ibBackendSpreadsheetObject> MakeDocument() {
	return wxObjectDataPtr<ibBackendSpreadsheetObject>(new ibBackendSpreadsheetObject());
}

// A notifier that counts what it was told. The document's whole contract with a
// renderer is these calls, so a test asserts the CALLS rather than any drawing.
class ibCountingNotifier : public ibBackendSpreadsheetNotifier {
public:
	int m_cleared = 0, m_values = 0, m_rowFreeze = -1, m_areasPut = 0;

	void ClearSpreadsheet() override { ++m_cleared; }
	void EnableEditing(bool) override {}
	void SetRowSize(int, int) override {}
	void SetColSize(int, int) override {}
	void SetRowFreeze(int row) override { m_rowFreeze = row; }
	void SetColFreeze(int) override {}
	void SetCellBackgroundColour(int, int, const wxColour&) override {}
	void SetCellTextColour(int, int, const wxColour&) override {}
	void SetCellTextOrient(int, int, const int) override {}
	void SetCellFont(int, int, const wxFont&) override {}
	void SetCellAlignment(int, int, const int, const int) override {}
	void SetCellBorderLeft(int, int, const ibSpreadsheetBorderDescription&) override {}
	void SetCellBorderRight(int, int, const ibSpreadsheetBorderDescription&) override {}
	void SetCellBorderTop(int, int, const ibSpreadsheetBorderDescription&) override {}
	void SetCellBorderBottom(int, int, const ibSpreadsheetBorderDescription&) override {}
	void SetCellSize(int, int, int, int) override {}
	void SetCellFitMode(int, int, ibSpreadsheetCellDescription::ibFitMode) override {}
	void SetCellReadOnly(int, int, bool) override {}
	void AddRowBrake(int) override {}
	void AddColBrake(int) override {}
	void DeleteRowBrake(int) override {}
	void DeleteColBrake(int) override {}
	void SetRowBrake(int) override {}
	void SetColBrake(int) override {}
	void SetCellValue(int, int, const wxString&) override { ++m_values; }
	void PutArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>&, unsigned int) override { ++m_areasPut; }
	void JoinArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>&, unsigned int) override {}
};

} // namespace

// ---------------------------------------------------------------------------
//  Cells
// ---------------------------------------------------------------------------

// A fresh document is empty and has no extent — nothing is reserved up front.
TEST(SpreadsheetDocument, Fresh_IsEmpty)
{
	auto doc = MakeDocument();
	EXPECT_TRUE(doc->IsEmptyDocument());
	EXPECT_EQ(0, doc->GetNumberRows());
	EXPECT_EQ(0, doc->GetNumberCols());
}

// THE EXTENT FOLLOWS THE WRITES. Writing one far cell is what makes the document
// that big — there is no separate "resize" step to forget.
TEST(SpreadsheetDocument, Write_GrowsTheExtent)
{
	auto doc = MakeDocument();
	doc->SetCellValue(3, 2, wxT("x"));

	EXPECT_FALSE(doc->IsEmptyDocument());
	EXPECT_EQ(wxT("x"), doc->GetCellValue(3, 2));
	EXPECT_LE(4, doc->GetNumberRows());
	EXPECT_LE(3, doc->GetNumberCols());
}

// A cell nobody wrote reads as empty rather than as an error — the whole grid is
// sparse, and asking about a cell that was never touched is ordinary.
TEST(SpreadsheetDocument, UnwrittenCell_ReadsEmpty)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("x"));

	EXPECT_TRUE(doc->GetCellValue(5, 5).IsEmpty());
	EXPECT_TRUE(doc->IsEmptyCell(5, 5));
	EXPECT_FALSE(doc->IsEmptyCell(0, 0));
}

TEST(SpreadsheetDocument, Clear_EmptiesTheDocument)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("x"));
	doc->SetCellValue(1, 0, wxT("y"));

	doc->ClearSpreadsheet();

	EXPECT_TRUE(doc->IsEmptyDocument());
	EXPECT_TRUE(doc->GetCellValue(0, 0).IsEmpty());
}

// ---------------------------------------------------------------------------
//  Parameters — the document's own named values
// ---------------------------------------------------------------------------

TEST(SpreadsheetDocument, Parameter_RoundTrips)
{
	auto doc = MakeDocument();
	doc->SetParameter(wxT("Partner"), ibValue(wxT("Alpha")));

	ibValue out;
	EXPECT_TRUE(doc->GetParameter(wxT("Partner"), out));
	EXPECT_EQ(wxT("Alpha"), out.GetString());
}

// A MISSING PARAMETER ANSWERS FALSE, not an empty value that reads like a set one.
// Templates rely on the difference: an unknown token renders empty, and the caller
// still knows nobody supplied it.
TEST(SpreadsheetDocument, MissingParameter_AnswersFalse)
{
	auto doc = MakeDocument();
	ibValue out;
	EXPECT_FALSE(doc->GetParameter(wxT("Nobody"), out));
}

// FILL TYPE "PARAMETER": the cell's whole text IS the parameter name.
TEST(SpreadsheetDocument, FillTypeParameter_ResolvesWholeText)
{
	auto doc = MakeDocument();
	doc->SetParameter(wxT("Total"), ibValue(wxT("42")));

	EXPECT_EQ(wxT("42"), Translated(doc->ComputeStringValueFromParameters(
		wxT("Total"), ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter)));
	// Nobody supplied it -> empty, never the name itself: a report showing the word
	// "Total" where a number belongs is worse than a blank.
	EXPECT_TRUE(doc->ComputeStringValueFromParameters(
		wxT("Missing"), ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter).IsEmpty());
}

// FILL TYPE "TEMPLATE": [tokens] inside running text are replaced in place.
TEST(SpreadsheetDocument, FillTypeTemplate_ReplacesBracketedTokens)
{
	auto doc = MakeDocument();
	doc->SetParameter(wxT("Name"), ibValue(wxT("Alpha")));
	doc->SetParameter(wxT("Sum"), ibValue(wxT("10")));

	// The template ITSELF is localisable text — it is authored in a template cell, and
	// a bare string translates to nothing at all.
	const wxString out = Translated(doc->ComputeStringValueFromParameters(
		Localised(wxT("[Name] owes [Sum]")), ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate));

	EXPECT_TRUE(out.Contains(wxT("Alpha")));
	EXPECT_TRUE(out.Contains(wxT("10")));
	EXPECT_FALSE(out.Contains(wxT("[")));
}

// An unknown token disappears rather than staying on the page as `[Whoever]`.
TEST(SpreadsheetDocument, FillTypeTemplate_UnknownTokenRendersEmpty)
{
	auto doc = MakeDocument();
	const wxString out = doc->ComputeStringValueFromParameters(
		wxT("[Whoever]"), ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate);
	EXPECT_FALSE(out.Contains(wxT("Whoever")));
}

// Plain text is returned untouched — the default fill type computes nothing.
TEST(SpreadsheetDocument, FillTypeText_IsLeftAlone)
{
	auto doc = MakeDocument();
	doc->SetParameter(wxT("Name"), ibValue(wxT("Alpha")));
	EXPECT_EQ(wxT("[Name]"), doc->ComputeStringValueFromParameters(
		wxT("[Name]"), ibSpreadsheetFillType::ibSpreadsheetFillType_StrText));
}

// ---------------------------------------------------------------------------
//  Drill-down, merge, freeze, breaks, sizes
// ---------------------------------------------------------------------------

TEST(SpreadsheetDocument, DetailsParameter_RoundTrips)
{
	auto doc = MakeDocument();
	doc->SetCellValue(1, 1, wxT("Alpha"));
	doc->SetCellDetailsParameter(1, 1, wxT("Cell_1_1"));

	wxString name;
	doc->GetCellDetailsParameter(1, 1, name);
	EXPECT_EQ(wxT("Cell_1_1"), name);
}

TEST(SpreadsheetDocument, CellSize_MergesAcrossColumns)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("Title"));
	doc->SetCellSize(0, 0, 1, 3);

	int rows = 0, cols = 0;
	doc->GetCellSize(0, 0, &rows, &cols);
	EXPECT_EQ(1, rows);
	EXPECT_EQ(3, cols);
}

TEST(SpreadsheetDocument, Freeze_IsRememberedPerAxis)
{
	auto doc = MakeDocument();
	doc->SetRowFreeze(2);
	doc->SetColFreeze(1);

	EXPECT_EQ(2, doc->GetRowFreeze());
	EXPECT_EQ(1, doc->GetColFreeze());
}

TEST(SpreadsheetDocument, RowBreak_AddedAndRemoved)
{
	auto doc = MakeDocument();
	doc->SetCellValue(5, 0, wxT("x"));
	doc->AddRowBrake(3);

	EXPECT_TRUE(doc->IsRowBrake(3));
	EXPECT_LE(3, doc->GetMaxRowBrake());

	doc->DeleteRowBrake(3);
	EXPECT_FALSE(doc->IsRowBrake(3));
}

// REGRESSION (2026-08-18). Deleting a break that is not there used to run
// `erase(std::remove(…))` — half the idiom — which erases end() on a miss, i.e.
// undefined behaviour. The verb has to be a no-op instead.
TEST(SpreadsheetDocument, DeletingAMissingBreak_IsANoOp)
{
	auto doc = MakeDocument();
	doc->SetCellValue(5, 0, wxT("x"));
	doc->AddRowBrake(3);

	doc->DeleteRowBrake(999);   // never added
	doc->DeleteColBrake(999);

	EXPECT_TRUE(doc->IsRowBrake(3));   // the real one survived
	doc->DeleteRowBrake(3);
	doc->DeleteRowBrake(3);            // …and deleting it twice is a no-op too
	EXPECT_FALSE(doc->IsRowBrake(3));
}

// REGRESSION (2026-08-18). Same defect on the index accessors: the bound was
// `idx > size()`, so asking for the one-past-the-end index handed back a pointer
// into nothing instead of null. Seven accessors carried it.
TEST(SpreadsheetDocument, IndexAccessors_RefusePastTheEnd)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("x"));
	doc->AddRowBrake(1);

	const ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();
	EXPECT_NE(nullptr, desc.GetCellByIdx(0));
	EXPECT_EQ(nullptr, desc.GetCellByIdx((size_t)desc.GetCellCount()));
	EXPECT_EQ(nullptr, desc.GetRowAreaByIdx(99));
	EXPECT_EQ(nullptr, desc.GetRowSizeByIdx(99));
}

TEST(SpreadsheetDocument, RowAndColSize_RoundTrip)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("x"));
	doc->SetRowSize(0, 40);
	doc->SetColSize(0, 120);

	EXPECT_EQ(40, doc->GetRowSize(0));
	EXPECT_EQ(120, doc->GetColSize(0));
}

// Sizes are found through an index by address (see spreadsheetDescription.h) while the vector
// keeps them in insertion order. This pins the two staying in step: setting the same line twice
// must REPLACE, not append, and reading by position must still see what was written.
TEST(SpreadsheetDocument, RowAndColSize_SetTwiceReplacesRatherThanAppends)
{
	auto doc = MakeDocument();

	doc->SetRowSize(5, 40);
	doc->SetRowSize(2, 30);
	doc->SetRowSize(5, 60);          // the same line again — must not become a second entry

	doc->SetColSize(3, 120);
	doc->SetColSize(3, 90);

	const ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();
	EXPECT_EQ(2, desc.GetSizeNumberRows());
	EXPECT_EQ(1, desc.GetSizeNumberCols());

	EXPECT_EQ(60, doc->GetRowSize(5));
	EXPECT_EQ(30, doc->GetRowSize(2));
	EXPECT_EQ(90, doc->GetColSize(3));

	// Insertion order, which the serializer reads by position: row 5 was declared first.
	ASSERT_NE(nullptr, desc.GetRowSizeByIdx(0));
	ASSERT_NE(nullptr, desc.GetRowSizeByIdx(1));
	EXPECT_EQ(5u, desc.GetRowSizeByIdx(0)->m_row);
	EXPECT_EQ(60u, desc.GetRowSizeByIdx(0)->m_height);
	EXPECT_EQ(2u, desc.GetRowSizeByIdx(1)->m_row);

	// A line nobody declared answers the default rather than the neighbour's size.
	EXPECT_NE(60, doc->GetRowSize(4));
}

// ---------------------------------------------------------------------------
//  Outline groups — what makes a composed report fold
// ---------------------------------------------------------------------------

// A group spans from the row count at Begin to the row count at End. The pair is a
// STACK, so nesting is expressed by nesting the calls rather than by arithmetic.
TEST(SpreadsheetDocument, RowGroup_SpansTheRowsBetweenBeginAndEnd)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("head"));

	doc->BeginRowGroup();
	doc->SetCellValue(1, 0, wxT("a"));
	doc->SetCellValue(2, 0, wxT("b"));
	doc->EndRowGroup();

	const ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();
	ASSERT_EQ(1, desc.GetGroupNumberRows());
	const ibSpreadsheetGroupDescription* group = desc.GetRowGroupByIdx(0);
	ASSERT_NE(nullptr, group);
	EXPECT_LE(group->m_start, group->m_end);
}

TEST(SpreadsheetDocument, NestedRowGroups_BothRecorded)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("head"));

	doc->BeginRowGroup();
	doc->SetCellValue(1, 0, wxT("outer"));
	doc->BeginRowGroup();
	doc->SetCellValue(2, 0, wxT("inner"));
	doc->EndRowGroup();
	doc->EndRowGroup();

	EXPECT_EQ(2, doc->GetSpreadsheetDesc().GetGroupNumberRows());
}

// ---------------------------------------------------------------------------
//  Areas — the template mechanism
// ---------------------------------------------------------------------------

// GetArea lifts a rectangle out and RE-ORIGINS it at (0,0) — an area is a document
// in its own right, not a view onto its parent.
TEST(SpreadsheetDocument, GetArea_ReOriginsTheRectangle)
{
	auto doc = MakeDocument();
	doc->SetCellValue(1, 1, wxT("inside"));
	doc->SetCellValue(0, 0, wxT("outside"));

	const ibSpreadsheetDescription area = doc->GetArea(1, 2, 1, 2);
	EXPECT_EQ(wxT("inside"), area.GetCellValue(0, 0));
}

// PutArea APPENDS BELOW: the area lands starting at the current row count, so the
// caller composes by repeating "put" rather than by tracking coordinates.
TEST(SpreadsheetDocument, PutArea_AppendsBelow)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("header"));
	const int before = doc->GetNumberRows();

	auto area = MakeDocument();
	area->SetCellValue(0, 0, wxT("row"));

	doc->PutArea(area);

	EXPECT_LT(before, doc->GetNumberRows());
	EXPECT_EQ(wxT("row"), doc->GetCellValue(before, 0));
	EXPECT_EQ(wxT("header"), doc->GetCellValue(0, 0));   // what was there stays
}

// A TEMPLATE CELL IS RESOLVED AS IT LANDS, against the AREA's parameters — which is
// what makes a template a template: fill the area's parameters, put it, repeat.
TEST(SpreadsheetDocument, PutArea_ResolvesTemplateCellsFromTheAreaParameters)
{
	auto doc = MakeDocument();

	auto area = MakeDocument();
	area->SetCellValue(0, 0, Localised(wxT("[Name]")));
	area->SetCellFillType(0, 0, ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate);
	area->SetParameter(wxT("Name"), ibValue(wxT("Alpha")));

	doc->PutArea(area);

	EXPECT_EQ(wxT("Alpha"), Translated(doc->GetCellValue(0, 0)));
	// …and it lands as TEXT: resolving it twice would look for parameters the
	// receiving document does not have.
	EXPECT_EQ(ibSpreadsheetFillType::ibSpreadsheetFillType_StrText, doc->GetCellFillType(0, 0));
}

// The same area put TWICE keeps both copies' drill-down, because the parameter name
// is made unique per landing position — otherwise the second row would decode to the
// first row's value.
TEST(SpreadsheetDocument, PutAreaTwice_KeepsBothDrillDowns)
{
	auto doc = MakeDocument();

	auto area = MakeDocument();
	area->SetCellValue(0, 0, wxT("row"));
	area->SetCellDetailsParameter(0, 0, wxT("Ref"));
	area->SetParameter(wxT("Ref"), ibValue(wxT("first")));

	doc->PutArea(area);
	const int secondRow = doc->GetNumberRows();

	area->SetParameter(wxT("Ref"), ibValue(wxT("second")));
	doc->PutArea(area);

	wxString firstName, secondName;
	doc->GetCellDetailsParameter(0, 0, firstName);
	doc->GetCellDetailsParameter(secondRow, 0, secondName);

	EXPECT_FALSE(firstName.IsEmpty());
	EXPECT_FALSE(secondName.IsEmpty());
	EXPECT_NE(firstName, secondName);                       // one name per position
	EXPECT_EQ(wxT("first"), doc->GetParameter(firstName).GetString());
	EXPECT_EQ(wxT("second"), doc->GetParameter(secondName).GetString());
}

// A group level on the put wraps exactly the rows that landed.
TEST(SpreadsheetDocument, PutArea_WithGroupLevel_RecordsAGroup)
{
	auto doc = MakeDocument();

	auto area = MakeDocument();
	area->SetCellValue(0, 0, wxT("a"));
	area->SetCellValue(1, 0, wxT("b"));

	doc->PutArea(area, /*groupLevel*/1);

	EXPECT_EQ(1, doc->GetSpreadsheetDesc().GetGroupNumberRows());
}

// JoinArea appends to the RIGHT — the other axis of the same verb.
TEST(SpreadsheetDocument, JoinArea_AppendsToTheRight)
{
	auto doc = MakeDocument();
	doc->SetCellValue(0, 0, wxT("left"));
	const int before = doc->GetNumberCols();

	auto area = MakeDocument();
	area->SetCellValue(0, 0, wxT("right"));

	doc->JoinArea(area);

	EXPECT_LT(before, doc->GetNumberCols());
	EXPECT_EQ(wxT("left"), doc->GetCellValue(0, 0));
	EXPECT_EQ(wxT("right"), doc->GetCellValue(0, before));
}

// ---------------------------------------------------------------------------
//  Notifiers — the contract a renderer subscribes to
// ---------------------------------------------------------------------------

// EVERY MUTATION IS ANNOUNCED. This is what lets the grid be a reader of the
// document rather than a second copy of it.
TEST(SpreadsheetDocument, Notifier_HearsWritesAndClears)
{
	auto doc = MakeDocument();
	auto notifier = doc->AddNotifier<ibCountingNotifier>();
	ibCountingNotifier* counter = static_cast<ibCountingNotifier*>(notifier.get());

	doc->SetCellValue(0, 0, wxT("x"));
	doc->SetRowFreeze(1);
	doc->ClearSpreadsheet();

	EXPECT_EQ(1, counter->m_values);
	EXPECT_EQ(1, counter->m_rowFreeze);
	EXPECT_EQ(1, counter->m_cleared);
}

// A removed notifier hears nothing more — a view that closed must not keep being
// told about a document that outlives it.
TEST(SpreadsheetDocument, RemovedNotifier_HearsNothingMore)
{
	auto doc = MakeDocument();
	auto notifier = doc->AddNotifier<ibCountingNotifier>();
	ibCountingNotifier* counter = static_cast<ibCountingNotifier*>(notifier.get());

	doc->SetCellValue(0, 0, wxT("x"));
	doc->RemoveNotifier(notifier);
	doc->SetCellValue(1, 0, wxT("y"));

	EXPECT_EQ(1, counter->m_values);
}

// REGRESSION (2026-08-18). Removing a notifier twice — a view closing after it was
// already detached — used to erase end(). It must be a no-op, and the OTHER
// subscribers must be untouched by it.
TEST(SpreadsheetDocument, RemovingANotifierTwice_IsANoOp)
{
	auto doc = MakeDocument();
	auto first = doc->AddNotifier<ibCountingNotifier>();
	auto second = doc->AddNotifier<ibCountingNotifier>();

	doc->RemoveNotifier(first);
	doc->RemoveNotifier(first);   // already gone

	doc->SetCellValue(0, 0, wxT("x"));

	EXPECT_EQ(0, static_cast<ibCountingNotifier*>(first.get())->m_values);
	EXPECT_EQ(1, static_cast<ibCountingNotifier*>(second.get())->m_values);
}

// Two views on one document both hear it — the reason the notifier list is a list.
TEST(SpreadsheetDocument, TwoNotifiers_BothHear)
{
	auto doc = MakeDocument();
	auto first = doc->AddNotifier<ibCountingNotifier>();
	auto second = doc->AddNotifier<ibCountingNotifier>();

	doc->SetCellValue(0, 0, wxT("x"));

	EXPECT_EQ(1, static_cast<ibCountingNotifier*>(first.get())->m_values);
	EXPECT_EQ(1, static_cast<ibCountingNotifier*>(second.get())->m_values);
}

// ---------------------------------------------------------------------------
//  Identity
// ---------------------------------------------------------------------------

// Each document is born with its own guid — two documents are never the same one.
TEST(SpreadsheetDocument, EachDocument_HasItsOwnGuid)
{
	auto first = MakeDocument();
	auto second = MakeDocument();
	EXPECT_NE(first->GetDocGuid(), second->GetDocGuid());
}

// ---------------------------------------------------------------------------
//  Taking an area — HOW WIDE IT IS
//
//  ⭐ THE FREE SIDE OF A HALF-SPECIFIED AREA IS BOUNDED BY THE CONTENT, never by
//  the page breaks. Both doors below (by name, by coordinates) used to ask
//  GetMaxColBrake() / GetMaxRowBrake() for it — the position of the last PAGE
//  BREAK, which is 0 on a sheet that declares none. A break says where the PAPER
//  ends and knows nothing about how many columns were written, so a template
//  authored without one yielded areas exactly ONE column wide; and since every
//  cell of a printed form lives to the right of column 0, the area came back
//  structurally correct — the right number of rows — and completely empty.
//
//  The receiving side already measured the other way (PutArea walks
//  GetNumberCols()), so one width had two roads that disagreed at the ends of a
//  single operation. (2026-08-31, an empty print form.)
//
//  ⚠ THE TWO DOORS DISAGREE ON ONE THING, DELIBERATELY: GetAreaByName's declared
//  band INCLUDES both ends, GetArea's range is EXCLUSIVE on the right
//  (`row < rowRight`). Rows 1..10 is `AddRowArea(name, 1, 10)` there and
//  `GetArea(1, 11)` here. Each test below respects its own convention.
// ---------------------------------------------------------------------------

namespace {

wxString CellText(int row, int col) {
	return wxString::Format(wxT("r%dc%d"), row, col);
}

// A print form the way a real one is built: a margin row and a margin column
// nobody writes into, content in rows 1..10 across columns 1..12, and NO page
// break declared anywhere — a template is authored by placing cells, not by
// saying where the paper ends. Extent: 11 rows, 13 columns.
wxObjectDataPtr<ibBackendSpreadsheetObject> MakePrintForm() {
	auto doc = MakeDocument();
	for (int row = 1; row <= 10; row++)
		for (int col = 1; col <= 12; col++)
			doc->SetCellValue(row, col, CellText(row, col));
	return doc;
}

// Ten written rows in column 0 — a known extent for the break verbs to clamp against.
wxObjectDataPtr<ibBackendSpreadsheetObject> MakeSheetOfRows(int count) {
	auto doc = MakeDocument();
	for (int row = 0; row < count; row++)
		doc->SetCellValue(row, 0, CellText(row, 0));
	return doc;
}

} // namespace

// THE ASSERTION THE DEFECT NEEDED: the cells, at named coordinates — not just a
// fragment with the right number of rows.
TEST(SpreadsheetDocument, GetAreaByName_RowAreaWithNoPageBreak_CarriesEveryColumn)
{
	auto doc = MakePrintForm();
	doc->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);

	const ibSpreadsheetDescription area = doc->GetAreaByName(wxT("Detail"));

	// Ten rows tall — the declaration includes both ends…
	EXPECT_EQ(10, area.GetNumberRows());
	// …and as wide as what is WRITTEN: thirteen columns, 0..12.
	EXPECT_EQ(13, doc->GetNumberCols());
	EXPECT_EQ(13, area.GetNumberCols());

	EXPECT_EQ(10 * 13, area.GetCellCount());
	EXPECT_EQ(CellText(1, 1),   area.GetCellValue(0, 1));
	EXPECT_EQ(CellText(1, 12),  area.GetCellValue(0, 12));
	EXPECT_EQ(CellText(10, 1),  area.GetCellValue(9, 1));
	EXPECT_EQ(CellText(10, 12), area.GetCellValue(9, 12));

	// Column 0 is the form's left margin — nobody wrote there, and lifting the
	// area must not invent content for it either.
	EXPECT_TRUE(area.GetCellValue(0, 0).IsEmpty());

	EXPECT_FALSE(area.IsEmptySpreadsheet());
}

// The fragment marks ITS OWN edges, re-origined at (0,0). Passing the source's
// through put the column mark at 0 on a sheet that declared no break — a page
// break drawn before the first column.
TEST(SpreadsheetDocument, GetAreaByName_RowArea_MarksItsOwnEdgesNotTheSources)
{
	auto doc = MakePrintForm();
	doc->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);

	const ibSpreadsheetDescription area = doc->GetAreaByName(wxT("Detail"));

	EXPECT_LE(0, area.GetMaxRowBrake());
	EXPECT_LE(0, area.GetMaxColBrake());
	EXPECT_EQ(area.GetNumberRows() - 1, area.GetMaxRowBrake());
	EXPECT_EQ(area.GetNumberCols() - 1, area.GetMaxColBrake());
}

// …AND A DECLARED BREAK MUST NOT BECOME AUTHORITATIVE AGAIN. A sheet with a page
// break yields exactly the area the same sheet without one does.
TEST(SpreadsheetDocument, GetAreaByName_WithAColumnBreakDeclared_TakesTheSameFullWidth)
{
	auto plain = MakePrintForm();
	plain->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);

	auto broken = MakePrintForm();
	broken->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);
	broken->AddColBrake(4);                       // the PAPER ends after column 4…

	const ibSpreadsheetDescription plainArea  = plain->GetAreaByName(wxT("Detail"));
	const ibSpreadsheetDescription brokenArea = broken->GetAreaByName(wxT("Detail"));

	// …and the AREA does not: a break is about paper, a width is about content.
	EXPECT_EQ(13, brokenArea.GetNumberCols());
	EXPECT_EQ(10 * 13, brokenArea.GetCellCount());
	EXPECT_EQ(CellText(1, 12), brokenArea.GetCellValue(0, 12));

	// The source's break is not carried either — the fragment marks its own edge.
	EXPECT_FALSE(brokenArea.IsColBrake(4));
	EXPECT_EQ(12, brokenArea.GetMaxColBrake());

	// Declaring a break changed nothing at all about what the area IS.
	EXPECT_TRUE(brokenArea == plainArea);
}

// The other axis of the same rule: a COLUMN area is as tall as the content.
TEST(SpreadsheetDocument, GetAreaByName_ColumnArea_IsAsTallAsTheContent)
{
	auto doc = MakePrintForm();
	doc->GetSpreadsheetDesc().AddColArea(wxT("Money"), 2, 5);

	// The row name is left empty — no row area answers to it, so only the column
	// side is narrowed.
	const ibSpreadsheetDescription area = doc->GetAreaByName(wxT(""), wxT("Money"));

	EXPECT_EQ(11, area.GetNumberRows());   // rows 0..10 — the sheet's own height
	EXPECT_EQ(4, area.GetNumberCols());    // columns 2..5, re-origined at 0
	EXPECT_EQ(CellText(1, 2),  area.GetCellValue(1, 0));
	EXPECT_EQ(CellText(10, 5), area.GetCellValue(10, 3));

	EXPECT_EQ(area.GetNumberRows() - 1, area.GetMaxRowBrake());
	EXPECT_EQ(area.GetNumberCols() - 1, area.GetMaxColBrake());
}

// Both axes named: the rectangle, and nothing free to be got wrong.
TEST(SpreadsheetDocument, GetAreaByName_BothAxesNamed_TakesTheRectangle)
{
	auto doc = MakePrintForm();
	doc->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);
	doc->GetSpreadsheetDesc().AddColArea(wxT("Money"), 2, 5);

	const ibSpreadsheetDescription area = doc->GetAreaByName(wxT("Detail"), wxT("Money"));

	EXPECT_EQ(10, area.GetNumberRows());
	EXPECT_EQ(4, area.GetNumberCols());
	EXPECT_EQ(CellText(1, 2),  area.GetCellValue(0, 0));
	EXPECT_EQ(CellText(10, 5), area.GetCellValue(9, 3));
	EXPECT_EQ(9, area.GetMaxRowBrake());
	EXPECT_EQ(3, area.GetMaxColBrake());
}

// A name nobody declared yields nothing at all — not a one-column ghost.
TEST(SpreadsheetDocument, GetAreaByName_UnknownName_YieldsAnEmptyFragment)
{
	auto doc = MakePrintForm();
	doc->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);

	const ibSpreadsheetDescription area = doc->GetAreaByName(wxT("Nobody"));

	EXPECT_TRUE(area.IsEmptySpreadsheet());
	EXPECT_EQ(0, area.GetCellCount());
	EXPECT_EQ(0, area.GetNumberRows());
	EXPECT_EQ(0, area.GetNumberCols());
}

// ---------------------------------------------------------------------------
//  …and the coordinate-taking twin, which carried the same defects
// ---------------------------------------------------------------------------

TEST(SpreadsheetDocument, GetArea_RowsOnly_TakesTheFullWidthWithContent)
{
	auto doc = MakePrintForm();

	// EXCLUSIVE on the right — rows 1..10 is written `1, 11` here.
	const ibSpreadsheetDescription area = doc->GetArea(1, 11, -1, -1);

	EXPECT_EQ(10, area.GetNumberRows());
	EXPECT_EQ(13, area.GetNumberCols());
	EXPECT_EQ(10 * 13, area.GetCellCount());

	// ⚠ The origin on this branch is 0, NOT `-colTop`. colTop is the ABSENCE
	// marker (-1) here, and subtracting it shifted every cell one column right —
	// a sentinel used as an origin.
	EXPECT_EQ(CellText(1, 1),   area.GetCellValue(0, 1));
	EXPECT_EQ(CellText(10, 12), area.GetCellValue(9, 12));
	EXPECT_TRUE(area.GetCellValue(0, 0).IsEmpty());
}

TEST(SpreadsheetDocument, GetArea_ColumnsOnly_TakesTheFullHeightWithContent)
{
	auto doc = MakePrintForm();

	const ibSpreadsheetDescription area = doc->GetArea(-1, -1, 1, 5);

	EXPECT_EQ(11, area.GetNumberRows());   // rows 0..10 — the sheet's own height
	EXPECT_EQ(4, area.GetNumberCols());    // columns 1..4, exclusive right

	// rowLeft is the absence marker on this branch — the same trap, other axis.
	EXPECT_EQ(CellText(1, 1),  area.GetCellValue(1, 0));
	EXPECT_EQ(CellText(10, 4), area.GetCellValue(10, 3));

	EXPECT_EQ(area.GetNumberRows() - 1, area.GetMaxRowBrake());
	EXPECT_EQ(area.GetNumberCols() - 1, area.GetMaxColBrake());
}

// ⚠ THE FRAGMENT'S MARK IS AN INDEX INTO THE FRAGMENT, and therefore never
// negative. It used to be computed the other way round (`rowLeft - rowRight`),
// so a three-row fragment shipped with a stored break at −3: inert for
// pagination (no loop counter matches a negative), but GetMaxRowBrake() answered
// a negative number, IsEmptySpreadsheet() could never be true for such a
// fragment, and the value was serialised.
TEST(SpreadsheetDocument, GetArea_FullyBounded_MarksANonNegativeEdgeOfItsOwn)
{
	auto doc = MakePrintForm();

	const ibSpreadsheetDescription area = doc->GetArea(1, 4, 1, 5);

	EXPECT_EQ(3, area.GetNumberRows());
	EXPECT_EQ(4, area.GetNumberCols());
	EXPECT_EQ(CellText(1, 1), area.GetCellValue(0, 0));
	EXPECT_EQ(CellText(3, 4), area.GetCellValue(2, 3));

	EXPECT_LE(0, area.GetMaxRowBrake());
	EXPECT_LE(0, area.GetMaxColBrake());
	EXPECT_EQ(area.GetNumberRows() - 1, area.GetMaxRowBrake());
	EXPECT_EQ(area.GetNumberCols() - 1, area.GetMaxColBrake());
}

// ⭐ ONE BAND, TWO DOORS, ONE ANSWER. The halves of the file disagreed once; this
// is the assertion that says they may not again.
TEST(SpreadsheetDocument, GetArea_AndGetAreaByName_DescribeTheSameBandIdentically)
{
	auto doc = MakePrintForm();
	doc->GetSpreadsheetDesc().AddRowArea(wxT("Detail"), 1, 10);

	const ibSpreadsheetDescription byName   = doc->GetAreaByName(wxT("Detail"));
	const ibSpreadsheetDescription byCoords = doc->GetArea(1, 11, -1, -1);   // exclusive right

	EXPECT_EQ(byName.GetNumberRows(),  byCoords.GetNumberRows());
	EXPECT_EQ(byName.GetNumberCols(),  byCoords.GetNumberCols());
	EXPECT_EQ(byName.GetCellCount(),   byCoords.GetCellCount());
	EXPECT_EQ(byName.GetMaxRowBrake(), byCoords.GetMaxRowBrake());
	EXPECT_EQ(byName.GetMaxColBrake(), byCoords.GetMaxColBrake());
	EXPECT_EQ(byName.GetCellValue(0, 1), byCoords.GetCellValue(0, 1));

	EXPECT_TRUE(byName == byCoords);
}

// ---------------------------------------------------------------------------
//  Print breaks — Add and Set are DIFFERENT VERBS
//
//  🛑 THESE TESTS STATE WHAT THE CODE DOES, not what the names suggest. A change
//  made on a reading of the name alone nearly shipped on 2026-08-31, so the
//  behaviour is written down plainly: SetRowBrake is an EXTENT MARKER — it
//  OVERWRITES the list's maximum element (or appends when the list is empty),
//  the value it writes is derived from the list's LAST element clamped to the
//  sheet's last line, and the argument only RAISES that value. It is what
//  GetArea / GetAreaByName call to stamp a fragment's own edge; it is not a way
//  to place a page break at a given line — AddRowBrake is.
// ---------------------------------------------------------------------------

TEST(SpreadsheetDescription, SetRowBrake_OnAnEmptyList_AppendsTheGivenRow)
{
	auto doc = MakeSheetOfRows(10);
	ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();

	desc.SetRowBrake(4);

	EXPECT_EQ(1, desc.GetBrakeNumberRows());
	EXPECT_TRUE(desc.IsRowBrake(4));
	EXPECT_EQ(4, desc.GetMaxRowBrake());
}

TEST(SpreadsheetDescription, SetRowBrake_OnANonEmptyList_OverwritesTheMaximumInsteadOfAppending)
{
	auto doc = MakeSheetOfRows(10);
	ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();

	desc.SetRowBrake(2);
	desc.SetRowBrake(6);

	EXPECT_EQ(1, desc.GetBrakeNumberRows());   // still ONE entry, not two
	EXPECT_FALSE(desc.IsRowBrake(2));          // the previous one is gone
	EXPECT_TRUE(desc.IsRowBrake(6));
}

TEST(SpreadsheetDescription, AddRowBrake_AndSetRowBrake_AreDifferentVerbs)
{
	auto doc = MakeSheetOfRows(10);
	ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();

	desc.AddRowBrake(2);
	desc.AddRowBrake(6);
	EXPECT_EQ(2, desc.GetBrakeNumberRows());   // Add PLACES a break

	desc.SetRowBrake(8);
	EXPECT_EQ(2, desc.GetBrakeNumberRows());   // Set never places one

	EXPECT_TRUE(desc.IsRowBrake(2));           // the lower break is untouched…
	EXPECT_FALSE(desc.IsRowBrake(6));          // …the highest one was rewritten
	EXPECT_TRUE(desc.IsRowBrake(8));
}

// ⚠ IT CAN DESTROY A BREAK A PERSON PLACED, and it does not necessarily write the
// line it was handed: the value comes from the list's LAST entry, the argument
// only raises it, and it lands on the MAXIMUM entry wherever that sits.
TEST(SpreadsheetDescription, SetRowBrake_WhenTheMaximumIsNotTheLastEntry_DropsThatBreakAndIgnoresItsArgument)
{
	auto doc = MakeSheetOfRows(10);
	ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();

	desc.AddRowBrake(8);
	desc.AddRowBrake(3);

	desc.SetRowBrake(1);

	EXPECT_EQ(2, desc.GetBrakeNumberRows());
	EXPECT_FALSE(desc.IsRowBrake(8));   // the page break a person put at row 8 — gone
	EXPECT_FALSE(desc.IsRowBrake(1));   // …and row 1, the argument, was never written
	EXPECT_TRUE(desc.IsRowBrake(3));
	EXPECT_EQ(3, desc.GetMaxRowBrake());
}

// …and it is an EXTENT marker: a break beyond the last written line is pulled
// back to it. That is the verb's actual job.
TEST(SpreadsheetDescription, SetRowBrake_WithABreakPastTheContent_ClampsItToTheLastWrittenRow)
{
	auto doc = MakeSheetOfRows(5);
	ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();
	ASSERT_EQ(5, desc.GetNumberRows());

	desc.AddRowBrake(9);      // beyond anything written
	desc.SetRowBrake(0);

	EXPECT_EQ(1, desc.GetBrakeNumberRows());
	EXPECT_FALSE(desc.IsRowBrake(9));
	EXPECT_TRUE(desc.IsRowBrake(4));   // the last written row
	EXPECT_EQ(4, desc.GetMaxRowBrake());
}

// The column twin, same shape and the same two facts in one go.
TEST(SpreadsheetDescription, SetColBrake_OverwritesTheMaximumAndClampsToTheContent)
{
	auto doc = MakeDocument();
	for (int col = 0; col < 5; col++)
		doc->SetCellValue(0, col, CellText(0, col));

	ibSpreadsheetDescription& desc = doc->GetSpreadsheetDesc();
	ASSERT_EQ(5, desc.GetNumberCols());

	desc.SetColBrake(2);
	EXPECT_EQ(1, desc.GetBrakeNumberCols());
	EXPECT_TRUE(desc.IsColBrake(2));

	desc.AddColBrake(9);
	desc.SetColBrake(0);

	EXPECT_EQ(2, desc.GetBrakeNumberCols());
	EXPECT_FALSE(desc.IsColBrake(9));   // clamped back to the last written column
	EXPECT_TRUE(desc.IsColBrake(2));    // the other entry is untouched
	EXPECT_EQ(4, desc.GetMaxColBrake());
}
