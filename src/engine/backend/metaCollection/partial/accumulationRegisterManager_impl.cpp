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
	const wxString table = GetPhysicalTableName();
	const wxString recordTypeField = ibRegValueField(GetRegisterRecordType());
	const ibQueryExprPtr zero = ibConst(ibValue(0.0));
	auto balanceExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		ibQueryExprPtr cond = ibBinOp(ibQueryBinOp::Eq, ibCol(recordTypeField), zero);
		ibQueryExprPtr neg  = ibBinOp(ibQueryBinOp::Sub, zero, ibCol(resField));
		ibQueryExprPtr body = ibCase({ { cond, neg } }, ibCol(resField));
		return ibCast(ibFunc(wxT("SUM"), { body }), ibTypeNumber(18, 6));   // pin as generous DECIMAL — bare NUMERIC narrows to (9,0)/(10,0) on FB/MySQL and truncates
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
				if (ibDbTableProvider::GetValueAttribute(object->GetPhysicalName() + "_N_Balance_", ibFieldTypes_Number, object, retVal, rs))
					retTable.SetByName(retRow, object->GetName() + "_Balance", retVal);
			}
		}
	}
	catch (...) {}

	return retTable;
}

// ============================================================================
// Companion queryables — the LIVE fallback.
//
// ComputeRows runs only when there is no materialised surface (IsComputedInRam() is then true —
// on a driver that cannot maintain derived state, i.e. ODBC). With views present the source is a
// derived table instead and these are never called; see GetSourceRelation below.
//
// The fallback is not legacy. It is the only path on such a driver, and it is the ORACLE a parity
// test measures the materialised path against: two roads to the same numbers, and a disagreement
// is a bug in the new one.
// ============================================================================

ibQueryRamTable ibBalanceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeBalance(m_period, m_filter);
}

ibQueryRamTable ibTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeTurnover(m_begin, m_end, m_filter);
}

