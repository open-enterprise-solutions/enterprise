////////////////////////////////////////////////////////////////////////////
//	Description : calculation register metadata - the READINGS and their virtual tables
////////////////////////////////////////////////////////////////////////////
//
// ONE PLACE FOR THE READINGS, as the accumulation and accounting registers have one for their totals. Nothing is
// kept beside the records (calculationRegister.h, "The fact — a reading of the records"); what is here READS them:
// the fact, a relation over the records and the chart's Displacing section, and the companion that publishes it
// to L3 as `<Register>.ActualActionPeriod`; and the base (GetBase).
//

#include "calculationRegister.h"
#include "chartOfCalculationTypes.h"                                // the Displacing section the fact reads

#include "backend/databaseLayer/databaseQueryBuilder.h"             // L2 — the readings' relations
#include "backend/query/columnLayout.h"                             // ColumnFieldNames
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegCompositeIR / the surface cache
#include "backend/metaCollection/dimension/metaDimensionObject.h"    // a position's dimensions
#include "backend/metaCollection/resource/metaResourceObject.h"      // its figures
#include "backend/metaCollection/partial/reference/reference.h"      // the base: a Base row's owner, as the type it is
#include "backend/query/dbTableProvider.h"                           // the base: a field's value off a row (GetValueAttribute)
#include "backend/system/value/valueArray.h"                         // the base: GetBase's arguments
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"                         // the base: the table GetBase answers

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

// The names the fact publishes beside a record's own — what a query writes.
namespace ibCalcFactColumn {
	inline constexpr const wxChar* PositionStart  = wxT("PositionStart");
	inline constexpr const wxChar* PositionEnd    = wxT("PositionEnd");
}

static const wxChar* const ibCalcFactName = wxT("ActualActionPeriod");

// ============================================================================
// The records and the Displacing section by physical names
// ============================================================================

ibCalcViewSpec ibCalcViewSpecOf(const ibValueMetaObjectCalculationRegister* reg)
{
	ibCalcViewSpec v;
	if (reg == nullptr || !reg->IsUseActionPeriod())
		return v;
	const auto fieldsOf = [](const ibValueMetaObjectAttributeBase* attribute, std::vector<wxString>& out) {
		for (const wxString& field : ibRegFieldsOf(attribute))
			out.push_back(field);
	};

	for (const ibValueMetaObjectDimension* dimension : reg->GetDimensionArrayObject())
		fieldsOf(dimension, v.m_dimensions);
	fieldsOf(reg->GetCalculationType(), v.m_type);
	fieldsOf(reg->GetActionPeriod(), v.m_actionPeriod);
	v.m_start = ibRegFieldOfRole(reg->GetActionPeriodStart()->GetQueryColumn(), ibColumnRole::Date);
	v.m_end = ibRegFieldOfRole(reg->GetActionPeriodEnd()->GetQueryColumn(), ibColumnRole::Date);
	for (const ibColumnSlot& slot : DescribeColumnLayout(reg->GetActionPeriodStart()->GetQueryColumn()))
		if (slot.m_role == ibColumnRole::Date) {
			v.m_dayType = slot.m_type;
			v.m_typedDays = true;
		}
	for (const ibValueMetaObjectResource* resource : reg->GetResourceArrayObject())
		v.m_resources.push_back(ibRegValueField(resource));

	v.m_period = ibRegValueField(reg->GetRegistrationPeriod());
	const ibBackendQueryColumn* type = reg->GetCalculationType()->GetQueryColumn();
	v.m_typeId = ibRegFieldOfRole(type, ibColumnRole::ReferenceId);

	// The section is an ordinary query source — what `ChartOfCalculationTypes.<chart>.Displacing` names — so its
	// fields are asked of it: the owner as a query names it (`Ref`), the displacer type as its own attribute.
	const ibValueMetaObjectChartOfCalculationTypes* chart = reg->GetChartOfCalculationTypes();
	const ibValueMetaObjectCalculationTypeRelationTable* displacing = chart != nullptr ? chart->GetDisplacingTable() : nullptr;
	const ibBackendQueryColumn* owner = displacing != nullptr && displacing->IsAllowed() && displacing->GetCalculationType() != nullptr
		? displacing->GetQueryable()->ResolveColumnByName(wxT("Ref")) : nullptr;
	if (const ibBackendColumnRawDB* rawOwner = owner != nullptr ? owner->AsRawColumn() : nullptr) {
		const ibBackendQueryColumn* named = displacing->GetCalculationType()->GetQueryColumn();
		for (const ibColumnRole role : { ibColumnRole::ReferenceType, ibColumnRole::ReferenceId }) {
			const wxString held = ibRegFieldOfRole(type, role), names = ibRegFieldOfRole(named, role);
			if (!held.IsEmpty() && !names.IsEmpty())
				v.m_named.emplace_back(held, names);
		}
		// No pair to join the type by, no displacement: joined on nothing, any two overlapping records would
		// displace each other (the recalculation skips a section it cannot pair the same way).
		if (!v.m_named.empty()) {
			v.m_displacing = displacing->GetPhysicalTableName();
			v.m_owner = rawOwner->GetPhysicalName();
			v.m_namedId = ibRegFieldOfRole(named, ibColumnRole::ReferenceId);
		}
	}

	v.m_records = reg->GetPhysicalTableName();
	fieldsOf(reg->GetRegisterRecorder(), v.m_recordKey);
	fieldsOf(reg->GetRegisterLineNumber(), v.m_recordKey);
	if (reg->IsUseBasePeriod()) {
		fieldsOf(reg->GetBasePeriodStart(), v.m_recordFields);
		fieldsOf(reg->GetBasePeriodEnd(), v.m_recordFields);
	}
	v.m_active = ibRegValueField(reg->GetRegisterActive());
	v.m_storno = ibRegValueField(reg->GetStorno());
	v.m_recordFields.push_back(v.m_active);
	v.m_recordFields.push_back(v.m_storno);
	return v;
}

// ============================================================================
// The fact, as a relation over the records and the Displacing section
// ============================================================================

