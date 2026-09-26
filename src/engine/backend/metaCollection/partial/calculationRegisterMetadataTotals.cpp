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
#include "backend/query/queryRamTable.h"                             // the base: the table GetBase answers, filled fast
#include "backend/metaData.h"                                        // the schedule data: the schedule register, by its id
#include "backend/diagnostics/journal.h"                             // the schedule data: what each read brought, and how long

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

// ⭐⭐ IS THERE ANYTHING TO MEET AT ALL — the same meeting `Pairs` joins by, said as a condition about ONE record:
// a Displacing row owned by its type, an active record of the type that row names, the same dimension values, an
// action period that overlaps its own. A record no such record meets loses no day, so its pieces are its action
// period and the walk has nothing to work out for it.
//
// It is what makes the walk affordable on a real month: a payroll registers a salary for every employee and a leave
// for a few, and the types a Displacing section names are the rare ones. Asked for every salary, the walk sorted
// two marks per record plus the pairs — 40 028 records of which some six thousand could lose anything (a July of
// 40 000 employees, 2026-09-17).
//
// ⚠ CONSERVATIVE, and deliberately: the netting of a storno against its record (the `Pairs` HAVING) is not repeated
// here, so a displacer a storno takes back still counts as something to meet. Such a record goes through the walk,
// which works out that it keeps its days — one record read for nothing, never a day lost that was not.
ibQueryExprPtr MeetsDisplacer(const ibCalcViewSpec& v, const ibCalcNarrowing& narrowing, const wxString& alias)
{
	if (v.m_displacing.IsEmpty())
		return nullptr;
	const wxString row = wxT("dx"), other = wxT("dy");   // own aliases: the condition is read inside other queries
	const ibQueryExprPtr owned = And(Eq(ibCol(row, v.m_owner), ibCol(alias, v.m_typeId)),
		ibBinOp(ibQueryBinOp::Ne, ibCol(row, v.m_namedId), ibCol(row, v.m_owner)));
	ibQueryExprPtr on = SameFields(other, v.m_dimensions, alias);
	for (const std::pair<wxString, wxString>& named : v.m_named)
		on = And(on, Eq(ibCol(other, named.first), ibCol(row, named.second)));
	on = And(on, And(ibBinOp(ibQueryBinOp::Le, Day(other, v.m_start), Day(alias, v.m_end)),
		ibBinOp(ibQueryBinOp::Ge, Day(other, v.m_end), Day(alias, v.m_start))));

	const ibQueryRelPtr met = ibFilter(ibJoin(ibScan(v.m_displacing, row), ibScan(v.m_records, other), on),
		And(owned, And(And(Counts(v, other), WithDays(v, other)), Narrowed(narrowing, other, /*subject*/ false))));
	return ibExists(ibProject(met, { ibQueryProjItem{ Int(1), wxT("ib_met") } }));
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

namespace {

// A record, as the register lays one out (Max, 2026-09-14): the registration, the recorder and the line first, the
// position, the base period, Active, Storno, the dimensions. Every reading of the records publishes these first —
// the fact, the schedule data.
std::vector<const ibValueMetaObjectAttributeBase*> ibCalcRecordLayout(const ibValueMetaObjectCalculationRegister* reg)
{
	std::vector<const ibValueMetaObjectAttributeBase*> record = { reg->GetRegistrationPeriod(), reg->GetRegisterRecorder(),
		reg->GetRegisterLineNumber(), reg->GetCalculationType(), reg->GetActionPeriod(), reg->GetActionPeriodStart(), reg->GetActionPeriodEnd() };
	if (reg->IsUseBasePeriod()) {
		record.push_back(reg->GetBasePeriodStart());
		record.push_back(reg->GetBasePeriodEnd());
	}
	record.push_back(reg->GetRegisterActive());
	record.push_back(reg->GetStorno());
	for (const ibValueMetaObjectDimension* dimension : reg->GetDimensionArrayObject())
		record.push_back(dimension);
	return record;
}

} // namespace

// ⭐ A RECORD'S OWN COLUMNS ARE THE REGISTER'S ATTRIBUTES, published as themselves (ibRegAttributeColumn): the same
// names, types and ids as on the register, laid out as the register lays a record out (ibCalcRecordLayout), then
// the resources — so the fact is interchangeable with the register as a source. What it adds is numbered as a
// derived column.
const ibBackendQueryable* ibValueMetaObjectCalculationRegister::GetFactSurface() const
{
	const std::vector<const ibValueMetaObjectAttributeBase*> record = ibCalcRecordLayout(this);
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

namespace {

// ⭐⭐ WHAT A READING OF THE RECORDS NARROWS ITS TABLES BY — the fact and the schedule data alike, from the same four
// arguments (ibCalcViewArg) and the WHERE around the reading (`extra`). The reading's conditions on the subject's own
// fields — whichever way they were written — become the narrowing of the tables the relation reads: a dimension
// narrows every one of them by equality, the rest of the subject the subject alone (calculationRegister.h says
// why), a date by a range as well. A narrowing only ever keeps MORE than the condition: each reading applies the
// whole condition itself afterwards. `pieces` says whether the reading's two action days are a piece's (the fact)
// or the record's own (the schedule data).
ibCalcNarrowing ibCalcNarrowingOf(const ibValueMetaObjectCalculationRegister* reg, const ibCalcViewSpec& spec, bool pieces,
	const std::vector<ibQueryCondition>& extra, const ibQueryPredicatePtr& condition,
	const ibValue& moment, const ibValue& actionFrom, const ibValue& actionTo)
{
	// A condition reaches here on the SURFACE's column, which carries the attribute's own id. What it may narrow,
	// and how:
	//   Everywhere — a dimension: every table, by equality;
	//   Subject    — the rest of the record's own: the records alone, by equality — and a DATE of its own by a
	//                range too, as written (the month; the registration);
	//   PieceStart / PieceEnd — the fact's two days, a PIECE's bounds and not the record's: a range on them
	//                narrows the record by what it implies, a piece lying inside its record — a piece beginning
	//                by X is of a record beginning by X, a piece ending from X of a record ending from X, a piece
	//                beginning from X of a record ending from X, a piece ending by X of a record beginning by X.
	//                Where the two days are the record's own (`pieces` false), they are the subject's, as its month is.
	enum class Reach { Everywhere, Subject, PieceStart, PieceEnd };
	struct Narrows { const ibValueMetaObjectAttributeBase* m_attribute; Reach m_reach; bool m_dated; };
	std::map<ibMetaID, Narrows> narrows;
	for (const ibValueMetaObjectDimension* dimension : reg->GetDimensionArrayObject())
		narrows[dimension->GetMetaID()] = { dimension, Reach::Everywhere, false };
	narrows[reg->GetCalculationType()->GetMetaID()] = { reg->GetCalculationType(), Reach::Subject, false };
	narrows[reg->GetActionPeriod()->GetMetaID()] = { reg->GetActionPeriod(), Reach::Subject, true };
	narrows[reg->GetActionPeriodStart()->GetMetaID()] = { reg->GetActionPeriodStart(), pieces ? Reach::PieceStart : Reach::Subject, true };
	narrows[reg->GetActionPeriodEnd()->GetMetaID()] = { reg->GetActionPeriodEnd(), pieces ? Reach::PieceEnd : Reach::Subject, true };
	// …and what makes a row a record's: a payroll asks for its own records' days by the recorder.
	narrows[reg->GetRegisterRecorder()->GetMetaID()] = { reg->GetRegisterRecorder(), Reach::Subject, false };
	narrows[reg->GetRegisterLineNumber()->GetMetaID()] = { reg->GetRegisterLineNumber(), Reach::Subject, false };
	narrows[reg->GetRegistrationPeriod()->GetMetaID()] = { reg->GetRegistrationPeriod(), Reach::Subject, true };

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
	const ibBackendQueryColumn* recordStart = reg->GetActionPeriodStart()->GetQueryColumn();
	const ibBackendQueryColumn* recordEnd = reg->GetActionPeriodEnd()->GetQueryColumn();

	// FOR WHICH DAYS OF ACTION (ibCalcViewArg): the subject's own action period meets them — it ends on
	// BeginOfActionPeriod or later and begins on EndOfActionPeriod or earlier.
	const auto dated = [](const ibValue& v) { return v.GetType() == TYPE_DATE && v.GetDateTime().IsValid(); };
	if (dated(actionFrom))
		terms.push_back({ recordEnd, actionFrom, ibQueryBinOp::Ge, false });
	if (dated(actionTo))
		terms.push_back({ recordStart, actionTo, ibQueryBinOp::Le, false });

	// What narrows the tables: the conditions of the WHERE around the reading (`extra`), and the plain AND of the
	// one written into its parentheses — the whole of which the reading applies itself afterwards.
	std::vector<ibQueryCondition> narrowedBy = extra;
	std::function<void(const ibQueryPredicatePtr&)> conjuncts = [&](const ibQueryPredicatePtr& node) {
		if (!node)
			return;
		if (node->m_kind == ibQueryPredicateKind::And)
			for (const ibQueryPredicatePtr& child : node->m_children)
				conjuncts(child);
		else if (node->m_kind == ibQueryPredicateKind::Leaf)
			narrowedBy.push_back(node->m_leaf);
	};
	conjuncts(condition);

	for (const ibQueryCondition& c : narrowedBy) {
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
	ibQueryExprPtr before;
	if (dated(moment))
		before = ibConst(ibValue(ibNextPeriodStart(moment.GetDateTime(), reg->GetPeriodicityUnit())));

	if (terms.empty() && !before)
		return nullptr;
	const ibMetaData* metaData = reg->GetMetaData();
	const wxString period = spec.m_period;
	return [terms, before, period, metaData](const wxString& alias, bool subject) {
		ibQueryExprPtr all;
		if (before)
			all = ibBinOp(ibQueryBinOp::Lt, ibCol(alias, period), before);
		for (const Term& one : terms)
			if (one.m_everywhere || subject)
				if (const ibQueryExprPtr term = ibRegCompositeIR(one.m_col, metaData, one.m_value, one.m_op, alias))
					all = all ? ibBinOp(ibQueryBinOp::And, all, term) : term;
		return all;
	};
}

} // namespace

// ⭐⭐ THE ROWS, FROM THE DATABASE, NARROWED INSIDE (ibCalcNarrowingOf). A payroll asking for one employee, for one
// type in one month, or for what is in force between two days has the database walk those and no others; the
// provider applies every condition over the rows afterwards.
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

	const ibCalcViewSpec spec = ibCalcViewSpecOf(m_reg);
	const ibMetaData* metaData = m_reg->GetMetaData();
	const ibCalcNarrowing narrowing = ibCalcNarrowingOf(m_reg, spec, /*pieces*/ true, extra, m_condition, m_moment, m_actionFrom, m_actionTo);
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
	// ⭐ THE CONDITION OF THE PARENTHESES, WHOLE, OVER THE PIECES. The narrowing above rides what an index can —
	// the plain AND, by what it implies of a record; this is what the author wrote, on the columns the table
	// shows (a piece's own days among them), NOT and OR and a walk included. It cannot be left to the WHERE
	// around the table: nothing puts it there any more.
	if (m_condition)
		if (const ibQueryExprPtr exact = ibDbTableProvider::BuildPredicateIR(surface,
				ibRegConditionOn(surface, m_condition, [](const ibBackendQueryColumn*) { return true; }), wxT("v")))
			q.Where(exact);
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

const ibBackendQueryable* ibCalcFactSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	return CreateQueryable(paParams, lSizeArray, {}, ibQueryReadColumns());
}

// What a query's condition is resolved against: the surface the reading publishes — `ActionPeriodStart` there is a
// PIECE's first day, and the condition means what the table shows.
const ibBackendQueryable* ibCalcFactSourceDescriptor::GetConditionScope() const
{
	return m_meta != nullptr ? m_meta->GetFactSurface() : nullptr;
}

// Built and KEPT by the base — the same call gives the same object back (queryableFactory.h, MakeCompanion); the
// condition is part of the call.
const ibBackendQueryable* ibCalcFactSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray,
	const std::vector<ibQueryPredicatePtr>& conditions, const ibQueryReadColumns& /*read*/)
{
	if (!m_meta->IsUseActionPeriod())
		return nullptr;
	return MakeCompanionFor<ibCalcFactQueryable>(conditions, paParams, lSizeArray, m_meta,
		ibRegArg(paParams, lSizeArray, ibCalcViewArg::Period),
		ibRegArg(paParams, lSizeArray, ibCalcViewArg::BeginOfActionPeriod), ibRegArg(paParams, lSizeArray, ibCalcViewArg::EndOfActionPeriod),
		ibRegConsumedCondition(conditions, ibCalcViewArg::Condition));
}

// FOUR ARGUMENTS, ALL OPTIONAL — AS OF WHICH MOMENT OF REGISTRATION (Period), FOR WHICH DAYS OF ACTION, THEN THE CONDITION
// (ibCalcViewArg): `ActualActionPeriod(, &MonthStart, &MonthEnd, Employee = &Employee)`. Each time is typed by the
// register's own attribute of that period. The condition is CONSUMED: the reading narrows every table it reads by
// what the condition says of the records, and applies the whole of it over the pieces (ComputeRows).
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
	// The fast table the answer is filled into (ibQueryRamTable::ToValueTable loads it), its columns numbered in the
	// order they stand: the line, a figure each, a section each.
	ibQueryRamTable table;
	const ibMetaID lineColumn = 1;
	table.AddColumn(lineColumn, wxT("LineNumber"), reg->GetRegisterLineNumber()->GetTypeDesc(),
		reg->GetRegisterLineNumber()->GetSynonym());
	std::vector<ibMetaID> figureColumns, sectionColumns;
	for (const ibCalcBaseAsked::ibFigure& figure : asked.m_figures) {
		figureColumns.push_back(static_cast<ibMetaID>(table.Columns().size() + 1));
		table.AddColumn(figureColumns.back(), figure.m_name, figure.m_resources.front().second->GetTypeDesc(),
			figure.m_resources.front().second->GetSynonym());
	}
	for (const auto& section : asked.m_sections) {
		sectionColumns.push_back(static_cast<ibMetaID>(table.Columns().size() + 1));
		table.AddColumn(sectionColumns.back(), section.second->GetName(), section.second->GetTypeDesc(),
			section.second->GetSynonym());
	}

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
	for (size_t i = 0; i < records.size(); ++i) {
		if (gathered[i].m_rows.empty())
			gathered[i].Row(std::vector<ibValue>(asked.m_sections.size()), asked.m_figures.size());
		for (const auto& breakdown : gathered[i].m_rows) {
			const long at = table.AppendRow();
			table.SetCell(at, lineColumn, records[i].m_line);
			for (size_t c = 0; c < asked.m_figures.size(); ++c)
				table.SetCell(at, figureColumns[c], ibValue(breakdown.second[c]));   // to the figure's type as it is loaded
			for (size_t s = 0; s < asked.m_sections.size(); ++s)
				table.SetCell(at, sectionColumns[s], breakdown.first[s]);
		}
	}
	return table.ToValueTable();
}

