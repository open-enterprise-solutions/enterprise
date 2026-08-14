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
#include "backend/metaCollection/partial/accountingRegister.h"     // ibAcctArgs — the layout a call is read by
#include "backend/metaCollection/partial/chartOfAccountsEnum.h"   // ibAccountType — the fold the balance applies

#include <set>
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

// =============================================================================
// A PUBLISHED COLUMN OWES THREE NAMES — and the third one had no owner
// =============================================================================
//
// A derived surface names each column for a QUERY (`AmountBalanceDr`), for the STORAGE
// (`Amount_BalanceDr`), and for a PERSON. The third used to be built somewhere else entirely — in
// the manager, per figure, spelled by hand — so the same number arrived captioned through the script
// door and bare through the query door. These pin the pieces that made one answer out of the three.

TEST(RegisterSurface, ACaptionFallsBackToTheNameAndNeverToNothing) {
    ibTypeDescription type;
    const ibTempColumn bare(wxT("AmountBalanceDr"), wxT("Amount_BalanceDr"), type, 1);
    // No caption given: the base class's own answer, which is right for a temp table whose columns
    // are named by whoever made it. What must NOT happen is an empty heading.
    EXPECT_EQ(wxT("AmountBalanceDr"), bare.GetSynonym());

    const ibTempColumn dressed(wxT("AmountBalanceDr"), wxT("Amount_BalanceDr"), type, 1, wxT("Amount Balance Dr"));
    EXPECT_EQ(wxT("Amount Balance Dr"), dressed.GetSynonym());
    // …and the other two names are untouched by it. Three names, one column, no aliasing.
    EXPECT_EQ(wxT("AmountBalanceDr"),  dressed.GetName());
    EXPECT_EQ(wxT("Amount_BalanceDr"), dressed.GetPhysicalName());
}

TEST(RegisterSurface, EveryFigureWordHasACaption) {
    // The point is not WHICH words come back (they are translated), it is that every figure the
    // registers publish is IN the list. A figure that is not answers with its own name — legible,
    // and the signal that the list has fallen behind the vocabulary.
    const wxChar* const figures[] = {
        ibRegFigure::Turnover, ibRegFigure::Receipt, ibRegFigure::Expense,
        ibRegFigure::Balance,  ibRegFigure::OpeningBalance, ibRegFigure::ClosingBalance,
    };
    for (const wxChar* figure : figures)
        EXPECT_FALSE(ibRegFigureCaption(figure).IsEmpty()) << "no caption for " << wxString(figure).ToStdString();

    EXPECT_EQ(wxT("SomethingNobodyCaptioned"), ibRegFigureCaption(wxT("SomethingNobodyCaptioned")));
}

TEST(RegisterSurface, TheSideRidesTheCaptionTheSameWayItRidesTheName) {
    // ibRegSidedFigure and ibRegSidedCaption are one pairing said twice — a name and a caption built
    // from the SAME (figure, side). The builder takes that pair rather than the finished suffix,
    // precisely so nobody recovers the side by reading the last two letters of a string.
    EXPECT_EQ(wxT("BalanceDr"), ibRegSidedFigure(ibRegFigure::Balance, /*credit*/ false));
    EXPECT_EQ(wxT("BalanceCr"), ibRegSidedFigure(ibRegFigure::Balance, /*credit*/ true));

    const wxString dr = ibRegSidedCaption(ibRegFigure::Balance, /*credit*/ false);
    const wxString cr = ibRegSidedCaption(ibRegFigure::Balance, /*credit*/ true);
    EXPECT_NE(dr, cr) << "the two sides must not read alike";
    EXPECT_TRUE(dr.StartsWith(ibRegFigureCaption(ibRegFigure::Balance)));
    EXPECT_TRUE(cr.StartsWith(ibRegFigureCaption(ibRegFigure::Balance)));
}