ibQueryRamTable ibBalanceAndTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeBalanceAndTurnover(m_begin, m_end, m_unit, m_filter);
}

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
	const wxString table = GetPhysicalTableName();
	const wxString recordTypeField = withSign ? ibRegValueField(GetRegisterRecordType()) : wxString();
	const ibQueryExprPtr zero = ibConst(ibValue(0.0));
	auto isReceipt = [&]() { return ibBinOp(ibQueryBinOp::Eq, ibCol(recordTypeField), zero); };
	auto turnoverExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		if (!withSign)
			return ibCast(ibFunc(wxT("SUM"), { ibCol(resField) }), ibTypeNumber(18, 6));   // pin as generous DECIMAL — bare NUMERIC narrows to (9,0)/(10,0) on FB/MySQL and truncates
		ibQueryExprPtr neg = ibBinOp(ibQueryBinOp::Sub, zero, ibCol(resField));
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt(), ibCol(resField) } }, neg) }), ibTypeNumber(18, 6));   // pin as generous DECIMAL — bare NUMERIC narrows to (9,0)/(10,0) on FB/MySQL and truncates
	};
	auto receiptExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt(), ibCol(resField) } }, zero) }), ibTypeNumber(18, 6));   // pin as generous DECIMAL — bare NUMERIC narrows to (9,0)/(10,0) on FB/MySQL and truncates
	};
	auto expenseExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString resField = ibRegValueField(res);
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt(), zero } }, ibCol(resField)) }), ibTypeNumber(18, 6));   // pin as generous DECIMAL — bare NUMERIC narrows to (9,0)/(10,0) on FB/MySQL and truncates
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
				if (ibDbTableProvider::GetValueAttribute(object->GetPhysicalName() + "_N_Turnover_", ibFieldTypes_Number, object, retValue, rs))
					retTable.SetByName(retRow, object->GetName() + "_Turnover", retValue);
				if (withSign) {
					ibValue retValue1;
					if (ibDbTableProvider::GetValueAttribute(object->GetPhysicalName() + "_N_Receipt_", ibFieldTypes_Number, object, retValue1, rs))
						retTable.SetByName(retRow, object->GetName() + "_Receipt", retValue1);
					ibValue retValue2;
					if (ibDbTableProvider::GetValueAttribute(object->GetPhysicalName() + "_N_Expense_", ibFieldTypes_Number, object, retValue2, rs))
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

// ============================================================================
// Balance AND turnover, per period — TWO server-side aggregates plus a shared in-memory fold:
//
//   1. the balance each key carried INTO the interval (everything strictly before `begin`).
//      Skipping it would report correct turnovers on top of balances that all start at zero —
//      the failure that reads like a rounding bug and is not one;
//   2. turnovers inside the interval, GROUPed BY the period TRUNCATED to the requested unit. The
//      unit is a QUERY parameter (the read granularity), and the truncation goes through the
//      dialect map, so the grouping means the same thing on every engine;
//   3. FoldBalancesForward turns (1) + (2) into per-period opening / closing.
//
// Step 3 deliberately lives in query/queryRamTable rather than here: it is identical for an
// accounting register, which signs its movements by debit / credit side instead of by record type
// — by the time rows reach the fold, both arrive as a receipt / expense pair per period.
// ============================================================================
ibQueryRamTable ibValueMetaObjectAccumulationRegister::ComputeBalanceAndTurnover(
	const ibValue& cBegin, const ibValue& cEnd, ibTotalsPeriod unit, const ibValue& cFilter) const
{
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);

	// --- output shape: dimensions, the period, then five reported columns per resource ---
	ibQueryRamTable retTable;
	for (const auto dimension : GetDimensionArrayObject())
		retTable.AddColumn(dimension->GetMetaID(), dimension->GetName(), dimension->GetTypeDesc());

	const ibMetaID periodId = 0x3F000000u;
	retTable.AddColumn(periodId, wxT("Period"), GetRegisterPeriod()->GetTypeDesc());

	ibMetaID derivedId = 0x40000000u;
	std::vector<ibBalanceFoldSlot> slots;
	std::vector<ibValueMetaObjectAttributeBase*> resources;
	for (const auto object : GetResourceArrayObject()) {
		ibBalanceFoldSlot s;
		s.m_opening  = derivedId++;  retTable.AddColumn(s.m_opening,  object->GetName() + wxT("_OpeningBalance"), object->GetTypeDesc());
		s.m_receipt  = derivedId++;  retTable.AddColumn(s.m_receipt,  object->GetName() + wxT("_Receipt"),        object->GetTypeDesc());
		s.m_expense  = derivedId++;  retTable.AddColumn(s.m_expense,  object->GetName() + wxT("_Expense"),        object->GetTypeDesc());
		s.m_turnover = derivedId++;  retTable.AddColumn(s.m_turnover, object->GetName() + wxT("_Turnover"),       object->GetTypeDesc());
		s.m_closing  = derivedId++;  retTable.AddColumn(s.m_closing,  object->GetName() + wxT("_ClosingBalance"), object->GetTypeDesc());
		slots.push_back(s);
		resources.push_back(object);
	}

	ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
	if (cFilter.ConvertToValue(valFilter))
		for (const auto object : GetDimensionArrayObject()) {
			ibValue vSelValue;
			if (valFilter->Property(object->GetName(), vSelValue)) selFilter.insert_or_assign(object, vSelValue);
		}

	const wxString       table           = GetPhysicalTableName();
	const wxString       recordTypeField = withSign ? ibRegValueField(GetRegisterRecordType()) : wxString();
	const ibQueryExprPtr zero            = ibConst(ibValue(0.0));
	const wxString       periodField     = ibRegValueField(GetRegisterPeriod());

	// What a movement contributes to each side. With no record type every movement is a receipt —
	// a turnover-only register has no expense side at all.
	auto receiptExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString f = ibRegValueField(res);
		if (!withSign) return ibCast(ibFunc(wxT("SUM"), { ibCol(f) }), ibTypeNumber(18, 6));
		ibQueryExprPtr isReceipt = ibBinOp(ibQueryBinOp::Eq, ibCol(recordTypeField), zero);
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt, ibCol(f) } }, zero) }), ibTypeNumber(18, 6));
	};
	auto expenseExpr = [&](const ibValueMetaObjectAttributeBase* res) -> ibQueryExprPtr {
		const wxString f = ibRegValueField(res);
		if (!withSign) return ibCast(ibFunc(wxT("SUM"), { zero }), ibTypeNumber(18, 6));
		ibQueryExprPtr isReceipt = ibBinOp(ibQueryBinOp::Eq, ibCol(recordTypeField), zero);
		return ibCast(ibFunc(wxT("SUM"), { ibCase({ { isReceipt, zero } }, ibCol(f)) }), ibTypeNumber(18, 6));
	};

	auto applyFilter = [&](ibDatabaseQueryBuilder& q) {
		for (auto filter : selFilter)
			q.Where(ibRegCompositeIR(filter.first, filter.second, ibQueryBinOp::Eq));
	};

	// The identity string a set of dimension values folds to. Built the SAME way for the opening
	// rows and for the output rows — if the two differed, every opening balance would silently miss
	// its key and the report would look like a fresh start.
	auto keyOfValues = [](const std::vector<ibValue>& values) {
		wxString k;
		for (const ibValue& v : values)
			k += v.GetHashKey() + wxT("\x1f");
		return k;
	};

	// --- 1. OPENING — everything strictly before the interval, per key -------------------------
	std::map<wxString, std::map<ibMetaID, ibNumber>> opening;
	if (withSign) {
		std::vector<ibQueryProjItem> proj;
		std::vector<ibQueryExprPtr>  keys;
		for (const auto dimension : GetDimensionArrayObject())
			for (const wxString& f : ibRegFieldsOf(dimension)) { proj.push_back(ibQueryProjItem{ ibCol(f), wxString() }); keys.push_back(ibCol(f)); }
		for (const auto object : resources)
			proj.push_back(ibQueryProjItem{
				ibBinOp(ibQueryBinOp::Sub, receiptExpr(object), expenseExpr(object)),
				ibRegValueField(object) + wxT("_Open_") });

		ibDatabaseQueryBuilder q;
		q.From(table)
		 .Where(ibRegCompositeIR(GetRegisterActive(), ibValue(true), ibQueryBinOp::Eq))
		 .Where(ibRegCompositeIR(GetRegisterPeriod(), cBegin, ibQueryBinOp::Lt))
		 .Project(proj);
		applyFilter(q);
		for (const ibQueryExprPtr& k : keys) q.GroupBy(k);

		try {
			ibQueryResult rs = ibDatabaseQueryBuilder().ExecuteIR(q.Build());
			while (rs.Next()) {
				std::vector<ibValue> keyValues;
				for (const auto dimension : GetDimensionArrayObject()) {
					ibValue v;
					ibDbTableProvider::GetValueAttribute(dimension, v, rs);
					keyValues.push_back(v);
				}
				const wxString key = keyOfValues(keyValues);
				for (size_t i = 0; i < resources.size(); i++) {
					ibValue v;
					if (ibDbTableProvider::GetValueAttribute(resources[i]->GetPhysicalName() + wxT("_N_Open_"),
					                                         ibFieldTypes_Number, resources[i], v, rs))
						opening[key][slots[i].m_turnover] = v.GetNumber();
				}
			}
		}
		catch (...) {}
	}

	// --- 2. TURNOVERS inside the interval, grouped by the truncated period ---------------------
	{
		const ibQueryExprPtr periodKey = ibPeriodTrunc(ibCol(periodField), unit);

		std::vector<ibQueryProjItem> proj;
		std::vector<ibQueryExprPtr>  keys;
		for (const auto dimension : GetDimensionArrayObject())
			for (const wxString& f : ibRegFieldsOf(dimension)) { proj.push_back(ibQueryProjItem{ ibCol(f), wxString() }); keys.push_back(ibCol(f)); }
		proj.push_back(ibQueryProjItem{ periodKey, periodField + wxT("_P_") });
		keys.push_back(periodKey);
		for (const auto object : resources) {
			proj.push_back(ibQueryProjItem{ receiptExpr(object), ibRegValueField(object) + wxT("_Receipt_") });
			proj.push_back(ibQueryProjItem{ expenseExpr(object), ibRegValueField(object) + wxT("_Expense_") });
		}

		ibDatabaseQueryBuilder q;
		q.From(table)
		 .Where(ibRegCompositeIR(GetRegisterActive(), ibValue(true), ibQueryBinOp::Eq))
		 .Where(ibRegCompositeIR(GetRegisterPeriod(), cBegin, ibQueryBinOp::Ge))
		 .Where(ibRegCompositeIR(GetRegisterPeriod(), cEnd,   ibQueryBinOp::Le))
		 .Project(proj);
		applyFilter(q);
		for (const ibQueryExprPtr& k : keys) q.GroupBy(k);

		// Key first, period last: the fold carries a running balance across each key's rows, so a
		// key's periods must ARRIVE in order — an unordered read would interleave two keys and
		// blend their histories into each other.
		//
		// ORDER BY takes column NAMES, not expressions, so the truncated period is ordered through
		// its projection ALIAS (every engine here accepts one); the dimensions order by their fields.
		for (const auto dimension : GetDimensionArrayObject())
			for (const wxString& f : ibRegFieldsOf(dimension))
				q.OrderBy(f, ibQuerySortDir::Asc);
		q.OrderBy(periodField + wxT("_P_"), ibQuerySortDir::Asc);

		try {
			ibQueryResult rs = ibDatabaseQueryBuilder().ExecuteIR(q.Build());
			while (rs.Next()) {
				const long retRow = retTable.AppendRow();
				for (const auto dimension : GetDimensionArrayObject()) {
					ibValue v;
					if (ibDbTableProvider::GetValueAttribute(dimension, v, rs))
						retTable.SetCell(retRow, dimension->GetMetaID(), v);
				}
				{
					ibValue v;
					if (ibDbTableProvider::GetValueAttribute(periodField + wxT("_P_"), ibFieldTypes_Date,
					                                         GetRegisterPeriod(), v, rs))
						retTable.SetCell(retRow, periodId, v);
				}
				for (size_t i = 0; i < resources.size(); i++) {
					ibValue v;
					if (ibDbTableProvider::GetValueAttribute(resources[i]->GetPhysicalName() + wxT("_N_Receipt_"),
					                                         ibFieldTypes_Number, resources[i], v, rs))
						retTable.SetCell(retRow, slots[i].m_receipt, v);
					if (ibDbTableProvider::GetValueAttribute(resources[i]->GetPhysicalName() + wxT("_N_Expense_"),
					                                         ibFieldTypes_Number, resources[i], v, rs))
						retTable.SetCell(retRow, slots[i].m_expense, v);
				}
			}
		}
		catch (...) {}
	}

	// --- 3. roll the openings forward through the periods --------------------------------------
	std::vector<ibMetaID> keyCols;
	for (const auto dimension : GetDimensionArrayObject())
		keyCols.push_back(dimension->GetMetaID());
	FoldBalancesForward(retTable, keyCols, periodId, slots, opening);

	return retTable;
}

