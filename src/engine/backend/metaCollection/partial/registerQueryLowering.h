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

// ⚠ NAMED, NOT INHERITED — MSVC hands these over transitively and GCC / Clang do not.
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
inline const std::vector<std::pair<ibTotalsPeriod, wxString>>& ibRegisterUnits()
{
	static const std::vector<std::pair<ibTotalsPeriod, wxString>> s_units = {
		{ ibTotalsPeriod::Second,   wxT("Second")   }, { ibTotalsPeriod::Minute,   wxT("Minute")   },
		{ ibTotalsPeriod::Hour,     wxT("Hour")     }, { ibTotalsPeriod::Day,      wxT("Day")      },
		{ ibTotalsPeriod::Week,     wxT("Week")     }, { ibTotalsPeriod::TenDays,  wxT("TenDays")  },
		{ ibTotalsPeriod::Month,    wxT("Month")    }, { ibTotalsPeriod::Quarter,  wxT("Quarter")  },
		{ ibTotalsPeriod::HalfYear, wxT("HalfYear") }, { ibTotalsPeriod::Year,     wxT("Year")     },
	};
	return s_units;
}

// The word a unit is written as — a lookup in that one table, not a second switch over it.
inline wxString ibRegisterUnitWord(ibTotalsPeriod unit)
{
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibRegisterUnits())
		if (u.first == unit)
			return u.second;
	return wxString();
}

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
	return figure;   // a figure nobody captioned reads as its own name rather than as nothing
}

// The sided caption — the same pairing as ibRegSidedFigure, one tier up. Kept beside it so a side
// that gains a spelling gains a caption in the same edit.
inline wxString ibRegSidedCaption(const wxString& figure, bool credit)
{
	return ibRegFigureCaption(figure) + wxT(" ") + (credit ? _("Cr") : _("Dr"));
}

