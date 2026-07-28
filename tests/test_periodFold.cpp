// =============================================================================
// OES Enterprise — period truncation + the balance roll-forward
//
// Two pure functions that every totals reading rests on, and both fail
// quietly when they are wrong:
//
//   ibTruncateToPeriod  — the RAM twin of the dialect's SQL truncation. If the
//                         two disagree, the same query returns different rows
//                         depending on whether the read pushed down or folded
//                         in memory. That looks like a rounding bug and is not
//                         one, so the calendar cases are pinned here.
//   FoldBalancesForward — turns per-period turnovers into per-period balances.
//                         Get it wrong and the report still prints: correct
//                         turnovers, plausible-looking balances, wrong numbers.
//
// The fold is shared rather than owned by the accumulation register: an
// accounting register signs its movements by debit / credit side instead of by
// record type, but by the time rows reach the fold both arrive as a receipt /
// expense pair per period. These tests exercise it through that neutral shape.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/query/queryRamTable.h"
#include "backend/databaseLayer/databaseLayer.h"

namespace {

wxDateTime Day(int y, int m, int d, int hh = 0, int mm = 0, int ss = 0)
{
    return wxDateTime(d, static_cast<wxDateTime::Month>(m - 1), y, hh, mm, ss);
}

wxString Ymd(const wxDateTime& d) { return d.Format(wxT("%Y-%m-%d")); }

} // namespace

// ---------------------------------------------------------------------------
// Period truncation
// ---------------------------------------------------------------------------

TEST(PeriodTruncate, MonthAndYearLandOnTheFirst) {
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 28), ibTotalsPeriod::Month)), wxT("2026-07-01"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 28), ibTotalsPeriod::Year)),  wxT("2026-01-01"));
}

TEST(PeriodTruncate, QuarterAndHalfYearLandOnTheirOpeningMonth) {
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 2, 14), ibTotalsPeriod::Quarter)),  wxT("2026-01-01"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 8, 14), ibTotalsPeriod::Quarter)),  wxT("2026-07-01"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 6, 30), ibTotalsPeriod::HalfYear)), wxT("2026-01-01"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7,  1), ibTotalsPeriod::HalfYear)), wxT("2026-07-01"));
}

// The week must start MONDAY on every engine and in RAM alike. Sunday is the
// case that catches an off-by-one: it belongs to the week that began six days
// earlier, not to the one starting the next day.
TEST(PeriodTruncate, WeekStartsMondayIncludingSunday) {
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 27), ibTotalsPeriod::Week)), wxT("2026-07-27"));  // Monday itself
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 29), ibTotalsPeriod::Week)), wxT("2026-07-27"));  // Wednesday
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 8,  2), ibTotalsPeriod::Week)), wxT("2026-07-27"));  // Sunday
}

// Ten-day periods start on the 1st / 11th / 21st. Day 31 is THE case: an
// uncapped offset floors it to a fourth bucket of its own, silently adding a
// one-day period nobody expects. The last ten-day period running 8-11 days is
// the definition, not a defect.
TEST(PeriodTruncate, TenDaysCapsTheLastPeriod) {
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7,  1), ibTotalsPeriod::TenDays)), wxT("2026-07-01"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 10), ibTotalsPeriod::TenDays)), wxT("2026-07-01"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 11), ibTotalsPeriod::TenDays)), wxT("2026-07-11"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 21), ibTotalsPeriod::TenDays)), wxT("2026-07-21"));
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 7, 31), ibTotalsPeriod::TenDays)), wxT("2026-07-21"));  // capped
    EXPECT_EQ(Ymd(ibTruncateToPeriod(Day(2026, 2, 28), ibTotalsPeriod::TenDays)), wxT("2026-02-21"));  // short month
}

TEST(PeriodTruncate, SubDayUnitsShearTheirTail) {
    const wxDateTime t = Day(2026, 7, 28, 14, 37, 52);
    EXPECT_EQ(ibTruncateToPeriod(t, ibTotalsPeriod::Day).Format(wxT("%H:%M:%S")),    wxT("00:00:00"));
    EXPECT_EQ(ibTruncateToPeriod(t, ibTotalsPeriod::Hour).Format(wxT("%H:%M:%S")),   wxT("14:00:00"));
    EXPECT_EQ(ibTruncateToPeriod(t, ibTotalsPeriod::Minute).Format(wxT("%H:%M:%S")), wxT("14:37:00"));
    EXPECT_EQ(ibTruncateToPeriod(t, ibTotalsPeriod::Second).Format(wxT("%H:%M:%S")), wxT("14:37:52"));
}

