////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From(slice) + Select materialises the slice through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR for the slice self-join (ComputeSlice)
#include "backend/databaseLayer/databaseResultSet.h"      // raw result set for slice materialisation
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegCompositeIR (shared lowering)

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}

	ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
	if (cFilter.ConvertToValue(valFilter)) {
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValue vSelValue;
			if (valFilter->Property(object->GetName(), vSelValue)) {
				selFilter.insert_or_assign(
					object, vSelValue
				);
			}
		}
	}

	// Filtered read through the L3 door: each selected dimension is an Eq condition,
	// decomposed inside L3 across its physical fields. Rows come from the L3
	// selection (GetValue) — no statement, no raw result set here.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		for (auto filter : selFilter)
			q.Where(filter.first, ibComparisonType::ibComparisonType_Equal, filter.second);
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
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

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
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		// Period + dimension filtered read through the L3 door: the period is an Eq
		// condition like any selected dimension; L3 decomposes each across its physical
		// fields and binds them. Rows come from the L3 selection (GetValue) — no raw
		// statement, no per-DBMS SQL here.
		try {
			ibDataQueryBuilder q;
			q.From(m_metaObject->GetQueryable());
			q.Where(m_metaObject->GetRegisterPeriod(), ibComparisonType::ibComparisonType_Equal, cPeriod);
			for (auto filter : selFilter)
				q.Where(filter.first, ibComparisonType::ibComparisonType_Equal, filter.second);
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
	ibSliceFirstQueryable slice(m_metaObject, cPeriod, cFilter);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToRecord(selection, m_metaObject);
}

ibValue ibValueManagerDataObjectInformationRegister::GetLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceLastQueryable slice(m_metaObject, cPeriod, cFilter);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToRecord(selection, m_metaObject);
}

ibValue ibValueManagerDataObjectInformationRegister::SliceFirst(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceFirstQueryable slice(m_metaObject, cPeriod, cFilter);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToTable(selection, m_metaObject);
}

