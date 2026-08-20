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
