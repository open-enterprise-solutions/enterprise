////////////////////////////////////////////////////////////////////////////
//	Description : a calculation register's RECALCULATION — its marks' table
//	              as a queryable and a source, and the marks themselves: what a
//	              written set's records lead, marked in one statement
//	              (calculationRegister.h, "The recalculation's marks"). The table
//	              is declared with the register's (calculationRegisterMetadataSchema.cpp).
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"               // the recalculation is the register's; its columns are the register's own
#include "chartOfCalculationTypes.h"           // the Leading section, read as a table
#include "backend/metaData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"         // L2 — the marks' relation, their INSERT … SELECT and DELETE
#include "backend/query/dataQueryBuilder.h"    // L3 — a recorder's own marks cleared
#include "backend/query/columnLayout.h"        // ibOwnerRefField, DescribeColumnLayout
#include "backend/metaCollection/dimension/metaDimensionObject.h"   // the register's dimensions, its marks' columns

#include <algorithm>
#include <set>

//***********************************************************************
//*                   ibRecalculationQueryable                          *
//***********************************************************************

// The recorder, the type, the month where the register keeps action periods, then the dimensions — every one the
// register's own column.
std::vector<const ibBackendQueryColumn*> ibRecalculationQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns{ m_reg->GetRegisterRecorder()->GetQueryColumn(),
		m_reg->GetCalculationType()->GetQueryColumn() };
	if (m_reg->IsUseActionPeriod())
		columns.push_back(m_reg->GetActionPeriod()->GetQueryColumn());
	for (const ibValueMetaObjectDimension* dimension : m_reg->GetDimensionArrayObject())
		columns.push_back(dimension->GetQueryColumn());
	return columns;
}

const ibBackendQueryColumn* ibRecalculationQueryable::ResolveColumnByName(const wxString& name) const
{
	for (const ibBackendQueryColumn* column : GetColumns())
		if (column->GetName().IsSameAs(name, false))
			return column;
	return nullptr;
}

wxString ibRecalculationQueryable::GetQueryTableName() const { return m_reg->GetRecalculationTableName(); }
const ibUniqueKey& ibRecalculationQueryable::GetQueryTableGuid() const { return m_reg->GetRecalculationObject()->GetGuid(); }
wxString ibRecalculationQueryable::GetQueryName() const { return m_reg->GetRecalculationObject()->GetName(); }
ibMetaID ibRecalculationQueryable::GetQueryTableId() const { return m_reg->GetRecalculationObject()->GetMetaID(); }
const ibMetaData* ibRecalculationQueryable::GetMetaData() const { return m_reg->GetMetaData(); }

//***********************************************************************
//*                 ibRecalculationSourceDescriptor                     *
//***********************************************************************

wxString ibRecalculationSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_meta->GetClassType());
}

wxString ibRecalculationSourceDescriptor::GetName() const
{
	return m_meta->GetName() + wxT(".") + m_meta->GetRecalculationObject()->GetName();
}

// A table: whatever it is called with, it is the register's own member.
const ibBackendQueryable* ibRecalculationSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return &m_queryable;
}

// A column of the marks IS the register's attribute.
void ibRecalculationSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibBackendQueryColumn* column : m_queryable.GetColumns())
		explorer.AppendColumn(column, /*enabled*/ true, /*visible*/ true);
}

//***********************************************************************
//*                    The recalculation's marks                        *
//***********************************************************************

