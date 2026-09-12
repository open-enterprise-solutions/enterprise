////////////////////////////////////////////////////////////////////////////
//	Description : Recalculation — the STRUCTURE it declares.
//
//	ContributeTables and nothing else: the ONE physical table a recalculation
//	becomes in the database — the record being recalculated + the calculation
//	type + one column per dimension child (+ the month, where the register keeps
//	action periods), keyed uniquely by all of them. Shaped after
//	commonObjectSchema.cpp's register contribution.
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"

#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot / ibSchemaTable

void ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::ContributeTables(ibSchemaSnapshot& out) const
{
	ibSchemaTable& t = out.CreateSchemaTable(GetQueryable());

	// The two standard columns — the SAME stable pointers the queryable vends: identity from this
	// recalculation's own predefined attributes (the differ tracks them by it), type from the register
	// (ibRecalculationStandardColumn). A column with no identity yet is not declared at all.
	for (const ibBackendQueryColumn* standard : { GetRecalculationObjectColumn(), GetCalculationTypeColumn(), GetActionPeriodColumn() })
		if (standard != nullptr)
			t.Add(standard);

	// One column per dimension child. A dimension HOLDS a query column rather than being one.
	for (const auto dimension : GetDimensionArrayObject())
		t.Add(dimension->GetQueryColumn());

	// THE KEY: the recalculation object, the calculation type, the dimension columns and — where the
	// register keeps action periods — the month the marked record is for; the same tuple
	// GetPrimaryKeyColumns answers with (see the note there on why the type belongs in it). UNIQUE — at
	// most one mark per position.
	//
	// ⭐ AND PAST THE ENGINE'S CEILING, THE REGISTERS' ANSWER, NOT A SECOND ONE (2026-09-11). A recorder,
	// a type and three reference dimensions are already more than Firebird's sixteen index segments (a
	// reference is three fields), and a plain unique index then refused the apply. The identity moves
	// into a digest column and a plain index rides the leading columns that fit (ibDeclareDerivedKey,
	// register-shared-machinery.md §4a/§4a.1). The digest's identity is this recalculation's number in
	// the band the totals use for theirs, so the differ can add it to a table that already exists; the
	// index keeps its old name, so a key that fits changes nothing on a base that already has it.
	const std::vector<const ibBackendQueryColumn*> idxCols = GetQueryable()->GetPrimaryKeyColumns();
	if (!idxCols.empty())
		ibDeclareDerivedKey(t, t.m_name, idxCols, GetMetaID() | 0x40000000, t.m_name + wxT("_INDEX"));
}
