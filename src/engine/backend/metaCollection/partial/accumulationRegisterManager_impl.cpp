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
#include "backend/databaseLayer/databaseResultSet.h"           // raw result set for materialisation
#include "backend/query/dataQueryBuilder.h"                     // L3 door — From(balance/turnover queryable).Select()
#include "backend/query/dbTableProvider.h"                      // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegValueField / ibRegCompositeIR
#include "backend/appData.h"
#include "backend/session/session.h"


// SelectionToBalanceTable — materialise the balance selection (dimensions + the derived
// resource-balance columns, all read from the RAM source by output name) into the table
// the runtime returns. The consumer never learns the rows were computed in RAM.
static ibValue SelectionToBalanceTable(ibDataQueryResult& selection,
                                       const ibValueMetaObjectAccumulationRegister* meta)
{
	ibValueModelTable* table = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();
	wxASSERT(cols);
	for (const auto dimension : meta->GetDimensionArrayObject())
		cols->AddColumn(dimension->GetName(), dimension->GetTypeDesc(), dimension->GetSynonym());
	for (const auto object : meta->GetResourceArrayObject())
		cols->AddColumn(object->GetName() + ibRegFigure::Balance, object->GetTypeDesc(), object->GetSynonym() + " " + _("Balance"));
	while (selection.Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		wxASSERT(line);
		for (const auto dimension : meta->GetDimensionArrayObject())
			line->SetAt(dimension->GetName(), selection.GetColumn(dimension->GetName()));
		for (const auto object : meta->GetResourceArrayObject())
			line->SetAt(object->GetName() + ibRegFigure::Balance, selection.GetColumn(object->GetName() + ibRegFigure::Balance));
		wxDELETE(line);
	}
	return table;
}

// The runtime entry — thin, exactly like the slice: build the call-scoped balance
// queryable (filters in the ctor) and read it through L3's From().Select().
ibValue ibValueManagerDataObjectAccumulationRegister::Balance(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query == nullptr || !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	// The Structure a script passes becomes the condition here — the same converter the query door uses.
	ibBalanceQueryable balance(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&balance).Execute(ibReadPageRequest{});
	return SelectionToBalanceTable(selection, m_metaObject);
}


// SelectionToTurnoverTable — like SelectionToBalanceTable, with the three derived
// columns (Turnover always; Receipt / Expense for a balance register).
static ibValue SelectionToTurnoverTable(ibDataQueryResult& selection,
                                        const ibValueMetaObjectAccumulationRegister* meta)
{
	const bool withSign = (meta->GetRegisterType() == ibRegisterType::eBalances);

	ibValueModelTable* table = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();
	wxASSERT(cols);
	for (const auto dimension : meta->GetDimensionArrayObject())
		cols->AddColumn(dimension->GetName(), dimension->GetTypeDesc(), dimension->GetSynonym());
	// ⭐ THE NAMES THE QUERY USES, here too. The runtime's table is the SAME table under another
	// entrance, so a script reading `Turnovers()` and a query naming the virtual table see one set of
	// column names. (This renamed the runtime's columns — `Resource1Turnover` rather than
	// `Resource1_Turnover` — because the query's spelling is the one that had to win.)
	for (const auto object : meta->GetResourceArrayObject()) {
		cols->AddColumn(object->GetName() + ibRegFigure::Turnover, object->GetTypeDesc(), object->GetSynonym() + " " + _("Turnover"));
		if (withSign) {
			cols->AddColumn(object->GetName() + ibRegFigure::Receipt, object->GetTypeDesc(), object->GetSynonym() + " " + _("Receipt"));
			cols->AddColumn(object->GetName() + ibRegFigure::Expense, object->GetTypeDesc(), object->GetSynonym() + " " + _("Expense"));
		}
	}
	while (selection.Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		wxASSERT(line);
		for (const auto dimension : meta->GetDimensionArrayObject())
			line->SetAt(dimension->GetName(), selection.GetColumn(dimension->GetName()));
		for (const auto object : meta->GetResourceArrayObject()) {
			line->SetAt(object->GetName() + ibRegFigure::Turnover, selection.GetColumn(object->GetName() + ibRegFigure::Turnover));
			if (withSign) {
				line->SetAt(object->GetName() + ibRegFigure::Receipt, selection.GetColumn(object->GetName() + ibRegFigure::Receipt));
				line->SetAt(object->GetName() + ibRegFigure::Expense, selection.GetColumn(object->GetName() + ibRegFigure::Expense));
			}
		}
		wxDELETE(line);
	}
	return table;
}

ibValue ibValueManagerDataObjectAccumulationRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod,
                                                                const ibValue& cFilter, const ibValue& cPeriodicity)
{
	if (ses_query == nullptr || !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	// The periodicity is read the way the QUERY side reads it — one function, so a word written in a
	// script and the same word written in a query mean the same thing.
	const ibRegFold fold = ibReadRegisterFold(cPeriodicity);

	ibTurnoverQueryable turnover(m_metaObject, cBeginOfPeriod, cEndOfPeriod, ibRegFilterPredicate(m_metaObject, cFilter), fold);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&turnover).Execute(ibReadPageRequest{});
	return SelectionToTurnoverTable(selection, m_metaObject);
}

