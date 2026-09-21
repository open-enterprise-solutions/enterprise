#ifndef __REGISTER_QUERY_LOWERING_H__
#define __REGISTER_QUERY_LOWERING_H__

// Shared L2-IR lowering helpers for register managers (information / accumulation /
// accounting): turn an attribute's metadata into physical-field IR — the flat field
// list and a composite (multi-field) predicate with bound Const values. Lets register
// balance / turnover / slice queries be built as structured ibQueryIR instead of raw
// concatenated SQL (dialect-free, injection-impossible, no manual positional binding).

#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dbTableProvider.h"   // ibDbTableProvider::SetValueAttribute — the DB write decomposition
#include "backend/query/columnLayout.h"      // the column-layout tier: ColumnFieldNames / ColumnFieldList / etc.
#include "backend/system/value/valueMap.h"   // ibValueStructure — the sugar ibRegFilterPredicate converts
#include "backend/system/value/valueTable.h" // ibValueModelTable — the table a reading hands a script
#include "backend/query/dataQueryBuilder.h"  // ibDataQueryResult — the selection those rows come off
#include "backend/query/tempTableQueryable.h" // ibTempColumn / ibDbTempTableQueryable — a derived surface IS one
#include "backend/query/schemaSnapshot.h"     // ibSchemaTable / ibSchemaMaterialize — the totals bundle's own vocabulary
#include "backend/query/queryColumn.h"        // ibBackendQueryColumn::SyntheticId — a derived column's number
#include "backend/query/queryHierarchy.h"     // ibQueryHierarchyScope — IN HIERARCHY in a reading's condition, resolved

// ⚠ NAMED, NOT INHERITED — MSVC hands these over transitively and GCC / Clang do not.
#include <algorithm>   // std::find — the running grid names each figure once
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "backend/system/value/valuePointInTime.h"   // ibValuePointInTime — a boundary that names a document
#include "backend/system/value/valueBoundary.h"       // ibValueBoundary — the position AND which side of it
#include "backend/stringUtils.h"             // CompareString — the one case-insensitive name comparison
#include "backend/query/queryableFactory.h"  // ibQuerySourceParameter — what a virtual table DECLARES it takes
#include "backend/session/session.h"         // ses_query — the session's channel; ibRequireOpenBase asks it

// --- the boundary a reading was asked to stand at --------------------------------------------------
// ⭐⭐ A BOUNDARY IS A DATE, OR A DATE AND THE DOCUMENT AT IT.
//
// "The balance on the 5th" and "the balance as of THIS document" are the same question asked at two
// precisions, and the second is not a luxury: three documents can carry one date, and a balance that
// cannot separate them answers about a moment nobody asked for. A PointInTime carries the pair, so
// the boundary is read from it rather than assembled at each callsite.
//
// ⚠ A boundary WITH a recorder cannot be answered by a totals view. Rolling movements up by period
// is exactly what drops the recorder, so the surface has no column to compare against — the reading
// has to stand on the MOVEMENTS. That is not a limitation of the view; it is what a view IS.
struct ibRegBound {
	ibValue m_date;          // the instant -- always the coarse half of the comparison
	ibValue m_recorder;      // the document AT that instant, when one was named
	bool    m_excluding = false;   // is the position itself OUTSIDE the interval?

	bool IsEmpty()      const { return m_date.IsEmpty(); }
	bool HasRecorder()  const { return !m_recorder.IsEmpty(); }
};

// Reads a boundary argument: a PointInTime yields both halves, anything else IS the date. One
// function, so a moment written in a script and a moment written in a query mean the same thing.
inline ibRegBound ibReadRegisterBound(const ibValue& given)
{
	ibRegBound bound;

	// ⭐ A BOUNDARY WRAPS A POSITION; it does not replace one. Unwrapped first and then read exactly
	// as a bare position would be, so `Balance(d)`, `Balance(moment)` and `Balance(Boundary(moment,
	// Excluding))` travel one road and differ only in which side of the position is meant.
	ibValueBoundary* boundary = nullptr;
	if (given.ConvertToValue(boundary) && boundary != nullptr) {
		bound = ibReadRegisterBound(boundary->m_value);
		bound.m_excluding = (boundary->m_kind == ibBoundaryKind_Excluding);
		return bound;
	}

	ibValuePointInTime* moment = nullptr;
	if (given.ConvertToValue(moment) && moment != nullptr) {
		if (moment->m_date.IsValid())
			bound.m_date = ibValue(moment->m_date);
		bound.m_recorder = moment->m_reference;
		return bound;
	}

	bound.m_date = given;
	return bound;
}

// --- the granularity a reading was asked to fold by ------------------------------------------------
// ⭐⭐ THE UNIT VOCABULARY, WRITTEN ONCE.
//
// The same ten words used to sit in three places: a `switch` in the reader, a table in the schema
// builder (which spells the view's projection columns), and a list of choices in DescribeParameters.
// Three copies of one dictionary, and each reader of a word would have gone on compiling while the
// other two changed — the drift shows up as a column nobody can name.
//
// ⚠ The ORDER matters and is not alphabetical: it is coarseness, ascending. The schema offers only
// the projections COARSER than what the totals store (`u > GetTotalsPeriodUnit()`) — an hour cannot
// be recovered from a day already summed — and that comparison is on the enum, so the enum's order
// is the meaning. Keep new units in their place on that scale.
//
// Shared rather than accumulation-only: an accounting register reports turnovers by month by the
// same words, and a second copy is how two registers come to disagree about what "Quarter" means.
// ⭐ THE TABLE MOVED, THE NAMES STAYED. It lives beside the expression that truncates by these
// units now (ibPeriodUnits, query/queryable.h), because a TOTALS level says a periodicity too —
// `BY Period PERIODS(Month, …)` — and the register was never the only one asking. These two remain
// as the register's own spelling of the same question.
inline const std::vector<std::pair<ibTotalsPeriod, wxString>>& ibRegisterUnits() { return ibPeriodUnits(); }
inline wxString ibRegisterUnitWord(ibTotalsPeriod unit) { return ibPeriodUnitWord(unit); }

// ⭐⭐ WHAT THE READING WAS ASKED TO FOLD BY — the WORD's MEANING, decided once.
//
// A periodicity arrives as a word (`Month`, `Period`, `Recorder`, left out). Five different answers
// live in that one argument, and they were being carried as a PAIR — `(ibTotalsPeriod unit, bool
// unitGiven)`. Two values hold two answers, so the moment there was a third the code had to pick
// which two to keep: `Period` and "left out" were both read as `unitGiven == false`, even though the
// parameter's own description says they are different questions ("the interval whole, one row per
// key" against "a row PER PERIOD"). Nothing was broken by a mistake — the shape had no room.
//
// So the answer is a TYPE, and the reading asks it what it means rather than reconstructing the
// meaning from a flag. The word itself stays a word ([[the periodicity is not a registered
// enumeration]]): this classifies it, it does not replace it.
enum class ibRegGranularity {
	Whole,      // nothing asked for — one row per key over the whole interval, no period column
	Auto,       // asked for explicitly and left UNDECIDED — every projection stays on offer
	Period,     // a row per the register's OWN period, exactly as stored
	Calendar,   // a row per calendar unit — the unit is in m_unit
	Recorder,   // a row per DOCUMENT — read from the movements, not from a rolled-up total
	Record      // a row per movement LINE — likewise
};

struct ibRegFold {
	ibRegGranularity m_kind = ibRegGranularity::Whole;
	ibTotalsPeriod   m_unit = ibTotalsPeriod::Month;   // meaningful when m_kind == Calendar

	// ⚠ WHOLE AND AUTO READ THE SAME AND OFFER DIFFERENTLY. Neither folds by a period, so the
	// reading treats them alike; but "left out" means the table has NO period column, while `Auto`
	// means nobody has decided yet and every projection the table can make stays on offer. Two
	// answers, and collapsing them is how a window promises a column the rows will not carry.
	bool IsWholeInterval()        const { return m_kind == ibRegGranularity::Whole || m_kind == ibRegGranularity::Auto; }
	bool OffersEveryProjection()  const { return m_kind == ibRegGranularity::Auto; }

	// A calendar fold groups by a TRUNCATED period; the register's own period groups by the column
	// as it stands. Both put a period column in the answer, which is why they are asked together.
	bool IsCalendar()    const { return m_kind == ibRegGranularity::Calendar; }
	bool HasPeriod()     const { return m_kind == ibRegGranularity::Calendar || m_kind == ibRegGranularity::Period; }

	// ⭐ THE MOVEMENTS ANSWER THIS ONE. A recorder is not a calendar interval — no rolled-up total
	// carries it, because rolling up is exactly what drops it. So the reading stands on the
	// register's own table instead of its view, and that is a property of the fold, asked here.
	bool FromMovements() const { return m_kind == ibRegGranularity::Recorder || m_kind == ibRegGranularity::Record; }
	bool HasLineNumber() const { return m_kind == ibRegGranularity::Record; }
};

// The word -> the fold. CASE-INSENSITIVE THROUGH THE ONE HELPER THE ENGINE ALREADY USES —
// `stringUtils::CompareString`, the same one the bytecode resolver and the value system compare
// names by. A second spelling of "the same word" is how two parts of a program start disagreeing
// about what a name is. A number is accepted as the unit's ordinal (the composer's own form).
inline ibRegFold ibReadRegisterFold(const ibValue& given)
{
	ibRegFold fold;

	if (given.GetType() == TYPE_NUMBER) {
		const long n = given.GetInteger();
		if (n >= static_cast<long>(ibTotalsPeriod::Second) && n <= static_cast<long>(ibTotalsPeriod::Year)) {
			fold.m_kind = ibRegGranularity::Calendar;
			fold.m_unit = static_cast<ibTotalsPeriod>(n);
		}
		return fold;
	}

	if (given.GetType() != TYPE_STRING)
		return fold;   // absent, or something this argument does not take: the interval whole

	const wxString word = given.GetString();
	if (word.IsEmpty())
		return fold;
	if (stringUtils::CompareString(word, wxT("Auto")))     { fold.m_kind = ibRegGranularity::Auto;     return fold; }
	if (stringUtils::CompareString(word, wxT("Period")))   { fold.m_kind = ibRegGranularity::Period;   return fold; }
	if (stringUtils::CompareString(word, wxT("Recorder"))) { fold.m_kind = ibRegGranularity::Recorder; return fold; }
	if (stringUtils::CompareString(word, wxT("Record")))   { fold.m_kind = ibRegGranularity::Record;   return fold; }

	for (const std::pair<ibTotalsPeriod, wxString>& u : ibRegisterUnits()) {
		if (stringUtils::CompareString(word, u.second)) {
			fold.m_kind = ibRegGranularity::Calendar;
			fold.m_unit = u.first;
			return fold;
		}
	}

	ibBackendCoreException::Error(
		_("periodicity '%s' is not one of: Period, Record, Recorder, Auto, Second..Year"), word);
	return fold;
}

