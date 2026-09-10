////////////////////////////////////////////////////////////////////////////
//	Description : Recalculation — the STRUCTURE it declares.
//
//	ContributeTables and nothing else: the ONE physical table a recalculation
//	becomes in the database — the record being recalculated + the calculation
//	type + one column per dimension child, keyed uniquely by (object, dimensions).
//	Shaped after commonObjectSchema.cpp's register contribution.
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"

#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot / ibSchemaTable

void ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::ContributeTables(ibSchemaSnapshot& out) const
{
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());

	// The two standard columns — the SAME stable pointers the queryable vends: identity from this
	// recalculation's own predefined attributes (the differ tracks them by it), type from the register
	// (ibRecalculationStandardColumn). A column with no identity yet is not declared at all.
	const ibBackendQueryColumn* recalcObject = GetRecalculationObjectColumn();
	const ibBackendQueryColumn* calcType     = GetCalculationTypeColumn();

	for (const ibBackendQueryColumn* standard : { recalcObject, calcType })
		if (standard != nullptr)
			t.Add(standard);

	// One column per dimension child. A dimension HOLDS a query column rather than being one.
	for (const auto dimension : GetDimensionArrayObject())
		t.Add(dimension->GetQueryColumn());

	// THE KEY: the recalculation object, the calculation type, then the dimension columns — the same
	// tuple GetPrimaryKeyColumns answers with (see the note there on why the type belongs in it).
	// UNIQUE — at most one recalculation row per (object, type, dimension tuple).
	//
	// ⚠ OPEN — the import called an `ibDeclareRecordsKey` that does not exist here, whose comment said a
	// wide key "degrades to a non-unique lookup index". This tree answers that question differently and
	// in one place: past the engine's ceiling the identity moves into a hashed column and the plain index
	// rides the leading columns that fit (`ibDeclareDerivedKey`, register-shared-machinery.md §4a/§4a.1),
	// and the ceiling is BOTH segments and bytes. A plain unique index is the faithful translation of the
	// intent and is correct for every key under the ceiling; a recalculation keyed on wide string
	// dimensions is the case still to decide, and it belongs with the rest of that machinery rather than
	// with a second, weaker rule declared here.
	const std::vector<const ibBackendQueryColumn*> idxCols = GetQueryable()->GetPrimaryKeyColumns();
	if (!idxCols.empty())
		t.Index(t.m_name + wxT("_INDEX"), idxCols, /*unique*/ true);
}
