////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register manager - Balance, Turnovers, DrCrTurnovers, BalanceAndTurnovers
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "accountingRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

ibValue ibValueManagerDataObjectAccountingRegister::Balance(const ibValue& cPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
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
	for (auto dimension : m_metaObject->GetDimentionArrayObject()) {
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
		for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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

	// Prepare and bind parameters
	ibPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

	if (hasAccountFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);

		// Read Account
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterAccount(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterAccount()->GetName(), retVal);
		}

		// Read Subconto 1-3
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto1(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto1()->GetName(), retVal);
		}
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto2(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto2()->GetName(), retVal);
		}
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto3(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto3()->GetName(), retVal);
		}

		// Read dimensions
		for (const auto object : m_metaObject->GetDimentionArrayObject()) {
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retVal, resultSet))
				retLine->SetAt(object->GetName(), retVal);
		}

		// Read resource balances
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_Balance_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retVal, resultSet))
				retLine->SetAt(object->GetName() + "_Balance", retVal);
		}
		wxDELETE(retLine);
	}

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);

	return retTable;
}

ibValue ibValueManagerDataObjectAccountingRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
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
	for (auto dimension : m_metaObject->GetDimentionArrayObject()) {
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
		for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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

	// Prepare and bind parameters
	ibPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position);
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position);

	if (hasAccountFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {

		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);

		// Read Account
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterAccount(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterAccount()->GetName(), retVal);
		}

		// Read Subconto 1-3
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto1(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto1()->GetName(), retVal);
		}
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto2(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto2()->GetName(), retVal);
		}
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto3(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto3()->GetName(), retVal);
		}

		// Read dimensions
		for (const auto object : m_metaObject->GetDimentionArrayObject()) {
			ibValue retValue;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
				retLine->SetAt(object->GetName(), retValue);
		}

		// Read resource turnovers
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValue retValueDr;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_TurnoverDr_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValueDr, resultSet))
				retLine->SetAt(object->GetName() + "_TurnoverDr", retValueDr);
			ibValue retValueCr;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_TurnoverCr_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValueCr, resultSet))
				retLine->SetAt(object->GetName() + "_TurnoverCr", retValueCr);
		}
		wxDELETE(retLine);
	}

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);

	return retTable;
}

ibValue ibValueManagerDataObjectAccountingRegister::DrCrTurnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	// DrCrTurnovers requires a complex correlated subquery (debit account x credit account pairs).
	// Return empty table with proper column structure for now.
	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
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

	return retTable;
}

ibValue ibValueManagerDataObjectAccountingRegister::BalanceAndTurnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount, const ibValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
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
	for (auto dimension : m_metaObject->GetDimentionArrayObject()) {
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
		for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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

	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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

	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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
	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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

	for (const auto object : m_metaObject->GetDimentionArrayObject()) {
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

	// Prepare and bind parameters
	ibPreparedStatement* statement = db_query->PrepareStatement(outerQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	// Opening balance subquery parameters
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position); // Period < begin

	if (hasAccountFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	// Turnovers subquery parameters
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position); // Period >= begin
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position); // Period <= end

	if (hasAccountFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterAccount(), cAccount, statement, position);
	}

	for (auto& filter : selFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);

		// Read Account
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterAccount(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterAccount()->GetName(), retVal);
		}

		// Read Subconto 1-3
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto1(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto1()->GetName(), retVal);
		}
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto2(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto2()->GetName(), retVal);
		}
		{
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterSubconto3(), retVal, resultSet))
				retLine->SetAt(m_metaObject->GetRegisterSubconto3()->GetName(), retVal);
		}

		// Read dimensions
		for (const auto object : m_metaObject->GetDimentionArrayObject()) {
			ibValue retVal;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retVal, resultSet))
				retLine->SetAt(object->GetName(), retVal);
		}

		// Read resource values
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValue retValOpening;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_OpeningBalance_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValOpening, resultSet))
				retLine->SetAt(object->GetName() + "_OpeningBalance", retValOpening);

			ibValue retValDr;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_TurnoverDr_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValDr, resultSet))
				retLine->SetAt(object->GetName() + "_TurnoverDr", retValDr);

			ibValue retValCr;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_TurnoverCr_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValCr, resultSet))
				retLine->SetAt(object->GetName() + "_TurnoverCr", retValCr);

			ibValue retValClosing;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_ClosingBalance_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValClosing, resultSet))
				retLine->SetAt(object->GetName() + "_ClosingBalance", retValClosing);
		}
		wxDELETE(retLine);
	}

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);

	return retTable;
}