// --- A DERIVED TOTALS TABLE AS A METAOBJECT -------------------------------------------------------
// It carries no data and declares nothing — it exists to hold an IDENTITY, which is exactly what a
// derived table lacked.
//
// The schema differ matches a table by id and never by name, so a totals table needs an id that
// belongs to no one else. It used to be derived arithmetically from the register's own metaID: they
// are small sequential integers, so `metaID ^ 1` was the NEIGHBOURING metaobject's id, and the totals
// declaration poured its columns into an unrelated table. A metaobject answers both questions at once
// — GenerateNewID walks every child in the tree, so the id is unique BY CONSTRUCTION rather than by a
// range nobody has claimed yet, and it survives a save because the holder writes this object's whole
// node inside the register's own.
//
// It lives HERE, with the rest of the register machinery: an accumulation register wants one per kind
// and an accounting register one per side, and a second class of identical body is how two registers
// come to differ in a detail nobody meant to change.
class BACKEND_API ibValueMetaObjectRegisterTotals : public ibValueMetaObject {
public:
	ibValueMetaObjectRegisterTotals(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString,
		const wxString& comment = wxEmptyString) : ibValueMetaObject(name, synonym, comment) {
	}
	virtual ~ibValueMetaObjectRegisterTotals() {}
};

// --- THE FIGURES, IN ONE PLACE -------------------------------------------------------------------
// ⭐⭐ ONE VOCABULARY FOR EVERY REGISTER THAT REPORTS ONE.
//
// A view builds its columns as `<Resource>` + a suffix; a reading spells the same word again to
// aggregate them; a manager spells it a third time to name a column in the table a script gets back.
// Three spellings of one name, and the day one of them changes the other two go on compiling and
// answer with nothing — which is how `Resource1_Turnover` and `Resource1Turnover` came to be two
// different columns for one figure.
//
// It lived inside the accumulation register first. It is here now because the accounting register
// reports the same figures — a balance, a turnover, an opening and a closing — and differs only in
// that each of them has a SIDE. A second copy of the words is how two registers come to disagree
// about what a column is called.
//
// ⚠ The PERIOD is deliberately not here: its column is named after the register's own period
// attribute, so the name belongs to the metadata rather than to this list.
namespace ibRegFigure {
	inline constexpr const wxChar* Turnover       = wxT("Turnover");
	inline constexpr const wxChar* Receipt        = wxT("Receipt");        // accumulation's two sides
	inline constexpr const wxChar* Expense        = wxT("Expense");
	inline constexpr const wxChar* Balance        = wxT("Balance");
	inline constexpr const wxChar* OpeningBalance = wxT("OpeningBalance");
	inline constexpr const wxChar* ClosingBalance = wxT("ClosingBalance");

	// ⭐⭐ THE SAME BALANCE, NOT NETTED BETWEEN THE SIDES. An accounting balance is reported FOLDED: an
	// active account carries what is left on the debit and nothing on the credit, and the two sides of
	// an active-passive one are netted onto whichever side the difference stands. That is the figure a
	// bookkeeper signs — and it is not the figure they check WITH, because the fold hides what each
	// side actually holds. The gross pair is what was there before the netting: "debits of 25 000 and
	// credits of 17 000", against a folded "8 000 on the debit".
	//
	// GROSS / NET are the accounting words for the pair (the reference calls them the expanded and
	// the collapsed balance); the ordinary `Balance` above stays the folded one, because that is what every
	// reading of it has always meant.
	inline constexpr const wxChar* GrossBalance        = wxT("GrossBalance");
	inline constexpr const wxChar* OpeningGrossBalance = wxT("OpeningGrossBalance");
	inline constexpr const wxChar* ClosingGrossBalance = wxT("ClosingGrossBalance");

	// ⭐ WHAT THE CORRESPONDENT MOVED, beside what the account moved. A figure the two sides of an entry
	// do not agree on (a quantity, a currency amount — `Balance` cleared) is two numbers on one line: what
	// reached the debit account and what left the credit one. A turnover row about one account reports its
	// own under `Turnover` and the other side's under this word, the way the reference does.
	inline constexpr const wxChar* CorrTurnover = wxT("CorrTurnover");
}

// The SIDE an accounting figure stands on. Not a third and fourth figure name — the same figures, said
// of one side of an entry, which is exactly why the suffix is separate from the word.
namespace ibRegSide {
	inline constexpr const wxChar* Debit  = wxT("Dr");
	inline constexpr const wxChar* Credit = wxT("Cr");
}

// `<figure><side>` — built rather than written out, so a figure and its sided spelling cannot drift.
inline wxString ibRegSidedFigure(const wxString& figure, bool credit)
{
	return figure + (credit ? ibRegSide::Credit : ibRegSide::Debit);
}

// ⭐⭐ THE SAME FIGURE, SAID TO A PERSON. The words above are NAMES — they go into a query, a column
// list, a stored view — and a name is not a caption: `Resource1ClosingBalance` is exactly right at
// the head of a SELECT and exactly wrong at the head of a column somebody reads.
//
// It lives HERE, beside the words it captions, for the reason every other pair in this header does:
// the two managers each built their own captions (`_("Balance")` spelled at the callsite), so the
// figure had a presentation through the script door and none at all through the query door. One
// list, and a figure added to it is captioned wherever it appears.
inline wxString ibRegFigureCaption(const wxString& figure)
{
	if (figure == ibRegFigure::Turnover)       return _("Turnover");
	if (figure == ibRegFigure::Receipt)        return _("Receipt");
	if (figure == ibRegFigure::Expense)        return _("Expense");
	if (figure == ibRegFigure::Balance)        return _("Balance");
	if (figure == ibRegFigure::OpeningBalance) return _("Opening balance");
	if (figure == ibRegFigure::ClosingBalance) return _("Closing balance");
	if (figure == ibRegFigure::GrossBalance)        return _("Gross balance");
	if (figure == ibRegFigure::OpeningGrossBalance) return _("Opening gross balance");
	if (figure == ibRegFigure::ClosingGrossBalance) return _("Closing gross balance");
	if (figure == ibRegFigure::CorrTurnover)        return _("Corresponding turnover");
	return figure;   // a figure nobody captioned reads as its own name rather than as nothing
}

// The side, said to a PERSON — the caption twin of ibRegSide, and the one place the two words are
// spelled. A FIELD held per side is captioned with it too ("Currency Dr"), so it stands on its own
// rather than inside the figure's caption below.
inline wxString ibRegSideCaption(bool credit)
{
	return credit ? _("Cr") : _("Dr");
}

// The sided caption — the same pairing as ibRegSidedFigure, one tier up. Kept beside it so a side
// that gains a spelling gains a caption in the same edit.
inline wxString ibRegSidedCaption(const wxString& figure, bool credit)
{
	return ibRegFigureCaption(figure) + wxT(" ") + ibRegSideCaption(credit);
}

// A published column's full caption: what it is OF, then what it is. `<resource synonym> <figure>` —
// "Amount Closing balance", "Currency Dr" — in the reader's language, whichever door produced the row.
inline wxString ibRegColumnCaptionOf(const wxString& ofWhat, const wxString& caption)
{
	return ofWhat.IsEmpty() ? caption : ofWhat + wxT(" ") + caption;
}

// --- the GRAIN: where a union view is cut between its two arms ------------------------------------
// ⭐⭐ ONE MECHANISM, NOT ONE PER REGISTER.
//
// A totals view carries the stored rows AND the movements that came after them, because a maintained
// total is complete only down to the grain it is stored at (a day) and everything inside the current
// grain lives in the movements and nowhere else. Both halves describe the same figures, so a read that
// took them all would count the current grain TWICE — and the result looks entirely plausible.
//
// The rule is identical for every register that stores totals: take the stored rows BELOW the
// boundary's grain, and the movements of that grain up to the boundary. It was written inside the
// accumulation register first; it lives here now because the accounting register asks exactly the same
// question, and a second copy is how two registers come to disagree about where a day ends.
//
// TReg only has to vend HasRecorder / GetRegisterRecorder / GetMetaData / GetTotalsPeriodUnit.

