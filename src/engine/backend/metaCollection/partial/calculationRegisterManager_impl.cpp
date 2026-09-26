////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"
#include "calculationRegisterManager.h"
#include "chartOfCalculationTypes.h"   // the charts a register's related registers are bound to

#include "backend/system/value/valueTable.h"
#include "backend/appData.h"
#include "backend/metaData.h"   // GetAnyArrayObject — the registers this one's chart may name (GetRelatedRegisters)
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From() + Where materialises the read through L3
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFilterPredicate (shared lowering)

#include <algorithm>

ibValue ibValueManagerDataObjectCalculationRegister::Get(const ibValue& cFilter)
{
	ibRequireOpenBase();

	// The Structure a script passes becomes the condition here — the SAME converter the query door
	// uses, so a script's filter and a query's condition are one thing from this point on.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);

	// Filtered read through the L3 door: each selected dimension is an Eq condition,
	// decomposed inside L3 across its physical fields. Rows come from the L3
	// selection (GetValue) — no statement, no raw result set here.
	//
	// 🛑 NO `catch (...) {}` AROUND THE READS OF THIS FILE ANY MORE. Every one of them turned a failed
	// read — a missing column, a refused statement — into an empty table, which a payroll calculation
	// then reads as "nothing was accrued". The error goes to whoever asked.
	ibDataQueryBuilder q;
	q.From(m_metaObject->GetQueryable());
	q.Where(filter);
	ibReadPageRequest page;
	page.m_count = 0;   // every matching record
	ibDataQueryResult selection = q.Execute(page);
	return ibRegSelectionToTable(selection, m_metaObject->GetQueryable());
}

ibValue ibValueManagerDataObjectCalculationRegister::Get(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibRequireOpenBase();

	// A calculation register is always dated — by its REGISTRATION period, the one period it has.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);

	// Period + dimension filtered read through the L3 door: the period is an Eq
	// condition like any selected dimension; L3 decomposes each across its physical
	// fields and binds them. Rows come from the L3 selection (GetValue) — no raw
	// statement, no per-DBMS SQL here.
	ibDataQueryBuilder q;
	q.From(m_metaObject->GetQueryable());
	q.Where(m_metaObject->GetRegistrationPeriod()->GetQueryColumn(), ibQueryFilterOp::Equal, cPeriod);
	q.Where(filter);
	ibReadPageRequest page;
	page.m_count = 0;   // every matching record
	ibDataQueryResult selection = q.Execute(page);
	return ibRegSelectionToTable(selection, m_metaObject->GetQueryable());
}

// GetBase(Filter, Resources, Dimensions, Sections) — the base of the records the filter chooses: the recorder at
// least, and any dimensions, each by equality. The records are read here, as they stand in the register, and asked
// of the register's reading (ibCalcReadBase) in the order of their lines; nothing is computed in the manager.
ibValue ibValueManagerDataObjectCalculationRegister::GetBase(const ibValue& cFilter, const ibValue& cResources,
	const ibValue& cDimensions, const ibValue& cSections)
{
	ibRequireOpenBase();

	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(filter, leaves);
	const ibBackendQueryColumn* recorder = m_metaObject->GetRegisterRecorder()->GetQueryColumn();
	if (std::none_of(leaves.begin(), leaves.end(), [recorder](const auto& leaf) { return leaf.first == recorder; }))
		ibBackendCoreException::Error(_("GetBase: the filter must name the recorder"));

	const ibCalcBaseAsked asked = ibCalcBaseAskedOf(m_metaObject, cResources, cDimensions, cSections);

	std::vector<ibCalcBaseRecord> records;
	{
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(filter);
		q.OrderBy(m_metaObject->GetRegisterLineNumber()->GetQueryColumn(), /*ascending*/ true);
		ibReadPageRequest page;
		page.m_count = 0;   // every record the filter chooses
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibCalcBaseRecord record;
			record.m_line = selection.GetValue(m_metaObject->GetRegisterLineNumber()->GetQueryColumn());
			record.m_type = selection.GetValue(m_metaObject->GetCalculationType()->GetQueryColumn());
			if (m_metaObject->IsUseBasePeriod()) {   // without one they are not columns of the table (ibCalcReadBase)
				record.m_from = selection.GetValue(m_metaObject->GetBasePeriodStart()->GetQueryColumn()).GetDateTime();
				record.m_to = selection.GetValue(m_metaObject->GetBasePeriodEnd()->GetQueryColumn()).GetDateTime();
			}
			record.m_registration = selection.GetValue(m_metaObject->GetRegistrationPeriod()->GetQueryColumn()).GetDateTime();
			for (const ibCalcBaseAsked::ibPairing& pairing : asked.m_dimensions)
				record.m_dimensions.push_back(selection.GetValue(pairing.m_own->GetQueryColumn()));
			records.push_back(std::move(record));
		}
	}
	return ibCalcReadBase(m_metaObject, asked, records);
}

//***********************************************************************
//*                      The registers this one's chart names            *
//***********************************************************************

std::vector<const ibValueMetaObjectCalculationRegister*> ibValueMetaObjectCalculationRegister::GetRelatedRegisters() const
{
	std::vector<const ibValueMetaObjectCalculationRegister*> out;
	const ibValueMetaObjectChartOfCalculationTypes* chart = GetChartOfCalculationTypes();
	if (chart == nullptr || m_metaData == nullptr)
		return out;
	const ibMetaDescription& otherCharts = chart->GetBaseCharts();
	for (const ibValueMetaObjectCalculationRegister* candidate :
		m_metaData->GetAnyArrayObject<ibValueMetaObjectCalculationRegister>(g_metaCalculationRegisterCLSID)) {
		const ibValueMetaObjectChartOfCalculationTypes* theirs = candidate->GetChartOfCalculationTypes();
		if (theirs != nullptr && (theirs == chart || otherCharts.ContainMetaType(theirs->GetMetaID())))
			out.push_back(candidate);
	}
	return out;
}