namespace {

const wxChar* const kFrom  = wxT("ib_from");    // a span of the fact's walk: its first day...
const wxChar* const kTo    = wxT("ib_to");      // ...and the day after its last
const wxChar* const kReach = wxT("ib_reach");   // how far the spans before it reach

ibQueryExprPtr Day(const wxString& q, const wxString& field)
{
	return ibPeriodTrunc(ibCol(q, field), ibTotalsPeriod::Day);
}

// A number in a SELECT list, typed. Bare, a constant binds as a parameter with no type of its own there,
// and Firebird refuses the statement ("Data type unknown").
ibQueryExprPtr Int(int value)
{
	return ibCast(ibConst(ibValue(value)), ibTypeInteger());
}

ibQueryExprPtr DayAfter(ibQueryExprPtr day)  { return ibDateAdd(std::move(day), ibTotalsPeriod::Day, Int(1)); }
ibQueryExprPtr DayBefore(ibQueryExprPtr day) { return ibDateAdd(std::move(day), ibTotalsPeriod::Day, Int(-1)); }

// A computed day, as the register stores its days (ibCalcViewSpec::m_dayType) — what a reading hands out.
ibQueryExprPtr Stored(const ibCalcViewSpec& v, ibQueryExprPtr day)
{
	return v.m_typedDays ? ibCast(std::move(day), v.m_dayType) : day;
}

ibQueryExprPtr And(const ibQueryExprPtr& a, const ibQueryExprPtr& b)
{
	return !a ? b : !b ? a : ibBinOp(ibQueryBinOp::And, a, b);
}

ibQueryExprPtr Eq(const ibQueryExprPtr& a, const ibQueryExprPtr& b) { return ibBinOp(ibQueryBinOp::Eq, a, b); }

// The same fields of two tables, equal field by field.
ibQueryExprPtr SameFields(const wxString& l, const std::vector<wxString>& fields, const wxString& r)
{
	ibQueryExprPtr out;
	for (const wxString& f : fields)
		out = And(out, Eq(ibCol(l, f), ibCol(r, f)));
	return out;
}

ibQueryExprPtr Narrowed(const ibCalcNarrowing& narrowing, const wxString& alias, bool subject)
{
	return narrowing ? narrowing(alias, subject) : nullptr;
}

// A position's fields, in their order.
std::vector<wxString> PositionFields(const ibCalcViewSpec& v)
{
	std::vector<wxString> out = v.m_dimensions;
	out.insert(out.end(), v.m_type.begin(), v.m_type.end());
	out.insert(out.end(), v.m_actionPeriod.begin(), v.m_actionPeriod.end());
	out.push_back(v.m_start);
	out.push_back(v.m_end);
	return out;
}

void Carry(std::vector<ibQueryProjItem>& out, const std::vector<wxString>& fields, const wxString& q)
{
	for (const wxString& f : fields)
		out.push_back(ibQueryProjItem{ ibCol(q, f), f });
}

// Active — an inactive record exists and counts for nothing.
ibQueryExprPtr Counts(const ibCalcViewSpec& v, const wxString& q)
{
	return ibBinOp(ibQueryBinOp::Ne, ibCol(q, v.m_active), Int(0));
}

ibQueryExprPtr WithDays(const ibCalcViewSpec& v, const wxString& q)
{
	return ibBinOp(ibQueryBinOp::Le, Day(q, v.m_start), Day(q, v.m_end));
}

// ---- the records and their displacers ------------------------------------------------------------------

// A record's fields, once each, in order: what a record is (the recorder, the line), its position, its
// registration and the rest it carries, its figures.
std::vector<wxString> RecordFields(const ibCalcViewSpec& v)
{
	std::vector<wxString> all = v.m_recordKey;
	const std::vector<wxString> position = PositionFields(v);
	all.insert(all.end(), position.begin(), position.end());
	all.push_back(v.m_period);
	all.insert(all.end(), v.m_recordFields.begin(), v.m_recordFields.end());
	all.insert(all.end(), v.m_resources.begin(), v.m_resources.end());
	std::vector<wxString> out;
	std::set<wxString> seen;
	for (const wxString& f : all)
		if (!f.IsEmpty() && seen.insert(f).second)
			out.push_back(f);
	return out;
}

// The records a fact is of: every active one with days, stornos included, narrowed as the subject.
ibQueryRelPtr Records(const ibCalcViewSpec& v, const ibCalcNarrowing& narrowing)
{
	std::vector<ibQueryProjItem> proj;
	Carry(proj, RecordFields(v), wxT("b"));
	return ibProject(ibFilter(ibScan(v.m_records, wxT("b")), And(And(Counts(v, wxT("b")), WithDays(v, wxT("b"))),
		Narrowed(narrowing, wxT("b"), /*subject*/ true))), proj);
}

// Every record `x` with every displacer in force over its days: the Displacing rows `r` owned by x's type — a row
// naming its owner left out — and the active records `y` of the types they name, for the same dimension values,
// whose action period meets x's; grouped by the displacer's position, in force while its records outnumber its
// stornos. Found through the records' lookup index (the dimensions and the type) from each x — the types a section
// names are the rare ones, a leave or an absence, so the few records of them are what is read. `x` is the fact's
// subject; `y` never is — narrowed by the dimensions alone.
ibQueryRelPtr Pairs(const ibCalcViewSpec& v, const ibCalcNarrowing& narrowing)
{
	const std::vector<wxString> carried = RecordFields(v);
	const ibQueryExprPtr owned = And(Eq(ibCol(wxT("r"), v.m_owner), ibCol(wxT("x"), v.m_typeId)),
		ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("r"), v.m_namedId), ibCol(wxT("r"), v.m_owner)));
	ibQueryExprPtr on = SameFields(wxT("y"), v.m_dimensions, wxT("x"));
	for (const std::pair<wxString, wxString>& named : v.m_named)
		on = And(on, Eq(ibCol(wxT("y"), named.first), ibCol(wxT("r"), named.second)));
	on = And(on, And(ibBinOp(ibQueryBinOp::Le, Day(wxT("y"), v.m_start), Day(wxT("x"), v.m_end)),
		ibBinOp(ibQueryBinOp::Ge, Day(wxT("y"), v.m_end), Day(wxT("x"), v.m_start))));
	const ibQueryRelPtr joined = ibJoin(ibJoin(ibSubquery(Records(v, narrowing), wxT("x")), ibScan(v.m_displacing, wxT("r")), owned),
		ibScan(v.m_records, wxT("y")), on);

	std::vector<wxString> displacer = v.m_type;
	displacer.insert(displacer.end(), v.m_actionPeriod.begin(), v.m_actionPeriod.end());
	displacer.push_back(v.m_start);
	displacer.push_back(v.m_end);
	std::vector<ibQueryProjItem> proj;
	std::vector<ibQueryExprPtr> group;
	for (const wxString& f : carried) {
		proj.push_back(ibQueryProjItem{ ibCol(wxT("x"), f), f });
		group.push_back(ibCol(wxT("x"), f));
	}
	for (const wxString& f : displacer) {
		proj.push_back(ibQueryProjItem{ ibCol(wxT("y"), f), ibCalcDisplacerTypePrefix + f });
		group.push_back(ibCol(wxT("y"), f));
	}
	// A record counts one and a storno takes one away: the storno nets out the way an accounting reversal does.
	const ibQueryExprPtr counted = ibCase({ { ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("y"), v.m_storno), Int(0)), Int(-1) } }, Int(1));
	return ibSubquery(ibAggregate(ibFilter(joined, And(And(Counts(v, wxT("y")), WithDays(v, wxT("y"))),
		Narrowed(narrowing, wxT("y"), /*subject*/ false))),
		proj, group, ibBinOp(ibQueryBinOp::Gt, ibFunc(wxT("SUM"), { counted }), Int(0))), wxT("x"));
}

// A displacer's day, as the pairs carry it (under the prefix).
ibQueryExprPtr DisplacerDay(const wxString& field) { return Day(wxT("x"), ibCalcDisplacerTypePrefix + field); }

// The days the displacer takes of `x` — the later of the two starts, the earlier of the two ends.
ibQueryExprPtr TakenFirst(const ibCalcViewSpec& v)
{
	return ibCase({ { ibBinOp(ibQueryBinOp::Gt, DisplacerDay(v.m_start), Day(wxT("x"), v.m_start)), DisplacerDay(v.m_start) } },
		Day(wxT("x"), v.m_start));
}