// The recorder's fields carrying a boundary's document, decomposed by the WRITE codec — the same
// decomposition the rows were stored through, so the comparison rides exactly the fields the index is
// built on. The discriminator is dropped: it says what KIND of value this is, which is the same for
// every recorder and would only make the tuple one constant longer.
template <typename TReg>
inline std::vector<std::pair<wxString, ibQueryExprPtr>> ibRegRecorderTuple(
	const TReg* reg, const std::vector<ibColumnSlot>& slots, const ibValue& recorder)
{
	std::vector<std::pair<wxString, ibQueryExprPtr>> tuple;

	std::vector<wxString> fields;
	for (const ibColumnSlot& slot : slots)
		fields.push_back(slot.m_name);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(reg->GetRegisterRecorder()->GetQueryColumn(), reg->GetMetaData(), recorder, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	for (size_t i = 0; i < slots.size() && i < consts.size(); i++) {
		if (slots[i].m_role == ibColumnRole::Discriminator || !consts[i])
			continue;
		tuple.push_back({ slots[i].m_name, consts[i] });
	}
	return tuple;
}

// ⭐⭐ THE TWO ARMS OF ONE VIEW, named once. A turnovers view carries the stored rows AND the movements
// that came after them, so a reader must SAY which arm it wants: silence counts every movement of the
// current grain twice — once rolled into the total, once as itself — and the result looks plausible.
// A stored row has no recorder, so the recorder column IS the row's answer to "which arm am I".
inline ibQueryPredicatePtr ibRegStoredArm(const ibBackendQueryColumn* recorderCol)
{
	return recorderCol != nullptr ? ibQueryPredicate::Null(recorderCol, /*negated*/ false) : nullptr;
}

inline ibQueryPredicatePtr ibRegMovementArm(const ibBackendQueryColumn* recorderCol)
{
	return recorderCol != nullptr ? ibQueryPredicate::Null(recorderCol, /*negated*/ true) : nullptr;
}

// ⭐ THE SIDE OF A POSTING NAMES AN ACCOUNT — the one rule, for the totals it accumulates into and for every
// reading of the movements that stands on one side. An off-balance entry leaves its other side unnamed, and
// an unnamed account is an EMPTY REFERENCE (`emptyAccount`, the column's typed empty) — or, in a row written
// before the column existed, no value at all. Neither is an account, and neither takes a row: left in, the
// credit pass of the movements reported turnovers under an empty account in correspondence with 01
// (measured 2026-09-17). The totals trigger says the same in its own words (accountingRegisterMetadataSchema.cpp).
inline ibQueryPredicatePtr ibRegSideNamed(const ibBackendQueryColumn* account, const ibValue& emptyAccount)
{
	if (account == nullptr)
		return nullptr;
	return ibQueryPredicate::Compose(ibQueryPredicateKind::And,
		ibQueryPredicate::Leaf(ibQueryCondition{ account, ibQueryFilterOp::NotEqual, ibValue() }),
		ibQueryPredicate::Leaf(ibQueryCondition{ account, ibQueryFilterOp::NotEqual, emptyAccount }));
}

// Fill a read spec's cut. `TSpec` is ibMaterializeReadSpec — taken as a template parameter so this
// header needs no L2-2 include; every caller has one already.
template <typename TReg, typename TSpec>
inline void ibRegFillArmCut(TSpec& read, const TReg* reg,
                            const ibRegBound& upper, const ibRegBound& lower = ibRegBound())
{
	// 🛑 WHICH SIDE OF ITS EDGE A BOUNDARY STANDS ON IS SAID BEFORE ANYTHING RETURNS. It was said after the
	// "no cut needed" return below, so a boundary that needs no cut - a date exactly on a grain edge - never
	// said it, and the reader took it as INCLUDED: `period <= midnight` admits the stored row keyed by that
	// midnight, which is the WHOLE DAY that starts there. "The balance before 10.01, excluding" answered
	// 164 / 26 291.37 where the movements fold to 116 / 6 612 - the difference being every movement of 10.01,
	// to the cent (measured 2026-09-20). The note further down says an excluded upper edge takes that grain
	// wholly OUT; this is what makes it true.
	//
	// First in the function - above the two ways out for a view with one arm as well. Those cannot be taken
	// today (every register has a recorder); the day one can, it must not bring the defect back.
	read.m_toExcluding   = upper.m_excluding;
	read.m_fromExcluding = lower.m_excluding;

	if (reg == nullptr || !reg->HasRecorder() || reg->GetRegisterRecorder() == nullptr)
		return;   // this view has one arm; there is nothing to cut

	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(reg->GetRegisterRecorder()->GetQueryColumn());
	if (slots.empty())
		return;

	// A stored row has no recorder, so any of its fields is NULL there — the mark is the row's own
	// answer to "which arm am I", with no flag column invented for it.
	read.m_markColumn = slots.front().m_name;

	const ibTotalsPeriod grain = reg->GetTotalsPeriodUnit();

	// ⭐ A BOUNDARY REACHES BELOW THE GRAIN when it falls inside one, or when it names a document.
	// No date at all is answered by the stored rows whole, and the movement arm is left excluded. The
	// trigger keeps the CURRENT grain's row up to date, so "no boundary" is not stale: it is complete at
	// grain resolution.
	//
	// 🛑 AND A DATE ON A GRAIN EDGE STILL TOUCHES THE GRAIN THAT STARTS THERE. A stored row is keyed by its
	// grain's first instant, so an upper boundary that takes midnight in takes the whole day under that
	// key: the balance "at 26.09 00:00" counted a posting of 26.09 10:00 (measured 2026-09-17 on the
	// rehearsal ledger, 107 where 100 stood — 09:59:59 read 100). An INCLUDED upper edge and an EXCLUDED
	// lower edge are therefore partial grains too; an excluded upper edge and an included lower one take
	// that grain wholly out or wholly in, and stay with the stored rows.
	const auto reachesInside = [&](const ibRegBound& b, bool upperEnd, wxDateTime& moment) {
		if (b.m_date.GetType() != TYPE_DATE)
			return false;
		moment = b.m_date.GetDateTime();
		if (b.HasRecorder() || ibTruncateToPeriod(moment, grain) != moment)
			return true;
		return upperEnd ? !b.m_excluding : b.m_excluding;
	};

	wxDateTime upperMoment, lowerMoment;
	const bool cutUpper = reachesInside(upper, /*upperEnd*/ true, upperMoment);
	const bool cutLower = reachesInside(lower, /*upperEnd*/ false, lowerMoment);

	if (!cutUpper && !cutLower)
		return;

	// The stored arm ends where the upper boundary's grain begins. With no upper boundary at all it
	// still ends somewhere — at the LOWER boundary's grain — because the only reason the arm is cut at
	// all is that one end of the interval is partial.
	read.m_floor = ibValue(ibTruncateToPeriod(cutUpper ? upperMoment : lowerMoment, grain));
	if (cutUpper && upper.HasRecorder())
		read.m_boundaryTail = ibRegRecorderTuple(reg, slots, upper.m_recorder);

	// The stored arm begins at the first WHOLE grain at or after the lower boundary: the grain the
	// boundary falls INTO holds movements before it as well, so it cannot be taken as a row.
	if (cutLower) {
		read.m_headSplit = ibValue(ibNextPeriodStart(lowerMoment, grain));
		read.m_headGrain = ibValue(ibTruncateToPeriod(lowerMoment, grain));
		if (lower.HasRecorder())
			read.m_boundaryHead = ibRegRecorderTuple(reg, slots, lower.m_recorder);
	}
}

// ⭐⭐ THE CALENDAR OF AN INTERVAL — every period a periodised reading reports, whether or not anything moved.
//
// A register's stored surface has only the periods something moved in, and no window invents the others; a
// month that only carried a balance is still a row (Max, 2026-09-16: "if a month has no turnover but has
// balances, you must show them with zero turnovers"). So a periodised balance reading stands on this: the
// period the lower boundary falls into through the one the upper boundary falls into, stepped by the same two
// functions the grain cut and the RAM roads use, so "a month" is one thing everywhere.
//
// Empty when the interval has no two ends, or when it has more than `maxPeriods` periods — a calendar the SERVER
// is handed goes into the SQL as a row per period (ibRegCalendarRelation), and a daily one over ten years is the
// live path's. The live path fills its empty periods from the same calendar with no cap (0).
//
// 🛑 200, NOT 400: every period of the calendar is a SELECT of its own inside the statement, and Firebird refuses
// a statement with more than 256 contexts ("Too many Contexts of Relation/Procedure/Views") — the reads, the
// registers, the chart around it take their share. A daily balance over nine months (273 periods) refused on a
// base with a thousand movements (2026-09-17), where the RAM road answers the same rows.
inline constexpr size_t ibRegMaxServerCalendar = 200;
inline std::vector<wxDateTime> ibRegCalendarOf(const ibValue& from, const ibValue& to, ibTotalsPeriod unit,
                                               size_t maxPeriods = ibRegMaxServerCalendar)
{
	std::vector<wxDateTime> periods;
	if (from.GetType() != TYPE_DATE || to.GetType() != TYPE_DATE)
		return periods;
	const wxDateTime last = to.GetDateTime();
	for (wxDateTime period = ibTruncateToPeriod(from.GetDateTime(), unit); period.IsValid() && !period.IsLaterThan(last);) {
		periods.push_back(period);
		if (maxPeriods != 0 && periods.size() > maxPeriods)
			return {};
		const wxDateTime next = ibNextPeriodStart(period, unit);
		if (!next.IsValid() || !next.IsLaterThan(period))
			break;   // a unit that does not advance would loop forever
		period = next;
	}
	return periods;
}

// The calendar as a RELATION — one row per period under `periodField`, laid one under the other; a one-row
// select needs no table (the dialect supplies its own, RDB$DATABASE on Firebird). Joined to every key of a
// reading, it is the grid a running balance walks.
inline ibQueryRelPtr ibRegCalendarRelation(const std::vector<wxDateTime>& periods, const wxString& periodField)
{
	ibQueryRelPtr calendar;
	for (const wxDateTime& start : periods) {
		const ibQueryRelPtr row = ibProject(nullptr, { { ibCast(ibConst(ibValue(start)), ibTypeDate()), periodField } });
		calendar = calendar ? ibUnionAll(calendar, row) : row;
	}
	return calendar;
}

// A figure's typed zero — what a side that stores nothing answers, and what a sum of nothing is. Typed, because
// the arms of a UNION and the branches of a COALESCE must agree on what the column is.
inline ibQueryExprPtr ibRegTypedZero()
{
	return ibCast(ibConst(ibValue(0.0)), ibTypeNumber(18, 6));
}

// One balance a periodised reading runs along the calendar: the period's net movement (a column of the
// turnover read), the balance entering the interval (a column of the opening read), and the two names the
// grid publishes the period's opening and closing under.
struct ibRegRunningFigure {
	wxString m_turnover;
	wxString m_opening;
	wxString m_openingOut;
	wxString m_closingOut;
};

// ⭐⭐ THE GRID OF A PERIODISED BALANCE READING — every key, every period of the calendar, and the balances
// run along them. Shared by every register that reports balances per period (the accounting one per side,
// the accumulation one per resource); what a register does with the rows afterwards — fold by the account's
// type, prune the all-zero ones — stays its own.
//
//   opening    the read of each key's balance entering the interval, grouped over EVERY row up to the end and
//              not pruned, so it is also the set of every key that holds a balance or moves in the interval;
//   turnovers  the read of each key's movement per calendar period, inside the interval;
//   grid       two arms laid one under the other and summed by key and period — opening × calendar (every key,
//              every period, no movement) and the turnovers (the periods that moved, no entering balance).
//
// Published: every key, the period, each `passThrough` column of the turnover read (zero where the period did
// not move), and for each running figure its opening and closing — closing = the entering balance plus the
// period's movement summed along the key's periods so far (peers included), opening = closing − the period's.
//
// ⭐⭐ SUMMED, NOT JOINED. The turnovers were LEFT JOINed to the grid on the period and the key, each key field
// matched NULL-safely as `a = b OR (a IS NULL AND b IS NULL)`. An OR in a join condition is one no engine can
// hash or look up by index: Firebird re-ran the whole turnover read — sorts, windows, both surfaces — for every
// row of the grid, and a year of month-by-month balances over 2 625 postings took 313 s inside the engine with
// one core spinning and nothing in the journal (measured 2026-09-17). A GROUP BY over the two arms asks the same
// question with one sort, and it matches NULL keys by itself: a group key compares NULLs as one value.
//
// 🛑 The key set is the opening read and not a DISTINCT over a union of both reads: rendered on Firebird, that
// DISTINCT merged into the union's first arm and the statement failed at BLR level (measured 2026-09-16).
inline ibQueryRelPtr ibRegRunningGrid(const ibQueryRelPtr& openRead, const ibQueryRelPtr& turnRead,
                                      const std::vector<wxDateTime>& periods, const wxString& periodField,
                                      const std::vector<wxString>& keyNames, const std::vector<wxString>& passThrough,
                                      const std::vector<ibRegRunningFigure>& running, const wxString& alias)
{
	if (openRead == nullptr || turnRead == nullptr || periods.empty())
		return nullptr;
	const wxString aO = alias + wxT("_po"), aT = alias + wxT("_pt"), aC = alias + wxT("_pc");
	const wxString aU = alias + wxT("_pu"), aS = alias + wxT("_ps");
	const auto orZero = [](const ibQueryExprPtr& e) {
		return ibCast(ibFunc(wxT("COALESCE"), { e, ibRegTypedZero() }), ibTypeNumber(18, 6));
	};

	// The figures each arm carries: what the turnover read moves (the pass-through columns and each running
	// figure's movement, once each) and what the opening read holds. An arm answers the other's with a zero.
	std::vector<wxString> moved, held;
	const auto once = [](std::vector<wxString>& names, const wxString& name) {
		if (std::find(names.begin(), names.end(), name) == names.end())
			names.push_back(name);
	};
	for (const wxString& name : passThrough)
		once(moved, name);
	for (const ibRegRunningFigure& figure : running) {
		once(moved, figure.m_turnover);
		once(held, figure.m_opening);
	}

	// The period is cast on both arms: the calendar's constant and the read's truncated column meet in one
	// UNION column, and a union takes one type.
	const auto arm = [&](const wxString& from, bool balances, const ibQueryExprPtr& period) {
		std::vector<ibQueryProjItem> items;
		for (const wxString& name : keyNames)
			items.push_back({ ibCol(from, name), name });
		items.push_back({ ibCast(period, ibTypeDate()), periodField });
		for (const wxString& name : moved)
			items.push_back({ balances ? ibRegTypedZero() : orZero(ibCol(from, name)), name });
		for (const wxString& name : held)
			items.push_back({ balances ? orZero(ibCol(from, name)) : ibRegTypedZero(), name });
		return items;
	};

	// "Every row with every row" — said over a column, because two bare parameters compared (`? = ?`) have no
	// type for the engine to prepare (Firebird: -804 Data type unknown). A calendar row always has its period.
	const ibQueryExprPtr always = ibBinOp(ibQueryBinOp::Eq, ibCol(aC, periodField), ibCol(aC, periodField));
	const ibQueryRelPtr everyPeriod = ibProject(
		ibJoin(ibSubquery(openRead, aO), ibSubquery(ibRegCalendarRelation(periods, periodField), aC), always, ibQueryJoinType::Inner),
		arm(aO, /*balances*/ true, ibCol(aC, periodField)));
	const ibQueryRelPtr moves = ibProject(ibSubquery(turnRead, aT), arm(aT, /*balances*/ false, ibCol(aT, periodField)));

	// Summed by key and period: one row per key per period, its movement if it moved and its entering balance.
	std::vector<ibQueryProjItem> sums;
	std::vector<ibQueryExprPtr> groupKeys;
	for (const wxString& name : keyNames) {
		sums.push_back({ ibCol(aU, name), name });
		groupKeys.push_back(ibCol(aU, name));
	}
	sums.push_back({ ibCol(aU, periodField), periodField });
	groupKeys.push_back(ibCol(aU, periodField));
	for (const wxString& name : moved)
		sums.push_back({ ibCast(ibFunc(wxT("SUM"), { ibCol(aU, name) }), ibTypeNumber(18, 6)), name });
	for (const wxString& name : held)
		sums.push_back({ ibCast(ibFunc(wxT("SUM"), { ibCol(aU, name) }), ibTypeNumber(18, 6)), name });
	const ibQueryRelPtr summed = ibAggregate(ibSubquery(ibUnionAll(everyPeriod, moves), aU), std::move(sums), std::move(groupKeys));

	std::vector<ibQueryExprPtr> partition;
	std::vector<ibQueryProjItem> projection;
	for (const wxString& name : keyNames) {
		partition.push_back(ibCol(aS, name));
		projection.push_back({ ibCol(aS, name), name });
	}
	projection.push_back({ ibCol(aS, periodField), periodField });
	for (const wxString& name : passThrough)
		projection.push_back({ ibCol(aS, name), name });
	for (const ibRegRunningFigure& figure : running) {
		const ibQueryExprPtr turn = ibCol(aS, figure.m_turnover);
		const ibQueryExprPtr closing = ibBinOp(ibQueryBinOp::Add, ibCol(aS, figure.m_opening),
			ibWindowed(ibFunc(wxT("SUM"), { turn }),
				ibQueryWindow{ partition, { ibQuerySortKey{ ibCol(aS, periodField), ibQuerySortDir::Asc } }, ibQueryFrame::RangeThroughPeers }));
		projection.push_back({ ibBinOp(ibQueryBinOp::Sub, closing, turn), figure.m_openingOut });
		projection.push_back({ closing, figure.m_closingOut });
	}
	return ibProject(ibSubquery(summed, aS), std::move(projection));
}

// --- register-side convenience over the column-layout tier ---------------------------------------
// An attribute IS an ibBackendQueryColumn; the tier functions take (col, metaData). These thin
// wrappers derive the metadata from the attribute so register lowering reads cleanly — the SQL-field
// machinery lives in the tier, NOT on the attribute. The structured ibSQLField is GONE: a register
// builds the comma-joined field list with ibRegFieldList, or picks the _TYPE tag / first value field.
inline wxString       ibRegFieldList (const ibValueMetaObjectAttributeBase* a, const wxString& aggr = wxEmptyString) { return ColumnFieldList(a->GetQueryColumn(), aggr); }
inline wxString       ibRegComposite (const ibValueMetaObjectAttributeBase* a, const wxString& cmp = wxT("=")) { return ColumnComparePredicate(a->GetQueryColumn(), cmp); }

// All physical fields of an attribute (the TYPE tag + per-type fields; a reference
// expands to _RTRef + _RRRef), in the order SetValueAttribute binds them.
inline std::vector<wxString> ibRegFieldsOf(const ibValueMetaObjectAttributeBase* a)
{
	return ColumnFieldNames(a->GetQueryColumn());
}

// The _TYPE discriminator field name (the first physical field of a composite column).
inline wxString ibRegTypeField(const ibValueMetaObjectAttributeBase* a)
{
	const std::vector<wxString> fields = ColumnFieldNames(a->GetQueryColumn());
	return fields.empty() ? wxString() : fields[0];   // [0] is always the _TYPE tag
}

// The first VALUE field of an attribute (the field after the TYPE tag) — the column a
// register resource / record-type aggregate operates on (res_N, recordType_N / _E).
inline wxString ibRegValueField(const ibValueMetaObjectAttributeBase* a)
{
	const std::vector<wxString> fields = ColumnFieldNames(a->GetQueryColumn());
	return fields.size() > 1 ? fields[1] : wxString();   // [0] is the _TYPE tag; [1] is the first value field
}

// ⭐ THE SAME QUESTIONS ASKED OF A COLUMN, for a caller that holds a column rather than an attribute. The
// bodies are the attribute's own, minus the one hop it makes first.
// The same signature a surface is cached by, taken from a COLUMN — see ibRegSignAttribute below, whose
// body this is with the one hop removed.
inline void ibRegSignColumn(wxString& signature, const ibBackendQueryColumn* c)
{
	if (c == nullptr)
		return;
	signature += c->GetName() + wxT(":");
	const ibTypeDescription& type = c->GetTypeDesc();
	for (unsigned int i = 0; i < type.GetClsidCount(); ++i)
		signature += wxString::Format(wxT("%llu,"), static_cast<unsigned long long>(type.GetByIdx(i)));
	signature += wxT(";");
}

inline wxString ibRegValueField(const ibBackendQueryColumn* c)
{
	const std::vector<wxString> fields = ColumnFieldNames(c);
	return fields.size() > 1 ? fields[1] : wxString();
}

// "q.f1, q.f2, …" — the attribute's field list qualified by a table alias (the SELECT/GROUP BY form
// used in the dr/cr join queries). Replaces the hand-rolled qualified walks over ibSQLField.
inline wxString ibRegQualifiedList(const ibValueMetaObjectAttributeBase* a, const wxString& q)
{
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a))
		out += (out.empty() ? wxString() : wxString(",")) + q + wxT(".") + f;
	return out;
}

