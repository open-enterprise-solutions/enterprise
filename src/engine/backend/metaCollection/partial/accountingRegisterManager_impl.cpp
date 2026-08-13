////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register manager - Balance, Turnovers, DrCrTurnovers, BalanceAndTurnovers
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "accountingRegisterManager.h"
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibReg* — column-layout tier wrappers (no attribute SQL façade)
// (accounting retrieval is commented out — non-functional, pending its migration to L2;
//  no ibDbTableProvider / ibQueryResult usage remains in compiled code here.)

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dbTableProvider.h"   // ibDbTableProvider::SetValueAttribute — DB write decomposition

// ⭐⭐ THE SCALE BELONGS TO THE RESOURCE, THE PRECISION TO THE FOLD.
//
// These reads cast every aggregate, and they used to cast it to a BARE `NUMERIC` — which on Firebird
// means `NUMERIC(9,0)` by the standard: nine digits, no fraction, stored as a 32-bit integer. Every
// balance and every turnover of this register was therefore rounded to whole units and capped at nine
// digits, silently. (The accumulation register never had this: it goes through the L2 IR, and the
// dialect spells a number as `NUMERIC(%d,%d)` — always with both.)
//
// `NUMERIC(18,6)` would fix the rounding and keep a second hardcode — one from before a resource
// could state its own type. And `NUMERIC(18, <scale>)` would keep HALF of it: 18 is not a law
// either. It is the dialect-1/3 ceiling; Firebird 4 raised NUMERIC to 38 digits on INT128, and this
// platform already speaks that (ibNumber::To128Bytes exists for exactly those columns). An author who
// declares a register resource as 20 digits with 10 after the point is asking for something the
// engine can give.
//
// So BOTH numbers come from the resource — the operator declared them, and nothing here knows better.
// The fallback is used only when nothing was declared at all.
//
// ⚠ AND THE SPELLING BELONGS TO THE DIALECT, not to this file. A totals manager that writes
// `NUMERIC(p,s)` itself has L1 leaking into it — the word differs per engine (the dictionary's own
// default is `DECIMAL`), and a second place that knows it is a second place to get it wrong. So the
// numbers are decided here and the WORD is asked for: `m_typeNumberPattern` is the same pattern the
// L2 renderer fills for a canonical ibTypeNumber, which is what keeps the hand-written SQL in this
// file and the generated SQL everywhere else from spelling one type two ways.
static wxString ibRegFoldNumeric(const ibValueMetaObjectAttributeBase* res)
{
	int precision = res != nullptr ? static_cast<int>(res->GetTypeDesc().GetPrecision()) : 0;
	int scale     = res != nullptr ? static_cast<int>(res->GetTypeDesc().GetScale())     : 0;
	if (precision <= 0)      precision = 18;          // nothing declared — a dialect-3 NUMERIC's own width
	if (scale < 0)           scale = 0;
	if (scale > precision)   scale = precision;       // the engine requires 0 <= scale <= precision

	return wxString::Format(db_query->GetDialect().m_typeNumberPattern, precision, scale);
}