ibQueryExprPtr TakenLast(const ibCalcViewSpec& v)
{
	return ibCase({ { ibBinOp(ibQueryBinOp::Lt, DisplacerDay(v.m_end), Day(wxT("x"), v.m_end)), DisplacerDay(v.m_end) } },
		Day(wxT("x"), v.m_end));
}

// ---- the fact ------------------------------------------------------------------------------------------

// The pieces, by a walk over spans in order of their starts, one record at a time: the record's own first day
// and the day after its last, as spans of no length, and every span its displacers take. A piece is the gap
// before a span that begins past everything the spans before it reach — the first day's marker has nothing
// before it and opens none, and adjacent spans leave no gap.
ibQueryRelPtr FactView(const ibCalcViewSpec& v, const ibCalcNarrowing& narrowing)
{
	const std::vector<wxString> carried = RecordFields(v);

	// ⚠ A record with no days walks nothing (Records keeps only records with days): its end's marker would stand
	// BEFORE its start's, and the gap between them read as a piece of a record in force on no day.
	std::vector<ibQueryProjItem> opens, closes;
	Carry(opens, carried, wxT("b"));
	opens.push_back(ibQueryProjItem{ Day(wxT("b"), v.m_start), kFrom });
	opens.push_back(ibQueryProjItem{ Day(wxT("b"), v.m_start), kTo });
	Carry(closes, carried, wxT("b"));
	closes.push_back(ibQueryProjItem{ DayAfter(Day(wxT("b"), v.m_end)), kFrom });
	closes.push_back(ibQueryProjItem{ DayAfter(Day(wxT("b"), v.m_end)), kTo });
	std::vector<ibQueryProjItem> taken;
	Carry(taken, carried, wxT("x"));
	taken.push_back(ibQueryProjItem{ TakenFirst(v), kFrom });
	taken.push_back(ibQueryProjItem{ DayAfter(TakenLast(v)), kTo });
	ibQueryRelPtr spans = ibUnionAll(ibProject(ibSubquery(Records(v, narrowing), wxT("b")), opens),
		ibProject(ibSubquery(Records(v, narrowing), wxT("b")), closes));
	if (!v.m_displacing.IsEmpty())
		spans = ibUnionAll(spans, ibProject(Pairs(v, narrowing), taken));

	std::vector<ibQueryExprPtr> record;   // what the walk goes one of at a time
	for (const wxString& f : v.m_recordKey)
		record.push_back(ibCol(wxT("w"), f));
	std::vector<ibQueryProjItem> walked;
	Carry(walked, carried, wxT("w"));
	walked.push_back(ibQueryProjItem{ ibCol(wxT("w"), kFrom), kFrom });
	walked.push_back(ibQueryProjItem{ ibWindowed(ibFunc(wxT("MAX"), { ibCol(wxT("w"), kTo) }),
		{ record, { ibQuerySortKey{ ibCol(wxT("w"), kFrom), ibQuerySortDir::Asc }, ibQuerySortKey{ ibCol(wxT("w"), kTo), ibQuerySortDir::Asc } },
		  ibQueryFrame::RowsBeforeCurrent }), kReach });
	const ibQueryRelPtr walk = ibProject(ibSubquery(spans, wxT("w")), walked);

	std::vector<ibQueryProjItem> pieces;
	for (const wxString& f : carried) {
		if (f == v.m_start)
			pieces.push_back(ibQueryProjItem{ Stored(v, ibCol(wxT("g"), kReach)), f });
		else if (f == v.m_end)
			pieces.push_back(ibQueryProjItem{ Stored(v, DayBefore(ibCol(wxT("g"), kFrom))), f });
		else
			pieces.push_back(ibQueryProjItem{ ibCol(wxT("g"), f), f });
	}
	pieces.push_back(ibQueryProjItem{ ibCol(wxT("g"), v.m_start), ibCalcPositionFirst });
	pieces.push_back(ibQueryProjItem{ ibCol(wxT("g"), v.m_end), ibCalcPositionLast });
	return ibProject(ibFilter(ibSubquery(walk, wxT("g")), ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("g"), kFrom), ibCol(wxT("g"), kReach))),
		pieces);
}

} // namespace

ibQueryRelPtr ibCalcFactRelation(const ibCalcViewSpec& spec, const ibCalcNarrowing& narrowing)
{
	if (spec.m_records.IsEmpty())
		return nullptr;
	return FactView(spec, narrowing);
}

// ============================================================================
// The shape the fact publishes
// ============================================================================

// ⭐ A RECORD'S OWN COLUMNS ARE THE REGISTER'S ATTRIBUTES, published as themselves (ibRegAttributeColumn): the same
// names, types and ids as on the register, laid out as the register lays a record out (Max, 2026-09-14) — the
// registration, the recorder and the line first, the position, the base period, Active, Storno, the dimensions,
// the resources — so the fact is interchangeable with the register as a source. What it adds is numbered as a
// derived column.
const ibBackendQueryable* ibValueMetaObjectCalculationRegister::GetFactSurface() const
{
	std::vector<const ibValueMetaObjectAttributeBase*> record = { GetRegistrationPeriod(), GetRegisterRecorder(),
		GetRegisterLineNumber(), GetCalculationType(), GetActionPeriod(), GetActionPeriodStart(), GetActionPeriodEnd() };
	if (IsUseBasePeriod()) {
		record.push_back(GetBasePeriodStart());
		record.push_back(GetBasePeriodEnd());
	}
	record.push_back(GetRegisterActive());
	record.push_back(GetStorno());
	for (const ibValueMetaObjectDimension* dimension : GetDimensionArrayObject())
		record.push_back(dimension);
	const std::vector<ibValueMetaObjectResource*> resources = GetResourceArrayObject();

	// Keyed by what it was built from — asked before the attributes were read, a surface would otherwise stay
	// empty for the session (ibRegSurfaceCache).
	wxString shape;
	for (const ibValueMetaObjectAttributeBase* attribute : record)
		ibRegSignAttribute(shape, attribute);
	for (const ibValueMetaObjectResource* resource : resources)
		ibRegSignAttribute(shape, resource);

	return m_surfaces.Obtain(ibCalcFactName, shape, GetPhysicalTableName() + wxT("_") + ibCalcFactName, GetMetaData(),
		[&](std::vector<ibTempColumn>& columns)
	{
		for (const ibValueMetaObjectAttributeBase* attribute : record)
			columns.push_back(ibRegAttributeColumn(attribute));
		for (const ibValueMetaObjectResource* resource : resources)
			columns.push_back(ibRegAttributeColumn(resource));
		// The two positions are positions OF the action period's start — numbered over it (ibRegDerivedColumnId).
		const ibTypeDescription& day = GetActionPeriodStart()->GetTypeDesc();
		const ibMetaID dayId = GetActionPeriodStart()->GetMetaID();
		columns.push_back(ibTempColumn(ibCalcFactColumn::PositionStart, ibCalcPositionFirst, day, ibRegDerivedColumnId(dayId, 1),
			_("Position start"), ibBackendQueryColumn::Kind::Computed, GetActionPeriodStart()->GetColumnIcon()));
		columns.push_back(ibTempColumn(ibCalcFactColumn::PositionEnd, ibCalcPositionLast, day, ibRegDerivedColumnId(dayId, 2),
			_("Position end"), ibBackendQueryColumn::Kind::Computed, GetActionPeriodStart()->GetColumnIcon()));
	});
}

