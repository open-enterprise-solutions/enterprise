////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
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

	// Subconto 1-3 columns
	{
		ibValueMetaObjectAttributeBase* attrSubconto1 = m_metaObject->GetRegisterSubconto1();
		colCollection->AddColumn(
			attrSubconto1->GetName(),
			attrSubconto1->GetTypeDesc(),
			attrSubconto1->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrSubconto2 = m_metaObject->GetRegisterSubconto2();
		colCollection->AddColumn(
			attrSubconto2->GetName(),
			attrSubconto2->GetTypeDesc(),
			attrSubconto2->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrSubconto3 = m_metaObject->GetRegisterSubconto3();
		colCollection->AddColumn(
			attrSubconto3->GetName(),
			attrSubconto3->GetTypeDesc(),
			attrSubconto3->GetSynonym()
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

	// Account + Subconto 1-3 + Dimensions: the full physical field list of each.
	sqlQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());

	ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
		m_metaObject->GetRegisterSubconto1(),
		m_metaObject->GetRegisterSubconto2(),
		m_metaObject->GetRegisterSubconto3()
	};
	for (auto subAttr : subcontoAttrs)
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

	// Subconto 1-3 in inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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
			" ) AS NUMERIC) AS " + resourceField + "_Balance_";
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

	// Group by Subconto 1-3
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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
			" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";
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

	// Subconto 1-3 columns
	{
		ibValueMetaObjectAttributeBase* attrSubconto1 = m_metaObject->GetRegisterSubconto1();
		colCollection->AddColumn(
			attrSubconto1->GetName(),
			attrSubconto1->GetTypeDesc(),
			attrSubconto1->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrSubconto2 = m_metaObject->GetRegisterSubconto2();
		colCollection->AddColumn(
			attrSubconto2->GetName(),
			attrSubconto2->GetTypeDesc(),
			attrSubconto2->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrSubconto3 = m_metaObject->GetRegisterSubconto3();
		colCollection->AddColumn(
			attrSubconto3->GetName(),
			attrSubconto3->GetTypeDesc(),
			attrSubconto3->GetSynonym()
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

	ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
		m_metaObject->GetRegisterSubconto1(),
		m_metaObject->GetRegisterSubconto2(),
		m_metaObject->GetRegisterSubconto3()
	};
	for (auto subAttr : subcontoAttrs)
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

	// Subconto 1-3 in inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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
			" ) AS NUMERIC) AS " + resourceField + "_TurnoverDr_,";
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   0.0 "
			" ELSE   " + resourceField + " END"
			" ) AS NUMERIC) AS " + resourceField + "_TurnoverCr_";
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

	// Group by Subconto 1-3
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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
			" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";
		sqlQuery += " OR(CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   0.0"
			" ELSE   " + resourceField + " END"
			" ) AS NUMERIC)) <> 0.0";
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
		sqlQuery += ", CAST(SUM(dr." + resourceField + ") AS NUMERIC) AS " + resourceField + "_Amount_";
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
			+ ") AS NUMERIC) " + orCaseEnd + " <> 0.0";
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

	// Subconto 1-3 columns
	{
		ibValueMetaObjectAttributeBase* attrSubconto1 = m_metaObject->GetRegisterSubconto1();
		colCollection->AddColumn(
			attrSubconto1->GetName(),
			attrSubconto1->GetTypeDesc(),
			attrSubconto1->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrSubconto2 = m_metaObject->GetRegisterSubconto2();
		colCollection->AddColumn(
			attrSubconto2->GetName(),
			attrSubconto2->GetTypeDesc(),
			attrSubconto2->GetSynonym()
		);
	}
	{
		ibValueMetaObjectAttributeBase* attrSubconto3 = m_metaObject->GetRegisterSubconto3();
		colCollection->AddColumn(
			attrSubconto3->GetName(),
			attrSubconto3->GetTypeDesc(),
			attrSubconto3->GetSynonym()
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

	ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
		m_metaObject->GetRegisterSubconto1(),
		m_metaObject->GetRegisterSubconto2(),
		m_metaObject->GetRegisterSubconto3()
	};
	for (auto subAttr : subcontoAttrs)
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

	// Subconto 1-3 in inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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
			" ) AS NUMERIC) AS " + resourceField + "_OpeningBalance_,";

		// TurnoverDr = 0 (no turnovers in opening balance query)
		sqlQuery += " CAST(0.0 AS NUMERIC) AS " + resourceField + "_TurnoverDr_,";

		// TurnoverCr = 0
		sqlQuery += " CAST(0.0 AS NUMERIC) AS " + resourceField + "_TurnoverCr_,";

		// ClosingBalance = same as opening (will be summed with turnovers in outer query)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE - " + resourceField + " END"
			" ) AS NUMERIC) AS " + resourceField + "_ClosingBalance_";
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
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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

	// Subconto 1-3 in turnovers inner select
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

	// Dimensions in turnovers inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibRegFieldList(object);
	}

	// Resource aggregates for turnovers
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		sqlQuery += "," + ibRegTypeField(object) + ", ";

		// OpeningBalance = 0 (no opening balance in turnover query)
		sqlQuery += " CAST(0.0 AS NUMERIC) AS " + resourceField + "_OpeningBalance_,";

		// TurnoverDr = SUM(CASE RecordType=Debit THEN Amount ELSE 0 END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE   0.0 END"
			" ) AS NUMERIC) AS " + resourceField + "_TurnoverDr_,";

		// TurnoverCr = SUM(CASE RecordType=Credit THEN Amount ELSE 0 END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   0.0 "
			" ELSE   " + resourceField + " END"
			" ) AS NUMERIC) AS " + resourceField + "_TurnoverCr_,";

		// ClosingBalance = SUM(CASE Debit THEN +Amount ELSE -Amount END)
		sqlQuery += " CAST(SUM(CASE WHEN " + recordTypeField + " = 0.0"
			" THEN   " + resourceField + " "
			" ELSE - " + resourceField + " END"
			" ) AS NUMERIC) AS " + resourceField + "_ClosingBalance_";
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
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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

	for (auto subAttr : subcontoAttrs)
		outerQuery += "," + ibRegFieldList(subAttr);

	for (const auto object : m_metaObject->GetDimensionArrayObject())
		outerQuery += "," + ibRegFieldList(object);

	// Resource SUM aggregates
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		const wxString resourceField = ibRegValueField(object);
		outerQuery += "," + ibRegTypeField(object);
		outerQuery += ", CAST(SUM(" + resourceField + "_OpeningBalance_) AS NUMERIC) AS " + resourceField + "_OpeningBalance_";
		outerQuery += ", CAST(SUM(" + resourceField + "_TurnoverDr_) AS NUMERIC) AS " + resourceField + "_TurnoverDr_";
		outerQuery += ", CAST(SUM(" + resourceField + "_TurnoverCr_) AS NUMERIC) AS " + resourceField + "_TurnoverCr_";
		outerQuery += ", CAST(SUM(" + resourceField + "_ClosingBalance_) AS NUMERIC) AS " + resourceField + "_ClosingBalance_";
	}

	outerQuery += " FROM (" + sqlQuery + ") AS T1";

	// GROUP BY for outer query
	outerQuery += " GROUP BY ";
	bool firstOuterGroup = true;

	outerQuery += ibRegFieldList(m_metaObject->GetRegisterAccount());
	firstOuterGroup = false;

	outerQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto1());
	outerQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto2());
	outerQuery += "," + ibRegFieldList(m_metaObject->GetRegisterSubconto3());

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

		outerQuery += orCase + " CAST(SUM(" + resourceField + "_OpeningBalance_) AS NUMERIC) " + orCaseEnd + " <> 0.0";
		outerQuery += " OR(CAST(SUM(" + resourceField + "_TurnoverDr_) AS NUMERIC)) <> 0.0";
		outerQuery += " OR(CAST(SUM(" + resourceField + "_TurnoverCr_) AS NUMERIC)) <> 0.0";
		outerQuery += " OR(CAST(SUM(" + resourceField + "_ClosingBalance_) AS NUMERIC)) <> 0.0";

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