// ============================================================================
// The schedule data — ScheduleData
// ============================================================================

namespace {

static const wxChar* const ibCalcScheduleDataName = wxT("ScheduleData");

// The four periods a schedule is summed over, in the order the columns stand, each numbered for its column id.
enum ibCalcSchedulePeriod {
	ibCalcSchedulePeriod_Action = 1,
	ibCalcSchedulePeriod_ActualAction,
	ibCalcSchedulePeriod_Base,
	ibCalcSchedulePeriod_Registration,
};

// `<resource><period>` — the period's part of a column's name.
wxString ibCalcSchedulePeriodName(ibCalcSchedulePeriod period)
{
	switch (period) {
	case ibCalcSchedulePeriod_Action:       return wxT("ActionPeriod");
	case ibCalcSchedulePeriod_ActualAction: return wxT("ActualActionPeriod");
	case ibCalcSchedulePeriod_Base:         return wxT("BasePeriod");
	case ibCalcSchedulePeriod_Registration: return wxT("RegistrationPeriod");
	}
	return wxEmptyString;
}

// The same period, said to a person — the caption twin of the name (ibRegFigureCaption).
wxString ibCalcSchedulePeriodCaption(ibCalcSchedulePeriod period)
{
	switch (period) {
	case ibCalcSchedulePeriod_Action:       return _("Action period");
	case ibCalcSchedulePeriod_ActualAction: return _("Actual action period");
	case ibCalcSchedulePeriod_Base:         return _("Base period");
	case ibCalcSchedulePeriod_Registration: return _("Registration period");
	}
	return wxEmptyString;
}

// The periods this register has: the base period only where its records carry one.
std::vector<ibCalcSchedulePeriod> ibCalcSchedulePeriods(const ibValueMetaObjectCalculationRegister* reg)
{
	std::vector<ibCalcSchedulePeriod> periods = { ibCalcSchedulePeriod_Action, ibCalcSchedulePeriod_ActualAction };
	if (reg->IsUseBasePeriod())
		periods.push_back(ibCalcSchedulePeriod_Base);
	periods.push_back(ibCalcSchedulePeriod_Registration);
	return periods;
}

// A record's own columns: the record as the register lays it out (ibCalcRecordLayout), its resources, its attributes.
std::vector<const ibValueMetaObjectAttributeBase*> ibCalcScheduleRecordAttributes(const ibValueMetaObjectCalculationRegister* reg)
{
	std::vector<const ibValueMetaObjectAttributeBase*> record = ibCalcRecordLayout(reg);
	for (const ibValueMetaObjectResource* resource : reg->GetResourceArrayObject())
		record.push_back(resource);
	for (const ibValueMetaObjectAttributeBase* attribute : reg->GetAttributeArrayObject())
		record.push_back(attribute);
	return record;
}

// The schedule register a register is bound to, or none.
const ibValueMetaObjectRegisterData* ibCalcScheduleRegister(const ibValueMetaObjectCalculationRegister* reg)
{
	const ibCalcScheduleDescription& scheduleDesc = reg->GetScheduleDesc();
	const ibMetaData* metaData = reg->GetMetaData();
	if (!scheduleDesc.IsOk() || metaData == nullptr)
		return nullptr;
	const ibValueMetaObjectRegisterData* schedule = metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(scheduleDesc.GetRegister());
	return schedule != nullptr && !schedule->IsDeleted() ? schedule : nullptr;
}

// What is summed: every resource of the schedule that holds a number.
std::vector<const ibValueMetaObjectResource*> ibCalcScheduleResources(const ibValueMetaObjectRegisterData* schedule)
{
	std::vector<const ibValueMetaObjectResource*> resources;
	if (schedule != nullptr)
		for (const ibValueMetaObjectResource* resource : schedule->GetResourceArrayObject())
			if (!resource->IsDeleted() && resource->GetTypeDesc().ContainType(g_valueNumberCLSID))
				resources.push_back(resource);
	return resources;
}

// ⭐⭐ WHICH OF THE SUMS THE QUERY READS — by the names the surface publishes them under, `<Resource><Period>`,
// matched against what the text asks of this table (ibQueryReadColumns: every clause of it, the condition and the
// order included). A PERIOD NOBODY NAMED IS NOT SUMMED AT ALL, and that is most of the work: the days of a period
// are read from the schedule for every record, and the periods of one record lie far apart — a July salary is in
// force in July, registered in July and based on March through June, so summing all of them read a hundred and
// fifty days per record where the report showed thirty-one. On a July of 40 000 employees the sums were 46 s of a
// 70 s reading in Debug, for columns the report never had (2026-09-17).
std::vector<wxString> ibCalcScheduleAskedSums(const ibValueMetaObjectCalculationRegister* reg, const ibQueryReadColumns& read)
{
	std::vector<wxString> asked;
	for (const ibValueMetaObjectResource* resource : ibCalcScheduleResources(ibCalcScheduleRegister(reg)))
		for (const ibCalcSchedulePeriod period : ibCalcSchedulePeriods(reg)) {
			const wxString name = resource->GetName() + ibCalcSchedulePeriodName(period);
			if (read.Reads(name))
				asked.push_back(name);
		}
	return asked;
}

}

