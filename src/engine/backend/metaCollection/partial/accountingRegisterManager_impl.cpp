////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register manager - Balance, Turnovers, DrCrTurnovers, BalanceAndTurnovers
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "accountingRegisterManager.h"
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

	ibValueMetaObjectAttributeBase::ibSQLField sqlColPeriod =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

	// --- Outer SELECT ---
	wxString sqlQuery = " SELECT "; bool firstSelect = true;

	// Account field in outer select
	{
		ibValueMetaObjectAttributeBase::ibSQLField sqlAccount = ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterAccount());
		sqlQuery += sqlAccount.m_fieldTypeName;
		for (auto dataType : sqlAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
		firstSelect = false;
	}

	// Subconto 1-3 fields in outer select
	{
		ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
			m_metaObject->GetRegisterSubconto1(),
			m_metaObject->GetRegisterSubconto2(),
			m_metaObject->GetRegisterSubconto3()
		};
		for (auto subAttr : subcontoAttrs) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlSubconto = ibValueMetaObjectAttributeBase::GetSQLFieldData(subAttr);
			sqlQuery += "," + sqlSubconto.m_fieldTypeName;
			for (auto dataType : sqlSubconto.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += "," + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}
	}

	// Dimension fields in outer select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlDimension = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlDimension.m_fieldTypeName;
		for (auto dataType : sqlDimension.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
	}

	// Resource balance fields in outer select
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
	}

	// --- Inner SELECT (subquery) ---
	sqlQuery += " FROM ( SELECT "; firstSelect = true;

	// Account in inner select
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	firstSelect = false;

	// Subconto 1-3 in inner select
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	// Dimensions in inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	// RecordType for CASE expression
	ibValueMetaObjectAttributeBase::ibSQLField sqlColRecordType =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

	// Resource aggregates: SUM(CASE WHEN RecordType=Debit(0) THEN Amount ELSE -Amount END)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName + ", ";
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
			" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
	}

	// FROM table
	sqlQuery += " FROM " + m_metaObject->GetTableNameDB();

	// WHERE clause
	sqlQuery += " WHERE ";
	sqlQuery += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

	// Account filter
	if (hasAccountFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterAccount());
	}

	// Dimension filters
	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
	}

	// GROUP BY
	sqlQuery += " GROUP BY "; bool firstGroupBy = true;

	// Group by Account
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	firstGroupBy = false;

	// Group by Subconto 1-3
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	// Group by dimensions
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	// Group by resource type names
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
	}

	// HAVING - filter out zero balances
	sqlQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += orCase + " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + ""
			" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
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

	ibValueMetaObjectAttributeBase::ibSQLField sqlColPeriod =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

	// --- Outer SELECT ---
	wxString sqlQuery = " SELECT "; bool firstSelect = true;

	// Account field in outer select
	{
		ibValueMetaObjectAttributeBase::ibSQLField sqlAccount = ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterAccount());
		sqlQuery += sqlAccount.m_fieldTypeName;
		for (auto dataType : sqlAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
		firstSelect = false;
	}

	// Subconto 1-3 fields in outer select
	{
		ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
			m_metaObject->GetRegisterSubconto1(),
			m_metaObject->GetRegisterSubconto2(),
			m_metaObject->GetRegisterSubconto3()
		};
		for (auto subAttr : subcontoAttrs) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlSubconto = ibValueMetaObjectAttributeBase::GetSQLFieldData(subAttr);
			sqlQuery += "," + sqlSubconto.m_fieldTypeName;
			for (auto dataType : sqlSubconto.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += "," + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}
	}

	// Dimension fields in outer select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlDimension = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlDimension.m_fieldTypeName;
		for (auto dataType : sqlDimension.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
	}

	// Resource TurnoverDr and TurnoverCr fields in outer select
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_";
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_";
	}

	// --- Inner SELECT (subquery) ---
	sqlQuery += " FROM ( SELECT "; firstSelect = true;

	// Account in inner select
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	firstSelect = false;

	// Subconto 1-3 in inner select
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	// Dimensions in inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	// RecordType for CASE expression
	ibValueMetaObjectAttributeBase::ibSQLField sqlColRecordType =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

	// Resource aggregates: TurnoverDr = SUM(CASE RecordType=0(Debit) THEN Amount ELSE 0),
	//                      TurnoverCr = SUM(CASE RecordType=1(Credit) THEN Amount ELSE 0)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName + ", ";
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
			" ELSE   0.0 END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_,";
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   0.0 "
			" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_";
	}

	// FROM table
	sqlQuery += " FROM " + m_metaObject->GetTableNameDB();

	// WHERE clause
	sqlQuery += " WHERE ";
	sqlQuery += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

	// Account filter
	if (hasAccountFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterAccount());
	}

	// Dimension filters
	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
	}

	// GROUP BY
	sqlQuery += " GROUP BY ";

	// Group by Account
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());

	// Group by Subconto 1-3
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	// Group by dimensions
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	// Group by resource type names
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
	}

	// HAVING - filter out zero turnovers
	sqlQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		// TurnoverDr <> 0 OR TurnoverCr <> 0
		sqlQuery += orCase + " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + ""
			" ELSE   0.0 END"
			" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";
		sqlQuery += " OR(CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   0.0"
			" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
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

	// Get SQL field info for key columns
	wxString accountFieldName = m_metaObject->GetRegisterAccount()->GetFieldNameDB();
	ibValueMetaObjectAttributeBase::ibSQLField sqlColAccount =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterAccount());
	ibValueMetaObjectAttributeBase::ibSQLField sqlColRecordType =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecordType());
	ibValueMetaObjectAttributeBase::ibSQLField sqlColRecorder =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecorder());
	ibValueMetaObjectAttributeBase::ibSQLField sqlColLineNumber =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterLineNumber());

	// DrCrTurnovers: self-join on Recorder+LineNumber to pair debit and credit rows.
	// dr alias = debit side (RecordType=0), cr alias = credit side (RecordType=1).
	//
	// The outer SELECT wraps the inner self-join to produce column names
	// compatible with GetValueAttribute (fieldName_TYPE, fieldName_N, fieldName_RTRef, etc.).
	// AccountDr uses prefix "AccountDr" and AccountCr uses prefix "AccountCr".

	wxString tableName = m_metaObject->GetTableNameDB();

	// --- Outer SELECT: aliases for GetValueAttribute compatibility ---
	wxString sqlQuery = " SELECT ";

	// AccountDr fields: alias dr.Account sub-columns to AccountDr_TYPE, AccountDr_B, AccountDr_RTRef, AccountDr_RRRef etc.
	{
		sqlQuery += "dr." + sqlColAccount.m_fieldTypeName + " AS AccountDr_TYPE";
		for (auto dataType : sqlColAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				// e.g. fieldName_N -> AccountDr_N, fieldName_B -> AccountDr_B, etc.
				wxString suffix = dataType.m_field.m_fieldName.Mid(accountFieldName.Len());
				sqlQuery += ", dr." + dataType.m_field.m_fieldName + " AS AccountDr" + suffix;
			}
			else {
				sqlQuery += ", dr." + dataType.m_field.m_fieldRefName.m_fieldRefType + " AS AccountDr_RTRef";
				sqlQuery += ", dr." + dataType.m_field.m_fieldRefName.m_fieldRefName + " AS AccountDr_RRRef";
			}
		}
	}

	// AccountCr fields: alias cr.Account sub-columns to AccountCr_TYPE, AccountCr_N, AccountCr_RTRef, etc.
	{
		sqlQuery += ", cr." + sqlColAccount.m_fieldTypeName + " AS AccountCr_TYPE";
		for (auto dataType : sqlColAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				wxString suffix = dataType.m_field.m_fieldName.Mid(accountFieldName.Len());
				sqlQuery += ", cr." + dataType.m_field.m_fieldName + " AS AccountCr" + suffix;
			}
			else {
				sqlQuery += ", cr." + dataType.m_field.m_fieldRefName.m_fieldRefType + " AS AccountCr_RTRef";
				sqlQuery += ", cr." + dataType.m_field.m_fieldRefName.m_fieldRefName + " AS AccountCr_RRRef";
			}
		}
	}

	// Resource amount aggregates: SUM(dr.Amount)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += ", dr." + sqlResource.m_fieldTypeName;
		sqlQuery += ", CAST(SUM(dr." + sqlResource.m_types[0].m_field.m_fieldName + ") AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Amount_";
	}

	// FROM with self-join
	sqlQuery += " FROM " + tableName + " dr";
	sqlQuery += " INNER JOIN " + tableName + " cr ON ";

	// Join on Recorder
	{
		bool firstJoin = true;
		for (auto dataType : sqlColRecorder.m_types) {
			if (!firstJoin) sqlQuery += " AND ";
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "dr." + dataType.m_field.m_fieldName + " = cr." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "dr." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = cr." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND dr." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = cr." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
			firstJoin = false;
		}
	}

	// WHERE clause
	sqlQuery += " WHERE ";

	// dr.RecordType = 0 (Debit)
	sqlQuery += "dr." + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0";

	// cr.RecordType = 1 (Credit)
	sqlQuery += " AND cr." + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 1.0";

	// dr.Active = true AND cr.Active = true
	ibValueMetaObjectAttributeBase::ibSQLField sqlColActive =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterActive());
	sqlQuery += " AND dr." + sqlColActive.m_types[0].m_field.m_fieldName + " = ?";
	sqlQuery += " AND cr." + sqlColActive.m_types[0].m_field.m_fieldName + " = ?";

	// Period range: dr.Period >= ? AND dr.Period <= ?
	ibValueMetaObjectAttributeBase::ibSQLField sqlColPeriod =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());
	sqlQuery += " AND dr." + sqlColPeriod.m_types[0].m_field.m_fieldName + " >= ?";
	sqlQuery += " AND dr." + sqlColPeriod.m_types[0].m_field.m_fieldName + " <= ?";

	// Account filter on debit side (manually prefixed with dr. for self-join)
	if (hasAccountFilter) {
		sqlQuery += " AND dr." + sqlColAccount.m_fieldTypeName + " = ?";
		for (auto dataType : sqlColAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += " AND dr." + dataType.m_field.m_fieldName + " = ?";
			}
			else {
				sqlQuery += " AND dr." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = ?";
				sqlQuery += " AND dr." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = ?";
			}
		}
	}

	// Dimension filters on debit side (manually prefixed with dr. for self-join)
	for (auto& filter : selFilter) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(filter.first);
		sqlQuery += " AND dr." + sqlDim.m_fieldTypeName + " = ?";
		for (auto dataType : sqlDim.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += " AND dr." + dataType.m_field.m_fieldName + " = ?";
			}
			else {
				sqlQuery += " AND dr." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = ?";
				sqlQuery += " AND dr." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = ?";
			}
		}
	}

	// GROUP BY AccountDr, AccountCr
	sqlQuery += " GROUP BY ";

	// Group by dr.Account fields
	{
		sqlQuery += "dr." + sqlColAccount.m_fieldTypeName;
		for (auto dataType : sqlColAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += ", dr." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", dr." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", dr." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
	}

	// Group by cr.Account fields
	{
		sqlQuery += ", cr." + sqlColAccount.m_fieldTypeName;
		for (auto dataType : sqlColAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += ", cr." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", cr." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", cr." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
	}

	// Group by resource type names
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += ", dr." + sqlResource.m_fieldTypeName;
	}

	// HAVING - filter out zero amounts
	sqlQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += orCase + " CAST(SUM(dr." + sqlResource.m_types[0].m_field.m_fieldName
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

	// RecordType for CASE expression
	ibValueMetaObjectAttributeBase::ibSQLField sqlColRecordType =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

	// Helper lambda to build grouping field names for Account + Subconto + Dimensions
	// We use a UNION ALL of two subqueries:
	//   1) Opening balance: all records with Period <= cBeginOfPeriod
	//   2) Period turnovers: records with Period >= cBeginOfPeriod AND Period <= cEndOfPeriod
	// Then the outer query combines them.

	// --- Build the SQL ---
	// Outer SELECT
	wxString sqlQuery = " SELECT "; bool firstSelect = true;

	// Account field in outer select
	{
		ibValueMetaObjectAttributeBase::ibSQLField sqlAccount = ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterAccount());
		sqlQuery += sqlAccount.m_fieldTypeName;
		for (auto dataType : sqlAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
		firstSelect = false;
	}

	// Subconto 1-3 fields in outer select
	{
		ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
			m_metaObject->GetRegisterSubconto1(),
			m_metaObject->GetRegisterSubconto2(),
			m_metaObject->GetRegisterSubconto3()
		};
		for (auto subAttr : subcontoAttrs) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlSubconto = ibValueMetaObjectAttributeBase::GetSQLFieldData(subAttr);
			sqlQuery += "," + sqlSubconto.m_fieldTypeName;
			for (auto dataType : sqlSubconto.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += "," + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}
	}

	// Dimension fields in outer select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlDimension = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlDimension.m_fieldTypeName;
		for (auto dataType : sqlDimension.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
	}

	// Resource aggregated columns in outer select
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_OpeningBalance_";
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_";
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_";
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_ClosingBalance_";
	}

	// --- Inner UNION ALL subquery ---
	sqlQuery += " FROM ( SELECT "; firstSelect = true;

	// Account in inner select
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	firstSelect = false;

	// Subconto 1-3 in inner select
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	// Dimensions in inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	// Resource aggregates for opening balance (records before period start)
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName + ", ";

		// OpeningBalance = SUM(CASE Debit THEN +Amount ELSE -Amount END)
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
			" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_OpeningBalance_,";

		// TurnoverDr = 0 (no turnovers in opening balance query)
		sqlQuery += " CAST(0.0 AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_,";

		// TurnoverCr = 0
		sqlQuery += " CAST(0.0 AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_,";

		// ClosingBalance = same as opening (will be summed with turnovers in outer query)
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
			" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_ClosingBalance_";
	}

	// FROM table - opening balance subquery
	sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
	sqlQuery += " WHERE ";
	sqlQuery += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<");

	if (hasAccountFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterAccount());
	}

	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
	}

	// GROUP BY for opening balance
	sqlQuery += " GROUP BY ";
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
	}

	// --- UNION ALL: turnovers subquery ---
	sqlQuery += " UNION ALL SELECT "; firstSelect = true;

	// Account in turnovers inner select
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	firstSelect = false;

	// Subconto 1-3 in turnovers inner select
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	// Dimensions in turnovers inner select
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	// Resource aggregates for turnovers
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName + ", ";

		// OpeningBalance = 0 (no opening balance in turnover query)
		sqlQuery += " CAST(0.0 AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_OpeningBalance_,";

		// TurnoverDr = SUM(CASE RecordType=Debit THEN Amount ELSE 0 END)
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
			" ELSE   0.0 END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_,";

		// TurnoverCr = SUM(CASE RecordType=Credit THEN Amount ELSE 0 END)
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   0.0 "
			" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_,";

		// ClosingBalance = SUM(CASE Debit THEN +Amount ELSE -Amount END)
		sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
			" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
			" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
			" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_ClosingBalance_";
	}

	// FROM table - turnovers subquery
	sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
	sqlQuery += " WHERE ";
	sqlQuery += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

	if (hasAccountFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterAccount());
	}

	for (auto& filter : selFilter) {
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
	}

	// GROUP BY for turnovers
	sqlQuery += " GROUP BY ";
	sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += "," + sqlResource.m_fieldTypeName;
	}

	// Close UNION ALL, wrap in outer aggregation
	sqlQuery += ") AS T_UNION";

	// Now wrap everything in an outer SELECT that sums the UNION ALL results
	wxString outerQuery = " SELECT "; firstSelect = true;

	// Account field
	{
		ibValueMetaObjectAttributeBase::ibSQLField sqlAccount = ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterAccount());
		outerQuery += sqlAccount.m_fieldTypeName;
		for (auto dataType : sqlAccount.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				outerQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				outerQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				outerQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
		firstSelect = false;
	}

	// Subconto 1-3
	{
		ibValueMetaObjectAttributeBase* subcontoAttrs[] = {
			m_metaObject->GetRegisterSubconto1(),
			m_metaObject->GetRegisterSubconto2(),
			m_metaObject->GetRegisterSubconto3()
		};
		for (auto subAttr : subcontoAttrs) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlSubconto = ibValueMetaObjectAttributeBase::GetSQLFieldData(subAttr);
			outerQuery += "," + sqlSubconto.m_fieldTypeName;
			for (auto dataType : sqlSubconto.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					outerQuery += "," + dataType.m_field.m_fieldName;
				}
				else {
					outerQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
					outerQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}
	}

	// Dimensions
	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlDimension = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		outerQuery += "," + sqlDimension.m_fieldTypeName;
		for (auto dataType : sqlDimension.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				outerQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				outerQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				outerQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}
	}

	// Resource SUM aggregates
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		outerQuery += "," + sqlResource.m_fieldTypeName;
		outerQuery += ", CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_OpeningBalance_) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_OpeningBalance_";
		outerQuery += ", CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_";
		outerQuery += ", CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_";
		outerQuery += ", CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_ClosingBalance_) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_ClosingBalance_";
	}

	outerQuery += " FROM (" + sqlQuery + ") AS T1";

	// GROUP BY for outer query
	outerQuery += " GROUP BY ";
	bool firstOuterGroup = true;

	outerQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterAccount());
	firstOuterGroup = false;

	outerQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto1());
	outerQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto2());
	outerQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterSubconto3());

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		outerQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		outerQuery += "," + sqlResource.m_fieldTypeName;
	}

	// HAVING - filter out rows where all values are zero
	outerQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);

		outerQuery += orCase + " CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_OpeningBalance_) AS NUMERIC) " + orCaseEnd + " <> 0.0";
		outerQuery += " OR(CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverDr_) AS NUMERIC)) <> 0.0";
		outerQuery += " OR(CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_TurnoverCr_) AS NUMERIC)) <> 0.0";
		outerQuery += " OR(CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + "_ClosingBalance_) AS NUMERIC)) <> 0.0";

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