// "q.f1 AS <alias><suffix>, …" — qualified + aliased (dr.fld_N AS AccountDr_N). The suffix is the
// field's tail past the base name (_TYPE / _N / _RTRef …), so the alias mirrors the physical layout.
inline wxString ibRegAliasedList(const ibValueMetaObjectAttributeBase* a, const wxString& q, const wxString& alias)
{
	const wxString base = a->GetPhysicalName();
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a)) {
		const wxString suffix = f.length() > base.length() ? f.Mid(base.length()) : wxString();
		out += (out.empty() ? wxString() : wxString(", ")) + q + wxT(".") + f + wxT(" AS ") + alias + suffix;
	}
	return out;
}

// "lq.f1 = rq.f1 AND …" — an all-fields equality join between two qualifiers (the dr/cr recorder join).
inline wxString ibRegJoinEq(const ibValueMetaObjectAttributeBase* a, const wxString& lq, const wxString& rq)
{
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a))
		out += (out.empty() ? wxString() : wxString(" AND ")) + lq + wxT(".") + f + wxT(" = ") + rq + wxT(".") + f;
	return out;
}

// "q.f1 = ? AND …" — an all-fields equality against bound params (a qualified composite filter).
inline wxString ibRegQualifiedEqParams(const ibValueMetaObjectAttributeBase* a, const wxString& q)
{
	wxString out;
	for (const wxString& f : ibRegFieldsOf(a))
		out += (out.empty() ? wxString() : wxString(" AND ")) + q + wxT(".") + f + wxT(" = ?");
	return out;
}

// ⭐⭐ WHAT A FILTER IS: a STRUCTURE that converts into a PREDICATE the query engine understands.
//
// That is the whole definition, and it settles every question the old code kept re-asking — what a
// filter may express, who unwraps it, which register does it differently. Nobody does it
// differently: there is one converter, and past it there are no structures, only conditions.
//
// ⭐ AND THE QUERY TEXT ALREADY DOES THIS, IMPLICITLY. `WHERE Warehouse = &Warehouse` is parsed into
// the same predicate; nobody had to write a converter for it because parsing IS the conversion. So
// the predicate has TWO producers — the parser and this function — and one form. That is why the
// structure had to be converted rather than carried: carried, it would have been a second currency
// that only one of the two entrances could spend.
// A script hands `New Structure("Warehouse", W)` — a runtime value, name to value. A query hands a
// condition. They were two different things all the way down: the structure was unwrapped BY HAND in
// five places, each rebuilding the same map, each able to drift from the others, and each able to
// express one thing only — equality on a dimension.
//
// So the sugar is converted ONCE, into what a query would have written: `Dimension = <value>`,
// AND-folded, the values riding as bound comparison values exactly as a parameter does. Everything
// downstream then sees a predicate and stops caring which door the reader came through.
//
// ⭐ WHAT A FILTER MAY NAME IS DECIDED BY WHAT THE READING DOES TO THE RECORDS — not by the register.
// A balance, a turnover, a totals row FOLD: a resource there is the folded value, and a condition over
// a fold belongs to the result rather than to an argument of the source — the same rule
// FillConditionExplorer offers to the window, so the two cannot disagree. Such a reading is filtered
// by its DIMENSIONS. A reading of the records as they were written folds nothing: every column of a
// record is still a column there, and the name is resolved exactly as a query's `WHERE Recorder = …`
// resolves it — by the register's own source (ResolveColumnByName), so the two producers of a
// predicate keep one vocabulary.
//
// 🛑 IT KNEW ONLY THE FOLDED ANSWER, AND WHAT IT COULD NOT PLACE IT DROPPED WITHOUT A WORD. Measured
// 2026-09-10 on a payroll base: `GetBase(Accruals, New Structure("Recorder", doc))` — the one condition
// a calculation always has, "the base of MY records" — named no dimension, so it came back as no
// filter at all and every record of every document was scored. A dropped condition is a wrong answer
// that looks right; a key that names nothing is now refused by its name.
enum class ibRegFilterOver {
	Folded,    // balance / turnover / totals — dimensions only
	Records,   // the records themselves — any column the register's source carries
};

template <typename TRegister>
inline ibQueryPredicatePtr ibRegFilterPredicate(const TRegister* reg, const ibValue& filter,
	const ibRegFilterOver over = ibRegFilterOver::Folded)
{
	ibValueStructure* structure = nullptr;
	if (reg == nullptr || !filter.ConvertToValue(structure) || structure == nullptr)
		return nullptr;   // nothing was asked for — which is not an empty filter, but no filter at all

	const ibBackendQueryable* const source = reg->GetQueryable();
	ibQueryPredicatePtr folded;
	for (long key = 0; key < structure->GetNProps(); ++key) {
		const wxString name = structure->GetPropName(key);

		const ibBackendQueryColumn* col = nullptr;
		if (over == ibRegFilterOver::Records) {
			col = source != nullptr ? source->ResolveColumnByName(name) : nullptr;
		}
		else {
			for (const auto dimension : reg->GetDimensionArrayObject()) {
				if (dimension != nullptr && stringUtils::CompareString(name, dimension->GetName())) {
					col = dimension->GetQueryColumn();
					break;
				}
			}
		}
		if (col == nullptr) {
			if (over == ibRegFilterOver::Records)
				ibBackendCoreException::Error(_("filter names '%s', which is not a field of '%s'"), name, reg->GetName());
			ibBackendCoreException::Error(
				_("filter names '%s', which is not a dimension of '%s' - this reading folds the records and is filtered by dimensions only"),
				name, reg->GetName());
		}

		ibValue value;
		structure->GetPropVal(key, value);

		ibQueryCondition leaf;
		leaf.m_col   = col;
		leaf.m_op    = ibQueryFilterOp::Equal;
		leaf.m_value = value;

		ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
		folded = folded ? ibQueryPredicate::Compose(ibQueryPredicateKind::And, folded, one) : one;
	}
	return folded;
}