namespace {

ibQueryExprPtr Day(const wxString& q, const wxString& field)
{
	return ibPeriodTrunc(ibCol(q, field), ibTotalsPeriod::Day);
}

// A number, typed: bare, a constant binds as a parameter with no type of its own, and Firebird refuses it where
// nothing beside it lends one.
ibQueryExprPtr Int(int value)
{
	return ibCast(ibConst(ibValue(value)), ibTypeInteger());
}

ibQueryExprPtr And(const ibQueryExprPtr& a, const ibQueryExprPtr& b)
{
	return !a ? b : !b ? a : ibBinOp(ibQueryBinOp::And, a, b);
}

ibQueryExprPtr Or(const ibQueryExprPtr& a, const ibQueryExprPtr& b)
{
	return !a ? b : !b ? a : ibBinOp(ibQueryBinOp::Or, a, b);
}

ibQueryExprPtr Eq(const ibQueryExprPtr& a, const ibQueryExprPtr& b) { return ibBinOp(ibQueryBinOp::Eq, a, b); }
ibQueryExprPtr Le(const ibQueryExprPtr& a, const ibQueryExprPtr& b) { return ibBinOp(ibQueryBinOp::Le, a, b); }

// Paired fields of two tables, equal pair by pair.
ibQueryExprPtr SamePairs(const wxString& first, const std::vector<std::pair<wxString, wxString>>& pairs, const wxString& second)
{
	ibQueryExprPtr out;
	for (const auto& pair : pairs)
		out = And(out, Eq(ibCol(first, pair.first), ibCol(second, pair.second)));
	return out;
}

ibQueryExprPtr Narrowed(const ibRecalculationNarrowing& narrowing, const wxString& alias, int source)
{
	return narrowing ? narrowing(alias, source) : nullptr;
}

// A record that counts: Active, as every register's totals are guarded.
ibQueryExprPtr Active(const wxString& q, const wxString& field)
{
	return ibBinOp(ibQueryBinOp::Ne, ibCol(q, field), Int(0));
}

// `fields` once each, under their own names — what a derived table carries out.
std::vector<ibQueryProjItem> Carried(const wxString& q, const std::vector<wxString>& fields)
{
	std::vector<ibQueryProjItem> out;
	std::set<wxString> seen;
	for (const wxString& field : fields)
		if (!field.IsEmpty() && seen.insert(field).second)
			out.push_back(ibQueryProjItem{ ibCol(q, field), field });
	return out;
}

// Every field of `a` with the field of `b` laid out for the same role — the pairing ibRegSameValueIR makes, kept
// here as names: a leading register's fields are joined under aliases this file chooses.
std::vector<std::pair<wxString, wxString>> PairedByRole(const ibBackendQueryColumn* a, const ibBackendQueryColumn* b)
{
	std::vector<std::pair<wxString, wxString>> out;
	const std::vector<ibColumnSlot> right = DescribeColumnLayout(b);
	for (const ibColumnSlot& x : DescribeColumnLayout(a))
		for (const ibColumnSlot& y : right)
			if (x.m_role == y.m_role) {
				out.push_back({ x.m_name, y.m_name });
				break;
			}
	return out;
}

// The dimensions `reg` and `other` both hold, by name — each of reg's with other's: a leading record holds the led
// record's values on these. A register sharing none with reg leads nothing of it: it could not say whose records.
std::vector<std::pair<const ibValueMetaObjectDimension*, const ibValueMetaObjectDimension*>>
SharedDimensions(const ibValueMetaObjectCalculationRegister* reg, const ibValueMetaObjectCalculationRegister* other)
{
	std::vector<std::pair<const ibValueMetaObjectDimension*, const ibValueMetaObjectDimension*>> out;
	for (const ibValueMetaObjectDimension* own : reg->GetDimensionArrayObject())
		for (const ibValueMetaObjectDimension* theirs : other->GetDimensionArrayObject())
			if (theirs->GetName().IsSameAs(own->GetName(), false)) {
				out.push_back({ own, theirs });
				break;
			}
	return out;
}

} // namespace

