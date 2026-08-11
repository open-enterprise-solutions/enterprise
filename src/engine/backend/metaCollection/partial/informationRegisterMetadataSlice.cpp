////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register metadata - SLICE: the reading and its virtual table
////////////////////////////////////////////////////////////////////////////
//
// ONE PLACE FOR THE SLICE. The reading is a clean function ON THE METAOBJECT (its own table and
// dimensions are what a slice is made of); the companion queryable publishes it to L3; and the
// manager keeps nothing of it - it builds the companion, reads it through the door and dresses the
// rows for a script (informationRegisterManager_impl.cpp).
//
// ⚠ AND THERE IS NO VIEW HERE, on purpose. A slice is "the record nearest a MOMENT", so the answer
// depends on the moment asked for - there is nothing to accumulate and nothing a materialised
// surface could hold. The accumulation register materialises because a SUM does not depend on the
// question; a slice does. The register table IS the source, and the slice is a query over it.
//

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From(slice) + Select materialises the slice through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR for the slice self-join (ComputeSlice)
#include "backend/databaseLayer/databaseResultSet.h"      // raw result set for slice materialisation
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegCompositeIR (shared lowering)


// ibValueMetaObjectInformationRegister::ComputeSlice — THE one place the period-slice
// is computed, and it belongs to the REGISTER (it is the register's own table /
// dimensions / SQL knowledge): build the self-join query, run it, materialise the
// slice table (one row per dimension key). The slice companion queryable calls it
// through m_reg, so BOTH the runtime (the manager's Slice* / Get*) and the L3 door
// (ComputeRows, when it materialises a query) reach this one compute — no parallel
// paths. The period bound is the only difference: last = MAX / "<=", first = MIN / ">=".
ibQueryRamTable ibValueMetaObjectInformationRegister::ComputeSlice(
	const ibValue& cPeriod, const ibQueryPredicatePtr& cFilter, ibSliceEnd end) const
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
		// The condition arrives already converted (ibRegFilterPredicate) — one converter for every
		// register, so a script's Structure and a query's condition are the same thing by the time they
		// get here.
		std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> selFilter;
		ibRegFlatLeaves(cFilter, selFilter);

		// Build the slice query through L2 (structured IR, dialect-free) instead of
		// hand-concatenated SQL: an aggregate subquery finds the boundary period per
		// dimension key (agg over the period, GROUP BY the keys), self-joined back to the
		// register table for the full record, then the dimension filter. Values ride as
		// bound Const — the renderer binds them; no per-DBMS SQL, no manual positional bind.
		const wxString table = meta->GetPhysicalTableName();
		const ibValueMetaObjectAttributeBase* periodAttr = meta->GetRegisterPeriod();

		// Field-list + composite-predicate lowering are shared across register managers
		// (registerQueryLowering.h): ibRegFieldsOf / ibRegCompositeIR.

		// ⭐ BOTH HALVES FROM ONE FACT. The LAST slice looks backwards (`period <= moment`) and takes
		// the greatest period in each group; the FIRST looks forwards and takes the least. Written as
		// two strings passed in separately, the pair could disagree — and the chain that parsed them
		// defaulted silently, so a typo produced a wrong slice instead of a complaint.
		const bool last = (end == ibSliceEnd::Last);
		const ibQueryBinOp periodOp   = last ? ibQueryBinOp::Le : ibQueryBinOp::Ge;
		const wxString     aggregateFn = last ? wxT("MAX") : wxT("MIN");

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
		  .Where(ibRegCompositeIR(meta->GetRegisterActive(), GetMetaData(), ibValue(true), ibQueryBinOp::Eq))
		  .Where(ibRegCompositeIR(periodAttr, GetMetaData(), cPeriod, periodOp))
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
			l3.Where(ibRegCompositeIR(filter.first, GetMetaData(), filter.second, ibQueryBinOp::Eq));

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
	return m_reg->ComputeSlice(m_period, m_filter, End());
}

// (The eight navigation methods forward to the register's own queryable — now shared in
// the ibComputedRegisterQueryable base; a slice returns the register's real records.)