// A record set's key value, as a condition — but a period kept to a unit coarser than the second is the whole of it,
// from its start up to the next one's: a record written earlier with its time (23:59 of a day) is that day's (Max,
// 2026-09-15).
template <typename TRegister>
inline void ibRegWhereKeyValue(ibDataQueryBuilder& q, const TRegister* reg,
	const ibValueMetaObjectAttributeBase* object, const ibValue& value)
{
	const ibTotalsPeriod unit = reg->GetPeriodicityUnit();
	if (unit != ibTotalsPeriod::Second && reg->IsRegisterPeriod(object->GetMetaID())
		&& value.GetType() == TYPE_DATE && value.GetDateTime().IsValid()) {
		const wxDateTime start = ibTruncateToPeriod(value.GetDateTime(), unit);
		q.WhereCompare(object->GetQueryColumn(), ibQueryFilterOp::GreaterEqual, ibValue(start));
		q.WhereCompare(object->GetQueryColumn(), ibQueryFilterOp::Less, ibValue(ibNextPeriodStart(start, unit)));
	}
	else
		q.Where(object->GetQueryColumn(), ibQueryFilterOp::Equal, value);
}

// The flat AND-leaves of a predicate, in order.
//
// ⚠ This is what the hand-built L2 aggregates can apply TODAY, and it is deliberately narrow: an OR,
// a range, a NOT are exactly the shapes those aggregates cannot express — and exactly the reason the
// readings are moving onto the door, where a predicate rides natively. Until then a richer condition
// is not silently half-applied: it simply has no leaves at this level and the caller sees that.
// It yields the leaf's COLUMN as it stands — nothing narrows it back to a metaobject, because
// nothing below needs one: ibRegCompositeIR spreads a column by asking the column.
inline void ibRegFlatLeaves(const ibQueryPredicatePtr& predicate,
                            std::vector<std::pair<const ibBackendQueryColumn*, ibValue>>& out)
{
	if (!predicate)
		return;
	if (predicate->m_kind == ibQueryPredicateKind::Leaf) {
		if (predicate->m_leaf.m_col != nullptr && predicate->m_leaf.m_op == ibQueryFilterOp::Equal)
			out.push_back({ predicate->m_leaf.m_col, predicate->m_leaf.m_value });
		return;
	}
	if (predicate->m_kind != ibQueryPredicateKind::And)
		return;
	for (const ibQueryPredicatePtr& child : predicate->m_children)
		ibRegFlatLeaves(child, out);
}

// A composite predicate (column <op> value) as IR, each per-field value bound as a Const —
// the structured form of the compare predicate. The TYPE tag (and a reference's _RTRef) always
// compare '='; the value fields use `op`. `qualifier` optionally qualifies the columns (table.col)
// for a join.
//
// ⭐ NAME THE COLUMN, GET ITS WHOLE FIELD SET. The spread is the COLUMN's own — `ColumnFieldNames`
// asks it, the codec writes through it — so a caller says "this resource" and never lists fields.
//
// ⚠ IT TAKES A COLUMN, NOT AN ATTRIBUTE, and that is a removal rather than a widening. The attribute
// was required for exactly one thing: to hand over `GetMetaData()`. Everything else already ran on
// the column face. Demanding the metaobject made every caller holding a plain column narrow back to
// one — a cast, which is the model saying it lost something on the way in. Now the metadata is
// passed, the column is enough, and nothing casts.
inline ibQueryExprPtr ibRegCompositeIR(const ibBackendQueryColumn* a, const ibMetaData* metaData,
                                       const ibValue& v,
                                       ibQueryBinOp op, const wxString& qualifier = wxEmptyString)
{
	// ⭐ THE SLOTS, NOT JUST THE NAMES. Which fields TAG the value (_TYPE, a reference's _RTRef) and
	// which CARRY it is a question about the field's ROLE, and the layout tier answers it — this used
	// to guess from position (`i == 0`) and spelling (`EndsWith("_RTRef")`), i.e. re-derive the
	// layout here, where a change to the lettering would never arrive.
	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(a);

	std::vector<wxString> fields;
	fields.reserve(slots.size());
	for (const ibColumnSlot& slot : slots)
		fields.push_back(slot.m_name);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(a, metaData, v, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	ibQueryExprPtr pred;
	for (size_t i = 0; i < slots.size(); ++i) {
		ibQueryExprPtr c = (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue());
		const bool tag = !ibIsValueRole(slots[i].m_role);
		ibQueryExprPtr col = qualifier.empty() ? ibCol(fields[i]) : ibCol(qualifier, fields[i]);
		ibQueryExprPtr term = ibBinOp(tag ? ibQueryBinOp::Eq : op, col, c);
		pred = pred ? ibBinOp(ibQueryBinOp::And, pred, term) : term;
	}
	return pred;
}

// The physical field of a column that plays `role` — empty when the column lays out no such field. The
// question a join asks of a column it compares by one part only (a reference's id, a date's value).
inline wxString ibRegFieldOfRole(const ibBackendQueryColumn* col, ibColumnRole role)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(col))
		if (slot.m_role == role)
			return slot.m_name;
	return wxString();
}

// The same VALUE in two columns of two qualified tables: every field both columns lay out, paired by role
// and compared EQUAL. The ROW twin of ibRegCompositeIR, which compares a column to a value.
//
// ⚠ PLAIN EQUALITY, AND THE INDEX IS WHY. An empty value is a typed empty in this storage, never SQL NULL
// (databaseLayer.h, queryRewrite.h lean on the same fact), so `a = b` already matches two empties. It
// was written `a = b OR (a IS NULL AND b IS NULL)` first — and an OR in each field is a condition no
// compound index can ride: a calculation register's lookup index (employee, type) was there and the base
// read walked the pieces anyway (MEASURED 2026-09-10 at 1000 employees, debug: 4 s for one statement).
inline ibQueryExprPtr ibRegSameValueIR(const ibBackendQueryColumn* a, const wxString& qa,
	const ibBackendQueryColumn* b, const wxString& qb)
{
	ibQueryExprPtr pred;
	const std::vector<ibColumnSlot> right = DescribeColumnLayout(b);
	for (const ibColumnSlot& x : DescribeColumnLayout(a)) {
		for (const ibColumnSlot& y : right) {
			if (x.m_role != y.m_role)
				continue;
			const ibQueryExprPtr term = ibBinOp(ibQueryBinOp::Eq, ibCol(qa, x.m_name), ibCol(qb, y.m_name));
			pred = pred ? ibBinOp(ibQueryBinOp::And, pred, term) : term;
			break;
		}
	}
	return pred;
}

// The condition a query wrote into slot `slot` of a table's parameters, as the lowering handed it over
// (ibQueryableSourceDescriptor::CreateQueryable) — null when nothing was written there.
inline ibQueryPredicatePtr ibRegConsumedCondition(const std::vector<ibQueryPredicatePtr>& conditions, int slot)
{
	return slot >= 0 && static_cast<size_t>(slot) < conditions.size() ? conditions[slot] : nullptr;
}

// Is this column one the rows of a register are KEPT by — a dimension? What a reading's condition selects by
// before the fold (ibRegConditionOn), asked by the column's identity.
template <typename TRegister>
std::function<bool(const ibBackendQueryColumn*)> ibRegSelectsByDimensions(const TRegister* reg)
{
	return [reg](const ibBackendQueryColumn* column) {
		if (reg == nullptr || column == nullptr)
			return false;
		for (const auto dimension : reg->GetDimensionArrayObject())
			if (dimension != nullptr && dimension->GetMetaID() == column->GetColumnId())
				return true;
		return false;
	};
}

// Does every field the condition names satisfy `names`? A computed side answers no — it is not a field.
inline bool ibRegConditionOnlyNames(const ibQueryPredicatePtr& condition,
                                    const std::function<bool(const ibBackendQueryColumn*)>& names)
{
	if (!condition)
		return true;
	switch (condition->m_kind) {
	case ibQueryPredicateKind::Leaf:
		if (condition->m_leaf.m_expr || condition->m_leaf.m_semiJoin)
			return false;
		return names(condition->m_leaf.m_path.empty() ? condition->m_leaf.m_col : condition->m_leaf.m_path.front());
	case ibQueryPredicateKind::IsNull:
	case ibQueryPredicateKind::RefType:
		if (condition->m_expr)
			return false;
		return names(condition->m_path.size() > 1 ? condition->m_path.front() : condition->m_col);
	default:
		for (const ibQueryPredicatePtr& child : condition->m_children)
			if (!ibRegConditionOnlyNames(child, names))
				return false;
		return true;
	}
}

// Two conditions that both have to hold; either may be absent.
inline ibQueryPredicatePtr ibRegBothConditions(const ibQueryPredicatePtr& a, const ibQueryPredicatePtr& b)
{
	if (!a) return b;
	if (!b) return a;
	return ibQueryPredicate::Compose(ibQueryPredicateKind::And, a, b);
}

// A column a reading's condition named and the rows cannot be selected by. RAISES: dropped, the leaf would
// widen the answer — more rows than were asked for, and nothing to say so.
inline void ibRegRefuseConditionColumn(const ibBackendQueryColumn* named)
{
	ibBackendCoreException::Error(
		_("'%s' cannot select the rows of this reading: a condition inside a virtual table narrows what is "
		  "folded, so it may name what the rows are kept by - the dimensions, and on an accounting register "
		  "the accounts and their breakdown. A figure is known only after the fold and a period is the "
		  "reading's own boundaries - put those into the query's WHERE"),
		named != nullptr ? named->GetName() : wxString());
}

// ⭐⭐ A READING'S CONDITION, FOUND AGAIN ON THE SURFACE IT READS.
//
// A condition written into a virtual table's parameters is a SELECTION made before anything is folded — the
// table arrives already narrowed, which is the whole point of writing it there rather than into the WHERE
// around the table (Max, 2026-09-17). So the whole tree the author wrote goes down into the read: NOT, OR, IN,
// IS NULL, REFS, and a walk through a reference, which stays a walk and becomes a correlated EXISTS — a filter
// never multiplies a row it keeps.
//
// It is written against the reading's CONDITION SCOPE (GetConditionScope), and read on whichever surface the
// reading stands on — the totals view, the movements, a slice. Found there BY NAME: these registers publish
// their fields under the fields' own names. (The accounting register finds its own by identity and per side —
// `Account` is a different column on each — see ConditionOnPass.) `selects` says which columns the rows can
// be selected by before the fold; any other is refused.
inline ibQueryPredicatePtr ibRegConditionOn(const ibBackendQueryable* source, const ibQueryPredicatePtr& condition,
                                            const std::function<bool(const ibBackendQueryColumn*)>& selects);