// Truncation must be IDEMPOTENT — re-truncating an already-truncated value to
// the same unit changes nothing. The view projects coarser periods off the
// stored one, so this is applied twice in the real path.
TEST(PeriodTruncate, IsIdempotent) {
    for (const ibTotalsPeriod u : { ibTotalsPeriod::Day, ibTotalsPeriod::Week, ibTotalsPeriod::TenDays,
                                    ibTotalsPeriod::Month, ibTotalsPeriod::Quarter, ibTotalsPeriod::Year }) {
        const wxDateTime once  = ibTruncateToPeriod(Day(2026, 7, 28, 9, 15, 1), u);
        const wxDateTime twice = ibTruncateToPeriod(once, u);
        EXPECT_EQ(Ymd(once), Ymd(twice));
    }
}

// ---------------------------------------------------------------------------
// The balance roll-forward
// ---------------------------------------------------------------------------

namespace {

// Column ids for a one-resource, one-dimension fixture.
enum : ibMetaID { kWarehouse = 1, kPeriod = 2, kIn = 10, kOut = 11, kTurn = 12, kOpen = 13, kClose = 14 };

ibBalanceFoldSlot OneSlot()
{
    ibBalanceFoldSlot s;
    s.m_receipt = kIn; s.m_expense = kOut; s.m_turnover = kTurn; s.m_opening = kOpen; s.m_closing = kClose;
    return s;
}

ibQueryRamTable MakeTable()
{
    ibQueryRamTable t;
    t.AddColumn(kWarehouse, wxT("Warehouse"), ibTypeDescription());
    t.AddColumn(kPeriod,    wxT("Period"),    ibTypeDescription());
    t.AddColumn(kIn,        wxT("Receipt"),   ibTypeDescription());
    t.AddColumn(kOut,       wxT("Expense"),   ibTypeDescription());
    t.AddColumn(kTurn,      wxT("Turnover"),  ibTypeDescription());
    t.AddColumn(kOpen,      wxT("Opening"),   ibTypeDescription());
    t.AddColumn(kClose,     wxT("Closing"),   ibTypeDescription());
    return t;
}

void AddRow(ibQueryRamTable& t, const wxString& wh, int month, double in, double out)
{
    const long r = t.AppendRow();
    t.SetCell(r, kWarehouse, ibValue(wh));
    t.SetCell(r, kPeriod,    ibValue(Day(2026, month, 1)));
    t.SetCell(r, kIn,        ibValue(in));
    t.SetCell(r, kOut,       ibValue(out));
}

double Cell(const ibQueryRamTable& t, long row, ibMetaID id) { return t.GetCell(row, id).GetNumber().ToDouble(); }

} // namespace

// Each period opens where the previous closed. This is the whole contract.
TEST(BalanceFold, CarriesClosingIntoTheNextOpening) {
    ibQueryRamTable t = MakeTable();
    AddRow(t, wxT("A"), 1, 100, 0);
    AddRow(t, wxT("A"), 2,  50, 30);
    AddRow(t, wxT("A"), 3,   0, 20);

    FoldBalancesForward(t, { kWarehouse }, kPeriod, { OneSlot() }, {});

    EXPECT_DOUBLE_EQ(Cell(t, 0, kOpen),  0);    EXPECT_DOUBLE_EQ(Cell(t, 0, kClose), 100);
    EXPECT_DOUBLE_EQ(Cell(t, 1, kOpen),  100);  EXPECT_DOUBLE_EQ(Cell(t, 1, kClose), 120);
    EXPECT_DOUBLE_EQ(Cell(t, 2, kOpen),  120);  EXPECT_DOUBLE_EQ(Cell(t, 2, kClose), 100);
}

TEST(BalanceFold, TurnoverIsReceiptMinusExpense) {
    ibQueryRamTable t = MakeTable();
    AddRow(t, wxT("A"), 1, 70, 25);
    FoldBalancesForward(t, { kWarehouse }, kPeriod, { OneSlot() }, {});
    EXPECT_DOUBLE_EQ(Cell(t, 0, kTurn), 45);
}