ibValue ibValueManagerDataObjectAccountingRegister::Balance(const ibValue& cPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);

	// Account column
	{
		ibValueMetaObjectAttributeBase* attrAccount = m_metaObject->GetRegisterAccount();
		colCollection->AddColumn(
			attrAccount->GetName(),
			attrAccount->GetTypeDesc(),
			attrAccount->GetSynonym()
		);
	}

	// Account dimension 1-3 columns
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension1 = m_metaObject->GetRegisterAccountDimension(0);
		colCollection->AddColumn(
			attrAccountDimension1->GetName(),
			attrAccountDimension1->GetTypeDesc(),
			attrAccountDimension1->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension2 = m_metaObject->GetRegisterAccountDimension(1);
		colCollection->AddColumn(
			attrAccountDimension2->GetName(),
			attrAccountDimension2->GetTypeDesc(),
			attrAccountDimension2->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension3 = m_metaObject->GetRegisterAccountDimension(2);
		colCollection->AddColumn(
			attrAccountDimension3->GetName(),
			attrAccountDimension3->GetTypeDesc(),
			attrAccountDimension3->GetSynonym()
		);
	}

	// Dimension columns
	for (auto dimension : m_metaObject->GetDimensionArrayObject()) {
		colCollection->AddColumn(
			dimension->GetName(),
			dimension->GetTypeDesc(),
			dimension->GetSynonym()
		);
	}

	// Resource balance columns
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		colCollection->AddColumn(
			object->GetName() + "_Balance",
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Balance")
		);
	}

	// Parse dimension filter
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

	// Check if account filter is provided
	bool hasAccountFilter = !cAccount.IsEmpty();

	// --- Outer SELECT ---
	wxString sqlQuery = " SELECT ";

	// Account + Account dimension 1-3 + Dimensions: the full physical field list of each.
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	ibValueMetaObjectAttributeBase* accountDimensionAttrs[] = {
		m_metaObject->GetRegisterAccountDimension(0),
		m_metaObject->GetRegisterAccountDimension(1),
		m_metaObject->GetRegisterAccountDimension(2)
	};
	for (auto subAttr : accountDimensionAttrs)
		sqlQuery += "," + ibRegFieldList(subAttr);

	for (const auto object : m_metaObject->GetDimensionArrayObject())
		sqlQuery += "," + ibRegFieldList(object);

	// Resource balance fields: the _TYPE tag + the value field aliased "_Balance_".
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		sqlQuery += "," + ibRegTypeField(object);
		sqlQuery += "," + ibRegValueField(object) + "_Balance_";
	}

	// --- Inner SELECT (subquery) ---
	sqlQuery += " FROM ( SELECT ";

	// Account in inner select
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	// Account dimension 1-3 in inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	// Dimensions in inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// RecordType value field for the CASE expression (Debit = 0.0).
	const wxString recordTypeField = ibRegValueField(m_metaObject->GetRegisterRecordType());

	// Resource aggregates: SUM(CASE WHEN RecordType=Debit(0) THEN Amount ELSE -Amount END)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object) + ", ";
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE - " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_Balance_";
	}

	// FROM table
	sqlQuery += " FROM " + m_metaObject->GetPhysicalTableName();

	// WHERE clause
	sqlQuery += " WHERE ";
	sqlQuery += ibRegComposite(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterPeriod(), "<=");

	// Account filter
	if (hasAccountFilter) {
		sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterAccount());
	}

	// Dimension filters
	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibRegComposite(filter.first);
	}

	// GROUP BY
	sqlQuery += " GROUP BY ";

	// Group by Account
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	// Group by Account dimension 1-3
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	// Group by dimensions
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// Group by resource type names
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		sqlQuery += "," + ibRegTypeField(object);
	}

	// HAVING - filter out zero balances
	sqlQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += orCase + " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + ""
			" ELSE - " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") " + orCaseEnd + " <> 0.0";
		firstHaving = false;
	}

	sqlQuery += ") AS T1";

#if 0   // accounting NON-FUNCTIONAL — the whole DB execution (raw L1 statement + bind + run) is
        // disabled pending the register's migration to the L3 write/read door. Returns the empty
        // retTable. (docs/query-language-arc.md — accounting is the last raw-L1 holdout.)
	// Prepare and bind parameters
	ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

	if (hasAccountFilter) {
		ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibDbTableProvider::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		// ... value reads via ibDbTableProvider::GetValueAttribute (disabled) ...
		wxDELETE(retLine);
	}

	ses_query->CloseResultSet(resultSet);
	ses_query->CloseStatement(statement);
#endif

	return retTable;
}

ibValue ibValueManagerDataObjectAccountingRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);

	// Account column
	{
		ibValueMetaObjectAttributeBase* attrAccount = m_metaObject->GetRegisterAccount();
		colCollection->AddColumn(
			attrAccount->GetName(),
			attrAccount->GetTypeDesc(),
			attrAccount->GetSynonym()
		);
	}

	// Account dimension 1-3 columns
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension1 = m_metaObject->GetRegisterAccountDimension(0);
		colCollection->AddColumn(
			attrAccountDimension1->GetName(),
			attrAccountDimension1->GetTypeDesc(),
			attrAccountDimension1->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension2 = m_metaObject->GetRegisterAccountDimension(1);
		colCollection->AddColumn(
			attrAccountDimension2->GetName(),
			attrAccountDimension2->GetTypeDesc(),
			attrAccountDimension2->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension3 = m_metaObject->GetRegisterAccountDimension(2);
		colCollection->AddColumn(
			attrAccountDimension3->GetName(),
			attrAccountDimension3->GetTypeDesc(),
			attrAccountDimension3->GetSynonym()
		);
	}

	// Dimension columns
	for (auto dimension : m_metaObject->GetDimensionArrayObject()) {
		colCollection->AddColumn(
			dimension->GetName(),
			dimension->GetTypeDesc(),
			dimension->GetSynonym()
		);
	}

	// Resource turnover columns: TurnoverDr and TurnoverCr for each resource
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		colCollection->AddColumn(
			object->GetName() + wxT("_TurnoverDr"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Turnover Dt")
		);
		colCollection->AddColumn(
			object->GetName() + wxT("_TurnoverCr"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Turnover Ct")
		);
	}

	// Parse dimension filter
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

	// Check if account filter is provided
	bool hasAccountFilter = !cAccount.IsEmpty();

	// --- Outer SELECT ---
	wxString sqlQuery = " SELECT ";

	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	ibValueMetaObjectAttributeBase* accountDimensionAttrs[] = {
		m_metaObject->GetRegisterAccountDimension(0),
		m_metaObject->GetRegisterAccountDimension(1),
		m_metaObject->GetRegisterAccountDimension(2)
	};
	for (auto subAttr : accountDimensionAttrs)
		sqlQuery += "," + ibRegFieldList(subAttr);

	for (const auto object : m_metaObject->GetDimensionArrayObject())
		sqlQuery += "," + ibRegFieldList(object);

	// Resource TurnoverDr / TurnoverCr fields (the _TYPE tag + the value field's two aliases).
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object);
		sqlQuery += "," + resourceField + "_TurnoverDr_";
		sqlQuery += "," + resourceField + "_TurnoverCr_";
	}

	// --- Inner SELECT (subquery) ---
	sqlQuery += " FROM ( SELECT ";

	// Account in inner select
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	// Account dimension 1-3 in inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	// Dimensions in inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// RecordType value field for the CASE expression (Debit = 0.0).
	const wxString recordTypeField = ibRegValueField(m_metaObject->GetRegisterRecordType());

	// Resource aggregates: TurnoverDr = SUM(Debit amounts), TurnoverCr = SUM(Credit amounts).
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object) + ", ";
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE   0.0 END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverDr_,";
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   0.0 "
			" ELSE   " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverCr_";
	}

	// FROM table
	sqlQuery += " FROM " + m_metaObject->GetPhysicalTableName();

	// WHERE clause
	sqlQuery += " WHERE ";
	sqlQuery += ibRegComposite(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterPeriod(), "<=");

	// Account filter
	if (hasAccountFilter) {
		sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterAccount());
	}

	// Dimension filters
	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibRegComposite(filter.first);
	}

	// GROUP BY
	sqlQuery += " GROUP BY ";

	// Group by Account
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	// Group by Account dimension 1-3
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	// Group by dimensions
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// Group by resource type names
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		sqlQuery += "," + ibRegTypeField(object);
	}

	// HAVING - filter out zero turnovers
	sqlQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		const wxString resourceField = ibRegValueField(object);
		// TurnoverDr <> 0 OR TurnoverCr <> 0
		sqlQuery += orCase + " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + ""
			" ELSE   0.0 END"
			" ) AS " + ibRegFoldNumeric(object) + ") " + orCaseEnd + " <> 0.0";
		sqlQuery += " OR(CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   0.0"
			" ELSE   " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ")) <> 0.0";
		firstHaving = false;
	}

	sqlQuery += ") AS T1";

#if 0   // accounting NON-FUNCTIONAL — DB execution disabled pending the register's migration to
        // the L3 door. Returns the empty retTable. (docs/query-language-arc.md.)
	// Prepare and bind parameters
	ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position);
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position);

	if (hasAccountFilter) {
		ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibDbTableProvider::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		// ... value reads via ibDbTableProvider::GetValueAttribute (disabled) ...
		wxDELETE(retLine);
	}

	ses_query->CloseResultSet(resultSet);
	ses_query->CloseStatement(statement);
#endif

	return retTable;
}

