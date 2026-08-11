// =============================================================================
// OES Enterprise — the rules a register READING is decided by
//
// Three pure questions stand between a written query and the rows it gets back:
//
//   * what granularity was asked for      (ibReadRegisterFold — a WORD, and its meaning)
//   * which columns that granularity has  (ibRegisterViewColumnFits)
//   * where the interval actually stops   (ibReadRegisterBound — a date, a moment, a boundary)
//
// All three are decided WITHOUT a database, which is why they belong here rather than in a parity
// run. And all three had shipped wrong, in the same shape: the window answered them one way and the
// engine another, so a query could name a column its own source does not produce and get neither
// the field nor a complaint — the worst of the three possible answers a reader can give.
//
// These tests exist so the two sides cannot drift again. Each one asks the rule the way BOTH sides
// ask it, because there is only one function under them now: if a test here goes red, the field
// tree and the resolver go wrong together, which is at least honest.
//
// (docs/register-totals-strategy.md, docs/query-constructor.md §5g)
// =============================================================================

#include <gtest/gtest.h>

#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/system/value/valuePointInTime.h"
#include "backend/system/value/valueBoundary.h"

namespace {

// The three names a register's own scaffold columns carry in these tests. Real registers read them
// off their attributes; the RULE takes them as arguments precisely so it never has to know.
const wxString kPeriod   = wxT("Period");
const wxString kRecorder = wxT("Recorder");
const wxString kLine     = wxT("LineNumber");

ibRegFold Fold(ibRegGranularity kind, ibTotalsPeriod unit = ibTotalsPeriod::Month)
{
	ibRegFold fold;
	fold.m_kind = kind;
	fold.m_unit = unit;
	return fold;
}

// The question as both sides ask it — with the register's scaffold names supplied.
bool Fits(const wxString& column, const ibRegFold& fold)
{
	return ibRegisterViewColumnFits(column, kPeriod, fold, kRecorder, kLine);
}

ibValue Word(const wxString& text)
{
	ibValue v;
	v.SetString(text);
	return v;
}

} // namespace

// =============================================================================
//  The word, and what it means
// =============================================================================

// NOTHING ASKED FOR IS NOT "UNDECIDED". They read the same (neither folds by a period) and they
// OFFER differently, and collapsing them is how a window promises a column the rows will not carry.
TEST(RegisterFold, AnAbsentPeriodicityIsTheWholeIntervalAndNotAuto)
{
    const ibRegFold absent = ibReadRegisterFold(ibValue());
    EXPECT_EQ(ibRegGranularity::Whole, absent.m_kind);
    EXPECT_TRUE (absent.IsWholeInterval());
    EXPECT_FALSE(absent.OffersEveryProjection());

    const ibRegFold undecided = ibReadRegisterFold(Word(wxT("Auto")));
    EXPECT_EQ(ibRegGranularity::Auto, undecided.m_kind);
    EXPECT_TRUE(undecided.IsWholeInterval());        // reads the same...
    EXPECT_TRUE(undecided.OffersEveryProjection());  // ...offers differently
}

TEST(RegisterFold, EachWordNamesItsOwnGranularity)
{
    EXPECT_EQ(ibRegGranularity::Period,   ibReadRegisterFold(Word(wxT("Period"))).m_kind);
    EXPECT_EQ(ibRegGranularity::Recorder, ibReadRegisterFold(Word(wxT("Recorder"))).m_kind);
    EXPECT_EQ(ibRegGranularity::Record,   ibReadRegisterFold(Word(wxT("Record"))).m_kind);
}

// CASE-INSENSITIVE THROUGH THE ONE COMPARER the engine already uses. A word typed in the constructor
// and the same word typed into the text must be the same word.
TEST(RegisterFold, TheWordIsReadWithoutRegardToCase)
{
    EXPECT_EQ(ibRegGranularity::Record, ibReadRegisterFold(Word(wxT("record"))).m_kind);
    EXPECT_EQ(ibRegGranularity::Record, ibReadRegisterFold(Word(wxT("RECORD"))).m_kind);

    const ibRegFold month = ibReadRegisterFold(Word(wxT("mOnTh")));
    EXPECT_EQ(ibRegGranularity::Calendar, month.m_kind);
    EXPECT_EQ(ibTotalsPeriod::Month, month.m_unit);
}

TEST(RegisterFold, ACalendarUnitCarriesTheUnitItself)
{
    const ibRegFold day = ibReadRegisterFold(Word(ibRegisterUnitWord(ibTotalsPeriod::Day)));
    EXPECT_EQ(ibRegGranularity::Calendar, day.m_kind);
    EXPECT_EQ(ibTotalsPeriod::Day, day.m_unit);
    EXPECT_TRUE (day.IsCalendar());
    EXPECT_TRUE (day.HasPeriod());
    EXPECT_FALSE(day.IsWholeInterval());
}

