////////////////////////////////////////////////////////////////////////////
//	Description : Calculation register — the STRUCTURE it declares.
//
//	The register's own table comes from the register-data base, unchanged: a
//	calculation register's columns ARE a register's columns (its extra ones —
//	action period, registration period, base period, calculation type, storno —
//	are predefined attributes, so the base picks them up through
//	FillArrayObjectByPredefinedAttribute like any other).
//
//	What is NOT the base's is the SUBORDINATE tables: the recalculations (each a
//	table-bearing child) and the actual action periods (a derived table the
//	register maintains itself).
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"

#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot

// The actual action periods' columns: the register's own, every one of them — a piece is a record, in
// force over part of its action period (see ibCalcActualPeriodSourceDescriptor).
static std::vector<const ibBackendQueryColumn*> ibCalcActualPeriodColumns(const ibValueMetaObjectCalculationRegister* reg)
{
	std::vector<const ibBackendQueryColumn*> columns;
	for (const ibValueMetaObjectAttributeBase* attribute : reg->GetGenericAttributeArrayObject())
		if (attribute != nullptr)
			columns.push_back(attribute->GetQueryColumn());
	return columns;
}

// ...keyed by the record (recorder, line) and the piece's start: two pieces of one record never share one.
static std::vector<const ibBackendQueryColumn*> ibCalcActualPeriodKey(const ibValueMetaObjectCalculationRegister* reg)
{
	return { reg->GetRegisterRecorder()->GetQueryColumn(), reg->GetRegisterLineNumber()->GetQueryColumn(),
		reg->GetActionPeriodStart()->GetQueryColumn() };
}

const ibBackendQueryable* ibValueMetaObjectCalculationRegister::GetActualActionPeriodQueryable() const
{
	if (!IsUseActionPeriod())
		return nullptr;
	if (!m_actualPeriodsQueryable)
		m_actualPeriodsQueryable = std::make_unique<ibSchemaTableQueryable>(GetActualActionPeriodTableName(),
			GetActualActionPeriodTableId(), ibCalcActualPeriodColumns(this), GetMetaData(), ibCalcActualPeriodKey(this));
	return m_actualPeriodsQueryable.get();
}

wxString ibCalcActualPeriodSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_meta->GetClassType());
}

wxString ibCalcActualPeriodSourceDescriptor::GetName() const
{
	return m_meta->GetName() + wxT(".ActualActionPeriod");
}

const ibBackendQueryable* ibCalcActualPeriodSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return m_meta->GetActualActionPeriodQueryable();
}

// ⭐ ONE ARGUMENT — THE CONDITION, as a virtual table of a register takes one (Max, 2026-09-10): which
// records' pieces, said on the register's own fields — `ActualActionPeriod(Employee = &Employee)`.
// Not consumed by this source: the pieces are STORED, so the condition only selects rows, and the
// lowering puts it into the statement's own WHERE over the pieces table — the dimensions are narrowed
// before a single piece is read, which is what the argument is for. Optional, as a slice's is: written
// without it, the table is every piece there is.
void ibCalcActualPeriodSourceDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	ibQuerySourceParameter condition;
	condition.m_name = wxT("Condition");
	condition.m_condition = true;
	condition.m_description = _("Which records' pieces - a condition on the register's own fields, applied before a piece is read");
	out.push_back(condition);
}

void ibCalcActualPeriodSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibBackendQueryColumn* column : ibCalcActualPeriodColumns(m_meta))
		explorer.AppendColumn(column, /*enabled*/ true, /*visible*/ true);
}

// ⭐⭐ THE RECORDS OF ONE EMPLOYEE — the read every road of this register makes, and nothing indexed it.
//
// The register's key is (Recorder, LineNumber): "the lines of this document". Everything a calculation
// asks is the other way round — whose salary does this sick leave cut (the actual-periods write), which
// records feed this bonus (GetBase), what did this employee earn this month (a sheet) — all of them "the
// records holding these dimension values, of this type". With no index that is the whole register per
// question, and the register only grows: forty thousand employees are forty thousand records a month.
// So both tables a calculation reads carry the dimensions and the type, in that order, as one lookup
// index — as many leading columns as the engine's index holds (ibDeclareLookupIndex).
static std::vector<const ibBackendQueryColumn*> ibCalcLookupKey(const ibValueMetaObjectCalculationRegister* reg)
{
	std::vector<const ibBackendQueryColumn*> key;
	for (const ibValueMetaObjectDimension* dimension : reg->GetDimensionArrayObject())
		key.push_back(dimension->GetQueryColumn());
	if (reg->GetCalculationType() != nullptr)
		key.push_back(reg->GetCalculationType()->GetQueryColumn());
	return key;
}

void ibValueMetaObjectCalculationRegister::ContributeTables(ibSchemaSnapshot& out) const
{
	// The register's own table — and its lookup index (above), on the table the base has just declared.
	ibValueMetaObjectRegisterData::ContributeTables(out);
	{
		const ibBackendQueryable* own = GetQueryable();
		ibSchemaTable& t = out.Shared(own->GetQueryTableId(), own->GetQueryTableName());
		ibDeclareLookupIndex(t, t.m_name + wxT("_DIX"), ibCalcLookupKey(this));
	}

	// The actual action periods — the same columns as the register's own (shared column objects, as the
	// accounting register's totals share its Account), one row per piece, under an identity of its own.
	if (IsUseActionPeriod()) {
		const wxString name = GetActualActionPeriodTableName();
		ibSchemaTable& t = out.Shared(GetActualActionPeriodTableId(), name);
		for (const ibBackendQueryColumn* column : ibCalcActualPeriodColumns(this))
			t.Add(column);
		t.Index(name + wxT("_INDEX"), ibCalcActualPeriodKey(this), /*unique*/ true);
		ibDeclareLookupIndex(t, name + wxT("_DIX"), ibCalcLookupKey(this));
	}

	// 🛑⭐⭐ AND THE RECALCULATION TABLES, WHICH NOTHING ELSE WOULD ASK FOR. The contract on
	// ibValueMetaObject::ContributeTables is explicit: a TABLE-BEARING object adds its own table and
	// does NOT recurse, because its children are attributes and forms rather than tables. That is true
	// of every other register — and false here: a calculation register owns Recalculation children,
	// and each of them IS a table.
	//
	// MEASURED before this existed: applying a configuration with a calculation register and one
	// recalculation created four tables and not the fifth, and `database_diff` then reported ZERO
	// differences — the recalculation was not absent from the database, it was absent from the
	// QUESTION. A missing table that nothing asks about cannot be noticed by the differ whose whole
	// job is to notice missing tables.
	//
	// ⭐ The descent is deliberately narrow: only recalculations, asked for by the register itself
	// (GetRecalculationArrayObject), rather than a blanket recursion over children — the base's reason
	// for not recursing still holds for the attributes and forms beside them.
	for (const auto recalculation : GetRecalculationArrayObject())
		if (recalculation != nullptr)
			recalculation->ContributeTables(out);
}
