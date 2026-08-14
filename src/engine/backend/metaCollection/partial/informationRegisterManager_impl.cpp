////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From(slice) + Select materialises the slice through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR for the slice self-join (ComputeSlice)
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegCompositeIR (shared lowering)

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}

	// The Structure a script passes becomes the condition here — the SAME converter the query door
	// uses, so a script's filter and a query's condition are one thing from this point on.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter);

	// Filtered read through the L3 door: each selected dimension is an Eq condition,
	// decomposed inside L3 across its physical fields. Rows come from the L3
	// selection (GetValue) — no statement, no raw result set here.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
					q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;   // every matching record
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object));
			wxDELETE(retLine);
		}
	}
	catch (...) {}

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				object->GetName(),
				object->GetTypeDesc(),
				object->GetSynonym()
			);
		colInfo->SetColumnID(object->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter);

		// Period + dimension filtered read through the L3 door: the period is an Eq
		// condition like any selected dimension; L3 decomposes each across its physical
		// fields and binds them. Rows come from the L3 selection (GetValue) — no raw
		// statement, no per-DBMS SQL here.
		try {
			ibDataQueryBuilder q;
			q.From(m_metaObject->GetQueryable());
			q.Where(m_metaObject->GetRegisterPeriod(), ibQueryFilterOp::Equal, cPeriod);
							q.Where(filter);
			ibReadPageRequest page;
			page.m_count = 0;   // every matching record
			ibDataQueryResult selection = q.Execute(page);
			while (selection.Next()) {
				ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
				wxASSERT(retLine);
				for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
					retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object));
				wxDELETE(retLine);
			}
		}
		catch (...) {}
	}

	return retTable;
}

// SelectionToTable / SelectionToRecord — materialise an L3 selection into the shapes
// the runtime methods return: Slice* yield the full table, Get* the single boundary
// row as a structure. Both read every generic attribute through the uniform selection
// surface (GetValue) — they do not know the rows were computed in RAM.
static ibValue SelectionToTable(ibDataQueryResult& selection,
                                const ibValueMetaObjectInformationRegister* meta)
{
	ibValueModelTable* table = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();
	wxASSERT(cols);
	for (const auto object : meta->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* col =
			cols->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		col->SetColumnID(object->GetMetaID());
	}
	while (selection.Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		wxASSERT(line);
		for (const auto object : meta->GetGenericAttributeArrayObject())
			line->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object));
		wxDELETE(line);
	}
	return table;
}

static ibValue SelectionToRecord(ibDataQueryResult& selection,
                                 const ibValueMetaObjectInformationRegister* meta)
{
	ibValueStructure* record = new ibValueStructure();
	for (const auto object : meta->GetGenericAttributeArrayObject())
		record->SetAt(object->GetName(), ibValue());
	if (selection.Next())
		for (const auto object : meta->GetGenericAttributeArrayObject())
			record->SetAt(object->GetName(), selection.GetValue(object));
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
	return SelectionToTable(selection, m_metaObject);
}

ibValue ibValueManagerDataObjectInformationRegister::SliceLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceLastQueryable slice(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToTable(selection, m_metaObject);
}