// The composer writes the unit as its ORDINAL. Same argument, same meaning — one reader.
TEST(RegisterFold, ANumberIsTheUnitsOrdinal)
{
    const ibValue ordinal(static_cast<double>(ibTotalsPeriod::Day));
    const ibRegFold fold = ibReadRegisterFold(ordinal);
    EXPECT_EQ(ibRegGranularity::Calendar, fold.m_kind);
    EXPECT_EQ(ibTotalsPeriod::Day, fold.m_unit);
}

// A WORD NOBODY DECLARED IS REFUSED, naming what is accepted. Silently folding it to "the whole
// interval" would answer a question nobody asked, and the author would never learn of the typo.
TEST(RegisterFold, AnUnknownWordIsRefused)
{
    EXPECT_THROW(ibReadRegisterFold(Word(wxT("Fortnight"))), ibBackendException);
}

// =============================================================================
//  The granularity decides which columns exist
//
//  THE DEFECT THIS PINS: a turnovers read whose periodicity had been removed still offered and
//  accepted `.Period`, and then produced no such column.
// =============================================================================

TEST(RegisterColumns, TheWholeIntervalHasNoPeriodColumnAtAll)
{
    const ibRegFold whole = Fold(ibRegGranularity::Whole);
    EXPECT_FALSE(Fits(kPeriod,   whole));
    EXPECT_FALSE(Fits(kRecorder, whole));
    EXPECT_FALSE(Fits(kLine,     whole));
}

// UNDECIDED OFFERS EVERYTHING, and that is not the same table as the one above. An argument written
// as a parameter has no value until the query runs, so the shape it is drawn against must be the
// widest it might turn out to have — never a guess at which.
TEST(RegisterColumns, UndecidedOffersEveryProjection)
{
    const ibRegFold undecided = Fold(ibRegGranularity::Auto);
    EXPECT_TRUE(Fits(kPeriod,   undecided));
    EXPECT_TRUE(Fits(kRecorder, undecided));
    EXPECT_TRUE(Fits(kLine,     undecided));
}

// THE RECORDER IS NOT A DIMENSION. It exists on a row only where a row IS a movement (or a
// document's worth of them): offering it beside a monthly turnover promises a document per row
// where the row is a month's worth of documents.
TEST(RegisterColumns, TheRecorderExistsOnlyWhereARowIsAMovement)
{
    EXPECT_TRUE (Fits(kRecorder, Fold(ibRegGranularity::Recorder)));
    EXPECT_TRUE (Fits(kRecorder, Fold(ibRegGranularity::Record)));
    EXPECT_FALSE(Fits(kRecorder, Fold(ibRegGranularity::Period)));
    EXPECT_FALSE(Fits(kRecorder, Fold(ibRegGranularity::Calendar, ibTotalsPeriod::Month)));
    EXPECT_FALSE(Fits(kRecorder, Fold(ibRegGranularity::Whole)));
}

// The line number goes one step further: a DOCUMENT's worth of movements has no single line.
TEST(RegisterColumns, TheLineNumberExistsOnlyAtTheMovementItself)
{
    EXPECT_TRUE (Fits(kLine, Fold(ibRegGranularity::Record)));
    EXPECT_FALSE(Fits(kLine, Fold(ibRegGranularity::Recorder)));
    EXPECT_FALSE(Fits(kLine, Fold(ibRegGranularity::Period)));
}

TEST(RegisterColumns, AGranularityThatKeepsThePeriodOffersIt)
{
    EXPECT_TRUE(Fits(kPeriod, Fold(ibRegGranularity::Period)));
    EXPECT_TRUE(Fits(kPeriod, Fold(ibRegGranularity::Calendar, ibTotalsPeriod::Month)));
    EXPECT_TRUE(Fits(kPeriod, Fold(ibRegGranularity::Recorder)));
    EXPECT_TRUE(Fits(kPeriod, Fold(ibRegGranularity::Record)));
}

// A DIMENSION OR A RESOURCE IS THERE WHATEVER THE GRANULARITY. The rule is about the period
// projections and the movement's identity; everything else is the register's own shape, and a fold
// has no opinion about it.
TEST(RegisterColumns, ADimensionIsThereOnEveryGranularity)
{
    for (const ibRegGranularity kind : { ibRegGranularity::Whole, ibRegGranularity::Auto,
                                         ibRegGranularity::Period, ibRegGranularity::Calendar,
                                         ibRegGranularity::Recorder, ibRegGranularity::Record }) {
        EXPECT_TRUE(Fits(wxT("Warehouse"),      Fold(kind))) << static_cast<int>(kind);
        EXPECT_TRUE(Fits(wxT("QtyTurnover"),    Fold(kind))) << static_cast<int>(kind);
    }
}

