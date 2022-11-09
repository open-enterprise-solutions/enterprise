////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumlation manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

#include "compiler/valueMap.h"
#include "compiler/valueTable.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h"

CValue CAccumulationRegisterManager::Balance(const CValue& cPeriod, const CValue& cFilter)
{
	CValueTable* retTable = new CValueTable();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto dimention : m_metaObject->GetObjectDimensions()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				dimention->GetName(),
				dimention->GetValueTypeDescription(),
				dimention->GetSynonym()
			);
	}

	for (auto resource : m_metaObject->GetObjectResources()) {
		colCollection->AddColumn(
			resource->GetName() + "_Balance",
			resource->GetValueTypeDescription(),
			resource->GetSynonym() + " " + _("Balance")
		);
	}

	if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {

		CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(dimension->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						dimension, vSelValue
					);
				}
			}
		}

		IMetaAttributeObject::sqlField_t sqlColPeriod =
			IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT "; bool firstSelect = true;

		for (auto dimension : m_metaObject->GetObjectDimensions()) {

			IMetaAttributeObject::sqlField_t sqlDimension = IMetaAttributeObject::GetSQLFieldData(dimension);

			sqlQuery += (firstSelect ? "" : ",") + sqlDimension.m_fieldTypeName;

			for (auto dataType : sqlDimension.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += "," + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}

			firstSelect = false;
		}

		for (auto resource : m_metaObject->GetObjectResources()) {

			IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName;
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
			firstSelect = false;
		}

		sqlQuery += " FROM ( SELECT "; firstSelect = true;

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += (firstSelect ? "" : ",") + IMetaAttributeObject::GetSQLFieldName(dimension);
			firstSelect = false;
		}

		IMetaAttributeObject::sqlField_t sqlColRecordType =
			IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

		for (auto resource : m_metaObject->GetObjectResources()) {

			IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName + ", ";
			sqlQuery += " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN - " + sqlResource.m_types[0].m_field.m_fieldName + " "
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) AS " + sqlResource.m_types[0].m_field.m_fieldName + "_Balance_";
			firstSelect = false;
		}

		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE ";

		sqlQuery += IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

		sqlQuery += " GROUP BY "; bool firstGroupBy = true;

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += (firstGroupBy ? "" : ",") + IMetaAttributeObject::GetSQLFieldName(dimension);
			firstGroupBy = false;
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += (firstGroupBy ? "" : ",") + sqlResource.m_fieldTypeName;
			firstGroupBy = false;
		}

		sqlQuery += " HAVING "; bool firstHaving = true;

		for (auto resource : m_metaObject->GetObjectResources()) {
			wxString orCase = (!firstHaving ? "OR (" : "");
			wxString orCaseEnd = (!firstHaving ? ")" : "");
			IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += orCase + " CAST(SUM(CASE WHEN " + sqlColRecordType.m_types[0].m_field.m_fieldName + " = 0.0"
				" THEN - " + sqlResource.m_types[0].m_field.m_fieldName + ""
				" ELSE   " + sqlResource.m_types[0].m_field.m_fieldName + " END"
				" ) AS NUMERIC) " + orCaseEnd + " <> 0.0";

			firstHaving = false;
		}

		sqlQuery += ") AS T1";

		PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery);

		if (statement == NULL)
			return retTable;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		DatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == NULL)
			return retTable;

		while (resultSet->Next()) {

			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				retLine->SetAt(dimension->GetName(),
					IMetaAttributeObject::GetValueAttribute(dimension, resultSet)
				);
			}

			for (auto resource : m_metaObject->GetObjectResources()) {
				retLine->SetAt(resource->GetName() + "_Balance",
					IMetaAttributeObject::GetValueAttribute(resource->GetFieldNameDB() + "_N_Balance_",
						IMetaAttributeObject::eFieldTypes_Number,
						resource, resultSet
					)
				);
			}
			delete retLine;
		}

		databaseLayer->CloseResultSet(resultSet);
		databaseLayer->CloseStatement(statement);
	}

	return retTable;
}