ibRecalculationSpec ibRecalculationSpecOf(const ibValueMetaObjectCalculationRegister* reg)
{
	ibRecalculationSpec v;
	if (reg == nullptr || !reg->HasRecalculation())
		return v;
	const ibValueMetaObjectChartOfCalculationTypes* chart = reg->GetChartOfCalculationTypes();
	const ibValueMetaObjectCalculationTypeRelationTable* leading = chart != nullptr ? chart->GetLeadingTable() : nullptr;
	if (leading == nullptr || !leading->IsAllowed() || leading->GetCalculationType() == nullptr)
		return v;

	const auto fieldsOf = [](const ibValueMetaObjectAttributeBase* attribute, std::vector<wxString>& out) {
		for (const wxString& field : ibRegFieldsOf(attribute))
			out.push_back(field);
	};
	const auto dayOf = [](const ibValueMetaObjectAttributeBase* attribute) {
		return ibRegFieldOfRole(attribute->GetQueryColumn(), ibColumnRole::Date);
	};

	// The marks' own columns, which are the register's (ibRecalculationQueryable), in their order.
	v.m_table = reg->GetPhysicalTableName();
	for (const ibBackendQueryColumn* column : reg->GetRecalculationQueryable()->GetColumns())
		for (const wxString& field : ColumnFieldNames(column))
			v.m_mark.push_back(field);

	if (reg->IsUseActionPeriod()) {
		v.m_start = dayOf(reg->GetActionPeriodStart());
		v.m_end = dayOf(reg->GetActionPeriodEnd());
	}
	if (reg->IsUseBasePeriod()) {
		v.m_baseStart = dayOf(reg->GetBasePeriodStart());
		v.m_baseEnd = dayOf(reg->GetBasePeriodEnd());
	}
	v.m_typeId = ibRegFieldOfRole(reg->GetCalculationType()->GetQueryColumn(), ibColumnRole::ReferenceId);
	v.m_registration = dayOf(reg->GetRegistrationPeriod());
	v.m_active = ibRegValueField(reg->GetRegisterActive());
	v.m_storno = ibRegValueField(reg->GetStorno());
	v.m_baseByRegistration = chart->GetBaseDependence() == ibBaseDependence::eBaseByRegistrationPeriod;
	v.m_leading = leading->GetPhysicalTableName();
	v.m_owner = ibOwnerRefField();

	for (const ibValueMetaObjectCalculationRegister* other : reg->GetRelatedRegisters()) {
		const auto shared = SharedDimensions(reg, other);
		if (shared.empty())
			continue;
		ibRecalculationLeading s;
		s.m_table = other->GetPhysicalTableName();
		s.m_named = PairedByRole(other->GetCalculationType()->GetQueryColumn(), leading->GetCalculationType()->GetQueryColumn());
		for (const auto& dimension : shared)
			for (const auto& pair : PairedByRole(dimension.first->GetQueryColumn(), dimension.second->GetQueryColumn()))
				s.m_dimensions.push_back(pair);
		s.m_registration = dayOf(other->GetRegistrationPeriod());
		s.m_active = ibRegValueField(other->GetRegisterActive());
		if (other->IsUseActionPeriod()) {
			s.m_start = dayOf(other->GetActionPeriodStart());
			s.m_end = dayOf(other->GetActionPeriodEnd());
		}
		v.m_sources.push_back(std::move(s));
	}
	return v;
}