// ⭐ THE SHAPE SCHEDULEDATA PUBLISHES — a record's own columns (ibRegAttributeColumn: the register's names, types
// and ids) and, for every numeric resource of the schedule, a sum of it over each of the record's periods, named
// `<Resource><Period>` and numbered as derived columns of that resource.
const ibBackendQueryable* ibValueMetaObjectCalculationRegister::GetScheduleDataSurface() const
{
	const std::vector<const ibValueMetaObjectAttributeBase*> record = ibCalcScheduleRecordAttributes(this);
	const std::vector<const ibValueMetaObjectResource*> resources = ibCalcScheduleResources(ibCalcScheduleRegister(this));
	const std::vector<ibCalcSchedulePeriod> periods = ibCalcSchedulePeriods(this);

	wxString shape;
	for (const ibValueMetaObjectAttributeBase* attribute : record)
		ibRegSignAttribute(shape, attribute);
	for (const ibValueMetaObjectResource* resource : resources)
		ibRegSignAttribute(shape, resource);

	return m_surfaces.Obtain(ibCalcScheduleDataName, shape, GetPhysicalTableName() + wxT("_") + ibCalcScheduleDataName, GetMetaData(),
		[&](std::vector<ibTempColumn>& columns)
	{
		for (const ibValueMetaObjectAttributeBase* attribute : record)
			columns.push_back(ibRegAttributeColumn(attribute));
		for (const ibValueMetaObjectResource* resource : resources) {
			for (const ibCalcSchedulePeriod period : periods) {
				const wxString name = resource->GetName() + ibCalcSchedulePeriodName(period);
				columns.push_back(ibTempColumn(name, name, resource->GetTypeDesc(), ibRegDerivedColumnId(resource->GetMetaID(), period),
					ibRegColumnCaptionOf(resource->GetSynonym(), ibCalcSchedulePeriodCaption(period)),
					ibBackendQueryColumn::Kind::Computed, resource->GetColumnIcon()));
			}
		}
	});
}