// ⚠ A REGISTER WITH NO RECORDER AT ALL supplies no name for one, and the rule must not then match
// the empty string against every column it is asked about.
TEST(RegisterColumns, ARegisterWithoutARecorderIsAskedWithoutOne)
{
    const ibRegFold record = Fold(ibRegGranularity::Record);
    EXPECT_TRUE(ibRegisterViewColumnFits(wxT("Warehouse"), kPeriod, record));
    EXPECT_TRUE(ibRegisterViewColumnFits(kPeriod,          kPeriod, record));
}

// =============================================================================
//  Where the interval stops
//
//  ⚠ These values are REFERENCE-COUNTED — built with `new`, held through ibValuePtr. Wrapping a
//  stack object in an ibValue hands its address to the refcount.
// =============================================================================

TEST(RegisterBound, ABareDateIsTheInstantAndNothingElse)
{
    const wxDateTime noon(15, wxDateTime::Mar, 2026, 12, 0, 0);
    const ibRegBound bound = ibReadRegisterBound(ibValue(noon));

    EXPECT_FALSE(bound.IsEmpty());
    EXPECT_FALSE(bound.HasRecorder());
    EXPECT_FALSE(bound.m_excluding);
    EXPECT_EQ(noon, bound.m_date.GetDateTime());
}

TEST(RegisterBound, AnEmptyArgumentIsNoBoundaryAtAll)
{
    EXPECT_TRUE(ibReadRegisterBound(ibValue()).IsEmpty());
}

// A MOMENT YIELDS BOTH HALVES — which is the whole reason it exists: three documents sharing a date
// cannot be told apart by the date.
TEST(RegisterBound, AMomentYieldsTheDateAndTheDocument)
{
    const wxDateTime when(15, wxDateTime::Mar, 2026, 12, 0, 0);
    ibValue recorder;
    recorder.SetString(wxT("a document"));   // any non-empty value: the bound only carries it

    const ibValuePtr<ibValuePointInTime> moment(new ibValuePointInTime(when, recorder));
    const ibRegBound bound = ibReadRegisterBound(*moment);

    EXPECT_EQ(when, bound.m_date.GetDateTime());
    EXPECT_TRUE (bound.HasRecorder());
    EXPECT_FALSE(bound.m_excluding);
}

// A BOUNDARY WRAPS A POSITION; it does not replace one. So a date, a moment and a boundary over
// either travel ONE road and differ only in which side of the position is meant.
TEST(RegisterBound, ABoundaryOnlyDecidesWhichSideOfThePositionIsMeant)
{
    const wxDateTime when(15, wxDateTime::Mar, 2026, 12, 0, 0);

    const ibValuePtr<ibValueBoundary> including(new ibValueBoundary(ibValue(when), ibBoundaryKind_Including));
    const ibRegBound in = ibReadRegisterBound(*including);
    EXPECT_EQ(when, in.m_date.GetDateTime());
    EXPECT_FALSE(in.m_excluding);

    const ibValuePtr<ibValueBoundary> excluding(new ibValueBoundary(ibValue(when), ibBoundaryKind_Excluding));
    const ibRegBound out = ibReadRegisterBound(*excluding);
    EXPECT_EQ(when, out.m_date.GetDateTime());
    EXPECT_TRUE(out.m_excluding);
}

// AND A BOUNDARY OVER A MOMENT KEEPS BOTH — the document survives the wrapping, or "everything up
// to and not including THIS document" would silently become "up to this date".
TEST(RegisterBound, ABoundaryOverAMomentKeepsTheDocument)
{
    const wxDateTime when(15, wxDateTime::Mar, 2026, 12, 0, 0);
    ibValue recorder;
    recorder.SetString(wxT("a document"));

    const ibValuePtr<ibValuePointInTime> moment(new ibValuePointInTime(when, recorder));
    const ibValuePtr<ibValueBoundary> excluding(new ibValueBoundary(*moment, ibBoundaryKind_Excluding));

    const ibRegBound bound = ibReadRegisterBound(*excluding);
    EXPECT_EQ(when, bound.m_date.GetDateTime());
    EXPECT_TRUE(bound.HasRecorder());
    EXPECT_TRUE(bound.m_excluding);
}