// `l` a leading record the narrowing chooses, `r` a Leading row naming l's type, `d` a record of the type owning r,
// holding l's values on the dimensions the two registers share. One part per leading register: its chosen records — a
// recorder's, found through the register's key — each meeting the led records of its dimension values through the
// lookup index.
ibQueryRelPtr ibRecalculationRelation(const ibRecalculationSpec& v, const ibRecalculationNarrowing& narrowing)
{
	if (v.m_table.IsEmpty() || v.m_leading.IsEmpty() || v.m_mark.empty())
		return nullptr;
	const bool acts = !v.m_start.IsEmpty(), based = !v.m_baseStart.IsEmpty();

	// The led record: in force and not a storno.
	const ibQueryExprPtr led = And(Active(wxT("d"), v.m_active), Eq(ibCol(wxT("d"), v.m_storno), Int(0)));
	const ibQueryExprPtr owned = Eq(ibCol(wxT("r"), v.m_owner), ibCol(wxT("d"), v.m_typeId));

	std::vector<ibQueryProjItem> mark;
	for (const wxString& field : v.m_mark)
		mark.push_back(ibQueryProjItem{ ibCol(wxT("d"), field), field });

	ibQueryRelPtr all;
	size_t parts = 0;
	for (size_t i = 0; i < v.m_sources.size(); ++i) {
		const ibRecalculationLeading& s = v.m_sources[i];
		if (s.m_named.empty())
			continue;   // its type pairs with no field of a Leading row: it names nothing
		const int source = static_cast<int>(i);
		const bool theyAct = !s.m_start.IsEmpty();

		// MEETING IN TIME, term by term as ibFindLedRecords weighs it; with no period on either side to weigh, the
		// registration period is the only time both records have.
		const bool byAction = acts && theyAct, byBase = based && theyAct && !v.m_baseByRegistration,
			byRegistration = based && v.m_baseByRegistration;
		ibQueryExprPtr meets;
		if (byAction)
			meets = Or(meets, And(Le(Day(wxT("l"), s.m_start), Day(wxT("d"), v.m_end)), Le(Day(wxT("d"), v.m_start), Day(wxT("l"), s.m_end))));
		if (byBase)
			meets = Or(meets, And(Le(Day(wxT("l"), s.m_start), Day(wxT("d"), v.m_baseEnd)), Le(Day(wxT("d"), v.m_baseStart), Day(wxT("l"), s.m_end))));
		if (byRegistration)
			meets = Or(meets, And(Le(Day(wxT("d"), v.m_baseStart), Day(wxT("l"), s.m_registration)), Le(Day(wxT("l"), s.m_registration), Day(wxT("d"), v.m_baseEnd))));
		if (!meets)
			meets = Eq(Day(wxT("l"), s.m_registration), Day(wxT("d"), v.m_registration));

		// The chosen records, each once by what the meeting asks of it.
		std::vector<wxString> leading;
		for (const auto& pair : s.m_named)
			leading.push_back(pair.first);
		for (const auto& pair : s.m_dimensions)
			leading.push_back(pair.second);
		for (const wxString& field : { s.m_registration, s.m_start, s.m_end })
			leading.push_back(field);
		const ibQueryExprPtr chosen = And(Active(wxT("l"), s.m_active), Narrowed(narrowing, wxT("l"), source));
		const ibQueryRelPtr joined = ibJoin(ibJoin(
			ibSubquery(ibDistinct(ibProject(ibFilter(ibScan(s.m_table, wxT("l")), chosen), Carried(wxT("l"), leading))), wxT("l")),
			ibScan(v.m_leading, wxT("r")), SamePairs(wxT("l"), s.m_named, wxT("r"))),
			ibScan(v.m_table, wxT("d")), And(owned, SamePairs(wxT("d"), s.m_dimensions, wxT("l"))));
		const ibQueryRelPtr part = ibProject(ibFilter(joined, And(And(led, meets), Narrowed(narrowing, wxT("d"), -1))), mark);
		all = all ? ibUnion(all, part) : part;
		++parts;
	}
	if (!all)
		return nullptr;
	return parts == 1 ? ibDistinct(all) : all;   // a union already answers each row once
}

// INSERT … SELECT the marks `rows` name that are not there yet — `rows` under the register's field names, which are
// the marks' own (`fields`).
static void InsertMarks(const ibValueMetaObjectCalculationRegister* reg, const std::vector<wxString>& fields,
	const ibQueryRelPtr& rows)
{
	const wxString table = reg->GetRecalculationTableName();
	std::vector<ibQueryProjItem> projection;
	ibQueryExprPtr same;
	for (const wxString& field : fields) {
		projection.push_back(ibQueryProjItem{ ibCol(wxT("v"), field), field });
		same = And(same, Eq(ibCol(wxT("t"), field), ibCol(wxT("v"), field)));
	}
	const ibQueryExprPtr absent = ibExists(ibProject(ibFilter(ibScan(table, wxT("t")), same),
		{ ibQueryProjItem{ ibCol(wxT("t"), fields.front()), fields.front() } }), /*negated*/ true);
	ibDatabaseQueryBuilder insert;
	if (insert.Execute(ibInsertSelect(table, fields, ibProject(ibFilter(ibSubquery(rows, wxT("v")), absent), projection))) < 0)
		ibBackendCoreException::Error(_("Register '%s': failed to mark records for the recalculation"), reg->GetSynonym());
}