inline ibQueryColumnExprPtr ibRegConditionExprOn(const ibBackendQueryable* source, const ibQueryColumnExprPtr& expr,
                                                 const std::function<bool(const ibBackendQueryColumn*)>& selects)
{
	if (!expr)
		return expr;
	auto here = std::make_shared<ibQueryColumnExpr>(*expr);
	if (here->m_kind == ibQueryColumnExprKind::Column && here->m_col != nullptr) {
		const ibBackendQueryColumn* found = selects(here->m_col) ? source->ResolveColumnByName(here->m_col->GetName()) : nullptr;
		if (found == nullptr)
			ibRegRefuseConditionColumn(here->m_col);
		here->m_col = found;
	}
	const auto again = [&](const ibQueryColumnExprPtr& e) { return ibRegConditionExprOn(source, e, selects); };
	here->m_lhs  = again(here->m_lhs);
	here->m_rhs  = again(here->m_rhs);
	here->m_else = again(here->m_else);
	for (ibQueryColumnExprPtr& arg : here->m_args)
		arg = again(arg);
	for (auto& branch : here->m_cases) {
		branch.first  = ibRegConditionOn(source, branch.first, selects);
		branch.second = again(branch.second);
	}
	return here;
}

inline ibQueryPredicatePtr ibRegConditionOn(const ibBackendQueryable* source, const ibQueryPredicatePtr& condition,
                                            const std::function<bool(const ibBackendQueryColumn*)>& selects)
{
	if (!condition || source == nullptr)
		return nullptr;

	const auto find = [&](const ibBackendQueryColumn* named) {
		const ibBackendQueryColumn* here = named != nullptr && selects(named)
			? source->ResolveColumnByName(named->GetName()) : nullptr;
		if (here == nullptr)
			ibRegRefuseConditionColumn(named);
		return here;
	};

	auto here = std::make_shared<ibQueryPredicate>(*condition);
	for (ibQueryPredicatePtr& child : here->m_children)
		child = ibRegConditionOn(source, child, selects);

	switch (here->m_kind) {
	case ibQueryPredicateKind::Leaf: {
		ibQueryCondition& leaf = here->m_leaf;
		if (leaf.m_semiJoin)
			ibBackendCoreException::Error(_("a reading's condition cannot hold a semi-join - write it as IN (SELECT ...)"));
		if (leaf.m_expr) {
			leaf.m_expr      = ibRegConditionExprOn(source, leaf.m_expr, selects);
			leaf.m_valueExpr = ibRegConditionExprOn(source, leaf.m_valueExpr, selects);
			break;
		}
		const bool walks = !leaf.m_path.empty();
		const ibBackendQueryColumn* column = find(walks ? leaf.m_path.front() : leaf.m_col);

		// `IN HIERARCHY` is resolved into the subtree it stands for, read through the column the walk ends at —
		// nothing below the lowering folds by the word, and a provider must never see it.
		if (leaf.m_unfold != ibQueryDimUnfold::Elements) {
			const ibBackendQueryable* owner = source;
			const ibBackendQueryColumn* lhs = column;
			for (size_t hop = 1; lhs != nullptr && walks && hop < leaf.m_path.size(); ++hop) {
				owner = owner->GetProvider().ResolveReferenceTarget(owner, lhs);
				lhs   = owner != nullptr ? owner->ResolveColumnByName(leaf.m_path[hop]->GetName()) : nullptr;
			}
			if (lhs == nullptr)
				ibRegRefuseConditionColumn(leaf.m_col);
			leaf.m_values = ibQueryHierarchyScope(owner, lhs, leaf.m_values, leaf.m_unfold).Accepted();
			leaf.m_unfold = ibQueryDimUnfold::Elements;
			leaf.m_op     = ibQueryFilterOp::In;
		}

		if (walks) {
			leaf.m_path.front() = column;
			leaf.m_asExists     = true;
		}
		else {
			leaf.m_col = column;
		}
		break;
	}
	case ibQueryPredicateKind::IsNull:
	case ibQueryPredicateKind::RefType:
		if (here->m_expr)
			ibBackendCoreException::Error(_("a reading's condition tests a field, not a computed value"));
		if (here->m_path.size() > 1)
			here->m_path.front() = find(here->m_path.front());
		else
			here->m_col = find(here->m_col);
		break;
	default:
		break;
	}
	return here;
}

// ⭐⭐ THE STORAGE NAME IS ASKED FOR, NEVER SPELLED.
//
// A view column carries BOTH names — `Resource1Turnover` for a query to write, `Resource1_Turnover`
// for the table to keep — and the pair is built in ONE place, where the view is declared. Typing the
// storage spelling out again at a reading is a second writing of one name, and this one drifts
// SILENTLY: a read spec naming a column the view does not have returns NULLs, not an error. So the
// reading asks the surface it stands on, and neither side spells the other's name.
inline wxString ibRegPhysicalOf(const ibBackendQueryable* view, const wxString& logical)
{
	const ibBackendQueryColumn* col = view != nullptr ? view->ResolveColumnByName(logical) : nullptr;
	return col != nullptr ? col->GetPhysicalName() : wxString();
}