std::vector<const ibBackendQueryColumn*> ibCalcScheduleDataQueryable::GetPrimaryKeyColumns() const
{
	std::vector<const ibBackendQueryColumn*> out;
	const ibBackendQueryable* surface = NavigationSource();
	for (const wxString& name : { m_reg->GetRegisterRecorder()->GetName(), m_reg->GetRegisterLineNumber()->GetName() })
		if (const ibBackendQueryColumn* column = surface != nullptr ? surface->ResolveColumnByName(name) : nullptr)
			out.push_back(column);
	return out;
}

// ⭐⭐ THE ROWS: three reads and a walk. The records (active ones — an inactive record counts for nothing), their
// actual pieces (the fact's relation, the same displacement ActualActionPeriod reads), and the schedule's rows over
// the days any of the records spans. The schedule is laid out per link key, day by day and summed from its first
// day, so each of a record's periods is two lookups. A dimension of the reading narrows every read where the
// WHERE around it says so by equality; the provider applies every condition over the rows afterwards.
ibQueryRamTable ibCalcScheduleDataQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable out;
	const ibBackendQueryable* surface = NavigationSource();
	if (surface == nullptr)
		return out;
	for (const ibBackendQueryColumn* column : surface->GetColumns())
		out.AddColumn(column->GetColumnId(), column->GetName(), column->GetTypeDesc());

	const ibMetaData* metaData = m_reg->GetMetaData();
	const ibCalcScheduleDescription& scheduleDesc = m_reg->GetScheduleDesc();
	const ibValueMetaObjectRegisterData* schedule = ibCalcScheduleRegister(m_reg);
	const ibValueMetaObjectAttributeBase* scheduleDate = schedule != nullptr
		? metaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(scheduleDesc.GetDate(), true) : nullptr;
	if (!m_reg->IsUseActionPeriod() || schedule == nullptr || scheduleDate == nullptr)
		return out;

	const std::vector<const ibValueMetaObjectAttributeBase*> record = ibCalcScheduleRecordAttributes(m_reg);
	const std::vector<const ibValueMetaObjectResource*> resources = ibCalcScheduleResources(schedule);
	const std::vector<ibCalcSchedulePeriod> periods = ibCalcSchedulePeriods(m_reg);

	// The sums this reading was asked for — and so the periods whose days it has to read at all
	// (ibCalcScheduleAskedSums). A period nobody named is summed for nobody: its column stays at zero and the
	// schedule is never opened over its days. The actual action period is what displacement leaves of the action
	// period, and a record of a type nothing displaces keeps it whole — so asking for one asks for the other.
	const std::vector<wxString> asked = ibCalcScheduleAskedSums(m_reg, m_read);
	const auto readsPeriod = [&asked, &resources](ibCalcSchedulePeriod period) {
		const wxString suffix = ibCalcSchedulePeriodName(period);
		for (const ibValueMetaObjectResource* resource : resources)
			if (std::find(asked.begin(), asked.end(), resource->GetName() + suffix) != asked.end())
				return true;
		return false;
	};
	const bool readsActual       = readsPeriod(ibCalcSchedulePeriod_ActualAction);
	const bool readsAction       = readsPeriod(ibCalcSchedulePeriod_Action) || readsActual;
	const bool readsBase         = readsPeriod(ibCalcSchedulePeriod_Base);
	const bool readsRegistration = readsPeriod(ibCalcSchedulePeriod_Registration);

	const auto placeOf = [&record](const ibValueMetaObjectAttributeBase* attribute) -> size_t {
		for (size_t i = 0; i < record.size(); ++i)
			if (record[i] == attribute)
				return i;
		return record.size();
	};
	const size_t recorderAt = placeOf(m_reg->GetRegisterRecorder()), lineAt = placeOf(m_reg->GetRegisterLineNumber());

	// ---- the narrowing, as the fact narrows (ibCalcNarrowingOf) — the two action days here the record's own
	const ibCalcViewSpec spec = ibCalcViewSpecOf(m_reg);
	const ibCalcNarrowing narrowing = ibCalcNarrowingOf(m_reg, spec, /*pieces*/ false, extra, m_condition, m_moment, m_actionFrom, m_actionTo);

	// ---- the records --------------------------------------------------------------------------------------------
	ibJournalStopwatch readRecords, readSums, lay;
	readRecords.Resume();
	const wxString r = wxT("r");
	// Which records: active, inside the narrowing, and meeting the condition of the parentheses WHOLE — NOT and OR
	// and a walk included. It is written on the record's columns; a sum of the schedule is not something a record
	// can be selected by, and is refused. Asked again by every sum below, over the same alias.
	const auto recordsWhere = [this, &spec](const ibCalcNarrowing& by, const wxString& alias) {
		ibQueryExprPtr where = Counts(spec, alias);
		if (by)
			if (const ibQueryExprPtr narrowed = by(alias, true))
				where = ibBinOp(ibQueryBinOp::And, where, narrowed);
		if (m_condition)
			if (const ibQueryExprPtr exact = ibDbTableProvider::BuildPredicateIR(m_reg->GetQueryable(),
					ibRegConditionOn(m_reg->GetQueryable(), m_condition,
						[this](const ibBackendQueryColumn* column) { return m_reg->FindAnyAttributeObjectByFilter(column->GetColumnId()) != nullptr; }),
					alias))
				where = ibBinOp(ibQueryBinOp::And, where, exact);
		return where;
	};
	// ⭐ AND A RECORD IS BUILT OF THE COLUMNS THAT ANSWER SOMEBODY. The ones the text names (ibQueryReadColumns), and
	// the ones the reading itself goes by: the recorder and the line a summed row comes back to, the dimensions it
	// narrows every table with, the fields the schedule is linked through, the bounds of the periods it sums. A
	// register carries its figures and its attributes beside those, and making a value of each for 86 280 records was
	// 10 s of a 57 s reading in Debug (2026-09-17). A column nobody reads stays out of the row it would fill.
	std::vector<bool> needed(record.size(), m_read.m_all);
	{
		const auto need = [&needed, &placeOf, &record](const ibValueMetaObjectAttributeBase* field) {
			const size_t at = field != nullptr ? placeOf(field) : record.size();
			if (at < record.size())
				needed[at] = true;
		};
		need(m_reg->GetRegisterRecorder());
		need(m_reg->GetRegisterLineNumber());
		for (const ibValueMetaObjectDimension* dimension : m_reg->GetDimensionArrayObject())
			need(dimension);
		for (const ibCalcScheduleLink& link : scheduleDesc.GetLinks())
			for (const ibValueMetaObjectAttributeBase* field : record)
				if (field->GetMetaID() == link.m_field)
					need(field);
		if (readsAction) {
			need(m_reg->GetActionPeriodStart());
			need(m_reg->GetActionPeriodEnd());
		}
		if (readsBase && m_reg->IsUseBasePeriod()) {
			need(m_reg->GetBasePeriodStart());
			need(m_reg->GetBasePeriodEnd());
		}
		if (readsRegistration)
			need(m_reg->GetRegistrationPeriod());
		for (size_t at = 0; at < record.size(); ++at)
			if (m_read.Reads(record[at]->GetName()))
				needed[at] = true;
	}

	std::vector<std::vector<ibValue>> records;   // each in the order of ibCalcScheduleRecordAttributes
	{
		std::vector<ibQueryProjItem> proj;
		std::set<wxString> projected;
		for (size_t at = 0; at < record.size(); ++at)
			if (needed[at])
				for (const wxString& field : ColumnFieldNames(record[at]->GetQueryColumn()))
					if (projected.insert(field).second)
						proj.push_back(ibQueryProjItem{ ibCol(r, field), field });

		ibDatabaseQueryBuilder q;
		q.From(ibScan(spec.m_records, r));
		q.Project(proj);
		q.Where(recordsWhere(narrowing, r));
		ibQueryResult rs = q.Execute();
		while (rs.Next()) {
			std::vector<ibValue> one(record.size());
			for (size_t at = 0; at < record.size(); ++at)
				if (needed[at]) {
					const ibBackendQueryColumn* column = record[at]->GetQueryColumn();
					column->ReadValue(column->GetPhysicalName(), metaData, one[at], rs);
				}
			records.push_back(std::move(one));
		}
	}
	if (records.empty())
		return out;

	readRecords.Pause();

	// ---- what the records share -------------------------------------------------------------------------------------
	// A DIMENSION EVERY RECORD READ HOLDS THE SAME VALUE OF narrows every table the sums read, the displacers' among
	// them: displacement and the schedule both go by equal dimensions. A condition written through a walk
	// (`Employee.Code = …`) narrows nothing by itself — the fact then walked every record of the register for one
	// employee's pieces, 28 s in Release on a 40 000-employee copy (2026-09-17). Once the records are read, what they
	// share is a plain equality the index rides.
	ibCalcNarrowing narrowed = narrowing;
	{
		std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> shared;
		for (const ibValueMetaObjectDimension* dimension : m_reg->GetDimensionArrayObject()) {
			const size_t at = placeOf(dimension);
			if (at >= record.size())
				continue;
			const ibValue& value = records.front()[at];
			if (std::all_of(records.begin(), records.end(),
					[at, &value](const std::vector<ibValue>& rec) { return rec[at].CompareValueLS(value) == 0; }))
				shared.emplace_back(dimension->GetQueryColumn(), value);
		}
		if (!shared.empty())
			narrowed = [narrowing, shared, metaData](const wxString& alias, bool subject) {
				ibQueryExprPtr all = narrowing ? narrowing(alias, subject) : nullptr;
				for (const auto& one : shared)
					if (const ibQueryExprPtr term = ibRegCompositeIR(one.first, metaData, one.second, ibQueryBinOp::Eq, alias))
						all = all ? ibBinOp(ibQueryBinOp::And, all, term) : term;
				return all;
			};
	}

	// ---- the sums, where the schedule is -------------------------------------------------------------------------------
	// ⭐⭐ EACH PERIOD'S SUMS COME FROM THE DATABASE, ONE ROW A RECORD. The records (or, for the actual action period,
	// the fact's pieces) are joined to the schedule by the links and by the days the period holds, grouped by the
	// record and summed there. The schedule's rows never leave the database: they used to come over whole — 6.1
	// million of them for a July of 40 000 employees, and building a value of each was most of the reading's
	// 57.6 s in Release (2026-09-17). The rule the sums keep is stated plainly in the calculation kernel
	// (ibScheduleSeries).
	readSums.Resume();
	const wxString s = wxT("s");
	std::map<std::pair<ibValue, ibValue>, std::vector<size_t>> recordAt;   // a record by its recorder and its line
	for (size_t i = 0; i < records.size(); ++i)
		recordAt[std::make_pair(records[i][recorderAt], records[i][lineAt])].push_back(i);
	std::vector<std::vector<ibNumber>> sums(records.size(), std::vector<ibNumber>(periods.size() * resources.size()));

	// The links: a record's field and the schedule dimension it answers for.
	std::vector<std::pair<const ibBackendQueryColumn*, const ibBackendQueryColumn*>> links;
	for (const ibCalcScheduleLink& link : scheduleDesc.GetLinks()) {
		const ibValueMetaObjectAttributeBase* dimension = metaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(link.m_dimension, true);
		const size_t at = std::find_if(record.begin(), record.end(),
			[&link](const ibValueMetaObjectAttributeBase* one) { return one->GetMetaID() == link.m_field; }) - record.begin();
		if (dimension != nullptr && at < record.size())
			links.emplace_back(record[at]->GetQueryColumn(), dimension->GetQueryColumn());
	}
	const wxString scheduleDay = ibRegFieldOfRole(scheduleDate->GetQueryColumn(), ibColumnRole::Date);

	// ⭐ THE SCHEDULE'S DATE AS ITS INDEX KNOWS IT: its type tag and its value. The register's index runs over the
	// dimensions' fields in order, the date's tag before its value, so a range on the value alone is no bound —
	// every row of the key is read. The first run of these sums left the tag out and read all 153 days of an
	// employee for every record, three times over: 162 s for a July of 40 000 employees in Release, against 57.6 s
	// for bringing the rows home (2026-09-17). The tag is said the way a comparison to a value says it
	// (ibRegCompositeIR), with the earliest day any record reads as that value.
	wxDateTime earliest;
	const auto widen = [&](const ibValueMetaObjectAttributeBase* field) {
		const size_t at = placeOf(field);
		if (at >= record.size())
			return;
		for (const std::vector<ibValue>& one : records)
			if (!one[at].IsEmpty() && (!earliest.IsValid() || one[at].GetDateTime().IsEarlierThan(earliest)))
				earliest = one[at].GetDateTime();
	};
	if (readsAction)
		widen(m_reg->GetActionPeriodStart());
	if (readsRegistration)
		widen(m_reg->GetRegistrationPeriod());
	if (readsBase && m_reg->IsUseBasePeriod())
		widen(m_reg->GetBasePeriodStart());
	const ibQueryExprPtr tagged = earliest.IsValid()
		? ibRegCompositeIR(scheduleDate->GetQueryColumn(), metaData, ibValue(earliest.GetDateOnly()), ibQueryBinOp::Ge, s) : nullptr;

	long sumRows = 0;
	// One period a query sums over: the days [from, next) a row of the source names.
	struct Span { ibCalcSchedulePeriod m_period; ibQueryExprPtr m_from, m_next; };
	const auto inside = [&s, &scheduleDay](const Span& span) {
		return ibBinOp(ibQueryBinOp::And,
			ibBinOp(ibQueryBinOp::Ge, ibCol(s, scheduleDay), span.m_from),
			ibBinOp(ibQueryBinOp::Lt, ibCol(s, scheduleDay), span.m_next));
	};
	const auto least = [](ibQueryExprPtr a, ibQueryExprPtr b) { return ibCase({ { ibBinOp(ibQueryBinOp::Lt, a, b), a } }, b); };
	const auto greatest = [](ibQueryExprPtr a, ibQueryExprPtr b) { return ibCase({ { ibBinOp(ibQueryBinOp::Gt, a, b), a } }, b); };

	// ⭐ ALL THE PERIODS OF A ROW IN ONE PASS: the schedule rows from the earliest day any of them holds to the last,
	// read once, and each period's sum taken out of them by a CASE. A salary's action, base and registration periods
	// are one month: read three times, its 31 days were 93 (the three queries took 41 s of 89 in Debug on a July of
	// 40 000 employees, 2026-09-17).
	//
	// ⭐⭐ …AND ONE QUESTION IS ASKED ONCE. What the schedule answers depends on the KEY and the DAYS, never on which
	// record is asking: an employee's salary and his bonus are in force over the same July, and the sum of his hours
	// over it is one number. So a pass may be grouped by the key the schedule answers by — the links and the period's
	// own bounds — instead of by the record, with the records that asked the same question folded into one row before
	// the schedule is opened at all (`fold`, `by`, `whom`). On a July of 40 000 employees the 86 280 records ask
	// 46 240 distinct questions about their action period (2026-09-17). A pass over the PIECES is grouped by the
	// record: a piece belongs to its record, and two records with the same pieces are not the same question — asking
	// the walk twice would cost more than it saves.
	const auto sumOver = [&](ibQueryRelPtr source, const wxString& alias, ibQueryExprPtr where, const std::vector<Span>& spans,
			const std::vector<const ibBackendQueryColumn*>& by, bool fold,
			const std::function<const std::vector<size_t>*(const std::vector<ibValue>&)>& whom) {
		if (spans.empty() || by.empty() || std::find(by.begin(), by.end(), nullptr) != by.end())
			return;
		// Timed on its own: the two passes read different tables over different days, and which of them a month
		// costs is not something to guess at (2026-09-17).
		ibJournalStopwatch pass;
		pass.Resume();
		long passRows = 0;
		// the links first and the date's tag, then its range — the order of the schedule register's index
		ibQueryExprPtr on;
		for (const auto& link : links)
			if (const ibQueryExprPtr same = ibRegSameValueIR(link.first, alias, link.second, s))
				on = on ? ibBinOp(ibQueryBinOp::And, on, same) : same;
		if (tagged)
			on = on ? ibBinOp(ibQueryBinOp::And, on, tagged) : tagged;
		Span covered = spans.front();
		for (size_t i = 1; i < spans.size(); ++i) {
			covered.m_from = least(covered.m_from, spans[i].m_from);
			covered.m_next = greatest(covered.m_next, spans[i].m_next);
		}
		on = on ? ibBinOp(ibQueryBinOp::And, on, inside(covered)) : inside(covered);

		// the fields the key is made of, once each — what the pass is grouped by, and what it is read back through
		std::vector<ibQueryProjItem> proj;
		std::set<wxString> projected;
		for (const ibBackendQueryColumn* column : by)
			for (const wxString& field : ColumnFieldNames(column))
				if (projected.insert(field).second)
					proj.push_back(ibQueryProjItem{ ibCol(alias, field), field });

		std::vector<ibQueryExprPtr> keys;
		for (const ibQueryProjItem& item : proj)
			keys.push_back(item.m_expr);

		// FOLDED FIRST, where the key is the schedule's own: the records that ask the same question become one row
		// before the join, under the same alias, so every expression built over them reads the same.
		if (fold) {
			source = ibSubquery(ibAggregate(where ? ibFilter(std::move(source), where) : std::move(source), proj, keys), alias);
			where = nullptr;
		}

		ibDatabaseQueryBuilder q;
		q.From(std::move(source));
		q.Join(ibScan(schedule->GetPhysicalTableName(), s), on);
		if (where)
			q.Where(where);
		for (const ibQueryExprPtr& key : keys)
			q.GroupBy(key);
		const auto nameOf = [](size_t span, size_t k) { return wxString::Format(wxT("ib_sum%zu_%zu"), span, k); };
		for (size_t i = 0; i < spans.size(); ++i)
			for (size_t k = 0; k < resources.size(); ++k) {
				const ibQueryExprPtr value = ibCol(s, ibRegValueField(resources[k]));
				proj.push_back(ibQueryProjItem{ ibFunc(wxT("SUM"), { spans.size() == 1 ? value : ibCase({ { inside(spans[i]), value } }) }), nameOf(i, k) });
			}
		q.Project(proj);
		ibQueryResult rs = q.Execute();
		while (rs.Next()) {
			++sumRows;
			++passRows;
			std::vector<ibValue> key;
			for (const ibBackendQueryColumn* column : by) {
				ibValue value;
				column->ReadValue(column->GetPhysicalName(), metaData, value, rs);
				key.push_back(value);
			}
			const std::vector<size_t>* mine = whom(key);
			if (mine == nullptr)
				continue;
			for (size_t i = 0; i < spans.size(); ++i) {
				const size_t slot = std::find(periods.begin(), periods.end(), spans[i].m_period) - periods.begin();
				if (slot >= periods.size())
					continue;
				for (size_t k = 0; k < resources.size(); ++k) {
					const ibNumber sum = rs.GetResultNumber(nameOf(i, k));
					for (const size_t at : *mine)
						sums[at][slot * resources.size() + k] += sum;
				}
			}
		}
		pass.Pause();
		wxString over;
		for (const Span& span : spans)
			over += (over.IsEmpty() ? wxString() : wxT(" + ")) + ibCalcSchedulePeriodName(span.m_period);
		ibJournalInfo(wxT("query.road"), wxT("ScheduleData of %s: %s summed over %s in %lld ms, %ld row(s)"),
			m_reg->GetName(), alias == r ? wxT("the records") : wxT("the pieces"), over, pass.Ms(), passRows);
	};

	const ibBackendQueryColumn* recorderColumn = m_reg->GetRegisterRecorder()->GetQueryColumn();
	const ibBackendQueryColumn* lineColumn = m_reg->GetRegisterLineNumber()->GetQueryColumn();

	// ⭐⭐ WHICH RECORDS CAN LOSE A DAY — the ones a displacer MEETS (MeetsDisplacer), asked of the database once, by
	// the record's own key. Every other record keeps its action period whole, so its actual action period is the sum
	// over that period and displacement has nothing to work out for it. A payroll registers a salary for every
	// employee and a leave for a few: of a July's 86 280 records on a 40 000-employee copy, some six thousand meet
	// anything at all, and asking the walk about the rest was the largest part of the reading (2026-09-17).
	// Asked BEFORE the sums, because it also says which records the pass below may leave out.
	std::vector<bool> meets(records.size(), false);
	bool anyMeets = false;
	long metRows = 0;
	if (!resources.empty() && readsActual) {
		if (const ibQueryExprPtr met = MeetsDisplacer(spec, narrowed, r)) {
			ibJournalStopwatch asking;
			asking.Resume();
			ibDatabaseQueryBuilder q;
			q.From(ibScan(spec.m_records, r));
			std::vector<ibQueryProjItem> proj;
			std::set<wxString> projected;
			for (const ibBackendQueryColumn* column : { recorderColumn, lineColumn })
				for (const wxString& field : ColumnFieldNames(column))
					if (projected.insert(field).second)
						proj.push_back(ibQueryProjItem{ ibCol(r, field), field });
			q.Project(proj);
			q.Where(ibBinOp(ibQueryBinOp::And, recordsWhere(narrowed, r), met));
			ibQueryResult rs = q.Execute();
			while (rs.Next()) {
				ibValue recorder, line;
				recorderColumn->ReadValue(recorderColumn->GetPhysicalName(), metaData, recorder, rs);
				lineColumn->ReadValue(lineColumn->GetPhysicalName(), metaData, line, rs);
				const auto found = recordAt.find({ recorder, line });
				if (found != recordAt.end())
					for (const size_t at : found->second) {
						meets[at] = true;
						anyMeets = true;
					}
				++metRows;
			}
			asking.Pause();
			ibJournalInfo(wxT("query.road"), wxT("ScheduleData of %s: %ld record(s) meet a displacer, asked in %lld ms"),
				m_reg->GetName(), metRows, asking.Ms());
		}
	}

	if (!resources.empty() && !asked.empty()) {
		// over the record's own days, its base period, the whole of the period it is registered in — one pass,
		// and only over the periods the query reads
		std::vector<Span> own;
		if (readsAction)
			own.push_back({ ibCalcSchedulePeriod_Action, ibCol(r, spec.m_start), DayAfter(ibCol(r, spec.m_end)) });
		if (readsBase && m_reg->IsUseBasePeriod())
			own.push_back({ ibCalcSchedulePeriod_Base,
				ibCol(r, ibRegFieldOfRole(m_reg->GetBasePeriodStart()->GetQueryColumn(), ibColumnRole::Date)),
				DayAfter(ibCol(r, ibRegFieldOfRole(m_reg->GetBasePeriodEnd()->GetQueryColumn(), ibColumnRole::Date))) });
		if (readsRegistration)
			own.push_back({ ibCalcSchedulePeriod_Registration, ibCol(r, spec.m_period),
				ibDateAdd(ibCol(r, spec.m_period), m_reg->GetPeriodicityUnit(), Int(1)) });

		// ⭐ …AND THE ACTION PERIOD IS SUMMED ONLY WHERE IT IS SOMEBODY'S ANSWER. When this pass carries nothing but the
		// action period, and that period was asked for as the ACTUAL one, a record a displacer meets gets its sum from
		// its pieces below — summing its whole action period here would read a month of the schedule for it and throw
		// the answer away. What is left is exactly the records the copy below serves.
		ibQueryExprPtr where = recordsWhere(narrowed, r);
		if (own.size() == 1 && own.front().m_period == ibCalcSchedulePeriod_Action && !readsPeriod(ibCalcSchedulePeriod_Action))
			if (const ibQueryExprPtr met = MeetsDisplacer(spec, narrowed, r))
				where = ibBinOp(ibQueryBinOp::And, where, ibNot(met));

		// The question the schedule is asked: the links, and the bounds of every period in this pass. Records that
		// hold the same values in all of them ask it once, and the answer is theirs together.
		std::vector<const ibValueMetaObjectAttributeBase*> question;
		for (const ibCalcScheduleLink& link : scheduleDesc.GetLinks())
			for (const ibValueMetaObjectAttributeBase* field : record)
				if (field->GetMetaID() == link.m_field)
					question.push_back(field);
		for (const Span& span : own) {
			if (span.m_period == ibCalcSchedulePeriod_Action) {
				question.push_back(m_reg->GetActionPeriodStart());
				question.push_back(m_reg->GetActionPeriodEnd());
			}
			else if (span.m_period == ibCalcSchedulePeriod_Base) {
				question.push_back(m_reg->GetBasePeriodStart());
				question.push_back(m_reg->GetBasePeriodEnd());
			}
			else
				question.push_back(m_reg->GetRegistrationPeriod());
		}
		// A field of the question the record does not carry leaves the pass grouped by the record, as it always was.
		const bool answerable = !question.empty() && std::none_of(question.begin(), question.end(),
			[&placeOf, &record](const ibValueMetaObjectAttributeBase* field) { return field == nullptr || placeOf(field) >= record.size(); });
		std::vector<const ibBackendQueryColumn*> by;
		std::map<std::vector<ibValue>, std::vector<size_t>> askedBy;   // the records behind one question
		if (answerable) {
			for (const ibValueMetaObjectAttributeBase* field : question)
				by.push_back(field->GetQueryColumn());
			for (size_t i = 0; i < records.size(); ++i) {
				std::vector<ibValue> key;
				for (const ibValueMetaObjectAttributeBase* field : question)
					key.push_back(records[i][placeOf(field)]);
				askedBy[key].push_back(i);
			}
		}
		else
			by = { recorderColumn, lineColumn };

		sumOver(ibScan(spec.m_records, r), r, where, own, by, answerable,
			[&askedBy, &recordAt, answerable](const std::vector<ibValue>& key) -> const std::vector<size_t>* {
				if (answerable) {
					const auto found = askedBy.find(key);
					return found != askedBy.end() ? &found->second : nullptr;
				}
				const auto found = key.size() > 1 ? recordAt.find({ key[0], key[1] }) : recordAt.end();
				return found != recordAt.end() ? &found->second : nullptr;
			});
	}

	// ---- and what displacement leaves of the action period ---------------------------------------------------------
	// Only where the actual action period is read: it is the one sum displacement has anything to do with, and it
	// costs a walk over the register (ibCalcFactRelation).
	if (!resources.empty() && readsActual) {
		const size_t actionSlot = std::find(periods.begin(), periods.end(), ibCalcSchedulePeriod_Action) - periods.begin();
		const size_t actualSlot = std::find(periods.begin(), periods.end(), ibCalcSchedulePeriod_ActualAction) - periods.begin();
		for (size_t i = 0; i < records.size(); ++i)
			if (!meets[i] && actionSlot < periods.size() && actualSlot < periods.size())
				for (size_t k = 0; k < resources.size(); ++k)
					sums[i][actualSlot * resources.size() + k] = sums[i][actionSlot * resources.size() + k];

		// over the pieces displacement leaves — the fact's own relation, read through the fact's columns: it carries
		// each field the way the fact publishes it, which is not the way the register's table stores it. Asked about
		// the records a displacer meets, and no others — the same condition the copy above went by, so the two cannot
		// come to different answers about one record.
		const ibBackendQueryable* factSurface = m_reg->GetFactSurface();
		if (anyMeets && factSurface != nullptr) {
			const ibCalcNarrowing byMeeting = [narrowed, spec](const wxString& alias, bool subject) {
				ibQueryExprPtr all = narrowed ? narrowed(alias, subject) : nullptr;
				if (!subject)
					return all;
				const ibQueryExprPtr met = MeetsDisplacer(spec, narrowed, alias);
				return met ? (all ? ibBinOp(ibQueryBinOp::And, all, met) : met) : all;
			};
			if (const ibQueryRelPtr fact = ibCalcFactRelation(spec, byMeeting))
				sumOver(ibSubquery(fact, wxT("v")), wxT("v"), nullptr,
					{ { ibCalcSchedulePeriod_ActualAction, ibCol(wxT("v"), spec.m_start), DayAfter(ibCol(wxT("v"), spec.m_end)) } },
					{ factSurface->ResolveColumnByName(m_reg->GetRegisterRecorder()->GetName()),
					  factSurface->ResolveColumnByName(m_reg->GetRegisterLineNumber()->GetName()) },
					/*fold*/ false,
					[&recordAt](const std::vector<ibValue>& key) -> const std::vector<size_t>* {
						const auto found = key.size() > 1 ? recordAt.find({ key[0], key[1] }) : recordAt.end();
						return found != recordAt.end() ? &found->second : nullptr;
					});
		}
	}

	readSums.Pause();

	// ---- one row a record ----------------------------------------------------------------------------------------
	// The sums that were asked for, by the ids their columns carry; a sum nobody asked for was not computed and its
	// cell is not filled either.
	std::vector<std::pair<size_t, ibMetaID>> filled;   // where in `sums`, and which column it is
	for (size_t k = 0; k < resources.size(); ++k)
		for (size_t p = 0; p < periods.size(); ++p)
			if (std::find(asked.begin(), asked.end(), resources[k]->GetName() + ibCalcSchedulePeriodName(periods[p])) != asked.end())
				filled.emplace_back(p * resources.size() + k, ibRegDerivedColumnId(resources[k]->GetMetaID(), periods[p]));

	lay.Resume();
	for (size_t i = 0; i < records.size(); ++i) {
		const std::vector<ibValue>& one = records[i];
		const long row = out.AppendRow();
		for (size_t a = 0; a < record.size(); ++a)
			if (needed[a])
				out.SetCell(row, record[a]->GetMetaID(), one[a]);
		for (const std::pair<size_t, ibMetaID>& cell : filled)
			out.SetCell(row, cell.second, ibValue(sums[i][cell.first]));
	}
	lay.Pause();
	ibJournalInfo(wxT("query.road"), wxT("ScheduleData of %s: %zu record(s) read in %lld ms, %zu of %zu sum column(s) asked for, ")
		wxT("%ld sum row(s) from the database in %lld ms, laid out in %lld ms"),
		m_reg->GetName(), records.size(), readRecords.Ms(), asked.size(), resources.size() * periods.size(),
		sumRows, readSums.Ms(), lay.Ms());
	return out;
}

