////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumlation manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"        // L2 — structured IR for balance / turnover aggregates
#include "backend/databaseLayer/databaseResultSet.h"           // raw result set for materialisation
#include "backend/query/dataQueryBuilder.h"                     // L3 door — From(balance/turnover queryable).Select()
#include "backend/query/dbTableProvider.h"                      // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegValueField / ibRegCompositeIR
#include "backend/appData.h"
#include "backend/session/session.h"

// ============================================================================
// Balance / turnover COMPUTE — on the register metaobject (its own aggregate-query
// knowledge, built as L2 IR). Each returns a RAM table the companion queryable hands
// to L3 via ComputeRows. Mirrors ibValueMetaObjectInformationRegister::ComputeSlice.
// ============================================================================

ibQueryRamTable ibValueMetaObjectAccumulationRegister::ComputeBalance(const ibValue& cPeriod, const ibValue& cFilter) const
{
	// L3's own table (no runtime ibValueModelTable). Dimensions keyed by metaID (so a
	// composed read can reach them by Value(col)); the derived _Balance columns by a
	// synthetic id, read by name. The script egress (SelectionToBalanceTable) rebuilds the
	// runtime table from the selection by name.
	ibQueryRamTable retTable;
	for (const auto dimension : GetDimensionArrayObject())
		retTable.AddColumn(dimension->GetMetaID(), dimension->GetName(), dimension->GetTypeDesc());
	ibMetaID derivedId = 0x40000000u;
	for (const auto object : GetResourceArrayObject())
		retTable.AddColumn(derivedId++, object->GetName() + "_Balance", object->GetTypeDesc());

	if (GetRegisterType() != ibRegisterType::eBalances)
		return retTable;   // a turnover-only register carries no balances

	ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
	if (cFilter.ConvertToValue(valFilter))
		for (const auto object : GetDimensionArrayObject()) {
			ibValue vSelValue;
			if (valFilter->Property(object->GetName(), vSelValue)) selFilter.insert_or_assign(object, vSelValue);
		}

	// SUM(CASE WHEN recordType = 0 THEN -res ELSE res END) up to the period, grouped by
	// the dimension key, kept non-zero. Values ride as bound Const.
	const wxString table = GetTableNameDB();
	const wxString recordTypeField = ibRegValueField(GetRegisterRecordType());
	const ibQueryExprPtr zero = ibConst(ibValue(0.0));
	auto balanceExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		ibQueryExprPtr cond = ibBinOp(ibQueryBinOp::Eq, ibCol(recordTypeField), zero);
		ibQueryExprPtr neg  = ibBinOp(ibQueryBinOp::Sub, zero, ibCol(resField));
		ibQueryExprPtr body = ibCase({ { cond, neg } }, ibCol(resField));
		return ibCast(ibFunc(wxT("SUM"), { body }), wxT("NUMERIC"));
	};

	std::vector<ibQueryProjItem> proj;
	std::vector<ibQueryExprPtr>  groupKeys;
	for (const auto object : GetDimensionArrayObject())
		for (const wxString& f : ibRegFieldsOf(object)) { proj.push_back(ibQueryProjItem{ ibCol(f), wxString() }); groupKeys.push_back(ibCol(f)); }
	for (const auto object : GetResourceArrayObject()) {
		const wxString resType = ibRegFieldsOf(object).front();
		proj.push_back(ibQueryProjItem{ ibCol(resType), wxString() }); groupKeys.push_back(ibCol(resType));
		proj.push_back(ibQueryProjItem{ balanceExpr(object), ibRegValueField(object) + wxT("_Balance_") });
	}
	ibQueryExprPtr having;
	for (const auto object : GetResourceArrayObject()) {
		ibQueryExprPtr nz = ibBinOp(ibQueryBinOp::Ne, balanceExpr(object), zero);
		having = having ? ibBinOp(ibQueryBinOp::Or, having, nz) : nz;
	}

	ibDatabaseQueryBuilder q;
	q.From(table)
	 .Where(ibRegCompositeIR(GetRegisterActive(), ibValue(true), ibQueryBinOp::Eq))
	 .Where(ibRegCompositeIR(GetRegisterPeriod(), cPeriod, ibQueryBinOp::Le))
	 .Project(proj);
	for (auto filter : selFilter)
		q.Where(ibRegCompositeIR(filter.first, filter.second, ibQueryBinOp::Eq));
	for (const ibQueryExprPtr& gk : groupKeys) q.GroupBy(gk);
	if (having) q.Having(having);

	try {
		ibQueryResult rs = ibDatabaseQueryBuilder().ExecuteIR(q.Build());
		while (rs.Next()) {
			const long retRow = retTable.AppendRow();
			for (const auto object : GetDimensionArrayObject()) {
				ibValue retVal;
				if (ibDbTableProvider::GetValueAttribute(object, retVal, rs))
					retTable.SetByName(retRow, object->GetName(), retVal);
			}
			for (const auto object : GetResourceArrayObject()) {
				ibValue retVal;
				if (ibDbTableProvider::GetValueAttribute(object->GetFieldNameDB() + "_N_Balance_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retVal, rs))
					retTable.SetByName(retRow, object->GetName() + "_Balance", retVal);
			}
		}
	}
	catch (...) {}

	return retTable;
}

// ============================================================================
// Companion queryables — navigation forwards to the register (shared in the
// ibComputedRegisterQueryable base); ComputeRows pulls the RAM table from the register's
// compute (the filters baked into the ctor).
// ============================================================================
ibQueryRamTable ibBalanceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const { return m_reg->ComputeBalance(m_period, m_filter); }
ibQueryRamTable ibTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const { return m_reg->ComputeTurnover(m_begin, m_end, m_filter); }

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
		cols->AddColumn(object->GetName() + "_Balance", object->GetTypeDesc(), object->GetSynonym() + " " + _("Balance"));
	while (selection.Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		wxASSERT(line);
		for (const auto dimension : meta->GetDimensionArrayObject())
			line->SetAt(dimension->GetName(), selection.GetColumn(dimension->GetName()));
		for (const auto object : meta->GetResourceArrayObject())
			line->SetAt(object->GetName() + "_Balance", selection.GetColumn(object->GetName() + "_Balance"));
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

	ibBalanceQueryable balance(m_metaObject, cPeriod, cFilter);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&balance).Execute(ibReadPageRequest{});
	return SelectionToBalanceTable(selection, m_metaObject);
}

