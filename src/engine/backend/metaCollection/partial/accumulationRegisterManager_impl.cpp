////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumlation manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"

ibValue ibValueManagerDataObjectAccumulationRegister::Balance(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto dimension : m_metaObject->GetDimensionArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				dimension->GetName(),
				dimension->GetTypeDesc(),
				dimension->GetSynonym()
			);
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		colCollection->AddColumn(
			object->GetName() + "_Balance",
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Balance")
		);
	}

	if (m_metaObject->GetRegisterType() == ibRegisterType::eBalances) {

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

		ibValueMetaObjectAttributeBase::ibSQLField sqlColPeriod =
			ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT "; bool firstSelect = true;

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {

			ibValueMetaObjectAttributeBase::ibSQLField sqlDimension = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);

			sqlQuery += (firstSelect ? "" : ",") + sqlDimension.m_fieldTypeName;

			for (auto dataType : sqlDimension.m_types) {
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

		for (const auto object : m_metaObject->GetResourceArrayObject()) {

			ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName;
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
			firstSelect = false;
		}

		sqlQuery += " FROM ( SELECT "; firstSelect = true;

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += (firstSelect ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
			firstSelect = false;
		}

		ibValueMetaObjectAttributeBase::ibSQLField sqlColRecordType =
			ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

		for (const auto object : m_metaObject->GetResourceArrayObject()) {

			ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName + ", ";
			sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN - " + sqlResource.m_types[0].m_field.m_fieldName + " "
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
			firstSelect = false;
		}

		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE ";

		sqlQuery += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

		sqlQuery += " GROUP BY "; bool firstGroupBy = true;

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += (firstGroupBy ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
			firstGroupBy = false;
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += (firstGroupBy ? "" : ",") + sqlResource.m_fieldTypeName;
			firstGroupBy = false;
		}

		sqlQuery += " HAVING "; bool firstHaving = true;

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			wxString orCase = (!firstHaving ? "OR (" : "");
			wxString orCaseEnd = (!firstHaving ? ")" : "");
			ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += orCase + " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN - " + sqlResource.m_types[0].m_field.m_fieldName + ""
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";

			firstHaving = false;
		}

		sqlQuery += ") AS T1";

		ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue retVal;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retVal, resultSet))
					retLine->SetAt(object->GetName(), retVal);
			}
			for (const auto object : m_metaObject->GetResourceArrayObject()) {
				ibValue retVal;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_Balance_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retVal, resultSet))
					retLine->SetAt(object->GetName() + "_Balance", retVal);
			}
			wxDELETE(retLine);
		}

		ses_query->CloseResultSet(resultSet);
		ses_query->CloseStatement(statement);
	}

	return retTable;
}

ibValue ibValueManagerDataObjectAccumulationRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto dimension : m_metaObject->GetDimensionArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				dimension->GetName(),
				dimension->GetTypeDesc(),
				dimension->GetSynonym()
			);
	}
	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		colCollection->AddColumn(
			object->GetName() + wxT("_Turnover"),
			object->GetTypeDesc(),
			object->GetSynonym() + " " + _("Turnover")
		);
		if (m_metaObject->GetRegisterType() == ibRegisterType::eBalances) {
			colCollection->AddColumn(
				object->GetName() + wxT("_Receipt"),
				object->GetTypeDesc(),
				object->GetSynonym() + " " + _("Receipt")
			);
			colCollection->AddColumn(
				object->GetName() + wxT("_Expense"),
				object->GetTypeDesc(),
				object->GetSynonym() + " " + _("Expense")
			);
		}
	}

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

	ibValueMetaObjectAttributeBase::ibSQLField sqlColPeriod =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

	wxString sqlQuery = " SELECT "; bool firstSelect = true;

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {

		ibValueMetaObjectAttributeBase::ibSQLField sqlDimension = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);

		sqlQuery += (firstSelect ? "" : ",") + sqlDimension.m_fieldTypeName;

		for (auto dataType : sqlDimension.m_types) {
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

	for (const auto object : m_metaObject->GetResourceArrayObject()) {

		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName;
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Turnover_";

		if (m_metaObject->GetRegisterType() == ibRegisterType::eBalances) {
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Receipt_";
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Expense_";
		}

		firstSelect = false;
	}

	sqlQuery += " FROM ( SELECT "; firstSelect = true;

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += (firstSelect ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		firstSelect = false;
	}

	ibValueMetaObjectAttributeBase::ibSQLField sqlColRecordType =
		ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

	for (const auto object : m_metaObject->GetResourceArrayObject()) {

		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName + ", ";

		if (m_metaObject->GetRegisterType() == ibRegisterType::eBalances) {
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

	sqlQuery += ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

	sqlQuery += " GROUP BY "; bool firstGroupBy = true;

	for (const auto object : m_metaObject->GetDimensionArrayObject()) {
		sqlQuery += (firstGroupBy ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		firstGroupBy = false;
	}

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		sqlQuery += (firstGroupBy ? "" : ",") + sqlResource.m_fieldTypeName;
		firstGroupBy = false;
	}

	sqlQuery += " HAVING "; bool firstHaving = true;

	for (const auto object : m_metaObject->GetResourceArrayObject()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		ibValueMetaObjectAttributeBase::ibSQLField sqlResource = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
		if (m_metaObject->GetRegisterType() == ibRegisterType::eBalances) {
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

	ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

	if (statement == nullptr)
		return retTable;

	int position = 1;

	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position);
	ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position);

	for (auto filter : selFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {

		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValue retValue;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
				retLine->SetAt(object->GetName(), retValue);
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValue retValue;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_Turnover_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValue, resultSet))
				retLine->SetAt(object->GetName() + "_Turnover", retValue);
			if (m_metaObject->GetRegisterType() == ibRegisterType::eBalances) {
				ibValue retValue1;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_Receipt_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValue1, resultSet))
					retLine->SetAt(object->GetName() + "_Receipt", retValue1);
				ibValue retValue2;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object->GetFieldNameDB() + "_N_Expense_", ibValueMetaObjectAttributeBase::ibFieldTypes_Number, object, retValue2, resultSet))
					retLine->SetAt(object->GetName() + "_Expense", retValue2);
			}
		}
		wxDELETE(retLine);
	}

	ses_query->CloseResultSet(resultSet);
	ses_query->CloseStatement(statement);

	return retTable;
}