wxString ibCalcScheduleDataSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_meta->GetClassType());
}

wxString ibCalcScheduleDataSourceDescriptor::GetName() const
{
	return m_meta->GetName() + wxT(".") + ibCalcScheduleDataName;
}

const ibBackendQueryable* ibCalcScheduleDataSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	return CreateQueryable(paParams, lSizeArray, {}, ibQueryReadColumns());
}

// What a condition is resolved against: the surface the reading publishes.
const ibBackendQueryable* ibCalcScheduleDataSourceDescriptor::GetConditionScope() const
{
	return m_meta != nullptr ? m_meta->GetScheduleDataSurface() : nullptr;
}

// Where the records keep action periods and a schedule is bound — the days and what they are counted against. Built
// and kept by the base, the condition part of the call (queryableFactory.h, MakeCompanion).
const ibBackendQueryable* ibCalcScheduleDataSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray,
	const std::vector<ibQueryPredicatePtr>& conditions, const ibQueryReadColumns& read)
{
	if (!m_meta->IsUseActionPeriod() || !m_meta->GetScheduleDesc().IsOk())
		return nullptr;
	// ⚠ WHICH SUMS ARE READ IS PART OF THE CALL: the rows carry them, so two queries over the same arguments, one
	// reading the base period's hours and one not, are two readings. Said to the companion store as one more
	// argument after the ones the call wrote — a key of the arguments alone would hand the second query the first
	// one's rows, with a column it asked for left at zero (MakeCompanionFor, queryableFactory.h).
	wxString sums;
	for (const wxString& name : ibCalcScheduleAskedSums(m_meta, read))
		sums += name + wxT(";");
	std::vector<ibValue> args;
	for (long i = 0; i < lSizeArray; ++i)
		args.push_back(paParams != nullptr && paParams[i] != nullptr ? *paParams[i] : ibValue());
	args.push_back(ibValue(sums));
	std::vector<ibValue*> keyed;
	for (ibValue& arg : args)
		keyed.push_back(&arg);

	return MakeCompanionFor<ibCalcScheduleDataQueryable>(conditions, keyed.data(), static_cast<long>(keyed.size()), m_meta,
		ibRegArg(paParams, lSizeArray, ibCalcViewArg::Period),
		ibRegArg(paParams, lSizeArray, ibCalcViewArg::BeginOfActionPeriod), ibRegArg(paParams, lSizeArray, ibCalcViewArg::EndOfActionPeriod),
		ibRegConsumedCondition(conditions, ibCalcViewArg::Condition), read);
}