ibQueryRamTable ibValueMetaObjectAccumulationRegister::ComputeTurnover(const ibValue& cBegin, const ibValue& cEnd, const ibValue& cFilter) const
{
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);

	// L3's own table (no runtime type): dimensions keyed by metaID, derived turnover /
	// receipt / expense columns by a synthetic id, all read by name downstream.
	ibQueryRamTable retTable;
	for (const auto dimension : GetDimensionArrayObject())
		retTable.AddColumn(dimension->GetMetaID(), dimension->GetName(), dimension->GetTypeDesc());
	ibMetaID derivedId = 0x40000000u;
	for (const auto object : GetResourceArrayObject()) {
		retTable.AddColumn(derivedId++, object->GetName() + wxT("_Turnover"), object->GetTypeDesc());
		if (withSign) {
			retTable.AddColumn(derivedId++, object->GetName() + wxT("_Receipt"), object->GetTypeDesc());
			retTable.AddColumn(derivedId++, object->GetName() + wxT("_Expense"), object->GetTypeDesc());
		}
	}

	ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
	if (cFilter.ConvertToValue(valFilter))
		for (const auto object : GetDimensionArrayObject()) {
			ibValue vSelValue;
			if (valFilter->Property(object->GetName(), vSelValue)) selFilter.insert_or_assign(object, vSelValue);
		}

	// For a balance register the record type signs the movement: turnover = SUM(±res),
	// receipt = SUM(res WHEN type=0), expense = SUM(res WHEN type<>0); a pure turnover
	// register has only the plain SUM. Kept non-zero. Values ride as bound Const.
	const wxString table = GetTableNameDB();
	const wxString recordTypeField = withSign ? ibRegValueField(GetRegisterRecordType()) : wxString();
	const ibQueryExprPtr zero = ibConst(ibValue(0.0));
	auto isReceipt = [&]() { return ibBinOp(ibQueryBinOp::Eq, ibCol(recordTypeField), zero); };
	auto turnoverExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		if (!withSign)
			return ibCast(ibFunc(wxT("SUM"), { ibCol(resField) }), wxT("NUMERIC"));
		ibQueryExprPtr neg = ibBinOp(ibQueryBinOp::Sub, zero, ibCol(resField));
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt(), ibCol(resField) } }, neg) }), wxT("NUMERIC"));
	};
	auto receiptExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt(), ibCol(resField) } }, zero) }), wxT("NUMERIC"));
	};
	auto expenseExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt(), zero } }, ibCol(resField)) }), wxT("NUMERIC"));
	};

	std::vector<ibQueryProjItem> proj;
	std::vector<ibQueryExprPtr>  groupKeys;
	for (const auto object : GetDimensionArrayObject())
		for (const wxString& f : ibRegFieldsOf(object)) { proj.push_back(ibQueryProjItem{ ibCol(f), wxString() }); groupKeys.push_back(ibCol(f)); }
	for (const auto object : GetResourceArrayObject()) {
		const wxString resType = ibRegFieldsOf(object).front();
		const wxString base    = ibRegValueField(object);
		proj.push_back(ibQueryProjItem{ ibCol(resType), wxString() }); groupKeys.push_back(ibCol(resType));
		proj.push_back(ibQueryProjItem{ turnoverExpr(object), base + wxT("_Turnover_") });
		if (withSign) {
			proj.push_back(ibQueryProjItem{ receiptExpr(object), base + wxT("_Receipt_") });
			proj.push_back(ibQueryProjItem{ expenseExpr(object), base + wxT("_Expense_") });
		}
	}
	ibQueryExprPtr having;
	auto orNonZero = [&](ibQueryExprPtr e) {
		ibQueryExprPtr nz = ibBinOp(ibQueryBinOp::Ne, e, zero);
		having = having ? ibBinOp(ibQueryBinOp::Or, having, nz) : nz;
	};
	for (const auto object : GetResourceArrayObject()) {
		orNonZero(turnoverExpr(object));
		if (withSign) { orNonZero(receiptExpr(object)); orNonZero(expenseExpr(object)); }
	}

	ibDatabaseQueryBuilder q;
	q.From(table)
	 .Where(ibRegCompositeIR(GetRegisterActive(), ibValue(true), ibQueryBinOp::Eq))
	 .Where(ibRegCompositeIR(GetRegisterPeriod(), cBegin, ibQueryBinOp::Ge))
	 .Where(ibRegCompositeIR(GetRegisterPeriod(), cEnd, ibQueryBinOp::Le))
	 .Project(proj);
	for (auto filter : selFilter)
		q.Where(ibRegCompositeIR(filter.first, filter.second, ibQueryBinOp::Eq));
	for (const ibQueryExprPtr& gk : groupKeys) q.GroupBy(gk);
	if (having) q.Having(having);

	try {
		ibQueryResult rs = ibDatabaseQueryBuilder().ExecuteIR(q.Build());
		while (rs.Next()) {
			const long retRow = retTable.AppendRow();
			for (const auto object : GetDimensionArrayObject()) {
				ibValue retValue;
				if (ibDbTableProvider::GetValueAttribute(object, retValue, rs))
					retTable.SetByName(retRow, object->GetName(), retValue);
			}
			for (const auto object : GetResourceArrayObject()) {
				ibValue retValue;
				if (ibDbTableProvider::GetValueAttribute(object->GetFieldNameDB() + "_N_Turnover_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValue, rs))
					retTable.SetByName(retRow, object->GetName() + "_Turnover", retValue);
				if (withSign) {
					ibValue retValue1;
					if (ibDbTableProvider::GetValueAttribute(object->GetFieldNameDB() + "_N_Receipt_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValue1, rs))
						retTable.SetByName(retRow, object->GetName() + "_Receipt", retValue1);
					ibValue retValue2;
					if (ibDbTableProvider::GetValueAttribute(object->GetFieldNameDB() + "_N_Expense_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValue2, rs))
						retTable.SetByName(retRow, object->GetName() + "_Expense", retValue2);
				}
			}
		}
	}
	catch (...) {}

	return retTable;
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
	for (const auto object : meta->GetResourceArrayObject()) {
		cols->AddColumn(object->GetName() + wxT("_Turnover"), object->GetTypeDesc(), object->GetSynonym() + " " + _("Turnover"));
		if (withSign) {
			cols->AddColumn(object->GetName() + wxT("_Receipt"), object->GetTypeDesc(), object->GetSynonym() + " " + _("Receipt"));
			cols->AddColumn(object->GetName() + wxT("_Expense"), object->GetTypeDesc(), object->GetSynonym() + " " + _("Expense"));
		}
	}
	while (selection.Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		wxASSERT(line);
		for (const auto dimension : meta->GetDimensionArrayObject())
			line->SetAt(dimension->GetName(), selection.GetColumn(dimension->GetName()));
		for (const auto object : meta->GetResourceArrayObject()) {
			line->SetAt(object->GetName() + wxT("_Turnover"), selection.GetColumn(object->GetName() + wxT("_Turnover")));
			if (withSign) {
				line->SetAt(object->GetName() + wxT("_Receipt"), selection.GetColumn(object->GetName() + wxT("_Receipt")));
				line->SetAt(object->GetName() + wxT("_Expense"), selection.GetColumn(object->GetName() + wxT("_Expense")));
			}
		}
		wxDELETE(line);
	}
	return table;
}

ibValue ibValueManagerDataObjectAccumulationRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cFilter)
{
	if (ses_query == nullptr || !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	ibTurnoverQueryable turnover(m_metaObject, cBeginOfPeriod, cEndOfPeriod, cFilter);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&turnover).Execute(ibReadPageRequest{});
	return SelectionToTurnoverTable(selection, m_metaObject);
}
