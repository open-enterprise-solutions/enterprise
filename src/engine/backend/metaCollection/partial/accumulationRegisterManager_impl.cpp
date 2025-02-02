////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumlation manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

#include "backend/compiler/value/valueMap.h"
#include "backend/compiler/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

CValue CAccumulationRegisterManager::Balance(const CValue& cPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueTable* retTable = CValue::CreateAndConvertObjectValueRef<CValueTable>();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto dimention : m_metaObject->GetObjectDimensions()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				dimention->GetName(),
				dimention->GetTypeDescription(),
				dimention->GetSynonym()
			);
	}

	for (auto& obj : m_metaObject->GetObjectResources()) {
		colCollection->AddColumn(
			obj->GetName() + "_Balance",
			obj->GetTypeDescription(),
			obj->GetSynonym() + " " + _("Balance")
		);
	}

	if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {

		CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(obj->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						obj, vSelValue
					);
				}
			}
		}

		IMetaObjectAttribute::sqlField_t sqlColPeriod =
			IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT "; bool firstSelect = true;

		for (auto& obj : m_metaObject->GetObjectDimensions()) {

			IMetaObjectAttribute::sqlField_t sqlDimension = IMetaObjectAttribute::GetSQLFieldData(obj);

			sqlQuery += (firstSelect ? "" : ",") + sqlDimension.m_fieldTypeName;

			for (auto dataType : sqlDimension.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += "," + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}

			firstSelect = false;
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {

			IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName;
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
			firstSelect = false;
		}

		sqlQuery += " FROM ( SELECT "; firstSelect = true;

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += (firstSelect ? "" : ",") + IMetaObjectAttribute::GetSQLFieldName(obj);
			firstSelect = false;
		}

		IMetaObjectAttribute::sqlField_t sqlColRecordType =
			IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

		for (auto& obj : m_metaObject->GetObjectResources()) {

			IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName + ", ";
			sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN - " + sqlResource.m_types[0].m_field.m_fieldName + " "
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
			firstSelect = false;
		}

		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE ";

		sqlQuery += IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

		sqlQuery += " GROUP BY "; bool firstGroupBy = true;

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += (firstGroupBy ? "" : ",") + IMetaObjectAttribute::GetSQLFieldName(obj);
			firstGroupBy = false;
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += (firstGroupBy ? "" : ",") + sqlResource.m_fieldTypeName;
			firstGroupBy = false;
		}

		sqlQuery += " HAVING "; bool firstHaving = true;

		for (auto& obj : m_metaObject->GetObjectResources()) {
			wxString orCase = (!firstHaving ? "OR (" : "");
			wxString orCaseEnd = (!firstHaving ? ")" : "");
			IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += orCase + " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN - " + sqlResource.m_types[0].m_field.m_fieldName + ""
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";

			firstHaving = false;
		}

		sqlQuery += ") AS T1";

		IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue retVal;
				if (IMetaObjectAttribute::GetValueAttribute(obj, retVal, resultSet))
					retLine->SetAt(obj->GetName(), retVal);
			}
			for (auto& obj : m_metaObject->GetObjectResources()) {
				CValue retVal;
				if (IMetaObjectAttribute::GetValueAttribute(obj->GetFieldNameDB() + "_N_Balance_", IMetaObjectAttribute::eFieldTypes_Number, obj, retVal, resultSet))
					retLine->SetAt(obj->GetName() + "_Balance", retVal);
			}
			wxDELETE(retLine);
		}

		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}

	return retTable;
}