TEST(RegisterSurface, AColumnCaptionSaysWhatItIsOfThenWhatItIs) {
    EXPECT_EQ(wxT("Amount Balance"), ibRegFigureColumnCaption(wxT("Amount"), wxT("Balance")));
    // A resource with no synonym of its own must not produce a leading space.
    EXPECT_EQ(wxT("Balance"), ibRegFigureColumnCaption(wxEmptyString, wxT("Balance")));
}

// =============================================================================
// The derived-surface cache — built once, rebuilt when the shape moves, never freed under a reader
// =============================================================================

TEST(RegisterSurface, TheSameQuestionGetsTheSameSurface) {
    ibRegSurfaceCache cache;
    ibTypeDescription type;
    const auto build = [&type](std::vector<ibTempColumn>& columns, ibMetaID& synthetic) {
        columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, synthetic++));
    };

    const ibBackendQueryable* first  = cache.Obtain(wxT("k"), wxT("sig-1"), wxT("T"), nullptr, build);
    const ibBackendQueryable* second = cache.Obtain(wxT("k"), wxT("sig-1"), wxT("T"), nullptr, build);
    ASSERT_NE(nullptr, first);
    EXPECT_EQ(first, second) << "same key, same signature — the surface is built once";
}

TEST(RegisterSurface, AChangedShapeRebuildsAndTheOldPointerStaysAlive) {
    ibRegSurfaceCache cache;
    ibTypeDescription type;
    const auto one = [&type](std::vector<ibTempColumn>& columns, ibMetaID& synthetic) {
        columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, synthetic++));
    };
    const auto two = [&type](std::vector<ibTempColumn>& columns, ibMetaID& synthetic) {
        columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, synthetic++));
        columns.push_back(ibTempColumn(wxT("B"), wxT("fldB"), type, synthetic++));
    };

    const ibBackendQueryable* before = cache.Obtain(wxT("k"), wxT("sig-1"), wxT("T"), nullptr, one);
    ASSERT_NE(nullptr, before);
    EXPECT_EQ(1u, before->GetColumns().size());

    // ⭐ THE SIGNATURE IS WHAT REBUILDS IT. Keyed by name alone, a surface asked for before the
    // register's attributes were read stays empty for the life of the session — that is the scar
    // this check exists for, and counting columns instead would miss a RE-TYPED one.
    const ibBackendQueryable* after = cache.Obtain(wxT("k"), wxT("sig-2"), wxT("T"), nullptr, two);
    ASSERT_NE(nullptr, after);
    EXPECT_NE(before, after);
    EXPECT_EQ(2u, after->GetColumns().size());

    // ⚠ AND THE OLD ONE IS RETIRED, NOT DESTROYED — a reader may still hold the pointer handed out
    // earlier. Reading through it after the rebuild is the whole reason the retired list exists; if
    // this ever becomes a use-after-free, it fails here rather than in somebody's report.
    EXPECT_EQ(1u, before->GetColumns().size());
}

TEST(RegisterSurface, TheIdBandIsOneConventionForEverySurface) {
    // Derived columns are numbered clear of every metaID. It was re-declared as a local constant at
    // each builder, which is a constant nobody owns.
    ibRegSurfaceCache cache;
    ibTypeDescription type;
    ibMetaID seen = 0;
    cache.Obtain(wxT("k"), wxT("sig"), wxT("T"), nullptr,
        [&](std::vector<ibTempColumn>& columns, ibMetaID& synthetic) {
            seen = synthetic;
            columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, synthetic++));
        });
    EXPECT_EQ(ibRegDerivedColumnBand, seen);
}

// =============================================================================
// The balance's server road — the shape of what it hands the door
// =============================================================================
//
// The reading itself needs a register, a chart and a database, so what is pinned here is the piece
// that decides the NUMBERS and can be checked without any of them: the fold by the account's own
// type, said as the CASE the relation carries.
//
//   active         a credit entry REDUCED the debit balance   ->  Dr - Cr , 0
//   passive        the mirror                                 ->  0 , Cr - Dr
//   active-passive both sides stand — folding them is a LOSS
//
// The RAM oracle is FoldSideByAccountType; if these two ever disagree, one road reports a different
// balance than the other for the same data, and nothing about either answer looks wrong.

