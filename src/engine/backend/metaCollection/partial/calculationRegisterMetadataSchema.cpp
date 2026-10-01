////////////////////////////////////////////////////////////////////////////
//	Description : Calculation register — the STRUCTURE it declares.
//
//	The register's own table comes from the register-data base, unchanged: a
//	calculation register's columns ARE a register's columns (its extra ones —
//	action period, registration period, base period, calculation type, storno —
//	are predefined attributes, so the base picks them up through
//	FillArrayObjectByPredefinedAttribute like any other).
//
//	What is NOT the base's is the lookup indexes and the recalculation's marks,
//	where the register keeps them. Nothing else is kept beside the records: the
//	fact is a reading of them (calculationRegister.h, "The fact — a reading of
//	the records").
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"

#include "backend/query/schemaSnapshot.h"                           // ibSchemaSnapshot / ibDeclareLookupIndex
#include "backend/metaCollection/dimension/metaDimensionObject.h"    // the dimensions the lookup index leads with

// ⭐⭐ THE RECORDS OF ONE EMPLOYEE — the read every road of this register makes, and nothing indexed it.
//
// The register's key is (Recorder, LineNumber): "the lines of this document". Everything a calculation
// asks is the other way round — which records feed this bonus (GetBase), what did this employee earn this
// month (a sheet) — all of them "the records holding these dimension values, of this type". With no index
// that is the whole register per question, and the register only grows: forty thousand employees are
// forty thousand records a month. So the records carry the dimensions and the type, in that order, as one
// lookup index — as many leading columns as the engine's index holds (ibDeclareLookupIndex). The fact finds a
// record's displacers by it too: the same dimension values, of a type the Displacing section names.
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
	// The register's own table — its lookup index (above), and the index a period's records are read by
	// (Get(Period, Filter)), on the table the base has just declared.
	ibValueMetaObjectRegisterData::ContributeTables(out);
	{
		const ibBackendQueryable* own = GetQueryable();
		ibSchemaTable& t = out.Shared(own->GetQueryTableId(), own->GetQueryTableName());
		ibDeclareLookupIndex(t, t.m_name + wxT("_DIX"), ibCalcLookupKey(this));
		ibDeclareLookupIndex(t, t.m_name + wxT("_RIX"), { GetRegistrationPeriod()->GetQueryColumn() });
	}

	// …AND THE RECALCULATION'S MARKS, where the register keeps them (IsUseRecalculation): the register's own columns
	// (ibRecalculationQueryable), identified by their holder. Found by their key, NOT held unique by it — the columns a
	// write asks "is it there already" by, and a clear deletes by. A mark carries nothing but its key, so two of one
	// say the same thing twice: the write inserts only what is absent, and where two postings mark the same record at
	// once — each its own transaction, neither seeing the other's row — the second stands beside the first instead of
	// failing its posting on a unique index. A reader lists positions once, and a clear takes every copy.
	if (HasRecalculation()) {
		const ibRecalculationQueryable* marks = GetRecalculationQueryable();
		ibSchemaTable& t = out.CreateSchemaTable(marks);
		for (const ibBackendQueryColumn* column : marks->GetColumns())
			t.Add(column);
		ibDeclareLookupIndex(t, t.m_name + wxT("_LIX"), marks->GetPrimaryKeyColumns());
	}
}