// ============================================================================
// The companion
// ============================================================================

// A piece of a record: the record, and where the piece begins.
std::vector<const ibBackendQueryColumn*> ibCalcFactQueryable::GetPrimaryKeyColumns() const
{
	const std::vector<wxString> names = { m_reg->GetRegisterRecorder()->GetName(), m_reg->GetRegisterLineNumber()->GetName(),
		m_reg->GetActionPeriodStart()->GetName() };
	std::vector<const ibBackendQueryColumn*> out;
	const ibBackendQueryable* surface = NavigationSource();
	for (const wxString& name : names)
		if (const ibBackendQueryColumn* column = surface != nullptr ? surface->ResolveColumnByName(name) : nullptr)
			out.push_back(column);
	return out;
}

// ⭐⭐ THE ROWS, FROM THE DATABASE, NARROWED INSIDE. The reading's conditions on the subject's own fields — whichever
// way they were written — become the narrowing of the tables the relation reads: a dimension narrows every one
// of them by equality, the rest of the subject the subject alone (calculationRegister.h says why), a date by a
// range as well. A payroll asking for one employee, for one type in one month, or for what is in force between
// two days has the database walk those and no others; the provider applies every condition over the rows
// afterwards.
ibQueryRamTable ibCalcFactQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable out;
	const ibBackendQueryable* surface = NavigationSource();
	if (surface == nullptr)
		return out;
	const std::vector<const ibBackendQueryColumn*> columns = surface->GetColumns();
	for (const ibBackendQueryColumn* column : columns)
		out.AddColumn(column->GetColumnId(), column->GetName(), column->GetTypeDesc());
	if (!m_reg->IsUseActionPeriod())
		return out;

	// A condition reaches here on the SURFACE's column, which carries the attribute's own id. What it may narrow,
	// and how:
	//   Everywhere — a dimension: every table, by equality;
	//   Subject    — the rest of the record's own: the records alone, by equality — and a DATE of its own by a
	//                range too, as written (the month; the registration);
	//   PieceStart / PieceEnd — the fact's two days, a PIECE's bounds and not the record's: a range on them
	//                narrows the record by what it implies, a piece lying inside its record — a piece beginning
	//                by X is of a record beginning by X, a piece ending from X of a record ending from X, a piece
	//                beginning from X of a record ending from X, a piece ending by X of a record beginning by X.
	enum class Reach { Everywhere, Subject, PieceStart, PieceEnd };
	struct Narrows { const ibValueMetaObjectAttributeBase* m_attribute; Reach m_reach; bool m_dated; };
	std::map<ibMetaID, Narrows> narrows;
	for (const ibValueMetaObjectDimension* dimension : m_reg->GetDimensionArrayObject())
		narrows[dimension->GetMetaID()] = { dimension, Reach::Everywhere, false };
	narrows[m_reg->GetCalculationType()->GetMetaID()] = { m_reg->GetCalculationType(), Reach::Subject, false };
	narrows[m_reg->GetActionPeriod()->GetMetaID()] = { m_reg->GetActionPeriod(), Reach::Subject, true };
	narrows[m_reg->GetActionPeriodStart()->GetMetaID()] = { m_reg->GetActionPeriodStart(), Reach::PieceStart, true };
	narrows[m_reg->GetActionPeriodEnd()->GetMetaID()] = { m_reg->GetActionPeriodEnd(), Reach::PieceEnd, true };
	// …and what makes a row a record's: a payroll asks for its own records' days by the recorder.
	narrows[m_reg->GetRegisterRecorder()->GetMetaID()] = { m_reg->GetRegisterRecorder(), Reach::Subject, false };
	narrows[m_reg->GetRegisterLineNumber()->GetMetaID()] = { m_reg->GetRegisterLineNumber(), Reach::Subject, false };
	narrows[m_reg->GetRegistrationPeriod()->GetMetaID()] = { m_reg->GetRegistrationPeriod(), Reach::Subject, true };

	struct Term { const ibBackendQueryColumn* m_col; ibValue m_value; ibQueryBinOp m_op; bool m_everywhere; };
	std::vector<Term> terms;
	const auto binOp = [](ibQueryFilterOp op, ibQueryBinOp& as) {
		switch (op) {
		case ibQueryFilterOp::Equal:        as = ibQueryBinOp::Eq; return true;
		case ibQueryFilterOp::Less:         as = ibQueryBinOp::Lt; return true;
		case ibQueryFilterOp::LessEqual:    as = ibQueryBinOp::Le; return true;
		case ibQueryFilterOp::Greater:      as = ibQueryBinOp::Gt; return true;
		case ibQueryFilterOp::GreaterEqual: as = ibQueryBinOp::Ge; return true;
		default:                            return false;
		}
	};
	const ibBackendQueryColumn* recordStart = m_reg->GetActionPeriodStart()->GetQueryColumn();
	const ibBackendQueryColumn* recordEnd = m_reg->GetActionPeriodEnd()->GetQueryColumn();

	// FOR WHICH DAYS OF ACTION (ibCalcViewArg): the subject's own action period meets them — it ends on
	// BeginOfActionPeriod or later and begins on EndOfActionPeriod or earlier.
	const auto dated = [](const ibValue& v) { return v.GetType() == TYPE_DATE && v.GetDateTime().IsValid(); };
	if (dated(m_actionFrom))
		terms.push_back({ recordEnd, m_actionFrom, ibQueryBinOp::Ge, false });
	if (dated(m_actionTo))
		terms.push_back({ recordStart, m_actionTo, ibQueryBinOp::Le, false });

	for (const ibQueryCondition& c : extra) {
		ibQueryBinOp op;
		if (c.m_col == nullptr || !c.m_path.empty() || c.m_expr || c.m_semiJoin || !binOp(c.m_op, op))
			continue;
		const auto found = narrows.find(c.m_col->GetColumnId());
		if (found == narrows.end())
			continue;
		const Narrows& n = found->second;
		if (op != ibQueryBinOp::Eq && !n.m_dated)
			continue;   // a range on a reference or a number says nothing an index could ride
		const bool byStart = op == ibQueryBinOp::Lt || op == ibQueryBinOp::Le;   // "by X"; else "from X"
		switch (n.m_reach) {
		case Reach::Everywhere:
		case Reach::Subject:
			terms.push_back({ n.m_attribute->GetQueryColumn(), c.m_value, op, n.m_reach == Reach::Everywhere });
			break;
		case Reach::PieceStart:
		case Reach::PieceEnd:
			if (op == ibQueryBinOp::Eq) {   // the piece holds X: the record begins by X and ends from X
				terms.push_back({ recordStart, c.m_value, ibQueryBinOp::Le, false });
				terms.push_back({ recordEnd, c.m_value, ibQueryBinOp::Ge, false });
			}
			else
				terms.push_back({ byStart ? recordStart : recordEnd, c.m_value, op, false });
			break;
		}
	}
	// ⭐ THE PERIOD IS A BOUNDARY OF EVERY TABLE READ: what was registered before the next period — the subject and
	// its displacers alike, the registration period under the register's field name.
	const ibCalcViewSpec spec = ibCalcViewSpecOf(m_reg);
	ibQueryExprPtr before;
	if (dated(m_moment))
		before = ibConst(ibValue(ibNextPeriodStart(m_moment.GetDateTime(), m_reg->GetPeriodicityUnit())));

	const ibMetaData* metaData = m_reg->GetMetaData();
	ibCalcNarrowing narrowing;
	if (!terms.empty() || before)
		narrowing = [&terms, &before, &spec, metaData](const wxString& alias, bool subject) {
			ibQueryExprPtr all;
			if (before)
				all = ibBinOp(ibQueryBinOp::Lt, ibCol(alias, spec.m_period), before);
			for (const Term& one : terms)
				if (one.m_everywhere || subject)
					if (const ibQueryExprPtr term = ibRegCompositeIR(one.m_col, metaData, one.m_value, one.m_op, alias))
						all = all ? ibBinOp(ibQueryBinOp::And, all, term) : term;
			return all;
		};

	const ibQueryRelPtr relation = ibCalcFactRelation(spec, narrowing);
	if (!relation)
		return out;

	// Every field the published columns read, once each.
	std::vector<ibQueryProjItem> proj;
	std::set<wxString> projected;
	for (const ibBackendQueryColumn* column : columns)
		for (const wxString& field : ColumnFieldNames(column))
			if (projected.insert(field).second)
				proj.push_back(ibQueryProjItem{ ibCol(wxT("v"), field), field });

	ibDatabaseQueryBuilder q;
	q.From(ibSubquery(relation, wxT("v")));
	q.Project(proj);
	ibQueryResult rs = q.Execute();
	while (rs.Next()) {
		const long row = out.AppendRow();
		for (const ibBackendQueryColumn* column : columns) {
			ibValue value;
			column->ReadValue(column->GetPhysicalName(), metaData, value, rs);
			out.SetCell(row, column->GetColumnId(), value);
		}
	}
	return out;
}

