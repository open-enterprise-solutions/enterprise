////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From(slice) + Select materialises the slice through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR for the slice self-join (ComputeSlice)
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegCompositeIR (shared lowering)

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cFilter)
{
	ibRequireOpenBase();

	// The Structure a script passes becomes the condition here — the SAME converter the query door
	// uses, so a script's filter and a query's condition are one thing from this point on.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);

	// Filtered read through the L3 door: each selected dimension is an Eq condition,
	// decomposed inside L3 across its physical fields. Rows come from the L3
	// selection (GetValue) — no statement, no raw result set here.
	//
	// 🛑 NO `catch (...) {}` AROUND THE READS OF THIS FILE ANY MORE: a failed read reached the script as an
	// empty table, which reads as "there is no such record". The error goes to whoever asked, as it does on
	// the calculation register (calculationRegisterManager_impl.cpp).
	ibDataQueryBuilder q;
	q.From(m_metaObject->GetQueryable());
	q.Where(filter);
	ibReadPageRequest page;
	page.m_count = 0;   // every matching record
	ibDataQueryResult selection = q.Execute(page);
	return ibRegSelectionToTable(selection, m_metaObject->GetQueryable());
}

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibRequireOpenBase();

	// A register with neither a period nor a recorder keeps no record AT a period: the answer is its
	// columns and no rows, as its slices answer (ComputeSlice) — an empty selection on the same road.
	if (m_metaObject->GetPeriodicity() == ibPeriodicity::eNonPeriodic &&
		m_metaObject->GetWriteRegisterMode() != ibWriteRegisterMode::eSubordinateRecorder) {
		ibDataQueryResult none(ibQueryRamTable{}, m_metaObject->GetQueryable());
		return ibRegSelectionToTable(none, m_metaObject->GetQueryable());
	}

	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);

	// Period + dimension filtered read through the L3 door: the period is the key's condition
	// (ibRegWhereKeyValue — the whole period), the dimensions the filter's; L3
	// decomposes each across its physical fields and binds them. Rows come from the L3
	// selection (GetValue) — no raw statement, no per-DBMS SQL here.
	ibDataQueryBuilder q;
	q.From(m_metaObject->GetQueryable());
	ibRegWhereKeyValue(q, m_metaObject, m_metaObject->GetRegisterPeriod(), cPeriod);
	q.Where(filter);
	ibReadPageRequest page;
	page.m_count = 0;   // every matching record
	ibDataQueryResult selection = q.Execute(page);
	return ibRegSelectionToTable(selection, m_metaObject->GetQueryable());
}

// SelectionToRecord — the single boundary row Get* returns, as a structure. It reads every
// generic attribute through the uniform selection surface (GetValue) — it does not know the
// rows were computed in RAM. (Slice* return the whole table: ibRegSelectionToTable, over the
// slice's columns, which are the register's.)
static ibValue SelectionToRecord(ibDataQueryResult& selection,
                                 const ibValueMetaObjectInformationRegister* meta)
{
	ibValueStructure* record = new ibValueStructure();
	for (const auto object : meta->GetGenericAttributeArrayObject())
		record->SetAt(object->GetName(), ibValue());
	if (selection.Next())
		for (const auto object : meta->GetGenericAttributeArrayObject())
			record->SetAt(object->GetName(), selection.GetValue(object->GetQueryColumn()));
	return record;
}

// The four period-slice retrievals are RUNTIME entry points, but they pull their data
// THROUGH L3 like any other query: build the slice companion queryable with its filters
// in the ctor (period + dimension filter — "the filter before the Where"), hand it to
// From(), let L3 pull the rows. The very same queryable is what a materialised query /
// JOIN feeds into From() — runtime and a composed query hit one identical path. Slice*
// return the table; Get* the boundary row as a structure.
ibValue ibValueManagerDataObjectInformationRegister::GetFirst(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceFirstQueryable slice(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToRecord(selection, m_metaObject);
}

ibValue ibValueManagerDataObjectInformationRegister::GetLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceLastQueryable slice(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToRecord(selection, m_metaObject);
}

ibValue ibValueManagerDataObjectInformationRegister::SliceFirst(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceFirstQueryable slice(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return ibRegSelectionToTable(selection, &slice);
}

ibValue ibValueManagerDataObjectInformationRegister::SliceLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceLastQueryable slice(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return ibRegSelectionToTable(selection, &slice);
}