// A published figure's full caption: what it is OF, then what it is. `<resource synonym> <figure>` —
// "Amount Closing balance", in the reader's language, whichever door produced the row.
inline wxString ibRegFigureColumnCaption(const wxString& ofWhat, const wxString& figureCaption)
{
	return ofWhat.IsEmpty() ? figureCaption : ofWhat + wxT(" ") + figureCaption;
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
	ibColumnCodec::WriteValue(reg->GetRegisterRecorder(), reg->GetMetaData(), recorder, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	for (size_t i = 0; i < slots.size() && i < consts.size(); i++) {
		if (slots[i].m_role == ibColumnRole::Discriminator || !consts[i])
			continue;
		tuple.push_back({ slots[i].m_name, consts[i] });
	}
	return tuple;
}

// Fill a read spec's cut. `TSpec` is ibMaterializeReadSpec — taken as a template parameter so this
// header needs no L2-2 include; every caller has one already.
template <typename TReg, typename TSpec>
inline void ibRegFillArmCut(TSpec& read, const TReg* reg,
                            const ibRegBound& upper, const ibRegBound& lower = ibRegBound())
{
	if (reg == nullptr || !reg->HasRecorder() || reg->GetRegisterRecorder() == nullptr)
		return;   // this view has one arm; there is nothing to cut

	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(reg->GetRegisterRecorder());
	if (slots.empty())
		return;

	// A stored row has no recorder, so any of its fields is NULL there — the mark is the row's own
	// answer to "which arm am I", with no flag column invented for it.
	read.m_markColumn = slots.front().m_name;

	const ibTotalsPeriod grain = reg->GetTotalsPeriodUnit();

	// ⭐ A BOUNDARY REACHES BELOW THE GRAIN when it falls inside one, or when it names a document.
	// Anything else — a date on a grain edge, no date at all — is answered by the stored rows whole,
	// and the movement arm is left excluded. The trigger keeps the CURRENT grain's row up to date, so
	// "no boundary" is not stale: it is complete at grain resolution.
	const auto reachesInside = [&](const ibRegBound& b, wxDateTime& moment) {
		if (b.m_date.GetType() != TYPE_DATE)
			return false;
		moment = b.m_date.GetDateTime();
		return b.HasRecorder() || ibTruncateToPeriod(moment, grain) != moment;
	};

	wxDateTime upperMoment, lowerMoment;
	const bool cutUpper = reachesInside(upper, upperMoment);
	const bool cutLower = reachesInside(lower, lowerMoment);
	if (!cutUpper && !cutLower)
		return;

	// The stored arm ends where the upper boundary's grain begins. With no upper boundary at all it
	// still ends somewhere — at the LOWER boundary's grain — because the only reason the arm is cut at
	// all is that one end of the interval is partial.
	read.m_toExcluding   = upper.m_excluding;
	read.m_fromExcluding = lower.m_excluding;
	read.m_floor = ibValue(ibTruncateToPeriod(cutUpper ? upperMoment : lowerMoment, grain));
	if (cutUpper && upper.HasRecorder())
		read.m_boundaryTail = ibRegRecorderTuple(reg, slots, upper.m_recorder);

	// The stored arm begins at the first WHOLE grain at or after the lower boundary: the grain the
	// boundary falls INTO holds movements before it as well, so it cannot be taken as a row.
	if (cutLower) {
		read.m_headSplit = ibValue(ibNextPeriodStart(lowerMoment, grain));
		if (lower.HasRecorder())
			read.m_boundaryHead = ibRegRecorderTuple(reg, slots, lower.m_recorder);
	}
}

// --- register-side convenience over the column-layout tier ---------------------------------------
// An attribute IS an ibBackendQueryColumn; the tier functions take (col, metaData). These thin
// wrappers derive the metadata from the attribute so register lowering reads cleanly — the SQL-field
// machinery lives in the tier, NOT on the attribute. The structured ibSQLField is GONE: a register
// builds the comma-joined field list with ibRegFieldList, or picks the _TYPE tag / first value field.
inline wxString       ibRegFieldList (const ibValueMetaObjectAttributeBase* a, const wxString& aggr = wxEmptyString) { return ColumnFieldList(a, aggr); }
inline wxString       ibRegComposite (const ibValueMetaObjectAttributeBase* a, const wxString& cmp = wxT("=")) { return ColumnComparePredicate(a, cmp); }

// All physical fields of an attribute (the TYPE tag + per-type fields; a reference
// expands to _RTRef + _RRRef), in the order SetValueAttribute binds them.
inline std::vector<wxString> ibRegFieldsOf(const ibValueMetaObjectAttributeBase* a)
{
	return ColumnFieldNames(a);
}

// The _TYPE discriminator field name (the first physical field of a composite column).
inline wxString ibRegTypeField(const ibValueMetaObjectAttributeBase* a)
{
	const std::vector<wxString> fields = ColumnFieldNames(a);
	return fields.empty() ? wxString() : fields[0];   // [0] is always the _TYPE tag
}

// The first VALUE field of an attribute (the field after the TYPE tag) — the column a
// register resource / record-type aggregate operates on (res_N, recordType_N / _E).
inline wxString ibRegValueField(const ibValueMetaObjectAttributeBase* a)
{
	const std::vector<wxString> fields = ColumnFieldNames(a);
	return fields.size() > 1 ? fields[1] : wxString();   // [0] is the _TYPE tag; [1] is the first value field
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
// ⚠ Only DIMENSIONS are filterable here. A resource is what the table FOLDED, and a condition over a
// fold belongs to the result rather than to an argument of the source — the same rule
// FillConditionExplorer offers to the window, so the two cannot disagree about what may be filtered.
template <typename TRegister>
inline ibQueryPredicatePtr ibRegFilterPredicate(const TRegister* reg, const ibValue& filter)
{
	ibValueStructure* structure = nullptr;
	if (reg == nullptr || !filter.ConvertToValue(structure) || structure == nullptr)
		return nullptr;   // nothing was asked for — which is not an empty filter, but no filter at all

	ibQueryPredicatePtr folded;
	for (const auto dimension : reg->GetDimensionArrayObject()) {
		ibValue value;
		if (dimension == nullptr || !structure->Property(dimension->GetName(), value))
			continue;

		ibQueryCondition leaf;
		leaf.m_col   = dimension;
		leaf.m_op    = ibQueryFilterOp::Equal;
		leaf.m_value = value;

		ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
		folded = folded ? ibQueryPredicate::Compose(ibQueryPredicateKind::And, folded, one) : one;
	}
	return folded;
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
	begin.m_type = periodType;
	out.push_back(begin);

	ibQuerySourceParameter end;
	end.m_name = wxT("EndOfPeriod");
	end.m_type = periodType;
	out.push_back(end);
}

// AND THE CONDITION, always last of the required ones and always a condition slot — the same shape
// every virtual table of every register has.
inline void ibAppendRegisterConditionParameter(std::vector<ibQuerySourceParameter>& out)
{
	ibQuerySourceParameter condition;
	condition.m_name      = wxT("Condition");
	condition.m_condition = true;
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


// ⭐ THE ID BAND A DERIVED COLUMN IS NUMBERED IN — clear of every metaID, so a surface's own columns
// (a figure, a coarser period projection) cannot be mistaken for an attribute's. It was re-declared
// as a local constant at each builder, which is a constant nobody owns: the next one is claimed by
// reading a comment.
inline constexpr ibMetaID ibRegDerivedColumnBand = 0x50000000u;

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
	                                 const std::function<void(std::vector<ibTempColumn>&, ibMetaID&)>& build) const
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
		ibMetaID synthetic = ibRegDerivedColumnBand;
		build(columns, synthetic);

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
inline ibTempColumn ibRegAttributeColumn(const ibValueMetaObjectAttributeBase* attribute)
{
	return ibTempColumn(attribute->GetName(), ibRegValueField(attribute),
	                    attribute->GetTypeDesc(), attribute->GetMetaID(), attribute->GetSynonym());
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
	const ibBackendQueryColumn* shard = t.OwnRaw(ibRawDBColumn::Number(shardColumnName, totals->GetMetaID()));
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
		ibQueryPredicate::Leaf(ibQueryCondition{ active, ibQueryFilterOp::Equal, ibValue(true) }));
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
	const ibBackendQueryColumn* c = t.OwnRaw(ibRawDBColumn::Number(name, id,
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
inline void ibRegSelfSourceFromDeclaration(ibSchemaTable& t, const ibMetaData* metaData)
{
	std::vector<const ibBackendQueryColumn*> sourceColumns = t.m_scaffold;
	for (const ibSchemaColumn& c : t.m_columns)
		sourceColumns.push_back(c.m_column);
	t.SelfSource(std::make_shared<ibSchemaTableQueryable>(t.m_name, t.m_id, std::move(sourceColumns), metaData));
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
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();
	wxASSERT(cols);

	std::vector<const ibBackendQueryColumn*> columns;
	if (shape != nullptr)
		columns = shape->GetColumns();

	for (const ibBackendQueryColumn* col : columns)
		if (col != nullptr)
			cols->AddColumn(col->GetName(), col->GetTypeDesc(), col->GetSynonym());

	while (selection.Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		wxASSERT(line);
		for (const ibBackendQueryColumn* col : columns)
			if (col != nullptr)
				line->SetAt(col->GetName(), selection.GetColumn(col->GetName()));
		wxDELETE(line);
	}

	return table;
}

#endif // __REGISTER_QUERY_LOWERING_H__