ibValue ibValueManagerDataObjectAccountingRegister::DrCrTurnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);

	// AccountDr column
	{
		ibValueMetaObjectAttributeBase* attrAccount = m_metaObject->GetRegisterAccount();
		colCollection->AddColumn(
			wxT("AccountDr"),
			attrAccount->GetTypeDesc(),
			_("Account Dt")
		);
	}

	// AccountCr column
	{
		ibValueMetaObjectAttributeBase* attrAccount = m_metaObject->GetRegisterAccount();
		colCollection->AddColumn(
			wxT("AccountCr"),
			attrAccount->GetTypeDesc(),
			_("Account Ct")
		);
	}

	// Resource amount columns
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		colCollection->AddColumn(
			object->GetName() + wxT("_Amount"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Amount")
		);
	}

	// Parse dimension filter
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

	// Check if account filter is provided
	bool hasAccountFilter = !cAccount.IsEmpty();

	// DrCrTurnovers: self-join on Recorder to pair debit and credit rows.
	// dr alias = debit side (RecordType=0), cr alias = credit side (RecordType=1).
	// The outer SELECT aliases the account sub-columns to AccountDr<suffix> / AccountCr<suffix> so the
	// result is GetValueAttribute-compatible.
	wxString tableName = m_metaObject->GetPhysicalTableName();
	ibValueMetaObjectAttributeBase* account = m_metaObject->GetRegisterAccount();
	const wxString recordTypeField = ibRegValueField(m_metaObject->GetRegisterRecordType());

	// --- Outer SELECT ---
	wxString sqlQuery = " SELECT ";
	sqlQuery += ibRegAliasedList(account, "dr", "AccountDr");
	sqlQuery += ", " + ibRegAliasedList(account, "cr", "AccountCr");

	// Resource amount aggregates: SUM(dr.Amount)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += ", dr." + ibRegTypeField(object);
		sqlQuery += ", CAST(SUM(dr." + resourceField + ") AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_Amount_";
	}

	// FROM with self-join on Recorder
	sqlQuery += " FROM " + tableName + " dr";
	sqlQuery += " INNER JOIN " + tableName + " cr ON ";
	sqlQuery += ibRegJoinEq(m_metaObject->GetRegisterRecorder(), "dr", "cr");

	// WHERE clause
	sqlQuery += " WHERE ";
	sqlQuery += "dr." + recordTypeField + " = 0.0";        // dr = Debit
	sqlQuery += " AND cr." + recordTypeField + " = 1.0";   // cr = Credit

	const wxString activeField = ibRegValueField(m_metaObject->GetRegisterActive());
	sqlQuery += " AND dr." + activeField + " = ?";
	sqlQuery += " AND cr." + activeField + " = ?";

	const wxString periodField = ibRegValueField(m_metaObject->GetRegisterPeriod());
	sqlQuery += " AND dr." + periodField + " >= ?";
	sqlQuery += " AND dr." + periodField + " <= ?";

	// Account / dimension filters on the debit side.
	if (hasAccountFilter)
		sqlQuery += " AND " + ibRegQualifiedEqParams(account, "dr");
	for (auto& filter : selFilter)
		sqlQuery += " AND " + ibRegQualifiedEqParams(filter.first, "dr");

	// GROUP BY dr.Account, cr.Account, resource type names
	sqlQuery += " GROUP BY ";
	sqlQuery += ibRegQualifiedList(account, "dr");
	sqlQuery += ", " + ibRegQualifiedList(account, "cr");
	for (const auto object : m_metaObject->GetResourceArrayObject())
		sqlQuery += ", dr." + ibRegTypeField(object);

	// HAVING - filter out zero amounts
	sqlQuery += " HAVING "; bool firstHaving = true;
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		sqlQuery += orCase + " CAST(SUM(dr." + ibRegValueField(object)
			+ ") AS " + ibRegFoldNumeric(object) + ") " + orCaseEnd + " <> 0.0";
		firstHaving = false;
	}

#if 0   // accounting NON-FUNCTIONAL — DB execution disabled pending the register's migration to
        // the L3 door. Returns the empty retTable. (docs/query-language-arc.md.)
	// Prepare and bind parameters
	ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	// dr.Active = true, cr.Active = true
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);

	// Period range
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position);
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position);

	// Account filter
	if (hasAccountFilter) {
		ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	// Dimension filters
	for (auto& filter : selFilter) {
		ibDbTableProvider::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		// ... value reads via ibDbTableProvider::GetValueAttribute (disabled) ...
		wxDELETE(retLine);
	}

	ses_query->CloseResultSet(resultSet);
	ses_query->CloseStatement(statement);
#endif

	return retTable;
}