// ⭐ IS THERE A DATABASE TO READ AT ALL — one sentence, asked by every register's manager.
//
// It stood in six places in four spellings: a named function in one register, a bare condition in
// another, and a two-branch `if / else if` raising the same message twice in the third. A check
// written four ways is four things to keep true; this is the same question every time, and its answer
// is the same refusal.
//
// It raises rather than returning: a manager call that reaches this has been asked for DATA, and an
// empty table would be indistinguishable from a register that genuinely holds none.
inline void ibRequireOpenBase()
{
	if (ses_query == nullptr || !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
}

// ==================================================================================================
//  THE CALL — reading a virtual table's arguments, and DECLARING them
// ==================================================================================================
//
// Every register vends virtual tables, and every one of them asks the same two things of the same
// call: what did the caller pass in slot N, and what does this table declare it takes. Written per
// register, the pair drifted immediately — the *(period, condition)* declaration existed in FOUR
// spellings and the argument reader in three, and the one that counted commas instead of asking read
// the CONDITION out of the periodicity's slot. These live here so a register states its ORDER (its
// own business, and genuinely different per table) and nothing else.

// One argument slot: present, in range, non-null — or empty. Empty is a legitimate answer for every
// optional slot, which is why this returns a value rather than refusing.
inline ibValue ibRegArg(ibValue** paParams, long lSizeArray, int slot)
{
	return (paParams != nullptr && slot >= 0 && slot < lSizeArray && paParams[slot] != nullptr)
		? *paParams[slot] : ibValue();
}

// The same, over an already-collected argument list (the source explorer is handed one).
inline ibValue ibRegArg(const std::vector<ibValue>& args, int slot)
{
	return (slot >= 0 && static_cast<size_t>(slot) < args.size()) ? args[slot] : ibValue();
}

// ⚠ THE NAMED SLOT, NOT A COUNTED ONE. This once read `args[2]` — the CONDITION's slot, not the
// periodicity's — so the field tree offered its columns based on whatever the filter happened to be.
inline ibRegFold ibRegisterFoldOfArgs(const std::vector<ibValue>& args, int slot)
{
	return ibReadRegisterFold(ibRegArg(args, slot));
}

// THE INTERVAL A TURNOVER IS COUNTED OVER — the two ends said separately, because they are two
// different moments and a reader must be able to hand a parameter to each.
//
// ⚠ IT TAKES THE TYPE, NOT THE REGISTER. The type is the register's OWN period type — asked of the
// attribute rather than written down as "a date" — and asking for it at the callsite is what lets
// every register share this without the helper learning what a register is.
inline void ibFillRegisterIntervalParameters(const ibTypeDescription& periodType,
	std::vector<ibQuerySourceParameter>& out)
{
	ibQuerySourceParameter begin;
	begin.m_name = wxT("BeginOfPeriod");
	begin.m_description = _("The first moment counted, inclusive. Left out, the reading starts at the "
	                 "register's first movement - not at some default date.");
	begin.m_type = periodType;
	out.push_back(begin);

	ibQuerySourceParameter end;
	end.m_name = wxT("EndOfPeriod");
	end.m_description = _("The last moment counted, inclusive - and it may name a DOCUMENT rather than a "
	               "date, which is how \"the turnovers up to this receipt\" is asked. Left out, "
	               "the reading runs to the last movement there is.");
	end.m_type = periodType;
	out.push_back(end);
}

// AND THE CONDITION, always a condition slot — the same shape every virtual table of every register has.
//
// ⭐⭐ CONSUMED BY THE SOURCE, EVERY TIME. It was sugar — ANDed into the WHERE around the table — while its own
// description promised "applied inside the reading", and the two are not the same query: around the table the
// whole register is read and folded and then most of it thrown away. Inside, the reading selects first and
// folds only what was asked for, which is the reason a virtual table has parameters at all (Max, 2026-09-17).
// So each register takes it (GetConditionScope + the four-argument CreateQueryable) and lowers it into every
// read it makes (ibRegConditionOn; the accounting register's ConditionOnPass).
inline void ibAppendRegisterConditionParameter(std::vector<ibQuerySourceParameter>& out)
{
	ibQuerySourceParameter condition;
	condition.m_name      = wxT("Condition");
	condition.m_description      = _("A condition on the DIMENSIONS, applied inside the reading - so it "
	                          "selects the rows BEFORE they are folded, and the table arrives already "
	                          "narrowed. Written as a predicate (Warehouse = &Warehouse), and any predicate "
	                          "is taken: NOT, OR, IN, a walk through a reference (Warehouse.Code = \"01\"). "
	                          "Put a table's filters HERE rather than into the WHERE around it - there the "
	                          "whole register is read and folded first.");
	condition.m_condition        = true;
	condition.m_consumedBySource = true;
	out.push_back(condition);
}

// ⭐ AT WHAT GRANULARITY IS THIS INTERVAL READ — the same question for every register that folds one,
// so the same list of answers.
//
// ⚠ A WORD, NOT A REGISTERED TYPE, and that was tried and dropped on purpose (2026-08-11). Turning it
// into an enumeration looked right by the usual rule ("a closed set is a type") and is wrong here for
// a reason the rule does not cover: the value is written in the query TEXT as a keyword
// (`…Turnovers(&From, &To, Month)`). A registered type would add a SECOND spelling of the same thing —
// `RegisterPeriodicity.Month` beside `Month` — plus a clsid that is permanent the moment any data
// carries it.
//
// ⚠ AND THE LIST IS NOT SHORTENED WHERE THE ENGINE CANNOT COMPUTE A UNIT. Hiding units makes the
// window quietly disagree with what the TABLE is; where the computation is missing the engine says so
// precisely, in its own words, which is the right place for it.
inline void ibAppendRegisterPeriodicityParameter(std::vector<ibQuerySourceParameter>& out)
{
	ibQuerySourceParameter periodicity;
	periodicity.m_name    = wxT("Periodicity");
	periodicity.m_description    = _("How finely this READING is cut, which decides what a row is and "
	                          "therefore which columns it has: none of them read whole, the period "
	                          "with a period, the recorder with both, a record with the line number "
	                          "too. Not the register's stored Periodicity property, which is the "
	                          "grain the totals are KEPT at and takes different words.");
	periodicity.m_choices = { wxT("Period"), wxT("Record"), wxT("Recorder"), wxT("Auto") };
	for (const std::pair<ibTotalsPeriod, wxString>& unit : ibRegisterUnits())
		periodicity.m_choices.push_back(unit.second);
	periodicity.m_default.clear();   // empty is its own answer: the interval read WHOLE
	out.push_back(periodicity);
}

// THE SHAPE OF A VIRTUAL TABLE, for the three descriptors at once. A balance / turnover table
// exposes the columns of its VIEW, and that view's column set is built from the register's own
// dimensions and resources (accumulationRegisterSchema.cpp) and cached — metadata only, so this
// answer costs nothing and works on a base that has never been opened. Deliberately NOT the
// companion's GetColumns(): in RAM mode a companion navigates through the register itself and
// would report the MOVEMENT columns, which is the one answer that would mislead here.
// ⭐⭐ THE PERIODICITY DECIDES WHICH COLUMNS EXIST, and this is the rule, written where the columns
// are handed out:
//
//     empty          NOTHING. The interval is read WHOLE — one row per key, begin to end — so
//                    there is no period, and therefore no recorder and no line either: a row that
//                    covers a whole interval was not written by any one document.
//     Auto           every projection the table can make — Period, PeriodSecond … PeriodYear,
//                    Recorder and LineNumber. Nothing has been DECIDED, so everything is on offer
//                    and the author picks. (Including an argument written as a parameter, whose
//                    value only exists at run time: the shape a query is drawn against must be the
//                    widest it might turn out to have, never a guess at which.)
//     Period         one column: the period itself.
//     a unit         one column: the period, rolled to that unit. Months asked for, months given —
//                    the finer projections are not part of that reading and showing them would
//                    promise rows the query will not return.
//     Recorder       the period and the document it came from.
//     Record         the period, the document, and the line within it.
//
// ⚠ THE RECORDER IS NOT A DIMENSION, and that is why it is decided here too. It exists on a row
// only when the reading is AT a movement's own identity — Recorder, Record, or the undecided Auto.
// Offering it beside a monthly turnover promises a document per row where the row is a month's
// worth of them.
inline bool ibRegisterViewColumnFits(const wxString& columnName, const wxString& periodName,
	const ibRegFold& fold, const wxString& recorderName = wxEmptyString,
	const wxString& lineName = wxEmptyString)
{
	// The movement's own identity — present only where a row IS a movement (or a document's worth).
	if (!recorderName.IsEmpty() && stringUtils::CompareString(columnName, recorderName))
		return fold.m_kind == ibRegGranularity::Auto
		    || fold.m_kind == ibRegGranularity::Recorder
		    || fold.m_kind == ibRegGranularity::Record;
	if (!lineName.IsEmpty() && stringUtils::CompareString(columnName, lineName))
		return fold.m_kind == ibRegGranularity::Auto
		    || fold.m_kind == ibRegGranularity::Record;

	// Not a period projection (a dimension, a resource) — always there, whatever the granularity.
	if (!columnName.StartsWith(periodName))
		return true;

	// ⚠ NOTHING ASKED FOR MEANS NO PERIOD AT ALL — not "the period, undecided". Left out, this table
	// reads the interval WHOLE: one row per key, begin to end, with no date on it. That is what the
	// engine already does (no unit given → no grouping by period), and the column list has to say the
	// same thing. Showing a `Period` column over a reading that has no period is the window promising
	// a value the rows will not carry.
	if (fold.m_kind == ibRegGranularity::Whole)
		return false;

	// AUTO is the opposite: nothing has been DECIDED, so every projection the table can make is on
	// offer and the author picks one.
	if (fold.OffersEveryProjection())
		return true;

	// Anything else names ONE granularity, and the reading then has ONE period column — `Period`,
	// rolled to it. The coarser projections are not part of that reading.
	return stringUtils::CompareString(columnName, periodName);
}

// ⭐⭐ DOES THIS READING PRODUCE THAT COLUMN? The same rule the field tree offers by, asked with the
// register's own names — so a reader and a writer of the query cannot disagree about which columns
// a chosen granularity has.
template <typename TReg>
inline bool ibRegisterFoldOffersColumn(const TReg* reg,
	const wxString& columnName, const ibRegFold& fold)
{
	if (reg == nullptr || reg->GetRegisterPeriod() == nullptr)
		return true;

	const bool subordinate = reg->HasRecorder();
	return ibRegisterViewColumnFits(columnName, reg->GetRegisterPeriod()->GetName(), fold,
		subordinate && reg->GetRegisterRecorder()   != nullptr ? reg->GetRegisterRecorder()->GetName()   : wxString(),
		subordinate && reg->GetRegisterLineNumber() != nullptr ? reg->GetRegisterLineNumber()->GetName() : wxString());
}


// ⭐⭐ A DERIVED COLUMN IS NUMBERED AS A SYNTHETIC ONE — stamped with SyntheticKind::Derived. Not a band
// any more.
//
// 🛑 IT WAS THE LAST POSITIVE BAND, and it outlived the thing that replaced it. `0x50000000` was one
// of five hand-carved ranges in the positive space; the sign scheme took over from all of them
// (queryColumn.h, `SyntheticId`) precisely because a band map has to be READ before every addition
// and nothing makes anybody read it — and this one simply was not migrated with the rest. So the
// invariant that file states as structural, *"a column nobody declared says so by its sign"*, was
// false for every register surface: `Turnovers` published `Charged` as `0x50000007`, positive,
// indistinguishable by that rule from a declared attribute (measured 2026-09-09).
//
// Nothing acted on it — `IsSyntheticId` has no callers at all — so no defect was reachable. That is
// exactly why it is worth closing NOW: the first reader of the invariant would have been the one to
// find out, on a register column, in whatever they were building.
//
// ⭐⭐ …AND NUMBERED OVER WHAT THE CONFIGURATION DECLARED, not over a running count. A derived column is
// OF something with a real metaID: a column standing for an attribute (a collapsed account, a breakdown
// slot, the correspondent's side of a dimension) is `no` 0 of that attribute; a column the attribute OWNS
// (a figure of a resource, a coarser period of the period, a position of a date) is its `no` from 1. The
// id then does not depend on what else a surface publishes before it, and two columns of one surface
// cannot meet on one number (Max, 2026-09-16: "take the real metas and feed them into the generator").
//
// ⚠ ONE WRAP, AND THE BODY STAYS SMALL. The body is `owner × 32 + no`, stamped once: a metaID of 1400 comes
// out near three million, and a further reading of the surface (an alias twin, one more digit) still fits
// an `int` for metaIDs up to about 130 000 (queryColumn.h, CanComposeSyntheticId). Wrapping twice to keep the
// two families apart multiplied by eight more for nothing — `no` 0 already does.
inline constexpr unsigned int ibRegColumnsPerOwner = 32;
inline ibMetaID ibRegDerivedColumnId(ibMetaID owner, unsigned int no = 0)
{
	wxASSERT(no < ibRegColumnsPerOwner);
	return ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Derived,
		owner * static_cast<ibMetaID>(ibRegColumnsPerOwner) + static_cast<ibMetaID>(no));
}

// ⭐⭐ WHAT A SURFACE WAS BUILT FROM — names and types, in order.
//
// NOT a count. Counting caught a column being ADDED and missed one being RE-TYPED: give a dimension
// a reference type and the count is what it was, so the stale surface kept the old type and the
// catalogue would not unfold it. Names and types in order is what a surface IS, and a mismatch is
// what makes the cache rebuild instead of handing out something built before the register had been
// read at all (which is how `Turnovers` once showed as period-only for a whole session).
inline void ibRegSignAttribute(wxString& signature, const ibValueMetaObjectAttributeBase* attribute)
{
	if (attribute == nullptr)
		return;
	signature += attribute->GetName() + wxT(":");
	const ibTypeDescription& type = attribute->GetTypeDesc();
	for (unsigned int i = 0; i < type.GetClsidCount(); ++i)
		signature += wxString::Format(wxT("%llu,"), static_cast<unsigned long long>(type.GetByIdx(i)));
	signature += wxT(";");
}

// ⭐⭐ A DERIVED SURFACE, KEPT — the cache, the signature check and the retirement rule, once.
//
// Three builders had a copy of this (the accumulation register's views, the accounting register's
// turnover view, and its per-call shapes), and the parts that matter are the ones that are easy to
// leave out of a fourth: a surface asked for before the register's attributes were read stays empty
// for the life of the session unless the SIGNATURE is checked, and a rebuilt surface that is
// DESTROYED rather than retired is an access violation under whoever still holds the pointer.
//
// What differs between the three is the KEY and the COLUMNS, so those are the caller's: a view name
// where one name is one surface, the whole call key where the output schema follows the arguments.
class ibRegSurfaceCache
{
public:
	const ibBackendQueryable* Obtain(const wxString& key, const wxString& builtFrom,
	                                 const wxString& table, const ibMetaData* metaData,
	                                 const std::function<void(std::vector<ibTempColumn>&)>& build) const
	{
		const auto cached = m_sources.find(key);
		if (cached != m_sources.end()) {
			if (cached->second.m_builtFrom == builtFrom)
				return cached->second.m_surface.get();
			// ⚠ RETIRED, NOT DESTROYED. Somebody may still be reading through a pointer handed out
			// earlier; a surface that dies under a live reader is an access violation, and this
			// subsystem has produced one of those before.
			m_retired.push_back(std::move(cached->second.m_surface));
			m_sources.erase(cached);
		}

		std::vector<ibTempColumn> columns;
		// Each builder numbers its columns over the metaIDs they are of (ibRegDerivedColumnId) — no counter
		// is handed in, because none is needed.
		build(columns);

		auto surface = std::make_unique<ibDbTempTableQueryable>(table, std::move(columns), metaData);
		const ibBackendQueryable* raw = surface.get();
		m_sources.emplace(key, Entry{ std::move(surface), builtFrom });
		return raw;
	}

private:
	struct Entry
	{
		std::unique_ptr<ibDbTempTableQueryable> m_surface;
		wxString                                m_builtFrom;   // names + types, in order
	};

	// Mutable: a surface is DERIVED from metadata, so a const read may fill the cache without the
	// register being logically modified.
	mutable std::map<wxString, Entry>                            m_sources;
	mutable std::vector<std::unique_ptr<ibDbTempTableQueryable>> m_retired;
};

