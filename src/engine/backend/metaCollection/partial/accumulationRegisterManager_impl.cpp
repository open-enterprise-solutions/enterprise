////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumlation manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"        // L2-1 — structured IR for the live aggregates
#include "backend/databaseLayer/databaseMaterializeBuilder.h"  // L2-2 — RenderMaterializedRead (the materialised readings)
#include "backend/query/dataQueryBuilder.h"                     // L3 door — From(balance/turnover queryable).Select()
#include "backend/query/dbTableProvider.h"                      // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegValueField / ibRegCompositeIR
#include "backend/appData.h"
#include "backend/session/session.h"


// ⭐⭐ THE COLUMNS COME FROM THE SOURCE — `ibRegSelectionToTable` (registerQueryLowering.h).
//
// Both readings here used to list their own: the dimensions, then a column per resource per figure,
// with the caption built on the spot (`GetSynonym() + " " + _("Balance")`). That was the THIRD
// spelling of names the view and the reading already agreed on, and the drift it produced is on the
// record — `Resource1_Turnover` against `Resource1Turnover`, two names for one number, each
// compiling. The surface now carries the caption too, so asking it for its columns loses nothing and
// gains the one thing this file had that the accounting register's copy did not.
//
// The runtime entry stays thin, exactly like the slice: build the call-scoped queryable (filters in
// the ctor) and read it through L3's From().Select().
ibValue ibValueManagerDataObjectAccumulationRegister::Balance(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibRequireOpenBase();

	// The Structure a script passes becomes the condition here — the same converter the query door uses.
	ibBalanceQueryable balance(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&balance).Execute(ibReadPageRequest{});
	return ibRegSelectionToTable(selection, &balance);
}


ibValue ibValueManagerDataObjectAccumulationRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod,
                                                                const ibValue& cFilter, const ibValue& cPeriodicity)
{
	ibRequireOpenBase();

	// The periodicity is read the way the QUERY side reads it — one function, so a word written in a
	// script and the same word written in a query mean the same thing.
	const ibRegFold fold = ibReadRegisterFold(cPeriodicity);

	ibTurnoverQueryable turnover(m_metaObject, cBeginOfPeriod, cEndOfPeriod, ibRegFilterPredicate(m_metaObject, cFilter), fold);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&turnover).Execute(ibReadPageRequest{});
	// ⚠ AND THE PERIODICITY IS WHY THE LIST COULD NOT STAY HAND-WRITTEN. Asked for months, this
	// reading publishes a `Period` column that the old list did not know about at all — it enumerated
	// dimensions and figures and nothing else. The shape knows, because the shape is what produced it.
	return ibRegSelectionToTable(selection, &turnover);
}