namespace {

// The RAM rule, restated here as the oracle — deliberately by hand, so a change to the engine's copy
// does not silently change what this test holds to be correct.
void FoldOracle(int accountType, double& debit, double& credit)
{
    if (accountType == ibAccountType::eActivePassive)
        return;
    const double net = debit - credit;
    if (accountType == ibAccountType::eActive) { debit = net;  credit = 0.0; }
    else                                       { debit = 0.0; credit = -net; }
}

// What the projected CASE computes, in the same order of tests the relation declares.
void FoldAsProjected(int accountType, double& debit, double& credit)
{
    const double dr = debit, cr = credit;
    debit  = (accountType == ibAccountType::eActive)  ? dr - cr : (accountType == ibAccountType::ePassive ? 0.0 : dr);
    credit = (accountType == ibAccountType::ePassive) ? cr - dr : (accountType == ibAccountType::eActive  ? 0.0 : cr);
}

} // namespace

TEST(AcctBalanceServerRoad, TheCaseFoldsExactlyAsTheRamReadingDoes) {
    const double figures[][2] = { { 100.0, 0.0 }, { 0.0, 100.0 }, { 100.0, 100.0 }, { 150.0, 40.0 }, { 0.0, 0.0 } };
    const int types[] = { ibAccountType::eActive, ibAccountType::ePassive, ibAccountType::eActivePassive };

    for (const int type : types)
        for (const auto& pair : figures) {
            double oracleDr = pair[0], oracleCr = pair[1];
            double caseDr   = pair[0], caseCr   = pair[1];
            FoldOracle(type, oracleDr, oracleCr);
            FoldAsProjected(type, caseDr, caseCr);
            EXPECT_DOUBLE_EQ(oracleDr, caseDr) << "type " << type << " debit " << pair[0] << "/" << pair[1];
            EXPECT_DOUBLE_EQ(oracleCr, caseCr) << "type " << type << " credit " << pair[0] << "/" << pair[1];
        }
}

TEST(AcctBalanceServerRoad, ActivePassiveIsNeverFolded) {
    // The one case that is a LOSS rather than a simplification: a receivable of 100 against a payable
    // of 100 is not zero, and "zero" is wrong in a way no formatting undoes.
    double dr = 100.0, cr = 100.0;
    FoldAsProjected(ibAccountType::eActivePassive, dr, cr);
    EXPECT_DOUBLE_EQ(100.0, dr);
    EXPECT_DOUBLE_EQ(100.0, cr);
}

TEST(AcctBalanceServerRoad, TheOppositeSideReducesRatherThanAccumulates) {
    // An active account credited by 40 against 150 debited holds 110 — on the DEBIT side, with
    // nothing on the credit one. Getting this backwards produces two plausible figures instead of one.
    double dr = 150.0, cr = 40.0;
    FoldAsProjected(ibAccountType::eActive, dr, cr);
    EXPECT_DOUBLE_EQ(110.0, dr);
    EXPECT_DOUBLE_EQ(0.0,   cr);

    dr = 40.0; cr = 150.0;
    FoldAsProjected(ibAccountType::ePassive, dr, cr);
    EXPECT_DOUBLE_EQ(0.0,   dr);
    EXPECT_DOUBLE_EQ(110.0, cr);
}