// ⭐⭐ AN ATTRIBUTE PUBLISHED AS ITSELF, on a derived surface — its own name, its own storage field,
// its own type, its own METAID and its own caption. All five come from the attribute, which is the
// whole point: a surface that republishes a register's attribute stays interchangeable with the
// register as a source instead of becoming a parallel vocabulary with worse names.
//
// It was written out at a dozen sites across the three surface builders, and the caption was the
// part every one of them forgot — so a period column headed `Period` where the register's own form
// says "Date of movement".
// ⭐⭐ THE COLUMN SAYS WHAT IT IS; ITS NAME IS NOT ASKED TO CARRY THAT.
//
// A derived surface stores a single-field attribute as ONE column (`fld1124_D` — the period), and a
// multi-field one as its whole spread (`fld1199_TYPE`, `fld1199_RTRef`, `fld1199_RRRef` — an
// account, a recorder). Those are two different shapes, and this one helper publishes both.
//
// 🛑 IT USED TO PUBLISH THEM ALIKE — always naming the column after `ibRegValueField`, the first
// VALUE field. For a single-field attribute that is the whole column and it was right. For a
// REFERENCE it named the column `fld1199_RTRef` while still declaring it composite, so the reader
// spread it a second time and asked the database for `fld1199_RTRef_RRRef`: a role suffix applied
// twice, `-206 Column unknown`, every reference key of the read at once. Measured 2026-08-31 from a
// trial balance, and invisible until then because only reference-typed attributes reach it.
//
// ⭐ So the shape is DECLARED rather than inferred from the spelling:
//   · several fields -> a COMPOSITE column named by the attribute's own base name, whose spread is
//     exactly the field list the surface declared (both come from DescribeColumnLayout);
//   · one field      -> a column that IS that field, said with the kind, which is what stops a
//     reader looking for a `_TYPE` the table does not have.
// Neither case leaves the name meaning something the reader has to work out.
// `published` — the name a reading gives the column when it is not the attribute's own (an accounting
// register's `Account` in a reading about one account, where the movements call it `AccountDr`).
//
// `columnId` — for a column that only LOOKS like the attribute: its type and fields, but values the reading
// composes (a turnover's corresponding account is the credit account on one pass and the debit one on the
// other). Such a column is not the attribute, and says so with an id of its own (ibRegDerivedColumnId).
inline ibTempColumn ibRegAttributeColumn(const ibValueMetaObjectAttributeBase* attribute,
                                         const wxString& published = wxString(), const wxString& publishedSynonym = wxString(),
                                         ibMetaID columnId = 0)
{
	const wxString name    = published.IsEmpty() ? attribute->GetName() : published;
	const wxString synonym = publishedSynonym.IsEmpty() ? attribute->GetSynonym() : publishedSynonym;
	const ibMetaID id      = columnId != 0 ? columnId : attribute->GetMetaID();
	// [0] is the _TYPE tag; anything past [1] means the value itself needs more than one field.
	const std::vector<wxString> fields = ColumnFieldNames(attribute->GetQueryColumn());
	const bool spreads = fields.size() > 2;

	// ⭐ THE TYPE A STORED COLUMN HOLDS, NOT THE ONE THE AUTHOR DECLARED (GetTypeValueDesc). They differ for
	// a CHARACTERISTIC — an account dimension's value slot declares "whatever the chart admits" as one
	// type — and a view column typed by the declaration laid itself out as a bare _TYPE field and told the
	// reader it held no reference: every analytics value read back from the totals came out empty, so a
	// balance broken down by counterparty folded all counterparties into one blank row (2026-09-15).
	const ibTypeDescription& stored = attribute->GetTypeValueDesc();
	return spreads
		? ibTempColumn(name, attribute->GetPhysicalName(),
		               stored, id, synonym,
		               ibBackendQueryColumn::Kind::Composite, attribute->GetColumnIcon())
		: ibTempColumn(name, ibRegValueField(attribute),
		               stored, id, synonym,
		               ibBackendQueryColumn::Kind::Computed, attribute->GetColumnIcon());
}

// ============================================================================
// The totals bundle — the parts both registers declare IDENTICALLY
// ============================================================================
//
// ⚠⚠ AND DELIBERATELY NOT "declare the bundle" AS ONE PROCEDURE. The two declarations differ in the
// KEY (a period plus dimensions; a period, an account, a (kind, value) pair per analytic and the
// dimensions), in what a movement CONTRIBUTES, and in how many tables there are. A single function
// over all of that would take the union of both signatures and branch inside on which caller it has
// — which is one function serving two callers separately, written as if it served them together, and
// the output here is a SCHEMA DIFF: a wrong one is a DROP.
//
// So what moved is what is word-for-word the same and cannot be got wrong twice. Each of these had
// two copies, and the drift between them is on the record — one register's `Active` guard was
// rendered without its comparison for two days while the other's was correct.

// THE SHARD COLUMN, INTO THE KEY. Split totals make several physical rows legal for one logical key,
// and the shard column is what makes them legal — so it belongs to the KEY, not beside it.
//
// It carries an IDENTITY rather than being scaffold: turning the switch ADDS a physical column to a
// table that already exists (and turning it back removes one), while scaffold columns are created
// with their table and never migrated. The totals object's own metaID names it — there is exactly one
// shard column per totals table, and it belongs to that table.
// ⭐⭐ RETURNS WHETHER THE COLUMN IS ACTUALLY THERE, and the caller must ask the BUNDLE the same
// question with the same answer. The column appears when `split` is on AND the totals object was
// found (its metaID names the column); the maintenance used to be told about the shard from `split`
// ALONE. One missing totals object was therefore enough to produce a table with no shard column and
// triggers written as if it had one — "Column unknown T.SHARD_", at CREATE TRIGGER time, after every
// preceding statement had reported success.
//
// Two conditions for one fact is the defect; this returns the fact so there can only be one.
inline bool ibRegSplitIntoKey(ibSchemaTable& t, const ibValueMetaObjectRegisterTotals* totals,
                              const wxString& shardColumnName, bool split,
                              std::vector<const ibBackendQueryColumn*>& keyCols)
{
	if (!split || totals == nullptr)
		return false;
	const ibBackendQueryColumn* shard = t.OwnRaw(ibBackendColumnRawDB::Number(shardColumnName, totals->GetMetaID()));
	t.Add(shard);
	keyCols.push_back(shard);
	return true;
}

// ⭐⭐ THE MOVEMENT IS IN FORCE — the delta's guard, and the ONE place that meaning belongs. Filtering
// it in each READING instead would be the same rule written in as many places as there are readings,
// and the day one forgot, an inactive entry would show up in exactly one report.
//
// ⚠ IT IS AN EXPRESSION, AND IT HAS TO BE A BOOLEAN ONE. A "boolean" attribute is stored as a
// SMALLINT, so `WHERE NEW.fld…_B` is a field where a condition is required — Firebird answers
// "invalid usage of boolean expression" and the whole CREATE TRIGGER fails, taking the restructuring
// that emitted it down with it. The cost is asymmetric: nothing READS wrong, the APPLY does not
// finish. This was written twice and one copy shipped without the ` <> 0`.
inline void ibRegGuardInForce(ibSchemaMaterialize& m, const ibValueMetaObjectAttributeBase* active)
{
	if (active == nullptr)
		return;
	m.Guard(wxT("{row}.") + ibRegValueField(active) + wxT(" <> 0"),
		ibQueryPredicate::Leaf(ibQueryCondition{ active->GetQueryColumn(), ibQueryFilterOp::Equal, ibValue(true) }));
}

// ⭐⭐ AN ACCUMULATING COLUMN, IN THE RESOURCE'S OWN PRECISION. Declared flat it came out
// NUMERIC(18,0) — no fraction at all — so a resource carrying kopecks lost them on the way INTO the
// totals, whatever the movements held. The digits come from the DECLARATION; only the word
// (NUMERIC / DECIMAL) is the dialect's. Three sites had this arithmetic, and one of them was wrong.
//
// The id is the caller's: the columns of one resource have to be told apart, and how (a high bit for
// the second) is the caller's own arrangement.
inline const ibBackendQueryColumn* ibRegAccumulatorColumn(ibSchemaTable& t, const wxString& name, ibMetaID id,
                                                          const ibValueMetaObjectAttributeBase* resource)
{
	const ibTypeDescription& type = resource->GetTypeDesc();
	const ibBackendQueryColumn* c = t.OwnRaw(ibBackendColumnRawDB::Number(name, id,
		type.GetPrecision() > 0 ? type.GetPrecision() : 18u, type.GetScale()));
	t.Add(c);
	return c;
}

// ⭐⭐ THE TOTALS TABLE AS A SOURCE, FROM ITS OWN DECLARATION. This table is declared BY a metaobject
// but is not one, so nothing else vends a queryable for it — and both L3-4 operations (regeneration,
// the shard fold) gate on exactly that, returning success having touched nothing when it is absent.
//
// Built from the table's own scaffold plus every logical column, so the source cannot drift from the
// schema it describes; called AFTER every column exists, which is the one thing a caller can get
// wrong here. The physical TABLE deliberately, never a view: a view sums the shards away, and the
// fold's whole business is the individual shard rows underneath.
// `keyCols` is what makes a row of this table unique — THE SAME LIST handed to ibDeclareDerivedKey,
// so the unique index and every write that has to identify a row read one declaration. Passing it
// here is why nothing downstream has to compose a key out of parts or look a column up by name.
inline void ibRegSelfSourceFromDeclaration(ibSchemaTable& t, const ibMetaData* metaData,
                                           std::vector<const ibBackendQueryColumn*> keyCols)
{
	std::vector<const ibBackendQueryColumn*> sourceColumns = t.m_scaffold;
	for (const ibSchemaColumn& c : t.m_columns)
		sourceColumns.push_back(c.m_column);
	t.SelfSource(std::make_shared<ibSchemaTableQueryable>(t.m_name, t.m_id, std::move(sourceColumns),
	                                                      metaData, std::move(keyCols)));
}

// ⭐⭐ THE ROWS A READING RETURNED, AS THE VALUE TABLE A SCRIPT GETS BACK — AND THE COLUMNS ARE THE
// SOURCE'S OWN.
//
// This was written three times: twice on the accumulation register (once per figure family, each
// re-listing the figures it expected) and once on the accounting one. The re-listing is what makes it
// worth sharing rather than the loop: a hand-written column list is a SECOND spelling of names the
// surface and the query already agree on, and the day they disagree the runtime table quietly answers
// with a column of nothing. That is exactly how `Resource1_Turnover` and `Resource1Turnover` came to
// be two names for one number.
//
// So the shape says what it publishes — names, types, ORDER and now captions — and this copies it.
// Nothing here knows what a figure is called, which is the point.
inline ibValue ibRegSelectionToTable(ibDataQueryResult& selection, const ibBackendQueryable* shape)
{
	ibValueModelTable* table = new ibValueModelTable();
	// 🛑 Held while its rows are made, as every table built for a script is: nobody else holds it
	// yet, and anything that took and let go of a hold meanwhile would delete it. See
	// valueQueryable.cpp, M::ToTable.
	const ibValue keep(table);
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();
	wxASSERT(cols);

	std::vector<const ibBackendQueryColumn*> columns;
	if (shape != nullptr)
		columns = shape->GetColumns();

	// Each table column's id, beside the source column it is made from.
	std::vector<std::pair<const ibBackendQueryColumn*, ibMetaID>> filled;
	for (const ibBackendQueryColumn* col : columns)
		if (col != nullptr)
			if (const auto* added = cols->AddColumn(col->GetName(), col->GetTypeDesc(), col->GetSynonym()))
				filled.emplace_back(col, added->GetColumnID());

	std::vector<std::pair<ibMetaID, ibValue>> row;
	while (selection.Next()) {
		// By the column, not by its name: GetColumn reads one scalar field under the read's own
		// alias, and a dimension is a reference of three. Every value used to come back empty.
		row.clear();
		for (const auto& one : filled)
			row.emplace_back(one.second, selection.GetValue(one.first));
		table->AppendRow(row);
	}

	return table;
}

#endif // __REGISTER_QUERY_LOWERING_H__