// ============================================================================
// The MATERIALISED path — each virtual table as a DERIVED TABLE over its view.
//
// GetSourceRelation is what puts the reading on the server. The parameters (the as-of date, the
// interval, the dimension filter) are baked into the SUBQUERY, so the selection happens inside it,
// before the outer query engine sees a row. A join to a catalog is then an ordinary SQL join and
// only matching rows ever leave the database — which is the whole point, since reading balances is
// the most latency-critical operation the platform performs.
//
// When there is no materialised surface these are never called: IsComputedInRam() stays true and
// the live aggregation answers instead.
// ============================================================================

namespace {

// The dimension filter, applied INSIDE the subquery.
void InnerDimensionFilter(ibDatabaseQueryBuilder& q, const ibValueMetaObjectAccumulationRegister* reg,
                          const ibValue& filter)
{
	ibValueStructure* valFilter = nullptr;
	if (!filter.ConvertToValue(valFilter))
		return;
	for (const auto dimension : reg->GetDimensionArrayObject()) {
		ibValue v;
		if (valFilter->Property(dimension->GetName(), v))
			q.Where(ibRegCompositeIR(dimension, v, ibQueryBinOp::Eq));
	}
}

// Project every column of a view straight through — the derived table exposes what the view has.
void ProjectAll(ibDatabaseQueryBuilder& q, const ibBackendQueryable* view)
{
	std::vector<ibQueryProjItem> proj;
	for (const ibBackendQueryColumn* c : view->GetColumns())
		proj.push_back(ibQueryProjItem{ ibCol(c->GetPhysicalName()), wxString() });
	q.Project(proj);
}

} // namespace