// The registers whose recalculation `written` leads, each with its reading narrowed to `written` as the one leader.
static std::vector<std::pair<const ibValueMetaObjectCalculationRegister*, ibRecalculationSpec>> LedBy(
	const ibValueMetaObjectCalculationRegister* written)
{
	std::vector<std::pair<const ibValueMetaObjectCalculationRegister*, ibRecalculationSpec>> out;
	const ibMetaData* metaData = written != nullptr ? written->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return out;
	const wxString writtenTable = written->GetPhysicalTableName();
	for (const ibValueMetaObjectCalculationRegister* reg :
		metaData->GetAnyArrayObject<ibValueMetaObjectCalculationRegister>(g_metaCalculationRegisterCLSID)) {
		ibRecalculationSpec spec = ibRecalculationSpecOf(reg);
		spec.m_sources.erase(std::remove_if(spec.m_sources.begin(), spec.m_sources.end(),
			[&writtenTable](const ibRecalculationLeading& s) { return s.m_table != writtenTable; }), spec.m_sources.end());
		if (!spec.m_sources.empty())
			out.push_back({ reg, std::move(spec) });
	}
	return out;
}

void ibRecalculationMarkLedBy(const ibValueMetaObjectCalculationRegister* written, const ibValue& recorder)
{
	const ibMetaData* metaData = written != nullptr ? written->GetMetaData() : nullptr;
	if (metaData == nullptr || recorder.IsEmpty() || written->GetRegisterRecorder() == nullptr)
		return;
	for (const auto& led : LedBy(written)) {
		const ibValueMetaObjectCalculationRegister* reg = led.first;
		// The recorder's records lead; its own records are never led — whichever register holds them.
		const ibRecalculationNarrowing narrowing = [written, reg, metaData, &recorder](const wxString& alias, int source) {
			const ibValueMetaObjectCalculationRegister* holder = source < 0 ? reg : written;
			const ibQueryExprPtr own = ibRegCompositeIR(holder->GetRegisterRecorder()->GetQueryColumn(), metaData,
				recorder, ibQueryBinOp::Eq, alias);
			return source < 0 ? ibNot(own) : own;
		};
		const ibQueryRelPtr relation = ibRecalculationRelation(led.second, narrowing);
		if (relation)
			InsertMarks(reg, led.second.m_mark, relation);
	}
}

void ibRecalculationAnswerBy(const ibValueMetaObjectCalculationRegister* reg, const ibValue& recorder)
{
	const ibMetaData* metaData = reg != nullptr ? reg->GetMetaData() : nullptr;
	if (metaData == nullptr || recorder.IsEmpty() || reg->GetStorno() == nullptr)
		return;
	const ibRecalculationSpec spec = ibRecalculationSpecOf(reg);
	if (spec.m_mark.empty())
		return;
	// The mark's fields past the recorder — the type, the month, the dimensions — against the storno's. The marks
	// carry the register's own field names, so the mark's side is qualified by its table: bare, it would read `s`'s.
	const wxString marks = reg->GetRecalculationTableName();
	const size_t objectFields = ColumnFieldNames(reg->GetRegisterRecorder()->GetQueryColumn()).size();
	ibQueryExprPtr answering = And(And(ibRegCompositeIR(reg->GetRegisterRecorder()->GetQueryColumn(), metaData, recorder,
		ibQueryBinOp::Eq, wxT("s")), ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("s"), ibRegValueField(reg->GetStorno())), Int(0))),
		Active(wxT("s"), ibRegValueField(reg->GetRegisterActive())));
	for (size_t f = objectFields; f < spec.m_mark.size(); ++f)
		answering = And(answering, Eq(ibCol(wxT("s"), spec.m_mark[f]), ibCol(marks, spec.m_mark[f])));
	const ibQueryExprPtr answered = ibExists(ibProject(ibFilter(ibScan(reg->GetPhysicalTableName(), wxT("s")), answering),
		{ ibQueryProjItem{ ibCol(wxT("s"), spec.m_mark.front()), spec.m_mark.front() } }));
	ibDatabaseQueryBuilder clear;
	if (clear.Execute(ibDelete(marks, answered)) < 0)
		ibBackendCoreException::Error(_("Register '%s': failed to clear the recalculation of what this recorder corrects"),
			reg->GetSynonym());
}

