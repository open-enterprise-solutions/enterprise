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
// (docs/private/register-totals-strategy.md, docs/private/query-constructor.md §5g)
// =============================================================================

#include <gtest/gtest.h>

#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/accountingRegister.h"     // ibAcctArgs — the layout a call is read by
#include "backend/metaCollection/partial/chartOfAccountsEnum.h"   // ibAccountType — the fold the balance applies

#include <algorithm>   // std::find — the condition surface's own columns
#include <set>
#include "backend/system/value/valuePointInTime.h"
#include "backend/system/value/valueBoundary.h"

#include <wx/init.h>                                           // wxInitializer - the live reading below
#include "backend/appData.h"
#include "backend/clsid.h"                                     // reference_to_clsid - the recorder's type
#include "backend/metadataConfiguration.h"                     // ibMetaDataConfigurationFile - a register in memory
#include "backend/metaCollection/metaObject.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"  // ibMaterializeReadSpec / RenderMaterializedRead
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

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
//  Where the two arms are cut - and which side of its edge the boundary stands on
//
//  🛑 "THE BALANCE BEFORE 10.01, EXCLUDING" INCLUDED THE WHOLE OF 10.01. A date exactly on a grain edge
//  needs no cut, so ibRegFillArmCut returned early - BEFORE it had said which side of the edge the
//  boundary stands on. The reader took it as included: `period <= midnight` admits the stored row keyed
//  by that midnight, and a stored row is the whole day that starts there. Measured on a file base
//  (2026-09-20): 164 / 26 291.37 where the movements fold to 116 / 6 612 - the difference being every
//  movement of that day, to the cent. Silent, and on the commonest question an accounting system is
//  asked: what was there at the start of the day.
// =============================================================================

namespace {

// A register in a configuration in memory, recorded by a document - what the cut is asked of.
struct ArmCutFix {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObjectAccumulationRegister* reg = nullptr;

    ArmCutFix() {
        ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
        if (root == nullptr) return;
        ibValueMetaObject* document = cfg.CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false);
        reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
            cfg.CreateMetaObject(g_metaAccumulationRegisterCLSID, root, /*runObject*/ false));
        if (document == nullptr || reg == nullptr || reg->GetRegisterRecorder() == nullptr) { reg = nullptr; return; }
        reg->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(reference_to_clsid(document->GetMetaID()));
    }

    ibMaterializeReadSpec CutAt(const wxDateTime& upTo, bool excluding) const {
        ibRegBound upper;
        upper.m_date = ibValue(upTo);
        upper.m_excluding = excluding;
        ibMaterializeReadSpec read;
        ibRegFillArmCut(read, reg, upper);
        return read;
    }
};

const wxDateTime kMidnight(5, wxDateTime::Mar, 2026);
const wxDateTime kAfternoon(5, wxDateTime::Mar, 2026, 14, 0, 0);

} // namespace

// The defect itself: no cut is needed, and the side is still said.
TEST(RegisterArmCut, AnExcludedDateOnAGrainEdgeNeedsNoCutAndStillSaysItIsExcluded)
{
    ArmCutFix f;
    ASSERT_NE(f.reg, nullptr);
    const ibMaterializeReadSpec read = f.CutAt(kMidnight, /*excluding*/ true);

    EXPECT_FALSE(read.m_markColumn.IsEmpty());
    EXPECT_NE(read.m_floor.GetType(), TYPE_DATE) << "the grain that starts at the edge is wholly out - nothing to cut";
    EXPECT_TRUE(read.m_toExcluding) << "said before the early return, or the reader reads `<=` and takes the day";
}

// ...on EVERY way out of the function, the one for a view with a single arm included. No register takes
// that way today (each has a recorder); the day one does, the defect must not come back with it.
TEST(RegisterArmCut, WithNothingToCutTheSideIsStillSaid)
{
    ibRegBound upper;
    upper.m_date = ibValue(kMidnight);
    upper.m_excluding = true;
    ibMaterializeReadSpec read;
    ibRegFillArmCut(read, static_cast<const ibValueMetaObjectAccumulationRegister*>(nullptr), upper);

    EXPECT_TRUE(read.m_markColumn.IsEmpty()) << "one arm: nothing to cut, no mark";
    EXPECT_TRUE(read.m_toExcluding);
}