ibBackendQueryProvider& ibBalanceQueryable::GetProvider() const
{
	// Materialised => the ordinary PHYSICAL provider, so the source behaves like any relation.
	return IsComputedInRam() ? ibComputedRegisterQueryable::GetProvider() : ibBackendQueryable::GetProvider();
}
ibBackendQueryProvider& ibTurnoverQueryable::GetProvider() const
{
	return IsComputedInRam() ? ibComputedRegisterQueryable::GetProvider() : ibBackendQueryable::GetProvider();
}
ibBackendQueryProvider& ibBalanceAndTurnoverQueryable::GetProvider() const
{
	return IsComputedInRam() ? ibComputedRegisterQueryable::GetProvider() : ibBackendQueryable::GetProvider();
}

const ibBackendQueryable* ibBalanceQueryable::NavigationSource() const
{
	// Balance folds the turnovers view, so its OUTPUT columns are the dimensions plus one balance
	// per resource — not the view's own. It therefore navigates through the BALANCE view, whose
	// column set is exactly that shape.
	return IsComputedInRam()
		? ibComputedRegisterQueryable::NavigationSource()
		: m_reg->GetViewQueryable(m_reg->GetBalanceViewName(), ibValueMetaObjectAccumulationRegister::ibViewShape::Balance);
}
const ibBackendQueryable* ibTurnoverQueryable::NavigationSource() const
{
	return IsComputedInRam()
		? ibComputedRegisterQueryable::NavigationSource()
		: m_reg->GetViewQueryable(m_reg->GetTurnoverViewName(), ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers);
}
const ibBackendQueryable* ibBalanceAndTurnoverQueryable::NavigationSource() const
{
	return IsComputedInRam()
		? ibComputedRegisterQueryable::NavigationSource()
		: m_reg->GetViewQueryable(m_reg->GetBalanceAndTurnoverViewName(), ibValueMetaObjectAccumulationRegister::ibViewShape::BalanceAndTurnovers);
}


