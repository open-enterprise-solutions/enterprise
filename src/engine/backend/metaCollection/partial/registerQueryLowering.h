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
#include "backend/system/value/valuePointInTime.h"   // ibValuePointInTime — a boundary that names a document
#include "backend/system/value/valueBoundary.h"       // ibValueBoundary — the position AND which side of it
#include "backend/stringUtils.h"             // CompareString — the one case-insensitive name comparison

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

#endif // __REGISTER_QUERY_LOWERING_H__