CValue CAccumulationRegisterManager::Turnovers(const CValue& cBeginOfPeriod, const CValue& cEndOfPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueTable* retTable = CValue::CreateAndConvertObjectValueRef<CValueTable>();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto dimention : m_metaObject->GetObjectDimensions()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				dimention->GetName(),
				dimention->GetTypeDescription(),
				dimention->GetSynonym()
			);
	}
	for (auto& obj : m_metaObject->GetObjectResources()) {
		colCollection->AddColumn(
			obj->GetName() + wxT("_Turnover"),
			obj->GetTypeDescription(),
			obj->GetSynonym() + " " + _("Turnover")
		);
		if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
			colCollection->AddColumn(
				obj->GetName() + wxT("_Receipt"),
				obj->GetTypeDescription(),
				obj->GetSynonym() + " " + _("Receipt")
			);
			colCollection->AddColumn(
				obj->GetName() + wxT("_Expense"),
				obj->GetTypeDescription(),
				obj->GetSynonym() + " " + _("Expense")
			);
		}
	}

	CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
	if (cFilter.ConvertToValue(valFilter)) {
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			CValue vSelValue;
			if (valFilter->Property(obj->GetName(), vSelValue)) {
				selFilter.insert_or_assign(
					obj, vSelValue
				);
			}
		}
	}

	IMetaObjectAttribute::sqlField_t sqlColPeriod =
		IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

	wxString sqlQuery = " SELECT "; bool firstSelect = true;

	for (auto& obj : m_metaObject->GetObjectDimensions()) {

		IMetaObjectAttribute::sqlField_t sqlDimension = IMetaObjectAttribute::GetSQLFieldData(obj);

		sqlQuery += (firstSelect ? "" : ",") + sqlDimension.m_fieldTypeName;

		for (auto dataType : sqlDimension.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		firstSelect = false;
	}

	for (auto& obj : m_metaObject->GetObjectResources()) {

		IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
		sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName;
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Turnover_";

		if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Receipt_";
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Expense_";
		}

		firstSelect = false;
	}

	sqlQuery += " FROM ( SELECT "; firstSelect = true;

	for (auto& obj : m_metaObject->GetObjectDimensions()) {
		sqlQuery += (firstSelect ? "" : ",") + IMetaObjectAttribute::GetSQLFieldName(obj);
		firstSelect = false;
	}

	IMetaObjectAttribute::sqlField_t sqlColRecordType =
		IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

	for (auto& obj : m_metaObject->GetObjectResources()) {

		IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
		sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName + ", ";

		if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
			sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN	 " + sqlResource.m_types[0].m_field.m_fieldName + " "
				" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Turnover_,";
			sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN   " + sqlResource.m_types[0].m_field.m_fieldName + " "
				" ELSE		0.0 END "
				" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Receipt_,";
			sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN		0.0 "
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Expense_";
		}
		else {
			sqlQuery += " CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + ") AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Turnover_";
		}

		firstSelect = false;
	}

	sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
	sqlQuery += " WHERE ";

	sqlQuery += IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

	sqlQuery += " GROUP BY "; bool firstGroupBy = true;

	for (auto& obj : m_metaObject->GetObjectDimensions()) {
		sqlQuery += (firstGroupBy ? "" : ",") + IMetaObjectAttribute::GetSQLFieldName(obj);
		firstGroupBy = false;
	}

	for (auto& obj : m_metaObject->GetObjectResources()) {
		IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
		sqlQuery += (firstGroupBy ? "" : ",") + sqlResource.m_fieldTypeName;
		firstGroupBy = false;
	}

	sqlQuery += " HAVING "; bool firstHaving = true;

	for (auto& obj : m_metaObject->GetObjectResources()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		IMetaObjectAttribute::sqlField_t sqlResource = IMetaObjectAttribute::GetSQLFieldData(obj);
		if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
			sqlQuery += orCase + " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN	 " + sqlResource.m_types[0].m_field.m_fieldName + ""
				" ELSE - " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";
			sqlQuery += " OR(CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN	 " + sqlResource.m_types[0].m_field.m_fieldName + ""
				" ELSE	 0.0 END"
				" ) AS NUMERIC)) <> 0.0";
			sqlQuery += " OR(CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN	 0.0"
				" ELSE	 " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC)) <> 0.0";
		}
		else {
			sqlQuery += orCase + " CAST(SUM(" + sqlResource.m_types[0].m_field.m_fieldName + ") AS NUMERIC) " + orCaseEnd + " <> 0.0";
		}

		firstHaving = false;
	}

	sqlQuery += ") AS T1";

	IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position);
	IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position);

	for (auto filter : selFilter) {
		IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {

		CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			CValue retValue;
			if (IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
				retLine->SetAt(obj->GetName(), retValue);
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			CValue retValue;
			if (IMetaObjectAttribute::GetValueAttribute(obj->GetFieldNameDB() + "_N_Turnover_", IMetaObjectAttribute::eFieldTypes_Number, obj, retValue, resultSet))
				retLine->SetAt(obj->GetName() + "_Turnover", retValue);
			if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
				CValue retValue1;
				if (IMetaObjectAttribute::GetValueAttribute(obj->GetFieldNameDB() + "_N_Receipt_", IMetaObjectAttribute::eFieldTypes_Number, obj, retValue1, resultSet))
					retLine->SetAt(obj->GetName() + "_Receipt", retValue1);
				CValue retValue2;
				if (IMetaObjectAttribute::GetValueAttribute(obj->GetFieldNameDB() + "_N_Expense_", IMetaObjectAttribute::eFieldTypes_Number, obj, retValue2, resultSet))
					retLine->SetAt(obj->GetName() + "_Expense", retValue2);
			}
		}
		wxDELETE(retLine);
	}

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);

	return retTable;
}