CValue CAccumulationRegisterManager::Turnovers(const CValue& cBeginOfPeriod, const CValue& cEndOfPeriod, const CValue& cFilter)
{
	CValueTable* retTable = new CValueTable();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto dimention : m_metaObject->GetObjectDimensions()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				dimention->GetName(),
				dimention->GetValueTypeDescription(),
				dimention->GetSynonym()
			);;
	}
	for (auto resource : m_metaObject->GetObjectResources()) {
		colCollection->AddColumn(
			resource->GetName() + "_Turnover",
			resource->GetValueTypeDescription(),
			resource->GetSynonym() + " " + _("Turnover")
		);
		if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
			colCollection->AddColumn(
				resource->GetName() + "_Receipt",
				resource->GetValueTypeDescription(),
				resource->GetSynonym() + " " + _("Receipt")
			);
			colCollection->AddColumn(
				resource->GetName() + "_Expense",
				resource->GetValueTypeDescription(),
				resource->GetSynonym() + " " + _("Expense")
			);
		}
	}

	CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
	if (cFilter.ConvertToValue(valFilter)) {
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			CValue vSelValue;
			if (valFilter->Property(dimension->GetName(), vSelValue)) {
				selFilter.insert_or_assign(
					dimension, vSelValue
				);
			}
		}
	}

	IMetaAttributeObject::sqlField_t sqlColPeriod =
		IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

	wxString sqlQuery = " SELECT "; bool firstSelect = true;

	for (auto dimension : m_metaObject->GetObjectDimensions()) {

		IMetaAttributeObject::sqlField_t sqlDimension = IMetaAttributeObject::GetSQLFieldData(dimension);

		sqlQuery += (firstSelect ? "" : ",") + sqlDimension.m_fieldTypeName;

		for (auto dataType : sqlDimension.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += "," + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += "," + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		firstSelect = false;
	}

	for (auto resource : m_metaObject->GetObjectResources()) {

		IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
		sqlQuery += (firstSelect ? "" : ",") + sqlResource.m_fieldTypeName;
		sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Turnover_";

		if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Receipt_";
			sqlQuery += "," + sqlResource.m_types[0].m_field.m_fieldName + "_Expense_";
		}

		firstSelect = false;
	}

	sqlQuery += " FROM ( SELECT "; firstSelect = true;

	for (auto dimension : m_metaObject->GetObjectDimensions()) {
		sqlQuery += (firstSelect ? "" : ",") + IMetaAttributeObject::GetSQLFieldName(dimension);
		firstSelect = false;
	}

	IMetaAttributeObject::sqlField_t sqlColRecordType =
		IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterRecordType());

	for (auto resource : m_metaObject->GetObjectResources()) {

		IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
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

	sqlQuery += IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
	sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
	sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");

	sqlQuery += " GROUP BY "; bool firstGroupBy = true;

	for (auto dimension : m_metaObject->GetObjectDimensions()) {
		sqlQuery += (firstGroupBy ? "" : ",") + IMetaAttributeObject::GetSQLFieldName(dimension);
		firstGroupBy = false;
	}

	for (auto resource : m_metaObject->GetObjectResources()) {
		IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
		sqlQuery += (firstGroupBy ? "" : ",") + sqlResource.m_fieldTypeName;
		firstGroupBy = false;
	}

	sqlQuery += " HAVING "; bool firstHaving = true;

	for (auto resource : m_metaObject->GetObjectResources()) {
		wxString orCase = (!firstHaving ? "OR (" : "");
		wxString orCaseEnd = (!firstHaving ? ")" : "");
		IMetaAttributeObject::sqlField_t sqlResource = IMetaAttributeObject::GetSQLFieldData(resource);
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

	PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery);

	if (statement == NULL)
		return retTable;

	int position = 1;

	IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position); //active = true
	IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cBeginOfPeriod.GetDate(), statement, position);
	IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cEndOfPeriod.GetDate(), statement, position);

	for (auto filter : selFilter) {
		IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == NULL)
		return retTable;

	while (resultSet->Next()) {

		CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			retLine->SetAt(dimension->GetName(),
				IMetaAttributeObject::GetValueAttribute(dimension, resultSet)
			);
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			retLine->SetAt(resource->GetName() + "_Turnover",
				IMetaAttributeObject::GetValueAttribute(resource->GetFieldNameDB() + "_N_Turnover_",
					IMetaAttributeObject::eFieldTypes_Number,
					resource, resultSet
				)
			);

			if (m_metaObject->GetRegisterType() == eRegisterType::eBalances) {
				retLine->SetAt(resource->GetName() + "_Receipt",
					IMetaAttributeObject::GetValueAttribute(resource->GetFieldNameDB() + "_N_Receipt_",
						IMetaAttributeObject::eFieldTypes_Number,
						resource, resultSet
					)
				);
				retLine->SetAt(resource->GetName() + "_Expense",
					IMetaAttributeObject::GetValueAttribute(resource->GetFieldNameDB() + "_N_Expense_",
						IMetaAttributeObject::eFieldTypes_Number,
						resource, resultSet
					)
				);
			}
		}
		delete retLine;
	}

	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);

	return retTable;
}