// =============================================================================
// The ARGUMENT LAYOUT — positions, and why they are worth pinning at all
// =============================================================================
//
// A virtual table's arguments are read BY POSITION. Two lists have to agree — the one the descriptor
// declares and the one the call is read by — and when they disagree nothing fails: the condition is
// read as a breakdown list, the breakdown as a periodicity, and the answer comes back looking like an
// answer. The neighbouring register shipped exactly that once, when a declared periodicity pushed
// every call's filter into the next slot.
//
// So the layout is a pure function and these are its golden rows. The reference order (the shape the
// reference implementation's dialogs show) is: the interval, HOW IT IS CUT, then the sides — each
// side being its account condition immediately followed by its breakdown — with the general
// condition between the two sides for a turnover and after both for the matrix.

TEST(AcctArgs, BalanceIsAMomentThenOneSide) {
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::Balance, /*correspondence*/ false);
    EXPECT_EQ(0, a.m_begin);
    EXPECT_EQ(-1, a.m_end)          << "a balance stands at ONE moment, not over an interval";
    EXPECT_EQ(-1, a.m_periodicity)  << "there is no interval to cut";
    EXPECT_EQ(1, a.m_accountDr);
    EXPECT_EQ(2, a.m_kindsDr)       << "the breakdown follows ITS account, not the other account";
    EXPECT_EQ(3, a.m_condition);
    EXPECT_EQ(4, a.m_count);
}

TEST(AcctArgs, ABalanceHasNoOppositeSideEvenInCorrespondence) {
    // "The balance of 51 against 62" is not a question: a balance is not a movement and has no other
    // end. The credit account is a filter for a TURNOVER, and a filter is not a column.
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::Balance, /*correspondence*/ true);
    EXPECT_EQ(-1, a.m_accountCr);
    EXPECT_EQ(-1, a.m_kindsCr);
    EXPECT_EQ(4, a.m_count);
}

TEST(AcctArgs, TurnoversCutTheIntervalBeforeAnythingElse) {
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::Turnovers, /*correspondence*/ false);
    EXPECT_EQ(0, a.m_begin);
    EXPECT_EQ(1, a.m_end);
    // ⭐ THIRD, not last. It is about the interval, and an author writes the interval and immediately
    // says how to cut it.
    EXPECT_EQ(2, a.m_periodicity);
    EXPECT_EQ(3, a.m_accountDr);
    EXPECT_EQ(4, a.m_kindsDr);
    EXPECT_EQ(5, a.m_condition);
    EXPECT_EQ(6, a.m_count);
}

TEST(AcctArgs, ACorrespondenceTurnoverPutsTheConditionBetweenTheSides) {
    // The reading asks "this debit side, so filtered, against THAT credit side" — the general
    // condition belongs to the reading rather than to either side, and it is written where it acts.
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::Turnovers, /*correspondence*/ true);
    EXPECT_EQ(2, a.m_periodicity);
    EXPECT_EQ(3, a.m_accountDr);
    EXPECT_EQ(4, a.m_kindsDr);
    EXPECT_EQ(5, a.m_condition);
    EXPECT_EQ(6, a.m_accountCr);
    EXPECT_EQ(7, a.m_kindsCr);
    EXPECT_EQ(8, a.m_count);
}

TEST(AcctArgs, TheMatrixNamesBothSidesFirstAndFiltersThePairAfterwards) {
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::DrCrTurnovers, /*correspondence*/ true);
    EXPECT_EQ(2, a.m_periodicity);
    EXPECT_EQ(3, a.m_accountDr);
    EXPECT_EQ(4, a.m_kindsDr);
    EXPECT_EQ(5, a.m_accountCr);
    EXPECT_EQ(6, a.m_kindsCr);
    EXPECT_EQ(7, a.m_condition) << "the pair is named symmetrically, THEN filtered";
    EXPECT_EQ(8, a.m_count);
}