// THIS recorder's marks in THIS register are answered: its records here were just computed again — or are gone.
void ibRecalculationAnswerOwn(const ibValueMetaObjectCalculationRegister* reg, const ibValue& recorder)
{
	if (reg == nullptr || recorder.IsEmpty() || !reg->HasRecalculation())
		return;
	ibDataQueryBuilder clear;
	clear.From(reg->GetRecalculationQueryable());
	clear.WithAccessPolicy(nullptr);   // which records are stale is a fact of the payroll, not of the one posting
	clear.Where(reg->GetRegisterRecorder()->GetQueryColumn(), recorder);
	if (!clear.Delete())   // deleting nothing is success; a refused statement is not
		ibBackendCoreException::Error(_("Register '%s': failed to clear the recalculation of this recorder"), reg->GetSynonym());
}

void ibRecalculationMarkReversed(const ibValueMetaObjectCalculationRegister* reg, const ibValue& recorder)
{
	const ibMetaData* metaData = reg != nullptr ? reg->GetMetaData() : nullptr;
	if (metaData == nullptr || recorder.IsEmpty() || reg->GetStorno() == nullptr)
		return;
	const wxString records = reg->GetPhysicalTableName();
	const wxString storno = ibRegValueField(reg->GetStorno()), active = ibRegValueField(reg->GetRegisterActive());
	const wxString registration = ibRegFieldOfRole(reg->GetRegistrationPeriod()->GetQueryColumn(), ibColumnRole::Date);

	// `s` a storno of the recorder, `d` a record of its position registered before it, of another recorder, in force.
	ibQueryExprPtr position;
	std::vector<const ibValueMetaObjectAttributeBase*> parts{ reg->GetCalculationType() };
	for (const ibValueMetaObjectDimension* dimension : reg->GetDimensionArrayObject())
		parts.push_back(dimension);
	if (reg->IsUseActionPeriod())
		for (const ibValueMetaObjectAttributeBase* part : { static_cast<const ibValueMetaObjectAttributeBase*>(reg->GetActionPeriod()),
				static_cast<const ibValueMetaObjectAttributeBase*>(reg->GetActionPeriodStart()),
				static_cast<const ibValueMetaObjectAttributeBase*>(reg->GetActionPeriodEnd()) })
			parts.push_back(part);
	for (const ibValueMetaObjectAttributeBase* part : parts)
		for (const wxString& field : ibRegFieldsOf(part))
			position = And(position, Eq(ibCol(wxT("d"), field), ibCol(wxT("s"), field)));
	const ibQueryExprPtr ownStorno = And(And(ibRegCompositeIR(reg->GetRegisterRecorder()->GetQueryColumn(), metaData, recorder,
		ibQueryBinOp::Eq, wxT("s")), ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("s"), storno), Int(0))), Active(wxT("s"), active));
	const ibQueryExprPtr reversed = And(And(ibNot(ibRegCompositeIR(reg->GetRegisterRecorder()->GetQueryColumn(), metaData, recorder,
		ibQueryBinOp::Eq, wxT("d"))), And(Eq(ibCol(wxT("d"), storno), Int(0)), Active(wxT("d"), active))),
		ibBinOp(ibQueryBinOp::Lt, ibCol(wxT("d"), registration), ibCol(wxT("s"), registration)));

	const ibRecalculationSpec spec = ibRecalculationSpecOf(reg);
	if (spec.m_mark.empty())
		return;
	InsertMarks(reg, spec.m_mark, ibDistinct(ibProject(ibFilter(ibJoin(ibScan(records, wxT("s")),
		ibScan(records, wxT("d")), position), And(ownStorno, reversed)), Carried(wxT("d"), spec.m_mark))));
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

// A recalculation saved as an object of its own — loaded so the configuration opens, and let go (calculationRegister.h).
METADATA_TYPE_REGISTER(ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation, "Recalculation", g_metaRecalculationCLSID);