// THE FACT'S FOUR ARGUMENTS (ibCalcFactSourceDescriptor::DescribeParameters): as of which registration period, for
// which days of action, then the condition — `ScheduleData(&Month, &Start, &End, Employee = &Employee)`.
void ibCalcScheduleDataSourceDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
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
		  "in the register's periodicity, and the displacement as it stood then. Left out, every period is read."));
	time(wxT("BeginOfActionPeriod"), m_meta != nullptr ? m_meta->GetActionPeriodStart() : nullptr,
		_("FOR WHICH DAYS OF ACTION, from - the records in force on this day or later. Left out, from the first."));
	time(wxT("EndOfActionPeriod"), m_meta != nullptr ? m_meta->GetActionPeriodEnd() : nullptr,
		_("FOR WHICH DAYS OF ACTION, to - the records in force on this day or earlier. Left out, to the last."));
	ibAppendRegisterConditionParameter(out);
}

// What the records can be selected by inside: their own fields.
void ibCalcScheduleDataSourceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	const auto append = [&explorer](const ibValueMetaObjectAttributeBase* field) {
		if (field != nullptr)
			explorer.AppendColumn(field->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
	};
	for (const ibValueMetaObjectDimension* dimension : m_meta->GetDimensionArrayObject())
		append(dimension);
	append(m_meta->GetCalculationType());
	append(m_meta->GetActionPeriod());
	append(m_meta->GetRegisterRecorder());
	for (const ibValueMetaObjectAttributeBase* attribute : m_meta->GetAttributeArrayObject())
		append(attribute);
}

// A record's column IS the register's attribute (the note in ibFillExplorerFromRegisterView); a sum is its own.
void ibCalcScheduleDataSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	const ibBackendQueryable* surface = m_meta->GetScheduleDataSurface();
	if (surface == nullptr)
		return;
	for (const ibBackendQueryColumn* column : surface->GetColumns()) {
		if (const ibValueMetaObjectAttributeBase* attribute = m_meta->FindAnyAttributeObjectByFilter(column->GetColumnId()))
			explorer.AppendColumn(attribute->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
		else
			explorer.AppendColumn(column);
	}
}