TEST(AcctArgs, TheSymbiosisAsksWhatToDoWithAnEmptyPeriod) {
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::BalanceAndTurnovers, /*correspondence*/ false);
    EXPECT_EQ(2, a.m_periodicity);
    // Right after the periodicity, because it is about the periods too — and only this reading can be
    // asked it, being the only one that reports a balance for a period nothing touched.
    EXPECT_EQ(3, a.m_fillMethod);
    EXPECT_EQ(4, a.m_accountDr);
    EXPECT_EQ(5, a.m_kindsDr);
    EXPECT_EQ(6, a.m_condition);
    EXPECT_EQ(7, a.m_count);
    EXPECT_EQ(-1, ibAcctArgs::For(ibAcctShape::Turnovers, false).m_fillMethod)
        << "a turnover has no balance to carry across an empty period";
}

TEST(AcctArgs, AListingTakesNeitherAccountNorBreakdownAndCanBeOrdered) {
    const ibAcctArgs a = ibAcctArgs::For(ibAcctShape::Records, /*correspondence*/ true);
    EXPECT_EQ(0, a.m_begin);
    EXPECT_EQ(1, a.m_end);
    EXPECT_EQ(-1, a.m_periodicity) << "a movement line is already as fine as this data gets";
    EXPECT_EQ(-1, a.m_accountDr)   << "a listing reports the lines as written";
    EXPECT_EQ(-1, a.m_kindsDr);
    EXPECT_EQ(2, a.m_condition);
    // Only a listing answers with LINES, so only a listing can be ordered and capped: a fold has no
    // line to put before another.
    EXPECT_EQ(3, a.m_order);
    EXPECT_EQ(4, a.m_top);
    EXPECT_EQ(5, a.m_count);
}

TEST(AcctArgs, EverySlotIsDistinctAndInsideTheCount) {
    // The property under all the rows above: a layout that hands two arguments the same position, or
    // a position past the end, is read as something else entirely.
    const ibAcctShape shapes[] = { ibAcctShape::Balance, ibAcctShape::Turnovers,
                                   ibAcctShape::DrCrTurnovers, ibAcctShape::BalanceAndTurnovers,
                                   ibAcctShape::Records };
    for (const ibAcctShape shape : shapes)
        for (const bool correspondence : { false, true }) {
            const ibAcctArgs a = ibAcctArgs::For(shape, correspondence);
            const int slots[] = { a.m_begin, a.m_end, a.m_periodicity, a.m_fillMethod,
                                  a.m_accountDr, a.m_kindsDr, a.m_accountCr, a.m_kindsCr,
                                  a.m_condition, a.m_order, a.m_top };
            std::set<int> seen;
            for (const int slot : slots) {
                if (slot < 0)
                    continue;
                EXPECT_LT(slot, a.m_count) << "slot outside the declared count";
                EXPECT_TRUE(seen.insert(slot).second) << "two arguments share position " << slot;
            }
        }
}

// =============================================================================
// The neighbour's order — the same rule, stated to the compiler there
// =============================================================================

TEST(AccumArgs, ThePeriodicityFollowsTheIntervalHereToo) {
    // Corrected 2026-08-13: it used to sit LAST, on the argument that an option nobody states belongs
    // where leaving it out is free. True of a DEFAULT, false of an ORDER — and two registers whose
    // arguments run in different orders are two things to remember instead of one.
    EXPECT_EQ(2, static_cast<int>(ibRegTurnoverArg::Periodicity));
    EXPECT_EQ(3, static_cast<int>(ibRegTurnoverArg::Filter));
    EXPECT_EQ(4, static_cast<int>(ibRegTurnoverArg::Count));

    EXPECT_EQ(2, static_cast<int>(ibRegBalTurnArg::Periodicity));
    EXPECT_EQ(3, static_cast<int>(ibRegBalTurnArg::FillMethod));
    EXPECT_EQ(4, static_cast<int>(ibRegBalTurnArg::Filter));
    EXPECT_EQ(5, static_cast<int>(ibRegBalTurnArg::Count));

    // A balance is a moment and a condition, and nothing has moved there.
    EXPECT_EQ(0, static_cast<int>(ibRegBalanceArg::Period));
    EXPECT_EQ(1, static_cast<int>(ibRegBalanceArg::Filter));
}