// ibValueMetaObjectInformationRegister::ComputeSlice — THE one place the period-slice
// is computed, and it belongs to the REGISTER (it is the register's own table /
// dimensions / SQL knowledge): build the self-join query, run it, materialise the
// slice table (one row per dimension key). The slice companion queryable calls it
// through m_reg, so BOTH the runtime (the manager's Slice* / Get*) and the L3 door
// (ComputeRows, when it materialises a query) reach this one compute — no parallel
// paths. The period bound is the only difference: last = MAX / "<=", first = MIN / ">=".
ibQueryRamTable ibValueMetaObjectInformationRegister::ComputeSlice(
	const ibValue& cPeriod, const ibValue& cFilter,
	const wxString& aggregateFn, const wxString& compareOp) const
{
	const ibValueMetaObjectInformationRegister* meta = this;

	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	// L3's own table (no runtime ibValueModelTable) — keyed by metaID; the script egress
	// (SelectionToTable) rebuilds the runtime table from the selection + metadata.
	ibQueryRamTable retTable;
	for (const auto object : meta->GetGenericAttributeArrayObject())
		retTable.AddColumn(object->GetMetaID(), object->GetName(), object->GetTypeDesc());

	if (meta->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		meta->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : meta->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		// Build the slice query through L2 (structured IR, dialect-free) instead of
		// hand-concatenated SQL: an aggregate subquery finds the boundary period per
		// dimension key (agg over the period, GROUP BY the keys), self-joined back to the
		// register table for the full record, then the dimension filter. Values ride as
		// bound Const — the renderer binds them; no per-DBMS SQL, no manual positional bind.
		const wxString table = meta->GetPhysicalTableName();
		const ibValueMetaObjectAttributeBase* periodAttr = meta->GetRegisterPeriod();

		// Field-list + composite-predicate lowering are shared across register managers
		// (registerQueryLowering.h): ibRegFieldsOf / ibRegCompositeIR.

		ibQueryBinOp periodOp = ibQueryBinOp::Le;
		if      (compareOp == wxT(">=")) periodOp = ibQueryBinOp::Ge;
		else if (compareOp == wxT("<"))  periodOp = ibQueryBinOp::Lt;
		else if (compareOp == wxT(">"))  periodOp = ibQueryBinOp::Gt;
		else if (compareOp == wxT("="))  periodOp = ibQueryBinOp::Eq;

		// Level 1 — boundary period per dimension key (aggregate subquery T1). The period
		// TYPE is grouped + projected bare; the period value is aggregated (MAX / MIN);
		// dimensions + resources are grouped + projected (the slice key).
		std::vector<ibQueryProjItem> l1proj;
		std::vector<ibQueryExprPtr>  groupKeys;
		const std::vector<wxString> periodFields = ibRegFieldsOf(periodAttr);
		for (size_t i = 0; i < periodFields.size(); ++i) {
			if (i == 0) {   // _TYPE tag: grouped + projected bare
				l1proj.push_back(ibQueryProjItem{ ibCol(periodFields[i]), wxString() });
				groupKeys.push_back(ibCol(periodFields[i]));
			}
			else {          // period value: aggregated (the boundary), NOT grouped
				l1proj.push_back(ibQueryProjItem{ ibFunc(aggregateFn, { ibCol(periodFields[i]) }), periodFields[i] });
			}
		}
		auto addKeyCols = [&](const ibValueMetaObjectAttributeBase* a) {
			for (const wxString& f : ibRegFieldsOf(a)) {
				l1proj.push_back(ibQueryProjItem{ ibCol(f), wxString() });
				groupKeys.push_back(ibCol(f));
			}
		};
		for (const auto object : meta->GetDimensionArrayObject()) addKeyCols(object);
		for (const auto object : meta->GetResourceArrayObject())  addKeyCols(object);

		ibDatabaseQueryBuilder l1;
		l1.From(table)
		  .Where(ibRegCompositeIR(meta->GetRegisterActive(), ibValue(true), ibQueryBinOp::Eq))
		  .Where(ibRegCompositeIR(periodAttr, cPeriod, periodOp))
		  .Project(l1proj);
		for (const ibQueryExprPtr& gk : groupKeys) l1.GroupBy(gk);

		// Level 2 — join the boundary back to the table for the full record. ON equates
		// every period / dimension / resource field of T1 (boundary) and T2 (table); the
		// projection is T2's same fields (aliased to the bare name -> materialise by name).
		std::vector<const ibValueMetaObjectAttributeBase*> joinAttrs;
		joinAttrs.push_back(periodAttr);
		for (const auto object : meta->GetDimensionArrayObject()) joinAttrs.push_back(object);
		for (const auto object : meta->GetResourceArrayObject())  joinAttrs.push_back(object);

		ibQueryExprPtr onPred;
		std::vector<ibQueryProjItem> l2proj;
		for (const auto a : joinAttrs)
			for (const wxString& f : ibRegFieldsOf(a)) {
				ibQueryExprPtr eq = ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("T1"), f), ibCol(wxT("T2"), f));
				onPred = onPred ? ibBinOp(ibQueryBinOp::And, onPred, eq) : eq;
				l2proj.push_back(ibQueryProjItem{ ibCol(wxT("T2"), f), f });
			}

		ibDatabaseQueryBuilder l2;
		l2.From(ibSubquery(l1.Build().m_root, wxT("T1")))
		  .Join(ibScan(table, wxT("T2")), onPred, ibQueryJoinType::Inner)
		  .Project(l2proj);

		// Level 3 — dimension filter over the joined slice: SELECT * FROM (l2) AS lastTable WHERE dims.
		ibDatabaseQueryBuilder l3;
		l3.From(ibSubquery(l2.Build().m_root, wxT("lastTable")));
		for (auto filter : selFilter)
			l3.Where(ibRegCompositeIR(filter.first, filter.second, ibQueryBinOp::Eq));

		// Run through L2 (session holder) + materialise the slice table BY metaID — the
		// columns read back the same way the door / record form read them.
		try {
			ibQueryResult rs = ibDatabaseQueryBuilder().ExecuteIR(l3.Build());
			while (rs.Next()) {
				const long retRow = retTable.AppendRow();
				for (const auto object : meta->GetGenericAttributeArrayObject()) {
					ibValue retValue;
					if (ibDbTableProvider::GetValueAttribute(object, retValue, rs))
						retTable.SetCell(retRow, object->GetMetaID(), retValue);
				}
			}
		}
		catch (...) {}
	}

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::SliceLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibSliceLastQueryable slice(m_metaObject, cPeriod, cFilter);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&slice).Execute(ibReadPageRequest{});
	return SelectionToTable(selection, m_metaObject);
}

//********************************************************************************************
//*    Slice companion queryable — a self-contained relation L3 reads / joins                *
//********************************************************************************************
// ComputeRows is the door's read hook for a computed (RAM) source: it builds the slice
// table from the filters baked into THIS instance's ctor (period + dimension filter)
// through the register's own compute (m_reg->ComputeSlice). Both the runtime (the
// manager's Slice* / Get*, which construct a configured slice and From() it) and a
// materialised query / JOIN reach that one place. All navigation forwards to the register.

ibQueryRamTable ibSliceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	// The slice's own scoping (period + dimension filter) rode in the ctor; build the
	// table from it. Extra door conditions (a further Where on the slice) compose on
	// top once the post-filter / JOIN-over-computed node lands — the manager path
	// passes none.
	return m_reg->ComputeSlice(m_period, m_filter, AggregateFn(), CompareOp());
}

// (The eight navigation methods forward to the register's own queryable — now shared in
// the ibComputedRegisterQueryable base; a slice returns the register's real records.)