ibValue ibValueManagerDataObjectAccountingRegister::BalanceAndTurnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);

	// Account column
	{
		ibValueMetaObjectAttributeBase* attrAccount = m_metaObject->GetRegisterAccount();
		colCollection->AddColumn(
			attrAccount->GetName(),
			attrAccount->GetTypeDesc(),
			attrAccount->GetSynonym()
		);
	}

	// Account dimension 1-3 columns
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension1 = m_metaObject->GetRegisterAccountDimension(0);
		colCollection->AddColumn(
			attrAccountDimension1->GetName(),
			attrAccountDimension1->GetTypeDesc(),
			attrAccountDimension1->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension2 = m_metaObject->GetRegisterAccountDimension(1);
		colCollection->AddColumn(
			attrAccountDimension2->GetName(),
			attrAccountDimension2->GetTypeDesc(),
			attrAccountDimension2->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrAccountDimension3 = m_metaObject->GetRegisterAccountDimension(2);
		colCollection->AddColumn(
			attrAccountDimension3->GetName(),
			attrAccountDimension3->GetTypeDesc(),
			attrAccountDimension3->GetSynonym()
		);
	}

	// Dimension columns
	for (auto dimension : m_metaObject->GetDimensionArrayObject()) {
		colCollection->AddColumn(
			dimension->GetName(),
			dimension->GetTypeDesc(),
			dimension->GetSynonym()
		);
	}

	// Resource columns: OpeningBalance, TurnoverDr, TurnoverCr, ClosingBalance
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		colCollection->AddColumn(
			object->GetName() + wxT("_OpeningBalance"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Opening balance")
		);
		colCollection->AddColumn(
			object->GetName() + wxT("_TurnoverDr"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Turnover Dt")
		);
		colCollection->AddColumn(
			object->GetName() + wxT("_TurnoverCr"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Turnover Ct")
		);
		colCollection->AddColumn(
			object->GetName() + wxT("_ClosingBalance"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Closing balance")
		);
	}

	// Parse dimension filter
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

	// Check if account filter is provided
	bool hasAccountFilter = !cAccount.IsEmpty();

	// RecordType value field for the CASE expressions (Debit = 0.0).
	const wxString recordTypeField = ibRegValueField(m_metaObject->GetRegisterRecordType());

	// UNION ALL of two subqueries — opening balance (Period < begin) and period turnovers — then the
	// outer query sums them into OpeningBalance / TurnoverDr / TurnoverCr / ClosingBalance.

	// --- Outer SELECT ---
	wxString sqlQuery = " SELECT ";

	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	ibValueMetaObjectAttributeBase* accountDimensionAttrs[] = {
		m_metaObject->GetRegisterAccountDimension(0),
		m_metaObject->GetRegisterAccountDimension(1),
		m_metaObject->GetRegisterAccountDimension(2)
	};
	for (auto subAttr : accountDimensionAttrs)
		sqlQuery += "," + ibRegFieldList(subAttr);

	for (const auto object : m_metaObject->GetDimensionArrayObject())
		sqlQuery += "," + ibRegFieldList(object);

	// Resource aggregated columns (the _TYPE tag + the four aggregate aliases of the value field).
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object);
		sqlQuery += "," + resourceField + "_OpeningBalance_";
		sqlQuery += "," + resourceField + "_TurnoverDr_";
		sqlQuery += "," + resourceField + "_TurnoverCr_";
		sqlQuery += "," + resourceField + "_ClosingBalance_";
	}

	// --- Inner UNION ALL subquery ---
	sqlQuery += " FROM ( SELECT ";

	// Account in inner select
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	// Account dimension 1-3 in inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	// Dimensions in inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// Resource aggregates for opening balance (records before period start)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object) + ", ";

		// OpeningBalance = SUM(CASE Debit THEN +Amount ELSE -Amount END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE - " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_OpeningBalance_,";

		// TurnoverDr = 0 (no turnovers in opening balance query)
		sqlQuery += " CAST(0.0 AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverDr_,";

		// TurnoverCr = 0
		sqlQuery += " CAST(0.0 AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverCr_,";

		// ClosingBalance = same as opening (will be summed with turnovers in outer query)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE - " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_ClosingBalance_";
	}

	// FROM table - opening balance subquery
	sqlQuery += " FROM " + m_metaObject->GetPhysicalTableName();
	sqlQuery += " WHERE ";
	sqlQuery += ibRegComposite(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterPeriod(), "<");

	if (hasAccountFilter) {
		sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterAccount());
	}

	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibRegComposite(filter.first);
	}

	// GROUP BY for opening balance
	sqlQuery += " GROUP BY ";
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object);
	}

	// --- UNION ALL: turnovers subquery ---
	sqlQuery += " UNION ALL SELECT ";

	// Account in turnovers inner select
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	// Account dimension 1-3 in turnovers inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	// Dimensions in turnovers inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// Resource aggregates for turnovers
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object) + ", ";

		// OpeningBalance = 0 (no opening balance in turnover query)
		sqlQuery += " CAST(0.0 AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_OpeningBalance_,";

		// TurnoverDr = SUM(CASE RecordType=Debit THEN Amount ELSE 0 END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE   0.0 END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverDr_,";

		// TurnoverCr = SUM(CASE RecordType=Credit THEN Amount ELSE 0 END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   0.0 "
			" ELSE   " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverCr_,";

		// ClosingBalance = SUM(CASE Debit THEN +Amount ELSE -Amount END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE - " + resourceField + " END"
			" ) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_ClosingBalance_";
	}

	// FROM table - turnovers subquery
	sqlQuery += " FROM " + m_metaObject->GetPhysicalTableName();
	sqlQuery += " WHERE ";
	sqlQuery += ibRegComposite(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterPeriod(), "<=");

	if (hasAccountFilter) {
		sqlQuery += " AND " + ibRegComposite(m_metaObject->GetRegisterAccount());
	}

	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibRegComposite(filter.first);
	}

	// GROUP BY for turnovers
	sqlQuery += " GROUP BY ";
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object);
	}

	// Close UNION ALL, wrap in outer aggregation
	sqlQuery += ") AS T_UNION";

	// Now wrap everything in an outer SELECT that sums the UNION ALL results
	wxString outerQuery = " SELECT ";

	outerQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	for (auto subAttr : accountDimensionAttrs)
		outerQuery += "," + ibRegFieldList(subAttr);

	for (const auto object : m_metaObject->GetDimensionArrayObject())
		outerQuery += "," + ibRegFieldList(object);

	// Resource SUM aggregates
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		outerQuery += "," + ibRegTypeField(object);
		outerQuery += ", CAST(SUM(" + resourceField + "_OpeningBalance_) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_OpeningBalance_";
		outerQuery += ", CAST(SUM(" + resourceField + "_TurnoverDr_) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverDr_";
		outerQuery += ", CAST(SUM(" + resourceField + "_TurnoverCr_) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_TurnoverCr_";
		outerQuery += ", CAST(SUM(" + resourceField + "_ClosingBalance_) AS " + ibRegFoldNumeric(object) + ") AS " + resourceField + "_ClosingBalance_";
	}

	outerQuery += " FROM (" + sqlQuery + ") AS T1";

	// GROUP BY for outer query
	outerQuery += " GROUP BY ";
	bool firstOuterGroup = true;

	outerQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());
	firstOuterGroup = false;

	outerQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(0));
	outerQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(1));
	outerQuery += "," + ibRegFieldList(m_metaObject->GetRegisterAccountDimension(2));

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		outerQuery += "," + ibRegFieldList(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		outerQuery += "," + ibRegTypeField(object);
	}

	// HAVING - filter out rows where all values are zero
	outerQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		const wxString resourceField = ibRegValueField(object);

		outerQuery += orCase + " CAST(SUM(" + resourceField + "_OpeningBalance_) AS " + ibRegFoldNumeric(object) + ") " + orCaseEnd + " <> 0.0";
		outerQuery += " OR(CAST(SUM(" + resourceField + "_TurnoverDr_) AS " + ibRegFoldNumeric(object) + ")) <> 0.0";
		outerQuery += " OR(CAST(SUM(" + resourceField + "_TurnoverCr_) AS " + ibRegFoldNumeric(object) + ")) <> 0.0";
		outerQuery += " OR(CAST(SUM(" + resourceField + "_ClosingBalance_) AS " + ibRegFoldNumeric(object) + ")) <> 0.0";

		firstHaving = false;
	}

#if 0   // accounting NON-FUNCTIONAL — DB execution disabled pending the register's migration to
        // the L3 door. Returns the empty retTable. (docs/query-language-arc.md.)
	// Prepare and bind parameters
	ibPreparedStatement* statement = ses_query->PrepareStatement(outerQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	// Opening balance subquery parameters
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position); // Period < begin

	if (hasAccountFilter) {
		ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibDbTableProvider::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	// Turnovers subquery parameters
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position); // Period >= begin
	ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position); // Period <= end

	if (hasAccountFilter) {
		ibDbTableProvider::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibDbTableProvider::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		// ... value reads via ibDbTableProvider::GetValueAttribute (disabled) ...
		wxDELETE(retLine);
	}

	ses_query->CloseResultSet(resultSet);
	ses_query->CloseStatement(statement);
#endif

	return retTable;
}