// ============================================================================
// The source
// ============================================================================

wxString ibCalcFactSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_meta->GetClassType());
}

wxString ibCalcFactSourceDescriptor::GetName() const
{
	return m_meta->GetName() + wxT(".") + ibCalcFactName;
}

// Built and KEPT by the base — the same call gives the same object back (queryableFactory.h, MakeCompanion).
const ibBackendQueryable* ibCalcFactSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	if (!m_meta->IsUseActionPeriod())
		return nullptr;
	return MakeCompanion<ibCalcFactQueryable>(paParams, lSizeArray, m_meta,
		ibRegArg(paParams, lSizeArray, ibCalcViewArg::Period),
		ibRegArg(paParams, lSizeArray, ibCalcViewArg::BeginOfActionPeriod), ibRegArg(paParams, lSizeArray, ibCalcViewArg::EndOfActionPeriod));
}

// FOUR ARGUMENTS, ALL OPTIONAL — AS OF WHICH MOMENT OF REGISTRATION (Period), FOR WHICH DAYS OF ACTION, THEN THE CONDITION
// (ibCalcViewArg): `ActualActionPeriod(, &MonthStart, &MonthEnd, Employee = &Employee)`. Each time is typed by the
// register's own attribute of that period. The condition is not consumed: it goes into the WHERE like any condition,
// and from there what it says of the records reaches the reading (ComputeRows) — the same road as when it is written
// in the WHERE itself.
void ibCalcFactSourceDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	const auto time = [&out](const wxChar* name, const ibValueMetaObjectAttributeBase* typedBy, const wxString& description) {
		ibQuerySourceParameter parameter;
		parameter.m_name = name;
		parameter.m_description = description;
		if (typedBy != nullptr)
			parameter.m_type = typedBy->GetTypeDesc();
		out.push_back(parameter);
	};
	time(wxT("Period"), m_meta != nullptr ? m_meta->GetRegistrationPeriod() : nullptr,
		_("AS OF WHICH REGISTRATION PERIOD - a moment: the records registered up to the end of the period it falls in, "
		  "in the register's periodicity, so a month reads as it stood when it closed and a correction registered later "
		  "is not in it. Left out, every period is read."));
	time(wxT("BeginOfActionPeriod"), m_meta != nullptr ? m_meta->GetActionPeriodStart() : nullptr,
		_("FOR WHICH DAYS OF ACTION, from - what is in force on this day or later. Left out, from the first."));
	time(wxT("EndOfActionPeriod"), m_meta != nullptr ? m_meta->GetActionPeriodEnd() : nullptr,
		_("FOR WHICH DAYS OF ACTION, to - what is in force on this day or earlier. Left out, to the last."));
	ibAppendRegisterConditionParameter(out);
}

// A dimension of a reading IS the dimension (the note in ibFillExplorerFromRegisterView): handed over as the
// register's own attribute wherever the column is one.
void ibCalcFactSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	const ibBackendQueryable* surface = m_meta->GetFactSurface();
	if (surface == nullptr)
		return;
	for (const ibBackendQueryColumn* column : surface->GetColumns()) {
		if (const ibValueMetaObjectAttributeBase* attribute = m_meta->FindAnyAttributeObjectByFilter(column->GetColumnId()))
			explorer.AppendColumn(attribute->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
		else
			explorer.AppendColumn(column);
	}
}