// ============================================================================
// The three virtual tables as DERIVED TABLES over the materialised surface.
//
// Each one DESCRIBES the reading — which figures, over what range, filtered how — and L2-2 spells
// the SQL. Nothing here builds an expression: that is the point of the level existing.
//
// The parameters land INSIDE the subquery, so the selection happens on the server before the outer
// query sees a row; a join to a catalog is then ordinary SQL. When there is no materialised surface
// these are not called at all — IsComputedInRam() stays true and the live aggregation answers.
// ============================================================================

namespace {

// The dimension filter, resolved to (physical column, value) pairs L2-2 applies inside the read.
std::vector<std::pair<wxString, ibValue>> ReadFilters(
	const ibValueMetaObjectAccumulationRegister* reg, const ibValue& filter)
{
	std::vector<std::pair<wxString, ibValue>> out;
	ibValueStructure* valFilter = nullptr;
	if (!filter.ConvertToValue(valFilter))
		return out;
	for (const auto dimension : reg->GetDimensionArrayObject()) {
		ibValue v;
		if (valFilter->Property(dimension->GetName(), v))
			out.push_back({ ibRegValueField(dimension), v });
	}
	return out;
}

// Every dimension's physical fields — the key of any read over the surface.
std::vector<wxString> ReadKeys(const ibValueMetaObjectAccumulationRegister* reg)
{
	std::vector<wxString> out;
	for (const auto dimension : reg->GetDimensionArrayObject())
		for (const wxString& f : ibRegFieldsOf(dimension))
			out.push_back(f);
	return out;
}

} // namespace