// The seed is what a report is usually FOR: the stock that existed before the
// interval began. Without it every key would appear to start empty.
TEST(BalanceFold, SeedsFromTheOpeningBalance) {
    ibQueryRamTable t = MakeTable();
    AddRow(t, wxT("A"), 5, 10, 0);

    std::map<wxString, std::map<ibMetaID, ibNumber>> opening;
    opening[wxT("A") + wxString(wxT("\x1f"))][kTurn] = ibNumber(500.0);

    FoldBalancesForward(t, { kWarehouse }, kPeriod, { OneSlot() }, opening);

    EXPECT_DOUBLE_EQ(Cell(t, 0, kOpen),  500);
    EXPECT_DOUBLE_EQ(Cell(t, 0, kClose), 510);
}

// Keys are independent: one warehouse's stock must never leak into another's
// running balance. Rows of the two keys are interleaved here on purpose.
TEST(BalanceFold, KeysDoNotBleedIntoEachOther) {
    ibQueryRamTable t = MakeTable();
    AddRow(t, wxT("A"), 1, 100, 0);
    AddRow(t, wxT("B"), 1,   7, 0);
    AddRow(t, wxT("A"), 2,   0, 40);
    AddRow(t, wxT("B"), 2,   3, 0);

    FoldBalancesForward(t, { kWarehouse }, kPeriod, { OneSlot() }, {});

    EXPECT_DOUBLE_EQ(Cell(t, 2, kOpen),  100);  // A carries A's 100
    EXPECT_DOUBLE_EQ(Cell(t, 2, kClose), 60);
    EXPECT_DOUBLE_EQ(Cell(t, 3, kOpen),  7);    // B carries B's 7
    EXPECT_DOUBLE_EQ(Cell(t, 3, kClose), 10);
}

// Several resources fold independently — quantity and amount each carry their
// own running balance, keyed by their own slot.
TEST(BalanceFold, ResourcesFoldIndependently) {
    ibQueryRamTable t = MakeTable();
    t.AddColumn(20, wxT("AmountIn"),   ibTypeDescription());
    t.AddColumn(21, wxT("AmountOut"),  ibTypeDescription());
    t.AddColumn(22, wxT("AmountTurn"), ibTypeDescription());
    t.AddColumn(23, wxT("AmountOpen"), ibTypeDescription());
    t.AddColumn(24, wxT("AmountClose"),ibTypeDescription());

    AddRow(t, wxT("A"), 1, 10, 0);
    t.SetCell(0, 20, ibValue(1000.0)); t.SetCell(0, 21, ibValue(0.0));
    AddRow(t, wxT("A"), 2, 0, 4);
    t.SetCell(1, 20, ibValue(0.0));    t.SetCell(1, 21, ibValue(250.0));

    ibBalanceFoldSlot amount;
    amount.m_receipt = 20; amount.m_expense = 21; amount.m_turnover = 22; amount.m_opening = 23; amount.m_closing = 24;

    FoldBalancesForward(t, { kWarehouse }, kPeriod, { OneSlot(), amount }, {});

    EXPECT_DOUBLE_EQ(Cell(t, 1, kOpen), 10);    EXPECT_DOUBLE_EQ(Cell(t, 1, kClose), 6);
    EXPECT_DOUBLE_EQ(Cell(t, 1, 23),    1000);  EXPECT_DOUBLE_EQ(Cell(t, 1, 24),     750);
}

// A period whose movements cancel out still reports: zero turnover, and a
// balance that carries through unchanged. This is NOT the same as the row being
// absent — absence is decided later, by the read filter, over the folded value.
TEST(BalanceFold, CancellingPeriodKeepsTheBalance) {
    ibQueryRamTable t = MakeTable();
    AddRow(t, wxT("A"), 1, 100, 0);
    AddRow(t, wxT("A"), 2, 100, 100);

    FoldBalancesForward(t, { kWarehouse }, kPeriod, { OneSlot() }, {});

    EXPECT_DOUBLE_EQ(Cell(t, 1, kTurn),  0);
    EXPECT_DOUBLE_EQ(Cell(t, 1, kOpen),  100);
    EXPECT_DOUBLE_EQ(Cell(t, 1, kClose), 100);
}