// What the reading can narrow inside (ComputeRows): the dimensions — every table — and a record's type, month and
// recorder — the records it produces. Any other condition is correct in the WHERE, applied over the rows once computed.
void ibCalcFactSourceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectDimension* dimension : m_meta->GetDimensionArrayObject())
		explorer.AppendColumn(dimension->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
	for (const ibValueMetaObjectAttributeBase* part : { static_cast<const ibValueMetaObjectAttributeBase*>(m_meta->GetCalculationType()),
			static_cast<const ibValueMetaObjectAttributeBase*>(m_meta->GetActionPeriod()),
			static_cast<const ibValueMetaObjectAttributeBase*>(m_meta->GetRegisterRecorder()) })
		if (part != nullptr)
			explorer.AppendColumn(part->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
}

// ============================================================================
// The base — GetBase
// ============================================================================

namespace {

// A calendar day as a number: whole days between two dates, whatever the clock did in between — a difference of
// local times across a daylight-saving change is an hour short of a day. Days from the civil calendar (y, m, d),
// so a date read back from the database counts the day it names.
long long ibCalcCalendarDay(const wxDateTime& t)
{
	int y = t.GetYear();
	const unsigned m = static_cast<unsigned>(t.GetMonth()) + 1;
	const unsigned d = t.GetDay();
	y -= m <= 2 ? 1 : 0;
	const long long era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<long long>(doe) - 719468;
}

// Each item of a comma-separated list, trimmed; empty items dropped.
std::vector<wxString> ibCalcBaseItemsOf(const wxString& list)
{
	std::vector<wxString> out;
	for (wxString item : wxSplit(list, wxT(','), wxT('\0'))) {
		item.Trim().Trim(false);
		if (!item.IsEmpty())
			out.push_back(item);
	}
	return out;
}

// "Register.Field": the register named — one this register takes a base from — and the field's name after the dot.
const ibValueMetaObjectCalculationRegister* ibCalcBaseRegisterOf(const ibValueMetaObjectCalculationRegister* reg,
	const wxString& path, wxString& field)
{
	const int dot = path.Find(wxT('.'));
	if (dot == wxNOT_FOUND)
		ibBackendCoreException::Error(_("GetBase: '%s' is not written as 'Register.Field'"), path);
	const wxString name = path.Left(dot);
	field = path.Mid(dot + 1);
	for (const ibValueMetaObjectCalculationRegister* related : reg->GetRelatedRegisters())
		if (stringUtils::CompareString(related->GetName(), name))
			return related;
	ibBackendCoreException::Error(_("GetBase: '%s' is not a register '%s' takes a base from"), name, reg->GetName());
	return nullptr;
}

template <typename T, typename List>
T* ibCalcNamed(const List& list, const wxString& name)
{
	for (T* one : list)
		if (one != nullptr && stringUtils::CompareString(one->GetName(), name))
			return one;
	return nullptr;
}

} // namespace

ibCalcBaseAsked ibCalcBaseAskedOf(const ibValueMetaObjectCalculationRegister* reg, const ibValue& resources,
	const ibValue& dimensions, const ibValue& sections)
{
	ibCalcBaseAsked asked;

	ibValueArray* figures = nullptr;
	if (!resources.ConvertToValue(figures) || figures == nullptr)
		ibBackendCoreException::Error(_("GetBase: the resources are an array of strings 'Register.Resource'"));
	for (unsigned int i = 0; i < figures->Count(); ++i) {
		ibValue item;
		figures->GetAt(ibValue(static_cast<int>(i)), item);
		ibCalcBaseAsked::ibFigure figure;
		for (const wxString& path : ibCalcBaseItemsOf(item.GetString())) {
			wxString name;
			const ibValueMetaObjectCalculationRegister* base = ibCalcBaseRegisterOf(reg, path, name);
			const ibValueMetaObjectResource* resource = ibCalcNamed<ibValueMetaObjectResource>(base->GetResourceArrayObject(), name);
			if (resource == nullptr)
				ibBackendCoreException::Error(_("GetBase: register '%s' has no resource '%s'"), base->GetName(), name);
			if (figure.m_name.IsEmpty())
				figure.m_name = resource->GetName();
			figure.m_resources.push_back({ base, resource });
		}
		if (!figure.m_resources.empty())
			asked.m_figures.push_back(std::move(figure));
	}

	ibValueStructure* pairs = nullptr;
	if (!dimensions.ConvertToValue(pairs) || pairs == nullptr)
		ibBackendCoreException::Error(_("GetBase: the dimensions are a structure of this register's dimensions"));
	for (long key = 0; key < pairs->GetNProps(); ++key) {
		ibCalcBaseAsked::ibPairing pairing;
		const wxString own = pairs->GetPropName(key);
		pairing.m_own = ibCalcNamed<ibValueMetaObjectDimension>(reg->GetDimensionArrayObject(), own);
		if (pairing.m_own == nullptr)
			ibBackendCoreException::Error(_("GetBase: '%s' is not a dimension of '%s'"), own, reg->GetName());
		ibValue value;
		pairs->GetPropVal(key, value);
		for (const wxString& path : ibCalcBaseItemsOf(value.GetString())) {
			wxString name;
			const ibValueMetaObjectCalculationRegister* base = ibCalcBaseRegisterOf(reg, path, name);
			const ibValueMetaObjectDimension* dimension = ibCalcNamed<ibValueMetaObjectDimension>(base->GetDimensionArrayObject(), name);
			if (dimension == nullptr)
				ibBackendCoreException::Error(_("GetBase: register '%s' has no dimension '%s'"), base->GetName(), name);
			pairing.m_base.push_back({ base, dimension });
		}
		asked.m_dimensions.push_back(std::move(pairing));
	}

	ibValueArray* breakdown = nullptr;
	if (sections.ConvertToValue(breakdown) && breakdown != nullptr) {
		for (unsigned int i = 0; i < breakdown->Count(); ++i) {
			ibValue item;
			breakdown->GetAt(ibValue(static_cast<int>(i)), item);
			wxString name;
			const ibValueMetaObjectCalculationRegister* base = ibCalcBaseRegisterOf(reg, item.GetString().Trim().Trim(false), name);
			const ibValueMetaObjectAttributeBase* field = ibCalcNamed<ibValueMetaObjectAttributeBase>(base->GetGenericAttributeArrayObject(), name);
			if (field == nullptr)
				ibBackendCoreException::Error(_("GetBase: register '%s' has no field '%s'"), base->GetName(), name);
			asked.m_sections.push_back({ base, field });
		}
	}
	return asked;
}

// ⭐ EACH SIDE READ ONCE, MATCHED HERE. The records asking come in hand (a record's GetBase: the one being written;
// the manager's: the recorder's, read by it); the base registers' records are read once for the span of their base
// periods — narrowed by every paired dimension the records agree on, so one record reads one employee — and meet
// the records here, by (type, paired values). A join into the fact has no index to ride (the records by the recorder
// were looked up per piece: 97 s where two reads took 20, 2026-09-14). The division stays here, in exact decimals: a
// NUMERIC divided in SQL keeps the scale of its operands and cuts at the cent on every piece.
ibValue ibCalcReadBase(const ibValueMetaObjectCalculationRegister* reg, const ibCalcBaseAsked& asked,
	const std::vector<ibCalcBaseRecord>& records)
{
	ibValueModelTable* table = new ibValueModelTable();
	const ibValue keep(table);   // held while its rows are made, as every builder of a table holds it
	ibValueModelTable::ibValueModelColumnCollection* columns = table->GetColumnCollection();
	const ibMetaID lineColumn = columns->AddColumn(wxT("LineNumber"), reg->GetRegisterLineNumber()->GetTypeDesc(),
		reg->GetRegisterLineNumber()->GetSynonym())->GetColumnID();
	std::vector<ibMetaID> figureColumns, sectionColumns;
	for (const ibCalcBaseAsked::ibFigure& figure : asked.m_figures)
		figureColumns.push_back(columns->AddColumn(figure.m_name, figure.m_resources.front().second->GetTypeDesc(),
			figure.m_resources.front().second->GetSynonym())->GetColumnID());
	for (const auto& section : asked.m_sections)
		sectionColumns.push_back(columns->AddColumn(section.second->GetName(), section.second->GetTypeDesc(),
			section.second->GetSynonym())->GetColumnID());

	// What each record gathers: its breakdowns (the section values, in the order first met) and a sum a figure each.
	struct ibGathered {
		std::unordered_map<std::vector<ibValue>, size_t, ibValueSeqHash, ibValueSeqEqual> m_at;
		std::vector<std::pair<std::vector<ibValue>, std::vector<ibNumber>>> m_rows;
		std::vector<ibNumber>& Row(const std::vector<ibValue>& sections, size_t figures) {
			const auto found = m_at.find(sections);
			if (found != m_at.end())
				return m_rows[found->second].second;
			m_at.emplace(sections, m_rows.size());
			m_rows.push_back({ sections, std::vector<ibNumber>(figures, ibNumber(0)) });
			return m_rows.back().second;
		}
	};
	std::vector<ibGathered> gathered(records.size());

	const ibValueMetaObjectChartOfCalculationTypes* chart = reg->GetChartOfCalculationTypes();
	const ibValueMetaObjectCalculationTypeRelationTable* feeds = chart != nullptr ? chart->GetBaseTable() : nullptr;
	const ibBaseDependence dependence = chart != nullptr ? chart->GetBaseDependence() : ibBaseDependence::eBaseNone;
	if (dependence == ibBaseDependence::eBaseNone)
		ibBackendCoreException::Error(_("Register '%s': its chart of calculation types takes no base - set the chart's 'Base dependence'"),
			reg->GetSynonym());

	// The records' base periods as days, and the span of them all. An empty base period (the year-1 date the
	// storage keeps for "none") is no span: that record takes no base. ⭐ A REGISTER KEEPING NO BASE PERIOD takes the
	// base over the period a record is registered in, at the register's periodicity (Max, 2026-09-14) — the month of
	// a monthly register, the same time the recalculation meets records by when neither keeps a period.
	const bool ownBasePeriod = reg->IsUseBasePeriod();
	const ibTotalsPeriod unit = reg->GetPeriodicityUnit();
	std::vector<std::pair<long long, long long>> spans(records.size(), { 0, -1 });
	wxDateTime windowFrom, windowTo;
	for (size_t i = 0; i < records.size(); ++i) {
		wxDateTime from = records[i].m_from, to = records[i].m_to;
		if (!ownBasePeriod && records[i].m_registration.IsValid()) {
			from = ibTruncateToPeriod(records[i].m_registration, unit);
			to = ibNextPeriodStart(from, unit) - wxDateSpan::Day();
		}
		if (!from.IsValid() || !to.IsValid() || from.GetYear() <= 1 || to < from)
			continue;
		spans[i] = { ibCalcCalendarDay(from), ibCalcCalendarDay(to) };
		if (!windowFrom.IsValid() || from < windowFrom) windowFrom = from;
		if (!windowTo.IsValid() || to > windowTo) windowTo = to;
	}

	if (feeds != nullptr && feeds->IsAllowed() && feeds->GetCalculationType() != nullptr && windowFrom.IsValid()) {
		// ---- which types a type takes its base from: the Base rows it owns ----------------------------------------
		std::unordered_map<std::vector<ibValue>, std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> namedBy;
		{
			ibDatabaseQueryBuilder q;
			q.From(ibScan(feeds->GetPhysicalTableName(), wxT("r")));
			std::vector<ibQueryProjItem> proj{ ibQueryProjItem{ ibCol(wxT("r"), ibOwnerRefField()), ibOwnerRefField() } };
			for (const wxString& field : ibRegFieldsOf(feeds->GetCalculationType()))
				proj.push_back(ibQueryProjItem{ ibCol(wxT("r"), field), field });
			q.Project(proj);
			ibQueryResult rs = q.Execute();
			while (rs.Next()) {
				wxMemoryBuffer bytes;
				rs.GetResultBlob(ibOwnerRefField(), bytes);
				if (bytes.GetDataLen() < sizeof(ibReference))
					continue;
				const ibReference* key = static_cast<const ibReference*>(bytes.GetData());
				const ibValue owner(ibValueReferenceDataObject::Create(chart, ibGuid(key->m_guid)));
				ibValue named;
				ibDbTableProvider::GetValueAttribute(feeds->GetCalculationType(), named, rs);
				namedBy[{ owner }].push_back(named);
			}
		}

		// ---- each base register named, read once ---------------------------------------------------------------
		std::vector<const ibValueMetaObjectCalculationRegister*> bases;
		for (const ibCalcBaseAsked::ibFigure& figure : asked.m_figures)
			for (const auto& resource : figure.m_resources)
				if (std::find(bases.begin(), bases.end(), resource.first) == bases.end())
					bases.push_back(resource.first);

		for (const ibValueMetaObjectCalculationRegister* base : bases) {
			const bool byAction = dependence == ibBaseDependence::eBaseByActionPeriod;
			if (byAction && !base->IsUseActionPeriod())
				ibBackendCoreException::Error(_("Register '%s' takes its base by action period, but the base register '%s' keeps no action periods"),
					reg->GetSynonym(), base->GetSynonym());

			// This register's pairings as they reach this base register, its sections, its resources a figure.
			std::vector<std::pair<size_t, const ibValueMetaObjectDimension*>> paired;   // (the pairing, its dimension here)
			for (size_t p = 0; p < asked.m_dimensions.size(); ++p)
				for (const auto& one : asked.m_dimensions[p].m_base)
					if (one.first == base)
						paired.push_back({ p, one.second });
			std::vector<const ibValueMetaObjectAttributeBase*> sectionFields(asked.m_sections.size(), nullptr);
			for (size_t s = 0; s < asked.m_sections.size(); ++s)
				if (asked.m_sections[s].first == base)
					sectionFields[s] = asked.m_sections[s].second;
			// By action period the rows are the fact's, and the fact carries a record's standard fields, dimensions and
			// resources — not its other attributes (a long string among them could not even be grouped). Said, rather
			// than left to the database as an unknown column.
			if (byAction) {
				const std::vector<wxString> carried = RecordFields(ibCalcViewSpecOf(base));
				for (const ibValueMetaObjectAttributeBase* field : sectionFields)
					if (field != nullptr)
						for (const wxString& physical : ibRegFieldsOf(field))
							if (std::find(carried.begin(), carried.end(), physical) == carried.end())
								ibBackendCoreException::Error(_("GetBase: register '%s' is read by action period, which carries no '%s' to break the base down by"),
									base->GetSynonym(), field->GetName());
			}
			std::vector<std::vector<const ibValueMetaObjectResource*>> figureResources(asked.m_figures.size());
			for (size_t c = 0; c < asked.m_figures.size(); ++c)
				for (const auto& resource : asked.m_figures[c].m_resources)
					if (resource.first == base)
						figureResources[c].push_back(resource.second);

			// A paired dimension every record holds the same value of narrows the read by it: one record reads its
			// own employee, a payroll the history of nobody else.
			std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> fixed;
			for (const auto& one : paired) {
				bool same = true;
				for (size_t i = 1; i < records.size() && same; ++i)
					same = records[i].m_dimensions[one.first].CompareValueLS(records.front().m_dimensions[one.first]) == 0;
				if (same && !records.empty())
					fixed.push_back({ one.second->GetQueryColumn(), records.front().m_dimensions[one.first] });
			}

			// What a row of the base register gives: its type and paired values (the key), its sections and figures,
			// and its days — the piece's by action period, the registration day by registration period.
			struct ibBaseItem { std::vector<ibValue> m_sections; std::vector<ibNumber> m_figures; long long m_first, m_last; std::vector<ibValue> m_record; };
			std::unordered_map<std::vector<ibValue>, std::vector<ibBaseItem>, ibValueSeqHash, ibValueSeqEqual> itemsOf;
			std::unordered_map<std::vector<ibValue>, long long, ibValueSeqHash, ibValueSeqEqual> daysOf;   // a record's days in force

			std::vector<ibQueryProjItem> proj;
			std::set<wxString> projected;
			const wxString alias = byAction ? wxT("f") : wxT("b");
			const auto fieldsOf = [&](const ibValueMetaObjectAttributeBase* attribute) {
				for (const wxString& field : ibRegFieldsOf(attribute))
					if (projected.insert(field).second)
						proj.push_back(ibQueryProjItem{ ibCol(alias, field), field });
			};
			fieldsOf(base->GetRegisterRecorder());
			fieldsOf(base->GetRegisterLineNumber());
			fieldsOf(base->GetCalculationType());
			for (const auto& one : paired)
				fieldsOf(one.second);
			for (const ibValueMetaObjectAttributeBase* field : sectionFields)
				if (field != nullptr)
					fieldsOf(field);
			for (const auto& resourcesOfFigure : figureResources)   // a resource is read as its number alone (the fact carries no more)
				for (const ibValueMetaObjectResource* resource : resourcesOfFigure)
					if (projected.insert(ibRegValueField(resource)).second)
						proj.push_back(ibQueryProjItem{ ibCol(alias, ibRegValueField(resource)), ibRegValueField(resource) });

			ibDatabaseQueryBuilder q;
			wxString firstField, lastField;
			if (byAction) {
				const ibCalcViewSpec spec = ibCalcViewSpecOf(base);
				const ibMetaData* baseMetaData = base->GetMetaData();
				const wxString startField = spec.m_start, endField = spec.m_end;
				const ibCalcNarrowing narrowing = [&fixed, baseMetaData, startField, endField, windowFrom, windowTo](const wxString& at, bool subject) {
					ibQueryExprPtr all;
					const auto also = [&all](const ibQueryExprPtr& term) {
						if (term)
							all = all ? ibBinOp(ibQueryBinOp::And, all, term) : term;
					};
					for (const auto& one : fixed)   // a dimension narrows every table
						also(ibRegCompositeIR(one.first, baseMetaData, one.second, ibQueryBinOp::Eq, at));
					if (subject) {
						also(ibBinOp(ibQueryBinOp::Le, ibCol(at, startField), ibConst(ibValue(windowTo))));
						also(ibBinOp(ibQueryBinOp::Ge, ibCol(at, endField), ibConst(ibValue(windowFrom))));
					}
					return all;
				};
				const ibQueryRelPtr fact = ibCalcFactRelation(spec, narrowing);
				if (!fact)
					continue;
				firstField = spec.m_start;
				lastField = spec.m_end;
				for (const wxString& field : { firstField, lastField })
					if (projected.insert(field).second)
						proj.push_back(ibQueryProjItem{ ibCol(alias, field), field });
				q.From(ibSubquery(fact, alias));
			}
			else {
				// By registration period: a record counts whole when it is registered inside the base period. An
				// inactive record is no base (the rule the fact keeps).
				firstField = lastField = ibRegFieldOfRole(base->GetRegistrationPeriod()->GetQueryColumn(), ibColumnRole::Date);
				if (projected.insert(firstField).second)
					proj.push_back(ibQueryProjItem{ ibCol(alias, firstField), firstField });
				q.From(ibScan(base->GetPhysicalTableName(), alias));
				q.Where(ibBinOp(ibQueryBinOp::Ge, ibCol(alias, firstField), ibConst(ibValue(windowFrom))));
				q.Where(ibBinOp(ibQueryBinOp::Le, ibCol(alias, firstField), ibConst(ibValue(windowTo))));
				q.Where(ibBinOp(ibQueryBinOp::Ne, ibCol(alias, ibRegValueField(base->GetRegisterActive())), ibConst(ibValue(0))));
				for (const auto& one : fixed)
					q.Where(ibRegCompositeIR(one.first, base->GetMetaData(), one.second, ibQueryBinOp::Eq, alias));
			}
			q.Project(proj);

			ibQueryResult rs = q.Execute();
			while (rs.Next()) {
				ibBaseItem item;
				std::vector<ibValue> key(1 + paired.size());
				ibDbTableProvider::GetValueAttribute(base->GetCalculationType(), key[0], rs);
				for (size_t k = 0; k < paired.size(); ++k)
					ibDbTableProvider::GetValueAttribute(paired[k].second, key[1 + k], rs);
				item.m_sections.resize(sectionFields.size());
				for (size_t s = 0; s < sectionFields.size(); ++s)
					if (sectionFields[s] != nullptr)
						ibDbTableProvider::GetValueAttribute(sectionFields[s], item.m_sections[s], rs);
				item.m_figures.resize(figureResources.size(), ibNumber(0));
				for (size_t c = 0; c < figureResources.size(); ++c)
					for (const ibValueMetaObjectResource* resource : figureResources[c])
						item.m_figures[c] += rs.GetResultNumber(ibRegValueField(resource));
				item.m_first = ibCalcCalendarDay(rs.GetResultDate(firstField));
				item.m_last = ibCalcCalendarDay(rs.GetResultDate(lastField));
				if (byAction) {
					ibValue recorder, line;
					ibDbTableProvider::GetValueAttribute(base->GetRegisterRecorder(), recorder, rs);
					ibDbTableProvider::GetValueAttribute(base->GetRegisterLineNumber(), line, rs);
					item.m_record = { recorder, line };
					daysOf[item.m_record] += item.m_last - item.m_first + 1;
				}
				itemsOf[key].push_back(std::move(item));
			}

			// ---- each record weighs the rows of its own values ---------------------------------------------------
			for (size_t i = 0; i < records.size(); ++i) {
				if (spans[i].second < spans[i].first)
					continue;
				const auto named = namedBy.find({ records[i].m_type });
				if (named == namedBy.end())
					continue;
				for (const ibValue& type : named->second) {
					std::vector<ibValue> key{ type };
					for (const auto& one : paired)
						key.push_back(records[i].m_dimensions[one.first]);
					const auto found = itemsOf.find(key);
					if (found == itemsOf.end())
						continue;
					for (const ibBaseItem& item : found->second) {
						ibNumber share(1);
						if (byAction) {
							const long long overlap = (std::min)(item.m_last, spans[i].second) - (std::max)(item.m_first, spans[i].first) + 1;
							const long long total = daysOf[item.m_record];
							if (overlap <= 0 || total <= 0)
								continue;
							share = ibNumber(overlap) / ibNumber(total);
						}
						else if (item.m_first < spans[i].first || item.m_first > spans[i].second)
							continue;
						std::vector<ibNumber>& sums = gathered[i].Row(item.m_sections, asked.m_figures.size());
						for (size_t c = 0; c < item.m_figures.size(); ++c)
							sums[c] += item.m_figures[c] * share;
					}
				}
			}
		}
	}

	// ---- one row a record — or a record and a section — in the records' order ---------------------------------------
	std::vector<std::pair<ibMetaID, ibValue>> row;
	for (size_t i = 0; i < records.size(); ++i) {
		if (gathered[i].m_rows.empty())
			gathered[i].Row(std::vector<ibValue>(asked.m_sections.size()), asked.m_figures.size());
		for (const auto& breakdown : gathered[i].m_rows) {
			row.clear();
			row.emplace_back(lineColumn, records[i].m_line);
			for (size_t c = 0; c < asked.m_figures.size(); ++c)
				row.emplace_back(figureColumns[c], asked.m_figures[c].m_resources.front().second->AdjustValue(ibValue(breakdown.second[c])));
			for (size_t s = 0; s < asked.m_sections.size(); ++s)
				row.emplace_back(sectionColumns[s], breakdown.first[s]);
			table->AppendRow(row);
		}
	}
	return table;
}