ibQueryRelPtr ibBalanceQueryable::GetSourceRelation(const wxString& alias) const
{
	if (IsComputedInRam())
		return nullptr;

	// A balance AS OF A DATE is the turnovers folded up to that moment. The date cannot live in a
	// view, so it lives in this read — which is exactly why the surface is queried rather than
	// stored per date.
	ibMaterializeReadSpec r;
	r.m_view          = m_reg->GetTurnoverViewName();
	r.m_periodColumn  = ibRegValueField(m_reg->GetRegisterPeriod());
	r.m_to            = m_period;
	r.m_keyColumns    = ReadKeys(m_reg);
	r.m_filters       = ReadFilters(m_reg, m_filter);
	r.m_dropZeroRows  = true;   // no stock means NO ROW, matching the live path

	for (const auto res : m_reg->GetResourceArrayObject())
		r.m_columns.push_back({ res->GetName() + wxT("_Balance"), res->GetName() + wxT("_Turnover"),
		                        wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true });

	return RenderMaterializedRead(r, alias);
}

ibQueryRelPtr ibTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (IsComputedInRam())
		return nullptr;

	// Turnovers need no fold — the surface already holds them per period. The interval and the
	// filter ARE the read.
	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName();
	r.m_periodColumn = ibRegValueField(m_reg->GetRegisterPeriod());
	r.m_from         = m_begin;
	r.m_to           = m_end;
	r.m_keyColumns   = ReadKeys(m_reg);
	r.m_filters      = ReadFilters(m_reg, m_filter);

	const bool withSign = (m_reg->GetRegisterType() == ibRegisterType::eBalances);
	for (const auto res : m_reg->GetResourceArrayObject()) {
		const wxString base = res->GetName();
		r.m_columns.push_back({ base + wxT("_Receipt"),  base + wxT("_Receipt"),  wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::Always, false });
		if (withSign)
			r.m_columns.push_back({ base + wxT("_Expense"), base + wxT("_Expense"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::Always, false });
		r.m_columns.push_back({ base + wxT("_Turnover"), base + wxT("_Turnover"), wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::Always, false });
	}

	return RenderMaterializedRead(r, alias);
}

ibQueryRelPtr ibBalanceAndTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (IsComputedInRam())
		return nullptr;

	// The symbiosis, in ONE pass: what was carried in, what moved, what remains. Three different
	// conditions over the same scan — which is why no join and no window is needed, and why a
	// register with an opening balance but no movements in the interval still reports.
	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName();
	r.m_periodColumn = ibRegValueField(m_reg->GetRegisterPeriod());
	r.m_from         = m_begin;
	r.m_to           = m_end;
	r.m_keyColumns   = ReadKeys(m_reg);
	r.m_filters      = ReadFilters(m_reg, m_filter);

	for (const auto res : m_reg->GetResourceArrayObject()) {
		const wxString base = res->GetName();
		const wxString turn = base + wxT("_Turnover");
		r.m_columns.push_back({ base + wxT("_OpeningBalance"), turn,                    wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, true });
		r.m_columns.push_back({ base + wxT("_Receipt"),        base + wxT("_Receipt"),  wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true });
		r.m_columns.push_back({ base + wxT("_Expense"),        base + wxT("_Expense"),  wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true });
		r.m_columns.push_back({ base + wxT("_Turnover"),       turn,                    wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true });
		r.m_columns.push_back({ base + wxT("_ClosingBalance"), turn,                    wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo,     true });
	}

	return RenderMaterializedRead(r, alias);
}