// An INCLUDED midnight takes the instant and not the day: that is a partial grain, so it is cut there.
TEST(RegisterArmCut, AnIncludedDateOnAGrainEdgeIsCutAtThatEdge)
{
    ArmCutFix f;
    ASSERT_NE(f.reg, nullptr);
    const ibMaterializeReadSpec read = f.CutAt(kMidnight, /*excluding*/ false);

    ASSERT_EQ(read.m_floor.GetType(), TYPE_DATE);
    EXPECT_EQ(read.m_floor.GetDateTime(), kMidnight);
    EXPECT_FALSE(read.m_toExcluding);
}

// Inside a grain the cut stands at the grain's start, and the side travels with it.
TEST(RegisterArmCut, ADateInsideAGrainIsCutAtItsStartAndKeepsItsSide)
{
    ArmCutFix f;
    ASSERT_NE(f.reg, nullptr);
    const ibMaterializeReadSpec read = f.CutAt(kAfternoon, /*excluding*/ true);

    ASSERT_EQ(read.m_floor.GetType(), TYPE_DATE);
    EXPECT_EQ(read.m_floor.GetDateTime(), kMidnight);
    EXPECT_TRUE(read.m_toExcluding);
}

// ⭐ AND THE NUMBERS, LIVE ON SQLITE: the cut the register fills, the reading the builder renders, the
// totals its own triggers keep. 100 received on 03.03, 12 written off on 04.03 - and 30 received on 05.03 at
// 09:00, which is the day the boundary stands at the start of. Before 05.03 there were 88; the defect said 118.
TEST(RegisterArmCut, TheBalanceBeforeMidnightLeavesOutTheDayThatStartsThere)
{
    wxInitializer wxInit;
    if (!wxInit.IsOk())
        GTEST_SKIP() << "wxBase init failed (no wxApp host)";
    if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
        GTEST_SKIP() << "appData env unavailable headless";
    struct EnvGuard { ~EnvGuard() { if (ibApplicationData::Get() != nullptr) ibApplicationData::DestroyAppDataEnv(); } } guard;
    ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
    if (pool == nullptr)
        GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
    auto db = std::make_shared<ibDatabaseLayerSQLite>();
    if (!db->Open(wxT(":memory:")))
        GTEST_SKIP() << "in-memory SQLite open failed";
    pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

    ArmCutFix f;
    ASSERT_NE(f.reg, nullptr);
    // The mark is whatever the register's recorder lays out first - the movements carry it under that name.
    const wxString mark = f.CutAt(kMidnight, true).m_markColumn;
    ASSERT_FALSE(mark.IsEmpty());

    ibMaterializeSpec spec;
    spec.m_table            = wxT("Reg9_T");
    spec.m_source           = wxT("Reg9");
    spec.m_keyColumns       = { wxT("wh") };
    spec.m_periodColumn     = wxT("period_");
    spec.m_periodSourceExpr = wxT("{row}.period_");
    spec.m_periodUnit       = ibTotalsPeriod::Day;
    spec.m_guard            = wxT("{row}.active_ <> 0");
    spec.m_deltas = {
        { wxT("qty_in"),  wxT("CASE WHEN {row}.rectype_ = 1 THEN {row}.qty ELSE 0 END") },
        { wxT("qty_out"), wxT("CASE WHEN {row}.rectype_ = 1 THEN 0 ELSE {row}.qty END") },
    };
    ibMaterializeView view;
    view.m_name = wxT("Reg9_Turnovers");
    view.m_columns = { { wxT("Qty_Turnover"), wxT("qty_in"), wxT("qty_out"), ibMaterializeAgg::Difference } };
    view.m_withMovements = true;
    view.m_movementColumns = { { mark, ibTypeString(36) } };
    spec.m_views = { view };

    db->RunQuery(wxT("%s"), wxT("CREATE TABLE Reg9 (") + mark + wxT(" TEXT NOT NULL, line_ INTEGER NOT NULL, period_ TEXT NOT NULL, ")
        wxT("active_ INTEGER NOT NULL, wh TEXT NOT NULL, rectype_ INTEGER NOT NULL, qty NUMERIC NOT NULL)"));
    db->RunQuery(wxT("CREATE TABLE Reg9_T (period_ TEXT NOT NULL, wh TEXT NOT NULL, ")
        wxT("qty_in NUMERIC NOT NULL DEFAULT 0, qty_out NUMERIC NOT NULL DEFAULT 0, PRIMARY KEY (period_, wh))"));
    const ibMaterializeSql sql = RenderMaterialization(spec, &ibDatabaseLayerSQLite::MaterializationDialect(), ibDatabaseLayerSQLite::Dialect());
    ASSERT_TRUE(sql.Apply(*db));

    db->RunQuery(wxT("INSERT INTO Reg9 VALUES ('r1', 1, '2026-03-03 09:00:00', 1, 'kitchen', 1, 100)"));
    db->RunQuery(wxT("INSERT INTO Reg9 VALUES ('w1', 1, '2026-03-04 23:00:00', 1, 'kitchen', 0, 12)"));
    db->RunQuery(wxT("INSERT INTO Reg9 VALUES ('r2', 1, '2026-03-05 09:00:00', 1, 'kitchen', 1, 30)"));

    const auto balance = [&](const wxDateTime& upTo, bool excluding) {
        ibMaterializeReadSpec read = f.CutAt(upTo, excluding);   // the cut, as the register fills it
        read.m_view         = wxT("Reg9_Turnovers");
        read.m_periodColumn = wxT("period_");
        read.m_keyColumns   = { wxT("wh") };
        read.m_to           = ibValue(upTo);
        read.m_columns = { { wxT("Qty_Balance"), wxT("Qty_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true } };

        ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
        q.From(RenderMaterializedRead(read, wxT("b")));
        q.Project({ ibQueryProjItem{ ibCol(wxT("b"), wxT("Qty_Balance")), wxT("Qty_Balance") } });
        ibQueryResult rs = q.Execute();
        return rs.Next() ? rs.GetResultDouble(wxT("Qty_Balance")) : -1e9;
    };

    EXPECT_DOUBLE_EQ(balance(kMidnight, /*excluding*/ true), 88.0) << "the day that starts at the edge is not before it";
    EXPECT_DOUBLE_EQ(balance(kMidnight, /*excluding*/ false), 88.0) << "the instant of midnight holds no movement";
    EXPECT_DOUBLE_EQ(balance(kAfternoon, /*excluding*/ true), 118.0) << "inside the day its morning counts";
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
    EXPECT_EQ(wxT("Amount Balance"), ibRegColumnCaptionOf(wxT("Amount"), wxT("Balance")));
    // A resource with no synonym of its own must not produce a leading space.
    EXPECT_EQ(wxT("Balance"), ibRegColumnCaptionOf(wxEmptyString, wxT("Balance")));
}

// =============================================================================
// The derived-surface cache — built once, rebuilt when the shape moves, never freed under a reader
// =============================================================================

TEST(RegisterSurface, TheSameQuestionGetsTheSameSurface) {
    ibRegSurfaceCache cache;
    ibTypeDescription type;
    const auto build = [&type](std::vector<ibTempColumn>& columns) {
        columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, ibRegDerivedColumnId(1001)));
    };

    const ibBackendQueryable* first  = cache.Obtain(wxT("k"), wxT("sig-1"), wxT("T"), nullptr, build);
    const ibBackendQueryable* second = cache.Obtain(wxT("k"), wxT("sig-1"), wxT("T"), nullptr, build);
    ASSERT_NE(nullptr, first);
    EXPECT_EQ(first, second) << "same key, same signature — the surface is built once";
}

TEST(RegisterSurface, AChangedShapeRebuildsAndTheOldPointerStaysAlive) {
    ibRegSurfaceCache cache;
    ibTypeDescription type;
    const auto one = [&type](std::vector<ibTempColumn>& columns) {
        columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, ibRegDerivedColumnId(1001)));
    };
    const auto two = [&type](std::vector<ibTempColumn>& columns) {
        columns.push_back(ibTempColumn(wxT("A"), wxT("fldA"), type, ibRegDerivedColumnId(1001)));
        columns.push_back(ibTempColumn(wxT("B"), wxT("fldB"), type, ibRegDerivedColumnId(1002)));
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

TEST(RegisterSurface, ADerivedColumnSaysByItsSignThatNobodyDeclaredIt) {
    // 🛑 It used to be a positive BAND, 0x50000000: the last of five hand-carved ranges to outlive
    // the sign scheme that replaced them (§ 31.2 of the query-language arc). A surface therefore
    // published columns nobody declared under ids indistinguishable from an attribute's metaID —
    // against the invariant `GetColumnId`'s own note calls structural. Pinned here as the RULE
    // rather than as the constant, so the next renumbering has to keep the property and not the
    // number.
    const ibMetaID standing = ibRegDerivedColumnId(1087);      // a column standing for attribute 1087
    const ibMetaID owned    = ibRegDerivedColumnId(1087, 1);   // …and one attribute 1087 owns
    EXPECT_TRUE(ibBackendQueryColumn::IsSyntheticId(standing));
    EXPECT_TRUE(ibBackendQueryColumn::IsSyntheticId(owned));
    EXPECT_NE(standing, owned);
}

TEST(RegisterSurface, DerivedColumnIdsNeverMeetAndLeaveRoomForAnotherReading) {
    // ⭐ Numbered over a declared metaID and a place (0 = the attribute itself, 1.. = a column it owns):
    // every (metaID, place) pair is its own number, whatever the metaIDs are…
    std::set<ibMetaID> ids;
    for (ibMetaID owner = 0; owner < 512; ++owner)
        for (unsigned int no = 0; no < ibRegColumnsPerOwner; ++no)
            EXPECT_TRUE(ids.insert(ibRegDerivedColumnId(owner, no)).second) << "owner " << owner << " no " << no;

    // …and a configuration's metaIDs leave room for a further reading of the surface (an alias twin wraps
    // the id once more — queryColumn.h, CanComposeSyntheticId), up to about 130 000.
    EXPECT_TRUE(ibBackendQueryColumn::CanComposeSyntheticId(ibRegDerivedColumnId(130000, ibRegColumnsPerOwner - 1)));
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
//   active-passive onto the side its net stands on — the server road's rows each stand on ONE set of
//                  the account's analytics (all its slots are in the key), and within one set folding
//                  is what the balance IS: a customer shipped 100 and paid 60 owes 40. A receivable
//                  and a payable of two different counterparties are two rows and never meet.
//
// The RAM oracle is FoldSideByAccountType; if these two ever disagree, one road reports a different
// balance than the other for the same data, and nothing about either answer looks wrong.

namespace {

// The RAM rule for a row that stands on one set of analytics, restated here as the oracle — deliberately
// by hand, so a change to the engine's copy does not silently change what this test holds to be correct.
void FoldOracle(int accountType, double& debit, double& credit)
{
    const double net = debit - credit;
    const bool onDebit = accountType == ibAccountType::eActive
        || (accountType == ibAccountType::eActivePassive && net >= 0.0);
    if (onDebit) { debit = net;  credit = 0.0; }
    else         { debit = 0.0; credit = -net; }
}

// What the projected CASE computes (FoldedPairOnServer), in the same order of tests the relation declares.
// `apFolds` — the row stands on one set of the account's analytics (FullAnalyticsOnServer).
void FoldAsProjected(int accountType, double& debit, double& credit, bool apFolds = true)
{
    const double dr = debit, cr = credit;
    const bool apFolding   = accountType == ibAccountType::eActivePassive && apFolds;
    const bool debitStands = apFolding && dr >= cr;
    debit  = accountType == ibAccountType::eActive  ? dr - cr
           : accountType == ibAccountType::ePassive ? 0.0
           : debitStands                            ? dr - cr
           : apFolding                              ? 0.0 : dr;
    credit = accountType == ibAccountType::ePassive ? cr - dr
           : accountType == ibAccountType::eActive  ? 0.0
           : debitStands                            ? 0.0
           : apFolding                              ? cr - dr : cr;
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

TEST(AcctBalanceServerRoad, ActivePassiveFoldsWithinOneSetOfAnalytics) {
    // One counterparty's row: shipped 69 820, paid 55 000 — it OWES 14 820, on the debit side. Both
    // figures reported as its balance (the rule until 2026-09-15) read as a receivable and a payable at once.
    double dr = 69820.0, cr = 55000.0;
    FoldAsProjected(ibAccountType::eActivePassive, dr, cr);
    EXPECT_DOUBLE_EQ(14820.0, dr);
    EXPECT_DOUBLE_EQ(0.0,     cr);

    // …and a supplier we owe more than we paid stands on the credit side.
    dr = 30000.0; cr = 63000.0;
    FoldAsProjected(ibAccountType::eActivePassive, dr, cr);
    EXPECT_DOUBLE_EQ(0.0,     dr);
    EXPECT_DOUBLE_EQ(33000.0, cr);
}

TEST(AcctBalanceServerRoad, ActivePassiveAcrossSeveralSetsKeepsBothSides) {
    // A row broken down by fewer kinds than the account keeps a balance along stands on several sets at once —
    // a receivable of one contract and a payable of another — and netting them would report neither.
    double dr = 69820.0, cr = 55000.0;
    FoldAsProjected(ibAccountType::eActivePassive, dr, cr, /*apFolds*/ false);
    EXPECT_DOUBLE_EQ(69820.0, dr);
    EXPECT_DOUBLE_EQ(55000.0, cr);

    // …while an active or a passive account folds whatever the row stands on.
    dr = 150.0; cr = 40.0;
    FoldAsProjected(ibAccountType::eActive, dr, cr, /*apFolds*/ false);
    EXPECT_DOUBLE_EQ(110.0, dr);
    EXPECT_DOUBLE_EQ(0.0,   cr);
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
    // The correspondent's analytics are columns of the row, not a list of kinds asked for.
    EXPECT_EQ(-1, a.m_kindsCr);
    EXPECT_EQ(7, a.m_count);
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

// =============================================================================
// A virtual table's condition is applied INSIDE the reading (2026-09-17)
// =============================================================================
//
// The condition written into a table's parameters selects the rows before they are folded. These pin the
// shared half every register by-name takes (ibRegConditionOn): the whole tree goes down, each column found
// again on the surface the reading stands on, a walk kept as a walk and marked for EXISTS, a column the
// rows cannot be selected by refused rather than dropped.

namespace {

class ConditionColumn : public ibBackendQueryColumn {
public:
	ConditionColumn(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

class ConditionSurface : public ibBackendQueryable {
public:
	explicit ConditionSurface(const wxString& name) : m_name(name) {}
	void Add(const ibBackendQueryColumn* column) { m_columns.push_back(column); }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_columns; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* column : m_columns)
			if (column->GetName().IsSameAs(name, false))
				return column;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* column) const override {
		return std::find(m_columns.begin(), m_columns.end(), column) != m_columns.end();
	}
	wxString GetQueryTableName() const override { return m_name; }
	ibMetaID GetQueryTableId() const override { return 1; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	wxString m_name;
	ibUniqueKey m_key;
	std::vector<const ibBackendQueryColumn*> m_columns;
};

const ibMetaID kWarehouseId = 101, kItemId = 102, kQuantityId = 103, kCodeId = 201;

ibQueryPredicatePtr Equal(const ibBackendQueryColumn* column, const ibValue& value,
                          const std::vector<const ibBackendQueryColumn*>& path = {})
{
	ibQueryCondition leaf;
	leaf.m_col   = column;
	leaf.m_value = value;
	leaf.m_path  = path;
	return ibQueryPredicate::Leaf(leaf);
}

const auto kDimensionsOnly = [](const ibBackendQueryColumn* column) {
	return column != nullptr && (column->GetColumnId() == kWarehouseId || column->GetColumnId() == kItemId);
};

} // namespace

TEST(VirtualTableCondition, TheWholeTreeIsFoundAgainOnTheSurfaceItReads) {
	// Written against the movements, read on the totals view: same names, other columns.
	ConditionColumn warehouse(wxT("Warehouse"), kWarehouseId), item(wxT("Item"), kItemId);
	ConditionColumn viewWarehouse(wxT("Warehouse"), kWarehouseId), viewItem(wxT("Item"), kItemId);
	ConditionSurface view(wxT("Turnovers"));
	view.Add(&viewWarehouse);
	view.Add(&viewItem);

	const ibQueryPredicatePtr written = ibQueryPredicate::Compose(ibQueryPredicateKind::Or,
		ibQueryPredicate::Not(Equal(&warehouse, ibValue(wxT("Main")))),
		ibQueryPredicate::Null(&item, /*negated*/ false));

	const ibQueryPredicatePtr onView = ibRegConditionOn(&view, written, kDimensionsOnly);
	ASSERT_TRUE(onView);
	ASSERT_EQ(ibQueryPredicateKind::Or, onView->m_kind);
	ASSERT_EQ(2u, onView->m_children.size());
	ASSERT_EQ(ibQueryPredicateKind::Not, onView->m_children[0]->m_kind);
	EXPECT_EQ(&viewWarehouse, onView->m_children[0]->m_children[0]->m_leaf.m_col);
	EXPECT_EQ(&viewItem, onView->m_children[1]->m_col);

	// …and the tree the author wrote is left as written.
	EXPECT_EQ(&warehouse, written->m_children[0]->m_children[0]->m_leaf.m_col);
}

TEST(VirtualTableCondition, AWalkThroughAReferenceStaysAWalkAndIsAskedAsExists) {
	ConditionColumn warehouse(wxT("Warehouse"), kWarehouseId), code(wxT("Code"), kCodeId);
	ConditionColumn viewWarehouse(wxT("Warehouse"), kWarehouseId);
	ConditionSurface view(wxT("Balance"));
	view.Add(&viewWarehouse);

	const ibQueryPredicatePtr onView = ibRegConditionOn(&view,
		Equal(&code, ibValue(wxT("01")), { &warehouse, &code }), kDimensionsOnly);
	ASSERT_TRUE(onView);
	ASSERT_EQ(2u, onView->m_leaf.m_path.size());
	EXPECT_EQ(&viewWarehouse, onView->m_leaf.m_path.front());   // the head is the surface's own column
	EXPECT_EQ(&code, onView->m_leaf.m_path.back());             // the leaf stays the target's
	EXPECT_TRUE(onView->m_leaf.m_asExists);                     // a filter never multiplies a row
}

TEST(VirtualTableCondition, AColumnTheRowsCannotBeSelectedByIsRefusedNotDropped) {
	// A figure exists only after the fold: dropped, the leaf would widen the answer without a word.
	ConditionColumn quantity(wxT("Quantity"), kQuantityId);
	ConditionColumn viewQuantity(wxT("Quantity"), kQuantityId);
	ConditionSurface view(wxT("Balance"));
	view.Add(&viewQuantity);

	EXPECT_THROW(ibRegConditionOn(&view, Equal(&quantity, ibValue(5)), kDimensionsOnly), ibBackendException);
}

TEST(VirtualTableCondition, WhatAConditionNamesDecidesWhereItMayStand) {
	ConditionColumn warehouse(wxT("Warehouse"), kWarehouseId), item(wxT("Item"), kItemId), quantity(wxT("Quantity"), kQuantityId);

	const ibQueryPredicatePtr keysOnly = ibQueryPredicate::Compose(ibQueryPredicateKind::And,
		Equal(&warehouse, ibValue(wxT("Main"))), ibQueryPredicate::Not(Equal(&item, ibValue(wxT("Tea")))));
	EXPECT_TRUE(ibRegConditionOnlyNames(keysOnly, kDimensionsOnly));

	const ibQueryPredicatePtr withAFigure = ibQueryPredicate::Compose(ibQueryPredicateKind::Or,
		Equal(&warehouse, ibValue(wxT("Main"))), Equal(&quantity, ibValue(0)));
	EXPECT_FALSE(ibRegConditionOnlyNames(withAFigure, kDimensionsOnly));
}

TEST(VirtualTableCondition, EveryRegistersConditionSlotIsConsumedByTheTable) {
	// Sugar around the table read and folded the whole register first; the slot is the table's own.
	std::vector<ibQuerySourceParameter> declared;
	ibAppendRegisterConditionParameter(declared);
	ASSERT_EQ(1u, declared.size());
	EXPECT_TRUE(declared.front().m_condition);
	EXPECT_TRUE(declared.front().m_consumedBySource);
}
